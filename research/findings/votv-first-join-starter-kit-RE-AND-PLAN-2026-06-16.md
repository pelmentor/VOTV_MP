# First-join coop starter kit — RE + the RULE-1 proper fix (2026-06-16)

Status at write time: **v1 scrape committed (3a3aac04); RULE-1 proper fix IN PROGRESS,
UNCOMMITTED, waiting on the user's hands-on probe test.** This doc is the resume plan.

## 1. Problem

A brand-new coop joiner (no persisted per-player inventory yet) spawns **EMPTY** in-game —
no flashlight/glasses/compass. Confirmed via the inventory selftest item-dump on a forced
first-join: client saveSlot ends `inventory=0 equipment=8(all empty) hold=2(all empty)`.
Root reason: a coop joiner LOADS the host's save (not a New Game), so the game's New-Game
begin-equipment never runs for them.

## 2. RE — what the SP starter kit is + where it lives

- The 3 SP New-Game starters (user-confirmed + verified in a played host save's live inv):
  - `prop_equipment_glasses_C`   — rides the **main inventory** (`saveSlot.inventoryData` @ 0x02E0)
  - `prop_equipment_flashlight_C` — rides the **worn-equipment** slot array (`saveSlot.equipment` @ 0x0440), slot 1
  - `prop_equipment_compass_C`    — worn-equipment, slot 2
  (selftest dump on host s_1234: `inv[0]=glasses(key=tFZ…)`, `eq[1]=flashlight(propName='flashlight')`, `eq[2]=compass`)
- `saveSlot.equipment_def` (0x04C0) / `hold_def` (0x04B0) — the OBVIOUS "default loadout" source —
  are **EMPTY** in a played host save (all 10 entries blank; `reset_player_inventory/equipment`
  copy `equipment=equipment_def` + a gamemode UI hint — saveSlot_fixed.json — so they're the
  empty "reset baseline", NOT the starter source). DO NOT read `_def` for the kit.
- The starters are added at New Game by the gamemode's **begin-equipment loop**, which lives in
  `ExecuteUbergraph_mainGamemode` (the BP event graph), fired by a CustomEvent. It iterates a
  begin-list and does `AddEquipment(spawnedItem.getData())` per item. The starter CLASSES are NOT
  literals in the gamemode bytecode (they're in `list_props.json` / the begin-list FNames).
- Callable UFunctions (the loop's primitives — these ARE callable; the loop itself is not):
  - `mainGamemode_C::AddEquipment(Fstruct_save Data, bool& return)` — adds one item to the LOCAL
    player; the game decides inventory-vs-worn placement + assigns a FRESH key.
  - `Aprop_C::getData(Fstruct_save& Data)` — serialize an item instance to its save record (0x100).
  - `Fstruct_save` is **0x100** bytes.
- The begin-equipment LOOP is **un-callable directly** (ubergraph CustomEvent offset — the same
  reason the spawn-menu InpActEvt/ExecuteUbergraph approach failed, votv-spawnmenu-RE). So the fix
  drives `AddEquipment` directly, exactly as the loop does.

## 3. v1 SCRAPE (committed 3a3aac04) — works, but a RULE-1 crutch

`player_inventory_sync::BuildFirstJoinStarterKit` reads the HOST's own live `ReadAll`, filters to
the 3 starter classes (1/class, preserving the 8-slot equip shape), and the host seeds that on the
first-join apply blob. Verified working (client gets glasses+flashlight+compass). **The user
correctly flagged this as a crutch:** it depends on the host HAVING the items, copies the host's
specific item *instances* (so every joiner's glasses shares the host's key tFZ… → a world-prop key
collision IF multiple peers drop the same starter), and hand-reconstructs what the game already does.

## 4. RULE-1 PROPER FIX (in progress) — drive the game's own equip path

For a first-join joiner, post-spawn: for each starter class, **SPAWN the prop → `getData()` →
`gamemode.AddEquipment(data)` → destroy the temp prop** — the exact calls the begin-equipment loop
makes. Canonical (the game equips + assigns a fresh key → retires the key-dup caveat),
host-INDEPENDENT (spawns from the class, not the host's bag), Principle-6 (routes the joiner through
SP's own equip path).

### Built so far (UNCOMMITTED, DLL @ 11:36 2026-06-16, deployed x4)
- NEW `ue_wrap/begin_equipment.{h,cpp}` — `bool GiveFromClass(const std::wstring& className)`:
  - resolve `FindClass(className)`, `FindObjectByClass("mainGamemode_C")`, `FindFunction` for
    `AddEquipment` (on the gamemode class) + `getData` (on the prop class) + `K2_DestroyActor`.
  - SEH-only inner `GiveFromClassRaw(...)` (NO C++ destructors in the `__try` frame — the
    game_thread.cpp doctrine): `SpawnActor(cls,{0,0,0})` → `CallFunction(prop,getData,data[0x100])`
    → copy data into a `frame[0x108]` (Data@0, ret bool@0x100) → `CallFunction(gm,AddEquipment,frame)`
    → `CallFunction(prop,K2_DestroyActor)`. Returns 1/0/-1(faulted). Outer logs + returns bool.
- `CMakeLists.txt`: added `src/ue_wrap/begin_equipment.cpp`.
- `player_inventory_sync.cpp` selftest: gated PROBE (ini `starterkit_test=1`) — calls
  `GiveFromClass` for the 3 starters + re-reads + logs `starterkit[probe]: after AddEquipment x3 …`.
  (+ `#include "ue_wrap/begin_equipment.h"`.)
- Ground prepared for the user (they're ON the PC): host ini `inventory_selftest=1` +
  `starterkit_test=1`, host log cleared.

### PROBE verdict criteria (the user runs the host solo, ~10 s post-world)
- PASS: `begin_equipment: equip 'prop_equipment_flashlight_C' via getData->AddEquipment -> true`
  (×3) + `starterkit[probe]: after … (was …)` counts INCREASE + NO `FAULTED (SEH)` + items
  appear in-game. → wire first-join.
- FAIL/FAULT: getData-on-a-freshly-spawned-prop may need the prop fully BeginPlay'd, or AddEquipment
  may need a possessed local player / different context. Fallbacks: spawn deferred + FinishSpawn
  before getData; or call AddEquipment with a hand-built minimal Fstruct_save (className only) and
  test materialization.

## 5. RESUME PLAN (after a PROBE PASS)

1. Wire the first-join edge:
   - HOST: on first-join (no persisted file), send a FIRST-JOIN MARKER instead of the scraped kit
     (e.g. a 1-byte flag prefixed to the apply blob: 0=normal inventory, 1=equip-defaults). Keep the
     empty/normal apply for the "file exists but empty" case (a player who dropped everything).
   - CLIENT: on the marker → apply EMPTY pre-materialize (joiner spawns empty) + latch a
     `g_pendingFirstJoin`. POST-world-up (the client `Tick`, gameplay-gated, after a short settle),
     once, call `ue_wrap::begin_equipment::GiveFromClass` for the 3 starters. The equipped kit then
     streams to the host (ClientStreamTick) → persisted → SUBSEQUENT joins reapply the persisted kit
     via the normal path (no re-equip).
2. DELETE the v1 scrape: `StarterIndex` + `BuildFirstJoinStarterKit` + its use in
   `SendInventoryToSlot` (revert that branch to send the first-join marker).
3. DELETE the selftest probe + revert host ini `starterkit_test` / `inventory_selftest` to 0.
4. SEH-guard the post-spawn equip on the client too (reuse `GiveFromClass`'s guard).
5. COMMIT + push + deploy x4. Verify (with the user, they're on the PC): a fresh joiner spawns with
   flashlight+glasses+compass, fresh keys, host inventory irrelevant.

## 6. Don't-repeat notes
- equipment_def/hold_def are EMPTY — never the starter source.
- The begin-equipment loop (ExecuteUbergraph_mainGamemode) is un-callable; drive AddEquipment.
- AddEquipment frame: Data (0x100) @ 0, return bool @ 0x100. Fstruct_save = 0x100.
- The committed scrape is the working fallback until the proper fix lands + verifies.
