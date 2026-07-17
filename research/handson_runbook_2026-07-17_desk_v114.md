# Hands-on runbook — v112 INPUT + v113 DISHES + v114 CADDY/TASK + v115 AUDIO/CURSOR + v115b PING-FSM (batched), take 3

DEPLOYED: `votv-coop.dll 0926373e2a256beb` x4 (HOST + CLIENT_1/2/3), hash-verified
2026-07-17 evening. kProtocolVersion **115** (v115b is a BEHAVIOR-only layer on the same
proto; a 114-or-older peer HARD-CLOSEs at the gate). v115 smoke PASS x2 + e2e audio
self-test PROVEN (see take-2 history in git); v115b smoke PASS (both peers stable, client
connected, puppet spawned, no RAM breach; zero new-lane lines while nobody pings —
correct; zero new WARN/ERROR from the diff).
**NOTHING below is hands-on verified yet.** This take BATCHES FIVE unverified layers
(v112 + v113 + v114 + v115 + v115b); per-lane log prefixes keep attribution:
`desk_input:`/`desk_sim:` = v112, `dish_sync:`/`[dish]` = v113, `[reel]`/`[task]` = v114,
`desk_snd:`/`desk_cursor:` = v115, `FSM-hold`/`ping attribution`/`re-init window` = v115b.
The v112+v113 steps live in `handson_runbook_2026-07-16_desk_v113.md` (still current for
those lanes — run its STEPS first or interleave); THIS file adds the v114 half + the v115
half + the v115b ping half (take-2 was superseded mid-take: the user's LIVE ping test at
14:46-14:48 surfaced the PHANTOM ping-FSM — the v112 coordIsPing raw apply woke a parallel
sim on the host; fixed same-evening, design
`votv-ping-fsm-phantom-v115b-DESIGN-2026-07-17.md`).

## What changed in v115b (the phantom ping FSM fix)
1. Only the PRESSER's machine runs a ping now. The observer's machine never wakes (no
   phantom stage sounds from the peer who did NOT press Enter; no phantom ARM).
2. While a ping runs, the desk is CLAIM-HELD for the pinger (host FSM-hold): another peer
   mounting the desk mid-ping gets the native deny + is seated back out. It releases by
   itself when the FSM ends.
3. A successful catch is no longer stomped a second later (the false DISARM after the
   catch replay is suppressed while the signal data lives).

## STEPS (v115b half; needs a signal in range — repeat the 14:46 scenario)
1. CLIENT at unit 1: place 3 dots, Enter. WATCH the HOST's log: there must be NO
   `desk_snd:` pingSound stage cues authored by slot 0 while the client pings, and NO
   `dish_sync: host ARM broadcast` before the client's own verdict.
2. While the client's ping runs (~20-60 s), HOST tries to mount the desk: expect the
   native deny click + forced exit; host log `device_occupancy: desk FSM-hold asserted
   for pinging slot 1`, then `... released (ping ended, slot 1)` when the FSM finishes.
3. If the ping FAILS ("inner circle too small" / "no signal in local area"): satellites
   moving + the re-Enter block are NATIVE (each attempt costs) — the unit-2 DETECTOR must
   NOT rise on a local fail anymore.
4. If the ping SUCCEEDS: `signal_catch: catch replay applied` on the host, and within ~30 s
   `dish_sync: host ARM broadcast` (the real one) with NO `DISARM applied (machine reset +
   signal actor deleted)` in between on the client. The host log may show ONE `dish_sync:
   mesh down with live signalData ... DISARM suppressed` line — that is the re-init window
   working (root-3); it repeating per-poll would be a bug (it is log-once per episode).
5. Right screen during the ping: coordLog lines appear ONCE each (no double-author thrash);
   the messages' cadence should be smooth now. Re-judge the CURSOR smoothness in the same
   sitting (residual R-c: the presser-side ema doubling had no root yet — the phantom was
   a per-tick machine on the observer and may have been it).

## What changed in v115 (desk AUDIO mirror + cursor v2 — design `votv-desk-audio-mirror-v115-DESIGN-2026-07-17.md`)
1. The OTHER peer now HEARS your desk unit-1 activity: keyboard clicks (down AND up),
   the button/verb sounds (switch-cursor, ping key, SHIFT scan, 1/2/3 dot), the outcome
   beeps (ok beepLong1 / cooldown-denied beep4 / broken-radar fail), the cursor-movement
   loop while you hold move keys, and the ping loop. Screen-button MOUSE clicks carry the
   same sounds.
2. Cursor momentum crosses: flick the cursor, get off the unit — every peer's screen shows
   the same glide to the same rest position (the screen dims at dismount, as native).
3. Cursor smoothness: the mirror no longer snaps on desk release/re-claim, and the interp
   window adapts to the sender's real frame cadence (fps dips no longer staircase).
4. NOT covered (known residuals): the "Satellites are active" error beep (PlaySound2D
   static); the LAPTOP's own keyboard clicks (same class, future whitelist extension);
   ping FSM stage sounds/rings stay presser-local (pre-existing).

## STEPS (v115 half; any point during the v112-v114 steps at the desk)
1. CLIENT sits at unit 1, types arrows/WASD + 1/2/3 + SHIFT (incl. on cooldown). HOST
   stands NEAR the desk (not using it): expect clicks per key down/up, buttonquick1 +
   beepLong1 on a dot, beep4 on a cooldown denial, buttonlong5 on SHIFT, and the
   cursor-glide loop sound WHILE keys are held (silent during pure momentum glide —
   native behavior). Swap roles and repeat.
2. HOST clicks the desk's physical SCREEN BUTTONS with the mouse (bring/switch coord):
   CLIENT nearby hears buttonquick1/buttonlong4 + the outcome beep.
3. Momentum: CLIENT flicks the cursor (hold a direction ~1s), releases the key AND
   immediately gets off the unit. HOST watches the screen: the cursor keeps gliding to a
   stop (dimmed), SAME rest position on both screens (compare CR readout if in doubt).
4. Smoothness: CLIENT drives the cursor in circles ~30s; HOST watches for jerks. Then
   CLIENT gets off/on repeatedly mid-motion — no snap-back on the HOST's screen.
5. Ping loop: CLIENT starts a triangulation ping (ENTER with a valid triangle) — HOST
   hears the ping loop start; it ends on the catch/fail (pingSuccess already crossed
   pre-v115; now the loop + start beep do too).

## WHAT TO READ IN THE LOGS (v115)
- Both peers at boot: `desk_audio: class-level resolve complete` + `desk_snd: audio seams
  installed (Play=1 SetActive=1 Activate=1)` (once).
- On the OBSERVER during the other peer's typing: `desk_snd: applied op=.. comp=.. cue=..`
  (1-in-32 throttle — a few lines per burst, not per click).
- `desk_snd: seam counters /60s: ...` — Play/SetActive/Activate rates; deskHits should
  roughly track the presser's key rate while at the desk.
- Cursor: `desk_cursor: applying slot=.. ema=..ms` (~5s throttle while mirroring);
  `desk_cursor: momentum tail ended (settled, ..ms)` on the sender after a glide;
  `desk_cursor: claim FLAP` = the occupancy-flicker attribution WARN (should NOT appear
  in normal play; if it does, that is the OPEN-1 attribution we wanted).
- BAD signs: any `desk_snd: apply failed`, `desk_snd: fx ring overflow`, a `claim FLAP`
  storm, loop sounds STUCK ON after the presser leaves/disconnects.

--- (v114 half below, unchanged) ---

## What changed in v114 (L7, all BUILT — design `votv-tape-caddy-L7-impl-DESIGN-2026-07-17.md`)
1. The STOLAS tape caddy's reel slots sync: insert/eject on either peer mirrors (slot meshes
   + hover progress); the recording accrual converges to the HOST's value (1 Hz corrector —
   both peers keep ticking natively, the corrector snaps; sawtooth <= 1%/s is by design).
2. A CLIENT-ejected reel now EXISTS on the host/others (it used to be a local-only ghost):
   the host authors it at the eject moment; it follows the client's hands.
3. Reel props carry their Progress % cross-peer at BIRTH (eject, container, join window,
   pocket->place) — hover shows the same % everywhere.
4. saveSlot.taskNew (the daily task: requirements, sigCompleted, reel grades, reward fields)
   mirrors host->client a second after any change (drone sell / day rollover).
5. The caddy ON/OFF toggle already synced (appliance lane, v45) — regression-check only.

## STEPS (v114 half; do the v112+v113 runbook first)
1. Both peers walk to the STOLAS caddy (base interior wall unit).
2. HOST ejects the BIG reel (E on the slot; refuse-while-active means toggle OFF first —
   the toggle itself must mirror, both peers see the unit stop). CLIENT watch: the slot mesh
   empties <= 0.5 s; the reel appears in the host's hands and its hover shows the SAME %.
3. HOST re-inserts it. CLIENT watch: slot mesh returns; hover Progress resumes accruing
   (toggle back ON) and the % matches host's within ~1%.
4. CLIENT ejects the SMALL reel. HOST watch: slot empties; a reel EXISTS in the client's
   hands on the host's screen (expect 2-4 one-shot `no local match` WARN lines in the host
   log at that moment — bounded, by design L7-R3). Client DROPS it on the desk: it lands on
   both peers at the same spot with the same hover %.
5. CLIENT picks it back up (E), pockets it (hold-R), then PLACES it from the hotbar: the
   placed reel keeps its Progress % on BOTH peers (the correctness-audit CRITICAL 1 case —
   a blank-tape reset here is the bug we fixed; verify it stays).
6. CLIENT throws a reel INTO the caddy funnel (route 2 insert): the slot fills on both peers
   with that reel's %, the thrown prop vanishes on both.
7. Let the unit record ~2 min with both reels in: hover % identical on both peers (+-1%).
8. Day flow (if in the session's plan anyway): drone-sell both reels HOST-side loaded ->
   CLIENT's tablet task page shows reel grades update <= 1-2 s ([task] line in the client
   log). KNOWN residual L7-R1: a sack loaded BY THE CLIENT does not grade (container
   inventory unsynced until the sack-contents lane) — not a v114 regression.

## WHAT TO READ IN THE LOGS
- `[reel] local INSERT/EJECT edge ... -- broadcast` on the presser; `[reel] wire INSERT/
  EJECT applied` on the mirror. NO repeating [reel] lines while nobody touches the caddy
  (a >=1 Hz repeat = a poll-echo bug — flag it).
- `[reel] wire INSERT onto OCCUPIED slot ... HOST keeps own` = the R5 tiebreak firing —
  expected ONLY on a genuine simultaneous insert; if it repeats, flag.
- `[task] taskNew changed -- broadcast` host-side at sell/rollover; `[task] taskNew
  mirrored` client-side. More than a few per game-day = the change-hash is unstable — flag.
- `[PROP-DROP] CLIENT authored REEL-EJECT intent ... +savedScalar` at a client eject;
  `[PROP-DROP] HOST spawned client-placed prop` right after.
- The v113 watch-fors (cue-reconciler repeat line, ARM pre-clear WARN) still apply.

## HONEST STATUS
v114 = BUILT + audited + smoke PASS. NOT hands-on. v112 and v113 are ALSO not hands-on —
three layers stack in this take; if a desk/dish regression appears, attribute by prefix
before assuming v114.
