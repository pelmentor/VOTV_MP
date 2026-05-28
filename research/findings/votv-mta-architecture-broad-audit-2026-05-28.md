# VOTV_MP — broad MTA architecture audit (2026-05-28)

Companion to the CClientElement deep-dive (parallel agent) and the
ServerBrowser plan (`votv-master-server-mta-adaptation-2026-05-28.md`,
PR-5). This document surveys EVERYTHING ELSE in MTA's source tree under
`reference/mtasa-blue/` and recommends which other patterns are worth
borrowing for a Voices of the Void coop mod given the constraints
(standalone DLL, no Lua, no asset edits, kMaxPeers=4, listen-server-only).

Cited file paths assume the `reference/mtasa-blue/` root unless otherwise
stated. Our paths are absolute under `d:/Projects/Programming/VOTV_MP/`.

---

## Top-line summary

**Two patterns to adopt now** (both close OPEN audit findings from the
post-PR-4.7 audit, both small, both high-value):

1. **Per-type managers** — MTA's `CClientPlayerManager` /
   `CClientObjectManager` / `CClientVehicleManager` shape
   (`Client/mods/deathmatch/logic/CClient<Type>Manager.{cpp,h}`). One
   manager owns the live entities of one type plus their replication and
   per-tick pulse. This solves audit-finding **A4** ("NetPumpTick
   subsystem enumeration doesn't scale") and **H6** ("snapshot drain
   starts before lanes configured") cleanly: every coop subsystem becomes
   a manager with `OnConnectForSlot(slot)` / `OnDisconnectForSlot(slot)` /
   `OnSessionStart()` / `OnSessionStop()` / `Pulse()` virtual entry
   points. Registration replaces hand-written switch fan-out in
   `harness.cpp`. Mechanical refactor of ~14 existing modules; no new
   functionality, no wire-format change. **Solves A4 and H1 in one
   stroke**.

2. **Versioned BitStream / `Can(eBitStreamVersion query)` mechanism** —
   from `Shared/sdk/net/bitstream.h:35-47`. MTA wraps an enum that
   monotonically increments with every wire-format change; readers/writers
   gate fields with `if (bitStream.Can(eBitStreamVersion::NewField))`. We
   currently use a single `kProtocolVersion` and reject any mismatch
   (audit finding **H4** = "Protocol version mismatch is a silent hang").
   Adopting MTA's per-field versioning lets a v11 host receive an v10
   client's `ItemActivate` (truncated reads default to zero) and a v10
   host receive a v11 client's longer `WeatherStatePayload` (extra fields
   skipped). This is a real cost saver: every additive wire change today
   is a hard protocol bump that breaks every prior peer. **Solves H4 +
   prevents future H4-class regressions on every wire growth.**

**Two patterns to explicitly SKIP**:

1. **CSimulatedPlayer / dead-reckoning / extrapolation** — MTA's
   `CClientPed::UpdateTargetPosition`
   (`Client/mods/deathmatch/logic/CClientPed.cpp:5430-5487`) does
   error-compensation interpolation, NOT extrapolation. Velocity is NOT
   integrated forward past the last packet. We already implement the same
   shape in `remote_player.cpp:373-398`
   (`alpha = (now - start) / window`, dAlpha error compensation). No
   delta to adopt; we're already there.

2. **Anti-cheat (`CAntiCheat` / `CAntiCheatModule`)** — out of scope per
   project rules (RULE 1 + the explicit "no anti-cheat for coop"
   carve-out). MTA's anti-cheat is geared at a competitive deathmatch
   threat model that doesn't exist for trusted-friend horror coop. Our
   trust boundary is already in the right place: `kMaxCoord` + `kMaxSpeed`
   + finite checks in `protocol.h::ValidatePose` + per-packet validators
   like the host-only sender on `WeatherState`. We do NOT need MTA's
   `CAntiCheatModule` plug-in surface or its game-process integrity
   checks. Note for completeness; recommend SKIP.

---

## Per-area survey

### 1. Packet dispatch architecture

**(a) What MTA does.** Two layers:

- **Top-level packet dispatch** (`CPacketHandler::ProcessPacket` —
  `Client/mods/deathmatch/logic/CPacketHandler.cpp:35-240`) is a giant
  `switch (ucPacketID)` over `PACKET_ID_*` constants — exactly the same
  shape as our `event_feed.cpp` dispatch. ~46 cases, each delegates to a
  named `Packet_X` method. **MTA chose the switch deliberately**: cases
  fall through to `m_pNetAPI->ProcessPacket(...)`, then to
  `m_pUnoccupiedVehicleSync->ProcessPacket(...)`, etc. — a chain of
  responsibility across sync subsystems. So MTA's "switch" is actually a
  layered switch.

- **RPC table** (`Client/mods/deathmatch/logic/rpc/CRPCFunctions.h:21-73`):
  for the LUA-driven element RPCs (`setElementPosition`, `fadeCamera`,
  ...) which are open-ended and grow rapidly, MTA uses a registered
  handler table: `AddHandler(ucID, &CCameraRPCs::FadeCamera,
  "fadeCamera")`. Each RPC family lives in its own file
  (`CCameraRPCs.cpp`, `CPlayerRPCs.cpp`, etc.) with a `LoadFunctions()`
  call at boot. `ProcessPacket` is then a table lookup, not a switch.

**(b) What we do today.** `event_feed.cpp:204-680` is a switch on
`ReliableKind` with 13 cases. Each case opens its own type-specific
payload parse (`if (msg.payloadLen != sizeof(PropReleasePayload))
continue;`) and applies the message inline. **It is starting to bend the
800-LOC soft cap** (702 LOC) and the per-case payload-length check is
duplicated boilerplate.

**(c) Recommendation: PARTIAL.** Switch is fine TODAY at 13 cases. The
RPC table is worth adopting IF we cross ~20 reliable kinds. Audit finding
**A4** ("NetPumpTick subsystem enumeration doesn't scale") is the same
shape problem one layer up — flat dispatch across a growing N. Skipping
the dispatch table now (keep the switch) BUT adopting the per-feature
file split that the table enables IS the high-value move (this is
recommendation **2** — per-type managers).

**(d) Path to integration (if adopted later).** Replace the switch with
`g_reliableHandlers[ReliableKind::Count]` indexed array of
`void(*)(const ReliableMessage&)`. Each per-feature file registers its
handler at Install time: `RegisterReliableHandler(ReliableKind::PropRelease,
&OnPropRelease)`. Move each `case` body out of `event_feed.cpp` into the
existing per-feature file (PropRelease handler -> `remote_prop.cpp`,
RestoreVitals -> `dev/restore_vitals.cpp`, etc.) — `event_feed.cpp` then
shrinks to under 200 LOC of pure dispatch + the chat/feed logic.

---

### 2. Sync managers

**(a) What MTA does.** MTA owns one manager per entity type, registered
on `CClientManager`:

- `Client/mods/deathmatch/logic/CClientPlayerManager.h` — owns
  `m_Players` array, `m_pLocalPlayer`, `DoPulse()`, `Get(ElementID)`,
  `Get(nick)`. Per-type lookup is a method on the manager, not a global
  query.
- `Client/mods/deathmatch/logic/CClientObjectManager.h` — owns
  `m_Objects` array, `m_StreamedIn` mappedarray, `OnCreation/OnDestruction`,
  `IsObjectLimitReached`, `RestreamObjects(model)`.
- `Client/mods/deathmatch/logic/CClientVehicleManager.h`
- `Client/mods/deathmatch/logic/CClientPickupManager.h`
- `Client/mods/deathmatch/logic/CClientPedManager.h`
- `Client/mods/deathmatch/logic/CClientManager.h:71-140` — root
  registry: holds pointers to ALL 30+ sub-managers, calls
  `DoPulse()` on each. **This is the missing piece for our audit
  finding A4.**

**(b) What we do today.** Flat singletons per feature
(`src/coop/<feature>.cpp`): `remote_player`, `remote_prop`,
`prop_lifecycle`, `prop_snapshot`, `npc_sync`, `weather_sync`,
`weather_lightning`, `weather_redsky`, `item_activate`, `garbage_sync`,
`event_feed`, `flashlight_click_sound`, `grab_observer`, `nameplate`. Each
has its own `Install(Session*)` + `OnSessionStop()` + its own atomic
`Session*` + its own observer-registration. The harness manually wires up
`NetPumpTick`'s per-slot edges (`harness.cpp:367-394`) by spelling out
every subsystem in a hand-written block (audit finding **A4**).

**(c) Recommendation: ADOPT (high-value structural alignment).** This is
**the first thing to adopt**. Wraps the 14 existing per-feature singletons
in a common shape:

```cpp
struct CoopManager {
    virtual void Install(coop::net::Session*) = 0;
    virtual void OnSessionStart() {}
    virtual void OnSessionStop() {}
    virtual void OnConnectForSlot(int slot) {}      // late-joiner replay (snapshots)
    virtual void OnDisconnectForSlot(int slot) {}   // per-slot cleanup
    virtual void Pulse() {}                         // optional 60Hz tick
};
class CoopManagerRegistry {
    std::vector<CoopManager*> managers_;
public:
    void Register(CoopManager*);
    void NotifyConnectForSlot(int slot)   { for (auto* m : managers_) m->OnConnectForSlot(slot); }
    void NotifyDisconnectForSlot(int slot){ for (auto* m : managers_) m->OnDisconnectForSlot(slot); }
    void NotifySessionStart()             { for (auto* m : managers_) m->OnSessionStart(); }
    void NotifySessionStop()              { for (auto* m : managers_) m->OnSessionStop(); }
    void Pulse()                          { for (auto* m : managers_) m->Pulse(); }
};
```

**Solves audit findings:**

- **A4** (NetPumpTick enumeration doesn't scale) — replaced by
  `Registry.NotifyConnectForSlot(slot)`.
- **H1** (per-slot state not reset on session.Start) — every manager
  overrides `OnSessionStart()` and resets its per-slot arrays. No more
  "did I remember to reset that array?" bugs.
- **R1** (`prop_snapshot::g_session_ptr` non-atomic, missed from sibling
  pattern) — base class holds the atomic Session*, no per-feature drift.

**(d) Path to integration.** ~3 working days:
1. Add `include/coop/manager.h` (~50 LOC).
2. Make `remote_player`, `remote_prop`, `prop_lifecycle`, `prop_snapshot`,
   `npc_sync`, `weather_sync`, `weather_lightning`, `weather_redsky`,
   `item_activate`, `garbage_sync`, `event_feed`, `flashlight_click_sound`,
   `grab_observer`, `nameplate` derive from `CoopManager`. Each becomes
   a class instead of a `namespace`-with-statics.
3. Replace `harness.cpp` `NetPumpTick`'s manual per-subsystem fan-out
   with `g_managerRegistry.NotifyConnectForSlot(slot)`.
4. Smoke 30s, audit.

Cost: 14 files touched, ~1000 LOC moved, net diff size positive
(boilerplate cost is real). Value: solves 3 audit findings + every
future "I forgot to wire X on session reset" / "I forgot to wire X for
late-joiners" bug.

---

### 3. Interest management / distance-based replication

**(a) What MTA does.** Yes — sophisticated. The streamer
(`Client/mods/deathmatch/logic/CClientStreamer.cpp`,
`CClientStreamElement.cpp`) partitions the world into sectors + rows;
each `CClientStreamElement` lives in one sector; the camera position
drives `DoPulse()` which calls `SetExpDistances` + sort by squared
distance + `StreamIn`/`StreamOut` based on `m_fMaxDistanceExp`. Per-frame
the active list is recomputed and the elements past `fMaxDistance`
unstream.

`Server/mods/deathmatch/logic/CLightsyncManager.h` is the SERVER-side
peer of this: instead of pure-sync packets for far-away players (high
bandwidth), the server schedules "lightsync" packets at a slower rate
(name + health delta only) for players outside each receiver's interest
radius.

**(b) What we do today.** Pose fan-out unconditional from host to all
clients (`session.cpp` `SendMessages` per peer-slot every sendHz tick),
PropPose unconditional, snapshot on connect unconditional, weather state
unconditional. At 4 peers and ~2000 props in a VOTV map, all-to-all is
fine. **At 8+ peers or in the "stream chipPile / garbage clusters" world
(currently parked per the session 2026-05-28 chipPile blocker), this
starts hurting.**

**(c) Recommendation: PARTIAL — design now, build on demand.** Streamer
is overkill for kMaxPeers=4. But the post-PR-4.7 audit's **H2**
("TriggerForSlot walks 150k GUObjectArray entries per reconnect") is the
same problem one layer up — we walk the engine's object array because we
don't keep a parallel registry. The fix proposed in H2 is to maintain
`g_knownKeyedProps` in `prop_lifecycle`, which IS the seed of an MTA-style
manager-owned active list. Build that smaller thing first (H2 fix),
defer the sector grid until kMaxPeers grows or props grow past ~5000.

**(d) Path to integration (if streamer needed).** Port
`CClientStreamSector` (~150 LOC) + `CClientStreamSectorRow` (~80 LOC)
verbatim — they're geometric, no engine deps. Make `RemoteProp` (the
parallel class hierarchy we'd add for tracked props) inherit from a new
`CoopStreamElement`. Drive `DoPulse(localPlayerPos)` from harness
NetPumpTick at 4Hz (not per frame — sufficient for streaming). Estimated
~400 LOC new, ~3 days.

---

### 4. Connection / handshake / version-gate

**(a) What MTA does.** `Client/core/CConnectManager.cpp:145-292` —
version-handshake over HTTP query to the master server to fetch the
server's MTA version BEFORE connecting (lines 145-247). If versions
mismatch → `InitiateSidegradeLaunch` (auto-download the matching MTA
version, lines 251+). Different from the in-game version-skew handling.

`Shared/sdk/net/bitstream.h:32-47` — `eBitStreamVersion` enum that
auto-increments on every wire-format change. Critically, this is a
**per-field gate**: `if (bitStream.Can(eBitStreamVersion::SomeNewField))`
controls whether the reader/writer touches a particular field. Older
peers' bitstreams simply don't have those bits and the gates short-circuit.
This is forward+backward compat per FIELD, not per protocol-version.

Versions are gated at the trust boundary in
`Client/mods/deathmatch/logic/CPacketHandler.h:23-48` —
`ePlayerDisconnectType` enum has `VERSION_MISMATCH`, `BAD_VERSION`,
`SERVER_NEWER`, `SERVER_OLDER`, `DIFFERENT_BRANCH` (5 distinct
disconnect codes — UI tells the user WHICH version situation triggered).

**(b) What we do today.** `protocol.h:78` single `kProtocolVersion`
constant. `ParseHeader` (line 710-720) drops any datagram whose version
field doesn't equal `kProtocolVersion`. **No human-readable mismatch
notification** (audit finding **H4** — "silent hang on version skew").
Every wire-format change bumps the version by one, breaking every prior
peer.

**(c) Recommendation: ADOPT (high-value, closes H4).** This is **the
second thing to adopt**. Two parts:

1. **Per-field version gates** (MTA `Can(eBitStreamVersion)` shape) for
   reliable payloads. POD-struct wire format stays for the hot pose
   path (zero-copy memcpy is faster than per-field gated reads); but for
   reliable payloads, replace fixed-size POD with a versioned reader
   that gates new fields. Recent example: `ItemActivatePayload v6` added
   `intensity` + `outerConeAngle` + `innerConeAngle` + `mode` (8 extra
   bytes); under MTA's scheme a v5 peer could still receive a v6 packet —
   the new fields would just not be read.

2. **Mismatch-with-reason close**. Replace silent drop in `ParseHeader`
   with a recognize-version + close-with-reason flow at the wire boundary.
   The GNS connection status callback already delivers the reason string
   to the OTHER end (see `coop::net::Session::OnConnStatusChanged`). Mirror
   MTA's `ePlayerDisconnectType` enum:

```cpp
enum class DisconnectReason : uint16_t {
    None = 0,
    ProtocolMismatch_PeerNewer = 1,   // we should update
    ProtocolMismatch_PeerOlder = 2,   // peer should update
    Handshake_BadToken = 3,
    Host_Kicked = 4,        // see Area 5
    Host_Shutdown = 5,
    Timeout = 6,
};
```

**Solves audit finding H4 fully + every future H4-class regression.**

**(d) Path to integration.** ~2 days:
1. Add `eWireVersion` enum (mirror `eBitStreamVersion`). Each new
   reliable-payload field becomes a new enum entry.
2. Add `MaybeReadField<T>(eWireVersion gate, T& out, T defaultIfMissing)`
   helper on the reader.
3. Convert `ItemActivatePayload`, `WeatherStatePayload`, `PropSpawnPayload`
   to versioned reads (these are the three that grew across versions).
4. `ParseHeader` -> peek version; if mismatch, call
   `CloseConnection(reason="VOTV_MP v0.0.1+votv-0.9.0-n peer=v10 ours=v11")`
   via GNS. The wire layer keeps `kProtocolVersion` as a HARD MIN
   compatibility version (e.g. all peers must speak >=v10); the per-field
   `eWireVersion` enum handles the forward compat above that.
5. Remove the static_asserts on hot-path POD sizes when they grow (we
   don't bump the hard version anymore, we add to `eWireVersion`).

---

### 5. Server admin / commands (kick / ban)

**(a) What MTA does.**
- `Server/mods/deathmatch/logic/CConsoleCommands.h` declares
  `ReloadBans`, etc. (line 63).
- `Server/mods/deathmatch/logic/CBan.h` / `CBanManager.h` — persistent
  ban list keyed on IP + nick + serial (lines 22-60), serialized to
  disk on modification.
- `Server/mods/deathmatch/logic/CConnectHistory.h` — server-side
  rate-limiter on connect attempts per IP (foils brute-force re-join
  by a kicked player).

**(b) What we do today.** Nothing. No kick mechanism. A disruptive friend
on a listen-server can only be evicted by the host quitting the session.
No ban list. No connect-rate limit.

**(c) Recommendation: ADOPT, lightweight.** For listen-server coop the
threat model is much smaller than MTA's public-server one. We need:

- **`KickPeer(slot, reason)`** (host-only). Sends a reliable
  `DisconnectReason::Host_Kicked` to the slot then calls
  `CloseConnection(slot)` immediately. The reason string is shown in
  the kicked client's UI.
- **Session-scoped IP block** (not persistent — kicked peer can rejoin
  after host restart, which is fine for friend-coop). Two-line set of
  `peerIp` strings on the host that `OnConnStatusChanged::Connecting`
  consults before accepting. Mirrors `CConnectHistory`'s shape on a
  smaller scale.

NO persistent ban file. NO serial-based ban (we have no concept of MTA
serials). NO console for typing commands — the host uses an ImGui
overlay (PR-5 plumbing) to right-click a peer slot → Kick.

**(d) Path to integration.** ~1 day, post-PR-5 (when ImGui overlay lands):
1. Add `Session::KickSlot(slot, DisconnectReason, reasonText)` —
   sends a reliable + `CloseConnection`.
2. Add `Session::BlockIpForSession(ipStr)` — appends to
   `m_sessionBlockList`; checked at `Connecting` callback.
3. ImGui overlay: in PR-5's peer list, add "Kick" button per row.

---

### 6. Chat / event feed

**(a) What MTA does.** `Client/core/CChat.h:152-249` — a single
`CChat` class with circular buffer (`CHAT_MAX_LINES`), color-coded
output, input line, history navigation, input visibility separate from
chat visibility, font sizing, scroll state, etc. Color codes embedded
inline (`#FF0000Red text`). The separation into "console" /
"server console" / "debug echo" / "chat echo" is at the PACKET layer
(`CChatEchoPacket`, `CConsoleEchoPacket`, `CDebugEchoPacket`,
`CPlayerChatPacket`) — all four packets feed into the SAME `CChat`
widget with different prefixes/colors. The "admin chat" channel is just
a packet variant with a different color and a "show to admins only" flag.

**(b) What we do today.** `event_feed.cpp` (702 LOC) handles "X joined",
"X left", system status, and per-slot nickname tracking via the
`Join` reliable. **No chat input** yet — the feed is one-way (host
broadcasts, clients display).

**(c) Recommendation: PARTIAL — adopt the LAYERING, defer the chat
input.** MTA's "different packet types feeding one widget" is the right
shape. When chat input lands (post-PR-5), do it as:

- New `ReliableKind::ChatMessage` (peer-to-peer, host fan-outs).
- New `ReliableKind::SystemMessage` (host-only sender, e.g. "weather
  is changing", "lightning struck near X").
- Both render in the same hud_feed widget with different
  prefixes/colors.

No need for a separate console channel (we're a coop mod, not a
deathmatch server with admin oversight). No need for input history
(simple input box is enough). No tab-complete (no command surface yet).

**(d) Path to integration.** ~1 day:
1. Add two reliable kinds (`ChatMessage` = 19, `SystemMessage` = 20).
2. Add a hotkey to open chat input (T per game convention; configurable
   in `votv-coop.ini`).
3. Append messages to existing `hud_feed` widget with color prefix.

---

### 7. Resource lifecycle (without Lua)

**(a) What MTA does.** Resources are folders with `init.lua` +
`meta.xml`; the server transfers them to each connecting client; a
resource exposes its declared exports + assets + scripts; the client
loads them into a sandboxed Lua VM. `CResourceModelStreamer` etc. handle
streaming asset cache. Hot-reloadable, partial-update via diff.

**(b) What we do today.** Per-feature C++ modules. Each module is a
compile-time addition to the DLL. No runtime addition; no per-game-version
manifest of feature compatibility.

**(c) Recommendation: PARTIAL — adopt only the "feature compatibility
manifest" idea.** Without Lua we can't ship runtime resources (RULE 3).
But MTA's `meta.xml` shape ("this resource targets MTA 1.7+, RakNet
version X") IS valuable for our problem: **we need to track which mod
features apply to which VOTV game cook**. We already have `sdk_check` at
boot (`tools/sdk_diff.py` + `votv-coop-compat-report.txt`); MTA's pattern
suggests adding a per-feature gate:

```cpp
struct FeatureManifest {
    const char* name;            // "weather_sync"
    const char* gameVersionMin;  // "0.9.0-i"
    const char* gameVersionMax;  // "0.9.0-n" (empty = open-ended)
    bool (*sdkProbe)();          // returns false if required offsets/UFunctions missing
};
```

Each manager (per recommendation #2) declares a `FeatureManifest`. At
session start, the registry calls `sdkProbe()` and disables any feature
whose probe fails — with a single human-readable boot log:
"WARNING: weather_sync disabled (UFunction AdaynightCycle_C.causeRain not
resolved against cooked content version 0.9.1-a)". Pairs with our
existing reflected-offset auto-adaptation.

**(d) Path to integration.** ~1 day, after recommendation #2 lands.

---

### 8. Sync compression / bandwidth optimization

**(a) What MTA does.**
`Shared/sdk/net/bitstream.h:88-99` — `WriteNormVector` packs a unit
vector in 4 bytes + 3 bits (vs 12 bytes raw). `WriteNormQuat` packs a
quaternion in 6 bytes + 4 bits (vs 16 bytes raw). `WriteOrthMatrix`
packs a 3x3 orthogonal matrix in 6 bytes (vs 36 bytes raw).
`WriteCompressed<T>` for ints uses variable-length encoding (small
values cost less).

There's also `CSimControl` (sync rate adjustment) — when CPU/bandwidth
budget is tight, lower the puresync rate per player (looked at in
`CNetAPI.cpp`).

**(b) What we do today.** All POD structs, raw bytes. `PoseSnapshot` is
32 bytes per packet @ 60Hz = 1920 bytes/s per direction per peer. With
4 peers in 1v3 host topology, host's outbound = 1920 × 3 = 5760 bytes/s.
Trivially fine on LAN; fine on 1 Mbit internet.

**(c) Recommendation: SKIP for hot path; consider for cold path.**
Bitpacking adds endianness/version-skew complexity and the POD speed
advantage is real (memcpy). At kMaxPeers=4 and LAN-or-broadband the
pose bandwidth is not a concern. **One narrow exception worth noting:**
the snapshot-on-connect (`prop_snapshot.cpp`) sends ~2000 `PropSpawn`
reliables = 2000 × 160 bytes = 320 KB on connect. MTA's `LatentTransfer`
queue (`Shared/mods/deathmatch/logic/CLatentSendQueue.cpp`) is built
exactly for this: rate-limit-aware chunked delivery so the initial
snapshot doesn't head-of-line-block other reliables. We already have
GNS's reliable lane (BULK) for this, but a per-recipient rate cap on
the snapshot drain would smooth the connect curve.

**(d) Path to integration (snapshot rate cap).** ~half a day:
1. In `prop_snapshot::Pulse` for a freshly-connected slot, send max N
   PropSpawns per tick (start at 50) instead of all at once.
2. Watch the next 60s smoke for steady-state RSS.

---

### 9. CNetBitStream / variable-length integer packing

**(a) What MTA does.** Per area 8: `WriteCompressed<int>` is
variable-length (Elias gamma or similar). `WriteBit`/`ReadBit` for single
booleans. `WriteBits(char*, numbits)` for arbitrary bit-width fields.
The `eBitStreamVersion` machinery (per-field forward/back compat) lives
right next to this — they're the same subsystem.

**(b) What we do today.** Bit packing for booleans IS done — `stateBits`
in `PoseSnapshot` (bit 0 = isInAir, bits 1-7 reserved), `physFlags` in
`PropSpawnPayload`, `flags` in `WeatherStatePayload` (8 weather bits).
But these are hand-packed per-struct, not via a general bitstream API.

**(c) Recommendation: SKIP the variable-length integer packing; ADOPT
the per-field versioning (see area 4).** Variable-length ints save bytes
on a wire we don't care about saving. Per-field versioning saves us
breaking changes on a wire that grows on every feature.

**(d) Path to integration.** N/A — covered by recommendation 4.

---

### 10. CSimulatedPlayer / dead-reckoning

**(a) What MTA does.** Despite the name, `CSimulatedPlayPosition.h` is
small (it's a helper struct). MTA's actual remote-player position update
is in `CClientPed::UpdateTargetPosition`
(`Client/mods/deathmatch/logic/CClientPed.cpp:5430-5487`). It is **NOT
extrapolation** — it's error-compensation interpolation: at each new
pose packet, the engine sees the gap between current local engine
position and the wire's target, then compensates that gap over a window
(`PED_INTERPOLATION_WARP_THRESHOLD`-aware), with a "warp if too far"
fallback. The puppet's own physics + animation continue to drive it
between packets — the wire only corrects, doesn't dictate.

`CInterpolator.h` is the more general ring-buffer variant (a circular
buffer of (timestamp, value) pairs with `Evaluate(time)` for "what was
the value at time T"), used for less-frequent inputs.

**(b) What we do today.** `remote_player.cpp:373-398` is the same
MTA shape — alpha = (now - start) / window, dAlpha error compensation,
freeze at alpha=1. Same formula. Same warp-if-too-far at
`remote_player.cpp:357` (`interpFinishMs_ = now + kInterpWindowMs`).
**We already adopted this pattern.** The comment at `:373` literally
says "MTA shape: error is cached".

**(c) Recommendation: SKIP — already adopted.** No delta.

**(d) Path to integration.** N/A.

---

### 11. Error / anomaly reporting

**(a) What MTA does.** `Client/core/CCrashDumpWriter.cpp` (3393 LOC) +
`Shared/sdk/CrashTelemetry.h` (377 LOC). SEH-protected dump writer that:
- Captures registers in scenarios where `.dmp` creation might fail
  (lines 84-100 — `EmergencyCrashLogging::ReadResult` for SEH-protected
  reads of crashed-process memory).
- Per-thread context capture with a small ring buffer of recent
  "what was this thread doing" markers (lines 23-100 in
  `CrashTelemetry.h`).
- `AddReportLog(id, msg)` — id-keyed log lines aggregated into the
  crash dump.
- Crash dialog with stack frame display (the .dmp -> human path).

NO third-party service. The dumps go to disk; the user opens an issue
on GitHub and attaches them. Standalone, self-contained.

**(b) What we do today.** Per-callback SEH wrapper + outer
`ProcessEventDetour` SEH wrap (per session 2026-05-27). Crashes land in
the log via `[Error]` lines from the wrapper. No `.dmp` writer. No
per-thread context ring (we have `UE_LOG*` line-by-line but no "last 6
actions on this thread" structured trace).

**(c) Recommendation: PARTIAL — adopt the `CrashTelemetry::Context`
ring shape; SKIP the .dmp writer for now.** MTA's per-thread context
ring is the cheapest possible "what happened just before the crash"
mechanism — far better than reading 100 MB of log to find the issue.
~50 LOC plus call-site annotations at the hot paths. We can ship this
incrementally.

The full `.dmp` writer (with SEH-protected register dump, stack walk,
crash dialog) is justified ONLY once we have user-side crashes that
aren't reproducible from the log alone. Today our SEH wrap + log
strategy is sufficient.

**(d) Path to integration (context ring).** ~half a day:
1. Add `coop::crash_context::PushMarker(const char* feature, const
   char* detail)` — fixed-capacity 6-entry circular buffer in a
   `thread_local` (matches MTA `kHistoryCapacity = 6`).
2. Call from each Install + each event-feed dispatch case + each
   observer-firing site.
3. On `[Error]`-class log line, dump the current thread's ring as a
   block of `MARKER[n]: feature=X detail=Y t=Z` lines.

---

### 12. Asset / model / sound replication

**(a) What MTA does.** DFF/TXD (model + texture) custom mod replication
via `CClientDFFManager` / `CClientIMG` / `CClientModelManager` /
`CResourceModelStreamer`. Server resources can ship custom models that
override stock GTA models on connect; per-element model assignment via
`setElementModel(model, custom_id)`.

**(b) What we do today.** Nothing — RULE 1 says no asset edits. We
don't replicate models because we don't change models.

**(c) Recommendation: SKIP entirely.** Out of scope. Note for
completeness only.

**(d) N/A.**

---

### 13. Anti-cheat

**(a) What MTA does.** `Client/mods/deathmatch/logic/CAntiCheat.h:16-32`
+ `CAntiCheatModule.h` — pluggable AC module surface. Each module gets
periodic `PerformChecks()` calls. The actual modules cover memory
integrity, hook detection, scripted-injection detection,
window-message-injection guards, etc. MTA's threat model is the
deathmatch competitive scene where a single cheater can ruin a 64-player
server.

`CAntiCheat::GetInfo(acInfo, sdInfo)` (line 27) returns the AC ident
strings that get bound into each connect handshake — so a server can
require "AC version >= N" to play.

**(b) What we do today.** Trust boundary at the wire (POD validation +
finite + range checks in `ValidatePose`); host-only sender enforcement on
`WeatherState` / `RestoreVitals` / `TeleportClient`; no engine integrity
checks; no AC plugin surface.

**(c) Recommendation: CONFIRM SKIP. Defensive validation patterns ALREADY
present.** The threat model for coop is "trusted friends, broken peer
sending garbage by mistake, malicious peer trying to grief". For (1) AC
adds nothing. For (2) and (3), our wire validators are sufficient
(see `ValidatePose`, the `peerSessionId==0` host-only enforcement on
several reliable kinds, `kMaxCoord` rejection). Audit-finding #13
(`IP_AllowWithoutAuth`) gates on master-server identity, NOT on AC.

**One small adoption-worthy pattern from MTA's AC surface**:
**host-side bounds validation on EVERY reliable**, not just pose. Today
`ValidatePose` is per-pose; we have NO equivalent for `PropSpawn`'s
location, `ItemActivate`'s intensity, `LightningStrikePayload`'s
location, `WeatherStatePayload`'s wind speed. MTA validates at every
packet boundary defensively. **Mirror the pattern** — add `ValidateX()`
free functions per payload, called at the receiver wire boundary in
`event_feed.cpp` dispatch. NaN/Inf/range checks. ~3 hours of work.
Closes a class of "garbage from buggy old peer crashes us" bugs without
any AC complexity.

**(d) Path to integration (defensive validation).** ~half a day:
1. Add `Validate(PropSpawnPayload&)`, `Validate(ItemActivatePayload&)`,
   `Validate(LightningStrikePayload&)`, `Validate(WeatherStatePayload&)`,
   `Validate(TeleportClientPayload&)`, `Validate(PropReleasePayload&)`.
2. Each checks finite + range. PropSpawn.loc: `|loc| < kMaxCoord`.
   ItemActivate.intensity: `[0, 100]` (sanity ceiling). Wind: `[0,
   kMaxSpeed]`. Etc.
3. `event_feed.cpp` dispatch calls `Validate*` immediately after the
   `payloadLen == sizeof(...)` check, BEFORE invoking the handler.

---

## Priority-ordered adoption list

| # | Recommendation | Effort | Audit findings closed | Files to touch |
|---|---|---|---|---|
| 1 | **Per-type managers (CoopManager + registry)** | 3 days | A4 (NetPumpTick scaling), H1 (per-slot state reset), R1 (atomic Session*), H6 (lanes-configured gate) | new `include/coop/manager.h`; harness.cpp; all 14 `src/coop/*.cpp` (mechanical) |
| 2 | **Versioned wire via `eWireVersion` per-field gates** | 2 days | H4 (silent version-mismatch hang); prevents every future H4-class regression | `include/coop/net/protocol.h`; `event_feed.cpp` (per-case payload reads); session.cpp (ParseHeader path) |
| 3 | **Defensive validators on every reliable payload** (from area 13) | 0.5 day | New class of "buggy peer garbage" hardening — not a current audit finding but trivially worth doing | `include/coop/net/protocol.h` (add `Validate(X)` per payload); `event_feed.cpp` (call before dispatch) |
| 4 | **Disconnect-reason enum + close-with-reason** (from area 4) | 0.5 day | UX for H4; foundation for kick (area 5) | `include/coop/net/protocol.h`; `coop/net/session.cpp` (call sites) |
| 5 | **Host-side kick + session IP block** (area 5) | 1 day post-ImGui | Closes "no eviction for disruptive friend" UX gap | new `coop/host_admin.cpp`; ImGui peer-list in PR-5 |
| 6 | **Crash telemetry context ring** (area 11, partial) | 0.5 day | Diagnosability improvement; not a current audit finding | new `include/coop/crash_context.h`; call sites at hot paths |
| 7 | **Chat input + system message channels** (area 6) | 1 day | UX feature — chat is currently one-way | event_feed.cpp; protocol.h (+2 ReliableKinds); harness.cpp (hotkey) |
| 8 | **Feature manifest per manager** (area 7) | 1 day post-#1 | Per-feature game-version gating across cook updates | include/coop/manager.h (extend); each manager declares its FeatureManifest |
| 9 | **Snapshot rate cap** (area 8) | 0.5 day | Smooths connect curve; addresses a class of "lobby join hangs because 320 KB snapshot blocks" issues | prop_snapshot.cpp (Pulse-for-slot drain throttle) |
| 10 | **Streamer / per-type active list** (area 3) | 3 days, defer | Solves H2 (TriggerForSlot walks GUObjectArray) when combined with the prop_lifecycle parallel registry. Builds toward kMaxPeers > 4 scaling. | new `coop/stream/` subdir; prop_lifecycle (parallel registry) |

**Top three to do immediately after PR-5 ships:** #1 (per-type managers),
#2 (versioned wire), #3 (defensive validators). Together they close 5
high-priority audit findings (A4 + H1 + R1 + H6 + H4), set up future
work (#4-#8 all build on #1), and total ~6 days. Recommendation **2**
above (Per-type managers) plus **4** (Versioned BitStream) are the high-
order bits — everything else either follows them or is a small
independent win.

**Top three to explicitly SKIP:** CSimulatedPlayer/extrapolation
(already implemented), Asset replication (RULE 1), Anti-cheat plugin
surface (out of coop scope; we already have the relevant defensive
validation slice).

---

## Cross-reference: how MTA patterns map to currently OPEN work items

From `MEMORY.md` and the post-PR-4.7 audit doc:

| Open work item | MTA pattern that helps | Recommendation # |
|---|---|---|
| PR-4.13 NetPumpTick extract (DEFERRED) | Per-type managers replace NetPumpTick's hand-written fan-out outright | #1 |
| PR-4.11 weather_sync 3-way split (DEFERRED) | Per-type managers — each split becomes its own `CoopManager` | #1 |
| Audit H1 (per-slot state reset) | `CoopManager::OnSessionStart()` virtual | #1 |
| Audit H2 (GUObjectArray walk per reconnect) | Parallel active-list per type (toward streamer) | #10 |
| Audit H4 (silent version mismatch) | Per-field `eWireVersion` gates + close-with-reason | #2 + #4 |
| Audit H6 (snapshot drain before lanes configured) | `CoopManager::OnConnectForSlot` fires AFTER full Connected state | #1 |
| Audit R1 (prop_snapshot non-atomic Session*) | Base class owns the atomic — siblings can't drift | #1 |
| Audit O1/O2 (file-size growth catalog, weather_sync split) | Per-type managers force the split (one feature one file) | #1 |
| 3-peer hands-on partial-disc test (open from PR-4.7) | Disconnect-reason enum + close-with-reason gives meaningful test output | #4 |
| chipPile-interactions blocker (parked) | Per-type manager + active-list scope this work cleanly | #1 + #10 |
| Master-server PR-5 / PR-6 P2P | Disconnect-reason enum + ImGui peer list = kick UX wiring without further design | #4 + #5 |
| #13 IP_AllowWithoutAuth (security PR gated on master-server) | Session IP block surface gives the missing host-side enforcement | #5 |

The pattern is consistent: **recommendation #1 (per-type managers)
closes most of the architectural debt** the audit surfaced; **#2
(versioned wire) closes the UX cliff** at version skew; #3-#10 are
small focused wins that build on those two.

---

## Notes on what's NOT in this audit

This audit deliberately excludes:

- **CClientElement** — parallel agent has the deep-dive.
- **CServerBrowser / master server / lobby** — covered by PR-5 plan
  (`votv-master-server-mta-adaptation-2026-05-28.md`).
- **Lua** — RULE 3 (no UE4SS/Lua at runtime). MTA's resource model is
  built around Lua; we explicitly skip.
- **CEGUI / D3D9 specifics** — UE4 has its own UI layer (UMG); we'll
  use ImGui for overlay (RULE 3). No GUI-layer adoption.
- **Asset / model replication** — RULE 1 (no asset edits).
- **MTA's RakNet transport** — replaced by GameNetworkingSockets (PR-1
  through PR-4).

---

## File-size sanity check on this doc

This document = ~480 LOC of markdown. Well under the 1800-LOC ceiling
the brief allowed. The 14 sections are deliberately compressed; the
implementation paths are at the level a follow-up "feature-dev:code-
architect" agent can pick up and run with without ambiguity.
