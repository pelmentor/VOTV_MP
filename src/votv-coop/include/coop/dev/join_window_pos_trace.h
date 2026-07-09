// coop/dev/join_window_pos_trace.h -- F1 join-window KEYED-prop POSITION trace (read-only, dev-only, CLIENT-side).
//
// THE QUESTION (docs/COOP F1 design, votv-F1-host-moved-prop-join-window-DESIGN-2026-07-09):
// the HOST moves a KEYED world prop (rock) DURING a client's join window; after the join the client
// renders it at the OLD SAVE position, not the host's moved position. The design's converged fix
// (generalize the b3 pile reconcile into ONE host-auth live-pos reconcile for keyed props too) is
// PROVISIONAL until this probe discriminates the root -- the two candidate roots need SEPARATING before
// any reconcile is built (PROBE-DON'T-GUESS, [[feedback-probe-dont-guess-rule]]):
//   (1) LOADOBJECTS-CLOBBER -- the snapshot delivers the host-moved pos FIRST, then the client's own
//       loadObjects (host-blob save-load) RECREATES the keyed prop at its SAVE pos AFTER, overwriting it.
//   (2) HOST-HELD-AT-SNAPSHOT -- the host was HOLDING the prop at snapshot time, so the snapshot
//       DRIVE-SKIPPED placing it (remote_prop_spawn.cpp :410/:423) -> a different root, different fix.
// The inferred hypothesis is (1) but the ORDER (snapshot vs loadObjects-recreate) is UNMEASURED; this
// separates them with a monotonic sequence stamp per keyed prop.
//
// THE TRACE (read-only, CLIENT-side): per keyed prop, in tick order, record
//   A) the snapshot expression (remote_prop_spawn.cpp exact-key branch): host-carried pos, the actor's
//      pos at that moment, whether the host held it (drive-skip), and a monotonic seq stamp.
//   B) the keyed-churn RE-BIND (join_membership_sweep.cpp): the loadObjects-recreate actor + its pos +
//      a seq stamp (proves the recreate came AFTER the snapshot when recreateSeq > snapshotSeq).
// At load quiescence (the sweep-fire point, alongside spawn_order_probe) resolve each key's FINAL bound
// actor by eid and classify: LOADOBJECTS-CLOBBER (final at save-pos, recreate after snapshot) /
// SNAPSHOT-WON (final at host-pos) / HOST-HELD-AT-SNAPSHOT (drive-skipped) / DEAD (grabbed/destroyed
// in-window -- covers the grab-during-window edge case for a rock, whose grab DESTROYS the actor) /
// UNRESOLVED. Observes + samples only; spawns nothing, binds nothing, mutates no game state.
// RULE-2-exempt diagnostic ([[feedback-rule2-exempts-probes-diagnostics-tools]]). Ini-gated
// [dev] join_window_pos_trace=1 on the CLIENT; absent/0 = every call is a cheap early-return.
//
// Piles are NOT probed here -- the b3 pile path already carries [PILE-B3] arm/apply logging; this probe
// answers the KEYED-prop half the design must confirm before generalizing b3.
//
// Game-thread only (the snapshot OnSpawn, the sweep re-bind, and the quiescence verdict are all GT).
#pragma once

#include <cstdint>
#include <string>

namespace ue_wrap { struct FVector; }

namespace coop::dev::join_window_pos_trace {

// True iff [dev] join_window_pos_trace=1 (latched once). When false, every call below is a cheap no-op.
bool IsEnabled();

// CLIENT join edge: arm the trace + clear the per-join record set. Called from BeginClaimTracking
// (SnapshotBegin), the same seam spawn_order_probe arms at. No-op when disabled.
void ArmForJoin();

// A) CLIENT snapshot expression (remote_prop_spawn.cpp exact-key branch): the host is expressing keyed
// prop `key`/`eid`; `hostPos` is the pos the snapshot carried, `actor` the resolved local actor,
// `hostHeld` whether the host held it (a drive-skip / local-grab-skip fired -> pos NOT placed).
// Behind IsEnabled it reads the actor's current pos. No-op when disabled / not armed.
void NoteSnapshotExpression(const std::wstring& key, uint32_t eid, void* actor,
                            const ue_wrap::FVector& hostPos, bool hostHeld);

// B) CLIENT keyed-churn RE-BIND (join_membership_sweep.cpp): the loadObjects-recreate of `key`/`eid`
// re-bound the mirror row onto `actor`. Behind IsEnabled it reads the recreate actor's pos (the
// candidate save-pos) + stamps the order. No-op when disabled / not armed.
void NoteRecreateRebind(const std::wstring& key, uint32_t eid, void* actor);

// CLIENT load quiescence (the sweep-fire point): resolve each key's final bound actor by eid, classify
// the root per key, emit the aggregate verdict, disarm. No-op when disabled / not armed.
void EmitVerdictAtQuiescence();

}  // namespace coop::dev::join_window_pos_trace
