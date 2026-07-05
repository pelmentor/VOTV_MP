// coop/world/alarm_sync.h -- the base radar alarm as a shared-world toggle (docs/events/alarm.md).
//
// Ground truth (bytecode, 2026-07-05): ONE trigger_alarm_C per map (gamemode key 'alarmTrigger').
// ON = analogDScreenTest's radar sweep hitting an important comp_radarPoint -> runTrigger(radar, 1);
// OFF = the panel_radar "b/Stop alarm" press -> runTrigger(self, 0). runTrigger is natively
// IDEMPOTENT (IntToBool(index) == active -> no-op) and fans out the WHOLE alarm: alarms[] lamp
// setActive, the Audio1 klaxon loop, the basementGrate prop toggle, ceilingLamp solar() flicker,
// and lib_C::setEvent (the native activeEvents registry -> no-save gate).
//
// Both callers dispatch runTrigger EX_VirtualFunction -- ProcessEvent-INVISIBLE
// (docs/COOP_DISPATCH_VISIBILITY.md: "every screen/panel device verb -> POLL the state field").
// So this lane POLLS trigger_alarm_C.active at 1 Hz on BOTH peers:
//   - HOST: observed transition -> broadcast AlarmState to all (the canonical fanout);
//   - CLIENT: observed LOCAL transition (its own scan fired early / its player pressed Stop)
//     -> send AlarmState to the host; the host applies natively and its poll broadcasts;
//   - receive -> apply = reflected runTrigger(nullptr, active) on the local trigger (we
//     dispatch ProcessEvent ourselves, so BP-internal invisibility is irrelevant); native
//     idempotency breaks every echo loop.
// Late-join answer (COOP_EVENT_JOIN.md 3.4): the host sends the CURRENT state to a joiner
// unconditionally at the world-ready edge -- a mid-alarm joiner starts its klaxon on arrival.

#pragma once

namespace coop::net {
class Session;
struct AlarmStatePayload;
}  // namespace coop::net

namespace coop::alarm_sync {

// Cache the session. Resolution (class + active offset + runTrigger) is lazy in Tick.
void Install(coop::net::Session* session);

// Per net-pump tick, game thread, ~1 Hz internally throttled, BOTH roles (see header comment).
void Tick();

// HOST, game thread, at a joiner's ClientWorldReady edge: send the current state to this slot.
void QueueConnectBroadcastForSlot(int slot);

// Game thread (event_feed drain): a peer's alarm state. Client applies; host applies and lets
// its own poll do the canonical broadcast.
void OnReliable(const coop::net::AlarmStatePayload& payload, int senderPeerSlot);

// Test/dev seam: post a game-thread native runTrigger(active) on the LOCAL trigger -- the
// lane's own poll then detects + broadcasts, so an e2e exercises the shipping path
// (harness/autotest_alarmforce; RULE-2-exempt diagnostics).
void DevForce(bool active);

// Teardown: drop the cached trigger + baseline, clear the session.
void OnDisconnect();

}  // namespace coop::alarm_sync
