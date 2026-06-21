---
name: project-session26-pile-fixes-take2-2026-06-19
description: "Session 26 (2026-06-19) — 3 fixes BUILT+AUDITED+DEPLOYED for the failed-hands-on symptoms (freeze probe, symptom-3 incremental-pile re-spawn, symptom-2b relay-on-morph). UNCOMMITTED, UNTESTED. Awaiting user hands-on next session."
metadata: 
  node_type: memory
  type: project
  originSessionId: 86866d94-8692-44e2-b0a3-136eb16f4d11
---

# Session 26 — pile saga + freeze, take 2 (2026-06-19)

State after [[project-session25-pile-eidbind-timing-2026-06-18]] HANDS-ON FAILED. This
session ROOT-CAUSED all 3 symptoms **from the user's real logs** (research/handson_fail_2026-06-19/)
and shipped fixes. **DLL `55CE5C38E343` deployed (host/client/dev hashes verified MATCH),
both votv-coop.log cleared, runbook at `research/handson_runbook_2026-06-19.md`.**
**UNCOMMITTED on `1272b0a3` (the broken batch + these fixes). UNTESTED — the user tests next
session.** Build clean, 2-agent audit (0 CRITICAL, 2 plausible fixed). STILL: do NOT claim
fixed from autonomous smokes — only the user's hands-on / a matching real log counts.

## NEXT SESSION ORDER
1. **READ the user's fresh re-test logs FIRST** (both host+client). Verify via the GOOD markers below.
2. **Freeze**: if it persists, the probe log says GT-stall vs render-stall vs which op → fix at that level.
3. **Piles**: the markers say which fix (if any) failed; re-derive from the log, don't guess.
4. Then: build the **interaction smoke** (the real gap), extract `coop/pile_seed.cpp`, and COMMIT a baseline once something is verified.

## FIX 1 — HOST FREEZE on connect = a PROBE, not a fix
- The save_capture retirement (session 25) DEPLOYED but the freeze PERSISTED → cause is **NOT save_capture**.
- Why I couldn't pin it: the per-second host-log lines (`net-diag`) are emitted from **Session::NetThread (net thread)**, NOT the GT — so they can't prove GT liveness (my session-25 "log is continuous → GT alive" was WRONG). `net stats`/`pos diag` ARE GT-posted (every ~2s, distinct timestamps → GT was draining tasks, so likely a RENDER stall not a hard GT block).
- **NEW: `coop/dev/freeze_probe.{h,cpp}` (TEMPORARY, RULE 2 — remove once diagnosed).** Always-on, logs only past a threshold:
  - `NoteGtTick()` @ net_pump::Tick top → `[freeze-probe] GAME-THREAD stall: ... gap=Nms` (>200ms).
  - `NotePresent()` @ imgui_overlay PresentDetour (render thread) → `[freeze-probe] RENDER/GPU stall: DXGI Present gap=Nms` (>250ms = the literal 0-GPU).
  - `ScopedStall` op-timers (>50ms) around `puppet-spawn` (RemotePlayer::Spawn), `save-read+crc` (save_transfer TryCaptureBlob_), `reseed-walk` (prop_element_tracker SeedWalk_).
- **Prime suspect (audit agent B): the puppet spawn at connect** — synchronous skeletal-mesh + AnimBP + actor-finish = the ONLY new-GPU-resource event in the connect path; UE may internally `FlushRenderingCommands` → render stall → 0 GPU. (Connect window is ~10:09:17→10:09:27; puppet spawn burst at 10:09:26.) GNS send-backlog + the 20MB save read+crc are RULED OUT (net thread / gated).
- **ASK the user WHEN it freezes** (on connect / during client world-load / first interaction) — narrows it.

## FIX 2 — symptom 3 (host grabs a lying pile → peer keeps a dupe, old doesn't vanish)
- ROOT CAUSE (verified, client log): an **incremental** host keyless-pile (expressed OUTSIDE the connect bracket → the host's steady re-seed broadcasts a bracket-free PropSpawn → the client FRESH-SPAWNS a mirror with NO local save twin, e.g. eid 5223 actor 00000272E50C3F80 @10:09:30). That mirror dies in the join **mass-purge churn** (10:09:38→10:10:26, "reaped 256 ... mass-purge/level-transition") and is NEVER re-established: it's NOT in `g_pileSeeds`, and a no-twin mirror has nothing to position-re-resolve to (the 726 SEED piles survive because they re-resolve to their re-loaded save twins). Host grab 10:10:54 → PropDestroy(5223) → client "OnDestroy eid=5223 has no local actor" → no vanish + the host clump dupes. The 870-vs-726 delta IS this set.
- **FIX** (`coop/prop_adoption.cpp` + remote_prop_spawn.cpp OnSpawn + remote_prop.cpp OnDestroy):
  - `PileSeed` += `bool isMirror`. `RecordMirrorPileSeed(eid,x,y,z,chipType,cls,slot,actor)` — called from the OnSpawn FRESH-SPAWN path (remote_prop_spawn.cpp:793) — enrolls incremental keyless `actorChipPile_C` mirrors (gated `g_joinedAsClient` + IsChipPile + not-already-seeded).
  - `BindPileSeeds_` path **(3a)**: a dead `isMirror` seed is **RE-SPAWNED** via `SpawnLocalTrashActor` + RegisterPropMirror (position re-resolve is impossible — no twin). Index-based loop + by-value seed + `n0` cap (realloc-safe). The bracket-claimed (isMirror=false) seeds keep the old position-re-resolve (path 3b).
  - `ForgetPileSeed(eid)` — called from remote_prop.cpp OnDestroy (keyless) so a destroyed/grabbed pile's seed is dropped → no ghost re-spawn. (OnConvert routes its old-pile destroy through OnDestroy, so grabs are covered.)
  - `g_joinedAsClient` (set BeginClaimTracking / clear ResetClaimTracking) → the HOST never seeds/re-spawns a pile in its own world.
- GOOD markers: `incremental pile mirror #N eid=N seeded for re-spawn`, `ambient pile eid-bind -- N re-spawned`, host-grab → client `OnDestroy: ... eid=N -> destroying local actor` (the BUG was `has no local actor`).

## FIX 3 — symptom 2b (client/peer grabs a pile → host doesn't sync it)
- ROOT CAUSE (**RE byte-exact**, research/bp_reflection/_mainplayer_uber_full.txt): a VOTV use-press is **GRAB XOR THROW** — hands FULL → `dropGrabObject` (THROW) then RETURN (lookAtActor never cast); hands EMPTY → `playerGrabbed` (spawns + HOLDS a clump, destroys the pile). The old `OnPileGrabPre` relayed a grab whenever the press was aimed at a pile → it **MISFIRED a grab-relay during a throw** (real log: threw clump 00000273D16EBB00, eid=0, THEN relayed grab for an unrelated pile 5268); and the local clump it had to capture only exists POST-morph → the adopt **deferred forever** ("deferring adopt onto clumpEid=5272", never completes; host clump 5272 sits undriven). NoteHeldClump's old held-edge capture raced and lost.
- **FIX = relay-on-morph** (`coop/pile_handle.{h,cpp}` + local_streams.cpp + trash_collect_sync.cpp):
  - `OnPileGrabPre` (trash_collect_sync.cpp): grab-vs-throw guard via **prev-grab-state tracking** (`s_prevGrabbing`) — skips the throw's acting edge (holding) AND its empty-handed RELEASE edge (held-on-prev-edge), runs on EVERY use-press for exact tracking. The CLIENT relay block is **DELETED** (the input edge can't tell grab from throw, and the clump doesn't exist yet). Host PropDestroy branch kept (now guarded).
  - `NoteAimedPile(lookAtActor)` (pile_handle.cpp) — called every client tick from local_streams.cpp **AFTER** the held-edge logic (so NoteHeldClump reads the PRIOR tick's aim; the morph destroys the pile so its eid must be cached while alive). Caches the crosshair chipPile's host eid (resolve on pile-CHANGE only).
  - `NoteHeldClump` rewritten: when OUR fresh local grab-morph clump appears, **RELAY + CAPTURE atomically** (RelayClientGrab(pileEid) then `g_pendingGrabClump=clump`), so the host's PropConvert can't beat the capture. A throw produces no new clump → structurally can't misfire. Deferred-adopt kept as a cold safety net.
- GOOD markers: `relay-on-morph -- captured local clump %p for host pileEid=N` → `adopted local clump %p onto host clumpEid=N` (the BUG stopped at `deferring adopt`).

## AUDIT (2 agents) — fixes applied
- **Perf: 0 CRITICAL.** WARN: `NoteAimedPile` is the first HOT (~125Hz GT) caller of `remote_prop::ResolveMirrorEidByActor`, which allocates a `vector<Prop*>` snapshot of the whole mirror map (~900) + linear-scans. Bounded (≤1 resolve/tick, only on a pile-CHANGE; ~tens of µs) → NOT the 120→60 class. DEFERRED fix = give remote_prop an O(1) actor→eid reverse map (like prop_element_tracker's g_actorToPropElementId).
- **Correctness: 2 PLAUSIBLE, both FIXED:** (1) `R::IsLive(g_pendingGrabClump/ThrowPile)` derefs pointers cached across the relay round-trip → AV risk on a GC'd landed pile → now `IsLiveByIndex` with a stored `int32_t` idx (the file's own convention). (2) throw RELEASE-edge spurious host PropDestroy on a crosshair-flick to a different pile → the prev-grab-state guard above. REFUTED: ghost piles (ForgetPileSeed), iterator-UAF in BindPileSeeds_ (by-value + n0 cap + RecordMirrorPileSeed unreachable from SpawnLocalTrashActor's MarkIncomingSpawn-suppressed Init), relay ordering, NoteAimedPile timing, thread-safety (NotePresent UE_LOGW render-thread-safe — net thread already logs), host/client gating, the OnPileGrabPre-delete regression.
- **Pre-existing (NOT a regression, worth a future guard):** the steady re-seed `SeedWalk_` phase-2 `MarkPropElement` double-mints a LOCAL peer-range Element for every pile MIRROR (its idempotency checks `g_actorToPropElementId`, but RegisterPropMirror installs into `PropMirrors()` → a second twin). Leaks a twin, no dupe (host-grab-vanish resolves by eid not the forward map; the client no longer self-authors trash). Guard = skip MarkPropElement when the actor is already a mirror. Symptom-3 deliberately makes more mirrors → worth doing.
- **Modular:** prop_adoption.cpp **822** + remote_prop_spawn.cpp **818** over the 800 soft cap (both < 1500 hard). Extract `coop/pile_seed.cpp` (PileSeed/g_pileSeeds/BindPileSeeds_/RebindPileSeedsAfterWorldChange/RecordMirrorPileSeed/ForgetPileSeed/g_joinedAsClient). DEFERRED: BindPileSeeds_ is entangled with g_pileBindIndex/EnsurePileBindIndex/g_claimedActors (the claim/sweep machinery) → not a clean move; needs an accessor refactor (same reason as the prior R0b/R0c deferral).

## FILES TOUCHED
NEW: `coop/dev/freeze_probe.{h,cpp}`. Symptom 3: `coop/prop_adoption.cpp`, `include/coop/remote_prop_spawn.h`, `coop/remote_prop_spawn.cpp`, `coop/remote_prop.cpp`. Symptom 2b: `coop/pile_handle.{h,cpp}`, `coop/trash_collect_sync.cpp`, `coop/local_streams.cpp`. Freeze-probe call sites: `coop/net_pump.cpp`, `ui/imgui_overlay.cpp`, `coop/remote_player.cpp`, `coop/save_transfer.cpp`, `coop/prop_element_tracker.cpp`. `CMakeLists.txt` (+freeze_probe.cpp).
