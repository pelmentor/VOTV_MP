// coop/world/event_active_sync.h -- the native ACTIVE-EVENT registry mirror (join-during-event).
//
// PHASE 0 (probe, read-only -- no wire): docs/COOP_EVENT_JOIN.md section 3.5. The game keeps its
// own in-flight event registry on mainGamemode_C: `activeEvents` (int refcount) +
// `activeEvents_senders` (TArray<UObject*> of the live event actors), single writer
// lib_C::setEvent (RAII-style self-registration by ~95 event classes; bytecode-verified,
// research/findings/votv-active-events-registry-RE-2026-07-04.md). setEvent itself is a lib_C CDO
// call from BP internals (EX-dispatch, hook-invisible) -- but the poll reads the RESULT, so no
// hook is needed (the passEvents-poll shape from event_fire_sync, proven at v95).
//
// This module (host-side, ~1 Hz):
//   - polls activeEvents_senders, diffs membership by object identity, and edge-logs
//     `event_active: BEGIN/END class=<sender class> n=<refcount>` with per-event elapsed time;
//   - at a joiner's world-ready edge, logs what an EventSnapshot WOULD carry
//     (class + elapsedSec per in-flight event) -- the exact payload Phase 1 puts on the wire.
//
// Phase 0 exists to prove the seam on real events (probe-don't-guess) before any wire:
// Phase 1 = ReliableKind::EventSnapshot + joiner replay with the InClientPassEvents
// active-override for replay-safe rows; Phase 2 = event_cue join snapshot + the full
// class->list_events-row map. The per-lane late-join contract table lives in
// docs/COOP_EVENT_JOIN.md section 3.4.

#pragma once

namespace coop::net {
class Session;
}  // namespace coop::net

namespace coop::event_active_sync {

// Cache the session. Resolution (gamemode class + the two property offsets) is lazy in Tick.
void Install(coop::net::Session* session);

// Per net-pump tick, game thread, ~1 Hz internally throttled. HOST + connected only:
// senders membership diff -> BEGIN/END edge log. No-op on the client (a joiner has no local
// event actors; the registry is delivered per-lane / per-snapshot, never counter-mirrored --
// accepted asymmetry, COOP_EVENT_JOIN.md section 3.3).
void Tick();

// HOST, game thread, at a joiner's ClientWorldReady edge (subsystems::ConnectReplayForSlot):
// log the would-be EventSnapshot for this slot (Phase 0 -- log only, nothing sent).
void LogJoinSnapshotForSlot(int slot);

// Teardown: drop the tracked membership + baseline + cached gamemode, clear the session.
void OnDisconnect();

}  // namespace coop::event_active_sync
