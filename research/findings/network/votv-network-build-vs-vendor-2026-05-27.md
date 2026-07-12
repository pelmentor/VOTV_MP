# VOTV coop networking — build-vs-vendor decision (2026-05-27)

**Scope.** Foundational architectural question: do we keep extending the
hand-rolled UDP/ARQ wire layer under `src/votv-coop/.../coop/net/`, or
vendor an existing networking library (RakNet / ENet / GameNetworkingSockets
/ yojimbo / ...)? The answer drives the next 1–2 weeks of work and the
shape of the next ~10 wire-features (priority lanes, sliding window,
fragmentation, etc.).

**TL;DR recommendation.** Keep hand-rolling, but in ONE concentrated
sliding-window-channel rewrite — not a stream of band-aid increments.
Decisive factor: RULE №3 + the size of what we actually need. None of the
viable candidates land cleanly inside the standalone-DLL constraint at a
cost cheaper than ~2 weeks of focused work on our own substrate, and the
feature set we genuinely need is small (3 lanes × 3 reliability modes,
no encryption, no NAT punch, no master-server matchmaking). The
methodology's "don't reinvent the wheel" (WP13) is satisfied by **MinHook
+ ImGui + DbgHelp** — the high-value wheels — not by a network stack
where every candidate is a worse fit than our 1.8k-LOC current code.

The danger we are NOT going to fall into is RULE №1: drip-feeding
priority/window/fragmentation onto today's stop-and-wait one-in-flight
channel feature-by-feature for 6 weeks. Section §6 is the proper
single-rewrite plan.

---

## §1. Current state — what we have today

Wire-layer LOC (header + impl), 2026-05-27 HEAD:

| File | LOC |
| --- | --- |
| `coop/net/transport.h` | 58 |
| `coop/net/transport.cpp` | 121 |
| `coop/net/protocol.h` | 719 |
| `coop/net/session.h` | 214 |
| `coop/net/session.cpp` | 481 |
| `coop/net/reliable_channel.h` | 102 |
| `coop/net/reliable_channel.cpp` | 141 |
| **Total** | **1836** |

Of that, `protocol.h` is 719 LOC of payload structs + comments — wire
format, not engine. The actual transport/session/reliable engine is
~1100 LOC. That's the thing we'd replace.

### Architectural shape

```
game thread                              net thread
-----------                              ----------
SetLocalPose ─┐                  ┌── Transport::SendTo  (sock_, sendto)
SendReliable ─┼─► [slot/queue] ─►│
              │                  │
              │                  ├── Transport::RecvFrom (recvfrom)
TryGetRemote ◄┤                  │
TryDrain     ◄┴── [slot/inbox] ◄─┘    HandleDatagram → dispatch by MsgType
```

- **Transport** (`transport.cpp`, 121 LOC): one Winsock UDP socket,
  refcounted `WSAStartup`, non-blocking, `Endpoint{addrBE,portBE}`. Zero
  protocol awareness. Trivial.
- **Protocol** (`protocol.h`, 719 LOC): packed POD packet structs +
  per-payload `static_assert`s on size + reliable-payload-fits-in-MTU
  guards. 9 protocol-version bumps to date (v3→v9), no back-compat
  layer (RULE №2). Wire format is fixed, packed, little-endian.
- **Session** (`session.cpp`, 481 LOC): the application layer. Owns
  the net thread, the handshake (Hello/Pong/Bye), the session-token
  trust boundary, peer-locking on first contact, peer-timeout, RTT
  via Ping/Pong, two unreliable streams (pose + held-prop-pose), and
  a single reliable channel.
- **ReliableChannel** (`reliable_channel.cpp`, 141 LOC):
  - **Sender**: FIFO queue + one-in-flight stop-and-wait ARQ + 250 ms
    RTO + drains at 1/RTT (~1000 msg/s on LAN). Rewritten 2026-05-27
    to push the queue into the channel itself (was per-feature retry
    duplication; that was the N-way crutch the rewrite fixed).
  - **Receiver**: single-message inbox, dedup by RFC1982 signed
    compare on `relSeq`, ack-out-of-band on duplicates (handles lost
    acks), back-pressure via "no ack while inbox full".

### Feature coverage matrix (today)

| Need | Today | Notes |
| --- | --- | --- |
| Reliable ordered | yes (1 lane) | single sender FIFO, single receiver inbox |
| Reliable unordered | no | (would require receiver-side seq tracking change) |
| Unreliable | yes (Pose, PropPose) | per-stream seq with stale-drop |
| Unreliable sequenced | yes (effectively) | "stale-drop strictly newer" is the unreliable-sequenced semantics |
| Priority lanes | no | one reliable lane shared by all 11 ReliableKind values |
| Sliding-window ARQ | no | stop-and-wait, throughput = 1/RTT msgs/sec |
| Fragmentation | no (256-byte hard cap, `kMaxPacketBytes`) | payloads >232 B rejected |
| Connection mgmt | yes | Hello/Bye/peer-timeout/session-token |
| Bandwidth shaping / congestion control | no | 5 ms sleep loop is the only pacing |
| Encryption | no (LAN; not needed) | future WAN would want this |
| Multi-peer | no | 1:1 host/client; `kMaxPeers=4` planned, registry shape ready |

### Call-site surface (the "API blast radius")

13 source files call `Session` methods. The shape they depend on is small
and clean (the rewrite to internal queue already shielded callers from
ARQ details):

```
SetLocalPose / TryGetRemotePose
SetLocalPropPose / TryGetRemotePropPose
SendReliable(kind, bytes, len)                   ← THE generic verb
SendPropRelease / SendPropSpawn / SendPropDestroy
SendEntitySpawn / SendEntityDestroy
TryGetReliable(out)
SendTo (none) — callers never touch Transport directly
```

Every reliable feature funnels through `Session::SendReliable` →
`ReliableChannel::Send`. Replacing the underlying transport without
touching the 13 callers is feasible IF we preserve those 8 method
signatures.

---

## §2. Candidate evaluation

For each: license, RULE-№3 vendoring fit, feature coverage, integration
cost, maintenance, and overall verdict.

### 2.1 RakNet (the MTA precedent)

- **License**: BSD-3 since 2014 (Oculus open-sourced the v4 source).
  Permissive. OK.
- **RULE №3**: **WHITE-FLAG PROBLEM.** mtasa-blue does NOT vendor RakNet
  source. Grepping the entire `reference/mtasa-blue/` tree for `RakPeer`
  or `RakNet` returns three files (a credits string, a test mock, and
  `packetenums.h` which redefines RakNet's enum values manually). The
  real RakNet lives in `net.dll` — a separate proprietary-build closed
  binary that the MTA loader downloads at runtime. That dll wraps
  RakNet 4 + their custom changes (NAT punchthrough server, bandwidth
  filters). **mtasa-blue is NOT a RakNet vendoring template; it is a
  RakNet ABI consumer.**
- **Sourcing**: We'd pull from `facebookarchive/RakNet` (upstream-archived
  2019, last commit 2018, ~150k LOC C++) or the SLikeNet fork
  (~community-maintained 2017 onward, similar size). Either way: archived
  upstream + ~150k LOC.
- **Feature coverage**: 100%. Every requirement on the matrix above plus
  encryption, NAT punch, bandwidth filters, the works. **Overkill by
  ~10×** for our scope.
- **Integration cost**: HIGH. RakNet ships as a CMake/VS-project
  monorepo with its own build, its own `MessageIdentifiers.h` enum, its
  own threading model. Static-linking is feasible but tunes the linker.
  ~150k LOC of dead-upstream C++ joining our maintenance surface.
- **Maintenance**: BAD. Upstream archived, no security patches in 7
  years, ARM/Windows-on-ARM compatibility unknown, several known bugs
  (SLikeNet fork exists exactly because RakNet's bugs accreted).
- **Verdict**: **NO.** The MTA precedent looks attractive on paper but
  unravels on contact: MTA didn't actually vendor it; they wrap a
  separate binary blob. For VOTV that would directly violate RULE №3
  (an external runtime dep on net.dll), and pulling source means
  importing 150k LOC of archived code for the next 10 years of
  maintenance to gain features we don't need.

### 2.2 ENet (http://enet.bespin.org/)

- **License**: MIT. OK.
- **RULE №3**: **CLEAN FIT.** ENet is one ~5k-LOC C library, no
  dependencies beyond Winsock (which we already use). Static-link into
  our DLL. No external runtime needed.
- **Sourcing**: `lsalzman/enet` upstream, last commit 2024 (active),
  used in production by League of Legends, Cube 2, Veloren, Mumble.
- **Feature coverage**:
  - Reliable ordered: yes (per channel)
  - Reliable unordered: yes
  - Unreliable: yes
  - Unreliable sequenced: yes
  - Multiple channels: yes (up to 255 — each independently
    ordered/sequenced)
  - Sliding-window ARQ: yes
  - Fragmentation: yes (up to ~64 KB per "reliable" message; ENet
    fragments + reassembles internally)
  - Connection mgmt: yes (peer connect/disconnect, ping, timeout)
  - Bandwidth limiting: yes (incoming/outgoing byte/sec caps)
  - Congestion control: yes (simple adaptive throttle)
  - Priority lanes: **NO** (no priority concept; only channels — but
    the bandwidth limit + channel system + reliability mode combination
    is functionally equivalent to MTA's 3-priority × 5-reliability
    matrix for our needs)
  - Encryption: not built-in (CHACHA20 patch available; we don't
    need it for LAN)
- **Integration cost**:
  - LOC: ~5500 across 9 .c files + 7 headers. Trivially vendorable —
    drop into `src/votv-coop/third_party/enet/`, two lines of CMake
    `add_subdirectory`. Builds clean on MSVC since 2002.
  - API ergonomics: `enet_peer_send(peer, channel, packet)` returns
    a packet with `ENET_PACKET_FLAG_RELIABLE` /
    `_UNRELIABLE_FRAGMENT` / `_UNSEQUENCED` flags. Maps 1:1 to our
    `ReliableKind` + `MsgType` split — we wrap each
    `Session::SendReliable(kind, ...)` as `enet_peer_send(peer,
    LANE_FOR(kind), packet)`.
  - Wrap behind `coop::net::Session` API: the 13 callers see zero
    change. Our `protocol.h` payload structs remain — ENet sends
    opaque bytes; we still serialize `PropSpawnPayload` etc.
    ourselves.
  - Migration: the new `Session` impl owns one `ENetHost*` + one
    `ENetPeer*`. Net thread is `enet_host_service(host, 5 ms)`-based
    instead of our `recvfrom + sleep(5ms)` loop. The two pose slots
    stay; the reliable channel becomes a free-form
    `Session::SendOpaque(kind, lane, bytes, len)` over the right
    ENet channel/flag.
- **Maintenance**: GOOD. Tiny, active upstream, used in shipping AAA
  (League). Bugs are well-known. We could fork it without pain (5k
  LOC vs RakNet's 150k).
- **Verdict**: **THE ONE CONTENDER.** ENet is the only library that
  passes RULE №3 (vendorable into our DLL, no runtime extras), MIT,
  small enough to maintain, and covers exactly the feature set we'd
  spend the next 2 weeks building ourselves.

### 2.3 Valve GameNetworkingSockets (GNS)

- **License**: BSD-3 (Valve open-sourced 2018). OK.
- **RULE №3**: **FAIL.** Hard runtime dependencies on OpenSSL (≥1.1)
  + libsodium + Protobuf. Static-linking all three into our DLL is
  technically possible but adds ~3 MB to the DLL and Protobuf's
  build system fights ours. The Steam variant also wants Steamworks.
- **Feature coverage**: full + DTLS encryption + relay through Valve
  infra (which we DON'T want — we're LAN-first).
- **Integration cost**: HIGH. Protobuf wire format conflicts with our
  packed-POD philosophy; their `ISteamNetworkingMessages` API is
  Steam-rich and the parts we don't need can't be cleanly excluded.
- **Verdict**: **NO.** Heavy, opinionated, opensource-but-Valve-centric.
  Overkill in the same way RakNet is, with the extra problem of three
  transitive C-library deps.

### 2.4 yojimbo (Glenn Fiedler)

- **License**: BSD-3. OK.
- **RULE №3**: **DEPENDENT.** Bundles libsodium (for crypto +
  cryptographically-secure RNG) and Mbed TLS. Vendorable but pulls
  in a second dep (libsodium) that's another ~30k LOC.
- **Feature coverage**: priority queues + sliding-window ARQ +
  fragmentation + encryption-first design (cannot be disabled
  cleanly). Glenn explicitly chose to bake encryption into yojimbo's
  identity from day one — for LAN coop that's pure overhead.
- **Integration cost**: MEDIUM (smaller than RakNet, larger than ENet;
  ~20k LOC + libsodium dep).
- **Maintenance**: low but alive. Glenn ships infrequent commits;
  authoritative reference impl status.
- **Verdict**: **NO.** Crypto-first is a misfit for LAN-first VOTV
  coop; for the same effort we get ENet which is smaller, simpler,
  and zero-dep.

### 2.5 Custom on top of Asio

Pure-header Asio is permissive (Boost license), but it's just an
async-I/O reactor — we'd still write our own ARQ/window/fragmentation
on top, AND add 100k LOC of Asio header weight. Net loss vs hand-rolling.
**Skip.**

### 2.6 Mongoose, NanoMSG, msquic, etc.

All TCP-/HTTP-/QUIC-oriented. Wrong shape for a 60 Hz pose stream.
**Skip.**

### 2.7 Keep hand-rolling

- **License**: trivially compliant.
- **RULE №3**: perfect.
- **Feature coverage**: today = single-lane stop-and-wait reliable +
  unreliable. To match ENet feature parity we need:
  1. **Priority lanes** (3 lanes; map ReliableKind→lane).
  2. **Sliding-window ARQ** (N-in-flight; track per-lane window, RTO,
     fast-retransmit on duplicate-ack).
  3. **Reliable unordered** (some events don't need ordering — e.g.
     PropDestroy of two separate props can arrive in any order).
  4. **Fragmentation** (one `PropSpawnPayload` is 160 B today; doable
     in our 232-byte budget, but `EntitySpawnPayload` is 96 B and
     future event payloads will exceed 232 B).
  5. **Congestion control** (TCP-friendly throttle so we don't melt
     a 5 GHz consumer router under a snapshot replay; today we drain
     at 1000 msg/s on LAN with no backpressure).
- **Integration cost** (estimating from MTA's wrapper LOC + similar
  small ARQ libs):
  - Priority lanes: ~80 LOC (per-lane queues, weighted-fair-queue
    drainer).
  - Sliding window ARQ + per-message timers: ~250 LOC (incl. tests
    for retransmit, fast-retransmit, RTO backoff, RFC1982 wraps).
  - Reliable unordered receiver path: ~60 LOC.
  - Fragmentation + reassembly + per-fragment timeouts: ~400 LOC
    (this is the expensive piece; reassembly buffers + per-fragment
    seq + holes + reassembly timeout).
  - Congestion control (simple AIMD or equivalent): ~150 LOC.
  - **Subtotal**: ~940 LOC net-new + edits to existing 1100 LOC.
  - **Plus test surface**: ARQ is famously edge-case-rich. Lost ack
    + duplicate + reorder + RTO-during-fast-retransmit + window-full
    + sender-stalled-receiver-drain — every combo needs a unit test
    or an autotest scenario, or we eat the bugs in user testing.
    Realistically: 1.5–2 weeks focused work + lingering bugs.
- **Maintenance** going forward: it's our own code, well-understood
  by us, modular (one feature per file), uses our logging + our
  threading conventions. **Lowest long-term burden.**

---

## §3. MTA precedent deep dive

### What MTA actually does

`Client/sdk/net/CNet.h` exposes:

```cpp
virtual bool SendPacket(
    unsigned char       ucPacketID,
    NetBitStreamInterface* bitStream,
    NetPacketPriority   packetPriority,
    NetPacketReliability packetReliability,
    ePacketOrdering     packetOrdering = PACKET_ORDERING_DEFAULT);
```

with the enums (from `net_common.h`):

```cpp
enum NetPacketPriority { HIGH, MEDIUM, LOW, COUNT };
enum NetPacketReliability {
    UNRELIABLE,
    UNRELIABLE_SEQUENCED,
    RELIABLE,
    RELIABLE_ORDERED,
    RELIABLE_SEQUENCED,
};
enum ePacketOrdering { DEFAULT, CHAT, DATA_TRANSFER, VOICE };
```

### How they actually USE it (grep-count of call sites)

Sampling 36+ `SendPacket(PACKET_ID_...)` call sites across `CClientGame`,
`CNetAPI`, `CClientPed`, `CDeathmatchVehicle`, `CPedSync`, `CObjectSync`,
`CResource`, `CStaticFunctionDefinitions`, `CUnoccupiedVehicleSync`,
`luadefs/CLuaWorldDefs`:

| Combination | Approx. usage |
| --- | --- |
| `HIGH + RELIABLE_ORDERED` | ~20 sites (events, RPC, lua-event, ped/vehicle inout, wasted, projectiles, explosions, damage sync) |
| `MEDIUM + UNRELIABLE_SEQUENCED` | ~10 sites (every `*sync` stream: pure-sync, vehicle-sync, ped-sync, object-sync, camera-sync, keysync) |
| `MEDIUM + RELIABLE` | 2 sites (bullet-sync — "delivered, unordered") |
| `LOW + RELIABLE_ORDERED + DATA_TRANSFER` | 2 sites (player screenshot upload) |
| `MEDIUM + UNRELIABLE` | 1 site (diagnostic) |

**Five enum values × three priorities × four ordering buckets =
60 combinations available; FOUR are actually used.** The MTA matrix
is RakNet's matrix exposed verbatim; MTA themselves only stress 4 cells
of it.

### What this tells us

The minimum viable shape is **three lanes**:

1. **STREAM lane** — unreliable-sequenced for high-rate pose
   streams. (today: Pose + PropPose.)
2. **EVENT lane** — reliable-ordered for game-event sync.
   (today: every `ReliableKind` value 1–14.)
3. **BULK lane** — reliable-ordered, low-priority,
   non-starvation-blocking. (future: snapshot-on-connect dumps,
   chat with attachments, future voice.)

We do NOT need MTA's 5-reliability matrix. We need **two reliability
modes** (reliable-ordered, unreliable) × **two-three priority lanes**.

### Mtasa-blue's wrapper

`Client/mods/deathmatch/logic/CNetAPI.cpp` is the entirety of the
MTA "wrap RakNet behind game-friendly API" layer for game-state sync:
~2300 LOC, but ~80% of it is per-packet bitstream serialization for
their >50 packet types — NOT networking. The networking-glue portion is
small: `RegisterPacketHandler` + `SendPacket` + `AllocateNetBitStream`
+ `DeallocateNetBitStream` and an "incoming packet" callback that
dispatches on `ucPacketID`. We have the equivalent already
(`HandleDatagram` switch in `session.cpp`).

---

## §4. Risk analysis

### Hand-roll risk register

1. **ARQ edge cases.** Sliding-window with fast retransmit / SACK is
   the textbook example of "looks simple, breaks subtly." Risk:
   **HIGH** without an autotest harness covering loss + reorder + dup.
   Mitigation: build a deterministic in-process loopback test that
   simulates lossy + reordered delivery (~200 LOC) before writing
   the window code.
2. **Fragmentation reassembly leaks.** A peer that drops mid-fragment
   leaks reassembly buffers. Risk: **MEDIUM**. Mitigation: per-fragment
   timeout + bounded reassembly arena.
3. **Throughput estimates wrong.** A 1264-entity snapshot replay
   (`PropSpawn` ×1264) at 1000 msg/s = 1.3 s. With a 4-window sliding
   window: ~0.3 s. With 16-window: ~80 ms. The current channel
   already drains at saturation on LAN per the new design (drain-on-ack
   = 1/RTT). The hand-roll cost is real but the win is also real.
4. **Bandwidth shaping omitted.** Hand-rolled code is famous for "no
   shaping until we hit the bug." For LAN that's safe; for the
   future-Internet phase it's a re-architect. Mitigation: design the
   per-lane interface with a `bytesPerSec` hook even if today it's
   no-op.
5. **Maintenance "the 14th increment" risk.** This is the explicit
   thing CLAUDE.md flags after harness.cpp grew silently. To avoid:
   ship the whole new design in ONE focused multi-file change with
   pre-extracted modules (one file per lane, one file per feature),
   not 14 incremental commits onto today's reliable_channel.cpp.

### Vendor-ENet risk register

1. **ENet API surface mismatch with our `Session`.** Risk: **LOW.**
   `enet_peer_send(peer, channel, packet)` maps 1:1 to our generic
   reliable-send; the two pose slots become `ENet`'s unreliable
   channel 0 + 1 with `ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT`. ~1
   day of glue to keep the 13 callers untouched.
2. **ENet's connection model (its own Hello / disconnect) duplicates
   ours.** Risk: **LOW.** We strip our handshake code and let ENet
   own connect/disconnect; the session-token trust boundary becomes
   ENet's `enet_peer_connect(... data ...)` payload.
3. **Threading model collision.** ENet wants you to call
   `enet_host_service(host, timeout_ms)` on one thread. That IS our
   net thread. Risk: **LOW.**
4. **Future feature gap.** ENet has no priority concept (only
   per-channel ordering + bandwidth caps). Risk: **LOW** — we map
   our 3-lane plan to 3 ENet channels with different reliability
   flags; per-channel bandwidth caps + interleaved sends give us
   functional priority.
5. **Vendoring 5k LOC of someone else's code we don't fully
   understand.** Risk: **MEDIUM.** When ENet misbehaves (and at some
   point it will — e.g. RTO tuning, IPv4-only assumption interacting
   with our LAN-discovery future plans), we need to fork-and-fix.
   That's the foundational cost of vendoring: we own the bug
   regardless of whose code it is.
6. **MTA-precedent dilution.** Vendoring ENet means our architecture
   stops mirroring MTA at the transport line (above the transport we
   still mirror MTA's parallel-class-hierarchy / packet-handler
   shape). Risk: **LOW.** MTA's RakNet wrapper code itself is
   closed; we never had transport-line MTA parallels to dilute.

### Decisive comparison

| Dimension | Hand-roll (proper rewrite) | Vendor ENet |
| --- | --- | --- |
| RULE №3 fit | perfect | perfect |
| Lines of code WE maintain | +940 (~2780 total) | +5500 third-party (we still own when it breaks) + ~300 wrapper |
| Lines of code WE write | +940 | ~300 |
| Edge-case bug surface we own | every line | ENet's bug-fix history, plus our wrapper |
| Time-to-feature-parity-with-ENet | 1.5–2 weeks focused | ~3 days integration + test |
| Knowing-it-cold | total | partial (would need to read ENet source for any non-trivial bug) |
| Tunability for VOTV quirks | total | limited to ENet's tunables; deeper changes = fork |
| Methodology fit | matches "augment SP" pattern — substrate-as-our-code | imports a foreign substrate |

The dimensions are split. ENet wins on **time-to-parity** by ~10 days.
Hand-roll wins on **edge-case ownership** and **methodology fit**.

---

## §5. Recommendation

**Hand-roll. ONE focused rewrite.**

### Reasoning

1. **RULE №3 is the binding constraint, and only ENet passes it
   cleanly.** RakNet doesn't (MTA-style; closed binary). yojimbo
   doesn't (libsodium dep). GNS doesn't (OpenSSL+Protobuf+libsodium
   deps). So the real choice is hand-roll vs ENet, not "any of five
   libraries."

2. **ENet vs hand-roll is not a clear win for ENet.** The 10-day
   time delta is real but it's a *one-time* delta. The 10-year
   maintenance question favors code we wrote. CLAUDE.md's WP13 (don't
   reinvent the wheel) covers MinHook + ImGui + DbgHelp — three
   wheels where the alternative is to write a hook engine + a GUI
   stack + a PDB parser. Network ARQ is qualitatively smaller than
   any of those; the analogy doesn't transfer.

3. **The feature set we need is a strict subset.** MTA stresses 4
   cells of RakNet's 60-cell capability matrix. We need 3 lanes ×
   2 reliability modes + fragmentation + a sliding window. That's
   ~940 LOC of focused code in a substrate we already understand,
   not ~5500 LOC of someone else's substrate.

4. **RULE №1 forbids the band-aid path.** The danger isn't hand-rolling
   — it's hand-rolling INCREMENTALLY. Adding priority lanes next week,
   then sliding-window the week after, then fragmentation the week
   after that, is exactly the 14-increment harness.cpp anti-pattern.
   **The hand-roll path is only safe if we commit to ONE design + ONE
   coherent rewrite + ONE PR/commit-cluster, not a drip.**

5. **The MTA precedent — read correctly — is "wrap your network in a
   priority/reliability/ordering API and let the call sites pick
   per-packet."** It does NOT say "use RakNet." MTA can't ship a
   network DLL without RakNet because their game shape demands NAT
   punch + WAN encryption + 1000-player rooms. VOTV coop is 2–4
   players on LAN, host-authoritative, no WAN concerns yet. We
   inherit MTA's API SHAPE without inheriting their substrate.

6. **Methodology principle 7 (engine-wrapper vs gameplay/network).**
   Our `coop/net/` IS the network substrate. Per the principle, it's
   ours, mirrors our reflection layer's "we own this" stance, and
   sits cleanly below `coop/` gameplay code. Vendoring a foreign
   substrate at this layer would force `coop/net/` to become a thin
   adapter — which works, but undermines the principle that we own
   our substrate.

### Confidence and caveats

- **Confidence: high** that hand-roll is right for the next 6
  months (Phase 5 work, all LAN-first, 2–4 players).
- **Caveat 1**: if we ever pivot to WAN multiplayer with NAT-punch +
  matchmaking, re-evaluate. At that point GNS's Steam relay layer
  becomes valuable enough to reconsider RULE №3 (Steam IS effectively
  the "external runtime" for that use case, and SDK distribution is
  standard).
- **Caveat 2**: if the rewrite drifts from "one focused multi-file
  PR" into "incremental over 6 weeks," **STOP** and reconsider
  vendoring ENet. The whole argument hinges on the 1.5–2 week
  estimate being achievable; if it slips by 3× we've shipped a
  RULE-1 crutch trail.

---

## §6. Roadmap — the hand-roll rewrite

**One coherent design. One feature-flagged rollout. One PR cluster.**

The new substrate replaces `reliable_channel.{h,cpp}` and the reliable
parts of `session.{h,cpp}`. `transport.{h,cpp}` stays as-is.
`protocol.h` payload structs stay; `MsgType`/`ReliableKind` enums
stay (the wire-visible names of in-flight features).

### Module layout (new files, per CLAUDE.md 800-LOC soft cap)

```
src/votv-coop/include/coop/net/
  transport.h                      (UNCHANGED)
  protocol.h                       (UNCHANGED -- payloads)
  session.h                        (TRIMMED -- generic SendOnLane verb)
  channel/
    channel.h                      (per-lane interface: enum Lane, enum Rel)
    window_sender.h                (sliding-window ARQ sender, 1 per lane)
    window_receiver.h              (sliding-window receiver, 1 per lane)
    fragmenter.h                   (split + reassemble multi-fragment msgs)
    lane_scheduler.h               (weighted-fair-queue across 3 lanes)

src/votv-coop/src/coop/net/
  transport.cpp                    (UNCHANGED)
  session.cpp                      (DESCOPED -- handshake/peer/RTT only)
  channel/
    window_sender.cpp              (~250 LOC)
    window_receiver.cpp            (~200 LOC)
    fragmenter.cpp                 (~300 LOC)
    lane_scheduler.cpp             (~120 LOC)
    channel.cpp                    (~100 LOC -- shared utility)
```

Total new code: ~970 LOC across 5 .cpp files, each well under the
800-LOC soft cap. No single file balloons.

### Lane model

```cpp
enum class Lane : uint8_t {
    Stream  = 0,   // unreliable-sequenced; for pose / prop-pose streams
    Event   = 1,   // reliable-ordered; default for ReliableKind values 1..14
    Bulk    = 2,   // reliable-ordered, low priority (snapshot replay)
};

enum class Rel : uint8_t {
    Unreliable,         // drop freely, newest-wins (Stream lane)
    ReliableOrdered,    // exactly-once, in-order per lane (Event, Bulk)
    ReliableUnordered,  // exactly-once, any order (NOT YET; future PropDestroy)
};
```

`Session::SendOnLane(lane, rel, kind, bytes, len)` is the new generic
verb. The 13 existing callers move to it:
- `SendReliable(kind, ...)` → `SendOnLane(Lane::Event, ReliableOrdered, kind, ...)`
- `SetLocalPose` keeps its dedicated slot (Stream lane, type=PoseSnapshot)
- `SetLocalPropPose` ditto (Stream lane, type=PropPose)

Pose-stream is NOT a `SendOnLane` caller — it's the only feature with
a "always overwrite, newest-wins" semantics that doesn't fit the
queue model. Keeps the pose path as the simple per-tick "memcpy +
sendto" it is today.

### Increment plan

Each increment is a single audited commit-cluster (per CLAUDE.md
"after shipping, audit with agents"). No increment ships without an
autonomous in-process loopback test (lossy + reordered) for the
new code.

| Inc | Deliverable | LOC est. | Audit focus |
| --- | --- | --- | --- |
| **Inc-0** | Loopback test harness: simulates loss / reorder / dup at the transport boundary | +200 | tooling |
| **Inc-1** | Sliding-window sender + receiver per lane (replaces stop-and-wait); Event lane only | +450 | RFC1982 wrap correctness, RTO correctness |
| **Inc-2** | Lane scheduler (3 lanes: Stream/Event/Bulk); lane-fairness | +120 | starvation-free draining, lane interleave correctness |
| **Inc-3** | Fragmentation + reassembly | +300 | reassembly memory bound, frag-timeout, lost-fragment dedup |
| **Inc-4** | Migrate the 13 callers from `SendReliable` to `SendOnLane(Event, ReliableOrdered, ...)` | ~50 net | call-site shape preserved; protocol bump if any wire layout shifted |
| **Inc-5** | Add `Bulk` lane usage: route Phase 5S0 snapshot-on-connect drain to it | +30 | confirms lane-priority works in practice |
| **Inc-6** | (Future) `ReliableUnordered` for events that don't need ordering | +80 | post-rewrite |

Inc-1 through Inc-4 are the core rewrite. Inc-5/-6 are post-rewrite
opportunistic uses. Estimated wall-clock: 7–10 working days for
Inc-0..Inc-4 with the autotest discipline applied. (Closer to 5 if no
nasty surprises; closer to 15 if SACK / fast-retransmit reveal subtle
bugs — but the autotest harness in Inc-0 is exactly the mitigation
for that.)

### What does NOT change

- `protocol.h` payload structs and `MsgType` / `ReliableKind` enum
  values stay where they are. Wire-version-bump only if a payload
  layout legitimately changes (Inc-3 fragmentation might add a
  fragment-header layer in front of the packet header, in which
  case bump v10).
- `transport.cpp` stays — pure Winsock, no reason to touch.
- The 13 callers' shapes are preserved by Inc-4's mechanical
  rename. They don't choose lanes; they call `SendXxx` verbs which
  internally map to `(Event, ReliableOrdered)`. The lane choice
  is THE thing we tune later for individual call sites if needed.

### Failure mode plan

If midway through Inc-1 (the sliding-window sender) we discover a
non-trivial bug class — e.g. RTO interaction with lane-scheduler
backoff isn't converging in the autotest harness, or fragmentation
turns out to have a 3-day debug session — **STOP and vendor ENet**
(under §2.2). That's the escape hatch. Document the failure, ship
the partial rewrite revert, and pivot to ENet integration. This
keeps the hand-roll bet bounded: max 1.5 weeks of work risked before
we accept the vendor path.

### Migration safety net

The new substrate ships behind a feature flag `votvcoop.net.newSubstrate=0`
(default off) for the first Inc-1 ship. Both substrates compile and
the old `reliable_channel.cpp` stays until Inc-4 is audited green
on an end-to-end LAN test. Then the flag flips default-on and the
old `reliable_channel.cpp` is **deleted in the same commit** per
RULE №2 (no migration baggage). No parallel old-and-new paths
beyond the single migration commit.

---

## §7. Summary table

| Aspect | Recommendation |
| --- | --- |
| **Decision** | Hand-roll, single coherent rewrite |
| **Decisive factor** | RULE №3 + scope: feature set is small (3 lanes × 2 reliability), candidates either fail RULE №3 (RakNet/yojimbo/GNS) or save only ~10 days (ENet) at the cost of carrying 5500 LOC of someone else's code for 10 years |
| **Rejected: RakNet** | mtasa-blue does NOT vendor RakNet; the precedent isn't what it looks like. 150k LOC archived upstream is wrong shape. |
| **Rejected: ENet** | Real contender; passes RULE №3 cleanly. Loses on long-term ownership of edge cases + on methodology principle 7 (we own our substrate). |
| **Rejected: GNS / yojimbo** | RULE №3 fail (OpenSSL/libsodium deps). |
| **Time** | 7–10 working days for Inc-0..Inc-4 |
| **Escape hatch** | If Inc-1 or Inc-3 reveals a bug class we can't close in 3 days, pivot to ENet vendoring per §2.2. |
| **MTA precedent inherited** | The API shape (lane × reliability × ordering at the call site) — NOT the substrate. |
| **Crutch trap to avoid** | Drip-feeding lanes / window / fragmentation in 6 separate weekly increments onto today's reliable_channel.cpp. One focused rewrite, one PR-cluster. |
