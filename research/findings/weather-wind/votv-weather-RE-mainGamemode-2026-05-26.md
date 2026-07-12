# RE: VOTV Weather Subsystem (mainGamemode + adjacent)

**Date**: 2026-05-26
**Target**: Alpha 0.9.0-n
**Method**: CXXHeaderDump static analysis ONLY. Files read:
`mainGamemode.hpp`, `daynightCycle.hpp`, `newsky.hpp`, `weatherFogController.hpp`,
`event_fleshRain.hpp`, `directionalWind.hpp`, `blackFog.hpp`, `lightningStrike.hpp`,
`tool_rain.hpp`, `redSkyEvent.hpp`, `badSun.hpp`, `outsideChurch.hpp`,
`struct_gameRules.hpp`, `struct_save.hpp`, `struct_settings1.hpp`, `saveSlot.hpp`,
`save_main.hpp`, `mainGameInstance.hpp`, `int_coms.hpp`, `ambientLightCurve.hpp`,
`trigger_eventer.hpp`, `halloweenMaster.hpp`, `mainPlayer.hpp`, `pryingCrowbar.hpp`,
`enum_seasons_enums.hpp`, `enum_difficulty_enums.hpp`, `Engine.hpp` (EHF only).
No IDA, no UE4SS Lua. No code modified.

---

## TL;DR

1. **`AmainGamemode_C` itself contains NO weather state fields.** Searching its
   564-line dump for any of `weather`/`rain`/`storm`/`precip`/`cloud`/`atmosphere`
   yields zero property matches. Closest fields are `bool isWinter @0x0EC8`,
   `bool isSnowyFootsteps @0x0D90` (a footstep-audio flag, not weather), and a
   pointer `AblackFog_C* blackFog @0x0880`. `mainGamemode` is the **owner of
   pointers to sub-systems** (sky, ToD, fog, wind, blackfog, redSky) but is
   not the canonical weather state holder.
2. **The canonical weather authority is `AdaynightCycle_C`** (`daynightCycle.hpp`,
   162 lines). It owns the rain particle component, the rain audio, the sun/moon
   directional lights, the SkyLight named `lightning`, the
   `UExponentialHeightFogComponent`, and ALL the rain/fog scheduling fields
   (`rain`, `isRaining`, `rainStrength`, `rainLightningChance`,
   `rainDeactivateChance`, `rainWindSpeed`, `rainToggle`, `rainProbability`,
   `fogProbability`, `permanentRain`, `permanentFog`, `thickFog`,
   `cloudsTimer`, `finalFogDensity`, `enable_fog`, `enable_superfog`,
   `enable_rain`, `isSnow`). `mainGamemode` reaches it via the pointer
   `AdaynightCycle_C* daynightCycle @0x0450`.
3. **`AweatherFogController_C`** is the temporary-fog *event* actor spawned by
   `daynightCycle::fogEvent()` / `spawnFog()`. It is a 0x250-byte fog-event
   object with `Alpha`, `Duration`, `fogPhase`, `permanent`, `permafog`,
   `Strength`. NOT the main fog (the main fog is the EHF component on
   `daynightCycle`); this is a *transient* fog spawn. There is at most one
   live at a time, referenced from `daynightCycle::fogEventObject @0x0338`.
4. **`Anewsky_C`** is the sky/sun visual actor (0x2F1 bytes). It owns the
   sky dome static mesh, sky/sun color curves, sun and moon intensity, and
   the `sunHidden`/`skyHidden`/`Eye` toggle bools. It does NOT own
   precipitation state. It is driven by `daynightCycle` (which holds it via
   `skysphere @0x02B8`) — `newsky::Init`/`upd`/`tp`/`setEye`/
   `setHiddenSuns`/`skyVisibility`/`setMoonPhase` are the mutator surface.
5. **`Aevent_fleshRain_C`** is a 0x254-byte stub actor inheriting
   `Aactor_save_C`. It contains a single `int32 I @0x0250` and two functions:
   `ReceiveBeginPlay` and `ExecuteUbergraph_event_fleshRain`. All logic is in
   the ubergraph (BP-only). The CXX dump alone cannot tell what it spawns or
   how it relates to rain. By naming it is a story event (cf. the
   "flesh"-themed Halloween / Akrobon events), NOT a generic rain variant.
6. **Weather state is RUNTIME-ONLY; nothing in the save**. Neither
   `saveSlot.hpp`, nor `save_main.hpp`, nor `mainGameInstance.hpp`, nor
   `struct_save.hpp` contains any field matching weather keywords. The ONLY
   weather-related save data is in `Fstruct_gameRules` (game rules):
   `permanentFog`, `permanentRain` (both bools). These are user-set rule
   toggles, not the live weather state. **On save→load the rain timers
   restart from probability rolls; the in-progress storm is not preserved.**
7. **No discrete weather enum.** The two relevant enums are
   `enum_difficulty` (5 values, affects `diff_mult` which scales rain) and
   `enum_seasons` (4 values: NewEnumerator0..3 — likely Winter/Spring/
   Summer/Autumn). Rain state is float (`rain`, `rainStrength`) + bool
   (`isRaining`); fog is float (`thickFog`, `finalFogDensity`, `Alpha` on
   the event); snow is bool (`isSnow`). There is no `EWeatherState` enum.
8. **Lightning strikes are discrete actors** (`AlightningStrike_C`, 0x280
   bytes). Spawned by `daynightCycle::timerLightning()` and
   `outsideChurch_C::lightningStrike()` / `truckLightning()` /
   `init_lightningStorm(Time)`. Each strike is a self-destructing actor with
   a point light + particles + 3 audio components. Sync model: discrete
   spawn event (location + tag + radius), no continuous state.
9. **Wind is driven by `AdirectionalWind_C`** (single instance pointed-to by
   `mainGamemode::directionalWind @0x0F70`). Its `setParameters(Intensity,
   Angle, Speed, Strength)` is called by `daynightCycle::setWindParameters`.
   It is whole-map and authoritative (the `tickable` flag at 0x02A0 + the
   `winds[]` TArray suggest it streams wind per-leaf-spawner). Wind state
   IS derivable from `(angle, speed, strength)` triple — small enough to
   ride on the `WeatherState` packet.

---

## Section 1 — `mainGamemode.hpp`: weather-relevant fields and functions

### 1.1 Direct weather property matches in `AmainGamemode_C`

Grep for `weather|rain|storm|fog|snow|wind|sky|lightning|thunder|precip|cloud|atmosphere`
across the full 564-line dump returns ONLY the following property hits
(non-pointer):

```
mainGamemode.hpp:124:    TArray<bool> eventsActive;                                                        // 0x0730 (size: 0x10)
mainGamemode.hpp:253:    bool isSnowyFootsteps;                                                            // 0x0D90 (size: 0x1)
mainGamemode.hpp:254:    bool isSnowyFootsteps_0;                                                          // 0x0D91 (size: 0x1)
mainGamemode.hpp:279:    bool isWinter;                                                                    // 0x0EC8 (size: 0x1)
mainGamemode.hpp:281:    bool isChristmas;                                                                 // 0x0F20 (size: 0x1)
mainGamemode.hpp:330:    bool isSummer;                                                                    // 0x1178 (size: 0x1)
mainGamemode.hpp:331:    bool isAutumn;                                                                    // 0x1179 (size: 0x1)
mainGamemode.hpp:332:    bool isSpring;                                                                    // 0x117A (size: 0x1)
mainGamemode.hpp:333:    TEnumAsByte<enum_seasons::Type> currentSeason;                                    // 0x117B (size: 0x1)
```

These are **season / calendar** flags, NOT weather. `isSnowyFootsteps` is a
footstep-audio flag set by `intComs_triggerSnow(bool)` (see Section 6 — the
snow signal is routed through this interface call, but the bool here merely
flips the *player's footstep audio*, not weather rendering).

### 1.2 Weather-subsystem pointer fields owned by `AmainGamemode_C`

These are the handles `mainGamemode` keeps to the actual weather actors:

```
mainGamemode.hpp:62 (line 62 of dump):
    class AdaynightCycle_C* daynightCycle;                                            // 0x0450 (size: 0x8)
mainGamemode.hpp:149:
    class AblackFog_C* blackFog;                                                      // 0x0880 (size: 0x8)
mainGamemode.hpp:150:
    class AredSkyEvent_C* redSky;                                                     // 0x0888 (size: 0x8)
mainGamemode.hpp:200:
    class AbadSun_C* badSun;                                                          // 0x0AD8 (size: 0x8)
mainGamemode.hpp:236:
    class Alakeglow_C* event_lakeglow;                                                // 0x0C60 (size: 0x8)
mainGamemode.hpp:290:
    class AdirectionalWind_C* directionalWind;                                        // 0x0F70 (size: 0x8)
```

There is NO `weatherController` / `weatherManager` / `AweatherFogController_C*`
pointer on `mainGamemode` — the temporary fog event lives on
`daynightCycle::fogEventObject @0x0338` (Section 2).

### 1.3 Weather-related UFunctions on `AmainGamemode_C`

```
mainGamemode.hpp:429:    void spawnBlackFog();
mainGamemode.hpp:428:    void spawnRedSky();
mainGamemode.hpp:417:    void Spawn Bad Sun();
mainGamemode.hpp:488:    void intComs_triggerSnow(bool isSnow);
mainGamemode.hpp:514:    void playWtr(FString NewParam);
mainGamemode.hpp:525:    void windParticles_check();
```

- `spawnBlackFog()` — spawns the `AblackFog_C` actor, BP body. Story
  event; the actor itself (Section 7) has a `bool M @0x024C` modifier and
  a `set()` function, no internal scheduling state.
- `spawnRedSky()` — spawns `AredSkyEvent_C` (Section 7): 17-line stub with
  `bool isred @0x0230` + `set(bool isred)`.
- `Spawn Bad Sun` — spawns `AbadSun_C` (Section 7): the "second sun"
  event with a timeline `A`, `bool endSiren @0x0270`, `bool Super @0x0278`.
- `intComs_triggerSnow(bool isSnow)` — interface call dispatched through
  the `Iint_coms_C` interface (Section 6). Both `daynightCycle` and
  `halloweenMaster` implement it.
- `playWtr(FString NewParam)` — grep across the CXXHeaderDump shows this is
  ONLY defined here (no implementations elsewhere). The `NewParam:FString`
  signature and `Wtr` abbreviation suggest "play weather (sound)" or
  "play wAter [audio]" — without IDA the disambiguation is unclear. Body
  is in `ExecuteUbergraph_mainGamemode`. **Open question for Phase 2.**
- `windParticles_check()` — single-arg-less UFunction. Wind audit hook.

### 1.4 Cross-references in `mainGamemode.hpp`

`mainGamemode` does NOT call `weatherFogController_C` functions directly
from any UFunction signature visible in the .hpp — the dump is a header,
so function bodies (which would show callers) are not in scope here.
What IS visible: the **field types**, which tell us the *owner* graph:

- `daynightCycle @0x0450` — owned by mainGamemode, references newsky via
  `skysphere @0x02B8` (daynightCycle.hpp:29) and references the fog event
  via `fogEventObject @0x0338` (daynightCycle.hpp:49). So the ownership
  chain is: `mainGamemode → daynightCycle → newsky` AND
  `mainGamemode → daynightCycle → weatherFogController` AND
  `mainGamemode → directionalWind` (parallel, not under daynightCycle).
- `eventsActive @0x0730` (`TArray<bool>`) — indexed list of bool flags for
  the global event slot system. Some weather-driving events likely set
  bits here.
- `activeEvents @0x0E68` (int32) and `activeEvents_senders @0x0E70`
  (`TArray<UObject*>`) — a separate count + sender list.

---

## Section 2 — `weatherFogController.hpp` (full layout)

File: `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/weatherFogController.hpp`
(22 lines).

```
weatherFogController.hpp:4:  class AweatherFogController_C : public AActor
weatherFogController.hpp:6:      FPointerToUberGraphFrame UberGraphFrame;                                          // 0x0220 (size: 0x8)
weatherFogController.hpp:7:      class USceneComponent* DefaultSceneRoot;                                          // 0x0228 (size: 0x8)
weatherFogController.hpp:8:      class AdaynightCycle_C* cyc;                                                      // 0x0230 (size: 0x8)
weatherFogController.hpp:9:      float Time;                                                                       // 0x0238 (size: 0x4)
weatherFogController.hpp:10:     float Alpha;                                                                      // 0x023C (size: 0x4)
weatherFogController.hpp:11:     float Duration;                                                                   // 0x0240 (size: 0x4)
weatherFogController.hpp:12:     float fogPhase;                                                                   // 0x0244 (size: 0x4)
weatherFogController.hpp:13:     bool permanent;                                                                   // 0x0248 (size: 0x1)
weatherFogController.hpp:14:     bool permafog;                                                                    // 0x0249 (size: 0x1)
weatherFogController.hpp:15:     float Strength;                                                                   // 0x024C (size: 0x4)
weatherFogController.hpp:17:     void ReceiveTick(float DeltaSeconds);
weatherFogController.hpp:18:     void ReceiveBeginPlay();
weatherFogController.hpp:19:     void ExecuteUbergraph_weatherFogController(int32 EntryPoint);
weatherFogController.hpp:20:  }; // Size: 0x250
```

### 2.1 Inheritance and role

- **Parent**: `AActor` (line 4). NOT a `USceneComponent`, NOT a controller
  in the UE sense — a *spawnable Actor* that exists for the duration of
  a fog event.
- **`cyc @0x0230`**: back-pointer to its parent `AdaynightCycle_C` —
  confirms `daynightCycle` owns the fog-event lifecycle (set
  `daynightCycle::fogEventObject` on spawn, null on destroy).

### 2.2 State properties

All scalar / bool — no arrays, no complex types:

| Field | Offset | Type | Role |
| --- | --- | --- | --- |
| `Time` | 0x0238 | float | elapsed time within the event (most likely a counter ticked by `ReceiveTick`) |
| `Alpha` | 0x023C | float | 0..1 blend factor (ramp-in / ramp-out fade) |
| `Duration` | 0x0240 | float | total event length seconds |
| `fogPhase` | 0x0244 | float | current phase in the fog cycle |
| `permanent` | 0x0248 | bool | does NOT auto-end |
| `permafog` | 0x0249 | bool | (subtly different — likely "permanent dense fog" vs `permanent` meaning permanent event) |
| `Strength` | 0x024C | float | density / intensity multiplier |

### 2.3 Mutation surface (UFunctions)

Only **three** UFunctions:
- `ReceiveTick(float DeltaSeconds)` — drives `Time`, `Alpha`, `fogPhase`,
  applies values to the parent's EHF component each frame.
- `ReceiveBeginPlay()` — initialization.
- `ExecuteUbergraph_weatherFogController(int32 EntryPoint)` — the BP body.

**No public Set* functions.** All mutation is internal-to-BP via ubergraph;
sync must therefore write the raw field offsets via reflection (the same
pattern we use elsewhere — see [[votv-flashlight-RE-2026-05-25]] for the
flashlight intensity field write).

### 2.4 Cross-references to `weatherFogController_C`

Grep across CXX dump:
```
daynightCycle.hpp:49:    class AweatherFogController_C* fogEventObject;                                    // 0x0338 (size: 0x8)
daynightCycle.hpp:138:   void fogEvent();
daynightCycle.hpp:142:   void superFogEvent();
daynightCycle.hpp:145:   void spawnFog();
```

So:
- **Producer** (spawner): `AdaynightCycle_C::fogEvent()`,
  `AdaynightCycle_C::superFogEvent()`, `AdaynightCycle_C::spawnFog()`.
- **Held by**: `AdaynightCycle_C::fogEventObject @0x0338` (single
  reference; only ONE fog event live at a time).
- **Owner-of-target**: applies its computed density to the
  `UExponentialHeightFogComponent* ExponentialHeightFog` on the
  parent daynightCycle (`daynightCycle.hpp:16`, offset 0x0270).

There is **no `weatherFogController_C` reference anywhere else** in the
CXX dump.

---

## Section 3 — `newsky.hpp` (full layout)

File: 51 lines.

### 3.1 Inheritance

`class Anewsky_C : public AActor` (newsky.hpp:4). 0x2F1 bytes total.

### 3.2 Component owns

```
newsky.hpp:7:      class UDirectionalLightComponent* light_blackhole;                                // 0x0228 (size: 0x8)
newsky.hpp:8:      class UStaticMeshComponent* m1;                                                   // 0x0230
newsky.hpp:9:      class UStaticMeshComponent* m3;                                                   // 0x0238
newsky.hpp:10:     class UStaticMeshComponent* m2;                                                   // 0x0240
newsky.hpp:11:     class UBillboardComponent* Billboard;                                             // 0x0248
newsky.hpp:12:     class UStaticMeshComponent* sky;                                                  // 0x0250
newsky.hpp:13:     class UArrowComponent* starRot;                                                   // 0x0258
newsky.hpp:14:     class UArrowComponent* dir;                                                       // 0x0260
newsky.hpp:15:     class USceneComponent* DefaultSceneRoot;                                          // 0x0268
```

— a sky dome (`sky`), 3 mesh slots (`m1`/`m2`/`m3`), the black-hole sun's
own directional light (`light_blackhole`), and rotation arrows for star
field / sun direction.

### 3.3 State properties

```
newsky.hpp:16:     float Alpha;                                                                      // 0x0270 (size: 0x4)
newsky.hpp:17:     class UMaterialInstanceDynamic* dynmat;                                           // 0x0278 (size: 0x8)
newsky.hpp:18:     class UCurveLinearColor* sky_bottom;                                              // 0x0280
newsky.hpp:19:     class UCurveLinearColor* sky_top;                                                 // 0x0288
newsky.hpp:20:     class UCurveLinearColor* sky_clouds;                                              // 0x0290
newsky.hpp:21:     class UCurveLinearColor* sun_color;                                               // 0x0298
newsky.hpp:22:     bool sunHidden;                                                                   // 0x02A0 (size: 0x1)
newsky.hpp:23:     float sunIntensityMult;                                                           // 0x02A4 (size: 0x4)
newsky.hpp:24:     FLinearColor sunColor;                                                            // 0x02A8 (size: 0x10)
newsky.hpp:25:     float moonIntensityMult;                                                          // 0x02B8 (size: 0x4)
newsky.hpp:26:     float moonPhase_mirror;                                                           // 0x02BC (size: 0x4)
newsky.hpp:27:     float moonPhase_normal;                                                           // 0x02C0 (size: 0x4)
newsky.hpp:28:     bool Eye;                                                                         // 0x02C4 (size: 0x1)
newsky.hpp:29:     class AmainGamemode_C* GameMode;                                                  // 0x02C8 (size: 0x8)
newsky.hpp:30:     class UMaterialInstanceDynamic* dynmat_bhole;                                     // 0x02D0
newsky.hpp:31:     class UMaterialInstanceDynamic* dynmat_sun;                                       // 0x02D8
newsky.hpp:32:     class AdaynightCycle_C* daylightCycleObject;                                      // 0x02E0
newsky.hpp:33:     bool applyMoonPhase;                                                              // 0x02E8 (size: 0x1)
newsky.hpp:34:     float DeltaSeconds;                                                               // 0x02EC (size: 0x4)
newsky.hpp:35:     bool skyHidden;                                                                   // 0x02F0 (size: 0x1)
```

**Key observation**: `newsky` carries `sunHidden`, `skyHidden`, `Eye`,
`applyMoonPhase`, the sun/moon intensity multipliers, and the active
color curves. These are **visual-state** fields, NOT precipitation
state. The "weather affecting the sky" pipeline likely runs
`daynightCycle` → mutate `newsky.sunIntensityMult` (e.g. dim sun during
rain) → newsky::upd renders. Note: there is NO `rain`/`fog`/`snow` field
on newsky itself.

### 3.4 Mutation surface

```
newsky.hpp:37:     void setHiddenSuns();
newsky.hpp:38:     void setEye(bool Eye);
newsky.hpp:39:     void setSunIntensity();
newsky.hpp:40:     void skyVisibility(bool bNewHidden);
newsky.hpp:41:     void Init();
newsky.hpp:42:     void upd(bool updateLights);
newsky.hpp:43:     void UserConstructionScript();
newsky.hpp:44:     void ReceiveTick(float DeltaSeconds);
newsky.hpp:45:     void ReceiveBeginPlay();
newsky.hpp:46:     void tp();
newsky.hpp:47:     void setMoonPhase();
newsky.hpp:48:     void ExecuteUbergraph_newsky(int32 EntryPoint);
```

— `setEye(bool)`, `skyVisibility(bool bNewHidden)`, `upd(bool updateLights)`
are the candidate hook surfaces for sky state sync. `Init`, `tp` (teleport
the sky dome to player pos), `setMoonPhase`, `setSunIntensity` adjust
specific axes.

### 3.5 Cross-references to `Anewsky_C`

```
daynightCycle.hpp:29:    class Anewsky_C* skysphere;                                                       // 0x02B8 (size: 0x8)
```

Only `daynightCycle` holds the `newsky` pointer. There is NO `newsky*`
field on `mainGamemode` directly.

---

## Section 4 — `event_fleshRain.hpp` (full layout)

File: 13 lines.

```
event_fleshRain.hpp:4:    class Aevent_fleshRain_C : public Aactor_save_C
event_fleshRain.hpp:6:        FPointerToUberGraphFrame UberGraphFrame;                                          // 0x0248 (size: 0x8)
event_fleshRain.hpp:7:        int32 I;                                                                          // 0x0250 (size: 0x4)
event_fleshRain.hpp:9:        void ReceiveBeginPlay();
event_fleshRain.hpp:10:       void ExecuteUbergraph_event_fleshRain(int32 EntryPoint);
event_fleshRain.hpp:11:    }; // Size: 0x254
```

### 4.1 What is this?

- **Inheritance**: `Aactor_save_C` (save-tracked actor base). Means this
  actor IS persisted in the save game when present.
- **State**: ONE `int32 I` field at 0x0250. Likely an event-phase index
  or instance counter. No `loc`, no `intensity`, no `duration` payload
  is exposed in the dump — everything is in the BP body.
- **Functions**: `ReceiveBeginPlay()` + the ubergraph body. NO trigger,
  payload, getData, or loadData functions in the .hpp.
- **Save behavior**: Inheriting `Aactor_save_C` means it WILL appear in
  the save snapshot (snap-on-connect Phase 5S0 already covers this).

### 4.2 Cross-references

Grep across the CXX dump for `fleshRain` returns nothing other than this
file. The actor is spawned dynamically by some other BP at runtime (or
hand-placed in the level); whatever spawns it is not visible in any
.hpp interface. **Open question — needs BP introspection or IDA.**

### 4.3 Interpretation (NOT verified by RE)

By name and by analogy with the `event_*` family in `trigger_eventer.hpp`
(`event_arirShip`, `event_solar`, `event_falseEnter`, etc — see Section 10),
this is a **scripted story event**, not a weather effect. The "rain" in
the name refers to falling flesh chunks (an Akrobon/erieflesh-themed
event) — visual effect, not the precipitation system. **It does NOT
participate in the `daynightCycle` rain pipeline.** Confirmed: grep for
`fleshRain` in `daynightCycle.hpp` yields no matches.

---

## Section 5 — Search for weather ENUMS in `mainGamemode.hpp` (and the dump at large)

### 5.1 Search results

```
$ grep -in 'enum_seasons|seasonsParams|enum_difficulty' mainGamemode.hpp
mainGamemode.hpp:106:    TEnumAsByte<enum_difficulty::Type> difficulty;                                    // 0x0658 (size: 0x1)
mainGamemode.hpp:328:    TMap<TEnumAsByte<enum_seasons::Type>, Fstruct_landscapeSeasonsTemplate> seasonsParams; // 0x1120 (size: 0x50)
mainGamemode.hpp:333:    TEnumAsByte<enum_seasons::Type> currentSeason;                                    // 0x117B (size: 0x1)
mainGamemode.hpp:381:    void getSeason(TEnumAsByte<enum_seasons::Type>& currentSeason);
mainGamemode.hpp:382:    void getSeasonFromTime(bool weekly, TEnumAsByte<enum_seasons::Type>& season);
mainGamemode.hpp:397:    void Update Season(bool updateLandscape);
mainGamemode.hpp:554:    void seasonUpdated__DelegateSignature(TEnumAsByte<enum_seasons::Type> newSeason);
```

### 5.2 `enum_seasons_enums.hpp` (full)

```
enum_seasons_enums.hpp:1:    namespace enum_seasons {
enum_seasons_enums.hpp:2:        enum Type {
enum_seasons_enums.hpp:3:            NewEnumerator0 = 0,
enum_seasons_enums.hpp:4:            NewEnumerator1 = 1,
enum_seasons_enums.hpp:5:            NewEnumerator2 = 2,
enum_seasons_enums.hpp:6:            NewEnumerator3 = 3,
enum_seasons_enums.hpp:7:            enum_MAX = 4,
enum_seasons_enums.hpp:8:        };
enum_seasons_enums.hpp:9:    }
```

— 4 unnamed values (BP-only names; the .hpp dumper doesn't pull them).
Used by the `seasonsParams` TMap to look up per-season landscape templates.
This is **season**, not weather.

### 5.3 `enum_difficulty_enums.hpp` (full)

```
enum_difficulty_enums.hpp:2:    enum Type {
enum_difficulty_enums.hpp:3:        NewEnumerator0 = 0,
enum_difficulty_enums.hpp:4:        NewEnumerator1 = 1,
enum_difficulty_enums.hpp:5:        NewEnumerator2 = 2,
enum_difficulty_enums.hpp:6:        NewEnumerator3 = 3,
enum_difficulty_enums.hpp:7:        NewEnumerator4 = 4,
enum_difficulty_enums.hpp:8:        enum_MAX = 5,
enum_difficulty_enums.hpp:9:    }
```

— 5 levels. Referenced by `daynightCycle::diff @0x02EC` which scales
`diff_mult @0x02E8` — a difficulty multiplier on weather rolls.

### 5.4 NO weather-state enum

Search for any enum file matching `weather`, `precip`, `wstate`, `rainType`,
`fogType`, `stormType`:
```
$ glob 'enum_*weather*' → no files
$ glob 'enum_*precip*' → no files
$ glob 'enum_*rain*' → no files
$ glob 'enum_*fog*' → no files
$ glob 'enum_*storm*' → no files
```

**Conclusion**: VOTV's weather has no discrete-state enum. State is a
small set of independent scalars/bools on `daynightCycle` and the event
actors. The "weather mode" is the *combination* of:
- `isRaining` (bool)
- `rain` (float, 0..1 intensity gradient)
- `rainStrength` (float)
- `rainLightningChance` (float)
- `rainWindSpeed` (float)
- `isSnow` (bool)
- presence/absence of `fogEventObject` (pointer)
- `thickFog` (float)
- `permanentRain`, `permanentFog` (bools, rule-driven)
- `enable_fog`, `enable_superfog`, `enable_rain` (bools, master enables)

The wire packet thus carries a **flat record of these scalars**, not an
enum tag.

---

## Section 6 — SAVE / LOAD code paths for weather

### 6.1 Search across save struct files

```
$ grep -in 'rain|fog|snow|wind|lightning|weather|storm' struct_save.hpp
→ No matches found.

$ grep -in 'rain|fog|snow|wind|lightning|weather|storm' saveSlot.hpp
saveSlot.hpp:62:    FString windowShit;                                                               // 0x0660 (size: 0x10)
saveSlot.hpp:67:    TArray<uint8> windowShitBytes;                                                    // 0x07A0 (size: 0x10)
[These are NOT weather — "windowShit" appears to be an internal name for
window/UI byte-blob persistence. False positive on the "wind" substring.]

$ grep -in 'rain|fog|snow|wind|lightning|weather|storm' save_main.hpp
→ No matches found.

$ grep -in 'rain|fog|snow|wind|lightning|weather|storm' mainGameInstance.hpp
→ No matches found.
```

**Conclusion**: weather state is NOT persisted in the save anywhere in
the standard save container.

### 6.2 The lone weather-related save data: `Fstruct_gameRules`

```
struct_gameRules.hpp:31:    bool permanentFog_61_33B0F8EA4EFC4825C466EA87C6A5B5BD;                            // 0x001E (size: 0x1)
struct_gameRules.hpp:32:    bool permanentRain_63_73F792B64077E7BAFE1671A95A7AC272;                           // 0x001F (size: 0x1)
struct_gameRules.hpp:33:    bool weeklySeason_66_BB921D484B8DE14E2FB16382BB12E686;                            // 0x0020 (size: 0x1)
```

These are GAMERULES — set when the save game is created (difficulty
settings, perma-fog, perma-rain). They are NOT the live weather state;
they merely BIAS the runtime probability rolls in
`AdaynightCycle_C::timerRain()` / `permaRain_timer()` / `setRainParameters()`.
Mapped via `AmainGamemode_C::Gamerule Is Permanent Black Hole(bool&
permanentBlackhole)` (mainGamemode.hpp:372) — there is a function that
reads each rule.

### 6.3 The DayNight save entry point

```
daynightCycle.hpp:121:    void loadtime(float totalTime, float Day);
```

Only `totalTime` + `Day` are loaded. **NOT** `rain`, `isRaining`, fog,
strength, or any weather state. On load, the rain system re-rolls from
the gamerule flags + the periodic timers (`timerRain`, `timerLightning`,
`permaRain_timer`). **Implication for coop sync**: the snapshot-on-connect
in Phase 5S0 captures save-state, but the in-progress storm is NOT in
the save — the host must additionally broadcast the LIVE weather state
to a joining client. This is the same shape as the flashlight `bool
flashlight @0x0838` state (see [[votv-flashlight-RE-2026-05-25]] Section
6) — runtime-only, must be sync'd at connect time outside the save.

### 6.4 `mainGamemode::saveAnim`, `save`, `autosave`

```
mainGamemode.hpp:494:    void saveAnim(bool Index);
mainGamemode.hpp:495:    void save(bool quicksave, bool bypassEvent, bool overwriteSubsave);
mainGamemode.hpp:498:    void autosave();
mainGamemode.hpp:453:    void saveObjects(bool quicksave);
mainGamemode.hpp:424:    void saveTriggers();
mainGamemode.hpp:377:    void Save Primitives();
```

None of these write weather state — they save the `saveSlot`,
`save_main`, gathered actors, and triggers. (Confirmed: grep for
`weather`/`rain`/`fog` finds nothing in their parameter lists or in
the structs they would touch.)

### 6.5 Canonical "current weather" field

Per Sections 1 + 2 + 6.1: **there is no single `currentWeather` field
on `mainGameInstance` or `mainGamemode`.** The canonical live state is
spread across:

- `AdaynightCycle_C` (the master) — runtime fields listed in Section 7.1.
- `AweatherFogController_C` (transient fog event) — Section 2.
- `AblackFog_C` (post-process black fog event) — Section 7.4.
- `AredSkyEvent_C` (red sky tint) — Section 7.5.
- `AbadSun_C` (second-sun event) — Section 7.6.
- `AdirectionalWind_C` (wind) — Section 7.7.

A wire `WeatherState` packet must serialize a tuple over THIS set.

---

## Section 7 — `AdaynightCycle_C` and the weather-actor cohort

### 7.1 `AdaynightCycle_C` — the canonical weather state holder

Parent: `AActor`. Size 0x44D bytes. Pointed to by
`AmainGamemode_C::daynightCycle @0x0450`. Pointed to by
`Anewsky_C::daylightCycleObject @0x02E0` (back-reference for the sky).
Pointed to by `AweatherFogController_C::cyc @0x0230` (back-ref for fog
event).

**Weather component handles** (all at well-known offsets):
```
daynightCycle.hpp:7:      class UParticleSystemComponent* eff_rain;                                         // 0x0228 (size: 0x8)
daynightCycle.hpp:8:      class UArrowComponent* rain_tilt;                                                 // 0x0230 (size: 0x8)
daynightCycle.hpp:9:      class UArrowComponent* rain_root;                                                 // 0x0238 (size: 0x8)
daynightCycle.hpp:10:     class UDirectionalLightComponent* light_moon;                                     // 0x0240 (size: 0x8)
daynightCycle.hpp:11:     class UDirectionalLightComponent* light_sun;                                      // 0x0248 (size: 0x8)
daynightCycle.hpp:15:     class USkyLightComponent* lightning;                                              // 0x0268 (size: 0x8)
daynightCycle.hpp:16:     class UExponentialHeightFogComponent* ExponentialHeightFog;                       // 0x0270 (size: 0x8)
daynightCycle.hpp:17:     class UAudioComponent* rainSnd;                                                   // 0x0278 (size: 0x8)
daynightCycle.hpp:18:     class USkyLightComponent* SkyLight_A;                                             // 0x0280 (size: 0x8)
daynightCycle.hpp:19:     class UParticleSystemComponent* eff_shootingStar;                                 // 0x0288 (size: 0x8)
```

NOTE: the `lightning` field at 0x0268 is a **`USkyLightComponent`**, not
a literal lightning bolt — the field is named `lightning` because it is
the sky-light used to provide the LIGHTNING FLASH ambient bounce (a
brief intense skylight burst when a strike fires). The actual lightning
bolt actors are `AlightningStrike_C` (Section 7.3).

**Day/time core**:
```
daynightCycle.hpp:21:     float Day;                                                                        // 0x0298 (size: 0x4)
daynightCycle.hpp:22:     float Phase;                                                                      // 0x029C (size: 0x4)
daynightCycle.hpp:26:     float MaxTime;                                                                    // 0x02AC (size: 0x4)
daynightCycle.hpp:27:     float totalTime;                                                                  // 0x02B0 (size: 0x4)
daynightCycle.hpp:28:     float TimeScale;                                                                  // 0x02B4 (size: 0x4)
daynightCycle.hpp:32:     FIntVector timeZ;                                                                 // 0x02D0 (size: 0xC)
```

— ToD state. Not weather per se but inseparable (see project-weather-sync-future
note in memory: "Time-of-day: VOTV may already couple weather to ToD").

**Weather scalars / bools (the canonical state)**:
```
daynightCycle.hpp:34:     float rain;                                                                       // 0x02E0 (size: 0x4)
daynightCycle.hpp:35:     bool isRaining;                                                                   // 0x02E4 (size: 0x1)
daynightCycle.hpp:36:     bool rainMuted;                                                                   // 0x02E5 (size: 0x1)
daynightCycle.hpp:37:     float diff_mult;                                                                  // 0x02E8 (size: 0x4)
daynightCycle.hpp:38:     TEnumAsByte<enum_difficulty::Type> diff;                                          // 0x02EC (size: 0x1)
daynightCycle.hpp:40:     float rainSpeed;                                                                  // 0x02F4 (size: 0x4)
daynightCycle.hpp:48:     float thickFog;                                                                   // 0x0330 (size: 0x4)
daynightCycle.hpp:49:     class AweatherFogController_C* fogEventObject;                                    // 0x0338 (size: 0x8)
daynightCycle.hpp:50:     bool Realtime;                                                                    // 0x0340 (size: 0x1)
daynightCycle.hpp:67:     class UParticleSystem* rainEffect;                                                // 0x03A8 (size: 0x8)
daynightCycle.hpp:68:     bool isSnow;                                                                      // 0x03B0 (size: 0x1)
daynightCycle.hpp:70:     FLinearColor skyLightColor;                                                       // 0x03B8 (size: 0x10)
daynightCycle.hpp:71:     FLinearColor fogColor;                                                            // 0x03C8 (size: 0x10)
daynightCycle.hpp:72:     bool enableSunlight;                                                              // 0x03D8 (size: 0x1)
daynightCycle.hpp:74:     FLinearColor sunColor;                                                            // 0x03F0 (size: 0x10)
daynightCycle.hpp:76:     float rainStrength;                                                               // 0x0404 (size: 0x4)
daynightCycle.hpp:77:     float rainLightningChance;                                                        // 0x0408 (size: 0x4)
daynightCycle.hpp:78:     float rainDeactivateChance;                                                       // 0x040C (size: 0x4)
daynightCycle.hpp:79:     float cloudsTimer;                                                                // 0x0410 (size: 0x4)
daynightCycle.hpp:80:     float cloudsTimerAccumulate;                                                      // 0x0414 (size: 0x4)
daynightCycle.hpp:81:     float finalFogDensity;                                                            // 0x0418 (size: 0x4)
daynightCycle.hpp:82:     float rainWindSpeed;                                                              // 0x041C (size: 0x4)
daynightCycle.hpp:83:     bool rainToggle;                                                                  // 0x0420 (size: 0x1)
daynightCycle.hpp:84:     float rainProbability;                                                            // 0x0424 (size: 0x4)
daynightCycle.hpp:85:     float fogProbability;                                                             // 0x0428 (size: 0x4)
daynightCycle.hpp:86:     bool permanentRain;                                                               // 0x042C (size: 0x1)
daynightCycle.hpp:87:     bool permanentFog;                                                                // 0x042D (size: 0x1)
daynightCycle.hpp:88:     float seasonalExponent;                                                           // 0x0430 (size: 0x4)
daynightCycle.hpp:90:     float timeRotationDelay;                                                          // 0x0438 (size: 0x4)
daynightCycle.hpp:92:     bool enableMoonlight;                                                             // 0x0448 (size: 0x1)
daynightCycle.hpp:93:     bool enable_fog;                                                                  // 0x0449 (size: 0x1)
daynightCycle.hpp:94:     bool enable_superfog;                                                             // 0x044A (size: 0x1)
daynightCycle.hpp:95:     bool enable_rain;                                                                 // 0x044B (size: 0x1)
```

**This is the canonical live weather state.** The minimal sync set is:

| Field | Offset | Type | Role |
| --- | --- | --- | --- |
| `rain` | 0x02E0 | float | live precipitation intensity (probably 0..1 ramp) |
| `isRaining` | 0x02E4 | bool | rain state on/off |
| `rainMuted` | 0x02E5 | bool | rain audio muted (e.g. indoors) — may be per-peer, not synced |
| `rainSpeed` | 0x02F4 | float | rain falling speed param |
| `thickFog` | 0x0330 | float | runtime thick-fog accumulator |
| `isSnow` | 0x03B0 | bool | snow mode |
| `rainStrength` | 0x0404 | float | precipitation strength (driver for particles + audio) |
| `rainLightningChance` | 0x0408 | float | per-tick lightning roll probability |
| `rainDeactivateChance` | 0x040C | float | per-tick "stop raining" roll |
| `rainWindSpeed` | 0x041C | float | wind speed during rain |
| `rainToggle` | 0x0420 | bool | latest causeRain() arg cache |
| `finalFogDensity` | 0x0418 | float | computed EHF density |
| `cloudsTimer` / `cloudsTimerAccumulate` | 0x0410 / 0x0414 | float | cloud motion timers (visual; whether to sync is open) |
| `enable_fog`, `enable_superfog`, `enable_rain` | 0x0449, 0x044A, 0x044B | bool | master enables (per-rule; user-toggled) |
| `permanentRain`, `permanentFog` | 0x042C, 0x042D | bool | persistence flags (mirrors gamerules) |
| `fogEventObject` | 0x0338 | ptr | presence => fog event active |

**Mutation surface (UFunctions on `AdaynightCycle_C`)**:
```
daynightCycle.hpp:106:    void SetFogDensity();
daynightCycle.hpp:107:    void setWindParameters();
daynightCycle.hpp:108:    void setRainParameters();
daynightCycle.hpp:109:    void causeRain(bool isRaining);
daynightCycle.hpp:110:    void setRainProperties(bool isRaining, float rainStrength, float rainLightningChance, float rainDeactivateChance, float rainWindSpeed);
daynightCycle.hpp:111:    void createNewTask(Fstruct_taskNew& struct_taskNew);
daynightCycle.hpp:112:    void setRainParticles();
daynightCycle.hpp:115:    void pluginLightCurve(class AambientLightCurve_C* Curve, bool& success);
daynightCycle.hpp:116:    void setSkyIntensity();
daynightCycle.hpp:137:    void ligh();
daynightCycle.hpp:138:    void rainS();
daynightCycle.hpp:139:    void fogEvent();
daynightCycle.hpp:142:    void Rewind();
daynightCycle.hpp:143:    void superFogEvent();
daynightCycle.hpp:144:    void rainCleanup();
daynightCycle.hpp:145:    void rainClean();
daynightCycle.hpp:146:    void spawnFog();
daynightCycle.hpp:147:    void timerRain();
daynightCycle.hpp:148:    void timerLightning();
daynightCycle.hpp:154:    void permaRain_timer();
daynightCycle.hpp:155:    void setSkyColor();
daynightCycle.hpp:151:    void intComs_triggerSnow(bool isSnow);
```

**Key public setter**:
```
setRainProperties(bool isRaining, float rainStrength, float rainLightningChance, float rainDeactivateChance, float rainWindSpeed)
```
— Five-arg call that sets the 5 primary rain fields in one shot. **This
is the best "host applies received WeatherState" UFunction.** Receiver
side can call it via reflected ProcessEvent dispatch and the rest of the
BP will follow (`setRainParameters`, `setRainParticles`, `setWindParameters`
chain).

**Best suppression targets on receiver (client) side**:
- `timerRain()` — periodic "should rain?" roll. Suppress on client.
- `timerLightning()` — periodic lightning roll. Suppress on client.
- `permaRain_timer()` — permanent-rain heartbeat. Suppress on client.
- `fogEvent()` / `superFogEvent()` / `spawnFog()` — fog event spawners.
  Suppress on client.

— Mirrors the established pattern from
[[project-coop-aprop-lifecycle-RE-inc3-scope]] (suppress mushroomMaster,
NpcSpawner on client; host-authoritative spawn).

### 7.2 `AdirectionalWind_C` (single instance per map)

File: `directionalWind.hpp`, 52 lines. Parent: `AActor`. 0x308 bytes.

```
directionalWind.hpp:19:    class AWindDirectionalSource* windSource;                                         // 0x0288 (size: 0x8)
directionalWind.hpp:23:    FVectorSpringState spring;                                                        // 0x02B8 (size: 0x18)
directionalWind.hpp:24:    float Intensity;                                                                  // 0x02D0 (size: 0x4)
directionalWind.hpp:25:    FVector windLocation;                                                             // 0x02D4 (size: 0xC)
directionalWind.hpp:26:    float windAdd;                                                                    // 0x02E0 (size: 0x4)
directionalWind.hpp:27:    float windSpeed_rain;                                                             // 0x02E4 (size: 0x4)
directionalWind.hpp:28:    float windStrength_rain;                                                          // 0x02E8 (size: 0x4)
directionalWind.hpp:29:    float windSpeed_background;                                                       // 0x02EC (size: 0x4)
directionalWind.hpp:30:    float windStrength_background;                                                    // 0x02F0 (size: 0x4)
directionalWind.hpp:31:    float windSpeed_total;                                                            // 0x02F4 (size: 0x4)
directionalWind.hpp:32:    float windStrength_total;                                                         // 0x02F8 (size: 0x4)
directionalWind.hpp:33:    float windTexStr;                                                                 // 0x02FC (size: 0x4)
```

Mutation:
```
directionalWind.hpp:36:    void setParameters(float Intensity, float Angle, float Speed, float Strength);
directionalWind.hpp:37:    void updState();
directionalWind.hpp:38:    void updateVars();
directionalWind.hpp:39:    void doWind();
directionalWind.hpp:47:    void updateDirWind();
```

The single public setter is **`setParameters(Intensity, Angle, Speed, Strength)`**
— 4 floats. The "Angle" arg is NOT a member field on the struct; it is
applied via the `windSource` actor's rotation. For wire sync, transmit
`(Intensity, Angle, Speed, Strength)` and call this UFunction on the
receiver — exactly the same shape as `setRainProperties`.

Pointed to by `mainGamemode::directionalWind @0x0F70`.

### 7.3 `AlightningStrike_C` (transient discrete actor)

File: `lightningStrike.hpp`, 27 lines. Parent: `AActor`. 0x280 bytes.

```
lightningStrike.hpp:7:      class USphereComponent* lightningRadius;                                          // 0x0228 (size: 0x8)
lightningStrike.hpp:8:      class UAudioComponent* audio_background;                                          // 0x0230 (size: 0x8)
lightningStrike.hpp:9:      class UAudioComponent* audio_far;                                                 // 0x0238 (size: 0x8)
lightningStrike.hpp:10:     class UAudioComponent* audio_close;                                               // 0x0240 (size: 0x8)
lightningStrike.hpp:11:     class UPointLightComponent* PointLight;                                           // 0x0248 (size: 0x8)
lightningStrike.hpp:12:     class UParticleSystemComponent* eff_lightning;                                    // 0x0250 (size: 0x8)
lightningStrike.hpp:14:     float lightTL_a_F221259D43A5A518CBF27F998E63014D;                                 // 0x0260 (size: 0x4)
lightningStrike.hpp:16:     class UTimelineComponent* lightTL;                                                // 0x0268 (size: 0x8)
lightningStrike.hpp:17:     int32 debris;                                                                     // 0x0270 (size: 0x4)
lightningStrike.hpp:18:     FName explosionTag;                                                               // 0x0274 (size: 0x8)
lightningStrike.hpp:19:     float Radius;                                                                     // 0x027C (size: 0x4)
```

Mutation: only `ReceiveBeginPlay()`, `lightTL__FinishedFunc`,
`lightTL__UpdateFunc`, ubergraph. No setter. **Construction-time
parametrized** — payload (debris count, tag, radius) is set at
`SpawnActor` deferred-construction time.

**Sync model**: this is a discrete event. Per the
[[project-weather-sync-future]] memory note ("Lightning bolts: discrete
events (location + timing) need their own packet kind OR can ride on a
unified EntityEventPacket"), the wire packet for a strike is just
`(SpawnLocation_FVector, explosionTag_FName, Radius_float, debris_int)`.

**Spawned by**:
- `AdaynightCycle_C::timerLightning()` — periodic random strike.
- `AoutsideChurch_C::lightningStrike()` — story-event strike at the church.
- `AoutsideChurch_C::truckLightning()` — strike on the truck.
- `AoutsideChurch_C::fireLightningAtPlayer()` — targeted at player.

**Listeners** (NPCs / props that react):
- `AmainPlayer_C::reachedByLightning(class AlightningStrike_C* lightning)`
  (mainPlayer.hpp:692)
- `ApryingCrowbar_C::reachedByLightning(...)` (pryingCrowbar.hpp:148)
- `AmainPlayer_C::lightningInfluence(bool& influence, float& Multiplier)`
  (mainPlayer.hpp:400)
- … and many other props with `reachedByLightning` / `lightningInfluence`
  (88 .hpp files match `lightning` substring — most are damage receivers,
  not weather-state holders).

### 7.4 `AblackFog_C` (story-event post-process)

File: `blackFog.hpp`, 24 lines. Parent: `AActor`. 0x268 bytes.

```
blackFog.hpp:7:      class UPostProcessComponent* PostProcess;                                         // 0x0228 (size: 0x8)
blackFog.hpp:9:      float A;                                                                          // 0x0238 (size: 0x4)
blackFog.hpp:10:     class UMaterialInstanceDynamic* dynmat;                                           // 0x0240 (size: 0x8)
blackFog.hpp:11:     float spd;                                                                        // 0x0248 (size: 0x4)
blackFog.hpp:12:     bool M;                                                                           // 0x024C (size: 0x1)
blackFog.hpp:13:     TArray<class Aeyer_C*> eyes;                                                      // 0x0250 (size: 0x10)
```

Mutation: only `set()`, `ReceiveBeginPlay`, `ReceiveDestroyed`,
`ReceiveTick`, `spawnGhost`. The arg-less `set()` is the trigger entry.

Pointed to by `mainGamemode::blackFog @0x0880`. Spawned by
`mainGamemode::spawnBlackFog()`. **Wire model**: an event spawn (with
no payload — the actor self-configures).

### 7.5 `AredSkyEvent_C`

File: `redSkyEvent.hpp`, 16 lines. Parent: `AActor`. 0x231 bytes.

```
redSkyEvent.hpp:8:      bool isred;                                                                       // 0x0230 (size: 0x1)
redSkyEvent.hpp:10:     void set(bool isred);
```

Single bool. Pointed to by `mainGamemode::redSky @0x0888`. Spawned by
`mainGamemode::spawnRedSky()`. **Wire**: `set(isred)` — 1 byte.

### 7.6 `AbadSun_C`

File: `badSun.hpp`, 27 lines. Parent: `Aactor_save_C` (save-persistent).
0x28C bytes.

```
badSun.hpp:7:      class UAudioComponent* Audio;                                                     // 0x0250 (size: 0x8)
badSun.hpp:10:     class UTimelineComponent* A;                                                      // 0x0260 (size: 0x8)
badSun.hpp:11:     class Uui_badSun_C* Widget;                                                       // 0x0268 (size: 0x8)
badSun.hpp:12:     bool endSiren;                                                                    // 0x0270 (size: 0x1)
badSun.hpp:13:     int32 destroy;                                                                    // 0x0274 (size: 0x4)
badSun.hpp:14:     bool Super;                                                                       // 0x0278 (size: 0x1)
```

Pointed to by `mainGamemode::badSun @0x0AD8`. Spawned by
`mainGamemode::Spawn Bad Sun()`. Since it inherits `Aactor_save_C`, it
appears in the snapshot-on-connect.

### 7.7 `Atool_rain_C` (toolgun rain control — DEV/CHEAT)

File: `tool_rain.hpp`, 22 lines. Parent: `AtoolObject_C` (a toolgun tool).
0x54E bytes.

```
tool_rain.hpp:7:      bool Active;                                                                      // 0x0538 (size: 0x1)
tool_rain.hpp:8:      float wind_speed;                                                                 // 0x053C (size: 0x4)
tool_rain.hpp:9:      float wind_strength;                                                              // 0x0540 (size: 0x4)
tool_rain.hpp:10:     float lightningChance;                                                            // 0x0544 (size: 0x4)
tool_rain.hpp:11:     float rainStopChance;                                                             // 0x0548 (size: 0x4)
tool_rain.hpp:12:     bool instant;                                                                     // 0x054C (size: 0x1)
tool_rain.hpp:13:     bool updateParameters_0;                                                          // 0x054D (size: 0x1)
```

This is the cheat-toolgun's rain controller (used in dev mode). It is
NOT part of the production weather path but its parameter mapping
(`wind_speed`, `wind_strength`, `lightningChance`, `rainStopChance`)
mirrors the runtime fields on `daynightCycle` exactly. Useful reference
for what params the engine considers "the canonical rain control set".

---

## Section 8 — The `intComs_triggerSnow` interface and snow path

```
int_coms.hpp:8:        void intComs_triggerSnow(bool isSnow);
```

Implemented in (grep across CXX dump):
```
mainGamemode.hpp:488:    void intComs_triggerSnow(bool isSnow);
daynightCycle.hpp:151:   void intComs_triggerSnow(bool isSnow);
trigger_eventer.hpp: [via inheritance — confirmed via halloweenMaster.hpp:34]
halloweenMaster.hpp:34:  void intComs_triggerSnow(bool isSnow);
actor_save.hpp:58:       void intComs_triggerSnow(bool isSnow);
```

So `intComs_triggerSnow(true)` propagates through:
- `mainGamemode` — likely sets `isSnowyFootsteps @0x0D90`, `isWinter @0x0EC8`
- `daynightCycle` — sets `isSnow @0x03B0` (turning rain particles to snow)
- `halloweenMaster` — adjusts holiday-event spawns

**Sync hook**: this is a single-bool broadcast. Host-fires `intComs_triggerSnow`
and we observe it; client receives via wire and dispatches the same
interface call.

---

## Section 9 — `outsideChurch_C` and the church-storm sub-system

`outsideChurch.hpp` is a placeable story actor. Relevant state:
```
outsideChurch.hpp:34:    class USphereComponent* lightningTrigger;                                         // 0x0328 (size: 0x8)
outsideChurch.hpp:70:    bool wasRaining;                                                                  // 0x0450 (size: 0x1)
outsideChurch.hpp:76:    bool superstorm;                                                                  // 0x0485 (size: 0x1)
outsideChurch.hpp:85:    void fireLightningAtPlayer();
outsideChurch.hpp:101:   void lightningStrike();
outsideChurch.hpp:107:   void truckLightning();
outsideChurch.hpp:110:   void init_lightningStorm(float Time);
```

— a story-driven lightning-storm sub-system (the church near the
Stolas herbs has its own "fire lightning at player" logic, separate
from `daynightCycle::timerLightning`). `wasRaining` is a flag that
remembers whether `daynightCycle::isRaining` was true on the previous
tick (likely for edge-detect of rain start/stop). `superstorm` is the
intense-storm phase trigger. These are STATE-CACHE flags, not the
canonical rain state.

**Sync implication**: host runs the church-storm BP; on client, suppress
the spawning function (`init_lightningStorm`) and rely on the host's
`AlightningStrike_C` event broadcasts instead.

---

## Section 10 — `Atrigger_eventer_C` and the event slot inventory

`trigger_eventer.hpp` is the event slot list. Notably it does NOT list a
weather slot — the events tracked are story events (ariralShip, solar,
arirSignal, falseEnter, vehicletp, mann, …). **Confirmed**: weather is
NOT routed through the `event_*` triggers; it runs on its own timers
in `daynightCycle`.

NOTE: `event_fleshRain` (Section 4) does NOT appear as a slot in
`trigger_eventer`. It is a standalone spawnable story actor — not in
the trigger registry.

---

## Section 11 — Engine.hpp: `UExponentialHeightFogComponent`

For completeness, the underlying UE4.27 engine class that `daynightCycle::
ExponentialHeightFog @0x0270` points to:
```
Engine.hpp:10975: class UExponentialHeightFogComponent : public USceneComponent
Engine.hpp:10977:     float FogDensity;                                                                 // 0x01F8 (size: 0x4)
Engine.hpp:10978:     float FogHeightFalloff;                                                           // 0x01FC (size: 0x4)
Engine.hpp:10980:     FLinearColor FogInscatteringColor;                                                // 0x020C (size: 0x10)
Engine.hpp:10999:     bool bOverrideLightColorsWithFogInscatteringColors;                               // 0x0290 (size: 0x1)
Engine.hpp:11014:     void SetFogInscatteringColor(FLinearColor Value);
Engine.hpp:11015:     void SetFogHeightFalloff(float Value);
Engine.hpp:11016:     void SetFogDensity(float Value);
```

These are the **engine-side setters** that `daynightCycle::SetFogDensity()`
(daynightCycle.hpp:106) likely wraps. If we ever needed to directly drive
fog from the wire (skipping the `daynightCycle` BP pipeline), these are
the standalone-callable UFunctions — but the preferred path is to call
`daynightCycle::SetFogDensity()` (then `setRainProperties` /
`setWindParameters` / `intComs_triggerSnow`) so the BP-side derived state
(audio, particles, light intensity) stays consistent.

---

## Section 12 — Summary: the authoritative weather state

### 12.1 Single-actor authority

**`AdaynightCycle_C`** is the canonical weather authority. `AmainGamemode_C`
is the entry point but stores only a pointer. `Anewsky_C` is a downstream
visual actor driven by daynightCycle. `AweatherFogController_C` is a
transient sub-actor spawned by daynightCycle for fog events. The wind,
black fog, red sky, bad sun, and lightning strikes are sibling actors
hung off mainGamemode pointers and (for the fog event) off daynightCycle.

### 12.2 Wire `WeatherState` packet — fields the host must broadcast

Based on the evidence above, the minimal `WeatherState` payload is:

```
struct WeatherState {
    // From AdaynightCycle_C — the master rain/fog/snow scalars
    float    rain;                  // @0x02E0 — current intensity ramp
    bool     isRaining;             // @0x02E4 — on/off
    bool     isSnow;                // @0x03B0 — snow mode
    float    rainStrength;          // @0x0404 — particle/audio drive
    float    rainLightningChance;   // @0x0408
    float    rainDeactivateChance;  // @0x040C
    float    rainWindSpeed;         // @0x041C
    float    rainSpeed;             // @0x02F4
    float    thickFog;              // @0x0330 — runtime fog accumulator
    float    finalFogDensity;       // @0x0418 — computed EHF density
    bool     permanentRain;         // @0x042C — sticky bias
    bool     permanentFog;          // @0x042D
    bool     enable_fog;            // @0x0449
    bool     enable_superfog;       // @0x044A
    bool     enable_rain;           // @0x044B
    bool     fogEventActive;        // derived: (fogEventObject @0x0338 != NULL)

    // From AdirectionalWind_C
    float    wind_intensity;
    float    wind_angle;            // applied to windSource rotation
    float    wind_speed;
    float    wind_strength;

    // Event-actor presence flags (each spawned/destroyed via discrete event)
    // — these are NOT in this packet; they ride on a separate EventActorSpawn
    //   packet (AblackFog_C, AredSkyEvent_C, AbadSun_C, AweatherFogController_C,
    //   AlightningStrike_C, Alakeglow_C, Aevent_fleshRain_C).
};
```

Estimated wire size: 9 floats + 8 bools = ~44 bytes. Fits comfortably in
one reliable UDP packet. Broadcast frequency: on STATE CHANGE only (rate
suppressed). The receiver applies via:

1. `AdaynightCycle_C::setRainProperties(isRaining, rainStrength,
   rainLightningChance, rainDeactivateChance, rainWindSpeed)` —
   covers the 5 primary scalars in one call.
2. Write `rain @0x02E0`, `rainSpeed @0x02F4`, `thickFog @0x0330`,
   `finalFogDensity @0x0418` via reflected memory writes (no UFunction
   for them directly).
3. `AdaynightCycle_C::intComs_triggerSnow(isSnow)` for snow mode.
4. `AdaynightCycle_C::SetFogDensity()` to recompute fog post-write.
5. `AdirectionalWind_C::setParameters(Intensity, Angle, Speed, Strength)`
   for wind.
6. `AmainGamemode_C::spawnBlackFog()` / `spawnRedSky()` /
   `Spawn Bad Sun()` triggered by event-actor spawn packets (sep packet).
7. Lightning strikes: per-strike packet `(Location, Tag, Radius, debris)`
   driving a `UWorld::SpawnActor<AlightningStrike_C>` on the receiver.

### 12.3 Client suppression set

To make the host authoritative, suppress on the CLIENT:
- `AdaynightCycle_C::timerRain` (periodic rain roll)
- `AdaynightCycle_C::timerLightning` (periodic strike roll)
- `AdaynightCycle_C::permaRain_timer` (heartbeat for perma-rain)
- `AdaynightCycle_C::fogEvent` / `superFogEvent` / `spawnFog` (fog spawners)
- `AmainGamemode_C::spawnBlackFog` / `spawnRedSky` / `Spawn Bad Sun`
  (event-actor spawners)
- `AoutsideChurch_C::init_lightningStorm` / `lightningStrike` /
  `truckLightning` / `fireLightningAtPlayer` (church-event strike spawners)

Suppression pattern: same as
[[project-coop-aprop-lifecycle-RE-inc3-scope]] — PRE-hook the UFunction,
check IsClient, set RetVal/skip-rest, no-op.

---

## Section 13 — Open questions / not-yet-RE'd

1. **`AmainGamemode_C::playWtr(FString NewParam)`** — what does this do?
   Single function, name ambiguous between "play weather (audio)" and
   "play water". Body is in ubergraph. Needs IDA or UE4SS Lua probe.
2. **`Aevent_fleshRain_C` spawn site** — what spawns it and from where?
   The CXX dump shows zero callers. Needs BP/IDA inspection.
3. **`AdaynightCycle_C` per-tick CPU cost on client when suppressed** —
   `setRainParticles`, `setRainParameters`, `setWindParameters`,
   `SetFogDensity` are likely called from `ReceiveTick`. If suppression
   is at `timerRain` level only, the per-frame derived calls still run
   (cheap, visual-only). If host wants to drive EVERY derived value,
   we need to suppress more aggressively. Performance audit gating per
   CLAUDE.md "After shipping, audit with agents" rule.
4. **Lightning thunder audio delay** — the `audio_far` vs `audio_close`
   on `AlightningStrike_C` (lightningStrike.hpp:9-10) suggests the
   thunder-after-flash delay is BP-internal. Per-peer reproduction:
   broadcast the strike location, both peers compute their own
   `flash→thunder` delay based on local listener position. NO need to
   sync audio playback timing — it derives from `(listenerLoc -
   strikeLoc)` distance locally.
5. **`Alakeglow_C`** — `mainGamemode::event_lakeglow @0x0C60` is in
   the weather-adjacent pointer set. Not RE'd in this pass; likely a
   pure visual event tied to water.
6. **Permanent fog ON the day-night-cycle: `permafog` vs `permanent`
   on `AweatherFogController_C`** — distinction unclear. Needs BP body
   inspection.
7. **`AmainGamemode_C::windParticles_check()`** — when is this called?
   Likely from a wind state change.

These do not block the wire-packet design; they are follow-ups for the
implementation phase.

---

## Section 14 — Cross-references to the methodology

- **Authority pattern (RULE 1 root cause)**: host-authoritative weather
  mirrors the same shape used for mushrooms ([[project-coop-mushroom-
  state-RE]]) and NPC spawners ([[project-coop-aprop-lifecycle-RE-inc3-
  scope]]). The authority is the actor holding the state, the suppression
  is the periodic scheduler on the client. Same pattern, different actor.
- **Engine-substrate (principle 7)**: the weather actors live behind
  `ue_wrap` — a new `ue_wrap/daynight_cycle.hpp` wrapping
  `AdaynightCycle_C` field reads / writes / UFunction dispatches; a
  `ue_wrap/wind.hpp` wrapping `AdirectionalWind_C::setParameters`; new
  `ue_wrap/weather_event.hpp` family for `AweatherFogController_C`,
  `AblackFog_C`, `AredSkyEvent_C`, `AbadSun_C`, `AlightningStrike_C`.
- **No save mutation**: weather state is runtime-only; the wire path is
  the SOLE persistence layer for in-progress storms. Snapshot-on-connect
  (Phase 5S0) handles the *static* save objects; a separate "live state
  resync on join" handshake covers weather + flashlight + similar
  transient flags (cf. [[votv-flashlight-RE-2026-05-25]] Section 6).
- **Whole-map sync (RULE)**: weather is map-wide; no AOI culling. Single
  small reliable packet per state change, broadcast to all peers.
- **Verbatim user scope**: "syncing weather effects, all weathers" —
  this RE covers ALL of: rain, snow, fog (both EHF and the fog-event
  variant), wind, lightning (incl. church storm), blackFog, redSky,
  badSun. Event_fleshRain and Lakeglow are story-event sub-cases noted
  but not assumed; if user expansion clarifies they are weather-
  adjacent, the same event-actor-presence packet pattern handles them.
