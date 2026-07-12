# MTA NPC/entity sync patterns — for VOTV coop adaptation

Date: 2026-05-24
Source: `reference/mtasa-blue/` (Client + Server + Shared)
Companion docs: `mta-object-pickup-sync-2026-05-23.md`, `mta-pose-interpolation-2026-05-23.md`, `mta-position-sync-2026-05-23.md`, `mta-chat-joinquit-reliability-2026-05-23.md`

This file documents how MTA:SA synchronises non-player entities (peds/NPCs,
physical objects, pickups, explosions, sounds) across peers. The VOTV
adaptation is intentionally out of scope here — that is a separate task.

---

## TL;DR

1. **MTA never replicates "the AI itself".** Server holds the *authoritative
   state record* (position/health/etc.) for every entity. AI / animation /
   pathing / physics simulation is delegated to **one chosen client**, called
   the *syncer*. The syncer streams partial-state deltas back to the server;
   the server relays them to nearby observers. AI does NOT run on the server
   and does NOT run on every client — it runs on *one* client at a time per
   entity. This is the central architectural invariant.

2. **The server picks the syncer based on proximity** (distance to the
   entity, with hysteresis to prevent flapping) and *load balance* (player
   syncing the fewest peds/vehicles/objects wins ties). When the chosen
   syncer leaves the radius, server tells the old syncer "STOP_SYNC", finds
   a new one, tells the new one "START_SYNC" with the current snapshot.

3. **Every entity carries `m_ucSyncTimeContext` (1 byte).** It is the
   server's version-stamp on the entity's authoritative state. The server
   bumps it on teleport / spawn / `setElementPosition` / authority handoff,
   and the new value is broadcast. Any in-flight sync packet stamped with
   the OLD context is silently dropped. This is the universal anti-stale
   guard and the universal ownership-handoff signal.

4. **Three orthogonal classes of sync traffic:**
   - **Per-tick streaming, unreliable-sequenced** (peds, objects,
     unoccupied vehicles): position/rotation/velocity/health, flag-gated
     so only changed fields are written, 400-500 ms tick from syncer.
   - **Reliable-ordered lifecycle events** (entity-add, entity-remove,
     start-sync, stop-sync, pickup hide/show, RPC state changes): driven
     by server, broadcast to all (or to relevant subset).
   - **Reliable-sequenced one-shot events** (explosion, sound, pickup
     hit-confirm, projectile): broadcast to players whose camera is within
     a radius of the event.

5. **Authority transfer is push-based, server-arbitrated.** A client
   doesn't *grab* authority — it can only *request* (e.g. push an
   unoccupied vehicle while not the syncer triggers a server-side
   `OverrideSyncer`). The server holds the syncer pointer on the entity
   and is the single arbiter of "who is allowed to write to this entity
   right now". Two clients cannot simultaneously authoritatively stream
   the same entity by construction.

---

## 1. CClientPed (NPC) sync

The MTA model treats *any* `CClientPed` that is not the local player
(`IS_PED(entity) && !IS_PLAYER(entity)`) as an NPC, syncable via
`CPedSync`. Players use a completely different higher-bandwidth path
(`CPlayerPuresync`); ped sync is the lighter-weight, broader path.

### 1.1 Lifecycle: creation, syncer assignment, destruction

**Creation:** server-side. The server creates a `CPed` via
`createPed`/Lua/XML. Once it exists, the server runs `CPedSync::DoPulse`
once per ~500 ms (the *syncer-update* tick, distinct from the sync rate):

```cpp
// Server/.../CPedSync.cpp:30
void CPedSync::DoPulse()
{
    // Time to update lists of players near peds?
    if (m_UpdateNearListTimer.Get() > 1000)
    {
        m_UpdateNearListTimer.Reset();
        UpdateNearPlayersList();
    }

    // Time to check for players that should no longer be syncing a ped or peds that should be synced?
    if (m_UpdateSyncerTimer.Get() > 500)
    {
        m_UpdateSyncerTimer.Reset();
        UpdateAllSyncer();
    }
}
```

There are **two cadences**: a 1 s "near-players list" rebuild (who can
*see* the ped, for relay decisions) and a 500 ms "syncer pick" pass (who
*owns* the ped). They are deliberately decoupled.

`UpdateSyncer` decides per ped:

```cpp
// Server/.../CPedSync.cpp:106
void CPedSync::UpdateSyncer(CPed* pPed)
{
    CPlayer* pSyncer = pPed->GetSyncer();

    // Handle no syncing
    if (!pPed->IsSyncable())
    {
        if (pSyncer)
            StopSync(pPed);
        return;
    }

    // This ped got a syncer?
    if (pSyncer)
    {
        // Is he close enough, and in the right dimension?
        if (IsSyncerPersistent() || (pPed->GetDimension() == pSyncer->GetDimension() &&
                                     IsPointNearPoint3D(pSyncer->GetPosition(), pPed->GetPosition(),
                                         (float)g_TickRateSettings.iPedSyncerDistance)))
            return;

        // Stop him from syncing it
        StopSync(pPed);
    }

    if (pPed->IsBeingDeleted())
        return;

    // Find a new syncer for it
    FindSyncer(pPed);
}
```

Key facts:
- `pPed->IsSyncable()` is a script-controllable bit (default true). Setting
  it false stops syncing entirely — the ped becomes frozen on every client.
- `g_TickRateSettings.iPedSyncerDistance` defaults to **100 metres** (see
  `Shared/.../CTickRateSettings.h:26`).
- Once a syncer is chosen, they keep it while inside the radius. When they
  leave, they get a `StopSync` packet, and a new syncer is sought.

**Syncer search** (`FindSyncer` → `FindPlayerCloseToPed`) intentionally
picks the *least-loaded* eligible player:

```cpp
// Server/.../CPedSync.cpp:141
void CPedSync::FindSyncer(CPed* pPed)
{
    // Note: the search radius is shrunk by 20 m vs the stop-sync radius.
    // This is the hysteresis: you must be 80 m to BECOME syncer, but only
    // 100 m to STAY syncer. Prevents flapping at the boundary.
    CPlayer* pPlayer = FindPlayerCloseToPed(pPed, g_TickRateSettings.iPedSyncerDistance - 20.0f);
    if (pPlayer)
        StartSync(pPlayer, pPed);
}

CPlayer* CPedSync::FindPlayerCloseToPed(CPed* pPed, float fMaxDistance)
{
    // ...iterate joined players in same dimension within radius...
    // He syncs less peds than the previous player close enough?
    if (!pLastPlayerSyncing || pPlayer->CountSyncingPeds() < pLastPlayerSyncing->CountSyncingPeds())
        pLastPlayerSyncing = pPlayer;
    // Return the player we found that syncs the least number of peds
    return pLastPlayerSyncing;
}
```

**Hysteresis (80 m start, 100 m stop) is the single anti-flap mechanism for
syncer churn.** Important: load-balancing is per-class (peds vs vehicles vs
objects each have their own counter on `CPlayer`).

**StartSync** sends a `CPedStartSyncPacket` (the initial snapshot) to the
chosen player, marks the ped's `m_pSyncer` pointer, and fires a Lua event:

```cpp
// Server/.../CPedSync.cpp:154
void CPedSync::StartSync(CPlayer* pPlayer, CPed* pPed)
{
    if (!pPed->IsSyncable())
        return;

    // Tell the player
    pPlayer->Send(CPedStartSyncPacket(pPed));

    // Mark him as the syncing player
    pPed->SetSyncer(pPlayer);

    // Call the onElementStartSync event
    CLuaArguments Arguments;
    Arguments.PushElement(pPlayer);  // New syncer
    pPed->CallEvent("onElementStartSync", Arguments);
}
```

The start packet body is the full minimal snapshot — position, rotation,
velocity, health, armor, camera rotation (no compression flags here, this
is a one-shot):

```cpp
// Server/.../packets/CPedStartSyncPacket.cpp:16
BitStream.Write(m_pPed->GetID());
BitStream.Write(vecTemp.fX); BitStream.Write(vecTemp.fY); BitStream.Write(vecTemp.fZ);
BitStream.Write(m_pPed->GetRotation());
BitStream.Write(vecTemp.fX); BitStream.Write(vecTemp.fY); BitStream.Write(vecTemp.fZ); // velocity
BitStream.Write(m_pPed->GetHealth());
BitStream.Write(m_pPed->GetArmor());
BitStream.Write(m_pPed->GetCameraRotation());
```

**Destruction:** server initiates. When a ped is deleted, all clients get
an entity-remove packet (separate path, not in `CPedSync`); whichever
client was the syncer just loses it silently because `pPed->IsBeingDeleted()`
short-circuits `UpdateSyncer`.

### 1.2 What's replicated, what's interpolated

The client syncer collects state every `PED_SYNC_RATE` (defaults to
`g_TickRateSettings.iPedSync = 400 ms`) and packs a *delta* into one packet:

```cpp
// Client/.../CPedSync.cpp:283  WritePedInformation
unsigned char ucFlags = 0;
if (vecPosition != pPed->m_LastSyncedData->vPosition)                  ucFlags |= 0x01;
if (pPed->GetCurrentRotation() != pPed->m_LastSyncedData->fRotation)  ucFlags |= 0x02;
if (vecVelocity != pPed->m_LastSyncedData->vVelocity)                 ucFlags |= 0x04;
if (pPed->GetHealth() != pPed->m_LastSyncedData->fHealth)             ucFlags |= 0x08;
if (pPed->GetArmor() != pPed->m_LastSyncedData->fArmour)              ucFlags |= 0x10;
if (pPed->IsOnFire() != pPed->m_LastSyncedData->bOnFire)              ucFlags |= 0x20;
if (pPed->IsInWater() != pPed->m_LastSyncedData->bIsInWater)          ucFlags |= 0x40;
if (pPed->IsReloadingWeapon() != ...isReloadingWeapon)                ucFlags |= 0x60;
if (pPed->HasSyncedAnim() && (!pPed->IsRunningAnimation() ||
                              pPed->m_animationOverridedByClient))    ucFlags |= 0x80;
// secondary byte:
if (!IsNearlyEqual(pPed->GetCameraRotation(), ...cameraRotation))     flags2 |= 0x01;

// Do we really have to sync this ped?
if (ucFlags == 0 && flags2 == 0)
    return; // skip writing entirely — silent if nothing changed
```

Then writes ID + sync-time-context + flag bytes + selected fields, packing
position with `SPositionSync` (a fixed-point compressed format,
`SFloatSync<14,10>` for X/Y — 14-bit value, 10-bit scale — and raw float
for Z; see `Shared/.../SyncStructures.h:216`).

Send is **unreliable-sequenced, medium-priority** — the dominant traffic
pattern for live-streamed state:

```cpp
// Client/.../CPedSync.cpp:277
g_pNet->SendPacket(PACKET_ID_PED_SYNC, pBitStream,
                   PACKET_PRIORITY_MEDIUM, PACKET_RELIABILITY_UNRELIABLE_SEQUENCED);
```

**Single packet per pulse aggregates ALL peds the local player syncs** —
not one packet per ped. The bitstream loops until empty on the receive
side.

### 1.3 The interpolation model — `SetTargetPosition`

Non-syncer clients (and the server) don't snap to the incoming position;
they call `SetTargetPosition(vecPosition, PED_SYNC_RATE)`:

```cpp
// Client/.../CPedSync.cpp:236
CClientPed* pPed = m_pPedManager->Get(ID);
if (pPed && pPed->CanUpdateSync(ucSyncTimeContext))
{
    if (ucFlags & 0x01)
        pPed->SetTargetPosition(vecPosition, PED_SYNC_RATE);
    if (ucFlags & 0x02)
        pPed->SetTargetRotation(PED_SYNC_RATE, fRotation, std::nullopt);
    if (ucFlags & 0x04)
        pPed->SetMoveSpeed(vecMoveSpeed);
    if (ucFlags & 0x08) pPed->LockHealth(fHealth);
    if (ucFlags & 0x10) pPed->LockArmor(fArmor);
    // ...
}
```

The receiver interpolates toward the new target over the SYNC_RATE window
(400 ms). This is documented in detail in our earlier
`mta-pose-interpolation-2026-05-23.md`. Velocity is set directly (it's a
hint to the integrator; the position lerp dominates).

`LockHealth` / `LockArmor` is the mechanism by which the syncer's reported
values become authoritative on the receiver — game-side health changes
from local damage application are suppressed until the lock is released.

### 1.4 Server relay model — near-list, far-sync

The server doesn't blindly rebroadcast the syncer's packet. It splits
peds into "near" players (rebroadcast every packet) and "far" players (a
single packet every `iPedFarSync = 2000 ms`):

```cpp
// Server/.../CPedSync.cpp:298
bool bDoFarSync = llTickCountNow - pPed->GetLastFarSyncTick() >= g_TickRateSettings.iPedFarSync;

if (!bDoFarSync && pPed->IsNearPlayersListEmpty())
    continue;
// ...
if (bDoFarSync)
{
    pPed->SetLastFarSyncTick(llTickCountNow);
    m_pPlayerManager->BroadcastOnlyJoined(PedPacket, pPlayer);  // everyone except syncer
    continue;
}

// Send to players nearby the ped
CSendList sendList;
for (auto iter = pPed->NearPlayersIterBegin(); iter != pPed->NearPlayersIterEnd(); iter++)
{
    CPlayer* pRemotePlayer = *iter;
    if (pRemotePlayer && pRemotePlayer != pPlayer)
        sendList.push_back(pRemotePlayer);
}
if (!sendList.empty())
    m_pPlayerManager->Broadcast(PedPacket, sendList);
```

The near-list is rebuilt once per second by a spatial-database query
around each player's *camera* position (`DISTANCE_FOR_NEAR_VIEWER`):

```cpp
// Server/.../CPedSync.cpp:333
void CPedSync::UpdateNearPlayersList()
{
    // Clear lists, then per player...
    pPlayer->GetCamera()->GetPosition(vecCameraPosition);
    CElementResult resultNearCamera;
    GetSpatialDatabase()->SphereQuery(resultNearCamera, CSphere(vecCameraPosition, DISTANCE_FOR_NEAR_VIEWER));
    for (CElement* pElement : resultNearCamera)
    {
        if (pElement->GetType() != CElement::PED) continue;
        if (pPlayer->GetDimension() != pPed->GetDimension()) continue;
        if (pPed->GetSyncer() == pPlayer) continue;  // syncer doesn't need to receive its own packet
        if ((vecCameraPosition - pPed->GetPosition()).LengthSquared() < DISTANCE_FOR_NEAR_VIEWER * DISTANCE_FOR_NEAR_VIEWER)
            pPed->AddPlayerToNearList(pPlayer);
    }
}
```

**This is the AOI/interest-management layer.** Two cadences:
- *Per-packet*: send to currently-near players (low latency).
- *Every 2 s* (`iPedFarSync`): one packet to everyone, so far players
  still see slow drift / health changes without spam.

The far-sync is the safety net so a far player who suddenly turns around
isn't seeing a hard pop — they have a recent (≤2 s old) state to interpolate
from when they re-enter the near radius.

### 1.5 AI behaviour — client-side, no server simulation

The server NEVER runs ped pathing or animations. The syncer client's local
GTA:SA AI runs the ped (with whatever Lua `setPedAnimation` /
`setPedControlState` commands script has issued). Output is observed by the
syncer and streamed back. Receivers get position/rotation/velocity/health
plus a small set of boolean flags (`bOnFire`, `bIsInWater`,
`isReloadingWeapon`).

**Animation sync is partial and event-driven, not per-frame.** A separate
`setPedAnimation` RPC (in `CPedRPCs`) sends an animation name + start time
+ duration; the receiver triggers the same anim locally. The
`HasSyncedAnim` bit + `m_animationOverridedByClient` flag tell the syncer
to inform the server when the syncer's local animation has been changed by
client code, so the server knows to invalidate (`SetAnimationData({})`)
the cached server-side animation record:

```cpp
// Server/.../CPedSync.cpp:295
if (Data.ucFlags & 0x80)
    pPed->SetAnimationData({});

// Server/.../CPedSync.cpp:90  inside UpdateAllSyncer
const SPlayerAnimData& animData = (*iter)->GetAnimationData();
if (animData.IsAnimating())
{
    const std::int64_t elapsedMs = currentTimestamp >= animData.startTime ? (currentTimestamp - animData.startTime) : 0;
    if (!animData.freezeLastFrame && animData.time > 0 && static_cast<float>(elapsedMs) >= animData.time)
        (*iter)->SetAnimationData({});
}
```

The server tracks `(name, startTime, duration)` so new joiners can be told
"this ped is in the middle of animation X" — but the *frame* of the
animation is not synced. The receiver client integrates locally.

### 1.6 Anti-stale guard

```cpp
// Client/.../CPedSync.cpp:237
if (pPed && pPed->CanUpdateSync(ucSyncTimeContext))
{
    // ...apply...
}

// Client/.../CClientEntity.cpp:179
bool CClientEntity::CanUpdateSync(unsigned char ucRemote)
{
    return (m_ucSyncTimeContext == ucRemote || ucRemote == 0 || m_ucSyncTimeContext == 0);
}
```

If the server (or a different syncer) has bumped the entity's context since
the sender wrote this packet, the receiver silently drops the data fields.
This is also what protects against the StartSync race (syncer A's last
in-flight packet arriving after server tells receivers "new context = N+1
because syncer changed to B").

---

## 2. CClientObject sync (compare with our Stage-4 prop wire)

`CObjectSync` is `CPedSync`'s sibling: same shape, same syncer-pick model,
fewer fields. **It is conditionally compiled (`#ifdef WITH_OBJECT_SYNC`)**
— object physics sync is gated behind a build flag in MTA because GTA:SA
itself doesn't do object physics deterministically enough across machines;
MTA's pragmatic answer is "if you opt-in, here's the simplest possible
syncer model". This is itself a useful design signal.

### 2.1 Lifecycle

Same as peds: server holds the `CObject`, syncer pick on
`SYNC_RATE = 500 ms` cadence, `MAX_PLAYER_SYNC_DISTANCE = 100.0f`:

```cpp
// Server/.../CObjectSync.cpp:17
#define SYNC_RATE                500
#define MAX_PLAYER_SYNC_DISTANCE 100.0f

// Server/.../CObjectSync.cpp:83  UpdateObject
if (!pObject->IsSyncable() || (pObject->IsStatic() && !pObject->IsBreakable()))
{
    if (pSyncer) StopSync(pObject);
    return;
}

if (pSyncer)
{
    if (!IsSyncerPersistent() && !IsPointNearPoint3D(pSyncer->GetPosition(), pObject->GetPosition(), MAX_PLAYER_SYNC_DISTANCE) ||
        (pObject->GetDimension() != pSyncer->GetDimension()))
    {
        StopSync(pObject);
        FindSyncer(pObject);
    }
}
else
{
    FindSyncer(pObject);
}
```

Key gate: `IsStatic() && !IsBreakable()` — there is NO POINT syncing an
object that can't move or break. Static, unbreakable objects are
permanently desync'd. Breakable objects (oil drums etc.) get a syncer so
the "broken" state can propagate.

`MAX_PLAYER_SYNC_DISTANCE = 100 m` (hard-coded, no hysteresis here unlike
peds).

### 2.2 Wire format — 3 flag bits, no animation/health/state grab-bag

```cpp
// Client/.../CObjectSync.cpp:211  WriteObjectInformation
unsigned char ucFlags = 0;
if (vecPosition != pObject->m_LastSyncedData.vecPosition) ucFlags |= 0x1;
if (vecRotation != pObject->m_LastSyncedData.vecRotation) ucFlags |= 0x2;
if (pObject->GetHealth() != pObject->m_LastSyncedData.fHealth) ucFlags |= 0x4;
if (ucFlags == 0) return;

pBitStream->Write(pObject->GetID());
pBitStream->Write(pObject->GetSyncTimeContext());
SIntegerSync<unsigned char, 3> flags(ucFlags);
pBitStream->Write(&flags);

if (ucFlags & 0x1) { SPositionSync position; ...; pBitStream->Write(&position); }
if (ucFlags & 0x2) { SRotationRadiansSync rotation; ...; pBitStream->Write(&rotation); }
if (ucFlags & 0x4) { SObjectHealthSync health; ...; pBitStream->Write(&health); }
```

Three bits: position, rotation, health. **No velocity** — the comment
`// No velocity due to issue #3522` in the start-sync handler is an
explicit choice (object velocity is too noisy / not reproducible cross-
client). Receiver lerps position/rotation; objects don't have animations.

Note: `SObjectHealthSync` is an 11-bit packed float clamped 0-1023.5 (range
0-1000 step 0.5):
```cpp
// Shared/.../SyncStructures.h:205
struct SObjectHealthSync : public SFloatAsBitsSync<11>
{
    SObjectHealthSync() : SFloatAsBitsSync<11>(0.f, 1023.5f, true, false) {}
};
```

### 2.3 Server applies + broadcasts in one pass

Unlike peds (which have the near/far split), objects use a simpler
"apply-then-broadcast-everyone-in-dimension":

```cpp
// Server/.../CObjectSync.cpp:195  Packet_ObjectSync
for (...)  // each object in packet
{
    if (pObject->GetSyncer() == pPlayer && pObject->CanUpdateSync(pData->ucSyncTimeContext))
    {
        if (pData->ucFlags & 0x1) {
            pObject->SetPosition(pData->vecPosition);
            g_pGame->GetColManager()->DoHitDetection(pObject->GetPosition(), pObject);
        }
        if (pData->ucFlags & 0x2) pObject->SetRotation(pData->vecRotation);
        if (pData->ucFlags & 0x4) pObject->SetHealth(pData->fHealth);
        pData->bSend = true;
    }
}
m_pPlayerManager->BroadcastOnlyJoined(Packet, pPlayer);
```

Note `g_pGame->GetColManager()->DoHitDetection(...)` — server still runs
collision detection on the authoritative position (for col-shape triggers,
script events). Same call appears in the ped path.

### 2.4 Comparison with our Stage-4 prop wire

Our physics-prop grab (per
`physics-object-pickup-coop-plan-2026-05-23.md`) is *picker-as-syncer*: the
player who is holding the prop streams pose, the receiver applies it.
MTA's model is more general — there's *always* a syncer, picked by
proximity, regardless of who is interacting. When you pick up an object in
MTA, server's `OverrideSyncer(pObject, pYouThePicker, /*bPersist=*/true)`
sets you as persistent syncer until release, which is the same end result
we already have, just generalised.

Specifically for VOTV's coop-scope of physics props: MTA wouldn't even
"hold the prop"; it would just continue ped-syncing the player, and the
object's normal proximity syncer (likely the same player) keeps writing
pose. The held vs not-held distinction is implicit. We chose explicit
because it lines up with VOTV's `getInteractedObject` driving the wire on
or off; that is an adaptation choice, not a deviation from MTA shape.

---

## 3. Pickups

Pickups in MTA are a distinct entity type (`CClientPickup`, parented
`CClientStreamElement`). They are health/armor/weapon/custom-model
collectibles. Unlike peds/objects, they don't have a syncer — the server
authoritatively owns visibility and respawn. Three packet flavours.

### 3.1 Creation/destruction — pure server-driven via `CEntityAddPacket`

Pickups (like all server-created entities) ship to joining clients in a
single `CEntityAddPacket` (`PACKET_ID_ENTITY_ADD`, reliable-sequenced,
high-priority). The packet writes per-entity: id, type, parent id, interior,
dimension, then a type-specific payload (model, type, ammo, etc.).

```cpp
// Server/.../packets/CEntityAddPacket.h:21
unsigned long GetFlags() const { return PACKET_HIGH_PRIORITY | PACKET_RELIABLE | PACKET_SEQUENCED; };

// Server/.../packets/CEntityAddPacket.cpp:85
bool CEntityAddPacket::Write(NetBitStreamInterface& BitStream) const
{
    unsigned int NumElements = m_Entities.size();
    BitStream.WriteCompressed(NumElements);
    for (...) {
        BitStream.Write(pElement->GetID());
        BitStream.Write(static_cast<unsigned char>(pElement->GetType()));
        BitStream.Write(ParentID);
        BitStream.Write(pElement->GetInterior());
        BitStream.WriteCompressed(pElement->GetDimension());
        // ...type-specific...
    }
}
```

Destruction is similarly `CEntityRemovePacket` (not shown here, same shape).
**One-way, server → client, reliable.** Clients do NOT spawn or destroy
pickups locally; if they try via Lua, the call routes through the server.

### 3.2 Hit-confirm — server arbitrates pickup-touched

Client streams pickups in (renders the model + col-shape) via the streamer.
When the local player walks into the col-shape, the local client's
`CClientPickup::Callback_OnCollision` fires, and the client *requests* a
pickup hit (separate path); the server validates and decides:

```cpp
// Server/.../CPickup.cpp:405  Use()
void CPickup::Use(CPlayer& Player)
{
    CLuaArguments Arguments; Arguments.PushElement(&Player);
    if (!CallEvent("onPickupUse", Arguments)) { ... cancelled, return ... }
    else
    {
        // Tell all the other players to hide it if the respawn intervals are bigger than 0
        if (m_ulRespawnIntervals > 0)
        {
            m_LastUsedTime = CTickCount::Now();
            m_bSpawned = false;
            SetVisible(false);
        }

        // Tell him to play the sound and hide/show it
        Player.Send(CPickupHitConfirmPacket(this, true));  // bPlaySound=true to user

        // Tell everyone else to hide/show it as necessary
        g_pGame->GetPlayerManager()->BroadcastOnlyJoined(CPickupHitConfirmPacket(this, false), &Player);

        // ...apply effect (health/armor/weapon)...
    }
}
```

Hit-confirm is reliable + sequenced + high priority:

```cpp
// Server/.../packets/CPickupHitConfirmPacket.h:23
unsigned long GetFlags() const { return PACKET_HIGH_PRIORITY | PACKET_RELIABLE | PACKET_SEQUENCED; };
```

**Two packets are sent: one with `bPlaySound=true` to the picker, one
with `bPlaySound=false` broadcast to everyone else.** The picker hears the
classic SA pickup sound; nearby players just see the pickup vanish.

### 3.3 Hide/show, retype — RPC-style

For respawn / Lua-driven state changes, the server sends
`CPickupHideShowPacket` (also reliable+sequenced+high-priority) and the
`SET_PICKUP_TYPE` / `SET_PICKUP_VISIBLE` RPCs:

```cpp
// Client/.../rpc/CPickupRPCs.cpp:28  SetPickupType
void CPickupRPCs::SetPickupType(CClientEntity* pSource, NetBitStreamInterface& bitStream)
{
    // Read out type, then conditionally read amount / weaponID+ammo / customModel
    CClientPickup* pPickup = m_pPickupManager->Get(pSource->GetID());
    if (pPickup) { pPickup->SetModel(...); pPickup->m_ucType = ucType; ... }
}
```

### 3.4 Streaming + limit cap

Pickups are `CClientStreamElement`s — same AOI machinery as objects/peds.
GTA:SA has a hard limit `m_uiPickupCount >= 620` (per
`CClientPickupManager::IsPickupLimitReached`), so the streamer's
limit-reached function gates stream-in.

---

## 4. Particles / sounds / one-shot effects

These are pure events — no element ID lifecycle, no per-tick streaming,
no syncer.

### 4.1 Explosions — client-initiated, server-arbitrated, AOI-broadcast

The local game spawns explosions natively (grenade detonates, vehicle
blows up). MTA intercepts via `CClientExplosionManager::Hook_ExplosionCreation`,
decides whether to handle it locally or push to server:

```cpp
// Client/.../CClientExplosionManager.cpp:121
// Handle this explosion client side only if entity is local or breakable (i.e. barrel)
if (pResponsible->IsLocalEntity() || (bHasModel && CClientObjectManager::IsBreakableModel(usModel)))
{
    pResponsible->CallEvent("onClientExplosion", arguments, true);
    // ...client-only path...
    return allowExplosion;  // don't sync — render locally only
}

// All explosions are handled server side (ATTENTION: always 'return false;' below)
// Is the local player responsible for this?
const bool bIsLocalPlayer = pResponsible == pLocalPlayer;
const bool bIsLocalPlayerVehicle = pResponsible == pLocalPlayer->GetOccupiedVehicle();
const bool bIsUnoccupiedVehicleSynced = g_pClientGame->GetUnoccupiedVehicleSync()->Exists(...);

if (!bIsLocalPlayer && !bIsLocalPlayerVehicle && !bIsUnoccupiedVehicleSynced)
    return false;  // not "your" explosion, don't send
// ...
g_pClientGame->SendExplosionSync(vecPosition, explosionType, pOriginSource, VehicleBlowState::AWAITING_EXPLOSION_SYNC);
return false;  // game must NOT render this — wait for the server's broadcast
```

**Critical pattern:** the original initiator *suppresses* its own local
explosion render and waits for the server-relayed packet (which will
include itself in the broadcast). This ensures everyone — including the
shooter — sees the same explosion at the same authoritative time. The
inverse would be: shooter renders immediately, but then the explosion is
echoed by the server too = double bang.

The hook returns `false` to game code to abort the local explosion path.

Wire format (`CExplosionSyncPacket`, `PACKET_ID_EXPLOSION`,
*reliable+sequenced+high-priority*):

```cpp
// Server/.../packets/CExplosionSyncPacket.h:41
unsigned long GetFlags() const { return PACKET_HIGH_PRIORITY | PACKET_RELIABLE | PACKET_SEQUENCED; };

// Packet body: hasOrigin bit + (originID + isVehicleResponsible + blowVehicleWithoutExplosion)
//              + SPositionSync (compressed XY) + SExplosionTypeSync
```

Server side decides the AOI:

```cpp
// Server/.../CGame.cpp:2932
// Make a list of players to send this packet to (including the explosion reporter).
CSendList sendList;
for (auto iter = m_pPlayerManager->IterBegin(); iter != m_pPlayerManager->IterEnd(); ++iter)
{
    CPlayer* player = *iter;
    CVector cameraPosition;
    player->GetCamera()->GetPosition(cameraPosition);
    if (IsPointNearPoint3D(explosionPosition, cameraPosition, MAX_EXPLOSION_SYNC_DISTANCE))
        sendList.push_back(player);
}
if (!sendList.empty())
    CPlayerManager::Broadcast(Packet, sendList);
```

**AOI is on the player's *camera* position, not pawn position** — so a free-
camera spectator gets explosions in view, and a player looking through a
sniper scope at a faraway explosion still sees it. (Same rule as the ped
near-list above.)

### 4.2 Sounds — `playSoundFrontEnd` is a one-byte RPC

```cpp
// Server/.../CStaticFunctionDefinitions.cpp:9030
bool CStaticFunctionDefinitions::PlaySoundFrontEnd(CElement* pElement, unsigned char ucSound)
{
    RUN_CHILDREN(PlaySoundFrontEnd(*iter, ucSound))  // recurse into element tree
    if (IS_PLAYER(pElement))
    {
        CBitStream BitStream;
        SIntegerSync<unsigned char, 7> sound(ucSound);  // 7-bit packed
        BitStream.pBitStream->Write(&sound);
        pPlayer->Send(CLuaPacket(PLAY_SOUND, *BitStream.pBitStream));
        return true;
    }
}
```

7 bits of payload (sound index 0-127), wrapped in `CLuaPacket(PLAY_SOUND, ...)`.
`CLuaPacket` uses the standard reliable-sequenced channel.

3D sounds (`CClientSound`) are full entities — created via the entity-add
path, streamed via the AOI streamer, and the BASS-audio playback is
local. The sound entity has position/velocity/volume/distance attributes
synced via the standard element property RPCs; the *audio data* is either
a local file path (server tells client "play foo.wav") or a URL the client
fetches itself. The server never streams PCM.

### 4.3 General pattern for one-shot events

All of explosion, projectile launch, sound, pickup-hit-confirm,
weapon-fire-flash share the same shape:
- Generated by *one* client (the initiator).
- Sent reliably to the server with a position + type + origin element id.
- Server validates (rate-limit, anti-cheat, dimension check, sometimes
  vehicle blow-state arbitration).
- Server broadcasts to players whose camera is within a fixed radius.
- Receivers render locally; nothing further.

Reliability tier: **reliable + sequenced + high-priority** for all of
them. (Compare to per-tick state: unreliable + sequenced + medium.) The
choice is "I cannot tolerate a missed explosion / sound / pickup
confirmation, but it must arrive in order with respect to subsequent ones
on the same channel".

---

## 5. CClientEntity base + ownership/authority model

### 5.1 The base class

`CClientEntity` (and its server twin `CElement`) provides every entity
with:
- `ElementID m_ID` — 16-bit server-assigned unique id, the wire identity.
- `unsigned char m_ucSyncTimeContext` — the version stamp.
- Parent pointer + child list (the *element tree*).
- Dimension, interior.
- `m_bBeingDeleted`.
- Custom data table (Lua-set arbitrary key/value).
- Attach-to (parent transform follow).

The element ID space is centralised in `CElementIDs`. IDs are reused
slowly (with a generation counter pattern) to avoid stale-ref bugs.

The `m_ucSyncTimeContext` documentation is in
`CClientEntity.h:170-178` and is worth quoting in full because it
articulates the invariant cleanly:

```cpp
// This is used for realtime synced elements. Whenever a position/rotation change is
// forced from the server either in form of spawn or setElementPosition/rotation a new
// value is assigned to this from the server. This value also comes with the sync packets.
// If this value doesn't match the value from the sync packet, the packet should be
// ignored. Note that if this value is 0, all sync packets should be accepted. This is
// so we don't need this byte when the element is created first.
unsigned char GetSyncTimeContext() { return m_ucSyncTimeContext; };
```

The `0 = accept anything` rule is so the first packet after creation doesn't
get rejected before the context has been negotiated. After the first server
push, the value becomes ≥1 and stays there forever (wraps from 255 → 1,
never 0):

```cpp
// Server/.../CElement.cpp:1281
unsigned char CElement::GenerateSyncTimeContext()
{
    ++m_ucSyncTimeContext;
    // It can't be 0 because that will make it not work when wraps around
    if (m_ucSyncTimeContext == 0)
        ++m_ucSyncTimeContext;
    return m_ucSyncTimeContext;
}

bool CElement::CanUpdateSync(unsigned char ucRemote)
{
    return (m_ucSyncTimeContext == ucRemote || ucRemote == 0 || m_ucSyncTimeContext == 0);
}
```

`GenerateSyncTimeContext` is called by every server-authoritative state
push: `setElementPosition`, `setElementRotation`, `setPedAnimation`,
`setVehicleHealth`, `spawnPlayer`, `setCameraTarget` — anywhere the
server is forcibly resetting state. The bumped value piggybacks on the
push packet, the client adopts it, and any in-flight sync packets stamped
with the old context are rejected.

### 5.2 Streaming — `CClientStreamer` + sector grid

The client streamer is a fixed-grid AOI: world is divided into
`SECTOR_SIZE × ROW_SIZE` cells (`WORLD_SIZE = 3000 m`-ish, sector ~100 m).
Each `CClientStreamElement` (peds, vehicles, objects, pickups, sounds 3D)
registers into one sector. The streamer's `DoPulse(playerPos)`:
1. Finds the sector under the player.
2. Walks the 3×3 sectors around the player + sorted-by-distance list of
   currently active elements.
3. Streams in N elements per pulse (rate-limited: 6 in / 6 out per frame
   unless `bMovedFar`, when 1000 in / 1000 out for a hard refresh).
4. Stream-in actually creates the GTA:SA game object (`pPed->Create()`,
   etc.). Stream-out destroys it. The MTA `CClientEntity` *record*
   persists; only the underlying game pointer comes and goes.

```cpp
// Client/.../CClientStreamer.cpp:377  Restream
constexpr float swapHysteresisDistanceSq = 10.0f * 10.0f;
constexpr std::uint32_t minStreamInDelayAfterOutMs = 1200u;
// ...
int iMaxOut = 6;
int iMaxIn = 6;
if (bMovedFar) { iMaxOut = 1000; iMaxIn = 1000; }
```

10 m squared hysteresis on swap-distance + 1200 ms delay-after-out — the
classic two-tier anti-flap.

The streamer is also dimension-aware:

```cpp
// Client/.../CClientStreamer.cpp:197  SetDimension
m_usDimension = usDimension;
// All currently-streamed-in elements get queued for stream-out unless visible-in-all-dimensions
```

This is how MTA implements the "dimension" feature: each dimension is a
separate slice of the world. Changing dimension queues a full restream.

### 5.3 Authority transfer — push vs pull, who is allowed to override

Authority transfer is **server-arbitrated, push-from-client-request**.
The clearest example is unoccupied vehicles:

```cpp
// Server/.../CUnoccupiedVehicleSync.cpp:476  Packet_UnoccupiedVehiclePushSync
void CUnoccupiedVehicleSync::Packet_UnoccupiedVehiclePushSync(CUnoccupiedVehiclePushPacket& Packet)
{
    CPlayer* pPlayer = Packet.GetSourcePlayer();
    // ...validate joined...
    CVehicle* pVehicle = ...;

    // Is the player syncing this vehicle and there is no driver? Also only process
    // this packet if the time context matches.
    if (pVehicle->GetSyncer() != pPlayer && pVehicle->GetTimeSinceLastPush() >= MIN_PUSH_ANTISPAM_RATE &&
        IsPointNearPoint3D(pVehicle->GetPosition(), pPlayer->GetPosition(), ...) &&
        pVehicle->GetDimension() == pPlayer->GetDimension())
    {
        CPed* pOccupant = pVehicle->GetOccupant(0);
        if (!pOccupant || !IS_PLAYER(pOccupant))
        {
            if (!pVehicle->GetSyncer() || !IsSyncerPersistent())
                OverrideSyncer(pVehicle, pPlayer);
            pVehicle->ResetLastPushTime();
        }
    }
}
```

`MIN_PUSH_ANTISPAM_RATE = 1500` ms (anti-spam: a player can't ask for
authority of an unoccupied vehicle more often than every 1.5 s).

The handshake: a client that wants to "push" a vehicle (collide with it,
move it) sends a `VEHICLE_PUSH_SYNC` packet. The server validates
(proximity, dimension, no driver, rate limit) and if OK calls
`OverrideSyncer` which is the same code path that the proximity loop
uses. The bumped sync-time-context invalidates any in-flight sync
packets from the previous syncer; the new syncer gets a `StartSync`
packet with the current snapshot and proceeds.

For peds/objects there is no `Push` packet — the server's proximity loop
is the only mechanism. Peds and objects can only be picked up as syncer
when you walk close to them.

`OverrideSyncer(entity, player, bPersist)` is the central API:

```cpp
// Server/.../CPedSync.cpp:58  OverrideSyncer
void CPedSync::OverrideSyncer(CPed* pPed, CPlayer* pPlayer, bool bPersist)
{
    CPlayer* pSyncer = pPed->GetSyncer();
    if (pSyncer)
    {
        if (pSyncer == pPlayer)
        {
            if (bPersist == false) SetSyncerAsPersistent(false);
            return;
        }
        StopSync(pPed);
    }
    if (pPlayer && !pPed->IsBeingDeleted())
    {
        SetSyncerAsPersistent(bPersist);
        StartSync(pPlayer, pPed);
    }
}
```

`bPersist = true` means "this player keeps this syncer assignment even if
they move out of radius" — used when a script explicitly grants ownership,
or when the player is interacting with the entity (holding, driving). The
proximity loop won't reclaim the syncer while `IsSyncerPersistent()` is
true.

### 5.4 Why two clients can't simultaneously sync the same entity

The invariant is enforced server-side at three points:
1. **`m_pSyncer` is a single pointer on the entity.** Only one player at a
   time. `StartSync` checks and `StopSync` clears.
2. **Server validates every incoming sync packet by checking
   `pEntity->GetSyncer() == pSender`** before applying:
   ```cpp
   // Server/.../CPedSync.cpp:243
   if (pPed->GetSyncer() != pPlayer || !pPed->CanUpdateSync(Data.ucSyncTimeContext))
       continue;
   ```
   If you stream pose for a ped you don't own, the server silently drops
   the data and does not rebroadcast. There's no rejection or error to the
   sender — your packet just dies.
3. **Sync-time-context invalidates in-flight stale writes** during the
   transition window between syncers (one syncer is "fired" by the server
   but its last unreliable packet is still in flight when the new syncer
   is anointed).

This is why MTA does not need locking or CAS on entity state — there is
only ever one writer.

### 5.5 Tick rate summary

From `Shared/.../CTickRateSettings.h`:

| Setting | Default | Used for |
|---|---|---|
| `iPureSync` | 100 ms | Local player full pose (high-bandwidth) |
| `iLightSync` | 1500 ms | Local player low-frequency state (afk, etc.) |
| `iCamSync` | 500 ms | Camera-only delta |
| `iPedSync` | 400 ms | NPC ped sync from syncer |
| `iPedFarSync` | 2000 ms | NPC ped far-player relay |
| `iUnoccupiedVehicle` | 400 ms | Unoccupied vehicle sync from syncer |
| `iObjectSync` (`#ifdef`) | 500 ms | Object sync from syncer |
| `iKeySyncRotation` | 100 ms | Per-key rotation input |
| `iKeySyncAnalogMove` | 100 ms | Per-key analog input |
| `iNearListUpdate` | 100 ms | Spatial near-list rebuild cadence |
| `iPedSyncerDistance` | 100 m | Ped syncer assignment radius |
| `iUnoccupiedVehicleSyncerDistance` | 130 m | Vehicle syncer assignment radius |
| `iVehicleContactSyncRadius` | 30 m | Allowed push distance |
| `playerTeleportAlert` | 100 m | Distance jump = "teleport" anti-cheat alert |

Sanity ratios: per-tick streaming runs at ~2.5 Hz for NPCs vs 10 Hz for
the local player. Far-sync at 0.5 Hz. AOI rebuild at 10 Hz. The whole
system is well under per-frame; bandwidth is the bottleneck, not CPU.

---

## Implications for VOTV

(Per task instructions: this documents MTA, NOT the VOTV implementation.
The notes below are short cross-refs only.)

### What MTA-fidelity gives us, mapped to VOTV's substrate

| MTA pattern | UE4.27 / VOTV equivalent or substitute |
|---|---|
| `CClientEntity` base + `ElementID` 16-bit | Our `coop::EntityKey` (proposed: 32-bit since UE objects have UObject*+SerialNumber; element id can be `(class_id, server_index)`). Engine-tree-as-element-tree doesn't map (no UE equivalent); flat registry. |
| `m_ucSyncTimeContext` (1 byte) | Direct adoption. **Already in our protocol spec.** Bump on host-side teleport / setActorLocation / spawn. |
| Syncer = one client | We have a host-client model so far; with N≥2 clients this becomes mandatory. For now, the host IS the syncer for all server-created entities; clients are the syncers only for state they uniquely own (their pawn pose, props they grab). |
| Server-owned `m_pSyncer` pointer | Host-side: a `TMap<EntityKey, ClientId>`. The host is always either "the syncer" or "the arbiter who picked one". |
| `iPedSync = 400 ms`, far-sync `2000 ms` | Direct port: NPCs (drones, kerfur entities) sync at ~2.5 Hz with far-relay every 2 s. |
| `iPedSyncerDistance = 100 m` (with 80 m hysteresis) | Direct port. VOTV's map is small (~500 m radius), so this catches everyone in most situations, but the hysteresis is still required. |
| `WritePedInformation` flag-byte delta | Direct port — we already use this shape on the prop wire. Pack into 1-2 bytes of flags + selected SPositionSync/SRotationSync. |
| `SPositionSync` 14+10+raw-Z fixed-point | UE world units are cm; we can adopt the same shape with our world scale. Or use a quantised int + scale. Skip for v1 — float32 is fine until bandwidth bites. |
| Receiver `SetTargetPosition(target, SYNC_RATE)` lerp | Already shipped (`remote-player-interp.md`). Same pattern applies to NPCs and props. |
| Server validates `GetSyncer() == sender` per packet | Direct port. Host drops any pose write from a client that doesn't own that entity. |
| `CanUpdateSync` 3-way (match / 0 / 0) | Direct port; trivial. |
| Anim sync = `(name, startTime, duration)` event + receiver integrates | Direct port. We have UE AnimBP — sync the *montage name* + start tick, not per-frame transforms. (Heavy bone-stream is the wrong default per the ragdoll memory note.) |
| `OverrideSyncer(entity, player, bPersist)` | Single host API; bPersist for held props and driven vehicles (when we get vehicles). |
| Unoccupied-vehicle `Push` packet → server arbitrates ownership | Useful for VOTV when a non-syncer collides with a host-synced physics prop (kicks a barrel). For Stage 4-6 we own this via "interactor becomes syncer" but the push-packet pattern is the right generalisation. |
| Near-list / far-list AOI | Less critical at 2-player but free win at N-player. For now, broadcast to all is fine; the AOI is a packed list of `playersInRadius(entity)` recomputed at 1 Hz. |
| `CClientStreamer` sector grid + stream in/out cap | UE has its own streaming. We DON'T need a parallel streamer. We just need "is this entity within `STREAM_DISTANCE` of any client?" for sync gating; UE's spatial queries handle that. Stream-in/out cap is a bandwidth concern not a render one. |
| Pickups: server creates + reliable `CEntityAddPacket`, hit-confirm reliable | Directly applicable to VOTV's signal pickups, alien crystals, etc. Lifecycle = server-authoritative, no per-tick stream, hit = reliable RPC, broadcast to all (small enough world). |
| Explosion: client-initiated, server-arbitrated, AOI broadcast, **initiator suppresses local render** | Critical pattern for VOTV's gunfire / explosion / signal events. The "don't render locally, wait for server echo" rule is the anti-double-bang fix. Reliable + sequenced + high-priority. |
| `playSoundFrontEnd` = 7-bit RPC | VOTV equivalent: enumerate the small set of UI/event sounds and send by index. Streaming WAVs over the wire is wrong. |
| `CClientSound` 3D sounds as entities | Promote to "real entity" with position + URL/path; receiver plays locally. Same AOI rules. |

### What we should copy directly

1. **`m_ucSyncTimeContext`-per-entity.** Already on our roadmap. Universal anti-stale + authority handoff signal.
2. **Single-syncer-per-entity invariant + server-side `m_pSyncer` pointer.** The architectural cornerstone.
3. **Two cadences: per-tick streaming (unreliable-sequenced) + lifecycle events (reliable-sequenced).** Mandatory split; mixing them is a known anti-pattern.
4. **Initiator-suppress-then-server-echo** for one-shot events (explosions, weapon fire, pickup sounds).
5. **Hysteresis on syncer-radius (80 m start, 100 m keep) + load-balanced syncer pick.**
6. **Animation = `(name, startTime, duration)` event, NOT per-frame transforms.**
7. **Flag-byte delta-encoded per-tick state.** Already on our prop wire — formalise it as the standard pattern.

### What needs adaptation for UE4.27 / VOTV

1. **No GTA:SA-style game-object pool to stream in/out.** UE actors persist
   once spawned (or are explicitly destroyed). Our "stream-in" is "spawn the
   actor"; "stream-out" is "destroy". This may not match MTA's
   stream-in/out cap pattern — we should think in terms of "actor exists"
   rather than "actor is currently in the per-player render set".
2. **Dimensions don't apply directly.** VOTV is single-map; the
   dimension-aware streamer collapses to no-op. Keep the concept reserved
   if VOTV ever adds map transitions.
3. **The Lua scripting layer is absent.** MTA's Lua events (`onElementStartSync`,
   `onPickupUse`, etc.) are host-side hooks for user code. We have no
   scripting layer to expose; the events become host-side internal
   callbacks (or are dropped entirely).
4. **Element-tree (parent/child)** isn't a thing in UE in the MTA sense.
   Attachment is, but it's a SceneComponent concern. We DON'T need to copy
   the element-tree wholesale — a flat `TMap<EntityKey, EntityRecord>` plus
   "attached-to" pointer is enough.
5. **UE replication system exists but VOTV doesn't use it.** Per CLAUDE.md
   principle 7 we're rolling our own UDP layer anyway. MTA's wire format
   is the right reference; UE replication is not.

### Cross-references

- Existing physics-prop grab plan (`physics-object-pickup-coop-plan-2026-05-23.md`)
  is the *single-entity* application of these patterns; this document is
  the *N-entity* generalisation.
- `mta-pose-interpolation-2026-05-23.md` documents the receiver-side lerp
  in more depth.
- `mta-position-sync-2026-05-23.md` documents the player puresync path
  which is the higher-bandwidth sibling of the ped-sync path here.
- The COOP_METHODOLOGY notes that AI / cutscene sync should follow MTA
  fidelity (WP2). This document gives concrete file:line references for
  that audit.
- `feedback-check-mta-and-document.md` rule: before designing the NPC/entity
  layer for VOTV, this document must be consulted; the implementation plan
  should be in a separate file (e.g. `votv-npc-entity-coop-plan-<date>.md`)
  cross-referencing this one.

### Key file references (for the next agent)

Client:
- `reference/mtasa-blue/Client/mods/deathmatch/logic/CClientEntity.{h,cpp}` — base
- `reference/mtasa-blue/Client/mods/deathmatch/logic/CClientStreamer.{h,cpp}` — AOI
- `reference/mtasa-blue/Client/mods/deathmatch/logic/CPedSync.{h,cpp}` — NPC syncer
- `reference/mtasa-blue/Client/mods/deathmatch/logic/CObjectSync.{h,cpp}` — physics object syncer (#ifdef)
- `reference/mtasa-blue/Client/mods/deathmatch/logic/CClientPickup.{h,cpp}` — pickup entity
- `reference/mtasa-blue/Client/mods/deathmatch/logic/CClientExplosionManager.{h,cpp}` — explosion intercept

Server:
- `reference/mtasa-blue/Server/mods/deathmatch/logic/CElement.{h,cpp}` — base + `GenerateSyncTimeContext`
- `reference/mtasa-blue/Server/mods/deathmatch/logic/CPedSync.{h,cpp}` — syncer-pick + AOI relay
- `reference/mtasa-blue/Server/mods/deathmatch/logic/CObjectSync.{h,cpp}` — object syncer-pick
- `reference/mtasa-blue/Server/mods/deathmatch/logic/CUnoccupiedVehicleSync.{h,cpp}` — push-packet authority handoff
- `reference/mtasa-blue/Server/mods/deathmatch/logic/CPickup.{h,cpp}` + `CPickupManager.{h,cpp}` — server-authoritative pickup
- `reference/mtasa-blue/Server/mods/deathmatch/logic/CGame.cpp:2831` — `Packet_ExplosionSync` (AOI broadcast)
- `reference/mtasa-blue/Server/mods/deathmatch/logic/packets/CPedSyncPacket.{h,cpp}` — packet shape
- `reference/mtasa-blue/Server/mods/deathmatch/logic/packets/CObjectSyncPacket.{h,cpp}` — packet shape
- `reference/mtasa-blue/Server/mods/deathmatch/logic/packets/CExplosionSyncPacket.{h,cpp}` — packet shape
- `reference/mtasa-blue/Server/mods/deathmatch/logic/packets/CEntityAddPacket.{h,cpp}` — entity creation broadcast
- `reference/mtasa-blue/Server/mods/deathmatch/logic/packets/CPickupHitConfirmPacket.{h,cpp}` — pickup-hit
- `reference/mtasa-blue/Shared/mods/deathmatch/logic/CTickRateSettings.h` — all the tick rates
- `reference/mtasa-blue/Shared/sdk/net/SyncStructures.h` — wire-format sync structs
