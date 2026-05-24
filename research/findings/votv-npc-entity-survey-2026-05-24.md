# VOTV NPC + entity surface — for coop sync design

Survey date: 2026-05-24
Source: `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/`
(UE4SS-generated C++ headers extracted from cooked blueprints, ~2645 files).
Companion: `research/findings/mta-npc-entity-sync-2026-05-24.md` (MTA conceptual precedent).

## TL;DR

- VOTV has **two NPC categories**: `ACharacter` subclasses with full
  AnimBlueprint + AAIController + nav-mesh + sense radius (kerfurOmega,
  zombies, ariral, deer, fossilhound, insomniac, krampus, funguy,
  antibreather, orborb, goreSlither, wisp), and `AActor` subclasses
  driven by timelines / spline / spring physics (UFO family, arirShip,
  HoelUfo, ufoDropper). Wisp uses ACharacter as a movement chassis but
  is logically an effect.
- **kerfurOmega is the heaviest** NPC class (size 0x9F0, 333 lines), is
  the player's pet/companion, can ride the ATV, carry props, and
  execute "task" state (delivering dishes to broken servers). It has
  ~20 cosmetic skin variants (`kerfurOmega_alien`, `_erie`, `_keith`,
  …) — all empty subclasses sharing the same fields.
- **Universal NPC interaction protocol**: every "thing the player can
  affect" implements the same predicate API — `canBePickedUp`,
  `canBeCollected`, `playerTryToGrab/Hold/Collect`, `lookAt`,
  `actionOptionIndex`, `addDamage`, `ImpactDamage`, `playerHandUse_LMB/
  _RMB`, `exploded`, `bitten`, `eaten`, `setKey`, plus a `setIgnoreSave`
  / `getData` / `loadData` save trio. This is also the surface where
  cross-actor dispatch happens (player hits NPC, NPC hits door, prop
  squishes player, all use the same set of UFunctions).
- **No engine replication exists.** All these classes are pure SP
  blueprints; none carry `Replicated` UPROPERTY flags or RPC stubs. Our
  custom UDP layer has to replicate them by hand, exactly as MTA does
  for GTA:SA.
- **Spawn surface**: most NPCs come from `Aticker_*Spawner_C` (ticking
  spawners polling for population caps) or `propSpawner_editor_C`
  (placed in world). UFO/dropper events come from
  `AariralRepEventHandler_C` (calculates "rep" then `launchEvent`).
  Special enemies spawn from `mainGamemode::trySpawnInsomniac` +
  `spawnPropThroughGamemode`. **Spawn ownership for sync = host-only**.
- **Volume hint**: world entity counts implied by gamemode arrays:
  `allKerfurs`, `allKerfuros`, `allGrayboars`, `allManns`, `allWManns`,
  `allMannsSpawns`, `Generators`, `printedMeshes`, `Effects`,
  `allBuriedItemAreas`, `allCeilingLights`, `allLampPosts`,
  `killerWisps`, `validityStack` — all `TArray` on gamemode = world
  population can be in the tens-to-hundreds per category. AOI culling
  required.

---

## 1. NPCs

All "true" NPCs in VOTV inherit `ACharacter` (UE's wheeled-capsule
+ skeletal + AAIController-friendly base). They use UE
`AAIController` + nav-mesh (`UNavigationInvokerComponent`) and
`UPawnSensingComponent` (zombies, funguy, fossilhound) for AI sense.
The host runs the AI; the client only renders.

### 1.1 kerfurOmega — pet / companion / utility cat

- **Class**: `AkerfurOmega_C : ACharacter` (0x9F0 bytes, 333 lines)
- **File**: `kerfurOmega.hpp:4`
- **Variants** (empty inheritance shells, skin-only): `_0`, `_1`, `_2`,
  `_alien`, `_antibreather`, `_argpl`, `_asmodena`, `_bonerman1`,
  `_col`, `_col_gamer`, `_erie`, `_erieV4`, `_furfur`, `_igetis`,
  `_keith`, `_keljoy`, `_mannequin`, `_monique`, `_mynet`, `_vargman`
  (e.g. `kerfurOmega_alien.hpp:4` — `class AkerfurOmega_alien_C : public AkerfurOmega_C { }`)
- **State fields** (kerfurOmega.hpp):
  - `State` @ 0x05C8 (`enum_kerfurCommand::Type`, 7 values — kerfurOmega.hpp:40)
  - `moveTo` @ 0x05D0 (`UObject*` — current target pawn/prop)
  - `Task` @ 0x05F8 (`Adish_C*` — server-feeding task)
  - `TargetActor` @ 0x0648 (`AActor*`)
  - `targetLoc` @ 0x0638 (`FVector`)
  - `holdObject` @ 0x0680 (`AActor*` — same shape as mainPlayer hold)
  - `Animation` @ 0x0678 (bool, grab-anim active)
  - `lean` @ 0x0668 (float, 1-axis tilt while moving)
  - `meshVel` @ 0x065C (`FVector`, mesh velocity for anim)
  - `sentient` @ 0x07D8 (bool — "alive" vs static prop form)
  - `remoteControl` @ 0x07A8 + `RC_vector` @ 0x07AC (RC-flying mode)
  - `Key` @ 0x09B8 (FName, save key — name-token used everywhere for
    cross-peer identity. **This is the entity ID for sync.**)
  - `hasFloppy` @ 0x0810, `floppyType` @ 0x07A4, `floppyData` @ 0x0790
    (TArray<FString>) — kerfurOmega carries floppy disks; mutates over
    time as it gathers/writes data
  - `face` @ 0x07C8 (`AkerfusFace_C*` — child actor for face mat)
  - `Camera` @ 0x0628 (`Aprop_camera_good_C*` — kerfur can hold a cam)
  - `transformer` @ 0x0630 (`Agenerator_C*` — currently-targeted
    transformer for repair task)
  - `occupyCar` @ 0x0840 (`AATV_C*` — kerfur can sit in ATV)
  - `gibs` @ 0x0818 (`TArray<FName>` — broken-off pieces)
  - `drip`, `dripArray`, `dripBones`, `dripData` — accumulating
    cosmetic damage on bones (kerfurOmega.hpp:70-85)
- **Mutating events**:
  - `death()` (kerfurOmega.hpp:147), `startKill()`, `attemptMurerfur()`
  - `move(bool moveServ)`, `moveToServ()`, `findServer()`,
    `findTransformer()`, `findTask()` — AI dispatch (line 301-303,
    165-167)
  - `dropObject()`, `holdObject_kerf(player, actor)`,
    `loadHoldItem()` — hold-state mutations (l. 151, 312, 152)
  - `sitOnCar(ATV)`, `getOffCar(kill)`, `tryToOccupyCar(ATV)` (l. 154-157)
  - `equipItem(player)` / `unequipItem(string)` (l. 149-150)
  - `makeSentient()`, `makeFace()`, `setFace()` (l. 148, 159-160)
  - `addDamage(actor, dmg, hit, impact, skip)` (l. 316),
    `reachedByExplosion`, `reachedByLightning`, `fireDamage`,
    `bitten`, `slice`, `crowbarOpen`
  - `RC()`, `dropKerfurProp()`, `updateDrip()`,
    `intComs_settingsApplied(settings)` (l. 163, 161, 158, 328)
- **AnimBP**: `UAnimBlueprint_kerfurOmega_regular_C` (and `_skerfuro`,
  `_vendingFigura` variants)
- **Instances**: world tracks them via `mainGamemode.allKerfuros`
  `TArray<AkerfurOmega_C*>` @ mainGamemode.hpp:261 — many.

### 1.2 npc_zombie + its variants — the chase-melee enemy

- **Class**: `Anpc_zombie_C : ACharacter` (0x6C1 bytes, npc_zombie.hpp:4)
- **Variants**: `_alien`, `_alienSummoner`, `_bucketHead`, `_conehead`,
  `_corpse`, `_deer`, `_erie`, `_jacko`, `_pumpkinhead`, `_skele`,
  `_skeleBloody`, `_skeleVarg`, `_skerfuro`, `_wheeler` (each its own
  hpp; mostly shell-inheriting Anpc_zombie_C or near-derivatives)
- **State fields** (npc_zombie.hpp:21-66):
  - `health` @ 0x0564 (float)
  - `Damage` @ 0x05BC (float, damage dealt per hit)
  - `attackSpeed` @ 0x05E4 (float)
  - `chaseActor` @ 0x0570 / `previousChase` @ 0x0578 (`AActor*`)
  - `focusedOn` @ 0x0588 (`Anpc_zombie_C*` — pack focus)
  - `stunned` @ 0x0560, `chasing` @ 0x0561, `attacking` @ 0x0568,
    `risen` @ 0x05B8, `isDead` @ 0x06C0, `isExploded` @ 0x05AC,
    `boney` @ 0x0690, `randomZomb` @ 0x05C8, `spawnCorpse` @ 0x05C0,
    `canCall` @ 0x05C1, `sniffOutThePlayer` @ 0x0591, `isChaseInSight` @ 0x0590
  - `headArmor` @ 0x05E0 (float)
  - `speed_attacking/_stunned/_chasing/_searching/_walking` @ 0x0594-05A8
  - `stuckTimes` @ 0x0694 (int32), `lastloc` @ 0x0698 (FVector)
  - `corpse` @ 0x06A8 (`TSubclassOf<Aragdoll_zombie_C>` — spawned on death)
  - `deathgibs` @ 0x06B0 (`TArray<AActor*>`)
  - `Anim` @ 0x05B0 (`UkerfurOmegaV1_Skeleton_AnimBlueprint_Child_C*` —
    interesting: shares skeleton with kerfur)
- **Mutating events**:
  - `died()` (l. 98), `call(bool skipChecks)`, `called()` (l. 97-99) —
    spawn-call sound that recruits other zombies
  - `Sense(saw)`, `senseTimer()`, `newPawnSense()` (l. 101, 184-185)
  - `attackInRadius()`, `doWalk()`, `SetSpeed()` (l. 103-104, 182)
  - `hitBoney(impact)`, `hitHeadArmor(impact)`, `hitFlesh(impact)`,
    `headArmorDestroyed()` (l. 90-94)
  - `focus(sender)` — pack-link (l. 187)
  - `exploded(damage, loc, explosion)`, `reachedByExplosion`,
    `reachedByLightning`, `eaten`, `microwave`, `fireDamage` (l. 188,
    156, 177, 128, 139, 137)
  - `getGoal(loc, chaseActor)`, `OnMoveFinished`, `OnRequestFailed` —
    AAIController pathfinding callbacks (l. 102, 111-112)
- **Instances**: many, spawned in waves; not tracked in single gamemode array.

### 1.3 ariral family — the "ufo aliens"

Multiple species; lives under treehouse, hostile or neutral.

- `Anpc_arirFollower_C : ACharacter` (0x630, npc_arirFollower.hpp:4)
  — base class for `_pigBeater`, `_shooter`, etc. (npc_ariral_pigBeater.hpp:4
  inherits from arirFollower, not ACharacter)
  - State: `canMove` @ 0x0518, `slapped` @ 0x0540, `throwing` @ 0x055A,
    `throwingProp` @ 0x0570 (`Aprop_C*`), `throwingComp` @ 0x0578,
    `A` @ 0x0580 (FTransform — throw transform), `hit_projectile` @ 0x05B0,
    `eat` @ 0x05C4, `eat_s` @ 0x05CC, `Key` @ 0x05D0 (FName),
    `kerfusRun` @ 0x05F8, `kerfTp` @ 0x05FC (FVector — TP destination),
    `specificTarget` @ 0x0610 (`AActor*`), `buster` @ 0x0620,
    `throwingPropClass` @ 0x0618 (`TSubclassOf<Aprop_C>`),
    `rep` @ 0x0608, `fed` @ 0x060C
  - Events: `targetDestroyed()`, `getTarget()`, `playerspd()`,
    `pickup(actor, NewParam)`, `OnFail/Success_*` AI path callbacks
- `Anpc_ariral_shooter_C : ACharacter` (0x520, npc_ariral_shooter.hpp:4)
  — **NOT inheriting arirFollower** (directly ACharacter)
  - State: `Target` @ 0x04F0, `moving` @ 0x04F8, `fungun` @ 0x0500
    (`Aprop_funGun_C*`), `runAway` @ 0x0508, `lockTarget` @ 0x0510,
    `treehouse` @ 0x0518 (`Atreehouse_C*`)
  - Events: `Sense()`, `findLookVector()`, `see(item, detected)`,
    `CustomEvent()`, `ReceiveTick`, `step(loc)`
- `Anpc_arirGunStealer_C` — variant of shooter, steals weapons
- `Anpc_ariral_pigBeater_C : Anpc_arirFollower_C` (shell — only adds
  begin-play + targetDest, npc_ariral_pigBeater.hpp:4)
- `AariralRepEventHandler_C : AActor` (ariralRepEventHandler.hpp:4) —
  ticking world singleton; field `prevRep` @ 0x0230; methods
  `calcRep()` (returns int32), `launchEvent()`. **This is the event
  driver for the ariral faction** — replays of `launchEvent` push
  ariral spawn events on the game world.

### 1.4 Zombie species + boss variants

All inherit `ACharacter` or `Anpc_zombie_C`:

- `Aantibreather_C : ACharacter` (antibreather.hpp:4) — paranormal
  follower. State: `dash` @ 0x0548, `Player` @ 0x0550, `aggressive` @
  0x0574, `punched` @ 0x0575, `runSpeed` @ 0x0578. Has
  `UPawnSensingComponent`.
- `Aantibreather_angry_C` — variant.
- `Anpc_krampus_C : ACharacter` (npc_krampus.hpp:4, 0x540)
  - State: `Mode` @ 0x04F8 (`enum_krampusMode::Type`, 6 values —
    krampus has explicit AI state machine), `stabPlayer` @ 0x0500,
    `running` @ 0x0508 (float), `jumped` @ 0x050C, `butter` @ 0x0510
    (`Aprop_C*` — krampus targets butter props), `ignoreButters` @
    0x0518, `fireballs` @ 0x0530, `Delay` @ 0x0534 / `maxDelay` @ 0x0538
  - Events: `throwFireball()`, `scanForButter()`, `prepareJump()`,
    `tryToDespawn()`, `safeTp()`, `fireball()`,
    `BndEvt__stabRadius_..._BeginOverlap`,
    `BndEvt__begoneRadius_..._BeginOverlap` (l. 26-54)
- `Anpc_funguy_C : ACharacter` (npc_funguy.hpp:4, 0x590)
  - State: `landed` @ 0x0560, `panic` @ 0x0561, `voiceMode` @ 0x0578,
    `awoken` @ 0x057C, `seen` @ 0x057D, `runawayFrom` @ 0x0588
    (`AActor*`)
  - Events: `awake()`, `tryAwake()`, `shroomed()`, `runAway()`,
    `Despawn()`
- `Anpc_goreSlither_C : ACharacter` (npc_goreSlither.hpp:4, 0x5DC)
  — water-zone slithering pack enemy.
  - State: `pack` @ 0x0558 (`TArray<Anpc_goreSlither_C*>` — flock),
    `TargetActor` @ 0x0568, `jumping` @ 0x0578, `jumpActor` @ 0x0580,
    `health` @ 0x05A0, `underwater` @ 0x05B0, `bloody` @ 0x05C0,
    `waterImmune` @ 0x05CC, `nearRadius` @ 0x05D0, `Key` @ 0x05D4
    (save key)
  - Events: `preDied()`, `died()`, `SetMesh()`, `turnTo(target)`,
    `checkJump(jump, jumpAtActor)`, `doJump()`, `doMove()`, `Sense()`,
    `sensedLose()`, `enteredTheWater()`, `exitTheWater()`,
    `waterDamage()`, `didBite()`, `getPack()`
  - **One of the only NPCs with explicit pack/flocking state** —
    relevant for AOI.
- `Anpc_orborb_C : ACharacter` (npc_orborb.hpp:4, 0x598) — paranormal
  rolling orb.
  - State: `beginEvent` @ 0x0560, `dassad` @ 0x0580 (sic), `snds` @
    0x0570, `NewVar_0` @ 0x0588 (FString)
- `Anpc_angryErieFlesh_C : ACharacter` (128 lines)
- `Adeer_C : ACharacter` (deer_DUPL_1.hpp:4, 0x509)
  - State: just `diss` @ 0x0508 (bool — disappearing). Very thin AI.
  - Events: `disappear()`. Uses `UTimelineComponent` for fade-out.
- `Afossilhound_C : ACharacter` (fossilhound.hpp:4)
  - State: `Key` @ 0x0538, `Anim`, `chase` @ 0x0548, `Steps`,
    `rendered`, `gibs`, `accDmg`, `health` @ 0x057C, `deth`, `maxHealth`,
    `inWater`, `digOut`, `gibLifespan`
  - Has `Ucomp_photographic_C` — photographable (camera mechanic).
- `Ainsomniac_C : ACharacter` (insomniac.hpp:4, 0x530) — spawned via
  `mainGamemode::trySpawnInsomniac` (mainGamemode.hpp:440).
  - State: `Anim`, `Alpha`, `walking`, `lastPoint`, `spooker`,
    `engage` (float — "stalking" state)
- `Awisp_C : ACharacter` (wisp.hpp:4, 0x528) — particle entity; uses
  ACharacter for nav-mesh wandering but logically an effect.
- `Akillerwisp_C` (killerwisp.hpp, 111 lines) — aggressive variant.
- `Agrayboar_C : Aprop_C` (grayboar.hpp:4) — **NOT an ACharacter** (it
  inherits prop). A floating hovercraft creature that holds a separate
  `AgrayboarPawn_C` for AI driving.
  - State: `Pawn` @ 0x0408 (`AgrayboarPawn_C*`), `movementVector` @
    0x0410, `Speed` @ 0x0428, `Target` @ 0x0430, `currPoint` @ 0x0440,
    `nextPoint` @ 0x044C, `health` @ 0x0474, `biting` @ 0x0466,
    `biteActor` @ 0x0468, `roaring`, `dying`, `lastDamagedActor`
- `Amurderkerfur_C`, `AmurderKerfuroDig_C` — special kerfur subspecies.
- `A00000000npc_C : ACharacter` (00000000npc.hpp:4) — appears to be a
  base placeholder, just a Billboard. Likely the editor preview /
  category root.

### Common NPC dispatch surface (every NPC implements these)

This is the universal **"player → NPC" / "NPC → world"** UFunction set
that appears verbatim on every NPC and most interactables:

```
addDamage(actor, damage, hit, impact, skipSetting)   // damage entry point
ImpactDamage(damage, hit, actor, impact)              // physics-impact damage
exploded(damage, loc, explosion)                       // taken explosion
reachedByExplosion(loc, damage, explosion)
reachedByLightning(lightning)
fireDamage(damage)  ignite(fuel)  extinguishFire()
microwave(microwave)  microwaveElec()
addTemperature(t)  accumulateTemperature(t, speed, dt)
slice(clean)  bitten(player)  eaten(player)  hooked(hook)  unhook()
playerHandUse_LMB/_RMB/_release_*/_mouse/_mouseWheel/_anyKey
playerUsedOn(player, hit, comp, holdObject, holdName)
playerLookAway(player)  lookedAt(player, hit, comp)
steppedOn(player, hit)  step(loc)  stepped(volume, loc)
crowbarOpen(crowbar)  crafted()  cleanSponge(...)
insertBattery(player, battery)
playerSit(player)  playerUnsit(player)
enterWater/leaveWater(water)
setPropProps(static, frozen, active, sleeping)
setKey(name)  GetKey(name)  getOnlyKey(name)
setIgnoreSave(ignore)  ignoreSave(out)
getData(out_struct_save)  loadData(struct_save, out)
gatherDataFromKey(gather, loadTransform)
```

**This is the cross-entity dispatch table.** Almost any player-side
interaction routes through one of these. For coop, the **host must
authoritatively call most of them**, and the client must hear about
state changes via property snapshots (NOT replay-the-call, since BP
side effects may dirty other state).

---

## 2. Interactables

### 2.1 Doors

- **Base**: `AtriggerBase_C : AActor` (triggerBase.hpp:4)
  - Fields: `Objects` @ 0x0230 (`TArray<AActor*>` — fan-out targets),
    `objects_keys` @ 0x0240, `IDs` @ 0x0250, `Key` @ 0x0260 (FName —
    save key), `routes` @ 0x0268 (cosmetic particle systems),
    `ignoreSave` @ 0x0278, `GameMode` @ 0x0280
  - Core method: `fireTrigger(target, type)` (l. 46), `runAll(allIndex)`
    (l. 50), `runTrigger(owner, index)` (l. 111),
    `setActiveTrigger(sentFrom, active)` (l. 110)
  - Trigger-save: `loadTriggerData(data, return)` / `getTriggerData(data)`
- `Adoor_C : AtriggerBase_C` (door.hpp:4, 0x42D)
  - State: `isOpened` @ 0x0350, `isMoving` @ 0x0351, `Active` @ 0x0352,
    `autoclose` @ 0x0353, `jammed` @ 0x03B0, `alienated` @ 0x03B1,
    `superClosed` @ 0x0378, `ignoreNPC` @ 0x0388, `keycardName` @
    0x03B8, `damageProtected` @ 0x0410, `cantClose` @ 0x042A,
    `doorBumping` @ 0x0429, `ignoreInteractions` @ 0x042B
  - Refs: `trigger_opened` @ 0x0358 (chained trigger), `trigger_closed`
    @ 0x0368, `passlocks` @ 0x0390 (`TArray<ApasswordLock_C*>`),
    `nav` @ 0x0380 (`AnavModifierBox_C*`)
  - Delegate: `doorOpened` @ 0x03D0 (`Fdoor_CDoorOpened`) — multicast.
  - Events: `doorOpen(bypassCheck)` (l. 118), `doorClose(bypassCheck)`
    (l. 117), `jam(autoCancel)` (l. 82), `hack(source)` (l. 83),
    `jamCancel(duration)` (l. 122), `checkSensor()` (l. 116),
    `settime(opened)` (l. 120), `del()` (l. 123).
  - **Sync-critical state**: `isOpened`, `isMoving`, `jammed`,
    `superClosed`, `Active` — plus interpolated mesh transform on
    timeline.
- `Adoor_pryable_C` (door_pryable.hpp, 14 lines — small derivative)
- `AcargoliftDoor_C` (cargoliftDoor.hpp, 13 lines)
- `AdreamfillDoor_C`, `Anav_door_C` (assoc nav modifier)
- `Aprop_microwaveDoor_C`, `Aprop_fridgeDoor_C/2`, `Aprop_safeDoor_C`,
  `Aprop_swinger_crematorDoor_C`, `Aprop_swinger_transformerDoor_C`,
  `Aprop_bunkhouseDOor_C` — **all small derivatives** of doors/swingers
  for specific furniture. Each has its own `isOpened`-like bool.
- `ATheDoor_C` — special storyline door.

### 2.2 Switches, buttons, levers

- `Alightswitch_C : AtriggerBase_C` (lightswitch.hpp:4, 0x2B0)
  - State: `A` @ 0x02A0 (bool — switch on/off), `Trigger` @ 0x02A8
    (`AtriggerBase_C*` — chained target)
  - Events: `use()` — flips and fires triggers
- `Abuttons_C : AtriggerBase_C` (buttons.hpp:4, 0x2A8) — skeletal-mesh
  button (pressed-down animation). State: just the component refs;
  state stored in trigger save.
- `Atrigger_button_C : AtriggerBase_C` (trigger_button.hpp:4, 0x2A1)
  - State: `Label` @ 0x02A0 (bool)
- `Aprop_wireComponent_button_C`, `Aprop_wireComponent_switch_C` —
  wireable variants (cord connection).

### 2.3 Keypads / locks / readers

- `ApasswordLock_C : AtriggerBase_C` (passwordLock.hpp:4)
  - State: `password` @ 0x0350 (FString — secret),
    `inPassword` @ 0x0380 (FString — buffer being typed),
    `Active` @ 0x0330, `entering` @ 0x0348, `enterFalse` @ 0x0349,
    `isAcc` @ 0x037C, `isDeny` @ 0x037D, `isCard` @ 0x0390,
    `isFocused` @ 0x0391, `isReset` @ 0x0360, `protected` @ 0x0361,
    `Num` @ 0x0378, `door_key` @ 0x0340, `door` @ 0x0338 (`Adoor_C*`),
    `Pair` @ 0x0320 (`ApasswordLock_C*`), `pair_key` @ 0x0328
  - Has 10 numeric box components + accept/deny + card boxes —
    a touch terminal.
  - **Sync-critical**: `inPassword` (per-keystroke), `isFocused` (only
    one player can type at a time — coop locking concern), `Active`.
- `Apanel_SATconsole_C`, `Apanel_radar_C` — radar/control terminals.
- `Atransformer*` family + `Aprop_computerpanels_C` — generator/computer
  panels (each is its own door/switch composite).
- `Akeycard*` family — readers + cards.

### 2.4 Wire / cord interactables (cross-actor signal carriers)

- `Acord_C`, `AcordSocket_C` referenced in triggerBase methods
  `cordPlugged/cordUnplugged`. Cords physically connect entities;
  socket state controls whether downstream triggers fire.
  **Sync = streaming cord endpoints + plug state.**

---

## 3. Pickups / items / props

### 3.1 Aprop_C — universal pickup/object base

- **Class**: `Aprop_C : AActor` (prop.hpp:4, 0x363) — the **physics
  prop** every gameplay item descends from (food, weapons, tools,
  ammo, terrariums, kerfurOmega's prop form, debris).
- **Critical state** (prop.hpp:6-39):
  - `propData` @ 0x0260 (Fstruct_prop, 0x78 — name, mass, volume,
    pricing, lore-config)
  - `Name` @ 0x0258 (FName — runtime asset name)
  - `Static` @ 0x02D8 (bool — physics frozen by world)
  - `frozen` @ 0x02DA (bool — frozen by player/spell)
  - `sleep` @ 0x02DD (bool — physics sleeping)
  - `ingoreFix` @ 0x02DC (bool — toolbox can't repair)
  - `Key` @ 0x02E0 (FName — save key / **cross-peer entity ID**)
  - `Hit` @ 0x02E8 (multicast delegate `Fprop_CHit`)
  - `unhooked` @ 0x02F8 (multicast `Fprop_CUnhooked`)
  - `takenByPlayer` @ 0x0328 (multicast `Fprop_CTakenByPlayer`)
  - `awoken` @ 0x0340 (multicast `Fprop_CAwoken`)
  - `touched` @ 0x0350 (multicast `Fprop_CTouched`)
  - `nametag` @ 0x0314 (FName — player-set label)
  - `propPicker` @ 0x0240 (`FDataTableRowHandle` — datatable row →
    static config)
- **Mutating events** (prop.hpp:110-200): see the universal NPC
  dispatch surface above — props share it almost identically.
  Additional prop-specific:
  - `Init()` (l. 92), `ignited()`, `setNametag()`, `Shadows(...)`
  - `awakeUnfreeze()` (l. 187), `createSleepingHit()` (l. 188)
  - `hittedd123(self, other, normalImpulse, hit)` (l. 182)
  - `physDestroyed()`, `physPreDestroyed()`
- **Inheritors** — 1206 `prop_*.hpp` files (literally every droppable
  item). Categories include:
  - **Food**: `Aprop_food_C` (prop_food.hpp, 0x3D8) — adds `foodData`,
    `Temperature`, `ripeness`, `isFrozen`, `useSpoiling`,
    `poison_strength`, `notEdible`, `uses`. Many sub-foods + cooking
    variants (`prop_cookingFood_*.hpp`, ~20 of them).
  - **Containers**: `Aprop_container_C : Aprop_C` (prop_container.hpp,
    0x42A) — adds `propInventory` @ 0x0398
    (`UpropInventory_C*`), `Locked` @ 0x03E8, `lootEntry` @ 0x03A0
    (`FDataTableRowHandle`), `massData/volumeData/nameData` for slot
    state, `velLinear/velAngular`. ~70+ subclasses (`_barrel`,
    `_cardboardBox*`, `_crate`, `_desk`, `_drawers`, `_fridge`,
    `_fileCabs`, `_garbagebin`, `_giftbox`, `_itemBox`, `_safe`,
    etc.)
  - **Cameras**: `Aprop_camera_C` + `_bad/_cursed/_good/_s/_ultra`
  - **Weapons / tools**: `prop_funGun`, `prop_flamethrower`,
    `pryingCrowbar`, `prop_toolbox`, `prop_workbench`, `prop_drive`,
    `prop_microwave`, `prop_gascan`, `prop_canopener`, etc.
  - **Floppies / data**: `prop_unotebook_kerfuro` etc.
  - **Light props**: `prop_candle`, `prop_ceilingstarsspawn`
  - **Pickup ammo/batteries**: `prop_ammobox`, `prop_ammobox2`,
    `prop_batts`, `prop_batteryCharger`, `prop_carBatteryCharger`,
    `prop_atvcarbattery`
  - **Plants**: `growingPlant` and friends.
- **Volume**: world tracks them via the gamemode `existingGatherers` /
  loot tables; counts can easily be **hundreds to thousands** in a
  played save (`s_may2026` is a long save). AOI streaming essential.

### 3.2 Player-grab / collect predicate API

Every interactable answers these — used to route the look-at + use
chord:

```
canBePickedUp(out ignore)
canBeCollected(out ignore)
playerTryToGrab(player, out collected)
playerTryToHold(player, out collected)
playerTryToCollect(player, out collected)
canBePutInContainer(out)
canPickup(out)
canBeUsedHold(out)
canHit(out noHit)
asContainer(out container)
asFarmPlant(out plant)
asCookedFood(out cooked)
asFood(out food)
asProp(out prop)
lookAt(player, hit, out return, out text, out boundComp, out number)
landedOn(player, out ignoreFallDamage)
isButtonUsed(out failed)
isNotSawable(out)
getActionOptions(player, comp, actor, numberIn, out options, out enum, out names, out number, out lookAtCenter)
actionOptionIndex(player, hit, action, lookAtComp)
playerGrabbed_pre(player, hit)
playerGrabbed(player, hit)
playerHoldPre(player)  playerHoldPost(player)
playerUnequip(player)  unequpped(player)
beginHoldingObject(player, hit)
```

This is the **already-shipped grab-stage hook surface** (per memory:
playerTryToGrab + canPickup + the 6 grab observers are wired). For
NPC/entity sync, we already have most of these observed; the
**unobserved set is the AI-side mutations** (move, attack, AI sensing).

---

## 4. Spawnable entities (runtime spawned, not world-loaded)

### 4.1 Spawner / ticker architecture

Two patterns:

1. **Editor-placed prop spawner** — `ApropSpawner_editor_C : AActor`
   (propSpawner_editor.hpp:4)
   - Fields: `prop` (FName), `obj` (TSubclassOf<AActor>), `Actor`
     (current spawned), `p_static/p_frozen/p_active/ignoreSave`,
     `spawnOnce` (FName), `autoSpawn`
   - Methods: `Spawn(out actor)`, `Despawn()`,
     `spawn_treehouse(treehouse, out actor)`
   - **Used by mappers** to place an asset reference and have it
     spawn at runtime (so the actor is fresh + scriptable).

2. **Ticker spawners** — `Aticker_*Spawner_C : Aticker_base_C`. These
   tick (ReceiveTick) and conditionally call SpawnActor; on the spawned
   actor's destruction, the ticker re-checks (e.g.
   `insomniacDest(DestroyedActor)`).
   - `Aticker_insomniacSpawner_C` (ticker_insomniacSpawner.hpp:4)
   - `Aticker_fossilhoundSpawner_C`, `_deerSpawner`, `_wispSpawner`,
     `_yellowWispSpawner`, `_mannequinSpawner`, `_hexahiveSpawner`,
     `_beehiveSpawner`, `_bp7Spawner`, `_treeSpawner`, `_bushSpawning`,
     `_susHoleSpawner`, `_fireflySpawner`
   - Each maintains internal state about how many are alive + max +
     cooldown.

### 4.2 Other spawner variants

- `AABplushSpawner_C`, `AantibreatherSPawner_C`, `Aarg_whspawn_C`,
  `Aargc_spawn_C`, `AarirBusterSpawner_C`, `AautumnLeafSpawner_C`,
  `AbatchSpawner_C` (+ `_babygame`, `_waterlocker`),
  `AbirchSpawner_C`, `AblackballSpawner_C`, `AbloodSkeletonSpawner_C`,
  `Abs_spawner_C`, `AbunnySpawner_C`, `AeasterEggSpawn_C`,
  `AfurfurAltarSpawner_C`, `AghostcarSpawner_C`,
  `AgoreSlithersSpawner_C`, `AgrayBoarSpawner_C`,
  `AgreenFireSpawner_C`, `AhexahiveSPawner_C`, `AhillRollerSpawner_C`,
  `AmushroomSpawner_C`, `AoilStainSpawn_C`, `ApineconeSpawner_C`,
  `ApiramidSpawner_C`, `Aprop_broomspawner_C`,
  `Aprop_ceilingstarsspawn_C`, `Aprop_notebook_busterSpawner_C`,
  `Aprop_tvSpawner_C`, `Aprop_kerfurOmega_C` (which acts as both prop +
  kerfurOmega-spawner via `spawnKerfuro()` — prop_kerfurOmega.hpp:13)
- `AeventSpawnerPivot_C` — event-system spawn-point (placed in world,
  the eventer picks from these).

### 4.3 Gamemode-driven spawn

`AmainGamemode_C` (mainGamemode.hpp) hosts:

- `trySpawnInsomniac(out canSpawn, out loc)` @ l. 440 — host-side
  decision.
- `spawnPropThroughGamemode(prop, transform, amount, out actor)` @ l.
  412 — central spawn API for arbitrary props by name.
- `Spawn Bad Sun()` @ l. 417, `spawnRedSky()` @ l. 428, `spawnBlackFog`
  @ l. 429 — environmental event spawns.
- `spawnErrorObject(transform)` @ l. 376 — error props (dev/debug).
- `getSignalSData(out struct_signal_spawn)` @ l. 463 — packed signal
  data for save.

`AariralRepEventHandler_C` (ariralRepEventHandler.hpp) — singleton
that ticks, computes `calcRep()` (int reputation), and calls
`launchEvent()`. **The event director for ariral spawns.**

### 4.4 UFOs / arir ships (event-driven flying actors)

All `AActor` subclasses (or `Aactor_save_C` for save-persisted), with
spline / timeline / spring physics rather than `ACharacter`:

- `AskyUfo_C : Aactor_save_C` (skyUfo.hpp:4, 0x299) — ambient
  high-altitude UFO. State: `clampHeight` @ 0x0290 (FVector2D),
  `rendered` @ 0x0298. Just tick.
- `AmorningUfo_C` (morningUfo.hpp) — short morning-time flyby.
- `Amidasufotest_C : Aactor_save_C` (midasufotest.hpp:4) — spline-driven
  flying. State: `A`, `Speed`, `lastloc`, `Velocity`, `Active`.
- `AufoAlienDropper_C : Aactor_save_C` (ufoAlienDropper.hpp:4, 0x301) —
  drops aliens. State: `flyAway`, `Init`, `Fly`, `Drop` (TSubclassOf),
  `Weight`, `aliens` (int), `arrived`, `alinens` (`TArray<AActor*>`),
  `spawnArir`. Events: `forceDeploy`, `tickerMortar`, `destAlien(...)`,
  `healer()`.
- `AufoAbducter_C : AActor` (ufoAbducter.hpp:4, 0x2E8) — abducts a
  target. State: `abduct` @ 0x02C0 (`AActor*`), `loc_A`, `abducting`,
  `Height`, `Add`. Events: `activated()`, `forceDeploy()`,
  `killKerfur()`.
- `AufoDreamer_C`, `AufoDropper_C` (+ `_body/_car/_pig/_tank`).
- `Aufo_joel_C`, `Aufo_pillfo_C`, `Aufo_ballfo_C`, `Aufo_boofo_C`,
  `Aufo_boofo_spawn1_C`, `Aufo_trifo_C` — themed UFO bosses.
- `Adream_ufo_C` — appears in dreams.
- `AarirShip_C : AActor` (arirShip.hpp:4, 0x2D8) — the main alien
  visitor ship. Timeline-driven: `Timeline_0`, `canart`, `Scanline`.
  Events: `lightInt()`, `getLoc()`, audio-finished callback. Heavy
  cosmetic mesh (warpStart audio, scan cone, flash material, etc.).
  - `AarirShipAppear_C`, `AarirShipVanishing_C`, `AarirShip_tower_C` —
    sub-states.
- `AHoelUfo_C : AActor` (HoelUfo.hpp:4, 0x320) — multi-orbit UFO swarm.
  State: `Amount`, `Speed`, `Meshes` (TArray), `speeds`, `Angles`,
  `distances`, `Vectors`, `lights`, `Velocity`, `FFloatSpringState
  spring`, `turnAt`, `Angle`, `prev`. Events: `attackBeam()`, `gen()`,
  `attack()`, `changeSpringDir()`, `changeSpeed()`.
- `AhoelUfoAttack_C` — its attack-actor.

### 4.5 Vehicles / mobile player chassis

- `AATV_C : APawn` (ATV.hpp:4, 0x8A1, 477 lines) — the player's quad
  bike. **Massive state surface, MTA-class vehicle for sync purposes.**
  - Inputs: `input_forward/_back/_right/_left/_alt/_control` (l.
    110-113, 130, 214)
  - Drive state: `Player` @ 0x05B0, `prevPlayer` @ 0x0620,
    `IsDrive` @ 0x05D0, `isDrive_sound` @ 0x05D1, `isDriven` @ 0x05F7,
    `turbo` @ 0x05D2, `nitro` @ 0x05F4, `Brake` @ 0x05D9, `Speed` @
    0x05C0, `rotAlpha`, `torqAlpha` @ 0x05B8-BC, `fuel` @ 0x05D4,
    `Empty` @ 0x05D8, `battery` @ 0x06E4, `lights` @ 0x05E0, `lift` @
    0x06BC, `Fly` @ 0x06BB
  - Body state: `health` @ 0x05E4, `imp` @ 0x05E8, `brokenn` @ 0x05E9,
    `bodyIsOnTheGround` @ 0x088A, `dirt` @ 0x07D8, `airtime`,
    `IsInAir`, `landed`, `underwater`, `inWater`, `floater`, `trap`,
    `zapped`
  - Wheels: `tires` @ 0x0788 (TArray<bool>), `tiresDurability`,
    `tiresDirt`, `tiresFixes`, `tiresTypes`, `tirescount` @ 0x0824,
    `wheelsOnSurface`, `playersWheel`, `lookAtTire`,
    `lookAtTireSocket`, `hasSpareTire`, `spareTire_*`
  - Modules: `Modules` @ 0x06D0 (TArray<`enum_physicalModules::Type`>),
    `hasBigLights`, `hasBumper`, `hasSolar`, `hasBelt`,
    `hasContainer`, `hasAircontrol`, `hasMap`, `hasRadio`, `hasGuns`,
    `hasFloaties`, `hasFly`, `hasChargedEngine`, `has_alternator`,
    `april1st`
  - Containers: `spawnedContainer` @ 0x0740, `containerKey` @ 0x0748
  - Sitting kerfur: `allKerfuros` @ 0x0650, `sittingKerfuro` @ 0x0660
  - Physics: `lastloc`, `interpVel` @ 0x0644, `interpTorque` @ 0x0684,
    `UpVector`, `lastVel`, `previousUpVector`, `prevDot`, `turnForce`,
    `isFrontflip`, `flipFinished`, `canDoFlip`
  - Key: `Key` @ 0x0618 (FName — save / sync ID)
  - Events: `unbrake/unzoom/f_brake/dismount/loadBrake/alarm/explode/
    impulse/damageWheel/makeDirtTire/unscrewPanel/atvBlink/wakeup/
    simulate_turbo/sound_drive/airtimeTimer/ImpactDamage/HitActor/
    InpAxisEvt_mouseY/_mouseX` etc.
- `Aatv_map_C` — map variant.
- `Acar1_witch_C` — Halloween witch broom (also drivable).
- `Aghostcar_C` — ghost car (event spawn, brief drive).

### 4.6 Drone (a flying entity)

- `Adrone_C : AActor` (drone.hpp:4) — the delivery drone.
  - State: `Points` (TArray<FVector> — flight path), `ind`, `Point`,
    `flyingType`, `pickup` (`AActor*`), `Direction`, `inst` (anim),
    `vecGrab`, `isGrabbing`, `pickedUp`, `sellLocation`, `Speed`,
    `Pitch`, `dist`, `order` (Fstruct_storeOrder), `hasOrder`,
    `orderDrop`, `Active`, `Spline` (`AdroneSpline_C*`),
    `sellBox_data`, `pickupLoc`, `pickupRot`, `Data` (full Fstruct_save),
    `hasIteembox`, `calledError`, `container`
    (`Aprop_inventoryContainer_drone_C*`), `canTakeOff`, `hasSack`,
    `sackInteract`, `leaveAfter5min`, `alarmCount`
  - Events: many (213 lines total).

---

## 5. Effect entities

### 5.1 Explosions / damage radials

- `Aexplosion_C : AActor` (explosion.hpp:4, 0x328) — generic
  explosion. State: `Force`, `Radius`, `Damage`, `debrisType` (enum, 6
  values), `debris` (int), `debrisForce`, `fireStrength`,
  `fireFuel`, `Loc`, `impact`, `Skip Setting`, `ignoreWalls`, `Shake`
  (`TSubclassOf<UCameraShakeBase>`), `innerShake`, `outerShake`,
  `objType`, `ignores/ignores_0` (TArray<AActor*>), `isEvent`, `Tag`,
  `postExplosion` (multicast delegate).
  - **Event-driven, transient.** Spawn → tick a timeline → destroy.
- `Aexplosion_pc_C`, `Aexplosion_saltrocket_C`, `Aexplosion_underwater_C`
  — variants.
- `AbegoExplosion_C` — special.
- `Agrime_explosionScorch_C` — visual decal.

### 5.2 Lightning

- `AlightningStrike_C : AActor` (lightningStrike.hpp:4, 0x280)
  - State: `debris`, `explosionTag`, `Radius`. Timeline-driven
    (lightTL).
- `AbaseLightningRod_C`, `AlightningRod_C`, `AlightningHeightAnalyzer_C`
  — interaction with lightning.

### 5.3 Projectiles (small, transient)

- `AnailProjectile_C : AActor` (nailProjectile.hpp:4, 0x258) — nail
  shot from nailgun. State: `lastloc`, `Force`, `nailType`
  (`TSubclassOf<Anail_C>`).
- `AgrimeProjectile_C : AActor` (grimeProjectile.hpp:4, 0x258) — grime
  blob. State: `lastloc`, `Grunge` (TSubclassOf), `Velocity`, `Size`.
- `AplungerProjectile_C : AActor` — plunger shot.
- `Aarir*Throw*` — ariral throws (handled by NPC code, not a separate
  projectile actor).
- `AalienJump_C : AActor` (alienJump.hpp:4) — alien "trampoline"
  spawn. State: meshes + `CanJump`, `debug`. Method `Spawn()`,
  `throw()`. Also has `alienJump_instantSpawn` variant.

### 5.4 Effect actors (player-side state effects)

These are **buffs / debuffs on the player**, not world entities:

- `Aeffect_C` — base; `effects` tracked on gamemode
  (mainGamemode.hpp:241 — `TArray<Aeffect_C*>`, `effects_names`,
  `effect_amount`).
- `Aeffect_afterVaccine_C`, `Aeffect_ariralVaccine_C`,
  `Aeffect_bloodLoss_C`, `Aeffect_foodPoison_C`, `Aeffect_lsd_C`,
  `Aeffect_nausea_C`, `Aeffect_poo_C`, `Aeffect_sleepy_C` — specific
  effects.
- **Sync-relevant if we want shared symptoms** between coop players;
  otherwise local per-peer.

### 5.5 Ragdolls

- `Aragdoll_C : AActor` (ragdoll.hpp:4, 0x240) — a SkeletalMesh +
  physics. Just a holder.
- Subclasses: `ragdoll_ariralLeg_L/_R`, `ragdoll_bodyBag`,
  `ragdoll_bunnydoll`, `ragdoll_zombie`, `ragdoll_zombie_erie`,
  `ragdoll_boneTransformSaveTest`.
- **NPCs spawn these on death** (`Anpc_zombie_C.corpse` @ 0x06A8 = a
  `TSubclassOf<Aragdoll_zombie_C>`).

### 5.6 Triggers / volumes (state-mutators on overlap)

Inherit `AtriggerBase_C` (above):
- Geometry triggers: `trigger_box`, `trigger_box_AB`, `trigger_box_N`,
  `trigger_box_rain` — region detectors.
- Sound triggers: `trigger_ambientSound`, `trigger_sound_DUPL_1`,
  `trigger_locationAmbience`, `trigger_forestMoan`.
- Event triggers: `trigger_alarm`, `trigger_arirEgg`,
  `trigger_bigmRoar`, `trigger_bedEvent`, `trigger_bloodSkeleton`,
  `trigger_breakDish`, `trigger_solarBoom`,
  `trigger_eventt_arirShip`, `trigger_wispSwarm`, `trigger_fakeLmaos`.
- Gameplay: `trigger_destroyByKeys`, `trigger_destroyInRadius`,
  `trigger_forceObject`, `trigger_lockerLooker`, `trigger_notif`,
  `trigger_lightRoot`, `trigger_jamDoor`, `trigger_TBoxActivator`,
  `trigger_achievement`, `trigger_agrav`, `trigger_teleporter`,
  `trigger_tpChamberSpawn`, `trigger_vehtp`,
  `trigger_spawnFollowingArir`, `trigger_spawnProp`,
  `trigger_eventer`, `trigger_delay_DUPL_1`.
- `Atrigger_button_C`, `Atrigger_jamDoor_C` — small derivatives.
- `AtriggerFallingSky_C`, `AtriggerTimer_C`, `AtriggerBase_skeleChair_C`,
  `AtriggerBase_unfreezeProp_C` — special purpose.
- `Aint_ttrigger_C` — internal trigger plumbing.

These are **mostly host-only state**; on overlap, the trigger fires
its `Objects` list → cascading state changes propagate via the
`runTrigger` / `setActiveTrigger` chain.

---

## 6. Cross-entity hooks (delegates + cross-class refs)

### 6.1 Multicast delegates on Aprop_C (delegate-driven events)

- `void hit()` (Fprop_CHit)
- `void unhooked()` (Fprop_CUnhooked)
- `void takenByPlayer(Aprop_C* prop)` (Fprop_CTakenByPlayer)
- `void awoken()` (Fprop_CAwoken)
- `void touched()` (Fprop_CTouched)

These are MTA-shape **broadcast events** — any subscriber can attach.
Coop layer can subscribe to these via UE4SS / reflection without
modifying the BP. Pattern matches how we already wired
playerTryToGrab / canPickup observers.

### 6.2 Multicast on Adoor_C

- `Fdoor_CDoorOpened doorOpened` (door.hpp:50)

### 6.3 Multicast on Aexplosion_C

- `Fexplosion_CPostExplosion postExplosion` (explosion.hpp:40)

### 6.4 Multicast on mainGamemode

- `wokenUp`, `fellAsleep`, `sleepTriggered`, `photoTaken`
  (mainGamemode.hpp:183, 198, 215, 219). High-value hooks for
  cross-peer state sync (sleep cycle, photo events).

### 6.5 Cross-class refs (heavy outbound coupling)

Functions whose params or fields couple multiple categories:

- `addDamage(actor, damage, hit, impact, skip)` — universal damage
  entry on **every** prop + NPC + interactable
- `reachedByExplosion(loc, damage, explosion)` — every damageable
  takes a pointer to `Aexplosion_C`
- `reachedByLightning(lightning)` — pointer to `AlightningStrike_C`
- `microwave(microwave)` — pointer to `Aprop_microwave_C` — any food/
  prop can be cooked
- `playerUsedOn(player, hit, comp, holdObject, holdName)` — every
  passive object answers "another prop was used ON me"
- `hooked(hook)` / `unhook()` — every prop can be attached to a
  `Ahook_C`
- `enterWater/leaveWater(water)` — every entity in/out of an
  `AwaterVolume_C`
- `wallFixer_fix(wallFixer)`, `toolboxFix(toolbox)`, `gascanFuel(...)`,
  `cleanSponge(...)`, `insertBattery(...)` — universal
  "fix/clean/fuel/charge me" hooks
- `crowbarOpen(crowbar)` — pry-open
- `padlock_lock/unlock(padlock)` — lockable
- `lookAt(player, hit, return, text, comp, number)` — universal HUD
  data fetch
- `cordPlugged(cord, socket)`, `cordUnplugged(cord, socket)` — cord
  wire system
- `holdObject_kerf(player, actor)` (kerfurOmega) — couples kerfur ↔
  prop
- `tryToOccupyCar(car)` / `sitOnCar(car)` — kerfur ↔ ATV
- `kerfusPendingServers` (mainGamemode) — gamemode tracks pending
  kerfur server-tasks
- `Anpc_zombie_C.focusedOn` = `Anpc_zombie_C*` — zombie ↔ zombie pack

### 6.6 Save / load (entity persistence keys)

Universal pattern:
- `Key` (FName) on every persistent entity = primary ID for save and
  for any cross-peer match
- `getData(Fstruct_save& Data)` / `loadData(Data, return)` — pack /
  unpack into a 0x100 save-blob
- `gatherDataFromKey(gather, loadTransform)` — host queries "should
  this be in the save"
- `ignoreSave`, `skipPreDelete`, `setIgnoreSave(ignore)` — opt-out
- Triggers have a parallel set: `getTriggerData`, `loadTriggerData`,
  `gatherDataFromKeyT`, `ignoreSave_trigger`,
  `set_ignoreSave_trigger`

**For coop: `Key` is the cross-peer entity ID.** If we replicate the
host's `Key` to the client when spawning the proxy actor, all
gameplay references (`chaseActor.Key`, `holdObject.Key`,
`trigger_opened.Key`, etc.) line up naturally on both sides.

---

## Implications for coop sync (preview, not design)

### High-volume vs low-volume

- **HIGH volume** (AOI / streaming required):
  - `Aprop_C` and all 1206 subclasses — props are everywhere; many
    are physics-active
  - `Anpc_zombie_C` (instanced waves, ~5-20 alive at peak)
  - `Anpc_goreSlither_C` (packs)
  - `Awisp_C` / `Akillerwisp_C` (swarms)
  - `Adeer_C` (multiple)
  - Triggers (hundreds in world, but mostly dormant)
- **MEDIUM volume**:
  - `AkerfurOmega_C` (a few — gamemode `allKerfuros` typically <10)
  - `Adoor_C` (~30-100 in world)
  - `ApasswordLock_C` (~10)
  - `Anpc_arir*_C` (a few alive at a time)
  - `Aragdoll_C` (corpses accumulate; capped by gamemode cleanup)
- **LOW volume** (unique or near-unique):
  - `AATV_C` (1 per spawned ATV — usually 1)
  - `Adrone_C` (1 or 2)
  - `AarirShip_C`, `AHoelUfo_C`, `AufoAbducter_C`, etc. — 0-1 at
    a time, event-triggered
  - `Ainsomniac_C`, `Afossilhound_C`, `Anpc_krampus_C`, `Anpc_funguy_C`,
    `Anpc_orborb_C`, `Aantibreather_C` — rare, 0-2
  - `Agrayboar_C` — gamemode `allGrayboars` array, but spawns are
    rare-event
  - `AariralRepEventHandler_C` — singleton

### Event-driven (transient broadcast) vs state-driven (continuous stream)

- **EVENT-driven** (one packet, "this happened"):
  - `Aexplosion_C`, `AlightningStrike_C`, projectiles
    (`Anail/Agrime/AplungerProjectile`) — spawn, run for <5s, die
  - `Adoor_C.doorOpen/doorClose` — discrete state flip + visual
    timeline plays locally
  - `Alightswitch_C.use` — single-bit flip
  - `Anpc_zombie_C.called` / `.died` — discrete
  - `AkerfurOmega_C.death` / `.makeMeow` / `.startKill`
  - `Aeffect_C` start/stop on a player
  - Multicast delegates (`Aprop_C.hit`, `.touched`, `.takenByPlayer`,
    `.awoken`, `.unhooked`, door `doorOpened`, explosion
    `postExplosion`, gamemode `wokenUp/fellAsleep/sleepTriggered/
    photoTaken`)
  - Trigger fires (`fireTrigger`, `runTrigger`, `setActiveTrigger`)
- **STATE-driven** (continuous interpolated stream):
  - NPC pose + AI state — `chaseActor`, `Mode` (krampus),
    `State` (kerfur), `attacking`, `chasing`, `stunned`,
    `risen`, `Velocity`, `targetLoc` — every tick
  - `AATV_C` — input bits + speed + transform + lights/fuel/dirt
  - `Aprop_C` physics (transform + velocity + sleep state) when
    awake — already covered by the in-progress physics-prop pickup
    feature for held props, but generic prop physics in the world
    also needs covering eventually
  - `Adrone_C` flight path / position
  - `Aexplosion_C` etc. — event but with a short cosmetic timeline
    (probably just spawn locally with the right seed)

### Authoritative ownership

- **Host-authoritative** (no client wiggle room — host runs the AI):
  - All NPC AI (every `ACharacter` NPC and prop-AI like grayboar)
  - All triggers and trigger chains
  - All spawners + gamemode-driven events
  - UFO / arir ship flight paths
- **Host-authoritative with client prediction acceptable**:
  - `AATV_C` — driver's machine runs physics, host validates +
    accepts, broadcasts to others (like MTA vehicle sync — client
    drives, server resolves disputes)
- **Per-player local**:
  - `Aeffect_C` debuffs (already per-player UI state)
  - Camera + HUD widgets
- **Shared / cross-cuts**:
  - `Aprop_C` held by a player → owner is whichever player holds it
    (matches MTA player-grab; already shipped)
  - `AkerfurOmega_C.remoteControl` — RC mode transfers ownership to
    the controlling player (per memory pattern)

### Cross-refs to existing memory + MTA findings

- **MTA precedent**: `research/findings/mta-npc-entity-sync-2026-05-24.md`
  — MTA's CPed / CObject / CVehicle / CElement hierarchy maps
  cleanly: VOTV `ACharacter` NPCs ↔ MTA CPed, VOTV `Aprop_C` ↔ MTA
  CObject, VOTV `AATV_C` ↔ MTA CVehicle, VOTV `AtriggerBase_C` ↔ MTA
  CColShape, VOTV `Aexplosion_C` ↔ MTA CExplosion (transient).
- **Physics-prop pickup (shipped Stage 1)** —
  `research/findings/physics-object-pickup-coop-plan-2026-05-23.md`
  + `physics-object-pickup-architecture-2026-05-23.md`. The prop
  pickup hooks (`playerTryToGrab`, `canPickup`,
  `smoothGrab/pickupObject/pickupObjectDirect/dropGrabObject/
  throwHoldingProp/switchToHeavyDrag`) cover the **player → prop**
  side. We do **not yet** have the **NPC → prop** observers (e.g.
  ariral throws, kerfur picks up its own object via
  `holdObject_kerf`, drone pickup via `Adrone_C.pickup`). When the
  user moves on to NPC sync, those observers need spawning too — to
  authoritatively propagate "kerfur is now holding prop X" to clients.
- **Pose sync (shipped, LAN-tested)** —
  `research/findings/mta-pose-interpolation-2026-05-23.md`. The
  receiver-side LERP / shortest-arc / velocity-scaled snap pattern
  generalises to NPC pawns; we already have the puppet machinery
  (`coop::RemotePlayer.Drive`). For NPCs the same machinery
  applies, just with the NPC's `ACharacter` class instead of
  `mainPlayer_C`, plus an AI-state byte (mode/state/animation flag)
  alongside the pose.
- **Open issues**: `project_remote_player_open_issues.md` notes the
  puppet-floats-in-hands-on bug — same root will affect NPC puppets
  built off `ACharacter`. Fix before broadening to NPCs.
- **CLAUDE.md rule №2 (no parallel paths)**: the existing satellite
  ACharacter pattern (for animation pinning) generalises to NPC
  puppets — reuse, don't fork.

### Notable surprises / risks

1. **kerfurOmega is a chimera** — it's a player-like ACharacter, an
   AI pet, a prop holder, a vehicle passenger (ATV), a save entity,
   a remote-control target, a sentience holder, and a face renderer
   in one class. Its `holdObject` field references real
   `Aprop_C*` — kerfur picking up a prop means **TWO entities'
   state changes** (kerfur's holdObject + prop's parent/transform).
2. **Skin-only variants** mean we can sync kerfur as ONE class — the
   ~20 variants are just `class X : public AkerfurOmega_C {}` (e.g.
   kerfurOmega_alien.hpp:4). The replicated state is shared; the
   variant class is just a TSubclassOf<> for spawn-time mesh/material
   selection.
3. **Anpc_zombie_C uses the kerfur skeleton anim BP**
   (`UkerfurOmegaV1_Skeleton_AnimBlueprint_Child_C` at
   npc_zombie.hpp:46). This means our existing satellite-ACharacter
   anim pinning pattern is **directly reusable** for zombie puppets.
4. **`Agrayboar_C` inherits Aprop_C, not ACharacter** —
   grayboar.hpp:4. It's an AI-driven floating prop, sharing the
   physics surface with regular props. Don't reach for the ACharacter
   path; reach for the prop path (which we already largely have).
5. **`Awisp_C` is an ACharacter** but visually is just a particle
   system — `eff_wisp` particle component on a navmesh-walking
   chassis. Cheap; many at once during wisp swarms. AOI is essential.
6. **`Aprop_kerfurOmega_C`** (prop_kerfurOmega.hpp:4) is the **prop
   form of a kerfur** — a `Aprop_C` that, when "spawned"
   (`spawnKerfuro()` at l.13), spawns the live `AkerfurOmega_C`. This
   prop ↔ pawn duality means coop spawn flows need to handle the
   conversion (host: prop disappears, pawn appears; client must
   mirror the disappear-then-appear atomically).
7. **`AATV_C` is a `APawn`**, not `ACharacter`. No CMC; physics is
   direct on the body. Custom physics body + 4 wheel constraints +
   suspension constraints (sus_FL1..BR1, ax_FL1..BR1) + mesh velocity
   integration. Cannot reuse the puppet ACharacter pattern; needs its
   own MTA-style vehicle sync (input bits + transform stream + impact
   notify).
8. **No replication keywords anywhere.** I grepped for `Replicated`,
   `ReplicateMovement`, `bReplicated` — these are pure SP BPs. UE's
   built-in replication is OFF; only our UDP layer can do this.

---

## Inventory summary (file counts)

- NPC `ACharacter` classes (unique non-shell): ~14 (kerfurOmega, zombie,
  arirFollower, arir_shooter, arirGunStealer, ariral_pigBeater,
  funguy, goreSlither, krampus, orborb, angryErieFlesh, antibreather,
  deer, fossilhound, insomniac, wisp, killerwisp, 00000000npc)
- NPC variants / skins (empty shells): ~30+ (kerfurOmega_*, zombie_*)
- `AtriggerBase_C` descendants: ~36 trigger_* + ~10 specialised (door,
  buttons, lightswitch, passwordLock, panels, etc.)
- `Aprop_C` descendants: ~1206 (numerically dominant)
- Spawners (`*Spawner*`, `ticker_*Spawner_C`): ~40
- UFO / event flying actors: ~20
- Vehicles (`APawn`): ATV (1), atvMap, car1_witch, ghostcar (~4
  total)
- Ragdolls: 8
- Effects: 10+
- Projectiles: 4 (nail, grime, plunger, alienJump)
- Explosions: 4 variants

**File-line references in this document point to
`Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/`
unless otherwise noted.**
