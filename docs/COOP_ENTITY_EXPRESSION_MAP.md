# COOP — Entity Expression Map

> **The single source of truth for: how does each synced entity get a cross-peer identity, which seam
> expresses it, what gates apply, who owns its identity, and how its destroy propagates.** This is the
> map that, had it existed, would have saved the pile-morph's 3 reworks (the question "which seam
> expresses a clump/pile?" is answered here, definitively). Synthesized 2026-06-20 from a code-verified
> trace (4 agents, cross-checked against live code + the IDA RE). Companion:
> [COOP_DISPATCH_VISIBILITY.md](COOP_DISPATCH_VISIBILITY.md).
>
> Confidence: **[V]** verified-from-code · **[RD]** from-comment/RE-doc · **[?]** needs-probe.

## Identity ranges (all families)

`coop::element::Registry`: **HOST eids = `[1, kHostRangeSize=32768)`; CLIENT/peer eids = `[32768, …)`**.
`IsAllowedHostAllocatedEid` / `IsAllowedPeerAllocatedEid` are the trust gates; every wire receiver is
host-authoritative (`senderPeerSlot != 0` ⇒ drop, except the either-range cases — PropDestroy/PropConvert).
**[V]** (`kHostRangeSize` literal is in `coop/element/registry.h`).

## The families at a glance

| Family | Spawn dispatch | Caught by | Identity | Module(s) |
|---|---|---|---|---|
| Keyed `Aprop_C` props | save-load / spawner / Q-menu (BeginDeferred) / takeObj | seed walk (save-loaded) · Init-POST observer (spawner) · `FinishSpawningActor` POST (Q-menu) · takeObj POST | host eid **+ the BP save Key string** | prop_lifecycle, prop_element_tracker, prop_snapshot, host_spawn_watcher |
| chipPile (ambient trash pile) | garbagePileSpawner-seeded / save-load | seed walk (keyless-pile lane) → connect snapshot / R1 re-seed | **host eid only (KEYLESS)** | prop_element_tracker, prop_snapshot, remote_prop_spawn (EnsurePileBindIndex) |
| garbageClump (the carried "ball") | `chipPile.toClump` on grab (EX_LocalVirtualFunction) | **pile_morph held-object adopt** (Init-POST is DEAD for it) | **host eid only** (re-skins the pile's E) | pile_morph |
| Held items (Aprop_C in hand) | grabbed | `EnsureHeldItemBroadcast` new-held edge (self-heal for untracked) + the held-pose stream | the item's Key/eid | trash_collect_sync, local_streams |
| NPCs / Characters | BeginDeferred (VISIBLE) | host `BeginDeferred` interceptor + POST; save-loaded via `RegisterExistingWorldNpcs` walk | host eid (no BP key) | npc_sync, npc_mirror, npc_world_enum, npc_adoption |
| WorldActors (event actors) | BeginDeferred (VISIBLE) | 2nd `BeginDeferred` interceptor (disjoint, NAME-matched allowlist) | host eid | world_actor_sync |
| Kerfur (prop⇄NPC) | conversion verbs (EX_CallMath, INVISIBLE) | **conversion death-watch POLL** + KerfurConvert broadcast | host **KerfurId** (spans both forms) + the per-form eid | kerfur_entity, kerfur_convert, kerfur_command, kerfur_prop_adoption |

## Per-family detail

### Keyed Aprop_C props
- **Spawn→catch:** save-loaded placed props are caught by the one-shot/throttled **GUObjectArray seed walk**
  (`SeedKnownKeyedProps`/`ReSeedKnownKeyedProps`), NOT the Init-POST observer **[V/RD]**; spawner-spawned
  props DO fire the **Init-POST observer** (`GrabObserver_Aprop_Init_POST_Body`) **[V]**; Q-menu/toolgun
  BeginDeferred spawns are caught at **`FinishSpawningActor` POST** (their `init()` is BP-internal →
  Init-POST never fires) **[V]**; container `takeObj` is caught at **takeObj POST** (the nested Init defers
  via `g_takeObjInFlight`) **[V]**.
- **Identity:** the **BP save Key string** is the cross-peer id (the game mints it via `NewGuid`; we READ it,
  and on a receiver write the host's Key via `setKey` before FinishSpawningActor) **[V/RD]**; a host eid
  rides alongside.
- **Client never authors a save-loaded Aprop_C** (`prop_lifecycle.cpp:193` `IsDescendantOfProp → return`) —
  host-authoritative. **[V]**
- **Destroy:** engine `K2_DestroyActor` **PRE** → `PropDestroy(key,eid)` **[V]**; BP-internal vanishes
  (truck/cull/LifeSpan, EX_CallMath) → the host **reaper death-watch** (`ReapDeadLocalPropElements` →
  explicit `PropDestroy(eid)`, kerfur-skipped) **[V]**.
- **Connect reconcile:** R2 blob-vs-live key-diff deletes → SnapshotBegin/claim-tracking → bracketed
  PropSpawn stream → SnapshotComplete → quiescence-gated **divergence sweep** (membership = the client's
  own local Prop Elements, mirror-excluded; **>50% world-wipe valve**) → `EnsurePileBindIndex` position-bind
  for keyless piles (retires the client-local identity). **[V]**

### chipPile + garbageClump + the MORPH (the dupe-critical family)
- **chipPile is KEYLESS** — `setKey` is a no-op (the BP re-reads None); the only identity is the host-minted
  eid. **[V/RD]** Caught by the **seed walk keyless-pile lane** (`IsChipPile` only) → expressed via the
  connect snapshot or the R1 re-seed. **[V]**
- **garbageClump (grab morph product): the Init-POST observer does NOT fire for it** (BP-deferred,
  `EX_LocalVirtualFunction`). **[V: host_spawn_watcher.cpp:132]** It is expressed ONLY by **pile_morph's
  held-object adopt** (`TryAdoptHeldClump` on the new-held edge → `PropConvert{ToClump}`, oldEid==newEid==E).
  Every other seam rejects it: `EnsureHeldItemBroadcast` (`IsGarbageClump → return false`), the re-seed
  (`BuildPropSpawnPayload_` rejects keyless non-chipPile), host_spawn_watcher (not in its ambient set). ⇒
  **the clump has exactly ONE expresser. No dupe.** **[V]**
- **Re-piled chipPile (land morph product): TWO potential expressers** — pile_morph's land-watch poll
  (`PropConvert{ToPile}`) AND the host re-seed (a landed chipPile IS a keyless `IsChipPile` the re-seed
  would express under a fresh eid). They are mutually exclusive: pile_morph's land claim calls
  `MarkKnownKeyedProp + MarkProcessedInit` so the re-seed skips it; if the re-seed won the race
  (`already != E`), pile_morph sends `PropDestroy(E)` (drop the clump) and lets the re-seed's eid stand.
  **[V]** **[?]** the timing margin (does the land claim always bind before the next re-seed tick, ~4 s
  cadence vs a per-frame poll) is the one unverified guarantee.
- **Grab/destroy seam:** `OnPileGrabPre` (the `InpActEvt_use` PRE, reads `lookAtActor`=pile-while-alive) arms
  `pile_morph::OnGrab` with a 400 ms deferred `PropDestroy(eid)` fallback (= the working take-17
  grab→vanish if the morph can't sync). **[V]**
- **[?] THE one runtime-unverified link:** does the morphed clump become `mainPlayer.holding_actor` so
  `local_streams`' new-held edge sees it → `TryAdoptHeldClump`? `local_streams.cpp:220` flags this as a
  live-BP question; the `garbage_pickup_probe` ini + ONE user grab settles it. An audit/build/host-grab
  smoke CANNOT. This is the documented "measure first" item. **[?]**
- Design vs as-built: `docs/piles/07` is the DESIGN; the `.cpp` is truth (400 ms not 250; 100 cm land
  search; `isLocal` folded into `RegisterPropMirror`'s `IsMirror()` routing). **[V]**

### NPCs / Characters
- Host `BeginDeferred` **interceptor** (allowlist of 14 ACharacter bases) allocs an `Npc` Element + POST
  binds the actor; **client suppresses its own** local spawn (zero ReturnValue + return true) and
  materializes from the wire (`OnEntitySpawn`). Save-loaded twins (`savePersisted=1`) → **deferred adoption**
  by class+nearest-pose (never by key — the key is random per peer). Pose via `EntityPose` batch. Destroy
  via `K2_DestroyActor` PRE → `EntityDestroy`. **[V]**

### WorldActors (event actors)
- A **second** `BeginDeferred` interceptor with a **disjoint, NAME-matched** allowlist (16 leaf classes;
  name-match because event classes load lazily). FULL-rotation pose (`WorldActorPose`). Otherwise the NPC
  shape. **[V]**

### Kerfur (the hardest — prop form ⇄ NPC form)
- The game gives a kerfur NO stable id (random key per peer per load). We keep ONE host-only **KerfurId (K)**
  per logical kerfur spanning both forms; the rendered form is a normal `Npc` or `Prop` mirror at a per-form
  eid; K is preserved in place across conversions (`BindFormActor`, the SOLE `KerfurConvert` broadcaster).
  **[V]**
- Conversions are **INVISIBLE** (EX_CallMath) → caught by a **5 Hz death-watch poll** (element-present +
  bound-actor-dead == a local conversion), client-quiescence-gated. Client RELAYS the turn-on/off
  (`KerfurConvertRequest`); host runs the real verb authoritatively + broadcasts `KerfurConvert`; the client
  **claims-and-adopts** its conversion ghost (parks/freezes, never destroy+respawn). The historical dupe came
  from the mid-join turn-on window + the invisible spawn leaving untracked ghosts. **[V/RD]**

## Dupe matrix (every two-seam overlap + its dedup) — the high-value reference

| Overlap | Dedup | Conf |
|---|---|---|
| Init-POST observer ↔ FinishSpawningActor POST (Q-menu Aprop) | shared `HasProcessedInit` latch | [V] |
| Init-POST (nested) ↔ takeObj POST | `g_takeObjInFlight` defer | [V] |
| connect drain ↔ live Init-POST/re-seed during a join | receiver `OnSpawn` exact-key/eid dedup → `RegisterPropMirror` idempotent | [V] |
| R1 re-seed ↔ a 2nd peer's connect drain | bracket-free additive; receiver dedups; no sweep re-arm | [V] |
| kerfur converge ↔ R1 re-seed re-expressing the kerfur | `MarkKnownKeyedProp` + `ExpressIncrementalSpawn` kerfur-skip + reaper kerfur-skip | [V] |
| **pile_morph land-watch ↔ host re-seed (the landed pile)** | `MarkKnownKeyedProp`+`MarkProcessedInit` suppress; race-loser → `PropDestroy(E)` | [V] / margin [?] |
| client save-loaded pile (own eid) ↔ host pile eid (connect bracket) | position-bind retires the client-local identity (`UnmarkKnownKeyedProp`) | [V] |
| client grabs host shared-trash (BP morphs locally) ↔ host author path | client host-authority gate (`EnsureHeldItemBroadcast:114`) — client never authors shared trash | [V] |
| NPC interceptor ↔ WorldActor interceptor (same BeginDeferred) | DISJOINT allowlists; multi-interceptor support | [V] |
| nested BeginDeferred steals a pending eid in POST | params-pointer correlation | [V] |
| client kerfur conversion ghost grabbed → client-eid dupe | `ClaimConversionGhosts` parks/freezes immediately (adopt or reap) | [V] |

## NEEDS-PROBE (do not encode as truth without it)
- **[?]** Does the morphed clump reach `mainPlayer.holding_actor` (the morph's one unverified link)?
  `garbage_pickup_probe` ini + 1 grab.
- **[?]** pile_morph land-claim vs re-seed timing margin.
- **[?]** NPC/WorldActor client-suppression assumes spawner BPs null-check before `FinishSpawningActor`
  (UE4 convention; not IDA-confirmed on VOTV spawners).
- **[?]** WorldActor 16-name allowlist completeness / all-spawn-via-BeginDeferred — smoke-pending.
