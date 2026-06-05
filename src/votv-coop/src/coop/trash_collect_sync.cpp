// coop/trash_collect_sync.cpp -- see coop/trash_collect_sync.h.
//
// One function: EnsureHeldItemBroadcast. net_pump's held-prop send calls it on
// the new-held edge. The collect itself (trashBitsPile_C::playerTryToCollect)
// is BP-internal (ProcessInternal) so it can't be observed; we instead act on
// the grabbing_actor the send already resolves -- a freshly-spawned, auto-
// grabbed Aprop_C with Key=None gets a stable Key + a PropSpawn here, then the
// existing pose stream mirrors it into the collector's hands.

#include "coop/trash_collect_sync.h"

#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/prop_element_tracker.h"
#include "coop/prop_synth_key.h"
#include "ue_wrap/engine.h"
#include "ue_wrap/log.h"
#include "ue_wrap/prop.h"
#include "ue_wrap/reflection.h"
#include "ue_wrap/types.h"

#include <string>
#include <vector>

namespace coop::trash_collect_sync {
namespace {

namespace R  = ue_wrap::reflection;
namespace PT = coop::prop_element_tracker;

// Death-watch set. The clump's morph-destroy is unobservable (see header), so the
// owner liveness-checks every clump it broadcast and broadcasts the despawn when the
// actor dies. GAME-THREAD ONLY: both the held-edge (EnsureHeldItemBroadcast) and the
// per-tick sweep (TickWatchReleasedClumps) run from net_pump::Tick on the game thread,
// so no lock is needed. Bounded -- clumps are transient (usually 0-1 active); the cap
// is a runaway backstop. [[project-bug-trash-chippile-uaf-crash]]
struct WatchedClump {
    void*    actor       = nullptr;
    int32_t  internalIdx = -1;     // captured while live -> IsLiveByIndex without deref
    uint32_t eid         = 0;      // PropDestroy identity (key=None)
    ue_wrap::FVector lastPos{};    // updated each tick while alive -> the landed-pile search anchor
    ue_wrap::FVector lastVel{};    // updated each tick while alive -> turnToPile dust velocity (v29)
};
std::vector<WatchedClump> g_watchedClumps;
constexpr size_t kMaxWatchedClumps = 32;

// Register a freshly-broadcast clump for death-watching. Idempotent per eid.
void WatchClump(void* clump, uint32_t eid) {
    if (!clump || eid == 0) return;
    const int32_t idx = R::InternalIndexOf(clump);
    if (idx < 0) return;
    const ue_wrap::FVector pos = ue_wrap::engine::GetActorLocation(clump);
    for (auto& w : g_watchedClumps) {
        if (w.eid == eid) { w.actor = clump; w.internalIdx = idx; w.lastPos = pos; return; }
    }
    if (g_watchedClumps.size() >= kMaxWatchedClumps) {
        // Backstop: shed the oldest (should never trigger in normal play).
        g_watchedClumps.erase(g_watchedClumps.begin());
    }
    g_watchedClumps.push_back(WatchedClump{clump, idx, eid, pos});
}

// When a watched clump dies, it RE-PILED on landing (its hit-handler spawned a fresh
// chipPile then destroyed the clump). That new chipPile is now sitting at ~the clump's
// last position. Broadcast it (key=None + a minted eid) so the PEER spawns the landed
// pile at OUR authoritative position -- reliable, unlike priming the bare mirror to
// re-pile itself (BP impulse/slope gates made that ~3/10). Returns true if a pile was
// found + broadcast (false if the clump expired without re-piling -> nothing to spawn,
// matching the peer's own no-pile outcome). [[project-bug-trash-chippile-uaf-crash]]
bool BroadcastLandedPileNear(const ue_wrap::FVector& pos, const ue_wrap::FVector& vel,
                             coop::net::Session* s) {
    float dist = -1.f;
    void* pile = ue_wrap::prop::FindNearestChipPile(pos, 200.f, &dist);
    if (!pile) return false;
    const std::wstring cls = R::ClassNameOf(pile);
    // Mint an eid so the peer's OnSpawn accepts it (key=None piles need a non-zero eid).
    PT::MarkProcessedInit(pile);
    PT::MarkPropElement(pile, L"", cls);
    const coop::element::ElementId eid = PT::GetPropElementIdForActor(pile);
    if (eid == coop::element::kInvalidId) return false;
    coop::net::PropSpawnPayload p{};
    p.className.len = 0;
    for (size_t i = 0; i < cls.size() && i < 63; ++i)
        p.className.data[p.className.len++] = static_cast<char>(cls[i]);
    p.key.len = 0;
    const ue_wrap::FVector  loc = ue_wrap::engine::GetActorLocation(pile);
    const ue_wrap::FRotator rot = ue_wrap::engine::GetActorRotation(pile);
    p.locX = loc.X; p.locY = loc.Y; p.locZ = loc.Z;
    p.rotPitch = ue_wrap::NormalizeAxis(rot.Pitch);
    p.rotYaw   = ue_wrap::NormalizeAxis(rot.Yaw);
    p.rotRoll  = ue_wrap::NormalizeAxis(rot.Roll);
    p.scaleX = p.scaleY = p.scaleZ = 1.f;
    // A settled ground pile (placed, NOT simulating) -- but FRESHLY LANDED: the receiver
    // dispatches turnToPile(initLinVel) on the spawn so it plays the impact dust+SOUND the
    // bare spawn otherwise lacks (the flying mirror is despawned, never physically lands on
    // the peer). The clump's pre-landing velocity drives the dust direction. v29.
    p.physFlags = coop::net::propspawn_flags::kFreshLanded;
    p.chipType  = ue_wrap::prop::GetChipType(pile);
    p.initLinVelX = vel.X; p.initLinVelY = vel.Y; p.initLinVelZ = vel.Z;
    p.elementId = static_cast<uint32_t>(eid);
    UE_LOGI("trash_collect: landed-pile BROADCAST cls='%ls' at (%.1f,%.1f,%.1f) variant=%u "
            "vel=(%.0f,%.0f,%.0f) dist=%.1f eid=%u",
            cls.c_str(), p.locX, p.locY, p.locZ, static_cast<unsigned>(p.chipType),
            vel.X, vel.Y, vel.Z, dist, static_cast<unsigned>(eid));
    s->SendPropSpawn(p);
    return true;
}

}  // namespace

bool EnsureHeldItemBroadcast(void* heldActor, coop::net::Session* s) {
    if (!heldActor || !s || !s->connected()) return false;
    if (!R::IsLive(heldActor)) return false;
    // Any KEYED interactable: Aprop_C trash items AND the non-Aprop_C
    // garbageClump/chipPile (the actual trash the player carries). CRASH-SAFETY
    // is enforced on the RECEIVER, not by excluding them here: GetStaticMesh
    // returns null for non-Aprop_C, so the peer never runs physics on the clump
    // mirror -- it spawns physics-free and is driven KINEMATICALLY (the inverse
    // of the reverted-2a UAF). [[project-bug-trash-chippile-uaf-crash]]
    if (!ue_wrap::prop::IsKeyedInteractable(heldActor)) return false;

    // Only freshly-spawned UNKEYED items need a mint + spawn broadcast. An
    // already-keyed held actor is a normal world prop the peer already has (the
    // pose stream alone mirrors it) -- AND after our own mint the Key is
    // non-None, so this is the idempotency guard against re-broadcasting.
    std::wstring keyStr = ue_wrap::prop::GetInteractableKeyString(heldActor);
    if (!keyStr.empty() && keyStr != L"None") return false;

    const std::wstring cls = R::ClassNameOf(heldActor);
    // Aprop_C trash items get a force-minted Key (their UCS holds it). The non-Aprop_C
    // trash CLUMP (prop_garbageClump_C) is NON-KEYABLE -- setKey doesn't stick (the
    // source re-reads None, autotest 33e7f25) -- so it rides the SAME prop pipeline but
    // is identified by our EID instead: broadcast with key=None + the eid, and the
    // receiver resolves its mirror by eid (PropPoseSnapshot.elementId, v26). The clump
    // renders on its own (bare spawn = the 'dirtball' mesh), so no mesh transfer needed.
    // [[project-bug-trash-chippile-uaf-crash]]
    const bool isAprop = ue_wrap::prop::IsDescendantOfProp(heldActor);
    keyStr = coop::prop_synth_key::EnsureKeyForBroadcast(heldActor, keyStr, /*mintForAprop=*/isAprop);
    if (keyStr.empty() || keyStr == L"None") {
        if (isAprop) {
            UE_LOGW("trash_collect: Aprop item %p cls='%ls' Key still None after force-mint -- cannot mirror",
                    heldActor, cls.c_str());
            return false;
        }
        keyStr.clear();  // clump: wire key stays None; the eid is the cross-peer identity
    }
    // Create the Prop Element shadow + dedupe latch so the item's eventual
    // K2_DestroyActor unwinds it through the normal destroy path.
    PT::MarkProcessedInit(heldActor);
    PT::MarkPropElement(heldActor, keyStr, cls);

    coop::net::PropSpawnPayload p{};
    p.className.len = 0;
    for (size_t i = 0; i < cls.size() && i < 63; ++i)
        p.className.data[p.className.len++] = static_cast<char>(cls[i]);
    p.key.len = 0;
    for (size_t i = 0; i < keyStr.size() && i < 31; ++i)
        p.key.data[p.key.len++] = static_cast<char>(keyStr[i]);

    const ue_wrap::FVector  loc = ue_wrap::engine::GetActorLocation(heldActor);
    const ue_wrap::FRotator rot = ue_wrap::engine::GetActorRotation(heldActor);
    p.locX = loc.X; p.locY = loc.Y; p.locZ = loc.Z;
    p.rotPitch = ue_wrap::NormalizeAxis(rot.Pitch);
    p.rotYaw   = ue_wrap::NormalizeAxis(rot.Yaw);
    p.rotRoll  = ue_wrap::NormalizeAxis(rot.Roll);
    p.scaleX = p.scaleY = p.scaleZ = 1.f;
    // Carry the trash VARIANT so the mirror shows the same chip/clump type the owner
    // grabbed (else a bare spawn defaults to variant 0 = the wrong-type bug). 0 for any
    // non-trash actor (GetChipType is a reflection no-op without a chipType property).
    p.chipType = ue_wrap::prop::GetChipType(heldActor);
    p.physFlags = coop::net::propspawn_flags::kSimulatePhysics;
    // IsHeavy/IsFrozen read Aprop_C struct offsets -- only meaningful on Aprop_C.
    // For a non-Aprop_C clump the receiver ignores physFlags (kinematic mirror),
    // so don't read stray bytes off it.
    if (ue_wrap::prop::IsDescendantOfProp(heldActor)) {
        if (ue_wrap::prop::IsHeavy(heldActor))  p.physFlags |= coop::net::propspawn_flags::kIsHeavy;
        if (ue_wrap::prop::IsFrozen(heldActor)) p.physFlags |= coop::net::propspawn_flags::kFrozen;
    }
    p.initLinVelX = p.initLinVelY = p.initLinVelZ = 0.f;
    p.initAngVelX = p.initAngVelY = p.initAngVelZ = 0.f;
    {
        const coop::element::ElementId eid = PT::GetPropElementIdForActor(heldActor);
        p.elementId = (eid == coop::element::kInvalidId) ? 0u : eid;
    }
    UE_LOGI("trash_collect: BROADCAST held unkeyed item cls='%ls' key='%ls' loc=(%.1f,%.1f,%.1f) "
            "-- held-pose stream now mirrors it into the collector's hands",
            cls.c_str(), keyStr.c_str(), p.locX, p.locY, p.locZ);
    s->SendPropSpawn(p);
    // Non-keyable trash CLUMP (key was cleared above): its eventual morph-destroy is
    // UNOBSERVABLE (bypasses the K2_DestroyActor observer), so death-watch it -- the
    // tick its actor dies we broadcast the despawn by eid. Keyed Aprop_C trash items
    // are NOT watched: their destroy fires the observer normally (the keyed path works).
    if (keyStr.empty() && p.elementId != 0) {
        WatchClump(heldActor, p.elementId);
    }
    return true;
}

void TickWatchReleasedClumps(coop::net::Session* s) {
    if (!s || !s->connected() || g_watchedClumps.empty()) return;
    for (size_t i = 0; i < g_watchedClumps.size();) {
        WatchedClump& w = g_watchedClumps[i];
        if (R::IsLiveByIndex(w.actor, w.internalIdx)) {
            w.lastPos = ue_wrap::engine::GetActorLocation(w.actor);  // track for the landed-pile search
            w.lastVel = ue_wrap::engine::GetActorVelocity(w.actor);  // track for turnToPile dust (v29)
            ++i;
            continue;
        }
        // The clump's actor went dead -> it re-piled on landing (its hit-handler spawned a
        // fresh chipPile at ~lastPos then destroyed itself) or its LifeSpan expired.
        // (1) If it re-piled, BROADCAST that landed pile so the peer spawns it at OUR
        //     authoritative position -- reliable, vs priming the bare mirror to convert
        //     itself (BP gates made that ~3/10). No-op if no pile (expired w/o re-piling).
        // (2) Broadcast PropDestroy(key=None, eid) so the peer despawns the flying clump
        //     mirror (the unobservable destroy never produced this). Both one-shot.
        const bool madePile = BroadcastLandedPileNear(w.lastPos, w.lastVel, s);
        coop::net::PropDestroyPayload dp{};
        dp.key.len   = 0;          // key=None: the clump is eid-only
        dp.elementId = w.eid;
        s->SendPropDestroy(dp);
        UE_LOGI("trash_collect: watched clump eid=%u went dead -> landed-pile=%s + PropDestroy (despawn mirror)",
                w.eid, madePile ? "BROADCAST" : "none");
        g_watchedClumps.erase(g_watchedClumps.begin() + i);
    }
}

void OnDisconnect() {
    g_watchedClumps.clear();
}

}  // namespace coop::trash_collect_sync
