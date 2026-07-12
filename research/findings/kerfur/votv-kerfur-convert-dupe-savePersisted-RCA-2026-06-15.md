# Kerfur conversion dupe (off->grab->on->repeat) -- RCA + fix (savePersisted origin)

Date: 2026-06-15. Trigger: user report "turning off grabbing and turning on - repeat - cycle is
duping kerfurs". Method: a 25-agent RCA workflow (5 finder lenses + adversarial per-candidate verify
+ synthesis; 19 candidates -> 8 survivors). Fix AS BUILT below matches section 3-4; built clean,
audited PASS, deployed x4 (SHA 77FDE01F). The "extract npc_world_enum" item remains DEFERRED.

---

# ROOT-CAUSE ANALYSIS + FIX DESIGN — Kerfur conversion dupe (off→grab→on→repeat)

## 0. Verification note

Every line cited below was read in the current tree (not the summary). Key confirmations: `npc_sync.cpp:925` (`p.savePersisted = ue_wrap::kerfur::HasSaveKey(obj) ? 1 : 0`), `npc_sync.cpp:360` (the contrasting hardcoded `p.savePersisted = 0` on the runtime-interceptor path), the **two distinct callers** of `RegisterExistingWorldNpcs()` — `subsystems.cpp:171` (connect-edge host init) and `kerfur_convert.cpp:213` (mid-session converge), `npc_mirror.cpp:172` (the `savePersisted`→`ArmAdoption` route), `npc_adoption.cpp:120` (exact-class, no-identity candidate match) and `:138` (8 s timeout fallback), and the third sender `npc_pose_host.cpp:79` (`QueueConnectBroadcastForSlot`, pure connect-edge).

---

## 1. ROOT CAUSE(S), ranked by certainty

### The honest headline first: there is NO confirmed mechanism in the *current* (v75) code that nets an extra **host** entity per clean cycle

Every one of the eight investigated mechanisms was independently graded `isRootCause=false`, and that grading is **correct against the code as it stands today**. I re-walked the host turn-ON / turn-OFF paths to be sure:

- **Turn-ON, host:** `OnConvertRequest` (kerfur_convert.cpp:472-490) runs `spawnKerfuro` **exactly once** and `ConvergeAfterConversion(toProp=0)` once. `RegisterExistingWorldNpcs` (npc_sync.cpp:832) skips already-tracked actors via the `g_actorToNpcId` guard (npc_sync.cpp:868), and `OnConvertRequest` is host-authoritative + range-gated (`IsAllowedHostAllocatedEid`, kerfur_convert.cpp:440) + liveness-gated (kerfur_convert.cpp:476). One eid in, one kerfur out.
- **Turn-OFF, host:** `dropKerfurProp` once, then `SyncDestroyedNpcActor` + `IngestSpawnedConversionProps` (kerfur_convert.cpp:200-202). The NPC element is released by pointer-key (npc_sync.cpp:808-816, map-key only, double-call-safe), the prop is ingested latch-deduped (kerfur_convert.cpp:186, `ExpressSpawnedProp` is `HasProcessedInit`-deduped).

So the **"2 kerfurs on host"** wording in the session-12 report is a **pre-v67 artifact**: in that era the client ran the whole conversion locally and the host never saw it (see `votv-kerfur-convert-RE-2026-06-12.md:94-100`). v67 made conversion host-authoritative; that specific host-side accumulation is already closed. **Do not chase a phantom — verify the repro on the current build first** (item 5).

### What IS a confirmed, per-cycle defect — and the most likely thing the user is actually seeing today

**ROOT CAUSE #1 (CONFIRMED, certainty: HIGH) — `RegisterExistingWorldNpcs` mislabels a mid-session conversion spawn as `savePersisted=1`, forcing the client's turn-ON kerfur down the deferred-adoption path it was never designed for.**

- **Birth line:** `src/votv-coop/src/coop/npc_sync.cpp:925` — `p.savePersisted = ue_wrap::kerfur::HasSaveKey(obj) ? 1 : 0;`
- **Why the flag is wrong here:** `HasSaveKey` is a property of the **actor** (does it have a non-None `int_save` Key). RE fact Q1 proves a turn-ON-spawned kerfur **always** has one — its UCS mints a random key (`getKey→assignKey→generateRandomKey`, `prop_kerfurOmega spawnKerfuro` passes no exposed-on-spawn key). So `savePersisted=1` *every* turn-ON. But `savePersisted` is *consumed* as a property of the **event**: "the joining client also booted this object and has a local twin to adopt" (npc_mirror.cpp:164-171). A mid-session converge spawn has **no client twin** — the client cancelled its own turn-ON (`kerfur_convert.cpp:298 return true`, fact F1). The flag's two meanings diverge exactly on this path.
- **Client consequence:** `npc_mirror.cpp:172` → `ArmAdoption`. `ResolvePending` finds no same-class untracked candidate → polls 5 Hz for the full `kAdoptTimeoutMs = 8000 ms` (npc_adoption.cpp:45, :138) → `SpawnFreshNpcMirror`. **The turned-ON kerfur is invisible on the client for up to 8 seconds, then pops in.** Per cycle, every cycle. This is a real, reproducible, user-visible glitch — and is the single most plausible thing behind a "something's wrong with the kerfur on/off" report on the current build.

**ROOT CAUSE #2 (PLAUSIBLE, certainty: MEDIUM, conditional) — during that 8 s window, the class-only/no-identity adoption match can FALSE-BIND an unrelated untracked same-class kerfur as the mirror, producing a true client-side double.**

- **Birth line:** `src/votv-coop/src/coop/npc_adoption.cpp:120` — `if (cands[c].cls != e.actorClass) continue;` (exact class equality, **no per-actor identity** — and the `int_save` Key *cannot* disambiguate, it's random per peer, RE fact Q1/Q2).
- **Trigger preconditions:** a turn-ON issued while ≥1 *other* same-skin-class kerfur is live AND **untracked** on the client at poll time. The everyday clean cycle does *not* create one (the client only ever held the destroyed prop). It is reachable only in races: (a) a turn-ON during the ~8 s window where another kerfur's connect-edge adoption is still pending and the one-shot ghost sweep hasn't fired; (b) a leftover from ROOT CAUSE #3.
- **Consequence:** `BindAsMirror` (npc_adoption.cpp:60-77) installs+parks the **wrong** local kerfur under the new eid; the host's real turned-ON kerfur then has no correct representation, and the wrongly-bound actor's own identity is orphaned → a visible, lingering second kerfur **on the client**.

**ROOT CAUSE #3 (CONFIRMED-as-fact, certainty: HIGH; reliability hole, not an entity creator) — the one-shot ghost sweep never re-arms for a mid-session conversion, so any leftover from #2 lingers for the rest of the session.**

- **Birth line:** `src/votv-coop/src/coop/npc_adoption.cpp:203` (`!g_ghostSwept` gate) / `:205` (latched true). Re-armed **only** by `OnClientWorldReady` (:228, world-swap) and `OnSessionEnd` (:235). A mid-session off→grab→on triggers neither → the safety net that would reconcile a #2 leftover is permanently disarmed mid-session.

**Reconciling with the user's "accumulation" semantics:** the *accumulating* symptom is best explained as **#2 leftovers that #3 never cleans up**, observed on the **client** screen (the report's "on the host" is the stale pre-v67 framing). The clean single-kerfur cycle dupes nothing today; the dupe needs the multi-kerfur / pending-adoption race that #1's misrouting *opens the door to* every single cycle. Fixing #1 (never route a mid-session conversion spawn into adoption) **structurally removes the door**, which is why #1 is the root to fix even though in isolation its guaranteed symptom is "only" the 8 s pop-in.

---

## 2. Why the existing kerfur_convert + v75 adoption don't cover it — the seam

The seam is a **single overloaded function asked to serve two callers with opposite semantics**:

```
RegisterExistingWorldNpcs()           // npc_sync.cpp:832, broadcasts savePersisted = HasSaveKey(obj)
  ├── subsystems.cpp:171  CONNECT-EDGE host init   → joiner MAY have a local save-twin → savePersisted=1 is CORRECT
  └── kerfur_convert.cpp:213  MID-SESSION converge → client has NO twin (cancelled own turn-ON) → savePersisted=1 is WRONG
```

The v75 design's own convention is stated and *correctly hardcoded* on the parallel path: the runtime-interceptor send forces `savePersisted = 0` with the verbatim rationale "a RUNTIME interceptor spawn fires AFTER the client's save-load is done, so the client has no local twin" (npc_sync.cpp:356-360). A mid-session conversion is **semantically identical** to a runtime-interceptor spawn (post-save-load, no client twin) — but it flows through `RegisterExistingWorldNpcs`, which unconditionally stamps `HasSaveKey`. The converge path was bolted onto the connect-edge enumerator (v67) and inherited its connect-edge `savePersisted` policy without re-deciding it. v75 then *amplified* the consequence: pre-v75 `savePersisted` didn't exist, so the mislabel was inert; v75 made `savePersisted=1` route into an 8 s class-match poll, turning an inert flag into a per-cycle glitch + a conditional dupe vector.

`npc_adoption` is *correct for its contract* (connect-edge twin adoption); it is simply being **fed a packet it was never meant to receive**. The class-only match (npc_adoption.cpp:120) is safe at the connect edge (the nearest same-class untracked local IS the twin) and unsafe mid-session (there is no twin, so "nearest same-class" is a guess).

---

## 3. Fix design (RULE 1 root-cause, RULE 2 no parallel paths, host-authoritative MTA shape)

### Architectural decision: model a conversion as a host-authoritative ENTITY REPLACEMENT — one destroy + one express *fresh* spawn — and make `savePersisted` carry the EVENT semantics, not the actor's has-a-key property.

The correct invariant: **`savePersisted=1` means and only means "this EntitySpawn is a connect-edge replay of an object present in the save both peers booted."** A mid-session spawn — interceptor *or* converge — is `savePersisted=0`, full stop. The flag must be set by the **caller's intent**, not recomputed from `HasSaveKey` deep inside the enumerator.

This is exactly the MTA element-RPC shape. In MTA an entity replacement is two server→client element packets, never a client-local mutation that both sides race to reconcile:

- **`reference/mtasa-blue/Server/mods/deathmatch/logic/CPlayerManager.cpp`** / `CElementRPCFunctions` — element create/destroy are server-authoritative RPCs; the client mirrors them and never invents identity. The kerfur conversion is an `RPC_DESTROY_ELEMENT(oldEid)` + `RPC_CREATE_ELEMENT(newEid)` pair.
- **`reference/mtasa-blue/Client/mods/deathmatch/logic/CClientEntity.*`** — the client `CClientEntity` is bound to a server element ID and is *told* when to create/destroy; it does no nearest-match guessing. Our `npc_adoption` nearest-class poll is the anti-pattern MTA explicitly avoids; it is legitimate ONLY at the join snapshot (where there is no server-pushed create yet for objects the client loaded itself).

So: **a mid-session conversion is a server-pushed create — it must take the `SpawnFreshNpcMirror` path immediately, never the adoption poll.** The adoption poll stays scoped to its true purpose: the join snapshot, where the client materialized save objects itself before any host create arrived.

### How to distinguish connect-edge from mid-session — an EXPLICIT param, not a heuristic

Do **not** add a heuristic (e.g. "is a peer mid-handshake?"). Thread an explicit origin through the one overloaded function:

```cpp
// npc_sync.h
enum class NpcEnumOrigin { ConnectEdge, MidSessionConverge };
int RegisterExistingWorldNpcs(NpcEnumOrigin origin);   // was: RegisterExistingWorldNpcs()
```

In `RegisterExistingWorldNpcs` (npc_sync.cpp:925) replace the recompute with:

```cpp
// Connect-edge: the joiner MAY have loaded this save object itself -> let it adopt its local twin.
// Mid-session converge: a host-only runtime spawn the client has NO twin of -> fresh-spawn now.
p.savePersisted = (origin == NpcEnumOrigin::ConnectEdge && ue_wrap::kerfur::HasSaveKey(obj)) ? 1 : 0;
```

- `subsystems.cpp:171` → `RegisterExistingWorldNpcs(NpcEnumOrigin::ConnectEdge);`
- `kerfur_convert.cpp:213` → `RegisterExistingWorldNpcs(NpcEnumOrigin::MidSessionConverge);`

This is a single source of truth, no parallel code path (RULE 2), and the `HasSaveKey` recompute is *gated by intent* rather than removed (it stays correct for the connect edge). The runtime-interceptor path (npc_sync.cpp:360) already says `0` and is untouched. After this, **all three senders agree**: `savePersisted=1` ⟺ connect-edge replay of a save object.

This change alone:
- Kills ROOT CAUSE #1: a mid-session turn-ON now broadcasts `savePersisted=0` → `npc_mirror.cpp:178 SpawnFreshNpcMirror` immediately. No 8 s pop-in.
- Kills ROOT CAUSE #2: the adoption poll is never armed for a conversion spawn → no class-only false-bind window for it.
- Neutralizes ROOT CAUSE #3's relevance to conversions: with #1/#2 closed, conversions produce no untracked ghost for the sweep to miss.

### How the grab/hold step fits

The grab/hold leg is already **safe and needs no change** (mechanism #17, CONFIRMED): a held kerfur-prop turned on remotely is torn down cleanly — `remote_prop.cpp:840 ClearAnyDriveFor` + `:849 ReleaseMainPlayerGrabIfHolding` (which unconditionally clears the grab slot, `engine_mainplayer.cpp:273 *grabbingSlot = nullptr`) before the echo-suppressed `K2_DestroyActor`. No dangling-hold orphan, no orphan-on-release. The grab is *not* a dupe vector; it merely re-arms the [off→on] each iteration. Leave it as is.

### Should we go further and make turn-ON an explicit destroy+create pair?

The converge already *is* a replacement: turn-OFF = `SyncDestroyedNpcActor` (destroy NPC eid) + prop express-spawn; turn-ON = `SyncDestroyedTrackedProp` (destroy prop eid) + NPC register/express. The only defect is the **flag on the NPC create**. So the minimal, complete root-cause fix is the `NpcEnumOrigin` thread-through — we do **not** need a new wire packet or a new replacement opcode. Keep the existing destroy+create shape; just label the create correctly.

---

## 4. File-by-file change plan

No wire-format change → **no protocol bump** (`kProtocolVersion` stays 75; `EntitySpawnPayload` byte layout is unchanged — only the host's *value* policy for `savePersisted` changes). Confirm in the PR that the static_assert at protocol.h:2538 is untouched.

| File | Change | Signature / detail |
|---|---|---|
| `include/coop/npc_sync.h:134` | Add `enum class NpcEnumOrigin { ConnectEdge, MidSessionConverge };` above; change `int RegisterExistingWorldNpcs();` → `int RegisterExistingWorldNpcs(NpcEnumOrigin origin);`. Update the doc-comment (npc_sync.h:122-134) to state the origin contract. | breaking sig (2 callers) |
| `src/coop/npc_sync.cpp:832` | Add the `origin` param; at **:925** replace `p.savePersisted = ue_wrap::kerfur::HasSaveKey(obj) ? 1 : 0;` with the origin-gated expression above. Update the :920-924 comment to name the two origins. | the fix site |
| `src/coop/subsystems.cpp:171` | `RegisterExistingWorldNpcs(coop::npc_sync::NpcEnumOrigin::ConnectEdge);` | connect-edge caller |
| `src/coop/kerfur_convert.cpp:213` | `coop::npc_sync::RegisterExistingWorldNpcs(coop::npc_sync::NpcEnumOrigin::MidSessionConverge);` | mid-session caller |
| `include/coop/net/protocol.h:2519-2538` | **Comment-only**: clarify `savePersisted=1` means "connect-edge replay of a save object", NOT merely "has a key". No layout change, no version bump. | docs |

**Defensive hardening (recommended, same commit) — close the adoption-poll false-bind even if some *other* future caller mislabels a mid-session spawn:**

| File | Change | Rationale |
|---|---|---|
| `src/coop/npc_adoption.cpp:138` | Reduce the dead-poll cost for the now-rare legitimate adoption miss is **not** needed; instead, leave timeout as-is. (No change required once #1 is fixed — listed only to note it was considered and rejected: the 8 s is correct for a genuinely slow connect-edge save-load.) | — |

I deliberately do **not** add a re-arm of the ghost sweep on converge (it would be a crutch masking a path that no longer produces ghosts once #1 is fixed — RULE 1). If the post-fix smoke ever shows a conversion ghost, that is a signal a *different* mislabel exists; fix that source, not the sweep.

**File-size / modularity flags (RULE 2026-05-25):**
- `npc_sync.cpp` is **942 LOC — already past the 800 soft cap** (it was 872 at HEAD; the kerfur arc pushed it). This change adds ~3 LOC. **Per the audit rule I must flag it now with the extraction already named in memory:** extract `RegisterExistingWorldNpcs` + the world-enum helpers into a new **`coop/npc_world_enum.{h,cpp}`** as a **separate prior commit**, then make the `NpcEnumOrigin` change in the extracted file. This keeps the dupe-fix commit small and stops `npc_sync.cpp` ballooning further. (This extraction is the DEFERRED item recorded in MEMORY.md for the kerfur arc — do it now, it is the trigger condition.)
- `kerfur_convert.cpp` (535), `npc_mirror.cpp` (590), `npc_adoption.cpp` (238), `npc_pose_host.cpp` (168), `subsystems.cpp` (338): all comfortably under cap.

---

## 5. Risks / test plan

### Exact repro (must be run on a build that contains the fix AND, as a control, first on the *current* build to confirm what the user actually sees)

1. Host New Game (fresh save, current game version, per CLAUDE.md). Client joins (named-window launchers `mp_host_game.bat` + `mp_client_connect.bat`).
2. Confirm `kerfur_convert: installed` and a connect-edge `npc-adopt: bound LOCAL save NPC ... eid=N` (the save kerfur adopts cleanly — fact F5 baseline).
3. **Client** looks at the kerfur → radial → **turn_off**. Then **E-grab** the resulting prop. Then radial → **turn_on**. Repeat **10×**.
4. After each ON, count kerfurs on **both** screens and in the host log.

### Pass markers (post-fix)

- Host log, every ON: `kerfur_convert: HOST executing turn-on eid=… ` then exactly one `npc-sync[world-enum]: registered 1 pre-existing world NPC` and **`SendEntitySpawn`** for that eid.
- Client log, every ON: `npc-sync[client OnSpawn]` → **`SpawnFreshNpcMirror`** path (NOT `npc-adopt: armed deferred adoption`). The string **`npc-adopt: armed`** must NOT appear for a conversion eid. The kerfur appears **immediately**, no ~8 s gap.
- After 10 cycles: exactly **1** kerfur on host, **1** on client. No `npc-adopt: eid=… no local twin in 8000ms` lines (those would indicate the misroute survived).

### Edge cases to exercise

- **Sentient refusal:** turn_off a sentient kerfur → BP refuses (`actor still live`); `ConvergeAfterConversion(toProp=1)` no-ops both syncs (kerfur_convert.cpp:199, `!IsLiveByIndex` false). Verify no destroy/spawn broadcast, no client change.
- **Kill-mode denial:** host turn_off denied when `kill` bool set (kerfur_convert.cpp:463) — verify no broadcast.
- **Host does its own cycle:** host menu off→on (not a client request) — `Tick` converge path (kerfur_convert.cpp:521-525). Same `MidSessionConverge` origin → client gets `savePersisted=0` → fresh-spawn. Verify symmetric with the client-initiated case.
- **Grab-then-on (held prop):** grab the prop, then turn it on while holding → `remote_prop` grab teardown (`ReleaseMainPlayerGrabIfHolding`) + fresh NPC mirror. Verify no floating camera (`prop_camera_good_C` rides the kerfur lifecycle, RE Q5 — a fresh mirror's later destroy cascades the cam child correctly) and no dangling hold.
- **Rapid repeats:** spam off/grab/on with <1 s spacing. Watch for `ArmAdoption` idempotency not even being reached (it shouldn't fire at all now); confirm `MirrorManager::Install` duplicate-eid guard (npc_mirror.cpp:140/297) never trips.
- **Disconnect mid-cycle:** client turn_off request in flight, then disconnect. Host `OnDisconnect` clears pending (kerfur_convert.cpp:530-533); client `OnSessionEnd` clears adoption state (npc_adoption.cpp:231). Verify no orphan NPC/prop element leaks on either side.
- **Connect-edge regression guard (critical):** a *second* client joins mid-session. The save kerfur and any earlier turned-ON kerfurs must still adopt/mirror correctly. Confirm `subsystems.cpp:171` (`ConnectEdge`) still sends `savePersisted=1` for the genuine save kerfur and the joiner adopts it (`npc-adopt: bound LOCAL save NPC`), while mid-session-spawned kerfurs reach the new joiner via `QueueConnectBroadcastForSlot` (npc_pose_host.cpp:79) — which is also connect-edge and still `savePersisted=HasSaveKey`. **Watch:** a kerfur that was turn-ON-spawned mid-session has a key, so to a *later* joiner it looks like a save object and `QueueConnectBroadcastForSlot` will mark it `savePersisted=1`. That is **correct for a later joiner** only if the later joiner could plausibly have a local twin — which it cannot (it loaded the *original* save, which didn't contain that runtime kerfur). This is a pre-existing, separate latent issue in the connect-broadcast path, NOT introduced by this fix; flag it for a follow-up (the connect-broadcast should arguably track per-element "was this in the booted save vs runtime-spawned" rather than recomputing `HasSaveKey`). Do not scope-creep it into this commit, but note it so the next-joiner test isn't misread as a regression.

### Risks

- **Low risk overall:** the change is one boolean-policy line gated by an explicit enum; no wire change, no new lifecycle.
- **Main risk is the extraction** (`npc_world_enum`) if done in the same PR — keep it a separate, behavior-preserving commit and re-run the connect-edge smoke after it to prove the enumerator still mirrors the save kerfur before layering the `NpcEnumOrigin` change.
- **Pre-deploy checklist applies** (CLAUDE.md): hot-path audit (the touched code is cold, user-action-rate — no per-frame scan added), modular-size flag raised above, deploy x4, ≥30 s LAN smoke running the 10× cycle, log diff clean (no `npc-adopt: armed` for conversion eids, no 8 s-timeout lines), todo marked done only after the smoke passes.

---

## Bottom line

The root cause is a **semantic overload of `savePersisted`**: `npc_sync.cpp:925` recomputes it from the actor's has-a-key property inside a function (`RegisterExistingWorldNpcs`) shared by a connect-edge caller and a mid-session converge caller, so every mid-session turn-ON kerfur is mislabeled `savePersisted=1` and shoved into the v75 connect-edge adoption poll it was never meant for — producing a guaranteed ~8 s pop-in and a conditional class-only false-bind dupe. The fix is to thread an explicit `NpcEnumOrigin` and set `savePersisted=1` **only** for connect-edge replays of save objects, matching the runtime-interceptor path's already-correct `savePersisted=0` convention and the MTA server-authoritative element-create/destroy shape (`CElementRPCFunctions` / `CClientEntity`). No wire change, no protocol bump, ~6 lines across 4 files — but extract `npc_world_enum.{h,cpp}` first because `npc_sync.cpp` is already at 942 LOC (over the 800 soft cap).