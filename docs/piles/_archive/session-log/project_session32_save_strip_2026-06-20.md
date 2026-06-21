---
name: project-session32-save-strip-2026-06-20
description: "Session 32 (2026-06-20) -- the pile dupe ROOT FIX = save-strip (host strips ambient piles from the transferred save). Overturned the thin-client doom (s30-31, which FAILED user hands-on). DEPLOYED, awaiting hands-on."
metadata: 
  node_type: memory
  type: project
  originSessionId: 86866d94-8692-44e2-b0a3-136eb16f4d11
---

# Session 32 (2026-06-20) -- SAVE-STRIP pile dupe root fix

## What happened
Session 31's thin-client doom (DEPLOYED `5D9C874C`) **FAILED the user's real hands-on**: "still dupes like
crazy, still old piles don't morph." I READ the real logs (not guessed): the pile doom WEDGED
`liveMirrors=758 < expected=868` and NEVER fired (zero "PILE DOOM FIRING" lines) -> save-originals never
destroyed -> dupes persisted. This is the Nth recurrence -> the patch LEVEL was wrong, it's ARCHITECTURAL.

## Root cause (now bytecode-verified, not guessed)
The joiner loads the host's LIVE-captured save, which ALREADY contains every ambient pile (`actorChipPile_C`)
-- AND the coop layer ALSO streams those same piles as host mirrors. **Save-loaded pile + streamed mirror =
the structural DUPE, by construction.** Every fix since s22 tried to RECONCILE the two copies; they all
rotted because **piles have NO cross-peer identity**. Two decisive RE rounds on the cooked-pak BP bytecode
(kismet-analyzer; artifacts under `research/pak_re/*.json`) proved:
- `actorChipPile_C` has NO Key field; `setKey()` writes a transient frame nothing reads = a NO-OP the game
  discards. `gatherDataFromKey` hardcodes gather=false.
- Piles persist via `primitivesData` (class+transform+chipType) AND objectsData (key="None"). LOAD =
  `loadObjects` spawns from objectsData, then "Load Primitives" DESTROYS all int_primitive actors and
  RESPAWNS piles POSITIONALLY from primitivesData. So primitivesData is the load-authoritative pile array.
- `setIgnoreSave(true)` gates ONLY the objectsData (int_save) walk; the Save-Primitives walk has NO skip
  gate -> setIgnoreSave alone is INSUFFICIENT.
=> The only stable identity a pile can have is the HOST-MINTED eid the coop layer streams. So make the host
the SOLE source and remove the second source.

This OVERTURNED the 6-agent RE workflow memo (`pile-dupe-rootcause-RE-2026-06-20.md`), which had recommended
A' (keep save piles + give them a shared identity). The bytecode proved A''s "gating unknown" (is there a
derivable pile identity?) = NO. So A (the user's original save-strip idea) is correct + MTA-faithful
(CEntityAddPacket + server-stamped ElementID = one instance, streamed with an id).

## The fix (DEPLOYED `B69C35D2`, proto v88 unchanged, x4 SHA-verified)
Host STRIPS ambient-pile records from the transferred scratch save before the joiner loads it -> joiner
loads ZERO piles -> host streams each pile once as the sole source -> no dupe; host grabs/morphs address the
one instance ("old piles morph" fixed).
- `ue_wrap/save_capture.cpp`: new `ArrayStripView` (anon ns) + a "3b. STRIP" block in
  `CaptureLiveWorldToScratchSlot(slot, stripClassNames)`. NON-DESTRUCTIVE: temporarily points the live
  saveSlot TArray header at a filtered SHALLOW-copy buffer (excluding records whose class@0x00
  `IsDescendantOfAny(actorChipPile_C)`), serializes, RESTORES the original header (RAII). Leak-free (shallow
  copy shares inner FString/TArray heap with the still-owning original; stripped elements never freed). Four
  arrays filtered: primitivesData@0xE30 (stride 0x60, LOAD-AUTHORITATIVE), objectsData@0x300 (0x100),
  trashPilesData@0x818 + grimeData@0x808 (0x100, defense-in-depth, empty in the live path -> no-op; class
  filter keeps grime).
- `save_transfer.cpp`: passes `{L"actorChipPile_C"}` (covers _erie/_wetConcrete/_leaves subclasses via
  IsDescendantOf). Kerfurs NOT stripped (camera-safe adoption).
- Offsets resolved TWO-SOURCE (CXXHeaderDump + UE4SS_ObjectDump_GAMEPLAY_SAVE.txt): struct_save stride
  0x100 (raw 0xF8), struct_primitiveSave stride 0x60 (raw 0x58), class ClassProperty @0x00 in both. STRIDES
  are the 16-aligned sizes (the tarray-stride rule -- raw size AVs on elem>=2).
- Adversarial audit: 2 LOW only (keep the strip-to-serialize window call-free; trashPilesData/grimeData
  empty in live path). Mechanism (reserve-before-insert, build-before-swap, RAII restore, aligned strides,
  null-class skip) verified sound. Build clean.

## KEY DECISION: strip-ONLY ship; doom NOT yet deleted
With the strip, the failing doom apparatus is PROVABLY BENIGN: it destroys non-mirror chipPiles, but after
the strip the client's only chipPiles are host mirrors, so it either DEFERS (mirrors missing) or FIRES and
destroys ZERO. Even the 758/868 wedge is harmless now (no save-originals to dupe). The strip kills the dupe
at the SOURCE regardless of the doom. So I shipped the SMALL strip (low-risk, root fix) and did NOT bundle a
big RULE-2 deletion into an un-smoked handoff (I can't smoke -- user is on the PC). The "PILE DOOM FIRING
... destroyed 0" log is now a strip-SUCCESS signal.

## STATE: DEPLOYED `B69C35D2B5B0D649` x4, proto v88, logs rotated to *.failrun-s31.log, runbook take-16,
AWAITING user hands-on. UNCOMMITTED on `1272b0a3` (the whole s28-s32 stack).

## NEXT (in order)
1. READ the user's hands-on result (markers in runbook take-16: host `save_capture: strip primitivesData
   N->M (stripped K)`; client NO dupes, piles morph; the doom's "destroyed 0" = strip worked). NEVER claim
   fixed from a smoke -- only the user's real hands-on counts.
2. If CLEAN: RULE-2-DELETE the now-confirmed-dead doom/catalog/resync apparatus (delete list in
   `pile-save-strip-DESIGN-2026-06-20.md` SS"RULE-2 DELETE"): TickPileReconcile/DoomNonMirrorChipPiles_/Arm*,
   g_hostPileCatalog, g_pendingRemoveEids, the OnSpawn catalog block, OnDestroy pending ordering, net_pump
   arm calls, event_feed arm, PileResyncComplete (KEEP the stream + pile_handle relay + PileResyncRequest->
   re-stream drain recovery; revert CountLoadTailUnsettled_ chipPile-inclusion to skip). Then COMMIT s28-s32.
3. KERFUR dupe (user's coupled question): SEPARATE fix = form-agnostic adopt-then-`KerfurConvert` (match on
   host eid/pose, NOT the per-peer-random save Key), NOT strip (strip regresses camera-safe twin adoption).
4. Watch-point: if the host stream under-delivers, client shows FEWER piles than host (missing, not duped) ->
   stream-reliability follow-up.

## Load-bearing facts
- Piles: no Key, setKey no-op, positional respawn from primitivesData; the only id is the host-streamed eid.
- The strip is host-side + non-destructive (live saveSlot byte-identical after restore); host's own save safe.
- save_capture calls saveObjects (which internally calls Save Primitives + saveHoldObj) -> the scratch blob's
  primitivesData IS fresh (not stale). Both objectsData + primitivesData carry piles.
- int_primitive implementers: actorChipPile_C (+subclasses), prop_garbageClump_C (+subclasses), grime_C
  (do NOT strip grime). Filter by IsDescendantOf(actorChipPile_C).
- Prior: [[project-session31-thin-client-pile-2026-06-20]] (the FAILED doom this replaced).
