// coop/weather_sync.cpp -- Phase 5W weather sync. See coop/weather_sync.h.

#include "coop/weather_sync.h"

#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "ue_wrap/call.h"
#include "ue_wrap/game_thread.h"
#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"
#include "ue_wrap/sdk_profile.h"
#include "ue_wrap/types.h"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace coop::weather_sync {
namespace {

namespace P = ue_wrap::profile;
namespace R = ue_wrap::reflection;
namespace GT = ue_wrap::game_thread;

// Resolved-once UFunction pointers. Each is nullptr until Install resolves
// it; once non-null, stays non-null for the session lifetime.
void* g_timerRainFn       = nullptr;
void* g_timerLightningFn  = nullptr;
void* g_fogEventFn        = nullptr;
void* g_superFogEventFn   = nullptr;
void* g_permaRainTimerFn  = nullptr;

void* g_causeRainFn          = nullptr;
void* g_setRainPropertiesFn  = nullptr;
void* g_setWindParametersFn  = nullptr;
void* g_intComsTriggerSnowFn = nullptr;
void* g_spawnFogFn           = nullptr;
void* g_setFogDensityFn      = nullptr;

bool g_installed = false;
bool g_observersRegistered = false;     // host POST observers
bool g_interceptorsRegistered = false;  // client PRE interceptors

// Session pointer; atomic so the observer / interceptor reads can't race
// the harness setter on another thread (matches dev::teleport_client +
// item_activate pattern).
std::atomic<coop::net::Session*> g_session{nullptr};

// FNV1a dedup signature over the wire payload. kNoSendYet sentinel
// disambiguates "never sent" from "last sent had this hash". Single atomic
// so the load is uninterruptible (matches the post-audit item_activate v6
// shape -- a split (have_flag + sig) had a race window with worker-thread
// senders; single atomic eliminates it).
inline constexpr uint64_t kNoSendYet = 0xFFFFFFFFFFFFFFFFULL;
std::atomic<uint64_t> g_lastSentSig{kNoSendYet};

// Pending connect-edge broadcast (mirrors item_activate::g_pendingBroadcast).
// On the connect rising-edge the harness calls QueueConnectBroadcast which
// snapshots state into this slot; TickConnect retries SendReliable each tick
// until the reliable channel accepts.
bool g_pendingBroadcast = false;
coop::net::WeatherStatePayload g_pendingBroadcastPayload{};

// Pending receiver-side apply (mirrors item_activate::g_pendingApplyValid).
// A WeatherState packet can arrive on the client BEFORE Install resolves
// all UFunctions (e.g. the cycle is still loading) OR before the local
// cycle is live. In both cases ApplyFromHost stashes the latest payload
// here; TickConnect drains it once both gates pass. Latest-wins -- a
// newer packet overrides a still-pending older one. Game thread only.
bool g_pendingApply = false;
coop::net::WeatherStatePayload g_pendingApplyPayload{};

// Cached cycle pointer. The cycle is singleton-per-session; FindObjectByClass
// is a linear GUObjectArray walk and CLAUDE.md's audit rule prohibits per-
// frame full-array scans. Cache the pointer; revalidate on use via IsLive
// (the cycle is destroyed + recreated on level transitions OMEGA -> story,
// at which point we re-resolve). Game-thread only.
void* g_cycleCache = nullptr;

void* ResolveCycle() {
    if (g_cycleCache && R::IsLive(g_cycleCache)) return g_cycleCache;
    g_cycleCache = R::FindObjectByClass(P::name::DaynightCycleClass);
    return g_cycleCache;
}

bool ReadCycleState(void* cycle, coop::net::WeatherStatePayload& out) {
    if (!cycle || !R::IsLive(cycle)) return false;
    out = {};
    out.peerSessionId = 0;  // host stamps 0 (we're only called on host)

    const uint8_t* base = reinterpret_cast<uint8_t*>(cycle);
    const bool isRaining       = *reinterpret_cast<const bool*>(base + P::off::AdaynightCycle_isRaining);
    const bool isSnow          = *reinterpret_cast<const bool*>(base + P::off::AdaynightCycle_isSnow);
    const bool enable_rain     = *reinterpret_cast<const bool*>(base + P::off::AdaynightCycle_enable_rain);
    const bool enable_fog      = *reinterpret_cast<const bool*>(base + P::off::AdaynightCycle_enable_fog);
    const bool enable_superfog = *reinterpret_cast<const bool*>(base + P::off::AdaynightCycle_enable_superfog);
    const bool enableSunlight  = *reinterpret_cast<const bool*>(base + P::off::AdaynightCycle_enableSunlight);
    const bool enableMoonlight = *reinterpret_cast<const bool*>(base + P::off::AdaynightCycle_enableMoonlight);
    const bool permanentRain   = *reinterpret_cast<const bool*>(base + P::off::AdaynightCycle_permanentRain);
    out.flags = 0;
    if (isRaining)       out.flags |= coop::net::weather_flags::kIsRaining;
    if (isSnow)          out.flags |= coop::net::weather_flags::kIsSnow;
    if (enable_rain)     out.flags |= coop::net::weather_flags::kEnableRain;
    if (enable_fog)      out.flags |= coop::net::weather_flags::kEnableFog;
    if (enable_superfog) out.flags |= coop::net::weather_flags::kEnableSuperfog;
    if (enableSunlight)  out.flags |= coop::net::weather_flags::kEnableSunlight;
    if (enableMoonlight) out.flags |= coop::net::weather_flags::kEnableMoonlight;
    if (permanentRain)   out.flags |= coop::net::weather_flags::kPermanentRain;

    out.rainStrength         = *reinterpret_cast<const float*>(base + P::off::AdaynightCycle_rainStrength);
    out.rainLightningChance  = *reinterpret_cast<const float*>(base + P::off::AdaynightCycle_rainLightningChance);
    out.rainDeactivateChance = *reinterpret_cast<const float*>(base + P::off::AdaynightCycle_rainDeactivateChance);
    out.rainWindSpeed        = *reinterpret_cast<const float*>(base + P::off::AdaynightCycle_rainWindSpeed);
    return true;
}

uint64_t SignaturePayload(const coop::net::WeatherStatePayload& p) {
    // FNV1a over the mutable fields (flags + 4 floats). peerSessionId is
    // constant (= 0); _pad is constant (= 0). Same shape as item_activate's
    // SignaturePayload.
    uint64_t h = 0xcbf29ce484222325ULL;
    auto mix = [&](uint64_t v) { h ^= v; h *= 0x100000001b3ULL; };
    mix(p.flags);
    mix(reinterpret_cast<const uint32_t&>(p.rainStrength));
    mix(reinterpret_cast<const uint32_t&>(p.rainLightningChance));
    mix(reinterpret_cast<const uint32_t&>(p.rainDeactivateChance));
    mix(reinterpret_cast<const uint32_t&>(p.rainWindSpeed));
    return h;
}

// ---- HOST: POST observer broadcasts state -------------------------------

void OnSchedulerPost(void* self, void* /*function*/, void* /*params*/) {
    // The post-observer fires AFTER the scheduler ran on the host side --
    // state fields are now post-mutation. Read the cycle, dedup, broadcast.
    //
    // Defensive game-thread gate: the ProcessEvent detour can fire from
    // task-graph WORKER threads for some UFunctions (parallel anim).
    // Timer-driven UFunctions (our 5 scheduler targets) are dispatched by
    // FTimerManager which runs on the game thread only, so in practice
    // this gate never trips -- but a future BP rewrite or a different
    // game version could change that, and the cycle's fields are not
    // designed for cross-thread reads. Cheap check (TLS load).
    if (!GT::IsGameThread()) {
        UE_LOGW("weather: OnSchedulerPost fired off-game-thread -- skipping "
                "(weather UFunctions should be timer-dispatched on GT only)");
        return;
    }
    if (!self) return;
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected()) return;
    if (s->role() != coop::net::Role::Host) return;

    coop::net::WeatherStatePayload p{};
    if (!ReadCycleState(self, p)) return;

    const uint64_t sig = SignaturePayload(p);
    const uint64_t storeSig = (sig == kNoSendYet) ? (kNoSendYet - 1) : sig;
    if (g_lastSentSig.load(std::memory_order_acquire) == storeSig) return;

    const bool sent = s->SendReliable(
        coop::net::ReliableKind::WeatherState, &p, sizeof(p));
    if (!sent) {
        // Channel busy. Don't dedup-store -- next observer fire will retry.
        // (Unlike connect-replay there's no per-tick drain here; the
        // observer-driven path waits for the next scheduler fire, which
        // on a real weather change is rapid.)
        return;
    }
    g_lastSentSig.store(storeSig, std::memory_order_release);
    UE_LOGI("weather: host broadcast flags=0x%02X rain=%.2f lc=%.2f dc=%.2f ws=%.2f (sig=%llX)",
            p.flags, p.rainStrength, p.rainLightningChance,
            p.rainDeactivateChance, p.rainWindSpeed,
            static_cast<unsigned long long>(storeSig));
}

// ---- CLIENT: PRE interceptor suppresses local scheduler -----------------

bool OnSchedulerPreSuppress(void* /*self*/, void* /*params*/) {
    // Returning true skips the original UFunction body. Client only -- the
    // host's role-gate at install time means the host never registers this.
    // No filter on `self`: there's one cycle per session; all dispatches
    // on the 5 scheduler UFunctions are this cycle's. Logging this at INFO
    // would spam (each fires on a timer); silent suppression is correct.
    return true;
}

// ---- common installer ---------------------------------------------------

bool TryResolveAllFunctions() {
    void* cls = R::FindClass(P::name::DaynightCycleClass);
    if (!cls) return false;

    struct Entry { const wchar_t* name; void** out; };
    Entry sched[] = {
        { P::name::DaynightCycle_timerRainFn,       &g_timerRainFn       },
        { P::name::DaynightCycle_timerLightningFn,  &g_timerLightningFn  },
        { P::name::DaynightCycle_fogEventFn,        &g_fogEventFn        },
        { P::name::DaynightCycle_superFogEventFn,   &g_superFogEventFn   },
        { P::name::DaynightCycle_permaRainTimerFn,  &g_permaRainTimerFn  },
    };
    Entry mut[] = {
        { P::name::DaynightCycle_causeRainFn,          &g_causeRainFn          },
        { P::name::DaynightCycle_setRainPropertiesFn,  &g_setRainPropertiesFn  },
        { P::name::DaynightCycle_setWindParametersFn,  &g_setWindParametersFn  },
        { P::name::DaynightCycle_intComsTriggerSnowFn, &g_intComsTriggerSnowFn },
        { P::name::DaynightCycle_spawnFogFn,           &g_spawnFogFn           },
        { P::name::DaynightCycle_setFogDensityFn,      &g_setFogDensityFn      },
    };

    int sResolved = 0, mResolved = 0;
    for (auto& e : sched) {
        if (*e.out) { ++sResolved; continue; }
        if (void* fn = R::FindFunction(cls, e.name)) { *e.out = fn; ++sResolved; }
    }
    for (auto& e : mut) {
        if (*e.out) { ++mResolved; continue; }
        if (void* fn = R::FindFunction(cls, e.name)) { *e.out = fn; ++mResolved; }
    }
    // All 5 schedulers required for host broadcasts to be complete AND for
    // client suppression to be airtight. All 6 mutators required so the
    // client can apply every kind of state delta. Partial resolution would
    // be a silent correctness hole.
    return sResolved == 5 && mResolved == 6;
}

}  // namespace

void Install(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
    if (g_installed) return;

    if (!TryResolveAllFunctions()) {
        // daynightCycle_C not yet loaded (still in OMEGA / loading) OR a
        // UFunction renamed in a future recook (caught here, not at runtime).
        // Retry on next NetPumpTick.
        return;
    }

    if (!session) {
        // Session not yet wired in -- harness calls Install with the session
        // pointer once it has it. Wait.
        return;
    }

    const bool isHost = (session->role() == coop::net::Role::Host);

    if (isHost && !g_observersRegistered) {
        int n = 0;
        void* targets[] = { g_timerRainFn, g_timerLightningFn, g_fogEventFn,
                            g_superFogEventFn, g_permaRainTimerFn };
        for (void* t : targets) {
            if (t && GT::RegisterPostObserver(t, &OnSchedulerPost)) ++n;
        }
        g_observersRegistered = true;
        UE_LOGI("weather: HOST POST observers registered on %d/5 scheduler "
                "UFunctions (timerRain=%p timerLightning=%p fogEvent=%p "
                "superFogEvent=%p permaRain_timer=%p)",
                n, g_timerRainFn, g_timerLightningFn, g_fogEventFn,
                g_superFogEventFn, g_permaRainTimerFn);
    }

    if (!isHost && !g_interceptorsRegistered) {
        int n = 0;
        void* targets[] = { g_timerRainFn, g_timerLightningFn, g_fogEventFn,
                            g_superFogEventFn, g_permaRainTimerFn };
        for (void* t : targets) {
            if (t && GT::RegisterInterceptor(t, &OnSchedulerPreSuppress)) ++n;
        }
        g_interceptorsRegistered = true;
        UE_LOGI("weather: CLIENT PRE interceptors registered on %d/5 scheduler "
                "UFunctions (client never decides 'rain now' locally; receives "
                "WeatherState from host instead)", n);
    }

    g_installed = true;
}

void QueueConnectBroadcast() {
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s) return;
    if (s->role() != coop::net::Role::Host) return;  // host-only sender
    void* cycle = ResolveCycle();
    if (!cycle) {
        UE_LOGI("weather: QueueConnectBroadcast no daynightCycle_C live -- "
                "skipping (client receives default; observer-driven path "
                "will catch the next state change)");
        return;
    }
    coop::net::WeatherStatePayload p{};
    if (!ReadCycleState(cycle, p)) return;
    g_pendingBroadcastPayload = p;
    g_pendingBroadcast = true;
    UE_LOGI("weather: QueueConnectBroadcast queued flags=0x%02X rain=%.2f "
            "lc=%.2f dc=%.2f ws=%.2f (retry SendReliable each tick)",
            p.flags, p.rainStrength, p.rainLightningChance,
            p.rainDeactivateChance, p.rainWindSpeed);
}

void TickConnect() {
    // (a) Host: drain pending broadcast (retry SendReliable on busy channel).
    if (g_pendingBroadcast) {
        auto* s = g_session.load(std::memory_order_acquire);
        if (!s || !s->connected()) {
            g_pendingBroadcast = false;
        } else if (s->SendReliable(coop::net::ReliableKind::WeatherState,
                                   &g_pendingBroadcastPayload,
                                   sizeof(g_pendingBroadcastPayload))) {
            const uint64_t sig = SignaturePayload(g_pendingBroadcastPayload);
            const uint64_t storeSig = (sig == kNoSendYet) ? (kNoSendYet - 1) : sig;
            g_lastSentSig.store(storeSig, std::memory_order_release);
            UE_LOGI("weather: connect-replay broadcast sent (flags=0x%02X rain=%.2f)",
                    g_pendingBroadcastPayload.flags,
                    g_pendingBroadcastPayload.rainStrength);
            g_pendingBroadcast = false;
        }
        // else: silent retry next tick.
    }

    // (b) Client: drain pending receiver-side apply once Install completes
    // + the local cycle becomes live. The stashed payload was set by
    // ApplyFromHost when a WeatherState arrived before Install was ready.
    if (g_pendingApply && g_installed) {
        void* cycle = ResolveCycle();
        if (cycle && R::IsLive(cycle)) {
            coop::net::WeatherStatePayload p = g_pendingApplyPayload;
            g_pendingApply = false;
            UE_LOGI("weather: draining deferred apply (flags=0x%02X rain=%.2f)",
                    p.flags, p.rainStrength);
            ApplyFromHost(p);  // re-enters; this time both gates pass
        }
        // else: cycle still loading; retry next tick.
    }
}

void OnDisconnect() {
    if (g_pendingBroadcast) {
        UE_LOGI("weather: OnDisconnect clearing pending broadcast (flags=0x%02X)",
                g_pendingBroadcastPayload.flags);
    }
    if (g_pendingApply) {
        UE_LOGI("weather: OnDisconnect clearing pending apply (flags=0x%02X)",
                g_pendingApplyPayload.flags);
    }
    g_pendingBroadcast = false;
    g_pendingBroadcastPayload = {};
    g_pendingApply = false;
    g_pendingApplyPayload = {};
    g_lastSentSig.store(kNoSendYet, std::memory_order_release);
    // Cycle cache may dangle if the session ends mid-level-transition;
    // clear so the next ResolveCycle re-walks via FindObjectByClass.
    g_cycleCache = nullptr;
}

void ApplyFromHost(const coop::net::WeatherStatePayload& payload) {
    // Defensive: only the host sends weather. A client peer sending one would
    // mean a protocol violation; drop.
    if (payload.peerSessionId != 0) {
        UE_LOGW("weather: ApplyFromHost peerSessionId=%u != 0 (host) -- dropping",
                static_cast<unsigned>(payload.peerSessionId));
        return;
    }
    // g_installed gate: TryResolveAllFunctions requires all 11 UFunctions
    // (5 scheduler + 6 mutator) to be resolved before flipping g_installed.
    // If a WeatherState packet arrives while we're still resolving (e.g.
    // mutators partially resolved on a slow boot), partial application
    // would brick the cycle into an inconsistent state. Stash latest-wins
    // and let TickConnect drain once g_installed flips true.
    void* cycle = g_installed ? ResolveCycle() : nullptr;
    if (!g_installed || !cycle || !R::IsLive(cycle)) {
        g_pendingApplyPayload = payload;
        g_pendingApply = true;
        UE_LOGI("weather: ApplyFromHost defer (installed=%d cycle=%p) -- "
                "stashing for TickConnect drain",
                g_installed ? 1 : 0, cycle);
        return;
    }

    uint8_t* base = reinterpret_cast<uint8_t*>(cycle);

    // Read current state for delta-compare; we avoid redundant UFunction
    // calls when the bit hasn't changed (causeRain / intComs_triggerSnow
    // fan out to BP listeners + start/stop particle systems; calling them
    // with the current value is a wasted dispatch + a redundant fanout
    // notification).
    coop::net::WeatherStatePayload cur{};
    ReadCycleState(cycle, cur);

    const uint8_t newFlags = payload.flags;
    const uint8_t curFlags = cur.flags;
    using namespace coop::net::weather_flags;

    // ---- direct-write config bits (no BP-listener fan-out required) -----
    // These mirror engine config state; the scheduler reads them on its
    // next fire (which is suppressed on the client, but writing the
    // canonical state matches host for save/snapshot consistency).
    if (((curFlags ^ newFlags) & kEnableRain) != 0)
        *reinterpret_cast<bool*>(base + P::off::AdaynightCycle_enable_rain) =
            (newFlags & kEnableRain) != 0;
    if (((curFlags ^ newFlags) & kEnableFog) != 0)
        *reinterpret_cast<bool*>(base + P::off::AdaynightCycle_enable_fog) =
            (newFlags & kEnableFog) != 0;
    if (((curFlags ^ newFlags) & kEnableSuperfog) != 0)
        *reinterpret_cast<bool*>(base + P::off::AdaynightCycle_enable_superfog) =
            (newFlags & kEnableSuperfog) != 0;
    if (((curFlags ^ newFlags) & kEnableSunlight) != 0)
        *reinterpret_cast<bool*>(base + P::off::AdaynightCycle_enableSunlight) =
            (newFlags & kEnableSunlight) != 0;
    if (((curFlags ^ newFlags) & kEnableMoonlight) != 0)
        *reinterpret_cast<bool*>(base + P::off::AdaynightCycle_enableMoonlight) =
            (newFlags & kEnableMoonlight) != 0;
    if (((curFlags ^ newFlags) & kPermanentRain) != 0)
        *reinterpret_cast<bool*>(base + P::off::AdaynightCycle_permanentRain) =
            (newFlags & kPermanentRain) != 0;

    // ---- isRaining via causeRain(bool) ----------------------------------
    // Field is at @0x02E4 but the BP also starts/stops a UParticleSystemComponent
    // + audio cue inside the function body. Direct field write would leave
    // the visual state out of sync. The setRainProperties call below ALSO
    // takes an isRaining arg, but that one is a state-block applier; the
    // dedicated causeRain handles the transition VFX.
    const bool newRain = (newFlags & kIsRaining) != 0;
    const bool curRain = (curFlags & kIsRaining) != 0;
    if (newRain != curRain && g_causeRainFn) {
        ue_wrap::ParamFrame f(g_causeRainFn);
        f.Set<bool>(L"isRaining", newRain);
        ue_wrap::Call(cycle, f);
    }

    // ---- setRainProperties(bool, 4 floats) ------------------------------
    // Updates rainStrength + rainLightningChance + rainDeactivateChance +
    // rainWindSpeed in one shot. Always-call when ANY rain scalar shifts
    // -- the function is cheap (just field writes + a setRainParameters
    // dispatch); skipping it would require per-field comparisons.
    const bool rainScalarsChanged =
        cur.rainStrength != payload.rainStrength ||
        cur.rainLightningChance != payload.rainLightningChance ||
        cur.rainDeactivateChance != payload.rainDeactivateChance ||
        cur.rainWindSpeed != payload.rainWindSpeed;
    if (rainScalarsChanged && g_setRainPropertiesFn) {
        ue_wrap::ParamFrame f(g_setRainPropertiesFn);
        f.Set<bool>(L"isRaining",            newRain);
        f.Set<float>(L"rainStrength",        payload.rainStrength);
        f.Set<float>(L"rainLightningChance", payload.rainLightningChance);
        f.Set<float>(L"rainDeactivateChance",payload.rainDeactivateChance);
        f.Set<float>(L"rainWindSpeed",       payload.rainWindSpeed);
        ue_wrap::Call(cycle, f);
    }

    // ---- setWindParameters() -- no args; reads cycle state internally ----
    // Always call after a rain-scalar change because rainWindSpeed feeds
    // into the wind actor via this propagator.
    if (rainScalarsChanged && g_setWindParametersFn) {
        ue_wrap::ParamFrame f(g_setWindParametersFn);
        ue_wrap::Call(cycle, f);
    }

    // ---- isSnow via intComs_triggerSnow(bool) ---------------------------
    // Per RE doc: 53 BP listeners subscribe to this interface dispatch.
    // Direct field write at @0x03B0 would update the bool but leave all
    // the snow-aware actors (footstep audio, weatherFogController, snow
    // particle volume) in their old state.
    const bool newSnow = (newFlags & kIsSnow) != 0;
    const bool curSnow = (curFlags & kIsSnow) != 0;
    if (newSnow != curSnow && g_intComsTriggerSnowFn) {
        ue_wrap::ParamFrame f(g_intComsTriggerSnowFn);
        f.Set<bool>(L"isSnow", newSnow);
        ue_wrap::Call(cycle, f);
    }

    UE_LOGI("weather: applied flags 0x%02X -> 0x%02X rain=%.2f lc=%.2f "
            "dc=%.2f ws=%.2f (rain-tx=%d snow-tx=%d scalars-changed=%d)",
            curFlags, newFlags,
            payload.rainStrength, payload.rainLightningChance,
            payload.rainDeactivateChance, payload.rainWindSpeed,
            (newRain != curRain) ? 1 : 0,
            (newSnow != curSnow) ? 1 : 0,
            rainScalarsChanged ? 1 : 0);
}

}  // namespace coop::weather_sync
