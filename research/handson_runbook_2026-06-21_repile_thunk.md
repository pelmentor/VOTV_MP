# Hands-on runbook — re-pile thunk READ-ONLY observe pass (2026-06-21)

**Deployed:** `votv-coop.dll` SHA `B7EEB1BF…` to all 4 copies (host/client/client2/dev). Proto **v82**
(UNCHANGED — this change adds no wire format; it is host-local logging only). HEAD `7cf34571` + UNCOMMITTED
working tree (the read-only thunk; commit held until this validates). Build CLEAN, audit CLEAN.

## What changed (and what did NOT)
- NEW `ue_wrap/ufunction_hook.{h,cpp}` — patches `BeginDeferredActorSpawnFromClass`'s `UFunction::Func`
  (@+0xD8) with a transparent forwarder, to catch the clump's chipPile re-pile spawn (`EX_CallMath`,
  invisible to our ProcessEvent hook). IDA-pinned offsets (Func@0xD8, FFrame::Object@0x18), bytecode-verified
  source (the clump's ubergraph passes `EX_Self` for WorldContextObject).
- `trash_collect_sync.cpp` — `OnBeginDeferredSpawnObserve`: **READ-ONLY**. It LOGS the (worldCtx, Result)
  pair for every chipPile/clump spawn. It has **no path to OnHostConvert** — it cannot convert.
- **The death-watch is UNCHANGED and still the sole converter.** So gameplay behaves exactly as the last
  deployed build (the re-pile still works via the death-watch). This pass only adds log lines.

## The test (host does the work; the thunk logs on the HOST only)
1. Launch **host** (from `Game_0.9.0n`) + **client** (from `Game_0.9.0n_copy`), get them connected (a real
   coop session — the thunk gates on `connected()` + `role()==Host`).
2. On the **HOST**: aim at a chipPile, press **E** to grab it (a clump appears in hand), carry it, **throw**
   it so it lands and **re-piles** (becomes a chipPile on the ground). Repeat **3–5 times**.
3. Bonus: do at least one re-pile **inside a dense cluster** of piles (the old morph mis-bound there; the
   thunk is cluster-immune by construction — worldCtx is the exact clump, zero proximity).

## What to capture — paste me the RAW lines (host log)
Host log: `Game_0.9.0n\WindowsNoEditor\VotV\Binaries\Win64\votv-coop.log`

PowerShell one-liner to extract exactly what I need:
```powershell
Select-String -Path 'Game_0.9.0n\WindowsNoEditor\VotV\Binaries\Win64\votv-coop.log' `
  -Pattern '\[REPILE\]|\[GRAB\]|\[HB\]|HOST RE-PILE|GRAB ADOPT' | ForEach-Object { $_.Line }
```
Paste the raw output (ptr/cls/eid intact) — not a summary. 3–5 `[REPILE]` lines + a few `[HB]` for contrast
is plenty.

## The gate (what the lines must show to enable the convert)
- **`[HB]` lines present** → the patch is LIVE (so an absent `[REPILE]` would mean "offset wrong", not
  "thunk never fired").
- **≥3 CLEAN `[REPILE]`**: `worldCtx=<ptr> cls='prop_garbageClump_C'` (or a variant `_erie/_leaves/…`)
  marked **`[CLUMP]`** with **`eid≠0`**, and `Result=<ptr> cls='actorChipPile_C'` (or variant).
- **eid cross-check:** each `[REPILE] … eid=E` should match a death-watch `[PILE] HOST RE-PILE eid=E` for the
  same re-pile, and the `[REPILE] Result=<ptr>` should match that line's `fresh pile %p`. Two independent
  paths agreeing = double-confirmation we read the right objects.
- **ZERO** `[REPILE]` lines with `[NOT-clump]` or `worldCtx=…<bad-read>` (a wrong-offset read).
- **ZERO** spurious `[GRAB]`/`[REPILE]` whose worldCtx reads as a tracked clump on a non-re-pile spawn.

If all green → I flip the thunk to convert + **atomically delete the death-watch in the same commit** (no
window with two converters — the RULE-2 swap; this also removes the ~5s vanish-return). If a line is dirty
→ the log itself names the problem (bad-read / NOT-clump / eid=0) and we fix before any convert.

## Reading the log — gate interpretation (agreed 2026-06-21, the 5 nuances)
1. **Liveness** is proven by the `ufunction_hook: patched ufn=… Func @0xD8` install line (the patch took) +
   the first `[GRAB]` (it fires) — NOT dependent on the sparse `[HB]`. Grab DOES spawn a clump via
   BeginDeferred (verified: `ExecuteUbergraph_actorChipPile` + `toClump` do `BeginDeferred(self,
   prop_garbageClump_C,…)`), so a grab always emits a `[GRAB]`.
2. **`[GRAB]` lines are expected** (worldCtx=`actorChipPile_C`, Result=`prop_garbageClump_C`). Their
   worldCtx flag will read `[NOT-clump]` — CORRECT (a grab's source is a pile). The `[CLUMP]` flag only
   validates `[REPILE]`; for `[GRAB]` read `cls=` directly. (The pile's `grime_blood2_C` effect spawn is
   not trash → won't tag `[GRAB]`/`[REPILE]`.)
3. **ISOLATED single re-pile:** thunk and death-watch MUST agree on eid AND pile-ptr (no neighbor to
   mis-pick). Divergence = RED, stop + investigate. **Count the >=3 CLEAN from isolated re-piles.**
4. **CLUSTER re-pile:** the thunk (worldCtx-clump, Result-pile, eid) is GROUND TRUTH. eid still matches both
   paths (same clump's adopt eid); the death-watch's `fresh pile %p` can diverge (nearest-untracked picks a
   neighbor). A thunk≠death-watch pile on a cluster = the death-watch's proximity mis-bind exposed → the
   thunk is RIGHT, do NOT cut the gate. (Distinguisher: was there a neighbor pile? isolated→must-agree;
   cluster→thunk-wins. Opposite interpretations.)
5. **`[REPILE]` is self-sufficient:** a clean `[REPILE]` (`worldCtx 'prop_garbageClump_C' [CLUMP] eid!=0` +
   `Result 'actorChipPile_C'`) counts on its own. The death-watch pairing is a bonus second witness; its
   ABSENCE does not make a clean `[REPILE]` dirty (the death-watch is a heuristic, may miss). The pairing
   only matters when PRESENT + DIVERGENT on an ISOLATED re-pile (then #3 → red).
