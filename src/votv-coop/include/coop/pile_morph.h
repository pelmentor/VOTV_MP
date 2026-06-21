// coop/pile_morph.h -- bind-model pile MORPH (v81 MORPH V2).
//
// The grab->carry->throw->land sync for ambient trash piles (actorChipPile_C). The pile is bound to the
// host-minted eid `E`; the morph RE-SKINS E in place across the three UObjects pile-A -> clump -> pile-B
// (oldEid==newEid==E), so there is never a second cross-peer entity, no death-watch-as-identity, no doom.
// A missed morph can NEVER regress the working grab (a deferred PropDestroy(E) fallback fires if the
// clump is never adopted in time).
//
// THE SEAMS (both PROVEN to fire -- the dead Init-POST observer the take-18 attempt bet on does NOT fire
// for BP-deferred morph products: host_spawn_watcher.cpp:132 "these classes' Init is BP-internal (never
// fires our prop_lifecycle Init-POST observer)"):
//   GRAB  the morphed clump becomes mainPlayer.holding_actor -> local_streams' new-held edge calls
//         TryAdoptHeldClump. Nothing else expresses the clump (Init-POST dead, EnsureHeldItemBroadcast
//         class-gated off, the host re-seed rejects garbageClumps), so binding E + PropConvert{ToClump}
//         is the SOLE expression -- no dupe.
//   LAND  the thrown clump's death is polled in Tick; the re-piled chipPile is found by position
//         (owner-side, sound). The re-piled chipPile IS a keyless pile the host re-seed would express,
//         so the land claim suppresses that (MarkKnownKeyedProp) when it binds E, and if the re-seed
//         already won the race it cleans up instead of duplicating.
// docs/piles/07-MORPH-V2-held-object-channel.md. Game-thread only.

#pragma once

#include "coop/element/element.h"   // ElementId
#include "ue_wrap/types.h"          // FVector

namespace coop::net { class Session; }

namespace coop::pile_morph {

// E-press on a chipPile (pile STILL alive). Arms a single-slot pending-morph for eid `E` and schedules a
// deferred PropDestroy(E) fallback (~400 ms) -- if the morph clump is NOT adopted by then (the grab
// produced no clump, e.g. it failed), the fallback fires so the peer's mirror pile still vanishes (the
// take-17 behaviour, no regression). `pilePos`: the grabbed pile's world position (the new-held clump
// must spawn near it to be adopted). A new grab supersedes a stale pending -- and fires the stale one's
// destroy so it is never leaked. Caller MUST skip this when the player's hands are already full.
void OnGrab(coop::net::Session& s, coop::element::ElementId eid, const ue_wrap::FVector& pilePos);

// local_streams new-held edge: the local player just started holding `heldActor`. If a pending-morph is
// live and `heldActor` is the morphed clump (a garbageClump near the grabbed pile), rebind E onto it in
// place (routed on the Element's authoritative IsMirror() flag), broadcast PropConvert{ToClump,E}, CANCEL
// the deferred destroy, and arm the land-watch. Returns true iff it adopted (the caller then skips
// EnsureHeldItemBroadcast -- the convert + the held-pose stream own the clump).
bool TryAdoptHeldClump(coop::net::Session& s, void* heldActor);

// Per-tick (game thread): (a) fire an expired deferred-destroy fallback; (b) poll each land-watch -- while
// its clump is live, track its position; on the clump's morph-death, find the owner's nearest re-piled
// chipPile and broadcast PropConvert{ToPile,E} (binding E onto it + suppressing the host re-seed), or
// PropDestroy(E) if the clump despawned without re-piling / the re-seed already claimed the pile. Cheap
// when idle (early return).
void Tick(coop::net::Session& s);

// Session teardown: clear the pending-morph + land-watch state.
void OnDisconnect();

}  // namespace coop::pile_morph
