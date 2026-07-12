# F1 — host-moved keyed prop during a client's JOIN WINDOW shows at the SAVE pos — DESIGN 2026-07-09

STATUS: FIX BUILT + AGENT-AUDITED (0 CRITICAL FAIL) + DEPLOYED 2026-07-09; NOT hands-on-verified.
The `/qf` design was ratified by the probe and shipped:

1. **PROBE ran, VERDICT = ROOT (1) CONFIRMED** (commit 7acd887d, `coop/dev/join_window_pos_trace`). User
   ran the repro; client log: `keys=2203 clobber=12 snapshot-won=2082 host-held=1 dead=1 unresolved=0`.
   The loadObjects-clobber root is MEASURED, not inferred. (unresolved=0 => the two-root model is complete.
   no-snapshot=107 = a probe COVERAGE gap: fresh-spawned keyed props the point-A converge trace didn't
   cover -- NOT a contradiction.)
2. **FIX BUILT** (commit 3199eae2 + perf throttle 44127e2e, DLL DA7901777A8DD305, all 4 folders hash-matched).
   Piece 1 = the b3 pile reconcile generalized to keyed props (`FlushDivergedPilePositionsForSlot` ->
   `FlushDivergedSavePositionsForSlot`; new `g_blobKeyedXforms` baseline via `CollectTrackedKeyedPropTransforms`).
   Piece 2 = the apply-time settled-skip in `ApplyPendingPosCorrections`. Receiver UNCHANGED (eid-generic;
   the chip overlay auto-skips a keyed eid).
3. **AUDITED** by an agent (2026-07-09): zero CRITICAL FAIL; the no-dup claim is code-verified
   (`UpdateChipHostPos` matches only `g_chipEntries` -> no vacate twin for a keyed eid). WARNs addressed/noted:
   perf (GetActorLocation is a UFunction dispatch -> keyed scan THROTTLED to firstRun + every 5th late-arm
   tick, 44127e2e); a narrow self-seed gap + the two file-size flags remain as noted below.

**NOT YET hands-on-verified** -- needs a real 2-peer join with the host moving a keyed rock during the
client's join window; watch the client log for `[PILE-B3] CLIENT ... keyed` apply lines + the rock landing
at the host pos. This is the user's test.

## KNOWN LIMITATIONS (from the audit, do NOT re-dig -- MEASURED)
- **Self-seed gap:** `CollectTrackedKeyedPropTransforms` (unlike the pile collector) does NOT self-seed an
  unindexed keyed prop. Mainline F1 is fine (a moved rock is index-tracked), but the COMPOUND case
  host-world-change + move-a-keyed-prop + join in the same deferred-reseed window can miss it. N=1; a
  won't-fix unless it recurs (add the self-seed to the collector, mirroring `CollectTrackedPileTransforms`).
- **Kerfur off-prop overlap:** the keyed map also captures off-form kerfurs (they are keyed Aprops). Benign
  (disjoint host actions vs the scope-A kerfur lane; a converted off-prop fails the IsLive gate), but noted.
- **Modularity:** `save_transfer.cpp` (901 LOC) + `prop_element_tracker.cpp` (1057) are over the 800 soft
  cap. Proposed extraction: pull the whole diverged-save-pos flush into `coop/session/save_diverged_pos_flush.*`.

## Symptom
The HOST moves a keyed world prop (rock) DURING a client's join window; after the client finishes joining
the prop renders at its OLD SAVE position, not where the host moved it. User-reported ("host moved two
rocks during a join window of the client"). NOT yet confirmed in a live join log.

## Root (MEASURED in code; the CAUSE itself still INFERRED — probe pending)
- The main connect-snapshot SENDS the keyed prop at the host's CURRENT position (`prop_snapshot.cpp:301`
  GetActorLocation), delivered PRE-loadObjects, and applies it at OnSpawn (`remote_prop_spawn.cpp:492`
  SetActorLocation, DRIVE-SKIP gated at :418 — skip if the host is holding it). [measured]
- The client's own loadObjects (save-load) RECREATES the keyed prop at its SAVE position, AFTER the
  snapshot -> overwrites the host-moved pos. [INFERRED from ordering + the b3 comment save_transfer.cpp:585-588]
- The keyed churn RE-BIND (`join_membership_sweep.cpp:257-269`) migrates the mirror row's IDENTITY (eid)
  onto the loadObjects recreate via RegisterPropMirror(rebindInPlace=true) — it does NOT re-apply POSITION.
  The mirror row stores NO transform (`prop.h:31` "UE owns transforms"). [measured]
- No lastHostXform cache exists. [measured]

## Converged direction (`/qf`): ONE host-authoritative live-pos reconcile (generalize b3)
F1 and the shipped **b3 pile hole are the SAME missing owner**: "the joiner's save-load clobbers any
host-in-window move." b3 being chipPile-only (`save_transfer.cpp:618` IsChipPile gate) is a §8 site-list.
FIX = generalize `FlushDivergedPilePositionsForSlot` into ONE reconcile at quiescence for EVERY
save-authoritative entity (keyed props + piles): send `PropSnapPos{eid, host-LIVE-pos}` where the host's
pos diverges; the client snaps the bound actor post-loadObjects. Read the host's LIVE pos (host-authoritative,
fresh) — NOT a client-cached snapshot pos (REFUTED: stale + non-authoritative + a second author lane).
- The `PropSnapPos` receiver core (`ArmPendingPosCorrection`, `event_dispatch_entity.cpp:408`) is already
  eid-generic; the `:411-422` overlay (`UpdateChipHostPos` / `ArmHostVacateTwin`) is chip-specific — a keyed
  branch uses the generic snap and SKIPS the chip identity overlay.
- SETTLED-SKIP at SEND (host side): skip an entity the host is currently HOLDING/driving (a mid-carry
  live-pos read is transient/undefined).

## The LATENT b3 APPLY-TIME GAP (found via the user's grab-during-window edge case, MEASURED)
`ApplyPendingPosCorrections` (`quiescence_drain.cpp:337-380`) re-validates ONLY liveness (`IsLiveByIndex`,
:346) at apply-time, NOT held/converted. So if a grab lands BETWEEN arm (PropSnapPos receipt) and apply
(quiescence sweep), eid E is now a CLUMP (grabbed pile) and the correction `SetActorLocation`s the CARRIED
clump to a ghost pos. This is a latent bug in the SHIPPED pile lane (snapshot-before-state-ready, OPUS §5),
not just F1. FIX: apply-time settled re-validation (skip if the bound actor is now held/converted/driven).
- Scope note (/qf R5-Q1): for a ROCK, hold-R pickup DESTROYS the world actor -> eid DEAD -> the liveness
  check ALREADY drops it. So the apply-time settled-skip is a PILE-ONLY concern (a clump survives liveness);
  do not over-generalize it to rocks before rule-of-three.

## The grab-during-window race (user's question, REASONED not measured)
Grabbing a prop DURING the reconcile window (before it snaps home) works host-authoritatively: the client
resolves the eid from its LOCAL mirror @save-pos, sends GrabIntent{eid}, the host enacts on its OWN prop
@host-pos; the reconcile then skips the now-grabbed prop (b3 skips clumps at :618; a rock's grab makes eid
DEAD). The prop is NOT ungrabbable. Only artifact = a visual jump. UNMEASURED — the probe must exercise it.

## MEASURE FIRST — the read-only probe (build this before ANY reconcile change)
A clean single-variable join (host moves ONE rock in-window, no hand/pile noise). Log, in tick order:
1. snapshot OnSpawn: host-pos APPLIED, or the `:418` DRIVE-SKIP fired? (discriminates loadObjects-clobber
   vs host-held-at-snapshot — the root is NOT confirmed until this separates them).
2. loadObjects recreate pos + the keyed RE-BIND eid + the final rendered pos.
3. grab-during-window order: host moves rock in-window, client grabs before the snap -> does eid resolve
   from the mirror @save-pos, does anything mis-snap?
4. host-holds-at-snap: is the host live-pos defined when the host still holds it?

Only after the probe: ratify/adjust the reconcile (piece 1), the send-side settled-skip, and the pile-only
apply-time settled-skip (piece 2). Provisional design above is a HYPOTHESIS, not AS-BUILT.
