# 05 — Morph / held-pile coop sync (the final pile increment)

> Design for the grab→carry→throw→land round-trip on the **NOW-RESTORED adopt-BIND model**
> (HEAD `1272b0a3`). Synthesized 2026-06-20 from the morph RE
> (`findings/votv-chippile-clump-morph-RE-2026-05-27.md`), the robust-design target
> (`04-ROBUST-DESIGN.md`), and a full read of the current code. Supersedes the morph half of
> the RE doc's §6-8 packet plan (that plan was written for the host-spawns-a-second-actor
> NonPropEntity pipeline, which is gone). The s23 thin-client morph relay (pile_handle.cpp) is
> deleted; this is the bind-model design that does NOT re-introduce the 10-session churn.

---

## 0. The one idea that makes this minimal

**The clump CARRIES the grabbed pile's eid `E` across all three morph UObjects.**

On the bind model BOTH peers own the same pile, bound to the shared host-minted eid `E`
(`remote_prop::RegisterPropMirror(E, ownPile)`). When the pile morphs, each peer morphs **its
own bound actor** and the two stay in agreement because they key on the same `E`. The morph is
therefore **symmetric** — there is no "host spawns, client mirrors a fresh actor" asymmetry,
hence no second actor, no doom, no catalog. The only cross-peer messages are two
`PropConvert(E)` edges (pile→clump on grab, clump→pile on land) plus the *already-working*
held-pose stream, fixed so the held clump streams under `E`.

The wire identity `E` is the stable thread; the three UObjects (pile-A, clump, pile-B) are
transient local renderings of it on each peer.

---

## 1. Eid-transfer mechanism — how the clump gets `E`

### 1.1 The grabbing peer (local morph)

The grab seam already exists and already resolves `E`:
`trash_collect_sync.cpp::OnPileGrabPre` (`trash_collect_sync.cpp:379`), the `InpActEvt_use`
PRE observer. It reads `mainPlayer.lookAtActor` = the pile **while it is still alive**, and
resolves the shared eid:

```cpp
// trash_collect_sync.cpp:387-390 (EXISTING)
coop::element::ElementId eid = PT::GetPropElementIdForActor(aimed);   // a pile WE own
if (eid == coop::element::kInvalidId)
    eid = coop::remote_prop::ResolveMirrorEidByActor(aimed);          // a pile WE mirror (host eid E)
```

So at grab time we have `E` and the pile actor, **before** the BP ubergraph runs `toClump()`
(spawns the keyless clump, destroys the pile). The problem: the spawned clump is a *different*
UObject and nothing carries `E` onto it.

**Transfer step (NEW, local):** stash `E` keyed by the grabbing player, then bind the clump to
`E` the instant it is born. The clump's birth is observable — the existing Init-POST observer
`prop_lifecycle.cpp::GrabObserver_Aprop_Init_POST` already fires on `prop_garbageClump_C` (it is
registered in `InstallTrashLateLoadInitObserver`, `prop_lifecycle.cpp:500-538`). We intercept the
clump there:

```
OnPileGrabPre(self=mainPlayer):
    aimed = lookAtActor; require IsChipPile(aimed); resolve E (above)
    g_pendingMorph = { player: self, fromEid: E, ts: now }     // a single-slot latch, TTL ~500 ms
    // (NO PropDestroy here anymore — see §2.1; the PropConvert replaces it)

clump Init-POST (self=prop_garbageClump_C):
    if g_pendingMorph is live and within TTL and clump.holdPlayer == g_pendingMorph.player:
        adopt:  remote_prop::RegisterPropMirror(E, clump, key="", cls="prop_garbageClump_C")
                // rebinds eid E from pile-A onto the clump — same eid, new actor
        broadcast PropConvert{ oldEid=E, newEid=E, kind=ToClump, chipType, holdPlayerSlot }
        clear g_pendingMorph
        return    // suppress the normal keyless-clump PropSpawn fall-through (§2.1)
```

`clump.holdPlayer` (offset `0x0240`, `prop_garbageClump.hpp:10`) is the correlation key: it is set
to the grabbing `mainPlayer_C` inside `toClump()`, so it uniquely pairs the just-spawned clump to
the pending grab — robust even if two piles are grabbed within the same tick by two peers (each
peer only ever has its OWN local mainPlayer as `holdPlayer`; puppets are unpossessed and never
grab — see `OnPileGrabPre`'s "puppets never process input" note, `trash_collect_sync.cpp:377`).

`E` is **reused** as both `oldEid` and `newEid` (`oldEid==newEid==E`). This is the deliberate
divergence from the v52 `PropConvert` semantics (which minted a *fresh* `newEid`): on the bind
model we are NOT creating a new cross-peer entity, we are **re-skinning the same one**. Keeping
the eid identical is what lets the receiver re-point its own bound actor instead of
destroy+spawn.

### 1.2 The mirror peer (via the wire)

The receiver gets `PropConvert{ oldEid=E, newEid=E, kind=ToClump, ... }`. It already has a local
pile bound to `E` (its own save-loaded, adopt-bound pile). It does the **same local morph**
against its own actor — but it cannot run the BP `toClump()` (that is a local-input-only BP path;
firing it on a puppet-less mirror is fragile and would re-enter our observers). Instead the
receiver does the **minimal visual morph**: destroy its bound pile-A actor and spawn a local
clump actor bound to the SAME `E`. Crucially this is **not** a "second actor" in the churn sense —
it is the receiver's single rendering of `E`, swapped in place, exactly mirroring what the grabber
did locally. One actor per peer per eid, before and after.

```
receiver OnConvert(kind=ToClump, E):
    pile = ResolveLiveActorByEid(E)                 // its own bound pile-A
    spawn a local prop_garbageClump_C at pile's transform (chipType from payload)
    RegisterPropMirror(E, clump, ...)               // rebind E: pile-A -> clump  (replaces the old binding)
    echo-suppressed K2_DestroyActor(pile)           // remove pile-A; E now renders as the clump
    // physics OFF on the clump (kinematic) — the held-pose stream drives it (§2.2)
```

Then the held-pose stream (already running, fixed per §2.2) drives the clump under `E`, so it
floats in the grabber-puppet's hands on the mirror peer — identical to how the kerfur-prop and
mannequin held-pose mirrors already work.

---

## 2. Packets / observers — the exact wire + hooks

### 2.1 On grab — `PropConvert{ToClump}` REPLACES the `PropDestroy`

Today `OnPileGrabPre` sends a bare `PropDestroy(E)` (`trash_collect_sync.cpp:401-404`) and the
held clump separately tries to express itself via `EnsureHeldItemBroadcast` (which is now
**gated off for clients** by the PART-2 host-authority gate, `trash_collect_sync.cpp:186`, and for
the host produces a keyless clump `PropSpawn` with a *fresh* eid — the eid=0 / second-identity
problem). Both are replaced:

- **`OnPileGrabPre`**: stop sending `PropDestroy(E)`. Instead arm `g_pendingMorph` (§1.1). The
  `PropConvert{ToClump, E}` emitted at clump-Init-POST is the single grab signal — it tells the
  receiver "convert your pile-A bound to `E` into a clump", which subsumes the destroy.
  - *Why move the emit to Init-POST, not keep it in the PRE?* At PRE the clump doesn't exist yet,
    so we have no `chipType`-from-clump and no actor to bind. Init-POST is the first instant both
    exist. (The pile's own chipType could be read at PRE and stashed, but reading it off the clump
    at POST is simpler and is the variant that actually round-trips.)
- **`EnsureHeldItemBroadcast` (`trash_collect_sync.cpp:147`)**: the clump must NOT emit a keyless
  `PropSpawn` anymore — its identity is `E`, delivered by the `PropConvert`. Add an early return:
  if the held actor is a `prop_garbageClump_C` that is already bound to an eid
  (`ResolveHeldPropEid(clump) != kInvalidId`, true after the Init-POST adopt), return false (no
  spawn). This is the analogue of the existing kerfur gate at line 159. The host path stops
  minting a second clump identity; the client path was already suppressed.
  - **RULE-2 cleanup:** with the clump now riding `PropConvert`, the entire keyless-clump
    `PropSpawn` + `WatchClump` + `BroadcastConvertNear` death-watch machinery in
    `trash_collect_sync.cpp` (lines 78-143, 329-361) is **retired** — its job (express the clump,
    then catch the unobservable land-morph by proximity) is replaced by the symmetric
    Init-POST-observed convert pair. See §2.4 for the land half. Removing it also deletes the
    `FindNearestChipPile`-on-owner land guess, a long-standing fragility.

`PropConvert` payload — **reuse the existing struct** (`protocol.h:3105`), with a 1-byte `kind`
discriminator carved from its `_pad[3]`:

```cpp
struct PropConvertPayload {        // size stays 100 B — no version-gating churn beyond the kind byte meaning
    uint32_t oldEid;               // = E   (the bound pile/clump being re-skinned)
    uint32_t newEid;               // = E   (SAME eid on the bind model; identity is preserved)
    WireClassName pileClass;       // ToClump: the clump leaf class; ToPile: the chipPile leaf class
    float locX, locY, locZ;        // resting/grab transform
    float rotPitch, rotYaw, rotRoll;
    uint8_t chipType;              // trash variant (carried across both edges)
    uint8_t kind;                  // NEW: 0 = ToClump (grab), 1 = ToPile (land)   [was _pad[0]]
    uint8_t _pad[2];
};
```

`kind` lets ONE handler do both directions (RULE 2 — one convert concept, one packet). The
existing event_feed validation (`event_dispatch_entity.cpp:307-356`) is unchanged except it now
also accepts `oldEid==newEid` (it already range-checks both; no new check needed).

**Host-authority for the grab:** when a CLIENT grabs, `OnPileGrabPre` resolves `E` from the
*mirror* (`ResolveMirrorEidByActor`) — i.e. the host's eid. The client emits
`PropConvert{ToClump, E}` which the host relays to other clients AND applies itself (the host's
own pile-A bound to `E` becomes a clump on the host). This is the host-authoritative outcome
without a request/relay round-trip: `E` is the host's identity, so every peer including the host
converges on it. (See §3 for the both-grab-same-pile conflict.)

### 2.2 The held clump streams under `E` — fix `ResolveHeldPropEid`

This is the eid=0 gap (gap #1 in the brief). `local_streams.cpp::ResolveHeldPropEid`
(`local_streams.cpp:171`) currently resolves only (a) the local forward map and (b) the kerfur
mirror map — neither knows the clump. After the §1.1 adopt, the clump is bound to `E` in the
**prop MirrorManager** (`RegisterPropMirror`). Add a third fallback:

```cpp
coop::element::ElementId ResolveHeldPropEid(void* heldActor) {
    auto eid = coop::prop_element_tracker::GetPropElementIdForActor(heldActor);   // owned (host re-grab)
    if (eid == kInvalidId) eid = coop::kerfur_entity::GetKerfurMirrorEidForActor(heldActor);
    if (eid == kInvalidId) eid = coop::remote_prop::ResolveMirrorEidByActor(heldActor);  // NEW: the clump's E
    return eid;
}
```

`ResolveMirrorEidByActor` (`remote_prop.cpp:747`) is an O(n) Snapshot walk, but the held-prop
edge calls it only on the **new-held edge** (`local_streams.cpp:277`, `heldActor != g_lastHeldProp`)
and on the per-tick stream where it is cheap relative to the send. The new-held edge is the
correct place; for the per-tick body, cache the resolved eid in `g_lastHeldProp`'s sibling (a
`g_lastHeldEid`) so the steady stream is O(1) (mirrors the existing `g_lastHeldKey` cache). With
the clump bound to `E`, the held PropPose carries `pp.elementId = E` (`local_streams.cpp:298`),
the receiver resolves the clump mirror by eid (`remote_prop.cpp` Tick → `ResolveLiveActorByEid`,
`remote_prop.cpp:307`), and the carry renders. **No new stream code** — the gap was purely the
missing eid resolution.

### 2.3 No new spawn observer for the clump

The clump's Init-POST is ALREADY observed (`prop_lifecycle.cpp:500-538` registers the POST
observer on `prop_garbageClump_C`). The §1.1 adopt is added INSIDE
`GrabObserver_Aprop_Init_POST_Body` (`prop_lifecycle.cpp:125`), gated on a clump class + a live
`g_pendingMorph`. When it adopts+converts it returns before the keyless `PropSpawn` fall-through
(§2.1) — so the clump is expressed exactly once, as a convert, never as a spawn.

**Exact placement (load-bearing):** insert the adopt block AFTER the early guards that must run
first —
`IsLive` (`:135`), `IsKeyedInteractable` (`:136`), the `Default__` CDO filter (`:145`),
`MarkKnownKeyedProp` (`:149`), `LoadSession`+`connected()` (`:151-153`) — and AFTER
`ConsumeIncomingSpawn` (`:154`) / `HasProcessedInit` (`:159`). The grabber's freshly-spawned
clump is **not** wire-received, so `ConsumeIncomingSpawn` does not suppress it (correct — we WANT
to express it, as a convert); and it has not been processed before, so the dedupe passes. Put the
block immediately after `MarkProcessedInit(self)` (`:164`), before the `g_takeObjInFlight` and
role/per-player/keyless logic (`:169+`). That position means: the morph adopt fires for the
grabber's own clump on BOTH roles (host and client) — which is exactly the symmetry we want (each
peer expresses its own clump's identity transfer; the CLIENT host-authority gate in
`EnsureHeldItemBroadcast` does NOT apply here because this is the convert path, not the keyless
held-spawn path). The mirror peer's clump (spawned by `OnConvert`, §2.5) is created with
`MarkIncomingSpawn`/echo-suppression so it hits `ConsumeIncomingSpawn` (`:154`) and returns early
— it must NOT re-broadcast a convert. Verify that suppression is armed when `OnConvert` spawns the
mirror clump (mirror the existing `MarkIncomingDestroy` pattern with the spawn-side suppressor).

### 2.4 On throw / land — `PropConvert{ToPile}`, symmetric

The land morph (clump hits ground → BP spawns a fresh `actorChipPile_C` via `clump.pile`,
destroys the clump) is **the mirror image of the grab**, and it is observed the SAME way: the
**new chipPile's Init-POST fires** (the same observer, registered on `actorChipPile_C`). At that
instant the clump still has `E` (we bound it at grab); the new pile does not yet. We bridge them:

```
clump Init-POST already happened at grab. Now on THROW:
  local_streams release edge (local_streams.cpp:331) detects held->not-held for the clump.
  It already reads the throw velocity. ADD: stash g_pendingLand = { clumpEid: E, lastClumpPos, vel, ts }.
  (A throw is the ONLY way E leaves the hand; the release edge is the exact, input-driven seam —
   NOT a proximity/death poll. This is why it can't over-fire like the s23 throw-watch.)

new chipPile Init-POST (self=actorChipPile_C):
  if g_pendingLand is live (TTL ~3 s) AND this pile is within ~200 cm of g_pendingLand.lastClumpPos
     AND its chipType matches:
        adopt: RegisterPropMirror(E, newPile)        // rebind E: clump -> pile-B
        broadcast PropConvert{ oldEid=E, newEid=E, kind=ToPile, pileClass, chipType, transform }
        clear g_pendingLand
        return    // suppress the keyless PropSpawn fall-through for this landed pile
```

The TTL + position + chipType gate is what keeps the land detection from mis-firing: it only
adopts a pile that appears near where THIS peer's clump last was, shortly after a throw. A pile
that spawns for any other reason (a different peer's clump, a garbagePileSpawner late tail) does
not match the pending-land anchor and falls through to its normal path. **This is sound on the
owner** (the pile genuinely spawns at the clump's last position locally) — the unsound case the
RE retired was doing this on the RECEIVER, where piles are not co-located. Here each peer adopts
its OWN landed pile against its OWN clump's last position; the wire only carries the
`PropConvert{ToPile, E}` so all peers re-skin `E` from clump back to pile.

**Receiver `OnConvert(kind=ToPile, E)`:** destroy its clump bound to `E`, spawn a settled local
`actorChipPile_C` (physics-resting, QueryAndPhysics so it's re-grabbable) bound to `E` at the
host-authoritative transform from the payload. The pile is now a normal bound pile again — a
subsequent grab re-enters `OnPileGrabPre` and the whole cycle repeats. The **landing position is
host-authoritative** because the host's `ToPile` convert (or its relay of a client's) carries the
host's pile transform; clients apply that, not their own local sim landing.

### 2.5 What `remote_prop::OnConvert` must do (the receiver, rewritten)

The current `OnConvert` (`remote_prop.cpp:887`) is destroy-oldEid + **fresh-spawn newEid via
OnSpawn**. On the bind model with `oldEid==newEid==E` that is wrong (it would
UnregisterMirror(E) then try to OnSpawn a brand-new actor at E). Rewrite it to **re-skin in
place**, branching on `kind`:

```cpp
void* OnConvert(payload, localPlayer, senderSlot) {
    void* cur = ResolveLiveActorByEid(payload.oldEid);    // our bound rendering of E (pile-A or clump)
    // Spawn the NEW rendering bound to the SAME eid, at the payload transform.
    void* next = (payload.kind == ToClump)
        ? SpawnLocalClump(payload.chipType, transform)             // kinematic, physics off
        : SpawnLocalPile(payload.pileClass, payload.chipType, transform);  // settled, QueryAndPhysics
    if (!next) return nullptr;
    RegisterPropMirror(payload.newEid, next, ...);        // newEid==oldEid==E: replaces the binding
    if (cur && cur != next) {
        ClearAnyDriveFor(cur);
        MarkIncomingDestroy(cur); K2_DestroyActor(cur);   // echo-suppressed; remove the old rendering
    }
    return next;
}
```

`SpawnLocalPile` is the existing settled-pile spawn (today's OnConvert step 2 minus the fresh
eid). `SpawnLocalClump` is a thin new helper (bare `SpawnActor(prop_garbageClump_C)` + physics
off + chipType stamp) — the clump renders its own dirtball mesh, no mesh transfer, exactly as the
old keyless clump mirror did. The `RegisterPropMirror` with `newEid==E` overwrites the prior
binding (the manager's Install is idempotent-by-eid; replacing the actor is the rebind). Order:
spawn-new → rebind → destroy-old, so `E` always resolves to a live actor (no flicker where `E`
points at nothing).

---

## 3. Both directions + the host-authority conflict

### 3.1 Symmetry on the bind model — confirmed

| | Host grabs a pile | Client grabs a pile |
|---|---|---|
| Resolve `E` | forward map (`E` is host's own) | mirror map (`E` is the host's eid the client adopted) |
| Local morph | own pile-A → clump, bound to `E` | own pile-A → clump, bound to `E` |
| Wire | host broadcasts `PropConvert{ToClump,E}` → all clients | client sends `PropConvert{ToClump,E}` → host relays → all other clients; **host also applies it** |
| Mirror peers | each re-skins its `E` pile→clump | each re-skins its `E` pile→clump (incl. the host) |
| Carry | host puppet hands (host's pose stream) | client puppet hands (client's pose stream) |
| Land | host broadcasts `PropConvert{ToPile,E}` (authoritative transform) | client sends it; host relays + applies |

It is genuinely symmetric: the only role-asymmetry is who relays (the host always relays a
client's convert to the other clients, and applies it locally — the standard
`SendReliable`/relay path every symmetric prop kind already uses, e.g. doors, stick-state).
`E` being host-minted is what makes "host also applies the client's convert" correct — the host
has a pile bound to `E` too (it is the authoritative pile), so it morphs it, and never spawns a
duplicate.

### 3.2 The conflict: both peers grab the SAME pile `E`

Two peers can press E on the same pile within the network RTT before either's convert arrives.
Each locally morphs its pile-A bound to `E` into a clump-in-hand. Then peer-B receives peer-A's
`PropConvert{ToClump,E}` (and vice-versa) — each tries to convert an eid that is already a clump
in its own hand. **Simplest correct resolution: the host arbitrates by first-arrival on `E`.**

- The host processes converts on `E` in arrival order. The **first** `ToClump{E}` it sees (its
  own, or the first client's) is the winner; it relays that one. A **second** `ToClump{E}` for an
  eid already in the `ToClump` state is **dropped by the host** (a tiny per-eid "current morph
  state" check — `E` is already a clump, a second grab is impossible authoritatively).
- The losing peer (whose grab lost the race) receives the winner's `ToClump{E}` while holding its
  own local clump bound to `E`. Its `OnConvert(ToClump,E)` is **idempotent**: `E` already renders
  as a clump locally, so re-skinning is a no-op (resolve `E` → already a clump → spawn-new would
  be skipped because `cur` is already the right class; just keep it). The loser's clump then is
  NOT the winner's — but they share `E`, and the **winner's held-pose stream under `E`** will
  drive the loser's clump to the winner's hands. The loser sees the pile leave their hands and fly
  to the winner — the correct authoritative outcome (the host says peer-A grabbed it).
- The loser's own `ToClump{E}` that it already sent is the second one the host drops — so it never
  propagates. The loser's local "I grabbed it" is overridden by the host's relayed winner.

This needs only a small host-side **per-eid morph-state map** (`E → {Pile|Clump}`), checked when
the host applies/relays a convert: reject a `ToClump` on an eid already `Clump`, reject a `ToPile`
on an eid already `Pile`. That same map naturally absorbs duplicate/echoed converts. It is the
minimal MTA-style "the server owns the entity's current state; conflicting client intents are
serialized and the late one is dropped" (cf. `CClientGame` sync-context arbitration). No locking,
no request/ack — just last-writer-loses-on-a-busy-eid at the host.

> If even this is deemed more than the increment needs for a first pass: a chipPile grab is a
> deliberate walk-up-and-press-E act; two peers racing the same pile within ~50 ms LAN RTT is
> vanishingly rare. The map can ship as the conflict hardening in the same increment (it is ~20
> lines and removes the only divergence window), so include it — but it is the *only* part that
> is about contention, not the happy path.

---

## 4. Minimal file / function change list

Reuse-first; the convert packet, the OnConvert receiver shell, the grab seam, the clump Init-POST
observer, and the held-pose stream all EXIST. Net new code is small.

| File | Change | Reuse? |
|---|---|---|
| `include/coop/net/protocol.h` | `PropConvertPayload`: carve `uint8_t kind` from `_pad[3]` (ToClump=0/ToPile=1); doc that `oldEid==newEid==E` on the bind model. Bump `kProtocolVersion` (→ next). | Existing struct, +1 byte meaning, size unchanged. |
| `src/coop/trash_collect_sync.cpp` | (a) `OnPileGrabPre`: drop the `PropDestroy(E)` send; arm `g_pendingMorph{player,E,ts}` instead. (b) `EnsureHeldItemBroadcast`: early-return for a clump already bound to an eid (no keyless `PropSpawn`). (c) **RETIRE** `WatchClump`/`g_watchedClumps`/`BroadcastConvertNear`/`TickWatchReleasedClumps` (RULE 2 — the convert pair replaces the death-watch). | Reuses the grab observer; deletes ~120 LOC. |
| `src/coop/prop_lifecycle.cpp` | In `GrabObserver_Aprop_Init_POST_Body`: early clump-adopt block — if `g_pendingMorph` live and `clump.holdPlayer==g_pendingMorph.player`, `RegisterPropMirror(E, clump)` + broadcast `PropConvert{ToClump,E,chipType}` + return (suppress fall-through). Also the **ToPile** half: if `g_pendingLand` live and the new pile matches its anchor, `RegisterPropMirror(E, pile)` + broadcast `PropConvert{ToPile,E,...}` + return. | Reuses the existing Init-POST observer + `RegisterPropMirror`. |
| `src/coop/local_streams.cpp` | (a) `ResolveHeldPropEid`: add the `ResolveMirrorEidByActor` fallback (clump's `E`). (b) cache it in a new `g_lastHeldEid` for the per-tick stream (O(1)). (c) release edge: arm `g_pendingLand{E,pos,vel,ts}` when the released held actor is an eid-bound clump. | Reuses the held-pose stream + release edge. |
| `src/coop/remote_prop.cpp` | Rewrite `OnConvert` to **re-skin in place** branching on `kind` (spawn-new bound to `E` → rebind → echo-destroy old), instead of OnSpawn-fresh-newEid. Add `SpawnLocalClump` helper (the old keyless-clump mirror spawn). | Reuses `ResolveLiveActorByEid`, `RegisterPropMirror`, `ClearAnyDriveFor`, `MarkIncomingDestroy`, the settled-pile spawn. |
| `src/coop/event_dispatch_entity.cpp` | `PropConvert` case: pass through unchanged (it already calls `OnConvert`); the `oldEid==newEid` is already range-valid. | No change beyond accepting equal eids. |
| `src/coop/event_feed.cpp` | **Verify** `PropConvert` is in the master reliable-router list (it is — `event_feed.cpp:157`). No change, but this is the `[[feedback-reliablekind-router-checklist]]` third place — confirm. | No change. |
| (host) a small `g_morphStateByEid` map | §3.2 conflict arbitration: reject a redundant convert on a busy eid; host-side only. Put it in `trash_collect_sync.cpp` (host relay path) or the OnConvert host branch. | New ~20 LOC. |

No new ReliableKind (reuse `PropConvert=41`). No new module file. No doom, catalog, pending-remove,
strip, PileSeed, or PileResync — all of that stays deleted.

**Modular note:** `remote_prop_spawn.cpp` (1347) / `remote_prop.cpp` (1024) / `prop_lifecycle.cpp`
are at/over the 800 soft cap. The clump-spawn helper + OnConvert rewrite are small, but if the
clump/pile-spawn helpers grow, extract a `coop/pile_morph.cpp` for the morph adopt/convert glue
(the grab-stash, land-stash, the two adopt blocks, the conflict map) rather than fattening
`prop_lifecycle.cpp`. Flag at review time per the file-size rule.

---

## 5. Pitfalls to avoid (the 10-session churn lessons)

1. **No second actor.** `oldEid==newEid==E`; each peer re-skins its single rendering of `E`. The
   receiver does spawn-new-then-destroy-old, but the count is invariant (one actor per eid per
   peer, before and after). NEVER mint a fresh `newEid` (the v52 behavior) — that creates a second
   cross-peer entity and is the dupe.
2. **The eid must survive all 3 UObjects.** pile-A→clump→pile-B are separate UObjects with broken
   pointer continuity. `E` is carried by (a) `g_pendingMorph` across grab→clump-Init, (b) the
   `RegisterPropMirror(E, clump)` rebind, (c) `g_pendingLand` across throw→pile-Init, (d) the
   `RegisterPropMirror(E, pile-B)` rebind. Every hop re-binds `E`; never let `E` resolve to a dead
   actor (spawn-new before destroy-old in OnConvert).
3. **No doom / catalog / pending-remove / strip / thin-client.** The client OWNS its bound pile and
   morphs it; nothing to re-stream, nothing to doom. That whole apparatus (s23–s31) stays deleted.
   If you find yourself adding a "wait for the full set" clock or a host catalog, STOP — the bind
   model already has identity (`E`); you don't need to reconstruct it.
4. **Throw-land detection must not over-fire (the s23 bug).** Land adoption is gated on THREE
   things at once: a live `g_pendingLand` (armed only on the input-driven release edge of an
   eid-bound clump), proximity to the clump's last position (~200 cm), and chipType match, all
   within a TTL (~3 s). It is NOT a proximity/death poll over all piles (the retired
   `g_watchedPiles` unsound mechanism). A pile that spawns for any other reason cannot match a
   pending-land anchor. If the clump expires without landing (LifeSpan), `g_pendingLand` simply
   times out and the receiver's clump is cleaned by the disconnect/teardown path — the convert pair
   is the only signal, so a non-land never fabricates a pile.
5. **Don't run the BP `toClump()`/`turnToPile()` on a mirror.** The receiver does the minimal
   spawn+destroy morph, not the BP morph (which is local-input-only and would re-enter our
   observers / need a puppet). The grabber runs the real BP locally (native mesh/physics/variant
   correct by construction); mirrors render the result.
6. **Echo-suppress every receiver destroy.** `OnConvert`'s destroy-old must `MarkIncomingDestroy`
   so our own K2_DestroyActor PRE observer doesn't re-broadcast it (the existing pattern,
   `remote_prop.cpp:883`). Otherwise a convert ping-pongs.
7. **Stream the held clump under `E`, not key=None+eid=0.** The whole "114× no local match" flood
   was the missing `ResolveMirrorEidByActor` fallback. With it, the carry streams by `E` and
   renders. Cache the resolved eid for the per-tick path (don't O(n)-walk every send).
8. **Host-authority via `E`, not via a request/relay.** Because `E` is host-minted, a client's
   convert applied by the host morphs the host's authoritative pile — the host never needs to
   "spawn on the client's behalf". Keep the §3.2 busy-eid drop so a grab race resolves to one
   winner.

---

## 6. End-to-end walk (verification scenario)

1. Both peers have pile `P` bound to host eid `E` (adopt-by-position at join). Host & client see it.
2. Client presses E on `P`. `OnPileGrabPre` resolves `E` (mirror map), arms `g_pendingMorph`.
   BP `toClump()` spawns clump `C` (holdPlayer=client), destroys `P` locally.
3. Clump Init-POST: `g_pendingMorph` live, `C.holdPlayer==client` → `RegisterPropMirror(E,C)` +
   send `PropConvert{ToClump,E,chipType}` (no keyless PropSpawn). Held-pose stream now carries
   `E`.
4. Host receives the convert: relays to other clients, AND applies it — its own `P` (bound to `E`)
   → spawn local clump bound to `E`, destroy `P`. The client's held-pose stream drives the host's
   `E`-clump into the client-puppet's hands on the host.
5. Client throws. Release edge arms `g_pendingLand{E,pos,vel}`. BP land spawns pile `P2` near
   `pos`, destroys `C`.
6. `P2` Init-POST: `g_pendingLand` matches (pos + chipType + TTL) → `RegisterPropMirror(E,P2)` +
   send `PropConvert{ToPile,E,pileClass,transform}`.
7. Host receives: relays + applies — its `E`-clump → settled pile `P2'` bound to `E` at the
   authoritative transform. Other clients do the same. `E` is a re-grabbable pile again.
8. Round-trip complete: round clump in hand → flat pile of the original variant on landing, in
   sync, one actor per peer throughout, no dupes, no doom, no second identity.
