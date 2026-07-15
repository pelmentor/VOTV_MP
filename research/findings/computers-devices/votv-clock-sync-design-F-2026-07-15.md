# Clock sync — design F (unreliable snapshot, client stays a pure mirror) — 2026-07-15

**Status: AS-BUILT (v110, `2dde3e16`, deployed but NOT hands-on-verified).**
Converged via a `/qf` pass (scratchpad `qf_thread_clock.md`). Supersedes the reliable-periodic model.

## Problem (user-reported)
The in-game clock (HUD + the physical alarm-clock prop) showed slightly different times on host vs
client — "slight desync."

## Measured root (not a skew — a refresh-cadence gap)
- The client `daynightCycle` runs at `TimeScale=0` — a **pure host-authoritative mirror, frozen**
  (measured `time_sync.cpp` `ClientTimeScale()`). It does NOT free-run.
- The load-bearing invariant is **"client `totalTime` never wraps `MaxTime`(=24.0 game-hours, measured
  `_daynight_uber_full.txt @17479`) locally"** → the midnight `newDay` cascade (`days_total++` save-stat
  @4347, weekday, seasonal-date) is structurally unreachable client-side; those fire host-side and mirror
  via the email/order channels.
- The desync was purely that the **reliable 2 s `TimeSync` correction refreshed the frozen mirror slower
  than the HH:MM display granularity** (~1.5 game-min lag at a long day). It is a sawtooth by construction
  (a frozen clock can't hold a constant offset vs a moving one; each correction re-zeroes it).

## Rejected alternative (candidate D — killed by /qf)
"Run the client clock + CLAMP `totalTime` just below `MaxTime` so it never wraps." WRONG:
1. Re-introduces **client-side simulation of a SHARED quantity**, then patches authority back out with the
   clamp = lateral / regression from the clean pure-mirror model.
2. The clamp is a per-broadcast **site-list** (`newDay` handled, `newMinute`/`newHour` trusted) — the next
   shared mutation hung off `newMinute` slips through = a RULE-1 skip wearing a clamp.
3. Measured: smooth sun REQUIRES advancing `totalTime` through the native `ReceiveTick`, which fires
   `newMinute`/`newHour`/`newDay` **regardless of who moved the clock** → "advance for display only" is
   impossible without firing the delegates or re-deriving the sun yourself.

**Provenance note:** "the clamp LEAKS `newMinute` events" is INFERRED-strong-but-UNMEASURED — `newMinute`
BROADCASTS (@3196) so consumers exist, but `func_newMinute`'s body + the broadcast's binding set were NOT
read (the `days_total++` mutation is on `newDay`, not `newMinute`). Immaterial for F (frozen client);
a HARD build gate if continuous-sun is ever revisited.

## Design F (as-built)
Keep the pure mirror (client never simulates). Fix only TRANSPORT + CADENCE:
- `MsgType::ClockPose=37`: HOST→all **unreliable** absolute clock snapshot (`ClockPosePacket` =
  `PacketHeader` + the reused `TimeSyncPayload`, 44 B), newest-wins by header seq, ~500 ms net-thread
  throttle (independent of pose sendHz), not relayed. The correct transport for periodic idempotent
  absolute state (pose/cursor-stream pattern) — a dropped snapshot is corrected by the next; no ARQ load.
- `Session::SetHostClock` (host GT publishes each `time_sync::Tick`) / `TryGetHostClock` (client GT drains,
  apply on isNew). Shared `ApplyClockSnapshot` forces `ClientTimeScale()` (never `payload.timeScale`) so
  the frozen-mirror invariant holds.
- Reliable `TimeSync`(29) kept ONLY for the connect-edge guaranteed initial sync; the reliable **periodic**
  push REMOVED (RULE 2, no dual path).
- Wire change → `kProtocolVersion` 109 → **110** (a mixed pair HARD-CLOSEs at the gate).

## Scope + open
- Fixes the **clock** (minute-discrete display).
- The **sun** still steps at the 500 ms refresh (unreported; truly-continuous sun is the heavier "advance
  the client clock + VM-suppress the `newMinute`/`newHour`/`newDay` broadcasts" path, deferred — and gated
  on the `newMinute`-consumer enumeration above).
- **NOT hands-on-verified.** Take (2 peers, one look): host + client show the **same HH:MM at a paused
  moment**; client log shows a one-shot `applied CONNECT-EDGE host clock` on join then `applied STREAM host
  clock` ~every 10 s; NO reliable-`TimeSync` spam.

Lesson: `[[lesson-frozen-mirror-desync-is-transport-not-authority]]`.
