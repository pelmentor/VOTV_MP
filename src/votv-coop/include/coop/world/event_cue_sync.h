// coop/event_cue_sync.h -- HOST-AUTHORITATIVE cosmetic emitter-cue mirror (v79; B1).
//
// The "sync all events" arc (research/findings/votv-all-events-coop-sync-classification-
// 2026-06-17.md, channel B1). Many of VOTV's scheduled/random/story events manifest ONLY as a
// one-shot cosmetic particle emitter spawned via `UGameplayStatics::SpawnEmitterAtLocation` --
// an EX_CallMath native call that BYPASSES our ProcessEvent detour (the same trap as fireflies)
// and leaves NO mirrorable artifact (no actor, no Key, no spawn UFunction). So a client sees
// nothing. The reported case is the Meteor Shower / Shooting Star ("starfall"):
// `trigger_eventer.runEvent('starRain')` -> `SpawnEmitterAtLocation(eff_shootingStar_rain,
// (0,0,6000))` (bytecode-verified, trigger_eventer @4709).
//
// Unlike fireflies (PEER-SYMMETRIC -- each peer rolls its own near its camera), these are WORLD
// events with a SINGLE producer: the HOST. Clients run a dormant scheduler (time_sync pins their
// TimeScale=0, the U6 gate) so they never fire an event themselves. So this is
// HOST-AUTHORITATIVE, not peer-symmetric:
//   - HOST (Tick, ~1 Hz, host+connected only): walk the live ParticleSystemComponents, keep the
//     ones whose `Template` is a registered cue, diff against the last poll. A NEW cue PSC means
//     the host just fired that event -> broadcast {cueId, pos} (EventCue reliable).
//   - CLIENT (OnReliable): replay the cue's emitter at the broadcast position via a reflected
//     SpawnEmitterAtLocation (the firefly spawn path).
// No suppression (clients never fire), no relay (host-only origin), no echo (the host never
// receives its own send, so it never double-spawns the emitter it already has).
//
// Connect snapshot (join-during-event Phase 2, docs/COOP_EVENT_JOIN.md 3.4): at a joiner's
// world-ready edge the host re-sends each LIVE ALREADY-BROADCAST cue PSC's {cueId,pos} ToSlot
// (the turbine_sync connect-snapshot shape) -- a mid-shower joiner replays the emitter fresh
// (emitter PHASE is not synced; the joiner sees the remaining shower from t=0 -- accepted for
// transient cosmetics). Only cues in g_lastCuePscs are re-sent: a cue newer than the last ~1 s
// poll is delivered by the next Tick broadcast instead (the slot's send gate is open by then);
// re-sending it here too would double the emitter on the joiner.
//
// The cue REGISTRY (in the .cpp) maps an emitter template -> cueId (+ an optional fixed spawn
// location for cues the BP hardcodes, like starRain). starRain is cue #0; Eye Moon / Pink Beam /
// TriFO / Blinking Lights / Green-Fire (all PSC-based cosmetic cues) are one registry line each.
// cueId is on the wire -> the registry is APPEND-ONLY.

#pragma once

namespace coop::net { class Session; struct EventCuePayload; }

namespace coop::event_cue_sync {

// Cache the session + opportunistically resolve the spawn path / cue templates. Idempotent,
// called every NetPumpTick from subsystems::Install. Detection lives in Tick(), not here.
void Install(coop::net::Session* session);

// HOST: the ~1 Hz authoritative detection poll (new cue PSC -> broadcast; kPollIntervalMs).
// No-op on a client / a solo peer. Game thread (driven from subsystems::TickGameplay, beside
// the other host pollers).
void Tick();

// CLIENT: replay a cue's emitter at the host's broadcast position. Game thread.
void OnReliable(const coop::net::EventCuePayload& payload);

// HOST: join-during-event Phase 2 -- re-send every live, already-broadcast cue PSC's {cueId,pos}
// to a joiner at its world-ready edge (subsystems::ConnectReplayForSlot). One bounded PSC walk
// (join edge is rare); cues NOT yet in the poll snapshot are skipped -- the next Tick broadcast
// owns those (see the header note). No-op on a client / when no cue template resolved. Game thread.
void QueueConnectBroadcastForSlot(int peerSlot);

// Teardown: clear the poll snapshot + drop the session pointer.
void OnDisconnect();

}  // namespace coop::event_cue_sync
