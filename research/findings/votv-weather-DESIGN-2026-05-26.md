# Phase 5W weather sync — design (synthesis of 3 RE docs)

Read first:
- `votv-weather-RE-mainGamemode-2026-05-26.md` (1107 LOC) — state ownership
- `votv-weather-RE-effect-actors-2026-05-26.md` (~780 LOC) — effect actors
- `votv-weather-RE-scheduler-2026-05-26.md` (992 LOC) — scheduler + tick

This doc is the implementable plan. It cites the 3 RE docs by section.

## Convergence point (all 3 agents independently confirm)

**`AdaynightCycle_C` is the single weather authority.** Singleton, owned by
`AmainGamemode_C::daynightCycle@0x0450`. There is NO separate
`AweatherController_C`; everything weather-related on the live timeline
flows through this one actor:

- The 5 timers that decide "what happens next" (rain/lightning/fog).
- The 12 scalar+bool state fields that describe the live weather.
- The 8 mutator UFunctions that apply the state.

`AweatherFogController_C` is an EVENT actor spawned by the cycle's
`fogEvent`/`superFogEvent`/`spawnFog`. Its state is a byproduct of those
spawn calls — no separate wire packet needed.

`Anewsky_C` is presentational only (sun/moon/sky dome). The ToD lives on
the cycle (`Day`, `Phase`, `MaxTime`, `totalTime`). Sync the cycle, not
the sky.

## What is in scope for Phase 5W

| Category | What | How |
|---|---|---|
| State (continuous) | rain on/off, strength, lightning chance, deactivate chance, wind speed, snow on/off, fog enables | `WeatherState` reliable packet (host pushes on state-change + connect-edge) |
| Wind | intensity, angle, speed, mult | Carried inside `WeatherState` (same packet) |
| Lightning strike | discrete `(loc, t)` event | NEW `LightningStrike` reliable packet (one per strike) |
| Suppression | client never runs its own scheduler | 5 PRE-hooks early-return on client |

## What is OUT OF scope for Phase 5W

Belongs in a future phase that introduces the unified `EntityEventPacket`
(see `project_coop_re_findings_2026_05_24_pm.md`):

- `AmainGamemode_C::spawnBlackFog` (story-event post-process)
- `AmainGamemode_C::spawnRedSky`
- `AmainGamemode_C::Spawn Bad Sun`
- `Aevent_fleshRain_C` (story event marker; NOT a rain variant)
- `AskyFallingEvent_C` (alien-ship event)
- `outsideChurch::init_lightningStorm` (story-driven lightning storm)

These are sparse one-shot story events with custom assets/sequences that
warrant their own packet kind. Logged in scope.md amendment.

Also out of scope for this phase:
- Full time-of-day sync (separate workstream — affects weather coupling
  but its own surface area).
- `weatherFogController.Alpha/Duration/fogPhase` real-time sync — the
  effect actor's state is reproduced on each peer by re-running the
  same `spawnFog`/`fogEvent`/`superFogEvent` UFunction on the receiver
  with the same params (the bodies are deterministic enough; if visual
  drift is noticed, revisit per RULE 1).

## Wire packet design

```cpp
// protocol.h additions:
enum class ReliableKind : uint8_t {
    // ...existing...
    WeatherState   = 13,  // continuous state push (rain/snow/fog/wind)
    LightningStrike= 14,  // discrete event (loc + tag)
};

// 24-byte WeatherState payload (no padding hazards on x86-64 alignment):
struct WeatherStatePayload {
    uint8_t peerSessionId;     // sender = host (always 0); validate on receive
    uint8_t flags;             // bit 0: isRaining; 1: isSnow;
                               // 2: enable_rain; 3: enable_fog;
                               // 4: enable_superfog; 5: enableSunlight;
                               // 6: enableMoonlight; 7: reserved
    uint16_t pad;
    float  rainStrength;       // [0..1] (BP-typical range)
    float  rainLightningChance;// [0..1]
    float  rainDeactivateChance;// [0..1]
    float  rainWindSpeed;      // m/s-ish (BP-defined)
    // Wind setParameters block (4 floats per RE doc):
    float  windIntensity;
    float  windAngle;          // degrees, normalized at wire boundary
    float  windSpeed;
    // Fog: synced via cycle scalars (no separate fields here; receiver
    // re-fires fogEvent/superFogEvent via the enable bits if needed).
}; // 28 bytes

struct LightningStrikePayload {
    uint8_t peerSessionId;
    uint8_t pad[3];
    float locX;
    float locY;
    float locZ;
}; // 16 bytes
```

## Host-side implementation (sender)

A new file `coop/weather_sync.{h,cpp}` under
`src\votv-coop\src\coop\` (NOT a subdir of item_activate; different
subsystem, dedicated file).

**Install** (idempotent, retried per NetPumpTick like other coop subsystems):
1. Find class `AdaynightCycle_C` via `R::FindClass`.
2. Resolve the 5 scheduler UFunctions for client suppression
   (`timerRain`, `timerLightning`, `fogEvent`, `superFogEvent`,
   `permaRain_timer`).
3. Resolve the 8 mutator UFunctions for receiver apply
   (`causeRain`, `setRainProperties`, `setRainParameters`,
   `setRainParticles`, `setWindParameters`, `spawnFog`,
   `SetFogDensity`, `intComs_triggerSnow`).
4. Register POST observers on the 5 scheduler UFunctions (host-only).
   The POST body reads the state fields off `AdaynightCycle_C` and
   sends a WeatherState packet IFF state changed (FNV1a signature
   dedup, same pattern as item_activate).
5. Register PRE observer on the 5 scheduler UFunctions with
   client-only cancel (PRE early-return like the mushroomMaster
   suppression model — see `prop_lifecycle.cpp`).
6. Also: install a one-shot observer on `K2_BeginDeferredActorSpawnFromClass`
   filtered on `class == AlightningStrike_C` to catch strikes
   (BP-internal SpawnActor call). On match (host only): read the
   `SpawnTransform.Location`, send a LightningStrike packet.

**State signature** (12-field hash):
```cpp
uint64_t hash = FNV1a({
    isRaining, isSnow,
    enable_rain, enable_fog, enable_superfog,
    enableSunlight, enableMoonlight,
    rainStrength, rainLightningChance, rainDeactivateChance,
    rainWindSpeed
    // (wind currently not on cycle directly; deferred read on host's
    //  AdirectionalWind_C if accessible — see open Q below)
});
```

**Connect-edge broadcast** (Phase 5F Inc5 pattern): host queues a
WeatherState payload regardless of dedup; per-tick retry until
reliable channel accepts.

## Client-side implementation (receiver + suppress)

**Suppression**: PRE observer on the 5 scheduler UFunctions on the
client. Body early-returns (the engine continues but the BP body
never executes). Uses existing `GT::RegisterPreObserver` substrate
with the cancel pattern already used in `prop_lifecycle.cpp` for
`mushroomMaster::Spawn`.

**Apply** (called from `event_feed.cpp` like ItemActivate):
1. Validate `peerSessionId == 0` (host-only sender).
2. Find local `AdaynightCycle_C` via `R::FindObjectByClass`.
3. Compute deltas vs current state.
4. For each delta, invoke the corresponding mutator UFunction with
   the new value (NOT direct field writes — `intComs_triggerSnow`
   has 53 listeners that need the UFunction dispatch fan-out).

**LightningStrike apply**: spawn `AlightningStrike_C` locally at the
received location via the `BeginDeferredActorSpawnFromClass` +
`FinishSpawningActor` pair (existing pattern in `remote_prop.cpp`).
The strike actor's own Timeline self-destructs ~3s later, no
manual cleanup needed.

## File size + extraction discipline

- New file `coop/weather_sync.{h,cpp}` — fresh, one feature, well
  under 800 LOC cap.
- No touch on `item_activate.cpp` (799 LOC — at cap).
- `event_feed.cpp` adds 2 new cases (WeatherState + LightningStrike).
  Currently 519 LOC; under cap.
- `harness.cpp` adds 1 line per session boot to call
  `weather_sync::Install`. Currently ~1100 LOC after the night
  refactor; well under cap.
- `protocol.h` adds 2 enum values + 2 struct definitions. Constants
  header; legitimate growth.

## Increment plan

**Inc1** (this commit): wire packet + host observer + client suppress +
receiver apply for the continuous state. Connect-edge replay. Audit.
Hands-on test sequence: host triggers rain via dev console or
`causeRain(true)` UFunction call; client should see the rain start
within 200ms; client's local scheduler never fires (verified by log).

**Inc2** (separate commit): lightning strike discrete event packet.
Observer on AlightningStrike_C spawn. Client suppresses local
lightning via the timerLightning PRE-hook (already done in Inc1);
receives strike events; spawns at the received location.

**Inc3** (separate commit): scope.md amendment marking Phase 5W
closed; audit pass.

## Open RE questions (resolve during implementation if needed)

These came up across the 3 RE docs. None block Inc1; each is a
fallback if hands-on testing surfaces a bug.

1. `AdaynightCycle_C::settingMultiplayer @ 0x0350` — what flag is this?
   May be an existing engine-side multiplayer detection field. Read on
   first run via probe log if behavior diverges.
2. Exact `SpawnActor<AlightningStrike_C>` call site is BP-internal
   inside `timerLightning` ubergraph. Inc2 uses `K2_BeginDeferredActorSpawnFromClass`
   class filter as a catch-all; if missed, fall back to
   `GUObjectArray` scan for new AlightningStrike_C instances per tick
   (cost: 1 scan per `timerLightning` fire, not per tick).
3. `setRainProperties` parameter names — RE doc lists 5 floats, exact
   FName for each must be verified by ParamFrame lookup at install.
4. `cloudsTimer` per-tick accumulator on the cycle — drives cloud
   coverage drift. Out of scope for Inc1 (cosmetic only); client's
   suppressed `ReceiveTick` body... wait, we do NOT suppress
   `ReceiveTick`. Only the 5 specific scheduler timers. Tick still
   runs and drives clouds locally. Result: cloud drift may diverge.
   Accept divergence for now (matches the project's existing
   no-perfect-determinism stance). Revisit if visually problematic.

## Cross-refs

- `[[project-weather-sync-future]]` — original user directive.
- `[[project-coop-aprop-lifecycle-RE-inc3-scope]]` — host-suppress pattern.
- `[[project-coop-mushroom-state-RE]]` — same suppress-on-client model.
- `[[project-inventory-item-activation-sync]]` — Phase 5F Inc5 connect-replay pattern (we mirror it).
- `[[feedback-deep-re-no-iteration]]` — followed: 3 parallel RE docs, no iterative guesses.
- `[[feedback-no-technical-user-questions]]` — followed: didn't ask user technical Qs.
