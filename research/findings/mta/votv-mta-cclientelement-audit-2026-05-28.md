# MTA CClientElement adoption audit for VOTV_MP

**Date**: 2026-05-28
**Author**: research agent (Claude)
**Context**: User directive verbatim — *"Lets borrow cclientelement, also look for what else we need. Audit"*. Trigger: PR-4.11a/b just split `weather_sync` into 3 files matching MTA's "one file per feature" precedent; user wants the next step of architectural alignment with MTA.
**Source root**: `reference/mtasa-blue/` (vendored read-only, MIT).
**Scope**: This is a **research deliverable** — no source changes. The output is this file + a high-level adoption proposal. Concrete implementation is a separate PR.

> Caveat on the name: MTA's client-side base is `CClientEntity`, not `CClientElement`. The **server** side has `CElement`. The user's "CClientElement" wording is the conceptual shape — base entity class with hierarchical ID + parent/child tree + type tag — which on the client is `CClientEntity` and shared via the wire-encoded `struct ElementID`. The audit treats `CClientEntity` / `CElement` / `ElementID` / `CElementIDs` as one architectural pattern.

---

## 1. Map the hierarchy

### 1.1 Base class — `CClientEntity` (client) / `CElement` (server)

`reference/mtasa-blue/Client/mods/deathmatch/logic/CClientEntity.h:151` declares
`class CClientEntity : public CClientEntityBase`. The base provides 14
responsibilities, observable directly in the header. Listed with the lines that
prove each:

| # | Capability | Header reference |
|---|---|---|
| 1 | **ID** (`m_ID`, `GetID`/`SetID`, `ElementID` value type) | `CClientEntity.h:201,347` |
| 2 | **Type tag** as enum + hashed string (`m_uiTypeHash`, `m_strTypeName`, `GetType()` pure virtual) | `CClientEntity.h:42-84,158,182-186,352-353` |
| 3 | **Class kind enum** for safe-downcast (`eCClientEntityClassTypes` + `DECLARE_BASE_CLASS`) | `CClientEntity.h:100-149,153` |
| 4 | **Parent/child tree** (`m_pParent`, `m_Children`, `GetParent`/`SetParent`/`AddChild`) | `CClientEntity.h:188-198,340-342` |
| 5 | **Named lookup** (`m_strName`, `FindChild`, `FindChildByType`, `FindAllChildrenByType`) | `CClientEntity.h:180-181,261-266,354` |
| 6 | **Custom data dictionary** (`CCustomData* m_pCustomData` — Lua key/value bag with sync flag) | `CClientEntity.h:204-212,345` |
| 7 | **Dimension/interior** (world layer + interior id; entities in different dimensions don't see each other) | `CClientEntity.h:229-230,315-316,349,374` |
| 8 | **Position/rotation/matrix** (virtual, pure on `GetPosition`/`SetPosition`; concrete subclasses override) | `CClientEntity.h:214-227` |
| 9 | **Attachment** (one parent attach + N attached children, with offsets and recursion check) | `CClientEntity.h:237-248,360-364` |
| 10 | **Lifecycle flag** `m_bBeingDeleted` + `SetBeingDeleted` (used everywhere to skip a doomed object) | `CClientEntity.h:193-194,366` |
| 11 | **System-entity flag** (a script can't delete this) | `CClientEntity.h:165-166,367` |
| 12 | **Sync-time context** byte (anti-late-packet for realtime-synced elements) | `CClientEntity.h:172-178,358` |
| 13 | **Event dispatch** (`CMapEventManager m_pEventManager`, `AddEvent`/`CallEvent`/`DeleteEvent` with propagation across parent/child) | `CClientEntity.h:250-258,289,368` |
| 14 | **Collision/origin-source bookkeeping** (`m_Collisions` list, `m_OriginSourceUsers` for peds standing on this entity) | `CClientEntity.h:273-282,296-300,370-373` |

The server-side `CElement` (`Server/mods/deathmatch/logic/CElement.h:53`) has the
same 14-responsibility shape with the type enum `EElementType` (`CElement.h:61-88`)
in place of `eClientEntityType`, plus a few server-only things (per-player
visibility / element-group / spatial database registration on
`CElement.cpp:124`).

### 1.2 The full client subclass tree

From `reference/mtasa-blue/Client/mods/deathmatch/logic/CClientEntity.h:100-149`
the registered class kinds are 40 entries. Walking the actual `class … : public …`
declarations in `CClient*.h`:

```
CClientEntity (base, abstract — GetType() pure virtual)
├── CClientCamera                          (final, CClientEntity.h)
├── CClientDummy                           (final — "anything user-defined")
├── CClientGUIElement                      (CEGUI binding)
├── CClientColShape                        (abstract collider; subtypes below)
│   ├── CClientColCircle, CClientColCuboid, CClientColPolygon,
│   │   CClientColRectangle, CClientColSphere, CClientColTube
├── CClientPathNode                        (final)
├── CClientEffect                          (final — particle effects)
├── CClientBuilding                        (final)
├── CClientStreamElement                   (streamed/streamable abstract)
│   ├── CClientObject
│   │   └── CDeathmatchObject              (vehicle-attachable, scriptable obj)
│   ├── CClientPed                         (CClientEntity.h:158 — also CAntiCheatModule)
│   │   └── CClientPlayer                  (final — local OR remote human)
│   ├── CClientVehicle
│   │   └── CDeathmatchVehicle
│   ├── CClientMarker        (CClientMarkerCommon-bridged: Checkpoint, 3DMarker, Corona)
│   ├── CClientPickup
│   ├── CClientRadarArea
│   ├── CClientRadarMarker
│   ├── CClientProjectile
│   ├── CClientSound
│   ├── CClientWater
│   ├── CClientWeapon
│   ├── CClientPointLights
│   ├── CClientSearchLight
├── (renderable / asset-like CClientRenderElement subtree:)
│   CClientDFF, CClientCol, CClientTXD, CClientIMG, CClientDxFont,
│   CClientGuiFont, CClientMaterial (Texture, Shader, RenderTarget,
│   ScreenSource), CClientWebBrowser, CClientVectorGraphic
├── CClientTeam                            (a tag with player list; not a world object)
└── CClientIFP                             (animation file binding)
```

Three structural observations matter for VOTV:

1. **The base is intentionally minimal** — `CClientEntity` is the ID + tree + name +
   events plumbing. World-position / streaming are pushed to
   `CClientStreamElement`. Asset-like things (`CClientDFF`, fonts, textures) are
   `CClientEntity` siblings of world objects, not children — *they're elements
   too*, with an ID, parent, and name, but no position/streaming.
2. **`CClientPlayer` is just a refinement of `CClientPed`** (`CClientPlayer.h:32`).
   Local and remote are the same class — the discriminator is `IsLocalPlayer()`
   (line `CClientEntity.h:159`: `IsLocalEntity() { return m_ID >= MAX_SERVER_ELEMENTS; }`
   is for client-side scriptable objects; for players, see `CClientPlayer::IsLocalPlayer`).
3. **`CClientObject` is the world-prop equivalent** — for our purposes, the
   chipPile/clump/trashBitsPile/`Aprop_*_C` analog. It inherits the full streaming
   + position machinery via `CClientStreamElement`.

### 1.3 What the base actually provides at runtime

From `CClientEntity::CClientEntity(ElementID)` constructor body
(`CClientEntity.cpp:19-60`):

```cpp
CClientEntity::CClientEntity(ElementID ID) : ClassInit(this)
{
    ...
    if (ID == INVALID_ELEMENT_ID)
        ID = CElementIDs::PopClientID();      // client-locally created
    m_ID = ID;
    CElementIDs::SetElement(ID, this);        // register in global ID->ptr table
    m_pCustomData = new CCustomData;
    m_pEventManager = new CMapEventManager;
    ...
}
```

Every CClientEntity instance allocates a custom-data dict + event manager up
front, and registers itself in a global ID-indexed array. The destructor
(`CClientEntity.cpp:62-130`) is the mirror: clear children, push the client-side
ID back to the free stack, delete CCustomData, walk attached entities to
re-attach them to null, clean collisions, etc.

This is **a lot** for a base class — about half of it (CCustomData,
CMapEventManager Lua dispatch, OriginSourceUsers, attachment chains) is
gameplay-script-driven. The lookup table + ID + parent/child + type tag is the
load-bearing minimum.

---

## 2. The ID system specifically

### 2.1 Type definition

`reference/mtasa-blue/Shared/sdk/Common.h:25-83`:

```cpp
#define RESERVED_ELEMENT_ID 0xFFFFFFFE
#define INVALID_ELEMENT_ID  0xFFFFFFFF
#define MAX_SERVER_ELEMENTS 131072         // 2^17
#define MAX_CLIENT_ELEMENTS 131072         // 2^17

struct ElementID {
    ElementID(const unsigned int& value = INVALID_ELEMENT_ID) : m_value(value) {}
    ...
    unsigned int& Value();
private:
    unsigned int m_value;
};
```

`ElementID` is a thin `uint32_t` wrapper with comparison + increment operators.
**32 bits, value-typed, passed by value everywhere.** It is the SAME type on
client and server.

### 2.2 The ID space is partitioned into TWO ranges

- **Server-assigned**: `[0, MAX_SERVER_ELEMENTS - 1]` = `[0, 131071]`. Issued by
  the **server** via `CElementIDs::PopUniqueID()`
  (`Server/mods/deathmatch/logic/CElementIDs.cpp:23-34`), pushed back via
  `PushUniqueID` (line 36-44).
- **Client-assigned**: `[MAX_SERVER_ELEMENTS, MAX_SERVER_ELEMENTS + MAX_CLIENT_ELEMENTS - 1]`
  = `[131072, 262143]`. Issued by the **client** via `CElementIDs::PopClientID()`
  (`Client/mods/deathmatch/logic/CElementArray.cpp:48-61`). The client stack
  stores ids in `[0, MAX_CLIENT_ELEMENTS-2]` but adds `MAX_SERVER_ELEMENTS` when
  it pops, so the externally visible id is in the client range.

`CClientEntity::IsLocalEntity()` (`CClientEntity.h:159`) is literally the
range check: `m_ID >= MAX_SERVER_ELEMENTS`. That is THE discriminator for
"this element was made by client script and is not synced to server" vs
"this element came from server / is server-tracked".

### 2.3 The free-list algorithm

Both server and client use `CStack<ElementID, MAX_-2>` (`CElementIDs.h:28`).
`PopUniqueID` returns the top of the stack and stores
`m_Elements[ID.Value()] = pElement` (a SFixedArray of size `MAX_SERVER_ELEMENTS`).
`PushUniqueID` puts the id back on the stack and `NULL`s the table slot.
**Deletion frees the id immediately**; the next created element on this side
may get the same id back. This is why MTA needs the `m_ucSyncTimeContext` byte
(`CClientEntity.h:172-178`) — a counter incremented on every sync-relevant
state change of the element, used as a "what generation of this id are you
talking about" anti-stale-packet check. Without it, a network packet referring
to "ID 42" could land on a different entity than the sender meant.

### 2.4 IDs on the wire

`reference/mtasa-blue/Shared/sdk/net/bitstream.h:381-411`:

```cpp
// Write an element ID
void Write(const ElementID& ID) {
    static const unsigned int bitcount = NumberOfSignificantBits<(MAX_ELEMENTS-1)>::COUNT;
    const unsigned int& IDref = ID.Value();
    #ifdef MTA_CLIENT
    if (IDref != INVALID_ELEMENT_ID && IDref >= MAX_SERVER_ELEMENTS) {
        dassert("Sending client side element id to server" && 0);
        ...
    }
    #endif
    WriteBits(reinterpret_cast<const unsigned char*>(&IDref), bitcount);
}
```

`MAX_ELEMENTS` is `131072` on client (`#define MAX_ELEMENTS MAX_CLIENT_ELEMENTS`)
and same on server. `NumberOfSignificantBits<131071>::COUNT` is **17**.
**Every entity reference on the wire is 17 bits.** With `MAX_VALUE = (1<<17)-1`
mapping to `INVALID_ELEMENT_ID`. That's the secret sauce: 32-bit type at rest,
17-bit packed on the wire.

### 2.5 Wire packets refer to entities by ID, exclusively

`reference/mtasa-blue/Client/mods/deathmatch/logic/rpc/CElementRPCs.cpp:64-82`:

```cpp
void CElementRPCs::SetElementParent(CClientEntity* pSource, NetBitStreamInterface& bitStream)
{
    ElementID ParentID;
    if (!bitStream.Read(ParentID)) return;
    CClientEntity* pParent = CElementIDs::GetElement(ParentID);
    if (!pParent) return;
    ...
    pSource->SetParent(pParent);
}
```

The pattern repeats everywhere: read `ElementID`, call
`CElementIDs::GetElement(ID)` to resolve to a typed `CClientEntity*`, dynamic-
cast as needed. **No prop-key string, no NPC session id, no player slot —
one ID for everything.**

### 2.6 Summary of MTA's ID model

| Property | MTA |
|---|---|
| Width | uint32_t (32 bit at rest, 17 bit on wire) |
| Space | server `[0, 131071]`, client `[131072, 262143]`, invalid `0xFFFFFFFF` |
| Authority | server assigns server-range; each client assigns within its own client-range |
| Reuse policy | Stack-based free-list; immediate reuse with `m_ucSyncTimeContext` byte to disambiguate generations |
| Storage | global `SFixedArray<CElement*, MAX>` lookup table |
| Lookup | `CElementIDs::GetElement(ID)` — O(1) array index |
| Lifecycle | `PopUniqueID()` in constructor, `PushUniqueID()` in destructor |
| Stability | NOT stable across sessions — ids are runtime-only. Persistence (map files, save state) uses NAMES not IDs. |

---

## 3. Compare to our 3-ID system

VOTV today has three parallel ID universes, used for three orthogonal kinds of
replicated entity. Reading `src/votv-coop/include/coop/net/protocol.h` and the
feature files, they are:

### 3.1 `peerSessionId` (uint8_t, value range 0..3)

- Defined: `players_registry.h:47` — `inline constexpr uint8_t kMaxPeers = 4`.
- Assignment: host always 0; clients receive 1..3 via `AssignPeerSlot`
  reliable message (`protocol.h:228-239`, `kProtocolVersion = 11`).
- Used in: `ItemActivatePayload.peerSessionId`, `WeatherStatePayload.peerSessionId`,
  `RedSkyPayload.peerSessionId`, `LightningStrikePayload.peerSessionId`.
- Lookup: `Registry::Puppet(uint8_t peerId) -> RemotePlayer*` — O(1) array index
  into `puppetByPeer_[kMaxPeers]` (`players_registry.cpp:64-67`).
- Reverse: `Registry::PeerIdOfActor(void* actor)` — linear scan of
  `puppetByPeer_` + local cache (`players_registry.cpp:97-106`).

### 3.2 Prop wire-key (variable-length string in `WireKey`, max 31 chars)

- Defined: `protocol.h:335-339` — `struct WireKey { uint8_t len; char data[31]; }`.
- Assignment: VOTV's `Aprop_C` BP exposes a `key` UProperty containing a
  GUID-like persistent identifier. chipPile/clump/trashBits expose a `GetKey()`
  UFunction; for None-keyed prop classes we synthesize at Init POST.
- Used in: `PropPoseSnapshot.key`, `PropReleasePayload.key`, `PropSpawnPayload.key`,
  `PropDestroyPayload.key`, `ItemActivatePayload.actorKeyHash` (CRC32 of the
  string for the case-a world-prop path).
- Lookup: `prop_wrap::FindByKeyString(const std::string& k) -> AActor*` —
  GUObjectArray scan filtered by class, then `Key` property compare.

### 3.3 `npcSessionId` (uint32_t)

- Defined: `protocol.h:473-479` — `EntitySpawnPayload.sessionId`.
- Assignment: host-only via `std::atomic<uint32_t> g_nextNpcSessionId++`.
- Used in: `EntitySpawnPayload.sessionId`, `EntityDestroyPayload.sessionId`.
- Lookup: `g_npcMirrorBySessionId[sessionId] -> AActor*` — std::unordered_map.

### 3.4 Direct comparison

| Concern | MTA | VOTV (today) |
|---|---|---|
| Address space | One unified uint32 across players/props/NPCs/markers/etc. | Three separate spaces |
| Width on wire | 17 bits | uint8 (peer), 32-byte WireKey (prop), uint32 (npc) |
| Authority | server (server range) + client (client range) | host (peer slot, npc id), distributed (prop key — from cooked content / synth) |
| Cross-peer stability of an existing reference | runtime-only, NOT save-stable | prop key IS save-stable (it's the persisted Aprop_C.Key); peer slot + npc id are session-only |
| Late-joiner snapshot | server walks its element list, sends `Element*` packets | needs three parallel walks — one per id system |
| Routing in event_feed | `CElementIDs::GetElement(ID)` -> dynamic_cast | three different lookup paths, three switch cases |

### 3.5 Could MTA's unified model work for VOTV?

**Yes, with one caveat.** The breakdown by kind:

#### Players — straightforward fit

`peerSessionId` is already a small-range integer assigned by the host
(authoritative side). Mapping to MTA terms:
- **Player elements** live in `[0, kMaxPeers)` of the unified ID space.
- This range is **host-only assigned** (analog of MTA's server range).
- The wire field becomes `ElementID` instead of `uint8_t peerSessionId`.
- `Registry::Puppet(slot)` becomes `ElementRegistry::Get(playerSlotToElementId(slot))`
  -> cast to `coop::element::Player*`.

The handshake (`AssignPeerSlot` packet) keeps its existing semantics —
it's literally the host telling the client which element id was reserved
for it on connect.

#### Props — fit, but `WireKey` stays as the **persistent identity**

This is the only subtlety. The prop's BP `Key` string IS the save-stable
identity — when the game persists a world, `Aprop_C.Key` is written to the
save and re-loaded on the next session. We **cannot retire `WireKey`** without
breaking that invariant (it's how the puppet's local prop is found at all on
world load before any network packet).

The right shape is:
- **Identity (persistent, save-stable)**: `Aprop_C.Key` string. Unchanged.
- **Address (runtime, wire-efficient)**: `ElementID` assigned on the
  POST-Init observer or first-receive. The element registry holds the
  string-key in `m_strName` (MTA already has this — `m_strName` is set
  via `SetName`).

On the wire, runtime `PropPose` packets shift from `WireKey` (32 bytes) to
`ElementID` (4 bytes / 17 bit on wire = 17/3 bits cheaper). Initial
`PropSpawn` carries BOTH the `ElementID` (host-assigned) AND the string
key (persistent identity, used for matching on world-load before sync).

**Crucial point**: MTA's `ElementID` is allowed to be non-persistent — `Element`
also has a `m_strName` and a `m_strTypeName` (`CClientEntity.h:181,183`). Our
WireKey maps to `m_strName`; types like `"Aprop_chipPile_C"` map to
`m_strTypeName`. The ElementID is just the runtime O(1) handle. We get both.

#### NPCs — straightforward fit

`npcSessionId` is already host-monotonic uint32_t. It collapses cleanly
into the unified ElementID space (host-assigned range, same authority model
as players). The `g_npcMirrorBySessionId` map dies and becomes a lookup
through the central element registry. The reflection: a `WireClassName`
(64 bytes) is still needed for the spawn message (we need to know which
UClass to spawn) but that's the class **type tag**, mapping to MTA's
`m_strTypeName`.

#### Would it collapse the 3 routing-id fields in payloads?

**Yes, every payload that today carries `peerSessionId` or a key or a
session id would carry `ElementID owner` (or `target`) instead.**
Concrete changes:
- `ItemActivatePayload`: drop `peerSessionId` (1 byte) + `actorKeyHash`
  (4 bytes) + `flags.has_actor_key` -> single `ElementID owner` (2-3
  bytes on wire) + `ElementID actor` (2-3 bytes on wire). Net: -1 wire
  byte but **much** cleaner — one resolve path instead of two.
- `WeatherStatePayload`, `RedSkyPayload`, `LightningStrikePayload`:
  `peerSessionId` becomes "is the sender element id in the host slot
  range" — same check, no payload field needed (it's already in the
  GNS connection metadata).
- `EntitySpawnPayload`: `sessionId` -> `ElementID`. Same shape.
- `PropSpawnPayload`: today carries `WireKey` (32 bytes). Switches to
  `ElementID` (4 bytes) + `WireKey` (32 bytes, optional — only for
  persistent props the receiver needs to bind by save-key). Net wider
  for first-spawn, narrower for everything subsequent.

`event_feed.cpp` dispatch currently has three switch cases —
`peerSessionId` resolution, prop key resolution, npc id resolution.
After adoption: one `ElementRegistry::Resolve(ElementID) -> Element*`
followed by `dynamic_cast<Player*|Prop*|Npc*>`. This is **the most
direct architectural win** — cyclomatic complexity in `event_feed` drops
substantially and a new entity type (Door, Light, Vehicle) requires
adding ONE subclass instead of ONE more parallel id system.

---

## 4. Concrete adoption proposal

### 4.1 Header sketch — `coop::element::Element`

```cpp
// src/votv-coop/include/coop/element/element.h
//
// Adapted from reference/mtasa-blue/Client/mods/deathmatch/logic/CClientEntity.h
// (MIT). The "minimum useful" version: ID + type + parent/child + actor pointer.
// Lua-driven features (CCustomData, CMapEventManager) DELIBERATELY OMITTED -- we
// have no Lua at runtime (RULE 3).

#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace coop::element {

using ElementId = uint32_t;
inline constexpr ElementId kInvalidId = 0xFFFFFFFFu;
inline constexpr uint32_t  kMaxElements = 65536;        // 16-bit on wire

enum class ElementType : uint8_t {
    Unknown = 0,
    Player  = 1,
    Prop    = 2,
    Npc     = 3,
    // Doors / Lights / Vehicles / etc. get IDs as the feature lands.
};

class Element {
public:
    explicit Element(ElementType t);
    virtual ~Element();

    // Identity
    ElementId   GetId() const   { return m_id; }
    ElementType GetType() const { return m_type; }

    // Save-stable identity (e.g. Aprop_C.Key) -- optional; empty for runtime-only
    // elements like players or NPCs.
    const std::string& GetName() const     { return m_name; }
    void SetName(std::string n)            { m_name = std::move(n); }

    // Class-name tag (e.g. "Aprop_chipPile_C", "MainPlayerClass"). Replaces
    // WireClassName in spawn payloads at the per-element level.
    const std::string& GetTypeName() const { return m_typeName; }
    void SetTypeName(std::string n)        { m_typeName = std::move(n); }

    // Engine actor pointer (the UE-side AActor*). Subclasses own the lifecycle of
    // their actor; the Element does NOT delete the actor in its destructor (the
    // engine owns it; MTA principle 3 in CLAUDE.md).
    void* GetActor() const          { return m_actor; }
    void  SetActor(void* a)         { m_actor = a; }

    // Lifecycle flag (mirrors MTA's m_bBeingDeleted)
    bool IsBeingDeleted() const     { return m_beingDeleted; }
    void SetBeingDeleted(bool b)    { m_beingDeleted = b; }

    // Sync-time context byte (mirrors MTA's m_ucSyncTimeContext -- generation
    // counter so stale packets after id reuse are dropped).
    uint8_t GetSyncContext() const  { return m_syncContext; }
    void    BumpSyncContext()       { ++m_syncContext; }

    // Hierarchy (lighter than MTA -- single parent, vector of children; no
    // attachment chain or origin-source-users).
    Element* GetParent() const                            { return m_parent; }
    void SetParent(Element* p);
    const std::vector<Element*>& GetChildren() const      { return m_children; }

private:
    ElementId   m_id           = kInvalidId;
    ElementType m_type         = ElementType::Unknown;
    std::string m_name;          // save-stable name, optional
    std::string m_typeName;      // class kind tag
    void*       m_actor        = nullptr;
    bool        m_beingDeleted = false;
    uint8_t     m_syncContext  = 0;
    Element*    m_parent       = nullptr;
    std::vector<Element*> m_children;
};

}  // namespace coop::element
```

### 4.2 ID allocator — `coop::element::Registry`

```cpp
// src/votv-coop/include/coop/element/registry.h
//
// Adapted from CElementIDs (MTA). Two-range partition:
//   host-assigned: [0, kMaxElements/2)        -- MTA's server range
//   peer-assigned: [kMaxElements/2, kMaxElements)  -- MTA's client range

namespace coop::element {

class Registry {
public:
    static Registry& Get();

    // Pop a fresh id from the host range. Authoritative side only.
    ElementId AllocHostId(Element* e);
    // Pop a fresh id from the local-peer range. For client-side-spawned things
    // (e.g. UI markers we never replicate).
    ElementId AllocLocalId(Element* e);

    // Return an id to its free stack. Called from Element destructor.
    void FreeId(ElementId id);

    // O(1) resolve.
    Element* Get(ElementId id);

    // Type-filtered iterator (e.g. for late-joiner snapshot of all Props).
    template <ElementType T> void ForEach(auto&& fn);

    // True if `id` came from the host-assigned range (server-element analog).
    static bool IsHostId(ElementId id) { return id < kMaxElements/2; }

private:
    Element*           m_byId[kMaxElements] = {};
    std::vector<ElementId> m_hostFree;   // free stack, host range
    std::vector<ElementId> m_localFree;  // free stack, local-peer range
};

}  // namespace coop::element
```

### 4.3 Subclass shape per current feature

```cpp
// src/votv-coop/include/coop/element/player.h
class Player : public Element {
public:
    Player();
    uint8_t PeerSlot() const;         // derived: id - kPlayerIdBase
    RemotePlayer* Puppet() const;     // existing class, owned externally
};

// src/votv-coop/include/coop/element/prop.h
class Prop : public Element {
public:
    Prop();
    // Save-stable Aprop_C.Key lives in Element::m_name.
    // Class (Aprop_chipPile_C etc) lives in Element::m_typeName.
    // Held-by-element pointer (the player who has it in hand):
    Element* GetHolder() const;
    void     SetHolder(Element* p);
};

// src/votv-coop/include/coop/element/npc.h
class Npc : public Element {
public:
    Npc();
    // Class (npc_zombie_C etc) lives in Element::m_typeName.
    // No persistent name -- npcs are runtime-only; m_name stays empty.
};
```

### 4.4 Where this slots in vs current code

- `coop::players::Registry` becomes a **thin facade** over `coop::element::Registry`:
  `Registry::Puppet(peerSlot)` is implemented as `element::Registry::Get(playerSlotToElementId(peerSlot))->As<Player>()->Puppet()`.
  The existing `players_registry.cpp:64` `puppetByPeer_[kMaxPeers]` array is replaced.
- `prop_lifecycle.cpp` `g_processedInitActors` and `prop_wrap::FindByKeyString`
  collapse: the registry already maps id <-> Element*; the persistent name
  (`Aprop_C.Key`) is searchable via a `name -> id` side-index.
- `npc_sync.cpp` `g_npcMirrorBySessionId` becomes `element::Registry::Get(npcId)`.
- `event_feed.cpp` central dispatch: `ResolveOwner(packet.ownerId)` returns
  an `Element*`; switch on `e->GetType()` if needed. One path.

### 4.5 Minimum proof-of-concept vs full adoption

**Proof-of-concept (one PR, ~1 week scope)**: convert **NPCs** first. Reasoning:
1. NPCs already have a uint32 atomic-counter — closest semantic match to
   MTA's ElementID free-list.
2. NPC lifecycle is the simplest (host spawns, host destroys, no
   per-frame ownership transfer like prop grab/release).
3. `g_npcMirrorBySessionId` (one map) is the smallest "before" footprint.
4. Wire impact: `EntitySpawnPayload.sessionId` becomes `EntitySpawnPayload.elementId`;
   `EntityDestroyPayload.sessionId` likewise. The npc_sync.cpp host-side
   counter goes away — `Registry::AllocHostId()` replaces it.

**Full adoption (post-PoC, ~3-4 PRs)**:
1. NPC PoC (above) — establishes the Element/Registry/Player/Prop/Npc files.
2. Players: thread ElementID through the AssignPeerSlot handshake, keep
   the `peerSlot` accessor for human-readable logging.
3. Props: dual-track Aprop_C.Key (save-stable) + ElementID (runtime).
   Late-joiner snapshot uses `Registry::ForEach<Prop>` instead of a
   GUObjectArray scan.
4. Retire `peerSessionId` field from payloads; promote `ItemActivatePayload`,
   `WeatherStatePayload`, `RedSkyPayload`, `LightningStrikePayload`,
   `EntitySpawnPayload`, `EntityDestroyPayload`, `PropSpawnPayload`,
   `PropDestroyPayload`, `PropReleasePayload`, `PropPoseSnapshot` to ElementID.
5. Protocol version bump v11 -> v12; delete old fields per RULE 2.

---

## 5. Cost/benefit — concrete wins on the open list

Reading `research/findings/architecture-audits/votv-coop-audit-post-pr4-7-2026-05-28.md` and the
open items in `memory/MEMORY.md`:

### Win 1 — Late-joiner snapshot for ALL replicated entities, single code path

**Open item**: "prop late-joiner snapshot" (audit doc, H-row category) — when a
new client connects mid-session, the host should send a snapshot of all
currently-spawned props so the client doesn't see physics-floating un-keyed
versions. Today this would require a GUObjectArray scan for keyed Aprop_C,
plus a parallel walk of `g_npcMirrorBySessionId`, plus the per-player puppet
state — three traversals, three packet sequences.

With Element adoption: `Registry::ForEach<Prop>([&](Prop* p){ SendSpawn(p->GetId(), p->GetName(), ...); });`
plus the same for `<Npc>` and `<Player>`. The host-side code is ~30 LOC and
covers every replicated entity type, including types added later (doors,
lights, vehicles) **automatically**.

This is genuinely the single biggest win — every future replicated-entity
feature inherits late-joiner support for free.

### Win 2 — Partial-disconnect cleanup is one line per element type

**Open item** (memory/MEMORY.md, post-PR-4.7 audit, PR-4.7 commit message):
"Per-slot disconnect-edge needs 3-peer hands-on partial-disc test for true
validation. ... remote_prop::OnDisconnectForSlot releases g_drives[slot] ...
item_activate::OnDisconnectForSlot clears g_pendingApply{Valid,Payload}[slot];
prop_lifecycle/npc_sync/weather_sync skipped per RULE 1 -- they hold GLOBAL
state not per-slot."

The whole shape of `OnDisconnectForSlot` exists because each feature owns its
own per-slot state. With Elements: on disconnect, walk
`Registry::ForEach<>` filtering by `e->GetParent() == disconnectedPlayer`
(or by stored authority — "this element is host-authoritative, my role just
became orphaned"). One central cleanup, no per-feature handlers, no
"prop_lifecycle skipped" footnotes.

### Win 3 (bonus — not on the open list but related) — Generation-counter slots in `m_ucSyncTimeContext`

**Open item** (audit C2 — `ResolveAndStartDrive does not initialize lastApplyMs`):
the underlying issue is that the prop's runtime state machine doesn't carry
a generation marker, so a stale packet can confuse a freshly-grabbed prop.
MTA's `m_ucSyncTimeContext` byte is precisely this — bumped on every grab/
release/destroy, sent with every sync packet, checked at receive to discard
out-of-order events. Adopting Element gives us this byte on every entity
automatically.

---

## 6. What NOT to copy from MTA

### 6.1 `CCustomData` (`CClientEntity.h:204-212,345`)

MTA's per-element key/value bag for Lua scripts to attach arbitrary state.
**Skip entirely.** We have no Lua at runtime (RULE 3). Any state we want
synced lives in typed C++ fields on the Element subclass. Including this
class would burden every Element with a `new CCustomData` + destructor cost
for no gain.

### 6.2 `CMapEventManager` + `AddEvent`/`CallEvent` (`CClientEntity.h:250-258,289,368`)

MTA's Lua event dispatch — register a Lua callback for an event name on an
element with propagation through the parent/child tree. **Skip entirely.**
No Lua. Our equivalent is direct C++ function calls inside the appropriate
subsystem (e.g. `OnSpawn`, `OnRelease`); we don't need a generic dispatcher.

### 6.3 `Dimension` and `Interior` (`CClientEntity.h:229-230,315-316,349,374`)

MTA's world-layering — entities in different dimensions don't collide / aren't
visible to each other. Used heavily in MTA game modes for parallel arenas.
**Skip for now.** VOTV is a single-world horror exploration game; there is no
parallel-arena gameplay. Add later only if we get a scope where it's
warranted (e.g. saved game state isolation, future "instanced rooms" feature).
Until then, dropping these two fields shaves 2 bytes per Element and removes
the dimension-check from every visibility/collision query.

### 6.4 Attachment chains (`CClientEntity.h:237-248,360-364`)

MTA's `AttachTo` / `m_AttachedEntities[]` — useful for vehicles + peds, weapons
on peds, etc. **Skip for now.** Where we need a held prop to follow a player
we already have a dedicated `PropPose` packet driving the engine transform
each tick. A general attachment abstraction would be over-engineering for
the current scope. Add later if we see a 2nd use case.

### 6.5 `m_OriginSourceUsers` + `m_Contacts` (`CClientEntity.h:296-300,372-373`)

MTA's "who is standing on this entity" bookkeeping for peds, used by the
collision system. **Skip.** Physics + collision are owned by UE; we don't
sync them at this level of abstraction.

### 6.6 Class-kind enum + `DECLARE_BASE_CLASS` macro (`CClientEntity.h:100-149,153`)

MTA uses this to power a custom safe-downcast (`ListEntities<Type>`) that
avoids RTTI. **Defer judgement.** It's not free — every subclass has to
register its class kind. Our existing code uses `dynamic_cast` sparingly,
and a `dynamic_cast<Player*>(e)` from a known-`Element*` is acceptable in
the few places we need a downcast. If profiling later shows hot-path
downcasts (unlikely — most subsystems already work on typed pointers from
spawn through teardown), revisit. **Skip for v1**.

### 6.7 Element-Group (`CClientEntity.h:284-285,371`)

MTA's mechanism for resource-script-owned element batches — when a Lua
resource (mod) is unloaded, its elements are bulk-deleted via the group.
**Skip.** We don't have resource scripts.

### 6.8 Spatial database registration (`CElement.cpp:124`)

Server-only — broad-phase spatial index for entities. **Skip.** UE owns
spatial queries.

---

## 7. Total LOC impact estimate

- `coop::element::Element` + `Registry`: ~250 LOC headers, ~200 LOC impl.
  Below soft cap.
- Subclass headers (`Player`, `Prop`, `Npc`): ~50 LOC each.
- Migration of `npc_sync.cpp` (PoC): -1 std::unordered_map, +1 base-class
  inheritance, net wash.
- Full migration: estimated -300 LOC across `prop_lifecycle.cpp`,
  `npc_sync.cpp`, `event_feed.cpp`, `players_registry.cpp` (lookup paths
  collapse to one). Protocol payloads shrink by an average ~2 bytes each.

The architectural ratio is favorable: ~500 LOC of central infrastructure
unlocks per-feature simplifications across ~6 existing feature files and
makes future replicated-entity features (doors, lights, vehicles) one
subclass each.

---

## 8. Summary recommendation

Adopt Element + ElementID + Registry as a **central runtime entity addressing
layer**, keeping the existing save-stable identity (`Aprop_C.Key`) as
`Element::m_name` for props. Skip every Lua-coupled feature of MTA's design
(CCustomData, event manager, dimensions, attachments, element groups).
Start with NPC sync as the PoC (smallest before-footprint, cleanest fit),
then players, then props with dual-track identity. Protocol bump to v12,
deletion of `peerSessionId` / `npcSessionId` / WireKey-in-runtime-packets
per RULE 2 (no migration baggage).

Most impactful single benefit: late-joiner snapshot becomes one code path
for all replicated entities, including future types. Second: partial-
disconnect cleanup collapses to a central registry walk instead of per-feature
`OnDisconnectForSlot` hooks. Third (lifecycle robustness, addresses audit C2
in spirit): `m_ucSyncTimeContext` generation byte is free with every Element.
