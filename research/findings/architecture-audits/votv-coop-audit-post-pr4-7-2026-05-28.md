# VOTV_MP coop mod audit — post PR-4.7 (2026-05-28)

Four-angle parallel agent audit run at HEAD=`72250d5` after the GNS migration
arc (PR-1 through PR-4.7) closed the 15-finding audit. User directive: "the
project is serious now. Audit what you shipped, audit architecture, audit
reliability (long sessions, multiple reconnections), audit how the code
organized - good or bad."

Agents:
- **feature-dev:code-reviewer** — bug hunting on the PR-4.X diff
- **feature-dev:code-architect** — per-slot multi-peer architecture review
- **general-purpose** — reliability under long sessions + reconnect storms
- **general-purpose** — code organization + modularity

All claims I followed up on were verified at file:line before writing this
report.

---

## Top-line verdict

- The PR-4.X arc landed clean for the multi-peer goals.
- **One ACTIVE correctness bug** is live today on every world load (C1).
- **Three latent correctness bugs** active under specific conditions (C2/C3/C4).
- **Seven architecture/reliability hazards** with concrete bite (H1-H6 + A3).
- **Five organization findings** — including silent growth past soft cap on
  weather_sync.cpp that no audit caught.
- Master-server migration cost (PR-5/PR-6) is genuinely low. The
  "topology-blind below Start()" claim holds.
- No 19-GB-hang-class issues found. Job-Object 12 GB cap is effective.

---

## CRITICAL — fix before next user test

### C1. `kProcessedInitCap = 256` silently wipes the dedupe set on every world load
**File**: `src/votv-coop/src/coop/prop_lifecycle.cpp:84,89`
```cpp
constexpr size_t kProcessedInitCap = 256;
...
if (g_processedInitActors.size() >= kProcessedInitCap) g_processedInitActors.clear();
g_processedInitActors.insert(actor);
```
**Mechanism**: VOTV worlds spawn ~2000 keyed-interactable props on level
load. The 257th `Init` blows away the prior 256 entries. The super-call
dedupe was designed to prevent subclass-Init→parent-Init from double-
broadcasting `PropSpawn` for the same actor — but with the set cleared,
the next parent-call re-fires. Receiver `OnSpawn` is idempotent (converges
transform) so behavior isn't "wrong" but it's silent reliable-channel
waste at the worst moment (initial snapshot).
**Severity**: CRITICAL (active today).
**Fix**: raise cap to 8192 (matching reliableInbox) and stop inserting on
overflow instead of clearing. Better: track via the prop's own FName.

### C2. `ResolveAndStartDrive` does not initialize `lastApplyMs`
**File**: `src/votv-coop/src/coop/remote_prop.cpp:279-281`
```cpp
g_drives[slot].actor = prop;
g_drives[slot].mesh  = mesh;
g_drives[slot].lastKey.assign(k.data, k.len);
// missing: g_drives[slot].lastApplyMs = NowMs();
```
**Mechanism**: After first packet establishes the drive, if next PropPose
drops or arrives >500ms later, the timeout check fires immediately
because `NowMs() - 0 > 500` is always true, releasing the prop on the
first network hiccup after a grab.
**Severity**: CRITICAL (active under packet loss).
**Fix**: 1 LOC — `g_drives[slot].lastApplyMs = NowMs();` after line 281.

### C3. `SendReliable` fan-out memcpy missing null-payload guard
**File**: `src/votv-coop/src/coop/net/session.cpp:553` (vs `:503` for
SendReliableToSlot)
```cpp
// Line 503 (SendReliableToSlot):
if (len > 0 && payload) {
    std::memcpy(buf + ..., payload, len);
}

// Line 553 (SendReliable fan-out):
std::memcpy(buf + ..., payload, len);  // unconditional
```
**Mechanism**: Asymmetric null-safety contract. No caller passes null
today but the gap will eventually be exploited as new packet kinds are
added.
**Severity**: CRITICAL (latent crash).
**Fix**: mirror the guard.

### C4. `flashlight_click_sound.cpp` local `kMaxPeers = 4` hardcode
**File**: `src/votv-coop/src/coop/flashlight_click_sound.cpp:29`
```cpp
constexpr uint8_t kMaxPeers = 4;
int g_lastAppliedStateByPeer[kMaxPeers] = {-1, -1, -1, -1};
```
**Mechanism**: Local constant does NOT track `coop::players::kMaxPeers`.
If central kMaxPeers bumps to 8, this file silently caps at 4 — peer 5+
state is dropped via the out-of-range check at line 42-46.
**Severity**: CRITICAL (latent scaling hazard).
**Fix**: include `coop/players_registry.h` and use `coop::players::kMaxPeers`.

---

## HIGH — architectural debt with concrete bite

### H1. Per-slot state not reset on session.Start (latent; first restart bites)
**Files**:
- `src/votv-coop/src/harness/harness.cpp:240,1008,1193` —
  `g_wasConnectedBySlot[]` never reset (only `g_wasConnected` is).
- `src/votv-coop/src/coop/event_feed.cpp:36-47` —
  `g_remoteNickBySlot`, `g_lastConnectedBySlot`, `g_joinSentBySlot`
  same gap.

**Mechanism**: Second `Start()` on the same process sees stale `true`
entries → phantom disconnect-edges + skipped connect-edge replay (no
snapshot for slots that were connected in prior session) + cosmetic "X
left the game" spam on first new-session tick.
**Severity**: HIGH (latent — no restart path today, but implied by GNS
forward-compat plan and "long sessions with reconnects" threat model).
**Fix**: add `coop::event_feed::OnSessionStart()`. Call it + reset
`g_wasConnectedBySlot.fill(false)` next to the existing
`g_wasConnected = false` at session.Start.

### H2. `prop_snapshot::TriggerForSlot` walks 150k GUObjectArray entries per reconnect
**File**: `src/votv-coop/src/coop/prop_snapshot.cpp:57-74`
**Mechanism**: `R::NumObjects()` linear walk with `R::ToString(R::NameOf(obj))`
heap alloc per object + `IsKeyedInteractable` super-walk. Under reconnect-
storm (peer drops every 15s for an hour = 240 reconnects), serially
burns the host's game thread. Two clients reconnecting in alternation
saturates GT.
**Severity**: HIGH (reconnect-storm threat model).
**Fix**: maintain parallel `g_knownKeyedProps` set in `prop_lifecycle`
(Init POST adds, K2_DestroyActor PRE removes). `TriggerForSlot` iterates
that set instead of GUObjectArray. ~30 LOC.

### H3. `event_feed::Update` allocates joinPayload every 125Hz tick after all Joins sent
**File**: `src/votv-coop/src/coop/event_feed.cpp:165-172`
**Mechanism**: `ToUtf8` + `WideCharToMultiByte` + vector alloc inside
`Update`, unconditionally. After all `g_joinSentBySlot[slot]==true` this
is pure waste. ~3 heap allocs per 8ms.
**Severity**: HIGH (hot-path template violation).
**Fix**: hoist construction inside the `!g_joinSentBySlot[slot]` branch
or short-circuit when all slots have sent.

### H4. Protocol version mismatch is a silent hang
**File**: `src/votv-coop/src/coop/net/session.cpp:602-607`
**Mechanism**: `ParseHeader` returns false on `h.version != kProtocolVersion`,
silently drops the packet. v10 client connecting to v11 host: handshake
succeeds, all app messages dropped both ways, "Connected" forever, no
feedback to either party.
**Severity**: HIGH (UX-blocking on version skew).
**Fix**: detect a version mismatch path and `CloseConnection(reason=
"protocol mismatch: peer=v10, ours=v11")` so the GNS status callback
delivers a human-readable string at both ends.

### H5. `FindSlotByKey` returns first match, not sender match
**File**: `src/votv-coop/src/coop/remote_prop.cpp:254-259`
**Mechanism**: `OnRelease`/`OnDestroy` use linear-first-match. Under
PR-4.6's "two clients can each hold different props simultaneously"
claim: if two slots share `lastKey` (race or bug), the wrong slot's
drive gets cleared.
**Severity**: HIGH (latent under specific edge cases).
**Fix**: pass `msg.senderPeerSlot` through to `OnRelease` from
event_feed dispatch + key on that.

### H6. Snapshot drain starts before Connected callback / lanes configured
**File**: `src/votv-coop/src/coop/prop_snapshot.cpp:95-103` +
`src/votv-coop/src/coop/net/session.cpp:208` (Connecting) vs `:266`
(Connected).
**Mechanism**: `peerConns_[slot]` is set in `Connecting` callback,
BEFORE `ConfigureLanesForPeer` runs in `Connected`. Harness sees
`IsSlotConnected(slot)` go true on NetPumpTick BEFORE lanes configured.
PropSpawn sent in that window goes on lane 0 (HIGH) instead of lane 2
(BULK) — defeating the head-of-line-blocking fix from PR-3.
**Severity**: HIGH (defeats PR-3's lane design under reconnect race).
**Fix**: add `lanesConfigured_[kMaxPeers]` atomic set in Connected
callback after `ConfigureConnectionLanes`. Harness gates `TriggerForSlot`
on it.

---

## ARCHITECTURE findings

### A1. Principle 7 layering leaks — coop/ files touch engine struct memory directly
**Locations**:
- `src/votv-coop/src/coop/remote_player.cpp:534-541` — raw `0xC4`
  offset write to CharacterMovementComponent → should be
  `ue_wrap::puppet::DriveMovementComponent(actor, vel, mm)`.
- `src/votv-coop/src/coop/item_activate.cpp` ~12 sites writing
  `light_R` light component fields → should be
  `ue_wrap::puppet::SetLightState(...)`.
- `src/votv-coop/src/coop/weather_sync.cpp:141-163,1128-1144` — cycle
  struct reads/writes.
- `src/votv-coop/src/coop/npc_sync.cpp:102` — re-implements SuperStruct
  walk that already exists in `ue_wrap/reflection.cpp`.
**Severity**: MEDIUM (principles drift; not bugs today).
**Fix**: extract engine wrappers; replace direct offset access at call
sites.

### A2. `dev::` namespace at global scope + cross-layer includes
**Locations**:
- `src/votv-coop/src/coop/event_feed.cpp:9-10` includes
  `dev/restore_vitals.h` + `dev/teleport_client.h`. RestoreVitals and
  TeleportClient are NOT really dev features — they're host-triggered
  coop features.
- `src/votv-coop/src/coop/weather_sync.cpp` includes `dev/common.h` for
  ini config helpers — `dev/common.h` is a shared config helper, not
  dev-only code.
- `src/dev/` files declare `namespace dev` at global scope, not under
  `coop::dev::` or `harness::dev::`.
**Severity**: MEDIUM (architectural ambiguity).
**Fix**: rename `dev/` → `coop/dev/` (mechanical) OR move
RestoreVitals/TeleportClient to `coop/`. Rename `dev/common.h` →
`coop/ini_config.h`.

### A3. `npc_sync` host-only globals masquerading as global state
**Locations**:
- `src/votv-coop/src/coop/npc_sync.cpp:62,185` — `g_nextNpcSessionId`
  non-atomic `uint32_t` incremented from `RegisterInterceptor`-bound
  callback that fires from parallel-anim worker threads (file's own
  comment lines 31-36 acknowledges this). Race when
  `VOTVCOOP_NPC_SYNC=1` and >1 NPC spawns concurrently.
- `src/votv-coop/src/coop/npc_sync.cpp:66,254-258` —
  `g_npcSessionByActor` map declared, cleared on disconnect, NEVER
  inserted into anywhere. Comment says "Populated by Inc3
  K2_DestroyActor PRE hook" — hook was never written. RULE 2 baggage.
**Severity**: HIGH when `VOTVCOOP_NPC_SYNC=1`; LOW today (default-off).
**Fix**: `std::atomic<uint32_t>` + `fetch_add`. Delete dead map.

### A4. NetPumpTick subsystem enumeration doesn't scale
**File**: `src/votv-coop/src/harness/harness.cpp:367-394` per-slot edge
block + `:398-419` aggregate block.
**Mechanism**: Every per-slot subsystem adds a hand-written line to
NetPumpTick's edge blocks. At >10 subsystems this won't hold.
**Severity**: MEDIUM (future scalability).
**Fix**: replace with registration pattern (subsystems register
`(OnConnectForSlot, OnDisconnectForSlot)` callbacks at Install time).
Mirror MTA's `CClientManager::DoPulse()` iteration.

### A5. `g_incomingNpcSpawnClass` single bypass slot races multi-client
**File**: `src/votv-coop/src/coop/npc_sync.cpp:259`
**Mechanism**: Single `void*` used as a bypass slot for "set immediately
before calling BeginDeferredActorSpawnFromClass, consume in next
interceptor fire." At 3 clients receiving NPC spawns concurrently, two
`MarkIncomingNpcSpawn` calls could overwrite each other.
**Severity**: HIGH when `VOTVCOOP_NPC_SYNC=1`; LOW today.
**Fix**: per-thread bypass slot or stack-local through the call path.

---

## RELIABILITY findings

### R1. `prop_snapshot::g_session_ptr` is non-atomic
**File**: `src/votv-coop/src/coop/prop_snapshot.cpp:34`
```cpp
coop::net::Session* g_session_ptr = nullptr;
```
**Mechanism**: All other sibling subsystems use
`std::atomic<Session*>` per the audit-C2 fix from
`[[feedback-install-idempotent-o1-steady-state]]`. prop_snapshot was
missed.
**Severity**: MEDIUM (currently safe by call ordering, but the design
doesn't enforce it).
**Fix**: convert to `std::atomic<Session*>` mirroring siblings.

### R2. `reliableInbox_` deque churns 0→1.9MB→0 per disconnect under storm
**File**: `src/votv-coop/src/coop/net/session.cpp:670-683`
**Mechanism**: `std::deque<ReliableMessage>` where each entry is 232
bytes. Under reconnect-storm grows to ~1.9 MB peak then drops on each
peer-disconnect inbox filter. The malloc/free churn is measurable.
**Severity**: MEDIUM (storm-only).
**Fix**: replace with fixed-capacity ring buffer (8192 inline slots
allocated once at session-start). ~50 LOC.

### R3. GNS default `TimeoutConnected=10s` may pile up disconnect-cascade callbacks
**File**: `src/votv-coop/src/coop/net/session.cpp` (no
SetConfigValue calls)
**Mechanism**: 4 peers simultaneous disconnect → 4× sequential
`reliableInbox_.erase` walks under `reliableInboxMutex_`, ~1ms hitch.
**Severity**: MEDIUM (minor hitch).
**Fix**: configure shorter `TimeoutConnected` OR per-peer inbox so
drop-on-disconnect is O(N_slot) not O(N_total).

---

## ORGANIZATION findings

### O1. File-size catalog refresh

| File | Now | Was | Verdict |
|---|---|---|---|
| harness.cpp | 1315 | 1103 (post-2026-05-25 refactor) | Past soft cap. NetPumpTick alone is 318 LOC. **Extract `coop/net_pump.cpp` next.** |
| weather_sync.cpp | 1214 | not in catalog | **Silent growth past soft cap.** **Split 3-way**: weather_state / weather_lightning / weather_redsky. |
| sdk_profile.h | 1121 | — | Single-feature header. Legitimate. |
| remote_prop.cpp | 938 | 885 | Past soft cap. Three subsystems under one roof. Split at next +100. |
| session.cpp | 810 | — | Just past. One feature. Leave. |
| item_activate.cpp | 776 | — | Under. Watch for next item (radio/torch). |
| event_feed.cpp | 702 | — | Under. |

### O2. weather_sync.cpp 3-way split proposal
| Feature | Estimated LOC | New file |
|---|---|---|
| Continuous weather state | ~750 | `coop/weather_state.cpp` |
| Lightning strike | ~230 | `coop/weather_lightning.cpp` |
| Red sky | ~290 | `coop/weather_redsky.cpp` |

Audit-rule failure: the "any touched file approaching soft cap must be
flagged" rule fired ZERO times across the 5+ commits that grew this
file. The audit prompt needs the file-size check as a hard preflight,
not an optional flag.

### O3. harness.cpp NetPumpTick extraction
Extract `NetPumpTick` (~318 LOC) + per-slot edge state (`g_puppets`,
`g_wasConnectedBySlot`, `g_wasConnected`, `g_lastHeldProp`,
`g_propEmitCount`) → new file `src/votv-coop/src/coop/net_pump.cpp` +
`include/coop/net_pump.h`. Post-extraction harness.cpp ~1000 LOC.

### O4. Header API leak in remote_prop.h
**File**: `src/votv-coop/include/coop/remote_prop.h:89-92`
`MarkIncomingSpawn`/`ConsumeIncomingSpawn`/`MarkIncomingDestroy`/
`ConsumeIncomingDestroy` are echo-suppress internals leaking into public
header. Only caller is `prop_lifecycle.cpp` (3 sites).
**Fix**: extract to `coop/prop_echo_suppress.h` — both modules include
it; remote_prop no longer exposes private API.

### O5. Comment-rot pattern
PR labels (`PR-4.X`), audit finding numbers (`#7`, `Finding I`), and
"pre-fix this was X" git-history-in-comments are pervasive across 6 hot
files. The `[[memory-link]]` references are GOOD (resolve to stable
memory entries); PR labels rot the moment the PR ships.
**Fix rule**: ban inline PR labels + finding numbers; keep
`[[memory-link]]` references + rationale prose. Mechanical pass across
`harness.cpp`, `weather_sync.cpp`, `remote_prop.cpp`, `event_feed.cpp`,
`item_activate.cpp`, `net/session.cpp`.

### O6. DebugForceToggle stop-and-wait retry loop = RULE 2 baggage
**File**: `src/votv-coop/src/coop/item_activate.cpp:391,426-430`
Comment claims "channel is stop-and-wait (max 1 in-flight)" — but PR-2
replaced that with GNS reliable queue. 200-retry × 25ms = 5s of dead
code. ~50 LOC removable.

---

## Master-server migration cost preview (PR-5 / PR-6)

**PR-5 (lobby browser)**: entirely additive.
- New `coop/net/lobby.cpp` HTTP client.
- `Config` struct extension.
- New ImGui UI.
- Zero changes to existing code.

**PR-6 (P2P signaling)**: minimal surgery.
- `session.cpp::Start()` adds an `if (cfg_.topology == Topology::P2P)`
  branch alongside existing LanDirect branch. 2-3 lines added, 0 lines
  removed.
- `Config` struct gains `topology`, `peerIdentity`, `signalingUrl`,
  `signalingAuthToken` fields.
- `harness/config.cpp` parses `topology=p2p` from ini.
- New file `coop/net/signaling_client.cpp` implementing
  `ISteamNetworkingConnectionSignaling`.
- **Zero changes** to: HandleMessage, HandleConnStatusChanged,
  SendReliable, SendReliableToSlot, TryGetRemotePose, peerConns_[],
  PollGroup, lanes, subsystem code.

**One concern not in the plan**: AssignPeerSlot dropped under P2P
signaling. Today it fires once from Connected callback (session.cpp:265-273).
If signaling succeeds but slot assignment drops (GNS Reliable should
prevent this), client `LocalPeerId()` stays at `kPeerIdUnknown` forever.
Document retry requirement.

---

## Recommended PR sequencing

1. **PR-4.8 "audit closeout — correctness"** — C1, C2, C3, C4. ~30 LOC
   total. One is an active bug today (C1). Ship first.
2. **PR-4.9 "audit closeout — architecture"** — H1, H2, H3, H4, H5, H6,
   A3 (npc_sync atomic + dead map delete). ~150 LOC.
3. **PR-4.10 "audit closeout — organization"** — O3 (NetPumpTick extract)
   + O4 (echo-suppress extract) + O6 (DebugForceToggle baggage delete).
4. **PR-4.11 "weather_sync split"** — O2.
5. **PR-4.12 "comment-rot strip"** — O5 mechanical pass.
6. **PR-5** — master server v0 (lobby browser).
7. **PR-6** — master server v1 (P2P signaling).

A1 (Principle 7 violations) gets folded into A2 (`dev/` rename) and the
next sub-PR each subsystem touches — opportunistic, not its own PR.

---

## Cross-refs

- 15-finding audit closeout: `[[project-session-2026-05-28-gns-pr4-1-ship]]`
- Master-server design doc: `research/findings/network/votv-gns-p2p-masterserver-plan-2026-05-28.md`
- Audit-prompt-perf-template: `reference/agency-agents/audit-prompt-perf-template.md`
- Install-idempotent rule: `[[feedback-install-idempotent-o1-steady-state]]`
- Modular file-size rule: `[[feedback-modular-file-size-rule]]`
- No-handoff-without-smoke rule: `[[feedback-no-handoff-without-smoke-test]]`
