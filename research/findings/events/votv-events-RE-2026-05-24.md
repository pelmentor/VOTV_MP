# VOTV Event System RE — clean-slate enumeration

**Date:** 2026-05-24
**Author:** RE agent
**Scope:** Reverse-engineer VOTV's BP event system so the coop mod can
replicate world events (alien ship crash, abductions, UFO flybys, weather
anomalies, story triggers) cross-peer.
**Methods:** CXX header dump (`CXXHeaderDump/`), IDA on the shipped exe
(`VotV-Win64-Shipping.exe.i64`), MTA precedent for replication patterns.
**Predecessors:**
- `research/findings/npc-creatures/votv-npc-entity-RE-2026-05-24.md`
- `research/findings/npc-creatures/votv-npc-entity-coop-architecture-2026-05-24.md`
- `research/findings/props-lifecycle/votv-aprop-lifecycle-RE-2026-05-24.md`

---

## TL;DR

- VOTV's event system is **100 % BP-driven**. The .exe has zero native
  strings for `runEvent`, `launchEvent`, `spawnRedSky`, UFO names, etc.
  All dispatch flows through `UObject::ProcessEvent` at
  `0x141465930` with the BP-bytecode body interpreted from the .pak.
- The **central dispatcher is `Atrigger_eventer_C`** (`trigger_eventer.hpp`),
  a single actor referenced from `AmainGamemode_C.eventer` at offset
  `0x04D8`. It carries 36 named `AtriggerBase_C*` slots (see §2.1) plus
  arrays of throwers/spawners. Its dispatch surface:
  - `runEvent(FName Event, FName Special)` — primary entrypoint
  - `runSpecialEvent(FName eventName1, bool& return)`
  - `summonArirPrank()`
  - `processKeys(bool& return)`
  - `loadTriggerData(Fstruct_triggerSave Data, bool& return)`
- Every concrete event is a subclass of `AtriggerBase_C` overriding
  `runTrigger(AActor* Owner, int32 Index)`. The base also exposes
  `fireTrigger(AActor* Target, int32 Type)`, `setActiveTrigger(AActor*
  sentFrom, bool Active)`, `runAll(int32 allIndex)`, `sendTag(FName Tag)`.
- **Reputation-driven events:** `AariralRepEventHandler_C`
  (`mainGamemode.ariralRepHandler` @ 0x0BC0) ticks per frame, computes
  `calcRep()`, and calls `launchEvent()` when rep crosses a threshold.
  This is RNG over ariral reputation, so two peers would diverge if both
  ran it.
- **Per-tick periodic spawners** (`Aticker_*` subclasses of
  `Aticker_base_C`): 23 distinct tickers including
  `fossilhoundSpawner`, `mannequinSpawner`, `insomniacSpawner`,
  `roachSummoner`, `kerf`, `serverBreaker`, `dishUncalib`, `flickerer`,
  `wispSpawner`, `yellowWispSpawner`, `beehiveSpawner`, `bp7Spawner`,
  `bushSpawning`, `deerSpawner`, `eyers`, `fireflySpawner`, `foodHall`,
  `foodSleeper`, `foodTolerance`, `hexahiveSpawner`,
  `paused`, `susHoleSpawner`, `tick`, `treeSpawner`,
  `surveilanceMaster`, `widgetRender`. Each makes per-tick RNG decisions
  about whether to spawn.
- **Save state is the authoritative event ledger.** `UsaveSlot_C` holds
  `passEvents: TArray<FName>` (events that already fired),
  `allEvents: TArray<FName>` (known events),
  `triggers: TArray<Fstruct_triggerSave>` (per-trigger persisted state),
  `forceObjects: TArray<FName>`, `specials: TArray<FName>`.
  These MUST stay in sync between host save and client save, otherwise
  the client's story / advancement state diverges.
- **No native scheduler.** There is no `Aevent_scheduler_C` /
  `Atimer_event_C` class. The "schedule" is the union of:
  1. `Atrigger_eventer_C` event slot list — dispatched by name on demand
  2. `AariralRepEventHandler_C` per-tick rep watcher
  3. `Aticker_*` per-tick spawners (one actor per spawn type)
  4. `AtriggerTimer_C::newMinute(FIntVector Time)` — game-time rollover
  5. `AdaynightCycle_C` — day/night phase, weather flags
  6. `AmainGamemode_C::ticker_radiotowerPoof / lockerhead / lakeMonsert /
     tickerBody / tickerFunguy / flowerSpawner / windParticles_check /
     eraseEmptyObjectsTimer / chk1 / checkATV` — gamemode-resident
     periodic UFunctions (timer-driven, not per-tick).

---

## §1. Section 1 — Alien ship crash full call graph

### 1.1 The class is `AskyFallingEvent_C` (not `Acrash_C` / not `AalienShip_C`)

**File:** `CXXHeaderDump/skyFallingEvent.hpp` — 21 lines, very thin.

```cpp
class AskyFallingEvent_C : public AActor {
    FPointerToUberGraphFrame UberGraphFrame;          // 0x0220
    class Ucomp_radarPoint_C* comp_radarPoint;        // 0x0228
    class UBillboardComponent* Billboard;             // 0x0230
    class UArrowComponent* Arrow1;                    // 0x0238
    class UAudioComponent* Audio;                     // 0x0240   // crash siren / approach loop
    class UPointLightComponent* PointLight;           // 0x0248   // approach glow
    class UParticleSystemComponent* eff_glow_tardis;  // 0x0250   // entry/burn particle FX
    class UArrowComponent* Arrow;                     // 0x0258
    class USceneComponent* DefaultSceneRoot;          // 0x0260

    void ReceiveTick(float DeltaSeconds);             // drives fall trajectory
    void ReceiveBeginPlay();                          // initializes spawn / FX
    void ExecuteUbergraph_skyFallingEvent(int32);
};
```

The size (0x268) and absence of `health/exploded/Mesh` fields confirm this
is a **trajectory/effect actor**, not the wreckage itself. It moves under
its own `ReceiveTick`, fires sound + light + tardis-glow particles.
**It does NOT itself spawn debris/loot/enemies** — those happen elsewhere.

### 1.2 The trigger is `AtriggerFallingSky_C` (player-overlap activator)

**File:** `CXXHeaderDump/triggerFallingSky.hpp` — 16 lines.

```cpp
class AtriggerFallingSky_C : public AActor {
    FPointerToUberGraphFrame UberGraphFrame;          // 0x0220
    class USphereComponent* Sphere;                   // 0x0228   // overlap probe
    class USceneComponent* DefaultSceneRoot;          // 0x0230
    class AmainGamemode_C* GameMode;                  // 0x0238

    void BndEvt__triggerFallingSky_Sphere_K2Node_ComponentBoundEvent_0_ComponentBeginOverlapSignature__DelegateSignature(...);
    void ReceiveBeginPlay();
    void ExecuteUbergraph_triggerFallingSky(int32);
};
```

So the wreckage event is **conditionally spawned** when the player overlaps
this sphere. The sphere is placed on the map (`s_may2026` or whichever
story map). Plain overlap-trigger pattern: BeginOverlap → BP graph → most
likely `SpawnActor<AskyFallingEvent_C>` + state mutation on mainGamemode.

### 1.3 The full crash chain (deduced from header surface)

```
[1] Player walks into AtriggerFallingSky_C.Sphere
       |
       v BeginOverlap fires
[2] AtriggerFallingSky_C.BndEvt_Sphere_BeginOverlap
       |  (BP UbergraphFrame logic — bytecode in pak)
       |  likely guards: GameMode.passEvents.Contains("skyFall") ? skip : run
       |  marks GameMode.passEvents += "skyFall"  (save mutation)
       v
[3] UWorld::SpawnActor<AskyFallingEvent_C>(...)
       |
       v
[4] AskyFallingEvent_C.ReceiveBeginPlay
       - inits Audio (approach loop), PointLight, eff_glow_tardis
       - registers radarPoint (so it shows on player radar — gameplay-critical)
       |
       v per-tick
[5] AskyFallingEvent_C.ReceiveTick(DeltaSeconds)
       - moves actor along its fall trajectory (under its own physics-light kinematics)
       - rotates Arrow1 direction
       - eventually:
[6] (BP-internal end-of-trajectory branch — exact mechanism unknown from header alone;
     likely a Line/Sphere trace from current loc to ground, with impact when hit)
       - spawn AburningDebris_C or similar at impact
       - spawn alien/grayboar NPCs around impact site
       - particle burst (one-shot via UGameplayStatics::SpawnEmitterAtLocation)
       - shake camera (matinee/timeline)
       - destroy self
```

### 1.4 Adjacent / suspected wreckage actors

The skyFallingEvent actor itself does not encode wreckage props. The
likely wreckage spawn surface, in order of priority, is:

1. **`AburningDebris_C`** (CXX dump has `burningDebris.hpp`) — burning
   chunks left after explosion. Likely spawned by skyFallingEvent's
   bytecode at impact.
2. **`AfallingBody_C`** (`fallingBody.hpp`) — has `TArray<FName> Drop`
   field; per-tick. This actor is a **body that falls and drops named
   items** at the end of its trajectory. **This is likely the UFO-payload
   variant of the falling-thing pattern.**
3. **`AufoDropper_C`** (and subclasses `ufoDropper_body`, `_car`, `_pig`,
   `_tank`, `_alien`/`ufoAlienDropper`) — a UFO that flies in via
   Timeline_0, opens up, drops a `TSubclassOf<AActor> Drop` payload, and
   leaves. `forceDeploy()` is the bypass-overlap entry point.
4. **`AufoAbducter_C`** — UFO that LIFTS a target (`abduct: AActor*`,
   `abducting: bool`) up via Timeline_0 + tractor beam (`volumeBeam`,
   `eff_ufobeam`).

These four actors are the **payload-delivery primitives**. skyFallingEvent
is the meteor-trajectory animation primitive; the actual gameplay impact
is delivered by one of the four above acting on the impact site.

### 1.5 Save-state impact of the alien ship crash

From `UsaveSlot_C`:
- `passEvents += "skyFall"` (or whatever FName the BP uses) — guards
  against double-fire on save reload.
- `signalsGlossary` / `catchedSignals` — if the crash produces a
  signal-pickup, the per-save signal ledger is mutated.
- `objectsData` — if debris is `Aactor_save_C`-derived, it goes here.
- `triggers` — `AtriggerFallingSky_C` itself, if `ignoreSave==false`,
  pushes its `Fstruct_triggerSave` here.

**Cross-peer consequence:** if the host fires this event and the client
does not get told, the host's `passEvents` permanently diverges from the
client's. On the next save load, the client will re-fire the event;
the host will skip it. Loot / NPC counts double or vanish depending on
which side authoritated the save. **This is the strongest single argument
for explicit event replication.**

---

## §2. Section 2 — Complete event taxonomy

### 2.1 Master event registry on `Atrigger_eventer_C`

These are the named `AtriggerBase_C*` slots that the dispatcher's
`runEvent(FName)` selects on. Each is a separate actor placed on the map.

| slot field name           | known concrete subclass / behaviour                                 |
|---------------------------|---------------------------------------------------------------------|
| `event_arirShip`          | `Atrigger_eventt_arirShip_C` — alarms-arming, ariral ship arrival   |
| `event_solar`             | `Atrigger_solarBoom_C` — solar boom shockwave on multiple light roots|
| `event_falseEnter`        | `AtriggerBase_C` — generic                                          |
| `event_arirSignal`        | `AtriggerBase_C` — signal-triggered event                           |
| `event_vehicletp`         | `Atrigger_vehtp_C` — vehicle teleport                               |
| `event_agrav`             | `Atrigger_agrav_C` — anti-gravity zone, props float                 |
| `event_mann`              | mannequin event                                                     |
| `event_arirSpk`           | ariral speak event                                                  |
| `event_arirpicSignal`     | ariral picture-signal event                                         |
| `event_peace`             | "peace" event                                                       |
| `event_bigmRoar`          | `Atrigger_bigmRoar_C` — big M roar                                  |
| `event_wisps`             | `Atrigger_wispSwarm_C` — wisp swarm                                 |
| `event_arirFollower`      | `Atrigger_spawnFollowingArir_C` — spawns following ariral           |
| `event_arirEgg`           | `Atrigger_arirEgg_C` — ariral egg event                             |
| `event_vent`              | vent event                                                          |
| `event_crys`              | crystal event                                                       |
| `event_fakeGrays`         | `Atrigger_fakeLmaos_C` (likely)                                     |
| `event_treehouse_0`       | treehouse stage 0                                                   |
| `event_picnic`            | picnic event                                                        |
| `event_breakVictor`       | break-victor server                                                 |
| `event_break_RomeoSierra` | break Romeo-Sierra server                                           |
| `event_destoyPicnic`      | destroy picnic                                                      |
| `event_toeStab`           | toe stab                                                            |
| `event_cookier`           | cookier event                                                       |
| `event_susArir`           | sus ariral                                                          |
| `event_arirSat_0/1/2`     | ariral satellite events 0..2                                        |
| `event_bedEvent`          | `Atrigger_bedEvent_C` → drives `AbedEvent_C.woken()`                |
| `event_paperGray`         | paper gray event                                                    |
| `event_treehouse_build_1..5` | progressive treehouse build stages                               |
| `event_obelisk`           | obelisk event                                                       |
| `event_piramid`           | pyramid event                                                       |
| `event_piramid_sig`       | pyramid signal event                                                |

Also fields on the eventer:
- `TArray<AariralThrowStuff_C*> ariralThrowers_bad / _good` — ariral
  foods-throw events
- `bool combat` — combat mode flag
- `TArray<AeventSpawnerPivot_C*> eventSpawner` + `TArray<FName>
  eventSpawner_tags` — generic FName-tagged spawn pivots

### 2.2 Concrete `event_*` classes (separate from `Atrigger_eventer_C` slots)

These are `Aactor_save_C`-derived or `AActor`-derived events placed on the
map directly, **outside** the trigger_eventer dispatch list. Each is its
own self-contained event with its own activation condition (overlap, save
flag, BeginPlay roll).

| Class                            | Behaviour                                                                              |
|----------------------------------|----------------------------------------------------------------------------------------|
| `Aevent_arirFuelsAtv_C`          | save-actor; ariral fuels ATV; tracks `fuel`, `health`, `arir TSubclassOf`, `Box` overlap |
| `Aevent_arirStealsGun_C`         | per-tick check, no state                                                               |
| `Aevent_arirTreehouseSleep_C`    | save-actor; `wokeup()` UFunction                                                       |
| `Aevent_bottomHoleController_C`  | save-actor; controls bottom-hole room, kavotia patrol, beacons; `BeginOverlap` driven  |
| `Aevent_consoleWrite_C`          | writes to console (`Uui_console_C`); BeginPlay-driven                                  |
| `Aevent_fleshRain_C`             | save-actor; spawns flesh rain on BeginPlay                                             |
| `Aevent_fossilBoarWar_C`         | spawns boar war between min/max box bounds (`spawnLoc()`)                              |
| `Aevent_funnyGascans_C`          | save-actor; gascan-overlap-triggered                                                   |
| `Aevent_lightsTurnoffer_C`       | overlap-driven; toggles lightswitch + doors + passwordLock                             |
| `Aevent_passwordGuesser_C`       | same shape as lightsTurnoffer                                                          |
| `Aevent_trashPiles_C`            | overlap-driven trash spawn                                                             |
| `Aevent_vaccine_C`               | overlap-driven vaccine event                                                           |
| `AbedEvent_C`                    | save-actor; `woken()` UFunction (sleep cycle)                                          |
| `AredSkyEvent_C`                 | `set(bool isred)` — anomaly weather; spawned by `mainGamemode.spawnRedSky()`           |
| `AskyFallingEvent_C`             | trajectory + FX actor for falling things (see §1)                                      |

### 2.3 UFO classes — five flyby variants + four delivery variants

**Flyby-only (decorative-ish, but spawn-anchored to specific gamemode UFunctions):**

| Class            | mainGamemode launcher    | Notes                                              |
|------------------|--------------------------|----------------------------------------------------|
| `Aufo_ballfo_C`  | `mainGamemode.ufo_ball`  | "ball" UFO; spline-driven; spawns at radar pivot   |
| `Aufo_boofo_C`   | `mainGamemode.ufo_boo`   | "boo" UFO; spline trajectory with shockwave option |
| `Aufo_joel_C`    | `mainGamemode.ufo_joel`  | "joel" UFO; postprocess effect (dizzy material)    |
| `Aufo_pillfo_C`  | `mainGamemode.ufo_pill`  | "pillfo" UFO; pill-shaped, has `Play()` UFunction  |
| `Aufo_trifo_C`   | n/a (likely event-spawned) | "trifo" — 3-light spotlight UFO                  |
| `Amidasufotest_C`| `mainGamemode.ufo_midas` | midas UFO; save-actor; spline-driven               |
| `AHoelUfo_C`     | spawned by `AsuperFog_C.spawnUfo()` | rotating-meshes anchor; carries `AhoelUfoAttack_C` lightning attacks |
| `AskyUfo_C`      | save-actor; ticks; `timer1/timer2`  | high-altitude UFO; clampHeight                    |
| `AmorningUfo_C`  | overlap-driven (`Box`)              | morning-spawned UFO; `flyAway` flag, `Init` pos    |

**Delivery (payload-carrying) UFO classes:**

| Class                 | Drop payload field           | Special behaviour                                       |
|-----------------------|------------------------------|---------------------------------------------------------|
| `AufoDropper_C`       | `TSubclassOf<AActor> Drop`   | base — overlap-activated; Timeline_0 drops payload;     |
|                       |                              | `forceDeploy()`, `activated()`, `killKerfur()` UFunctions|
| `AufoDropper_body_C`  | inherits Drop                | body variant                                            |
| `AufoDropper_car_C`   | inherits Drop                | car variant                                             |
| `AufoDropper_pig_C`   | inherits Drop                | pig variant                                             |
| `AufoDropper_tank_C`  | inherits Drop                | tank variant                                            |
| `AufoAlienDropper_C`  | + `aliens: int32`, `alinens: TArray<AActor*>`, `spawnArir: bool` | drops alien NPCs; tracks count |
| `AufoAbducter_C`      | `abduct: AActor*`, `abducting: bool`, `Height: float`            | LIFTS a target (player/cow/etc) up via Timeline_0; tractor-beam (`volumeBeam`, `eff_ufobeam`) |
| `AufoDreamer_C`       | extends `Adreamer_C`         | dream-context UFO                                       |
| `Adream_ufo_C`        | extends `AdreamBase_C`       | full dream-room UFO scene with trees + rocks            |

### 2.4 Weather / anomaly events

| Class                       | mainGamemode launcher                | Behaviour                              |
|-----------------------------|--------------------------------------|----------------------------------------|
| `AredSkyEvent_C`            | `spawnRedSky()`                      | sets `isred`, full-sky red overlay     |
| `AblackFog_C`               | `spawnBlackFog()`                    | post-process fog with `spawnGhost()`   |
| `AbadSun_C`                 | `Spawn Bad Sun()` (note the space)   | sun anomaly; `siren()`, `Remove()`     |
| `AsuperFog_C`               | (BP-side; subclass of fog)           | + `spawnUfo()` (spawns HoelUfo)        |
| `AweatherFogController_C`   | (auto-spawned by daynightCycle)      | controls regular fog over time         |
| `AdaynightCycle_C`          | singleton on mainGamemode @ 0x0450   | day/night phase, weather flags, `isRaining`, `rain`, `SStar_active` |
| `AlakeglowEvent` → `Alakeglow_C` | mainGamemode.event_lakeglow @ 0x0C60 | lake-glow anomaly                    |

### 2.5 Day/Night + time-of-day events

`AdaynightCycle_C` exposes:
- `Day: float`, `Phase: float`, `MaxTime: float`, `totalTime: float`
- `timeZ: FIntVector` — discrete game-time (hour, minute, ?)
- `rain: float`, `isRaining: bool`, `SStar_active: bool`
- `diff: enum_difficulty::Type`

And `AtriggerTimer_C::newMinute(FIntVector Time)` fires **once per
game-minute** for every timer trigger on the map, gated by `Hour: int32`
and `invert: bool`. This is the **only built-in time-scheduling
primitive**, and it's per-trigger, not centralised.

### 2.6 Reputation-driven event spawning

`AariralRepEventHandler_C` lives on mainGamemode @ 0x0BC0 and runs:
```
ReceiveTick(DeltaSeconds):
    rep := calcRep()
    if rep != prevRep:
        launchEvent()
        prevRep = rep
```

`launchEvent()` is a BP function whose body is bytecode — it likely
inspects the rep delta sign and triggers one of: ariral-friendly events
(picnic, treehouse_build_N, peace) on positive rep crossings, or
ariral-hostile (combat, arirShip, fakeGrays, arirpicSignal, alienJump)
on negative.

### 2.7 Periodic spawners (`Aticker_*`)

Each `Aticker_*` is a `AActor` subclass of `Aticker_base_C` (which has
`GameMode` ptr and all the `intComs_*` hook surface from triggerBase).
They run their own `ReceiveTick` and roll RNG against game state to
decide if/where to spawn a thing.

Notable ones with direct event implications:
- `Aticker_fossilhoundSpawner_C` → spawns `Afossilhound_C` packs
- `Aticker_mannequinSpawner_C` → spawns `Aprop_mannequin_C`
- `Aticker_insomniacSpawner_C` → spawns `Ainsomniac_C` (the player-night-stalker)
- `Aticker_roachSummoner_C` → spawns roaches
- `Aticker_serverBreaker_C` → breaks a server (triggers a gameplay task)
- `Aticker_dishUncalib_C` → uncalibrates a dish
- `Aticker_flickerer_C` → flickers a light
- `Aticker_kerf_C` → kerfur-spawning logic
- `Aticker_susHoleSpawner_C` → spawns "sus hole" anomaly
- `Aticker_widgetRender_C`, `Aticker_paused_C`, `Aticker_tick_C` —
  pure-render-side, NOT gameplay events

Plus environmental ones not gameplay-critical: `treeSpawner`,
`bushSpawning`, `beehiveSpawner`, `bp7Spawner`, `deerSpawner`, `eyers`,
`fireflySpawner`, `foodHall`, `foodSleeper`, `foodTolerance`,
`hexahiveSpawner`, `surveilanceMaster`, `wispSpawner`,
`yellowWispSpawner`.

### 2.8 Gamemode-resident event UFunctions (mainGamemode)

These are non-trigger one-shot UFunctions that other code calls to fire
an event. They are the closest thing VOTV has to a "game event API":

| UFunction                       | Effect                                                          |
|---------------------------------|-----------------------------------------------------------------|
| `ufo_ball()`                    | spawn + start ballfo UFO flyby                                  |
| `ufo_joel()`                    | spawn joel UFO + screen-distortion postprocess                  |
| `ufo_boo()`                     | spawn boofo flyby                                               |
| `ufo_pill()`                    | spawn pillfo flyby                                              |
| `ufo_midas()`                   | spawn midas UFO                                                 |
| `spawnRedSky()`                 | instantiate `AredSkyEvent_C`                                    |
| `spawnBlackFog()`               | instantiate `AblackFog_C`                                       |
| `Spawn Bad Sun()`               | instantiate `AbadSun_C`                                         |
| `trySpawnInsomniac(canSpawn, Loc)` | RNG roll, if pass return spawn location                     |
| `processDream(customDream)`     | enter dream sequence; spawns `AdreamBase_C` subclass            |
| `createDream(customDream)`      | helper                                                          |
| `leaveDream()`                  | exit dream sequence                                             |
| `sleep(bed, dropItem, ignoreRagdoll)` | sleep cycle entry                                         |
| `wakeup()`                      | sleep cycle exit                                                |
| `theItemWasBuried(dirtHole, Actor)` | item-buried gameplay event (multicast)                      |
| `itemBuried(Actor, dirtHole)`   | helper for above                                                |
| `breakServer()`                 | break a random server (gameplay task)                           |
| `killedNisse()`                 | nisse-kill event                                                |
| `setJolly()`                    | jolly-mode toggle                                               |
| `Update Season(updateLandscape)`| season transition                                               |
| `setLake(WaterMaterial, waterPostProcess)` | lake-state change                                    |
| `setBloodLake(blood)`           | toggle blood-lake mode                                          |
| `setDryness(Add)`               | dryness anomaly                                                 |
| `transition(LevelName)`         | map transition                                                  |
| `addEmail(Item)`, `addAchievementPopup`, `Add Hint from Gamemode` | meta-events |
| `gameSaved` delegate            | fires after save                                                |
| `seasonUpdated` delegate        | fires on season transition                                      |
| `wokenUp` / `fellAsleep` / `sleepTriggered` delegates | sleep cycle stages              |
| `theItemWasBuried` delegate     | item-buried multicast                                           |
| `dishesStop`, `powerChanged`    | system events                                                   |

These are RPC-shape — caller fires, gamemode listeners react.

### 2.9 Other periodic UFunctions on mainGamemode itself

`mainGamemode.hpp` declares: `ticker_radiotowerPoof`, `ticker_lockerhead`,
`ticker_lakeMonsert`, `tickerBody`, `tickerFunguy`, `flowerSpawner`,
`gorelockertest`, `windParticles_check`, `eraseEmptyObjectsTimer`,
`chk1`, `checkATV`, `CustomEvent` × 10. These are timer-bound (set up in
ReceiveBeginPlay) and run periodically.

### 2.10 NPC visit / encounter events

These belong to the entity-sync surface but are conceptually events:

- `Anpc_krampus_C` — krampus visits the player at specific game-time +
  conditions. Has `Mode: enum_krampusMode::Type` (stab/run/etc.),
  `stabPlayer: AmainPlayer_C*`.
- `Afossilhound_C` — fossilhound packs; spawned by ticker; `chase` field.
- `Ainsomniac_C` — night-stalker; spawned by ticker_insomniacSpawner.
- `AkerfurOmega_alien_C` — alien-variant abducting kerfur.
- `Aprop_wAlien_C` / `Aprop_wAlien1/2_C` — wall-alien props (decorative
  spawn-events).
- `AgrayTest_C` — gray alien NPCs; managed by `AgrayEventController_C`
  (has `Spawn()`, `Despawn()`, `begin()`, `turnedOn()`, `deac()`,
  `BeginOverlap`; tracks `IsActive`, `grays: TArray<AgrayTest_C*>`,
  `transformer: Agenerator_C*`).
- `AalienJump_C` / `AalienJump_instantSpawn_C` — alien jump animation
  rig (likely cinematic stage props with timeline-driven mesh swaps).
- `AhoelUfoAttack_C` — lightning attacks fired by HoelUfo, has Target +
  Loc, lasts ~one BeginPlay→audio-finished cycle.

### 2.11 Sleep / dream events

`mainGamemode.sleep(bed, ...)` → `dreaming = true` →
`mainGamemode.processDream(customDream)` chooses dream class →
spawns `AdreamBase_C` subclass (`dream_burger`, `dream_climb`,
`dream_fill`, `dream_jump`, `dream_mann`, `dream_room`, `dream_run`,
`dream_ufo`, `dream_wend`, `dream_dreambase`, `dream_boulders`) →
player is teleported into the dream (`playerPreDream` saves pre-dream
transform) → `leaveDream` restores.

`AbedEvent_C` and `Aevent_arirTreehouseSleep_C` are sleep-context events
that fire DURING sleep.

### 2.12 Halloween / Christmas / seasonal

- `AhalloweenMaster_C` (mainGamemode.halloweenMaster @ 0x0720,
  `isHalloween: bool` @ 0x0728)
- `isChristmas: bool` @ 0x0F20, `isWinter: bool` @ 0x0EC8,
  `isSummer/isAutumn/isSpring: bool` @ 0x1178+, `april1st: bool` @ 0x11A8
- Triggered by `Update Season` + real-system-date check inside the BP

### 2.13 Master event-count

After dedup, distinct **gameplay events** in VOTV: **~80**.
Broken down:
- 36 named trigger_eventer slots
- 14 `Aevent_*` / `bedEvent` / `redSkyEvent` / `skyFallingEvent` classes
- 9 distinct UFO classes (5 flyby + 4 delivery + abducter + dreamer + dream_ufo)
- 5 anomaly weather (redSky, blackFog, badSun, superFog, weatherFog)
- ~20 ticker spawners (gameplay-relevant subset of 27 total tickers)
- 12 dream classes
- 5 gamemode-resident UFO launchers
- ~20 gamemode-resident event UFunctions (spawnRedSky, breakServer,
  killedNisse, Update Season, theItemWasBuried, setLake, setBloodLake,
  setJolly, setDryness, transition, addEmail, addAchievementPopup, …)

---

## §3. Section 3 — Per-event trigger source + determinism analysis

### 3.1 Trigger sources by category

| Trigger source                        | Determinism between peers | Examples                                                        |
|---------------------------------------|---------------------------|-----------------------------------------------------------------|
| **Player overlap (BeginOverlap)**     | Local — fires on whoever overlaps; deterministic IF same player overlaps | `AtriggerFallingSky_C`, all `Aevent_*` overlap-driven, ufoDropper, lightsTurnoffer |
| **Save-flag check on BeginPlay**      | Deterministic — both peers read same save | bedEvent, arirTreehouseSleep, event_arirFuelsAtv (Aactor_save_C state) |
| **`trigger_eventer.runEvent(FName)`** | Deterministic IF runEvent caller is replicated; otherwise DIVERGENT | most ariralRep-launched events, story progress events |
| **Per-tick RNG (ticker)**             | DIVERGENT — each peer rolls own RNG | fossilhoundSpawner, mannequinSpawner, insomniacSpawner, etc.    |
| **ariralRepEventHandler per-tick**    | DIVERGENT — `calcRep()` may read peer-local cached state | launchEvent (full event set) |
| **Game-time minute rollover**         | Deterministic IF gamemode time is replicated | triggerTimer.newMinute, daynightCycle phase events |
| **Sleep entry**                       | Deterministic — both peers know they slept | bed sleep, treehouse sleep, dream events |
| **Story progress (signal/dish/task)** | Deterministic IF prog state is replicated | computer task complete → unlock event |
| **Equipment use / hand action**       | Local to that player | playerHandUse_LMB on a trigger, padlock unlock, gascan refuel  |
| **Damage / explosion event**          | Triggered by damage source | exploded(), reachedByLightning, ImpactDamage, fireDamage       |
| **Real-system-date**                  | Deterministic IF both clients have same locale | isHalloween, isChristmas, april1st                            |

### 3.2 The five UFO flyby UFunctions on mainGamemode

`ufo_ball`, `ufo_joel`, `ufo_boo`, `ufo_pill`, `ufo_midas` — these are
explicit UFunction entry points. They are CALLED FROM something (either
ariralRepEventHandler.launchEvent, or a timer, or a dish-task complete).
**The caller matters** — if both peers run the caller independently,
both spawn the UFO independently → two UFOs visible. If only the host
runs it, only the host has the UFO → desync.

### 3.3 RNG sources in VOTV

VOTV's BP RNG goes through `UKismetMathLibrary::RandomFloat`,
`RandomInteger`, `RandomFloatInRange` — these are NOT seeded
deterministically across peers; each peer's `FRandomStream` is initialised
from system time at game start. Therefore **any event whose dispatch
depends on a runtime RNG roll WILL DIVERGE between peers** unless one
peer is authoritative.

### 3.4 Categorisation summary

- **Deterministic (both peers can run independently and converge):**
  bedEvent, arirTreehouseSleep, daynightCycle phase rollovers, season
  transitions, seasonal flags, story-progress triggers IF story state is
  replicated.
- **Host-authoritative needed:** all ticker spawners, ariralRepEventHandler,
  all 5 ufo_* flyby calls, spawnRedSky/spawnBlackFog/Spawn Bad Sun,
  trySpawnInsomniac, breakServer, processDream, the alien ship crash.
- **Local-OK (per-peer):** purely-cosmetic ambient sound, decoration
  ticker (treeSpawner, fireflySpawner, etc.), camera shake from a
  remote-driven event.

---

## §4. Section 4 — Per-event replication recommendations

Using the four-strategy taxonomy from the request:

- **A.** Host-only run, wire-broadcast event packet, client mirrors
- **B.** Both peers run independently (deterministic)
- **C.** Host-only run + already-covered-by-prop-lifecycle (Inc2 Aprop_C::Init POST observer)
- **D.** Host-only run + needs explicit script-state replication

### 4.1 Alien ship crash & adjacent

| Event                              | Strategy | Rationale                                                                      |
|------------------------------------|----------|--------------------------------------------------------------------------------|
| `AtriggerFallingSky_C` overlap     | **D**    | Triggered by host player overlap; must broadcast `EventStart(sky_fall, loc)` + replicate the `passEvents += "skyFall"` save mutation. Client doesn't run its own overlap — it gets told. |
| `AskyFallingEvent_C` trajectory    | **A**    | Host spawns; client receives `EventStart` and runs an identical visual-only `AskyFallingEvent_C` locally OR streams the actor's pose (heavier). Recommendation: just `EventStart(class_id, spawn_loc, start_velocity, seed)` and let client run the timeline locally — far cheaper than streaming a per-frame pose. |
| Wreckage props at impact           | **C**    | Wreckage is `Aprop_C` / `AburningDebris_C` — falls out via Inc2 Aprop_C::Init POST observer (already in scope for [[project-physics-object-pickup]] + npc-entity sync). |
| Alien NPCs spawned at impact       | **C**    | NPCs are `ACharacter`/`AActor` and would be covered by NPC sync (separate WP, see `votv-npc-entity-coop-architecture-2026-05-24.md`). |
| `passEvents += "skyFall"` save mutation | **D** | Save-flag replication. See §5.1.                                              |

### 4.2 UFO flybys

| Event                              | Strategy | Rationale                                                                      |
|------------------------------------|----------|--------------------------------------------------------------------------------|
| `ufo_ball/joel/boo/pill/midas`     | **A**    | Host calls the UFunction; broadcasts `EventStart(ufo_ball, spawn_loc, seed)`; client re-runs the UFunction locally with the same input. UFOs are spawned actors but their flight path is timeline-driven and not save-relevant; **don't waste bandwidth on per-frame pose** — just trigger then let client simulate. |
| `Aufo_joel_C` postprocess          | **A**    | Same packet starts the postprocess locally on the client.                      |
| `AmorningUfo_C` overlap            | **D**    | Overlap by player → host fires `EventStart(morning_ufo)`; client mirrors.      |
| `AskyUfo_C` per-tick               | **B**    | Save-actor, deterministic init; both peers can run it. Verify by diffing `rendered` state. |

### 4.3 UFO delivery (drop / abduct)

| Event                              | Strategy | Rationale                                                                      |
|------------------------------------|----------|--------------------------------------------------------------------------------|
| `AufoDropper_C` (body/car/pig/tank/alien) | **C** (delivery) + **A** (timeline) | Host runs the timeline; client receives `EventStart(ufo_dropper, class_id, loc)`. The DROPPED actor itself (the body/car/pig/tank/alien) is `Aprop_C`/`ACharacter`-derived and replicates via Inc2 Aprop_C::Init POST observer or NPC sync. |
| `AufoAbducter_C` lifting a target | **D**    | Lifting a target modifies the target's transform. Host must broadcast `AbductStart(target_key, ufo_loc)`. Client mirrors the lift on its puppet of the target. Save state may change (`passEvents += "abducted"`). |

### 4.4 Weather / anomaly

| Event                              | Strategy | Rationale                                                                      |
|------------------------------------|----------|--------------------------------------------------------------------------------|
| `spawnRedSky` → `AredSkyEvent_C`   | **A**    | Host fires; broadcast `EventStart(red_sky, set(isred=true))`; client mirrors.   |
| `spawnBlackFog` → `AblackFog_C`    | **A**    | Same.                                                                          |
| `Spawn Bad Sun` → `AbadSun_C`      | **A**    | Same. Includes `siren()`.                                                      |
| `AsuperFog_C` (+ `spawnUfo` → `AHoelUfo_C`) | **A** | Cascading: host fires superFog start; superFog spawns HoelUfo; HoelUfo flies via timeline. **Broadcast the OUTER event only.** Don't replicate each cascading spawn. Client re-runs the cascade locally. |
| `AweatherFogController_C`          | **B**    | Tied to daynightCycle.phase. Deterministic if daynight is replicated.          |
| `AdaynightCycle_C` phase/time      | **D**    | Time itself must be replicated as part of [[project-coop-foundation]] — see `mainGamemode.Time: FIntVector` @ 0x04E0. |
| `Alakeglow_C`                      | **A**    | Host-fires anomaly.                                                            |
| `setLake/setBloodLake`             | **A**    | Host fires; client mirrors via mirror UFunction call.                          |

### 4.5 Trigger-eventer named events (the 36 slots)

For all 36 `event_*` slots on `Atrigger_eventer_C`: **Strategy A** —
host's `trigger_eventer.runEvent(FName)` is the dispatch point.
Hook it. Broadcast `EventDispatch(slot_fname, special_fname)`. Client
mirrors by calling the same UFunction on its own `trigger_eventer` instance.
The sub-trigger's `runTrigger(Owner, Index)` does all the work locally.
Special handling needed only when the sub-trigger spawns physics props or
NPCs (those replicate via prop / NPC sync — strategy C).

### 4.6 Tickers (periodic spawners)

For all gameplay tickers: **Strategy A** (host-authoritative).
Disable client-side ticker actors at session start (or skip their
ReceiveTick on the client via a peer-role check). All spawning happens
on host; the spawned actors propagate via the prop / NPC sync layer (C).
Exception: cosmetic-only tickers (`treeSpawner`, `bushSpawning`,
`fireflySpawner`, `widgetRender`, `paused`, `tick`, `surveilanceMaster`)
can be **B** — both peers run them independently for visual fill;
divergence between two peers' tree distribution is invisible at gameplay
level. **Verify each ticker's actual gameplay impact before declaring it B.**

### 4.7 Sleep / dream events

| Event                              | Strategy | Rationale                                                                      |
|------------------------------------|----------|--------------------------------------------------------------------------------|
| `mainGamemode.sleep(...)`          | **D**    | Sleep is a SHARED state — both peers must enter sleep simultaneously, or one is dreaming while the other is awake. Critical UX issue. Host fires sleep; client mirrors. Same for `wakeup`. |
| `processDream(customDream)`        | **A**    | Host picks dream class via RNG; broadcasts `DreamStart(class_id, seed)`; client spawns same dream class locally. |
| `AbedEvent_C.woken`                | **A**    | Linked to sleep; host fires after sleep timer expires.                         |

### 4.8 ariralRepEventHandler

| Event                              | Strategy | Rationale                                                                      |
|------------------------------------|----------|--------------------------------------------------------------------------------|
| `AariralRepEventHandler_C.launchEvent()` | **A** | Host's tick runs `calcRep` → `launchEvent`. Disable client-side handler's tick (skip ReceiveTick). Host broadcasts the chosen event as a single packet. |

### 4.9 Story progress + meta events

| Event                              | Strategy | Rationale                                                                      |
|------------------------------------|----------|--------------------------------------------------------------------------------|
| `breakServer`                      | **D**    | Mutates server state (`brokenServers`); host fires; client mirrors + save replicates. |
| `theItemWasBuried` delegate        | **A**    | Multicast event; host broadcasts; client raises the same delegate locally.      |
| `killedNisse`                      | **A**    | Host fires; client mirrors.                                                    |
| `setJolly`                         | **A**    | Same.                                                                          |
| `Update Season`                    | **B**    | Save-driven season transition; both peers can compute it if `mainGamemode.Time` is replicated. |
| `transition(LevelName)`            | **D**    | Map change — both peers must transition together. Critical sync point.          |
| `addEmail`, `addAchievementPopup`, `Add Hint from Gamemode` | **A** | Per-player UI events; host broadcasts; each peer raises locally for itself. |

---

## §5. Section 5 — Proposed wire shape

### 5.1 Recommendation: ONE unified `EntityEventPacket` + a separate `SaveStateDeltaPacket`

After surveying the event surface, the recommendation is a **single
unified `EntityEventPacket`** for the event firing itself, plus a
**separate `SaveStateDeltaPacket`** for the persistent-state mutations
that events cause. Justification follows.

### 5.2 `EntityEventPacket` — unified event firing

```cpp
// proposed: src/votv-coop/include/coop/net/protocol.h additions
struct EntityEventPacket {
    uint8  kPacketType = PKT_EVENT;
    uint32 sessionToken;     // session anti-spoof
    uint32 seqId;            // RFC1982 seq
    uint32 eventClassWireId; // FNV-1a("AredSkyEvent_C") or similar registry id
    uint32 eventNameWireId;  // FNV-1a(FName), e.g. for runEvent FName parameter
    FVector spawnLoc;        // (NaN allowed = "no location")
    FRotator spawnRot;       // (NaN allowed)
    uint64 seed;             // RNG seed for client-side determinism
    uint32 targetKeyHash;    // FName.string hash if event acts on a target (abduct, ufoDropper)
    uint8  flags;            // 1=start, 2=end, 4=force-cleanup
    uint16 paramBlobLen;     // <= 128 bytes
    uint8  paramBlob[];      // per-event-class struct, declared in a class-id-to-layout registry
};
```

- **eventClassWireId** is a stable FNV-1a hash of the BP class name
  (e.g. `"AredSkyEvent_C" → 0xC4F5...`). Host and client share a
  registry built at startup. Same approach as the WireKey design in
  `votv-npc-entity-coop-architecture-2026-05-24.md`.
- **eventNameWireId** is for FName parameters (e.g. `runEvent("event_arirShip")`).
  Same hashing scheme.
- **seed** lets the client re-run client-side RNG for the same outcomes
  (deterministic VFX, deterministic ariral-thrower selection, etc.).
- **paramBlob** carries event-specific small extras: `set(isred=bool)`
  for redSky, `dream_class_id` for dream, etc. Strictly bounded to
  ≤ 128 bytes; longer state goes through a separate reliable channel.

**Channel:** reliable ordered (existing chat/quit-reliable channel from
[[project-coop-chat-feed]] / [[project-phase3-udp-transport]]). Event
fires are rare (~one per 10s peak) and must not be dropped.

### 5.3 `SaveStateDeltaPacket` — save mutations from events

```cpp
struct SaveStateDeltaPacket {
    uint8  kPacketType = PKT_SAVE_DELTA;
    uint32 sessionToken;
    uint32 seqId;
    uint8  deltaType;        // 1=passEvents.Add, 2=passEvents.Remove,
                             // 3=specials.Add, 4=forceObjects.Add,
                             // 5=foundFish.Add, 6=foundTreasures.Add,
                             // 7=advancement.Set(name, completed),
                             // 8=allEvents.Add, 9=storeItems.Add,
                             // 10=tutLvls.Add, 11=lastPlayed.Add
    uint32 keyHash;          // FName hash
    uint8  valueByte;        // for boolean deltas, the value
    uint16 stringLen;        // for FString-typed values
    uint8  stringData[];
};
```

This carries the persistent ledger mutations (`passEvents +=`,
`advancements += {name, true}`, etc.) so the client's save matches the
host's after an event. Without this, on next load the client re-fires
events the host already fired.

### 5.4 Trade-off discussion: unified vs per-event-class packets

**Why one packet (recommended):**
- Event firing is RARE (~1/10s peak). Per-event-class packets would
  bloat the protocol with ~80 packet types for ~10 packets/min traffic.
- A single eventClassWireId registry is easy to verify (one table, one
  lookup) vs 80 separate decoders.
- The `paramBlob` lets event-class-specific layouts evolve without
  protocol breaking changes.
- Mirrors the MTA `CElementRPCs` pattern: one RPC family with a verb id
  rather than per-element-type packets (see
  `reference/mtasa-blue/Server/mods/deathmatch/logic/CElementRPCs.cpp`).

**Why per-event-class (rejected):**
- Each packet would be smaller without paramBlob length-prefix overhead.
- But: ~80 distinct packet types × per-type registration + decoder is
  more code (RULE №2 baggage); the savings are < 10 bytes/packet at
  1 packet/10s.

**Save deltas are SEPARATE** because:
- They are state-replicating, not event-firing — different semantics.
- They may fire WITHOUT an event (e.g. host's save autosaves a field
  the client also needs).
- Different anti-cheat / validation surface (only the host should
  emit these).
- They can be batched / coalesced (collect multiple deltas over 1s,
  send as one packet).

### 5.5 What the wire packet does NOT carry

- **Spawned actor identities.** Props/NPCs spawned by an event are
  replicated by their own systems (Inc2 Aprop_C::Init POST observer for
  props, NPC manifest sync for NPCs). Per the architecture in
  `votv-npc-entity-coop-architecture-2026-05-24.md`, this is the
  "PropSpawn" event flow.
- **Per-frame UFO pose.** The UFO flies on a timeline locally — the
  start packet has loc+rot+seed, that's enough.
- **VFX particles / sound.** Both peers fire VFX/SFX locally from the
  same EventStart packet — they don't need pose tracking.

---

## §6. Section 6 — Integration with Inc1/Inc2 prop lifecycle

### 6.1 What's "free" via Inc2 PropSpawn

Per `votv-aprop-lifecycle-RE-2026-05-24.md`, our existing Inc2 hook on
`Aprop_C::Init POST` already broadcasts a PropSpawn for any
`Aprop_C`-derived actor that comes into existence on the host. **That
means any event whose visible payload is just `Aprop_C` props gets the
spawn replication for free.**

Events that ONLY spawn props:
- `Aevent_trashPiles_C` — trash props
- `Aevent_funnyGascans_C` — gascan props
- `Aevent_fleshRain_C` — fleshy props (assuming `Aprop_C`-derived; verify)
- `AufoDropper_body_C` payload (body prop)
- `AufoDropper_car_C` payload (car... but `AATV_C` may not be `Aprop_C`-derived — verify)
- `AskyFallingEvent_C` wreckage debris (if `AburningDebris_C` is `Aprop_C`-derived; verify)
- Most trigger_eventer slots that just place items
- ariralThrowStuff `throw()` (`throwFoods()`, `throwFoods_good()`) —
  spawns food props

**For these, the only wire is the EventStart packet for VFX/sound; the
prop spawn itself is auto-replicated by PropSpawn.**

### 6.2 What's NOT covered — events that need explicit replication

These events DON'T just spawn `Aprop_C`s; they need EventStart + extra:

| Event                              | Why PropSpawn doesn't cover it                                 |
|------------------------------------|---------------------------------------------------------------|
| All weather (`redSky`, `blackFog`, `badSun`, `superFog`) | Post-process / sky materials, not props |
| All UFO flybys (ufo_ball/joel/boo/pill/midas) | `Aufo_*_C` are `AActor`-derived, not `Aprop_C` |
| Abduction (`AufoAbducter_C`)       | Target lift modifies an existing actor's transform, not a spawn |
| Dream entry (`processDream`)       | Teleports the player; spawns dream BP (not Aprop_C)            |
| Sleep cycle (`sleep`/`wakeup`)     | Pure state mutation                                            |
| Map transition (`transition`)      | Triggers level change                                          |
| Save-state mutations (`passEvents`, `specials`, achievements) | Pure data, no actor |
| Day/night phase rollovers          | Daynight cycle state, not a spawn                              |
| Camera shake / screen distortion (joel ufo postprocess) | UI/PP, not actor |
| Sound-only events (forestMoan, bigmRoar, etc.) | No actor                                       |
| NPC spawns (alienDropper, fossilhoundSpawner, etc.) | NPCs are `ACharacter`/`AActor`, covered by NPC sync (separate WP) |

### 6.3 Integration boundary

```
       [Host event fires]
            |
            v
   +--------+----------+
   |                   |
   v                   v
PropSpawn        EntityEventPacket
(Inc2 obs)       (new, this RE)
spawns Aprop_C   wire-broadcasts the event itself
actors           (VFX, sound, weather state, target hits)
   |                   |
   |                   v
   |             [Client receives]
   |                   |
   |                   v
   |             handler dispatches by eventClassWireId
   |                   |
   |             +-----+----------+
   |             |                |
   v             v                v
[Aprop_C       [Spawn local      [Mutate
auto-spawned   AskyFallingEvent_C  daynightCycle /
on client]     locally, etc.]    redSky / etc.]
```

The two layers are **complementary** — neither fully covers the other.
Most events trigger both: spawn some props (PropSpawn handles that) AND
do something else (EventStart handles that).

---

## §7. Section 7 — IDA renames + IDB save

Native-side surface is minimal because the entire event system is
BP-driven. The .exe contains:

- `UObject_ProcessEvent` @ `0x141465930` — already renamed; **annotated
  in this session** with a comment summarising the BP event dispatch.
- `ProcessEvent_NetCheckedWrapper_maybe` @ `0x1428d4530` — UE engine
  wrapper.
- No other native event-system functions (no `runEvent`, no `launchEvent`,
  no UFO names, no scheduler).

**Verified by string search:** zero hits for `ufo`, `alienShip`, `crashSite`,
`runEvent`, `launchEvent`, `spawnRedSky`, `eventer`, `ariralRepHandler`,
`triggerEventer`, `trigger_eventer`, `eventManager`, `Aevent_`, etc.

**IDA action taken:**
- Comment added to `0x141465930` (`UObject_ProcessEvent`) summarising the
  event dispatch surface (run by all event UFunctions from §1–4 above
  through this single fn).

**IDB saved:** `D:\Projects\Programming\VOTV_MP\Game_0.9.0n\WindowsNoEditor\VotV\Binaries\Win64\VotV-Win64-Shipping.exe.i64`
(via `idb_save`).

No new function renames in this RE because the entire event system is
BP bytecode interpreted through one already-named native fn; renaming
the BP UFunction stubs is not productive (they live as `UFunction`
objects in GUObjectArray at runtime, not as exe symbols). Future event
hooks should use `UFunction::FindFunctionChecked(FName)` from the
mod side at runtime, not native exe-symbol resolution.

---

## §8. Section 8 — Open questions / followups

These are not blockers for the design but should be resolved before
implementation lands:

1. **What's the exact caller of each `mainGamemode.ufo_*` UFunction?**
   - Likely candidate: `ariralRepEventHandler.launchEvent` for some, and
     fixed timers for others. Confirm via Lua probe at runtime, dumping
     the callsite stack when `ufo_ball/joel/...` fires.
2. **Verify `AburningDebris_C` and `AfallingBody_C` are `Aprop_C`-derived**
   — if yes, alien-ship-crash wreckage replicates via PropSpawn for free.
   If no, they need explicit Inc1/Inc2-style observers added.
3. **Confirm `Aevent_fleshRain_C` spawned things are `Aprop_C`** — same
   reason.
4. **Determine the exact set of FName values used in
   `passEvents`** so the save-delta packet can validate them
   (anti-cheat: only accept known event names).
5. **Map `Atrigger_eventer_C.eventSpawner_tags` FName values** — these
   are the spawn-pivot names that BP-side passes when calling
   `getTagTransform(Tag)`. Needed for accurate target-loc resolution
   in the wire packet.
6. **Day-night cycle replication** — `mainGamemode.Time: FIntVector` @
   0x04E0 needs streaming, otherwise daynight-bound events diverge.
   This is a separate coop-foundation work item; flag in
   [[project-coop-foundation]].
7. **Sleep cycle UX** — when host sleeps, does client get put to sleep
   too? Or does client keep playing while host's screen fades? Major UX
   call; affects whether `sleep` is strategy D (synchronous-sleep) or
   A (host-sleeps-only-affects-host's-save).

---

## Appendix A — Quick file index

All files under `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/`:

**Event dispatchers:**
- `trigger_eventer.hpp`  — master dispatcher (36 slots)
- `triggerBase.hpp`      — base class (133 lines!)
- `triggerTimer.hpp`     — time-of-day trigger
- `ariralRepEventHandler.hpp` — per-tick rep watcher

**Direct event classes:**
- `event_arirFuelsAtv.hpp`, `event_arirStealsGun.hpp`,
  `event_arirTreehouseSleep.hpp`, `event_bottomHoleController.hpp`,
  `event_consoleWrite.hpp`, `event_fleshRain.hpp`,
  `event_fossilBoarWar.hpp`, `event_funnyGascans.hpp`,
  `event_lightsTurnoffer.hpp`, `event_passwordGuesser.hpp`,
  `event_trashPiles.hpp`, `event_vaccine.hpp`
- `bedEvent.hpp`, `redSkyEvent.hpp`, `skyFallingEvent.hpp`,
  `triggerFallingSky.hpp`

**Concrete trigger_eventer slot subclasses (selected):**
- `trigger_eventt_arirShip.hpp` (note: doubled-t in source),
  `trigger_solarBoom.hpp`, `trigger_agrav.hpp`, `trigger_bigmRoar.hpp`,
  `trigger_wispSwarm.hpp`, `trigger_spawnFollowingArir.hpp`,
  `trigger_arirEgg.hpp`, `trigger_bedEvent.hpp`,
  `trigger_jamDoor.hpp`, `trigger_alarm.hpp`, `trigger_notif.hpp`,
  `trigger_achievement.hpp`, `trigger_bloodSkeleton.hpp`,
  `trigger_TBoxActivator.hpp`, `trigger_breakDish.hpp`,
  `trigger_destroyByKeys.hpp`, `trigger_destroyInRadius.hpp`,
  `trigger_forceObject.hpp`, `trigger_lockerLooker.hpp`,
  `trigger_fakeLmaos.hpp`, `trigger_vehtp.hpp`,
  `trigger_teleporter.hpp`, `trigger_tpChamberSpawn.hpp`

**UFO classes:**
- `ufo_ballfo.hpp`, `ufo_boofo.hpp`, `ufo_joel.hpp`, `ufo_pillfo.hpp`,
  `ufo_trifo.hpp`, `midasufotest.hpp`, `HoelUfo.hpp`, `skyUfo.hpp`,
  `morningUfo.hpp`, `dream_ufo.hpp`
- `ufoDropper.hpp`, `ufoDropper_body.hpp`, `ufoDropper_car.hpp`,
  `ufoDropper_pig.hpp`, `ufoDropper_tank.hpp`,
  `ufoAlienDropper.hpp`, `ufoAbducter.hpp`, `ufoDreamer.hpp`,
  `hoelUfoAttack.hpp`

**Weather / anomaly:**
- `blackFog.hpp`, `badSun.hpp`, `superFog.hpp`,
  `weatherFogController.hpp`, `daynightCycle.hpp`

**NPC encounter / spawn classes:**
- `npc_krampus.hpp`, `fossilhound.hpp`, `grayTest.hpp`,
  `grayEventController.hpp`, `alienJump.hpp`,
  `alienJump_instantSpawn.hpp`, `kerfurOmega_alien.hpp`,
  `prop_kerfurOmega_alien.hpp`, `prop_wAlien.hpp/wAlien1/wAlien2.hpp`

**Tickers (selected gameplay-relevant):**
- `ticker_base.hpp`, `ticker_fossilhoundSpawner.hpp`,
  `ticker_mannequinSpawner.hpp`, `ticker_insomniacSpawner.hpp`,
  `ticker_kerf.hpp`, `ticker_roachSummoner.hpp`,
  `ticker_serverBreaker.hpp`, `ticker_susHoleSpawner.hpp`,
  `ticker_dishUncalib.hpp`, `ticker_flickerer.hpp`

**Save / data structures:**
- `mainGamemode.hpp` (564 lines — the gamemode actor with all event APIs)
- `mainGameInstance.hpp`, `saveSlot.hpp`, `save_main.hpp`
- `struct_event.hpp` (10 lines — FIntVector time + FName specialTrigger)
- `struct_triggerSave.hpp` (25 lines — per-trigger persisted state)
- `struct_multiSave.hpp`, `struct_multiSave_triggers.hpp`,
  `struct_multiSave_primitive.hpp`

**Spawn primitives:**
- `eventSpawnerPivot.hpp` (FName tag + scene root — placement helper)
- `fallingBody.hpp`, `burningDebris.hpp`

---

## Appendix B — `mainGamemode.hpp` event-relevant offsets reference

(For native-side reflection access if a hook needs to read/write these.)

| Field                                       | Offset   | Type                                |
|---------------------------------------------|----------|-------------------------------------|
| `eventer` (Atrigger_eventer_C*)             | 0x04D8   | AActor*                             |
| `Time` (game time IntVector)                | 0x04E0   | FIntVector                          |
| `isSleep`                                   | 0x04EC   | bool                                |
| `daynightCycle`                             | 0x0450   | AdaynightCycle_C*                   |
| `playerInterface`                           | 0x0328   | Uui_UI_C*                           |
| `Console`                                   | 0x03A0   | Uui_console_C*                      |
| `eventsActive` (TArray<bool>)               | 0x0730   | TArray<bool>                        |
| `activeEvents` (int32)                      | 0x0E68   | int32                               |
| `activeEvents_senders` (TArray<UObject*>)   | 0x0E70   | TArray<UObject*>                    |
| `disableEventTrack` (int32)                 | 0x0E98   | int32                               |
| `redSky` (AredSkyEvent_C*)                  | 0x0888   | AActor*                             |
| `blackFog` (AblackFog_C*)                   | 0x0880   | AActor*                             |
| `badSun` (AbadSun_C*)                       | 0x0AD8   | AActor*                             |
| `ariralRepHandler`                          | 0x0BC0   | AariralRepEventHandler_C*           |
| `halloweenMaster`                           | 0x0720   | AhalloweenMaster_C*                 |
| `isHalloween` / `isChristmas` / `isWinter`  | 0x0728 / 0x0F20 / 0x0EC8 | bool                    |
| `april1st`                                  | 0x11A8   | bool                                |
| `currentSeason`                             | 0x117B   | TEnumAsByte<enum_seasons::Type>     |
| `event_lakeglow`                            | 0x0C60   | Alakeglow_C*                        |
| `undergroundSpawnerMaster`                  | 0x0C68   | AundergroundGarbageSpawner_C*       |
| `dreaming`                                  | 0x0568   | bool                                |
| `playerPreDream`                            | 0x0570   | FTransform                          |
| `dream` (AdreamBase_C*)                     | 0x05A0   | AActor*                             |
| `bed`                                       | 0x05A8   | Abed_C*                             |
| `apoc`                                      | 0x0610   | bool                                |
| `virus`                                     | 0x0790   | bool                                |
| `dreamProbability`                          | 0x1030   | float                               |
| `nightmaresEnabled`                         | 0x1260   | bool                                |
| `backroomsEnabled` / `backroomsPass`        | 0x08D9 / 0x0C48 | bool                         |
| `bloodLake`                                 | 0x0F78   | bool                                |
| `isFlying`                                  | 0x08A4   | bool                                |
| `mainPlayer`                                | 0x0630   | AmainPlayer_C*                      |
| `sleepingPawn`                              | 0x1258   | AplayerSleepingPawn_C*              |

These are the **state surfaces an event mutates**. The save-delta packet
needs to mirror the booleans/Time/dreaming/apoc/virus fields between
host and client; the actor-pointer fields are populated locally per-peer
(client has its own redSky/blackFog/etc. instances).

---

**End of findings.**
