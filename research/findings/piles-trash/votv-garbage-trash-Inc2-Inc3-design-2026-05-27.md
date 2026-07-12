# Phase 5G garbage/trash sync — Inc 2 + Inc 3 concrete design — 2026-05-27

Builds on `research/findings/votv-garbage-trash-interaction-RE-2026-05-27.md`
(class hierarchy + crash hypothesis) and the shipped Inc 1
(`src/votv-coop/src/coop/garbage_sync.cpp`: client-side PRE-cancel of
`Aprop_openContainer_C::ReceiveTick` + `::checkPickup` for
`Aprop_garbageContainer_C`).

This doc is implementation-ready: no further RE needed for the listed
suppression points (CXX dumps confirm them). The single open
runtime-only question is the `glueInside` attach semantics
(call-out in §3 + §5); a small `[probe] garbage=1` run answers it.

---

## 1. Inc 3 — host-authoritative spawner gating

5 spawner classes per the RE doc. Per-class table:

| # | Class | Target UFunction | Hook kind | Suppress on client? | Rationale |
|---|---|---|---|---|---|
| 1 | `AundergroundGarbageSpawner_C` | `Timer` | PRE-cancel (interceptor) | YES | Periodic loot roll via `prepareItems(Loc)`; pure timer. Cancelling stops the entire generation cycle including the random landscape-point pick. POST-and-destroy would still consume `prepareItems`' RNG state (host vs client divergence on subsequent rolls). |
| 2 | `Aevent_trashPiles_C` | `BndEvt__event_funnyGascans_Box_K2Node_ComponentBoundEvent_0_ComponentBeginOverlapSignature__DelegateSignature` | PRE-cancel | YES | Box-overlap-driven cluster spawn. The dump shows ONE delegate handler (long-name BndEvt -- single hook target). Cancel kills the cluster at source; matches mushroom_C overlap-spawn precedent (Stream B mushroom7_C suppression). |
| 3 | `AarirTrasher_C` | `trash` | PRE-cancel | YES | Periodic, `Count`-bounded. Same pattern as #1. PRE-cancel preserves `Count` parity (host's `Count` decrements; client's stays at session start -- doesn't matter because client only mirrors). |
| 4 | `Atool_garbageSpawner_C` | `Init` (with `class Aprop_toolgun_C* toolgun` param) | **NO suppression -- PLAYER ACTION** | NO | Toolgun fire is a per-shot local player action. RULE 1: route per-player inside SP systems (principle 6). Local client's toolgun fire MUST spawn the chip-pile/clump on the client; the resulting actor then broadcasts via the **existing Aprop-lifecycle Init POST observer** for prop-derived spawns OR via the new Inc-2 non-prop entity broadcast (§2) for actorChipPile / garbageClump spawns. **Exception flag in code: this class is explicitly listed as a NON-suppressed allowlist entry to document the deliberate carve-out.** |
| 5 | `AbaseCleaner_trashBits_C` | `ReceiveBeginPlay` (inherited from `AbaseCleaner_C`) | PRE-cancel | YES | The class adds no methods; its parent's `ReceiveBeginPlay` schedules the cleanup loop. Cancelling on the client stops the loop registration entirely. Host-authoritative cleanup events arrive via the Inc-2 `EntityDestroy`-style broadcast for the items the host's cleaner removes. |

**Implementation note:** All 4 suppression targets (#1, #2, #3, #5) use
`ue_wrap::game_thread::RegisterInterceptor` (PRE-cancel), mirroring the
weather-scheduler 5-fn pattern in `weather_sync.cpp` (which already runs a
multi-slot interceptor table of the same shape). Atool_garbageSpawner_C
(#4) is NOT hooked at all here -- its spawn output is captured downstream
by the existing/new entity-spawn observers.

For #2 the very long UFunction name is the canonical name in the dump;
resolution via `R::FindFunction(cls, L"BndEvt__...")` works as-is (an
identical pattern is already in use for other BndEvt hooks; see
`research/findings/votv-doors-and-lightswitches-RE-2026-05-25.md`).

---

## 2. Inc 2 — non-Aprop_C entity replication

### 2.1 Which entities

Per the RE doc, three non-Aprop_C classes need replication:

| Class | Parent | Identity field | Variant state |
|---|---|---|---|
| `AtrashBitsPile_C` | `Aactor_save_C` (has `setKey` via save-actor lineage; uses Aprop_C's Key system) | Key (FName) | `Shape@0x258, typeA@0x25C, typeB@0x25D, randomTypeA@0x25E, randomTypeB@0x25F, amountA@0x260, amountB@0x264, tt_*[8] @0x26C..0x273` (8 bool allowlist) |
| `Aprop_garbageClump_C` | `AActor` (NO save lineage) | **synthesized sessionId** | `chipType@0x0238 (1 byte), pile@0x0260 (UClass* -- send as classHash)` |
| `AactorChipPile_C` | `AActor` (NO save lineage) | **synthesized sessionId** | `chipType@0x0238 (1 byte), pile@0x0258 (UClass*), clump@0x0260 (UClass*)` |

`AtrashBitsPile_C` has a real save Key (its parent `Aactor_save_C` is the
save-actor base). The other two (`Aprop_garbageClump_C`,
`AactorChipPile_C`) are PURE `AActor` derivatives with no Key field --
they're not persisted across saves; they're transient world litter. They
need a host-assigned monotonic `sessionId`, **same pattern as NPCs in
`EntitySpawnPayload`**.

### 2.2 itemsInside content sync — DECISION: NOT REPLICATED IN INC 2

Re-RE during this design pass revealed: the `Aprop_openContainer_C` class
has **no** dedicated add/remove UFunction (only `checkPickup`,
`ReceiveTick`, `propAwoken`, `doStuff`, `loadData`, `glueInside`,
`playerHoldPre`, `freezed`, `ReceiveBeginPlay`,
`intComs_gamemodeBeginPlay`, `ReceiveDestroyed`, `getData`).
Add/remove is **BP-inlined inside `ReceiveTick`**: it walks the Box
collision component's overlap set, appends new overlapping AActor*s to
`itemsInside`, and `checkPickup` is the player-extract path that removes
entries.

Because Inc 1 **already PRE-cancels `ReceiveTick` + `checkPickup` on the
client**, the client's `itemsInside` never mutates after `loadData`. The
only remaining consumers (`glueInside`, `doStuff`) are themselves called
from `ReceiveTick` -- also suppressed. The array becomes inert storage,
not walked, no AV. The RULE 1 fix is the suppression itself; no wire
sync of `itemsInside` is required to prevent the crash.

**The visible-state consequence we accept in Inc 2:** if the host walks
across the map carrying a garbage container, its `itemsInside` contents
do NOT travel with the container on the client (the client renders the
container shell at the wire pose, but the items inside stay at their
original world positions because `glueInside` is suppressed). This is
visually wrong but non-crash. Inc 3+ can lift the suppression on
`glueInside` selectively once `itemsInside` is replicated (see §5
"Open work" -- this is the Inc-4 deliverable, not Inc-2).

This deletes the proposed `OpenContainerContents` packet from the
original brief. Cleaner: one fewer ReliableKind to wire + matches the
RE truth (no canonical mutator exists).

### 2.3 Wire packet — single `NonPropEntityState`

Bump `kProtocolVersion` 8 -> 9. Add to `protocol.h`:

```cpp
// ReliableKind enum: assign next free value. Current allocations:
//   1 Join, 2 PropRelease, 3 PropSpawn, 4 PropDestroy,
//   5 EntitySpawn, 6 EntityDestroy, 7 RestoreVitals, 8 TeleportClient,
//   12 ItemActivate, 13 WeatherState, 14 LightningStrike, 15 RedSky
// Free: 9, 10, 11, 16, ...
//   9 -> NonPropEntityState  (this packet)
//  10 -> NonPropEntityDestroy

enum class NonPropEntityClass : uint8_t {
    Invalid          = 0,
    TrashBitsPile    = 1,   // AtrashBitsPile_C       (has Key)
    GarbageClump     = 2,   // Aprop_garbageClump_C   (no Key, sessionId)
    ActorChipPile    = 3,   // AactorChipPile_C       (no Key, sessionId)
    // Subclass-aware: erie / leaves / wetConcrete variants distinguished
    // via the inner class-hash field (variantClassHash) so subclasses don't
    // need new enum entries. Subclass match uses SuperStruct walk in
    // npc_sync.cpp style.
};

struct NonPropEntityStatePayload {
    uint8_t  entityClass;       // NonPropEntityClass
    uint8_t  peerSessionId;     // 0 (host) -- receiver drops if !=0
    uint16_t variantClassHash16;// CRC32(leaf class name) low 16 bits (subclass)
                                //   for trashBitsPile: 0 (no subclasses in RE)
                                //   for clump:   "_C" vs "_erie_C" vs "_leaves_C" vs "_wetConcrete_C"
                                //   for chipPile: same 4-way split
    uint32_t identity;          // (a) for TrashBitsPile: low-32 of CRC32(Key string)
                                // (b) for GarbageClump / ActorChipPile: host-minted
                                //     monotonic sessionId (1-based; 0 invalid)
    // Identity disambiguation is implicit in entityClass: hosts and clients
    // never confuse a Key-CRC32 with a sessionId because lookup tables are
    // per-class.
    float    locX, locY, locZ;  // world cm
    float    rotPitch, rotYaw, rotRoll;
    uint8_t  variantState[12];  // class-specific blob; layout per §2.4 below
    uint8_t  _pad[4];           // 8-byte align the payload tail
};
static_assert(sizeof(NonPropEntityStatePayload) == 56,
              "NonPropEntityStatePayload must be 56 bytes");
static_assert(sizeof(NonPropEntityStatePayload) <= 256 - 20 - 8,
              "NonPropEntityStatePayload must fit one reliable datagram");

struct NonPropEntityDestroyPayload {
    uint8_t  entityClass;       // NonPropEntityClass
    uint8_t  peerSessionId;
    uint8_t  _pad[2];
    uint32_t identity;          // same union semantics as Spawn payload
};
static_assert(sizeof(NonPropEntityDestroyPayload) == 8,
              "NonPropEntityDestroyPayload must be 8 bytes");
```

Generic-vs-specific tradeoff resolution: **generic single packet**, because
(a) it keeps the ReliableKind enum tight (2 new values vs 6), (b) the
variant blob is fixed-size at 12B per class -- no class needs more, (c)
adding a 4th non-Aprop garbage entity later (e.g. a new debris pile)
requires only an enum entry, not a new wire kind.

### 2.4 variantState layout per class

```
TrashBitsPile (12 B used):
  [0]  Shape                  (int32 -- 4 B)
  [4]  typeA                  (uint8)
  [5]  typeB                  (uint8)
  [6]  amountA_high           (uint8 -- high byte of int32)
  [7]  amountA_low            (uint8 -- low  byte; receiver reconstructs as uint16-clamped)
  [8]  amountB_high           (uint8)
  [9]  amountB_low            (uint8)
  [10] ttMask                 (uint8 -- bit0=tt_plastic .. bit7=tt_organic)
  [11] flagsMask              (uint8 -- bit0=randomTypeA, bit1=randomTypeB,
                                bit2=snapToSurface,bit3=AlignToNormal, bit4=randomYRoll)

GarbageClump (3 B used):
  [0]  chipType               (uint8 -- enum_chipPileType)
  [1]  flagsMask              (uint8 -- bit0=canConvert, bit1=delayOnHit, bit2=skipSave1)
  [2]  pileClassIndex         (uint8 -- 0..3 mapping to {default,erie,leaves,wetConcrete};
                               receiver maps to actual TSubclassOf via class table)
  [3..11] reserved 0

ActorChipPile (3 B used):
  [0]  chipType               (uint8)
  [1]  spwnd                  (uint8 -- bit0)
  [2]  pileClassIndex         (uint8)
  [3]  clumpClassIndex        (uint8)
  [4..11] reserved 0
```

amountA/B are nominally int32 but in practice are small counts (<256
per RE doc context); 16-bit reconstruction at the receiver covers
0..65535. If a save ever exceeds 65535 trash bits in one pile the
receiver clamps -- log + continue (single-line warn, not error: the
host's local value remains correct and the BP's collect loop will
eventually decrement under 65535 even if it temporarily looks wrong on
the client).

### 2.5 Sender path on host

```cpp
// coop/non_prop_entity_sync.cpp (NEW file)

void OnTrashBitsPile_Init_POST(void* self, void*, void*) {
    if (Role != Host || !Connected) return;
    if (IsIncomingWireSpawn(self)) return;            // echo guard
    BroadcastNonPropEntityState(self, NonPropEntityClass::TrashBitsPile);
}

void OnGarbageClump_Init_POST(void* self, void*, void*) {
    // Echo guard: receiver tags self as g_incomingNonPropSpawn[ptr]=true
    // immediately before BeginDeferredActorSpawnFromClass; consume here.
    if (ConsumeIncomingNonPropSpawn(self)) return;
    if (Role != Host || !Connected) return;
    uint32_t id = MintSessionId();
    g_hostClumpToSessionId[self] = id;               // for later destroy
    BroadcastNonPropEntityState(self, NonPropEntityClass::GarbageClump, id);
}

void OnActorChipPile_Init_POST(void* self, void*, void*) {
    if (ConsumeIncomingNonPropSpawn(self)) return;
    if (Role != Host || !Connected) return;
    uint32_t id = MintSessionId();
    g_hostChipPileToSessionId[self] = id;
    BroadcastNonPropEntityState(self, NonPropEntityClass::ActorChipPile, id);
}

// Single K2_DestroyActor PRE observer; already exists for Aprop_C in
// prop_lifecycle.cpp. Either:
//  (a) add three more class-specific PRE observers (mirroring NPC pattern), OR
//  (b) extend prop_lifecycle's existing K2_DestroyActor PRE to also check
//      the host-tracked sessionId maps and emit NonPropEntityDestroy.
// Recommend (b): one observer, less reflection-table churn.
```

### 2.6 Receiver path on client

```cpp
void ApplyNonPropEntityState(const NonPropEntityStatePayload& p) {
    if (p.peerSessionId != 0) return;                 // host-only
    void* cls = ResolveLeafClassForVariant(p.entityClass, p.variantClassHash16);
    if (!cls) { /* class not yet loaded; enqueue retry */ return; }

    // Lookup-or-spawn by identity.
    void* actor = nullptr;
    switch (p.entityClass) {
      case NonPropEntityClass::TrashBitsPile:
        // Identity is CRC32(Key)low32. Find by walking GUObjectArray-of-class
        // and CRC32-matching the actor's stringified Key. Cache result.
        actor = FindTrashBitsPileByKeyCrc32(p.identity);
        break;
      case NonPropEntityClass::GarbageClump:
        actor = g_clumpBySessionId[p.identity];       // unordered_map
        break;
      case NonPropEntityClass::ActorChipPile:
        actor = g_chipPileBySessionId[p.identity];
        break;
    }
    if (!actor) {
        // First sighting -- spawn via BeginDeferred + setKey (trashBitsPile)
        // or BeginDeferred + identity caching (clump/chipPile).
        MarkIncomingNonPropSpawn();                   // bypass echo on Init POST
        FTransform tr = MakeTransform(p);
        actor = BeginDeferredActorSpawnFromClass(cls, tr);
        if (!actor) return;
        // Pre-FinishSpawning field writes for trashBitsPile via setKey path
        // (so Aactor_save_C's Init doesn't reroll Key); for clump/chipPile,
        // direct field writes (no Key contract).
        if (p.entityClass == NonPropEntityClass::TrashBitsPile) {
            // setKey would normally take an FName; we don't have the original
            // string at the receiver -- only the CRC32 hash. Solution: the
            // sender extends the payload to include the WireKey (32 B). For
            // 56-byte budget that doesn't fit; instead we send a SEPARATE
            // "KeyMap" sidecar packet that maps CRC32 -> WireKey ONCE per
            // session connect-edge snapshot replay. Inc 2 ships WITHOUT this
            // and resolves identity by hash on subsequent updates; the very
            // first spawn uses the host-replayed snapshot path which already
            // exists for Aprop_C (prop_lifecycle::Install -> snapshot loop).
            // For Inc 2 minimum: trashBitsPile spawn is via the SAME snapshot
            // replay loop (host enumerates AtrashBitsPile_C on connect-edge,
            // calls SendNonPropEntityState for each).
        }
        ApplyVariantState(actor, p);
        FinishSpawningActor(actor, tr);
        if (p.entityClass == NonPropEntityClass::GarbageClump)
            g_clumpBySessionId[p.identity] = actor;
        else if (p.entityClass == NonPropEntityClass::ActorChipPile)
            g_chipPileBySessionId[p.identity] = actor;
    } else {
        // Update path: write transform + variantState in place.
        SetActorLocation(actor, p.locX,p.locY,p.locZ);
        SetActorRotation(actor, p.rotPitch,p.rotYaw,p.rotRoll);
        ApplyVariantState(actor, p);
    }
}
```

ApplyVariantState writes the field offsets via reflection (the offsets
above are stable per the 0.9.0n CXX dump; if a future game patch shifts
them, the SDK-check infra in `harness/sdk_check.cpp` flags the drift).

---

## 3. Risks / open work

| # | Risk | Probe / mitigation |
|---|---|---|
| R1 | `Aevent_trashPiles_C` PRE-cancel might break SP behaviour on the **host** if the BndEvt is the only overlap-trigger path. | Host does NOT install the interceptor (role-gated, same shape as weather_sync). Only clients cancel. |
| R2 | `glueInside` -- does it actually `K2_AttachToActor` items to the container? Decides whether items-inside-mobile-container Inc-4 work needs the attach path or per-item PropPose stream. | `[probe] garbage=1` build: PRE-observe `glueInside`, log self + itemsInside.Num + iterate items + read `GetAttachParentActor` per item before and after. ONE hands-on test (host picks up garbage container with items, walks 5 m). |
| R3 | `AtrashBitsPile_C` save-Key vs hash collision. CRC32-low32 collision probability is ~1 in 4B; with <100 trashBitsPiles per session, P(collision) < 1e-6 -- acceptable. | None; document the bound. |
| R4 | `Atool_garbageSpawner_C::Init` -- does the SPAWNED actor (clump or chipPile) get its `Init` POST observer fire on the spawner, the local player, OR neither? If `Init` only runs on the toolgun, the new actor is missed. | Probe at install time: log the Init POST observer's `self` once per fire to confirm it's the spawned actor. Worst case: add a separate POST observer on the toolgun's Init param-extract path (read the SpawnTransform out-param). |
| R5 | `AbaseCleaner_trashBits_C::ReceiveBeginPlay` -- what if the cleaner is needed on both peers for SOME local-only cosmetic reason? Per RE doc the class is pure deletion-loop, no rendering side. | Stays the conservative client-suppress; if a hands-on test reveals a missed local-only effect, lift the suppression and add a per-target wire echo instead. |
| R6 | `NonPropEntityStatePayload` at 56 B; if a future class needs >12 B of variantState we have to either extend payload (bump v9 -> v10) or fork the kind. | Document the 12 B variant budget in the protocol header comment. Today's three classes use 3-12 B; headroom is ample. |
| R7 | Initial-snapshot replay on client connect: host must enumerate AtrashBitsPile_C / Aprop_garbageClump_C / AactorChipPile_C instances and emit NonPropEntityState for each. Without it, client connecting mid-game sees no existing trash. | Add to existing connect-edge snapshot replay loop (`prop_lifecycle::Install` -- already does this for Aprop_C). Extend with 3 more GUObjectArray scans. ~2000 garbage actors max per RE = ~2000 * 56 B = 112 KB reliable burst, fits the existing ARQ window. |

---

## 4. File layout

**Recommendation: SPLIT into `coop/non_prop_entity_sync.cpp` (+ `.h`).**

Current `garbage_sync.cpp` is 145 LOC of focused container-tick suppression
(Inc 1). Adding Inc 2 (3 spawn observers + 2 wire payload encoders +
3 receiver paths + connect-edge replay + class-table resolution + 2 host
session-id maps) and Inc 3 (4 interceptors + role gates) would push the
file to ~600 LOC.

Per the modular file-size rule (CLAUDE.md, 2026-05-25 post-harness-bloat):
800 LOC soft cap and **"one feature per file"** when adding a new
feature subsystem. Inc 2 introduces a distinct subsystem (non-Aprop
entity replication, parallels `npc_sync.cpp`'s shape). Inc 3 is a
focused suppressor table (parallels `weather_sync.cpp`'s interceptor
slate).

Concrete split:

```
coop/garbage_sync.{h,cpp}              -- Inc 1 (today, 145 LOC -- unchanged)
                                          + Inc 3 spawner suppression
                                          (adds ~120 LOC; final ~265 LOC)
coop/non_prop_entity_sync.{h,cpp}      -- NEW, Inc 2
                                          (~450 LOC: payload encode/decode,
                                           host observers, client receiver,
                                           class table, sessionId maps,
                                           connect-edge snapshot replay)
```

Both stay well under the 800 LOC soft cap. `non_prop_entity_sync.cpp`
mirrors `npc_sync.cpp`'s patterns (atomic Session*, sessionId mint, host-
tracked map, role-gated install, env-var gate during initial rollout).

`harness.cpp` install order:
```
coop::garbage_sync::Install(session);          // Inc 1 + Inc 3
coop::non_prop_entity_sync::Install(session);  // Inc 2
```

---

## 5. Implementation checklist

Order of work:

1. **Inc 3 in `garbage_sync.cpp`** (build on the existing file's install
   pattern):
   - Resolve `AundergroundGarbageSpawner_C`, `Aevent_trashPiles_C`,
     `AarirTrasher_C`, `AbaseCleaner_trashBits_C` UClasses; resolve their
     target UFunctions (table above).
   - `RegisterInterceptor` each, with a single shared callback that
     role-gates (client-only) + returns true.
   - DELIBERATELY skip `Atool_garbageSpawner_C` -- add a code comment
     citing the player-action exception.
   - Log on first cancel per class (throttled, mirroring Inc 1).

2. **Inc 2 wire protocol** in `protocol.h`:
   - Bump `kProtocolVersion = 9`.
   - Add `ReliableKind::NonPropEntityState = 9`, `NonPropEntityDestroy = 10`.
   - Add `NonPropEntityClass` enum, `NonPropEntityStatePayload`,
     `NonPropEntityDestroyPayload` structs + static_asserts.

3. **Inc 2 in `non_prop_entity_sync.cpp`**:
   - Host install: POST observers on `Init` for each of the 3 classes,
     PRE observer on `K2_DestroyActor` (or extend the existing one in
     `prop_lifecycle.cpp` with a callout to the new module's destroy
     emitter).
   - Connect-edge snapshot replay: enumerate each class via
     `R::ForEachObjectOfClass` (or GUObjectArray walk filtered by class)
     and emit NonPropEntityState for each (host-only).
   - Client receiver path: lookup-or-spawn by identity, apply state.
   - Echo-suppression set (`g_incomingNonPropSpawn` set, mirroring
     `remote_prop::MarkIncomingSpawn`).
   - `[probe] garbage=1` env-var-gated diagnostics during initial bring-up
     (mirroring `VOTVCOOP_NPC_SYNC=1`).

4. **Inc 2 session integration** (`coop/net/session.h/cpp`):
   - Add `SendNonPropEntityState(...)` and `SendNonPropEntityDestroy(...)`
     parallels to `SendEntitySpawn` / `SendEntityDestroy`.

5. **Inc 4 (DEFERRED, NOT THIS WORK):** items-inside-container mobility.
   Probe R2 from §3, then either lift `glueInside` suppression with
   synced `itemsInside` array, or stream per-item PropPose while held.
   Bracketed by Inc 2 landing first because the per-item identity
   resolution depends on Inc 2's class table.

---

## 6. Summary deliverables (for the parent agent's report)

- **Inc 3 table:** 5 spawners. 4 client-suppressed via PRE-cancel
  interceptors on `Timer` / `BndEvt__..._BeginOverlap` / `trash` /
  `ReceiveBeginPlay`. 1 (`Atool_garbageSpawner_C`) is the deliberate
  player-action exception -- NOT suppressed; its spawn output rides the
  Inc 2 non-prop entity broadcast path.
- **Inc 2 wire shape:** generic single `NonPropEntityState` (56 B) +
  `NonPropEntityDestroy` (8 B). NonPropEntityClass enum discriminates the
  3 target classes; variantState[12] is per-class. `kProtocolVersion` 8 -> 9.
- **itemsInside mutator hook:** NONE -- design pass concluded `itemsInside`
  add/remove is BP-inlined inside `ReceiveTick`, and Inc 1's PRE-cancel
  already prevents client mutation. No wire sync for itemsInside in Inc 2.
  Item-mobility-with-moving-container is split out as Inc 4 pending the R2
  probe of `glueInside`'s attach semantics.
