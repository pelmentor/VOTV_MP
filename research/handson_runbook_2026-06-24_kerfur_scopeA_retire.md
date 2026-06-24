# Hands-on runbook — scope A: kerfur forward off->active dup RETIRE

**Deployed:** MD5 `E27D176C80B8B0727BEDFC02A3F8B737` (short `E27D176C`) on HOST + CLIENT + CLIENT2 + DEV.
Build clean. **Proto bumped v86 -> v87** (KerfurConvertBroadcastPayload 104->116) -- BOTH peers MUST run this
DLL; a v86<->v87 mix is rejected at the header. Audited: perf PASS (no hot-path walk; all COLD/WARM), correctness
HIGH (sweep-liveness coupling) ROOT-FIXED before this deploy.

## What this build does (the fix under test)
A kerfur that is OFF in the transferred save but the host turns ON in the client's join-load window: the host
stops expressing it as an off-prop (it is now an active NPC on the npc channel), so the client's own save-loaded
local off-prop is never adopted as a host mirror and SURVIVES beside the new active NPC = a visible duplicate
(census 15:43: an UNCLAIMED prop_kerfurOmega_C). FIX: the host captures every off-form kerfur's blob-instant
SAVE-TIME position (`g_blobKerfurXforms`), carries it on the KerfurConvert when it turns the kerfur ON, and the
client RETIRES its stale local off-prop matched within 1 cm of that save-time key -- immediately at convert-apply
(`kerfur_reconcile::RetireLocalOffPropAtSaveTime`) or, if the native has not async-loaded yet, at a post-
quiescence backstop sweep (`SweepReconcileSaveTimeKerfurs`, driven by the kerfur client poll so it fires even
when the pile bracket did not arm -- the SnapshotBegin-lost flake).

## Setup -- the combined forward test (same as the 15:11/15:41/15:43 repro)
- Recent FRESH save with **6 kerfurs: 4 OFF + 2 ACTIVE** (or any mix with >=1 OFF kerfur).
- Client connects. **In the join-load window the host turns one OFF kerfur ON** (radial menu -> turn-on). For the
  full sweep, also turn one ACTIVE kerfur OFF (the reverse, to confirm no regression).
- Host ends at 6 (one more active, one more off than start). The BUG was: client showed 7 (the turned-on kerfur
  as active + a stale off-prop object), OR (15:41) the off-prop collapsed onto a wrong position under an active.
- Use a CLEAN bracket if possible (confirm `claim tracking ARMED` + `divergence sweep FIRING` appear) -- but the
  fix is now driven by the kerfur poll, so it should ALSO retire on a bracket-not-armed join (that is the H1 fix).

## What to read in the CLIENT log (grep these)
- `save_transfer: slot N -- captured ... + M kerfur save-time xforms` (host side) -- M >= 1 confirms the capture
  fired (the save-time keys exist).
- `kerfur_entity: BindFormActor K=... ->NPC(turn-on) ...` (host) -- the window turn-on converged + broadcast.
- THE FIX -- ONE of:
  - `kerfur_reconcile: RETIRED stale local off-prop kerfur at save-time (X,Y,Z) K=...` (immediate, at convert-apply), OR
  - `kerfur_reconcile: sweep-retire -- N of M pending save-time kerfur retire(s) at post-quiescence ...` (backstop).
- NOT expected (failure signatures): `RETIRE SKIP ... ambiguous` (two off-kerfurs <1cm -- coverage gap), or
  `sweep-retire ABORTED ... >50%` (racing bracket -- reconnect on a clean bracket).

## Acceptance (the 6 criteria)
1. **Dup gone (15:43):** the turned-ON kerfur shows on the client as an ACTIVE NPC ONLY -- no stale off-prop
   object beside it. Client total kerfur count == host (6, not 7).
2. **Position-skew gone (15:41):** no off-prop sitting at a wrong position / under an active. Every off-prop is
   at its own save position; every active is the kerfur the host has active.
3. **Reverse no regress:** the kerfur the host turned OFF in the window shows on the client as an off-prop (the
   reverse follow-ghost fix still holds -- exactly one off-prop, no ghost active twin).
4. **Off-stays-off clean (fuzzy-gate fix#1 intact):** the kerfurs left untouched reconcile as before (the 15:00
   baseline: each off-prop on its own distinct actor, no collision).
5. **No regress** to OBS-2 (a save kerfur object missing) or L1 (pile dup) -- kerfur objects all present, piles fine.
6. **Position uniqueness:** no WRONG kerfur retired (every kerfur the host still has -- off OR active -- is present
   on the client; the retire only removed the stale dup half).

## After the run
Paste: the `kerfur_reconcile:` line(s), the host `BindFormActor ->NPC(turn-on)` line, the `captured ... kerfur
save-time xforms` count, and the client kerfur count vs host. On PASS (dup gone + reverse intact + counts match)
-> remove no probe (none added; this is shipping code), commit is already done (working-tree), push stays HELD.
On FAIL -> paste the full kerfur block + whether a `RETIRE SKIP`/`ABORTED`/no-`kerfur_reconcile`-line appeared, so
I can diagnose (no blind fix).
