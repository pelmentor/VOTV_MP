# RE: VOTV Weather Effect Actors — Deep RE pass

**Date**: 2026-05-26
**Target**: Alpha 0.9.0-n
**Method**: CXXHeaderDump static analysis only — `lightningStrike.hpp`,
`lightningRod.hpp`, `baseLightningRod.hpp`, `lightningHeightAnalyzer.hpp`,
`event_fleshRain.hpp`, `trigger_box_rain.hpp`, `tool_rain.hpp`, `newsky.hpp`,
`blackFog.hpp`, `superFog.hpp`, `weatherFogController.hpp`,
`directionalWind.hpp`, `skyFallingEvent.hpp`, `triggerFallingSky.hpp`,
`redSkyEvent.hpp`, `daynightCycle.hpp`, `actor_save.hpp`, `mainGamemode.hpp`,
`mainPlayer.hpp`, `eelecPole.hpp`, `prop_lightningRod.hpp`,
`tool_wind.hpp`, `tool_lightning.hpp`, `prop_physModule_lightning.hpp`,
`prop_physModule_stormfilter.hpp`, `prop_winder.hpp`, `windturbine.hpp`,
`skyUfo.hpp`, `prop_skypiece.hpp`, `ambientLightCurve.hpp`, `ambientLight.hpp`.
No IDA pass, no UE4SS Lua probe pass. Findings are dump-derived only;
unanswerable questions are flagged inline.

---

## TL;DR

1. **`AdaynightCycle_C` is the single weather scheduler / picker** for the
   entire weather subsystem. All probabilistic state transitions
   (`rainProbability`, `fogProbability`, `rainLightningChance`,
   `rainDeactivateChance`, `permanentRain`, `permanentFog`) live as fields
   on this one actor (`daynightCycle.hpp:84-87, 412, 422, 428`). It owns
   `timerRain()`, `timerLightning()`, `fogEvent()`, `superFogEvent()`,
   `spawnFog()`, `rainCleanup()`, `causeRain(isRaining)`,
   `setRainProperties(...)`, `setRainParameters()`, `setWindParameters()`,
   `permaRain_timer()`. **This is the host-authoritative scheduler we must
   suppress on the client** to make weather sync work — direct parallel to
   the `mushroomMaster` / `NpcSpawner` suppression pattern called out in
   `[[project-coop-aprop-lifecycle-RE-inc3-scope]]`.

2. **Lightning strikes are spawned actors, not effects on the rod.**
   `AlightningStrike_C : public AActor` (size 0x280) carries its own audio
   (3 components), particle, point light, sphere collider, and timeline
   (`lightningStrike.hpp:7-19`). The rod / analyzer pair are only the
   *aim-picker*: `AlightningHeightAnalyzer_C` carries the picked
   `lightningRodLocation @ 0x0288` and a back-ref `rod @ 0x0298` to a
   `AbaseLightningRod_C` (`lightningHeightAnalyzer.hpp:17-19`). The strike
   actor itself has **no Location/Target field — its strike point is its
   own ActorLocation** (i.e. it's spawned at the chosen world point and
   plays its effect there).

3. **Strike damage / interaction is interface-style broadcast**. Every
   damageable actor implements `void reachedByLightning(class
   AlightningStrike_C* lightning)` from the `Aactor_save_C` parent
   (`actor_save.hpp:114`) — 70+ subclasses in the dump declare this
   override. `AmainPlayer_C::reachedByLightning(...)` at
   `mainPlayer.hpp:692` confirms the player participates. So a strike
   only needs `(loc, t)` on the wire — the local BPs each peer is already
   running will fire `reachedByLightning` on whatever they overlap with.

4. **Two named "rain" entities and neither is the rain particle.**
   `Aevent_fleshRain_C` (`event_fleshRain.hpp`) is a save-loaded one-shot
   `Aactor_save_C` with just `int32 I @ 0x0250` and a `ReceiveBeginPlay`
   — a story event marker, NOT the falling-rain VFX. The actual rain
   particle is **`UParticleSystemComponent* eff_rain @ 0x0228` on
   `AdaynightCycle_C`** (`daynightCycle.hpp:7`). Rain is a single
   always-loaded component on the scheduler actor, not a spawn-per-event
   actor.

5. **`Anewsky_C` is the sky dome** (sun/moon/star material) but does NOT
   own time-of-day. Time-of-day lives on `AdaynightCycle_C`
   (`daynightCycle.hpp:21-27` — `Day`, `Phase`, `phase_sin`, `MaxTime`,
   `totalTime`, `TimeScale`, `timeZ @ 0x02D0`). `Anewsky_C` reads
   sky/sun color from `daylightCycleObject @ 0x02E0` (back-ref to the
   cycle) and exposes `upd(bool updateLights)`, `setSunIntensity()`,
   `skyVisibility(bool)`, `setMoonPhase()`, `setEye(bool)` to apply
   visual state (`newsky.hpp:40-47`). To sync the visible sky, we sync
   the source cycle, not the sky dome.

6. **Fog has two co-existing actors and one component**:
   - `AweatherFogController_C` (`weatherFogController.hpp`) — the
     "rolling weather fog" controller; `fogPhase @ 0x0244`, `Time @
     0x0238`, `Duration @ 0x0240`, `Alpha @ 0x023C`, `permanent @ 0x0248`,
     `permafog @ 0x0249`, `Strength @ 0x024C`. Held by
     `daynightCycle.fogEventObject @ 0x0338`.
   - `AsuperFog_C` (`superFog.hpp`) — the rarer dense fog event with
     `Duration @ 0x023C`, `Thickness @ 0x0240`, `Alpha @ 0x0238`, and a
     `UFO @ 0x0248` slot — spawns a `AHoelUfo_C` via `spawnUfo()`
     (`superFog.hpp:18`). Daynight calls `superFogEvent()` /
     `spawnFog()` (`daynightCycle.hpp:142, 145`).
   - `UExponentialHeightFogComponent* ExponentialHeightFog @ 0x0270` on
     `AdaynightCycle_C` (`daynightCycle.hpp:16`) — the engine-level
     height fog the controllers drive (via `SetFogDensity()` at
     `daynightCycle.hpp:106`, `finalFogDensity @ 0x0418`).
   - `AblackFog_C` is a **separate spookier post-process event**, not
     the weather fog (see §5).

7. **Wind has two pieces**: `AdirectionalWind_C` is the gameplay-side
   wind actor (audio, particle, leaf spawner, sphere trigger) with
   `setParameters(Intensity, Angle, Speed, Strength)` as the single
   external setter (`directionalWind.hpp:36`). The actual rendering-side
   wind driver is `AWindDirectionalSource* windSource @ 0x0288` (a
   stock UE class) which `directionalWind` keeps in sync via
   `updateDirWind()` and `setWindLoc()`. Daynight calls
   `setWindParameters()` (`daynightCycle.hpp:107`) to drive intensities.

8. **No standalone "rain particle actor" class.** Rain is one
   `UParticleSystemComponent` on `AdaynightCycle_C`. The two rain-related
   *actor* classes (`Atrigger_box_rain_C`, `Atool_rain_C`) are a
   per-area enable/disable trigger and a debug toolgun module — not the
   rain VFX itself. `Aevent_fleshRain_C` is a story event (see §3).

9. **All weather state lives in `AdaynightCycle_C`** — boolean state
   `isRaining @ 0x02E4`, `rain @ 0x02E0`, `rainStrength @ 0x0404`,
   `rainWindSpeed @ 0x041C`, `rainLightningChance @ 0x0408`,
   `rainDeactivateChance @ 0x040C`, `rainToggle @ 0x0420`,
   `permanentRain @ 0x042C`, `permanentFog @ 0x042D`, `enable_rain @
   0x044B`, `enable_fog @ 0x0449`, `enable_superfog @ 0x044A`,
   `isSnow @ 0x03B0`, `enableSunlight @ 0x03D8`, `enableMoonlight @
   0x0448`, `thickFog @ 0x0330`. **A single `WeatherState` packet
   capturing this struct is the wire footprint** — particles/audio re-derive
   on each peer locally because each peer runs the same BP code, gated on
   these booleans.

10. **What is NOT in the CXXHeaderDump (cannot answer from dumps alone)**:
    the exact `SpawnActor` call site for `AlightningStrike_C` (we know it
    happens — every actor has a `reachedByLightning` handler — but the
    spawner is in `daynightCycle::timerLightning` ubergraph BP, not
    visible as a UFunction signature in the dump). Same for who picks
    the rod when `AlightningHeightAnalyzer_C::lightningRod=true`. These
    are BP-ubergraph internals that need either UE4SS Lua introspection
    OR IDA disasm of the ubergraph trampolines. Flagged in §8 (Open Flags).

---

## Section 1 — `AlightningStrike_C` (the lightning strike actor)

### 1.1 Class layout — `lightningStrike.hpp:4-25`

```
class AlightningStrike_C : public AActor
{
    FPointerToUberGraphFrame UberGraphFrame;             // 0x0220 (0x8)
    class USphereComponent* lightningRadius;             // 0x0228 (0x8)
    class UAudioComponent* audio_background;             // 0x0230 (0x8)
    class UAudioComponent* audio_far;                    // 0x0238 (0x8)
    class UAudioComponent* audio_close;                  // 0x0240 (0x8)
    class UPointLightComponent* PointLight;              // 0x0248 (0x8)
    class UParticleSystemComponent* eff_lightning;       // 0x0250 (0x8)
    class USceneComponent* DefaultSceneRoot;             // 0x0258 (0x8)
    float lightTL_a_F221259D43A5A518CBF27F998E63014D;    // 0x0260 (0x4)
    TEnumAsByte<ETimelineDirection::Type> lightTL__Direction_F221...; // 0x0264 (0x1)
    class UTimelineComponent* lightTL;                   // 0x0268 (0x8)
    int32 debris;                                        // 0x0270 (0x4)
    FName explosionTag;                                  // 0x0274 (0x8)
    float Radius;                                        // 0x027C (0x4)

    void lightTL__FinishedFunc();
    void lightTL__UpdateFunc();
    void ReceiveBeginPlay();
    void ExecuteUbergraph_lightningStrike(int32 EntryPoint);
}; // Size: 0x280
```

### 1.2 Strike location

**There is no `StrikeLocation` / `Target` / `aimPoint` field.** The
strike's world position IS the actor's own `ActorLocation` (UE `RootComponent`
transform). The lifecycle is:
1. Some external code (the scheduler — `AdaynightCycle_C::timerLightning`,
   see §9) computes a world point.
2. Calls `UWorld::SpawnActor<AlightningStrike_C>(loc, rot, ...)`.
3. `ReceiveBeginPlay()` (`lightningStrike.hpp:23`) kicks off the timeline
   `lightTL` (`@ 0x0268`).
4. `lightTL__UpdateFunc` drives the point light intensity over time.
5. `lightTL__FinishedFunc` destroys the actor.

This is the same archetype as `AskyFallingEvent_C` (§7) and
`AskyUfo_C` — a one-shot spawned event actor with its own audio + light
+ particle + timeline.

### 1.3 Audio components — `lightningStrike.hpp:8-10`

Three `UAudioComponent` instances:
- `audio_background @ 0x0230` — the omnipresent rumble layer.
- `audio_far @ 0x0238` — distant thunder (delayed by distance).
- `audio_close @ 0x0240` — close hit (instant crack).

(The header gives only the field types, not the sound assets. The 3-way
split implies the strike actor handles its own distance/audio mixing
client-side; we don't need to stream audio params on the wire — just the
`(loc, t)` and each peer's local copy plays the same way.)

### 1.4 Light + particle

- `PointLight @ 0x0248` — the flash light source.
- `eff_lightning @ 0x0250` — the bolt particle system.
- Driven via `lightTL` timeline (`@ 0x0268`) on
  `lightTL__UpdateFunc` callbacks.

### 1.5 Strike-induced damage

- `USphereComponent* lightningRadius @ 0x0228` — overlap volume.
- `float Radius @ 0x027C` — the configured radius value.
- `int32 debris @ 0x0270` — debris-spawn count (BP-internal use).
- `FName explosionTag @ 0x0274` — tag for filtering targets.

**Damage dispatch is via the `reachedByLightning` interface callback,
NOT direct from this actor.** The strike actor's ubergraph (not in dump)
walks `lightningRadius` overlaps and calls
`OverlappedActor->reachedByLightning(this)` on each. Every actor in
`Aactor_save_C` (and all its 70+ subclasses) implements it:
- `mainPlayer.hpp:692` — `AmainPlayer_C::reachedByLightning(...)`
- `actor_save.hpp:114` — the base declaration (default no-op overridden
  per subclass)
- 70+ other classes, including: `ATV.hpp:461`, `cookier.hpp:109`,
  `cubemapRenderer.hpp:106`, `dirthole.hpp:111`, `droneConsole.hpp:127`,
  `drone.hpp:170`, `figura.hpp:118`, `fossilhound.hpp:125`,
  `grayboar.hpp:121`, `greenfire.hpp:97`, `prop.hpp` (visible in the
  files-with-matches list), and many `grime_*` decals.

(`eelecPole.hpp:53` exposes a `void lightningStrike()` UFunction —
**not** the strike actor; this is the pole's OWN method that probably
plays a one-off VFX when the pole is hit. Cross-reference unconfirmed
from dump alone.)

### 1.6 Lifetime / destruction path

- No `ReceiveDestroyed` override declared (`lightningStrike.hpp` only
  has `ReceiveBeginPlay` and the timeline callbacks).
- Destruction must be driven from `lightTL__FinishedFunc()` (the timeline's
  finished callback at `lightningStrike.hpp:21`) calling `K2_DestroyActor`
  in the ubergraph.

---

## Section 2 — Lightning trigger / aim system

### 2.1 `AlightningRod_C` (the visible rod static mesh) — `lightningRod.hpp:4-15`

```
class AlightningRod_C : public AActor
{
    class UStaticMeshComponent* StaticMesh3;   // 0x0220 (0x8)
    class UStaticMeshComponent* StaticMesh;    // 0x0228 (0x8)
    class UStaticMeshComponent* StaticMesh5;   // 0x0230 (0x8)
    class UStaticMeshComponent* StaticMesh4;   // 0x0238 (0x8)
    class UBillboardComponent* Billboard;      // 0x0240 (0x8)
    class UStaticMeshComponent* cube1;         // 0x0248 (0x8)
    class UStaticMeshComponent* cube;          // 0x0250 (0x8)
    class USceneComponent* DefaultSceneRoot;   // 0x0258 (0x8)
}; // Size: 0x260
```

**Pure visual mesh assembly.** No state fields, no UFunctions. The actual
lightning-attractor logic is on the base class:

### 2.2 `AbaseLightningRod_C` (the active rod) — `baseLightningRod.hpp:4-12`

```
class AbaseLightningRod_C : public AActor
{
    class UStaticMeshComponent* radiusVisual;  // 0x0220 (0x8)
    class UStaticMeshComponent* StaticMesh;    // 0x0228 (0x8)
    class USceneComponent* DefaultSceneRoot;   // 0x0230 (0x8)
    bool Active;                               // 0x0238 (0x1)
}; // Size: 0x239
```

Only one state field: `bool Active @ 0x0238`. (No UFunctions visible in
the dump.)

### 2.3 `AlightningHeightAnalyzer_C` — the picker — `lightningHeightAnalyzer.hpp:4-26`

```
class AlightningHeightAnalyzer_C : public AActor
{
    FPointerToUberGraphFrame UberGraphFrame;              // 0x0220 (0x8)
    class USceneComponent* DefaultSceneRoot;              // 0x0228 (0x8)
    int32 checkAmount;                                    // 0x0230 (0x4)
    float heightZ;                                        // 0x0234 (0x4)
    TArray<float> hZ;                                     // 0x0238 (0x10)
    TArray<FVector> hLoc;                                 // 0x0248 (0x10)
    TArray<class AActor*> ignores;                        // 0x0258 (0x10)
    class AInstancedFoliageActor* Foliage;                // 0x0268 (0x8)
    FVector Offset;                                       // 0x0270 (0xC)
    float Search;                                         // 0x027C (0x4)
    float chance;                                         // 0x0280 (0x4)
    bool lightningRod;                                    // 0x0284 (0x1)
    FVector lightningRodLocation;                         // 0x0288 (0xC)
    class AbaseLightningRod_C* rod;                       // 0x0298 (0x8)

    void multHeight(class UPhysicalMaterial* InputPin,
                    TSubclassOf<class AActor> Class,
                    float& mult);
    void ReceiveTick(float DeltaSeconds);
    void ReceiveBeginPlay();
    void ReceiveDestroyed();
    void ExecuteUbergraph_lightningHeightAnalyzer(int32 EntryPoint);
}; // Size: 0x2A0
```

### 2.4 How strike location is picked — evidence

The analyzer walks the world building parallel arrays of candidate hit
points:
- `TArray<float> hZ @ 0x0238` — candidate heights.
- `TArray<FVector> hLoc @ 0x0248` — candidate world locations.
- `int32 checkAmount @ 0x0230` — how many candidates to sample.
- `float Search @ 0x027C` — search radius.
- `float chance @ 0x0280` — picker probability weight.
- `FVector Offset @ 0x0270` — offset added to candidate points.
- `TArray<class AActor*> ignores @ 0x0258` — actors excluded from
  consideration.
- `AInstancedFoliageActor* Foliage @ 0x0268` — foliage source (so trees
  can be struck).
- `float heightZ @ 0x0234` — current chosen height.

The analyzer **prefers a lightning rod when one is in range**:
- `bool lightningRod @ 0x0284` — true when a rod is the chosen target.
- `FVector lightningRodLocation @ 0x0288` — the rod's chosen world point.
- `class AbaseLightningRod_C* rod @ 0x0298` — back-ref to the rod
  selected.

`void multHeight(UPhysicalMaterial* InputPin, TSubclassOf<AActor> Class,
float& mult)` is a per-material/per-class height multiplier — i.e. tall
metal objects are biased upward in the height scoring.

**Summary**: strike location is picked by **tallest-candidate scoring
with material/class weighting**, with a hard preference for a nearby
active `AbaseLightningRod_C` (`Active=true`). Pure random in-area
picking is the fallback when no rod qualifies.

### 2.5 Cross-reference: who actually spawns `AlightningStrike_C`?

**This is not visible in the dump.** Evidence:
- `AlightningHeightAnalyzer_C` has no `SpawnStrike` / `Trigger` UFunction
  declared. Its `ReceiveTick` ubergraph is in the BP-internal bytecode
  (not in the .hpp).
- `AdaynightCycle_C` HAS a `void timerLightning()` UFunction
  (`daynightCycle.hpp:147`) and a `void ligh()` UFunction
  (`daynightCycle.hpp:136`).
- `AdaynightCycle_C::lightning @ 0x0268` (`daynightCycle.hpp:15`) is a
  `USkyLightComponent*` — the global sky-light flash, NOT a class
  reference to `AlightningStrike_C`.
- No `AdaynightCycle_C` field is typed `AlightningStrike_C*` or
  `TSubclassOf<AlightningStrike_C>` in the dump.

**Best-evidence inference (NOT confirmed from dump)**: `timerLightning()`
fires on rain ticks, queries the heightAnalyzer for a candidate
location, `SpawnActor<AlightningStrike_C>` at that location, and
optionally pulses the global `lightning` sky-light component. **The rod
does NOT self-spawn the strike** — it is a passive attractor whose
location overrides the analyzer's tallest-point pick. **Confirming
this requires UE4SS Lua introspection of `timerLightning`'s ubergraph
or IDA disasm of the `ExecuteUbergraph_daynightCycle` trampoline.**
Flagged in §8.

### 2.6 `Aprop_lightningRod_C` — held lightning-rod prop — `prop_lightningRod.hpp:4-10`

```
class Aprop_lightningRod_C : public Aprop_C
{
    FPointerToUberGraphFrame UberGraphFrame;  // 0x0368 (0x8)
    void playerHandUse_LMB(class AmainPlayer_C* Player);
    void ExecuteUbergraph_prop_lightningRod(int32 EntryPoint);
}; // Size: 0x370
```

A grabbable hand-prop variant of the rod. Has no state field, just a
`playerHandUse_LMB` handler — i.e. clicking with it in hand does
something (BP-only). Not part of the active attractor system (that's
`AbaseLightningRod_C`).

### 2.7 Storm-related prop modules — `prop_physModule_lightning.hpp`, `prop_physModule_stormfilter.hpp`

```
class Aprop_physModule_lightning_C : public Aprop_physModule_C
{ }; // Size: 0x364   (no extra fields)

class Aprop_physModule_stormfilter_C : public Aprop_physModule_C
{ }; // Size: 0x364   (no extra fields)
```

These are empty stubs — all logic is inherited from `Aprop_physModule_C`
(not in scope). Likely sensor/decoration items related to the storm
event but no separate behavior visible here.

---

## Section 3 — Rain system

### 3.1 There is no standalone rain-actor class

Searching the entire dump for `rain` / `Rain` files yields:
- `event_fleshRain.hpp` — story event (§3.5).
- `trigger_box_rain.hpp` — area trigger that toggles rain ON enter.
- `tool_rain.hpp` — debug toolgun module to drive rain parameters.

**None of these is the rain VFX actor.** The rain itself is a
particle component on the scheduler:

### 3.2 The rain particle lives on `AdaynightCycle_C` — `daynightCycle.hpp:7-12`

```
class UParticleSystemComponent* eff_rain;   // 0x0228 (0x8)
class UArrowComponent* rain_tilt;           // 0x0230 (0x8)
class UArrowComponent* rain_root;           // 0x0238 (0x8)
class UDirectionalLightComponent* light_moon;   // 0x0240 (0x8)
class UDirectionalLightComponent* light_sun;    // 0x0248 (0x8)
...
class UAudioComponent* rainSnd;             // 0x0278 (0x8)
```

- `eff_rain @ 0x0228` — the rain particle system, **always-loaded** as a
  component of the persistent `AdaynightCycle_C` (one instance per world).
- `rain_tilt @ 0x0230` — arrow for the wind-induced tilt direction.
- `rain_root @ 0x0238` — arrow for the rain spawn root (follows player
  via tick — see `ReceiveTick`).
- `rainSnd @ 0x0278` — the rain ambient audio.
- `UParticleSystem* rainEffect @ 0x03A8` (`daynightCycle.hpp:67`) — the
  particle ASSET (template) used to (re)set the active particle.

### 3.3 Rain state variables — `daynightCycle.hpp:34-86, 404-432`

```
float rain;                          // 0x02E0 — current rain intensity (0..1)
bool isRaining;                      // 0x02E4 — top-level on/off
bool rainMuted;                      // 0x02E5
float rainSpeed;                     // 0x02F4 — fall speed
float rainStrength;                  // 0x0404 — visual strength multiplier
float rainLightningChance;           // 0x0408 — per-tick lightning prob
float rainDeactivateChance;          // 0x040C — per-tick rain-stop prob
float rainWindSpeed;                 // 0x041C — wind component during rain
bool rainToggle;                     // 0x0420 — debounce flag
float rainProbability;               // 0x0424 — base storm rolldown
bool permanentRain;                  // 0x042C — sticky-on
bool enable_rain;                    // 0x044B — feature flag
bool isSnow;                         // 0x03B0 — winter-event variant
```

### 3.4 Rain UFunctions — `daynightCycle.hpp:108-146`

- `void causeRain(bool isRaining)` (`:109`) — toggle entry point.
- `void setRainProperties(bool isRaining, float rainStrength,
  float rainLightningChance, float rainDeactivateChance,
  float rainWindSpeed)` (`:110`) — apply-all-at-once setter. **This is
  the canonical wire target**: one packet → one call to this UFunction
  on each receiver.
- `void setRainParameters()` (`:108`) — re-applies the field values to
  the particle system.
- `void setRainParticles()` (`:112`) — re-spawns/swaps `eff_rain`
  template.
- `void rainCleanup()` (`:143`) / `void rainClean()` (`:144`) — cleanup
  paths.
- `void timerRain()` (`:146`) — host-side scheduler tick (the picker).
- `void permaRain_timer()` (`:154`) — drives `permanentRain` state.
- `void rainS()` (`:137`) — short rain pulse (BP-internal).
- `void timerLightning()` (`:147`) — companion lightning scheduler.

### 3.5 `Aevent_fleshRain_C` — story event, NOT VFX — `event_fleshRain.hpp:4-11`

```
class Aevent_fleshRain_C : public Aactor_save_C
{
    FPointerToUberGraphFrame UberGraphFrame;   // 0x0248 (0x8)
    int32 I;                                   // 0x0250 (0x4)
    void ReceiveBeginPlay();
    void ExecuteUbergraph_event_fleshRain(int32 EntryPoint);
}; // Size: 0x254
```

Only field is `int32 I @ 0x0250` (likely an event index). Parent is
`Aactor_save_C` so it's a save-persistent marker actor — fits the
shipped Phase 5S0 snapshot model (already covered as part of the
generic actor sync; not a weather-specific concern).

### 3.6 `Atrigger_box_rain_C` — per-area rain override — `trigger_box_rain.hpp:4-13`

```
class Atrigger_box_rain_C : public Atrigger_box_C
{
    FPointerToUberGraphFrame UberGraphFrame;   // 0x02C8 (0x8)
    bool rain;                                 // 0x02D0 (0x1)
    void getTriggerData(Fstruct_triggerSave& Data);
    void loadTriggerData(Fstruct_triggerSave Data, bool& return);
    void BndEvt__Box_K2Node_ComponentBoundEvent_0_ComponentBeginOverlapSignature__DelegateSignature(...);
    void ExecuteUbergraph_trigger_box_rain(int32 EntryPoint);
}; // Size: 0x2D1
```

A trigger box (placed in level) that on player overlap toggles the
local rain state (via the `BndEvt__Box_..._ComponentBeginOverlapSignature`
delegate). Has `getTriggerData`/`loadTriggerData` — save-persistent box
state. Per-area, not global. (May complicate coop: if host enters and
client doesn't, rain only changes for host. Best handled by host-only
trigger evaluation + global `causeRain` broadcast — same suppression
pattern as the mushroom spawners.)

### 3.7 `Atool_rain_C` — toolgun debug module — `tool_rain.hpp:4-20`

```
class Atool_rain_C : public AtoolObject_C
{
    FPointerToUberGraphFrame UberGraphFrame;   // 0x0530
    bool Active;                               // 0x0538
    float wind_speed;                          // 0x053C
    float wind_strength;                       // 0x0540
    float lightningChance;                     // 0x0544
    float rainStopChance;                      // 0x0548
    bool instant;                              // 0x054C
    bool updateParameters_0;                   // 0x054D
    void setParameters(Fstruct_toolParameters Parameters, bool& return);
    void getParameters(Fstruct_toolParameters& Parameters);
    void assignParameters(Fstruct_toolParameters Parameters, bool& return);
    void Init(class Aprop_toolgun_C* toolgun);
    void ExecuteUbergraph_tool_rain(int32 EntryPoint);
}; // Size: 0x54E
```

A toolgun attachment for tweaking rain parameters in-dev. Not part of
the runtime weather machinery (developer cheat tool).

### 3.8 Spawn / despawn pattern (confirmed from dump)

**Always-loaded**: `eff_rain` is a permanent component of the persistent
`AdaynightCycle_C` actor. **NOT spawned per-event** — only its activity
(`Active`/`bAutoActivate`) is toggled.

This means: on the wire, we never spawn/despawn the rain effect. The
sync target is just the boolean+float state on `AdaynightCycle_C`, applied
via `causeRain` and `setRainProperties` UFunctions on the receiver. The
local BP code on each peer renders the particles from that state.

---

## Section 4 — `Anewsky_C` (the sky dome)

### 4.1 Class layout — `newsky.hpp:4-49`

```
class Anewsky_C : public AActor
{
    FPointerToUberGraphFrame UberGraphFrame;             // 0x0220 (0x8)
    class UDirectionalLightComponent* light_blackhole;   // 0x0228 (0x8)
    class UStaticMeshComponent* m1;                      // 0x0230 (0x8)
    class UStaticMeshComponent* m3;                      // 0x0238 (0x8)
    class UStaticMeshComponent* m2;                      // 0x0240 (0x8)
    class UBillboardComponent* Billboard;                // 0x0248 (0x8)
    class UStaticMeshComponent* sky;                     // 0x0250 (0x8)
    class UArrowComponent* starRot;                      // 0x0258 (0x8)
    class UArrowComponent* dir;                          // 0x0260 (0x8)
    class USceneComponent* DefaultSceneRoot;             // 0x0268 (0x8)
    float Alpha;                                         // 0x0270 (0x4)
    class UMaterialInstanceDynamic* dynmat;              // 0x0278 (0x8)
    class UCurveLinearColor* sky_bottom;                 // 0x0280 (0x8)
    class UCurveLinearColor* sky_top;                    // 0x0288 (0x8)
    class UCurveLinearColor* sky_clouds;                 // 0x0290 (0x8)
    class UCurveLinearColor* sun_color;                  // 0x0298 (0x8)
    bool sunHidden;                                      // 0x02A0 (0x1)
    float sunIntensityMult;                              // 0x02A4 (0x4)
    FLinearColor sunColor;                               // 0x02A8 (0x10)
    float moonIntensityMult;                             // 0x02B8 (0x4)
    float moonPhase_mirror;                              // 0x02BC (0x4)
    float moonPhase_normal;                              // 0x02C0 (0x4)
    bool Eye;                                            // 0x02C4 (0x1)
    class AmainGamemode_C* GameMode;                     // 0x02C8 (0x8)
    class UMaterialInstanceDynamic* dynmat_bhole;        // 0x02D0 (0x8)
    class UMaterialInstanceDynamic* dynmat_sun;          // 0x02D8 (0x8)
    class AdaynightCycle_C* daylightCycleObject;         // 0x02E0 (0x8)
    bool applyMoonPhase;                                 // 0x02E8 (0x1)
    float DeltaSeconds;                                  // 0x02EC (0x4)
    bool skyHidden;                                      // 0x02F0 (0x1)

    void setHiddenSuns();
    void setEye(bool Eye);
    void setSunIntensity();
    void skyVisibility(bool bNewHidden);
    void Init();
    void upd(bool updateLights);
    void UserConstructionScript();
    void ReceiveTick(float DeltaSeconds);
    void ReceiveBeginPlay();
    void tp();
    void setMoonPhase();
    void ExecuteUbergraph_newsky(int32 EntryPoint);
}; // Size: 0x2F1
```

### 4.2 Time-of-day fields

**There ARE NO time-of-day fields on `Anewsky_C`.** Time lives entirely
on `AdaynightCycle_C`. `Anewsky_C` reads from `daylightCycleObject @
0x02E0` (a back-ref to the cycle, set in `Init()` or
`UserConstructionScript`) to compute its visual orientation.

The Day/Time-of-day fields are on **`AdaynightCycle_C`** at
`daynightCycle.hpp:21-32`:

```
float Day;             // 0x0298 — current day-of-year / continuous day counter
float Phase;           // 0x029C — phase within day [0..1]
float phase_sin;       // 0x02A0
float phase_nsin;      // 0x02A4
float phase_normsin;   // 0x02A8
float MaxTime;         // 0x02AC — length of one day (seconds)
float totalTime;       // 0x02B0 — accumulated runtime
float TimeScale;       // 0x02B4 — speed multiplier
class Anewsky_C* skysphere;  // 0x02B8 — forward ref to the sky dome
FVector sunAxis;       // 0x02C0
bool IsActive;         // 0x02CC
FIntVector timeZ;      // 0x02D0 — packed Hour/Minute/Day discrete time
```

`AdaynightCycle_C::timeZ @ 0x02D0` (`FIntVector` = 3 × int32, 12 bytes)
holds discrete hour/minute/day, used by `newMinute`, `newHour`, `newDay`
multicast delegates (`daynightCycle.hpp:46, 59, 61`).

### 4.3 Atmospheric tint / cloud cover

`Anewsky_C` exposes the sky-color curves but evaluates them per-tick
based on the cycle's phase:

- `UCurveLinearColor* sky_bottom @ 0x0280` — horizon color curve.
- `UCurveLinearColor* sky_top @ 0x0288` — zenith color curve.
- `UCurveLinearColor* sky_clouds @ 0x0290` — cloud tint curve.
- `UCurveLinearColor* sun_color @ 0x0298` — sun-disc color curve.
- `UMaterialInstanceDynamic* dynmat @ 0x0278` — the dynamic material on
  the `sky` static mesh (`@ 0x0250`).
- `UMaterialInstanceDynamic* dynmat_sun @ 0x02D8` — for the sun.
- `UMaterialInstanceDynamic* dynmat_bhole @ 0x02D0` — for the blackhole
  visual.

There is **no `cloudCover` scalar field** in the dump. Cloud cover is
implied by the cycle's separate `cloudsTimer @ 0x0410` and
`cloudsTimerAccumulate @ 0x0414` (`daynightCycle.hpp:79-80`), driven
into the sky material via the curves above.

### 4.4 Methods to apply state — `newsky.hpp:37-47`

- `void Init()` — bind to `daylightCycleObject`, fetch curves, set up
  dynmats.
- `void upd(bool updateLights)` — main per-tick refresh; recomputes
  sun/moon transforms and material params from the cycle phase.
- `void setSunIntensity()` — refresh the directional light intensity
  from `sunIntensityMult @ 0x02A4`.
- `void setMoonPhase()` — re-evaluate `moonPhase_normal @ 0x02C0` and
  `moonPhase_mirror @ 0x02BC`.
- `void skyVisibility(bool bNewHidden)` — toggle the whole sky mesh.
- `void setHiddenSuns()` — toggle sun visibility based on `sunHidden @
  0x02A0`.
- `void setEye(bool Eye)` — story-event eye-in-sky toggle (`Eye @
  0x02C4`).
- `void tp()` — teleport / re-anchor the sky (BP-internal label).
- `void ReceiveTick(float DeltaSeconds)` — per-frame driver.

**To sync the sky for coop**: do NOT sync `Anewsky_C` fields. Sync the
cycle's `Day`, `totalTime`, `TimeScale`, and discrete `timeZ`. The sky
dome regenerates locally on each peer from those.

### 4.5 Sleeplessness / season fields on the cycle — `daynightCycle.hpp:54-89`

```
float settingMultiplayer;           // 0x0350
int32 sleeplessDays;                // 0x0354
bool eyeSpawned;                    // 0x0358
TArray<int32> Struct Task New Sig Required;  // 0x03E0
float sleepingTimeDilation;         // 0x0400
float seasonalExponent;             // 0x0430
bool isHalloween;                   // 0x0434
float timeRotationDelay;            // 0x0438
FTimerHandle sunRotation_timerHandle;  // 0x0440
bool skipDaySet;                    // 0x044C
```

These are all part of the time/season system. Note especially `float
settingMultiplayer @ 0x0350` — the field name suggests VOTV's BP already
*reserves a hook for multiplayer behaviour*. **This is unconfirmed**;
the field's actual usage cannot be determined from the dump alone.
(Worth checking once weather sync work begins — see §8.)

### 4.6 The companion `AambientLightCurve_C` — `ambientLightCurve.hpp:4-49`

Bound from the cycle at `daynightCycle.hpp:63` (`AambientLightCurve_C*
lightCurve @ 0x0390`). The cycle calls `pluginLightCurve` /
`plugoutLightCurve` (`daynightCycle.hpp:113, 115`) to add/remove these
curves dynamically (e.g. entering a building). Each `AambientLightCurve_C`
has a USphereComponent `s_radius` + a USplineComponent `Spline` defining
where it applies. **Out of scope for weather sync** (these are area
overrides, not weather state — but worth noting they affect ambient
lighting alongside weather).

---

## Section 5 — `AblackFog_C` (the "black fog" event)

### 5.1 Class layout — `blackFog.hpp:4-23`

```
class AblackFog_C : public AActor
{
    FPointerToUberGraphFrame UberGraphFrame;   // 0x0220 (0x8)
    class UPostProcessComponent* PostProcess;  // 0x0228 (0x8)
    class USceneComponent* DefaultSceneRoot;   // 0x0230 (0x8)
    float A;                                   // 0x0238 (0x4)
    class UMaterialInstanceDynamic* dynmat;    // 0x0240 (0x8)
    float spd;                                 // 0x0248 (0x4)
    bool M;                                    // 0x024C (0x1)
    TArray<class Aeyer_C*> eyes;               // 0x0250 (0x10)
    class AmainGamemode_C* GameMode;           // 0x0260 (0x8)

    void set();
    void ReceiveBeginPlay();
    void ReceiveDestroyed();
    void ReceiveTick(float DeltaSeconds);
    void spawnGhost();
    void ExecuteUbergraph_blackFog(int32 EntryPoint);
}; // Size: 0x268
```

### 5.2 Role & evidence

This is **not the weather fog** — it's a spooky-event actor that:
- Owns a `UPostProcessComponent* PostProcess @ 0x0228` to apply a
  fullscreen post-process material (the "blackness" effect).
- Drives `float A @ 0x0238` (alpha/intensity) and `float spd @ 0x0248`
  (transition speed).
- Owns a `TArray<class Aeyer_C*> eyes @ 0x0250` — eye-ghost actors
  spawned during the event (`spawnGhost()` UFunction at `:20`).
- Has a back-ref `class AmainGamemode_C* GameMode @ 0x0260`.

### 5.3 Spawn lifecycle

- `mainGamemode.hpp:149` declares `class AblackFog_C* blackFog @ 0x0880`
  — singleton slot on the gamemode.
- `mainGamemode.hpp:429` declares `void spawnBlackFog()` UFunction —
  this is the gamemode-side spawner.
- `ReceiveBeginPlay` (`blackFog.hpp:17`) initializes; `ReceiveDestroyed`
  (`:18`) cleans up (when fog event ends).

So **`AmainGamemode_C::spawnBlackFog()` is the host-side trigger**. Wire
target: a discrete one-shot event packet to spawn (or destroy) the fog
on the receiver via the same UFunction.

### 5.4 Method `void set()` (`:16`)

No parameter list in the dump's UFunction declaration —
unanswerable what it sets. Likely a refresh / re-apply of `A` and `spd`
to the post-process material (`dynmat`). Confirmation requires UE4SS
Lua or IDA pass.

---

## Section 5b — `AsuperFog_C` (the heavy weather fog) — `superFog.hpp:4-22`

```
class AsuperFog_C : public AActor
{
    FPointerToUberGraphFrame UberGraphFrame;   // 0x0220 (0x8)
    class USceneComponent* DefaultSceneRoot;   // 0x0228 (0x8)
    class AdaynightCycle_C* cyc;               // 0x0230 (0x8)
    float Alpha;                               // 0x0238 (0x4)
    float Duration;                            // 0x023C (0x4)
    float Thickness;                           // 0x0240 (0x4)
    class AHoelUfo_C* UFO;                     // 0x0248 (0x8)

    void Point(const FVector& OutputPin);
    void ReceiveTick(float DeltaSeconds);
    void ReceiveBeginPlay();
    void ReceiveDestroyed();
    void spawnUfo();
    void ExecuteUbergraph_superFog(int32 EntryPoint);
}; // Size: 0x250
```

- `cyc @ 0x0230` — back-ref to the cycle.
- `Alpha @ 0x0238`, `Duration @ 0x023C`, `Thickness @ 0x0240` —
  visible-state fields. **No `bool Active`** in this class — the actor's
  existence IS the active state; `ReceiveDestroyed` ends it.
- `UFO @ 0x0248` — `AHoelUfo_C` spawned alongside (a fog-related UFO
  encounter).
- Triggered by `daynightCycle.hpp:142` `void superFogEvent()` UFunction
  and `daynightCycle.hpp:145` `void spawnFog()`.

---

## Section 5c — `AweatherFogController_C` (the rolling fog timer) — `weatherFogController.hpp:4-21`

```
class AweatherFogController_C : public AActor
{
    FPointerToUberGraphFrame UberGraphFrame;   // 0x0220 (0x8)
    class USceneComponent* DefaultSceneRoot;   // 0x0228 (0x8)
    class AdaynightCycle_C* cyc;               // 0x0230 (0x8)
    float Time;                                // 0x0238 (0x4)
    float Alpha;                               // 0x023C (0x4)
    float Duration;                            // 0x0240 (0x4)
    float fogPhase;                            // 0x0244 (0x4)
    bool permanent;                            // 0x0248 (0x1)
    bool permafog;                             // 0x0249 (0x1)
    float Strength;                            // 0x024C (0x4)

    void ReceiveTick(float DeltaSeconds);
    void ReceiveBeginPlay();
    void ExecuteUbergraph_weatherFogController(int32 EntryPoint);
}; // Size: 0x250
```

- Held by `daynightCycle.fogEventObject @ 0x0338`
  (`daynightCycle.hpp:49`).
- Fields are wire-targets for fog sync: `Time`, `Alpha`, `Duration`,
  `fogPhase`, `Strength`, `permanent`, `permafog`.
- Driven by `daynightCycle.hpp:138` `void fogEvent()` UFunction.

### 5d — Fog summary

VOTV has **three fog systems coexisting**:
1. **Base height fog** = `UExponentialHeightFogComponent` on the cycle
   (`daynightCycle.hpp:16`), driven by `SetFogDensity()`
   (`daynightCycle.hpp:106`) using `finalFogDensity @ 0x0418`,
   `thickFog @ 0x0330`, `enable_fog @ 0x0449`.
2. **Rolling weather fog** = `AweatherFogController_C` event actor.
3. **Heavy fog event** = `AsuperFog_C` (one-shot, spawns UFO).

The black fog (`AblackFog_C`) is a separate post-process event, not part
of the regular weather rotation (story event).

---

## Section 6 — `AdirectionalWind_C` (the wind actor)

### 6.1 Class layout — `directionalWind.hpp:4-50`

```
class AdirectionalWind_C : public AActor
{
    FPointerToUberGraphFrame UberGraphFrame;     // 0x0220 (0x8)
    class UArrowComponent* Strength;             // 0x0228 (0x8)
    class UBillboardComponent* windOrigin;       // 0x0230 (0x8)
    class UBillboardComponent* windTarget;       // 0x0238 (0x8)
    class UBillboardComponent* windOffset;       // 0x0240 (0x8)
    class USceneComponent* windPivots;           // 0x0248 (0x8)
    class UAudioComponent* Audio;                // 0x0250 (0x8)
    class UParticleSystemComponent* eff_wind;    // 0x0258 (0x8)
    class USphereComponent* Sphere;              // 0x0260 (0x8)
    class UArrowComponent* Direction;            // 0x0268 (0x8)
    class UBillboardComponent* Billboard;        // 0x0270 (0x8)
    class UChildActorComponent* windActor;       // 0x0278 (0x8)
    class USceneComponent* DefaultSceneRoot;     // 0x0280 (0x8)
    class AWindDirectionalSource* windSource;    // 0x0288 (0x8)
    TArray<class AActor*> objs;                  // 0x0290 (0x10)
    bool tickable;                               // 0x02A0 (0x1)
    TArray<FVector> winds;                       // 0x02A8 (0x10)
    FVectorSpringState spring;                   // 0x02B8 (0x18)
    float Intensity;                             // 0x02D0 (0x4)
    FVector windLocation;                        // 0x02D4 (0xC)
    float windAdd;                               // 0x02E0 (0x4)
    float windSpeed_rain;                        // 0x02E4 (0x4)
    float windStrength_rain;                     // 0x02E8 (0x4)
    float windSpeed_background;                  // 0x02EC (0x4)
    float windStrength_background;               // 0x02F0 (0x4)
    float windSpeed_total;                       // 0x02F4 (0x4)
    float windStrength_total;                    // 0x02F8 (0x4)
    float windTexStr;                            // 0x02FC (0x4)
    class AmainGamemode_C* GameMode;             // 0x0300 (0x8)

    void setParameters(float Intensity, float Angle, float Speed, float Strength);
    void updState();
    void updateVars();
    void doWind();
    void BndEvt__directionalWind_Sphere_K2Node_ComponentBoundEvent_0_ComponentBeginOverlapSignature__DelegateSignature(...);
    void BndEvt__directionalWind_Sphere_K2Node_ComponentBoundEvent_1_ComponentEndOverlapSignature__DelegateSignature(...);
    void Dest(class AActor* DestroyedActor);
    void changeWindOrigin();
    void setWindLoc();
    void timer_spawnLeaves();
    void ReceiveTick(float DeltaSeconds);
    void updateDirWind();
    void ReceiveBeginPlay();
    void ExecuteUbergraph_directionalWind(int32 EntryPoint);
}; // Size: 0x308
```

### 6.2 The canonical wind state

Two pairs of speed+strength scalars, plus a totalled pair:
- Rain-mode wind: `windSpeed_rain @ 0x02E4`, `windStrength_rain @
  0x02E8`.
- Background wind: `windSpeed_background @ 0x02EC`,
  `windStrength_background @ 0x02F0`.
- Total (sum of above): `windSpeed_total @ 0x02F4`, `windStrength_total
  @ 0x02F8`.
- Auxiliary: `Intensity @ 0x02D0`, `windAdd @ 0x02E0`, `windTexStr @
  0x02FC` (texture-flow strength used by foliage materials),
  `windLocation @ 0x02D4` (current origin in world space).

### 6.3 Methods to apply state

- `void setParameters(float Intensity, float Angle, float Speed,
  float Strength)` (`:36`) — **the single-call external setter**. This
  is the wire target for "host applies wind state on each peer".
- `void updState()` (`:37`), `void updateVars()` (`:38`) — internal
  refreshers.
- `void doWind()` (`:39`) — apply the wind to nearby physics props
  (the `objs @ 0x0290` array tracks them; populated/drained by the
  sphere overlap delegates at `:40-41`).
- `void changeWindOrigin()` (`:43`), `void setWindLoc()` (`:44`),
  `void updateDirWind()` (`:47`) — origin/direction updaters.
- `void timer_spawnLeaves()` (`:45`) — spawn drifting leaf VFX.

### 6.4 Driver from the cycle

`AdaynightCycle_C::setWindParameters()` (`daynightCycle.hpp:107`) is the
caller that feeds `directionalWind`. `AmainGamemode_C::directionalWind @
0x0F70` (`mainGamemode.hpp:290`) holds the singleton actor instance.
`AmainGamemode_C::windParticles_check()` (`mainGamemode.hpp:525`) checks
whether the wind particle/audio should be active.

### 6.5 Engine-side wind

`AWindDirectionalSource* windSource @ 0x0288` — this is a stock UE
class that drives shader/material wind sampling globally. The
`AdirectionalWind_C` BP keeps this in sync via `updateDirWind()`.
Foliage and cloth respond to `AWindDirectionalSource` automatically.

### 6.6 `Atool_wind_C` — toolgun debug — `tool_wind.hpp:4-17`

```
class Atool_wind_C : public AtoolObject_C
{
    float wind_intensity;   // 0x0538
    float wind_angle;       // 0x053C
    float wind_speed;       // 0x0540
    float wind_strength;    // 0x0544
    void setParameters(Fstruct_toolParameters Parameters, bool& return);
    void getParameters(Fstruct_toolParameters& Parameters);
    void assignParameters(Fstruct_toolParameters Parameters, bool& return);
    void Init(class Aprop_toolgun_C* toolgun);
};
```

Dev tool — same shape as `tool_rain`. The 4 floats are exactly the args
to `AdirectionalWind_C::setParameters` confirming the canonical wind
state signature.

---

## Section 7 — `AskyFallingEvent_C` and related triggers

### 7.1 `AskyFallingEvent_C` — `skyFallingEvent.hpp:4-19`

```
class AskyFallingEvent_C : public AActor
{
    FPointerToUberGraphFrame UberGraphFrame;             // 0x0220 (0x8)
    class Ucomp_radarPoint_C* comp_radarPoint;           // 0x0228 (0x8)
    class UBillboardComponent* Billboard;                // 0x0230 (0x8)
    class UArrowComponent* Arrow1;                       // 0x0238 (0x8)
    class UAudioComponent* Audio;                        // 0x0240 (0x8)
    class UPointLightComponent* PointLight;              // 0x0248 (0x8)
    class UParticleSystemComponent* eff_glow_tardis;     // 0x0250 (0x8)
    class UArrowComponent* Arrow;                        // 0x0258 (0x8)
    class USceneComponent* DefaultSceneRoot;             // 0x0260 (0x8)

    void ReceiveTick(float DeltaSeconds);
    void ReceiveBeginPlay();
    void ExecuteUbergraph_skyFallingEvent(int32 EntryPoint);
}; // Size: 0x268
```

This is the **alien-ship-falling-from-sky event** (matches
`comp_radarPoint @ 0x0228` — appears on the radar — and the
`eff_glow_tardis @ 0x0250` particle name). The `Audio @ 0x0240` is the
descending whine.

**No external state fields — it's a self-driving timeline actor.** All
behavior in `ReceiveTick` + ubergraph. State (position, age) is just the
actor transform + tick time.

### 7.2 Trigger — `AtriggerFallingSky_C` — `triggerFallingSky.hpp:4-15`

```
class AtriggerFallingSky_C : public AActor
{
    FPointerToUberGraphFrame UberGraphFrame;   // 0x0220 (0x8)
    class USphereComponent* Sphere;            // 0x0228 (0x8)
    class USceneComponent* DefaultSceneRoot;   // 0x0230 (0x8)
    class AmainGamemode_C* GameMode;           // 0x0238 (0x8)

    void BndEvt__triggerFallingSky_Sphere_K2Node_ComponentBoundEvent_0_ComponentBeginOverlapSignature__DelegateSignature(...);
    void ReceiveBeginPlay();
    void ExecuteUbergraph_triggerFallingSky(int32 EntryPoint);
}; // Size: 0x240
```

A sphere-overlap trigger that fires when the player enters. The overlap
delegate triggers spawning of an `AskyFallingEvent_C` actor (BP-internal
detail; confirms the existing finding in
[[project-coop-re-findings-2026-05-24-pm]]).

This is the **`AskyFallingEvent`** that the previous events RE doc
already identified. **Out of weather sync** strictly speaking (it's a
story event), but candidate for the same `EntityEventPacket` mechanism
as lightning strikes.

---

## Section 8 — Schedulers / random pickers (search results)

### 8.1 Search performed

Globbed `'d:/.../CXXHeaderDump/' | grep -iE 'schedul|random|rnd'`.
**Zero hits.** No class named `*Schedule*`, `*Scheduler*`, `*Rnd*`, or
`*Random*` exists in the dump.

### 8.2 The actual picker is `AdaynightCycle_C`

All weather-rolling logic is BP-internal in `AdaynightCycle_C`'s
ubergraph. The relevant UFunctions visible in the dump:

```
void timerRain();                  // daynightCycle.hpp:146 — rain RNG tick
void timerLightning();             // daynightCycle.hpp:147 — lightning RNG tick
void fogEvent();                   // daynightCycle.hpp:138 — fog spawn check
void superFogEvent();              // daynightCycle.hpp:142 — heavy fog check
void spawnFog();                   // daynightCycle.hpp:145 — fog spawner
void rainCleanup();                // daynightCycle.hpp:143
void rainClean();                  // daynightCycle.hpp:144
void permaRain_timer();            // daynightCycle.hpp:154 — sticky-rain
void rainS();                      // daynightCycle.hpp:137 — short rain pulse
void ligh();                       // daynightCycle.hpp:136 — sky-light flash
void setRainParticles();           // daynightCycle.hpp:112
void setRainParameters();          // daynightCycle.hpp:108
void setWindParameters();          // daynightCycle.hpp:107
void SetFogDensity();              // daynightCycle.hpp:106
void causeRain(bool isRaining);    // daynightCycle.hpp:109
void setRainProperties(bool isRaining, float rainStrength,
                       float rainLightningChance,
                       float rainDeactivateChance,
                       float rainWindSpeed); // daynightCycle.hpp:110
```

And the probability fields it rolls against:
- `rainProbability @ 0x0424`
- `fogProbability @ 0x0428`
- `rainLightningChance @ 0x0408`
- `rainDeactivateChance @ 0x040C`

### 8.3 Suppression target for coop (the rule for clients)

To make coop weather host-authoritative, the client must suppress:
- `timerRain()` execution.
- `timerLightning()` execution.
- `fogEvent()` execution.
- `superFogEvent()` / `spawnFog()` execution.
- `permaRain_timer()` execution.
- `rainCleanup()` / `rainClean()` (host-driven cleanups apply via wire
  instead).

And **allow** to run on the client:
- The actual particle/audio renderers (`setRainParameters`,
  `setRainParticles`, `setWindParameters`, `SetFogDensity`).
- The state-applier UFunctions (`causeRain`, `setRainProperties`,
  `AdirectionalWind_C::setParameters`) — receiving these from the wire
  drives the local visuals.

This is the same suppress-host-scheduler/keep-renderer split used in
[[project-coop-aprop-lifecycle-RE-inc3-scope]] (mushroomMaster) and
[[project-coop-re-findings-2026-05-24-pm]] (NPC spawners).

---

## Section 9 — Lifetime / who-owns-what

### 9.1 Always-resident actors

- `AdaynightCycle_C` — one per world. Owns rain particle, rain audio,
  height fog, sky-light, sun-light, moon-light. Lifetime = world.
- `Anewsky_C` — one per world (the sky dome). Lifetime = world.
- `AdirectionalWind_C` — one per world. Lifetime = world.

### 9.2 Lazy-spawned event actors

- `AlightningStrike_C` — spawned on roll, destroyed via timeline finish.
- `AblackFog_C` — spawned by `AmainGamemode_C::spawnBlackFog()`,
  destroyed when event ends. **Singleton slot** in
  `AmainGamemode_C::blackFog @ 0x0880` (`mainGamemode.hpp:149`).
- `AsuperFog_C` — spawned by `AdaynightCycle_C::spawnFog()` /
  `superFogEvent()`. Holds `AHoelUfo_C* UFO @ 0x0248`.
- `AweatherFogController_C` — held as singleton at
  `daynightCycle.fogEventObject @ 0x0338`. Spawned for each fog event.
- `AskyFallingEvent_C` — spawned by overlap of
  `AtriggerFallingSky_C` (story event).
- `AredSkyEvent_C` — held at `AmainGamemode_C::redSky @ 0x0888`
  (`mainGamemode.hpp:150`); `AmainGamemode_C::spawnRedSky()`
  (`mainGamemode.hpp:428`). Toggleable via `void set(bool isred)`
  (`redSkyEvent.hpp:10`).

### 9.3 `AredSkyEvent_C` — `redSkyEvent.hpp:4-14`

```
class AredSkyEvent_C : public AActor
{
    FPointerToUberGraphFrame UberGraphFrame;   // 0x0220 (0x8)
    class USceneComponent* DefaultSceneRoot;   // 0x0228 (0x8)
    bool isred;                                // 0x0230 (0x1)

    void set(bool isred);
    void ReceiveBeginPlay();
    void ReceiveDestroyed();
    void ExecuteUbergraph_redSkyEvent(int32 EntryPoint);
}; // Size: 0x231
```

A single bool — `isred @ 0x0230`. Trivial to sync (one bool +
spawn/destroy event). Likely tints the sky red during the event.

---

## Section 10 — Open flags (unanswerable from dump alone)

These require either UE4SS Lua probes or IDA disasm of the BP
ubergraphs:

1. **Exact spawn site for `AlightningStrike_C`.** Inferred to be
   `AdaynightCycle_C::timerLightning()` ubergraph, but the
   `SpawnActor` call is BP-internal and not visible in the .hpp
   UFunction declarations. **Recommended probe**: UE4SS Lua
   `RegisterHook("/Game/.../daynightCycle_C:timerLightning", ...)` +
   walk the call stack on fire.

2. **How `AlightningHeightAnalyzer_C` is invoked.** The analyzer has a
   `ReceiveTick` (`:23`) but no UFunction taking a strike-spawn request.
   The connection between `lightningRodLocation @ 0x0288` and the strike
   spawn is BP-internal.

3. **`AdaynightCycle_C::settingMultiplayer @ 0x0350`** — what does this
   field DO in VOTV's BP code? Field name implies multiplayer awareness
   but no UFunction signature reveals it.

4. **`AdaynightCycle_C::Realtime @ 0x0340`** — is this real-time clock
   tracking, or just a debug toggle? Could matter for sync (host's
   real-time vs client's).

5. **`AdaynightCycle_C::loan @ 0x0318`** — unknown semantics. Possibly
   a story flag.

6. **The `intComs_*` multicast interface members** (e.g.
   `intComs_triggerSnow(bool isSnow)` at `daynightCycle.hpp:151`).
   These are presumably broadcast hooks that propagate state to all
   listening actors. Useful for "when snow toggles, everyone reacts" —
   may overlap with the wire layer we'd build.

7. **`AblackFog_C::set()`** signature/body — no params declared. What
   does it set?

8. **Whether `AskyFallingEvent_C` and `AredSkyEvent_C` qualify as
   "weather"** in the user's intent. Both are story events
   (alien-ship, red-sky-anomaly) — not strictly atmospheric rotation.
   The user's "all weathers" directive is ambiguous on these. The
   weather sync packet design should probably handle weather state
   (rain/fog/wind/sky) separately from story events
   (skyFallingEvent / blackFog / redSky / superFog-UFO) — story
   events fit the `EntityEventPacket` from
   [[project-coop-re-findings-2026-05-24-pm]] better than the
   `WeatherState` packet.

9. **Snow.** `bool isSnow @ 0x03B0` exists on the cycle but no
   dedicated `Asnow*` actor exists in the dump (only `snowman.hpp`,
   `prop_snowball*.hpp` — game props, not VFX). Snow VFX is likely
   driven by season-aware material swaps + a swapped `rainEffect`
   particle template. Confirmed by `intComs_triggerSnow` and
   `updSeason(enum_seasons::Type newSeason)` (`daynightCycle.hpp:148,
   151`). No standalone "snow scheduler" exists — it's a seasonal
   modifier of the rain machinery.

10. **The `TArray<int32> Struct Task New Sig Required @ 0x03E0`**
    (`daynightCycle.hpp:73`) — the very oddly-named field suggests a
    Blueprint compile artifact. Not understood.

---

## Section 11 — Wire-packet sketch (informational only — NOT a design decision)

This section is reference, not commitment. Real design lands in the
implementation phase.

### 11.1 Continuous state — `WeatherStatePacket`

Reliable broadcast, fires only on host-side change. Captures the cycle
fields:

| Source field                                   | Bytes |
|------------------------------------------------|-------|
| `isRaining`                                    | 1     |
| `rain` (intensity)                             | 4     |
| `rainStrength`                                 | 4     |
| `rainLightningChance`                          | 4     |
| `rainDeactivateChance`                         | 4     |
| `rainWindSpeed`                                | 4     |
| `permanentRain`                                | 1     |
| `enable_rain`                                  | 1     |
| `enable_fog`                                   | 1     |
| `enable_superfog`                              | 1     |
| `thickFog`                                     | 4     |
| `finalFogDensity`                              | 4     |
| `permanentFog`                                 | 1     |
| `isSnow`                                       | 1     |
| Wind: `Intensity`, `Angle`, `Speed`, `Strength`| 16    |
| Time-of-day: `Day`, `totalTime`, `TimeScale`   | 12    |
| Discrete: `timeZ.X`, `timeZ.Y`, `timeZ.Z`      | 12    |

Total: ~75 bytes per state change. Sparse — most fields only change at
weather rotation boundaries (~minutes apart).

Application on receiver: drive `causeRain` + `setRainProperties` on
the local `AdaynightCycle_C`, drive `setParameters` on local
`AdirectionalWind_C`, write `Day`/`totalTime`/`TimeScale`/`timeZ`
fields directly via reflection.

### 11.2 Discrete events — `WeatherEventPacket` (or `EntityEventPacket` per [[project-coop-re-findings-2026-05-24-pm]])

| Event           | Payload                                                    |
|-----------------|------------------------------------------------------------|
| LightningStrike | `FVector loc` + optional `float radius` + `int32 seed`     |
| SpawnBlackFog   | (no payload — host-driven toggle)                          |
| SpawnRedSky     | `bool isred`                                               |
| SpawnSuperFog   | (no payload — host-driven toggle)                          |
| SpawnSkyFall    | `FVector loc` (for the falling-sky ship event)             |

### 11.3 Suppression on client

For each peer-locally-running scheduler tick, gate execution on the
"is local peer the host" check. Hooks needed:
- `AdaynightCycle_C::timerRain` (UFunction hook PRE — early-return
  on non-host).
- `AdaynightCycle_C::timerLightning` (same).
- `AdaynightCycle_C::fogEvent` (same).
- `AdaynightCycle_C::superFogEvent` (same).
- `AdaynightCycle_C::permaRain_timer` (same).
- `AdaynightCycle_C::rainCleanup` (same).
- `AmainGamemode_C::spawnBlackFog` (same — but allow if wire-driven).
- `AmainGamemode_C::spawnRedSky` (same).

Mirrors the pattern used for mushroomMaster/NpcSpawner suppression in
existing coop work.

---

## Section 12 — Cross-refs to existing project docs

- `[[project-weather-sync-future]]` — scope source (user verbatim).
- `[[project-coop-aprop-lifecycle-RE-inc3-scope]]` — host-suppression
  pattern that applies here.
- `[[project-coop-mushroom-state-RE]]` — same suppress-on-client model.
- `[[project-coop-re-findings-2026-05-24-pm]]` — unified
  `EntityEventPacket` design that lightning strikes / blackFog / redSky
  spawns / skyFallingEvent fit into.
- `[[project-coop-whole-map-sync]]` — weather is whole-map, no AOI
  culling.
- `[[project-coop-save-host-authoritative]]` — `AdaynightCycle_C`'s
  Day/totalTime/timeZ are part of the host snapshot to send on
  connect.
- `research/findings/events/votv-events-RE-2026-05-24.md` — events RE
  including `AskyFallingEvent_C`.

---

## Section 13 — Summary findings

1. **Single scheduler**: `AdaynightCycle_C` owns everything. Suppress
   ~7 timer-UFunctions on the client; receive state via wire; reapply
   via existing setter-UFunctions. No new scheduler architecture
   needed.
2. **Lightning is a spawned actor + interface broadcast**:
   `AlightningStrike_C` is a one-shot actor; damage dispatches via
   `reachedByLightning(AlightningStrike_C*)` on every overlapped
   actor. Wire = `(loc, t)` + spawn on each peer; local BP handles
   the rest. (Identical archetype to `AskyFallingEvent_C` and
   `AskyUfo_C`.)
3. **Rain is a permanent component, not a spawned actor**: never
   spawn/despawn — only toggle state on `AdaynightCycle_C`.
4. **Three fog systems**: base height fog (always on), rolling
   weather fog (`AweatherFogController_C`), heavy fog (`AsuperFog_C`).
   Each is independent — wire each separately.
5. **`AblackFog_C` is a story event, not weather rotation.** Separate
   sync path (singleton on gamemode, toggle via `spawnBlackFog`).
6. **Wind is two pieces**: `AdirectionalWind_C` (gameplay) drives
   stock `AWindDirectionalSource` (engine). Sync `setParameters(I, A,
   S, S)`; engine-side auto-applies to foliage / cloth shaders.
7. **Sky dome `Anewsky_C` is presentational only** — owns no
   time-of-day state. Sync the cycle's time fields; the dome
   regenerates locally.
8. **No `*Schedule*` / `*Rnd*` / `*Random*` class exists** — all RNG
   happens inside `AdaynightCycle_C`'s ubergraph (BP-internal). No
   separate picker actor needs to be hooked or suppressed.
9. **70+ subclasses implement `reachedByLightning`** — confirms
   "spawn one strike actor on each peer" is the right wire shape
   (each peer's local BP wired to its local props/NPCs handles
   damage correctly).
10. **Snow shares the rain machinery** — `bool isSnow` on the cycle +
    seasonal material/particle swaps. No dedicated snow actor or
    scheduler.
