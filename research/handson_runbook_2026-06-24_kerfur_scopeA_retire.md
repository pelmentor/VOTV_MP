# Hands-on runbook — scope A v1: kerfur forward off->active dup RETIRE (NPC channel)

**Deployed:** MD5 `D8A3D89EA650B0E1FA9FA4011B7DE967` (short `D8A3D89E`) on HOST + CLIENT + CLIENT2 + DEV.
Build clean. **Proto v88** (EntitySpawnPayload 96->108) -- BOTH peers MUST run this DLL. Audited: perf PASS +
correctness PASS (no CRITICAL/HIGH; 2 benign LOW documented).

## What changed vs the failed 6c668b9d (16:37)
The 16:37 run FAILED because the retire was triggered by the KerfurConvert, and a join-window turn-on's
`SendReliable(KerfurConvert)` FAILS (the joiner isn't ready for reliable gameplay mid save-transfer -- host
log 16:37:10). So the retire never fired. **v1 moves the trigger to the channel that ACTUALLY reaches the
joiner: the kerfur's npc EntitySpawn.** The host stamps the off->active kerfur's blob-instant save-time pos
onto that EntitySpawn; the client arms a retire keyed by it; the quiescence sweep retires the stale local
off-prop. Identity-key-driven, independent of the (failing) convert -- exactly what doc 03 prescribed.

## Setup -- the SAME 6-kerfur forward test
- Recent FRESH save, **>=1 kerfur OFF** (e.g. 4 off + 2 active).
- Client connects. **In the join-load window the host turns one OFF kerfur ON** (radial menu -> turn-on).
- The BUG (16:37): client showed 7 (the turned-on kerfur as active NPC + a stale off-prop object). Census
  showed an `*** UNCLAIMED ***` prop_kerfurOmega_C.
- A CLEAN bracket helps (flicker = `claim tracking ARMED`) but the fix is now bracket-INDEPENDENT (driven by
  the kerfur client poll at quiescence), so it should retire even if the bracket flakes.

## What to read in the logs (grep)
HOST:
- `save_transfer: slot N -- captured ... + M kerfur save-time xforms` -- M >= 1 (the capture fired).
- `kerfur_entity: BindFormActor K=... ->NPC(turn-on) ...` -- the window turn-on converged.
- (`SendReliable(KerfurConvert) failed ...` MAY still appear -- that is now EXPECTED + HARMLESS; the fix does
  not depend on the convert.)

CLIENT (the fix chain):
1. `kerfur_reconcile: ARMED off->active retire eid=... at save-time (X,Y,Z) (from npc EntitySpawn ...)`
   -- proves the save-time key ARRIVED on the npc EntitySpawn (NOT the convert) and the retire is armed.
2. `kerfur_reconcile: sweep-retire -- N of M pending save-time kerfur retire(s) at post-quiescence ...`
   -- the retire FIRED (the stale local off-prop destroyed).
3. `[KERFUR CENSUS] ... TOTAL ... (0 UNCLAIMED)` -- the dup is gone.

NOT expected (failure signatures): an `ARMED` line but NO `sweep-retire` (the off-prop never matched -- paste
the census); `sweep-retire ABORTED ... >50%` (racing bracket -- reconnect clean); a census `*** UNCLAIMED ***`
prop still present (dup persists).

## Acceptance (the 6 criteria)
1. **save-time pos on the npc channel:** the client `kerfur_reconcile: ARMED ... (from npc EntitySpawn)` line
   appears (NOT a convert line).
2. **retire fired npc-driven:** `kerfur_reconcile: sweep-retire -- N of M ...` appears at quiescence.
3. **DUP gone:** client shows the turned-on kerfur as an active NPC ONLY; **client total == host (6, not 7)**;
   census `0 UNCLAIMED`.
4. **independent of the convert:** even if `SendReliable(KerfurConvert) failed` appears on the host, the retire
   still fires (the key rode the npc channel).
5. **position-skew gone:** no off-prop sitting under an active at a wrong position.
6. **No regress:** reverse (host turns an active OFF in window -> client gets the off-prop), fuzzy-gate
   (off-stays-off cluster clean), OBS-2, L1 piles -- all unaffected.

## After the run
Paste: the client `ARMED` + `sweep-retire` lines, the `[KERFUR CENSUS]` block (0 UNCLAIMED?), client kerfur
count vs host. On PASS (dup gone + ARMED-then-sweep-retire + census 0) -> commit is done (working tree),
push stays HELD until you confirm. On FAIL -> paste the census + which line is missing (ARMED? sweep-retire?)
so I diagnose (no blind fix).
