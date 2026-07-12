# Valve GameNetworkingSockets (GNS) — concrete integration plan

**Date**: 2026-05-27
**Author**: research agent (post-decision; user picked GNS as the wire layer)
**Status**: opinionated; conditionally-recommended (see §11 + risks)

This document is the concrete integration plan for vendoring Valve's
[GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets)
(BSD-3, "GNS" hereafter) as the wire layer for the VOTV coop mod,
**statically linked into `votv-coop.dll`** so the shipped product is
still a single DLL the user drops in. No external runtime dependencies,
per RULE №3.

The sibling agent's build-vs-vendor report (`votv-network-build-vs-vendor-2026-05-27.md`)
recommended **hand-rolling** over GNS specifically because of GNS's
Protobuf + OpenSSL deps. The user overrode and picked GNS. This plan
treats that as the decision and lays out *how* to make it work cleanly
— including the points at which the integration cost makes the
hand-roll alternative still the right escape hatch (§11).

---

## §1. License sanity check

**Verified**: GNS is **BSD-3-Clause** (`Copyright (c) 2018, Valve
Corporation`, the standard 3-clause BSD text reproduced verbatim at
<https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/LICENSE>).

Compatibility with our shipping:

- We can vendor + modify the source, link statically into
  `votv-coop.dll`, ship binary-only, no source disclosure required.
- The 3rd clause ("Neither the name of the copyright holder nor the
  names of its contributors may be used to endorse or promote products
  derived from this software without specific prior written permission")
  bars us from advertising VOTV_MP as "Valve approved" or similar.
  We don't do that. Fine.
- Attribution: we must reproduce the BSD-3 notice somewhere in our
  distribution. Existing convention in our repo: a `NOTICE`/`LICENSE`
  file (we already attribute MinHook; we add GNS to the same list).
- Binary form attribution: clause 2 ("Redistributions in binary form
  must reproduce the above copyright notice ... in the documentation
  and/or other materials provided with the distribution"). Reproducing
  the notice in the repo's `LICENSE-THIRD-PARTY.md` satisfies this.

**Transitive GPL/LGPL risk**: GNS's three crypto backends are
**OpenSSL** (Apache-2.0 since 3.0; previously dual-license OpenSSL +
SSLeay, both permissive), **libsodium** (ISC), and **Windows BCrypt
via bcrypt.lib** (Microsoft Windows SDK; no separate license — part
of the platform). The optional NAT-punching layer USE_STEAMWEBRTC
pulls Google WebRTC (BSD-3 itself but >100 transitive deps). We are
**not** enabling USE_STEAMWEBRTC (LAN-only — see §4).

**Protobuf** is BSD-3 from Google. Same compatibility as GNS itself.

**Verdict**: license is clean across all chosen sub-deps. No GPL
contamination. **Static linking is permitted.**

---

## §2. Build dependencies + how to handle each

GNS's `CMakeLists.txt` exposes (verified by reading the upstream
file at HEAD):

```cmake
option(BUILD_STATIC_LIB "Build the static link version of the client library" ON)
option(BUILD_SHARED_LIB "Build the shared library version of the client library" ON)
option(BUILD_EXAMPLES "Build the included examples" OFF)
option(BUILD_TESTS "Build crypto, pki and network connection tests" OFF)
option(BUILD_TOOLS "Build cert management tool" OFF)
option(ENABLE_ICE "Enable support for NAT-punched P2P connections using ICE protocol" ON)
option(USE_STEAMWEBRTC "Build Google's WebRTC library to get ICE support for P2P" OFF)
option(Protobuf_USE_STATIC_LIBS "Link with protobuf statically" OFF)
set(USE_CRYPTO "OpenSSL" CACHE STRING "Crypto library to use for AES/SHA256")
set_property(CACHE USE_CRYPTO PROPERTY STRINGS OpenSSL libsodium BCrypt)
# USE_CRYPTO25519 takes values: Reference, OpenSSL, libsodium
```

And it validates:

```cmake
if(USE_CRYPTO STREQUAL "BCrypt" AND NOT WIN32)
    message(FATAL_ERROR "USE_CRYPTO=\"BCrypt\" is only valid on Windows")
endif()
```

### 2.1 Crypto — `USE_CRYPTO=BCrypt` + `USE_CRYPTO25519=Reference`

This is the **only** crypto configuration we want. Reasoning:

- **OpenSSL**: would pull in ~3 MB of OpenSSL static libs. Heavy.
  Vendoring OpenSSL is doable (vcpkg) but adds a second 3rd-party
  forest to maintain. Worst option for binary size.
- **libsodium**: ~30k LOC, Apache-2.0, smaller than OpenSSL (~600 KB
  static). Reasonable fallback but still an external dep.
- **BCrypt** (Windows SDK `bcrypt.lib`): **zero new code**, just a
  link-time addition. AES-CTR + SHA-256 + HMAC come from the OS;
  link cost is negligible (a few KB stub).
- For curve25519 specifically, GNS bundles a **donna-style reference
  implementation** (`crypto_25519_donna.cpp` in `src/common/`).
  Setting `USE_CRYPTO25519=Reference` uses the vendored donna code —
  **no external crypto dep for the ed25519/x25519 signature + ECDH**.

**Combined**: `USE_CRYPTO=BCrypt` + `USE_CRYPTO25519=Reference` is the
Windows-only, **zero-external-crypto-lib** configuration. All
crypto requirements (AES/SHA from BCrypt; curve25519 from donna)
are satisfied without OpenSSL or libsodium in our tree.

CMake setting:

```cmake
set(USE_CRYPTO "BCrypt" CACHE STRING "" FORCE)
set(USE_CRYPTO25519 "Reference" CACHE STRING "" FORCE)
```

Plus `target_link_libraries(votv-coop PRIVATE bcrypt)` (already in
Windows SDK).

### 2.2 Protobuf — the gnarly dependency

**Status**: GNS uses protobuf-cpp for its handshake/messages wire
serialization (`src/common/steamnetworkingsockets_messages*.proto`).
The `src/common/` directory contains the **.proto files but not the
generated .pb.cc/.pb.h** — they are generated by `protoc` at build
time. Confirmed by listing `src/common/` (only the three `.proto` files
present; no `.pb.cc`).

This is the **hardest part of the integration**. Three sub-problems:

1. **Where does `protoc` come from?** GNS's CMake expects
   `find_package(Protobuf REQUIRED)`. On Windows, options:
   - **vcpkg manifest mode** — GNS's documented recommended path. We
     add a `vcpkg.json` declaring `protobuf` as a dep; CMake picks it
     up via the vcpkg toolchain. **Adds vcpkg as a build prerequisite**
     to our project. Not a runtime dep (vcpkg is build-time only) so
     RULE №3 is preserved. But CMakeLists complexity goes up.
   - **Vendor a known-good protoc binary** in `third_party/` —
     unportable across host machines; cross-compile/CI risk.
   - **Vendor protobuf-cpp source + cross-compile protoc** ourselves
     — most self-contained but adds ~200k LOC + a 20-second protoc
     build to every clean.

   **Recommendation**: vcpkg manifest mode. It is the standard MSVC
   path for protobuf on Windows in 2026, doesn't pollute the runtime,
   and a single `vcpkg.json` is half a dozen lines.

2. **Static vs dynamic protobuf?** `Protobuf_USE_STATIC_LIBS=ON` is
   the GNS flag. Set it to ON so libprotobuf.a/.lib links statically
   into our DLL. (Otherwise the user would need
   `libprotobuf.dll` on `PATH`, breaking RULE №3.)

3. **MSVC runtime matching.** Per the existing MinHook handling in
   our CMake, every static lib we link must use the same CRT
   (`MultiThreaded` static). vcpkg's default triplet is
   `x64-windows` (dynamic CRT) — wrong. We need
   `x64-windows-static` triplet so vcpkg builds protobuf with `/MT`.
   This is a one-line setting in `CMAKE_TOOLCHAIN_FILE` invocation:

   ```
   cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
   ```

4. **Linker symbol footprint**: protobuf-cpp is ~3 MB static lib
   uncompressed. After link-time dead-code elimination (the `/OPT:REF`
   `/OPT:ICF` we already have on MSVC) typical usage trims to
   ~600-900 KB in the final DLL — but the trim is highly dependent on
   how many protobuf features GNS uses. **Realistically expect
   ~800 KB added to our DLL from protobuf alone.**

### 2.3 WinSock2

GNS uses `ws2_32.lib` directly. We already link it. No change.

### 2.4 ICE / WebRTC

`ENABLE_ICE=ON` by default but defers to `USE_STEAMWEBRTC=OFF` (also
default). ICE without WebRTC is a no-op stub. **Keep both at defaults**
(`ENABLE_ICE=ON USE_STEAMWEBRTC=OFF`) — gives us the ICE API for
future P2P-over-NAT (Phase 7+ WAN) without pulling WebRTC's 100+
transitive deps now. (`ENABLE_ICE=OFF` would also work; the difference
is whether the ICE stub code compiles. Keep ON; zero binary cost.)

### 2.5 `BUILD_SHARED_LIB=OFF`, `BUILD_STATIC_LIB=ON`

We only want static. Saves build time and produces no extraneous
`GameNetworkingSockets.dll` next to ours.

### 2.6 `BUILD_TESTS=OFF BUILD_EXAMPLES=OFF BUILD_TOOLS=OFF`

Defaults. Already off.

### 2.7 Summary of CMake settings

```cmake
set(GAMENETWORKINGSOCKETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/GameNetworkingSockets)
set(BUILD_STATIC_LIB     ON  CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIB     OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES       OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS          OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS          OFF CACHE BOOL "" FORCE)
set(USE_CRYPTO           "BCrypt"    CACHE STRING "" FORCE)
set(USE_CRYPTO25519      "Reference" CACHE STRING "" FORCE)
set(Protobuf_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(ENABLE_ICE           ON  CACHE BOOL "" FORCE)
set(USE_STEAMWEBRTC      OFF CACHE BOOL "" FORCE)
add_subdirectory(${GAMENETWORKINGSOCKETS_DIR} EXCLUDE_FROM_ALL)
```

`EXCLUDE_FROM_ALL` so the GNS shared lib (if it ever gets accidentally
re-enabled) doesn't end up in our install set.

---

## §3. Can encryption be disabled?

**Answer**: NO. There is no `USE_CRYPTO=none` upstream flag. The
opensource version mandates AES-GCM + curve25519 ECDH on every
connection (the Steam protocol handshake from which GNS is derived
treats encryption as mandatory; the open-source build inherits this).

Searching the BUILDING.md text + the CMakeLists for any disable path
returned nothing. Some community forks (e.g.
<https://github.com/nillerusr/GameNetworkingSockets-disable-crypto>)
patch out the crypto block but none are mainstream and they fall
behind upstream within ~6 months. **We accept the encryption cost.**

Practical impact:

- **Connect-time latency**: GNS handshake is ~3 round-trips for
  ECDH + cert verify. LAN RTT 1-2 ms → ~6-10 ms one-time at connect.
  Compared to our current Hello/HelloAck single-RTT (~1-2 ms LAN)
  this is a +5-10 ms one-time-per-connection delta. Negligible.
- **Per-packet overhead**: AES-GCM adds ~16 B per packet (auth tag)
  + ~4 B nonce/seq. At 60 Hz pose × 4 peers fan-out × 20 B overhead
  = 4.8 KB/s extra. Negligible.
- **Steady-state CPU**: AES-NI on any 2011+ CPU. Sub-microsecond per
  pose packet. Negligible.

**The cost we DO accept**: complexity. The handshake's existence is
visible at the integration boundary — we cannot send our first
packet until GNS signals Connected. Our `ConnState::Handshaking ->
Connected` transition becomes "wait for ConnectionStatusChanged
callback with state==k_ESteamNetworkingConnectionState_Connected".

---

## §4. Binary size estimate

Methodology: GNS itself is ~50k LOC compiled. After static link with
`/OPT:REF /OPT:ICF`, expect ~600-800 KB of GNS code in the DLL.
Protobuf static is the variable. Crypto choice further varies:

| Configuration | GNS code | Crypto | Protobuf | Total added | Final DLL est. |
| --- | --- | --- | --- | --- | --- |
| **BCrypt + Reference + Protobuf static** (RECOMMENDED) | ~700 KB | ~5 KB (bcrypt.lib stub) | ~800 KB | **~1.5 MB** | **~2.2 MB** |
| libsodium + Reference + Protobuf static | ~700 KB | ~250 KB | ~800 KB | ~1.75 MB | ~2.4 MB |
| OpenSSL 3.x + OpenSSL 25519 + Protobuf static | ~700 KB | ~2.5 MB | ~800 KB | ~4 MB | ~4.7 MB |

Current `votv-coop.dll` is **671 KB** (verified by `dir` on the
deployed build: 687,104 B). Recommended configuration brings us to
~2.2 MB — a ~3.3× growth. Still a single small DLL the user drops in.

For comparison: a typical UE4 mod DLL with Dear ImGui added (our
planned future) sits ~1.5-2 MB on its own. GNS doubles the
footprint vs hand-roll but is below the practical noise floor —
user won't notice.

**Worst-case warning**: if protobuf static link fails to trim under
LTCG and brings ~2.5 MB instead of ~800 KB, final DLL hits ~4 MB.
Still fine. Above ~5 MB we'd want to re-evaluate.

---

## §5. API mapping — current `Session` vs GNS

### 5.1 Initialization

| Today (hand-roll) | GNS |
| --- | --- |
| `Session::Start(cfg)` opens UDP socket via Winsock | `GameNetworkingSockets_Init(nullptr, errMsg)` once at DLL init; `SteamNetworkingSockets()` returns the interface singleton |
| | `g_pSocketsLib->CreateListenSocketIP(addr, 0, nullptr)` on host |
| | `g_pSocketsLib->ConnectByIPAddress(addr, 0, nullptr)` on client |
| Net thread `recvfrom` loop | Net thread `g_pSocketsLib->RunCallbacks()` + `ReceiveMessagesOnConnection(hConn, ppMsgs, nMax)` |
| `Session::Stop()` joins thread + closes socket | `CloseConnection(hConn, ...)` + `GameNetworkingSockets_Kill()` |

The standalone-library entry point is `SteamNetworkingSockets_LibV12()`
(via the `SteamNetworkingSockets_Lib()` inline). We just call
`SteamNetworkingSockets()` everywhere — same name, redefined for the
opensource variant.

### 5.2 Send / receive translation table

| Our API | GNS call | Flags |
| --- | --- | --- |
| `SetLocalPose(pose)` (per-tick, unreliable, newest-wins) | `SendMessageToConnection(hConn, &pose, sizeof, k_nSteamNetworkingSend_Unreliable \| k_nSteamNetworkingSend_NoNagle \| k_nSteamNetworkingSend_NoDelay, nullptr)` — fire-and-forget; GNS drops on its own outbound when stale | NoDelay = drop if can't send promptly (matches our newest-wins semantics) |
| `SetLocalPropPose(pose)` (per-tick, unreliable) | same flags as above | |
| `TryGetRemotePose(out)` | `ReceiveMessagesOnConnection(hConn, &msg, 1)` then dispatch by our `MsgType` header tag — we keep the existing `protocol.h` payload structs | |
| `SendReliable(kind, bytes, len)` (one-shot reliable) | `SendMessageToConnection(hConn, packet, len, k_nSteamNetworkingSend_Reliable, &outMsgNum)` | |
| `TryDrain` reliable inbox | `ReceiveMessagesOnConnection` (same one as poses; we demultiplex by `MsgType` header) | |
| Connection state machine (Connecting/Connected/Disconnected) | `SteamNetConnectionStatusChangedCallback_t` callback in `RunCallbacks()`. The `k_ESteamNetworkingConnectionState_*` enum has 7 states; we collapse to our 3. | |

### 5.3 Send flag mapping

GNS flag constants (verified in `include/steam/steamnetworkingtypes.h`):

- `k_nSteamNetworkingSend_Unreliable = 0` — fire-and-forget. Use for
  PoseSnapshot, PropPose, EntityPoseBatch, future VoiceFrame.
- `k_nSteamNetworkingSend_NoNagle = 1` — bypass Nagle's algorithm
  for this message. Always OR this in for unreliable streams (we want
  immediate send, not batched).
- `k_nSteamNetworkingSend_UnreliableNoNagle = 1` — convenience alias
  for the two above.
- `k_nSteamNetworkingSend_NoDelay = 4` — "if can't send promptly,
  drop". Maps PERFECTLY to our newest-wins pose semantics. Use for
  pose streams.
- `k_nSteamNetworkingSend_Reliable = 8` — reliable, ordered.
  Use for every current `ReliableKind`.
- (No `UseCurrentThread` flag — that was an older RakNet idiom.)

### 5.4 Lanes — the matching feature for our priority requirement

GNS exposes `ConfigureConnectionLanes(hConn, nNumLanes,
pLanePriorities, pLaneWeights)` which is **exactly** the HIGH /
NORMAL / BULK split the requirements doc (§2 of
`votv-network-requirements`) asks for. Lanes are independent
ordering domains within a single reliable channel — same shape as
RakNet "ordering streams" and the per-lane scheduler proposed in
the hand-roll plan.

This is the **single biggest reason GNS is attractive over ENet**:
ENet has no priority concept; GNS has it natively, with explicit
priority + weighted-fair-queue knobs.

Concrete mapping:

```cpp
// At connect time, after k_ESteamNetworkingConnectionState_Connected:
const int kLaneCount = 3;
int laneP[kLaneCount]  = { 0, 1, 2 };   // 0 = highest priority
uint16 laneW[kLaneCount] = { 4, 2, 1 }; // weighted-fair-queue ratio
g_pSocketsLib->ConfigureConnectionLanes(hConn, kLaneCount, laneP, laneW);
// Lane 0 = HIGH (TeleportClient, RestoreVitals, TerminalInput, ...)
// Lane 1 = NORMAL (PropDestroy, EntitySpawn, WeatherState, ...)
// Lane 2 = BULK (PropSpawn snapshot, SaveSnapshotChunk, ...)
```

To send on a specific lane, use `SendMessages()` (the
batched variant) with `SteamNetworkingMessage_t::m_idxLane` set per
message. The single-message `SendMessageToConnection()` always uses
lane 0; for lane control we must build `SteamNetworkingMessage_t*`
via `g_pSocketsLib->AllocateMessage(cbAllocateBuffer)`, fill it,
and call `SendMessages(1, &msg, &msgNum, true)`.

### 5.5 Per-peer connection model

| Today | GNS |
| --- | --- |
| 1 socket, 1 peer endpoint (4-peer plan = `std::array<PeerState, 4>` over single socket) | 1 `HSteamListenSocket` (host) + up to N `HSteamNetConnection` (one per peer); we keep N=4 peer connections in `std::array<HSteamNetConnection, kMaxPeers>` |
| Host fan-out is manual (write same buffer to each peer) | Host fan-out is manual — call `SendMessages` to each peer's `HSteamNetConnection`. (`HSteamNetPollGroup` aggregates receive but does NOT broadcast send.) |

To receive across multiple peer connections efficiently, host puts
all 3 client `HSteamNetConnection`s into one `HSteamNetPollGroup`:

```cpp
hPollGroup_ = sockets->CreatePollGroup();
sockets->SetConnectionPollGroup(hConn1, hPollGroup_);
// ...
// Receive loop:
SteamNetworkingMessage_t* msgs[16];
int n = sockets->ReceiveMessagesOnPollGroup(hPollGroup_, msgs, 16);
for (int i = 0; i < n; i++) {
    HandleDatagram(msgs[i]->m_pData, msgs[i]->m_cbSize,
                   msgs[i]->m_conn);  // m_conn = sender peer
    msgs[i]->Release();
}
```

This collapses the 4-peer poll into one call. Clean.

### 5.6 Connection state translation

| GNS state | Our state |
| --- | --- |
| `k_ESteamNetworkingConnectionState_None` | Disconnected |
| `k_ESteamNetworkingConnectionState_Connecting` | Handshaking |
| `k_ESteamNetworkingConnectionState_FindingRoute` | Handshaking |
| `k_ESteamNetworkingConnectionState_Connected` | Connected |
| `k_ESteamNetworkingConnectionState_ClosedByPeer` | Disconnected (peer-initiated) |
| `k_ESteamNetworkingConnectionState_ProblemDetectedLocally` | Disconnected (timeout / error) |

We collapse them into our 3 inside the `SteamNetConnectionStatusChangedCallback_t`
callback. The callback fires on the net thread (post-RunCallbacks),
not on the engine thread — same threading shape as our current
`HandleDatagram`.

---

## §6. Connection model — handshake + binding

Today:

- **Host**: binds a UDP socket on `0.0.0.0:47621`, awaits Hello from
  any source, locks on first one.
- **Client**: binds ephemeral local UDP, sends Hello to host's
  `peerIp:47621`, awaits HelloAck.

GNS:

- **Host**: `addr.SetIPv6(SteamNetworkingIPAddr::AnyIP, 47621)` then
  `CreateListenSocketIP(addr, 0, nullptr)`. (GNS is dual-stack IPv6
  natively; IPv4 addresses go in as IPv4-mapped IPv6. We can also
  set `SetIPv4` explicitly.)
- **Client**: `addr.ParseString("192.168.1.10:47621")` then
  `ConnectByIPAddress(addr, 0, nullptr)`. Returns
  `HSteamNetConnection` immediately (Connecting state); the
  Connected transition arrives via callback.
- **Host accept path**: in the
  `SteamNetConnectionStatusChangedCallback_t`, when state goes
  `None → Connecting`, call `AcceptConnection(hConn)`. This is
  where we apply the session-token / peer-locking trust boundary —
  reject if we already have a connection from a different IP and the
  scope is 1v1, or if `kMaxPeers` is full.

**Compatibility points**:

- **IPv6**: GNS uses IPv6 internally; IPv4 addresses come in as
  v4-mapped. Existing config (`peerIp = "127.0.0.1"`) parses fine via
  `SteamNetworkingIPAddr::ParseString`. No change in user-visible
  UX.
- **Handshake**: GNS does its own ECDH handshake; our Hello/Pong
  is **redundant and must be removed** (RULE №2). The session-token
  goes into the connection's `connectionUserData` / negotiated cert
  payload instead. Practical drop: our 60 LOC of Hello/HelloAck code
  is deleted; the trust boundary moves into the AcceptConnection
  callback.
- **Port**: 47621 stays. GNS binds it the same way.

---

## §7. Threading model

Today:

- **Game thread** writes pose slots, calls `SendReliable`.
- **Net thread** loops `recvfrom + sleep(5 ms)`; on packet,
  `HandleDatagram` dispatches by `MsgType`; on send, drains the
  reliable channel + sends pose if dirty.
- Bridge: `GT::Post` from net thread when an inbound event must
  run on game thread (e.g. spawn a puppet actor).

GNS:

- **Single net thread (ours)** calls `SteamNetworkingSockets()->
  RunCallbacks()` every ~5 ms to dispatch the
  `SteamNetConnectionStatusChangedCallback_t` and pump internal
  timers (retransmit, congestion).
- Then `ReceiveMessagesOnPollGroup()` drains inbound messages.
- Then we call `SendMessageToConnection` / `SendMessages` for the
  outbound queue.
- **GNS internal threads**: by default GNS spawns ONE background
  thread for low-level packet I/O + crypto. Confirmed by reading
  `src/steamnetworkingsockets/steamnetworkingsockets_lowlevel.cpp`
  upstream — there is a `g_pSocketsThread` for the global service
  pump. We *can* set the config value
  `k_ESteamNetworkingConfig_TimeoutInitial` etc. to tune; we
  cannot avoid the internal thread without re-implementing the
  socket pump. **Net thread count: 1 (ours) + 1 (GNS internal) = 2.**

That second thread is a slight increase (today: 1 net thread). It
does not touch the game thread, does not touch the engine, runs at
sub-1% CPU on LAN. Acceptable.

**Critical invariant**: `SteamNetworkingMessage_t` payload pointers
returned by `ReceiveMessagesOnConnection`/`ReceiveMessagesOnPollGroup`
are GNS-owned. We must call `msg->Release()` after consuming the
payload (`m_pfnFreeData` then `m_pfnRelease`, or just `Release()`).
Forgetting this is a memory leak — to be caught by an autotest.

---

## §8. Concrete CMake integration steps

### 8.1 Vendoring location

`src/votv-coop/third_party/GameNetworkingSockets/` as a **git
submodule** pinned to a known-good upstream commit. Pattern matches
MinHook (`third_party/minhook/` is a submodule; see existing
CMakeLists for the convention).

```bash
cd src/votv-coop/third_party
git submodule add https://github.com/ValveSoftware/GameNetworkingSockets.git
cd GameNetworkingSockets
git checkout v1.4.1  # or whatever HEAD is stable at the time
cd ../../../..
git add .gitmodules src/votv-coop/third_party/GameNetworkingSockets
git commit -m "[net] vendor GameNetworkingSockets as submodule (pinned)"
```

(Pin to a specific tag, never `master`. RULE №1 — we own the
version; never silently take an upstream bump.)

### 8.2 vcpkg manifest

`src/votv-coop/vcpkg.json`:

```json
{
  "name": "votv-coop",
  "version": "0.0.1",
  "dependencies": [
    "protobuf"
  ],
  "builtin-baseline": "<vcpkg-commit-sha-here>"
}
```

Adds vcpkg as a **build-time** dep. The user / CI invokes:

```
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 ^
      -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
      -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release
```

Existing devs without vcpkg installed need a one-time setup:
`git clone https://github.com/microsoft/vcpkg && bootstrap-vcpkg.bat`.
**Document this in `BUILDING.md`** at repo root (currently there is
none; create it).

### 8.3 Top-level CMake additions

Append to `src/votv-coop/CMakeLists.txt` after the MinHook block:

```cmake
# --- GameNetworkingSockets (BSD-3, vendored as git submodule) ---
# Static-link config: BCrypt crypto (Windows-native, zero extra dep),
# vendored donna for curve25519, static protobuf via vcpkg.
set(BUILD_STATIC_LIB         ON  CACHE BOOL   "" FORCE)
set(BUILD_SHARED_LIB         OFF CACHE BOOL   "" FORCE)
set(BUILD_EXAMPLES           OFF CACHE BOOL   "" FORCE)
set(BUILD_TESTS              OFF CACHE BOOL   "" FORCE)
set(BUILD_TOOLS              OFF CACHE BOOL   "" FORCE)
set(USE_CRYPTO               "BCrypt"    CACHE STRING "" FORCE)
set(USE_CRYPTO25519          "Reference" CACHE STRING "" FORCE)
set(Protobuf_USE_STATIC_LIBS ON  CACHE BOOL   "" FORCE)
set(ENABLE_ICE               ON  CACHE BOOL   "" FORCE)
set(USE_STEAMWEBRTC          OFF CACHE BOOL   "" FORCE)

# MSVC: GNS must use the same static CRT as our DLL.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(third_party/GameNetworkingSockets EXCLUDE_FROM_ALL)

# Link the static lib + bcrypt (Windows SDK).
target_link_libraries(votv-coop PRIVATE GameNetworkingSockets_s bcrypt)
target_include_directories(votv-coop PRIVATE
    third_party/GameNetworkingSockets/include)
```

Notes:
- `GameNetworkingSockets_s` is GNS's static-lib target name (suffix
  `_s` per upstream convention).
- `bcrypt` is the Windows SDK lib — no extra paths.
- The `CMAKE_MSVC_RUNTIME_LIBRARY` global apply MUST happen before
  the `add_subdirectory`. (Per-target override on `votv-coop` and
  `minhook` stays as-is in our existing file.)

### 8.4 Patches likely needed

Order them from "definitely needed" to "maybe":

1. **GNS's CMake unconditionally sets `BUILD_SHARED_LIBS`** in some
   places — overriding our static target. Mitigation: confirm by
   inspecting `src/CMakeLists.txt` of GNS after vendoring; patch
   to respect the parent project's `BUILD_SHARED_LIBS=OFF` if it
   doesn't.
2. **`/permissive-` and `/W4` we apply to `votv-coop`** are NOT
   applied to GNS's targets (good — GNS won't compile clean at /W4).
   GNS likely defaults to `/W3` or lower internally; expect to see
   build warnings from GNS sources. **Don't fix them upstream-style
   — silence them with `target_compile_options(GameNetworkingSockets_s
   PRIVATE /w)` if they're noisy.**
3. **Protobuf find logic** in GNS may default to dynamic libprotobuf.
   `Protobuf_USE_STATIC_LIBS=ON` should fix it, but vcpkg's
   protobuf-config-cmake module sometimes ignores this. If it does:
   patch GNS's `cmake/FindProtobuf.cmake` (if it has one) to honor
   our setting, OR set `Protobuf_LIBRARIES` manually via
   `find_package(Protobuf CONFIG REQUIRED)` before the
   add_subdirectory.

Document any patches we make in a `third_party/GameNetworkingSockets-patches/`
folder (single file, listing the upstream commit + the diff) so we
can rebase cleanly on a future GNS bump.

---

## §9. Build time impact

Today's clean build (verified via `cmake --build`): **~4-6 seconds**
on the dev machine (~30 .cpp files, MinHook small, static CRT, single
DLL).

GNS adds:

- **Protobuf static** (built once via vcpkg, cached afterward in
  `<vcpkg>/installed/x64-windows-static/`): one-time ~30-60 s on the
  first `cmake` invocation. Cached after; subsequent builds zero cost.
- **GNS sources** (~50k LOC, ~200 .cpp/.h): ~20-40 s clean build.
- **`.proto` → `.pb.cc` via protoc**: ~1-2 s.

**Clean-build delta estimate**: +30 s first time (vcpkg protobuf
bootstrap), +20-40 s subsequent clean builds (GNS itself).

**Incremental build delta**: zero (GNS objects cached, only our
sources recompile).

**Configure-time delta**: +5-10 s on first `cmake` due to
`find_package(Protobuf)` + GNS's own configure work. Subsequent
reconfigs cache.

**CI impact**: clean CI builds go from ~10 s to ~60-90 s on first
run; subsequent runs use vcpkg binary cache → back to ~30-40 s.

**Acceptable**: a 60-second clean build is still under a minute and
matches every other vcpkg-based Windows C++ project. We're not in
the "your CI takes 20 minutes" regime.

---

## §10. Migration roadmap

The hand-roll plan in the sibling doc proposed a feature-flagged
parallel-substrate roadmap (Inc-1..Inc-5) then RULE-2-cleanup. For
GNS the calculus changes because:

- The substrate is large + foreign + the migration boundary is
  cleaner (our `Session` is the abstraction layer).
- Feature-flagging two substrates simultaneously violates RULE №2
  (`no migration baggage`, no `parallel old+new code paths`).
- BUT — RULE №1 is louder: a failed integration mid-way leaves the
  shipped mod broken. We need a safe rollback path.

### Recommended: 4-PR linear migration, NOT feature-flagged

The compromise that honors both rules: each PR is **complete and
revertable**, no parallel-paths committed. If PR-2 reveals GNS
fails our requirements, we revert PR-2 (one git revert) — PR-1 stays
because it's only added build infrastructure (no behavior change).

| PR | Deliverable | Reversibility | Wall-clock est. |
| --- | --- | --- | --- |
| **PR-1: vendor + link** | Add submodule, vcpkg.json, CMake additions. **GNS compiles + links into our DLL.** Code does NOT call any GNS API yet. Just `#include <steam/steamnetworkingsockets.h>` in a single .cpp behind `#if 0`. Verifies the build setup + binary growth + no symbol collisions. | Trivial revert (revert the 4-file PR). | 1-2 days (most time is vcpkg/CMake debugging). |
| **PR-2: replace transport + handshake** | Delete `transport.cpp` + handshake code in `session.cpp`. Implement new `session.cpp` using `CreateListenSocketIP` + `ConnectByIPAddress` + the `SteamNetConnectionStatusChangedCallback_t`. **All existing `Session` methods (SendReliable, SetLocalPose, etc.) call into GNS now.** Reliable uses `k_nSteamNetworkingSend_Reliable`; pose uses Unreliable+NoNagle+NoDelay. Single lane (lane 0) initially. Old `reliable_channel.cpp` deleted. | Revert if the LAN autotest fails to reach Connected within 5 s. | 3-5 days. The high-risk PR. |
| **PR-3: lanes + multi-peer** | Configure 3 lanes via `ConfigureConnectionLanes`. Tag every `ReliableKind` send with HIGH/NORMAL/BULK. Bump `kMaxPeers` end-to-end to 4 (host accepts up to 3 client `HSteamNetConnection`s, poll group). | Revert reverts to single-lane single-peer GNS (PR-2 state). | 2-3 days. |
| **PR-4: bump MTU + scaffolding for future features** | Raise per-message size limit from our 256 B to GNS's natural ~1200 B. Wire up the unreliable batch path for future `EntityPoseBatch`. Document the API in a new `coop/net/README.md`. | Low-risk PR — mostly tuning + docs. | 1 day. |

Total: **7-11 working days**. Comparable to the hand-roll plan's
7-10 days (§6 of build-vs-vendor doc), with risk shifted from
"writing ARQ" to "integrating someone else's ARQ."

### Why NOT one big migration commit

Per RULE №1 we don't ship half-finished features, but per the spirit
of "STOP and fix" — having intermediate green-tested checkpoints means
if PR-2 introduces a 3-day debug session, PR-1's binary still ships
+ user testing continues on the old wire. The 4-PR shape is the
proper engineering hygiene, not a crutch.

### Why NOT feature-flag the substrate

Feature-flagging two substrates ("new = on/off") creates exactly the
"parallel old+new code paths" RULE №2 forbids. If PR-2 ships, the
old `transport.cpp` is GONE the same commit (RULE №2). Revert path
is `git revert PR-2-sha`, not a runtime flag.

---

## §11. Risks and the escape hatch

### Top 5 risks, ordered by severity

1. **Protobuf integration on Windows MSVC static** is the
   load-bearing build piece. **CRITICAL.** vcpkg's protobuf works
   well in 2026, but GNS's internal `find_package(Protobuf)` may
   not honor the `Protobuf_USE_STATIC_LIBS` flag cleanly when
   triplet `x64-windows-static` is selected. **First thing to
   verify** in PR-1. If it fails, we're in patch-GNS-CMake territory,
   adding 1-3 days of build-system debugging.
   - **Verification before committing**: get an empty .cpp inside
     `votv-coop.dll` that does `#include <google/protobuf/message.h>
     ; #include <steam/steamnetworkingsockets.h>` and `cmake --build`
     to a clean link. If that doesn't work in PR-1, abort GNS and
     re-evaluate.

2. **GNS internal thread interacts badly with the proxy DLL load
   order**. Our DLL loads early (via `xinput1_3.dll` proxy) when
   the game starts. If GNS's `GameNetworkingSockets_Init()` is called
   at DLL attach time, it spawns a thread inside `DllMain` —
   **fatal**, `DllMain` cannot create threads. We must defer
   `GameNetworkingSockets_Init()` to first-Session-start (already
   our model), but the testing burden is to verify no
   crash-on-game-launch.
   - **Verification**: a launch-without-Session-start smoke test
     (start game, hover in main menu 30 s, no crash) before PR-2
     ships.

3. **GNS handshake adds ~10 ms to connect latency**. Already
   discussed (§3). Not a real risk for LAN, but **could surprise
   the user** if they expect "instant connect like before." Documented
   expectation; not technical blocker.

4. **Encryption cannot be disabled**. We pay the cost. Discussed §3.

5. **Binary growth to ~2.2 MB**. 3.3× over current. The user-visible
   "drop a DLL in" experience is unchanged (one file); the warm
   feeling of "tiny mod" is gone. Discussed §4. Not a blocker.

### Risks NOT applying

- License contamination: BSD-3 + ISC + Apache + Microsoft SDK. All
  clean. Discussed §1.
- ABI breaks: GNS pins its ABI per-tag (`v12` interface today). We
  pin a tag; upstream bumps don't break us. Discussed §8.1.
- IPv6-only assumption: GNS supports IPv4 transparently.

### Escape hatch

If PR-1 reveals the build cost (protobuf static + vcpkg) is more
than 5 days of build-system work, **pivot to ENet vendoring** from
the build-vs-vendor doc's §2.2. ENet's RULE №3 fit is the same
(static-linkable, no runtime deps), at the cost of no priority-lanes
(we'd emulate via per-channel bandwidth caps). We have to recreate
the work GNS gives us for free — but ENet's cost in the same PR-1
scope is ~half a day (add submodule, add CMake `add_subdirectory`,
done).

**Hard limit**: 5 days on PR-1 work. Day 6+ on build setup → revert
+ pivot to ENet.

---

## §12. The single most likely-to-fail step + how to verify FIRST

**The failure mode**: protobuf static link with vcpkg
`x64-windows-static` triplet on MSVC produces unresolved externals
because GNS's CMake refers to the dynamic-link import lib name.

**Symptom**: link errors like
`unresolved external symbol "protobuf_AddDesc_...@@..."` in
`steamnetworkingsockets_messages.pb.obj`.

**Root cause**: GNS's `find_package(Protobuf)` finds the vcpkg config
file, which exposes `protobuf::libprotobuf` as a target that's
correctly static — but the legacy `${Protobuf_LIBRARIES}` variable
GNS sometimes uses still points at the dynamic-lib name.

**Verify FIRST (before any code change)**:

1. Clone GNS standalone in a sandbox dir.
2. Set up vcpkg, install `protobuf:x64-windows-static`.
3. Configure + build GNS standalone:

   ```
   cmake -S GameNetworkingSockets -B build ^
         -G "Visual Studio 16 2019" -A x64 ^
         -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake ^
         -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
         -DUSE_CRYPTO=BCrypt -DUSE_CRYPTO25519=Reference ^
         -DBUILD_SHARED_LIB=OFF -DBUILD_STATIC_LIB=ON ^
         -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF ^
         -DProtobuf_USE_STATIC_LIBS=ON
   cmake --build build --config Release --target GameNetworkingSockets_s
   ```

4. Confirm `GameNetworkingSockets_s.lib` exists and is `>5 MB` (the
   uncompressed static lib).

If step 4 succeeds, the integration is unblocked. If it fails, we
have a build-system surgery problem to solve BEFORE introducing
to our tree. **This is the verification gate for PR-1.**

Expected effort: 2-4 hours sandbox setup + build. **Do this
before opening PR-1.**

---

## §13. Summary

| Question | Answer |
| --- | --- |
| Can GNS be statically linked into `votv-coop.dll`? | **YES, conditionally.** Conditions: USE_CRYPTO=BCrypt + USE_CRYPTO25519=Reference + Protobuf_USE_STATIC_LIBS=ON + vcpkg x64-windows-static triplet + GNS pinned to a tag. |
| Realistic final DLL size? | **~2.2 MB**, up from 671 KB. ~3.3× growth. Acceptable. |
| Build-time clean delta? | +30-60 s first vcpkg run; +20-40 s subsequent. Incremental: zero. |
| Encryption disablable? | **No.** Mandatory. Cost: ~10 ms one-time at connect + ~20 B per packet. Negligible. |
| Single biggest risk? | Protobuf+vcpkg static-link config on MSVC. Verify by building GNS standalone in a sandbox BEFORE PR-1 lands. |
| Migration shape? | **4 PRs, linear, each reversible.** Not feature-flagged. RULE №1 + RULE №2 honored together via PR-granularity. |
| Escape hatch? | If PR-1's build setup is >5 days of work, pivot to ENet (sibling doc §2.2). |
| Wall-clock total? | **7-11 days** for the full migration, including post-PR audits per CLAUDE.md "after shipping, audit" rule. |
