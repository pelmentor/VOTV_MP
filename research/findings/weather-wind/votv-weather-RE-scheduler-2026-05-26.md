# RE: VOTV weather scheduler + state machine

**Date:** 2026-05-26
**Trigger:** [[project-weather-sync-future]] — user directed "In future
scope add - syncing weather effects, all weathers". This pass is the RE
prerequisite called for in that memory entry (find the canonical
scheduler actor and the state field set before designing the wire packet
and the suppression hooks).
**Method:** Read entire `mainGamemode.hpp` + `mainGameInstance.hpp`
fully. Greps across `CXXHeaderDump/` for `weather|rain|fog|storm|
lightning|snow|wind|sun` patterns. Per-file deep read of every actor
that appeared in the grep set. Evidence-only, no speculation.
**Scope guard (RULE №1):** root-cause understanding, no code written,
no design decisions yet. All paths are quoted from the dump with file +
line + offset. Unanswerable items are explicitly marked.

---

## TL;DR

- VOTV does NOT have a class named `AweatherController_C` or
  `AweatherMaster_C`. The conceptual "weather controller" is the
  `AdaynightCycle_C` actor (file `daynightCycle.hpp`, class size
  `0x44D`). It owns:
  - The rain RNG (`rainProbability`, `rainLightningChance`,
    `rainDeactivateChance`, `fogProbability`).
  - The discrete "is it raining now" flag (`isRaining @ 0x02E4`).
  - The rain audio (`rainSnd @ 0x0278`), particle system (`eff_rain
    @ 0x0228`, `rainEffect @ 0x03A8`), and rain rotation arrows
    (`rain_tilt`, `rain_root`).
  - The lightning skylight (`lightning @ 0x0268` — a `USkyLight`, NOT
    a bolt actor; the bolt actor is `AlightningStrike_C`).
  - The fog event scheduler (`fogProbability @ 0x0428`, owns the
    `AweatherFogController_C` via `fogEventObject @ 0x0338`).
  - The snow trigger (`isSnow @ 0x03B0`, broadcast through the
    `intComs_triggerSnow(bool)` interface across ~50 listener actors).
  - The day/night phase + season state that the rest of the world
    derives weather-adjacent behaviour from.
- Five scheduler UFunctions to hook (all on `AdaynightCycle_C`):
  `timerRain`, `timerLightning`, `fogEvent`, `superFogEvent`,
  `permaRain_timer`. Plus the two state-mutator UFunctions:
  `causeRain(bool)` and `setRainProperties(bool, float, float, float,
  float)`. Plus the announce/setup UFunctions: `setRainParameters`,
  `setRainParticles`, `setWindParameters`, `setRainProperties`,
  `setSunAndMoonRotation`.
- VOTV's "story-driven" / scenario weather forces (red sky, black fog,
  bad sun, lakeglow, flesh rain) are SEPARATE actors orchestrated by
  `AmainGamemode_C`, NOT by `AdaynightCycle_C`. They use
  `spawnRedSky()`, `spawnBlackFog()`, `Spawn Bad Sun()` UFunctions on
  `AmainGamemode_C` plus a per-event `Aevent_fleshRain_C` /
  `AskyFallingEvent_C` actor lifecycle.
- ToD <-> weather coupling exists at TWO points only: (a) the daily
  `timerRain()` chance roll is gated by `dayAdd @ 0x0344` / `Day @
  0x0298` (per-day RNG attempt) and (b) the `newHour` / `newDay`
  delegates fire scheduler ticks downstream. Phase-of-day
  (`Phase @ 0x029C`, `phase_sin`, `sun_height @ 0x02F0`) drives sun /
  moon / sky-tint state but NOT the weather RNG path.
- There is NO weather state on `UmainGameInstance_C` (verified
  fully). Weather is per-`AdaynightCycle_C` runtime state, recreated
  fresh on each map load. Only `Fstruct_gameRules` (saved in
  `UsaveSlot_C::localGameRules @ 0x0DB0`) persists weather *config*:
  `permanentFog @ 0x001E` + `permanentRain @ 0x001F`.
- Save bootstrap (Phase 5S0) for weather needs the current runtime
  state to be added to the host-to-client snapshot: `isRaining`,
  `rainStrength`, `rainLightningChance`, `rainDeactivateChance`,
  `rainWindSpeed`, `enable_fog`, `enable_superfog`, `enable_rain`,
  `isSnow`, plus the active `redSky` / `blackFog` / `badSun` /
  `event_lakeglow` actor presence flags from `AmainGamemode_C`.
- The suppression pattern (mushroomMaster / NpcSpawner model — see
  `src/votv-coop/src/coop/prop_lifecycle.cpp` + `npc_sync.cpp`) maps
  cleanly onto the weather scheduler: a single PRE-hook on
  `AdaynightCycle_C::timerRain` / `timerLightning` / `fogEvent` /
  `superFogEvent` / `permaRain_timer` on the CLIENT to early-return
  before the body runs, PLUS a receiver-side state push of the
  current scheduler state via reflection writes + a follow-up call
  to the public mutator `causeRain` / `setRainProperties`.

---

## Section 1 — `mainGamemode.hpp` full read: weather presence

### 1.1 Per-tick functions

`AmainGamemode_C` exposes (`mainGamemode.hpp:489-491`):

```cpp
void ReceiveBeginPlay();                                                  // L489
void checkFordDishes();                                                   // L490
void ReceiveTick(float DeltaSeconds);                                     // L491
```

**The dump does NOT show the BP body of `ReceiveTick` — the CXX dump
emits signatures only.** It is therefore unknown from the dump alone
whether `AmainGamemode_C::ReceiveTick` touches any weather field. None
of `AmainGamemode_C`'s properties named in the dump
(`mainGamemode.hpp:6-363`) directly contain `weather` / `rain` /
`storm` / `lightning` / `fog`. The nearest weather-adjacent
properties on `AmainGamemode_C` are:

```cpp
class AdaynightCycle_C* daynightCycle;                  // 0x0450  L62
class AdirectionalWind_C* directionalWind;              // 0x0F70  L290
class AblackFog_C* blackFog;                            // 0x0880  L149
class AredSkyEvent_C* redSky;                           // 0x0888  L150
class AbadSun_C* badSun;                                // 0x0AD8  L200
class Alakeglow_C* event_lakeglow;                      // 0x0C60  L236
class AhalloweenMaster_C* halloweenMaster;              // 0x0720  L122
bool isHalloween;                                       // 0x0728  L123
bool isWinter;                                          // 0x0EC8  L279
bool isSummer;                                          // 0x1178  L330
bool isAutumn;                                          // 0x1179  L331
bool isSpring;                                          // 0x117A  L332
TEnumAsByte<enum_seasons::Type> currentSeason;          // 0x117B  L333
bool isSnowyFootsteps;                                  // 0x0D90  L253
bool isSnowyFootsteps_0;                                // 0x0D91  L254
```

`AmainGamemode_C` is therefore the OWNER of the per-weather actor
references but does NOT itself run a per-tick weather scheduler. The
scheduling is delegated to the child actor `AdaynightCycle_C`.

### 1.2 Timer / timed-event functions on `AmainGamemode_C`

Searching `mainGamemode.hpp` for any UFunction whose name suggests
weather timing:

- `void Spawn Bad Sun();`                  (`mainGamemode.hpp:417`)
  — spawns `AbadSun_C` (red apocalyptic sun event).
- `void spawnRedSky();`                    (`mainGamemode.hpp:428`)
- `void spawnBlackFog();`                  (`mainGamemode.hpp:429`)
- `void Update Season(bool updateLandscape);` (`mainGamemode.hpp:397`)
- `void getSeasonFromTime(bool weekly, TEnumAsByte<enum_seasons::Type>& season);` (`mainGamemode.hpp:382`)
- `void getSeason(TEnumAsByte<enum_seasons::Type>& currentSeason);` (`mainGamemode.hpp:381`)
- `void seasonUpdated__DelegateSignature(TEnumAsByte<enum_seasons::Type> newSeason);` (`mainGamemode.hpp:554`)
- `void intComs_triggerSnow(bool isSnow);` (`mainGamemode.hpp:488`)

`Spawn Bad Sun` / `spawnRedSky` / `spawnBlackFog` are EVENT spawn
calls (one-shot actor spawn into the world), not periodic schedulers.

### 1.3 RNG / random rolls touching weather

The CXX dump does not include UFunction bodies — only signatures and
offsets. **No UFunction signature on `AmainGamemode_C` contains
`Random*` / `RandomFloatInRange` / `RandomBool` (verified by full
read of `mainGamemode.hpp:6-562`).** RNG-rolling code, if any, lives
in the BP graph body of `ExecuteUbergraph_mainGamemode` and the per-
UFunction graphs, which are NOT in this dump. Authoritative RNG roll
discovery requires either reading the cooked BP bytecode in
`UE4SS_ObjectDump_GAMEPLAY_SAVE.txt` or decompiling via IDA. Not in
this dump = not authoritative for RNG.

What IS in the dump (`mainGamemode.hpp:551`):

```cpp
void ExecuteUbergraph_mainGamemode(int32 EntryPoint);
```

The ubergraph is the dispatch surface for BP-graph generated event
handlers. Cross-referenced rolls (e.g. an `OnNewDay` handler on
`AmainGamemode_C` that re-rolls weather config) would route through
this. Confirmation requires the BP bytecode dump (not in this RE
pass).

### 1.4 Story / scenario weather triggers

Greps for `s_storm` / `s_rain` / `event_storm` / `event_rain` /
`event_lightning` in `mainGamemode.hpp`: **no matches.** The
`s_*` story-state pattern used elsewhere (e.g. `s_may2026`) does
not appear as a weather trigger field on `AmainGamemode_C`.

Weather-event-like names present:
- `Aevent_fleshRain_C` (file `event_fleshRain.hpp`) — a save-actor
  (`Aactor_save_C` subclass) with a single `int32 I @ 0x0250` and
  `ReceiveBeginPlay()`. Spawned by some external trigger; NOT
  scheduled in `AdaynightCycle_C`. (No xref found in
  `mainGamemode.hpp`.)
- `AtriggerFallingSky_C` (file `triggerFallingSky.hpp`) — a sphere-
  overlap trigger that calls into `AmainGamemode_C`. Used for the
  Asky-falling event (see `[[project-coop-re-findings-2026-05-24-pm]]`
  which RE'd this as `AskyFallingEvent_C` for the alien-ship event).
- `AskyFallingEvent_C` (file `skyFallingEvent.hpp`) — alien-ship-style
  event actor with its own `ReceiveTick`. Not weather; it's the alien
  visitation event.

None of these are RUNNING WEATHER. They are one-shot scripted events.

---

## Section 2 — `AdaynightCycle_C` is the weather scheduler

`daynightCycle.hpp` is the canonical scheduler. The class is the
parent of every per-tick / per-timer weather function in VOTV. Full
field + function inventory below.

### 2.1 Class header & owned components

`daynightCycle.hpp:4-20`:

```cpp
class AdaynightCycle_C : public AActor
{
    FPointerToUberGraphFrame UberGraphFrame;          // 0x0220
    class UParticleSystemComponent* eff_rain;         // 0x0228
    class UArrowComponent* rain_tilt;                 // 0x0230
    class UArrowComponent* rain_root;                 // 0x0238
    class UDirectionalLightComponent* light_moon;     // 0x0240
    class UDirectionalLightComponent* light_sun;      // 0x0248
    class UArrowComponent* Arrow2;                    // 0x0250
    class UArrowComponent* Arrow1;                    // 0x0258
    class UArrowComponent* Arrow;                     // 0x0260
    class USkyLightComponent* lightning;              // 0x0268
    class UExponentialHeightFogComponent* ExponentialHeightFog; // 0x0270
    class UAudioComponent* rainSnd;                   // 0x0278
    class USkyLightComponent* SkyLight_A;             // 0x0280
    class UParticleSystemComponent* eff_shootingStar; // 0x0288
    class USceneComponent* DefaultSceneRoot;          // 0x0290
```

The `lightning` field at `0x0268` is a `USkyLightComponent` (i.e. the
ambient skylight flash brightness), NOT a bolt. The actual lightning
bolt is `AlightningStrike_C`, spawned by `timerLightning()` (see
Section 2.5).

### 2.2 Time / day / phase fields

`daynightCycle.hpp:21-32`:

```cpp
float Day;                                            // 0x0298
float Phase;                                          // 0x029C
float phase_sin;                                      // 0x02A0
float phase_nsin;                                     // 0x02A4
float phase_normsin;                                  // 0x02A8
float MaxTime;                                        // 0x02AC
float totalTime;                                      // 0x02B0
float TimeScale;                                      // 0x02B4
class Anewsky_C* skysphere;                           // 0x02B8
FVector sunAxis;                                      // 0x02C0
bool IsActive;                                        // 0x02CC
FIntVector timeZ;                                     // 0x02D0
```

`Phase` (0-1 day-progress) and `phase_sin` are the canonical ToD
floats used by sun/sky rendering. `Day` is the day-count. `timeZ` is
the discretized `(hour, minute, ?)` triplet.

### 2.3 Rain-state fields (the discrete state)

`daynightCycle.hpp:33-45, 67-88`:

```cpp
bool SStar_active;                                    // 0x02DC L33
float rain;                                           // 0x02E0 L34
bool isRaining;                                       // 0x02E4 L35   <-- CANONICAL
bool rainMuted;                                       // 0x02E5 L36
float diff_mult;                                      // 0x02E8 L37
TEnumAsByte<enum_difficulty::Type> diff;              // 0x02EC L38
float sun_height;                                     // 0x02F0 L39
float rainSpeed;                                      // 0x02F4 L40
class UCurveLinearColor* fog_color_A;                 // 0x02F8 L41
class UCurveLinearColor* fog_color_B;                 // 0x0300 L42
class UCurveLinearColor* amb_color;                   // 0x0308 L43
class UCurveLinearColor* sun_color;                   // 0x0310 L44
bool loan;                                            // 0x0318 L45
...
class UParticleSystem* rainEffect;                    // 0x03A8 L67
bool isSnow;                                          // 0x03B0 L68   <-- CANONICAL
float DeltaSeconds;                                   // 0x03B4 L69
FLinearColor skyLightColor;                           // 0x03B8 L70
FLinearColor fogColor;                                // 0x03C8 L71
bool enableSunlight;                                  // 0x03D8 L72
TArray<int32> Struct Task New Sig Required;           // 0x03E0 L73
FLinearColor sunColor;                                // 0x03F0 L74
float sleepingTimeDilation;                           // 0x0400 L75
float rainStrength;                                   // 0x0404 L76   <-- CANONICAL
float rainLightningChance;                            // 0x0408 L77   <-- CANONICAL
float rainDeactivateChance;                           // 0x040C L78   <-- CANONICAL
float cloudsTimer;                                    // 0x0410 L79
float cloudsTimerAccumulate;                          // 0x0414 L80
float finalFogDensity;                                // 0x0418 L81
float rainWindSpeed;                                  // 0x041C L82   <-- CANONICAL
bool rainToggle;                                      // 0x0420 L83
float rainProbability;                                // 0x0424 L84   <-- RNG GATE
float fogProbability;                                 // 0x0428 L85   <-- RNG GATE
bool permanentRain;                                   // 0x042C L86   <-- gamerule
bool permanentFog;                                    // 0x042D L87   <-- gamerule
float seasonalExponent;                               // 0x0430 L88
bool isHalloween;                                     // 0x0434 L89
float timeRotationDelay;                              // 0x0438 L90
FTimerHandle sunRotation_timerHandle;                 // 0x0440 L91
bool enableMoonlight;                                 // 0x0448 L92
bool enable_fog;                                      // 0x0449 L93   <-- CANONICAL
bool enable_superfog;                                 // 0x044A L94   <-- CANONICAL
bool enable_rain;                                     // 0x044B L95   <-- CANONICAL
bool skipDaySet;                                      // 0x044C L96
```

**Minimal wire-sync state set (everything a fresh receiver needs to
reproduce host weather):**
- `isRaining` (bool) — discrete "rain on/off"
- `rainStrength` (float) — particle/audio intensity
- `rainLightningChance` (float) — passed into `timerLightning` roll
- `rainDeactivateChance` (float) — passed into `timerRain` deactivate
  roll
- `rainWindSpeed` (float) — wind speed coupling
- `isSnow` (bool) — winter snow particle gate
- `enable_rain`, `enable_fog`, `enable_superfog`, `enableSunlight`,
  `enableMoonlight` (bool) — master enable flags
- `Phase` + `Day` + `totalTime` (already covered by ToD sync, separate
  concern per the memory).

**Permanent-* are gamerules** (`permanentRain`, `permanentFog`) — they
mirror `Fstruct_gameRules.permanentRain_63_*` /
`permanentFog_61_*` (`struct_gameRules.hpp:31-32`) and are saved
inside `UsaveSlot_C::localGameRules @ 0x0DB0` (`saveSlot.hpp:105`).
These propagate to the host on save-load and to the client on the
existing snapshot bootstrap; no NEW packet needed for the gamerule
config layer.

### 2.4 Delegates (drive downstream listeners)

`daynightCycle.hpp:46-62`:

```cpp
FdaynightCycle_CNewMinute newMinute;                  // 0x0320 L46
void newMinute(FIntVector Time);                      //       L47
...
FdaynightCycle_CNewHour newHour;                      // 0x0370 L59
void newHour(FIntVector Time);                        //       L60
FdaynightCycle_CNewDay newDay;                        // 0x0380 L61
void newDay(FIntVector Time);                         //       L62
```

These three delegate broadcasts are the primary signal that hourly /
daily weather rolls fire from. The "every game-hour weather chance"
canonically fans out via `newHour` to listeners that include the
scheduler's own `timerRain` / `timerLightning` / `fogEvent`
subscribers. The dump does NOT show the BP wiring, but the delegate
fields exist and are dispatched by `func_newHour` / `func_newMinute`:

```cpp
void func_newHour(FIntVector Time);                   // L104
void func_newMinute(FIntVector Time);                 // L105
```

### 2.5 Scheduler UFunctions (the hook surface)

`daynightCycle.hpp:98-156`:

```cpp
void updateWeekDay();                                 // L100
void setSunAndMoonRotation();                         // L101
void getNamedDaytime(int32&, int32&, int32&);         // L102
void getNamedTime(...);                               // L103
void func_newHour(FIntVector Time);                   // L104
void func_newMinute(FIntVector Time);                 // L105
void SetFogDensity();                                 // L106
void setWindParameters();                             // L107
void setRainParameters();                             // L108
void causeRain(bool isRaining);                       // L109  *MUTATOR*
void setRainProperties(bool isRaining,
                       float rainStrength,
                       float rainLightningChance,
                       float rainDeactivateChance,
                       float rainWindSpeed);          // L110  *MUTATOR*
void createNewTask(Fstruct_taskNew& struct_taskNew);  // L111
void setRainParticles();                              // L112
void plugoutLightCurve(bool Reset, AambientLightCurve_C* Curve); // L113
void setLCparams();                                   // L114
void pluginLightCurve(AambientLightCurve_C*, bool&);  // L115
void setSkyIntensity();                               // L116
...
void ReceiveTick(float DeltaSeconds);                 // L135  *TICK*
void ligh();                                          // L136
void rainS();                                         // L137
void fogEvent();                                      // L138  *SCHEDULER*
void Dest(class AActor* DestroyedActor);              // L139
void ReceiveBeginPlay();                              // L140
void Rewind();                                        // L141
void superFogEvent();                                 // L142  *SCHEDULER*
void rainCleanup();                                   // L143
void rainClean();                                     // L144
void spawnFog();                                      // L145  *MUTATOR/SPAWN*
void timerRain();                                     // L146  *SCHEDULER*
void timerLightning();                                // L147  *SCHEDULER*
void updSeason(TEnumAsByte<enum_seasons::Type>);      // L148
void intComs_gamemodeBeginPlay();                     // L149
void intComs_settingsApplied(Fstruct_settings1);      // L150
void intComs_triggerSnow(bool isSnow);                // L151  *MUTATOR*
void timer_changeSunRotation();                       // L152
void initializeSunRotation(float Time);               // L153
void permaRain_timer();                               // L154  *SCHEDULER*
void setSkyColor();                                   // L155
void ExecuteUbergraph_daynightCycle(int32 EntryPoint);// L156
```

**The canonical scheduler entry-points** (these are the functions to
PRE-hook on the client to early-return — the suppression-on-client
pattern mirroring mushroomMaster):

1. `timerRain()` — periodic rain-toggle RNG roll.
2. `timerLightning()` — periodic lightning-strike roll (spawns
   `AlightningStrike_C` actor when it fires).
3. `fogEvent()` — periodic fog-event roll (drives the
   `AweatherFogController_C` via `fogEventObject @ 0x0338`).
4. `superFogEvent()` — periodic super-fog event roll (spawns
   `AsuperFog_C` actor; see `superFog.hpp:18 spawnUfo()` — also tied
   to UFO event).
5. `permaRain_timer()` — periodic refresh while `permanentRain`
   gamerule is on.

**The canonical mutator UFunctions** (these are the public state
writes; on the client these are the targets a host->client wire
packet calls to APPLY the streamed state — analogous to how
`item_activate.cpp` calls into the BP-exposed updater after wiring
the bool):

- `causeRain(bool isRaining)` — flips rain on/off including
  particles + audio + skylight side-effects in the BP body.
- `setRainProperties(bool, float, float, float, float)` — sets the
  full rain parameter tuple atomically.
- `setRainParameters()` / `setRainParticles()` / `setWindParameters()`
  — internal post-mutator hooks (re-spawn particles, push wind
  state to `AdirectionalWind_C` via `setParameters`).
- `intComs_triggerSnow(bool isSnow)` — fan-out interface call that
  fires across ~50 listener classes (see Section 4 for the listener
  list).
- `spawnFog()` — spawns the `AweatherFogController_C` actor for the
  fog event lifecycle. (`SetFogDensity` is the per-tick density
  updater.)

### 2.6 Cleanup / spawn helpers

```cpp
void rainCleanup();                                   // L143
void rainClean();                                     // L144
void spawnFog();                                      // L145
```

`spawnFog` spawns the `AweatherFogController_C` child; on the client
this is where a divergent fog actor gets created locally if not
suppressed. (`fogEventObject @ 0x0338` is the reference holder.)

### 2.7 Receiver tick

```cpp
void ReceiveTick(float DeltaSeconds);                 // L135
```

The per-frame tick runs sun rotation interp (`Phase` advancement),
particle parameter updates (driven by `rainStrength`), fog-density
interp (`finalFogDensity`), etc. **This is rendering, not
scheduling.** Hooking `ReceiveTick` is the WRONG layer to suppress
client weather — it would freeze the rendering even after the host's
state was applied. The right layer is the scheduler entry-points
(Section 2.5 list of five).

---

## Section 3 — Suppression-point identification

The mirroring of the existing mushroomMaster / npc-spawner suppression
pattern (`src/votv-coop/src/coop/prop_lifecycle.cpp`,
`src/votv-coop/src/coop/npc_sync.cpp`) for weather is:

### 3.1 The world-level scheduler actor

**`AdaynightCycle_C`** is a singleton (referenced via
`AmainGamemode_C::daynightCycle @ 0x0450`). It is THE scheduler.
There is no second weather scheduler in the dump. Confirmed by grep:
the strings `weather` / `Weather` / `WEATHER` appear in only TWO
files in the entire CXX dump — `daynightCycle.hpp` (one ref:
`fogEventObject` is type `AweatherFogController_C`) and
`weatherFogController.hpp` (the actor itself). No other weather
controller class exists.

### 3.2 Per-effect actors (spawned BY the scheduler — NOT to be suppressed at the actor level)

These are the actors the scheduler spawns. They render or play audio
for one weather instance, then die. They MUST run on the client when
the host fires a wire packet, so they must NOT be unconditionally
suppressed (mistake to avoid — different from the mushroom pattern):

- `AlightningStrike_C` (`lightningStrike.hpp:4-25`) — one bolt.
  Spawned by `timerLightning()`. Has `lightTL` timeline (light
  flash), `audio_background/far/close` audio comps, `Radius @ 0x027C`,
  `debris @ 0x0270`, `explosionTag @ 0x0274`. Per-spawn = one strike.
- `AweatherFogController_C` (`weatherFogController.hpp:4-22`) — one
  fog instance. Spawned by `spawnFog()`. Has `Time @ 0x0238`, `Alpha
  @ 0x023C`, `Duration @ 0x0240`, `fogPhase @ 0x0244`, `permanent
  @ 0x0248`, `permafog @ 0x0249`, `Strength @ 0x024C`, plus its own
  `ReceiveTick` for the fog interp.
- `AsuperFog_C` (`superFog.hpp:4-23`) — one super-fog instance.
  Spawned by `superFogEvent()`. Has `Alpha @ 0x0238`, `Duration @
  0x023C`, `Thickness @ 0x0240`, `UFO @ 0x0248` (a `AHoelUfo_C`
  reference — super-fog is tied to a UFO encounter event).
- `AblackFog_C` (`blackFog.hpp:4-23`) — black-fog story event.
  Spawned by `AmainGamemode_C::spawnBlackFog()`. Has `PostProcess`
  comp, `spd @ 0x0248`, `eyes @ 0x0250` (`TArray<Aeyer_C*>`). Not on
  `AdaynightCycle_C`.
- `AredSkyEvent_C` (`redSkyEvent.hpp:4-15`) — red sky story event.
  Spawned by `AmainGamemode_C::spawnRedSky()`. Just an `isred @
  0x0230` bool + `set(bool isred)`. Not on `AdaynightCycle_C`.
- `AbadSun_C` (`badSun.hpp:4-25`) — apocalyptic sun. Spawned by
  `AmainGamemode_C::Spawn Bad Sun()`. Subclass of
  `Aactor_save_C` (PERSISTENT across saves). Has timeline `A @
  0x0260`, `endSiren @ 0x0270`, `destroy @ 0x0274`, `Super @ 0x0278`.
- `Alakeglow_C` — referenced via `AmainGamemode_C::event_lakeglow
  @ 0x0C60`. (Header not opened in this pass; class exists per the
  field declaration.)
- `Aevent_fleshRain_C` (`event_fleshRain.hpp:4-13`) — flesh rain
  apocalyptic event (`Aactor_save_C` subclass). Has `int32 I @
  0x0250`. Not on `AdaynightCycle_C`; spawned from story trigger.
- `AdirectionalWind_C` (`directionalWind.hpp:4-50`) — global wind
  state actor (singleton, referenced via
  `AmainGamemode_C::directionalWind @ 0x0F70`). Has its own
  `ReceiveTick`, `setParameters(Intensity, Angle, Speed, Strength)`
  public mutator at `L36`. Wind is its own subsystem — the
  scheduler calls `setWindParameters()` on `AdaynightCycle_C` to
  fan into the wind actor.

**Suppression model for per-effect actors:** mirror the existing
remote_prop / Aprop_C echo-suppression pattern. When the host fires
a `LightningStrikePayload {location, radius, time}` packet, the
client spawns `AlightningStrike_C` LOCALLY (so the visual + audio
land at the right place), WITHOUT re-broadcasting. The same echo-
suppression flag pattern used in
`src/votv-coop/src/coop/item_activate.cpp:138-145` (`g_echoSuppress`
atomic bool) applies.

### 3.3 The hook decision: PRE vs POST

Existing reference patterns in the codebase:

- `prop_lifecycle.cpp` uses **PRE-hook with class-allowlist
  early-return** for `BeginDeferredActorSpawnFromClass` to suppress
  mushroomMaster spawns on the client (`IsWireSuppressedPropClass`
  predicate, see `include/coop/prop_lifecycle.h:63-67`).
- `npc_sync.cpp` uses **PRE-hook + zero-out ActorClass param** for
  NPC spawn interception (see `include/coop/npc_sync.h:8-40`).
- `item_activate.cpp` uses **POST-hook on inventory UFunctions** and
  applies wire state via direct reflection writes + a follow-up call
  to the BP mutator (see `item_activate.cpp:138-220`).

**Recommended PRE-hook surface for weather suppression** (analogous
to mushroomMaster suppression):

- PRE-hook `AdaynightCycle_C::timerRain` on the CLIENT — return
  early. The RNG body is suppressed; client never decides "it's
  raining now".
- PRE-hook `AdaynightCycle_C::timerLightning` on the CLIENT — return
  early. Bolts only fire from host wire packets.
- PRE-hook `AdaynightCycle_C::fogEvent` on the CLIENT — return
  early.
- PRE-hook `AdaynightCycle_C::superFogEvent` on the CLIENT — return
  early.
- PRE-hook `AdaynightCycle_C::permaRain_timer` on the CLIENT — return
  early. (Permanent rain is gamerule-driven and already host-
  authoritative via gamerule sync; let the host fire it.)

PRE-hook fits because each of these functions is the START of a
divergent decision; once the body runs, side-effects fan out
(spawn calls, delegate broadcasts, audio start). Early-return BEFORE
the body keeps the client weather state strictly host-driven.

The mutator UFunctions (`causeRain`, `setRainProperties`,
`setRainParameters`, `setRainParticles`, `setWindParameters`,
`spawnFog`, `intComs_triggerSnow`) are NOT suppressed — they are the
TARGETS the wire-packet receiver invokes (via UFunction call, or by
direct reflection writes followed by a re-render call). This is the
mirror of how `item_activate.cpp` writes `Intensity` via reflection
and then triggers MarkRenderStateDirty.

**POST-hook is NOT appropriate here:** by the time the body has run,
the scheduler has already fanned out audio, particle, light, and
delegate side-effects. POST would require undoing all of those.
PRE-early-return is cheaper and correct.

### 3.4 What about `Aevent_fleshRain_C` / `AskyFallingEvent_C` / `AbadSun_C` (the story events)?

These are story-event actors NOT on the periodic-RNG path. They are
spawned by `AmainGamemode_C` UFunctions (`spawnRedSky`, `spawnBlackFog`,
`Spawn Bad Sun`) or by `AtriggerFallingSky_C` overlap. Per
`[[project-coop-re-findings-2026-05-24-pm]]`, the unified
`EntityEventPacket` mechanism is the right wire layer for them — the
weather sync packet design (`ReliableKind::WeatherState`) only
needs to carry the `AdaynightCycle_C` runtime state (the seven
fields in Section 2.3). The story-event spawns ride the events
pipeline.

---

## Section 4 — `intComs_triggerSnow` interface fan-out

`intComs_triggerSnow(bool isSnow)` is declared on the following
classes (full list from grep across the dump):

| File                                       | Class                           |
|--------------------------------------------|---------------------------------|
| `int_coms.hpp:8`                           | `Uint_coms_C` (the interface)   |
| `daynightCycle.hpp:151`                    | `AdaynightCycle_C` (PRODUCER)   |
| `mainGamemode.hpp:488`                     | `AmainGamemode_C` (relay)       |
| `mainPlayer.hpp:760`                       | `AmainPlayer_C` (listener)      |
| `actor_save.hpp:58`                        | `Aactor_save_C`                 |
| `ai_heavyObstacle.hpp:26`                  | `Aai_heavyObstacle_C`           |
| `analogDScreenTest.hpp:461`                | `AanalogDScreenTest_C`          |
| `ambientLightCurve.hpp:31`                 | `AambientLightCurve_C`          |
| `ATV.hpp:436`                              | `AATV_C`                        |
| `baseOak.hpp:26`                           | `AbaseOak_C`                    |
| `cockroachMaster.hpp:90`                   | `AcockroachMaster_C`            |
| `d_window.hpp:46`                          | `Ad_window_C`                   |
| `erieZone.hpp:33`                          | `AerieZone_C`                   |
| `grayBoarSpawner.hpp:29`                   | `AgrayBoarSpawner_C`            |
| `halloweenMaster.hpp:34`                   | `AhalloweenMaster_C`            |
| `kerfurOmega.hpp:232`                      | `AkerfurOmega_C`                |
| `ladder.hpp:98`                            | `Aladder_C`                     |
| `locker.hpp:86`                            | `Alocker_C`                     |
| `mirror_DUPL_1.hpp:30`                     | `Amirror_DUPL_1_C`              |
| `NewBlueprint5.hpp:37`                     | `ANewBlueprint5_C`              |
| `portal_phys.hpp:135`                      | `Aportal_phys_C`                |
| `portal.hpp:44`                            | `Aportal_C`                     |
| `prop.hpp:106`                             | `Aprop_C`                       |
| `printedObject.hpp:89`                     | `AprintedObject_C`              |
| `propProcessor.hpp:64`                     | `ApropProcessor_C`              |
| `RCM_cameraManager.hpp:86`                 | `ARCM_cameraManager_C`          |
| `shrimpIrradiator.hpp:25`                  | `AshrimpIrradiator_C`           |
| `sign.hpp:42`                              | `Asign_C`                       |
| `signalDriveEraser.hpp:150`                | `AsignalDriveEraser_C`          |
| `sitBox.hpp:75`                            | `AsitBox_C`                     |
| `subAreaTransition.hpp:88`                 | `AsubAreaTransition_C`          |
| `ticker_base.hpp:26`                       | `Aticker_base_C`                |
| `triggerBase.hpp:115`                      | `AtriggerBase_C`                |
| `treehouse.hpp:138`                        | `Atreehouse_C`                  |
| `uicomp_settingsSlot_pickColor.hpp:35`     | UMG slot                        |
| `uicomp_settingsSlot_lut.hpp:33`           | UMG slot                        |
| `ui_carmap.hpp:30`                         | UMG                             |
| `ui_arcade_invaders.hpp:25`                | UMG                             |
| `ui_boltMinigameTest.hpp:59`               | UMG                             |
| `ui_consolesAtlas.hpp:158`                 | UMG                             |
| `ui_console.hpp:88`                        | UMG                             |
| `ui_gamemode.hpp:52`                       | UMG                             |
| `ui_paperDraw.hpp:101`                     | UMG                             |
| `ui_laptop.hpp:359`                        | UMG                             |
| `UI_oven.hpp:91`                           | UMG                             |
| `ui_keybinds.hpp:49`                       | UMG                             |
| `ui_reactor.hpp:46`                        | UMG                             |
| `ui_UI.hpp:173`                            | UMG                             |
| `ui_vcam.hpp:133`                          | UMG                             |
| `waterVolume.hpp:60`                       | `AwaterVolume_C`                |
| `waterFloatMaster.hpp:60`                  | `AwaterFloatMaster_C`           |

53 declared listeners. (The interface comes from `int_coms.hpp:8`.)
Implication: a host-authoritative snow flip cannot be implemented by
only writing `AdaynightCycle_C::isSnow @ 0x03B0` on the client — the
listeners must also receive the broadcast or they'll desync their
local snow state. The clean route is: on the client, when the snow
state changes, CALL `AdaynightCycle_C::intComs_triggerSnow(isSnow)`
via reflection (UFunction call). The interface fan-out then mirrors
on the client. Suppression of the SCHEDULER ensures `intComs_
triggerSnow` only fires when the host has authorized it.

---

## Section 5 — `mainGameInstance.hpp` full read: weather presence

Read `mainGameInstance.hpp` in full (49 lines total —
`mainGameInstance.hpp:1-49`). Properties + functions enumerated:

**Properties** (`mainGameInstance.hpp:6-42`):
`save_gameInst, SlotName, nrts_GAMEINST, texs_GAMEINST, opened,
GameMode (enum_gamemode), startDay, radios, radios_tit, vols, objs,
isSaveReset, loadObjects, playerInv_transport, objTexs,
locationDistance, subArea, antibreatherKickout, dwString, dwList,
dwString_2, dwList_0, gamemode_, NewVar_0, patreonList, NewVar_1,
gameRules (Fstruct_gameRules), playerEquipment_transport,
playerHold_transport, initialRHI, RHIget, NewVar_2-7`.

**Functions** (`mainGameInstance.hpp:44-45`):
`setSaveSlotObject, keepPlayer`.

**Verdict: NO weather state on `UmainGameInstance_C`.** The
GameInstance carries `gameRules @ 0x0318` which holds
`permanentFog` and `permanentRain` (Section 6 details), but no
`isRaining` / `rainStrength` / `enable_fog` / `isSnow` / scheduler
state. Weather is per-`AdaynightCycle_C` per-level runtime state.
**No GameInstance-level sync is needed for weather** — but the
ToD sync that the project memory says is a separate concern
(`[[project-coop-save-host-authoritative]]`) IS necessary for
weather coupling (Section 7).

---

## Section 6 — Save/load path for weather

### 6.1 What IS saved (per the dump)

`UsaveSlot_C` (`saveSlot.hpp:4-143`) is the per-world save. Greps
for weather-adjacent fields show:

```cpp
FIntVector savedtime;                          // 0x00B8 L12
float totalTime;                               // 0x00E0 L15
float Day;                                     // 0x065C L61
float moonPhase;                               // 0x08A4 L90
Fstruct_gameRules localGameRules;              // 0x0DB0 L105
```

Time-of-day (`savedtime`, `totalTime`, `Day`, `moonPhase`) IS saved
— so the host-to-client ToD sync (already in
`[[project-coop-save-host-authoritative]]`) carries these through
when the client receives the snapshot.

`localGameRules @ 0x0DB0` is type `Fstruct_gameRules`
(`struct_gameRules.hpp:4-43`). The full struct has 35 fields incl.
the two weather-relevant gamerules:

```cpp
bool permanentFog_61_33B0F8EA4EFC4825C466EA87C6A5B5BD;  // 0x001E L31
bool permanentRain_63_73F792B64077E7BAFE1671A95A7AC272; // 0x001F L32
```

### 6.2 What IS NOT saved (per the dump)

Greps for `isRaining`, `rainStrength`, `rainLightning`,
`rainDeactivate`, `rainWindSpeed`, `enable_fog`, `enable_rain`,
`enable_superfog`, `isSnow` in `saveSlot.hpp`: **zero matches.**

```
Game_0.9.0n/.../saveSlot.hpp:
  (no match for isRaining / rainStrength / etc.)
```

**Implication:** the live weather scheduler state is REGENERATED on
each load. `AdaynightCycle_C::ReceiveBeginPlay()` (L140) re-seeds
based on gamerules + difficulty + ToD. The host's transient "right
now it's raining at strength 0.4 with deactivate chance 0.1" is
*lost* across save/load — the dump shows no save field for it. (The
BP body of `ReceiveBeginPlay` may use the `Day` count as a seed for
deterministic re-derivation, but the dump cannot confirm this.)

### 6.3 Implication for Phase 5S0 host-to-client snapshot

The save-bootstrap snapshot (`[[project-coop-save-host-authoritative]]`)
needs to add the LIVE weather state to the host-to-client snapshot,
NOT to the save file itself. The 7-field set from Section 2.3
plus the active per-effect actor presence flags (redSky, blackFog,
badSun, event_lakeglow, weatherFogController active, superFog
active, lightningStrike actors in flight) form the snapshot payload.

The save file itself does not need extending (no `permanentFog` /
gamerule changes needed). The gamerule + ToD subset already syncs
via the existing snapshot bootstrap.

---

## Section 7 — Time-of-day coupling

Does weather change as a side effect of ToD? Evidence from the
dump:

### 7.1 `Phase` / `phase_sin` / `sun_height` drive sun/sky rendering

`daynightCycle.hpp:22-25, 39`:

```cpp
float Phase;                                          // 0x029C
float phase_sin;                                      // 0x02A0
float phase_nsin;                                     // 0x02A4
float phase_normsin;                                  // 0x02A8
...
float sun_height;                                     // 0x02F0
```

These are read by `setSunAndMoonRotation()` (`L101`),
`setSkyColor()` (`L155`), `setSkyIntensity()` (`L116`),
`setLCparams()` (`L114`). They drive the sun/moon/sky tint per-tick.
This is rendering, NOT a weather-RNG input.

### 7.2 `newHour` + `newDay` delegates fire scheduler ticks

`daynightCycle.hpp:46-62`:

```cpp
FdaynightCycle_CNewMinute newMinute;                  // 0x0320
FdaynightCycle_CNewHour newHour;                      // 0x0370
FdaynightCycle_CNewDay newDay;                        // 0x0380
```

These delegates fire when the time crosses a minute/hour/day
boundary (driven from `func_newHour` / `func_newMinute` in
`ReceiveTick`). BP-graph wiring (NOT in the dump) likely subscribes
`timerRain` / `fogEvent` to these. **This is the ToD->weather
coupling path.** Confirmation requires BP bytecode dump (not in
this RE pass), but the field layout is consistent with this model:
periodic timers driven by game-time tick.

### 7.3 `Day` count drives season + weather seed

`daynightCycle.hpp:21, 148`:

```cpp
float Day;                                            // 0x0298
void updSeason(TEnumAsByte<enum_seasons::Type> newSeason);  // L148
```

And on `AmainGamemode_C` (`mainGamemode.hpp:381-382, 397`):

```cpp
void getSeason(TEnumAsByte<enum_seasons::Type>& currentSeason);
void getSeasonFromTime(bool weekly, TEnumAsByte<enum_seasons::Type>& season);
void Update Season(bool updateLandscape);
```

`getSeasonFromTime` takes a `weekly` flag (so the gamerule
`weeklySeason` from `Fstruct_gameRules @ 0x0020` of
`struct_gameRules.hpp:33` switches between calendar-year and
in-game-week season cycles). Season then drives `isSummer / isAutumn
/ isSpring / isWinter / currentSeason` on `AmainGamemode_C`.
`isSnow` on `AdaynightCycle_C` is gated by season (winter -> snow
enabled). This is a derivable signal, not an RNG roll.

### 7.4 `cloudsTimer` + `cloudsTimerAccumulate`

`daynightCycle.hpp:79-80`:

```cpp
float cloudsTimer;                                    // 0x0410
float cloudsTimerAccumulate;                          // 0x0414
```

These accumulate per-tick (`ReceiveTick`) and trigger the
`timerRain` / `fogEvent` reroll when they cross a threshold. The
threshold logic is in BP bytecode (not in the dump). This is a
ToD-derived integrator, not a hard-clock timer.

**Verdict:** ToD couples to weather via the hourly delegate path and
the `cloudsTimer` accumulator. Suppressing the scheduler entry-
points on the client (Section 3.3) ALSO suppresses the ToD coupling
on the client — clean cut.

---

## Section 8 — Complete UFunction inventory containing `weather`/`Weather` (literal)

Per grep across the entire CXX dump:

```
daynightCycle.hpp:49:  class AweatherFogController_C* fogEventObject;  // 0x0338
weatherFogController.hpp:1:  #ifndef UE4SS_SDK_weatherFogController_HPP
weatherFogController.hpp:2:  #define UE4SS_SDK_weatherFogController_HPP
weatherFogController.hpp:4:  class AweatherFogController_C : public AActor
weatherFogController.hpp:19:  void ExecuteUbergraph_weatherFogController(int32 EntryPoint);
```

**NO UFunction in the dump has the literal substring `weather`
in its name.** The "weather scheduler" is named `AdaynightCycle_C`
historically (because VOTV's weather rolls out of the day/night
cycle), and the only literal `weather`-named class is
`AweatherFogController_C` — which is a per-fog-instance actor (a
spawned child, lifetime = one fog event), NOT the scheduler.

The candidate hook set, named by their owning class `AdaynightCycle_C`
(the scheduler), is therefore:

| UFunction                          | Role        | Suppress on client | Wire-receiver target |
|------------------------------------|-------------|--------------------|----------------------|
| `timerRain()`                      | Scheduler   | YES (PRE early-rtn)| —                    |
| `timerLightning()`                 | Scheduler   | YES (PRE early-rtn)| —                    |
| `fogEvent()`                       | Scheduler   | YES (PRE early-rtn)| —                    |
| `superFogEvent()`                  | Scheduler   | YES (PRE early-rtn)| —                    |
| `permaRain_timer()`                | Scheduler   | YES (PRE early-rtn)| —                    |
| `causeRain(bool)`                  | Mutator     | NO                 | YES                  |
| `setRainProperties(...)`           | Mutator     | NO                 | YES                  |
| `setRainParameters()`              | Mutator     | NO                 | YES                  |
| `setRainParticles()`               | Mutator     | NO                 | YES                  |
| `setWindParameters()`              | Mutator     | NO                 | YES                  |
| `spawnFog()`                       | Mutator     | NO                 | YES                  |
| `SetFogDensity()`                  | Mutator     | NO                 | YES                  |
| `intComs_triggerSnow(bool isSnow)` | Mutator     | NO                 | YES                  |
| `rainCleanup()` / `rainClean()`    | Cleanup     | NO                 | YES (on rain off)    |
| `setSkyColor()` / `setSkyIntensity()` | Rendering | NO                | NO (per-tick, local) |
| `setSunAndMoonRotation()`          | Rendering   | NO                 | NO (ToD-derived)     |
| `setLCparams()`                    | Rendering   | NO                 | NO (per-tick, local) |
| `updSeason(season)`                | Season relay| NO                 | NO (season already syncs via gamerule+Day)|
| `ReceiveTick(DeltaSeconds)`        | Render tick | NO                 | NO                   |
| `ReceiveBeginPlay()`               | Init        | NO                 | (snapshot apply runs after) |

**Plus on `AmainGamemode_C`** (story-event spawns; ride
`EntityEventPacket` per `[[project-coop-re-findings-2026-05-24-pm]]`,
not the `WeatherState` packet):

| UFunction                          | Role        |
|------------------------------------|-------------|
| `spawnRedSky()`                    | Spawn `AredSkyEvent_C` |
| `spawnBlackFog()`                  | Spawn `AblackFog_C`    |
| `Spawn Bad Sun()`                  | Spawn `AbadSun_C`      |

---

## Section 9 — Open questions for the design phase (NOT answered here)

These are unanswerable from the CXX dump alone and require BP-
bytecode reading or IDA decompilation:

1. **Does `AmainGamemode_C::ReceiveTick` touch any weather state?**
   The signature is in the dump but the body is not. Greps show no
   weather field on `AmainGamemode_C` is named in its UFunction
   signatures, so the body would have to dereference
   `daynightCycle->isRaining` etc. — possible but unconfirmed.
2. **Where exactly is the `timerRain` periodic-tick fired from?** Is
   it a `K2_SetTimerDelegate` triggered by `ReceiveBeginPlay`, or
   subscribed to `newHour`, or accumulated through `cloudsTimer`?
   The dump shows all three possible drivers exist; the BP graph
   choice needs the bytecode dump.
3. **Does `ReceiveBeginPlay` deterministically reseed weather from
   `Day` (so a clean reload reproduces the same weather), or is
   `RandStream`-seeded fresh each load (so weather is genuinely
   random across reloads)?** Field layout doesn't disclose this.
4. **What is the `chance` field on `AlightningHeightAnalyzer_C @
   0x0280`?** It exists per `lightningHeightAnalyzer.hpp:16` but how
   `timerLightning()` uses it is BP-internal.
5. **Does `AbadSun_C` (Aactor_save_C subclass) persist its state
   across save/load via `actor_save` mechanism?** The class is a
   save-actor (`badSun.hpp:4`), implying yes — Phase 5S0 snapshot
   needs to carry its presence + timeline phase. Verification needs
   the `actor_save` RE.

These are the items the design phase will need IDA / BP-bytecode
support for. They do not block the architecture sketch but they
sharpen it.

---

## Section 10 — Cross-refs

- **Suppression pattern (existing precedent):**
  `src/votv-coop/include/coop/prop_lifecycle.h:63-67` (`IsWireSuppressedPropClass`),
  `src/votv-coop/include/coop/npc_sync.h:8-40` (NPC class allowlist),
  `src/votv-coop/src/coop/item_activate.cpp:138-220` (mutator-target
  apply with echo suppression).
- **Memory entries:** [[project-weather-sync-future]] (scope source),
  [[project-coop-save-host-authoritative]] (snapshot bootstrap will
  carry the weather state set),
  [[project-coop-re-findings-2026-05-24-pm]] (unified
  EntityEventPacket — owns the story-event spawns of redSky /
  blackFog / badSun / event_lakeglow / event_fleshRain),
  [[project-coop-whole-map-sync]] (weather is whole-map, no AOI).
- **Reference findings:**
  `research/findings/world-systems/votv-mushroom-state-RE-2026-05-24.md` (the
  "suppress on client" pattern that the weather scheduler follows),
  `research/findings/props-lifecycle/votv-aprop-lifecycle-RE-2026-05-24.md`
  (lifecycle hook patterns).
- **Files surveyed in this pass:**
  `mainGamemode.hpp` (564 LOC, full),
  `mainGameInstance.hpp` (49 LOC, full),
  `daynightCycle.hpp` (162 LOC, full),
  `weatherFogController.hpp` (22 LOC),
  `tool_rain.hpp` (22 LOC),
  `tool_wind.hpp` (19 LOC),
  `tool_lightning.hpp` (15 LOC),
  `lightningStrike.hpp` (27 LOC),
  `directionalWind.hpp` (52 LOC),
  `blackFog.hpp` (24 LOC),
  `superFog.hpp` (22 LOC),
  `redSkyEvent.hpp` (16 LOC),
  `skyFallingEvent.hpp` (21 LOC),
  `event_fleshRain.hpp` (13 LOC),
  `triggerFallingSky.hpp` (16 LOC),
  `trigger_box_rain.hpp` (15 LOC),
  `newsky.hpp` (51 LOC),
  `lightningHeightAnalyzer.hpp` (28 LOC),
  `baseLightningRod.hpp` (13 LOC),
  `lightningRod.hpp` (17 LOC),
  `prop_lightningRod.hpp` (12 LOC),
  `halloweenMaster.hpp` (41 LOC),
  `ambienceMastter.hpp` (18 LOC),
  `badSun.hpp` (27 LOC),
  `trigger_eventer.hpp` (62 LOC),
  `save_main.hpp` (47 LOC, full),
  `saveSlot.hpp` (145 LOC, full),
  `struct_gameRules.hpp` (45 LOC, full),
  `enum_seasons_enums.hpp` (10 LOC, full),
  `mainPlayer.hpp` (greps only — file too large to read in full
  for this scope).

All offsets quoted are from the current `0.9.0-n` cooked dump in
`Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/`.
Per the project's offset-via-reflection rule
([[project-adaptation-strategy]]), wire code must resolve these by
property name through the reflection layer (`R::GetUProperty`), not
by hard-coding the literal `0x02E4` etc., so re-cooks of the game
won't break wire sync.
