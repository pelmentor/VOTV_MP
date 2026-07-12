# VOTV garbage / trash interaction RE — 2026-05-27

Triggered by a CLIENT crash (AV @ 0x0F) when picking up "a pile of garbage".
Goal: map the family so we can sync it properly (RULE 1), not carve it out.

Sources: CXX header dumps. IDA confirms ZERO native impls — every garbage
class is pure BP. Where BP-body behaviour is required, runtime probes are
proposed.

## 1. Class hierarchy

```
AActor
 +- Aprop_C  (0x363)                              physics-prop baseline
 |   +- Aprop_garbageBag_C       (0x371)          + chipType byte
 |   +- Aprop_garbageGun_C       (0x374)          + Type
 |   +- Aprop_openContainer_C    (0x3A9)          + TArray<AActor*> itemsInside @0x378
 |   |   +- Aprop_garbageContainer_C (0x3B8)      + NavModifier; <- "pile of garbage" candidate
 |   +- Aprop_container_C        (0x42A)          + propInventory_C
 |       +- Aprop_garbageBin_C   (0x484)          (5 color variants -- same UClass)
 |       +- Aprop_container_garbagebin2_C
 +- AactorChipPile_C  (0x274)           NOT Aprop_C; + chipType, ignore[], pile, clump
 |   +- AactorChipPile_erie_C / _leaves_C / _wetConcrete_C
 +- Aprop_garbageClump_C  (0x2F4)       NOT Aprop_C; + holdPlayer, canConvert, pile
 |   +- Aprop_garbageClump_erie_C / _leaves_C / _wetConcrete_C
 +- Aactor_save_C  (0x248)              save-key baseline
 |   +- AtrashBitsPile_C  (0x274)       world-spawned heap; Shape, typeA/B, amountA/B, tt_*[8]
 |   +- Aevent_trashPiles_C  (0x258)    overlap-trigger volume
 |   +- AarirTrasher_C  (0x264)         AI trash-spawner
 +- AundergroundGarbageSpawner_C (0x360)   periodic spawner (loot TMap)
 +- AbaseCleaner_C / +AbaseCleaner_trashBits_C  periodic remover

AtoolObject_C
 +- Atool_garbageSpawner_C (0x539)          toolgun attachment
```

## 2. Per-class interaction model

| Class | Pickup | Aprop_C? | Wire sync today | State |
|---|---|---|---|---|
| `Aprop_garbageBag_C` | PHC light-grab | YES | spawn+pose+release OK | chipType |
| `Aprop_garbageContainer_C` | PHC grab on shell | YES | shell OK; **itemsInside NOT synced** | itemsInside, isHolding |
| `Aprop_garbageBin_C` / `garbagebin2` | RMB-open container | YES | shell OK; inventory PRIVATE per [[project-coop-inventory-private]] | Locked, Slot, Type |
| `Aprop_garbageGun_C` | PHC grab + LMB/RMB fire | YES | shell OK; fire event NOT synced | Type |
| `Aprop_garbageClump_C` | PHC grab on mesh; BP sets holdPlayer | **NO** | **NONE -- IsDescendantOfProp=false** | holdPlayer, canConvert, pile |
| `AactorChipPile_C` | PHC grab + `turnToPile`/`toClump` morph | **NO** | **NONE** | chipType, ignore[], spwnd |
| `AtrashBitsPile_C` | NOT PHC -- custom `playerTryToCollect` -> `getRandomTrashType` -> `trashToProp` -> spawn Aprop_; pile decrements + self-destroys at 0 | **NO** | **NONE** | Shape, typeA/B, amountA/B, tt_*[8] |

trashBits "pickup" is a collect-and-morph: no grab on the pile itself, the
pile becomes individual Aprop_C items spawned at the player.

## 3. Spawners

- `AundergroundGarbageSpawner_C::Timer` -- weighted `loot: TMap<FName,float>` -> `prepareItems(Loc)` at random landscape pts. Per-peer-divergent.
- `Aevent_trashPiles_C` -- overlap-trigger volume spawns cluster of trashBitsPiles.
- `AarirTrasher_C::trash()` -- Count-bounded periodic.
- `Atool_garbageSpawner_C::Init(toolgun)` -- toolgun aim-fire.

**None route through `Aprop_C::Init`**, so our existing Init POST observer never sees them.

## 4. AtrashBitsPile_C — "pile of garbage"

Pickable, NOT via PHC. Collect path: `playerTryToCollect` -> `getRandomTrashType` (over the 8 enum_trashType + tt_*[8] allowlist) -> `trashToProp(Type) -> FName prop` -> world-spawns one Aprop_C item -> decrement amountA/B -> destroy self at zero. Related BP-only UFunctions: `playerTryToGrab`, `playerTryToHold`, `playerGrabbed`, `broken`, `kicked`, `thrown`, `vacuumed`, `broomed`.

## 5. Crash hypothesis (AV @ 0x0F)

Two candidates:

**(a) HIGH: `Aprop_garbageContainer_C` with stale `itemsInside`.** TArray<AActor*> @0x378 walked every tick by `Aprop_openContainer_C::ReceiveTick` -> `checkPickup`/`doStuff`/`glueInside`. After save-load + per-peer-divergent undergroundGarbageSpawner, client's array holds a freed/GC'd AActor*. AActor has uint8 flag bytes in 0x00..0x1F range -- 0x0F matches one on a freed pointer. GAME BP code crashing on state WE LEFT STALE (we don't sync container contents).

**(b) MEDIUM: `AtrashBitsPile_C::trashToProp` resolving an unloaded class.** FName -> UClass returns null, SpawnActor derefs UClass at small offset, AV. Less likely (the collect path runs LATER than "pickup" symptom suggests).

Both root-cause to: we don't sync container contents or trashBits state, client diverges, BP body crashes on divergent state.

Confirm via runtime PRE-observers on `checkPickup` + `playerTryToCollect`, logging self+ClassNameOf+itemsInside.{Num,Data}+amountA/B.

## 6. Proposed sync design

### Inc 1 -- stop crashing + cross-peer visible (no new packets)

1. Defensive log: confirm `IsDescendantOfProp` excludes garbageClump / trashBitsPile / chipPile (it should — they're AActor / Aactor_save).
2. **Client-side suppress `Aprop_openContainer_C::ReceiveTick` / `checkPickup` early-return** for garbageContainer — eliminates the itemsInside walk -> eliminates the stale-ptr AV. Items inside are eye-candy; loss is acceptable for Inc-1.
3. **Host-authoritative suppress for non-Aprop_C trash entities** (mirror the mushroom7 pattern). On client, K2_DestroyActor `AtrashBitsPile_C`, `AactorChipPile_C`, `Aprop_garbageClump_C` at Init / BeginPlay. New helper `IsWireSuppressedNonPropClass`. Client doesn't see them in Inc-1 -- acceptable.

### Inc 2 -- full state sync

4. New `ReliableKind::NonPropEntityState` (~48B): `{WireKey, classHash, loc, rot, varBlob:u8[16]}`. varBlob = class-specific bytes (trashBits: Shape+typeA/B+amountA/B+tt_mask = 8B; clump: chipType+pile classHash = 5B). Host broadcasts on Init + field change. Client spawns via BeginDeferred->setKey->Finish (Aactor_save_C has its own `setKey` UFunction per dump -- use that for trashBits/save-actors; raw AActor path for chipPile/clump).
5. **`itemsInside` mirror** for Aprop_openContainer_C: new `ReliableKind::OpenContainerContents` `{containerKey, items:[{classHash,itemKey}]}`. Host re-broadcasts on add/remove. Add/remove UFunction TBD via probe (likely `putObjectIn_overlap` and Aprop_openContainer_C::propAwoken / doStuff).

### Inc 3 -- spawner host-authoritative gating

6. Extend the Phase 5N1 deferred-spawn-from-class observer with the garbage spawner allowlist: `AundergroundGarbageSpawner_C`, `Aevent_trashPiles_C`, `AarirTrasher_C`, `Atool_garbageSpawner_C`, `AbaseCleaner_trashBits_C`. Client BP body returns early; host's wire packets feed the client. Mirrors the existing mushroom-spawner pattern.

## 7. Hooks / UFunctions to register

Inc-1 (client suppression / diagnostics):
- `Aprop_openContainer_C::checkPickup` PRE -- client early-return.
- `Aprop_openContainer_C::ReceiveTick` PRE -- client early-return (belt-and-braces).
- `AtrashBitsPile_C::Init` POST + `Aprop_garbageClump_C::Init` POST + `AactorChipPile_C::Init` POST -- client K2_DestroyActor.
- `AtrashBitsPile_C::playerTryToCollect` PRE -- diagnostic for crash-bisect.

Inc-2 (host broadcast):
- Same Init POSTs above, host path -- emit `NonPropEntityState`.
- `AtrashBitsPile_C::trashToProp` POST -- read return-param FName (the spawned class) and broadcast a child Aprop_C spawn.
- `AactorChipPile_C::turnToPile` / `toClump` POST -- morph sync.
- Aprop_openContainer_C add/remove UFunction is BP-inlined; **probe required**: hook every `Aprop_openContainer_C::*` POST in a `[probe] garbage=1` build, log itemsInside.Num before/after to identify the canonical mutator. Once known, hook + broadcast `OpenContainerContents`.

Inc-3 (host-authoritative spawner gating, client PRE early-return):
- `AundergroundGarbageSpawner_C::Timer`, `Aevent_trashPiles_C::BndEvt__..._BeginOverlap`, `AarirTrasher_C::trash`, `Atool_garbageSpawner_C::Init`, `AbaseCleaner_trashBits_C` (inherited `ReceiveBeginPlay`).

**Diagnostic probe to confirm the crash culprit:** `[probe] garbage=1`-gated PRE on `Aprop_openContainer_C::checkPickup` + `AtrashBitsPile_C::playerTryToCollect` logging `self`, `ClassNameOf(self)`, `itemsInside.{Num,Data}`, `amountA/B`. Last log line before the AV identifies the actor + faulty field.
