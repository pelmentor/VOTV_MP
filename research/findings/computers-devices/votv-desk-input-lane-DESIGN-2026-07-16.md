# Desk INPUT lane + BUGS-v111 fix — converged design (2026-07-16, /qf 12 rounds "that holds")

The fix design for the 5 v111 desk bugs (`votv-desk-sim-v111-coop-bugs-audit-2026-07-16.md`,
signals TRACKER **BUGS-v111**). Produced by a 12-round `/qf` design pass (scratchpad
`qf_thread.md`; converged with a genuine critic "that holds" at round 12/15 — R10/R11 found
progressively less, every load-bearing noun measured). Status at write time: **DESIGN,
build starting**. One protocol bump: **111 -> 112**.

## The architecture in one line

Every desk scalar gets exactly ONE owner by CLASS: **INPUT** fields are shared last-writer
state on a new claim-free field-granular event lane; **OUTPUT** fields stay on the v111
host-auth stream (now with per-channel exact-snap); **COOLDOWN** is event-charged +
natively dt-decayed per peer. The v64/v111 claimed/unclaimed DeskState live lanes RETIRE
(RULE 2) in the same commit. ZERO new hooks — everything is poll + events + stream.

## D1 — the INPUT lane (bugs 1/2/3, the claim-axis fix)

**INPUT class (13 fields):** `DL_FrFilterSpeed`, `DL_poFilterSpeed`, `DL_activeFrFilter`,
`DL_activePoFilter`, `DL_PolarityDir`, `play_volume`, `play_selectIndex`, `comp_maxLevel`,
`active_play`, `active_download`, `active_coords`, `active_comp`, `coord_isPing`.
Write-site census complete (qf R1/R4, bytecode): presses/scroll surfaces only + two
TUTORIAL-only one-shot auto-flips + the ping-FSM end (presser-only). No oscillating sim
writer exists — the poll-edge misclassification class (v111's `dlDownloading>0` flap)
structurally cannot reappear.

- **Change detection:** 250 ms per-field poll diff over the 13 fields (surface-agnostic:
  E-press / scroll / mouse-click / keyboard / tutorial all mutate the FIELD; desk verbs are
  EX_Local* PE-INVISIBLE — the poll is the canonical detector per COOP_DISPATCH_VISIBILITY
  row "screen/panel verbs"). `ReadScalars` = 13 raw cached-offset reads — no dispatch, 4 Hz
  is negligible. Prime-on-first-sight (first read only primes, never sends).
- **Propagation:** new reliable `ReliableKind::DeskInput` — field-granular delta
  (field-id + value). Client -> host; host applies + relays to all EXCEPT the originator
  (echo to the author would revert a newer local value and prime over it — the eaten-scroll
  race, qf R2). Host's own deltas broadcast. Lane::Normal pinned (GNS orders within a lane).
- **ECHO-PRIME rule:** every wire-triggered mutation (delta apply, scan replay, adopt)
  primes the poll baseline for exactly the fields it touched — INCLUDING the cooldown
  jump-detector baseline — atomically in the same GT task (poll/drain/apply are all
  game-thread-serial). No echo storms, no re-broadcast of applied state.
- **Apply path:** read Scalars -> patch the ONE field -> existing `WriteScalars` (its full
  upd* repaint chain is the proven author). EXCEPT the 5 setter-managed fields: the four
  `active_*` power toggles apply via their native SETTER EVENTS (their handlers own
  light.SetVisibility + hum.SetActive + stopSound — uber [1113-1156]; raw writes leave
  mirror hums/lights dead, the no-raw-write-of-setter-managed-fields rule) and `play_volume`
  via `widget.setSignalVolume` (its native writer). Exact event-name resolution = impl
  measurement; fallback = reflected side-effect replication.
- **Teardown:** host tracks `pingSetterSlot` (originator of the last `coordIsPing=true`
  delta). On that peer's leave: host clears the field + broadcasts the falling edge
  (else every peer's OnKeyDown swallows keys forever). Full OnDisconnect resets
  baselines/primed/pingSetter/interp. NO suppression loans anywhere in this design.

## D-COOLDOWN (bug 1)

`coord_cooldown` LEAVES the DeskSimPose stream (it was ch7; the 10 Hz host overwrite was
the unwinnable client-charge erase). New model:
- **Charge:** unconditional charge event on any UPWARD poll jump, carrying the presser's
  observed value (native semantics — the verb ran there). All charging verbs are measured
  cooldown<=0-gated (`movePointToCursor` bytecode has the LessEqual gate + `getMaxCooldown`;
  scan gate at [2502-2508]) — no verb ever writes it downward, so the upward-jump detector
  is complete. Single source proven: the scan replay does NOT charge.
- **Decay:** native per-peer `VictoryFloatMinusEquals(coord_cooldown, DeltaSeconds)` + FMax 0
  ([2627-2629]) — dt-scaled, wall-clock-true at any FPS; gates reopen in phase.
- Native press-gates then work on EVERY peer; CDOWN log lines regenerate per-peer (the one
  kPrefixes survivor).

## D2 — per-channel exact-snap interp (bug 4)

`SimInterp` becomes per-channel `{target, err, deadline}`: a channel whose incoming target
is bitwise-unchanged KEEPS its deadline -> arrives -> `cur[i] = target[i]` EXACT snap; a
changed channel rebases err + deadline = now+150 ms. (The v111 shared window was reopened
by every packet — the exact 1.0 never landed; a whole-vector skip can never fire during an
active download because `decoded` moves every packet — qf R1.) The host latches bitwise-exact
1.0 (`FClamp(...,0,1.0f)` emits the literal bound, [133-135]) -> the snap delivers the exact
latch. Residual: the client's own unsuppressed needle inc can double-beep <=1-2 detector
pulses near the crossing (suppressing it needs DL_detectorMultiplier=0 loans — rejected).

## D3 — SHIFT scan + coordLog (bug 5)

- **Scan detection = the poll itself** (surface-agnostic; the OnKeyDown-hook variant was
  dropped in R7): an upward cooldown jump is classified SCAN iff
  `newValue > coord_maxCooldown/2 + 0.01` — the scan uniquely charges to the FULL
  `coord_maxCooldown` (dots charge exactly to `getMaxCooldown()/2`; ENTER ping never touches
  cooldown), threshold hitch-proof to ~2.49 s at min max=5 s. Verdict LOGGED with values
  (dead-guard rule). Scan -> also broadcast `ReliableKind::DeskScanEvent`.
- **Mirror apply = accepted-branch EFFECTS only:** reflected `spawnDirs()` +
  `playPingSound(newdesk_beepLong1)`, null-guarded on the atlas/ui_coordinates widget.
  NOT a `useSearch()` replay — the verb's own cooldown gate would refuse on decay jitter.
  spawnDirs bytecode-measured: reads only the wire-mirrored `signals_a` + widget refs, no
  RNG, no local-player read -> identical arrows on every peer. It is ADDITIVE (no
  ClearChildren) -> concurrent-scan double-arrows = documented cosmetic residual.
  The `<c>Initializing quick scan...` log line rides the producer; the charge rides the
  charge event. useSearch pak-wide census: 2 assets, 1 call site (atlas SHIFT branch).
- **kPrefixes shrinks to `{CDOWN: [}`** — the filter's regenerate-premise is measured FALSE
  for `CR:[` (gated on move_* input bools), `APPROXIMATION:`/`ANALYSIS:` (gated on the ping
  FSM) AND `AREA SCAN: [` (measured @43336-80: ALSO ping-FSM-gated, not scan — reclassified
  in R3). All four now ship via the existing 1 Hz producer (CR bounded <=~1 KB/s by the
  1000-char BP cap; wire line cap 120 chars truncate+warn safe-degrade).

## RETIRE in the same commit (RULE 2)

- The claimed 1 Hz `ScalarsDiffer` live stream, the unclaimed `DiscreteDiffer` edge lane +
  `DescribeDiscreteDiff` + `g_deskEdge*` machinery, and the v111 keep-local gate
  (console_state_sync.cpp:594-603).
- `OnDeskState` becomes ADOPT-only (the desk_diag `NoteJoinAdopt` hook stays — probes are
  RULE-2-exempt). The DeskState relay-whitelist row DELETES (clients never send DeskState
  again); `DeskInput` + `DeskScanEvent` relay rows ADD.
- Negative-grep-vs-known-positive verification of zero remaining consumers is part of the
  commit checklist.
- Unchanged: DishAimState (claim-gated 330 ms), the cursor pose lane, the 1 Hz log
  producer, CompState/CompData, SavedSignal 58/59 (SAVE/DELETE), the adopt/connect flow.

## Join/adopt correctness (code-verified, R10/R11)

Relays are `IsSlotWorldReady`-gated (session_relay.cpp:87) -> a pre-world joiner receives
no deltas; the adopt snapshot reads LIVE host fields at the ready edge (earlier deltas are
inside it); post-edge deltas follow the snapshot on the same Normal lane (per-lane FIFO);
edge + snapshot are one GT task. The adopt carries cooldown + speeds and primes ALL
baselines including the cooldown jump detector.

## Documented residuals (the acceptance record)

1. Sub-RTT same-control simultaneous writes cross: loser-side LOCAL state divergence
   (incl. play_volume = actual local audio level) persists until the next touch of that
   field; shared mechanics stay host-consistent (the sim reads HOST fields).
2. A delta arriving while the receiver's desk is unresolved (level transition) is dropped —
   stale until the next press. Inherited class (all desk reliable appliers share it).
3. Client needle double-beep <=1-2 pulses near the detector crossing.
4. Exotic cross-console click into the desk's screen buttons (operability requires
   interface mode by measured gates; documented, not designed for).
5. Scan mirror latency <=250 ms (arrows live seconds).
6. Concurrent-scan double arrows (additive spawnDirs; alpha-fade self-expiring).
7. Mid-session upgrade purchase divergence = the separate OPEN-3 workstream.

## Bug -> fix map

| Bug | Root (audit) | Fix here |
|---|---|---|
| 1 cooldown erased | ch7 host stream overwrite + both uplinks discard | cooldown out of the stream; charge events + native decay + native gates per peer |
| 2 client input dead + host stomp | claim-gated intent lane + full-blob flap rebroadcast | claim-free field-granular delta lane both directions; flap consumer deleted |
| 3 sounds dead | speeds on no lane; hums are setter-event-driven | speeds/toggles mirror via deltas (gauge loops are per-tick field-driven — wake); active_* via native setter events; no full-blob stomps |
| 4 stuck beep | interp window reopened per packet; exact 1.0 never lands | per-channel exact-snap interp; host latches bitwise 1.0 |
| 5 scan+coordLog unmirrored | no scan lane; kPrefixes premise false | scan by charge signature -> spawnDirs+beep replay; kPrefixes={CDOWN} |

Corrections queued for existing docs in the impl commit: chain-RE §6's "player_use/
playerUsedOn PE-visible" clause is WRONG (re-derived: interface calls are EX_Context+
EX_LocalVirtualFunction, INVISIBLE — matches COOP_DISPATCH_VISIBILITY row 101); the 07-16
audit's "AREA SCAN self-regenerates post-replay" line (AREA SCAN is ping-family).
