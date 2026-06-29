# Hands-on runbook — #2 kerfur hang FIX + moved-pile PROBE (2026-06-28, post-10:15)

Deployed DLL `E3E6BEAB17FC...` (HEAD `150da133`), hash-verified MATCH on host (`Game_0.9.0n`) + client
(`Game_0.9.0n_copy`). [dev] inis already set: `save_identity_bind=1`, `save_identity_map_log=1`,
`pile_delta_probe=1`, `reseed_orphan_selftest=0` (probe off). NO rebuild/redeploy needed. You launch the
bats; I grep (you don't read logs).

From the 10:15 hands-on: #1 twitch is GONE (good). This run does TWO things: (1) confirm the #2 hang FIX,
(2) PROBE the moved-pile root (I won't blind-fix it -- the next log pins the exact failure).

## TEST 1 -- #2 kerfur HANG-IN-AIR (should be FIXED)
1. HOST: `mp_host_game.bat`, load the fresh 6-kerfur save.
2. CLIENT: `mp_client_connect.bat`, join, let it settle (~20s).
3. On the CLIENT, TOGGLE a kerfurOmega OFF (turn it off).
4. WATCH the off-prop (`prop_kerfurOmega`): it should **FALL and rest on the ground**, NOT hang frozen at
   standing height. Toggle it back ON + OFF a couple times to be sure.
- PASS (what I'll grep): `kerfur_convert[client]: adopted parked turn-off ghost ... physics re-enabled -> settles to rest`
  AND no off-prop left floating. FAIL: it still hangs (then the host isn't streaming a resting pose and I
  switch to a host->client one-shot rest-pose).

## TEST 2 -- MOVED-IN-WINDOW pile (PROBE, not yet fixed)
This is the pile_1c scenario (see `research/handson_runbook_2026-06-23_pile_1c_dup.md` for the exact timing).
The point is to make the CLIENT log show WHY a moved pile lands at the stale spot.
1. HOST: load the save with chipPiles. Stand near a save-loaded chipPile.
2. CLIENT: start `mp_client_connect.bat`. **DURING the client's load window** (before it reaches the world --
   you have ~15-25s), on the HOST **grab that chipPile and MOVE it** a few meters, then drop it. (The move
   must happen BEFORE the host log prints `[PILE-1C] ... JOIN-WINDOW CLOSED` for that slot.)
3. Let the client finish loading + settle (~30s).
4. WATCH on the CLIENT: is the moved pile at the host's NEW position, or back at its OLD (save) spot?
5. Quit both. Tell me "logs ready."

What I'll grep in the CLIENT log to pin the root (no fix yet -- this DECIDES the fix):
- `[PILE-B3] CLIENT armed pos-correction eid=N host=(x,y,z)` -- was the correction RECEIVED? (PropSnapPos arrived)
- `[PILE-B3] CLIENT pos-correction APPLIED eid=N ... drift=Xcm` -- did it APPLY? drift~0 = snapped OK; drift>0 = SetActorLocation no-op (mobility).
- `[PILE-B3] Reset DROPPING N undrained pos-correction(s)` -- the NEW probe: the correction was DROPPED before it ever applied (armed but native never bound in time / Reset raced it).
- `RE-BIND by position` / `BOUND ... save-loaded NATIVE` -- was the moved native bound, and to which position?

Outcome -> fix:
- armed + DROPPING, no APPLIED => the bind didn't land before Reset => fix = apply the correction at the
  bind edge (mirror sync_reconcile's bind-then-apply into the bind path) / hold the correction across Reset.
- armed + APPLIED drift>0 => Static-mobility no-op => set the native Movable before the snap.
- no `armed` at all => the host never sent PropSnapPos for the move => fix on the host detect/send side.

## After
TEST 1 PASS -> the hang fix is hands-on-verified -> I commit it as verified + (already pushed base) push the fix.
TEST 2 -> I read the [PILE-B3] trace, pin the exact failure, build the ONE targeted fix, redeploy, re-run.
