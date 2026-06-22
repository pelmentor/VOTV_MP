// coop/puppet_carry_drive.cpp -- see coop/puppet_carry_drive.h.

#include "coop/puppet_carry_drive.h"

#include "coop/net/protocol.h"       // TrashClumpPoseSnapshot (the carry pose batch entry)
#include "coop/net/session.h"        // SetLocalTrashCarryBatch (host publish)
#include "coop/players_registry.h"
#include "coop/remote_player.h"
#include "coop/trash_channel.h"     // IsCarrying / HasPendingSettle / CtxForEid (carry latch + stamp)
#include "ue_wrap/engine.h"          // SetActorLocation / GetActorLocation / GetActorRotation
#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"      // IsLiveByIndex / InternalIndexOf
#include "ue_wrap/types.h"           // FVector / FRotator / NormalizeAxis

#include <cstdint>
#include <vector>

namespace coop::puppet_carry_drive {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;

// The probe (votv-puppet-grab-feasibility-RE-2026-06-22) read grabLen frozen at 150 on the puppet --
// that is the BP-authored hold distance. We use it directly (the puppet's grabLen Timeline never
// advances, so there is no live value to read).
constexpr float kGrabLenCm = 150.f;

struct PuppetHeld {
    uint32_t eid   = 0;        // the trash entity
    uint8_t  slot  = 0;        // the peer whose puppet holds it
    void*    clump = nullptr;  // the held garbageClump actor (cross-tick: validate via clumpIdx)
    int32_t  clumpIdx = -1;    // GUObjectArray index captured at NotePuppetHeld (IsLiveByIndex)
    // FLIGHT (set by NoteThrown at a client's ThrowIntent): the throw released the puppet's grab + applied
    // physics velocity, so the host STOPS hand-driving and the clump flies free. We keep streaming its
    // (physics) pose each tick so every client renders the throw ARC, until the clump re-piles (the latch
    // closes / a settle commits) -> the entry is dropped + the ToPile convert snaps the proxy.
    bool     flying = false;
};

std::vector<PuppetHeld> g_held;  // GT-only; at most one per peer (carry AND flight, distinguished by `flying`)

// True while we published a NON-empty batch last tick. Lets Tick skip the SetLocalTrashCarryBatch call
// (a session-mutex lock) entirely at idle (the common case: nobody carrying), publishing one empty batch
// to CLEAR the stream when a carry ends, then staying quiet. Avoids a per-host-tick lock for no reason.
bool g_wasStreaming = false;

}  // namespace

void NotePuppetHeld(coop::element::ElementId eid, uint8_t slot, void* clump) {
    if (eid == 0u || eid == coop::element::kInvalidId || !clump) return;
    const uint32_t e = static_cast<uint32_t>(eid);
    for (auto& h : g_held) {                       // idempotent: a re-register updates in place
        if (h.eid == e) {
            h.slot = slot; h.clump = clump; h.clumpIdx = R::InternalIndexOf(clump); h.flying = false;
            UE_LOGI("[PUPPET-DRIVE] re-note eid=%u slot=%u clump=%p", e, slot, clump);
            return;
        }
    }
    g_held.push_back(PuppetHeld{e, slot, clump, R::InternalIndexOf(clump), false});
    UE_LOGI("[PUPPET-DRIVE] NOTE eid=%u slot=%u clump=%p -- per-tick hand-follow ON "
            "(the puppet tick does not drive the PHC; the host drives the hold pose)", e, slot, clump);
}

void NoteThrown(coop::element::ElementId eid) {
    const uint32_t e = static_cast<uint32_t>(eid);
    for (auto& h : g_held) {
        if (h.eid == e) {
            h.flying = true;   // stop hand-driving; keep streaming the physics flight pose until the re-pile
            UE_LOGI("[PUPPET-DRIVE] THROWN eid=%u slot=%u -- hand-drive OFF, flight pose stream ON "
                    "(physics owns the arc; re-pile ends it)", e, h.slot);
            return;
        }
    }
}

void Tick(coop::net::Session& s) {
    std::vector<coop::net::TrashClumpPoseSnapshot> batch;  // host->all carry/flight pose batch (empty = stop sending)
    static thread_local int sTick = 0;
    ++sTick;
    for (auto it = g_held.begin(); it != g_held.end(); ) {
        const coop::element::ElementId E = static_cast<coop::element::ElementId>(it->eid);
        // Guard 1 (FIRST): the carry latch closed = a re-pile land COMMIT (TickCarry ran BEFORE this in
        // TickGameplay + already cleared g_heldBy). The clump became a pile -> stop following, no release.
        if (!coop::trash_channel::IsCarrying(E)) {
            UE_LOGI("[PUPPET-DRIVE] eid=%u slot=%u -- carry latch closed (landed) -> drive OFF", it->eid, it->slot);
            it = g_held.erase(it);
            continue;
        }
        // Guard 2: the clump still live? (cross-tick cached pointer -> IsLiveByIndex, never bare IsLive.) The
        // re-pile DESTROYS the clump (K2_DestroyActor(self)) while a land-settle is pending (g_carry still
        // open for kLandSettleTicks) -- that is a LANDING, NOT a lost clump: erase WITHOUT releasing so the
        // settle can COMMIT the ToPile. Only a clump gone with NO pending settle is a genuine loss (audit
        // MEDIUM-1: release the hold so the eid is re-grabbable, else g_heldBy strands it un-grabbable).
        if (!R::IsLiveByIndex(it->clump, it->clumpIdx)) {
            if (coop::trash_channel::HasPendingSettle(E)) {
                UE_LOGI("[PUPPET-DRIVE] eid=%u slot=%u -- clump consumed by a re-pile (settle pending) -> drive OFF (land commits)",
                        it->eid, it->slot);
            } else {
                UE_LOGI("[PUPPET-DRIVE] eid=%u slot=%u -- clump gone with NO land -> drive OFF + release hold", it->eid, it->slot);
                coop::trash_channel::ReleaseClientHold(s, E);
            }
            it = g_held.erase(it);
            continue;
        }
        if (!it->flying) {
            // Guard 3 (hand-drive only): the puppet still live? A not-live puppet without a full disconnect
            // would strand the hold -- release it. (During flight the clump is independent of the puppet.)
            coop::RemotePlayer* rp = coop::players::Registry::Get().Puppet(it->slot);
            if (!rp || !rp->valid()) {
                UE_LOGI("[PUPPET-DRIVE] eid=%u slot=%u -- puppet gone -> drive OFF + release hold", it->eid, it->slot);
                coop::trash_channel::ReleaseClientHold(s, E);
                it = g_held.erase(it);
                continue;
            }
            // Drive: position the held clump at the puppet's hand = head + syncedAim * grabLen. The puppet's
            // synced aim is already streamed (curYaw_/curPitch_), so the clump tracks where the remote player
            // looks. SetActorLocation is a teleport that wins over the (un-driven, frozen) PHC target.
            const ue_wrap::FVector head = rp->GetHeadPosition();
            const ue_wrap::FVector fwd  = rp->GetSyncedAimDirection();
            const ue_wrap::FVector hold{ head.X + fwd.X * kGrabLenCm,
                                         head.Y + fwd.Y * kGrabLenCm,
                                         head.Z + fwd.Z * kGrabLenCm };
            E::SetActorLocation(it->clump, hold);
        }
        // STREAM the clump's CURRENT pose (hand pos when carrying, physics pos when flying) to ALL peers so
        // every client renders the carry + the throw arc. Host-authoritative + host-originated (the relay
        // can't echo to the grabber; a client drives only slot 0). eid+ctx keyed -> the receiver's per-eid
        // ActiveDrive interp; ctx is the carry generation (stale-pose guard on the client).
        const ue_wrap::FVector  loc = E::GetActorLocation(it->clump);
        const ue_wrap::FRotator rot = E::GetActorRotation(it->clump);
        coop::net::TrashClumpPoseSnapshot snap{};
        snap.eid   = it->eid;
        snap.x = loc.X; snap.y = loc.Y; snap.z = loc.Z;
        snap.pitch = ue_wrap::NormalizeAxis(rot.Pitch);
        snap.yaw   = ue_wrap::NormalizeAxis(rot.Yaw);
        snap.roll  = ue_wrap::NormalizeAxis(rot.Roll);
        snap.ctx   = coop::trash_channel::CtxForEid(E);
        if (batch.size() < static_cast<size_t>(coop::net::kMaxTrashCarryBatchEntries))
            batch.push_back(snap);
        if ((sTick % 60) == 0)
            UE_LOGI("[TRASH-CARRY] HOST PUBLISH eid=%u slot=%u %s -> (%.1f,%.1f,%.1f) ctx=%u",
                    it->eid, it->slot, it->flying ? "FLIGHT" : "carry", loc.X, loc.Y, loc.Z,
                    static_cast<unsigned>(snap.ctx));
        ++it;
    }
    // Publish only when streaming, or ONCE to clear when a carry just ended (empty batch + g_wasStreaming).
    // At idle (no carry, was not streaming) skip the call entirely -> no per-tick session-mutex lock.
    if (!batch.empty() || g_wasStreaming) {
        s.SetLocalTrashCarryBatch(batch);   // empty -> the session stops fanning TrashCarryPose
        g_wasStreaming = !batch.empty();
    }
}

void OnPeerLeft(uint8_t slot) {
    for (auto it = g_held.begin(); it != g_held.end(); ) {
        if (it->slot == slot) {
            UE_LOGI("[PUPPET-DRIVE] OnPeerLeft slot=%u -- dropping held eid=%u", slot, it->eid);
            it = g_held.erase(it);
        } else { ++it; }
    }
}

void OnDisconnect() {
    g_held.clear();
    g_wasStreaming = false;
}

}  // namespace coop::puppet_carry_drive
