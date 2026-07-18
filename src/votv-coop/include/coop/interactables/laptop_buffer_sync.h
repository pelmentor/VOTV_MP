// coop/interactables/laptop_buffer_sync.h -- v121 (OPEN-10): the laptop
// file-buffer QUAD sync lane {floppyData, floppyBuffer, floppyBufferUIDs,
// floppyReadwrites}.
//
// Design of record: votv-laptop-v2-OPEN10-impl-DESIGN-2026-07-18.md SS2
// (11-round /qf "that holds"). Shape: change-edge client EDIT-SCRIPT batches
// (the measured no-move grammar: removeAt + append-at-END only) -> host
// content-anchored apply -> UNCONDITIONAL post-batch canonical (the canonical
// IS the ack); host organic -> canonical on-change (no host derivation
// exists). Receivers adopt canonicals (senderSlot==0 only) with
// drain-before-adopt + skip-rebuild-on-equal + the EAGER widget rebuild
// (measured: updFloppy regenerates floppyBuffer FROM bufferSlots -- a stale
// widget stomps wire values at every native/our updFloppy site).
//
// Wire: ReliableKind::LaptopQuad (BlobChunkPayload; blob head op 0=batch
// client->host, 1=canonical host->clients). Never refanned.
//
// Game thread throughout.

#pragma once

#include "coop/net/protocol.h"

#include <cstdint>

namespace coop::net { class Session; }

namespace coop::laptop_buffer_sync {

void Install(coop::net::Session* session);

// 4 Hz: int pre-filter (fdN/fbN/uidN/rw -- every native verb is int-visible,
// rw monotone proof) + floppyType predicate (slot machinery owns slot
// transitions) + derive/send (client) or canonical (host organic) + selftest.
void Tick();

// LaptopQuad chunks: host consumes batches (+ answers canonical); clients
// adopt host-authored canonicals.
void OnQuadChunk(const coop::net::BlobChunkPayload& p, uint8_t senderSlot);

// The PrimeBaselines piggyback (design invariant: every wire-driven laptop
// write path ends in laptop_sync::PrimeBaselines, which calls this).
void PrimeQuadBaseline();

// HOST: ship the joiner the canonical quad (after the op=3 + slot content).
void QueueConnectBroadcastForSlot(int peerSlot);

void OnDisconnect();

}  // namespace coop::laptop_buffer_sync
