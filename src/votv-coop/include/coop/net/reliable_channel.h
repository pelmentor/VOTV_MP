// coop/net/reliable_channel.h -- reliable, ordered delivery over the UDP session.
//
// The pose channel is intentionally unreliable (drop freely, newest wins). Chat and
// system events must NOT be lost or reordered, so they ride this channel: the SAME
// socket and net thread, distinguished by MsgType::Reliable / ReliableAck (RULE 2:
// one transport, two reliability classes -- not a second socket).
//
// 2026-05-27 ROOT-CAUSE REWRITE (per user RULE 1: MTA reliable teleport is instant
// + one command; ours required ~10 F4 presses because Send() returned false when
// the in-flight slot was busy and dropped the packet silently). The previous
// stop-and-wait design exposed transient busy state to every caller, forcing each
// of prop_lifecycle / weather_sync / item_activate / dev hotkeys to invent its
// own per-feature retry queue. That was N-way crutch duplication.
//
// NEW DESIGN: sender-side FIFO queue at the channel layer. Send() enqueues and
// always succeeds (only fails on payload-too-large or queue-overflow). Tick()
// transmits the front of the queue at the same one-in-flight cadence as before
// (the receiver still enforces strict ordering with single-message inbox). On
// OnAck for the in-flight message, pop front + immediately kick the next one.
// Drain rate is bounded by RTT (1/RTT messages per second, ~1000/sec on LAN) --
// same wire throughput as before, but callers see a clean "always succeeds"
// interface and don't have to maintain their own retry buffers.
//
// Owned by Session; it never touches the engine -- it hands delivered payloads to
// the game thread via TryDrain.

#pragma once

#include "coop/net/protocol.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

namespace coop::net {

// Sends a datagram to the peer (or a specific address for acks). Returns true if
// handed to the OS. Supplied by Session, which owns the Transport + peer.
using SendDatagramFn = std::function<bool(const void* data, int len)>;

class ReliableChannel {
public:
    // Maximum queued sends. Hit the limit only in pathological flooding cases;
    // a 4096-deep queue at the LAN drain rate (~1000 pkt/s) is ~4 seconds of
    // backlog. Overflow drops the newest send (the in-flight head must remain
    // because the receiver expects its seq next).
    static constexpr size_t kMaxQueuedSends = 4096;

    // Game thread (typical) or any thread: queue a reliable message. Returns
    // true on success. Returns false only on:
    //   - payload-too-large (caller bug: limit is kMaxReliablePayload)
    //   - queue overflow (>kMaxQueuedSends backlog; logs the drop)
    // Stop-and-wait transient busy is NO LONGER an externally-visible failure.
    bool Send(ReliableKind kind, const void* payload, int payloadLen);

    // Net thread: retransmit the in-flight (queue front) message if its RTO
    // elapsed. If the queue is empty, no-op. If the front just became the new
    // in-flight (post-Ack), this also transmits the first attempt immediately.
    void Tick(const SendDatagramFn& sendToPeer, uint64_t token);

    // Net thread: a Reliable datagram arrived. Ack it (always, via `ack`), then
    // deliver to the inbox if it is the next expected seq (else dedup/drop).
    void OnReliable(const void* data, int len, uint64_t token, const SendDatagramFn& ack);

    // Net thread: an ack arrived -- pop the front of the queue if its seq matches.
    void OnAck(uint32_t relSeq);

    // Game thread: pop a delivered message, if any new one arrived since last call.
    struct Message { ReliableKind kind; std::vector<uint8_t> payload; };
    bool TryDrain(Message& out);

    // Clear all state (disconnect / reconnect): a reconnecting peer restarts its
    // relSeq at 0, so stale expected/seq state must be reset or it stalls.
    void Reset();

    // Diagnostics: current outbox queue depth (for logs / audit).
    size_t QueuedSendCount() const;

private:
    static constexpr auto kRto = std::chrono::milliseconds(250);

    struct OutMsg {
        ReliableKind kind;
        std::vector<uint8_t> payload;
        uint32_t relSeq;  // assigned at enqueue time; matches OnAck
    };

    mutable std::mutex outboxMutex_;
    std::deque<OutMsg> outQueue_;
    uint32_t nextAssignSeq_ = 0;        // next relSeq to assign on enqueue
    std::chrono::steady_clock::time_point nextRetransmit_;  // for queue front

    std::mutex inboxMutex_;
    bool hasInbox_ = false;
    Message inbox_;
    uint32_t expectedRelSeq_ = 0;       // next seq we expect to deliver from the peer
};

}  // namespace coop::net
