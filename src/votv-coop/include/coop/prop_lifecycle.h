// coop/prop_lifecycle.h -- Aprop_C spawn/destroy/extract wire observers.
//
// The host-authoritative prop sync layer:
//   - Aprop_C::Init POST (subclass-aware GUObjectArray scan): host
//     broadcasts PropSpawn on every BP-spawned prop derivative; client
//     suppresses intermediate-variant local spawns (mushroom7_C) and
//     waits for the mature variant via the wire.
//   - AActor::K2_DestroyActor PRE: bidirectional broadcast (host AND
//     client destroys replicate -- food consumption, container break,
//     etc.). Echo-suppressed via the remote_prop incoming-destroy set.
//   - propInventory_C::takeObj PRE/POST: brackets container extracts so
//     the nested Init POST defers its broadcast (Key is NewGuid pre-
//     loadData) and takeObj POST broadcasts with the restored saved UUID.
//
// Also owns the bounded retry queues used by all three send sites + the
// snapshot drainer. Drain* are called by NetPumpTick each frame.
//
// Principle 7: this is gameplay/network logic; talks to ue_wrap through
// reflection + engine + game_thread.

#pragma once

#include "coop/net/protocol.h"

#include <cstddef>
#include <string>

namespace coop::net { class Session; }

namespace coop::prop_lifecycle {

// Cache the session pointer. Call once at boot, BEFORE any code path
// that could fire DrainPending* or the observers (e.g. before NetPumpTick
// starts ticking). The Install / InstallInventory functions ALSO accept
// a session pointer for backward compatibility, but this dedicated
// setter exists so the session is bound from the moment Drain* may run
// (NetPumpTick can call Drain* on a frame BEFORE g_netLocal is resolved
// + Install* is reached, leaving the queue undrained until Install runs).
void SetSession(coop::net::Session* session);

// Install Aprop_C::Init POST + AActor::K2_DestroyActor PRE observers.
// First call does a one-shot GUObjectArray scan to find every Init
// UFunction in the prop_C lineage. Idempotent; retried each NetPumpTick
// until the prop_C base class is loaded.
void Install(coop::net::Session* session);

// Install propInventory_C::takeObj PRE/POST observers. Separate because
// the inventory class may load later than the prop class. Idempotent;
// retried each NetPumpTick until propInventory_C appears.
void InstallInventory(coop::net::Session* session);

// Bounded retry queues. PropSpawn capacity 4096 (sized for a snapshot
// bootstrap ~2000 props x 160B payload = ~320 KB); PropDestroy capacity
// 256. Drops oldest on overflow with a warning. Game-thread access only.
void EnqueuePropSpawnForRetry(const coop::net::PropSpawnPayload& payload);
void EnqueuePropDestroyForRetry(const coop::net::WireKey& key);

// Drainers called by NetPumpTick each frame. Send until the reliable
// channel reports busy; preserve enqueue order. No-op when empty.
void DrainPendingPropSpawns();
void DrainPendingPropDestroys();

// Host-authoritative intermediate-variant suppression predicate. Three
// call sites (Init POST host-broadcast, snapshot enumerate, client local
// spawn destroy) use this for symmetry. Public so prop_snapshot can
// share it.
bool IsWireSuppressedPropClass(const std::wstring& cls);

// Per-session state cleanup on disconnect. Returns counters for the
// log line. Clears: retry queues, takeObj-in-flight flag, processed-Init
// dedupe set.
struct DisconnectStats {
    size_t droppedSpawns = 0;
    size_t droppedDestroys = 0;
    size_t initProcessedDropped = 0;
};
DisconnectStats OnDisconnect();

}  // namespace coop::prop_lifecycle
