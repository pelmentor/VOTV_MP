# GNS PR-4 audit — 15 findings to close

**Date:** 2026-05-28
**HEAD at audit:** `5330274` (GNS migration PR-1..PR-4 complete)
**Source:** /code-review xhigh + 3 broad audits (perf / security / architecture)
**10 specialized agents (7 code-review angles + 3 cross-cutting audits); findings deduped across multi-vote convergence.**

Next session: land **PR-4.1** with findings #1-#6 (focused wire-layer
patch, all in `session.{cpp,h}`, ~50 LOC). Findings #7-#11 belong with
the queued harness multi-puppet drive. Findings #12-#15 with master-
server PRs or hardening passes.

## The 15 findings, ranked most-severe first

### 1. state_ stomp at AcceptConnection (session.cpp:209) — CRITICAL

When the second client connects while peer-1 is already Connected, the
Connecting callback unconditionally writes `state_=Handshaking`.
For ~10-200ms (GNS handshake) every `connected()` call returns false:
- NetThread pose fan-out to peer-1 stops
- TryGetRemotePose returns false for peer-1
- event_feed resets g_joinSent and re-broadcasts Join
- Harness puppet-teardown path can trigger (g_wasConnected=true && !isConnected)

**Fix:**
```cpp
// session.cpp around line 209
peerConns_[slot].store(hConn);
if (state_.load() == ConnState::Disconnected) {
    state_.store(ConnState::Handshaking);
}
```

**Confidence:** 95. Confirmed by Angles A + B + E (3-vote).

### 2. state_=Handshaking instead of Disconnected on full disconnect (session.cpp:253)

When all peers disconnect, the all-peers-gone branch sets
`state_=Handshaking`, not `Disconnected`. Reconnect UI / harness logic
polling `state()==Disconnected` is permanently blocked.

**Fix:**
```cpp
// session.cpp around line 253
if (connectedPeerCount() == 0) {
    state_.store(ConnState::Disconnected);  // was Handshaking
    // ... rest of full-clear ...
}
```

**Confidence:** 85. Angle E.

### 3. Stop() ordering: linger after thread join is inoperative (session.cpp:332-346)

`thread_.join()` (line 334) runs BEFORE `CloseConnection(..., linger=true)`
(line 342). Linger requires `RunCallbacks()` to be pumped to flush queued
reliable data; after the join, nobody pumps it. Plus
`DestroyPollGroup` (line 346) runs while connections are still lingering,
stripping their PollGroup membership mid-flush. Reliable PropSpawn /
ItemActivate / TeleportClient in flight at Stop time is silently dropped.

**Fix:** reorder — close connections + destroy PollGroup + close listen
socket BEFORE joining the net thread:
```cpp
void Session::Stop() {
    if (!running_.exchange(false)) return;
    auto* sockets = SteamNetworkingSockets();
    if (sockets) {
        for (int i = 0; i < kMaxPeers; ++i) {
            const uint32_t hConn = peerConns_[i].exchange(0);
            if (hConn != 0) sockets->CloseConnection(hConn, 0, "session stop", true);
        }
        // Pump RunCallbacks a few times so linger has a chance to flush
        for (int i = 0; i < 20; ++i) {
            sockets->RunCallbacks();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const uint32_t hPoll = hPollGroup_.exchange(0);
        if (hPoll != 0) sockets->DestroyPollGroup(static_cast<HSteamNetPollGroup>(hPoll));
        const uint32_t hListen = hListen_.exchange(0);
        if (hListen != 0) sockets->CloseListenSocket(static_cast<HSteamListenSocket>(hListen));
    }
    if (thread_.joinable()) thread_.join();
    // ...
}
```

Alternative: switch to `linger=false` and document best-effort
delivery on shutdown. The pump-then-join is cleaner.

**Confidence:** 82. Altitude + Angle E (2-vote).

### 4. Per-receive heap allocation on net thread (session.cpp:564-568, session.h:65)

```cpp
ReliableMessage m;
m.kind = ...;
m.payload.assign(src_begin, src_end);  // ← system malloc on net thread, per packet
reliableInbox_.push_back(std::move(m));
```

During snapshot fan-out (hundreds of PropSpawn × kMaxPeers connected
clients = thousands of small allocations), the net thread is burning
through malloc on every received packet.

**Fix:** replace `std::vector<uint8_t>` with a fixed-size array.
`kMaxReliablePayload = 228 B` so the whole ReliableMessage is ~232 B
inline:
```cpp
// session.h
struct ReliableMessage {
    ReliableKind kind;
    uint16_t payloadLen;
    uint8_t payload[kMaxReliablePayload];
};

// session.cpp around line 564
ReliableMessage m;
m.kind = static_cast<ReliableKind>(rh.kind);
m.payloadLen = static_cast<uint16_t>(payloadLen);
std::memcpy(m.payload, src_begin, payloadLen);
reliableInbox_.push_back(std::move(m));  // still moves cheaply
```

Caller migration: ~6 callers using `out.payload.data()` / `.size()`
change to `out.payload` / `out.payloadLen`. Grep `TryGetReliable` to find
them.

**Confidence:** 88. Perf audit HIGH.

### 5. m_nConnUserData → peerSlot narrowing without prior bounds check (session.cpp:614)

```cpp
peerSlot = static_cast<int>(msgs[i]->m_nConnUserData);
```

GNS sets the field to its connection-user-data value, which we set via
`SetConnectionUserData(hConn, slot)`. But:
- If a message arrives before `SetConnectionUserData` has propagated
  (the narrow window between AcceptConnection and SetConnectionUserData),
  the field is the GNS default (0 or -1 depending on version)
- Connected-without-prior-Connecting (see #6) leaves the field at
  default

A default of 0 on the host writes `remotePoses_[0]` (the host's own
local-self slot), then the backward-compat `TryGetRemotePose()` scans
from i=0 and permanently returns the corrupt slot.

**Fix:** validate before narrowing AND reject slot 0 on host:
```cpp
// session.cpp around line 614
const int64 ud = msgs[i]->m_nConnUserData;
if (ud < (cfg_.role == Role::Host ? 1 : 0) || ud >= kMaxPeers) {
    msgs[i]->Release();
    continue;  // unknown peer slot -- drop
}
const int peerSlot = static_cast<int>(ud);
```

**Confidence:** 82. Angles A + D (2-vote).

### 6. Connected-without-prior-Connecting unguarded (session.cpp:219)

The Connected callback calls `FindPeerSlotForConn` without guarding for
slot==-1. The GNS API explicitly notes the Connecting callback may not
be delivered ("In some rare cases, you may not receive a notification
for the None->Connecting transition" in the header doc on
`SteamNetConnectionStatusChangedCallback_t`).

If Connected fires without a prior Connecting:
- `peerConns_[]` has no entry for hConn (never accepted via our path)
- FindPeerSlotForConn returns -1
- ConfigureLanesForPeer runs OK (uses hConn directly, not slot)
- But UE_LOGI logs "peer slot -1 CONNECTED"
- Future inbound messages on this connection carry m_nConnUserData
  default → combined with #5, corrupts slot 0
- On close: line 235 (`peerConns_[slot].store(0)`) is guarded by
  `slot>=0` so it's a no-op; ResetPeerRemoteState similarly skipped
  → ghost connection state leak

**Fix:** detect the skipped-Connecting case and register the slot
lazily:
```cpp
// session.cpp around line 219
if (newState == k_ESteamNetworkingConnectionState_Connected) {
    int slot = FindPeerSlotForConn(hConn);
    if (slot < 0 && cfg_.role == Role::Host) {
        // Connecting callback was skipped; register slot now.
        slot = FindFreePeerSlotForClient();
        if (slot < 0) {
            UE_LOGW("net: host full at Connected (Connecting missed) -- closing");
            sockets->CloseConnection(hConn, 0, "host full", false);
            return;
        }
        sockets->SetConnectionUserData(hConn, slot);
        const uint32_t hPoll = hPollGroup_.load();
        if (hPoll != 0) sockets->SetConnectionPollGroup(hConn, static_cast<HSteamNetPollGroup>(hPoll));
        peerConns_[slot].store(hConn);
        UE_LOGI("net: late-registered slot %d (Connecting was skipped)", slot);
    }
    if (slot < 0) {
        UE_LOGW("net: Connected on unknown connection h=0x%08x", static_cast<unsigned>(hConn));
        return;
    }
    ConfigureLanesForPeer(hConn);
    if (state_.load() != ConnState::Connected) {
        state_.store(ConnState::Connected);
    }
    UE_LOGI("net: peer slot %d CONNECTED ...", slot, ...);
    return;
}
```

**Confidence:** 80. Angle A.

---

### 7. Snapshot fan-out misses mid-drain late joiners (prop_snapshot.cpp:74)

`DrainChunk` sends each PropSpawn via the host's `SendReliable` which
fan-outs to ALL connected peers. But `Trigger()` only fires once, on the
first peer's connect-edge. Client-2 connecting at index k of the drain
only sees props [k..N-1]; props [0..k-1] are absent from its world.

**Fix:** per-peer snapshot drain. Either re-Trigger on every new
peer-connect, or track per-peerSlot drain progress and replay missing
range to the new peer specifically. The latter is RULE 1-correct but
needs `SendReliableToSlot(int peerSlot, ...)` (currently missing,
covered by finding #11 architecture NOTE-C).

**Belongs in:** harness multi-puppet drive PR.

### 8. Connect-edge handlers fire only on first connect (harness.cpp:381)

`g_wasConnected = false → true` edge fires Trigger / QueueConnectBroadcast
for prop_snapshot, weather_sync, item_activate. Subsequent clients joining
an already-Connected session: g_wasConnected is true, edge never fires.
Late-joiners get no snapshot / no weather / no flashlight broadcast.

**Fix:** per-peer edge tracking. When a new peerSlot transitions from
"not connected" → "connected", fire the connect-edge handlers FOR THAT
PEER (using SendReliableToSlot). Requires #11 (sender attribution) +
SendReliableToSlot.

**Belongs in:** harness multi-puppet drive PR.

### 9. peerSessionId hardcoded 0/1 → false self-echo in 3-peer (item_activate.cpp:164)

```cpp
p.peerSessionId = (role == Host) ? 0 : 1;  // BAKED-IN 1v1
```

In 3-peer: client-1 sends `peerSessionId=1`. Host fans it out to client-2.
Client-2 computes `selfId=1`. Equality → silent drop. Client-2's puppet
of client-1 never reflects flashlight state.

**Fix:** real per-peer session IDs from `players::Registry` slot
assignment (which already tracks 0..kMaxPeers-1).

**Belongs in:** harness multi-puppet drive PR.

### 10. ReliableMessage has no senderPeerSlot (session.h:65)

The shared `reliableInbox_` loses per-sender attribution. Two clients'
Joins overwrite each other's nickname in event_feed. Two clients'
PropRelease both apply to single g_orphan puppet. Host can't address a
TeleportClient or RestoreVitals reply back to the specific client that
triggered it.

**Fix:** add a senderPeerSlot field; populate in HandleMessage:
```cpp
struct ReliableMessage {
    ReliableKind kind;
    int senderPeerSlot;       // NEW: -1 = unknown, 0..kMaxPeers-1
    uint16_t payloadLen;
    uint8_t payload[kMaxReliablePayload];
};
// In HandleMessage's Reliable branch:
m.senderPeerSlot = peerSlot;
```

Unlocks #7, #8, #9 fixes (per-peer routing on reliable channel).

**Confidence:** Architecture NOTE-B + Angle B + Altitude (3-vote).

**Belongs in:** can land standalone (small change), unlocks the rest.

### 11. reliableInbox stale entries on per-peer disconnect (session.cpp:240)

When one of N peers drops, that slot's remote pose state is cleared but
reliable messages already in the shared inbox from that peer remain.
Game thread can drain a PropSpawn from a ghost peer (the prop appears,
no future PropDestroy can arrive).

**Fix:** with #10's senderPeerSlot field, the per-peer disconnect path
can filter the inbox:
```cpp
// In HandleConnStatusChanged disconnect branch, after slot zero-out:
{ std::lock_guard<std::mutex> lk(reliableInboxMutex_);
  std::erase_if(reliableInbox_, [slot](const auto& m){ return m.senderPeerSlot == slot; });
}
```

**Confidence:** Altitude + Architecture (2-vote).

**Belongs in:** with #10 — same patch.

---

### 12. reliableInbox unbounded growth (session.cpp:569)

`std::deque<ReliableMessage>` has no size cap. A handshaked-but-flooding
peer can grow it without limit on the host's net thread.

**Fix:** cap at 1024 entries:
```cpp
// In HandleMessage's Reliable branch, before push_back:
if (reliableInbox_.size() >= 1024) {
    UE_LOGW("net: reliableInbox full (1024) -- dropping kind=%u from peer %d",
            static_cast<unsigned>(rh.kind), peerSlot);
    return;
}
```

**Confidence:** Security NOTE-1.

**Belongs in:** PR-4.1 (defensive, one-line).

### 13. GNS IP_AllowWithoutAuth=2 default (session.cpp:267 / EnsureGnsInit)

GNS open-source build default permits unauthenticated peers. Connection
encryption (ECDH+AES-GCM) is still active, but anyone reachable to the
port can connect into a slot with no shared-secret verification. NOT a
regression from pre-PR-2 (sessionToken didn't authorize the initial
Hello either). Acceptable for LAN-trusted-friends. Needs application-
level shared-secret challenge before Internet scope.

**Fix:** Post-Connected, host sends a random nonce encrypted with a
user-configured PIN; client must respond with HMAC(secret, nonce) before
session goes "fully Connected" for game purposes. Reject via
CloseConnection on mismatch.

**Belongs in:** future security PR after master-server lands (or
sooner if Internet scope is taken).

### 14. 0-arg TryGetRemotePose shadows slots ≥2 (session.cpp:381)

Backward-compat overload scans from i=0, returns first hasRemote_ hit.
Host with 2 clients: only slot 1 is ever returned. 4-vote audit
consensus (A + B + C + D).

**Fix:** harness migration to per-slot drive. Already-known design
limitation documented in session.h:91-92. No code change in
session.cpp — fix is in the harness.

**Belongs in:** harness multi-puppet drive PR. When that lands, delete
the 0-arg overload entirely (RULE 2).

### 15. payloadLen `<0` dead check (session.cpp:562)

```cpp
const int payloadLen = static_cast<int>(rh.payloadLen);  // uint16_t cast
if (payloadLen < 0 || payloadLen > kMaxReliablePayload) return;
```

uint16_t can't be negative; the `<0` arm is unreachable. Only meaningful
guard is the upper bound.

**Fix:** drop the dead `<0` check:
```cpp
if (payloadLen > kMaxReliablePayload) return;
```

**Confidence:** Angle D, 82.

**Belongs in:** PR-4.1 (one-line trim).

---

## Cleanup tier (not in 15, deferred to future modular-refactor pass)

- **10 per-peer std::array fields → struct PerPeerRemoteState{}** — cleanup audit
- **0-arg overloads duplicate per-slot logic** — fold via delegation
- **kSendStaging dead constant** — leftover from PR-2 Winsock days
- **NetThread 5ms sleep wastes ~67% wakeups at 60 Hz** — sleep until next event
- **Trailing Session::Start log line shows misleading "127.0.0.1:port" on host** — make topology-conditional at PR-6
- **g_session name collision across 5+ TUs** — convention rename on next refactor pass
- **ConfigureLanesForPeer before FindPeerSlotForConn** — just a log slot mismatch
- **Double-lock on remoteMutex_ in last-peer-disconnect** — merge critical sections

## PR-4.1 patch scope (next session, target)

Findings #1, #2, #3, #4, #5, #6, #12, #15 — all in session.{cpp,h}, plus
finding #4's ~6 caller-site type changes (`out.payload.data()` →
`out.payload`). Estimated ~80 LOC net change. Single focused PR, easy
review, LAN smoke + maybe a connection-reset test.

Findings #10 + #11 can ride PR-4.1 if patch size permits (adds
senderPeerSlot to ReliableMessage, populates it in HandleMessage, drains
on per-peer disconnect). Unlocks #7/#8/#9 fixes which then ship with the
harness multi-puppet drive PR.

Findings #7, #8, #9, #14 ship with the harness multi-puppet drive PR
(call this PR-5a or some such). That's the larger work item — touches
harness.cpp, item_activate.cpp, prop_snapshot.cpp, weather_sync.cpp,
and adds `Session::SendReliableToSlot`.

Finding #13 (auth) is its own dedicated PR after master-server design
lands (PR-6+).
