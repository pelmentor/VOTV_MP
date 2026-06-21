// coop/trash_collect_sync.cpp -- see coop/trash_collect_sync.h.
//
// One function: EnsureHeldItemBroadcast. net_pump's held-prop send calls it on
// the new-held edge. The collect itself (trashBitsPile_C::playerTryToCollect)
// is BP-internal (ProcessInternal) so it can't be observed; we instead act on
// the grabbing_actor the send already resolves -- a freshly-spawned, auto-
// grabbed Aprop_C with Key=None gets a stable Key + a PropSpawn here, then the
// existing pose stream mirrors it into the collector's hands.

#include "coop/trash_collect_sync.h"

#include "coop/kerfur_entity.h"  // K-5: IsKerfurActor (the held-kerfur class-gate)
#include "coop/pile_morph.h"     // v81 MORPH V2: the bind-model grab edge (replaces the v52 death-watch)
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/prop_element_tracker.h"
#include "coop/prop_synth_key.h"
#include "coop/remote_prop.h"        // ResolveMirrorEidByActor (the pile-grab hook mirror eid resolve)
#include "coop/remote_prop_spawn.h"
#include "ue_wrap/engine.h"
#include "ue_wrap/game_thread.h"     // RegisterPreObserver (the InpActEvt_use pile-grab observer)
#include "ue_wrap/log.h"
#include "ue_wrap/prop.h"
#include "ue_wrap/reflection.h"
#include "ue_wrap/sdk_profile.h"     // MainPlayerClass + MainPlayerUseInputEventFn
#include "ue_wrap/types.h"

#include <atomic>
#include <chrono>
#include <string>
#include <vector>

namespace coop::trash_collect_sync {
namespace {

namespace R  = ue_wrap::reflection;
namespace PT = coop::prop_element_tracker;
namespace GT = ue_wrap::game_thread;
namespace P  = ue_wrap::profile;

// Cached Session for the pile-grab observer (the PRE callback can't take a param). Set by Install
// (re-cached every call for reconnect). Game-thread read inside the observer.
std::atomic<coop::net::Session*> g_session{nullptr};
bool g_grabObserverInstalled = false;  // InpActEvt_use PRE registration latch (stays for process life)

// RULE 1+2 (2026-06-20, v81 MORPH V2): the CLUMP death-watch that used to live here (WatchedClump /
// g_watchedClumps / WatchClump / BroadcastConvertNear / TickWatchReleasedClumps) is RETIRED. It
// expressed a held clump as a FRESH-eid keyless PropSpawn then liveness-polled it to emit a fresh-eid
// PropConvert on land -- the destroy-and-recreate model the robust design condemns: a SECOND cross-peer
// entity per morph (the dupe), and the host's held clump was suppressed forever by the pre-quiescence
// guard so it never fired at all. Replaced by coop/pile_morph: the bind-model morph RE-SKINS the pile's
// eid E in place (pile-A -> clump -> pile-B, oldEid==newEid==E), anchored on the PROVEN held-object
// channel, with a deferred-destroy fallback. docs/piles/07-MORPH-V2-held-object-channel.md.
//
// NOTE (2026-06-17, RULE 1+2): the mirror-PILE death-watch that used to live here (g_watchedPiles +
// WatchPile/...) was RETIRED earlier. It inferred "grabbed" from "a watched pile's actor died NEAR the
// local camera", which is UNSOUND: a chipPile dies near the camera for many non-grab reasons (a peer
// bump, a physics nudge, a LifeSpan despawn), each misread as a grab -> a spurious PropDestroy -> the
// pile wiped on BOTH peers (the recurring "piles destroyed when touched a lot" bug). The DECISIVE RE
// (votv-pile-grab-observable-hook-RE-2026-06-08-pass1.md) proved the correct mechanism is OnPileGrabPre
// below: a PRE observer on the (ProcessEvent-visible) E-press InpActEvt_use that reads lookAtActor ->
// the pile, still ALIVE + eid-resolvable. It fires ONLY on a real grab (a bump/stream-out/physics-death
// is not an E-press). OnPileGrabPre now arms the pile_morph grab edge (carry + re-pile) with a
// deferred-destroy fallback that preserves the working grab->vanish.

}  // namespace

bool EnsureHeldItemBroadcast(void* heldActor, coop::net::Session* s) {
    if (!heldActor || !s || !s->connected()) return false;
    if (!R::IsLive(heldActor)) return false;
    // K-5 kerfur class-gate: a kerfur prop is a host-owned entity (the host owns its KerfurId + its
    // host-range eid). A client must NEVER express/mint a peer-range eid for it -- that was the
    // grab-dupe root (redesign Failures #1/#3/#6: the minted peer-range eid is dropped by the host's
    // host-range conversion gate, then re-mirrored as a fresh client entity -> the dupe-and-drop loop).
    // Never mint here; the held-pose stream (local_streams) instead carries the kerfur prop MIRROR by
    // its host-range eid (kerfur_entity::GetKerfurMirrorEidForActor), which the host's remote_prop
    // receiver resolves to the authoritative kerfur prop + kinematic-drives -- no PropSpawn needed. On
    // the HOST this is redundant (its kerfur prop is tracker-known -> the express skip below already
    // returns false), but gating up-front is role-agnostic + explicit.
    if (coop::kerfur_entity::IsKerfurActor(heldActor)) return false;
    // v81 MORPH V2 class-gate: a garbageClump is the bind-model morph's product -- pile_morph owns its
    // identity, claiming it at its Init-POST (TryClaimMorphProduct) where it re-skins the grabbed pile's
    // eid E onto the clump + suppresses the independent expression; the carry then streams under E. The
    // clump must NEVER be expressed here as a FRESH-eid keyless PropSpawn (the retired v52 death-watch
    // model = a second cross-peer entity = the dupe). If a clump reaches here it had no live grab to
    // claim (a stray pickup / a missed morph the deferred-destroy fallback handles) -> never author it.
    if (ue_wrap::prop::IsGarbageClump(heldActor)) return false;
    // Any KEYED interactable: Aprop_C trash items AND the non-Aprop_C
    // garbageClump/chipPile (the actual trash the player carries). CRASH-SAFETY
    // is enforced on the RECEIVER, not by excluding them here: GetStaticMesh
    // returns null for non-Aprop_C, so the peer never runs physics on the clump
    // mirror -- it spawns physics-free and is driven KINEMATICALLY (the inverse
    // of the reverted-2a UAF). [[project-bug-trash-chippile-uaf-crash]]
    if (!ue_wrap::prop::IsKeyedInteractable(heldActor)) return false;
    // PART 2 (2026-06-18) CLIENT host-authority gate for SHARED TRASH (chipPile / garbageClump /
    // trashBitsPile -- the non-Aprop_C "world litter"). These are HOST-OWNED shared entities. When a
    // CLIENT grabs a host pile, its BP locally morphs it to a clump (EX_CallMath, un-hookable) and this
    // path would SendPropSpawn that clump -> the host fresh-spawns a DUPLICATE, and on the clump's later
    // land-convert (BroadcastConvertNear, reachable only if we WatchClump below) the host spawns yet
    // another pile = "a new pile born from the host's original, from the host's perspective." The client
    // must NEVER author a shared trash entity -- the exact host-authority rule the K-5 kerfur gate above
    // enforces. Suppressing here ALSO disarms the clump-convert watch (WatchClump is only reached past
    // this return), so neither the clump PropSpawn nor the PropConvert ever leaves the client -> the dupe
    // is killed at the SOURCE. The client's grab already removed the host's original via OnPileGrabPre's
    // PropDestroy (eid-resolved on the mirror), so the original IS removed -- no orphan. The client holds
    // the clump locally; a client-THROWN ground pile staying client-local is a known phase-2 gap (the
    // host-request spawn model) -- a benign divergence, NOT a dupe. Aprop_C items (IsDescendantOfProp)
    // are unaffected (per-player carry stays). The HOST still authors its own held trash below.
    // (Audit M1, 2026-06-18: this suppresses all 3 shared-trash bases but OnPileGrabPre's host-original
    // PropDestroy covers only chipPile -- safe because the other two are never carried-as-a-host-mirror:
    // a trashBitsPile dispenses an Aprop item + a separately-synced counter (not a held clump), and a
    // garbageClump only ever exists as a chipPile's morph product, whose chipPile grab already fired
    // OnPileGrabPre. So no host original is ever left orphaned.)
    if (s->role() == coop::net::Role::Client && !ue_wrap::prop::IsDescendantOfProp(heldActor)) {
        UE_LOGI("trash_collect: CLIENT holds shared trash cls='%ls' -- NOT authoring it (host owns world "
                "piles/clumps; the grab's PropDestroy already removed the host original). No dupe.",
                R::ClassNameOf(heldActor).c_str());
        return false;
    }

    // A divergence sweep is pending and this held actor is one of its candidates
    // (an in-universe, unclaimed save-loaded local the host did NOT express -- a
    // ghost awaiting adjudication, e.g. the save-transfer kerfur the host turned
    // ON before this client joined). Do NOT express it: a SendPropSpawn below
    // would fresh-spawn a host duplicate, and the RecordClaimIfTracking after it
    // would claim the ghost past the pending sweep (permanently rescuing it). Let
    // the deferred sweep destroy it. A genuinely client-originated drop is claimed
    // via the takeObj path, so it is NOT a candidate and streams normally here.
    if (coop::remote_prop_spawn::IsPendingSweepCandidate(heldActor)) {
        UE_LOGI("trash_collect: held item %p is a pending divergence-sweep candidate "
                "(unclaimed ghost) -- NOT expressing (the deferred sweep will destroy it)", heldActor);
        return false;
    }

    // Pre-quiescence JOIN WINDOW (2026-06-16 hands-on, the "kerfur off+grab dupe"). The guard above
    // only fires once the divergence sweep is ARMED (g_sweepPending). But a save-loaded in-universe
    // keyed prop the client grabs in the ~25 s window BEFORE the snapshot bracket opens slips past it
    // (g_sweepPending still false) -- and expressing it here SendPropSpawns a host duplicate the
    // client-side sweep can NEVER reclaim (it ran: host fresh-spawned a kerfur prop, eid 43938,
    // ownerSlot=1, permanent). Same divergence-universe membership, just not yet sweep-armed: until
    // load-tail quiescence the world is NOT reconciled, so a grabbed un-adjudicated local is not ours
    // to announce. Keep holding it locally (no pop); the host expresses its OWN copy in the bracket
    // (our held copy is then key/fuzzy-matched + claimed -> the held-pose stream mirrors it, no dupe),
    // or the sweep destroys our copy if the host converted it away. HasLoadTailQuiesced flips true the
    // instant the sweep fires, so this gates ONLY the join window -- never steady-state gameplay.
    if (!coop::remote_prop_spawn::HasLoadTailQuiesced() &&
        coop::remote_prop_spawn::IsInDivergenceUniverseUnclaimed(heldActor)) {
        UE_LOGI("trash_collect: held item %p cls='%ls' grabbed PRE-QUIESCENCE (join window not yet "
                "reconciled) -- NOT expressing (host expresses its own / the sweep adjudicates ours; "
                "prevents the off+grab host dupe)", heldActor, R::ClassNameOf(heldActor).c_str());
        return false;
    }

    // Express-if-unknown (ghost-twin fix, 2026-06-10 hands-on forensics). The
    // old predicate skipped ANY keyed held actor as "a normal world prop the
    // peer already has" -- but "has a key" is NOT "peer has it": a prop that
    // materialized in the world-load's late tail (keyless at sweep/seed time,
    // key self-minted by its Init AFTER both) is keyed yet completely unknown
    // to the wire -- never expressed, never bound, eid=0. The user carrying
    // one was invisible to the host (127 'no local match' PropPose warns) and
    // the wall stack diverged one-way. The correct skip test is TRACKER-KNOWN:
    // a prop with a live Element (seed-walked / Init-announced / previously
    // expressed) is genuinely shared -- the pose stream alone mirrors it. An
    // untracked one gets expressed RIGHT HERE, key and all, the moment a
    // player first touches it -- the self-heal for any straggler class.
    // (The peer's OnSpawn may fuzzy-bind it to a nearby same-class prop; for a
    // genuine EXTRA actor that is bounded weirdness vs the strictly-worse
    // one-way invisibility. The structural fix for the late-tail stragglers
    // themselves is tracked separately -- see the sweep's keyless histogram.)
    std::wstring keyStr = ue_wrap::prop::GetInteractableKeyString(heldActor);
    if (!keyStr.empty() && keyStr != L"None" &&
        PT::GetPropElementIdForActor(heldActor) != coop::element::kInvalidId) {
        return false;  // keyed AND tracker-known: the peer has it; pose stream suffices
    }

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
    // v54: real scale (was hardcoded 1,1,1) + identity row below.
    const ue_wrap::FVector scl = ue_wrap::engine::GetActorScale3D(heldActor);
    p.scaleX = scl.X; p.scaleY = scl.Y; p.scaleZ = scl.Z;
    // Carry the trash VARIANT so the mirror shows the same chip/clump type the owner
    // grabbed (else a bare spawn defaults to variant 0 = the wrong-type bug). 0 for any
    // non-trash actor (GetChipType is a reflection no-op without a chipType property).
    p.chipType = ue_wrap::prop::GetChipType(heldActor);
    p.physFlags = coop::net::propspawn_flags::kSimulatePhysics;
    // IsHeavy/IsFrozen read Aprop_C struct offsets -- only meaningful on Aprop_C.
    // For a non-Aprop_C clump the receiver ignores physFlags (kinematic mirror),
    // so don't read stray bytes off it.
    p.propName.len = 0;
    if (ue_wrap::prop::IsDescendantOfProp(heldActor)) {
        if (ue_wrap::prop::IsHeavy(heldActor))  p.physFlags |= coop::net::propspawn_flags::kIsHeavy;
        if (ue_wrap::prop::IsFrozen(heldActor)) p.physFlags |= coop::net::propspawn_flags::kFrozen;
        if (ue_wrap::prop::IsStatic(heldActor)) p.physFlags |= coop::net::propspawn_flags::kStatic;
        if (ue_wrap::prop::IsSleeping(heldActor)) p.physFlags |= coop::net::propspawn_flags::kSleep;
        if (ue_wrap::prop::ReadRemoveWOrespawn(heldActor)) {
            p.physFlags |= coop::net::propspawn_flags::kRemoveWOrespawn;
        }
        // v54 identity: the list_props row -- a held GENERIC prop_C (e.g. a
        // cubicle gib panel) must mirror as itself, not the CDO 'cube'.
        const std::wstring nm = ue_wrap::prop::GetPropNameString(heldActor);
        for (size_t i = 0; i < nm.size() && i < 31; ++i) {
            p.propName.data[p.propName.len++] = static_cast<char>(nm[i]);
        }
    }
    p.initLinVelX = p.initLinVelY = p.initLinVelZ = 0.f;
    p.initAngVelX = p.initAngVelY = p.initAngVelZ = 0.f;
    {
        const coop::element::ElementId eid = PT::GetPropElementIdForActor(heldActor);
        p.elementId = (eid == coop::element::kInvalidId) ? 0u : eid;
    }
    UE_LOGI("trash_collect: BROADCAST held untracked item cls='%ls' key='%ls' loc=(%.1f,%.1f,%.1f) "
            "-- held-pose stream now mirrors it into the collector's hands",
            cls.c_str(), keyStr.c_str(), p.locX, p.locY, p.locZ);
    s->SendPropSpawn(p);
    // Fork B 2c: self-claim -- this peer just wire-expressed the held item;
    // an open bracket's sweep must not destroy it as "unclaimed".
    coop::remote_prop_spawn::RecordClaimIfTracking(heldActor);
    // (v81 MORPH V2: the former clump WatchClump enroll is GONE -- clumps return early above; the
    // bind-model pile_morph owns the clump's identity + its land conversion.)
    return true;
}

// ---- pile-grab destroy: the InpActEvt_use PRE observer (RULE-1 replacement for the death-watch) ----
//
// DECISIVE RE (votv-pile-grab-observable-hook-RE-2026-06-08-pass1.md, summary table): a chipPile grab
// is the E-press input action AmainPlayer_C::InpActEvt_use_..._41 -> icast(lookAtActor)->playerGrabbed
// (spawn clump + pickupObjectDirect(clump) + K2_DestroyActor(self)), all dispatched BP-internally
// (EX_LocalVirtualFunction / EX_VirtualFunction -> ProcessInternal) and INVISIBLE to our ProcessEvent
// detour. The ONLY ProcessEvent-observable edge with the pile STILL ALIVE + eid-resolvable is a PRE
// observer on InpActEvt_use (the native input -> UFunction dispatch DOES hit ProcessEvent -- the same
// seam door + kerfur-menu sync use), reading mainPlayer.lookAtActor = the pile under the crosshair
// BEFORE the ubergraph converts+destroys it. This fires ONLY on a real grab: a bump, a stream-out, a
// physics death, a LifeSpan despawn are NOT an E-press, so they can never enter this path -- which is
// exactly why it replaces the unsound proximity death-watch (the recurring "piles destroyed when a
// peer touches them" bug). Symmetric (BOTH roles): the owner re-grabbing its own pile resolves via the
// forward map (GetPropElementIdForActor); a peer grabbing a host-owned mirror pile resolves via
// remote_prop::ResolveMirrorEidByActor. Puppets are unpossessed -> never process input -> this only
// ever fires for the local player.
static void OnPileGrabPre(void* self, void* /*function*/, void* /*params*/) {
    if (!self) return;
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected()) return;  // BOTH roles -- whoever physically grabs
    void* aimed = ue_wrap::engine::ReadMainPlayerLookAtActor(self);  // the pile (PRE-conversion, alive)
    if (!aimed || !ue_wrap::prop::IsChipPile(aimed)) return;         // E-press not aimed at a pile
    // HANDS-FULL gate: if the player is ALREADY holding something (a carried clump/item), this E-press
    // cannot grab the aimed pile -- no morph will happen. Arming a morph (with its deferred PropDestroy
    // fallback) here would spuriously vanish an un-grabbed pile's mirror on peers. Skip. (PRE observer:
    // the held state is the player's PRIOR carry; the aimed pile's own morph hasn't run yet.)
    {
        ue_wrap::engine::MainPlayerGrabState gs{};
        if (ue_wrap::engine::ReadMainPlayerGrabState(self, gs) && (gs.grabbingActor || gs.holdingActor))
            return;
    }
    // Cross-peer identity: a pile WE own is in the forward map; a pile WE mirror is in the prop
    // MirrorManager. Either resolves THE shared host-minted eid the other peer keys its copy on. (The
    // morph's rebind routes on the Element's authoritative IsMirror() flag inside RegisterPropMirror, so
    // we no longer need to thread which map it came from.)
    coop::element::ElementId eid = PT::GetPropElementIdForActor(aimed);
    if (eid == coop::element::kInvalidId)
        eid = coop::remote_prop::ResolveMirrorEidByActor(aimed);
    if (eid == coop::element::kInvalidId || eid == 0u) return;  // untracked pile -> no peer mirror to drop
    // InpActEvt_use dispatches on BOTH the press AND the release of one tap; the press already
    // converts+destroys the pile so the release is a natural no-op (lookAtActor no longer the pile),
    // but collapse a same-eid repeat within a tap window defensively. One E-press grabs one pile, so a
    // single (eid,time) slot suffices -- no unbounded per-pile map.
    static coop::element::ElementId s_lastEid = coop::element::kInvalidId;
    static std::chrono::steady_clock::time_point s_lastTs{};
    const auto now = std::chrono::steady_clock::now();
    if (eid == s_lastEid && now - s_lastTs < std::chrono::milliseconds(300)) return;
    s_lastEid = eid;
    s_lastTs  = now;
    // v81 MORPH V2: arm the bind-model morph (carry + re-pile) on the pile's eid E. pile_morph claims the
    // morphed clump at its Init-POST (rebind E + PropConvert{ToClump} + suppress the clump's independent
    // identity). A deferred PropDestroy(E) fallback inside pile_morph fires if no clump is claimed in time
    // -- so this STILL produces the working take-17 grab->vanish when the morph can't sync (no regression),
    // and the carry/re-pile when it can (the upgrade).
    const ue_wrap::FVector pilePos = ue_wrap::engine::GetActorLocation(aimed);  // pile alive here
    coop::pile_morph::OnGrab(*s, eid, pilePos);
}

void Install(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);  // re-cache every call (reconnect)
    if (g_grabObserverInstalled) return;
    void* cls = R::FindClass(P::name::MainPlayerClass);
    if (!cls) return;  // mainPlayer_C not loaded yet -> retry on the next world-gated Install
    void* fn = R::FindFunction(cls, P::name::MainPlayerUseInputEventFn);
    if (!fn) {
        UE_LOGW("trash_collect: InpActEvt_use UFunction not found -- pile grabs cannot drop peer mirrors");
        g_grabObserverInstalled = true;  // permanent give-up (don't re-walk the class forever)
        return;
    }
    if (!GT::RegisterPreObserver(fn, &OnPileGrabPre)) {
        UE_LOGW("trash_collect: InpActEvt_use PRE observer register failed (table full?)");
        return;  // not latched -> retry next Install
    }
    g_grabObserverInstalled = true;
    UE_LOGI("trash_collect: pile-grab observer installed on InpActEvt_use (E-press on a chipPile -> "
            "pile_morph grab edge: carry+re-pile, deferred-destroy fallback)");
}

void OnDisconnect() {
    g_session.store(nullptr, std::memory_order_release);
    coop::pile_morph::OnDisconnect();  // clear the morph latches (pending-morph + land-watch)
}

}  // namespace coop::trash_collect_sync
