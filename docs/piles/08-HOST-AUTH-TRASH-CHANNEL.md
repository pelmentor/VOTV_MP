# 08 — HOST-AUTHORITATIVE TRASH CHANNEL (the pile-sync redesign)

> **Status: DESIGN (locked 2026-06-21), pre-implementation.** Supersedes **07** (the MORPH V2
> re-skin + proximity land-watch — RETIRED: it false-fired in dense pile clusters and never wired the
> client→host direction; the autonomous smoke that "verified" it was a false positive on a sanitized
> single pile). Foundation = the 4-investigator + synthesis workflow `pile-sync-understand`
> (2026-06-21), grounded in the chipPile/clump bytecode + the SDK + the real hands-on failure logs +
> the MTA precedent. Status lifecycle: DESIGN (this) → AS-BUILT → VERIFIED (real hands-on, both
> directions, in a CLUSTER — NEVER a smoke). [[feedback-docs-piles-living-knowledge-base]]

## Why 07 (the morph) failed in the real game (hands-on 2026-06-21)

- **Host grab:** the morph plumbing fired (grab armed → ADOPT → convert → carry → release → land →
  convert), all in ~1s, client received both converts — but the user saw nothing sync, because the
  **land-watch `FindNearestChipPile(lastPos, 100cm)` grabbed a NEIGHBOR pile** in the cluster (log:
  original (1214,940), re-pile reported (1132,937), a 21-22cm neighbor) → eid rebound to the wrong
  pile → divergence. This is the exact unsound position-consume the DECISIVE dupe-RE said to retire;
  the morph re-introduced it at the LAND edge.
- **Client grab:** ZERO `pile_morph: grab armed` on the client, ZERO converts to the host → the
  client→host direction **was never wired** (the client morphed locally but sent nothing). The host
  kept its pile; the user saw only the client puppet's grab animation.

## The VERIFIED mechanic (byte-exact — bytecode + SDK + live log; finally not guessed)

**`actorChipPile_C` = CARRY-AND-THROW** (never collected — every collect/pickup/hold verb is
hard-false/no-op). **`trashBitsPile_C` = COLLECT** (a SEPARATE entity: `playerTryToCollect` →
`trashToProp` → spawn one `Aprop_C` + decrement count + self-destroy at 0; rides the existing keyed-
Aprop lane — do NOT force it through the carry channel).

```
PILE (actorChipPile_C; keyless; chipType@0x0238; fresh-load pos is map-deterministic)
  └─[E-press; lookAtActor==pile]→ playerGrabbed @2607: BeginDeferred(clump) @2748 ·
       clump.chipType:=pile.chipType @2790 · clump.holdPlayer:=player @2831 · FinishSpawning @3013 ·
       player.pickupObjectDirect(clump) @3051 · K2_DestroyActor(self=pile) @2563
       ⇒ CLUMP-HELD   [spawn 1 clump, destroy 1 pile; ONLY chipType carried]
CLUMP-HELD (prop_garbageClump_C; keyless; holdPlayer set; held via PHC grabHandle)
  ├─ convert ABORTS while the holder is still grabbing (@2927 gate)
  ├─[E-press on a NEW pile]→ grabbing_actor reassigned; THIS clump silently RELEASED (NO merge) ⇒ FLYING
  └─[grab decays / drop]→ grabbing_actor:=null; PHC release ⇒ FLYING  (residual sim vel ~34cm/s —
       NOT a thrown impulse; the PHC just lets go as it settles)
CLUMP-FLYING (simulating; grabbing_actor null)
  └─[OnComponentHit #1]→ delayOnHit:=false @2716   (ARM ONLY — no convert on the 1st contact)
  └─[OnComponentHit #2; canConvert && !holder.grabbing_actor && Dot(N,Up)>0.75 && !hitComp.IsSimulating]
       SphereTrace(10cm)→restingXform @27 · BeginDeferred(pile,restingXform) @1722 ·
       pile.chipType:=clump.chipType @1764 · FinishSpawning @2557 · pile.SetLifeSpan @2595 ·
       K2_DestroyActor(self=clump) @2640
       ⇒ PILE (NEW, at the clump's SPHERE-TRACED RESTING POINT — NOT lastPos, NOT the source pile pos)
```

**Decisive corrections vs 07:** the re-pile is at the clump's **resting point**, not `lastPos` (so
proximity-from-lastPos was doomed); the "release ~1s later" is the PHC settling, not a throw/timer; a
2nd grab while holding **replaces, never merges**; only `chipType` (a cosmetic variant @0x0238)
carries — it is NOT a unique id. Piles are keyless (`Key=None`); identity must be our eid.

## The OBSERVABILITY reversal (load-bearing — corrects the old pass2 "unobservable" claim)

- **INVISIBLE** (`EX_LocalVirtualFunction`→ProcessInternal, bypasses our PE detour): `playerGrabbed`,
  `pickupObjectDirect`, both `K2_DestroyActor(self)`. (A reflection `CallFunction(InpActEvt_use)` fires
  our PRE/POST observer but NOT the input-gated BP body — see COOP_DISPATCH_VISIBILITY observe-vs-drive.)
- **VISIBLE** (reach ProcessEvent — our detour fires):
  - `InpActEvt_use` PRE/POST — the one observable grab-INTENT seam (pile alive at PRE).
  - **`BeginDeferredActorSpawnFromClass` POST + `FinishSpawningActor` POST** (both `EX_CallMath`) —
    PROVEN: `host_spawn_watcher` already catches these for `garbagePileSpawner` (host_spawn_watcher.cpp
    reads the ReturnValue + SpawnTransform params at the POST). Extending its param reads to the
    **WorldContextObject (= `self`, the dying clump / the converting pile)** gives, at the convert
    spawn POST, BOTH the new actor (ReturnValue) AND the source (WorldContextObject) — **the
    deterministic, ZERO-PROXIMITY clump↔pile link.** The SAME POST catches BOTH the grab (pile→clump)
    and the re-pile (clump→pile).
  - `reflection::CallFunction(player, playerGrabbed, frame)` re-enters PE and runs the REAL verb — the
    mechanism for the host to execute a client's grab intent.

## Root causes (file:line)

- **Client grab never arms** — `trash_collect_sync.cpp` `OnPileGrabPre`: (1) DOMINANT the hands-full
  gate `:284-288` (`grabbingActor||holdingActor` → silent return) — a chipPile carry leaves a clump in
  `holding_actor` that clears only on an explicit drop, so grabbing pile #2 reads "hands full"; (2) the
  silent `kInvalidId` eid return `:293-296`. And the client→host direction was never wired
  (`pile_morph.cpp:140` dead-ends if not armed). Confirm the exact slot/return with **PROBE-A** (below).
- **Host land false-fire** — `pile_morph.cpp:189` `FindNearestChipPile(lastPos,100cm)` consumes a
  cluster neighbor; `:198` rebinds eid to the wrong pile.

## THE ARCHITECTURE — host-authoritative trash-entity state machine

MTA single-syncer + sync-time-context, **structurally cloned from the shipped door channel**
(`interactable_channel.h:220 OnRequest`, `Channel::Mode::HostAuth`). Principle 6 / the door rule: the
non-authority moves ONLY on host echoes.

**Data model — the eid is a host-minted, life-stable logical trash entity** (re-skinned in place
pile↔clump↔pile; keep `oldEid==newEid==E`). **Position is NEVER identity anywhere** → the cluster
mis-bind is impossible by construction.

```
TrashEntity { eid; state(PILED@xform | HELD_BY(slot N) | FLYING(vel)); chipType; xform; ctx(uint8) }
```
The host owns the authoritative state; every receiver (incl. a client that initiated a grab) drives a
LOCAL visual actor from that state — never advances state locally, never guesses identity by proximity.

**Cluster-safe identity, NO proximity:**
1. **eid end-to-end.** DELETE `FindNearestChipPile`. The clump→pile link is the **convert spawn POST**:
   at `BeginDeferred(self=clump, pile, restingXform)` POST, `BoundEidOf(self-clump)` → rebind that eid
   onto ReturnValue (the exact new pile). Zero spatial search (the host_spawn_watcher seam).
2. **MTA sync-time-context (`ctx` byte).** Stamp every PropConvert/Pose/Release; bump on EVERY
   transition; receivers drop stale-ctx packets (port `CElement::GenerateSyncTimeContext`/`CanUpdateSync`,
   MTA `CElement.cpp:1281/1300`). A late pose/land packet for eid E after a transition is REJECTED —
   never re-applied to a neighbor. (The single guard the morph lacked.)
3. **Drain survival (MTA EntityAdd-on-rescope).** On the shadow-drain edge the client sends
   `PileResyncRequest`; the host re-streams `PropSpawn` per live pile (host eid preserved). The client
   instantiates a pile mirror ONLY from a host PropSpawn (`IsReceivedPropMirror` the sole keep/doom
   discriminator) — no rotting cached seed.

**Packets (both directions symmetric through the host):**
```
HOST → ALL (authoritative state change):
  PropConvert{eid, kind(kToClump|kToPile), ctx, xform, chipType}
  PropPose{eid, ctx}                      // carry (existing held-pose stream + ctx)
  PropRelease{eid, vel, ctx}              // throw
  PropSpawn{eid, class, xform, chipType}  // (re)scope-in incl. resync reply
  PropDestroy{eid}                        // collect/despawn (no re-pile)
CLIENT → HOST (intent/request, NOT a state push):
  GrabIntent{eid}   ThrowIntent{eid}   PileResyncRequest{}
```

**Host-grab** (host is authority AND grabber): `InpActEvt_use` PRE resolves the aimed pile's eid → host
applies PILED→HELD_BY(0), bump ctx, broadcast `PropConvert{kToClump}`. Carry streams `PropPose`.
Throw → HELD→FLYING, `PropRelease`. Land caught at the **convert spawn POST** (the BP's own resting
xform — no proximity) → FLYING→PILED, `PropConvert{kToPile, xform}`.

**Client-grab** (the dead direction — the door `OnRequest` pattern verbatim):
```
client InpActEvt_use PRE: SUPPRESS the native BP grab for THIS dispatch (null lookAtActor — the
   device_screen ClearAimForDispatch analog — so icast(lookAtActor)→playerGrabbed fails, NO local
   clump spawns; this kills the local eid=0 dupe at the source), then send GrabIntent{eid}.
host OnGrabIntent (role==Host): validate state==PILED && !HELD (per-peer guard, door holdOpen_ analog);
   EXECUTE the real grab on puppet-N: reflection::CallFunction(pile, playerGrabbed, {puppetN, hit});
   PILED→HELD_BY(N); bump ctx; broadcast PropConvert{kToClump} to ALL incl. the requester.
every peer mirrors the host's authoritative convert ⇒ ONE actor per eid, no local dupe.
```
This single move fixes BOTH the dead client direction AND the local-clump dupe (the proven door fix).
**Fix the silent returns:** never `return` on `kInvalidId` without a path (queue the intent against the
pending resync); exempt the hands-full gate for a garbageClump-family carry (gate only on a genuine
PHC hold that truly blocks a new grab).

**Carry / re-pile / collect:** carry = the existing held-pose stream + `ctx` (no new stream code);
re-pile = owner-authoritative `PropConvert{kToPile, xform}` from the convert-spawn POST's resting xform,
receiver claims/adopts (re-skin in place — NEVER destroy-and-recreate); collect (`trashBitsPile`) = a
SEPARATE transition table on the keyed-Aprop lane (count decrement + spawn item + PropDestroy at 0).

**MTA precedent (cited):** `CObjectSync.cpp` single-syncer (`GetSyncer`/`SetSyncer` :47/:140, syncer-gated
`Packet_ObjectSync` :214, re-broadcast :234); `CStaticFunctionDefinitions.cpp` `AttachElements`/`Detach`
:1602/:1656 (host-broadcast carry transition by ID :1644 + ctx on transition :1689); `CElement.cpp`
`GenerateSyncTimeContext`/`CanUpdateSync` :1281/:1300. In-tree proven instance: the door channel
(`interactable_channel.h:220`, `interactable_sync.cpp:221-290`). All driven by reflected UFunctions +
state push — no BP-asset edits, no Replicated props/RPCs, no pak edits (A6 respected).

## Implementation plan

- **Step 0 — PROBE-A** (one log line atop `OnPileGrabPre`, reading `gs` once): role, aimed, isChipPile,
  grabbing/holding slot, fwd+mirror eid. One client grab in a cluster confirms the carry slot + which
  early-return fires. NON-BLOCKING (the design fixes both regardless).
- **Step 1 — DELETE the morph's fragile parts (RULE 2, fully):** `pile_morph.cpp` proximity land-detect
  (`FindNearestChipPile` :189, the Tick land branch :178-220, `lastPos` :181, the deferred-PropDestroy
  fallback :64,170); the silent `kInvalidId` return + the clump-blocking hands-full behavior in
  `trash_collect_sync.cpp`. Salvage only the eid-rebind-in-place primitive (moves to the convert handler).
- **Step 2 — Spawn-catch identity:** extend `host_spawn_watcher` `IsMirroredPropClass` to
  `actorChipPile_C` + `prop_garbageClump_C`; resolve the WorldContextObject param; at the BeginDeferred/
  FinishSpawning POST, link `BoundEidOf(self-clump)` → ReturnValue (new pile), rebind + broadcast
  `PropConvert{kToPile, xform, ctx}`. Zero proximity.
- **Step 3 — NEW `coop/trash_channel.{cpp,h}`** (one feature, keep `trash_collect_sync.cpp` from
  bloating — check `wc -l`, extract past 800): the `TrashEntity` state map + `OnGrabIntent`/`OnThrowIntent`/
  `OnResyncRequest`, cloned from `interactable_channel.h:220`. Host validates, executes the real verb,
  bumps ctx, broadcasts.
- **Step 4 — Protocol (wire in THREE places, [[feedback-reliablekind-router-checklist]]):** bump
  `kProtocolVersion 81→82`; `ctx` byte on PropConvert/Pose/Release (from `_pad`); `GrabIntent`/
  `ThrowIntent`/`PileResyncRequest` kinds — enum/payload + family dispatcher + `event_feed.cpp` master
  router.
- **Step 5 — Client suppress-native + GrabIntent; drain-resync:** client `InpActEvt_use` PRE nulls
  lookAtActor for that dispatch + sends `GrabIntent{eid}`; on `kInvalidId` queue against the pending
  resync. Drain edge → `PileResyncRequest` → host re-streams `PropSpawn` per live pile.
- **Step 6 — ctx enforcement:** port `GenerateSyncTimeContext`/`CanUpdateSync`; bump on every transition;
  drop stale-ctx on receive.

## How to VERIFY for real (the smoke the morph faked)

The prior smoke false-passed by calling `playerGrabbed` directly on ONE sanitized pile — never the
cluster, never the input seam, never the client direction. This time:
1. **Real input seam** (drive `InpActEvt_use` / key-inject so the PRE + suppression actually run) — an
   interaction smoke, not a join smoke ([[feedback-interaction-smoke-not-join-smoke]]).
2. **A CLUSTER** — 4+ piles within ~30cm (the failing geometry). Grab one, carry, throw to re-pile among
   neighbors; assert exactly ONE actor per eid on BOTH peers + the re-pile bound to the CORRECT eid.
3. **BOTH directions** — host-grab→client-mirror AND client-grab→host-executes→both-mirror; assert the
   host pile actually disappears on a client grab.
4. **Pre-handoff checklist (RULE 2026-05-27):** hot-path re-entry table, `wc -l` modularity, deploy ×4,
   ≥30s LAN smoke via the named-window launchers, host+client log diff clean, RSS stable; audit agents.
5. **User hands-on is the final gate** — real cluster, both directions. "Works" only after that, NEVER
   from a smoke.

## Open [?] (verify during implementation, none blocking the design)

- **PROBE-A** — the carry slot (grabbing vs holding) + which early-return; Step 0.
- **`reflection::CallFunction(pile, playerGrabbed, {puppetN, hit})` on a PUPPET** — confirm the synthesized
  `HitResult` drives `pickupObjectDirect` so puppet-N visibly holds the clump on the host; verify the
  param layout during Step 3 (the verb is VISIBLE-re-entrant → exercisable in the smoke).
- **`BndEvt__...ComponentHit` PE-visibility** — candidate-VISIBLE but NOT on the critical path (we use the
  BeginDeferred/FinishSpawning POST). A future tightening only.
