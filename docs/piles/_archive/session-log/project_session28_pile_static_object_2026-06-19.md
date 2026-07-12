---
name: project-session28-pile-static-object-2026-06-19
description: "Session 28 (2026-06-19) -- LONG session. FIXED: freeze (email_sync prime-race flood), save_capture restored (kerfur dupe), pile eid-bind made SOLID (invariant BindPileSeeds_ -> 0 unmatched), client-can't-interact (OnConvert destroyed the pile mid-grab). NOT FIXED: host-grab dupe = a TIMING issue (grabbed-pile destroy delayed ~4s by the death-watch because OnPileGrabPre is suppressed for rapid grabs). KEY DOC INSIGHT: MTA does NOT eid-sync static map objects -- eid-binding 870 identical static piles was the root over-engineering. DLL D3E5F530AF5D deployed x4, UNCOMMITTED, awaiting next session."
metadata: 
  node_type: memory
  type: project
  originSessionId: 86866d94-8692-44e2-b0a3-136eb16f4d11
---

# Session 28 (2026-06-19) -- the pile saga continued + the static-object insight

VERY long session. The user got increasingly furious ("why are you so stupid", "we tried 20 times").
**Current DLL `D3E5F530AF5D` deployed x4 (SHA256 MATCH), logs cleared, UNCOMMITTED on `1272b0a3`.
User: "Documentize, prepare for compact. We tackle this in next session."** Proto is now **v84**.

## NEXT SESSION ORDER (do this first)
1. **READ the fresh `D3E5F530AF5D` hands-on logs FIRST** (host `Game_0.9.0n`, client `Game_0.9.0n_copy`,
   `WindowsNoEditor/VotV/Binaries/Win64/votv-coop.log`). Verify the client-interact fix (below) and re-derive
   the host-dupe state from the REAL logs -- do NOT guess.
2. **Fix the HOST-GRAB DUPE** (the remaining bug) = the confirm-by-clump plan below.
3. Then the RULE-2 cleanup: stop eid-binding static piles entirely (the MTA static-object model).

## THE ARCHITECTURAL TRUTH (the user pointed me to the docs -- I'd been reinventing)
`research/findings/mta/mta-npc-entity-sync-2026-05-24.md:430`: MTA **does NOT sync static, unbreakable world
objects** -- `if (!pObject->IsSyncable() || (pObject->IsStatic() && !pObject->IsBreakable())) [skip]`. They
exist IDENTICALLY on every client from the map load. Only MOVING/breaking objects get synced.
- VOTV's resting ambient `actorChipPile_C` are exactly that: both peers load the SAME save -> IDENTICAL piles
  at IDENTICAL rest positions (the user CONFIRMED "same layout"). **Eid-binding 870 identical static piles was
  the whole 6-session mistake** -- the binding rots across the join's multi-reload churn, so the host's
  `PropDestroy(eid)` can't find the client's co-located pile. Identity for a STATIC object = position at the
  instant it's touched (co-located until grabbed), NOT a fragile runtime eid binding.
- See `docs/AUTHORITATIVE_INTERACTABLE_MIGRATION.md` (the host-authoritative pattern) +
  `research/findings/mta/mta-object-pickup-sync-2026-05-23.md` (how MTA conveys "player A grabs object X").

## WHAT WAS FIXED THIS SESSION (all in D3E5F530AF5D, all verified-by-log or audited)
1. **THE FREEZE** (38s GT block) = an `email_sync` PRIME-RACE flood (~2056-email save loads async, primed at
   2 mid-load -> mass-broadcast 2054 to the joiner). FIXED `coop/email_sync.cpp`: stabilize-before-prime +
   bulk-load re-baseline (>32 rows/poll = a load, adopt not broadcast) + hash-based echo-proof. LOG-CONFIRMED
   GONE (`primed at 2056 (settled)`, 0 broadcasts, no GT stall).
2. **save_capture RESTORED** (I'd wrongly retired it in s25 blaming the freeze) = the kerfur-dupe fix (LIVE
   host save on join). Restored fully (files + CMake + sdk_profile + R2). Lesson saved
   [[feedback-verify-before-retiring-a-fix]].
3. **Pile eid-bind made SOLID** = the invariant-enforcing `BindPileSeeds_` (`coop/prop_adoption.cpp`):
   `FindCoLocatedSaveTwin_` FIRST -> ADOPT the save twin (destroy any orphan mirror, downgrade isMirror);
   re-spawn ONLY a genuine runtime incremental (gated `bornPreQuiesce`). 2-architect CONVERGENCE +
   adversarial audit (invariant HOLDS, 0 crit/high). RESULT: the eid-bind is now `871 already-live, 0
   unmatched, 0 adopted-over-dupe` (was rotting/duping). **This is REAL progress -- do NOT undo it.**
4. **Client pile GRAB** = relay at the reliable `InpActEvt_use` PRE edge (deleted the fragile aim-cache crutch,
   `pile_handle.cpp` + `trash_collect_sync.cpp`). Worked in some runs.
5. **Client CAN'T INTERACT** (the latest, D3E5F530AF5D) = `OnConvert` (`remote_prop.cpp`) destroyed the
   grabbing client's pile MID-BP-MORPH (the unconditional `OnDestroy(oldEid)` ran even when the adopt
   DEFERRED -> the BP grab interrupted -> no clump -> can't interact). FIX: when `interactor && !adopted`
   (deferred), DRAIN the oldEid mirror but DON'T destroy the live mid-grab actor (the BP consumes it;
   NoteHeldClump completes the adopt). NEEDS hands-on verify next session.
6. **v84 PropDestroy + position** (`protocol.h` 40->48B, kProtocolVersion 84) = the static-object
   position-fallback: `OnPileGrabPre` host sends the grabbed pile's position; `OnDestroy` destroys the
   co-located NATIVE pile by position when the eid resolves no actor. **Fired 0 times** (the eid-bind is now
   solid so it wasn't needed) -- kept as a robustness net, low-risk (gated keyless + non-zero pos + skip-mirror).

## THE REMAINING BUG -- HOST-GRAB DUPE (root-caused, NOT yet fixed)
From the `0327`/v84 logs (re-verify in the D3E5F530AF5D logs): the host did **6 grab-throws but
`OnPileGrabPre` fired only 1/6**. So the grabbed-pile destroy fell to the **net_pump death-watch, which only
broadcasts every ~4s** (batched 1,3,3,5). So for ~4s after each rapid grab the client still shows the old
pile + the clump + the thrown pile = the visible dupe, ACCUMULATING when grabbing faster than the 4s cycle.
- WHY OnPileGrabPre is suppressed: the grab-vs-throw guard (`s_prevGrabbing` in `trash_collect_sync.cpp`
  `OnPileGrabPre`). `InpActEvt_use` fires on BOTH press AND release and is BP-internal -- a throw's
  empty-handed RELEASE edge is indistinguishable from a grab's press, so the guard suppresses grabs that
  follow throws. The input edge is fundamentally ambiguous.
- **THE FIX (next session) = CONFIRM-BY-CLUMP** (the unambiguous grab signal is the held clump appearing,
  NOT the input edge). `OnPileGrabPre`: CACHE the aimed pile's host-range eid + position (read BEFORE the
  morph destroys it), don't broadcast. When the host's held clump appears (`EnsureHeldItemBroadcast`, fires
  RELIABLY 6/6), confirm the grab -> broadcast `PropDestroy(cached eid + pos)` IMMEDIATELY + clear (with a
  ~500ms TTL so a throw's release that produced no clump expires). This is reliable (no input-edge
  ambiguity -> no suppressed grabs, no spurious release destroys) AND immediate (no 4s death-watch lag). The
  same shape works for the CLIENT relay (cache at the edge, relay when the local clump appears) -- consider
  unifying. (The aim-cache I deleted earlier was the right IDEA done wrong: a 250ms window + a peer-range eid
  resolve; do it right -- cache the host-range eid + a reasonable TTL.)

## KEY FACTS (don't re-derive wrong)
- eid-bind is SOLID now (0 unmatched). The dupe is a DESTROY-TIMING problem, not a binding problem.
- `OnPileGrabPre` (InpActEvt_use PRE) is BP-internal, fires press+release, grab/throw ambiguous -> unreliable
  for the instant destroy. The held clump (EnsureHeldItemBroadcast / NoteHeldClump) is the reliable signal.
- The net_pump death-watch (`net_pump.cpp:438`) is the BACKUP (4s), not the primary; it's correct but slow.
- `FindNearestChipPile` exists (`ue_wrap/prop.h:303`); `FindCoLocatedSaveTwin_` skips mirrors (native only).
- A STATIC save pile on the client is a host-eid MIRROR (bound). The grabbed pile is bound -> destroy by EID
  (death-watch / OnPileGrabPre), NOT by the position-fallback (which skips mirrors).
- RULE in force: NEVER claim fixed from autonomous smokes -- only the user's hands-on / a matching real log
  ([[feedback-user-tests-claude-prepares-ground]]). I shipped MANY partial fixes this session that shifted
  the failure each time; the recurring-bug rule [[feedback-recurring-bug-is-architectural]] applied (the
  eid-binding LEVEL was wrong; the docs had the answer).

## STATE
- DLL `D3E5F530AF5D` x4 MATCH, logs cleared, runbook `research/handson_runbook_2026-06-19.md`. UNCOMMITTED on
  `1272b0a3`. Prior-run logs archived under `research/handson_2026-06-19_{freeze_emailflood,piledupe,nomirror,
  nomirror2}/`. Lessons: [[feedback-verify-before-retiring-a-fix]]. Prior: [[project-session27-freeze-emailflood-savecapture-2026-06-19]].
