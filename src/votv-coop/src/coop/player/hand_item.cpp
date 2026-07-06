// coop/player/hand_item.cpp -- see coop/player/hand_item.h for the design.

#include "coop/player/hand_item.h"

#include "coop/net/protocol.h"
#include "coop/player/players_registry.h"
#include "coop/player/remote_player.h"
#include "coop/props/prop_echo_suppress.h"
#include "ue_wrap/engine.h"
#include "ue_wrap/fname_utils.h"
#include "ue_wrap/game_thread.h"
#include "ue_wrap/hot_path_guard.h"  // UE_ASSERT_GAME_THREAD
#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"
#include "ue_wrap/types.h"

#include <cstring>
#include <string>
#include <vector>

namespace coop::hand_item {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;

// Wire caps. BP class names ("prop_physgun_C") and item names ("grenade_p")
// are short ASCII; 63 is generous headroom, one byte each on the wire.
constexpr size_t kMaxStr = 63;

// updateHold destroys + respawns the hand actor on every quick-slot switch, so
// holding_actor is null for a frame or two mid-switch. Only a null that
// PERSISTS this many owner ticks is a real stow. (60 Hz -> ~250 ms; a switch
// lands in 1-3 frames, far inside the window.)
constexpr int kEmptyDebounceTicks = 15;

// The native hold floats the item in front of the player at grabLen (the BP
// Timeline tops out at 150 -- the same constant puppet_carry_drive uses).
// Attached to the puppet ROOT (capsule), so it follows body yaw; slightly
// below eye line reads as "in hands".
constexpr float kHoldForwardCm = 150.f;
constexpr float kHoldUpCm      = -20.f;

// ---- per-slot wire state (what each peer's hand holds) -------------------
struct SlotHand {
    bool has = false;
    std::wstring cls;   // BP class name, e.g. L"prop_physgun_C"
    std::wstring name;  // Aprop_C 'name' FName (drives the generic prop mesh), may be empty
};
SlotHand g_hands[coop::players::kMaxPeers];  // GT-only

// ---- per-slot display mirror ---------------------------------------------
struct Mirror {
    void*        actor = nullptr;
    int32_t      idx   = -1;  // GUObjectArray index (IsLiveByIndex validation)
    std::wstring cls;
    std::wstring name;
    int32_t      puppetIdx = -1;  // the puppet we attached to (re-attach on respawn)
};
Mirror g_mirrors[coop::players::kMaxPeers];  // GT-only

// The session, cached by TickOwner (net_pump tick site) for the connect-replay
// entry that only gets a slot (subsystems fanout convention). GT-only.
coop::net::Session* g_session = nullptr;

// ---- owner-side change detection ------------------------------------------
bool         g_ownHas = false;
std::wstring g_ownCls;
std::wstring g_ownName;
int          g_ownEmptyStreak = 0;
// The last-seen holding_actor identity (pointer + captured GUObjectArray index —
// updateHold recycles addresses). While it is unchanged, TickOwner skips the
// ClassNameOf/ReadItemName renders entirely (audit 2026-07-06 MEDIUM: FName::
// ToString allocates engine-side per call; 2 renders x 60 Hz while holding was
// the exact per-tick pattern local_streams already latches for the held eid).
void*        g_ownHeldPtr = nullptr;
int32_t      g_ownHeldIdx = -1;

// Aprop_C 'name' FName property offset, resolved once (the field lives on the
// prop base class; every hotbar item is a descendant).
int32_t PropNameOffset() {
    static int32_t s_off = -1;
    if (s_off >= 0) return s_off;
    if (void* propCls = R::FindClass(L"prop_C"))
        s_off = R::FindPropertyOffset(propCls, L"name");
    return s_off;
}

std::wstring ReadItemName(void* actor) {
    const int32_t off = PropNameOffset();
    if (off < 0 || !actor) return {};
    R::FName n{};
    std::memcpy(&n, reinterpret_cast<const uint8_t*>(actor) + off, sizeof(n));
    std::wstring s = R::ToString(n);
    if (s == L"None") s.clear();
    return s;
}

// [u8 slot][u8 has][u8 clsLen][cls ascii][u8 nameLen][name ascii]
std::vector<uint8_t> BuildPayload(uint8_t slot, const SlotHand& h) {
    std::vector<uint8_t> out;
    const size_t cl = h.has ? (h.cls.size() > kMaxStr ? kMaxStr : h.cls.size()) : 0;
    const size_t nl = h.has ? (h.name.size() > kMaxStr ? kMaxStr : h.name.size()) : 0;
    out.reserve(4 + cl + nl);
    out.push_back(slot);
    out.push_back(h.has ? 1 : 0);
    out.push_back(static_cast<uint8_t>(cl));
    for (size_t i = 0; i < cl; ++i) out.push_back(static_cast<uint8_t>(h.cls[i]));
    out.push_back(static_cast<uint8_t>(nl));
    for (size_t i = 0; i < nl; ++i) out.push_back(static_cast<uint8_t>(h.name[i]));
    return out;
}

void SendState(coop::net::Session& session, uint8_t slot, const SlotHand& h) {
    const std::vector<uint8_t> p = BuildPayload(slot, h);
    if (session.role() == coop::net::Role::Host) {
        for (int x = 1; x < coop::net::kMaxPeers; ++x) {
            if (x == slot) continue;                 // never echo the originator
            if (!session.IsSlotReady(x)) continue;
            session.SendReliableToSlot(x, coop::net::ReliableKind::HandItem,
                                       p.data(), static_cast<int>(p.size()));
        }
    } else {
        session.SendReliableToSlot(0, coop::net::ReliableKind::HandItem,
                                   p.data(), static_cast<int>(p.size()));
    }
}

void DestroyMirror(uint8_t slot, const char* why) {
    Mirror& m = g_mirrors[slot];
    if (m.actor && R::IsLiveByIndex(m.actor, m.idx)) {
        // Suppress the K2_DestroyActor PRE observer's echo (host would
        // otherwise broadcast a PropDestroy for a display-only actor).
        coop::prop_echo_suppress::MarkIncomingDestroy(m.actor);
        E::DestroyActor(m.actor);
        UE_LOGI("hand_item: slot %u mirror destroyed (%s)", static_cast<unsigned>(slot), why);
    }
    m = Mirror{};
}

void SpawnMirror(uint8_t slot, void* puppetActor) {
    const SlotHand& want = g_hands[slot];
    void* cls = R::FindClass(want.cls.c_str());
    if (!cls) {
        // BP classes load on demand; if this peer's game never loaded the item
        // class, we retry each tick. Warn once per class to keep the log sane.
        static std::wstring sWarned;
        if (sWarned != want.cls) {
            sWarned = want.cls;
            UE_LOGW("hand_item: class '%ls' not found (unloaded?) -- will retry", want.cls.c_str());
        }
        return;
    }
    const ue_wrap::FVector loc = E::GetActorLocation(puppetActor);
    const ue_wrap::FRotator rot{};
    void* actor = E::BeginDeferredSpawn(cls, loc, rot);
    if (!actor) {
        UE_LOGW("hand_item: BeginDeferredSpawn '%ls' failed", want.cls.c_str());
        return;
    }
    // Stamp the item 'name' BEFORE Init runs (generic prop_C meshes are
    // name-driven, same pattern as the game's own updateHold spawn).
    if (!want.name.empty()) {
        const int32_t off = PropNameOffset();
        if (off >= 0) {
            R::FName n = ue_wrap::fname_utils::StringToFName(want.name);
            std::memcpy(reinterpret_cast<uint8_t*>(actor) + off, &n, sizeof(n));
        }
    }
    // Suppress the Init POST spawn-catch echo (host_spawn_watcher /
    // prop_lifecycle must never express a display mirror as a world prop).
    coop::prop_echo_suppress::MarkIncomingSpawn(actor);
    if (!E::FinishDeferredSpawn(actor, loc, rot)) {
        UE_LOGW("hand_item: FinishDeferredSpawn '%ls' failed", want.cls.c_str());
        return;
    }
    E::SetActorSimulatePhysics(actor, false);
    E::SetActorEnableCollision(actor, false);
    void* rootComp = E::GetActorRootComponent(puppetActor);
    if (rootComp && E::AttachActorToComponentSocket(actor, rootComp, nullptr)) {
        E::SetActorRelativeLocation(actor,
                                    ue_wrap::FVector{kHoldForwardCm, 0.f, kHoldUpCm});
    } else {
        UE_LOGW("hand_item: attach to puppet root failed (slot %u) -- mirror follows at spawn pos",
                static_cast<unsigned>(slot));
    }
    Mirror& m = g_mirrors[slot];
    m.actor = actor;
    m.idx = R::InternalIndexOf(actor);
    m.cls = want.cls;
    m.name = want.name;
    m.puppetIdx = R::InternalIndexOf(puppetActor);
    UE_LOGI("hand_item: slot %u mirror SPAWNED cls='%ls' name='%ls' (display-only, attached)",
            static_cast<unsigned>(slot), want.cls.c_str(), want.name.c_str());
}

}  // namespace

void TickOwner(coop::net::Session& session, void* holdingProp) {
    g_session = &session;
    if (!session.connected()) return;
    const uint8_t self = coop::players::Registry::Get().LocalPeerId();
    if (self >= coop::players::kMaxPeers) return;

    if (holdingProp) {
        g_ownEmptyStreak = 0;
        // O(1) steady-state: same live actor as last tick -> skip the renders,
        // EXCEPT a slow re-check every 30 ticks (~0.5 s) — the game renames a
        // held prop IN PLACE (grenade arm: 'name' -> grenade_1), which a pure
        // pointer latch would never see. 2 renders/s vs 120/s steady-state.
        static uint32_t sSameStreak = 0;
        const int32_t idx = R::InternalIndexOf(holdingProp);
        if (holdingProp == g_ownHeldPtr && idx == g_ownHeldIdx &&
            (++sSameStreak % 30) != 0) {
            return;
        }
        g_ownHeldPtr = holdingProp;
        g_ownHeldIdx = idx;
        const std::wstring cls = R::ClassNameOf(holdingProp);
        const std::wstring name = ReadItemName(holdingProp);
        if (!g_ownHas || cls != g_ownCls || name != g_ownName) {
            g_ownHas = true;
            g_ownCls = cls;
            g_ownName = name;
            SlotHand& h = g_hands[self];
            h.has = true; h.cls = cls; h.name = name;
            SendState(session, self, h);
            UE_LOGI("hand_item: local hand -> cls='%ls' name='%ls' (announced)",
                    cls.c_str(), name.c_str());
        }
    } else if (g_ownHas) {
        // The hand is empty this tick: drop the identity latch so a re-appear
        // (same OR different actor) re-renders + re-compares.
        g_ownHeldPtr = nullptr;
        g_ownHeldIdx = -1;
        // Debounce the updateHold switch flicker: only a PERSISTENT null is a stow.
        if (++g_ownEmptyStreak >= kEmptyDebounceTicks) {
            g_ownHas = false;
            g_ownCls.clear();
            g_ownName.clear();
            SlotHand& h = g_hands[self];
            h = SlotHand{};
            SendState(session, self, h);
            UE_LOGI("hand_item: local hand -> EMPTY (announced)");
        }
    }
}

void TickMirrors() {
    auto& reg = coop::players::Registry::Get();
    const uint8_t self = reg.LocalPeerId();
    for (uint8_t slot = 0; slot < coop::players::kMaxPeers; ++slot) {
        if (slot == self) continue;  // never mirror our own hand
        const SlotHand& want = g_hands[slot];
        Mirror& m = g_mirrors[slot];
        const bool mirrorLive = m.actor && R::IsLiveByIndex(m.actor, m.idx);

        if (!want.has) {
            if (mirrorLive || m.actor) DestroyMirror(slot, "hand now empty");
            continue;
        }
        coop::RemotePlayer* pup = reg.Puppet(slot);
        void* puppetActor = pup ? pup->GetActor() : nullptr;
        if (!puppetActor || !R::IsLive(puppetActor)) {
            // Puppet not up yet (join window) or torn down: keep the state,
            // drop any orphaned mirror, retry next tick.
            if (mirrorLive || m.actor) DestroyMirror(slot, "puppet gone");
            continue;
        }
        const int32_t pupIdx = R::InternalIndexOf(puppetActor);
        if (mirrorLive && m.cls == want.cls && m.name == want.name &&
            m.puppetIdx == pupIdx) {
            continue;  // steady state
        }
        if (mirrorLive || m.actor) DestroyMirror(slot, "state/puppet changed");
        SpawnMirror(slot, puppetActor);
    }
}

bool HandleHandItem(coop::net::Session& session,
                    const coop::net::Session::ReliableMessage& msg) {
    UE_ASSERT_GAME_THREAD("HandleHandItem");
    if (msg.payloadLen < 4) {
        UE_LOGW("hand_item: payload %zu B too short -- dropping",
                static_cast<size_t>(msg.payloadLen));
        return true;
    }
    const uint8_t* p = msg.payload;
    size_t off = 0;
    const uint8_t describedSlot = p[off++];
    const bool has = p[off++] != 0;
    const uint8_t clsLen = p[off++];
    if (off + clsLen + 1 > static_cast<size_t>(msg.payloadLen)) {
        UE_LOGW("hand_item: truncated class field -- dropping");
        return true;
    }
    std::wstring cls;
    for (uint8_t i = 0; i < clsLen; ++i) cls.push_back(static_cast<wchar_t>(p[off++]));
    const uint8_t nameLen = p[off++];
    if (off + nameLen > static_cast<size_t>(msg.payloadLen)) {
        UE_LOGW("hand_item: truncated name field -- dropping");
        return true;
    }
    std::wstring name;
    for (uint8_t i = 0; i < nameLen; ++i) name.push_back(static_cast<wchar_t>(p[off++]));

    if (describedSlot >= coop::net::kMaxPeers) return true;
    if (has && cls.empty()) return true;  // malformed: "has item" with no class

    if (session.role() == coop::net::Role::Host) {
        // Forgery guard: a client may only describe ITS OWN hand.
        if (msg.senderPeerSlot != describedSlot || describedSlot == 0) {
            UE_LOGW("hand_item: slot=%u from senderSlot=%d -- forged, dropping",
                    static_cast<unsigned>(describedSlot), msg.senderPeerSlot);
            return true;
        }
        SlotHand& h = g_hands[describedSlot];
        h.has = has; h.cls = cls; h.name = name;
        // Rebroadcast to every other ready client (originator excluded).
        const std::vector<uint8_t> out = BuildPayload(describedSlot, h);
        for (int x = 1; x < coop::net::kMaxPeers; ++x) {
            if (x == describedSlot) continue;
            if (!session.IsSlotReady(x)) continue;
            session.SendReliableToSlot(x, coop::net::ReliableKind::HandItem,
                                       out.data(), static_cast<int>(out.size()));
        }
        return true;
    }

    // Client: only the host relays hand states.
    if (msg.senderPeerSlot != 0) {
        UE_LOGW("hand_item: HandItem from non-host senderPeerSlot=%d -- dropping",
                msg.senderPeerSlot);
        return true;
    }
    if (describedSlot == coop::players::Registry::Get().LocalPeerId()) return true;  // own echo
    SlotHand& h = g_hands[describedSlot];
    h.has = has; h.cls = cls; h.name = name;
    return true;
}

void ReplayPeerStatesToSlot(int slot) {
    coop::net::Session* session = g_session;
    if (!session || session->role() != coop::net::Role::Host) return;
    if (slot < 1 || slot >= static_cast<int>(coop::net::kMaxPeers)) return;
    for (uint8_t s = 0; s < coop::players::kMaxPeers; ++s) {
        if (s == slot) continue;
        if (!g_hands[s].has) continue;  // empty is the joiner's default -- nothing to send
        const std::vector<uint8_t> p = BuildPayload(s, g_hands[s]);
        session->SendReliableToSlot(slot, coop::net::ReliableKind::HandItem,
                                    p.data(), static_cast<int>(p.size()));
        UE_LOGI("hand_item: connect-replay -- slot %u holds '%ls' -> joiner slot %d",
                static_cast<unsigned>(s), g_hands[s].cls.c_str(), slot);
    }
}

void Reset() {
    for (uint8_t s = 0; s < coop::players::kMaxPeers; ++s) {
        DestroyMirror(s, "session reset");
        g_hands[s] = SlotHand{};
    }
    g_ownHas = false;
    g_ownCls.clear();
    g_ownName.clear();
    g_ownEmptyStreak = 0;
    g_ownHeldPtr = nullptr;
    g_ownHeldIdx = -1;
}

void OnSlotDisconnected(uint8_t slot) {
    if (slot >= coop::players::kMaxPeers) return;
    DestroyMirror(slot, "peer disconnected");
    g_hands[slot] = SlotHand{};
}

}  // namespace coop::hand_item
