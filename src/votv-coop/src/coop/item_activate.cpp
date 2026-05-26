// coop/item_activate.cpp -- Phase 5F flashlight (and future item-activation)
// sync. See coop/item_activate.h for the public interface and the RE doc
// at research/findings/votv-flashlight-RE-2026-05-25.md for the full
// rationale (Case-b verdict, save-persistence gap, etc).
//
// Implementation shape mirrors coop/grab_observer.cpp:
//   1) Install() resolves mainPlayer_C + updateFlashlight + caches a
//      per-class hash. Idempotent + retried every NetPumpTick.
//   2) The POST observer is the SENDER path. It reads mp.flashlight
//      AFTER the BP function ran (so the bool reflects the new state)
//      and pushes an ItemActivatePayload onto the reliable channel.
//   3) ApplyToPuppet() is the RECEIVER path -- invoked from event_feed
//      ::Update's drain loop with the puppet's actor pointer.

#include "coop/item_activate.h"

#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "dev/common.h"
#include "ue_wrap/call.h"
#include "ue_wrap/engine.h"
#include "ue_wrap/game_thread.h"
#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"
#include "ue_wrap/sdk_profile.h"
#include "ue_wrap/types.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace coop::item_activate {
namespace {

namespace P = ue_wrap::profile;
namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;
namespace GT = ue_wrap::game_thread;

bool g_installed = false;

// Resolved once at Install: pointers to BOTH candidate UFunctions in the
// flashlight call chain. Hands-on 2026-05-25 NIGHT-3 found that
// `updateFlashlight` is BP-INLINED into `Flashlight Update` and never
// reaches ProcessEvent -- so we register a POST observer on both, and
// whichever the BP compiler actually dispatches wins. If both fire in
// the same press (which the inlining argument says won't happen), the
// receiver tolerates the duplicate (writing the same bool twice = no-op
// + at human-press cadence the doubled wire traffic is negligible).
void* g_updateFlashlightFn = nullptr;
void* g_flashlightUpdateFn = nullptr;
void* g_flashlightInput13Fn = nullptr;
void* g_flashlightInput14Fn = nullptr;

// Last sent state so the input-event hook doesn't send a packet for
// the F-RELEASE event (which fires the InpActEvt_14 UFunction but
// doesn't change `mp.flashlight`). One value per local process is
// enough; the sender is always the local player. -1 = no send yet.
std::atomic<int> g_lastSentState{-1};

// Latched "on" intensity. Sampled from the LOCAL player's light_R the
// first time we observe it greater than the off-state default (which
// is 0.2 in Unitless mode -- VOTV's flashlight). The receiver uses
// this to drive the puppet's light_R intensity.
//
// CRITICAL: VOTV's light_R uses ELightUnits::Unitless (offset 0x0328
// on ULocalLightComponent). In that mode Intensity is a small
// multiplier (we see 0.2 in default state; real "on" is roughly
// 5-10). Stock UE4.27 default of 5000 lumens DOES NOT APPLY -- a
// 5000 Unitless multiplier is wildly different. The g_intensityOnFallback
// is set to a safe Unitless value used only until the latch sees a
// real on value.
//
// 0 = not yet sampled. Cross-peer assumption: the "on" intensity is
// the same on both peers because both run the same VOTV BP defaults
// (the latch will reach the SAME value on each peer once each peer's
// user toggles their own flashlight even once).
std::atomic<float> g_latchedOnIntensity{0.f};
inline constexpr float kIntensityOnFallback = 5.f;  // Unitless safe default
inline constexpr float kIntensityOffDefault = 0.f;  // turn light fully off

// Hash of "prop_equipment_flashlight_C" -- the class the wire packet's
// itemClassHash field carries for flashlight events. Both _a and _b
// variants use this same hash (they're identical from the world-effect
// perspective; the puppet's light_R toggles either way). Resolved at
// Install + then constant.
uint32_t g_flashlightClassHash = 0;

// Session pointer (atomic so the observer's BG read can't race with
// a setter on another thread -- same pattern as dev::teleport_client).
std::atomic<coop::net::Session*> g_session{nullptr};

// Echo-suppression: when we APPLY a remote flashlight state to the
// puppet, we don't directly invoke updateFlashlight (which would
// re-dispatch and re-broadcast); we write the bool + toggle the
// component directly. But future generalizations might invoke a
// UFunction on the puppet, so the flag is here pre-emptively. Set
// before invoking, cleared after. The observer checks it and skips
// the send. Atomic for the same reason as g_session above.
std::atomic<bool> g_echoSuppress{false};

bool ProbeLogEnabled() {
    // Read once; ini parsing is cheap but the observer is hot. Static
    // initialization means we resolve this ONCE per process lifetime,
    // which is acceptable for a dev-only flag (restart to flip it).
    static const bool s_enabled = ::dev::IsIniKeyTrue("flashlight_log");
    return s_enabled;
}

void OnUpdateFlashlightPost(void* self, void* function, void* /*params*/) {
    if (!self) return;
    // Identify which of the two candidate hooks fired -- diagnostic in
    // probe mode so we can prove which BP entry actually dispatches.
    const char* which =
        (function == g_flashlightUpdateFn) ? "Flashlight_Update" :
        (function == g_updateFlashlightFn) ? "updateFlashlight" :
        (function == g_flashlightInput13Fn) ? "InpActEvt_13" :
        (function == g_flashlightInput14Fn) ? "InpActEvt_14" :
        "<unknown>";

    // Echo-suppress: a receiver-applied state change going through any
    // future path that invokes updateFlashlight on the puppet should
    // NOT bounce back as a wire packet.
    if (g_echoSuppress.load(std::memory_order_acquire)) return;

    // Skip puppets entirely. If updateFlashlight ever fires on an
    // orphan puppet (it shouldn't -- puppets have no controller, no
    // input bindings), we'd send the puppet's spurious state back to
    // the very peer that authored it. GetController()!=nullptr is
    // the local-vs-puppet discriminator per CLAUDE.md.
    if (!E::GetController(self)) return;

    // Read state AFTER the BP function ran -- the bool now reflects
    // the new on/off value.
    const bool flashlight = *reinterpret_cast<bool*>(
        reinterpret_cast<uint8_t*>(self) + P::off::AmainPlayer_flashlight);
    const bool hasFlashlight = *reinterpret_cast<bool*>(
        reinterpret_cast<uint8_t*>(self) + P::off::AmainPlayer_hasFlashlight);
    const bool crankFlashlight = *reinterpret_cast<bool*>(
        reinterpret_cast<uint8_t*>(self) + P::off::AmainPlayer_crankFlashlight);

    if (ProbeLogEnabled()) {
        // [probe] flashlight_log=1 hands-on verification: see whether
        // the BP early-returns when !hasFlashlight (the bool should
        // not change in that case) and confirm light_R visibility is
        // in lockstep with the bool. The crank-flashlight (_c) path
        // is logged separately because we currently skip the wire
        // send for it (see below).
        void* light_R = *reinterpret_cast<void**>(
            reinterpret_cast<uint8_t*>(self) + P::off::AmainPlayer_light_R);
        bool lightVisible = false;
        float lightIntensity = -1.f;
        if (light_R && R::IsLive(light_R)) {
            const uint8_t flagsByte = *reinterpret_cast<uint8_t*>(
                reinterpret_cast<uint8_t*>(light_R) + P::off::USceneComponent_VisFlagsByte);
            lightVisible = (flagsByte & 0x10) != 0;
            lightIntensity = *reinterpret_cast<float*>(
                reinterpret_cast<uint8_t*>(light_R) + P::off::ULightComponentBase_Intensity);
        }
        UE_LOGI("flashlight[POST %s] self=%p flashlight=%d hasFL=%d crankFL=%d "
                "light_R=%p bVisible=%d Intensity=%.1f", which, self,
                flashlight ? 1 : 0, hasFlashlight ? 1 : 0,
                crankFlashlight ? 1 : 0, light_R, lightVisible ? 1 : 0,
                lightIntensity);
    }

    // Defer the _c crank lantern variant (its own light components on the
    // item actor; the player's light_R is NOT what toggles for it) --
    // covered by Inc6 of the 5F plan.
    if (crankFlashlight) {
        if (ProbeLogEnabled()) {
            UE_LOGI("flashlight: crank lantern (_c) -- wire send deferred to Inc6");
        }
        return;
    }

    // Don't broadcast if the BP guard early-returned because no
    // flashlight is equipped. Without this guard a future re-cook
    // could have updateFlashlight() always set flashlight=true on
    // F-press and we'd happily ship spurious activations.
    if (!hasFlashlight) {
        if (ProbeLogEnabled()) {
            UE_LOGI("flashlight: hasFlashlight=false -- BP early-returned, no wire send");
        }
        return;
    }

    // Latch the local "on" intensity the first time we see it clearly
    // above the off-state value. VOTV's flashlight Unitless default in
    // off state is ~0.2 (not 0 -- a sentinel "barely visible" value).
    // The on state is much higher (Unitless scale, roughly 5-10 per
    // Agent 1's research). Require >1.0 to filter out the off-state
    // sentinel.
    if (flashlight) {
        void* light_R = *reinterpret_cast<void**>(
            reinterpret_cast<uint8_t*>(self) + P::off::AmainPlayer_light_R);
        if (light_R && R::IsLive(light_R)) {
            const float intensity = *reinterpret_cast<float*>(
                reinterpret_cast<uint8_t*>(light_R) + P::off::ULightComponentBase_Intensity);
            if (intensity > 1.f && g_latchedOnIntensity.load(std::memory_order_acquire) == 0.f) {
                g_latchedOnIntensity.store(intensity, std::memory_order_release);
                UE_LOGI("flashlight: latched local 'on' intensity = %.2f (will drive puppet)", intensity);
            }
        }
    }

    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected()) return;

    // Dedup: if InpActEvt_14 (release) fires after InpActEvt_13 (press),
    // mp.flashlight didn't change between them -- last-sent-state catches
    // that and skips the redundant send. Also catches a future BP recook
    // where some other handler fires repeatedly with the same state.
    const int newState = flashlight ? 1 : 0;
    if (g_lastSentState.exchange(newState, std::memory_order_acq_rel) == newState) {
        if (ProbeLogEnabled()) {
            UE_LOGI("flashlight: state unchanged (still %d) -- no wire send", newState);
        }
        return;
    }

    coop::net::ItemActivatePayload p{};
    p.itemClassHash = g_flashlightClassHash;
    // 1v1 session: host=0, client=1. Once we scale to N peers, this gets
    // replaced with a real session id allocated by the host.
    p.peerSessionId = (s->role() == coop::net::Role::Host) ? 0 : 1;
    p.state = flashlight ? 1 : 0;
    p.flags = 0;          // Case (b) -- no actor key
    p._pad = 0;
    p.actorKeyHash = 0;
    // p.paramBlob stays zero.

    const bool sent = s->SendReliable(coop::net::ReliableKind::ItemActivate, &p, sizeof(p));
    if (!sent) {
        UE_LOGW("flashlight: SendReliable failed (channel busy or not connected)");
    } else {
        UE_LOGI("flashlight: sent state=%d (peer=%u)", p.state, p.peerSessionId);
    }
}

}  // namespace

bool DebugForceToggle(void* mp) {
    if (!mp) return false;

    // Field flip MUST run on the game thread (UObject memory). The
    // wire-send retry that follows can sleep, so it CANNOT run on the
    // game thread or it would block the ack pump and deadlock the
    // reliable channel. So: GT::Post the flip, wait for it, then
    // retry the send from THIS (worker) thread.
    //
    // The autotest already calls DebugForceToggle from its worker
    // thread (NOT inside a GT::Post), so this layering works.
    auto done = std::make_shared<std::atomic<int>>(0);
    auto newStateOut = std::make_shared<std::atomic<bool>>(false);
    GT::Post([mp, done, newStateOut] {
        bool* fl = reinterpret_cast<bool*>(
            reinterpret_cast<uint8_t*>(mp) + P::off::AmainPlayer_flashlight);
        const bool newState = !(*fl);
        *fl = newState;
        newStateOut->store(newState, std::memory_order_release);
        UE_LOGI("flashlight: DebugForceToggle wrote flashlight=%d on %p (LAN test path)",
                newState ? 1 : 0, mp);
        done->store(1, std::memory_order_release);
    });
    while (done->load(std::memory_order_acquire) == 0) ::Sleep(1);
    const bool newState = newStateOut->load(std::memory_order_acquire);

    // The reliable channel is stop-and-wait (max 1 in-flight). The HOST
    // has continuous PropSpawn / EntitySpawn traffic from Phase 5S0
    // continuous-spawn observers, so any SendReliable can land while
    // the channel is busy and return false. The autonomous test can't
    // tolerate that -- we need the packet to fly. Retry up to 200x
    // with 25 ms backoff (5 s max wait per toggle) -- on LAN ack RTT
    // is ~1-2 ms so a free slot opens within a few tries, but if the
    // host's PropSpawn snapshot is mid-burst we may need longer.
    //
    // Bypass the dedup (last-sent-state) for retry purposes: directly
    // call SendReliable here with the exact same payload shape the
    // observer would have sent. The receiver path is identical.
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected()) {
        UE_LOGW("flashlight: DebugForceToggle session not connected -- skipping wire send");
        return newState;
    }

    coop::net::ItemActivatePayload p{};
    p.itemClassHash = g_flashlightClassHash;
    p.peerSessionId = (s->role() == coop::net::Role::Host) ? 0 : 1;
    p.state = newState ? 1 : 0;
    p.flags = 0;
    p._pad = 0;
    p.actorKeyHash = 0;

    bool sent = false;
    int attempts = 0;
    for (; attempts < 200; ++attempts) {
        sent = s->SendReliable(coop::net::ReliableKind::ItemActivate, &p, sizeof(p));
        if (sent) break;
        ::Sleep(25);
    }
    if (sent) {
        // Update the dedup tracker so the regular observer path (if any
        // BP-driven fire happens later) doesn't re-send the same value.
        g_lastSentState.store(p.state, std::memory_order_release);
        UE_LOGI("flashlight: DebugForceToggle sent state=%d (peer=%u, after %d retr%s)",
                p.state, p.peerSessionId, attempts,
                attempts == 1 ? "y" : "ies");
    } else {
        UE_LOGW("flashlight: DebugForceToggle FAILED to send after 200 retries -- "
                "reliable channel never freed (state=%d)", p.state);
    }
    return newState;
}

uint32_t HashClassName(const wchar_t* s) {
    // FNV-1a 32-bit. Operates byte-by-byte on the UTF-16 bytes (each
    // wchar_t = 2 bytes on Windows). Cross-peer stable because the
    // string is the same UTF-16 encoding everywhere.
    uint32_t h = 0x811c9dc5u;
    if (!s) return h;
    for (; *s; ++s) {
        const wchar_t w = *s;
        h ^= static_cast<uint8_t>(w & 0xFF);
        h *= 0x01000193u;
        h ^= static_cast<uint8_t>((w >> 8) & 0xFF);
        h *= 0x01000193u;
    }
    return h;
}

void Install(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
    if (g_installed) return;

    void* playerCls = R::FindClass(P::name::MainPlayerClass);
    if (!playerCls) {
        // mainPlayer_C BP not yet loaded (still in OMEGA / menu) --
        // retry on the next NetPumpTick. Match grab_observer's
        // wait-and-retry shape.
        return;
    }

    // Register POST observers on BOTH candidate UFunctions. The BP graph
    // dispatch path is:
    //   InpActEvt_flashlight_...  -> "Flashlight Update"  -> updateFlashlight
    // Hands-on 2026-05-25 NIGHT-3 showed that only the OUTER function is
    // actually ProcessEvent-dispatched; the inner one is BP-inlined into
    // it and never fires the observer. Registering both is cheap (2 of
    // the 64 observer slots) and survives a future BP recook that might
    // un-inline one or the other.
    // Try all four candidate UFunctions. The dispatch chain per the RE
    // doc is:
    //   InpActEvt_flashlight_K2Node_InputActionEvent_13/14
    //     -> Flashlight Update()
    //        -> updateFlashlight()
    // Hands-on showed both inner functions are BP-inlined into the input
    // events. We hook the input events too -- they are ProcessEvent-
    // dispatched by the engine input system (same as grab_observer's
    // InpActEvt_use, which we already know fires). last-sent-state
    // dedups duplicates if more than one fires for a single F-press.
    struct Candidate { const wchar_t* name; void** outPtr; };
    Candidate cs[] = {
        { P::name::MainPlayerUpdateFlashlightFn,  &g_updateFlashlightFn  },
        { P::name::MainPlayerFlashlightUpdateFn,  &g_flashlightUpdateFn  },
        { P::name::MainPlayerFlashlightInput13Fn, &g_flashlightInput13Fn },
        { P::name::MainPlayerFlashlightInput14Fn, &g_flashlightInput14Fn },
    };
    int registered = 0;
    for (auto& c : cs) {
        void* fn = R::FindFunction(playerCls, c.name);
        if (!fn) {
            UE_LOGW("flashlight: UFunction '%ls' not found on %ls", c.name,
                    P::name::MainPlayerClass);
            continue;
        }
        if (!GT::RegisterPostObserver(fn, &OnUpdateFlashlightPost)) {
            UE_LOGW("flashlight: RegisterPostObserver(%ls) failed", c.name);
            continue;
        }
        *c.outPtr = fn;
        ++registered;
    }
    if (registered == 0) {
        UE_LOGW("flashlight: no candidate observers registered -- aborting install");
        return;
    }

    g_flashlightClassHash = HashClassName(L"prop_equipment_flashlight_C");
    g_installed = true;
    UE_LOGI("flashlight: %d POST observer(s) installed (updateFlashlight=%p, "
            "'Flashlight Update'=%p, InpActEvt_13=%p, InpActEvt_14=%p, "
            "classHash=0x%08X, probe_log=%d)",
            registered, g_updateFlashlightFn, g_flashlightUpdateFn,
            g_flashlightInput13Fn, g_flashlightInput14Fn,
            g_flashlightClassHash, ProbeLogEnabled() ? 1 : 0);
}

void ApplyToPuppet(void* puppetActor, uint32_t classHash, uint8_t state) {
    if (!puppetActor || !R::IsLive(puppetActor)) {
        UE_LOGW("flashlight: ApplyToPuppet called with invalid puppet=%p", puppetActor);
        return;
    }
    if (classHash != g_flashlightClassHash) {
        // Unknown item class -- could be a future radio/torch entry we
        // don't handle yet. Surface it as a warning rather than silently
        // dropping.
        UE_LOGW("flashlight: ApplyToPuppet classHash mismatch (got 0x%08X, "
                "expected 0x%08X = flashlight) -- dropping", classHash,
                g_flashlightClassHash);
        return;
    }

    // Write the canonical state bool first so any code that reads
    // flashlight @0x0838 sees the same value before light_R is touched.
    bool* flashlightBool = reinterpret_cast<bool*>(
        reinterpret_cast<uint8_t*>(puppetActor) + P::off::AmainPlayer_flashlight);
    const bool newState = (state != 0);
    *flashlightBool = newState;

    // Toggle the puppet's spot light. Echo-suppress around the call --
    // SetComponentVisible internally uses reflection UFunctions
    // (SetVisibility / SetHiddenInGame) which DO dispatch through
    // ProcessEvent. None of those is updateFlashlight so the observer
    // wouldn't fire anyway, but the suppress flag is cheap insurance
    // for future code that calls the higher-level path.
    void* light_R = *reinterpret_cast<void**>(
        reinterpret_cast<uint8_t*>(puppetActor) + P::off::AmainPlayer_light_R);
    if (!light_R || !R::IsLive(light_R)) {
        UE_LOGW("flashlight: puppet light_R missing or dead -- bool written, "
                "light not toggled (puppet=%p light=%p)", puppetActor, light_R);
        return;
    }
    g_echoSuppress.store(true, std::memory_order_release);
    // The ONLY thing the receiver mutates is light_R.Intensity. VOTV's
    // BP also gates by intensity (the local probe proved bVisible stays
    // 0 even when the flashlight visibly toggles), so visibility-flag
    // toggles are a distractor here. The cascading parent-visibility
    // problem (lag_fl + light_R inheriting hidden state from the CDO)
    // is fixed ONCE at puppet spawn in puppet.cpp::SpawnPuppetMainPlayer
    // via SetComponentVisible(lag_fl + light_R, true, false). After that,
    // per-toggle just needs to drive Intensity through SetIntensity
    // (which internally MarkRenderStateDirty's the light proxy).
    float targetIntensity = kIntensityOffDefault;
    if (newState) {
        targetIntensity = g_latchedOnIntensity.load(std::memory_order_acquire);
        if (targetIntensity == 0.f) {
            // The local player hasn't toggled their flashlight on yet,
            // so we don't have the BP's real "on" value latched. Use
            // a Unitless-safe fallback. The latch updates as soon as
            // either peer toggles their real flashlight; subsequent
            // applies will use the real value on each receiver.
            targetIntensity = kIntensityOnFallback;
            UE_LOGW("flashlight: no latched intensity yet -- using Unitless fallback %.2f",
                    targetIntensity);
        }
    }
    // Resolve ULightComponent::SetIntensity once. SetIntensity is declared
    // on ULightComponent (the PARENT of USpotLightComponent), not on
    // USpotLightComponent itself -- ue_wrap::reflection::FindFunction does
    // NOT walk the inheritance chain (it only checks the immediate Outer),
    // so we have to look up the parent class explicitly. Lookup is by
    // string "LightComponent" (UClass name, no "U" prefix per UE4 reflection).
    // SetIntensity internally calls MarkRenderStateDirty on the proxy --
    // direct field writes do NOT, so the renderer wouldn't pick up the
    // change. Never fall back to direct write.
    static void* sSetIntensityFn = nullptr;
    if (!sSetIntensityFn) {
        void* lightCls = R::FindClass(L"LightComponent");
        if (lightCls) sSetIntensityFn = R::FindFunction(lightCls, P::name::SetIntensityFn);
        if (!sSetIntensityFn) {
            UE_LOGW("flashlight: failed to resolve SetIntensity on LightComponent class "
                    "(lightCls=%p) -- intensity writes will no-op", lightCls);
        }
    }
    bool intOk = false;
    if (sSetIntensityFn) {
        ue_wrap::ParamFrame f(sSetIntensityFn);
        f.Set<float>(L"NewIntensity", targetIntensity);
        intOk = ue_wrap::Call(light_R, f);
    } else {
        UE_LOGW("flashlight: SetIntensity UFunction unresolved on light_R class "
                "(this should never happen on a stock UE4.27 USpotLightComponent)");
    }
    g_echoSuppress.store(false, std::memory_order_release);
    UE_LOGI("flashlight: applied to puppet=%p state=%d Intensity=%.2f (intOk=%d, latched=%.2f)",
            puppetActor, newState ? 1 : 0, targetIntensity, intOk ? 1 : 0,
            g_latchedOnIntensity.load(std::memory_order_acquire));

    // Diagnostic readback: confirm the puppet's light_R actually has the
    // fields we expect AFTER our writes. If Intensity reads back as the
    // target but bAffectsWorld is 0 or visByte bit-4 is 0, the light is
    // STILL not rendering. Also dump the LOCAL player's light_R so we
    // can diff puppet vs local (the local works visually, the puppet
    // doesn't -- the gating field must differ between them).
    const float puppetIntensityAfter = *reinterpret_cast<float*>(
        reinterpret_cast<uint8_t*>(light_R) + P::off::ULightComponentBase_Intensity);
    const uint8_t puppetFlagsByte = *reinterpret_cast<uint8_t*>(
        reinterpret_cast<uint8_t*>(light_R) + 0x0214);  // bAffectsWorld + cast flags packed
    const uint8_t puppetVisByte = *reinterpret_cast<uint8_t*>(
        reinterpret_cast<uint8_t*>(light_R) + P::off::USceneComponent_VisFlagsByte);
    UE_LOGI("flashlight[POST-APPLY puppet]: light_R=%p Intensity=%.1f flags@0x214=0x%02X "
            "visByte@0x14C=0x%02X (bAffectsWorld=bit0, bVisible=bit4)",
            light_R, puppetIntensityAfter, puppetFlagsByte, puppetVisByte);
    // Local player's light_R, for comparison.
    void* localPlayer = R::FindObjectByClass(P::name::MainPlayerClass);
    if (localPlayer && localPlayer != puppetActor) {
        // GUObjectArray walk may return the puppet on its first hit; skip
        // if equal. (Find the OTHER one if needed via E::GetController.)
        if (!E::GetController(localPlayer)) {
            // Got the puppet -- walk for the real local.
            const int32_t n = R::NumObjects();
            for (int32_t i = 0; i < n; ++i) {
                void* obj = R::ObjectAt(i);
                if (!obj || obj == puppetActor) continue;
                if (R::ClassNameOf(obj) != P::name::MainPlayerClass) continue;
                if (!R::IsLive(obj)) continue;
                if (!E::GetController(obj)) continue;
                localPlayer = obj;
                break;
            }
        }
    }
    if (localPlayer && localPlayer != puppetActor) {
        void* localLightR = *reinterpret_cast<void**>(
            reinterpret_cast<uint8_t*>(localPlayer) + P::off::AmainPlayer_light_R);
        if (localLightR && R::IsLive(localLightR)) {
            const float li = *reinterpret_cast<float*>(
                reinterpret_cast<uint8_t*>(localLightR) + P::off::ULightComponentBase_Intensity);
            const uint8_t lf = *reinterpret_cast<uint8_t*>(
                reinterpret_cast<uint8_t*>(localLightR) + 0x0214);
            const uint8_t lv = *reinterpret_cast<uint8_t*>(
                reinterpret_cast<uint8_t*>(localLightR) + P::off::USceneComponent_VisFlagsByte);
            UE_LOGI("flashlight[POST-APPLY local-cmp]: localLight_R=%p Intensity=%.1f "
                    "flags@0x214=0x%02X visByte@0x14C=0x%02X",
                    localLightR, li, lf, lv);
        }
    }
}

}  // namespace coop::item_activate
