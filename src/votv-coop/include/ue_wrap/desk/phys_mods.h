// ue_wrap/phys_mods.h -- standalone engine access for the desk's PHYSICAL
// MODULES array (L8, v118). Principle-7 engine-wrapper layer: NO network
// logic; coop::physmods_sync drives the mirror through here.
//
// RE (2026-07-18 bytecode censuses, qf thread L8): physMods =
// TArray<TEnumAsByte<enum_physicalModules>> @0x12A0 on AanalogDScreenTest_C,
// fixed 12 slots, 0 = empty; the array is a SET (plugInModule's native
// dup-check denies a byte already Contained). Writers: plugInModule (find-free
// elem write + module prop K2_DestroyActor), the playerHitWith unplug path
// (module reborn INTO THE HAND via lib.physModToActor -> slot := 0), setData
// (save-load; calls updPhysMods), gatherData (save marshal). updPhysMods() =
// the parameterless consumer re-runner, measured a PURE function of the array
// (per-slot socket visuals + Contains-gated speed/lamp/shield effects; no
// player refs, no spawns, no audio) => mirror-safe reflected call.

#pragma once

#include <cstdint>
#include <string>

namespace ue_wrap::phys_mods {

inline constexpr int kSlots = 12;

// Resolve the physMods offset + updPhysMods on the desk class, the module
// base class (Aprop_physModule_C) and lib.physModToActor. Piggybacks
// console_desk::EnsureResolved for the desk class/instance. Throttled lazy
// retry; idempotent; game thread.
bool EnsureResolved();

// Read the live array into out[kSlots] (missing tail zero-filled; n = the
// engine array's Num, clamped). False when unresolved / no desk.
bool ReadArray(uint8_t out[kSlots]);

// Wholesale write of all kSlots elements into the EXISTING engine allocation
// (fixed-size CDO array; never realloc). False if the engine Num < kSlots.
bool WriteArray(const uint8_t in[kSlots]);

// Reflected desk updPhysMods() -- re-runs every consumer from the array.
// The caller holds the coop-side wire-apply guard.
bool CallUpdPhysMods();

// Is `cls` Aprop_physModule_C or a descendant (the per-byte module classes)?
bool IsModuleClass(void* cls);

// lib.physModToActor(byte) -> the module class for `byte` (null on fail /
// unresolved / byte unmapped). Reflected static-library call on the lib CDO.
void* ClassForByte(uint8_t byte);

// The byte a module CLASS encodes: reads the class's default object's module
// byte... NOT AVAILABLE statically-cheap; instead the reverse map is built by
// probing ClassForByte over the enum range once (cached). 0 = unknown.
uint8_t ByteForClass(void* cls);

// Session-end cache reset (instance pointers only; class-level persists).
void ResetCache();

}  // namespace ue_wrap::phys_mods
