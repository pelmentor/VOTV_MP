// coop/interactables/floppybox_sync.h -- v121 (OPEN-10): the disc crate
// (Aprop_floppyBox_C) LIFO stack sync {floppyTypes[], floppyData[]}.
//
// Design of record: votv-laptop-v2-OPEN10-impl-DESIGN-2026-07-18.md SS4 -- the
// v119 RackState shape verbatim: client tail value-ops (push{type,dataString}/
// pop{contentHash}) -> host tail-anchored apply -> canonical arrays after
// EVERY op + on organic host change; pop-miss -> DENY -> the author reaps its
// just-spawned in-hand disc if still alive, skips if consumed (content
// duplication is native-legal; no prop dupe survives an insert). The disc
// ACTORS cross on existing lanes (destroy seam / birth channels + hand-item);
// only the box arrays are new wire state.
//
// Wire: ReliableKind::FloppyBoxState (BlobChunkPayload; blob head
// [u8 op][u32 eid]; op 0=push 1=pop 2=deny 3=canonical). Never refanned;
// clients accept canonicals/denies from the host only.
//
// Game thread throughout.

#pragma once

#include "coop/net/protocol.h"

#include <cstdint>

namespace coop::net { class Session; }

namespace coop::floppybox_sync {

void Install(coop::net::Session* session);

// 1 Hz element sweep (pointer class gate; first-sight silent prime): client
// derives tail ops vs the shadow -> host; host organic change -> canonical.
void Tick();

// FloppyBoxState chunks.
void OnBoxChunk(const coop::net::BlobChunkPayload& p, uint8_t senderSlot);

// HOST: ship the joiner one canonical per live box.
void QueueConnectBroadcastForSlot(int peerSlot);

void OnDisconnect();

}  // namespace coop::floppybox_sync
