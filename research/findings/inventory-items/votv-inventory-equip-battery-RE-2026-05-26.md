# RE: VOTV Inventory / Equip / Battery system
**Date**: 2026-05-26
**Target**: Alpha 0.9.0-n
**Purpose**: Phase 5F autotest needs to programmatically set up a working flashlight (give, equip, battery) so the BP toggle path can run during the autonomous LAN test. The reflection-only `updateFlashlight` / `Flashlight Update` calls dispatch but don't toggle — the BP gates on equip-state + battery state we hadn't set.

---

## TL;DR

- **Give**: `AmainPlayer_C::addPropToPlayer(FName prop)` — single-arg, mirrors the cheat menu's spawn path. Pass `FName("prop_equipment_flashlight_C")`.
- **Equip**: `AmainPlayer_C::addEquip(Fstruct_save Data, bool& return, FString& rebug)` — moves the item from bag to `saveSlot.hold` array. Requires building an `Fstruct_save` with `class_3` field pointing at the flashlight `UClass*` + matching `key_64` FName.
- **Battery**: directly write the two `saveSlot_C` fields — `saveSlot.battery @0x0100` (float, charge level) and `saveSlot.flashlightBattery @0x0890` (TSubclassOf<Aprop_batts_C>, class of inserted battery). Simpler than calling `mainPlayer::insertBattery(player, battery)` which requires spawning a battery actor first.
- **Battery class**: `Aprop_batts_C` (standard) — only that and `Aprop_batts_acc_C` (accumulator) exist in the dump. No alkaline/AA variants.
- **Gate**: `mainPlayer.hasFlashlight @0x0CC2 == true` (set by `addEquip`). `Flashlight Update()` ALSO checks `saveSlot.battery > 0` and immediately turns the light off if zero.
- **Inner vs outer UFunction**: `updateFlashlight()` is the toggle that flips `flashlight` bool + `light_R` visibility + fires `flashlightStateChanged`. `Flashlight Update()` is the timer/tick state machine that drains battery and decides when to call `updateFlashlight`.

---

## 1. Give Item Path

Three entry points, ordered from highest-level (preferred) to lowest:

### Option A — `AmainPlayer_C::addPropToPlayer(FName prop)` (PREFERRED)

```cpp
// mainPlayer.hpp:444
void addPropToPlayer(FName prop);
```

Takes an FName class name. Spawns the actor + runs Init + adds to player's inventory container in one shot. Cheat-menu-equivalent path.

Call sequence:
```
mainPlayer.addPropToPlayer(FName("prop_equipment_flashlight_C"))
```

### Option B — `AmainGamemode_C::AddEquipment(Fstruct_save Data, bool& return)`

```cpp
// mainGamemode.hpp:432
void AddEquipment(Fstruct_save Data, bool& return);
```

Save-restore path. Requires a fully-formed `Fstruct_save` (size 0xF8 per `struct_save.hpp`). More complex from C++ reflection.

### Option C — Low-level `propInventory_C::addObject`

```cpp
// propInventory.hpp:33
void addObject(AActor* Actor, int32 insertIndex1, bool& return, FString& err);
```

On the `propInventory` component of `playerContainer` (`Aprop_inventoryContainer_player_C`). Accessible via `AmainGamemode_C::playerContainer @0x0780`. Lowest level, most fragile.

**Recommendation**: Option A. Cheat-menu-tested in VOTV; one UFunction call.

---

## 2. Equip Path

"Equipped" means the item is in `saveSlot.hold` array (active hand), distinct from `saveSlot.equipment` (worn gear like glasses/hazmat).

`saveSlot_C` arrays:
- `saveSlot.equipment @0x0440` — `TArray<Fstruct_equipment>` (worn)
- `saveSlot.hold @0x0450` — `TArray<Fstruct_equipment>` (held)

`Fstruct_equipment` (from `struct_equipment.hpp`):
- `Fstruct_propDynamic prop` — has FName name + FName key
- `Fstruct_save data` — 0x100 bytes; class_3 + key_64 are the load-bearing fields
- `FName tag` — identifies the slot (e.g. "flashlight" for the flashlight slot)

Player UFunction:
```cpp
// mainPlayer.hpp:476
void addEquip(Fstruct_save Data, bool& return, FString& rebug);
```

The BP behavior: parse Data.class_3, append a new Fstruct_equipment to `saveSlot.hold`, set `hasFlashlight @0x0CC2 = true`, fire `updateEquipment()` to refresh HUD.

**Note**: writing `hasFlashlight` directly (as some earlier autotest hacks attempted) bypasses the slot registration, so the BP gate appears to pass but the equipped actor is never spawned/attached — toggling does nothing visible.

---

## 3. Battery Path

Battery state for the STANDARD flashlight (`flashlight_C` / `flashlight_b_C`) is NOT on the flashlight actor (those classes are empty, size 0x378). It lives in `saveSlot_C`:

```cpp
// saveSlot.hpp
float battery @0x0100;                              // charge level (0.0-1.0)
TSubclassOf<Aprop_batts_C> flashlightBattery @0x0890;  // CLASS of inserted battery (not instance)
```

The crank lantern `Aprop_equipment_flashlight_c_C` is DIFFERENT — it has its own `float energy @0x03AC` on the actor and a hand-crank UI. Out of scope for Inc4 (deferred to Inc6 per Phase 5F RE doc).

Two insertion approaches:

### Direct write (PREFERRED for autotest)

```cpp
// Find saveSlot via mainGamemode
saveSlot.battery @0x0100 = 1.0f
saveSlot.flashlightBattery @0x0890 = FindClass(L"prop_batts_C")  // CDO pointer is fine
```

Bypasses the full battery-spawn + consume flow. Sufficient for the BP to see "battery installed at full charge".

### Spawn + insertBattery UFunction (proper but complex)

```cpp
// mainPlayer.hpp:704
void insertBattery(AmainPlayer_C* Player, Aprop_batts_C* battery);
```

Requires spawning an `Aprop_batts_C` instance, setting its `energy @0x0370 = 1.0`, calling this UFunction. Battery actor gets consumed.

**Note on caller**: this is on `AmainPlayer_C` itself as the Aprop_C-interface receiver method. The standard in-game flow is "player holds battery, uses it on equipped flashlight" — calling the UFunction directly mimics that.

---

## 4. Battery Item Class Name

Two concrete classes in the dump:
- `Aprop_batts_C` (`prop_batts.hpp`, size 0x374, `float energy @0x0370`) — the standard battery
- `Aprop_batts_acc_C` (`prop_batts_acc.hpp`, extends base, no extra fields) — the accumulator/rechargeable

There is NO `prop_batts_alkaline_C` / `prop_batts_AA_C` / etc. — just these two.

For autotest: use `prop_batts_C` (standard battery class).

---

## 5. Gate Conditions for `updateFlashlight`

`mainPlayer.hpp` flashlight state fields:

| Field | Offset | Type | Role |
|---|---|---|---|
| `flashlight` | 0x0838 | bool | current on/off state |
| `flashlightMode` | 0x0C79 | uint8 | mode selector |
| `hasFlashlight` | 0x0CC2 | bool | **equip gate** (must be true) |
| `crankFlashlight` | 0x0CC4 | bool | true if `_c` crank variant equipped |
| `batteryUsage` | 0x0D58 | float | per-tick drain rate |
| `input_flashlight` | 0x0E39 | bool | F-key press state |
| `timeFlashlightTimerHandle` | 0x0E30 | FTimerHandle | drain timer handle |

Two distinct UFunctions:

- **`updateFlashlight()`** (mainPlayer.hpp:435) — the LOW-LEVEL toggle: flips `flashlight` bool, sets `light_R` visibility/intensity, fires `flashlightStateChanged` delegate. Gate: `hasFlashlight == true`.
- **`Flashlight Update()`** (mainPlayer.hpp:488 — literal space in name) — the HIGH-LEVEL state machine: drains battery per tick, decides press-vs-hold via timer, calls `updateFlashlight` to actually toggle. Gate: `hasFlashlight && saveSlot.battery > 0`.

For the autotest to see a SUSTAINED toggle:
- `hasFlashlight` must be true (equip step)
- `saveSlot.battery > 0` (battery step) — otherwise `Flashlight Update` immediately turns it back off
- Calling `updateFlashlight()` directly bypasses the timer check and toggles the light unconditionally (gate-passing), but `Flashlight Update` on the next tick will kill it if no battery.

---

## 6. Recommended Autotest Sequence

```
1. Give flashlight to inventory:
   AmainPlayer_C::addPropToPlayer(FName("prop_equipment_flashlight_C"))

2. (Verify) Read mainPlayer.hasFlashlight @0x0CC2.
   If true: save already had one equipped, skip step 3.
   If false: continue.

3. Equip flashlight (move to hold slot):
   Build Fstruct_save with class_3 = UClass*(prop_equipment_flashlight_C)
   AmainPlayer_C::addEquip(Fstruct_save, &outBool, &outFString)
   -- The simpler alternative: write saveSlot.hold TArray entry directly
   -- and write hasFlashlight = true. addEquip is preferred (full BP equip
   -- logic runs) but riskier (needs correct Fstruct_save layout).

4. Set battery charge:
   Get saveSlot pointer via mainGamemode.saveSlot @0x04B0
   Write saveSlot.battery @0x0100 = 1.0f
   Write saveSlot.flashlightBattery @0x0890 = FindClass("prop_batts_C")

5. Verify pre-toggle state:
   read mainPlayer.hasFlashlight (true)
   read saveSlot.battery (> 0)
   read mainPlayer.crankFlashlight (false; if true it's the _c variant)

6. Toggle:
   AmainPlayer_C::updateFlashlight()
   -- light_R USpotLightComponent visibility/intensity flips
   -- flashlight bool flips
   -- flashlightStateChanged delegate fires
   -- our POST observer fires, latches the real "on" intensity, sends
      wire packet to peer
```

---

## 7. Open Flags

**F-INV-1**: `addEquip`'s `Fstruct_save` layout. The struct is 0x100 bytes; only `class_3` + `key_64` are believed load-bearing. If `addEquip` re-spawns the actor (vs. matching the bag instance by key), we'd get a duplicate. Needs UE4SS Lua probe OR IDA trace.

**F-INV-2**: `addPropToPlayer` auto-equip behavior. Whether it puts the item in the BAG only (requiring separate `addEquip`) or auto-equips when adding equipment items. A Lua probe calling `addPropToPlayer("prop_equipment_flashlight_C")` then inspecting `saveSlot.hold` vs `saveSlot.equipment` would resolve in one test.

**F-INV-3**: `mainPlayer::insertBattery(player, battery)` semantics. Whether the player IS the receiver (player gets battery applied to its equipped flashlight) or the flashlight item actor should be the receiver. Direct field writes to saveSlot.battery + saveSlot.flashlightBattery sidesteps this entirely.

**F-INV-4**: `saveSlot` pointer nullability. `mainGamemode.saveSlot @0x04B0` may be null before save fully loads. The autotest's 15s post-load wait should cover this but a null-check is mandatory.

---

## 8. Implementation notes

Files we'll touch:
- `src/votv-coop/include/ue_wrap/sdk_profile.h` — add UFunction names + offsets for inventory/save fields.
- `src/votv-coop/src/dev/` — new module `dev_inventory.cpp/.h` with `GiveFlashlight()` / `SetBatteryFull()` / `EquipFlashlight()` helpers. Dev-only; not for shipping path.
- `src/votv-coop/src/harness/autotest.cpp` — call the helpers before the toggle loop.

Out of scope:
- The actual `addEquip` Fstruct_save build is complex; we'll try `addPropToPlayer` first + verify if it auto-equips. If not, the direct-write fallback (write `hasFlashlight = true` + manually append to `saveSlot.hold`) is cheap.

---

## Cross-references

- `research/findings/votv-flashlight-RE-2026-05-25.md` — Phase 5F flashlight RE (parent of this work)
- `[[project-inventory-item-activation-sync]]` — Phase 5F scope memo
- `[[project-coop-inventory-private]]` — parent decision: inventory PRIVATE, world-effects sync
