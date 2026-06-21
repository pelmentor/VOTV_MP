---
name: project-session24-position-keyed-pile-2026-06-18
description: "SESSION 24 (2026-06-18): the POSITION-KEYED ambient-pile fix (protocol v82). Session-23's invisible-handle relay was CORRECT but sat on a connect-time eid<->actor binding that ROTS (the bound save-loaded pile actors die in the JOIN load-tail/world-swap churn). Re-derived from the user's FRESH logs (root cause: bound actor 000001EEB595F600/eid4735 dead by interaction time -> OnDestroy 'no local actor' = symptom A; client grab resolves no eid = symptom B). Fix = resolve pile identity by WORLD POSITION at interaction time (MTA CObjectSync shape; an independent architect agent CONVERGED). 10km confirmed FREE (VOTV = ONE persistent ~12km resident UWorld, ZERO streaming -> host always has the pile). Built 6 files, 2-agent audit (perf zero-crit; correctness CONFIRMED-1 host-grabs-ambient-unsynced -> FIXED), deployed x4 (DLL 15:01:37), logs cleared. UNCOMMITTED on 1272b0a3. AWAITING user hands-on re-test -> READ THE FRESH LOGS FIRST."
metadata: 
  node_type: memory
  type: project
  originSessionId: 86866d94-8692-44e2-b0a3-136eb16f4d11
---

# Session 24 — POSITION-KEYED ambient pile resolution (protocol v82)

**State:** UNCOMMITTED on HEAD `1272b0a3` (session-22 baseline; ALL session-23 +
session-24 pile work is uncommitted). Deployed DLL `15:01:37` ×4 (identical).
Host+client logs cleared. **AWAITING the user's fresh hands-on re-test ->
READ THE NEW LOGS FIRST (do NOT guess — that discipline is what found this).**

Supersedes the eid-binding layer of [[project-session23-invisible-handle-pile-2026-06-18]]
(the relay is KEPT; only the fragile binding it sat on is replaced).

## What happened

Session-23 shipped the invisible-handle relay (DLL 5d1973c5). The user re-tested
and it FAILED (verbatim): *"Old piles don't get destroyed when host grabs them
(from client's perspective). When client interacts with Piles they don't get
grabbed, don't move, don't sync mirror, don't spawn at new locations based on
interactions at all (from host's perspective)."*

I **read the user's actual logs** (host `Game_0.9.0n/...` + client
`Game_0.9.0n_copy/...`) instead of guessing.

## ROOT CAUSE (log-confirmed, NOT guessed)

The cross-peer identity of a keyless chipPile is its host element-id (eid). The
client binds its local save-loaded pile actors onto host eids via the
**connect-time pile-bind** (`remote_prop_spawn.cpp` OnSpawn keyless-pile block):
during the join snapshot bracket it position-matches each incoming host
PropSpawn to a co-located local pile and `RegisterPropMirror(hostEid, localPile)`.

This **WORKED** at runtime: `pile-bind #1..#800+`, all `d=0.0cm aligned`, at
13:29:59 — 873 ambient `actorChipPile_C` mirrors registered.

**THE BUG:** the pile-bind runs DURING the connect bracket (13:29:59), which is
BEFORE the client's load-tail QUIESCENCE (13:30:04). The save-loaded pile actors
that exist at bracket time are PRE-settle actors that the JOIN load-tail /
world-swap then SILENTLY DESTROYS + replaces (the "reaped 256 dead local Prop
Element(s) (mass-purge / level-transition cleanup)" churn at 13:30:07/27/39 — a
ONE-TIME world swap, NOT ongoing streaming). So the bindings point at DEAD actors
by interaction time:
- Host grabbed ambient piles eid **4735, 4763** -> on the client, `OnDestroy:
  eid=N -> has no local actor (already destroyed or never spawned here)`
  (`ResolveLiveActorByEid` -> `IsLiveByIndex(boundActor)` false = actor died).
  The visible pile is a DIFFERENT unbound native actor -> survives = **symptom A**.
- The client's grabs (held `prop_garbageClump_C` eid=0, prop-mirror=no) ->
  `OnPileGrabPre` resolves no eid (forward map empty after Unmark; mirror map
  stale for the new actor) -> no `RelayClientGrab` -> host sees nothing = **symptom B**.

**The asymmetry that IS the bug:** the HOST re-marks every pile on stream-in via
the `prop_lifecycle` Init observer (its forward map stays current); the CLIENT
binds pile mirrors ONCE at connect and NEVER re-binds -> the bindings rot.

Decisive proof: bound actor `000001EEB595F600` (eid 4735) appears ONCE in the
client log (13:29:59 bind), dead by 13:30:37. FRESH interaction-time mirrors
(host's converted piles 5263/5264) WORK (`destroying local actor`, alive).

### Hypotheses REFUTED by static analysis (verify-don't-guess paid off, ~20 tool calls)
- **Timing race (empty bind index):** REFUTED — 873 binds succeeded at 0.0cm.
- **Kinematic conversion breaks grab:** REFUTED — `ReconcileToHostPhysics` /
  `RestoreCollisionIfNeeded` are NO-OPS for chipPiles (`GetStaticMesh` null;
  collision-restore is mushroom-class only).
- **Observer-registration conflict:** REFUTED — `SetObserverSlot` dedups by
  `(targetFn,cb)` PAIR; PRE (`OnPileGrabPre`) + POST (`GrabObserver_InpActEvt_use`)
  are separate tables, both fire.
- **Reaper severs mirrors:** REFUTED — `ReapDeadLocalPropElements` skips mirrors
  (`if (pr.mirror) continue`).
The survivor: the bound ACTOR died (join churn), the binding rotted.

## THE FIX — position-keyed lazy resolution (v82)

Stop relying on the persistent actor binding. Resolve pile identity by WORLD
POSITION at event time (an untouched pile's rest position is identical cross-peer
even as actor pointers churn). MTA precedent: `CObjectSync` reads `GetPosition()`
at lookup time, never caches a stale ptr. An independent `feature-dev:code-architect`
agent CONVERGED on the same design (2 signals).

### The 10km question — resolved FREE (no new subsystem)
User asked (3×) for interactions to sync even "10km away" + proposed a data-layer
relay / host-residency. A background agent (`votv-cave-level-structure-RE`) confirmed
**VOTV is ONE persistent UWorld, ~50,637 actors RESIDENT across a ~12km-wide map,
ZERO live level streaming** (no LevelStreaming instances, World Composition off;
counts flat as the player moves). So the host ALWAYS has the pile loaded ->
position-keyed resolves at ANY distance. **NO data-layer / residency subsystem
needed** (avoids the handover-dupe risk). Caveat (future, not piles): the nine
`sl_*` "dimension" sublevels (backrooms/void) MIGHT stream -> if a future feature
syncs THOSE, force-residency via `ULevelStreaming::SetShouldBeLoaded` is the fix
(NOT puppet-as-streaming-source — 4.27 doesn't cleanly support it; puppets aren't
streaming sources). Puppets are unpossessed orphans, not streaming sources.

### The 6-file build
- **protocol.h (v82):** `PileGrabRequestPayload` +`locX/Y/Z` (8->20B);
  `PropDestroyPayload` +`chipType`+`locX/Y/Z` (40->52B; all senders `{}`-init ->
  zero for non-pile destroys; dispatch validates `sizeof`).
- **trash_collect_sync.cpp `OnPileGrabPre`:** ROLE-FIRST. **CLIENT** relays the
  HOST-RANGE mirror eid if fresh else POSITION (mirEid=0); NEVER the client-minted
  PEER-range forward eid (`mirEid>=kHostRangeSize -> 0`, useless to the host);
  dedup by aimed-actor (not eid, since pos-relay has no eid). **HOST** ALWAYS sends
  `PropDestroy(eid-or-0, pos, chipType)` for a chipPile grab — a keyless ambient
  pile has eid=kInvalidId -> send 0 + pos (the client position-resolves). Does NOT
  mint (minting re-arms the K2_DestroyActor observer into a 2nd PropDestroy).
- **pile_handle.{h,cpp}:** `RelayClientGrab(pileEid, chipType, locX,Y,Z)`;
  `g_grabPending` + `g_pendingGrabLoc` (the UNIFIED adopt key — `TryAdoptOrDeferConvert`
  GRAB matches by `GrabPosMatches` 100cm, NOT oldEid); `OnPileGrabRequest`
  position-fallback (`FindNearestChipPile` 60cm + chipType; read/mint `hostPileEid`;
  `cp.oldEid = REAL host pile eid` so peers drop the right mirror); 5s grab-pending
  TTL (`ExpireStaleGrab` — a host-DROPPED relay can't false-adopt a later convert).
- **remote_prop.cpp:** `FindNearestNativeChipPile` (nearest chipPile, skips
  host-driven MIRRORS via `ResolveMirrorEidByActor`, chipType guard); `OnDestroy`
  position-fallback (when eid binds no live actor + keyless + non-zero pos ->
  destroy the native pile near pos = symptom A); `OnConvert` REORDER — adopt-check
  FIRST, then `OnDestroy(oldEid)` carrying position ONLY for NON-interactors (the
  grabbing client's pile already morphed, so it must NOT position-destroy a
  neighbor; a non-interactor peer DOES position-destroy its native pile).
- **remote_prop_spawn.cpp:** RETIRED the `RegisterPropMirror` ambient-pile binding
  (RULE 2). The pile-claim block now keeps `RecordClaim` + `UnmarkKnownKeyedProp`
  + `return` (the one-walk `EnsurePileBindIndex` STAYS for dup-prevention +
  sweep-spare; identity is position-keyed). NO sweep change (claim spares the pile).

### 2-agent audit
- **Perf: ZERO CRITICAL.** All GUObjectArray walks (`FindNearestChipPile`,
  `EnsurePileBindIndex`, `ResolveMirrorEidByActor`) are COLD (per-grab / per-join /
  per-death-edge), not per-frame. The one per-frame path (`TickClientThrowWatch`)
  is O(1) until a clump-death edge. 2 LOW (cacheable `FindClass` in
  `SpawnLocalTrashActor`; host duplicate mirror-resolve — folded LOW-1 into the
  role-first fix).
- **Correctness: CONFIRMED-1** = host-grabs-AMBIENT-pile was UNSYNCED — the old
  `if (eid invalid) return` bailed before sending any PropDestroy (keyless piles
  aren't seeded -> eid always invalid) -> THE common case left the pile on the
  client. **FIXED** (host always sends eid-or-0 + pos). Plus #3 (TTL), #5 (dead
  `g_pendingGrabPileEid`/`g_pendingGrabChipType` removed). Rebuilt clean.

## LOAD-BEARING FACTS (do NOT re-derive wrong)
- `kHostRangeSize = 32768` (element.h): host eids `[1,32768)`; client-minted PEER
  eids `[32768,65536)`. A client pile eid >= 32768 is USELESS to the host -> the
  client relays mirror-eid-or-POSITION, never the forward (peer) eid.
- The pile-actor churn is the **JOIN load-tail / world-swap (one-time)**, NOT
  ongoing streaming. VOTV is fully resident (~12km, ~50k actors). After the world
  settles piles are rock-stable.
- `IsChipPile` = `actorChipPile_C` lineage ONLY (NOT `prop_garbageClump_C`).
- The position-fallback is gated **keyless + non-zero position** and **skips
  mirrors** -> non-pile destroys never trip it; host mirrors are never wrongly killed.
- `trashBitsPile_C` (placed keyed collect-points, `trash_pile_sync`) is a SEPARATE
  working system — NOT the grabbable `actorChipPile_C`/`garbageClump` piles.
- The host's PropConvert(pile->clump) `oldEid` MUST be the REAL host pile eid (not
  0/the wire request eid) so other peers drop the right mirror; the grabbing client
  adopts by POSITION regardless.
- The host must NOT mint an eid for its own ambient grab (re-arms the
  K2_DestroyActor observer -> double PropDestroy).

## NEXT SESSION
1. **READ THE FRESH RE-TEST LOGS FIRST** (markers: `CLIENT pile grab -> ... relay
   (by position)`, `pile_handle[host]: PileGrabRequest ... -> host pile eid`,
   `remote_prop::OnDestroy: ... POSITION-fallback destroy of native pile`,
   `pile-claim #`). Do NOT guess.
2. If clean: **commit** the baseline (session-22 `1272b0a3` parent first, then the
   session-23 + session-24 pile work) + build the **INTERACTION SMOKE** (STILL
   unbuilt — THE verification gap; joinchurn tests JOIN only;
   [[feedback-interaction-smoke-not-join-smoke]]).
3. DEFERRED audit follow-ups: #2 (`TickClientThrowWatch` 250cm landed-pile radius
   has no mirror/chipType guard — tighten); modular extraction (`remote_prop_spawn.cpp`
   1347 / `remote_prop.cpp` 1091 / `prop_lifecycle.cpp` 914 -> a `coop/pile_claim.cpp`
   co-located with `pile_handle.cpp`); LOW perf caches.
4. Carried: the HUD dev-feature request (still needs clarification).
