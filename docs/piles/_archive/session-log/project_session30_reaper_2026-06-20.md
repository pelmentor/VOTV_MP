---
name: project-session30-reaper-2026-06-20
description: "Session 30 (2026-06-20) -- the client-local pile/clump dupe. Verified the morph-hook PIVOT IMPOSSIBLE (grab Func-swap is a silent no-op, IDA-decisive) then BUILT the CONVERGENCE REAPER instead: tag client-authored pile/clump at Init POST (gated post-quiescence + !InPurgeEpisode), reconcile on adopt, reap unbound past 8s TTL -> client converges to host's set. Adversary caught a world-wipe CRITICAL; fixed with the InPurgeEpisode gate. DLL 253905CD1376, proto v87 wire-unchanged, deployed x4, UNCOMMITTED on 1272b0a3, AWAITING hands-on."
metadata:
  node_type: memory
  type: project
  originSessionId: 86866d94-8692-44e2-b0a3-136eb16f4d11
---

# Session 30 (2026-06-20) -- the convergence reaper (client-local pile/clump dupe)

Prior: [[project-session29-confirm-by-clump-2026-06-19]]. The session-29 grab self-heal (DLL `7CF1298AB76E`)
killed the cross-peer cascade; the RESIDUAL the user reported: **"client still dupes but only for his own
local world"** -- the host stays singular, only the CLIENT accumulates local-only piles/clumps.

## THE VERIFICATION JOURNEY (verify-don't-guess paid off twice)
1. User asked for BROAD architectural solutions (3 agents) then said "I don't know" then "pivot" to hooking
   the BP grab/throw morph directly. **Then asked: "is it really true the morph is un-hookable?"**
2. **Verified (2 IDA agents, the deeper 2nd pass DECISIVE): the grab `Func`-pointer swap is a SILENT NO-OP.**
   `actorChipPile_C::playerGrabbed` is a SCRIPT (non-native) UFunction dispatched `EX_LocalVirtualFunction
   -> CallFunction -> sub_141453550` (the bytecode interpreter), which NEVER reads `UFunction->Func@0xD8`.
   Only ProcessEvent / the `FUNC_Native` branch of CallFunction read Func. So swapping Func intercepts
   NOTHING for the real grab. The 1st IDA agent's "you can swap Func" claim was WRONG; the 2nd pass (decompiled
   CallFunction's FUNC_Native branch + proved the interpreter never reads 0xD8) caught it. **The pivot the
   user approved was unbuildable -- verifying BEFORE building saved a deploy of a no-op.**
3. The throw-morph trigger (clump `BndEvt__...ComponentHitSignature` delegate) IS ProcessEvent-visible
   (RE'd 2026-06-08, `votv-clump-ball-to-pile-conversion-RE-and-event-fix-2026-06-08.md` Section 4.1) but the
   architect recommended DEFERRING it (needs IDA-confirmed gate-field offsets; a simpler death-watch fix
   exists). So the morph-hook pivot does NOT pan out; the buildable root-cause fix is a CONVERGENCE GUARANTEE.

## WHAT WAS BUILT: the CONVERGENCE REAPER (DLL `253905CD1376`, proto v87 WIRE-UNCHANGED)
Root (ground-truth agent, code+log verified): the dupe is a RELAY NO-OP CASCADE, NOT an OnConvert over-spawn
(`OnConvert` returns early for any interactor, remote_prop.cpp:1012-1022 -- never fresh-spawns). When the grab
relay no-ops (host eid rotted / peer-range / unbound -> `RelayClientGrab` bails at pile_handle.cpp:436), the
client's BP-authored local clump/pile is held with eid=0, the host is NEVER told, and it ACCUMULATES on
re-grab. The morph is un-hookable (above) so the local artifact IS authored regardless -> the fix must
GUARANTEE convergence independent of any relay landing.
- **MTA invariant restored** (client never keeps an unregistered shared object). 4 files, pure client-side:
  - `pile_handle.{h,cpp}`: `g_unreconciled` map + `MarkUnreconciledLocal` (tag) + `ReconcileLocalActor`
    (untag, called from `AdoptLocalOntoEid` the single bind choke-point) + `TickUnreconciledReaper`
    (destroy unbound past TTL). `kUnreconciledTtl=8s` (> the 5s grab/throw pending TTLs so a deferred adopt
    completes first). `OnDisconnect` clears the set.
  - `prop_lifecycle.cpp:~213`: `MarkUnreconciledLocal(self)` at the client "NOT authoring shared trash" gate
    (AFTER the echo-suppress gate -> host mirrors NEVER tagged; audit verified).
  - `subsystems.cpp:345`: `TickUnreconciledReaper()` next to `TickClientThrowWatch` (cheap no-op when empty).
- **Tag gates (BOTH load-bearing, do NOT remove):** client-only + pile/clump-only (`IsClumpActor||IsChipPile`
  -> excludes trashBitsPile + all else) + `HasLoadTailQuiesced()` (post-join) + `!InPurgeEpisode()` (not
  mid-reload). **Reaper gates:** client-only + `!InPurgeEpisode()`. **Reap condition:** tagged + IsLiveByIndex
  + past 8s + NOT host-bound (`ResolveMirrorEidByActor` kInvalidId or PEER-range -- peer-range is the client's
  own minted id, still an orphan) + NOT IsReceivedPropMirror + NOT the currently-held actor (read
  `holdingActor` via ReadMainPlayerGrabState -> no hand-yank; reaped once thrown/released).

## THE AUDIT CRITICAL (world-wipe) -- caught + fixed BEFORE deploy
1st cut gated the tag on `HasLoadTailQuiesced` ONLY. Adversary: `g_sweepFired` (= HasLoadTailQuiesced) resets
ONLY on a true UWorld swap, NOT on the join's SAME-WORLD multi-reload shadow-drain -> re-created save piles
fire Init POST while quiesced=true -> tagged -> reaped at 8s BEFORE the ~16-48s drain-exit
`RebindPileSeedsAfterWorldChange` re-binds them = the **2979/3229 world-wipe scar recurring**
([[feedback-join-reconcile-sweep-safety]]). FIX (audit's own prescription): gate BOTH the tag and the reap on
`!coop::prop_element_tracker::InPurgeEpisode()` (the existing same-world-shadow-drain bool, net_pump.cpp:498-521).
Tag-gate structurally excludes re-created save piles (kills the perf spike too); reap-gate + 8s TTL + host-bound
re-check protect any tag that slips the pre-episode race until the re-bind spares it. Audit verified SAFE:
held-item skip, host-mirror tag exclusion, thread-safety (all 3 accessors game-thread), crash/iterator,
trashBitsPile exclusion, first-sweep ordering.

## HOST-GRAB EID-BIND FIX (the v86 gate pattern, 2 more sites) -- DLL `E284E993A2F2`
The user hands-on-tested `253905CD1376` (HOST grabs only; client grabbed nothing -> the reaper never ran,
STILL UNVERIFIED) and found a SEPARATE pre-existing bug: **"кучки хоста не удаляются с перспективы клиента"**
(host grabs do NOT remove the client's co-located pile). DIAGNOSED from the user's log + a code-traced agent
(verify-don't-guess, decisive):
- The client's ~870 save piles are CLAIMED but their host-eid bind is DEFERRED to the divergence sweep
  (`pile-claim ... host-eid bind DEFERRED to the post-quiescence sweep`). In this join the sweep FIRED at the
  instant a world-reload had transiently freed the piles (870->0->870 churn) -> bound **0 of 870** (`ambient
  pile eid-bind -- 0 (re)bound ... 870 unmatched`). Quiescence mis-fired because the probe
  (`CountLoadTailUnsettled_`, prop_adoption.cpp:724) EXCLUDES chipPiles -> blind to the pile churn
  ([[feedback-join-reconcile-sweep-safety]]: probe settles during a transient-empty window it doesn't track).
- **DECISIVE root (NOT the timing -- the GATE):** the client's 4 s steady re-seed (`ReSeedKnownKeyedProps` ->
  `MarkPropElement` -> AllocAndInstall PEER-RANGE LOCAL, IsMirror()==false) re-marks every native save pile, so
  `ResolveMirrorEidByActor` returns a NON-invalid (peer-range local) eid for a genuine save pile. BOTH recovery
  paths gated on the OVER-BROAD `ResolveMirrorEidByActor(a) != kInvalidId` ("any bound eid -> skip as mirror")
  -> discarded ALL 870: (1) `FindCoLocatedSaveTwin_` (prop_adoption.cpp:346) -> deferred/lazy bind bound 0;
  (2) OnDestroy POSITION-fallback (remote_prop.cpp:932) -> host-grab backstop NEVER fired (0 `POSITION-resolved`
  lines) -> the client's pile survived. This is the IDENTICAL class as the v86 `IsReceivedPropMirror` fix
  (pile_handle.cpp:489,733).
- **FIX (surgical, race-proof, the v86 pattern at the 2 remaining sites):** replace
  `ResolveMirrorEidByActor(a) != kInvalidId` with `IsReceivedPropMirror(a)` (IsMirror()==true ONLY) at
  prop_adoption.cpp:346 (lets the bind adopt a re-seeded save pile) and remote_prop.cpp:932 (lets a host grab
  position-destroy the co-located re-seeded pile). Works via the position-fallback REGARDLESS of the
  sweep-timing race; does NOT touch the "DO NOT UNDO" eid-bind machinery. Both client-side -> proto STAYS v87.
  DEFERRED secondary (the agent's source-fix): drop the `if (IsChipPile) continue;` in `CountLoadTailUnsettled_`
  so quiescence waits for pile stability (closes the race at source + hardens the reaper tag); held back
  (quiescence scar tissue; the primary self-heals; the reaper's InPurgeEpisode gate already covers keyed
  reloads -- mass pile churn is always a keyed reload). NOT separately audited (2-line v86-proven pattern,
  agent cross-verified the wire/FindNearest/gate).

## REAPER REMOVED (DLL `9303CA245281`) -- it was a 2.5 fps cause AND the crutch the user rejects
The user hands-on-tested `E284E993A2F2` -> **client 2.5 fps** + "host still dupes but the 2nd pile disappears
after some time. Мне надо максимально нативное взаимодействие без всяких костылей, как будто это реальный
игрок взаимодействует" (= I want maximally NATIVE interaction, NO crutches, like a real player). Log
(freeze_probe): constant `net_pump::Tick gap=200-250ms` stalls = ~4 fps. ROOT (decisive): the reaper ran
EVERY tick; gate-fix #2 grew the mirror set ~30 -> ~931 (901 `RegisterPropMirror` chipPile), and the reaper's
per-tick `ResolveMirrorEidByActor` does a full `PropMirrors().Snapshot()` (vector copy) -> 30x heavier x
per-frame -> the stall. (253905CD1376 = reaper + ~30 mirrors felt fine; E284E993A2F2 = reaper x 931 mirrors
tanked.) AND the reaper IS the "appears-then-disappears" crutch the user explicitly rejects. **RULE 2: the
reaper is FULLY REMOVED** (all 4 edits reverted: pile_handle.{h,cpp} Mark/Reconcile/TickReaper + g_unreconciled
+ AdoptLocalOntoEid reconcile + OnDisconnect clear; prop_lifecycle.cpp tag; subsystems.cpp tick). KEPT the
2 host-grab gate fixes (root-cause, not crutches). The client-local-dupe convergence guarantee is now GONE --
to be solved at the ROOT (the user's "native, no crutches" directive), NOT by a reactive reaper. The root of
the whole crutch parade = the eid-binding between the client's save piles and the host eids ROTS under the
multi-reload join churn (-> self-heal, position-fallback, gate fixes, reaper all patch a symptom of THAT).
The native fix = make that binding robust (the client's piles stay bound to host eids -> host grabs propagate,
client never authors/dupes, no per-interaction patches). NEXT SESSION: design that (likely agents, clean
architecture), do NOT add more band-aids.

## THE NATIVE REDESIGN -- "go fix properly" (user green-lit; 2 agents + MTA CONVERGED on THIN CLIENT)
User: "Мне надо максимально нативное взаимодействие без всяких костылей, как будто это реальный игрок" ->
"go fix properly please". Spawned 2 design agents (architect + crutch-map/MTA-miner). **BOTH converged
DECISIVELY on Family A = THIN CLIENT** (the MTA model):
- **ROOT of the whole crutch parade:** the client loads the SAME save -> its OWN ~870 chipPile copies; we
  BIND them to host eids (pile-claim/PileSeed/BindPileSeeds_); the bind ROTS (the 870->0->870 reload churn
  frees bound actor ptrs; the sweep fires once during a freed window -> binds 0; the steady 4s re-seed
  re-marks each pile with a PEER-RANGE LOCAL eid that SHADOWS the host mirror). Every crutch (self-heal,
  position-fallback, re-bind-on-reload, mass-purge gate, two-pass resolver, the reaper) patches a symptom of
  THIS.
- **MTA (verified, cited): the client NEVER loads/owns shared state.** Server streams every element via
  EntityAdd with a server-assigned ElementID (SFixedArray index); identity is ALWAYS by ID, NEVER position;
  client never predicts; remove is by ID. There is NO MTA precedent for reconciling client-loaded objects.
- **Family B (robust binding) is IMPOSSIBLE** (architect): no stable per-actor identity for a keyless pile
  survives a cross-process reload; position fails dense same-variant clusters (v82's 0/53); every hardening
  path for B converges on A's premise. **The kerfur K-5 model ALREADY proves A works at this scale** (kerfurs
  are also un-hookable + save-loaded on both peers; handled by suppress + fresh-spawn-on-wire since s22).
- **Family A mechanism:** the client NEVER mints a LOCAL eid for a save pile; it fresh-spawns the HOST's
  PropSpawn-streamed piles as mirrors (host-range eid); the join sweep DOOMS every client chipPile that is
  not a host mirror; post-join EVERY pile is a host mirror resolved by eid -- NO seed/bind/position/self-heal.
  This is the kerfur model generalized to piles. DELETES ~200 lines, ADDS ~15. Net shrink, rot can't recur.

### PHASED PLAN (architect blueprint; NOT YET IMPLEMENTED -- tree reverted clean for the compact checkpoint)
- **PHASE 1 (re-apply FIRST next session -- exact edit below):** in `MarkPropElement`
  (prop_element_tracker.cpp ~329-336) extend the EXISTING kerfur K-5 client-mint gate to chipPiles. Add
  `const bool isChipPile = ue_wrap::prop::IsChipPile(actor);` beside `isKerfur` (outside the lock) and change
  the gate line to `if ((isKerfur || isChipPile) && s != nullptr && s->role() == coop::net::Role::Client) return;`.
  Effect: the CLIENT no longer mints the peer-range LOCAL shadow eid for a save pile (host still mints for its
  snapshot; RegisterPropMirror mirrors unaffected). It is the kerfur gate verbatim, applied to piles. COUPLED
  -- compiles but breaks pile sync ALONE; only build/deploy after Phase 2+3. (Was applied + reverted this
  session to leave the tree CLEAN = deployed 9303CA245281.)
- **PHASE 2 (coupled):** (a) `remote_prop_spawn.cpp` OnSpawn -- replace the `detail::TryClaimKeylessPileAtBracket`
  call + its `if(result)return` with the direct keyless fresh-spawn (`SpawnLocalTrashActor` + RegisterPropMirror)
  so the client mirrors the host pile instead of claiming its save twin. (b) `prop_adoption.cpp`
  RunDivergenceSweep_ (~516) -- replace `const int pileBound = BindPileSeeds_()` with a ONE-SHOT GUObjectArray
  walk dooming `IsChipPile(obj) && IsLive && !Default__ && !IsReceivedPropMirror(obj)` (the client's save
  originals; mirrors are spared). The existing in-propPairs chipPile doom (line 582) is now structurally dead
  (client chipPiles aren't MarkPropElement'd -> not in propPairs) -> remove it. **SAFETY (audit this):** only
  doom a save-original whose host mirror has ALREADY arrived, else a brief gap; consider gating the doom on
  pile-stream completion (the deferred `CountLoadTailUnsettled_` chipPile-inclusion fix is the clean source-gate).
- **PHASE 3 (RULE 2 deletions, now orphaned):** prop_adoption.cpp -- `PileSeed`+`g_pileSeeds`+`g_joinedAsClient`,
  `PileBindCandidate`+`g_pileBindIndex`+`ResetPileBindIndex`+`EnsurePileBindIndex`, `TryClaimKeylessPileAtBracket`
  (223-295), `FindCoLocatedSaveTwin_` (336-356), `BindPileSeeds_` (368-450), `RebindPileSeedsAfterWorldChange`
  (457-466)+RecordMirrorPileSeed+ForgetPileSeed; net_pump.cpp:521,542 + pile_handle.cpp:241 (RebindPileSeeds
  calls); remote_prop.cpp OnDestroy self-heal (894-901) + POSITION-fallback (912-943); pile_handle.cpp
  SetGrabCandidate self-heal (231-247); simplify `ResolveMirrorEidByActor` to single-pass. KEEP
  `IsReceivedPropMirror` (still used by OnLocalClumpAppeared:471 + the new doom loop) + the client-never-
  authors invariant (prop_lifecycle:205 / trash_collect:186) + the confirm-by-clump SIGNAL + the throw
  death-watch SIGNAL (real BP-hook gaps, not rot). Clean up remote_prop_spawn_internal.h (likely empties).
- **PHASE 4:** build (fix dangling refs), adversarial audit (doom-loop join-window safety, no per-frame
  walks, kerfur-parity), deploy x4, runbook, memory. Workflow run IDs: architect ad359e2376b5744b0,
  crutch-map a93a9692e05f6f054 (SendMessage to continue). The crutch TABLE + MTA cites are in their outputs.

## STATE (compact checkpoint -- working tree CLEAN)
DLL `9303CA245281` x4 SHA256 MATCH (host-grab gate fix + REAPER REMOVED) = the USER's current testable build
(needs: re-test fps is restored + host grabs remove client piles). proto **v87** (wire unchanged). Both logs
cleared (host-grab test archived `research/handson_2026-06-20_hostgrab_eidbind/`). Runbook take 14.
**Working tree == 9303CA245281 sources** (Phase 1 of the redesign was applied then REVERTED so the tree is
clean + matches the deployed DLL). The thin-client redesign is **DESIGNED + FULLY PLANNED, NOT YET
IMPLEMENTED.** Everything UNCOMMITTED on `1272b0a3` (the big s28+s29+s30 stack).
>>> NEXT SESSION (in priority order):
1. READ the user's fresh `9303CA245281` hands-on logs if any (fps restored? host grabs propagate?).
2. IMPLEMENT THE THIN-CLIENT REDESIGN (Phases 1-4 above) -- the user GREEN-LIT "go fix properly". This is
   the real fix that deletes the crutch stack. Do it as ONE careful audited pass (the doom-loop join-window
   safety is a world-wipe hazard -- adversary-audit it). Do NOT add more band-aids.
3. After it lands + the user's hands-on confirms native pile interaction -> COMMIT the whole stack.
DEFERRED (likely DELETED by the redesign): throw cluster-ambiguity, throw BndEvt PRE, position-fallback grab
relay, Fix C, 3+peer, remote_prop extraction, quiescence-probe chipPile inclusion (becomes the doom-loop
source-gate). NEVER claim fixed from smokes -- only the user's hands-on / a matching real log. UNCOMMITTED on `1272b0a3`
(the big s28+s29+s30 stack: confirm-by-clump + gate fix + churn-desync + grab self-heal + REAPER + the 2 v86
skip-mirror gate fixes). Build clean. Runbook: `research/handson_runbook_2026-06-20.md` (take 13). modular:
prop_lifecycle.cpp 922 LOC > 800 soft cap (flagged, pre-existing; future extract prop_authoring_policy).
>>> NEXT: READ the fresh `E284E993A2F2` hands-on logs FIRST. Markers: (HOST-grab fix) `OnDestroy: ...
POSITION-resolved co-located native pile` appears + `ambient pile eid-bind -- N (re)bound` N>0 (was 0/870);
(reaper) `REAPED unreconciled local pile/clump` + `reconciled local pile/clump` + NO mass pile disappearance
on join. Verify BOTH: host grabs remove client piles AND client grabs don't accumulate dupes. If clean ->
COMMIT the whole stack. Deferred: the quiescence-probe source-fix; throw cluster-ambiguity (FindNearestChipPile
250->80cm + chipType); throw BndEvt PRE observer (gate-field RE); position-fallback grab RELAY (make unbound
grabs SYNC not just not-dupe, if the reaper-vanish feels bad); Fix C; 3+peer gap; remote_prop.cpp extraction.
NEVER claim fixed from smokes -- only the user's hands-on / a matching real log.
