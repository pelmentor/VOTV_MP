// coop/util/array_growth_gate.h -- the ONE periodic-full-walk gate (L5 FPS fix, 2026-06-23).
//
// WHY: a dozen *_sync subsystems each rebuild an index by walking the whole GUObjectArray (~237k
// objects) on a 2s throttle. Run unconditionally that is ~5 full walks every 2s on the game thread =
// a periodic FPS stutter. A new object of interest can only appear when the array GROWS (a UObject is
// appended), so a rebuild is only needed when NumObjects() CHANGED since the last rebuild -- plus a
// periodic SAFETY rebuild to bound the free-slot-reuse coverage gap (a spawn reusing a freed slot
// leaves the count flat). This is the proven net_pump re-seed / atv-grime pattern, factored into ONE
// helper so every call site shares a single implementation (the user's "one principle, not one-at-a-
// time" -- two prior one-off gates left five other walks un-gated; this closes them together).
//
// Usage (one static gate per call site):
//   static coop::util::ArrayGrowthGate sGate;
//   if (coop::util::ShouldRebuild(sGate)) { ScopedWalkTimer t("foo:RebuildIndex"); RebuildIndex(); }
//
// Game-thread only (NumObjects + the static state are GT-serial, like every *_sync Tick).
#pragma once

#include "ue_wrap/reflection.h"  // NumObjects

#include <cstdint>

namespace coop::util {

struct ArrayGrowthGate {
    int32_t lastNum   = -1;  // GUObjectArray count at the last rebuild (-1 = never rebuilt -> first call rebuilds)
    int     sinceFull = 0;   // throttle-ticks since the last rebuild (the safety counter)
};

// True iff a full rebuild should run now: the object count changed since the last rebuild (structural
// change -> a new/removed object of interest), OR the periodic safety interval elapsed (bounds the
// free-slot-reuse gap). On a true return the gate is reset, so the caller must rebuild. `periodicEvery`
// is in throttle-ticks of the CALLER (e.g. a 2s throttle * 15 = a ~30s safety walk).
inline bool ShouldRebuild(ArrayGrowthGate& g, int periodicEvery = 15) {
    const int32_t cur      = ue_wrap::reflection::NumObjects();
    const bool    changed  = (cur != g.lastNum);
    const bool    periodic = (++g.sinceFull >= periodicEvery);
    if (changed || periodic) {
        g.lastNum   = cur;
        g.sinceFull = 0;
        return true;
    }
    return false;
}

}  // namespace coop::util
