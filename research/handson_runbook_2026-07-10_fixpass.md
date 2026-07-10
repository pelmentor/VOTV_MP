# Hands-on runbook — 2026-07-10 fix-pass + probe (take 1)

**Supersedes** `handson_runbook_2026-07-09_F2inc1.md` (that build had the two rock-lane defects
below UNFIXED — do not test against it).

**Deployed:** DLL from HEAD (fix pass `db6ecd0b` + reorg `a0853315` + probe `7109efd1` +
garbage-gate fix); hash printed by deploy at documentize. Proto unchanged (107).
**Probe note:** `[dev] rng_roll_census=1` is ON in HOST + CLIENT_1 inis — your session
automatically accumulates the T1 RNG census ([RNG-CENSUS] lines; ~every 10 min a TICKERS/RESIDUE/
DUMP block). Leave it on unless something feels off; it is read-only.

## What changed since the 07-09 runbook (why rock is now testable)
1. F2 drop-intent CRITICAL: the hand view husk can no longer author a false intent (drain-time
   re-check) — pre-fix, EVERY hold-R pickup risked a host-side duplicate.
2. F2 drop-intent HIGH: PropDropIntent now rides the same GNS lane as PropDestroy — the
   "husk-destroy delivers before the intent" ordering actually holds under load.
3. serverbox: leaving a session now restores the local server-breaker (solo play breaks servers
   again).
4. Join safety: the world-load episode force-closes after 150 s even on the SnapshotBegin flake.
5. Remote peers' hand-item mirrors can no longer be adopted as phantom world props.

## Test steps (client = you, host = second box/window unless noted)
1. **F2 rock (the headline)**: as CLIENT, hold-R pick up a keyed rock -> walk -> hold-R place it.
   EXPECT: host sees the rock vanish at pickup and appear at the place spot; NO duplicate at the
   pickup spot, NO rock stuck mid-air at the hand. Log markers: `[PROP-DROP] CLIENT authored drop
   intent key=...` (client) -> `[PROP-DROP] HOST spawned placed prop` + a host PropSpawn broadcast.
   Also try: pick up, then QUICK-SLOT switch a few times while carrying, then place (the drain-time
   re-check path); pick up and NEVER place (expect: nothing spawns anywhere).
2. **serverbox**: host waits for / forces a server break. EXPECT client sees the same server down
   (map + box skin), no false "server down" on the client at other times, none at join. Then
   CLIENT DISCONNECTS GRACEFULLY (menu) and keeps playing solo: EXPECT
   `serverbox_sync: restored N ticker_serverBreaker` in the client log and servers CAN break in
   solo afterwards.
3. **email host-only**: client plays until a story email WOULD trigger client-side. EXPECT: host
   inbox receives ZERO client-authored emails; both peers' inboxes match the host's.
4. **peer-action chat**: either peer deletes an email. EXPECT chat line "<nick> deleted an email:
   <subject>" on all peers ("You ..." on the deleter). F1 > Cosmetics > Chat > "Peer action
   notifications" toggle off -> no line; toggle survives relaunch.
5. **(passive) census**: play >= one full in-game day at NORMAL speed (no clock acceleration —
   it breaks the census axes). The [RNG-CENSUS] blocks in both logs feed the T1 fork call.

## Honest status
Everything above is AS-BUILT + autonomous-smoke-clean ONLY (LAN connect, 0 [Error], watchdog
silent, RAM stable). None of the positive paths (an actual place, a graceful-disconnect restore,
an email delete) ran in the smokes — this runbook IS the verification.
