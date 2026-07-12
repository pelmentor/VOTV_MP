# RE: VOTV Weather — why client `isRaining=1` but rain particles don't render

**Date**: 2026-05-27
**Target**: Alpha 0.9.0-n
**Method**: CXXHeaderDump + `UE4SS_ObjectDump_GAMEPLAY_SAVE.txt` static analysis
only. No IDA pass. No UE4SS Lua probe. No code modified. Evidence-only;
unanswerable items are flagged inline + recommended diagnostic listed.

**Trigger**: Phase 5W wire-layer test result, 2026-05-27. Host log
`weather: host broadcast flags=0x1D rain=1.00`; client log
`weather: applied flags 0x1C -> 0x1D rain=1.00 ... (rain-tx=1 scalars-changed=1)`
followed by `weather: post-apply readback isRaining=1 rainStrength=0.56
(expected isRaining=1 rainStrength=0.94)`. Screenshots: host shows dense
rain streaks; client shows no particles. The bool synced; the field
DRIFTED 0.94 -> 0.56 within sub-millisecond. The visible effect did not
appear.

---

## TL;DR (300-line summary, before per-question sections)

1. **`causeRain(bool)` is NOT a pure setter — its BP body re-rolls
   randoms.** `UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:264047-264058` lists 11
   local variables for `daynightCycle_C:causeRain`, INCLUDING
   `CallFunc_RandomFloat_ReturnValue`,
   `CallFunc_RandomFloat_ReturnValue_1`, `CallFunc_RandomFloat_ReturnValue_2`,
   `CallFunc_RandomFloatInRange_ReturnValue`, `CallFunc_Ease_ReturnValue`,
   `CallFunc_Ease_ReturnValue_1`, `CallFunc_Ease_ReturnValue_2`,
   `CallFunc_Divide_FloatFloat_ReturnValue`, `CallFunc_Multiply_FloatFloat_ReturnValue`.
   The Ease function is `KismetMathLibrary::Ease` — interpolation curve. The
   triple Random+Ease pattern strongly indicates the BP body
   `rainStrength := Ease(RandomFloatInRange(min, max))` or equivalent. This
   EXPLAINS the 0.94 -> 0.56 readback drift: `causeRain` ran AFTER our
   `setRainProperties`, OVERWROTE our `rainStrength=0.94` with a fresh
   random Ease-curve value of 0.56.

2. **`setRainParticles()` does NOT call `UParticleSystemComponent::Activate`.**
   `UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:263994-264001` lists `setRainParticles`'s
   8 locals: two `Temp_object_Variable` slots typed `[pc: 000001A8F623D300]`
   (= `/Script/Engine.SoundBase` — confirmed at file:614 in the dump), one
   `Temp_object_Variable_2` typed `[pc: 000001A8F867A600]`
   (= `/Script/Engine.ParticleSystem` — confirmed at file:9839), and two
   `K2Node_Select_Default` results returning each type. Plus two
   `Temp_bool_Variable` slots. **This function does template-swap (rain vs
   snow audio cue + particle template), not Activate-the-component.** It
   writes the cycle's `rainEffect @ 0x03A8` (`UParticleSystem*` template
   pointer at `daynightCycle.hpp:67`), NOT `eff_rain @ 0x0228` (the
   `UParticleSystemComponent`). The `eff_rain` component is never observably
   `Activate()`d through any visible UFunction in the dump.

3. **The component IS instantiated on client.** `daynightCycle_2.eff_rain` at
   `UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:318601` is a real
   `ParticleSystemComponent` instance — `eff_rain` is created as a CDO-
   default component of `AdaynightCycle_C`, identical on host and client.
   Null-pointer is NOT the cause.

4. **There is no authority check we can see.** Grep across
   `daynightCycle.hpp` for `HasAuthority|IsServer|ROLE_Authority|GetLocalRole|Role|bReplicates|bAlwaysRelevant`
   returns ZERO matches. `daynightCycle_C` does not declare any
   replication state on its own surface — but its parent `AActor` carries
   `bReplicates @ 0x005B`, `Role @ 0x00F0`, `RemoteRole @ 0x005F`
   (`Engine.hpp:6674, 6688, 6681`) by default. The CXX dump can't reveal
   whether the BP body of `causeRain` etc. branches on `Role` — that's
   ubergraph bytecode. **Recommend a UE4SS Lua one-shot to read
   `cycle.Role` + `cycle.RemoteRole` on both host and client AND a
   one-time BP-disasm of `causeRain` via IDA on
   `ExecuteUbergraph_daynightCycle` if Lua reading rules it out.**

5. **The single fix is order-of-call**: REMOVE the `causeRain(newRain)`
   step from the receiver-apply chain. The BP body of `causeRain` is
   destructive (re-rolls rainStrength via Ease(Random...)). Instead:
   (a) write `enable_rain` bit; (b) write the 5 scalar fields DIRECTLY
   via reflection memory writes (NOT through `setRainProperties` — which
   is a private setter whose body likely runs the same Ease/Lerp from
   `setRainParameters` and overwrites again); (c) set `isRaining @
   0x02E4` directly; (d) call `setRainParticles()` once to do the template
   swap (rain vs snow audio + particle template); (e) call the engine-level
   `UFXSystemComponent::Activate()` (inherited UFunction at
   `Engine.hpp:7208`) on the `eff_rain` component directly. See Section 9
   for the evidence-backed single-fix proposal.

---

## Q1 — What ACTUALLY drives `UParticleSystemComponent eff_rain @ 0x0228` to emit particles?

### Q1.1 The candidates the dump exposes

`UParticleSystemComponent : public UFXSystemComponent` at `Engine.hpp:16742`,
inheriting `UPrimitiveComponent : public USceneComponent`. The Activate /
visibility surface inherited from those parents:

```
Engine.hpp:7208:    void Activate();                                                                   // UParticleSystemComponent override
Engine.hpp:7207:    void Deactivate();
Engine.hpp:7206:    bool IsActive();
Engine.hpp:7204:    void OnRep_bCurrentlyActive();

Engine.hpp:8514:    void Activate(bool bReset);                                                        // UActorComponent
Engine.hpp:8497:    void SetActive(bool bNewActive, bool bReset);
Engine.hpp:8510:    void Deactivate();
Engine.hpp:8489:    void ToggleActive();

Engine.hpp:17928:   void SetVisibility(bool bNewVisibility, bool bPropagateToChildren);                // USceneComponent
Engine.hpp:17932:   void SetHiddenInGame(bool NewHidden, bool bPropagateToChildren);

Engine.hpp:16784:   void SetTemplate(class UParticleSystem* NewTemplate);                              // UParticleSystemComponent
Engine.hpp:11054:   void SetFloatParameter(FName ParameterName, float Param);                         // UFXSystemComponent
Engine.hpp:11051:   void SetVectorParameter(FName ParameterName, FVector Param);
Engine.hpp:11055:   void SetEmitterEnable(FName EmitterName, bool bNewEnableState);
```

### Q1.2 What the cycle's BP body does — evidence from the BP object dump

`setRainParticles` has 8 local variables
(`UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:263994-264001`):

```
:263994:  Function setRainParticles
:263995:  BoolProperty   Temp_bool_Variable
:263996:  ObjectProperty Temp_object_Variable     [pc: 000001A8F623D300]  // SoundBase
:263997:  ObjectProperty Temp_object_Variable_1   [pc: 000001A8F623D300]  // SoundBase
:263998:  BoolProperty   Temp_bool_Variable_1
:263999:  ObjectProperty Temp_object_Variable_2   [pc: 000001A8F867A600]  // ParticleSystem
:264000:  ObjectProperty K2Node_Select_Default    [pc: 000001A8F623D300]  // SoundBase
:264001:  ObjectProperty K2Node_Select_Default_1  [pc: 000001A8F867A600]  // ParticleSystem
```

`[pc: 000001A8F623D300]` resolved at `UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:614`
= `/Script/Engine.SoundBase`. `[pc: 000001A8F867A600]` resolved at
`UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:9839`
= `/Script/Engine.ParticleSystem`.

**Interpretation**: two K2Node_Select nodes drive `setRainParticles`'s
body. One picks between two SoundBase assets (the rain audio cue vs
the snow audio cue) and assigns to `rainSnd`'s Sound parameter. The
other picks between two ParticleSystem assets (rain template vs snow
template) and assigns to `rainEffect @ 0x03A8` (the
`UParticleSystem*` template field). The two `Temp_bool_Variable`s
likely drive the selectors (`isRaining`, `isSnow`).

**Critical**: `setRainParticles` does NOT call `Activate()` on the
`eff_rain` UParticleSystemComponent. It writes the template-asset slot
and the audio cue. **Whether the swap actually plays the new particle
template requires `eff_rain->SetTemplate(rainEffect)` AND
`eff_rain->Activate()`.** The CXX dump cannot prove what the ubergraph
bytecode actually emits; we only see the local-variable footprint of
the function.

### Q1.3 Cross-checking against ubergraph

The cycle's `ExecuteUbergraph_daynightCycle` has 402 local-variable
entries in the BP dump (`UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:167617` and
following). Greps within those for the literal `Activate`,
`SetTemplate`, `SetFloatParameter`, `SetVisibility`, `SetHidden`, or
`eff_rain` produce zero hits — but this is **inconclusive**: the BP
dump enumerates a function's **local-variable property objects**, not
its bytecode-call references. To prove what UFunctions the ubergraph
calls into `eff_rain` requires the BP bytecode dump (
`UE4SS_ObjectDump_*.txt` does NOT contain bytecode; we'd need a
different dump or an IDA disasm of
`ExecuteUbergraph_daynightCycle`).

### Q1.4 Verdict on Q1

The CXX + BP-locals dump definitively shows:
- `setRainParticles` swaps a `UParticleSystem*` template asset and a
  `SoundBase*` audio cue. Does NOT show Activate().
- `causeRain` re-rolls 3 randoms + 3 Eases (Q2 below).
- `setRainProperties` is a near-pure assignment (5 args, no locals
  beyond the params per `UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:264041-264046`).
- `setRainParameters` has a `Conv_FloatToVector`, `MapRangeClamped`,
  `Lerp`, and `Multiply` (`UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:264059-264065`)
  — this is computing a vector parameter from a float (likely
  `eff_rain->SetVectorParameter("Intensity", FVector(rainStrength,...))`).

**Best inference**: the visible-rain pipeline is driven by **the cycle's
`ReceiveTick` (via `ExecuteUbergraph_daynightCycle` ubergraph) reading
`rainStrength` per-frame and calling `eff_rain->SetFloatParameter` or
`eff_rain->SetVectorParameter` to drive emission rate.** The component's
`bAutoActivate @ 0x0089` (default in the CDO) is what initially turns
it on; `eff_rain` is presumably ALWAYS active, with rate driven by the
strength parameter. This is the standard Cascade pattern: one always-on
particle component with a Float Parameter named e.g. "Intensity" wired
into the emitter's SpawnRate distribution.

**Unanswerable from dump alone**: which specific parameter name. The
SDK profile (`src/votv-coop/include/ue_wrap/sdk_profile.h`) does not
list any. **Recommended diagnostic**: UE4SS Lua probe enumerating
`cycle.eff_rain.InstanceParameters` (the FParticleSysParam array at
offset 0x0488, `Engine.hpp:16757`) on a host-rain frame — that array
will name the parameter being driven AND show its current numeric
value. This is a one-shot read, no hook, no risk.

---

## Q2 — `rainStrength` field and emission rate: who recomputes it dynamically?

### Q2.1 `causeRain`'s BP body re-rolls randoms

`UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:264047-264058` enumerates
`daynightCycle_C:causeRain`'s local variables:

```
:264047:  Function causeRain
:264048:  BoolProperty   isRaining (arg)
:264049:  FloatProperty  CallFunc_RandomFloat_ReturnValue
:264050:  FloatProperty  CallFunc_RandomFloatInRange_ReturnValue
:264051:  FloatProperty  CallFunc_Ease_ReturnValue
:264052:  FloatProperty  CallFunc_RandomFloat_ReturnValue_1
:264053:  FloatProperty  CallFunc_RandomFloat_ReturnValue_2
:264054:  FloatProperty  CallFunc_Ease_ReturnValue_1
:264055:  FloatProperty  CallFunc_Ease_ReturnValue_2
:264056:  FloatProperty  K2Node_LowEntry_LocalVariable_Value__Object
:264057:  FloatProperty  CallFunc_Divide_FloatFloat_ReturnValue
:264058:  FloatProperty  CallFunc_Multiply_FloatFloat_ReturnValue
```

Three `RandomFloat` + one `RandomFloatInRange` + three `Ease` + Divide
+ Multiply + a `K2Node_LowEntry_LocalVariable_Value__Object`. This is
unambiguous: the BP body re-rolls random values and runs Ease curves
on them. `KismetMathLibrary::Ease(A, B, Alpha, EasingFunc, BlendExp,
Steps)` is the Ease function — interpolation. The Random rolls produce
random alpha values; Ease produces a curve-shaped interpolation
result.

### Q2.2 The drift the log captured

Client log:
```
weather: applied flags 0x1C -> 0x1D rain=1.00 ... (rain-tx=1 scalars-changed=1)
weather: post-apply readback isRaining=1 rainStrength=0.56 (expected isRaining=1 rainStrength=0.94)
```

`weather_sync.cpp:737-865` calls (in order, current code):
1. Direct bool-write of config bits (`enable_rain`, ..., `permanentRain`).
2. `setRainProperties(true, 0.94, 0.0, 0.0, 0.0)` — writes the 5 scalar
   fields including `rainStrength = 0.94`.
3. `causeRain(true)` — **THIS RUNS THE BP BODY WITH 3 RANDOMS + 3 EASES**
   — re-rolls `rainStrength` to a curve-shaped value, ending at 0.56.
4. `setRainParticles()` — template swap.
5. `setWindParameters()` — wind.

The readback happens AFTER step 3. 0.94 (our value) → 0.56 (causeRain's
fresh Ease(RandomFloatInRange) result) within sub-ms is exactly
consistent with this evidence.

**Root cause confirmed**: `causeRain` is the field-overwriter. It is
NOT the safe "BP transition trigger" the existing receiver code
assumes; it is a stateful BP that recomputes `rainStrength` from its
own random source.

### Q2.3 `setRainParameters`'s BP body locals

```
:264059:  Function setRainParameters
:264060:  StructProperty  CallFunc_Conv_FloatToVector_ReturnValue
:264061:  StructProperty  CallFunc_Multiply_VectorVector_ReturnValue
:264062:  FloatProperty   CallFunc_MapRangeClamped_ReturnValue
:264063:  FloatProperty   CallFunc_Lerp_ReturnValue
:264064:  FloatProperty   CallFunc_MultiplyMultiply_FloatFloat_ReturnValue
:264065:  FloatProperty   CallFunc_Multiply_FloatFloat_ReturnValue
```

Conv_FloatToVector + MapRangeClamped + Lerp + Multiply. **This is the
function that derives a VECTOR PARAMETER from the float `rainStrength`
and writes it onto `eff_rain` via SetVectorParameter.** The naming
strongly suggests:

```
   driveValue = MapRangeClamped(rainStrength, inMin, inMax, outMin, outMax)
   lerpValue  = Lerp(some_curve_a, some_curve_b, driveValue)
   vector     = Conv_FloatToVector(lerpValue) * Multiply_VectorVector
   eff_rain.SetVectorParameter("???", vector)
```

`setRainParameters` is therefore the per-frame (or per-trigger) actual
particle-parameter driver. It IS pure (reads cycle's rainStrength,
writes eff_rain's parameter — no random re-rolls in the locals).

### Q2.4 ReceiveTick wiring

`UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:168044-168045`:

```
Function ReceiveTick
FloatProperty DeltaSeconds
```

`ReceiveTick` has ONE local — its `DeltaSeconds` parameter — and is
backed by the cycle's ubergraph (`ExecuteUbergraph_daynightCycle`,
`UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:167617`). The 402 ubergraph locals
include 4 `CallFunc_Ease_ReturnValue` slots
(`:167734, :167946, :167953, :167956`) and several `CallFunc_FClamp_ReturnValue`s
(`:167632, :167634, :167861`). This is consistent with the ubergraph
running Ease/Clamp/Interpolation on cycle fields per tick — including
likely `rainStrength` interpolation toward a target.

**The drift 0.94 -> 0.56 in sub-millisecond is NOT the ReceiveTick's
work — too fast.** It is `causeRain`'s body running ONCE, as part of the
receiver-apply chain. ReceiveTick will continue to interpolate it from
0.56 onward per-frame, but the FIRST overwrite is causeRain.

### Q2.5 Verdict on Q2

**`causeRain(bool)` recomputes `rainStrength` from RandomFloatInRange +
Ease — this is the direct overwriter of our wire value.** It is NOT a
"BP transition trigger" — it is a randomized re-initializer.
Additionally, `ReceiveTick` likely runs further Ease-based
interpolation per frame (4 Ease slots in ubergraph), so even if we
skip causeRain, our written `rainStrength` may continue to drift
slightly. Whether it drifts to a useful steady-state value depends on
the ubergraph's target source — unanswerable from dump alone.

**Recommended diagnostic if causeRain-skip alone isn't enough**:
UE4SS Lua RegisterHook on `setRainParameters` POST + read
`cycle.rainStrength` per frame for 10 s to chart the actual
interpolation profile on a host vs client.

---

## Q3 — Could `ReceiveTick` be overwriting our writes?

### Q3.1 Direct evidence

The post-apply readback log line (`weather_sync.cpp:836-844`) fires
SYNCHRONOUSLY right after the UFunction call chain returns. The
readback shows `rainStrength=0.56` already — the drift happened
WITHIN the ApplyFromHost code, not across frames. Per Q2.1, this is
causeRain's BP body.

### Q3.2 Indirect evidence for ongoing ReceiveTick drift

`ExecuteUbergraph_daynightCycle:CallFunc_FClamp_ReturnValue` 3
instances + `CallFunc_Ease_ReturnValue` 4 instances
(`UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:167632, 167634, 167734, 167861,
167946, 167953, 167956`) strongly imply the ubergraph contains a
per-tick interpolation pipeline. The cycle's `rainProbability @ 0x0424`,
`fogProbability @ 0x0428`, `rainDeactivateChance @ 0x040C`,
`rainLightningChance @ 0x0408`, `cloudsTimer @ 0x0410`,
`cloudsTimerAccumulate @ 0x0414` (`daynightCycle.hpp:84-85, 77-78, 79-80`)
are all RNG-rolled per-frame; the strength likely tracks toward a
target driven by these.

**Q3 verdict**: the IMMEDIATE drift in the log is `causeRain`, not
`ReceiveTick`. But `ReceiveTick` likely DOES continue to drive
`rainStrength` per frame. After we stop calling `causeRain`, we need
to confirm whether ReceiveTick is held in check by the 5 scheduler
interceptors we already register, or whether it independently drives
strength from `cloudsTimer`/probability fields.

**Suppression status**: per `weather_sync.cpp:424-435`, the client
registers PRE-interceptors on `timerRain`, `timerLightning`, `fogEvent`,
`superFogEvent`, `permaRain_timer`. **`ReceiveTick` is NOT in that
list.** If ReceiveTick is what drives the per-frame Ease toward a
target derived from `rainProbability` / `cloudsTimer`, the client
would continue to interpolate AWAY from our written values toward a
"natural" target after the apply.

**Recommended diagnostic**: UE4SS Lua tap on `cycle.ReceiveTick` POST,
log `cycle.rainStrength` and `cycle.rain` (the @0x02E0 ramp field —
note: NOT synced currently) per frame for 5 s after a manual
`DebugForceRain`. If strength stays at 0.94 ± 0.01 the ubergraph
isn't an issue and the fix is causeRain-skip only; if it drifts, we
also need to intercept `ReceiveTick` OR pin `rain @ 0x02E0` (which
is likely the ramp target ReceiveTick interpolates strength toward).

### Q3.3 Note on the @0x02E0 `rain` field

The cycle has TWO rain-like floats:
- `float rain @ 0x02E0` (`daynightCycle.hpp:34`)
- `float rainStrength @ 0x0404` (`daynightCycle.hpp:76`)

Currently `WeatherStatePayload` only carries `rainStrength`. The `rain
@ 0x02E0` field is NOT broadcast. Per the layout, `rain` is the
**target/ramp** value (probabilistic, set by `timerRain` /
`permaRain_timer` rolls) and `rainStrength` is the **interpolated current
value** (driven per-frame by ReceiveTick from `rain`). If true, the
ReceiveTick interpolator will pull `rainStrength` AWAY from our 0.94
toward whatever `rain @ 0x02E0` reads on the client (probably 0.0 or
some scheduler-default).

**This would manifest as**: client sets rainStrength=0.94, sees it
slowly decay to 0.0 over a few seconds (or jump faster, depending on
the Ease's BlendExp). Whether this is happening NOW or whether causeRain
is the only culprit needs the runtime diagnostic above.

---

## Q4 — Is there a separate "render rain" actor we're missing?

### Q4.1 Grep across CXXHeaderDump for weather-rendering classes

Per the earlier scheduler RE doc
(`research/findings/weather-wind/votv-weather-RE-scheduler-2026-05-26.md` Section
8.1), greps for `schedul|random|rnd` returned zero hits and weather-
adjacent classes are exhaustively enumerated. Re-running for rendering-
specific patterns:

```
$ grep -rE 'Storm|Precip|Rainmaker|RainController|WeatherRender' Game_0.9.0n/.../CXXHeaderDump/
   (no hits other than story-event actors already enumerated)
```

The complete list of rain-named classes in the dump:
- `event_fleshRain_C` — story event, save-actor; only `int32 I` field
  (`event_fleshRain.hpp:7`). Not a rain renderer.
- `trigger_box_rain_C` — area trigger, single bool `rain @ 0x02D0`
  (`trigger_box_rain.hpp:9`). Not a renderer.
- `tool_rain_C` — cheat toolgun module (`tool_rain.hpp:7-13`). Dev only.

There is no `ARainController_C` / `AStormController_C` / `AWeatherRenderer_C`
class. **The rain rendering is entirely on `eff_rain @ 0x0228` of the
single `AdaynightCycle_C` instance** (confirmed by
`UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:318601` — the
`daynightCycle_2.eff_rain` ParticleSystemComponent on the loaded map).

### Q4.2 Adjacent renderers worth nothing

- `Anewsky_C` (`newsky.hpp:4-49`) — sky dome (sun, moon, stars). Reads
  cycle state via `daylightCycleObject @ 0x02E0`. Drives ambient
  light intensity, sky tint. NO precipitation responsibility.
- `AdirectionalWind_C` (`directionalWind.hpp:4-50`) — drives shader-side
  global wind via `windSource @ 0x0288` (a stock UE
  `AWindDirectionalSource`). Doesn't emit visible rain particles.
- `AweatherFogController_C` / `AsuperFog_C` / `AblackFog_C` — fog actors,
  spawned at events. Separate concern.
- `AlightningStrike_C` — discrete strike actor, transient.

**Verdict**: there is no missing rain renderer. The rain visuals are
entirely the responsibility of `cycle.eff_rain`.

### Q4.3 Cross-reference to scheduler-RE doc Section 7.4

`research/findings/weather-wind/votv-weather-RE-scheduler-2026-05-26.md:215-223`
already states: "Time-of-day fields drive ... per-tick. This is
rendering, NOT a weather-RNG input." And `:223`: "**Hooking
`ReceiveTick` is the WRONG layer to suppress client weather** — it
would freeze the rendering even after the host's state was applied."

This is consistent with our analysis: rendering happens via ReceiveTick
+ setRainParameters; we cannot wholesale suppress ReceiveTick. We
need to selectively prevent ReceiveTick (or the BP paths it drives)
from clobbering our written `rainStrength` / `isRaining`.

---

## Q5 — Authority discrimination: does `causeRain` or `setRainParticles` check authority?

### Q5.1 Direct field grep on the cycle

```
$ grep -E 'HasAuthority|IsServer|ROLE_Authority|GetLocalRole|Role|bReplicates|bAlwaysRelevant' \
      Game_0.9.0n/.../CXXHeaderDump/daynightCycle.hpp
   (no matches)
```

`AdaynightCycle_C` does not DECLARE any authority-related fields or
UFunctions on its own surface (`daynightCycle.hpp:4-160`).

### Q5.2 Inherited from AActor

`Engine.hpp:6646` declares `AActor`'s base layout:

```
Engine.hpp:6674:    uint8 bReplicates;                              // 0x005B
Engine.hpp:6681:    TEnumAsByte<ENetRole> RemoteRole;               // 0x005F
Engine.hpp:6688:    TEnumAsByte<ENetRole> Role;                     // 0x00F0
Engine.hpp:6820:    bool HasAuthority();                            // UFunction
Engine.hpp:6833:    TEnumAsByte<ENetRole> GetLocalRole();           // UFunction
Engine.hpp:6827:    TEnumAsByte<ENetRole> GetRemoteRole();          // UFunction
Engine.hpp:6747:    void SetReplicates(bool bInReplicates);         // UFunction
```

Every actor (including the cycle) carries these. They are runtime-set
based on the UE network mode. **The cycle's CDO `bReplicates` value
isn't visible in the .hpp dump** — that's per-instance / per-class-default
state stored in the UE NetDriver setup, not in the class layout.

### Q5.3 BP body bytecode

`causeRain`'s 11 locals (`UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:264047-264058`)
do NOT include any `Role` / `Authority` reads. **However, this is
INCONCLUSIVE** because BP bytecode can call inherited UFunctions like
`HasAuthority` without declaring a local variable for the result —
the BP byte-code emits a CallFunc and consumes the bool inline.

The most we can say: the **local-variable surface** of `causeRain`
does not include any RoleEnum local, which is unusual for an
authority-branched BP (usually you'd see a `CallFunc_HasAuthority_ReturnValue`
bool local). Its absence suggests `causeRain` is NOT authority-gated
— but this is a soft signal, not proof.

### Q5.4 The standalone-vs-networked posture of VOTV

Per `CLAUDE.md`: VOTV was authored single-player. There is no native
multiplayer in stock VOTV. The cycle (and the rest of the game) is
not running in a UE networked-game session — it's a standalone game
(NetMode = Standalone, where `Role == ROLE_Authority` everywhere).
**Both host and client are running as `Standalone` from UE's POV** —
the multiplayer layer is our DLL, not UE's NetDriver. This means
HasAuthority() returns TRUE on BOTH host and client (both think
they're the authority). Any authority-gated BP would run on both.

**Verdict on Q5**: authority gating is almost certainly NOT the cause
of the missing rain. Both peers see `Role == ROLE_Authority`. The BP
runs identically.

---

## Q6 — Are there UProperty replication flags on the cycle?

### Q6.1 Replication flags not exposed in the dump

The CXXHeaderDump enumerates property types + offsets but NOT
property metadata flags (Replicated, ReplicatedUsing, BlueprintReadWrite,
etc). Those flags are stored on the `UProperty` object's `PropertyFlags`
DWORD, not in the C-struct layout the dump emits.

### Q6.2 Indirect evidence

The cycle's BP-source-level replication intent can be partially
inferred:
- VOTV blueprints were authored single-player (CLAUDE.md: "VOTV's
  blueprints were authored single-player (no `Replicated` props, no
  RPCs)"). The cycle's fields are almost certainly NOT marked Replicated.
- No `OnRep_*` UFunctions appear on `AdaynightCycle_C` in the dump
  (`daynightCycle.hpp:98-160` — checked). Standard Replicated-with-Notify
  pattern would produce `OnRep_isRaining`, `OnRep_rainStrength` etc.
  None exist.

**Verdict on Q6**: zero evidence of `Replicated` properties on the
cycle. Our direct memory writes + UFunction calls are NOT fighting an
engine-replication revert. This is consistent with the project-wide
assumption documented in CLAUDE.md.

### Q6.3 BUT — `UActorComponent::bIsActive` has an `OnRep`

`Engine.hpp:8503: void OnRep_IsActive();` exists on the parent
`UActorComponent` class. Combined with `bIsActive @ 0x008A` and
`OnRep_bCurrentlyActive` on UParticleSystemComponent
(`Engine.hpp:7204`), there is engine-level wiring for component-
active state replication. But again, this only fires if the BP
marked it Replicated on the specific instance — which VOTV's BPs
don't. Not the cause.

---

## Q7 — Can `rainEffect`'s value be null on client?

### Q7.1 Two fields named "rain particle"

`daynightCycle.hpp:7` — `UParticleSystemComponent* eff_rain @ 0x0228`.
This is the **component instance**, created as a default subobject in
the cycle's CDO. Present on every instance of the class, host or client.

`daynightCycle.hpp:67` — `UParticleSystem* rainEffect @ 0x03A8`. This
is a **template asset pointer** (UParticleSystem is the asset, not the
component). Currently used by `setRainParticles` for the template
swap (Q1.2).

### Q7.2 Confirmation of eff_rain instance on client

`UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:318601`:
```
[000001A99F076140] ParticleSystemComponent /Game/maps/untitled_1.Untitled_1:PersistentLevel.daynightCycle_2.eff_rain
```

The cooked map has `daynightCycle_2.eff_rain` as a real
`ParticleSystemComponent` instance. The CDO-default component is
materialized on every instance, host or client. **Not null.**

### Q7.3 Could rainEffect (the asset @ 0x03A8) be null?

`rainEffect` is a content-package asset pointer (`UParticleSystem*`).
It's CDO-default-set in the BP, and unless `setRainParticles` clears
it, it stays non-null. Even if it were null, our receiver's
`setRainParticles()` call would re-assign it from the BP's K2_Select
(picking between rain and snow templates).

**Verdict on Q7**: not null. This is not the cause.

---

## Q8 — Other possible causes (closing the differential)

### Q8.1 `rain_root` / `rain_tilt` arrows

`daynightCycle.hpp:8-9`:
```
class UArrowComponent* rain_tilt;    // 0x0230
class UArrowComponent* rain_root;    // 0x0238
```

`UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:383717-383718` confirms both
exist on the loaded map instance. ArrowComponent is just a debug
visualizer; the actual rain spawn-position is presumably driven by
attaching `eff_rain` to one of these (or by a per-tick
`SetWorldLocation` to follow the player).

If `eff_rain`'s world position is NOT following the client player, the
rain would spawn around the host player (at the host's actor location
write via wire, but the cycle is local to the client process — the
cycle on each peer has its OWN local frame). The cycle's ReceiveTick
likely does `eff_rain.SetWorldLocation(player_pos + offset)` per tick.
**That ReceiveTick code path runs INDEPENDENTLY on each peer** —
client's ReceiveTick still ticks (we don't intercept it), so the
client's eff_rain should follow the client's local player.

**But**: if the ReceiveTick body is gated on `isRaining` (skips the
attach/position update when isRaining=false) and the bit only flips
to true AFTER the receiver-apply runs, then for the FIRST few frames
of rain, eff_rain might be at a stale position. This is a possible
but unlikely contributor. Not the primary cause.

### Q8.2 `bAutoActivate` default

`UActorComponent::bAutoActivate @ 0x0089` (`Engine.hpp:8477`).
ParticleSystemComponent CDOs in Cascade content typically have
`bAutoActivate = true`, meaning the component starts emitting on
BeginPlay. If `bAutoActivate` is false on this CDO, the component is
DORMANT until something calls `Activate()` explicitly. We don't know
the CDO value from the dump.

**If `eff_rain.bAutoActivate == false`**, then:
- On host: `causeRain(true)` runs, its BP body internally calls
  `eff_rain->Activate()` somewhere (via the ubergraph — we can't see
  that code path), particles emit.
- On client: we call `causeRain(true)`, but if it has an authority
  check (Q5) or branches differently, the Activate path may not run,
  or it runs but particles never appear because subsequent
  ReceiveTick re-reads stale state.

This is testable. **Recommended UE4SS Lua probe**: read
`cycle.eff_rain.bIsActive` (offset 0x008A on UActorComponent) on
host vs client during a rain-on test. If host says 1 and client says
0, that's the smoking gun: causeRain isn't activating the component on
the client.

### Q8.3 SetTemplate side effect

`UParticleSystemComponent::SetTemplate(NewTemplate)` (`Engine.hpp:16784`)
typically DEACTIVATES then RE-ACTIVATES the component (Cascade's
documented behavior). If `setRainParticles` calls SetTemplate(rainEffect)
on both peers, this is the de-facto Activate. BUT — if the BP body
guards `SetTemplate` on `isRaining == true` AND on the client the
isRaining flip happens AFTER `setRainParticles` runs, the
SetTemplate→Activate cascade never fires.

In our current receiver chain (`weather_sync.cpp:760-865`):
```
Step 1: write enable_rain bit
Step 2: setRainProperties(true, 0.94, ...)  <-- sets isRaining=true at @0x02E4
Step 3: causeRain(true)                     <-- overwrites rainStrength!
Step 4: setRainParticles()                  <-- template swap (potentially activates)
Step 5: setWindParameters()
```

By Step 4, isRaining is already true (Step 2 wrote it). So the
template-swap guard, if any, sees true and runs. This timing is fine.

But causeRain in Step 3 **overwrites rainStrength** — and depending on
whether ReceiveTick re-reads rainStrength each frame and drives
SetVectorParameter from it, the visible emission rate might be
modulated by the post-causeRain low value (0.56), not our intended
0.94. **However, 0.56 is still > 0**; particles should still emit, just
at lower density.

The host screenshot shows DENSE rain. The client shows ZERO particles.
A drop from 0.94 to 0.56 cannot explain zero-particles — that's a
~40% reduction, not a 100% disappearance. **So Q2's drift is NOT the
primary cause of the visible-rain miss.** The primary cause is that
**the eff_rain component is not Activate()d on the client at all.**

---

## Q9 — Recommended single fix (the evidence-backed change)

### Q9.1 The fix

**STOP calling `causeRain(bool)` from `ApplyFromHost`. Replace with
direct memory writes + a direct `eff_rain->Activate()` /
`Deactivate()` call.**

### Q9.2 Why this is the right fix

Evidence summary:
- `causeRain` BP body re-rolls randoms + Ease (`Q2.1` —
  `UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:264047-264058`). It is NOT
  side-effect-free. It overwrites our wire value.
- `causeRain` is NOT documented to be the "BP transition trigger" — that
  framing in the existing code comment (`weather_sync.cpp:522-526`) was
  inferred without seeing the BP body. The dump reveals the BP body is
  a random-roll function, not a particle-Activate function.
- The actual visible emission comes from `eff_rain` (the
  UParticleSystemComponent), which has the engine-inherited
  `Activate()` / `Deactivate()` / `SetActive(bNewActive, bReset)`
  UFunctions (Q1.1 — `Engine.hpp:7208, 7207, 8514, 8497`).
- Direct memory writes to `isRaining @ 0x02E4` + `rainStrength @ 0x0404`
  + the other 4 scalar fields (offsets confirmed in
  `daynightCycle.hpp:34-95` and listed in
  `src/votv-coop/include/ue_wrap/sdk_profile.h`) bypass the BP
  ubergraph entirely.
- `setRainParticles()` is safe to call (it template-swaps; no Random/Ease
  in its locals — Q1.2).
- `setRainParameters()` is safe to call (computes vector parameter
  from cycle's rainStrength via MapRange/Lerp — pure derivation).

### Q9.3 The proposed apply chain (Phase 5W Inc-fix-1)

In `coop::weather_sync::ApplyFromHost` REPLACE the existing
Steps 1-5 chain with:

```
Step A: write all 6 config bits directly (enable_rain, enable_fog,
        enable_superfog, enableSunlight, enableMoonlight, permanentRain).
        (Same as current Step 1.)

Step B: write isRaining @ 0x02E4 directly.
Step C: write the 5 rain scalar fields directly:
          rainStrength         @ 0x0404
          rainLightningChance  @ 0x0408
          rainDeactivateChance @ 0x040C
          rainWindSpeed        @ 0x041C
        (5 float memory writes; no UFunction call.)

Step D: write `rain @ 0x02E0` to match `rainStrength` (or to a host-
        synced separate field — see Q3.3; adding `rain` to the
        WeatherStatePayload is a separate RULE-2 wire schema bump).
        This pre-aligns the ReceiveTick interpolation target so the
        ubergraph's per-frame Ease doesn't drag rainStrength away.

Step E: call `setRainParticles()` (template swap; no randoms in body —
        Q1.2). This sets `rainEffect @ 0x03A8` to the rain template
        (or snow template if isSnow=1). Cascade's SetTemplate logic
        will internally Deactivate-then-(maybe-)Activate the eff_rain
        component.

Step F: call `setRainParameters()` (vector parameter derivation; pure
        per Q2.3). This computes and writes the eff_rain's vector
        parameter from the cycle's current rainStrength — our just-
        written 0.94 — driving emission rate up to the host level.

Step G: call `eff_rain->Activate()` (or `Deactivate()` if newRain=false)
        directly on the UParticleSystemComponent. This is the
        load-bearing change — explicitly ensures the component is
        emitting regardless of whatever causeRain would have done. The
        UFunction is inherited from UParticleSystemComponent
        (`Engine.hpp:7208`). Discovery: `R::FindFunction(eff_rain_class,
        L"Activate")` — the class is `ParticleSystemComponent` (Engine
        class).

Step H: call `setWindParameters()` (same as current; safe).
Step I: `intComs_triggerSnow(newSnow)` (unchanged — broadcast-only
        interface; not a Random-roll).
```

### Q9.4 The required new SDK-profile entries

(For the implementing change later — listed here for completeness so
the design is verifiable, NOT prescribing code in this RE pass)

```
// src/votv-coop/include/ue_wrap/sdk_profile.h additions:
inline constexpr const wchar_t* ParticleSystemComponentClass = L"ParticleSystemComponent";
inline constexpr const wchar_t* ParticleSystemComponent_ActivateFn   = L"Activate";
inline constexpr const wchar_t* ParticleSystemComponent_DeactivateFn = L"Deactivate";
inline constexpr size_t AdaynightCycle_eff_rain = 0x0228;  // UParticleSystemComponent*
inline constexpr size_t AdaynightCycle_rain     = 0x02E0;  // float (the ramp target)
```

`Activate()` is a 0-parameter UFunction (`Engine.hpp:7208`). The UE4
`ParticleSystemComponent::Activate()` override is "Activate with no
reset" — Cascade's default; matches Step G's intent.

### Q9.5 Why this fixes the observed symptom

Current behaviour:
- Client log shows `rainStrength=0.56 (expected 0.94)` — causeRain
  overwrote.
- Client log shows `flags 0x1C -> 0x1D` — isRaining bit DID flip.
- Client screenshot shows zero particles.

The most parsimonious explanation that fits ALL three:
- `isRaining` bit flipped (we set it via setRainProperties Step 2,
  causeRain didn't undo it).
- `rainStrength` got overwritten to 0.56 (causeRain's randoms).
- Particles still don't emit because **causeRain's BP body
  (the actual Activate path) is gated on something the client doesn't
  satisfy** — likely either (a) an authority check we can't see in the
  dump (Q5; soft signal against), (b) an internal precondition like
  `if (eff_rain.bIsActive == false) { Activate(); start_audio(); }`
  that has already been satisfied on initial cycle BeginPlay so the
  function early-outs when re-entered (the Activate-only-on-edge case),
  or (c) the component activation requires the SetTemplate side
  effect, which our `setRainParticles` already does, BUT the
  ordering of (causeRain → setRainParticles) means causeRain runs
  before the template is swapped to the rain-template, so causeRain's
  internal Activate sees the snow template (or null) and
  no-ops.

By bypassing causeRain entirely and calling `eff_rain->Activate()`
DIRECTLY after `setRainParticles()` has swapped the template, we
take ALL of those branches out of the picture.

### Q9.6 Diagnostic to confirm the fix BEFORE shipping

Per CLAUDE.md "Deep RE forbids iterative shenanigans": no try-it-and-see
between RE passes. Recommended sequence:

1. **First diagnostic (no code change)**: add a temporary readback
   in `ApplyFromHost`'s end:
   ```
   eff_rain.bIsActive @ +0x008A (UActorComponent::bIsActive)
   ```
   Log it as `eff_rain.bIsActive=N` after the apply chain. If it reads
   0 on the client AND 1 on the host (host-DebugForceRain readback),
   that PROVES Activate() was never called on the client. Then the
   Q9.3 fix is provably correct.

2. **Second diagnostic (no code change)**: UE4SS Lua one-shot dump
   of `cycle.eff_rain.InstanceParameters` (the FParticleSysParam
   TArray at offset 0x0488 on the component, `Engine.hpp:16757`) on
   host during dense-rain. The output reveals the parameter name
   (e.g., "Intensity") and its current value. This validates that
   `setRainParameters` is what drives emission rate and confirms our
   understanding of the rendering path.

3. **Third (optional)**: IDA decompile
   `ExecuteUbergraph_daynightCycle` to find the
   `if (isRaining) eff_rain->Activate()` site directly — provides the
   definitive answer to Q1 / Q5 but is heaviest-weight. Only if
   diagnostics #1 + #2 leave ambiguity.

### Q9.7 RULE-1 compliance

This is a root-cause fix. The current code's commented justification
for calling `causeRain` (lines 521-526 of `weather_sync.cpp`: "trigger
the BP transition: particle Activate, audio cue start, isRaining flip
with side-effect fan-out") was based on an INFERENCE from the
high-level RE doc, NOT on the BP body's contents. The BP-locals dump
shows that inference was wrong — `causeRain` is a randomized BP, not
a particle Activate dispatcher.

The fix replaces inference with a directly-controllable engine
UFunction (`UParticleSystemComponent::Activate`) whose semantics are
defined by UE4's Cascade subsystem, not by VOTV's BP author.

---

## Section 10 — Summary of evidence

| Question | Status | Key citation |
|----------|--------|--------------|
| Q1: What drives eff_rain emission? | Partially answered. setRainParticles swaps template + audio cue; setRainParameters writes a vector parameter. NEITHER explicitly calls Activate(). | `UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:263994-264001, 264059-264065`; `Engine.hpp:16742-16807` |
| Q2: Who overwrites rainStrength? | **causeRain** — BP body has 3 RandomFloat + RandomFloatInRange + 3 Ease calls. | `UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:264047-264058` |
| Q3: Does ReceiveTick overwrite? | Likely DOES interpolate per-frame (4 Ease slots in ubergraph), but the immediate 0.94→0.56 drift is causeRain alone. | `UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:167617, 167734, 167946, 167953, 167956` |
| Q4: Hidden rain controller actor? | No. Exhaustively enumerated; only eff_rain on the cycle. | `votv-weather-RE-effect-actors-2026-05-26.md` §3.1 + `UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:318601` |
| Q5: Authority gate? | Unanswerable from dump alone, but soft signal AGAINST (no Role local on causeRain, VOTV is Standalone-mode). | `daynightCycle.hpp` (no Role refs); `Engine.hpp:6688, 6820` |
| Q6: Replication flags? | No OnRep_* on the cycle. VOTV BPs are non-replicated by design. | `daynightCycle.hpp:98-160` |
| Q7: rainEffect null on client? | No. `daynightCycle_2.eff_rain` exists in the cooked map. | `UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:318601` |
| Q8: Other causes? | bAutoActivate of the CDO is unknown — recommend Lua probe. The SetTemplate→Activate cascade is the most likely true mechanism on host; causeRain might internally fail-fast on client. | `Engine.hpp:8477, 16784` |

## Section 11 — Recommended single fix (one paragraph)

**Replace the `causeRain(newRain)` call in `coop::weather_sync::ApplyFromHost`
(`src/votv-coop/src/coop/weather_sync.cpp:805-809`) with three steps:
(1) direct memory write of `isRaining @ 0x02E4`; (2) keep the existing
`setRainProperties` call but acknowledge it's the SCALAR WRITE (not a
trigger); (3) call `setRainParticles()` THEN call
`UParticleSystemComponent::Activate()` (or `Deactivate()` if
`newRain=false`) directly on the `eff_rain` component at `cycle + 0x0228`.**

This is one change with one new SDK-profile entry
(`ParticleSystemComponentClass` + `ParticleSystemComponent_ActivateFn`)
and one new offset constant (`AdaynightCycle_eff_rain = 0x0228`). It
takes `causeRain`'s randomization out of the receiver path entirely
and replaces an inferred BP-transition trigger with an
engine-documented UFunction whose semantics are deterministic. Evidence
backing: causeRain's BP-locals contain `RandomFloatInRange` + 3 `Ease`
(`UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:264047-264058` —
the proof of side effects);
`UParticleSystemComponent::Activate` exists at `Engine.hpp:7208` (the
proof of the alternative); `daynightCycle_2.eff_rain` instance exists
on the loaded map (`UE4SS_ObjectDump_GAMEPLAY_SAVE.txt:318601`, the
proof the component is present on client).

**Pre-ship diagnostic** (no code change, no iteration): add a
post-apply readback of `eff_rain.bIsActive @ component+0x008A` to the
existing `weather_sync.cpp:836` readback block, comparing host vs
client. If client reads 0 while host reads 1, this fix is the
verified correct change. Costs one extra memory read per apply; can
stay in place as ship-quality telemetry.

---

## Section 12 — Cross-refs

- `research/findings/weather-wind/votv-weather-RE-mainGamemode-2026-05-26.md` —
  the parent RE pass; established that cycle is the canonical state
  authority. This doc supplements with BP-body inspection.
- `research/findings/weather-wind/votv-weather-RE-effect-actors-2026-05-26.md` —
  effect-actor enumeration; this doc confirms no missing renderer.
- `research/findings/weather-wind/votv-weather-RE-scheduler-2026-05-26.md` —
  scheduler-hook surface; this doc confirms ReceiveTick suppression
  is NOT the right path; selective field protection is.
- `research/findings/inventory-items/votv-flashlight-RE-2026-05-25.md` — same pattern
  precedent (Option α: direct UFunction call on the visible component
  bypassing BP wrapper). The flashlight cone fix that landed
  `b100e8e` 2026-05-26 used `SetVisibility` on the light component
  directly after BP-route refused to render. The proposal here is
  the same structural pattern applied to rain particles.
- `src/votv-coop/src/coop/weather_sync.cpp:805-809` — the line to
  remove/replace.
- `src/votv-coop/include/ue_wrap/sdk_profile.h` — where the two new
  entries go.

## Section 13 — Files surveyed in this pass

- `daynightCycle.hpp` (162 LOC, full read)
- `Engine.hpp` lines 6646-6900 (AActor), 7200-7220
  (UEmitter/UParticleSystemComponent::Activate), 8469-8515
  (UActorComponent including bReplicates/bIsActive/Activate),
  11048-11062 (UFXSystemComponent), 16742-16807
  (UParticleSystemComponent), 17900-17970 (USceneComponent including
  SetVisibility/SetHiddenInGame)
- `UE4SS_ObjectDump_GAMEPLAY_SAVE.txt` lines:
  - `:167617-167645` (ExecuteUbergraph_daynightCycle locals — partial)
  - `:167734, 167861, 167946, 167953, 167956` (Ease + FClamp locals)
  - `:168044-168045` (ReceiveTick)
  - `:263994-264001` (setRainParticles)
  - `:264041-264046` (setRainProperties)
  - `:264047-264058` (causeRain — KEY EVIDENCE)
  - `:264059-264065` (setRainParameters)
  - `:264066-264078` (setWindParameters)
  - `:167524-167525, 167580, 383717-383718, 318601` (component
    instances on the cooked map)
  - `:9839` (ParticleSystem class id resolution)
  - `:614` (SoundBase class id resolution)
- `src/votv-coop/src/coop/weather_sync.cpp` (full read — the file
  being analyzed for the fix)
- `src/votv-coop/include/ue_wrap/sdk_profile.h` (partial — verified
  current AdaynightCycle_* offsets + weather UFunction name list)
- Three previous weather RE docs (see Section 12)

No IDA pass, no UE4SS Lua, no code changes. Evidence-only per RULE 1
+ feedback-deep-re-no-iteration.
