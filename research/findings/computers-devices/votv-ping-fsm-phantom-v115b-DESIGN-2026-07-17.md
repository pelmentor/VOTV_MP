# v115b — the phantom ping FSM (coord_isPing raw-apply retired) + companions (2026-07-17)

Design of record for the v115b BEHAVIOR fix (no wire-format change; kProtocolVersion stays
115). Diagnosed live during the user's v112-115 batched hands-on via a 3.5-round /qf
(thread archived in the session scratchpad `qf_thread_ping_fsm.md`); built the same
afternoon under the user's directive "root-1/2 safe, write; root-3 verify during impl".

## The reports (user live-test, 14:46-14:48)

1. Client placed dots + Enter -> "Ping failed inner circle too small" / "Ping failed no
   signal in the local area"; each failed attempt moved the satellites; re-Enter blocked
   by "Satellites are moving". User asks: bugs?
2. DESPITE the error, the signal "got caught anyway" -- unit-2 DETECTOR STATUS rose.
3. The mirror cursor turns jerky "after some time", and the right-screen messages arrive
   in the same jerky cadence.

## Root cause (measured, logs + bytecode)

**The native ping FSM is a LATENT TICK MACHINE, not an Enter-verb** [MEASURED,
analogDScreenTest uber]: `@82980 IFNOT(coord_isPing) POP` gates the stage engine at
`@80105` (switch on `coord_pingStage`; per-stage `VictoryFloatPlusEquals(coord_ping_ping,
dt * rate)`; the rate reads `widget.ui_coordinates.innerCircleRadius` + the save's
`upg_coordPingSpeed`); stage transitions are `coord_ping_* == 1.0` latch checks INSIDE the
gated loop (`@79979`, `@80028`). The AREA SCAN display chain (`@43326`, a separate uber
entry) is gated on the SAME flag (`@43336: coord_isPing && pingStage != 3`).

v112's DeskInput lane mirrored CoordIsPing as a raw field write (`ApplyField ->
sc.coordIsPing -> WriteScalars`), and the DeskState join-adopt snapshot carried it too
(second wire path). **The wire write WOKE a phantom parallel FSM on every observer** --
the `lesson_smooth_mirror_wakes_dormant_per_peer_generation` class. Measured episode:

- 14:46:14 client (presser) Enter -> coordIsPing=1; the delta crosses.
- 14:46:24 CLIENT log: `desk_snd: applied cue='newdesk_panelCoord_stageChange' from slot
  0` -- the HOST's machine played a ping stage cue: the phantom was RUNNING on the host.
- The two FSMs were PHASE-MISALIGNED (the phantom started at its own stage 0 mid-client-
  run; ring progress is per-FSM time state) -> divergent verdicts are STRUCTURAL. The
  client's runs failed (native verdict strings); the phantom SUCCEEDED.
- 14:47:38 HOST: `dish_sync: host ARM broadcast` -- the phantom armed the host machine
  (mesh valid). The client's unit-2 detector view mirrors the HOST machine ->
  `needle` rose from 14:47:39 == report 2 ("error, but it detected anyway").
- 14:47:57 the client's REAL attempt succeeded (`pingSuccess` + `signal_catch: catch
  replay applied ('brownDwarf_0')`).
- 14:47:58 the catch replay's `ResetDownloadMachine` made the host mesh transiently
  invalid -> HostArmPoll read it as an organic disarm edge -> **false DISARM broadcast
  stomped the fresh catch** (client: "machine reset + signal actor deleted"). Host
  re-armed 14:48:22 (latent display respawn ~24 s).
- Right-screen jerks: BOTH peers shipped console_state desk-log lines simultaneously
  (client 220/min + host 93/min at 14:46) -- double-FSM = double text authorship. The
  cursor APPLY cadence was measured CLEAN (applying-line spacing steady 5 s = 60 Hz all
  session); the cursor-jerk residual is the presser-side production ema 17->33 ms during
  its own FSM episodes -- re-judge at the next take.

Native-vs-bug split (report 1): fail verdicts, satellites moving on ANY attempt, and the
"Satellites are moving" re-Enter block are NATIVE semantics; the detector rising during a
local FAIL and the post-success stomp were ours.

## The fix (one machine per ping: the PRESSER's, organically)

Shape chosen over "host-owned FSM + park the presser" because (a) parking kills the
presser's own display (the run-flag and the display gate are the SAME field), (b)
exact-snap output-mirroring is impossible for this machine (the ==1.0 latches fire on
snapped values -- `lesson_mirrored_threshold_latch_needs_exact_snap` does NOT extend to
event-firing terminal branches), (c) observers already see no ping visuals (residual R2)
so nothing is lost, and (d) the FSM's world inputs are host-truth VIA MIRRORS already
(dots v112, dish theater v113, signals table host-owned, upgrades same save).

1. **Root-1 (the wake)**: CoordIsPing wire deltas are BOOKKEEPING ONLY at receivers
   (desk_input_sync OnDeskInput early-return: host tracks `g_pingSetterSlot`, rising sets
   / same-slot falling clears / leaver clears; PatchScalar lost the CoordIsPing case as
   defense in depth). The DeskState join-adopt never copies the flag (PayloadToScalars
   forces false -- a joiner cannot be mid-ping; the payload byte stays as diagnostics).
   RULE 2: the OnPeerLeft/OnDisconnect machine-field clears died with the raw write that
   made them necessary (no peer's machine can hold a wire-applied TRUE anymore).
2. **The FSM-hold (replaces the accidental key-swallow)**: the retired raw write had one
   accidental benefit -- observers' native OnKeyDown swallowed desk keys during a remote
   ping. Replacement at the claim layer: device_occupancy's host reconciler (250 ms
   throttle at the top of Tick) keeps a REAL busy-table entry `desk -> pinger` while
   `PingActiveSlot() != 0xFF`. Every existing surface then does the work: BusyByOther's
   local immediate-deny + ForceExitLocal (a peer mounting mid-ping is seated out with the
   native deny), the arbitration's held-by deny, and OnDishAim's holder gate (a
   non-holder's aim never ships -> the presser's committed dots can't be stomped
   mid-FSM). `g_deskFsmHold` marks the entry FSM-authored: a physical grant of the desk
   (the pinger sat back down) clears the mark and hands the entry over; a physical
   release mid-FSM is re-asserted by the next reconciler tick; ping end releases only a
   still-marked entry (never tears down a sitting player).
3. **Root-3 (the DISARM stomp)**: HostArmPoll gained the RE-INIT WINDOW predicate --
   `!mesh && prevMeshValid && signalKey present` is a catch-replay's latent display
   respawn, NOT a disarm (a real disarm deletes signalData FIRST: the @33832 chain / the
   kind=1 replay's ClearCoordSignal-then-Reset order). The poll holds its baselines
   through the window (log-once), so the eventual respawn fires the real ARM edge (or
   arm-over-arm via the key change on a same-poll respawn). Chosen over a
   flag-from-the-applier: the one-shot flag missed slow respawns (measured ~24 s) and ate
   the legit ARM on fast ones; the predicate is state, not timing.
4. **(B)-text**: single authorship of the right screen emerges from the single machine --
   no code change needed beyond root-1 (the phantom's lines were the second author).
5. **CRIT-1 (correctness audit, fixed pre-commit)**: a SOLO host's ping rising edge is
   absorbed by the unwired baseline (desk_input Tick's !connected() branch never diffs)
   -> attribution stayed 0xFF -> a client joining mid-host-ping got NO FSM-hold.
   Fix: `SeedPingAttributionFromMachine()` at ConnectReplayForSlot -- re-derive from the
   machine's ground truth (coord_isPing TRUE with no wire author can only be the host's
   own FSM); a live wire attribution is never clobbered. The falling edge self-heals
   (the absorbed TRUE baseline diffs false on the first connected poll).

## Residuals (named)

- R-a: observers still see no ping stage visuals (pre-existing R2; unchanged).
- R-b: the FSM-hold covers claim-mediated surfaces; the 7 world-space screen buttons
  bypass claims natively (they bypass the native swallow in SP too -- native parity).
- R-c: cursor-jerk during the presser's own FSM episodes (production ema 17->33 ms) --
  unexplained residual; re-judge at the take now that the phantom (a per-tick uber
  machine on the observer) is gone.
- R-d: a peer force-exited by the FSM-hold gets the native deny click but no textual
  "ping in progress" hint (observers can't see the remote ping -- R-a).

## Verification state (as-built 2026-07-17 evening)

- Build Release clean (warnings pre-existing). Final DLL `0926373e2a256beb` deployed x4
  (hash-verified all folders; the interim `8c12a42d` build predates the CRIT-1 fix).
- Perf audit: PASS, 0 CRITICAL 0 WARN (function-by-function table; ReconcileDeskFsmHold
  steady state = plain reads + one time compare, zero alloc/mutex/engine calls; all new
  log sites edge/latch-gated; no new walks; no 800-LOC crossings).
- Correctness audit: 1 CRITICAL (the solo-host mid-ping join hole, confidence 85) --
  FIXED pre-commit (part 5 above); all other traced interleavings (disconnect ordering,
  self-claim override reachability, reinit-window disarm order, PayloadToScalars
  consumers, no-bump reasoning) verified clean by the auditor.
- Smoke #1 (DLL 8c12a42d): PASS -- both peers stable, client connected, puppet spawned,
  no RAM breach; ZERO new-lane lines while nobody pings (correct); zero new WARN/ERROR.
  Smoke #2 (final DLL): see the session log tail.
- PROCESS LOSS: the smoke relaunch rotated the peers' live logs -- the user's 14:4x raw
  hands-on logs are gone (lesson_copy_peer_log_before_relaunch violated); every
  load-bearing line is quoted here + in the /qf thread.
- NOT hands-on. Stacks as the FIFTH unverified proto layer (v112+v113+v114+v115+v115b);
  the runbook take 2 carries the ping scenario (dots -> Enter -> fail/success on the
  SAME peer only; observer detector follows the presser's machine via the host mirror;
  no phantom stage sounds from the non-presser; catch not stomped at :58-style).
