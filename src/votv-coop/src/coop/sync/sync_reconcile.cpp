// coop/sync/sync_reconcile.cpp -- see coop/sync/sync_reconcile.h.

#include "coop/sync/sync_reconcile.h"

#include "coop/pile_reconcile.h"
#include "coop/remote_prop_spawn.h"   // HasLoadTailQuiesced
#include "coop/save_identity_bind.h"
#include "ue_wrap/log.h"

#include <chrono>

namespace coop::sync {

namespace {
// Debounce for the steady-state reconcile: when work is pending we want to retire
// the stale twin promptly (so a grabbed save-pile's ghost@old does not linger),
// but not re-walk the GUObjectArray every tick. 250 ms is well under human-visible
// for a ghost to vanish and bounds the walk rate. Game-thread only -> a plain
// static is safe.
constexpr auto kSteadyReconcileDebounce = std::chrono::milliseconds(250);
std::chrono::steady_clock::time_point g_lastSteadyReconcile{};
}  // namespace

void RunIdentityReconcile(bool joinSweep) {
    // Order matters (preserved from the join one-shot tail it was extracted from):
    // twin-destroy BEFORE the position re-bind (a destroyed stale twin must not be
    // a re-bind candidate), and b3 pos-correction LAST (it resolves the now-bound
    // eid). Each step is internally bounded to armed/pending work, so a steady-
    // state pass with nothing pending is cheap (early-returns inside each).
    coop::pile_reconcile::SweepReconcileSaveTimeTwins();          // D1: retire stale native@old
    coop::save_identity_bind::BindUnboundReCreatesByPosition();   // variant-1: re-bind GC-churned natives
    coop::pile_reconcile::ApplyPendingPosCorrections();           // b3: snap window-moved piles
    if (joinSweep) coop::pile_reconcile::LogCensus();             // one-shot orphan census (join only)
}

void OnReconcileTick() {
    // Steady-state trigger (the D1 structural fix). Only meaningful after the join
    // load tail has drained -- before that, the join one-shot owns reconcile and a
    // mid-load native set is not yet trustworthy. Then: only when there is armed
    // reconcile work, and only on the debounce edge, do the (bounded) walk.
    if (!coop::remote_prop_spawn::HasLoadTailQuiesced()) return;
    if (!coop::pile_reconcile::HasPendingWork()) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - g_lastSteadyReconcile < kSteadyReconcileDebounce) return;
    g_lastSteadyReconcile = now;
    UE_LOGI("sync_reconcile: steady-state identity reconcile (pending work past quiescence -- "
            "a save-pile grabbed/moved after the join sweep; retiring its stale twin)");
    RunIdentityReconcile(/*joinSweep=*/false);
}

}  // namespace coop::sync
