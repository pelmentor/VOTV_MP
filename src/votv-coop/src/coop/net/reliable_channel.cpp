#include "coop/net/reliable_channel.h"

#include "ue_wrap/log.h"

#include <cstring>

namespace coop::net {

// Audit Fix H2 (2026-05-27): the stack buffer in Tick() is sized at
// kMaxPacketBytes; Send() rejects payloads > kMaxReliablePayload. Make the
// invariant load-bearing at compile time so a future header-size change
// can't silently overflow.
static_assert(
    sizeof(PacketHeader) + sizeof(ReliableHeader) + kMaxReliablePayload
        == kMaxPacketBytes,
    "Tick() stack buffer must cover exactly one max-size reliable datagram");



bool ReliableChannel::Send(ReliableKind kind, const void* payload, int payloadLen) {
    if (payloadLen < 0 || payloadLen > kMaxReliablePayload) {
        UE_LOGW("net: reliable Send rejected (len=%d > %d)", payloadLen, kMaxReliablePayload);
        return false;
    }
    std::lock_guard<std::mutex> lk(outboxMutex_);
    if (outQueue_.size() >= kMaxQueuedSends) {
        UE_LOGW("net: reliable outbox queue full (%zu) -- dropping new send (kind=%u)",
                outQueue_.size(), static_cast<unsigned>(kind));
        return false;
    }
    const bool wasEmpty = outQueue_.empty();
    OutMsg m;
    m.kind = kind;
    m.payload.assign(static_cast<const uint8_t*>(payload),
                     static_cast<const uint8_t*>(payload) + payloadLen);
    m.relSeq = nextAssignSeq_++;
    outQueue_.push_back(std::move(m));
    if (wasEmpty) {
        // New head -- kick transmission on the next Tick (no RTO wait).
        nextRetransmit_ = std::chrono::steady_clock::now();
    }
    return true;
}

void ReliableChannel::Tick(const SendDatagramFn& sendToPeer, uint64_t token) {
    // Build the datagram under the lock, then send OUTSIDE it: never hold a mutex
    // across socket I/O (so the game thread's Send can't block behind a stalled send).
    uint8_t buf[kMaxPacketBytes];
    int total = 0;
    {
        std::lock_guard<std::mutex> lk(outboxMutex_);
        if (outQueue_.empty()) return;
        const auto now = std::chrono::steady_clock::now();
        if (now < nextRetransmit_) return;
        const OutMsg& m = outQueue_.front();
        auto* hdr = reinterpret_cast<PacketHeader*>(buf);
        WriteHeader(*hdr, MsgType::Reliable, m.relSeq, token);
        auto* rh = reinterpret_cast<ReliableHeader*>(buf + sizeof(PacketHeader));
        std::memset(rh, 0, sizeof(*rh));
        rh->kind = static_cast<uint8_t>(m.kind);
        rh->payloadLen = static_cast<uint16_t>(m.payload.size());
        std::memcpy(buf + sizeof(PacketHeader) + sizeof(ReliableHeader),
                    m.payload.data(), m.payload.size());
        total = static_cast<int>(sizeof(PacketHeader) + sizeof(ReliableHeader) + m.payload.size());
        nextRetransmit_ = now + kRto;
    }
    sendToPeer(buf, total);
}

void ReliableChannel::OnReliable(const void* data, int len, uint64_t token, const SendDatagramFn& ack) {
    if (len < static_cast<int>(sizeof(PacketHeader) + sizeof(ReliableHeader))) return;
    PacketHeader hdr;
    std::memcpy(&hdr, data, sizeof(hdr));
    ReliableHeader rh;
    std::memcpy(&rh, static_cast<const uint8_t*>(data) + sizeof(PacketHeader), sizeof(rh));
    const int payloadLen = static_cast<int>(rh.payloadLen);
    if (payloadLen > kMaxReliablePayload) return;  // uint16 -> always >= 0
    if (len < static_cast<int>(sizeof(PacketHeader) + sizeof(ReliableHeader)) + payloadLen) return;

    // Decide accept/ack under the lock; SEND the ack outside it (never socket I/O
    // while holding a mutex). Receiver is single-message-inbox stop-and-wait:
    //  - seq < expected  : already delivered -> ack again (recovers a lost ack), no re-deliver.
    //  - seq == expected & inbox free : deliver + advance + ack.
    //  - seq == expected & inbox FULL : drop without ack -> sender keeps retransmitting
    //    until the game thread drains. Natural back-pressure.
    //  - seq > expected  : gap (shouldn't happen given sender is FIFO + one-in-flight) -> drop, no ack.
    bool doAck = false;
    {
        std::lock_guard<std::mutex> lk(inboxMutex_);
        const int32_t d = static_cast<int32_t>(hdr.seq - expectedRelSeq_);
        if (d < 0) {
            doAck = true;  // duplicate of an already-delivered message
        } else if (d == 0 && !hasInbox_) {
            inbox_.kind = static_cast<ReliableKind>(rh.kind);
            inbox_.payload.assign(
                static_cast<const uint8_t*>(data) + sizeof(PacketHeader) + sizeof(ReliableHeader),
                static_cast<const uint8_t*>(data) + sizeof(PacketHeader) + sizeof(ReliableHeader) + payloadLen);
            hasInbox_ = true;
            ++expectedRelSeq_;
            doAck = true;
        }
        // d == 0 && hasInbox_ (inbox full) or d > 0 (gap): no ack, no deliver.
    }
    if (doAck) {
        PacketHeader a;
        WriteHeader(a, MsgType::ReliableAck, hdr.seq, token);
        ack(&a, sizeof(a));
    }
}

void ReliableChannel::OnAck(uint32_t relSeq) {
    std::lock_guard<std::mutex> lk(outboxMutex_);
    if (outQueue_.empty()) return;
    if (outQueue_.front().relSeq != relSeq) return;  // stray / duplicate ack
    outQueue_.pop_front();
    if (!outQueue_.empty()) {
        // Immediately kick the next message -- no RTO wait between consecutive
        // successful ACKs (drain at 1/RTT, ~1000 msg/s on LAN). This is THE
        // throughput optimization that makes a 1264-entity snapshot replay
        // drain in ~1.3s instead of ~316s if we had waited 250ms per packet.
        nextRetransmit_ = std::chrono::steady_clock::now();
    }
}

bool ReliableChannel::TryDrain(Message& out) {
    std::lock_guard<std::mutex> lk(inboxMutex_);
    if (!hasInbox_) return false;
    out = inbox_;
    hasInbox_ = false;
    return true;
}

void ReliableChannel::Reset() {
    {
        std::lock_guard<std::mutex> lk(outboxMutex_);
        outQueue_.clear();
        nextAssignSeq_ = 0;
    }
    {
        std::lock_guard<std::mutex> lk(inboxMutex_);
        hasInbox_ = false;
        expectedRelSeq_ = 0;
        inbox_.payload.clear();
    }
}

size_t ReliableChannel::QueuedSendCount() const {
    std::lock_guard<std::mutex> lk(outboxMutex_);
    return outQueue_.size();
}

}  // namespace coop::net
