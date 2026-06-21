---
name: project-session31-thin-client-pile-2026-06-20
description: "Session 31 (2026-06-20) -- IMPLEMENTED + DEPLOYED the THIN-CLIENT ambient-pile redesign (the s30 native-fix decision), re-thought via an 11-agent workflow that OVERTURNED the captured plan (client-side respawn-from-seed = MTA-violating prediction; killed by the adversaries) -> host-re-stream drain bridge + dedicated pile clock + pending-remove-by-id. Built, then a 9-lens adversarial audit (GO-WITH-FIXES) caught 2 CRITICAL (CR-1 frozen-denominator permanent-defer dupe; CR-2 missing world-wipe floor) + 5 HIGH -- ALL FIXED + the gate re-verified by me. DEPLOYED votv-coop.dll 5D9C874C02B2 x4 (SHA match), proto v88, runbook take-15, logs fresh. AWAITING user hands-on. UNCOMMITTED on 1272b0a3."
metadata:
  node_type: memory
  type: project
  originSessionId: 86866d94-8692-44e2-b0a3-136eb16f4d11
---

# Session 31 (2026-06-20) -- THIN-CLIENT ambient-pile redesign IMPLEMENTED (audit fixes in flight)

Prior: [[project-session30-reaper-2026-06-20]] (the decision: Family A = THIN CLIENT, the MTA model). This
session IMPLEMENTED it -- but first RE-THOUGHT the captured plan (user: "well spawn agents re-think the plan")
and the re-think OVERTURNED a load-bearing piece, then a hard adversarial audit caught 2 CRITICAL bugs (one in
my OWN connect-doom fix). NOT yet deployed.

## THE DESIGN (4 durable docs in research/findings/, READ THEM):
- `thin-client-pile-sync-redesign-2026-06-20.md` -- the architecture (§5 doom gate, §6 world-wipe proof, §8
  delete list, §10 file-by-file, §12 audit-focus). From an 11-agent re-think workflow (5 design / 3 judge / 2
  adversary / 1 synth).
- `thin-client-pile-sync-IMPL-SPEC-2026-06-20.md` -- the exact code-location spec (enum values, wire sites,
  build order).
- `thin-client-pile-sync-AUDIT-2026-06-20.md` -- the 9-lens audit verdict (GO-WITH-FIXES) + the 2 CRIT + 5 HIGH.
- `thin-client-pile-sync-FIX-SPEC-2026-06-20.md` -- the EXACT mechanics for the CR-1/CR-2 gate fix + H-2.

**The model:** the CLIENT owns ZERO save piles (class actorChipPile_C). Every pile it shows is a HOST MIRROR
(host-range eid via RegisterPropMirror). A dedicated, quiescence-clocked, RE-RUNNABLE pile doom
(DoomNonMirrorChipPiles_) destroys the client's save-original piles. Identity is the host eid, NEVER position.
The kerfur K-5 model generalized to piles.

**THE RE-THINK OVERTURN (decisive):** the captured s30 plan persisted host-pile data as re-spawnable SEEDS
and re-spawned mirrors LOCALLY on the shadow-drain. The adversaries killed this in ALL 5 candidate designs: a
client re-spawning a mirror from a cached seed is the client PREDICTING host state -> a stale-position GHOST
DUPE if the host grabbed/moved that pile during the ~50s drain. The MTA-correct replacement: on the join
"shadow-drain" (a same-world mass actor purge that frees runtime mirrors + re-creates save piles, with NO
re-announce / NO new snapshot bracket), the CLIENT sends **PileResyncRequest**; the HOST re-streams one
BRACKET-FREE PropSpawn per live pile + a **PileResyncComplete{count}** marker; the client mirrors them. A
**pending-remove-by-id** queue (g_pendingRemoveEids) replaces the deleted OnDestroy position-fallback. Also
decisive: `HasLoadTailQuiesced() IS g_sweepFired` (shared with NPC/kerfur fresh-spawn) -> the pile doom MUST use
its OWN clock (g_pileDoom*), never that sticky bit, or it re-dupes kerfurs.

## WHAT WAS IMPLEMENTED (proto v88; UNCOMMITTED on 1272b0a3; NOT deployed)
- **P1 mint-gate** (prop_element_tracker.cpp MarkPropElement ~344): the kerfur client-mint gate extended to
  `isChipPile` -- a client never mints a local eid for a save pile (the v86 peer-range-shadow root).
- **Wire** (protocol.h v88): PileResyncRequest=80 (client->host, no payload) + PileResyncComplete=81
  (host->client, PileResyncCompletePayload{uint32 pileCount}); 3-place wiring (enum/payload + event_feed.cpp:453
  family router + event_dispatch_state.cpp:749 handlers).
- **Host re-stream** (prop_snapshot.cpp ReStreamPilesToSlot): host-only, bracket-free PropSpawn per live chipPile.
- **Pile-reconcile core** (prop_adoption.cpp, namespace remote_prop_spawn): g_hostPileCatalog, g_pendingRemoveEids,
  the dedicated pile clock (g_pileDoom* + g_pileExpectedMirrors + g_pileResyncCompleteSeen), CataloguePileMirror/
  ForgetPileCatalogEntry, EnqueuePendingRemove/ConsumePendingRemove, ArmPileDoom, **ArmPileDoomForConnect** (I
  added -- arms the FIRST doom at connect SnapshotComplete; the retired claim-bind's replacement),
  OnPileResyncComplete, TickPileReconcile (the §5 gate), DoomNonMirrorChipPiles_. CountLoadTailUnsettled_ made
  chipPile-inclusive. RunDivergenceSweep_ lost the BindPileSeeds_ call + the in-propPairs chipPile doom.
- **net_pump.cpp** InPurgeEpisode-exit + small-travel edges (client-gated): send PileResyncRequest + ArmPileDoom
  (replacing RebindPileSeedsAfterWorldChange). TickPileReconcile driven from npc_mirror.cpp:641.
- **OnSpawn** (remote_prop_spawn.cpp): TryClaimKeylessPileAtBracket deleted; keyless piles fresh-spawn host
  mirrors; CataloguePileMirror + ConsumePendingRemove on the create path.
- **OnDestroy** (remote_prop.cpp): the self-heal + the v84 POSITION-fallback DELETED; keyless eid resolving no
  actor -> EnqueuePendingRemove. **pile_handle.cpp** SetGrabCandidate self-heal deleted; IsPileInActiveRelay +
  IsLocalRelayInFlight (doom-collateral + deadline-defer guards).
- **RULE-2 deletions** (full): PileSeed/g_pileSeeds, PileBindCandidate/g_pileBindIndex*, TryClaimKeylessPileAtBracket,
  FindCoLocatedSaveTwin_, BindPileSeeds_, RebindPileSeedsAfterWorldChange, RecordMirrorPileSeed, ForgetPileSeed;
  remote_prop_spawn_internal.h removed; the OnSpawn fromConvert param removed across 4 sites.

## MY MANUAL REVIEW (caught 1 real bug the impl agent missed)
- **Connect-time doom gap:** ArmPileDoom was only wired to the shadow-drain edges; a join with NO drain (clean
  single-load / already-in-world joiner) would never doom the ~870 save originals = massive dupe. FIXED: added
  ArmPileDoomForConnect at the client SnapshotComplete (event_feed.cpp). Also made the gate require this-cycle's
  explicit complete marker for ANY fire (timing-independent).

## THE 9-LENS ADVERSARIAL AUDIT -> GO-WITH-FIXES (2 CRITICAL + 5 HIGH)
The audit (workflow w9crdxjj4) PASSED the 8 hard landmines structurally (pile clock independent of g_sweepFired,
no RunDivergenceSweep_ re-arm, bracket-free re-stream, npc_adoption untouched) but found:
- **CR-1 (a bug in MY connect-doom fix):** ArmPileDoomForConnect FROZE g_pileExpectedMirrors=~870. A normal host
  pile-grab during the 2-15s load-tail drops liveMirrors 870->869 but never decrements the frozen latch (and
  the actor resolved -> no pending-remove) -> `869<870` -> the gate defers FOREVER -> the ~870 save originals
  never die = the exact persistent dupe. Near-routine trigger.
- **CR-2:** the design's mandated `liveMirrors >= 1` world-wipe floor was NOT implemented; `expected =
  g_pileExpectedMirrors - g_pendingRemoveEids.size()` subtracts the WHOLE leak-prone global pending set (leaks
  unbounded; polluted by non-pile clump eids) -> expected clamps to 0 -> the doom fires against an empty mirror
  set = 2979/3229-class partial world-wipe.
- **THE UNIFIED FIX (my FIX-SPEC, the KEY INSIGHT):** the QUIESCENCE clock already enforces "wait for the full
  set"; the `expected` denominator's ONLY job is the world-wipe guard. So drive `expected` off the LIVE
  g_hostPileCatalog (self-tracking: +CataloguePileMirror, -ForgetPileCatalogEntry on every host destroy) MINUS
  this-cycle pending-removes -- NOT the frozen latch (CR-1) and NOT the global pending set (CR-2). The catalog
  grows WITH liveMirrors on a clean stream (quiescence times the fire) but stays STALE-HIGH across a drain (frees
  actors without OnDestroy -> liveMirrors<catalog -> defer until the re-stream restores them). Plus: single
  mirror-actor Snapshot per gate tick (H-2 perf: kills the per-pile IsReceivedPropMirror mutex-Snapshot =
  2.5fps-reaper-class); pile-gate EnqueuePendingRemove on catalog membership (call BEFORE ForgetPileCatalogEntry);
  clear g_pendingRemoveEids on doom fire; reset completeSeen/expected in BeginClaimTracking (#3).
- **HIGH:** H-1 ReStreamPilesToSlot ~870 synchronous PropSpawns -> host hitch (chunk like DrainChunk <=100/tick);
  H-2 (folded into the unified fix); H-3 no host debounce on PileResyncRequest; H-4 NO interaction smoke forces a
  shadow-drain across a grab (the [[feedback-interaction-smoke-not-join-smoke]] gap). Plus nits: #7 pin
  PileResyncComplete to Lane::Bulk; #17 drop the dead v84 locX/Y/Z from PropDestroyPayload; #20 small-travel
  double-arm; #12/#14 chipPile short-circuit in CountLoadTailUnsettled_.

## STATE -- FINISHED + DEPLOYED, awaiting user hands-on
- **DEPLOYED `votv-coop.dll` = `5D9C874C02B2`** x4 (HOST/CLIENT/CLIENT2/DEV, SHA256 verified MATCH; built
  15:17:33 from current source -- newer than the last edit 15:14:54, so it includes ALL audit fixes). proto
  **v88**. Runbook take-15 (research/handson_runbook_2026-06-20.md). Logs fresh.
- The fix agent `a502b13c7fe818b2b` applied ALL audit fixes (CR-1/CR-2 unified + H-1/H-3 re-stream chunk+debounce
  + H-2 single-snapshot + H-4 smoke + #3/#7/#12/#17/#20/#5); build clean. **I RE-VERIFIED the safety-critical
  code myself in the actual files** (NOT the self-report): TickPileReconcile gate (prop_adoption.cpp:724-781 --
  `expected` = `|g_hostPileCatalog \ this-cycle-pending|`, single mirror snapshot, clear-on-fire), the
  EnqueuePendingRemove pile-gate (553), and the OnDestroy order (remote_prop.cpp:935-940 -- EnqueuePendingRemove
  BEFORE ForgetPileCatalogEntry). All correct, matches the FIX-SPEC. CR-1/CR-2 genuinely fixed.
- **The H-4 interaction smoke CANNOT force the engine's real same-world shadow-drain** from the harness (mp.py
  has no client-reload primitive; the drain fires from net_pump's reaper). It exercises the wire round-trip +
  the doom gate only. So the drain re-stream path is validated by the USER's 2nd-reload HANDS-ON, not a smoke.
  I did NOT launch the game (user is on the PC -> user tests, I prepared the ground).
- **>>> NEXT:** read the user's `5D9C874C02B2` hands-on result (markers in the runbook: PILE DOOM armed for the
  CONNECT bracket / PILE DOOM FIRING liveMirrors=N expected=N / no dupe / no world-wipe / no 2.5fps). If CLEAN ->
  **COMMIT the whole s28+s29+s30+s31 stack**. NEVER claim fixed from smokes -- only the user's hands-on / a real log.
- **modular debt + cleanup (follow-up commits):** prop_adoption.cpp 821 + remote_prop_spawn.cpp 836 + remote_prop.cpp
  1151 > 800 soft cap (future: extract coop/pile_reconcile.cpp + coop/prop_mirror_registry.cpp); freeze_probe
  (#4/#19) is a TEMP dev probe compiled unconditionally -- remove after the H-1 host-burst measurement is read.

## KERFUR (user raised a deep adjacent issue; my take given; HELD pending user go)
User: a new joiner's transferred scratch save is stale by ~seconds; in that window the host turns a kerfur ON,
the joiner still has it OFF + can turn it on -> dupe. Proposed: STRIP kerfur entities from the save the client
receives. **My take (verify-don't-guess: save_transfer.cpp:310 does a LIVE SaveGameToSlot capture -> reads a
~20MB OPAQUE binary .sav -> chunks it):** the PRINCIPLE is right (same MTA/thin-client invariant as piles -- the
client must own no stale actionable copy of a host-managed entity), but blob-stripping that custom format is
RE-heavy + version-fragile + risks dangling refs + a save that won't load is worse than a dupe. I LEAN toward
closing it at the ACTION layer instead: make the kerfur turn-on HOST-AUTHORITATIVE (a client turn-on RELAYS to
the host, which dedupes against live state -- the invisible-handle model generalized; there's already a
KerfurConvertRequest client->host kind that may leak), + thin-client receive. CONSTRAINT (both approaches): the
current model deliberately ADOPTS the client's save twin (npc_adoption.cpp:64 BindAsMirror) because a FRESH-
spawned kerfur mirror had the v74 FLOATING-CAMERA bug -- fresh-spawn must solve that camera-child init. PLAN
(deferred until the pile fix lands): RE the save-strip feasibility AND the kerfur turn-on path, then decide
strip-vs-relay. P6 (the coupled kerfur drain-bridge from the redesign §P6 -- do NOT clear g_ghostSwept without a
host NPC re-send) is also queued.
