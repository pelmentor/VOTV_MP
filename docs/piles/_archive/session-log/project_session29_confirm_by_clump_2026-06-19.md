---
name: project-session29-confirm-by-clump-2026-06-19
description: "Session 29 (2026-06-19) -- BUILT the CONFIRM-BY-CLUMP host-grab pile-dupe fix (the fix queued by session 28). Root cause (re-verified from code, no fresh logs existed): the host pile-destroy + client relay fired at the BP-internal press/release-AMBIGUOUS InpActEvt_use input edge, gated by a grab-vs-throw guard that misclassified ~5/6 of rapid grabs -> the destroy fell to the 4s reaper -> ~4s accumulating host dupe. FIX: OnPileGrabPre only CACHES the aimed pile (eid+pos off the LIVE pile); the destroy/relay fires on the UNAMBIGUOUS garbageClump rising edge (pile_handle::OnLocalClumpAppeared). RULE-2 deleted the whole grab-vs-throw classifier. Designed + adversarially reviewed by 2 workflows (design convergence, then post-build audit: perf PASS, adversary SHIP on 8 scenarios). DLL 59024BFE71E9 deployed x4, proto v85, UNCOMMITTED on 1272b0a3, AWAITING the user's hands-on."
metadata:
  node_type: memory
  type: project
  originSessionId: 86866d94-8692-44e2-b0a3-136eb16f4d11
---

# Session 29 (2026-06-19) -- the confirm-by-clump host-grab pile-dupe fix

Session 28 documented + queued the host-grab dupe fix for "next session." This session BUILT it.
**DLL `59024BFE71E9` deployed x4 (SHA256 MATCH), proto v85, logs clean, UNCOMMITTED on `1272b0a3`.
AWAITING the user's hands-on test** (no hands-on has run; the user does the testing,
[[feedback-user-tests-claude-prepares-ground]]). Runbook: `research/handson_runbook_2026-06-19.md` (take 8).

## NEXT SESSION ORDER
1. **READ the fresh `59024BFE71E9` hands-on logs FIRST** (host `Game_0.9.0n`, client `Game_0.9.0n_copy`,
   `WindowsNoEditor/VotV/Binaries/Win64/votv-coop.log`). Verify the host dupe is GONE + the client grab syncs.
   Markers below. Do NOT guess -- re-derive from the REAL logs.
2. If clean: COMMIT the baseline (`1272b0a3` is still the parent; this is a big uncommitted stack -- session
   28's freeze/save_capture/eid-bind fixes + this confirm-by-clump fix). Then the deferred RULE-2 cleanup +
   the 3+ peer gap (below).
3. If it still dupes: the markers tell you which layer (see "if it fails" below).

## WHAT WAS BUILT (the fix, all verified-by-audit, NOT by hands-on)
Root cause (re-verified against the CODE -- no fresh logs existed, the user hadn't tested since s28):
`trash_collect_sync::OnPileGrabPre` (the `InpActEvt_use` PRE observer) broadcast the host PropDestroy AND the
client RelayClientGrab at the E-press edge. That edge is BP-internal + fires on BOTH press and release, so a
grab vs a throw are indistinguishable; the `s_prevGrabbing`/`wasHolding` grab-vs-throw guard suppressed grabs
that followed throws -> ~1/6 fire rate on rapid grabs -> the destroy fell to the net_pump 4s reaper -> ~4s of
visible duplicate pile per grab, accumulating ("host тупо дюпает бесконечно").

THE FIX = confirm-by-clump (do NOT decide at the ambiguous edge; decide at the unambiguous clump rising edge):
- `OnPileGrabPre` (trash_collect_sync.cpp) STRIPPED to: session-check + `ReadMainPlayerLookAtActor` +
  `IsChipPile` + `pile_handle::SetGrabCandidate(aimed)`. RULE-2 DELETED the grab-vs-throw guard
  (`s_prevGrabbing`/`wasHolding`), the 300ms `s_lastAimed` dup-press window, the immediate host PropDestroy,
  and the immediate client RelayClientGrab. Removed the now-dead `remote_prop.h` include.
- `pile_handle.cpp` NEW: `struct GrabCandidate{pileEid,locX/Y/Z,chipType,ts,valid}` + `g_grabCandidate` +
  `kGrabCandidateTtl=600ms`. NEW `SetGrabCandidate(pileActor)` caches the pile's eid OFF THE LIVE PILE
  (HOST: forward id `GetPropElementIdForActor` then mirror fallback; CLIENT: mirror-only
  `ResolveMirrorEidByActor` -- never the client's peer-range forward id) + pos + chipType. NEW anon
  `ConfirmGrabCandidate_(s)`: HOST -> `SendPropDestroy(eid+pos)`; CLIENT -> `RelayClientGrab`.
- `NoteHeldClump` RENAMED -> `OnLocalClumpAppeared` (BOTH roles now): the NOT-A-MIRROR gate
  (`ResolveMirrorEidByActor==kInvalidId`) is HOISTED to the top (protects BOTH confirm + capture -- the
  design-review CRITICAL the architects missed: a RECEIVED mirror clump would else spuriously confirm a stale
  candidate), then `ConfirmGrabCandidate_(s)` (MUST run before the client capture so g_grabPending is armed),
  then the CLIENT-only capture (unchanged g_pendingAdoptClumpEid / g_grabPending branches). OnDisconnect
  clears g_grabCandidate.
- `local_streams.cpp:289`: `NoteHeldClump` -> `OnLocalClumpAppeared` (the new-held rising edge, fired after
  EnsureHeldItemBroadcast; for the CLIENT, EnsureHeldItemBroadcast returns early via the host-authority gate,
  so OnLocalClumpAppeared is the client's only confirm+capture path).
- `remote_prop.cpp`: OnDestroy position-fallback radius 100cm -> 50cm (static save piles are ~0cm apart
  cross-peer; tighter = less neighbour mis-target). PLUS the audit-MEDIUM fix: `UnregisterPropMirror` now
  evicts the pile held-clump map (`pile_handle::ForgetPileMirror`) symmetrically with the kerfur map (was
  asymmetric -> a stale clump actor->eid entry could stream a held pose under a wrong eid for a GC-recycled
  actor until the self-heal fired).
- `protocol.h`: kProtocolVersion 84 -> 85. WIRE BYTES UNCHANGED; only the grab-sync SEMANTICS changed, so an
  old (v84) peer + a new (v85) peer must NOT interop -> the bump rejects the mix.

## WHY THE TIMING (load-bearing -- don't re-derive wrong)
- `local_streams.cpp:283` `if (heldActor != g_lastHeldProp)` is a once-per-NEW-held-actor RISING EDGE. A
  garbageClump appears in the hand ONLY on a grab (a throw REMOVES the clump + lands a pile). So this edge is
  the unambiguous "a grab happened" signal -- unlike the input edge.
- The BP grab morph (pile -> clump) is SYNCHRONOUS within the InpActEvt_use ubergraph, so the clump is the
  held actor within 1-2 game-thread ticks of the cache. TTL 600ms is ample for any hitch.
- A throw / mis-aim caches a candidate that no clump confirms -> the TTL discards it. A second real grab
  OVERWRITES the candidate. The not-a-mirror gate + IsChipPile cache-gate + overwrite make a false-confirm
  STRUCTURALLY impossible (no liveness check needed -- a throw produces no LOCAL-morph clump; a received
  mirror clump is gated out). The host owns its pile until the relay arrives (client grab), so the relay
  moving from the input edge to the clump edge (~1 tick later) is SAFE.
- The candidate eid MUST be captured OFF THE LIVE PILE (at SetGrabCandidate) and REUSED at the clump edge --
  the clump edge can't re-resolve a dead pile (that is exactly the relay-on-morph crutch that failed).

## AUDIT RESULT (2 workflows)
- Design convergence (2 code-architects + 1 adversary): confirm-by-clump CORRECT_WITH_CHANGES. The adversary
  caught the not-a-mirror-gate-on-confirm CRITICAL the architects missed -> absorbed.
- Post-build audit (perf + correctness + adversary): perf PASS (all new hooks event-driven / rising-edge,
  NOT per-frame; thread-safe game-thread-only g_grabCandidate, asserted); correctness WARN (only LOW/MEDIUM
  doc + the ForgetPileMirror MEDIUM -- all fixed); adversary SHIP (8 host-grab/client-grab/edge scenarios all
  SAFE, RULE-2 fully clean, ordering traced correct). Fixed: ForgetPileMirror symmetry, the pile_handle.h
  FLOW comment, the trash_collect include comment, the autotest markers.

## KNOWN / DEFERRED (NOT this fix)
- **3+ peer client-grab non-interactor gap:** `OnConvert`'s oldEid teardown `dp` does NOT carry
  `payload.locX/Y/Z`, so a NON-interactor peer whose eid binding rotted keeps a stale pile mirror. INERT at 2
  peers (a client grab's only other peer is the host = the authority, not a mirror-holder). DO NOT naively
  copy payload pos into the OnConvert teardown -- it would break the THROW convert (oldEid=clump; the
  position-fallback would FindNearestChipPile near the landing pos and destroy a NEIGHBOUR pile). Needs a
  grab-vs-throw-convert distinction. Fix when 3+ peer pile sync is hardened.
- **remote_prop.cpp 1116 LOC > 800 soft cap:** flagged. Extraction proposal: coop/prop_drive.cpp (the drive
  machinery) + coop/prop_convert_receiver.cpp (OnConvert/OnDestroy/fallback), leaving remote_prop.cpp as the
  mirror registry. Separate-commit refactor; does NOT block.
- **OnDestroy FindNearestChipPile** still walks the full GUObjectArray (~150-250k) per host grab whose eid
  rotted (pre-existing v84). With the now-solid eid-bind it rarely fires, but if hands-on shows a per-grab
  hitch, bucket it. **net_pump 4s reaper backstop is intact** (idempotent 2nd PropDestroy on the receiver).
- TickClientThrowWatch 250cm FindNearestChipPile has no chipType/mirror guard (session-24 deferred item).

## IF IT FAILS (don't guess -- read the markers)
- HOST: `pile_handle: grab CANDIDATE cached` then `pile_handle[host]: grab CONFIRMED by clump -> PropDestroy`.
- CLIENT: `pile_handle[client]: grab CONFIRMED by clump -> PileGrabRequest relay=sent` + `adopted local clump`.
- If `grab CONFIRMED by clump` is MISSING for some grabs -> the clump rising edge isn't firing (a different
  layer -- check heldActor resolution in local_streams). If the confirm fires but the client's `OnDestroy`
  resolves NO actor -> identity (eid/pos), not timing. NEVER claim fixed from a smoke -- only the user's
  hands-on / a matching real log [[feedback-recurring-bug-is-architectural]].

## GATE-FIX ITERATION (take 9, proto v86, DLL `6F15538B5ACB`) -- the host-grab-lingers regression
The v85 build (59024BFE71E9) was hands-on tested -> the CLIENT-grab path worked but the HOST grab made the
grabbed pile LINGER ~4 s on the client before converting to a ball ("convert too slow / breaks native look").
ROOT CAUSE (from the user's fresh logs: host `grab CANDIDATE cached`=4, `grab CONFIRMED by clump`=0): the v85
NOT-A-MIRROR gate in `OnLocalClumpAppeared` used `ResolveMirrorEidByActor(clump) != kInvalidId`, but the
`MirrorManager<Prop>` deliberately MIXES m_mirror=false LOCALS (the host's own piles/clumps, AllocAndInstall'd
by `MarkPropElement`) with m_mirror=true wire MIRRORS. `EnsureHeldItemBroadcast` (local_streams:285, BEFORE the
confirm at :289) AND the garbageClump Init-POST observer (`prop_lifecycle.cpp` -- fires SYNCHRONOUSLY during the
BP grab) both `MarkPropElement` the host's own clump -> it has a local eid -> the "any eid" gate FALSELY tripped
-> the host confirm never fired -> the host PropDestroy fell to the 4 s reaper. The CLIENT dodged it because its
`EnsureHeldItemBroadcast` returns early (host-authority gate) without MarkPropElement.
FIX (3-agent UNANIMOUS `B_PRECISE_GATE`): new `coop::remote_prop::IsReceivedPropMirror(actor)` (snapshot
PropMirrors, return true iff actor matches AND `p->IsMirror()`); the gate now `if (IsReceivedPropMirror(clump))
return;`. A LOCAL (IsMirror()==false) never trips it regardless of when its eid was minted; a received mirror
(IsMirror()==true) still correctly rejected (CRITICAL-1 preserved). proto v85->v86 (WIRE UNCHANGED; forces a
matched pair so a stale broken-gate peer can't connect). **The workflow REJECTED the reorder (Fix A) I almost
shipped -- the Init-POST mints the clump eid before local_streams runs, so a reorder wouldn't help; verify-via-
agents paid off again.** DEFERRED (Fix C, follow-on): make the host grab use ONE atomic PropConvert(pile->clump)
like the client-relay + throw already do (RULE 2 unification) -- closes even the sub-ms LAN two-message gap.

## CHURN-DESYNC FIX (take 10, proto v87, DLL `03E4470044E7`) -- the rapid-E-press total break
v86 (gate fix) was hands-on tested: the host grab looked good for ~10s, then the CLIENT mashed E on a pile and
the sync-mirror FULLY broke. ROOT CAUSE (DECISIVE from the client log, NOT the pile relay): rapid grab/throw
balloons the Prop Element registry to ~5800; the reaper (`prop_element_tracker::ReapDeadLocalPropElements`)
caps at `kReapEvictCap=256`/scan; `net_pump.cpp:488` misread `reaped >= kReseedPurge(64)` as a LEVEL-TRANSITION
mass-purge (12x "mass-purge detected (256 >= 64)", NO world change) -> repeated episode-end re-seed
(+2989!) + `RebindPileSeedsAfterWorldChange(force=true)` storm -> churned the client's pile eid identities ->
the re-seed minted PEER-range forward eids that SHADOWED the host mirror eids -> `ResolveMirrorEidByActor`
returned a peer eid (45693 > kHostRangeSize) -> `RelayClientGrab` no-op -> EVERY client grab failed to relay ->
total desync. The net_pump comment at :432 had PREDICTED the reap-count heuristic would break "IF a partial
purge is ever added."
FIX (2 workflows; the FIRST converged on a world-pointer gate that the adversary PROVED breaks EVERY join --
the join's save-transfer menu-shadow-drain legitimately mass-purges WITHIN one persistent world, same pointer,
and NEEDS the episode re-seed/re-bind; so a world-pointer gate is WRONG. SECOND workflow converged on the
KEYED-DEATH discriminator): keyed save props (kerfurs/doors/terminals, all carry a save-UUID Key in
`g_actorToKey`) die ONLY on a structural world event (level swap OR join shadow-drain), NEVER on interaction
(grab/throw kills only KEYLESS chipPiles/clumps). So gate the mass-purge episode on `reaped >= kReseedPurge &&
keyedReaped > 0` (the adversary's CO-CONDITION, NOT a magnitude -- a lone keyed reap like a collected
trashBitsPile stays < kReseedPurge so the AND gates it out). 4 changes: (1) `ReapDeadLocalPropElements` gains
`size_t* outKeyedReaped` (peek `g_actorToKey.count(actor)` inside `ownActorEntry` before `EraseKeyIndexForActor_`,
separate lock scope); (2) reaper log shows `X keyed / Y keyless`; (3) net_pump gate += `&& keyedReaped > 0`;
(4) `ResolveMirrorEidByActor` two-pass -- PREFER the host MIRROR eid over a peer LOCAL eid (defense-in-depth vs
the steady re-seed's peer-eid shadowing). proto v86->v87 (WIRE UNCHANGED). Did NOT do the adversary's SeedWalk
mint-skip (O(n^2) in the seed walk; the two-pass resolver covers the functional issue surgically).

## GRAB SELF-HEAL (take 11, proto v87 WIRE-UNCHANGED, DLL `7CF1298AB76E`) -- the churn-rot ROOT
v87 (churn-desync) hands-on: the mass-purge gate HELD (no false re-init during mash, VERIFIED), but the system
STILL broke under client E-spam. ROOT (from the v87 client log + a 3-agent architectural workflow): the GRAB
relay was the ONLY pile-identity resolver with NO rebind-retry self-heal on a miss. `remote_prop::OnDestroy`
(remote_prop.cpp:899) already rebinds+retries on an eid miss; the grab (`SetGrabCandidate`) just cached
kInvalidId and `RelayClientGrab` no-op'd. Under spam the bound save-pile ACTOR dies/replaces in a reload AND
`ReconcileIndexThrottled` (prop_element_tracker.cpp:456, cap 4096 x8) drains the churned local tracking, so
`ResolveMirrorEidByActor` returns kInvalidId even though the client HAS the pile -> every grab after the drain
no-op'd (7/8) -> cascade desync. THE LOG SMOKING GUN: `reaped 1904 dead (0 keyed/1904 keyless; cap 4096)` =
ReconcileIndexThrottled; then every grab `relay=no-op (eid=4294967295)`.
FIX (the adversary's exact root-cause point): `SetGrabCandidate` CLIENT branch -- on kInvalidId, call
`RebindPileSeedsAfterWorldChange(force=false)` (the SAME throttled call OnDestroy uses) + RE-RESOLVE. Restores
the rotted mirror eid for a seed-bound static pile -> the grab stays on the PRECISE eid relay. WIRE UNCHANGED
(pure client re-resolve) so proto STAYS v87.
SCOPING (deliberate, RULE 1 + risk): the workflow converged on a hybrid eid+POSITION grab relay, but the
adversary proved (a) pure position is AMBIGUOUS for dense same-variant clusters (the v87 piles were ALL
chipType=2 ~30cm apart -> position would grab the WRONG neighbor + quietly desync) and (b) the eid path must
self-heal regardless. So I shipped the PRECISE self-heal (fixes the static-pile cascade, low-risk) and
DEFERRED the position-fallback hybrid. The deferred hybrid (IF thrown-orphan piles still don't sync): wire
v88 PileGrabRequest +locX/Y/Z; RelayClientGrab sends eid=0+pos when unbound, GATED client-side on quiescence
(host HasLoadTailQuiesced is perma-false -> v82 join-divergence re-breaks if ungated); host OnPileGrabRequest
FindNearestChipPile with the v84 guard set (chipType match + 50cm + skip-mirror -- FindNearestChipPile lacks
chipType filter, must add); eid=0 adopt-by-position in TryAdoptOrDeferConvert (else dupe); + identity-precise
throw-landed-pile capture (hook chipPile Init near the dying clump) to ELIMINATE orphans at the source (the
real robust fix for clustered thrown piles -- position can't disambiguate them). Workflow run IDs:
wf_b13e1a14-9b3 (architecture) + the 2 prior (world-pointer REJECTED, keyed-death gate).

## STATE
DLL `7CF1298AB76E` x4 MATCH, proto **v87** (wire unchanged), logs CLEARED (tests archived
`research/handson_2026-06-19_{hostlinger,massreseed,churn_untrack,...}/`), runbook take 11. UNCOMMITTED on
`1272b0a3` (a LARGE stack: s28 + s29 confirm-by-clump + gate fix + churn-desync mass-purge gate + grab
self-heal). The eid-bind is SOLID (871 live, 0 unmatched) -- DO NOT UNDO. >>> NEXT: READ the fresh
`7CF1298AB76E` logs FIRST -- fix marker: during mash, client grabs keep `relay=sent (eid host-range)` + the
NEW `grab self-heal -- rebound rotted pile eid=N` line appears (was a wall of `relay=no-op eid=4294967295`).
If the CASCADE is gone but a single THROWN pile still doesn't sync -> do the DEFERRED position-fallback +
identity-precise throw capture (above). If clean: COMMIT the whole stack. Other deferred: Fix C (atomic
host-grab PropConvert), 3+peer gap, remote_prop.cpp extraction. NEVER claim fixed from smokes.
Prior: [[project-session28-pile-static-object-2026-06-19]].
