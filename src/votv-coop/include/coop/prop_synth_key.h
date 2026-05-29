// coop/prop_synth_key.h -- mint synthetic FName Key strings on non-Aprop_C
// keyed-interactables (chipPile / clump / trashBitsPile families).
//
// Aprop_C BP UCS auto-mints a NewGuid Key when ResetKey==true || Key==None.
// The 2026-05-27 IsKeyedInteractable extension brought in three non-Aprop_C
// classes whose BPs do NOT auto-mint (probe confirmed clump.GetKey returns
// FName(NAME_None) at fresh-spawn). Without a synthetic Key, the "None"
// guard in the Init POST broadcast path would silently drop every
// chipPile / clump / trashBits morph.
//
// Extracted from prop_lifecycle.cpp 2026-05-29 (M-1, post-E-2 ship). The
// machinery is single-caller (GrabObserver_Aprop_Init_POST_Body) and has
// no session / role dependency; pulling it out is a no-behavior-change
// modular cleanup. Keep this file under 200 LOC -- this is a single-
// concern utility.
//
// Synthetic format: `cs_<process-low32>_<monotonic-counter>` -- per-peer
// namespace via process-low32 + monotonic counter, so two peers minting
// concurrently can't collide on identity lookup.

#pragma once

#include <string>

namespace coop::prop_synth_key {

// Returns the (possibly-newly-minted) Key string for `self`. If GetKey is
// already non-None, returns it unchanged. Otherwise, for non-Aprop_C
// keyed-interactables, mints a synthetic + calls setKey + returns the
// minted string. For Aprop_C-derived actors with a None Key, returns
// "None" unchanged (caller skips; BP UCS will mint on its own pass).
//
// Game thread only -- calls ProcessEvent via ue_wrap::Call.
std::wstring EnsureKeyForBroadcast(void* self, const std::wstring& currentKey);

}  // namespace coop::prop_synth_key
