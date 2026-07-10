// coop/props/world_load_episode.cpp -- see coop/props/world_load_episode.h.
//
// A single game-thread bool. The join-window driver (join_membership_sweep) owns the CLEAR edge (it already
// drives the downstream join modules at load-tail quiescence); this module owns only the ARM (the causal
// world-load trigger, from the harness client-join path) + the query the destroy seam reads.

#include "coop/props/world_load_episode.h"

#include "ue_wrap/log.h"

#include <chrono>

namespace coop::world_load_episode {
namespace {
// Game-thread only (armed on the join bringup thread's Post to the game thread; cleared on the client
// reconcile tick; read on the destroy seam -- all game thread). No atomic needed.
bool g_inEpisode = false;

// The watchdog ceiling sits ABOVE join_membership_sweep's 120 s absolute sweep ceiling: when the sweep
// path is healthy it always closes the episode first (quiescence or its own deadlines), so the watchdog
// fires ONLY when that path never armed (the SnapshotBegin-lost flake) or is pathologically stuck.
constexpr long long kEpisodeWatchdogMs = 150000;
std::chrono::steady_clock::time_point g_armedAt{};  // valid while g_inEpisode
}  // namespace

void Arm() {
    if (g_inEpisode) return;  // idempotent -- one arm per world-load
    g_inEpisode = true;
    g_armedAt = std::chrono::steady_clock::now();
    UE_LOGI("world_load_episode: ARMED -- client world-load starting; KEYED-prop destroy broadcasts "
            "suppressed until load-tail quiescence (host-wipe root fix)");
}

void TickWatchdog() {
    if (!g_inEpisode) return;  // steady-state: one bool read
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - g_armedAt).count();
    if (elapsed < kEpisodeWatchdogMs) return;
    g_inEpisode = false;
    UE_LOGW("world_load_episode: WATCHDOG force-close after %lldms -- the quiescence edge never fired "
            "(SnapshotBegin-lost flake / sweep never armed?). KEYED-prop destroy broadcasts resume; "
            "investigate the join bracket in this log.",
            static_cast<long long>(elapsed));
}

void NotifyQuiesced() {
    if (!g_inEpisode) return;
    g_inEpisode = false;
    UE_LOGI("world_load_episode: CLOSED at load-tail quiescence -- KEYED-prop destroy broadcasts resume");
}

void Reset() { g_inEpisode = false; }

bool InEpisode() { return g_inEpisode; }

}  // namespace coop::world_load_episode
