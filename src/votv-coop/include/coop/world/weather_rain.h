// coop/weather_rain.h -- Phase 5W rain+snow cycle-side sub-lane (2026-07-19).
//
// Extracted from weather_sync.cpp (1154 LOC > the 800 soft cap) in the shipped
// family shape (weather_fog / weather_lightning / weather_redsky): the module
// owns the rain+snow engine substrate on AdaynightCycle_C -- its OWN 5 mutator
// UFunction resolves (causeRain / setRainProperties / setWindParameters /
// intComs_triggerSnow / setRainParticles), its own latch, the causeRain
// echo-suppress flag + PRE interceptor (registered in-module), and the
// rain-side read/apply/debug bodies. weather_sync (the orchestrator) keeps
// the scheduler observers/interceptors, the WeatherState broadcast + dedup
// sig, the connect seed, TickConnect, and the composition ReadCycleState /
// ApplyFromHost that call into this module the same way they call
// weather_fog / directionalwind.
//
// P7: this module owns the rain/snow engine substrate (offsets + UFunction
// thunks); weather_sync (gameplay/network) drives it. Mirrors weather_fog.

#pragma once

namespace coop::net { class Session; struct WeatherStatePayload; }

namespace coop::weather_rain {

// Session pointer for the Debug* host-role checks (the weather_redsky /
// weather_lightning SetSession shape). Set from weather_sync::Install every
// re-entry; atomic inside.
void SetSession(coop::net::Session* session);

// Idempotent install. Resolves the 5 mutator UFunctions off daynightCycle_C
// (own once-latch; UFunction ptrs are UClass-stable across cycle recreation)
// and registers the causeRain echo-suppress PRE interceptor (both roles;
// pass-through unless the echo flag is set around the receiver's apply).
// Returns true once all 5 mutators are resolved. Safe every NetPumpTick.
bool Install();

// HOST read half: stamp the cycle's config/rain/snow FLAG bits (incl. the
// enable_fog/enable_superfog CONFIG gates -- their actor-driven APPLY lives
// in weather_fog) + the 5 rain scalars into the wire payload. The fog ACTOR
// bits (flags2) and the wind fields are stamped by weather_fog /
// ue_wrap::directionalwind from the orchestrator's composition. Resets
// out.flags; call FIRST in the composition. Game thread.
void ReadState(void* cycle, coop::net::WeatherStatePayload& out);

// Receiver apply half: the cycle-side delta apply (enable-bit direct writes,
// setRainProperties, rain ease-target pin, echo-bracketed causeRain,
// setRainParticles, setWindParameters, intComs_triggerSnow). `cur` is the
// pre-apply composite state the ORCHESTRATOR read (the delta-compare
// baseline); `outcome` reports what transitioned so the orchestrator emits
// the fused "weather: applied" log line unchanged. Game thread.
struct ApplyOutcome {
    bool rainTx = false;
    bool snowTx = false;
    bool scalarsChanged = false;
};
void ApplyFromHost(void* cycle, const coop::net::WeatherStatePayload& payload,
                   const coop::net::WeatherStatePayload& cur, ApplyOutcome& outcome);

// Phase 5W test entrypoint (host only). Proper-invocation rain force per the
// 2026-05-27 RE pass: enable_rain write + setRainProperties + causeRain +
// setWindParameters. The host's POST observers (registered by weather_sync
// on its own resolves of the same UFunctions) catch the calls + broadcast.
// Returns false if the cycle is not live or the mutators aren't resolved.
// Game thread only.
bool DebugForceRain(bool isRaining, float rainStrength);

// Phase 5W hands-on test entrypoint (host only). intComs_triggerSnow(isSnow)
// -- the visually-unambiguous signal (53 BP listeners). Game thread only.
bool DebugForceSnow(bool isSnow);

// Diagnostic: read the local cycle's isRaining bool. `outFound` distinguishes
// "false (cycle null)" from "false (not raining)". Game thread only.
bool ReadLocalIsRaining(bool* outFound);

// The module's validated cycle pointer (may be null while loading). For
// diagnostic consumers (coop/dev/weather_probe) that gate their own cadence;
// the console_desk::Instance() precedent. Game thread only.
void* Cycle();

// Disconnect hook: clears the session ptr, the echo flag (defensive), and
// the module's own cycle cache (may dangle across level transitions).
void OnDisconnect();

}  // namespace coop::weather_rain
