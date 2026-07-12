# Stage 2 — 82 missing mirrors (host 1264 broadcasts → client 1182 spawns)

Date: 2026-05-27
Logs analyzed:
- HOST: `Game_0.9.0n/.../votv-coop.log` (session 21:19:05 – 21:21:23)
- CLIENT: `Game_0.9.0n_copy/.../votv-coop.log` (session 21:19:29 – 21:21:23)

## Headline counts (from logs)

- Host replay summary: `broadcast snapshot for 1264 live non-prop entities` (skipped 9 CDOs). Source `non_prop_entity_sync.cpp:668`.
- Host class-table install completed at **21:19:19** (`all 9 classes installed`), 27 s before replay at **21:19:46**. **Class table was fully populated before broadcast began** — eliminates the "host skipped untracked classes" hypothesis (specific point 6).
- Host retry queue at OnDisconnect: `dropped 0 pending states + 0 pending destroys`. Drain log line at 21:21:00 `drained 1 state(s); 0 remaining` — **all 1264 packets were sent + ACKed before the connection ended** (eliminates "transient packet loss" / "queue overflow" — specific point 5).
- Client spawn lines (`spawned mirror`): **1182**.
- Client spawn failures (`BeginDeferredSpawn failed`): **1** (kind=3 id=852).
- Client trust-boundary drops (`payload too short`, `entityClass=… out of range`): **0** (specific point 3).
- Client "no class for kind" drops: **0** (specific point 1; client install completed at 21:19:46, same second as host replay — and ALL `spawned mirror` lines carry `variantHash=0x0000`, so the table state for chosen variants was sufficient).
- Reliable-channel `dropping OLDEST` / send-failures on either side: **0**.

**Gap = 1264 − 1182 = 82.** Composition:
- 1 explicit BeginDeferredSpawn failure (kind=3 id=852, logged at 21:21:00 in client log line 15421).
- **81 silent UPDATE-path takeovers** in `DoApplyState` (`non_prop_entity_sync.cpp:454-462`): `if (existing && R::IsLive(existing)) { /* update + return */ }`. The update branch emits NO log.

## Smoking-gun observation (identity range)

Client kind=3 (actorChipPile) successful spawn ids:
- Min: 1
- Max: **872**
- Distinct ids: 871 (consecutive 1..872 with one hole at 852, which is the explicit failure).
- Distinct ids > 872: **zero** (verified by `awk '$2>872 {print}'`).

If the host had minted sessionIds 1..953 for 953 chipPile actors (311 trash + 953 chip = 1264), the client's last id should be 953, not 872. Stop-and-wait reliable delivery is strictly in-order, so the missing 81 (873..952 inclusive plus 852) cannot have been "lost in transit before id=872 was acked".

**Therefore: the host's monotonic counter `g_nextSessionId` only reached 873 (next-to-mint), i.e. only 872 unique chipPile actors were minted. 953 chipPile `BroadcastState` calls happened, but 81 of them re-used a previously-cached id from `g_hostSessionIdByActor` — i.e. were duplicate broadcasts of an already-broadcast actor.**

## Why does the same actor get broadcast twice during a single synchronous replay walk?

`ReplaySnapshotForJoinedClient` (`non_prop_entity_sync.cpp:639-670`) is a single-pass `for (i = 0; i < n; ++i)` over `GUObjectArray`. A flat array walk cannot visit the same UObject* twice. So the duplicate broadcasts MUST come from a SECOND emitter overlapping the walk.

Candidate emitter: the `OnInitPost` observer (`non_prop_entity_sync.cpp:422-440`).

Critical asymmetry — the host-side observer has NO echo guard. From the code:

```cpp
void OnInitPost(void* self, void*, void*) {
    if (!self) return;
    const ClassEntry* entry = FindEntryForActor(self);
    if (!entry) return;
    auto* s = LoadSession();
    if (s && s->role() == coop::net::Role::Client) {
        if (ConsumeIncomingWireSpawn(self)) return;   // <-- CLIENT-ONLY guard
    }
    BroadcastState(self, *entry);
}
```

If an `Init` UFunction fires for an actor that already exists (e.g. as part of a load-from-save deferred-init step, a BP-level re-init, or a worker-thread parallel-anim Init dispatch documented in `non_prop_entity_sync.cpp:141-142`), `BroadcastState` re-broadcasts with the CACHED id from `g_hostSessionIdByActor` (line 228-229). That second packet, on the client, hits the UPDATE branch silently.

The race is concretely possible during the replay window because:
1. The replay walk holds the game thread for the full ~ms enumeration.
2. Init POST observers can dispatch on parallel-anim worker threads (`ue_wrap/game_thread.h:118-120`, `non_prop_entity_sync.cpp:141-142`).
3. Even on the game thread, BP-driven re-init paths exist for some actors that bind/rebind components after a save-restore.

81/953 ≈ 8.5 % duplicate rate during a connect-edge replay is consistent with a parallel-anim worker hitting a hot subset of chipPile actors that the game-thread walk is iterating in parallel.

Diagnostic gap that allowed this to go undetected: the UPDATE branch is silent. We have no way to distinguish "this is a legitimate update" from "this is a phantom duplicate of an already-spawned mirror" without instrumenting.

## What's NOT the cause (eliminated)

1. Class-table-not-loaded on client (specific point 1): host install at 21:19:19, client install at 21:19:46 (same second as replay; subclass hashes were registered, base entries fully populated; 0 "no class for kind" warnings).
2. BeginDeferredSpawn/FinishDeferredSpawn failures (point 2): exactly 1 — the id=852 case, accounted for separately.
3. Trust-boundary or payload-shape drops (point 3): 0.
4. Reliable-channel backpressure / queue overflow (point 5): host queue drained cleanly with `0 pending dropped`.
5. Order-of-operations during host install (point 6): install completed 27 s before replay began.

## Recommended fixes (RULE 1: root cause, not workaround)

### Fix 1 — make the silent UPDATE path NOT silent (diagnostic surface)

`non_prop_entity_sync.cpp:454-462` (`DoApplyState`, update branch) currently returns with no log. Add a throttled INFO log so future audits can immediately distinguish duplicate-broadcast from genuine-state-update:

```cpp
if (existing && R::IsLive(existing)) {
    // ...updates...
    static std::atomic<uint64_t> sUpdCount{0};
    const uint64_t n = sUpdCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 5 || (n % 200) == 0) {
        UE_LOGI("non_prop_entity_sync[client]: update existing kind=%u id=%u (#%llu)",
                p.entityClass, p.identity, (unsigned long long)n);
    }
    return;
}
```

This is observability, not a fix — but it's the file:line that proves/disproves the duplicate hypothesis on the NEXT test run with zero further code changes.

### Fix 2 — suppress redundant host re-broadcasts for already-broadcast actors

`OnInitPost` (`non_prop_entity_sync.cpp:422-440`): on host, dedupe by checking whether `g_hostSessionIdByActor` already has an entry for `self` BEFORE calling `BroadcastState`. If it does, the actor has already been broadcast at least once; this re-Init does not represent new world state worth re-broadcasting unless transform/variantState materially changed. Sketch:

```cpp
void OnInitPost(void* self, void*, void*) {
    if (!self) return;
    const ClassEntry* entry = FindEntryForActor(self);
    if (!entry) return;
    auto* s = LoadSession();
    if (!s) return;
    if (s->role() == coop::net::Role::Client) {
        if (ConsumeIncomingWireSpawn(self)) return;
    } else {
        // Host: skip if we've already broadcast this actor at least once
        // (trashBitsPile has a deterministic Key-based identity so it's
        // idempotent; clump/chipPile use monotonic sessionId — re-Init
        // would re-broadcast the same id and waste a reliable slot).
        if (entry->kind != coop::net::NonPropEntityClass::TrashBitsPile) {
            std::lock_guard<std::mutex> guard(g_idMutex);
            if (g_hostSessionIdByActor.count(self) != 0) return;
        }
    }
    BroadcastState(self, *entry);
}
```

This is the root-cause fix per RULE 1: the host stops emitting wasted duplicate reliable-channel slots.

### Fix 3 — long term: change the replay protocol from "fire-and-forget reliable burst" to "snapshot-ack-windowed"

Out of scope for this 82-mirror fix, but: a 1264-entity replay over a stop-and-wait channel takes 74 s on LAN (observed 16 pkt/s). That's the user's window for closing the client (which they did at +97 s in this session). Either widen the reliable channel (Go-Back-N or selective-ack) or chunk the snapshot into compressed batches. Tracked separately.

## File/line refs

- `src/votv-coop/src/coop/non_prop_entity_sync.cpp:422-440` — OnInitPost (no host-side dedupe).
- `src/votv-coop/src/coop/non_prop_entity_sync.cpp:444-462` — DoApplyState UPDATE branch (silent return).
- `src/votv-coop/src/coop/non_prop_entity_sync.cpp:220-233` — IdentityForActor (caches sessionId per actor pointer).
- `src/votv-coop/src/coop/non_prop_entity_sync.cpp:639-670` — ReplaySnapshotForJoinedClient (single-pass GUObjectArray walk).
- `src/votv-coop/include/ue_wrap/game_thread.h:118-120` — ProcessEvent detour may fire on parallel-anim worker threads.

## Bonus: the 1 explicit BeginDeferredSpawn failure (id=852)

Client log line 15421 (`[21:21:00] [WARN ] BeginDeferredSpawn failed for kind=3 id=852`). Adjacent ids 851 and 853 spawned successfully at the same wall-clock second from nearby locations. Likely a transient UE4 spawn-budget/collision-conflict failure rather than a systemic issue. Not pursued further.
