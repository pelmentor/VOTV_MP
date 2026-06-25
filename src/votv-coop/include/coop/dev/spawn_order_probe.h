// coop/dev/spawn_order_probe.h -- Phase 1 step 1A: spawn-order correlation PROBE (read-only, dev-only).
//
// THE QUESTION (docs/COOP_STABLE_ID_SIDECAR.md S3.2, Phase 1 build plan S0/1A): can the client bind each
// save-loaded KEYLESS native (chipPile | off-prop kerfur) to the host's index->eid map entry by SPAWN ORDER,
// i.e. "the client's k-th keyless spawn == the host's k-th keyless objectsData entry"? Both peers load the
// SAME blob, and loadObjects (bytecode loadObjects_dump.txt [223-291]) spawns per-index in a SYNCHRONOUS
// loop via BeginDeferredActorSpawnFromClass -- the UFunction the mod already native-hooks
// (trash_collect_sync.cpp:76 OnBeginDeferredSpawnObserve, below the BP VM). So order follows from the
// bytecode IF the thunk catches EVERY keyless load-spawn. The one empirically-unproven thing is therefore
// COVERAGE: does the thunk fire for every SURVIVING keyless native?
//
// THE PROBE (self-contained, no struct_save layout RE): during the client join, record every keyless-family
// BeginDeferred spawn the thunk sees (by actor ptr). At load quiescence (the sweep-fire point), walk the
// GUObjectArray for the surviving keyless natives and check each was recorded. Verdict per family:
//   - CAUGHT-ALL (0 survivors missed) -> spawn-order is the deterministic PRIMARY (build plan S4.1).
//   - MISSES (>0 survivors never fired the thunk) -> fall to the EXACT-transform bijection (S4.2/S3.3).
// Read-only: it only OBSERVES + COUNTS + walks; it spawns nothing, binds nothing, mutates no game state.
// RULE-2-exempt (a diagnostic; [[feedback-rule2-exempts-probes-diagnostics-tools]]). Ini-gated
// [dev] spawn_order_probe=1; absent/0 = every entry point is a cheap early-return.
//
// Game-thread only (the thunk + the sweep tick are GT; the recorded set is GT-touched only).
#pragma once

#include <cstdint>

namespace coop::dev::spawn_order_probe {

// Families the index->eid map covers (build plan S1). Keyed forms bind by key -> not probed here.
enum class Family : uint8_t { ChipPile = 0, KerfurOff = 1 };

// True iff [dev] spawn_order_probe=1 (latched once). When false, every call below is a cheap no-op.
bool IsEnabled();

// CLIENT join edge: arm the probe + clear the recorded set. Called from BeginClaimTracking (SnapshotBegin),
// the same client-join seam that resets the census. No-op on host / when disabled.
void ArmForJoin();

// CLIENT BeginDeferred thunk: record a keyless-family load-spawn by actor ptr. Called from
// OnBeginDeferredSpawnObserve's client branch (before the host gate). No-op when disabled / not armed.
void NoteKeylessSpawn(void* newActor, Family family);

// CLIENT load quiescence (the sweep-fire point in TickClientReconcile): walk the surviving keyless natives,
// check each was recorded, emit the per-family verdict, disarm. No-op when disabled / not armed.
void EmitVerdictAtQuiescence();

}  // namespace coop::dev::spawn_order_probe
