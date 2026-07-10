// coop/dev/rng_roll_census.h -- the T1 live client-roll census probe (v9, pre-registered).
//
// READ-ONLY diagnostic (RULE-2-exempt). Implements the instrument pre-registered in
// docs/COOP_RNG_AUTHORITY.md ("T1 PRE-REGISTRATION"): measure which of the game's autonomous
// RNG rollers are actually LIVE on each role, so the structural-vs-allowlist fork is decided
// from data, not the static upper bound. Ini-gated `[dev] rng_roll_census=1`; zero cost when
// off (interceptors early-out on one memoized bool).
//
// Channels (see the tracker for full semantics):
//  (a) BeginDeferred pass-through census -- npc_sync's interceptor calls NotePassThrough for
//      every spawn it does NOT handle (host non-allowlisted + client silent pass-through).
//      Name-agnostic discovery: every class logs (first-sight + per-class counters).
//  (b) DRIVER census -- Func-patch POST observers on the timer/delay/tick-interval engine
//      natives (K2_SetTimerDelegate, K2_SetTimer, Delay, RetriggerableDelay, SetActorTickInterval;
//      DelayFrames resolved best-effort). First-sight-per-owner-class + counters; the ARM is the
//      liveness signal (fires every re-arm cycle regardless of the spawn-gate outcome).
//  (c) periodic ticker census -- live ticker_base_C descendants: existence + tick-enabled.
//  (d) global QuitGame log -- the calculateAreaError consequence guard (log-only, never blocks).
//  (e) periodic world BP-actor histogram -- per-actor set diff; new actors with no Registry
//      binding + not hand-axis + outside the join episode = the seam-agnostic residue.
//
// Tagging per record: role, JOIN-EPISODE (world_load_episode), COOP-ORIGIN
// (ue_wrap::reflection::InCoopDispatch -- excludes our own machinery's arms). Analysis joins
// against the live suppressed sets OFFLINE (nothing hand-copied here).
//
// Threading: Note* callbacks fire on the ProcessEvent-dispatching thread (parallel-anim workers
// possible) -- callback state is mutex-guarded counters + GNames reads ONLY (no engine dispatch
// off-thread). Tick() (censuses c/e + counter dump) is game-thread only.

#pragma once

#include <cstdint>

namespace coop::net { class Session; }

namespace coop::dev::rng_roll_census {

// Memoized `[dev] rng_roll_census` ini flag.
bool IsEnabled();

// Resolve natives + register interceptors. Called from the subsystems Install fanout (pre-world,
// per-pump-tick until latched; self-throttled). No-op when the ini flag is off.
void Install(coop::net::Session* session);

// Channel (a) entry: npc_sync's BeginDeferred interceptor reports a spawn it passes through
// untouched (actorClass = the UClass* about to spawn). Any-thread safe.
void NotePassThrough(void* actorClass, bool isHostRole);

// Periodic censuses (c)+(e) + the (b)/(d) counter dump. Game thread; call every pump tick
// (self-throttled to the census cadence; single bool read when disabled/idle).
void Tick();

}  // namespace coop::dev::rng_roll_census
