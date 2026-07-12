# VOTV_MP — Foundation Audit (2026-05-29 v2)

**HEAD:** `e15b1fc` (post-M-1 engine_mainplayer extract)
**Predecessor audit:** 8-dimension review on `56d039a` (this doc replaces it)
**Method:** 14-dimension parallel audit (114 agents, ~7.8M tokens) + 1-vote adversarial verify + completeness critic + 5 missed-angle expansions
**Scope:** entire `src/votv-coop/` tree + `docs/` + `memory/` + `CLAUDE.md`
**Lens:** "Lay a robust foundation for 4+ player coop with many entity types (vehicles, doors, AI variants, inventory, sanity/stamina, world events)"

> **Status:** 87/99 new candidates survived adversarial verify. Of the ~75 prior findings, ~35 are CLOSED (shipped in quad-audit a50e288, A-1..A-4 P7 wrappers, M-1 extractions, B-1/B-2/B-3 protocol work). ~20 SURVIVE. 5 missed angles identified by the critic must be added to the queue.

---

## TL;DR — The 4 critical foundation realizations

These are blockers for the user's stated 4+ player goal. Everything else queues behind them.

1. **The network topology is fundamentally 2-player today.** No host relay of client-originated PoseSnapshot/PropPose/ItemActivate between peers (NEW-N7-1, S-5). 3+ peers see each other frozen at their spawn point with no flashlight/grab/state because each client only talks to host and host never forwards. Late-joiners see only HOST state, not peer state (NEW-N7-2). MTA solved this 15 years ago via `CGame::Relay` + `CGame::JoinPlayerComplete`. Ours doesn't.
2. **NPC AI runs independently on every client (S-1).** No EntityPose ReliableKind exists. Host kerfur chases host player; client kerfur chases client player; combat/horror loop is incoherent. Player health/vitals/death also unreplicated (S-2). The entire coop survival horror premise depends on this.
3. **Identity / id-range trust is enforced at FOUR places and forgotten at SEVEN more.** RegisterMirror, MirrorManager::Install, PropSpawn receiver, PropDestroy receiver, Join, EntitySpawn, EntityDestroy all accept any eid in `[1, kMaxElements)` regardless of sender role. Single `IsAllowedSenderEid(slot, eid)` helper closes E-1, the 3 PropSpawn/Destroy/Join gaps, host-migration eid collision, and the save-game scrub angle in one shape.
4. **No autonomous coverage for 3+ peers OR any gameplay subsystem.** mp.py smoke verdict is process-liveness only (NEW-T13-1). lan-test.ps1 has the rich verdict logic but is orphaned. Every commit that touches slot/mirror/snapshot/RTT logic ships with structural-only validation. Adding new entity types under this regime = monotonic dark-code accumulation.

These four are the foundation gates. **PR-FOUNDATION-1..5 below address them.**

---

## PR-FOUNDATION strategic priorities (from completeness critic)

Ordered by foundation impact. Each is 4-10 days of work and unblocks N follow-on features.

### PR-FOUNDATION-1: Identity epoch + range enforcement
**Closes:** E-1, the 3 PropSpawn/PropDestroy/Join range gaps, W-1 0-context bypass, syncContext-wrap aliasing, EntitySpawn/EntityDestroy missing senderContext, host-migration eid collision pre-emption.
**Shape:** Replace 8-bit senderContext with a 32-bit per-peer session-epoch (monotonic, never recycled, allocated at Connecting). Carry in every reliable packet header (reclaim from unused 8-byte PacketHeader.token field). Add `bool ValidateSender(slot, eid, epoch)` invoked at every receive site and Save-write boundary.
**Estimate:** 4-6 days. Blocks NPC/Door/Vehicle expansion.

### PR-FOUNDATION-2: Save-game safety contract
**Closes:** Missed-angle "save corruption" + handles host-crash-mid-coop persisting peer state into single-player save.
**Shape:** Hook AmainGamemode_C save UFunction PRE; emit `PreSaveScrub` event that walks Registry and removes coop-only Elements + mirrored actors from any host-authoritative array the save path serializes; emit `PostSaveRestore` after. Atomic-rename discipline (write `.sav.tmp`, fsync, rename). Snapshot active save to `backup/` on session start.
**Estimate:** 1 week. Without this, every public 4-player session is one autosave-during-disconnect away from save corruption. Blocks any wider community alpha.

### PR-FOUNDATION-3: Manager pattern collapse (one canonical mirror table)
**Closes:** T-2 UAF, T-5 split window, T-10 asymmetry, MirrorManager::Get UAF, Registry::Get UAF, T-7/T-8 "no lock boundary", per-slot disconnect mirror leak, A-6 host AllocManager<Npc>.
**Shape:** Replace per-type bespoke maps (`g_drives`, `g_npcElements`, `g_actorToNpcId`, `g_propMirrors`, `g_remoteNickBySlot`, `g_puppets`) with `element::Registry` as SOLE canonical owner, accessed exclusively through RAII handles (`ScopedElementRef<T>` holding the type-shard lock for its lifetime).
**Estimate:** 1-2 weeks. **Do BEFORE NPC 5N expansion lands more shape.** Foundation precondition for Door/Vehicle/Switch/Interactable: today's code would require each new entity type to add its own bespoke map + repeat all 5 race classes.

### PR-FOUNDATION-4: Host policy layer
**Closes:** Shared reliableInbox starvation (NEW-D3-4 + NEW-D9-9), senderPeerSlot==0 topology fragility, missed-angle "anti-griefing".
**Shape:** `HostPolicy` module owning (a) per-peer-slot inbound message rate counters per MsgType (sliding 30s window), (b) per-peer outbound bandwidth caps, (c) `KickPeer(slot, reason)` API → GNS CloseConnection with structured reason code surfaced as chat-feed line on kicked peer, (d) host-side banlist persisted to mod data folder, (e) admin chat commands `/kick /ban /unban /ratelimit` gated by `[host]` checkbox.
**Estimate:** 1 week. **Required before any master-server browser ships.**

### PR-FOUNDATION-5: Per-peer observability HUD + structured event stream
**Closes:** Missed-angle "user-perception observability", per-peer RTT hidden by min-fan-out, "I think it was laggy" bug reports.
**Shape:** F-key-toggled developer overlay per peer surfacing (a) GNS `GetConnectionRealTimeStatus` drill-down (ping/loss/queueDepth/outBps/inBps), (b) last 30s msg-type counts per direction, (c) per-lane queued bytes, (d) protocol epoch + nickname + Element id range, (e) latest 5 reliable msgs received timeline. Plus structured-event log line per significant lifecycle transition (peer join, mirror install, eid alloc/free, save fire, NPC spawn) with stable JSON-ish shape.
**Estimate:** 4-5 days. Foundation hinges on community testers reporting reproducible bugs.

---

## Cross-cutting themes (root-cause clusters)

Patterns that appeared across multiple dimensions. Each theme is a single architectural fix that closes N findings:

1. **ID-range trust boundaries are unenforced at BOTH the wire boundary and in-memory boundary.** Same root for E-1, the 3 PropSpawn/PropDestroy/Join range gaps, save-write angle, host-migration eid collision. → PR-FOUNDATION-1's `IsAllowedSenderEid` helper.
2. **Mirror lifecycle ownership is split across Registry + MirrorManager<T> + per-feature bespoke maps; every cross-cutting bug is a shear between these owners diverging.** T-2/T-5/E-3/N-3/N-5, dangling-actor-after-K2_Destroy, MirrorManager::Get UAF, Registry::Get UAF all live in this gap. → PR-FOUNDATION-3.
3. **Wire boundary trust is established once at Join then never re-validated; session state DOES drift.** senderContext 8-bit wraps at 255. Host-trust gate `senderPeerSlot==0` breaks under any relay topology. Range checks absent. A reconnecting peer with fresh nick + recycled eid + 256-ctx-rollover delivers a packet that passes EVERY gate while referring to dead identity. → PR-FOUNDATION-1.
4. **Hot-path discipline is implicit (game-thread-only) but never enforced.** T-7/T-8/T-10 "safe today but no lock boundary"; P-1/P-2/P-4/P-5/P-9 "no throttle"; ParamFrame ctor allocs per call; ProcessEvent detour 272 atomic loads. Every future subsystem inherits the implicit rules and introduces a new violation. → `class HotPathGuard` (debug-mode GT-assert + RAII throttle counter + bounded log helper) every new observer MUST take.
5. **Subsystem-bootstrap pattern is duplicated and inconsistent.** `npc_sync::Install` two-stage push, `weather_sync::Install` plain bool, `item_activate::Install` plain bool, `grab_observer::Install` plain-bool-no-countdown (P-2 fix added atomic latch only there). Each differs in throttling/retry/permanent-failure semantics. → `ue_wrap::install::Latch` primitive (atomic done + retry countdown + permanent-failure log threshold + GT-only assert).

---

## Missed angles (critic-identified)

Foundation-grade concerns that ALL 14 finder dimensions missed. Add to queue:

### M-A: Save-game safety / persistence corruption surface
VOTV is long-form horror; one corrupted save = hours lost + community trust evaporates. Nothing under `src/coop/` touches saves. Host has no PreSave hook to evict coop-only state. Crashes mid-coop autosave WILL persist peer puppets as mainPlayer_C orphans + synth-keyed props the SP loader was never designed to deserialize. → **PR-FOUNDATION-2.**

### M-B: Host migration / graceful host-quit handling
4+ players invested in a session; host quits → all 3 instantly lose progress. Today: host monolithic (host runs NPC AI, weather, item spawners, saves). Host's `Registry` peer-range eid state is held only host-side; new host inherits no allocation map → eid collision. MTA shipped `CMaster*` migration; we have zero references. → New phase TBD.

### M-C: Version skew between mod build and VOTV game patches
sdk_check warms 50+ offsets resolved at boot. Two peers running same mod against DIFFERENT VOTV patches pass the wire handshake then write/read mainPlayer_C fields at offsets that don't match the actual class. Concrete: client on 0.9.0-m + host on 0.9.0-n silently connect; client PropSpawn writes to flashlight offset `+0x1A8` which on 0.9.0-m maps to `sanity_C` → host's sanity bar randomly jumps when client picks up flashlight. → Add to Join handshake: exe sha256 + sdk_check pass-summary + mod git-rev; reject mismatch with plain-English error.

### M-D: Anti-griefing primitives (kick/ban/ratelimit at session level)
4+ player public sessions guarantee griefers will arrive. Today: zero host-side admin surface. No kick, no IP/eid ban list, no per-peer reliable-channel rate cap, no per-peer entity-spawn quota. Once master-server browser ships, random strangers join. → **PR-FOUNDATION-4.**

### M-E: Player-perception observability
Server-side log throttling covered, but user-facing observability gap missed: when a 4-player session goes wrong, only diagnostic is reading 3 separate log files post-mortem. Single RTT readout (R-2 fix made it min-across — even less informative). No bandwidth-up/down counter per peer; no per-lane queue depth; no GNS `SteamNetConnectionRealTimeStatus_t` drill (pingMs, outBitsPerSec, queuedReliableBytes — all available, none surfaced). → **PR-FOUNDATION-5.**

---

## Status of prior findings (from 2026-05-29 8-dim audit)

### CLOSED (shipped since 56d039a)

| Prior ID | Closed in | Evidence |
|---|---|---|
| T-2 | npc_mirror.cpp DrainClientMirrors GT-pump serialization | npc_mirror.cpp:357-368 |
| T-4 / E-7 | EstablishMirrorForSlot idempotent republish | players_registry.cpp:246-261 (a50e288) |
| E-2 | PropSpawn/PropDestroy senderContext + IsMirror gate (proto v15) | event_feed.cpp:273-287, 336-350 (bab1594) |
| E-5 | MirrorManager::Install atomic commit closes transient window | mirror_manager.h:117-128 |
| E-8 | playerBySlot_ assigned BEFORE atomic publish | players_registry.cpp:373-380 |
| W-3 | Join reject payloadLen<5 | player_handshake.cpp:218-223 (a50e288) |
| P-1 | grab_observer SetPhysics PRE throttle | grab_observer.cpp |
| P-2 | grab_observer Install atomic latch + 60-tick countdown | grab_observer.cpp |
| N-1 | PropDestroy/EntityDestroy → Bulk lane (was Normal) | session.cpp:105-108 (a50e288) |
| N-3 | UnregisterPuppet unconditional on disconnect | net_pump.cpp:202-209 (a50e288) |
| N-4 | MaybeSendJoinToSlot gated on IsSlotReady | event_feed.cpp:114-124 (a50e288) |
| N-5 | g_remoteNickBySlot cleared OnSlotDisconnected | player_handshake.cpp:189 (a50e288) |
| N-7 | connectedPeerCount AND-gates peerLanesConfigured_ | session_status.cpp:91-102 (a50e288) |
| R-1 | catch(...) removal + log-not-swallow with C++ exception types | game_thread.cpp:265-285 (a50e288 v2) |
| R-2 | HUD RTT min-across-all-peers, no early break | session.cpp:647-662 (a50e288) |
| R-6 | ENABLE_ICE OFF until PR-6 P2P ships | CMakeLists.txt:82 |
| M-1 (partial) | remote_prop / npc_sync / prop_lifecycle / session / engine.cpp under 800 cap | 5 commits (7125eff, bcafb18, 0857237, 45f4e4d, 35f2fe9, e15b1fc) |
| M-2 | GetDriveActor dead-shim deleted | remote_prop.cpp |
| M-3 | RotatorToQuat + GetWorldContext extracted to ue_wrap::engine | engine.h, engine.cpp (a50e288) |
| M-4 (re-broken) | Finding #N labels stripped — but reintroduced post-strip | a50e288 + reintroduced by later commits |
| M-6 | Stale "Inc3 will" / "NEXT" markers replaced | npc_sync.cpp, event_feed.cpp |
| M-7 | dev/pos_hud.cpp g_local dead write removed | pos_hud.cpp |
| A-1..A-5 | item_activate / CMC-read / USoundAttenuation / grab_observer / RemotePlayer::Spawn | 5 commits (3a71ce5, 4013b2e, 0cf0655, c930fca, a50e288) |
| A-5 (v2 fallback) | Registry::Local() + FindObjectByClass fallback in OMEGA window | remote_player.cpp:62-65 |

### SURVIVING (still real in current code)

| Prior ID | Severity | Why it survives | Current location |
|---|---|---|---|
| **T-1** | moderate | event_feed PropSpawn/PropDestroy still call OnSpawn/OnDestroy directly (no GT::Post) while EntitySpawn/EntityDestroy use GT::Post — asymmetric pattern footgun | event_feed.cpp:307, 356 vs 389 |
| **T-3** | low | DropPlayerElement_ stores kInvalidIdCtxPair BEFORE playerBySlot_.reset() → emits eid=0 in burst window | players_registry.cpp:396-403 |
| **T-5** | important | NpcSpawn_POST writes el->SetActor outside the mutex used for g_actorToNpcId insert | npc_sync.cpp:256-260 |
| **T-6** | moderate | PropMirrors race vs queued GT::Post — moot today but no structural guarantee | net_pump.cpp:253-256 vs 444 |
| **T-7** | moderate | Registry::GetPlayerElement / EnsurePlayerElement_ / DropPlayerElement_ unsynchronized; GT-only by convention | players_registry.cpp:186-189, 319-406 |
| **T-8** | moderate | g_localNick + g_remoteNickBySlot unsynchronized | player_handshake.cpp:24, 28 |
| **T-9** | moderate | K2_DestroyActor not atomic with IsLive in DrainClientMirrors / OnDisconnect | npc_mirror.cpp:336-352, 376-383 |
| **T-10** | low | g_drives module-scope no mutex | remote_prop.cpp:52 |
| **E-1** | **critical** | RegisterMirror still does not validate id range; with v13/v14 mirror handshake routinely exercised, collisions more reachable | registry.cpp:107-126 |
| **E-3** | important | Late-joiner snapshot Prop-only — no NPC, no held-prop, no door, no terminal, no item state | prop_snapshot.cpp:91; net_pump.cpp:232 |
| **E-4** | important | Host SendEntitySpawn failure still leaks host-range eid (no rollback) | npc_sync.cpp:415-422 |
| **E-6** | moderate | OnDisconnect drain ordering safe today, latent for same-process listen-server | npc_sync.cpp:507-532 |
| **W-1** | design-note | VerifySenderContext 0-on-either-side bypass duplicated to 3 sites; no seed-window timer | event_feed.cpp:62, 273-287, 336-350 |
| **W-2** | important | PropSpawn.physFlags reserved bits not rejected; PoseSnapshot.stateBits same shape | remote_prop_spawn.cpp:386, remote_player.cpp:545 |
| **W-4** | moderate | PropDestroy empty key check one layer deeper than PropSpawn — asymmetric defense | event_feed.cpp:329 vs remote_prop.cpp:588 |
| **W-5** | moderate | PacketHeader._pad, .token, 10 payload-internal _pads accepted with any content | protocol.h:353, 355, 415, 417 |
| **W-6** | low | Size checks use `<` instead of `==` — silent oversized truncation | event_feed.cpp:140, 193, 322, 365, 397, 439, 505, 641, 684, 729 |
| **N-2** | important | Connect-edge replay still no NPC branch (E-3 dup) | net_pump.cpp:226-241 |
| **N-6** | important | SetConnectionPollGroup skipped silently when hPollGroup_==0 → silent blackhole on Stop linger race | session_status.cpp:132-135, 168-171 |
| **N-8** | low | Pose stream lane 0 default (Lane::High), no IsSlotReady gate | session.cpp:615-639 |
| **N-9** | low | Stop linger triggers redundant HandleConnStatusChanged log spam | session.cpp:241-252 |
| **N-10** | low | SendReliable sends to all slots incl. originator | session.cpp:360-401 |
| **M-1 (residual)** | moderate | weather_sync.cpp 807 LOC, event_feed.cpp 800 LOC (at cap) — next entity type breaches both | weather_sync.cpp, event_feed.cpp |
| **M-5** | low | Redundant kMaxLinVel/kMaxAngVel/kMaxCoord/kMaxVel local constants vs protocol.h | event_feed.cpp:174-175, 219-220 |
| **A-6** | important | npc_sync host-side bespoke g_npcElements + g_actorToNpcId; NOT MirrorManager<Npc> | npc_sync.cpp:130-132 |
| **A-7** | low | MTA citation coverage essentially zero | 9 coop/ TUs missing file/fn-header MTA citations |
| **R-3** | important | NpcSuppress_Interceptor still depends on unconfirmed K2Node null-check BP codegen | npc_sync.cpp:459-477 |
| **R-4** | moderate | AmushroomSpawner_C interim retirement criterion (allowlist) unmet | remote_prop_spawn.cpp:91-128 |
| **R-5 (partial)** | low | Runtime UE_LOGE still mentions UE4SS — user-facing .txt was stripped, log not | sdk_check.cpp:418 |

---

## NEW findings by dimension (87 confirmed)

### D1 — Threading / Concurrency / Lifecycle (8 new)

- **D1-1 [important]** Tear-prone two-load `LocalPlayerElementId + LocalPlayerSyncContext` at 11 sender sites; paired `LocalPlayerIdentity()` exists but only used at 3. — `prop_lifecycle.cpp:281+285, 348+352, 442+446`; `weather_sync.cpp:131+138`; `weather_redsky.cpp:73+79, 117+123`; `weather_lightning.cpp:71+77`; `item_activate.cpp:165+172`; `player_handshake.cpp:150+164`. CONFIRMED. Mechanical replace.
- **D1-2 [important]** `g_takeObjInFlight` process-global; clobber race across concurrent parallel-anim takeObj. — `prop_lifecycle.cpp:70, 366, 373, 190`. Should be `thread_local` matching `t_pendingNpc` precedent.
- **D1-3 [critical]** `MirrorManager<T>::Get()` returns raw T* with lock released → caller-side UAF latent across all future entity types. — `mirror_manager.h:162-166`. **Foundation precondition** for vehicles/doors/AI. Fix: return `shared_ptr<T>` or RAII handle.
- **D1-4 [critical]** `element::Registry::Get()` returns raw Element* with lock released → identical UAF surface. — `registry.cpp:144-148`. Same fix as D1-3.
- **D1-5 [important]** Host SendEntitySpawn failure leaks host-range Element id (E-4 unremediated, surfaces under 4-peer reliable backpressure). — `npc_sync.cpp:393-422`.
- **D1-6 [moderate]** `Registry::Local()` does full GUObjectArray scan on cache miss; 12 sites without local-cache. — `players_registry.cpp:106-112` + `item_activate.cpp:642`, `weather_lightning.cpp:196`, `harness.cpp:225` etc. Single `mainPlayerAtomic_` set by mainPlayer_C spawn/possess observer (MTA shape).
- **D1-7 [moderate]** Per-slot disconnect doesn't drain client-side NpcMirrors/PropMirrors for that slot — 4-peer rolling reconnect leaks accumulate. — `net_pump.cpp:201-224`. Mirrors should be slot-tagged with per-slot eviction API.
- **D1-8 [moderate]** `npc_sync::Install` + `npc_mirror::SetClientRefs` two-stage push has TOCTOU between resolve and Install across parallel-anim ticks. — `npc_sync.cpp:537-714` + `npc_mirror.cpp:54-60`.

### D2 — Element / Registry / Mirror lifecycle (8 new)

- **D2-1 [critical]** `MirrorManager<T>::Install` accepts any eid in `[1, kMaxElements)` — no host-range enforcement. Every new Vehicle/Door/AI type inherits E-1's collision hazard. — `mirror_manager.h:79-130`. Validation belongs at the lowest layer.
- **D2-2 [critical]** `PropSpawn` / `RegisterPropMirror` does NOT validate elementId range; client-sourced PropSpawn with host-range eid collides with host's allocator. Asymmetric with `npc_mirror.cpp:75-82` which DOES validate. — `event_feed.cpp:188-308` + `remote_prop.cpp:520-541` + `remote_prop_spawn.cpp:174,193,221,272,410`.
- **D2-3 [important]** Prop mirror Element holds dangling actor pointer after local K2_DestroyActor; SnapshotActorsByType reads it. — `prop_element_tracker.cpp:147-170`; `remote_prop.cpp:530-541`; `registry.cpp:163-174`.
- **D2-4 [important]** syncContext byte wraps at 255 with no reconnect-storm protection; aliased generation accepts stale packets. — `players_registry.cpp:42-65`; `element.h:142-147`. Either widen to uint32_t (PR-FOUNDATION-1) or kill old generation on reconnect.
- **D2-5 [important]** Late joiner has NO NPC snapshot AND no door/item/interactable convergence — E-3 generalizes to every future host-authoritative type. — `net_pump.cpp:232-241`. Unified `Registry::SnapshotByType` orchestrator missing.
- **D2-6 [important]** Host SendEntitySpawn failure path orphans engine actor (no rollback, no retry). — `npc_sync.cpp:415-425`. Foundation rule: host-allocated wire identities are transactional; need RAII guard.
- **D2-7 [moderate]** `Registry::SnapshotActorsByType` holds m_mutex across full 65536-slot scan; concurrent 4-peer joins serialize through one mutex. — `registry.cpp:163-174`. Per-type linked-list (MTA `CClientEntity::AttachedList` shape) → O(live-of-type).
- **D2-8 [moderate]** Local-allocated Player Element 'placeholder' published into atomic identity, then re-published when mirror swap arrives — net-thread readers can observe non-mirror placeholder eid. — `players_registry.cpp:319-385, 225-317`.

### D3 — Wire protocol / payload validation / security (7 new)

- **D3-1 [important]** `EntitySpawn`/`EntityDestroy` have NO senderContext field — stale-gen defense gap on the only host-authoritative NPC packets. Reclaim from padding. — `protocol.h:598-619`; `event_feed.cpp:359-418`.
- **D3-2 [important]** `PropSpawn`/`PropDestroy` payload.elementId not range-validated — client can mint host-range eids. — `event_feed.cpp:190-308, 310-358`. Single `IsValidSenderEid(slot, eid)` helper.
- **D3-3 [important]** `Join`'s senderElementId not range-validated — client can install HOST-range mirror in its own peer slot. Asymmetric with `AssignPeerSlot` which gates via `IsHostId(p.hostElementId)`. — `player_handshake.cpp:242-246`.
- **D3-4 [important]** Single shared `reliableInbox_` cap=8192 — one flooding peer starves all others (no per-peer fairness). — `session.cpp:521-526`. → per-peer deque + round-robin drain (MTA `CNetServer::ProcessIncomingPacket`).
- **D3-5 [moderate]** Host-trust gate via `senderPeerSlot==0` is topology-fragile — breaks under any future relay/forward path. 8 gate sites. — `event_feed.cpp:375,405,426,449,654,697,746` + `player_handshake.cpp:299`. Migrate to senderElementId-based via `Registry::Get(senderElementId)->PeerSlot()==0`.
- **D3-6 [moderate]** `PacketHeader.token` always 0 under GNS — 8 unused bytes per packet (32 KB/s LAN waste at 4 peers × 125Hz). v15→v16 transition window to reclaim. — `protocol.h:355`; `session.cpp:324, 374, 435, 621, 631`.
- **D3-7 [moderate]** PoseSnapshot + PropPoseSnapshot have NO senderContext — stale-gen reconnect window for pose streams. — `protocol.h:385-394, 447-454`.

### D4 — Performance / hot paths (8 new — 2 CRITICAL)

- **D4-1 [critical]** `ParamFrame` ctor allocates 2 vectors + per-param wstrings on EVERY UFunction call — **engine wrapper foundation tax**. Profile would show malloc/free + std::wstring dominating steady-state pump tick. — `call.cpp:10-30`; `reflection.cpp:274-293`. Fix: cache `(fn → vector<{wstring,offset}>)` in UFunction reflection metadata + thread-local scratch `buf_`.
- **D4-2 [critical]** `ProcessEvent` detour walks 128+128+16 = **272 atomic acquire-loads per UFunction dispatch**; at UE4-typical 100k PE/sec × 4 peers = 100M+ atomic loads/sec. — `game_thread.cpp:131-139, 193-202`; `game_thread.h:122`. Fix: hash-map keyed on `function*` OR per-UFunction registered-observer flag + atomic counter zero-skip.
- **D4-3 [important]** `Registry::SnapshotActorsByType` walks 65536 element slots under mutex on every prop snapshot drain. (See D2-7.) — `registry.cpp:163-174`; `prop_snapshot.cpp:91`.
- **D4-4 [important]** `prop_snapshot::DrainChunk` allocates 2 wstrings + 2 UFunction dispatches per candidate; multiplies by peer count on connect storm. — `prop_snapshot.cpp:190, 203, 212-213`. Cache `(UClass* → wire className bytes)` + `(actor → key bytes)` at MarkKnownKeyedProp time.
- **D4-5 [important]** `Session::SendReliable` fan-out allocates fresh GNS message per peer in loop with no batching. — `session.cpp:348-401`. GNS supports batched `SendMessages(kMaxPeers, msgs[], ...)`.
- **D4-6 [important]** `ResolveLightIntensityFn`/`ResolveSceneVisibilityFn`/`ResolveSpotConeFns` walk GUObjectArray on every miss — no negative-cache. — `engine_mainplayer.cpp:69-89`. Atomic-latch+countdown pattern OR eager `WarmupCache()` at boot.
- **D4-7 [moderate]** `Registry::Local()` re-validates with `GetController` UFunction dispatch on every call site invocation. — `players_registry.cpp:106-112`. Invalidate on level transition not per read.
- **D4-8 [moderate]** `remote_prop::Tick` scales O(kMaxPeers) × engine-call cost regardless of how many peers actually hold props. — `remote_prop.cpp:310-378`. Early-exit + active-drives bitmask.

### D5 — Modularity / file size / dead code / comment rot (8 new)

- **D5-1 [important]** `event_feed.cpp` 13-case dispatcher AT EXACTLY 800 LOC — next entity type guarantees soft-cap breach. No abstraction layer. — `event_feed.cpp:132-797`. Table-driven dispatch (kind → {payloadSize, requiresHostTrust, payloadValidator, handler}) lets each subsystem own its handler in its own TU.
- **D5-2 [moderate]** `weather_sync.cpp` 807 LOC monolith — mixes scheduler observation, mutator install, host/client interceptors, broadcast queue, 4 DebugForce* dev APIs. — `weather_sync.cpp:1-807`. Ripe for `weather_scheduler.{h,cpp}` extraction.
- **D5-3 [moderate]** **CLAUDE.md `Existing oversized files` catalog is 100% stale** — all 3 listed files are now under cap. New oversized files (weather_sync 807, event_feed 800) not in catalog. — CLAUDE.md:188-192. → Auto-generate from `wc -l` at audit-prompt time.
- **D5-4 [moderate]** `harness.cpp::TimelineThread` is a single ~450 LOC function — `harness.cpp:290-738`. Blocks parallel scenario authoring.
- **D5-5 [low]** `item_activate.cpp:254-260` stale "Inc6 of the 5F plan" deferral with no work item. Crank lanterns silently not replicated.
- **D5-6 [low]** `weather_sync.cpp` + `event_feed.cpp` peppered with `Phase 5W IncN` / `Inc-fix-2` references describing shipped code. M-4 strip recurring.
- **D5-7 [low]** Inconsistent `E::` vs `ue_wrap::engine::` vs `::coop::weather_sync::` qualification across coop/. No canonical style.
- **D5-8 [design-note]** 13-case ReliableKind switch is the ONLY entity-type extension point; no per-type handler registration. (See D5-1.)

### D6 — Architecture / Principle 7 / MTA fidelity (4 new)

- **D6-1 [important]** `remote_player.cpp:493-520` still leaks P7 — raw `AmainPlayer_lag_fl` deref + raw `USceneComponent_RelativeRotation` write + bare FindClass/FindFunction at runtime. Per-puppet hot path × 4+ peers. — Extension: `WriteMainPlayerLagFlashlightPitch` + `ReadMainPlayerLagFlashlight` wrappers in `ue_wrap::engine`.
- **D6-2 [important]** UPrimitiveComponent engine-substrate UFunctions (`SetSimulatePhysics`, `SetPhysicsLinearVelocity`, `SetPhysicsAngularVelocity`) hand-resolved in `remote_prop.cpp` at gameplay layer — no ue_wrap::engine wrapper. — `remote_prop.cpp:114-160`. Next entity type (Vehicle/Door) faces copy-paste vs reach-across-anon-ns.
- **D6-3 [important]** `weather_sync.cpp` has 21 raw `P::off::AdaynightCycle_*` derefs + ParamFrame/Call to AdaynightCycle UFunctions — **entire subsystem is a P7 leak**. — `weather_sync.cpp:142-163, 468, 555, 721-736`. Same shape as A-3 SoundAttenuation. Extract: `ue_wrap::engine::ReadDayNightCycleState/WriteDayNightCycleState` + dispatcher wrappers. Closes 21+18 leaks + file-size pressure in one extraction.
- **D6-4 [moderate]** Spawn-actor dispatch pattern (`BeginDeferredActorSpawnFromClass + FinishSpawningActor`) duplicated across `remote_prop_spawn`, `npc_mirror`, `weather_redsky`, `weather_lightning` — no shared `ue_wrap::engine::SpawnActorOfClass` wrapper. Foundation pattern for every replicated entity type. — Cite MTA `CClientEntityFactory`.

### D7 — Net layer / disconnect / late-joiner / lanes (8 new — 2 CRITICAL)

- **D7-1 [CRITICAL] Star topology has NO host-side relay — client-A pose/PropPose/ItemActivate never reach client-B/C.** Foundation-blocking for 3+ peers. — `session.cpp:300-402` (no relay logic). Either host-relay (MTA `CGame::Relay*`) or mesh. **Without this fix the 4+ player coop is fundamentally broken for any peer-originated state.**
- **D7-2 [CRITICAL]** Per-slot connect-edge replay is HOST-state-only — new client never receives EXISTING peers' state. Compound failure with D7-1. — `net_pump.cpp:226-241`; `item_activate.cpp:632-678`; `weather_sync.cpp:564-592`; `prop_snapshot.cpp:74-103`. → Host owns canonical mirror of every peer's persistent state (MTA `CGame::SendNewElementSpawnPackets`).
- **D7-3 [important]** OnDisconnect inbox erase is O(N) under reliableInboxMutex_ blocking GNS net thread receive loop. — `session_status.cpp:253-259, 268-270`. Per-slot deque + swap-and-clear-outside-lock.
- **D7-4 [important]** Stop() linger trampoline dispatches into HandleConnStatusChanged with stale state — benign today, foundation hazard tomorrow. — `session.cpp:220-256`. Add explicit `stopping_` flag set BEFORE linger loop.
- **D7-5 [important]** No GNS reliable-queue depth observability — snapshot fan-out to 3 peers can silently saturate GNS internal queue. — `session.cpp` SendReliable + `prop_snapshot.cpp` DrainChunk. → Per-tick `GetConnectionRealTimeStatus.m_cbPendingReliable` read + back-off.
- **D7-6 [important]** SendReliable fan-out has no per-slot success accounting — returns `anySuccess`; dead peer makes whole call appear successful. — `session.cpp:360-401`. → return `uint8_t failedSlotMask`.
- **D7-7 [important]** Pose-stream lane hardcoded to lane 0 (default) — under burst it head-of-line-blocks behind High-priority reliables sharing the same lane. — `session.cpp:623-636`. → Dedicate Lane::High to pose OR add Lane::Pose at priority 0.
- **D7-8 [important]** Slot-reuse on rapid client disconnect/reconnect can install fresh peer into SAME slot prior peer occupied mid-replay drain. Fragile per-slot cleanup ordering. — `session_status.cpp:115`; `prop_snapshot.cpp:160-165`; `players_registry.cpp:225-302`. Per-slot generation counter (MTA `CPlayer::m_uiPlayerCounter`).

### D8 — RULE 1 / RULE 2 / RULE 3 compliance (7 new)

- **D8-1 [moderate]** Per-TU FindClass cache duplication across engine_*.cpp split TUs — no shared resolver. Documented trade-off vs architecturally-correct path. — `engine_mainplayer.cpp:48-56` vs `grab_observer.cpp:214` vs `autotest.cpp:99`.
- **D8-2 [moderate]** Stale "for now" comment in npc_sync hot path — `npc_mirror::OnEntityDestroy` already ships. — `npc_sync.cpp:290-292`.
- **D8-3 [low]** Comment rot reintroduced post-QUAD-audit M-4 strip: "Finding #N" / "audit-N" labels in fresh code (remote_prop, remote_prop_spawn, npc_mirror).
- **D8-4 [low]** weather_sync "visible-particle gap is now an OPEN problem" undated TODO without retirement-criterion.
- **D8-5 [low]** `autotest.cpp` + 4 RunAutonomous* paths ship in DLL despite RULE 3 "autonomous test harness ... does not ship" language. — `autotest.cpp` + `CMakeLists.txt:149`. → `#ifdef VOTVCOOP_DEV_BUILD` gate.
- **D8-6 [moderate]** R-3 NpcSuppress IDA-confirm aged 5+ days without IDA work despite available `ida-pro-mcp` tooling. RULE 1 root-cause fix mechanically available. — `npc_sync.cpp:466`. Foundation hazard: every new entity type that uses Suppress_Interceptor inherits unverified codegen assumption.
- **D8-7 [design-note]** `remote_prop_spawn.cpp:10` "class workaround" phrasing for AmushroomSpawner without single tracked retirement registry.

### D9 — Player-count scaling (4 new — 4 CRITICAL)

- **D9-1 [CRITICAL]** **Host does not relay client-originated PoseSnapshot / PropPose between clients — clients mutually invisible at 3+ peers.** (Same as D7-1.) — `session.cpp:467-503, 604-641`. Confidence 98%.
- **D9-2 [CRITICAL]** **Host does not propagate cross-peer Player Element identity — clients cannot resolve senderElementId of peer-clients.** B1/A4 wire migration only wired host↔client direction. 4 peers = 6 client-pair edges; only 3 host-pair edges work. — `player_handshake.cpp:134-180, 198-272`. Need `CGame::JoinPlayerComplete` broadcast equivalent.
- **D9-3 [CRITICAL]** **Late-joiner sees NO state from peer-clients (only host state).** (Same as D7-2.) — `net_pump.cpp:232-240`.
- **D9-4 [CRITICAL]** Pose stream outbound bandwidth at host O(P²) and uses ALL peer connections including stale slots. At 8 peers (when scaled) ~900 KB/s host upstream just for poses. — `session.cpp:614-639`. Per-slot lane gating + relay-O(P²) awareness; per-peer rate adaptation (MTA `CClientPlayerManager::DoPulses`).
- **D9-5 [important]** Snapshot drain serializes across slots — 4 simultaneous joiners wait O(P) × ~14s for full Prop seed. — `prop_snapshot.cpp:136-148, 242-261`. Single enumeration cached + fan-out.
- **D9-6 [important]** `FindFreePeerSlotForClient` linear scan without slot-allocator atomicity under concurrent accept. — `session_status.cpp:62-69, 104-148`. CAS-based scan+CAS.
- **D9-7 [important]** TickConnect drains bounded by kMaxPeers but called every 8ms — 4-peer × per-tick atomic-pair loads compound; each new subsystem adds 4-slot scan. — `item_activate.cpp:680-702`; `event_feed.cpp:87-126`.
- **D9-8 [design-note]** ReliableMessage inbox single shared deque — no per-sender fairness/backpressure. (See D3-4.)

### D10 — State synchronization completeness (5 NEW — all CRITICAL)

- **S-1 [CRITICAL]** **NPC pose stream does not exist** — clients see kerfurs/zombies running independent local AI from spawn point, diverging from host immediately. No `SendEntityPose` / `EntityPose` ReliableKind in protocol.h. Combat/horror loop incoherent. — `npc_sync.cpp` (no SendEntityPose); `npc_mirror.cpp:300` spawns full BP, no `SetActorTickEnabled(false)`. Either host-authoritative pose (MTA `CClientPed`) + client tick disabled, OR per-NPC ownership transfer. Scope item "NPCs sync" unimplemented despite being marked SHIPPED-prerequisite.
- **S-2 [CRITICAL]** **Player health/vitals/death/respawn not replicated** — peers see remote walking around dead, or alive after fatal hit. — `protocol.h` (no PlayerVitalsState / PlayerDeath / Respawn). Only F3 dev-key RestoreVitals broadcast exists.
- **S-3 [CRITICAL]** **Ownership transfer absent** — peer A and B both press E on same prop same frame; both run divergent PropPose streams; oscillation; no recovery except disconnect. — `grab_observer.cpp` (no holder-slot arbitration); `remote_prop.cpp:272` FindSlotByKey assumes 1-holder/key without enforcing. Need GrabRequest → GrabGranted/Denied handshake (MTA `m_pSyncedElement` ownership).
- **S-4 [CRITICAL]** **Late-joiner snapshot misses NPC roster, RedSky active, held-prop transitive state, day/time, peer-flashlight** — 30 min into session, joining peer sees parallel universe of partial truth. (Generalizes E-3.) — `prop_snapshot.cpp:91`; `net_pump.cpp:232`.
- **S-5 [CRITICAL]** **Peer-to-peer state replay missing** — host's `QueueConnectBroadcastForSlot` reads ONLY `Registry::Local()` flashlight; doesn't replay peer A's last-stored ItemActivate to peer C. — `item_activate.cpp:632`. Symmetric problem for every future per-peer state.

### D11 — Diagnostics / Observability (8 new — 1 CRITICAL)

- **D11-1 [CRITICAL]** Session exposes only aggregate RTT/sent/recv — no per-peer stats for 4+ player diagnosis. min-across-peers RTT actively HIDES worst peer. — `session.h:128-130`; `session.cpp:643-663`; `event_feed.cpp:87-96`.
- **D11-2 [important]** Logs lack per-peer correlation — only 15 of 481 coop UE_LOG calls carry `slot=` or `peer=`. Reconstructing peer-3 timeline = multi-day. → `LOG_PEER(slot, ...)` macro + convention.
- **D11-3 [important]** No drop/malformed-packet/protocol-mismatch counter — each subsystem logs locally, no aggregate view. → `Session::stats()` per-MsgType per-peer struct.
- **D11-4 [important]** Log file TRUNCATES on every launch — no rotation, no history. Yesterday's crash log GONE after one game restart. — `log.cpp:57` (`_wfsopen(path, L"w", ...)`). → 3-file rotation `.log + .1.log + .2.log`.
- **D11-5 [important]** No log-level filter — INFO runs unconditionally at 80+/sec steady-state — no field-deployable verbosity dial. → INI-tunable per-subsystem verbosity.
- **D11-6 [important]** HUD feed surfaces only join/leave — protocol mismatch, kick reasons, connect failures invisible to user. → `hud_feed::Push` from session_status disconnect-edge with close-reason string.
- **D11-7 [moderate]** No runtime console/command surface — diagnostic-state inspection requires shipping new build. → `/coopstat /coopdumpregistry /coopdumppeers` via UE Console binding OR ImGui overlay.
- **D11-8 [moderate]** `hud_feed::Push` has no mutex — silently relies on undocumented "all callers on game thread" invariant. — `hud_feed.cpp:74-79`. Foundation: MUST be safe from net thread (when D11-6 lands).

### D12 — Extensibility / API surface for new entity types (8 new — 2 CRITICAL)

- **D12-1 [CRITICAL]** Late-joiner snapshot hardwired to ElementType::Prop; every new entity type ships with no convergence path. — `prop_snapshot.cpp:91-92`; `prop_snapshot.h:30`. Generic `Registry::SnapshotByType` exists; drain/chunk/lane DRIVER is hardcoded.
- **D12-2 [CRITICAL]** Adding new entity type requires editing 10+ files; no plugin/registration surface. User scoped "add Door sync" as 1-day task; reality is multi-day. — Cross-cutting: protocol.h, event_feed.cpp, session.cpp::LaneForKind, element.h, element/<type>.h, <type>_sync.cpp (~400 LOC), <type>_mirror.cpp (~280 LOC), net_pump.cpp, sdk_profile.h, snapshot. → Subsystem `Register()` API + `SyncStrategy<T>` template alongside `MirrorManager<T>`.
- **D12-3 [important]** `BeginDeferred/FinishSpawn/GameplayStatics CDO` UFunction cache duplicated per-receiver; third type means third copy + drift. — `npc_mirror.cpp:42-46` vs `remote_prop_spawn.cpp:42-46`. → `ue_wrap::engine::DeferredSpawn` helper.
- **D12-4 [important]** `event_feed.cpp` ReliableKind switch is 700-LOC monolith with no type dispatch table. (Same as D5-1.) → Registration-based dispatch.
- **D12-5 [important]** Per-slot disconnect fan-out is hand-edited list; missing one leaks per-slot state silently. — `net_pump.cpp:222-223` (only remote_prop + item_activate wired). → `Registry::ForEachSubsystem(&OnDisconnectForSlot)`.
- **D12-6 [important]** ItemActivate is one-class-per-hash dispatch — "unified item-state" packet doesn't unify (hardcoded flashlight). — `item_activate.cpp:495, 512`. → Per-class strategy registry.
- **D12-7 [important]** Wire payloads have no shared header struct — senderContext/elementId/senderPeerSlot redeclared per-payload (8 payloads). → `struct WireEntityHeader { uint32 senderElementId; uint8 senderContext; uint8 pad[3]; }` + compile-time static_assert.
- **D12-8 [moderate]** Element subclasses (Npc, Prop, Player) are type-tag wrappers with no per-type policy hooks. Adding behavior leaks specialization into call sites. → Per-type SnapshotPolicy/LaneForSpawn/AllowlistedClasses/Factory virtual interface.

### D13 — Testing harness coverage + hands-on gates (7 new — 3 CRITICAL)

- **D13-1 [CRITICAL]** **mp.py smoke verdict is process-liveness only — does NOT validate session ever connected, paired, or exchanged any packet.** Future commit silently breaks session handshake; both peers boot, never reach Connected, sit idle 30s; mp.py reports PASS. — `tools/mp.py:534-568`. Every memory says "Smoke PASS state=2 sent=620 recv=642" — those numbers came from MANUAL log tail reading, not mp.py parsing.
- **D13-2 [CRITICAL]** **Autonomous smoke does NOT exercise ANY gameplay subsystem** — grab, flashlight, weather, NPC, PropSpawn, takeObj, teleport, vitals all structurally validated only. All 4 autotest scenarios gated on env vars that mp.py never sets. Every A-1 through A-4 memory verbatim says "autonomous smoke does NOT exercise [path], structural-only validation."
- **D13-3 [CRITICAL]** **No 4+ player coverage anywhere — mp.py smoke hardcoded host+single-client.** client2 launcher exists but unused. No 3-peer scenario. No late-joiner test. Project goal is 4+ player coop; current smoke launches exactly 2 processes.
- **D13-4 [important]** Two parallel test harnesses (mp.py smoke + lan-test.ps1) with NO shared verdict — autotest scenarios + log-parsing verdicts only exist in OLD harness. RULE 2 violation (parallel implementations of one concept).
- **D13-5 [important]** No autonomous scenario exercises disconnect/reconnect/mid-game peer churn — N-3/N-5/T-4/E-7/N-4 all peer-churn fixes ship without churn coverage.
- **D13-6 [important]** Autotest scenarios fire-and-forget threads with no completion signaling — mp.py / lan-test cannot tell when scenario finished or whether asserted. → Structured `TEST [name] {PASS|FAIL|SKIP} reason=...` protocol.
- **D13-7 [important]** Smoke does not parse logs for `[Warn]/[Error]/SEH crash dumps` — pre-deploy checklist step 5 enforced by human eyeball only. Pattern that caused 19GB-RAM hang ship.

### D14 — Documentation / catalog rot / memory consistency (7 new — 3 CRITICAL)

- **D14-1 [CRITICAL]** **MEMORY.md is 83KB vs 24.4KB harness truncation limit — system has ALREADY truncated it on load.** Every fresh session loses ~57 of 61 entries. Load-bearing rules (`feedback-clean-rebuild-after-global-move`, `project-coop-4-player-target`, `project-coop-scale-100-entities`) silently missing from context.
- **D14-2 [CRITICAL]** **CLAUDE.md "existing oversized files" catalog 100% stale** — all 3 listed files refactored below soft cap; new oversized (weather_sync 807, event_feed 800) not in catalog. Audit prompts referencing it greenlight growth on real bloat while flagging non-existent. → Auto-generate from `wc -l` at audit-prompt time.
- **D14-3 [CRITICAL]** **COOP_SCOPE.md still declares 2 players (host + one client)** while user's foundation rule + memory state 4+ players target + live code has `kMaxPeers=4`. Fresh Claude reads canonical scope doc, derives 1v1 mental model, under-provisions all new subsystem datastructures. — `COOP_SCOPE.md:20-23`.
- **D14-4 [important]** ROADMAP.md Phase 3 references vanished `transport.cpp` + `reliable_channel.cpp`; entire net stack migrated to GameNetworkingSockets but ROADMAP still describes pre-PR4 hand-rolled UDP. — `ROADMAP.md:83, 99`.
- **D14-5 [important]** MEMORY.md index entries massively exceed 200-char rule — avg 911 chars, 60/61 entries over, top entry 3672 chars (18x over budget). Root cause of D14-1.
- **D14-6 [important]** ROADMAP "Phase 5N1 Inc3 NPC client-mirror queued" but npc_mirror.{h,cpp} has shipped — ROADMAP doesn't reflect shipped state.
- **D14-7 [moderate]** `feedback_modular_file_size_rule.md` duplicates CLAUDE.md catalog with identical stale data + adds `sdk_profile.h '739 LOC'` claim (actually 1123, +52%). Two sources of truth, no sync mechanism.
- **D14-8 [moderate]** Memory directory contains ~25 stale per-session entries from 2026-05-24..05-28 referencing files/decisions refactored. No purge discipline. Read-tool safety rail fires but doesn't GC.

---

## Severity tally (NEW + SURVIVED, excluding CLOSED)

| Severity | New | Surviving | Total |
|---|---|---|---|
| CRITICAL | 17 | 1 (E-1) | **18** |
| IMPORTANT | 47 | 12 | **59** |
| MODERATE | 19 | 9 | **28** |
| LOW / DESIGN-NOTE | 4 | 8 | **12** |

Of CRITICAL findings: 11 directly block the 4+ player foundation (D7-1, D7-2, D9-1, D9-2, D9-3, D9-4, S-1, S-2, S-3, S-4, S-5). 7 block extensibility / observability / testing (D1-3, D1-4, D2-1, D2-2, D4-1, D4-2, D11-1, D12-1, D12-2, D13-1, D13-2, D13-3, D14-1, D14-2, D14-3, E-1).

---

## Next-session work-list (priority-ordered)

After compact, the next session should pick from this list. Order is foundation-criticality.

### Tier 0 — Documentation hygiene (unblocks every future session)
These cost <1 hour each and prevent the next session from making decisions under stale context.
1. **D14-3** Update COOP_SCOPE.md to declare 4+ players (host + N clients).
2. **D14-1 + D14-5** Rewrite MEMORY.md index entries to ≤200 chars (one-line pointers, detail in topic files). Target file size <12 KB.
3. **D14-2** Replace CLAUDE.md oversized-files catalog with auto-generated wording: "Run `wc -l` on touched files; flag any past 800 LOC for extraction proposal."
4. **D14-4 + D14-6** Refresh ROADMAP.md: replace `transport.cpp`/`reliable_channel.cpp` with GNS lane vocabulary; mark Phase 5N1 Inc3 partially shipped (NPC client-mirror + EntityDestroy done; EntityPoseBatch remains).
5. **D14-7 + D14-8** Purge stale `project_session_2026_05_2[4-8]_*.md` files describing refactored state; delete `feedback_modular_file_size_rule.md` catalog block (replace with pointer to CLAUDE.md).

### Tier 1 — Identity epoch (PR-FOUNDATION-1)
Closes 6+ findings across 3 dimensions in one shape.
6. Replace 8-bit senderContext with 32-bit per-peer session-epoch. Reclaim from `PacketHeader.token` (8 unused bytes).
7. Add `bool ValidateSender(slot, eid, epoch)` helper; invoke at every receive site.
8. Move host-trust gate from `senderPeerSlot==0` to `Registry::Get(senderElementId)->PeerSlot()==0`.

### Tier 2 — Cross-peer relay topology (D7-1 + D7-2 + D9-1..D9-3)
**This is the most important architectural decision blocking everything else for 3+ players.** Pick one:
- **Option A: Host relay** (MTA shape, recommended). Host receives client-originated PoseSnapshot/PropPose/ItemActivate, decides routing, forwards to other peers. Bandwidth O(P²) at host but no NAT issues. Cite `reference/mtasa-blue/Server/mods/deathmatch/logic/CGame.cpp Relay*`.
- **Option B: P2P mesh.** Each peer connects to every other peer. Bandwidth distributed but requires NAT traversal (ICE; currently OFF per R-6). Reactivates ENABLE_ICE.
- Implementation phase includes broadcasting `PlayerJoined(slot, elementId, ctx, nick)` to all existing peers on Connecting + replay of every peer's state to late-joiners.

### Tier 3 — Manager pattern collapse (PR-FOUNDATION-3)
9. `ScopedElementRef<T>` RAII handle replacing raw `Registry::Get` / `MirrorManager::Get`.
10. Migrate `g_drives`, `g_npcElements`, `g_actorToNpcId`, `g_propMirrors`, `g_remoteNickBySlot`, `g_puppets` into Registry/MirrorManager.
11. Closes D1-3, D1-4, T-2, T-5, T-7, T-8, T-10, mirror lifecycle UAF class. **Do BEFORE NPC 5N expansion.**

### Tier 4 — State-sync completeness (S-1..S-5)
12. EntityPose ReliableKind + host-authoritative NPC pose stream (S-1).
13. PlayerVitalsState payload (S-2).
14. GrabRequest/GrabGranted handshake for ownership arbitration (S-3).
15. Generalized Registry-driven late-joiner snapshot (S-4 + S-5 + D12-1).

### Tier 5 — Save-game safety (PR-FOUNDATION-2)
16. PreSaveScrub + PostSaveRestore hooks.
17. Atomic-rename save discipline.
18. Active save → backup/ on session start.

### Tier 6 — Performance hot paths (D4-1, D4-2)
19. ParamFrame metadata cache + thread-local scratch buf_ (D4-1).
20. ProcessEvent detour observer fast-path (D4-2).
21. SnapshotActorsByType per-type linked-list (D2-7 + D4-3).

### Tier 7 — Host policy + observability (PR-FOUNDATION-4, 5)
22. HostPolicy module + `KickPeer(slot, reason)`.
23. Per-peer stats overlay + structured event stream.

### Tier 8 — Testing
24. mp.py log-parse verdicts (D13-1).
25. Smoke env-vars to exercise grab/flashlight/weather/redsky autotest scenarios (D13-2).
26. 4-peer smoke variant (D13-3).
27. Disconnect/reconnect churn scenario (D13-5).

### Tier 9 — Remaining Principle 7 closures (D6-1, D6-2, D6-3)
28. `remote_player.cpp` lag_fl wrapper.
29. UPrimitiveComponent physics wrappers in ue_wrap::engine.
30. weather_sync AdaynightCycle wrapper extraction (also closes weather_sync 807 LOC).

### Tier 10 — Misc
31. R-3 IDA-confirm NpcSuppress K2Node null-check codegen (D8-6).
32. event_feed.cpp dispatch table (D5-1, D12-4).
33. Per-TU FindClass cache → shared resolver (D8-1).
34. R-5 strip UE4SS string from runtime log path.

---

## Methodology

- 14 dimensions, 14 parallel finder agents, each scoped by prior IDs from the 8-dim audit + new "foundation" angles.
- Each finder returned: closedFromPrior (with evidence), survivingFromPrior, newFindings (up to 8 each).
- 99 non-clean new candidates → 1-vote adversarial verify → 87 survived (12 REFUTED).
- Completeness critic identified 5 missed angles + 5 cross-cutting themes + 5 PR-FOUNDATION recommendations.
- This doc replaces the prior 8-dim audit. Subsystem owners may reference prior IDs for archaeology.

**Workflow run:** `wkgh1i3ye` (114 agents, 32 min wall-clock, ~7.8M subagent tokens). Full raw output at `tools-results/wkgh1i3ye.output` (650 KB).
