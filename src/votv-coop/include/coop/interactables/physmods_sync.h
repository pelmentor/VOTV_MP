// coop/physmods_sync.h -- L8: the desk PHYSICAL-MODULES array sync
// (PhysModsState=108, v118). Design: 8-round /qf 2026-07-18 (converged "that
// holds" R8; design doc votv-physmods-L8-impl-DESIGN-2026-07-18.md).
//
// SHAPE -- value-ops + host-canonical array (the array is a SET: the native
// dup-check makes every module byte unique, so ops need no slot):
//   DETECT: 1 Hz 12-byte poll on every peer (outcome-based -- plug, unplug,
//     deny and explosion branches all converge through the array; prime on
//     the first connected poll; drain-before-adopt closes the eaten-edge race).
//   OPS (peer->host, host-terminal): plug{byte} / unplug{byte}, derived from
//     the local diff. The HOST applies to ITS live array (plug: find-free;
//     unplug: zero-where-found), runs updPhysMods, then broadcasts the FULL
//     canonical 12-byte array; every peer (presser included) adopts wholesale
//     + primes + reflected updPhysMods under the audio-seam wire guard
//     (measured a pure function of the array -- idempotent).
//   DENY (host -> the no-op author): a dup PLUG gets a REFUND (the host
//     spawns the module class at the desk -- the item is never lost); a no-op
//     UNPLUG makes the author destroy its local hand ghost (or sweep its
//     untracked module actors of that byte -- the drop-before-deny case).
//     The host also reaps a denied byte's fresh kind-104 birth inside a TTL.
//   The module-prop halves need (almost) no lane code: the plug CONSUME
//   rides the bidirectional v106 destroy seam; the HOST's unplug birth is
//   expressed same-tick by the spawn watcher; the CLIENT's unplug birth
//   crosses at its drop via the widened client-birth class whitelist
//   (prop_drop_intent + IsModuleClass).
// JOIN: save transfer seeds the array (setData pokes consumers natively);
// the host also ships the canonical array in connect-replay (parks pre-desk).

#pragma once

#include "coop/net/protocol.h"

#include <cstdint>

namespace coop::net { class Session; }

namespace coop::physmods_sync {

void Install(coop::net::Session* session);

// Per-net-pump: resolve backoff, the 1 Hz diff poll (ops out), pending
// canonical apply.
void Tick();

// PhysModsState from the wire (router: event_dispatch_signal.cpp).
void OnPhysMods(const coop::net::PhysModsStatePayload& p, uint8_t senderSlot);

// HOST: ship the canonical array to a joiner (connect replay).
void QueueConnectBroadcastForSlot(int slot);

// HOST consult from the kind-104 birth author: reap a module birth that
// matches a fresh unplug-deny for this sender (the r8 ghost). True = refuse.
bool HostShouldReapModuleBirth(uint8_t senderSlot, void* moduleClass);

// Full state reset. Wired into the subsystems teardown fanout.
void OnDisconnect();

}  // namespace coop::physmods_sync
