# Hands-on runbook — join-window #1 (kerfur) + #2 (pile-move dup) + b2 + **b3 (dropped-convert moved-pile position)**

**Date:** 2026-06-26 (b3 run)
**Deployed DLL:** SHA256 head `BA9459985119` — hash-verified MATCH on HOST + CLIENT + DEV (proto **v90**).
**Build:** clean (Release). **Audit:** SHIP (b3; 3-place wire-in complete, no clean-join/b2/#1/carry regression). **push:** HELD.
**Commits:** #1 `39a381b0`, #2 `acc416eb`, b2 `2829ce6d`, **b3 `10284a8a` (PropSnapPos)**. Grab/throw chain already pushed (`eb85ddfb`).

> **#1 + #2 (real bugs: dup, identity) are hands-on VERIFIED (15:42).** **b2 (16:42)** verified the position snap
> for piles whose move-convert was DELIVERED (eid 2309, drift=0). **b3 is the new fix** for the remaining tail:
> a pile the host moves EARLY in the window — before the joiner's reliable channel is ready — has its move-convert
> DROPPED, and chipPiles carry no position in the connect-snapshot, so the native stuck at the old save spot
> (eid 3144 at 16:42, fixed itself only on grab). b3 has the host deliver that pile's current position at
> world-ready; the client snaps the bound native at quiescence — **so the moved pile should now be at its moved
> position on join with NO interaction.** #1/#2/b2 must stay PASS.

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

## What to do (ONE scenario — move piles EARLY so the convert is DROPPED = the b3 case)
1. Launch `mp_host_game.bat` (host) — your usual save (with off-kerfurs + chipPiles).
2. Launch `mp_client_connect.bat` (client). **As EARLY as you can in the connect window** (right after the client
   starts joining — the earlier the better, that's what drops the convert and is exactly the 3144 case):
   - On the **host**: **grab and move two-or-three chipPiles** to clearly different, far-apart spots (grab, carry,
     drop). Do this FIRST and FAST, while the joiner is still early in its load. [tests **b3** — dropped convert]
   - On the **host**: turn a kerfur **ON** (wake a lying-down/off one). [tests #1]
   - On the **host**: optionally turn one **OFF** (a legit off-kerfur control).
   - If you can, ALSO move one more pile a bit LATER in the window (convert likely delivered) — that exercises the
     b2/#2 path too. Optional.
3. Let the client finish joining + settle (~10–15s; b3 applies at the quiescence sweep, a few seconds after world-ready).
4. On the **client**, look at the moved piles (b3) and the kerfurs (#1) — **without grabbing anything yet**.
5. **Then capture the logs (see top).** THEN you may grab/throw to confirm the interaction path still works.

## Acceptance
| # | Check | PASS = |
|---|-------|--------|
| 1 | Kerfur count | client shows the **same kerfurs as the host** — NO extra lying-down (stale) kerfur. The turned-on one is an active NPC on both; the turned-off one is a lying-down off-prop on both. |
| 2 | Moved piles (dup) | the two moved piles appear **once each** on the client — NO duplicate / NO ghost (this is #2, already verified; must hold). |
| 2b | **EARLY-moved piles (POSITION — b3)** | each pile you moved EARLY (convert dropped) renders **at its MOVED position** on the client **BEFORE you grab it** — NOT back at the old spot. *This is the whole point of b3.* The 16:42 failure was this pile sitting at the old spot until grabbed; it should now be correct on join. |
| 3 | Regression (clean) | everything else looks normal: other piles/kerfurs in place, no missing/extra entities, world intact. |
| 4 | Regression (grab/throw) | after join, client can still grab a native pile (E), E-drop it, and LMB-throw it (the verified chain) — unaffected. |

PASS = #1 no extra kerfur + #2 no pile dup + **2b EARLY-moved piles at the moved position (no grab needed)** + #3/#4 nothing else broke.

### The b3 reading (the key new evidence — host + client both log it)
b3 is the dropped-convert position fix. The captured logs now carry a matched HOST→CLIENT pair per moved pile:
- **HOST** (in the connect replay): `[PILE-B3] HOST slot N pos-correction eid=... save=(...) -> current=(...) drift=...cm`
  for each EARLY-moved pile, then a `diverged-pile flush -- K correction(s) sent of M save-time pile(s)` summary.
- **CLIENT** (at quiescence): `[PILE-B3] CLIENT armed pos-correction eid=...` then
  `[PILE-B3] CLIENT pos-correction APPLIED eid=... applied=(...) host=(...) drift=X.XXcm`.
Reading:
- **Pile at moved spot on join + a CLIENT `APPLIED ... drift≈0`** → b3 delivered the position and the native snapped → **b3 VERIFIED**.
- **HOST `flush -- 0 correction(s) sent`** for a pile you moved early → the host thought it wasn't diverged (its actor
  resolved to the old pos, or the eid changed at re-pile) — tell me the eid; that's a host-detect gap, not a client one.
- **CLIENT `APPLIED ... drift` LARGE (not ~0)** → `SetActorLocation` didn't take on the save-loaded native (a
  Static-mobility no-op — the lesson). Tell me the eid + drift; the fix would move the snap onto a Movable handle.
- **No CLIENT `APPLIED` line at all** for a moved pile → the correction never armed (host didn't send / it was dropped)
  OR the native never bound — tell me whether the HOST `pos-correction eid=` line was present.

### The b2 drift reading (the DELIVERED-convert path, still present)
For a pile whose convert was DELIVERED (moved LATER in the window), the CLIENT logs the b2 line:
`[PILE] CLIENT ToPile SNAP(spawn-on-convert) eid=... drift=X.XXcm` — drift≈0 = placed correctly (b2 verified 16:42).

## Log markers (I'll confirm from the captured logs)
- #1 success: CLIENT `kerfur_reconcile: retired bound-mirror stale off-prop ... (mirror-aware teardown)` and a
  `sweep-retire -- 1 of 1` (not `0 of 1`).
- #2 success: CLIENT `save_identity_bind: PROXY-WINS ... case(ii)-converted: pile grabbed/moved in-window` for the moved pile(s); no overflow-driven extra pile.
- **b3 (the new one): HOST `[PILE-B3] HOST slot N pos-correction eid=...` + `flush -- K of M` AND CLIENT `[PILE-B3] CLIENT pos-correction APPLIED eid=... drift=X.XXcm`** for each EARLY-moved pile (see the b3 reading above).
- b2 (delivered-convert path): CLIENT `[PILE] CLIENT ToPile SNAP(spawn-on-convert) eid=... drift=X.XXcm` (if any pile was moved later).
- Regression: bind summary still `bound 874/874` (or the run's total); no new `[Warn]/[Error]` from our DLL.

## FAIL signatures
- #1 still an extra lying-down kerfur + log `sweep-retire 0 of 1` → the bound-mirror still excluded / teardown didn't fire (tell me the eid).
- #2 still two piles for a moved one → CtxForEid didn't gate (tell me the host eid + whether you saw `PROXY-WINS`).
- Anything in clean-join/grab/throw broken → a regression (the isolation should prevent this; capture logs).

## Honest note
Autonomous smoke can't do "host mutates while client joins" (timing-sensitive hands-on) and is rendering-blind.
The fixes are RE-pinned + audited SHIP, but the position-correct / no-regression verdict is your eyeball + the
captured logs. **b3 is the root fix for the dropped-convert case** (deliver the moved position at world-ready),
not an "attempt + observability" like b2 was — but it does rest on two things the drift log will confirm: the
host detecting the divergence (`HOST pos-correction` line present) and the client snap taking on the save-loaded
native (`CLIENT ... APPLIED drift≈0`). The reading section above says exactly what each log outcome means and
what I'd do next. On PASS (EARLY-moved piles at the moved position on join with NO grab + no regression) this
closes the join-window rubicon → push #1/#2/b2/b3 together. If the host sends 0 corrections, or the client drift
is large, capture the eid + the `[PILE-B3]` lines and I'll pin the exact gap (host-detect vs Static-mobility snap).
