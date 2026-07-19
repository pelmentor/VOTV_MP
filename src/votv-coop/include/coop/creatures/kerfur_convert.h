// coop/kerfur_convert.h -- host-authoritative kerfur NPC <-> prop conversion
// (v67, 2026-06-12). Fixes the user-reported dupe: "client sees a turned off
// kerfur-object, client turns it on, client turns it off - now host sees 2
// kerfurs lying turned off."
//
// GROUND TRUTH (kismet disassembly, research/findings/votv-kerfur-convert-RE-
// 2026-06-12.md):
//   - turn OFF: radial menu -> kerfurOmega_C::actionName(Player, Hit, "turn_off")
//     [cross-object, ProcessEvent-VISIBLE] -> ubergraph `if (kill) return;` ->
//     dropKerfurProp() [LOCAL self-call, PE-invisible]: spawns the floppy prop
//     (if hasFloppy, with data/readWrites copied), spawns the `dropProp` class
//     (Aprop_kerfurOmega_C skin variant) at the NPC's transform (or Z=20000 in
//     the flesh room), copies `sentient`, K2_DestroyActor()s the NPC. A SENTIENT
//     kerfur refuses (audio blip only -- no spawn, no destroy).
//   - turn ON: radial menu -> prop_kerfurOmega_C::actionOptionIndex(.., Action==8)
//     [PE-VISIBLE] -> spawnKerfuro() [LOCAL]: BeginDeferred+FinishSpawning the
//     `spawnKerfur` NPC class at spwn+50Z (yaw only); on success K2_DestroyActor()s
//     the prop, on failure shows a hint and the prop SURVIVES.
//
// WHY IT DUPED: every spawn/destroy inside the verbs is BP-INTERNAL
// (EX_CallMath BeginDeferredActorSpawnFromClass; by-name K2_DestroyActor) --
// none of it dispatches through ProcessEvent, so our interceptors/observers
// never saw it (zero npc-suppress[client] / npc-sync[host]-broadcast lines in
// every real-session log). A client's conversion ran fully LOCAL (untracked
// rogue kerfur, then an untracked local prop), and even a HOST conversion
// spawned an NPC no client could see until the next connect-edge world walk.
//
// THE FIX (MTA request shape -- DoorOpenRequest precedent):
//   - CLIENT: PRE-interceptors on the two PE-visible menu dispatchers CANCEL the
//     local conversion and queue a request; Tick() (game thread) resolves the
//     WIRE eid off the actor's mirror Element (the host id -- the local tracker
//     only holds a peer-range shadow) and sends KerfurConvertRequest{eid,
//     toProp}. (Cancel-at-the-script-fn is the ambient_spawner_suppress
//     precedent; the verbs themselves are the v44 EX_LocalVirtualFunction trap
//     -- not hookable.)
//   - HOST: the request handler resolves eid -> actor, validates (live, right
//     class, the BP's `kill` guard replicated from the disassembly), executes
//     the REAL verb via ProcessEvent, then CONVERGES the BP-internal side
//     effects the pipelines could not see:
//       actor !IsLive       -> npc_sync::SyncDestroyedNpcActor /
//                              prop_lifecycle::SyncDestroyedPropActor
//       new prop (+ floppy) -> ExpressSpawnedProp over a targeted GUObjectArray
//                              walk (untracked prop_kerfurOmega_C- /
//                              prop_floppyDisc_C-descendants)
//       new NPC             -> npc_sync::RegisterExistingWorldNpcs (broadcasts
//                              EntitySpawn for newly-registered while connected)
//   - HOST's OWN menu use: the dispatch passes through (the BP converts
//     natively); the conversion is then detected EVENT-DRIVEN at the fresh
//     prop's expression edge (TryAdoptFreshKerfurProp -- the generic prop
//     pipeline gives the kerfur layer first refusal), with the ALIVE->DEAD
//     death-watch poll as the 5 Hz backstop (solo-host / seam-missed spawns).
//
// Principle 7: gameplay/network module; engine access via ue_wrap reflection /
// game_thread only. One feature, own file pair.

#pragma once

#include "coop/element/element.h"  // ElementId (TryCaptureKerfurPropDestroy dyingEid)

#include <cstdint>

namespace coop::net {
class Session;
struct KerfurConvertPayload;
struct KerfurConvertBroadcastPayload;
}  // namespace coop::net

namespace coop::kerfur_convert {

// Idempotent install, retried each net-pump tick until the kerfur BP classes
// load: resolves kerfurOmega_C::actionName + prop_kerfurOmega_C::actionOptionIndex
// (+ their 'name' / 'action' param offsets) and registers the two PRE
// interceptors; resolves dropKerfurProp / spawnKerfuro for the host-execution
// path + the `kill` field offset for the replicated guard + the prop/floppy
// classes for the converge walk. Caches `session`.
void Install(coop::net::Session* session);

// The HOST executor half (OnConvertRequest + ConvergeAfterConversion + the
// request-verb bracket / ActiveRequestVerbEid) lives in kerfur_convert_host.h
// (s27 cut).

// The CLIENT half (the KerfurConvert wire apply + the conversion-ghost custody:
// claim/cleanup/TakeParkedGhostByEid) lives in kerfur_convert_client.h (s27 cut).

// Drain the deferred-action queue (pushed by the interceptors, which may run
// on a parallel-anim worker and must not call engine functions or walk the
// element registries): client entries resolve the wire eid off the actor's
// mirror Element + send the request; host entries converge -- one full tick
// AFTER they were pushed (two-phase arming; a nested dispatch inside the
// conversion verb can drain this queue mid-verb otherwise). Cheap no-op when
// empty. Game thread (net-pump tick).
void Tick();

// HOST: FIRST REFUSAL on the generic expression of a kerfur PROP-form actor (take-8
// 2026-07-12 host-own toggle dupe RCA). The turn_off verb's fresh prop spawns EX-internally;
// since spawn_authority Inc-1 (2026-07-10) the generic pipeline (FinishSpawningActor seam
// drain + census incremental express) claims and PropSpawn-broadcasts it within one tick --
// the death-watch poll's converge then finds it TRACKED, gives up, and never broadcasts
// KerfurConvert: the client keeps its NPC mirror AND gains a generic prop mirror = the dupe.
// EVERY generic express lane must therefore offer a kerfur prop HERE before broadcasting:
// if the actor is UNTRACKED and a dead, un-handled kerfur NPC watch sits within 5 m, this IS
// the conversion product -- converge it now (mint the eid silently, release the dead NPC
// element, BindFormActor -> KerfurConvert broadcast, floppies) and return true (caller must
// NOT express). Otherwise (tracked = established identity; hand-place; purchase) return false
// -- the generic keyed-prop path is correct for it. The kerfur is ONE entity: KerfurConvert
// is its sole conversion wire signal (redesign 10.3). Host + game thread only (no-op else).
bool TryAdoptFreshKerfurProp(void* actor);

// BOTH ROLES: FIRST REFUSAL on the generic DESTROY of a kerfur PROP-form actor (take-9
// 2026-07-13 turn-on pair RCA) -- the destroy-edge twin of TryAdoptFreshKerfurProp. The
// turn-on verb spawnKerfuro is SPAWN-then-DESTROY (kismet: BeginDeferred+FinishSpawning the
// NPC -> IsValid -> K2_DestroyActor self), so when the prop's destroy seam fires INSIDE the
// verb the conversion-product NPC already exists ZERO ticks old -- a SYNCHRONOUS premise no
// periodic enroll lane can beat. If a fresh unowned kerfur NPC sits within the spawn radius,
// this death IS conversion churn the kerfur layer owns; the caller (destroy seam) must NOT
// broadcast PropDestroy:
//   CLIENT: suppress the keyed-destroy relay (take-9 bug 1: the relay killed the host's
//     authoritative prop BEFORE the turn-on request landed -> "no live prop" -> the kerfur
//     deleted on every peer). The poll's request+ghost machinery stays the client driver.
//   HOST: converge INLINE at the destroy edge -- RegisterHostNpcSilent + BindFormActor ->
//     ONE KerfurConvert broadcast, no generic PropDestroy (take-9 bug 2: the host's own
//     turn-on had NO converge at all -- this same seam drains the prop Element synchronously
//     with the actor death, so the poll's "element present + actor dead" premise NEVER holds
//     for a host prop; the client kept a stale off-prop + never saw the NPC).
// No adjacent fresh NPC -> false: a genuine destroy (hold-R into hand, incinerator) keeps
// the generic relay. `dyingEid` = the seam's pre-Unmark element id (kInvalidId -> host
// declines: no wire identity to converge). Game thread (the destroy seam).
bool TryCaptureKerfurPropDestroy(void* actor, coop::element::ElementId dyingEid);

// Clear per-session state (the poll watch + the poll throttle) and fan the
// disconnect to the client/host halves (parked ghosts; the request bracket).
void OnDisconnect();

}  // namespace coop::kerfur_convert
