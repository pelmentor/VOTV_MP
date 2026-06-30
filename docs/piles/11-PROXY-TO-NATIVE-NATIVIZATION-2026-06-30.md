# 11 — Pile mirror: proxy → NATIVE nativization (2026-06-30)

**Status: DESIGN of record + AS-BUILT increment 1 (deployed `75CB1762`, UNCOMMITTED) + PROVEN (hands-on probe).
Increment 2 (the observable win) NOT built.** Living doc; update as increments land.

## The decision (root-cause, user-driven)
The client's mirror of a host-authoritative trash PILE was a bare `AStaticMeshActor` PROXY
(`coop/props/trash_proxy`). It can NEVER be the game's `lookAtActor` (the `mainPlayer::LookAtFunction`
sphere-trace HARD-filters on `DoesImplementInterface(hit, int_player_C)`) and carries no collision — so the
client got **no native hover GUI, no movement-block, no occlusion-correct aim, wrong rotation** (every proxy
rendered at identity rotation; see superseded fix below). The user's call: this is treating the SYMPTOM.
The root is that the mirror isn't a real pile. **Remove the legacy proxy for the pile form; make the mirror a
real `actorChipPile_C` native** — which IS `int_player_C` by construction, so GUI + collision + rotation +
occlusion + movement-block all return for free, in one move.

### Why the proxy existed (and why that's now obsolete)
The proxy was built 2026-06-21 because a client-SPAWNED real `actorChipPile_C` mirror "went not-live within
~10s on its own" → orphan → visible DUP (`research/findings/votv-pile-mirror-staleness-robustness-DESIGN-
2026-06-21.md`). That finding said the death was "self-morph / self-destruct / GC-eligible (unrooted)". The
2026-06-30 inertness probe **disambiguated it: the death was GC (the mirror was UNROOTED)**, NOT BP autonomy.

## PROVEN — the inertness probe (hands-on, `coop/dev/native_pile_inert_probe`)
Gated by ini `native_pile_inert_probe`. Spawns ONE rooted runtime `actorChipPile_C` + the inert recipe, logs
`[INERT-PROBE]` IsLive/class every 1s. Two runs, both hands-on:
- **RUN 1 (NoCollision):** 39s flat-inert (live + `actorChipPile_C`, no self-destruct, no self-morph) — 4× past
  the original ~10s UNROOTED death window. Session ended at 39s.
- **RUN 2 (COLLISION-ON, the real recipe, spawned 2.5 m ahead so the contact path is excited):** full 60s
  `[INERT-PROBE] VERDICT GO` — live + pile through the contact path, no self-destruct, no self-morph. **[V]**
- The user confirmed **"gui was there"** — aiming at the runtime native showed the **native hover GUI**. **[V]**

→ A rooted runtime `actorChipPile_C` is a safe, fully-native mirror. The ~10s death was GC; `AddToRoot` (which
the proxy already used) stops it; the live ubergraph does NOT self-destruct/self-morph when left alone.
[[lesson-runtime-staticmeshactor-must-be-movable]] · [[feedback-probe-dont-guess-rule]]

## The boundary (honest)
- **RESTING / runtime / re-pile PILE = rooted real `actorChipPile_C` native.** Inert recipe: `AddToRoot` +
  `SetActorTickEnabled(false)` + `SetActorSimulatePhysics(false)` + `SetActorRootMovable` + native collision +
  skin the chipType mesh (keeps the native's own random mesh rotation + its Collision component).
- **In-hand / flying CLUMP = bare proxy** (`prop_garbageClump_C` has a LifeSpan @0x0258 + autonomous
  re-pile-on-contact → too live to keep native; the proxy stays the kinematic carrier).

## The machinery is class-agnostic + already built (the map)
A materialized native is BOUND + MARKED save-native (`Element::SetSaveNative(true)` after
`RegisterPropMirror`) so it rides the SAME proven machinery as a save-loaded bound native (`IsBoundMirrorNative`):
- **pose drive** — `ResolveLiveActorByEid` (remote_prop.cpp ~779), class-agnostic by eid. [V]
- **b3 position-correction** — `quiescence_drain::ApplyPendingPosCorrections` (SetActorRootMovable + teleport). [V]
- **grab route — ALREADY BUILT** (`trash_collect_sync.cpp:414`): reads `lookAtActor`, `IsBoundMirrorNative`
  gate, nulls `lookAtActor` on the InpActEvt_use PRE edge to suppress the local grab, `SendGrabIntent` to the
  host. The camera-cone (`EidForAimedPileProxy`) is the UNBOUND-proxy fallback. [V]
- **morph hand-off pile→clump — ALREADY BUILT** (`remote_prop.cpp:902-916`): on a host grab (ToClump) of a
  bound native, spawn a clump proxy + `RegisterPropMirror(rebindInPlace)` + retire the native. [V]
- **sweep exemption** — `join_membership_sweep` skips `pr.mirror`; a registered native is a mirror. [V]
- **retire** — `remote_prop_destroy` + the morph hand-off, plus the un-root (below).

## AS-BUILT — INCREMENT 1 (spawn-seam) — deployed `75CB1762`, UNCOMMITTED
- **NEW `coop/props/native_pile_mirror.{h,cpp}`** — `Materialize(eid, className, chipType, loc, scale,
  senderSlot, skipBind, rebindInPlace)`: spawn the native + inert recipe + skin chipType mesh + (unless
  skipBind) `RegisterPropMirror` + `SetSaveNative(true)`.
- **`remote_prop_spawn.cpp` (~281):** steady-state (`!IsClaimTrackingActive`) PILE → `Materialize`; clump +
  in-bracket → the bare proxy. SCOPED to `!bracket` so native-spawn and the in-bracket `TryDestroyTwin`
  level-twin reconcile are **mutually exclusive** → a native+twin dup is impossible by construction.
- **LEAK FIX** (a rooted native must un-root before destroy, else a rooted PendingKill leaks its slot):
  `R::RemoveFromRoot(actor)` before `K2_DestroyActor` in `remote_prop_destroy.cpp::DestroyResolvedLocalActor_`
  AND before the morph hand-off destroy in `remote_prop.cpp`. No-op on the unrooted save-loaded natives.
- **`pile_hover_gui.{h,cpp}` DELETED + unwired** — the native GUI is the real fix; the openHovertext-drive
  subsystem is retired (RULE 2). `trash_proxy::HasAnyProxy` (its only consumer) also removed.
- Inert probe DISARMED (ini=0); the probe code stays (RULE-2-exempt diagnostic).

### SCOPING FINDING (why increment 1 is near-invisible)
Increment 1 only nativizes the UNCOMMON steady-state host-only `PropSpawn` pile. The user's actual GUI-less
proxy piles come from paths increment 1 leaves alone: **re-piled piles** (`OnConvert ToPile` → still a proxy
re-skin) and **join-window host piles** (in the bracket → still a proxy). So `75CB1762` is a SAFE FOUNDATION,
not the observable win.

## NEXT (in order)
1. **No-regression test** (`research/handson_runbook_2026-06-30_nativization_inc1_noregress.md`) — bank the
   foundation GREEN before touching the delicate carry-land path, so increment 2's attribution is clean.
   On green → **commit increment 1**.
2. **INCREMENT 2 (the observable win):** `remote_prop.cpp` OnConvert `ToPile` on a CLUMP proxy → clear the
   carry drive (`ClearAnyDriveFor` + `trash_clump_pose_stream::ClearDriveForEid`) + `RetireProxy` +
   `native_pile_mirror::Materialize` at the landed rest + rebind. Closes the full cycle `native pile ⇄ clump
   proxy`. CARRY-LAND path risk (historical 2fps-teleport) → isolated delta on the green foundation.
3. **RULE-2 cleanup** (after full nativization hands-on verified): retire `f79bbe84` (the host-side
   mesh-world rotation capture — the native does its own rotation, and `f79bbe84` has a latent b3
   double-rotate on save-loaded natives); retire `EidForAimedPileProxy`-for-pile (the cone is dead once all
   piles are native); convert the in-bracket level-pile path from `TryDestroyTwin`-destroy to bind-the-loaded-
   native.

## Git
origin/main `4028c571`. `f79bbe84` (rotation-fix) is 1 commit ahead, HELD — RULE-2-retire candidate.
Increment 1 = UNCOMMITTED WIP on disk (deployed `75CB1762`). Commit on no-regression green.

[[project-pile-nativization-2026-06-30]]
