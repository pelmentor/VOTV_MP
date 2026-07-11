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
// "None" unchanged (caller skips; BP UCS will mint on its own pass) --
// UNLESS `mintForAprop` is true.
//
// `mintForAprop`: force-mint even for Aprop_C lineage. Needed for the
// trash-pile collect (trashBitsPile::playerTryToCollect spawns an Aprop_C
// item AND auto-grabs it the SAME frame, before the BP UCS has minted a
// NewGuid Key -- so the held actor reads "None" and would never broadcast
// or match a held-pose). The item's BP setKey accepts our synth Key the
// same way the clump/chip classes do. Default false preserves the original
// "trust the UCS" behaviour for the normal Init-POST broadcast path.
//
// Game thread only -- calls ProcessEvent via ue_wrap::Call.
std::wstring EnsureKeyForBroadcast(void* self, const std::wstring& currentKey,
                                   bool mintForAprop = false);

// KEY-UNIQUENESS AUTHORITY mint (2026-07-11, take-3 RCA): force-mint a fresh
// unique Key onto `self` REGARDLESS of its current Key -- used by the host
// census when a second live actor is found carrying an already-indexed Key
// (VOTV's own save data ships clone families sharing one GUID; the identity
// layer assumes uniqueness). Format `rk_<64-bit-random-hex>` (19 chars, fits
// the 31-char wire key field; random -- unlike the cs_ counter format -- so a
// key PERSISTED into the save can never collide with a future boot's mints).
// Resolves setKey on the actual class first (chip/clump/trashBits declare
// their own), falling back to the Aprop_C base for prop descendants
// (FindFunction is exact-owner, [[lesson-findfunction-exact-owner-no-superstruct-climb]]).
// Returns the CONFIRMED re-read key on success, L"" on any failure (caller
// keeps the old key and logs). Game thread only.
std::wstring MintFreshKeyForDuplicate(void* self);

}  // namespace coop::prop_synth_key
