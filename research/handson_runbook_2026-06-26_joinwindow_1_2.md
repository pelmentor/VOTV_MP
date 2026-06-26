# Hands-on runbook — join-window #1 (kerfur turn-on dup) + #2 (pile-move dup), ONE run

**Date:** 2026-06-26
**Deployed DLL:** SHA256 head `FE87964A893A` — hash-verified MATCH on HOST + CLIENT + DEV.
**Build:** clean (Release). **Audit:** SHIP (both fixes; no defects). **push:** HELD.
**Commits:** #1 `39a381b0` (kerfur_reconcile), #2 `acc416eb` (save_identity_bind). Grab/throw chain already pushed (`eb85ddfb`).

## ⚠️ CAPTURE LOGS IMMEDIATELY after this run
The 12:02 raw logs were overwritten by later runs. **Right after you finish this test, BEFORE launching
anything else,** copy both logs so I can confirm the refinements:
- `Game_0.9.0n\WindowsNoEditor\VotV\Binaries\Win64\votv-coop.log`  (HOST)
- `Game_0.9.0n_copy\WindowsNoEditor\VotV\Binaries\Win64\votv-coop.log`  (CLIENT)
(or just tell me "done, logs saved" and I'll copy them.)

## What these fix (both = save-identity bind colliding with live host mutation DURING the join window)
- **#1:** host turns a kerfur ON in the connect window → the convert reliable-send fails mid-join (by design) →
  the retire fallback used to skip the stale off-prop because the bind marked it a host mirror. Now the sweep
  reconciles bound mirrors → the stale lying-down kerfur is destroyed.
- **#2:** host grabs+moves a pile in the connect window → the client's proxy correctly tracks the move, but the
  bind used to destroy it and bind the native at the OLD position. Now: if a convert touched the eid in-window,
  the proxy wins and the redundant stale native is retired → no dup.

## What to do (ONE scenario — exactly the 12:02 case)
1. Launch `mp_host_game.bat` (host) — your usual save (with off-kerfurs + chipPiles).
2. Launch `mp_client_connect.bat` (client). **While the client is still joining (the connect window, before it
   fully spawns in):**
   - On the **host**: turn a kerfur **ON** (interact with a lying-down/off kerfur to wake it). [tests #1]
   - On the **host**: also optionally turn one **OFF** (so there's a legit off-kerfur too — control).
   - On the **host**: **grab and move two chipPiles** to new spots (grab, carry, drop). [tests #2]
3. Let the client finish joining + settle (~10s).
4. On the **client**, look at the kerfurs and the moved piles.
5. **Then capture the logs (see top).**

## Acceptance
| # | Check | PASS = |
|---|-------|--------|
| 1 | Kerfur count | client shows the **same kerfurs as the host** — NO extra lying-down (stale) kerfur. The turned-on one is an active NPC on both; the turned-off one is a lying-down off-prop on both. |
| 2 | Moved piles | the two moved piles appear **once each at their moved position** on the client — NO duplicate / NO ghost at the old spot. |
| 3 | Regression (clean) | everything else looks normal: other piles/kerfurs in place, no missing/extra entities, world intact. |
| 4 | Regression (grab/throw) | after join, client can still grab a native pile (E), E-drop it, and LMB-throw it (the verified chain) — unaffected. |

PASS = #1 no extra kerfur + #2 no pile dup + #3/#4 nothing else broke.

## Log markers (I'll confirm from the captured logs)
- #1 success: CLIENT `kerfur_reconcile: retired bound-mirror stale off-prop ... (mirror-aware teardown)` and a
  `sweep-retire -- 1 of 1` (not `0 of 1`).
- #2 success: CLIENT `save_identity_bind: PROXY-WINS ... case(ii)-converted: pile grabbed/moved in-window` for the moved pile(s); no overflow-driven extra pile.
- Regression: bind summary still `bound 874/874` (or the run's total); no new `[Warn]/[Error]` from our DLL.

## FAIL signatures
- #1 still an extra lying-down kerfur + log `sweep-retire 0 of 1` → the bound-mirror still excluded / teardown didn't fire (tell me the eid).
- #2 still two piles for a moved one → CtxForEid didn't gate (tell me the host eid + whether you saw `PROXY-WINS`).
- Anything in clean-join/grab/throw broken → a regression (the isolation should prevent this; capture logs).

## Honest note
Autonomous smoke can't do "host mutates while client joins" (it's a timing-sensitive hands-on), and it's
rendering-blind. The fixes are RE-pinned + audited SHIP, but the dup-gone / no-regression verdict is your eyeball
+ the captured logs. On PASS this closes the last join-window class → push; then optionally (c) host-gate
generalization.
