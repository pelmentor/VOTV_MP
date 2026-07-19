// coop/kerfur_convert_client.h -- the CLIENT half of the kerfur conversion
// feature (extracted verbatim from kerfur_convert.cpp, 2026-07-19 s27 cut):
// conversion-ghost CUSTODY (claim/park -> cleanup-reap -> take-by-eid adopt)
// + the KerfurConvert wire APPLY (mirror destroy/adopt/materialize/restore).
// The DETECTION (death-watch poll + first-refusal seams) and the HOST executor
// stay in kerfur_convert.{h,cpp} / kerfur_convert_host.{h,cpp}; see
// kerfur_convert.h for the feature narrative + the kismet ground truth.
//
// Principle 7: gameplay/network module; engine access via ue_wrap only.

#pragma once

#include "coop/element/element.h"

#include <cstdint>

namespace coop::net {
class Session;
struct KerfurConvertBroadcastPayload;
}  // namespace coop::net

namespace coop::kerfur_convert_client {

// Session pointer for the wire-apply role gate. Called by kerfur_convert::
// Install EVERY attempt (top-of-call, before its resolve throttle) -- mirrors
// the residual's own per-call g_session.store, so a reconnect that changes the
// session pointer stays fresh here too.
void SetSession(coop::net::Session* session);

// Resolved kerfur class pointers (npc base / prop base / floppy). Pushed by
// kerfur_convert::Install every attempt right after its opportunistic resolve
// block (idempotent overwrite) -- values appear here as soon as they resolve,
// exactly as the pre-cut single-TU statics did (the DISABLED install state
// keeps the custody/claim machinery working, as before the cut).
void SetClasses(void* npcClass, void* propClass, void* floppyClass);

// Find + park the client's own just-spawned conversion ghost(s) near the
// conversion site (the poll's client branch drives this on a detected local
// toggle). See the custody narrative in the .cpp.
void ClaimConversionGhosts(uint32_t srcEid, bool wantNpc, float x, float y, float z);

// Reap parked ghosts (adopted/dead dropped; un-confirmed orphans destroyed
// after the timeout). Driven at the poll cadence. Cheap no-op when empty.
void CleanupParkedGhosts();

// CLIENT (D2 relay): TAKE (find + remove) the parked conversion-ghost tagged with `srcEid` of the requested
// form (wantNpc -> the parked turn-on NPC; !wantNpc -> the frozen turn-off prop), returning its actor or
// nullptr. The client's own toggle spawns a local kerfur via the un-hookable EX_CallMath path; the poll
// PARKS it (AI/physics off) tagged with the converting eid. npc_mirror::OnEntitySpawn (turn-on) and
// OnKerfurConvert adopt THAT exact actor by EXACT eid -- deterministic, replacing the old 500cm position
// match (FindParkedGhostNpcNear, removed v91). nullptr for a peer that did not initiate -> it fresh-spawns.
// Game thread.
void* TakeParkedGhostByEid(uint32_t srcEid, bool wantNpc);

// CLIENT-only receiver for KerfurConvert (v78, host->all; wired in event_dispatch_state, slot-0
// gated). The SOLE conversion-transition signal (kerfur redesign 10.3): destroy the old-form mirror
// at payload.oldEid, then ADOPT this peer's own claimed local conversion ghost to the authoritative
// payload.newEid (the initiator) or materialize a fresh mirror (other peers). rejected=1 -> the host
// refused (sentient): keep the mirror, or restore it if we optimistically converted locally (fixes
// Failure #7). `localPlayer` (live AmainPlayer_C*, may be null) feeds the prop teardown/materialize
// (held-prop guard + PHC release). Game thread (event_feed drain).
void OnKerfurConvert(const coop::net::KerfurConvertBroadcastPayload& payload, void* localPlayer);

// Clear the parked-ghost list. Net disconnect (fanned from kerfur_convert::OnDisconnect).
void OnDisconnect();

}  // namespace coop::kerfur_convert_client
