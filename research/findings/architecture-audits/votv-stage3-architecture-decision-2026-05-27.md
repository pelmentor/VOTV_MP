# Phase 5G STAGE 3 — architecture decision: client interaction with mirror entities (2026-05-27)

**Question**: when CLIENT presses E on a host-spawned `AactorChipPile_C` /
`Aprop_garbageClump_C` mirror, the BP locally runs `toClump()` morph. Host
knows nothing. Which model do we adopt so host sees the client's interaction?

**Options under consideration**:
- A. **Bidirectional events** — remove host-only gates in `BroadcastState` +
  `K2_DestroyActor PRE`; both peers broadcast spawn/destroy + echo-suppress
  via existing `g_incomingWireSpawn`. Peer-namespaced identity.
- B. **Host-authoritative RPC** — client PRE-cancels `toClump` locally,
  sends a "remote pickup intent" RPC; host runs the morph; host's existing
  observers broadcast destroy + spawn back to client.

**TL;DR — Recommend Option B** (host-authoritative RPC). See §7 for the
single decisive factor (save-state authority) and §6 for the edge-case
landmines that disqualify A.

---

## 1. MTA precedent

### 1.1 What MTA actually does

MTA is **fully server-authoritative** for any "world entity gets picked up
or consumed" interaction. The client never decides "I picked it up"
locally; the server decides and tells the client.

The canonical pipeline (verified end-to-end against
`reference/mtasa-blue/`):

```
[CLIENT]  player walks into pickup col-shape
          -> CClientPickup::Callback_OnCollision  (CClientPickup.cpp:244)
              -> CallEvent("onClientPickupHit", ...)    // LOCAL Lua event
              -> Entity.CallEvent("onClientPlayerPickupHit", ...)
            *NOTHING ELSE HAPPENS LOCALLY.* The client does not hide the
            pickup, play sound, or grant the weapon. It just notifies its
            local Lua VM that a hit occurred (for UI/scripting hooks).

[SERVER]  same collision check runs server-side (the server also simulates
          the col-shape against player positions it receives from clients):
          -> CPickup::Callback_OnCollision   (Server/.../CPickup.cpp:483)
              -> CallEvent("onPickupHit") + CallEvent("onPlayerPickupHit")
              -> if both events return continue + CanUse(Player) -> Use(Player)
          -> CPickup::Use(Player)            (Server/.../CPickup.cpp:405)
              -> CallEvent("onPickupUse") + CallEvent("onPlayerPickupUse")
              -> SetVisible(false), m_bSpawned = false, save respawn time
              -> Player.Send(CPickupHitConfirmPacket(this, /*bPlaySound=*/true))
              -> g_pGame->GetPlayerManager()->BroadcastOnlyJoined(
                     CPickupHitConfirmPacket(this, /*bPlaySound=*/false),
                     &Player)               // tell everyone else
              -> apply the gameplay effect (GiveWeapon / SetHealth / ...)

[CLIENT]  Packet_PickupHitConfirm           (Client/.../CPacketHandler.cpp:4416)
          -> pPickup->SetVisible(bHide)
          -> PlayFrontEndSound(40) if bPlaySound
            *Client just obeys.* Even the player who "hit" the pickup gets
            the consume effect from the server packet, not from local code.
```

**Key citations** (file:line, verbatim where load-bearing):

- `Server/mods/deathmatch/logic/CPickup.cpp:405` — `CPickup::Use(CPlayer&)`
  is the AUTHORITATIVE pickup consumer. SERVER-side.
- `Server/mods/deathmatch/logic/CPickup.cpp:435-438` — server emits
  `CPickupHitConfirmPacket` to the picker AND broadcasts to all other
  joined players. There is no "client claims the pickup" packet upstream.
- `Client/mods/deathmatch/logic/CClientPickup.cpp:244-262` — client-side
  collision callback only fires Lua events. The Lua `onClientPickupHit`
  is observation-only; if the script wants to actually consume the
  pickup it must call a `setElementVisible(false)` or similar that goes
  through the SCM/sync path which is in turn server-authoritative.
- `Client/mods/deathmatch/logic/CPacketHandler.cpp:4434-4451` — client
  RECEIVING the server's hit confirm just hides the pickup + plays sound.
  No decision logic at all.

### 1.2 The conceptual MTA shape applicable to us

MTA treats pickup-hit (and analogously every "world entity transitions
state because a player interacted with it" event) as:
1. **Detection on either side is observational**, not authoritative.
2. **Decision is server-side only**. Server runs `CanUse`, `Use`,
   visibility flip, respawn timer, gameplay-effect dispatch.
3. **Result propagation is server-to-all**, including back to the actor
   that triggered the detection. The picker doesn't even self-hide its
   local mirror — server-sent `CPickupHitConfirmPacket` does.

**There is no bidirectional broadcast.** Even the player WHO picked up
the pickup waits for the server's hit-confirm packet to see the
consumption. The latency cost is accepted as the price of authoritative
correctness.

MTA does have lower-latency primitives — e.g. `CClientPed`'s pure-sync
ped state stream is peer-authoritative-for-self (the keysync packet) —
but those are for **continuous owned state** (your own input, your own
position), never for **shared world-entity mutation**. Pickup, weapon
spawn, vehicle door-state, marker hit — all server-decided.

### 1.3 Direct corollary to our chipPile question

A chipPile sitting on the ground is morally a **world-entity that gets
consumed/transformed by a player interaction**. The `toClump` morph IS
the consumption. The MTA precedent — 22 years of debugging — says: this
class of action is **host-authoritative**, full stop.

The peer-authoritative-for-self model in MTA covers **continuous owned
state** (your pose, your held weapon, your animation). It does NOT cover
**discrete shared-world mutations** (a tree falls, a pickup is consumed,
a door opens, an object morphs).

---

## 2. Existing project pattern

### 2.1 The PropPose stream is a special case, not the model

The existing wire layer has a `PropPose` stream (held physics-object
pose, `src/votv-coop/src/coop/net/session.cpp:114-118, 176-184, 417-422`).
This LOOKS peer-authoritative-for-held — host sends host's held-prop
pose to client and vice versa — but on close inspection the authority
model is narrower than it appears.

**What the PropPose stream actually authorizes**:
- The grabbing peer sends per-tick world transform of the prop they're
  currently grabbing via UPhysicsHandleComponent (`PHC`). This is purely
  the kinematic position stream — the same "your own pose" stream pattern
  as the player keysync, but for an inanimate object that you're driving
  via PhysicsHandleComponent on your machine.
- The handed prop's identity (Key) is host-minted at world spawn (a
  `Aprop_C` Init POST + `setKey` UFunction). The Key never changes
  because of a grab.
- Grab and release are **reliable events** carrying authoritative
  velocity / impulse. The receiver applies physics state via UFunction
  calls on the local mesh component.

**What it does NOT authorize**:
- The prop is never DESTROYED by the grab. It's the same UObject the
  whole time on each peer. There's no morph, no class change, no
  destroy-spawn cascade.
- The grabbing peer does not get to decide a state OTHER than position.
  It cannot change Aprop_C's `propData`, its `heavy` bool, its breakable
  state. Those stay deterministic (loaded from the same DataTable on
  both peers) or host-authoritative (broadcast on change).
- Crucially: the grabbing peer does not get to CHANGE THE ACTOR CLASS.

The PropPose stream is the UE4 equivalent of MTA's keysync — it's
"continuous owned position state of an object I'm currently driving".
That's the narrow legitimate use of peer-authoritative-for-self in our
layer. **It is not a precedent for letting the client mint world-entity
destroys/spawns.**

### 2.2 `prop_lifecycle.cpp` is genuinely bidirectional but for a different reason

`src/votv-coop/src/coop/prop_lifecycle.cpp` does broadcast destroys from
both host and client (`GrabObserver_Actor_K2DestroyActor_PRE`,
`prop_lifecycle.cpp:253-307`). This is sometimes cited as "we already
have a bidirectional model" — but the rationale is different:

- `Aprop_C` Keys are **save-game UUIDs** that are stable across peers
  by virtue of save snapshot bootstrap (Phase 5S0). When CLIENT destroys
  an `Aprop_C` (e.g. eats a food prop they're holding), HOST's same-Key
  actor must also die. The destroy IS the legitimate per-peer
  authority — the client is currently grabbing the food, the client eats
  it, the host's identical save-Key actor must reflect that.
- The echo-suppression (`g_incomingDestroys`) works at actor-pointer
  granularity, not Key/identity. It prevents the wire-applied destroy
  from re-broadcasting, but it does NOT prevent a divergent state ladder
  (host's "real" Aprop_C dying on its save state because client's
  client-side decision said so).
- The save-side coherence comes from the fact that BOTH peers persist
  Aprop_C state via the SAME save mechanism (host writes; client's
  optional local copy mirrors). Client's destroy IS what should happen
  to the save: host's autosave routine writes the destroyed-state world
  next save tick.

The chipPile case is **different** in two critical ways:
1. **No save-Key identity** between peers. chipPile uses host-minted
   `sessionId` (`non_prop_entity_sync.cpp:144`). Client's destroy of a
   mirror sessionId is destruction of A MIRROR, not destruction of THE
   ENTITY. If we route that destroy back to host as a "real" destroy,
   host's truly-authoritative chipPile dies on client's signal.
2. **The morph creates a NEW class.** `toClump` is not just a destroy —
   it's a destroy + spawn of a DIFFERENT actor (chipPile -> garbageClump).
   Both broadcast events must be applied in order on the other peer.
   Authority over the new entity (the clump) is the unresolved question.

### 2.3 Echo-suppression: how it works today (file:line trace)

The current echo suppression is two-sided:

- Outgoing-marked-before-engine-call (host-side at wire spawn /
  destroy):
  - `coop::remote_prop::MarkIncomingSpawn(actor)` is called BEFORE
    `FinishSpawningActor` in `remote_prop.cpp:706`.
  - `coop::remote_prop::MarkIncomingDestroy(actor)` is called BEFORE
    `K2_DestroyActor` in `remote_prop.cpp:835`.
  - Set is keyed by **actor pointer** (`std::unordered_set<void*>`).
- Consumer in observers:
  - `prop_lifecycle.cpp:163` consumes `IncomingSpawn` in Init POST.
  - `prop_lifecycle.cpp:261` consumes `IncomingDestroy` in K2_DestroyActor PRE.
  - `non_prop_entity_sync.cpp:436-438` consumes the analogous
    `g_incomingWireSpawn` set in OnInitPost.

**The key invariant for echo-suppression to work**: the actor pointer
that gets marked must be the SAME pointer the observer sees. This holds
because both happen in-process: the marker runs in the receiver-thread
that just spawned the actor; the observer fires on the same actor pointer
later in the same process.

**What it does NOT do**: it does not bind host-actor to client-actor
identity. Host's `chipPile-A* = 0x12345678` is not the same address as
client's mirror `chipPile-A* = 0xABCDEF00`. The echo set is per-process.
Cross-process suppression relies entirely on the host-only role gate
(`role() == Host` in the broadcaster) PLUS this per-process echo set
catching the local "I just spawned this from wire" actor.

---

## 3. Option A full trace — bidirectional events

### 3.1 Setup

- Remove the `role() == Host` gate from `BroadcastState`
  (`non_prop_entity_sync.cpp:394`).
- Remove the `role() == Host` gate from the
  `IdentityForDestroyingActor` path in `prop_lifecycle.cpp:276`.
- Add `peerSessionId` namespacing to identity (already in the payload at
  `NonPropEntityStatePayload.peerSessionId`).
- Both peers' Init POST + K2_DestroyActor PRE broadcast.

### 3.2 Round-trip: client presses E on host-spawned chipPile mirror

T0: host has chipPile-A with host-minted sessionId 17, broadcast at world
spawn.
T1: client has mirror chipPile-A' (the local actor address differs;
identity (HostPeer=0, sid=17) maps to it). Map entry:
`g_clientActorByIdentity[(0,17)] = chipPile-A'`.
T2: client presses E on chipPile-A'. Client's BP runs `toClump`:
  - spawns clump-Y' locally
  - calls `chipPile-A'.K2_DestroyActor()`
T3: client's K2_DestroyActor PRE fires for chipPile-A':
  - `IdentityForDestroyingActor(chipPile-A')` — but **only the HOST has
    `g_hostSessionIdByActor[chipPile-A'] = 17`**. On the client this map
    is empty (it only gets populated when client's own Init POST sees a
    natively-spawned actor — and we suppress that for wire-received ones
    via `g_incomingWireSpawn`).
  - So `IdentityForActor(chipPile-A')` would mint a NEW client-side
    sessionId 1. The destroy broadcast is `{peer=ClientPeer=1,
    identity=1}`.
  - Host receives `{peer=1, identity=1}`. Host's
    `g_clientActorByIdentity` is empty on the host side (host doesn't
    maintain a mirror map for client's entities — it has the real
    actors). Host has no way to look up "client's identity 1 -> which of
    my real chipPile actors".

  **Failure 1**: identity scheme breaks. With bidirectional events, the
  destroy/spawn identity authority becomes a 2-way map both peers must
  maintain. Today the map is one-way (host mints, client mirrors).

T4 (alternative if we fix identity): client's destroy broadcast carries
`{peer=HostPeer=0, identity=17}` — referring to the original host-minted
sessionId. Client looks this up via the inverse map
`g_clientIdentityByActor[chipPile-A']` (already maintained at
`non_prop_entity_sync.cpp:495`). OK so far.
T5: host receives `{peer=0, identity=17}` destroy. Host looks up:
  - Host's `g_hostSessionIdByActor[chipPile-A] = 17` (REAL host actor).
  - Host destroys chipPile-A.
  - Host's K2_DestroyActor PRE fires.
  - `g_incomingDestroys` was marked? **NO — we'd have to add that
    marking on the receive side too.** Add it. OK.
  - Echo suppressed.
T6: client's clump-Y' Init POST fires. Client's broadcaster runs (we
removed the host-only gate). Identity is client-minted — sessionId 5,
peer=ClientPeer=1. Broadcast `{peer=1, identity=5, ..., chipPile-A's
original spawn loc}`.
T7: host receives `{peer=1, identity=5}` spawn. Host has no mirror map
for client-namespaced identities, so this is fresh — host's
`DoApplyState` creates a NEW actor clump-Y at the broadcast loc.
T8: host's clump-Y Init POST fires. `g_incomingWireSpawn` was marked;
suppressed. OK.

T9: host walks. Host's clump-Y is **inert on the host** — it has no
holdPlayer, no canConvert, it's just sitting at the spawn position.
Meanwhile client's clump-Y' is attached to client's mainPlayer hand (BP
set holdPlayer=Player during toClump). Host renders clump-Y at ground
position; client renders clump-Y' at client's hand position. **DESYNC.**

  **Failure 2**: the client's BP runs locally and sets the clump's
  holdPlayer to the CLIENT's mainPlayer instance. Host has no clue. The
  clump on host is decoupled from any player; nobody drives its hand
  pose. The morph LOOKS correct visually on the client only.

T10: host's autosave fires. Host's save state now contains: clump-Y at
ground position (the wrong place — clump is "transient" anyway with
`skipSave1 @ 0x0254`, but the chipPile-A is GONE from host's save).
Reload/disconnect: client's local copy of host save (per
[[project-coop-save-host-authoritative]]) has chipPile-A as destroyed,
clump-Y as transient (probably not saved due to skipSave1). The save
NOW reflects client's local interaction — but only because client made
host's authoritative actor obey, not because host actually decided.

  **Failure 3**: save state has been mutated by client's local action,
  bypassing host's authority. This violates
  [[project-coop-save-host-authoritative]] in the LETTER of the rule
  ("no client→host save writeback"). Client's destroy DID writeback —
  through the wire as a "destroy event" but with the same effect.

### 3.3 Race: both peers press E simultaneously on the same chipPile mirror

T0: chipPile-A at sessionId 17 (host's). Client mirror chipPile-A' at
client.
T1: client presses E. Client's BP `toClump`: destroys chipPile-A',
spawns clump-Y'. Client broadcasts `Destroy{peer=0, identity=17}` AND
`State{peer=1, identity=5, classClumpVariant}`.
T2: at SAME tick, host presses E on chipPile-A. Host's BP `toClump`:
destroys chipPile-A, spawns clump-Z. Host's K2_DestroyActor PRE
broadcasts `Destroy{peer=0, identity=17}` (echoed back through wire).
Host's Init POST on clump-Z broadcasts
`State{peer=0, identity=18, classClumpVariant}`.

Host applies client's `Destroy{peer=0, identity=17}` — but host's
chipPile-A is already destroyed (host destroyed it itself). Host's
`g_hostSessionIdByActor[17]` was erased in
`IdentityForDestroyingActor` (`non_prop_entity_sync.cpp:765`). So the
client's destroy event lands on... nothing. Drop. OK.

Client applies host's `Destroy{peer=0, identity=17}` — client already
destroyed mirror chipPile-A'. Client's `g_clientActorByIdentity[(0,17)]`
already erased. Drop. OK.

Client applies host's `State{peer=0, identity=18, ...}` — host's clump.
Client spawns mirror clump-Z'.
Host applies client's `State{peer=1, identity=5, ...}` — client's
clump. Host spawns mirror clump-Y.

**Result**: TWO clumps in the world. Host sees clump-Z (its own) +
clump-Y' (client's, but mirrored on host). Client sees clump-Y' (its
own) + clump-Z' (host's, mirrored on client). Both are "held" — but the
holdPlayer assignment is per-peer-local, so on host clump-Z is held by
host-mainPlayer and clump-Y' is unheld; on client it's the inverse.

  **Failure 4**: double-pickup race produces two clumps where there
  should be one. The losing peer's interaction was supposed to be
  cancelled, not duplicated.

### 3.4 Echo loop analysis for Option A

The user's question: "in Option A, when client broadcasts a morph event,
host applies it. Host's K2_DestroyActor PRE observer fires (host
destroyed its real chipPile). Host broadcasts a destroy back. Does the
existing `g_incomingWireSpawn` echo suppression prevent the loop?"

Trace precisely:
- Client broadcasts `Destroy{identity=17}`.
- Host receives, calls `DoApplyDestroy` (`non_prop_entity_sync.cpp:502`).
- `DoApplyDestroy` looks up the mirror — but on the host, the
  `g_clientActorByIdentity` map is **client-side state** (the map is
  populated only in `DoApplyState` when client receives a spawn). On the
  host, that map is empty. Host's destroy resolution path FAILS.

  Actually re-reading: the existing `DoApplyDestroy` was designed for
  CLIENT-side application of HOST's destroys. On HOST, with bidirectional,
  we'd have to make `g_clientActorByIdentity` track client→host mirror
  too — i.e. host maintains a "client-spawned identity -> host's actor"
  map. The current code does not do this. We'd have to add a
  `g_hostActorByClientIdentity` symmetric structure.

  Assume we add it. Host receives client's destroy of identity 17;
  resolves to host's REAL chipPile-A; marks `IncomingDestroy(chipPile-A)`;
  calls `K2_DestroyActor`.
- Host's K2_DestroyActor PRE fires on chipPile-A.
  `ConsumeIncomingDestroy(chipPile-A)` returns true. SUPPRESSED. OK.

So a one-hop ping-pong is prevented. But:

  **Failure 5 (loop via secondary effects)**: the destroyed chipPile-A
  is gone from host. Host's `IdentityForDestroyingActor` already erased
  it from `g_hostSessionIdByActor` (`non_prop_entity_sync.cpp:765`). If
  client now broadcasts the spawn `State{peer=1, identity=5}` (the
  clump-Y'), host receives it AFTER the destroy. Host spawns mirror
  clump-Y. Host's Init POST fires, suppressed by `g_incomingWireSpawn`.
  OK so far.

  But then HOST also broadcasts its OWN clump-Z spawn back to client.
  Client now has clump-Y' (local), clump-Z' (mirror of host's). Two
  clumps. The race condition is the actual hazard, not the echo loop.

### 3.5 The IDLE case in Option A

User asked: "what about the IDLE case (no interaction)? Currently host
broadcasts initial state; client mirrors. If we go bidirectional, what
stops the client from also broadcasting initial state for its mirrors
(duplicate broadcast, host already knows)?"

In Option A, when a wire-received chipPile arrives on the client and
goes through `DoApplyState` -> `BeginDeferredSpawn` -> `FinishDeferredSpawn`,
the client's Init POST fires. **The current echo suppression handles
this**: `non_prop_entity_sync.cpp:480` calls `MarkIncomingWireSpawn`
BEFORE `FinishDeferredSpawn` -> `non_prop_entity_sync.cpp:436-438`
consumes it in `OnInitPost`. **So the idle-broadcast loop is suppressed
correctly**.

BUT: that suppression covers only the FIRST Init POST after a wire spawn.
If the client's BP then runs anything later that re-broadcasts state
(e.g. a `setPropProps` mutator firing on the mirror, a future state
sync, or — critically — a client-initiated morph that produces a brand
new actor with its own first Init POST), that NEW broadcast is NOT
echo-suppressed because the new actor was NOT marked.

So Option A's echo suppression works for idle / pure mirror lifecycle,
but breaks the moment a client-initiated action produces a new local
entity. Which is exactly the chipPile-morph case. The "no client
broadcast" gate is the ACTUAL safeguard; removing it is what introduces
all of §3.2-3.4.

### 3.6 Failure summary for Option A

1. Identity scheme needs a 2-way host-mints-AND-client-mints map. Major
   refactor of `g_hostSessionIdByActor` + `g_clientActorByIdentity` to
   become symmetric. Touches everything in `non_prop_entity_sync.cpp`.
2. Held-state desync after morph: holdPlayer is set on the morphing
   peer's local mainPlayer; the other peer's mirror has no holdPlayer
   set. Visual desync the moment the carrying peer moves.
3. Save-state divergence: client's local action mutates host's save
   state by destroying host's "real" chipPile. Violates
   [[project-coop-save-host-authoritative]] in spirit (no client writes
   to host save) even if the wire packet calls itself an "event" not a
   "save edit".
4. Double-pickup race: both peers press E in the same tick. Both BPs
   morph locally. Both broadcasts go through. Result: TWO clumps. The
   loser's interaction should have been rejected.
5. Generalization risk: once we allow bidirectional, every future shared
   world-entity sync (doors, switches, light states, keypad codes,
   terminal interactions, signal-catching button presses) must
   independently solve identity, race, save-coherence. We end up
   reinventing host-authoritative tie-breakers per feature, which IS the
   broad-suppression anti-pattern (principle 4).

---

## 4. Option B full trace — host-authoritative RPC

### 4.1 Setup

- Add a new `ReliableKind` packet `RemoteInteract` (per
  [`votv-chippile-clump-morph-RE-2026-05-27.md`] §6.6, this is the
  generic client-initiated remote-action substrate).

  ```cpp
  enum class ReliableKind : uint8_t {
      ...,
      RemoteInteract = 12,    // client -> host: "I want to interact with X"
  };

  struct RemoteInteractPayload {
      uint8_t  entityClass;     // NonPropEntityClass discriminator
      uint8_t  peerSessionId;   // sender peer id (host validates this matches their mapping)
      uint8_t  action;          // enum: 1=pickup_morph (toClump), future: 2=drop, 3=use, ...
      uint8_t  _pad;
      uint32_t identity;        // wire identity of the target entity (host-minted sessionId)
      // Total: 8 bytes.
  };
  ```

- Add a PRE-interceptor on the client that cancels `toClump` (and the
  upstream `playerTryToHold`/`InpActEvt_use_K2Node_InputActionEvent_41`
  branches that lead to it) when the target is a mirror entity owned by
  another peer. The interceptor doesn't run if THIS peer is the host
  (host's BP runs normally).
- Interceptor lookup: client maintains its existing
  `g_clientActorByIdentity` map. If the actor about to be morphed lives
  in that map (i.e. it's a wire-mirrored entity), the interceptor:
  1. Cancels the BP body (`return true` from PRE-interceptor).
  2. Sends `RemoteInteract{action=pickup_morph, identity=...}` to host.
  3. Returns.
- Host receives `RemoteInteract`. Host's session dispatcher routes to a
  new handler `coop::held_entity_sync::ApplyRemoteInteract` (or whichever
  module owns chipPile interactions). Handler:
  1. Looks up the actor by identity via host's existing
     `g_hostSessionIdByActor` REVERSE map (which we'd need to add — a
     `std::unordered_map<uint32_t, void*>` keyed by sessionId).
  2. Verifies the actor is alive + of the expected class.
  3. Looks up the client's puppet (the mirror of the client's
     mainPlayer on host's side; this is the existing
     `coop::remote_player::g_remoteMainPlayer` or equivalent — let me
     verify in code: this pawn exists on host as the client's
     `mainPlayer_C` orphan).
  4. Calls `actor.playerTryToHold(clientPuppet)` via `R::CallFunction`
     — feeding the client's puppet as the Player arg. The BP runs
     normally on host, which means `toClump` spawns a clump with
     `holdPlayer = clientPuppet`. The clump's per-tick BP (ReceiveTick
     or attach-mechanism) drives the clump to follow the client puppet's
     hand on HOST.
  5. Host's existing Init POST on the new clump + K2_DestroyActor PRE
     on the destroyed chipPile broadcast back to client. Client receives
     `Destroy{identity=oldChipPile}` + `State{identity=newClump,
     peerSessionId=ClientPeer=1, ...}`.
  6. Client's `DoApplyState` spawns mirror clump on client at the
     authoritative pose. Client's local mainPlayer can be marked as
     `holdPlayer` of the mirror clump VIA a follow-up
     `HeldEntityAttach{attached=1, peerSessionId=1}` packet (also from
     host), so the mirror clump on client follows client's local hand
     visually.

### 4.2 Latency

Project's reliable channel:
- `coop/net/reliable_channel.cpp:15`: stop-and-wait, one in flight at a
  time, RTO-based retransmit.
- Session RTT is measured via Ping/Pong every 1s
  (`session.cpp:431-437`). The session stores last RTT in
  `lastRttMs_`. On LAN this has been observed at ~1-3 ms (see RULE 2026-05-26
  session checkpoint memory notes about LAN gigabit measurements).

For a client-initiated chipPile pickup:
- T0: client presses E.
- T1: client's PRE-interceptor cancels BP, sends RemoteInteract. 1 RTT
  to host = ~1-3 ms LAN, ~50-150 ms WAN (typical residential WAN, irrelevant
  here — LAN is the design target).
- T2: host processes, runs `toClump`, broadcasts the destroy + spawn.
  Host's `K2_DestroyActor` + `FinishDeferredSpawn` are synchronous on
  the game thread; this is sub-millisecond.
- T3: client receives destroy + spawn. Stop-and-wait means destroy
  arrives ~RTT/2 after host sent; spawn arrives ~RTT/2 + 1 RTO if first
  packet was lost (rare on LAN).
- T4: client applies — mirror chipPile gone, mirror clump appears in
  hand.

**Total user-perceptible latency: ~2-5 ms on LAN.** Below the
single-frame threshold (at 60 fps, one frame is 16.7 ms). The user
sees the morph happen "instantly" — same frame, or one frame after, the
E-press. Practically indistinguishable from local-immediate.

On WAN, this scales to ~50-150 ms — equivalent to one or two visible
frames. Users would perceive this as "slight click delay" similar to
clicking a button in a 50ms-ping online game. Acceptable for a
not-real-time-critical interaction; the user is not aim-firing at the
chipPile.

The original concern that "round-trip delay before client sees the
morph may feel laggy" is unfounded for the design-target case (LAN).

### 4.3 Race: both peers press E simultaneously

T0: chipPile-A on host (real) + chipPile-A' on client (mirror).
T1: HOST presses E on host's chipPile-A. Host's BP runs immediately:
`toClump`, destroy chipPile-A, spawn clump-Z. Host's observers
broadcast `Destroy{identity=17}` + `State{identity=18}`.
T2: at the same wall-clock tick, CLIENT presses E on mirror chipPile-A'.
Client's PRE-interceptor fires, cancels BP, sends `RemoteInteract`.

T3: Host's `Destroy{17}` arrives at client. Client applies it:
mirror chipPile-A' destroyed.
T4: Host's `State{18}` arrives at client. Client spawns mirror clump.

T5: Client's `RemoteInteract` arrives at host. Host's handler:
- Look up identity 17 via `g_hostActorBySessionId` reverse map.
- Result: NOT FOUND (host already destroyed it in T1, and
  `IdentityForDestroyingActor` erased it). REJECT the interact.
- Optionally send a `RemoteInteractReject{identity=17, reason=stale}`
  back to client. Client can log it (the local cancel already happened
  visually — client's mirror is gone, client's BP didn't morph
  locally — so no UI repair needed).

**Result**: ONE clump. Host wins. The client's interaction is
rejected because by the time it landed on host, host had already
consumed the chipPile. Clean, deterministic, no double-pickup.

Compare to Option A §3.3 where the same race produced TWO clumps.

### 4.4 Client races itself: client presses E twice rapidly

T0: chipPile-A mirror on client.
T1: client presses E. PRE-interceptor cancels, sends RemoteInteract.
T2: 50ms later (still waiting for host's response): client presses E
again on the same mirror — it's still visible because host's destroy
hasn't arrived.
T3: PRE-interceptor cancels again (same mirror still in
g_clientActorByIdentity), sends a SECOND RemoteInteract for the same
identity.
T4: host's handler runs both. First one: chipPile-A still alive,
morph runs, identity 17 erased. Second one: identity 17 lookup fails,
reject.
T5: client receives destroy + spawn (from first morph). Mirror updates
correctly. Second interact rejected silently.

**OK**. The host's identity-not-found rejection is the natural
deduplication.

### 4.5 Generalization

Option B's RemoteInteract packet is the **generic client-initiated
host-authoritative interaction substrate** that we need for nearly
every future feature anyway:
- Doors (client wants to open a host-spawned door): already
  host-authoritative for NPC-sensor-triggered doors per the doors+lights
  scope; player-initiated client doors land here too.
- Light switches: host-authoritative; client sends RemoteInteract.
- Terminal button clicks: synced via the same `RemoteInteract` with an
  `action` field set to a terminal-button enum value.
- Inventory pickups of world props (the per-peer-private inventory rule
  applies only to inventory CONTENTS; the world-prop->inventory
  transition is a world event).
- Future "use" actions on any host-authoritative entity.

Building this once now and reusing it is a big architectural win.
Option A would need bespoke race-resolution per feature.

### 4.6 Edge case: client picks up an entity host has never seen

Can't happen by construction. The mirror entity ONLY exists on client
because host broadcast it. If host destroyed it (e.g. mushroom7_C
suppressor cleaned it up before client saw it), client never had a
mirror to press E on.

The reverse — host has an entity client hasn't yet received — also
can't be interacted with by client because the visual + collision
mirror doesn't exist on client yet. Once it does (on next state
broadcast / connect snapshot replay), client can interact.

### 4.7 Edge case: dropping while other peer is also dropping the same item

The "same item" can only be the client-held clump or the host-held
clump. Both can't hold the same actor — there's only one. Whoever
picked it up has it; the other peer is rendering a mirror following the
holder's hand. Drop is initiated by the holder. The OTHER peer can't
drop "the same item" because they don't have it.

If we interpret the question as "both peers throw their own held
items at the same time", that's two independent throw events for two
distinct actors. Each is host-authoritative for the actor in question:
- Host throws its held: host's BP runs, broadcasts launch.
- Client throws its held: client's PRE-interceptor cancels local throw,
  sends `RemoteInteract{action=drop_throw, identity=...}` to host.
  Host runs the throw on its instance (the clump that has client puppet
  as holdPlayer), broadcasts back. Client mirror catches up.

No conflict.

### 4.8 The clump's per-tick hand follow

The chipPile RE doc §4 identifies that the clump's per-tick world
transform must follow the holder's hand. The mechanism is BP-internal
and per-peer-local on the holder side. Under Option B:
- The holding decision lives on host (host's BP runs `toClump` with
  client puppet as holdPlayer).
- Host's clump's per-tick world transform follows host's view of the
  client puppet's hand. Host broadcasts NonPropEntityState updates as
  the clump moves (already done via existing observers if the BP
  drives the transform via SetActorLocation, which `non_prop_entity_sync`
  doesn't currently observe — see GAP in §8).
- Client receives the periodic transform updates and pushes them onto
  its mirror clump.

Or, per the chipPile RE doc §6.2 recommendation, add a HeldEntityAttach
packet so the attachment is parented and client's mirror auto-follows
without per-tick wire traffic. Either way the holding semantics on
client are correct because the authoritative holdPlayer assignment is
made on host with the correct puppet identity.

---

## 5. Save-state implications

### 5.1 Option A

- Client's local `toClump` runs immediately on client's mirror
  chipPile-A'. Mirror is destroyed; mirror clump-Y' spawns; client's
  K2_DestroyActor PRE broadcasts destroy back.
- Host receives destroy, applies to its REAL chipPile-A. Host's autosave
  next tick now reflects: chipPile-A gone. Host's save state has been
  mutated by client.
- This is a wire-event-mediated client→host save writeback. The wire
  packet calls itself a "destroy event" but its effect is identical to
  the writeback that [[project-coop-save-host-authoritative]] forbids.
- Worse: chipPile's `getData` (`actorChipPile.hpp:20`) saves position +
  chipType + pile/clump subclass refs. When host saves, the world
  snapshot loses chipPile-A entirely. If host disconnects + reloads
  solo, the world no longer has the chipPile that the host themselves
  never interacted with. The host's solo experience has been silently
  edited by the client's interaction.

### 5.2 Option B

- Client cancels local morph. Sends `RemoteInteract`.
- Host runs the morph authoritatively on its own actor. Host's BP
  decides whether the morph is legal (CanUse-equivalent for chipPile
  — currently the BP just runs `toClump` unconditionally, but a future
  gameplay rule could reject e.g. "you can't morph a wet-concrete
  chipPile while it's still drying" by adding a BP check).
- Host's save state changes ONLY because host's BP ran `toClump`. This
  is identical to host single-player behavior. The save reflects
  legitimate host-authored state mutation.
- Client's optional local save copy (per
  [[project-coop-save-host-authoritative]]) receives host's snapshot
  on next connect; the snapshot reflects the morph.

**Verdict**: Option A violates the save authority rule in effect.
Option B preserves it.

### 5.3 Trace of `getData` / `loadData` for chipPile vs clump

Per the chipPile RE doc (§3.1):
- **chipPile** IS saved. `getData` writes Fstruct_save: position +
  rotation + chipType + pile/clump TSubclassOf refs.
- **clump** is normally NOT saved (`skipSave1 @ 0x0254`). When holding
  during save, the player's held-item descriptor is saved separately
  (this is the per-peer-private inventory data per
  [[project-coop-inventory-private]]).

So save flow in Option B (client initiates):
1. Host runs morph. Host's chipPile-A `getData` would have saved it
   PRE-morph; POST-morph chipPile-A is destroyed → not in save.
2. Clump exists transiently on host with `skipSave1` → not saved.
3. When the clump lands (via host's BP turnToPile) → new chipPile-B
   spawns at landing pose → host saves it on next autosave.

Result: world saves correctly. ChipPile moved from spawn pose to
landing pose, mediated by a transient clump.

In Option A:
1. Client destroys mirror chipPile-A'. Broadcasts to host. Host
   destroys real chipPile-A.
2. Client's local clump-Y' transient. Save? Client's local save isn't
   the authoritative one anyway; doesn't matter.
3. Client throws clump. Clump-Y' lands. Client's BP runs turnToPile,
   spawns chipPile-B' locally. Broadcasts spawn back to host.
4. Host applies spawn → spawns chipPile-B on host.
5. Host's save: chipPile-A gone, chipPile-B at new pose.

Same end-state for save IFF every step round-trips correctly. But the
authority chain is inverted: host's save is mutated by client's
broadcast, not by host's own action. If the client lags or
disconnects mid-morph, the host's save is left in an intermediate
state (chipPile-A destroyed but no chipPile-B yet). On client
re-connect, host's snapshot has the missing-mid-state inconsistency.

---

## 6. Edge cases (collected)

### 6.1 Disconnect mid-morph

**Option A**: client destroyed chipPile-A locally, hasn't broadcast
yet (or broadcast lost). Client disconnects. Host still has chipPile-A.
On reconnect, snapshot replay re-mirrors chipPile-A to client. But
client's BP graph might have stashed state in the local mainPlayer
(grabbing_actor pointer); reconciling that with host's snapshot is a
mess.

**Option B**: client sent RemoteInteract, host hadn't acked yet, client
disconnects. Reliable channel times out the unacked packet. Host runs
the morph if the interact arrived (idempotent identity-erase makes a
re-send safe). Or the interact is lost; host's chipPile stays put.
Reconnect → snapshot replay reflects host state. Clean.

### 6.2 Mid-flight client picks up host-held clump

User asked about overlap with future combat / damage sync. Not in scope
for chipPile morph today, but the model generalizes:
- Option B: any client attempt to interact with a host-held entity
  routes through `RemoteInteract`. Host decides: is this a steal? Is
  the entity transferrable? If the BP says yes, host transfers the
  holdPlayer via a state update and broadcasts. Client mirror updates.

### 6.3 Network reorder

Reliable channel is stop-and-wait + sequence-ordered
(`reliable_channel.cpp` ordering check at line 65). No reorder hazard
for our packets; each is delivered exactly once in order. Both options
benefit equally.

### 6.4 Snapshot replay re-broadcast

On reconnect, `ReplaySnapshotForJoinedClient` (`non_prop_entity_sync.cpp:639`)
re-broadcasts all tracked entities. In Option B nothing changes — host
is still the only broadcaster. In Option A, replay must NOT include the
client's namespaced entities (`peer=1, ...`), only host's. Adds a
conditional to the replay loop.

### 6.5 Client-only entities

There's a "client inventory is unique" rule
([[project-coop-inventory-private]]). Some entities are intentionally
client-local (inventory props). Option A blurs the line — every
broadcast must check "is this a client-private one?" Option B keeps
the line crisp: client-private entities never leave the client
(no broadcast at all from client); world entities only mutate via
host.

---

## 7. Recommendation

### **Adopt Option B (host-authoritative RPC) without carve-out.**

### Single decisive factor

[[project-coop-save-host-authoritative]] (USER DECISION 2026-05-24,
verbatim "HOST is the one who owns the save"). Option A's destroy
broadcast from client → host directly mutates host's authoritative
chipPile state. Whether the wire calls it an "event" or not, the
effect IS a client→host save writeback. The rule forbids this; not by
spirit but by direct cause-and-effect. Option B is the only model in
which client-initiated chipPile interaction respects this rule by
construction.

### Reinforcing factors (each independently sufficient if save weren't decisive)

1. **MTA precedent is explicit** (§1). 22 years of MTA's pickup model
   says server-authoritative. We are documented to follow MTA's
   architectural choices unless we have a game-specific reason to
   diverge. We have no such reason here.
2. **Race resolution is free** (§4.3). Option B's identity-not-found
   rejection deterministically resolves double-pickup. Option A
   produces duplicate clumps on the same race.
3. **Latency is negligible on LAN** (§4.2). 2-5 ms = sub-frame.
   Originally proposed concern is unfounded.
4. **Generalization** (§4.5). The RemoteInteract packet is the
   substrate every future client-initiated interaction needs: doors,
   light switches, terminal buttons, signal-catching, drone controls,
   keypads. Build once; reuse.
5. **Echo-suppression is fragile under bidirectional** (§3.5). Today's
   actor-pointer-keyed set works precisely because broadcasts are
   host-only; client only consumes. Adding client broadcasts forces a
   symmetric reverse map + extra marking sites; the bug surface grows.

### Implementation order (no carve-outs, no staging)

1. Bump protocol v9 → v10.
2. Add `ReliableKind::RemoteInteract` + 8-byte payload (§4.1).
3. Add host-side identity reverse map
   `std::unordered_map<uint32_t, void*> g_hostActorBySessionId` in
   `non_prop_entity_sync.cpp`. Populate at the same site as
   `g_hostSessionIdByActor`; erase at the same site
   (`IdentityForDestroyingActor`).
4. Add client-side PRE-interceptor cascade for `toClump`,
   `playerTryToHold`, `playerHandRelease_LMB`, `playerHandRelease_RMB`,
   `unequpped` on `AactorChipPile_C` + `Aprop_garbageClump_C` (+
   subclasses by inheritance — interceptor on the leaf UFunction
   suffices). Cancel only when the actor is in
   `g_clientActorByIdentity` (i.e. it's a mirror; client never blocks
   interactions on client-only entities like inventory props).
5. Add host-side `ApplyRemoteInteract` handler in a new file
   `src/votv-coop/src/coop/remote_interact.cpp` (per the modular
   file-size rule; this is a new subsystem). Handler:
   - Resolves actor by identity.
   - Resolves caller's puppet by `peerSessionId` (via
     `coop::players::Registry`).
   - Calls `actor.playerTryToHold(callerPuppet)` via
     `R::CallFunction`. Existing observers handle the broadcast back.
6. Add `HeldEntityAttach` packet (per chipPile RE doc §6.2) so the
   client's mirror clump attaches to client's local mainPlayer hand
   visually after spawn.
7. Add `Aprop_dirtball_C` to the class table (per chipPile RE doc §6.4).
8. Audit pass per CLAUDE.md "After shipping, audit with agents (RULE)"
   — performance + thread-safety + RULE №1/№2 compliance + file-size
   modularity.

No legacy paths. No "support both for now". No feature flag. Ship B,
delete any old Option-A leanings (none in code today — STAGE 2 is
host-only by gate, this just extends that to STAGE 3).

### Caveat to flag before implementing

The reverse identity map (`g_hostActorBySessionId`) is currently
absent. Adding it must be done in the SAME commit that introduces the
RemoteInteract handler, NOT as a precursor commit, because adding an
unused map then using it next commit means a transient state where
the map is populated for no reason — confusing to reviewers and to
future you (RULE №2: no migration baggage). One commit, full feature.

---

## 8. Out-of-scope follow-ups (logged for future)

These do NOT block Option B from shipping but should be tracked:

- **G-1**: per-tick clump pose broadcast vs attach packet — the
  HeldEntityAttach approach (chipPile RE doc §6.2) is preferred but
  requires the new packet + attach-mechanism wiring. The interim
  fallback (broadcast clump's per-tick world transform from host's
  ReceiveTick observer) is wasteful (200 Hz writes) and should be
  treated as RULE №1 quick-fix to avoid. Plan on HeldEntityAttach in
  the same Inc.

- **G-2**: the chipPile RE doc OPEN-A (`Aprop_dirtball_C`'s pile
  default) needs a quick runtime probe. Doesn't block Option B; just
  affects dirtball's specific roundtrip.

- **G-3**: the chipPile RE doc OPEN-C (per-tick clump tick vs
  attach-driven) needs a runtime check. Determines whether
  HeldEntityAttach is sufficient or whether we ALSO need per-tick
  override.

- **G-4**: the BP's `playerTryToHold` semantics may not accept an
  arbitrary `Player` arg — it might cast to `AmainPlayer_C` and the
  client's puppet IS an `AmainPlayer_C` orphan (per
  [[project-coop-enemies-target-both]]), so the cast succeeds. Verify
  via runtime probe before assuming.
