// coop/sync/sync_reconcile.h -- the world-sync identity reconcile engine.
//
// The first piece of the consolidated coop::sync module (sync-consolidation
// refactor 2026-06-27, research/findings/sync-consolidation-refactor-PLAN-2026-06-27.md).
//
// THE D1 STRUCTURAL FIX (plan step 4b): the identity reconcile mechanisms
// (save-time twin retire, GC-churned-native re-bind, b3 position correction)
// were CONSUMED ONLY by the join-window one-shot RunDivergenceSweep_
// (remote_prop_spawn.cpp). A save-pile grabbed/moved AFTER the one-shot armed a
// pending twin that NOTHING consumed -> the stale native@old survived as a ghost
// dup (the 15:01:49 D1). This module makes the identity reconcile NON-one-shot:
// ONE entry (RunIdentityReconcile) driven by TWO triggers --
//   (1) the join-window quiescence sweep (as before), and
//   (2) a steady-state throttled tick (OnReconcileTick) that fires whenever
//       there is armed-but-unconsumed reconcile work past load-tail quiescence.
//
// What it does NOT own: the MEMBERSHIP doom sweep (destroy locals the host's
// snapshot didn't claim + the >50% valve) stays a join-window one-shot in
// remote_prop_spawn -- running it in steady state would wipe a legitimately-
// diverged world. Only the per-eid identity reconcile (bounded to armed work)
// is safe to run steadily, and that is what lives here.
//
// Game-thread ONLY (both triggers run on the game thread: the divergence sweep
// and the client reconcile tick). No mutex of its own.

#pragma once

namespace coop::sync {

// The identity reconcile sequence: retire save-time twins -> re-bind GC-churned
// unbound natives by save-position (variant-1) -> apply b3 position corrections.
// `joinSweep` = true at the join-window quiescence sweep (also logs the one-shot
// orphan census); false on a steady-state pass (no census spam). Game-thread only.
void RunIdentityReconcile(bool joinSweep);

// Steady-state trigger. Call every client reconcile tick. Past load-tail
// quiescence, when there is pending reconcile work (pile_reconcile::HasPendingWork)
// and the debounce interval has elapsed, runs RunIdentityReconcile(false). Cheap
// when idle: a pending-work bool poll + a time compare, NO GUObjectArray walk
// unless there is actually work (the perf rule). Game-thread only.
void OnReconcileTick();

}  // namespace coop::sync
