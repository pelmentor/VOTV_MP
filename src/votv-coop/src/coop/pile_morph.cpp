// coop/pile_morph.cpp -- see coop/pile_morph.h.
//
// The bind-model pile MORPH (v81 MORPH V2): grab->carry->throw->land sync that re-skins eid E in place
// across pile-A -> clump -> pile-B, anchored on PROVEN seams (the held-object channel for the grab; the
// owner-side death-watch poll for the land), with a deferred-destroy fallback so a missed morph never
// regresses the working grab. Replaces the v52 fresh-eid death-watch. The Init-POST observer the take-18
// attempt bet on does NOT fire for BP-deferred morph products (host_spawn_watcher.cpp:132), so nothing is
// anchored on it. docs/piles/07-MORPH-V2-held-object-channel.md.

#include "coop/pile_morph.h"

#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/prop_element_tracker.h"   // GetPropElementIdForActor / MarkKnownKeyedProp / MarkProcessedInit
#include "coop/remote_prop.h"            // RegisterPropMirror (single rebind entry) / ResolveMirrorEidByActor
#include "ue_wrap/engine.h"
#include "ue_wrap/log.h"
#include "ue_wrap/prop.h"                // IsGarbageClump / IsChipPile / GetChipType / FindNearestChipPile
#include "ue_wrap/reflection.h"
#include "ue_wrap/types.h"              // NormalizeAxis

#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace coop::pile_morph {
namespace {

namespace R  = ue_wrap::reflection;
namespace E  = ue_wrap::engine;
namespace PT = coop::prop_element_tracker;

using Clock = std::chrono::steady_clock;

// ---- state (game-thread only: OnGrab from the InpActEvt_use PRE observer, TryAdoptHeldClump from
// local_streams' new-held edge, Tick from subsystems -- all on the game thread) -------------------------
// pending-morph: armed on the E-press (OnGrab), consumed by the new-held clump (TryAdoptHeldClump) or the
// deferred-destroy fallback (Tick). Single-slot: the local player grabs ONE pile at a time, and OnGrab is
// skipped when the hands are already full.
struct PendingMorph {
    bool                 active  = false;
    coop::element::ElementId eid = coop::element::kInvalidId;
    ue_wrap::FVector     pilePos{};
    Clock::time_point    armedAt{};
};
// land-watch: armed on adopt; a VECTOR because several thrown clumps can be in flight at once (throw A,
// then grab+throw B before A lands). Each is consumed by its re-pile (Tick land branch) or, on a no-land
// despawn, by the same Tick poll -> PropDestroy(E).
struct LandWatch {
    coop::element::ElementId eid = coop::element::kInvalidId;
    void*                clump   = nullptr;
    int32_t              clumpIdx = -1;     // GUObjectArray index -> IsLiveByIndex without deref
    ue_wrap::FVector     lastPos{};         // updated each tick while alive -> the landed-pile anchor
};
PendingMorph             g_pending;
std::vector<LandWatch>   g_land;
constexpr size_t         kMaxLandWatch = 16;  // bounded runaway backstop (clumps are transient)

// The clump appears as holding_actor within a frame or two of the E-press; 400 ms (~24 frames @60fps) is
// generous. If it does not adopt by then, the pile DID morph (it is gone locally) but we could not see the
// clump -> vanish the peer's mirror (take-17 behaviour, never a regression).
constexpr auto  kDeferredDestroyMs = std::chrono::milliseconds(400);
// The morphed clump spawns at the pile's transform / the player's hand (within a few metres of the
// walk-up-and-press-E pile). Generous so a real morph is never rejected; a reject falls back to the
// deferred destroy (take-17), so erring wide is safe.
constexpr float kClumpNearPileCm = 600.f;
// The re-piled chipPile spawns AT the clump's last live position (tracked per-tick, so <=~1 tick of
// flight). Search a tight radius for the owner's just-spawned pile.
constexpr float kLandSearchCm = 100.f;

inline bool Near(const ue_wrap::FVector& a, const ue_wrap::FVector& b, float cm) {
    const float dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return dx * dx + dy * dy + dz * dz <= cm * cm;
}

// Re-skin eid E onto `actor` in place. RegisterPropMirror is the SINGLE rebind entry point: it routes on
// the Element's authoritative IsMirror() flag (a MIRROR -> SetActor; a host's OWN local element ->
// RebindLocalElementActor, keeping the forward map consistent), so the morph never needs to guess local
// vs mirror -- correct even for a host applying a client's convert against its own pile.
void RebindE(coop::element::ElementId E, void* actor) {
    coop::remote_prop::RegisterPropMirror(E, actor, L"", R::ClassNameOf(actor), /*senderSlot=*/0,
                                          /*rebindInPlace=*/true);
}

void SendConvert(coop::net::Session& s, coop::element::ElementId E, uint8_t kind, void* actor) {
    const std::wstring cls = R::ClassNameOf(actor);
    coop::net::PropConvertPayload p{};
    p.oldEid = static_cast<uint32_t>(E);   // bind model: oldEid == newEid == E
    p.newEid = static_cast<uint32_t>(E);
    p.pileClass.len = 0;
    for (size_t i = 0; i < cls.size() && i < 63; ++i)
        p.pileClass.data[p.pileClass.len++] = static_cast<char>(cls[i]);
    const ue_wrap::FVector  loc = E::GetActorLocation(actor);
    const ue_wrap::FRotator rot = E::GetActorRotation(actor);
    p.locX = loc.X; p.locY = loc.Y; p.locZ = loc.Z;
    p.rotPitch = ue_wrap::NormalizeAxis(rot.Pitch);
    p.rotYaw   = ue_wrap::NormalizeAxis(rot.Yaw);
    p.rotRoll  = ue_wrap::NormalizeAxis(rot.Roll);
    p.chipType = ue_wrap::prop::GetChipType(actor);
    p.kind     = kind;
    s.SendReliable(coop::net::ReliableKind::PropConvert, &p, sizeof(p));
}

void SendDestroy(coop::net::Session& s, coop::element::ElementId E) {
    coop::net::PropDestroyPayload dp{};
    dp.key.len   = 0;                        // chipPile/clump are eid-only (Key=None)
    dp.elementId = static_cast<uint32_t>(E);
    s.SendPropDestroy(dp);
}

// Resolve any eid already bound to `actor` (local forward map OR wire mirror). kInvalidId = unbound.
coop::element::ElementId BoundEidOf(void* actor) {
    coop::element::ElementId e = PT::GetPropElementIdForActor(actor);
    if (e == coop::element::kInvalidId) e = coop::remote_prop::ResolveMirrorEidByActor(actor);
    return e;
}

}  // namespace

void OnGrab(coop::net::Session& s, coop::element::ElementId eid, const ue_wrap::FVector& pilePos) {
    // Superseding a still-active pending (no adopt, no fallback yet) would LEAK its deferred destroy --
    // fire it now so the abandoned pile's mirror is not stranded (take-17 grab->vanish preserved).
    if (g_pending.active && g_pending.eid != eid) {
        SendDestroy(s, g_pending.eid);
        UE_LOGI("pile_morph: grab eid=%u supersedes un-adopted pending eid=%u -> fallback PropDestroy(old)",
                eid, g_pending.eid);
    }
    g_pending.active  = true;
    g_pending.eid     = eid;
    g_pending.pilePos = pilePos;
    g_pending.armedAt = Clock::now();
    UE_LOGI("pile_morph: grab armed -- eid=%u pilePos=(%.1f,%.1f,%.1f); deferred PropDestroy in %lldms if "
            "no clump is adopted",
            eid, pilePos.X, pilePos.Y, pilePos.Z, static_cast<long long>(kDeferredDestroyMs.count()));
}

bool TryAdoptHeldClump(coop::net::Session& s, void* heldActor) {
    if (!g_pending.active || !heldActor) return false;
    if (!ue_wrap::prop::IsGarbageClump(heldActor)) return false;  // only the morph product is a clump
    const ue_wrap::FVector cloc = E::GetActorLocation(heldActor);
    if (!Near(cloc, g_pending.pilePos, kClumpNearPileCm)) {
        const float dx = cloc.X - g_pending.pilePos.X, dy = cloc.Y - g_pending.pilePos.Y,
                    dz = cloc.Z - g_pending.pilePos.Z;
        UE_LOGI("pile_morph: held clump %p is %.0fcm from the grabbed pile (eid=%u) -- not the morph, ignoring",
                heldActor, std::sqrt(dx*dx + dy*dy + dz*dz), g_pending.eid);
        return false;
    }
    const coop::element::ElementId E_ = g_pending.eid;
    RebindE(E_, heldActor);                                  // re-skin E: pile-A -> clump
    SendConvert(s, E_, coop::net::propconvert_kind::kToClump, heldActor);
    g_pending.active = false;                                // cancels the deferred destroy
    if (g_land.size() < kMaxLandWatch) {
        g_land.push_back({E_, heldActor, R::InternalIndexOf(heldActor), cloc});
    } else {
        UE_LOGW("pile_morph: land-watch cap %zu hit -- eid=%u re-pile will not sync (peers keep the clump "
                "until teardown)", kMaxLandWatch, E_);
    }
    UE_LOGI("pile_morph: ADOPTED held clump %p onto eid=%u -> PropConvert{ToClump}; land-watch armed "
            "(%zu in flight)", heldActor, E_, g_land.size());
    return true;
}

void Tick(coop::net::Session& s) {
    if (!g_pending.active && g_land.empty()) return;  // idle: no per-tick cost
    const auto now = Clock::now();
    // (a) deferred-destroy fallback -- the pile morphed but no clump was adopted in time -> vanish the
    // peer's mirror (the working take-17 grab outcome). A real adopt already cleared g_pending.
    if (g_pending.active && now - g_pending.armedAt >= kDeferredDestroyMs) {
        SendDestroy(s, g_pending.eid);
        UE_LOGI("pile_morph: deferred-destroy fired -- eid=%u not adopted in %lldms -> PropDestroy "
                "(peer drops its mirror; no regression vs take-17)",
                g_pending.eid, static_cast<long long>(kDeferredDestroyMs.count()));
        g_pending.active = false;
    }
    // (b) land-watch poll.
    for (size_t i = 0; i < g_land.size();) {
        LandWatch& lw = g_land[i];
        if (R::IsLiveByIndex(lw.clump, lw.clumpIdx)) {
            lw.lastPos = E::GetActorLocation(lw.clump);  // track the carry/flight for the anchor
            ++i;
            continue;
        }
        // The clump went dead -> turnToPile re-piled it (owner: the new pile is co-located at lastPos --
        // SOUND) OR it despawned (LifeSpan / a cleaner ate it). Find the owner's nearest re-piled pile.
        const coop::element::ElementId E_ = lw.eid;
        float dist = -1.f;
        void* pile = ue_wrap::prop::FindNearestChipPile(lw.lastPos, kLandSearchCm, &dist);
        if (pile) {
            const coop::element::ElementId already = BoundEidOf(pile);
            if (already == coop::element::kInvalidId) {
                // UNBOUND -> this is our just-spawned re-pile, not yet claimed by anything. Bind E onto
                // it + suppress the host's 0.25 Hz keyless-pile re-seed (which WOULD otherwise express
                // this chipPile under a fresh eid = a dupe) by marking it known + processed.
                PT::MarkKnownKeyedProp(pile);
                PT::MarkProcessedInit(pile);
                RebindE(E_, pile);                          // re-skin E: clump -> pile-B
                SendConvert(s, E_, coop::net::propconvert_kind::kToPile, pile);
                UE_LOGI("pile_morph: land detected for eid=%u -> pile %p at %.0fcm -> PropConvert{ToPile} "
                        "(re-seed suppressed)", E_, pile, dist);
            } else if (already == E_) {
                // Already bound to E (an echo / a double poll) -- nothing to do.
                UE_LOGI("pile_morph: land pile for eid=%u already bound to E -- no-op", E_);
            } else {
                // The host re-seed WON the race and expressed this pile under its own eid E2 (already
                // synced to peers). Do NOT double-bind -- just drop the now-defunct clump mirror; E2 is
                // the authoritative pile.
                SendDestroy(s, E_);
                UE_LOGI("pile_morph: land pile for eid=%u was already expressed as eid=%u (re-seed won) "
                        "-> PropDestroy(clump eid); the pre-expressed pile stands", E_, already);
            }
        } else {
            // No pile near -> the clump despawned without re-piling. Drop the peers' flying clump mirror.
            SendDestroy(s, E_);
            UE_LOGI("pile_morph: clump for eid=%u died with no re-pile near -> PropDestroy (peers drop "
                    "the clump mirror)", E_);
        }
        g_land.erase(g_land.begin() + i);
    }
}

void OnDisconnect() {
    g_pending.active = false;
    g_land.clear();
}

}  // namespace coop::pile_morph
