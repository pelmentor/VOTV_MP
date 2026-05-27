# VOTV coop wire layer — feature-by-feature networking requirements

**Date**: 2026-05-27
**Author**: research agent (parallel to build-vs-vendor sibling)
**Purpose**: inventory the actual networking requirements the VOTV coop
mod needs from its wire layer, derived from the shipped + planned
`COOP_SCOPE.md` features, the current protocol surface (`protocol.h`
v9), the recent reliable-channel rewrite (FIFO queue, sender-side, one-
in-flight), and the user-stated scale rules (4 peers, 100 entities,
whole-map sync, host-authoritative save bootstrap, future voice chat).

The output drives the build-vs-vendor decision (RakNet / ENet / yojimbo /
GameNetworkingSockets / keep hand-rolled). It is OPINIONATED — only
features actually in scope (per `COOP_SCOPE.md` + memory directives)
appear; speculative engine internals do not.

---

## 1. Full feature requirements table

Columns:

- **Direction** — who sends to whom (`host→all`, `peer→all`,
  `peer→host`, `peer↔peer`, `client→host`, `host→client`).
- **Reliability** — `unreliable` / `reliable` / `reliable-sequenced`
  (newest beats older; older arriving == drop) / `unreliable-sequenced`
  (dup-drop by seq, no ack). "Reliable" implies ordered FIFO in the
  current channel; explicit `reliable-unordered` is flagged when the
  ordering invariant doesn't matter.
- **Ordering** — `FIFO-global` (current channel behaviour), `FIFO per-stream`
  (e.g. per-prop, per-terminal), `newest-wins` (last write wins; older
  arrivals discarded), `none`.
- **Priority** — `high` (user-perceived latency-sensitive interactive),
  `normal` (background gameplay state), `bulk` (large transfer that
  must not starve the others). See §2.
- **Rate** — typical send cadence. `60 Hz`, `30 Hz`, `bursty`, `sparse`
  (event-driven, multiple per minute at most), `rare`, `once`.
- **Payload size** — current or projected on-wire bytes (excludes
  PacketHeader 20B + ReliableHeader 8B for reliable kinds).
- **Identity** — what the payload uses to address the affected entity
  on the receiver: `peerSessionId`, `WireKey` (32B Aprop_C save UUID),
  `sessionId` (host-assigned uint32 monotonic for NPCs), `actorKeyHash`
  (CRC32 of WireKey for compactness), `--` (none, action is fixed).
- **Sender blocks?** — does the sender pause / spin / drop until ACK?
  Today (post-rewrite) the answer is uniformly NO — `Send()` enqueues
  and returns true. Column reflects the *semantic requirement*: does a
  retry-loop make sense if the wire dropped, or is the event lost?
- **Idempotent?** — can the receiver safely apply the same packet
  twice? (Wire-level dup-drop handles UDP duplicates today.)
- **Order against OTHER kinds?** — does ordering vs other reliable
  kinds matter, or only same-kind / same-entity?

### 1.1 Shipped today (protocol v9)

| Feature | Direction | Reliability | Ordering | Priority | Rate | Payload | Identity | Notes |
|---|---|---|---|---|---|---|---|---|
| **Hello / handshake** | client→host then host→client | unreliable burst until ACK by Pose start | n/a | high (gates session) | once | 20 B (header only) | -- | Token mint on host side; client learns token from server's Hello. Out-of-order would still resolve via locked-peer state. |
| **Bye / disconnect** | bidirectional | unreliable (single fire-and-forget) | n/a | high | once | 20 B | -- | If lost, peer-timeout (lastRecvMs heartbeat) eventually disconnects. Idempotent. |
| **Ping / Pong (RTT probe)** | bidirectional | unreliable-sequenced | newest-wins | normal | ~1 Hz | 28 B | -- | Stale Pong harmless; sender drops by senderMs echo. |
| **PoseSnapshot (puppet pose)** | peer→peer (bidirectional) | unreliable-sequenced | newest-wins | normal | 60 Hz | 32 B | implicit (only one local pose per sender) | The reference for "loss-tolerant streaming"; receiver applies newest non-stale, drops freely. |
| **PropPose (held prop drive)** | grabbing-peer → others | unreliable-sequenced | newest-wins per `WireKey` | normal | 60 Hz | 56 B | WireKey | Per-stream newest-wins. Stream stop > 500 ms == implicit release. |
| **PropSpawn (Aprop_C world spawn)** | broadcast (host or grabbing client) | reliable | FIFO-global | **bulk** (1264-entity replay on connect) + sparse mid-game | bursty | 160 B | WireKey | The "snapshot bootstrap" pressure case. At LAN drain rate ~1000 msg/s the replay drains in ~1.3 s; the rewrite is what enables this. |
| **PropDestroy** | bidirectional | reliable | FIFO per-WireKey at minimum | normal | sparse | 32 B | WireKey | Idempotent at receiver (echo-suppress + dup actor lookup returns null). |
| **PropRelease (throw)** | grabbing-peer → others | reliable | FIFO after the last PropPose for the same WireKey | normal | rare | 56 B | WireKey + vel | Must arrive AFTER its PropPose stream to drive correct hand-off; today FIFO-global suffices. |
| **EntitySpawn (NPC)** | host→all | reliable | FIFO-global (before any EntityPose for same sessionId) | normal | sparse | 96 B | sessionId | Inc2 wired-not-live; receiver: BeginDeferred + Finish + cache. |
| **EntityDestroy (NPC)** | host→all | reliable | FIFO-global | normal | sparse | 8 B | sessionId | |
| **ItemActivate (flashlight / equipment)** | bidirectional | reliable | FIFO per-peer (state machine) | normal | sparse | 24 B | peerSessionId + actorKeyHash | Sender retries each tick if previously busy (item_activate.cpp:738); post-rewrite this collapses to single enqueue. |
| **WeatherState** | host→all | reliable-**sequenced** semantics (logically newest-wins) | newest-wins is the *correct* semantics; today wire is FIFO-ordered | normal | sparse + burst on storm onset | 20 B | -- | Today FIFO-ordered; if 2 stale states queue while connection lags, only the latest matters. See §3.2. |
| **LightningStrike** | host→all | reliable | FIFO-global | normal | sparse (during storm) | 16 B | -- | Discrete event at point; cannot be reduced. |
| **RedSky (story toggle)** | host→all | reliable | FIFO-global | normal | rare | 8 B | -- | Logically newest-wins (only most recent on/off matters) but rate is so low the distinction is academic. |
| **Join (nickname announce)** | peer→peer | reliable | first reliable message of session | normal | once | ~24 B | -- | Idempotent (re-Join after reconnect resets nick). |
| **RestoreVitals (F3 dev)** | host→client | reliable | FIFO-global | **high** (user-interactive instant) | rare | 0 B | -- | The 10-press F4 bug was the same priority shape: dropped silently behind PropSpawn flood. **Currently same FIFO as PropSpawn → can stall behind 1264-entity snapshot.** Wants high-priority lane. |
| **TeleportClient (F4 dev)** | host→client | reliable | FIFO-global | **high** | rare | 24 B | -- | Same priority concern as RestoreVitals — this is *the* feature that exposed the priority gap. |

### 1.2 Planned (in scope per COOP_SCOPE.md, not yet wired)

| Feature | Direction | Reliability | Ordering | Priority | Rate | Payload | Identity | Notes |
|---|---|---|---|---|---|---|---|---|
| **ChatText (typed)** | host-relayed (peer→host→others) | reliable | FIFO-global | normal | sparse (human typing) | ≤200 B | senderPeerId | MTA-shape host-relay; same channel as Join. |
| **SystemEvent (joined / left / error feed)** | host→all (synthesized) | reliable | FIFO-global | normal | rare | ≤80 B | -- | Already de facto exists as Join/Bye derivatives. |
| **EntityPoseBatch (NPC pose, aggregated)** | host→all | unreliable-sequenced | newest-wins per batch | normal | 30 Hz aggregate | ~22-26 B × N entries (1 datagram, N≤8 at 256B MTU; **bigger MTU needed**) | sessionId per entry | Major scope item. See §5 on MTU. |
| **EntityManifest (NPC list)** | host→client | reliable | once on connect, before EntityPoseBatch | bulk | once | per-NPC + chunked | sessionId | Required so receiver can resolve EntityPoseBatch entries. |
| **EntityState delta (NPC state change)** | host→all | reliable | FIFO per-sessionId | normal | sparse | ~16 B + flag bytes | sessionId | Flag-byte delta encoding per MTA precedent. |
| **EntityEvent (transient: explosion/projectile/sound)** | host→all | reliable | FIFO-global | normal (single events) | sparse | ~40 B | -- | One-shot world events; broadcast unconditionally per whole-map rule. |
| **DoorState** | bidirectional (player-triggered) or host-only (NPC-triggered) | reliable | FIFO per actorKeyHash | normal (gameplay) — could be **high** for player E-press to avoid laggy door | sparse | ~12 B | actorKeyHash | RE doc landed; impl pending. Player-induced lag visible if behind bulk. |
| **LightState (switch)** | bidirectional | reliable | FIFO per actorKeyHash | normal (could be high for interactive feel) | sparse | ~12 B | actorKeyHash | |
| **LockState (keypad)** | bidirectional | reliable | FIFO per actorKeyHash | normal | sparse | ~12 B | actorKeyHash | |
| **TerminalEnter / TerminalExit** | peer→host (request), host→all (broadcast) | reliable | FIFO per terminalKey | normal | rare | ~16 B | terminalKey | |
| **TerminalInput (analog control deltas)** | peer→host | unreliable-sequenced + 1Hz heartbeat | newest-wins per terminalKey | **high** (interactive race minigame) | 20 Hz peak | ~16 B | terminalKey + peerId | "Race together" requires lowest possible queue latency. |
| **TerminalCrosshairState (hot state broadcast)** | host→all | unreliable-sequenced | newest-wins | high | 20 Hz | ~24 B | terminalKey | Both peers render same state from this broadcast. |
| **TerminalDelta (slow state)** | host→all | reliable | FIFO per terminalKey | normal | 10 Hz | ≤64 B | terminalKey | |
| **TerminalSnapshot (full state on enter)** | host→peer | reliable | once per Enter | **bulk** (fits chunked because ≤952 B) | once | ~952 B chunked | terminalKey | Multi-fragment because > 228B reliable payload limit. |
| **TerminalButtonPress (A1a/A1b clicks)** | peer→host → broadcast | reliable | FIFO per terminalKey | **high** (interactive) | sparse | ~12 B | terminalKey + componentHash | 80 ms per-componentHash de-dup gate on host. |
| **TerminalScroll (A2)** | peer→host | unreliable-sequenced + 1Hz heartbeat | newest-wins | high | 10 Hz | ~12 B | terminalKey + peerId | |
| **SaveSnapshotChunk (Phase 5S0 bootstrap)** | host→client | reliable | FIFO-global | **bulk** | once per session | many × ≤228 B chunks | chunkIdx | Could be KB-to-MB depending on save size. THE bulk-channel justification. |
| **SaveSnapshotReady (client→host signal)** | client→host | reliable | once after final chunk | high | once | 4 B | -- | |
| **VoiceFrame (Plasmo-style)** | peer→peer (or peer→host→others if host-relayed) | **unreliable-sequenced** | newest-wins per-sender (jitter buffer reorders within window) | normal (continuous data; latency-sensitive but **not** reliability-sensitive) | 30-50 Hz × N speakers | ~80-200 B (opus 16-64 kbps) | senderPeerId + audioSeq | **Hardest to model** — see §3.5 + §6. |

---

## 2. Priority class taxonomy

The recent F4-needs-10-presses bug exposes the **fundamental priority gap**:
reliable channel is single global FIFO, so a 1264-entity PropSpawn replay
(snapshot bootstrap) blocks RestoreVitals / TeleportClient for the duration.
Post-rewrite the *callers* no longer drop, but the *user-perceived latency*
of high-priority interactive actions is unchanged — they're at the back of
the queue.

### 2.1 Recommendation: **3 priority classes**

Not 2 (high/normal), not 4 (RakNet's IMMEDIATE/HIGH/MEDIUM/LOW). Three is
the actual taxonomy our scope demands:

| Class | Latency budget | Examples | Behaviour |
|---|---|---|---|
| **HIGH** (interactive) | ≤ 1 RTT (≤ 1 packet ahead of bulk) | TeleportClient, RestoreVitals, TerminalInput, TerminalButtonPress, TerminalCrosshairState, DoorState (player-induced), ChatText (typed) | Jumps ahead of NORMAL and BULK in the outbox. Strict FIFO within class. |
| **NORMAL** (gameplay state) | ≤ 100 ms | PropDestroy, PropRelease, ItemActivate, WeatherState, LightningStrike, RedSky, EntitySpawn/Destroy, EntityEvent | Default. Sequenced ahead of BULK, behind HIGH. |
| **BULK** (background transfer) | best-effort, hours OK | PropSpawn (during bootstrap burst), SaveSnapshotChunk, TerminalSnapshot, EntityManifest | Drains last. Must not starve completely — see §2.3. |

PropSpawn deserves nuance: a steady-state mid-game spawn (player drops one
item) is logically NORMAL; the connect-edge replay of 1264 entities is
BULK. The current code can't distinguish. **Recommendation**: introduce a
priority enum parameter on `SendReliable` (defaulting NORMAL); have the
bootstrap path tag its sends BULK.

### 2.2 Why not RakNet's 4 classes

RakNet's IMMEDIATE_PRIORITY (bypass even the outgoing buffer) maps to
zero of our use cases. The closest semantic — "send right now, before any
queued packet" — would still serialize through the same UDP socket and
yield ≤1 packet ahead, identical to HIGH. We don't need that fourth
distinction.

### 2.3 Anti-starvation policy

A pure priority FIFO starves bulk under sustained NORMAL traffic. Use a
**weighted round-robin** across non-empty classes per outbox-drain event:
e.g. 4 HIGH, then 2 NORMAL, then 1 BULK per cycle. Tuned so the 1264-prop
snapshot still drains in ~few seconds while a TeleportClient arrives in
≤2 packet times. Concrete: drain ratio 4:2:1 at ~1000 packets/s LAN means
even saturated BULK lets a HIGH packet through within ~7 ms.

---

## 3. Reliability mode taxonomy

### 3.1 Modes we actually use

| Mode | What it means | Examples in scope |
|---|---|---|
| **Unreliable** | fire-and-forget; no ack, no dedup | Hello, Bye, Ping, Pong |
| **Unreliable-sequenced** | unreliable but receiver drops anything older than the highest-seen seq | PoseSnapshot, PropPose, TerminalCrosshairState, TerminalScroll, TerminalInput, VoiceFrame, EntityPoseBatch |
| **Reliable-ordered (FIFO)** | guaranteed delivery, in order of send | Today's everything-else |
| **Reliable-sequenced (newest-wins)** | guaranteed delivery, but receiver drops stale arrivals — older-than-latest never applied | WeatherState (only the latest scheduler-state matters), RedSky (only latest on/off) |
| **Reliable-unordered** | guaranteed delivery, no ordering constraint | We have **no current use** — every reliable kind has some ordering meaning |

### 3.2 Reliable-sequenced — yes, we need it

**WeatherState** is the textbook case. Today: storm onset fires
`causeRain(true)` + several `setRainProperties` updates within a few
seconds; if reliable channel is congested, all of them queue and apply
in sequence on the client — the client briefly applies an old
intermediate state, then catches up to current. This is *visible* (rain
intensity ramps weirdly) but not *broken*.

In a future world with 100-NPC bandwidth contention this *will* break.
A reliable-sequenced mode collapses redundant state updates: queue front
is "the latest WeatherState"; any new WeatherState replaces it; ack
removes only when the *latest* delivers.

Same pattern fits RedSky toggle and any future host-broadcast game-state
field (sharedMoney, dayTime, story flags).

### 3.3 Unreliable-sequenced — already covered

`PoseSnapshot` is the canonical case. Receiver compares incoming `seq`
against `lastRemoteSeq_` via RFC-1982 serial-compare; older = drop. The
*infrastructure* exists for this mode (see session.cpp pose path) but is
not generalized. Voice frames will need the same shape, and so will the
future `EntityPoseBatch`.

**Recommendation**: factor "unreliable-sequenced" into a generic helper
the wire layer exposes, instead of each feature re-implementing the seq
compare.

### 3.4 What does NOT need reliability

- **VoiceFrame** — opus packet loss < 5% is imperceptible; ack+retransmit
  would add latency that destroys interactivity. Strict unreliable
  with jitter buffer on receiver.
- **TerminalInput / TerminalScroll / TerminalCrosshairState** — newest-
  wins beats acked-ordered; an old crosshair position is worse than a
  dropped packet.
- **PropPose / PoseSnapshot / EntityPoseBatch** — same.

### 3.5 The hard one: voice

Voice chat (future per `project-voice-chat-future`) is the feature that
*least* fits the current architecture. Properties:

- High bandwidth — 30-50 packets/s × N speakers; at 4 peers all talking
  this is 120-200 pkt/s of voice alone.
- Latency-sensitive — every ms of jitter adds perceived lag.
- Loss-tolerant — opus tolerates 5-10% loss with concealment.
- Spatial — receiver mixes voice into a UAudioComponent positioned at
  the speaker's puppet.

The wire layer needs to support **per-sender unreliable-sequenced
substreams**, not just one global unreliable stream. This is the use
case that pushes the "single one-in-flight reliable + single unreliable
slot" model past its limit. See §4.3 + §6.

---

## 4. Ordering requirements

### 4.1 Three ordering domains

| Domain | What | Examples |
|---|---|---|
| **Per-stream FIFO** | Order matters only between packets in the same logical stream (same prop, same NPC, same terminal) | PropPose+PropRelease per WireKey; TerminalInput per terminalKey+peerId; EntityPose per sessionId |
| **Global FIFO** | Order matters across all reliable messages | Today's whole reliable channel. Mostly a *coincidence* of the implementation, not a hard requirement of most features |
| **None / newest-wins** | Older arrivals are discarded, no enforced order | PoseSnapshot, WeatherState (if reliable-sequenced), TerminalCrosshairState, VoiceFrame |

### 4.2 What truly needs global FIFO?

Working through the table: **almost nothing**. Specifically:

- **PropSpawn before PropDestroy** for the same WireKey — yes, but
  that's per-WireKey, not global.
- **EntityManifest before EntityPoseBatch** — yes, but that's a one-time
  bootstrap edge; a "send EntityManifest first then start streaming"
  ordering at the sender suffices.
- **Join first** — yes; ensure Join is the literal first reliable send.
- **SaveSnapshotChunk[N] before SaveSnapshotChunk[N+1]** — yes, but
  chunkIdx is in-payload, so receiver could reassemble out-of-order.

In practice global FIFO is the easy implementation; per-stream FIFO is
the *correct* model. Switching to per-stream sequencing would let
HIGH/NORMAL/BULK reorder cleanly — a HIGH TeleportClient does not need to
wait for an in-progress PropSpawn burst because they don't share an
ordering domain.

### 4.3 Per-channel sub-streams (RakNet's "ordering streams")

RakNet's model: 32 independent ordering streams per channel; a packet
declares its stream id; ordering is enforced within stream only.

Mapped onto our scope: ~6-8 logical streams suffice.

| Stream | Members |
|---|---|
| `STREAM_SESSION` | Join, Bye, ChatText, SystemEvent, RestoreVitals, TeleportClient (in-session lifecycle + interactive dev) |
| `STREAM_PROP_LIFECYCLE` | PropSpawn, PropDestroy, PropRelease (the natural ordering domain — and BULK during bootstrap) |
| `STREAM_NPC_LIFECYCLE` | EntitySpawn, EntityDestroy, EntityState, EntityEvent, EntityManifest |
| `STREAM_WORLD_STATE` | WeatherState, LightningStrike, RedSky (host-broadcast world flags) |
| `STREAM_ITEM` | ItemActivate (per-peer item activation state) |
| `STREAM_DOOR` | DoorState, LightState, LockState (interactable world state) |
| `STREAM_TERMINAL` | TerminalEnter/Exit/Input/CrosshairState/Delta/Snapshot/ButtonPress/Scroll |
| `STREAM_SAVE_BOOT` | SaveSnapshotChunk, SaveSnapshotReady (one-time, bulk) |

This buys two concrete things:

1. **A door E-press doesn't wait for a prop snapshot replay.**
2. **A terminal click during weather change doesn't wait for the prop
   destroy that fired earlier.**

A simple in-flight slot per stream gives the same drain semantics
locally — 8 in-flight messages instead of 1, but each in its own
ordering domain. This is the path of least surprise.

---

## 5. Bandwidth + throughput estimates

### 5.1 Datagram budget

`kMaxPacketBytes = 256` today. This is the *load-bearing* constraint
behind the multi-fragment TerminalSnapshot and the 4-byte WireKey-to-id
optimization. **256 B is tight** and will need to grow:

- EntityPoseBatch (100 NPCs × 26 B = 2.6 KB) — already 10× over.
- VoiceFrame at higher bitrates would exceed 256 B.
- SaveSnapshotChunk benefits from larger MTU.

Recommendation: **bump kMaxPacketBytes to 1200 B** (safe LAN MTU,
matches RakNet default). This unblocks EntityPoseBatch, larger save
chunks, and higher-quality voice in a single change.

### 5.2 Peak bandwidth at 4-peer scale

| Stream | Peak rate | Bytes/packet | KB/s out (per sender) |
|---|---|---|---|
| Pose (each peer sends own) | 60 Hz | 52 B (incl header) | 3.1 KB/s × 1 (self) |
| Pose receive (3 puppets) | 60 Hz × 3 | 52 B | 9.4 KB/s in |
| PropPose (if 1 held per peer) | 60 Hz × 1 | 76 B | 4.6 KB/s |
| EntityPoseBatch (host→all) | 30 Hz | ~2.6 KB (100 NPCs) | 78 KB/s × 3 fan-out = 234 KB/s |
| PropSpawn snapshot bootstrap | 1000 pkt/s for ~1.3 s | 188 B | ~150 KB/s during burst |
| WeatherState | sparse | 48 B | trivial |
| VoiceFrame at 32 kbps × 4 speakers | 50 Hz × 4 | 100 B | 4 KB/s × N receivers |
| SaveSnapshotChunk (peak during bootstrap) | 1000 pkt/s | 256-1200 B | up to ~1.2 MB/s peak |

**Aggregate steady-state at 4 peers, 100 NPCs**: ~250 KB/s host-out,
~80 KB/s per-client out — within whole-map-sync's ~300-400 KB/s peak
projection. Confirmed.

**Aggregate during connect-edge burst**: SaveSnapshotChunk + PropSpawn
replay simultaneously can hit ~1.4 MB/s briefly. LAN handles this; WAN
would not. **WAN is Phase 7+; not a target.**

### 5.3 In-flight window

Current: 1 reliable message in flight per channel (stop-and-wait inside
the FIFO queue). RakNet default: ~32 messages sliding-window. ENet: per-
channel reliability, multiple in flight.

LAN drain at 1 in flight = 1/RTT msg/s. RTT 1-2 ms LAN → ~1000 msg/s, OK
for current scale. WAN RTT 30-50 ms → ~30 msg/s; *that's where 1-in-flight
breaks*. Phase 7+ concern. **Recommendation: design wire layer to support
sliding window N, ship 1, scale to ~16-32 when WAN matters.**

### 5.4 Connection scaling

`kMaxPeers = 4` per `project-coop-4-player-target`. Wire layer needs:

- Per-peer **send pacing** (one Tx queue per peer, not one global)
- Per-peer **endpoint** (Endpoint struct, today single)
- Per-peer **session token** + sequence spaces
- Host-side **fan-out** of host→all packets to each connected client
  endpoint (today: host has at most one peer)
- Per-peer **RTT, packet-loss, congestion** stats

Per-peer fanout cost at 4 peers ≈ negligible. The Session class needs to
own a `std::array<PeerState, 4>` instead of single fields.

---

## 6. Future scope deep-dives

### 6.1 Voice chat — the hardest model

Voice doesn't fit any current packet. Requirements:

- **Per-sender substream** with own seq space (4 peers = 4 sequenced
  voice streams).
- **Newest-wins** ordering within sender.
- **Jitter buffer** at receiver (50-150 ms target depth).
- **Backpressure-free**: voice never holds up gameplay; queue overflow
  = drop oldest, not block.
- **High datagram count**: 30-50 pkt/s × 4 senders = 120-200 pkt/s of
  voice alone. Must coexist with gameplay datagrams on the same socket.
- **MTU**: opus packets at 32 kbps × 20 ms frame = ~80 B; at 64 kbps ×
  10 ms = ~80 B. Comfortable in 256 B today; trivially fits 1200 B.

Recommendation for the build-vs-vendor decision: voice is the strongest
argument for either (a) a vendor lib that already does per-sender
unreliable-sequenced streams (RakNet does, GameNetworkingSockets does),
or (b) extending hand-rolled with explicit "named stream" abstraction.
The vendor cost is paid once; the hand-rolled cost is paid every time
we add a new stream type.

### 6.2 Save snapshot bootstrap — the bulk channel

Per `project-coop-save-host-authoritative`: full snapshot at connect.
RE not yet done on Avotvsave_C, but estimated size: 10-500 KB compressed.
This is *the* feature that proves we need a bulk priority class — a
client disconnect-and-reconnect during play must not freeze the host's
weather/door/item updates while the snapshot replays.

Wire layer requirements:

- **Chunked** with reassembly (one reliable packet per chunk; receiver
  reassembles in chunkIdx order; gaps requeue).
- **Bulk priority** (yields to HIGH/NORMAL via WRR drain).
- **Resumable** if disconnect mid-stream — receiver remembers received
  chunkIdx bitmap; sender starts from first missing.
- **Throttleable** — host should be able to cap snapshot KB/s if the
  client is bandwidth-constrained (WAN). Phase 7+.

### 6.3 EntityPoseBatch — the aggregated unreliable stream

Per `project-coop-scale-100-entities`: 1 packet/tick covering 100 NPCs.
At 26 B/entry × 100 = 2.6 KB. Today's 256 B MTU rules this out.

Wire layer requirements:

- **MTU bump to ≥ 1200 B** (mandatory).
- **Aggregator** on host: every tick, walk active NPCs, pack entries.
- **Unreliable-sequenced** with seq compare on the *batch*, not the
  entries.
- **Per-entry rate gating** — NPCs at different activity rates (5/15/30/60 Hz);
  aggregator includes each entry only when its rate cycle ticks.

This stream is single-direction (host → clients) and doesn't need any
per-client tailoring (whole-map sync == same payload to everyone). Pure
broadcast.

### 6.4 Terminal sync — the busiest reliable surface

Per `project-coop-interactable-terminals`: 4 new wire kinds + concurrent
streams + high-priority interactivity. With 8 simultaneous controllable
terminals × 20 Hz crosshair updates × 4 peers = 80 pkt/s of
TerminalCrosshairState alone, plus per-peer TerminalInput. Pushes the
per-stream sequencing model from §4.3 hard.

If we ship STREAM_TERMINAL as one ordering stream the busy crosshair
broadcasts can starve a click. If we ship it as one stream per terminal
(8 + dynamic) the per-stream overhead grows. Cleanest answer:
**per-stream FIFO per terminalKey** (i.e. dynamic per-actor streams
within STREAM_TERMINAL), only enforce ordering when keys match — same
shape as PropPose per WireKey.

---

## 7. Connection-edge requirements

Every "what does a fresh client see on connect?" event drives reliable
floods. Inventoried:

| Event | What floods | Volume |
|---|---|---|
| Phase 5S0 SaveSnapshot | save fields + props + NPCs | 10s of KB to a few MB |
| Aprop_C continuous snapshot | every keyed prop (~1264 today) | ~200 KB |
| EntityManifest | every active NPC (~10-100) | ~5-30 KB |
| Weather connect-replay | current WeatherState | 48 B |
| ItemActivate connect-replay | per-peer item state | small |

Aggregate connect-edge burst: ~200 KB - 5 MB depending on save size and
prop count. Drain at LAN ~1000 msg/s × ~200 B/msg = 200 KB/s → 1 s to
25 s depending on save. **Confirms the bulk-priority lane requirement.**

---

## 8. Summary table — counts driving the architectural decision

| Question | Answer |
|---|---|
| Priority classes needed | **3** (HIGH / NORMAL / BULK) |
| Reliability modes needed | **4** (Unreliable, Unreliable-Sequenced, Reliable-Ordered, Reliable-Sequenced/newest-wins) |
| Ordering streams needed | **~8** named + per-key dynamic substreams (PropPose, TerminalInput, EntityPose) |
| Max concurrent in-flight reliable | **1 today; design for 16-32 when WAN matters** |
| Max peers | **4** (host + 3 clients) |
| MTU | **bump 256 B → 1200 B** to unblock EntityPoseBatch + voice + save chunks |
| Bandwidth budget | **~300-400 KB/s steady, ~1.4 MB/s peak during connect** (LAN-only) |
| Hardest feature to model | **Voice chat** (per-sender newest-wins unreliable substreams + jitter buffer) — followed by SaveSnapshot bootstrap (chunked resumable bulk) |
| Features the wire MUST distinguish today | TeleportClient (HIGH) vs PropSpawn snapshot (BULK) — root cause of the F4-10-press bug |

---

## 9. "What we need from a wire layer" — one-paragraph spec

The VOTV coop wire layer must support a single UDP socket multiplexing
(a) one **unreliable** stream per logical sender (PoseSnapshot, PropPose,
EntityPoseBatch, future VoiceFrame) with newest-wins sequenced drop at
the receiver; (b) a **reliable** transport with three priority classes
(HIGH interactive, NORMAL gameplay, BULK background) drained
weighted-round-robin so a snapshot bootstrap cannot starve a teleport
hotkey; (c) **~8 named ordering streams** plus dynamic per-key
substreams, so per-prop / per-terminal / per-NPC FIFO is independent of
global reliable order; (d) a fourth **reliable-sequenced (newest-wins)**
mode for host-broadcast world flags like WeatherState; (e) **per-peer
endpoints + fan-out** for 4-peer scale (host→all currently means host→1);
(f) **MTU ≥ 1200 B** to fit aggregated NPC pose batches; (g) **chunked
reassembly with resume** for the save-snapshot bootstrap; (h) a
**sliding-window sender** designed for N in-flight (ship 1, scale to ~16
when WAN matters). Latency-critical interactive (HIGH) packets must
land in ≤ 1 RTT under sustained BULK traffic. The layer remains
**LAN-first** (no congestion control beyond pacing, no NAT traversal);
WAN is a Phase 7+ concern that wants a sliding window + a master-server
relay path, neither of which the current scope requires.
