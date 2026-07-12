# RE: Aprop_food_mushroom_C growth state machine — Phase 5S0 Inc3 Stream A scoping

**Date**: 2026-05-24
**Trigger**: User hands-on 2026-05-24 confirmed Gap I-1 rekey works
for mushroom cross-peer sync, BUT receiver's mushroom still falls
through the floor on release because its BP-internal growth-state
machine kept it in collision-disabled mode. This RE enumerates that
state machine in enough detail to design Inc3 Stream A (per-prop
state replication) for `Aprop_food_mushroom_C`.

**Method**: SDK reflection dumps (`CXXHeaderDump/` — authoritative
header layout) + cooked-content UObject dump
(`UE4SS_ObjectDump_GAMEPLAY_SAVE.txt` — function-locals lists ground
the BP-graph bytecode shape) + IDA IDB FName-pool inspection (engine
UFunctions). Per [[feedback-re-related-functions]]: enumerate every
related UFunction before designing the hook.

---

## TL;DR

- There is **no `grow` / `growth` / `mature` float field** on
  `Aprop_food_mushroom_C`. The class adds ONLY two fields over the
  food base: `UberGraphFrame@0x03D8` and `comp_photographic@0x03E0`.
  No growth scalar. No timer state on the prop itself.
- Growth is driven by a **two-class compound state machine**: 
  - `Aprop_food_mushroom7_C` (the SMALL / growing variant, extends
    `Aprop_food_mushroom_C`) holds a `K2_SetTimerDelegate`; on
    timeout its `Transform()` UFunction calls
    `BeginDeferredActorSpawnFromClass` for `Aprop_food_mushroom_C`
    (the MATURE variant) and destroys self.
  - `AmushroomMaster_C` is the per-cluster spawner that emits
    mushroom actors via `form()` into its `Data : TArray<Fstruct_shroom>`
    pool, then calls `spawnedNaturally()` on each fresh mushroom.
- "Growing state" (small + buried + collision-disabled) is therefore
  the **identity of the actor class** (`mushroom7_C` vs `mushroom_C`)
  combined with the **engine collision-mode byte on
  StaticMeshComponent** — NOT a scalar field on the prop. The receiver
  having "growing state" means the receiver's local actor is the WRONG
  CLASS (`mushroom7_C` instead of `mushroom_C`), OR the right class but
  with StaticMesh CollisionEnabled == NoCollision because
  spawnedNaturally was called on it.
- Variant count is **2** (not 7 as the file name suggested):
  `Aprop_food_mushroom_C` (mature) and
  `Aprop_food_mushroom7_C` (growing). The "7" is an asset-index
  artifact, not a variant index. No mushroom1..6.
- Recommendation: **Option B (suppress client-side
  AmushroomMaster_C / AmushroomSpawner_C)** is the
  RULE-1-correct fix (eliminate the divergent spawner; host's
  PropSpawn broadcast is authoritative). Option A (extend
  PropSpawnPayload) and Option C (force-mature shim) are tactically
  cheaper but leave the receiver running a parallel spawn loop that
  will keep desyncing across other dimensions (timing, neighbor
  count, surface trace).

---

## Section 1 — `Aprop_food_mushroom_C` field offsets

Per `CXXHeaderDump/prop_food_mushroom.hpp`:

```cpp
class Aprop_food_mushroom_C : public Aprop_food_C
{
    FPointerToUberGraphFrame UberGraphFrame;       // 0x03D8 (size 0x8)
    Ucomp_photographic_C*   comp_photographic;     // 0x03E0 (size 0x8)
    // ... no other added fields ...
};  // total size 0x3E8
```

**No `grow` / `growStage` / `growSpeed` / `isMature` / `mushroomType` /
`growthTimer` / `lastGrowTickMs` field exists.** Growth is encoded by
actor CLASS, not by a numeric field. This was the single most
surprising finding.

Per `CXXHeaderDump/prop_food_mushroom7.hpp`:

```cpp
class Aprop_food_mushroom7_C : public Aprop_food_mushroom_C
{
    FPointerToUberGraphFrame UberGraphFrame;       // 0x03E8 (size 0x8)
    // ... no other added fields ...
};  // total size 0x3F0
```

Same — `mushroom7_C` adds no field state either. The timer for the
growing variant lives entirely inside the UberGraphFrame
(`K2_SetTimerDelegate_ReturnValue@0x58` per the cooked dump).

Inherited state we CAN read (parent classes — already known from
prior RE):

| Field | Class | Offset | Type | Semantics in mushroom context |
|---|---|---|---|---|
| `Static` | Aprop_C | 0x02D8 | bool | If true, mushroom is non-grabbable |
| `frozen` | Aprop_C | 0x02DA | bool | BP-controlled freeze; quest-locked |
| `sleep` | Aprop_C | 0x02DD | bool | Physics-sleep flag |
| `Key` | Aprop_C | 0x02E0 | FName | Save-stable cross-peer identifier |
| `StaticMesh` | Aprop_C | 0x0238 | UStaticMeshComponent* | Bears the collision-mode enum (read indirectly via GetCollisionEnabled UFunction; the byte enum lives inside `UPrimitiveComponent::BodyInstance::CollisionEnabled`) |
| `propData` | Aprop_C | 0x0260 | Fstruct_prop | DataTable-driven defaults (heavy flag, mass, mesh refs) |
| `ResetKey` | Aprop_C | 0x0362 | bool | If true, Init generates a new Guid for Key |
| `foodData` | Aprop_food_C | 0x0388 | Fstruct_food | Calories, edible, spoilage |
| `Temperature` | Aprop_food_C | 0x03A8 | float | Cold mushrooms vs hot |
| `ripeness` | Aprop_food_C | 0x03B0 | float | Spoilage indicator (NOT growth) |
| `useSpoiling` | Aprop_food_C | 0x03BD | bool | Some props don't spoil |

Note: `ripeness` is FOOD spoilage (overripe → eaten), not GROWTH.
Distinct system from the mushroomMaster pipeline.

### 1a. The "growth state" decoder

Given the absence of a numeric growth field, "growth state" decodes
to the pair:

```
(ActorClass, StaticMesh.CollisionEnabled)
```

| ActorClass | CollisionEnabled | Meaning |
|---|---|---|
| `mushroom7_C` | NoCollision (0) | Growing — small, buried, fall-through |
| `mushroom7_C` | QueryAndPhysics (3) | Transient — between timer fire and self-destroy |
| `mushroom_C` | QueryAndPhysics (3) | Mature — visible, grabbable, collidable |
| `mushroom_C` | NoCollision (0) | BUGGY MID-STATE — the user's repro |

The user's reproduction state is the last row: the receiver has a
`Aprop_food_mushroom_C` (correct mature class — because Gap I-1
fuzzy-matched to the host's mature one) with collision DISABLED
(because the receiver's `spawnedNaturally` ran when its OWN
mushroomMaster spawned it locally, but the local spawner picked the
"start as mushroom7_C" path — which spawnedNaturally configures by
disabling collision — and then Gap I-1's fuzzy match REKEY'd that
mushroom7_C to look like the host's mature one, but the collision
state was never undone).

Actually deeper: under Gap I-1 the receiver might already have its
`mushroom_C` instance (mature timeline already fired on its side too).
The "growing" collision-off state is INHERITED by the spawned mature
mushroom because `mushroom7_C::Transform()` may pass forward the
small-collision state via `FinishSpawningActor`'s initial transform
(scale) — but more likely the receiver's mushroom is still actually
`mushroom7_C` and the fuzzy de-dupe is matching cross-class. Either
way: the fix is to STOP the receiver from running the spawner.

---

## Section 2 — `spawnedNaturally()` signature + body

**Declaration** (per `CXXHeaderDump/prop_food_mushroom.hpp`):
```cpp
void spawnedNaturally();   // no params
```

**Cooked dump locals**: ZERO local variables for the UFunction (only
the FName entry exists; no temporaries in the BP graph for this
function). This means the body consists of **direct field writes
and/or UFunction calls with no return-value capture into local
temps**.

**Caller**: per the prior `votv-aprop-lifecycle-RE-2026-05-24.md`
Section 1d, `AmushroomMaster_C::form()` calls `spawnedNaturally()`
on each freshly-spawned mushroom immediately after
`FinishSpawningActor()` returns. (Confirmed by mushroomMaster cooked
locals showing `BeginDeferredActorSpawnFromClass_ReturnValue` followed
by `FinishSpawningActor_ReturnValue`, then a delegate call into the
spawned actor — that delegate target is `spawnedNaturally`.)

**Inferred body** (zero locals + no return capture + the timer
machinery sits in mushroom7's Ubergraph, not here):
```
// Pseudocode, host-side, called by mushroomMaster_C::form
self->Static = false;                          // direct field write
self->frozen = false;                          // direct field write
self->StaticMesh->SetCollisionEnabled(NoCollision);  // BP UFunction call
self->SetActorHiddenInGame(true);              // optional, by class default
// possibly: SetActorScale3D(small_vec) -- but mushroom7 already has tiny scale via UCS
```

The high confidence claim is `SetCollisionEnabled(NoCollision)` on
`StaticMesh` — Init's locals on the SAME class show
`GetCollisionEnabled_ReturnValue` is consulted (proving the class
cares about its collision-mode byte). The lower-confidence claims
(hidden, scale) are not directly observable from the dump but are
consistent with the user's observation that the growing mushroom is
"buried" (a hidden + small + collision-off combo).

**No sister UFunction `spawnedMatured()` / `spawnedFromSave()`
exists.** Save-load restoration goes through `Aprop_C::loadData`
(inherited; not overridden on mushroom). loadData restores Key +
transform from `Fstruct_save`; the mushroom's class identity is
already baked into the save (`Fstruct_save.class_3` field) so
loadData's caller (mainGamemode) picks the right class
(`mushroom_C` for mature saved mushrooms, `mushroom7_C` for
mid-growth saved mushrooms — though saves probably only persist
mature mushrooms because the natural spawner reruns on each load).

---

## Section 3 — Growth tick / state machine

There is **no per-tick growth increment on `Aprop_food_mushroom_C`**.
The cooked dump's Ubergraph for the prop references only
`IsSimulatingPhysics`, `IsChildActor`, `GetAttachParentActor`,
`IsValid`, and `progressAdvancement` (achievement notify) — no
timer, no scale change, no growth math.

The growth tick lives on `Aprop_food_mushroom7_C`:

```cpp
// Aprop_food_mushroom7_C::ExecuteUbergraph_prop_food_mushroom7 locals
RandomBoolWithWeight_ReturnValue : bool   // weighted roll (chance to mature)
GetTransform_ReturnValue : FTransform     // self transform for spawn
K2Node_CreateDelegate_OutputDelegate : Delegate
BeginDeferredActorSpawnFromClass_ReturnValue : Object  // the new mushroom_C
K2_SetTimerDelegate_ReturnValue : FTimerHandle         // the growth timer
FinishSpawningActor_ReturnValue : Aprop_food_mushroom_C*
K2Node_DynamicCast_AsInt_Objects : (interface cast)
```

**State machine** (per-mushroom7 instance):

```
mushroom7 spawn (from mushroomMaster.form OR from save)
  Init runs (UCS path) — sets up collision read, propData
  spawnedNaturally runs (only if natural spawn from mushroomMaster) —
    disables collision, marks invisible
  ReceiveBeginPlay runs (mushroom7 override; cooked dump shows fn entry):
    SetTimerDelegate(K2Node_CreateDelegate_OutputDelegate, T)
      where T = growth duration (random seconds), 
      delegate target = an entry point in ExecuteUbergraph_prop_food_mushroom7
  ... time passes ...
  timer fires -> Ubergraph entry point:
    RandomBoolWithWeight (chance to actually mature now)
      true:  BeginDeferredActorSpawnFromClass(Aprop_food_mushroom_C, GetTransform)
             FinishSpawningActor -> the mature mushroom
             [implicit] DestroyActor(self) — the mushroom7 self-destructs
      false: SetTimerDelegate again with new T (retry)
```

**Threshold semantics**: there is no float threshold (no `if grow >
0.5`). It's a discrete roll on a recurring timer. The mushroom7 has
a finite chance per timer tick of converting to mushroom_C; on
failure, retries.

**The "growing state" the user observed (small + buried +
collision-off)** is therefore the mushroom7_C's PERSISTENT default
state, set by `spawnedNaturally` and held there indefinitely until
the timer rolls true and the class swap happens.

---

## Section 4 — Collision-control mechanism

**One-shot, not per-tick.** Confirmed by:

1. **`Aprop_food_mushroom_C::Init` locals** include
   `GetCollisionEnabled_ReturnValue` and
   `EqualEqual_ByteByte_ReturnValue` — Init READS the current
   collision mode and compares against a default. It does NOT
   re-assert collision every tick (no ReceiveTick override on
   mushroom_C; ReceiveTick exists only on the food parent for
   ripeness, not for collision).
2. **`Aprop_food_mushroom7_C` has NO ReceiveTick override either**
   (only `ReceiveBeginPlay` and `Transform`).
3. **The `ExecuteUbergraph_prop_food_mushroom` body has no per-tick
   collision write** (locals don't include any
   `SetCollisionEnabled` return capture; if it called SetCollision
   the return-value local would be present).

**Implication**: ONCE the receiver's mushroom has been put into
collision-off mode (by spawnedNaturally), no BP code re-asserts it.
A one-shot `SetCollisionEnabled(QueryAndPhysics)` on the receiver's
StaticMesh would PERMANENTLY restore collision (until something else
explicitly disables it again).

The underlying engine field is the
`UPrimitiveComponent::BodyInstance::CollisionEnabled` byte
(TEnumAsByte<ECollisionEnabled::Type>). Values:
- 0 = `NoCollision`
- 1 = `QueryOnly`
- 2 = `PhysicsOnly`
- 3 = `QueryAndPhysics`

Stage 1's grab RE already established that VOTV grabs flip
`SetCollisionEnabled` for the `HeldByPlayer` channel — the same
engine UFunction we'd use here for restoration.

---

## Section 5 — Save/load growth restoration path

**Save → load path** (when a save is loaded):

1. `mainGamemode` walks `Fstruct_save` entries from disk.
2. For each entry, `SpawnActor(entry.class_3, entry.transform)` is
   called. **The class is baked into the save.** A mature mushroom
   saved as `Aprop_food_mushroom_C` is restored as
   `Aprop_food_mushroom_C` directly — bypassing the `mushroom7_C`
   growth path entirely.
3. `setKey(entry.key)` is called BEFORE `FinishSpawningActor`
   (standard VOTV save-restore pattern — known from prior RE).
4. `FinishSpawningActor` runs UCS → Init. Init's collision-mode
   check (`GetCollisionEnabled_ReturnValue` + ByteByte comparison)
   reads the SMC's collision default. If the SMC's default
   collision is `QueryAndPhysics` (true for mature mushrooms) it
   stays that way.
5. `loadData(entry, return)` is then called by mainGamemode (after
   UCS but before BeginPlay or via timer; exact slot per VOTV's
   loadData convention). The base `Aprop_C::loadData` walks
   `entry.bools[] / floats[] / vectors[]` and restores any named
   state. Mushroom doesn't override `loadData`, so only the BASE
   fields restore. Nothing mushroom-specific is in `Fstruct_save`.
6. **`spawnedNaturally` is NOT called on save-restored mushrooms.**
   That UFunction is invoked only by `mushroomMaster_C::form`
   during NATURAL spawning. Save-restoration uses
   `mainGamemode`'s direct SpawnActor path, which never invokes
   spawnedNaturally.

**Key insight**: save-load **inherently bypasses the growing-state
machine** for mature mushrooms. The "small+buried+collision-off"
state cannot be reached via save-load on a mature mushroom_C; that
state is reachable ONLY via the natural spawner pipeline.

This means: **if the receiver's natural spawner is suppressed,
mature mushrooms received via wire will spawn directly as
mushroom_C with default collision — exactly as if they were
save-restored.** No special force-mature handling required, just
suppression of the local natural spawner.

---

## Section 6 — Variant class table

The header dump and the cooked dump together enumerate:

| Class | Extends | Role | Has own Init? | Has own RBP? | Has timer? |
|---|---|---|---|---|---|
| `Aprop_food_mushroom_C` | Aprop_food_C | Mature mushroom (visible, grabbable, collidable) | Yes (the 7-byte one) | No (inherits food's) | No |
| `Aprop_food_mushroom7_C` | Aprop_food_mushroom_C | Growing mushroom (small, buried, collision-off, timer-driven) | No (inherits mature's) | Yes | Yes (K2_SetTimerDelegate) |

**Only 2 variants total.** The "mushroom7" naming is from the
asset's original cooked index, not a numerical variant series.
There is no `mushroom1` ... `mushroom6` and no `mushroom8`+.

`Fstruct_shroom` (the data-table row used by `AmushroomMaster_C`)
defines mushroom TYPES (cap colors, sizes, spawn-surface rules) but
they all spawn as the SAME `Aprop_food_mushroom_C` /
`Aprop_food_mushroom7_C` actor classes — the visual differentiation
is via the `prop_19_` FName field, which keys a DataTable row in the
mature mushroom's `propPicker` for mesh / material selection. So
visually different mushroom species → all the same C++ class →
field state differentiates them, not class.

`AmushroomSpore_C` is a separate non-Aprop_C class (a falling spore
projectile, unrelated to mushroom state).

`AmushroomMaster_C` and `AmushroomSpawner_C` are the SPAWNER
controllers, not props.

---

## Section 7 — Wire-replication recommendation

### Ranked recommendation

**Recommended: Option B (suppress client's natural spawners)** —
rank 1.
**Strong second: Option C (one-shot force-mature shim)** — rank 2,
tactically cheaper, can ship first as a stepping stone.
**Not recommended: Option A (extend PropSpawnPayload)** — rank 3.

### Why B is RULE-1-correct

The root cause is **two independent natural spawners running on
two peers**. Patching the symptoms (force-mature, broadcast extra
state) leaves the divergent spawners running, which will keep
desyncing across other dimensions:

- mushroom positions still diverge → Gap I-1 fuzzy-match has to do
  work on every natural spawn forever
- mushroom counts still diverge (per-peer RNG rolls a different
  cluster size for the same mushroomMaster)
- spawn surfaces still diverge (per-peer trace returns different
  collision results on geometry the peer has streamed/loaded
  differently)
- the growth timer still ticks independently on each peer, so even
  the rare matching pair desyncs over time

Suppress the client's spawner → host's
`AmushroomMaster_C::form` is THE authoritative source of mushroom
positions. PropSpawn broadcasts spawn `Aprop_food_mushroom_C`
DIRECTLY on the receiver (skipping `mushroom7_C`'s growing-state
entirely), with default collision. The fall-through bug evaporates
because the receiver never enters the collision-off state.

This aligns with the listed Inc3 Stream B in
[[project-coop-aprop-lifecycle-re-inc3-scope]] and matches
[[project-coop-whole-map-sync]] (host-authoritative for spawners,
no AOI). It is the natural extension of the architecture already
agreed.

### Why A is the wrong shape

Extending `PropSpawnPayload` with mushroom-specific state bytes
forces a per-class extension pattern (`mushroomGrowState`,
`containerLootState`, `mreState`, ...) baked into a wire
protocol shared by ~600 prop classes. RULE 2 (no migration
baggage) makes this future-painful: every new state field needs a
protocol version bump + receiver migration. And it doesn't even
solve the underlying divergence — the receiver's own natural
spawner still runs and creates duplicates that fuzzy-match has to
clean up.

### Why C is acceptable as a tactical first step

A one-shot `SetCollisionEnabled(QueryAndPhysics)` shim on the
receiver, applied in `OnSpawn` AFTER the fuzzy-match rekey,
converges the visible collision state without changing the wider
architecture. It IS NOT a crutch under RULE 1 because the
receiver's authoritative target IS "mature mushroom with default
collision" and we are merely converging to that target by
reflection write — analogous to how PropPose drives transform
convergence by setting position via reflection. It doesn't fix
the spawner-divergence issue (positions still diverge, counts
still diverge), but it fixes the user's IMMEDIATE
fall-through-the-floor symptom with minimal new RE.

A reasonable cadence:
1. **Now**: Ship Option C (one-shot collision restore on receiver
   in OnSpawn). 1-2 day work. Fixes the visible bug. Unblocks user
   testing other features.
2. **Soon (Inc3 Stream B)**: Ship Option B (suppress receiver's
   `AmushroomMaster_C` + `AmushroomSpawner_C` ReceiveBeginPlay /
   Spawn UFunctions on client). 3-5 day work. Eliminates the
   divergence root cause for mushrooms specifically. Pattern
   replicates to other natural spawners (`AundergroundGarbageSpawner_C`,
   etc.) per the Inc3 Stream B list.

---

## Section 8 — Implementation sketch (Option C, the tactical first step)

Option C is the smallest unit of work that makes the user's repro
disappear. Sketch:

### 8a. Wire path (no new packets)

Reuse `PropSpawn` as-is. The host's `Aprop_C::Init` POST observer
already broadcasts PropSpawn for the host's mature mushroom. On the
receiver:

```cpp
// In coop::remote_prop::OnSpawn, AFTER the fuzzy-match-rekey path
// finishes (whether it matched-and-rekeyed or it spawned-new):
//
//   void* localProp = ... // either the rekey'd local actor or a freshly spawned one
//   if (localProp != nullptr && ue_wrap::prop::GetClassName(localProp) starts_with L"Aprop_food_mushroom") {
//     // Force the StaticMesh component back to QueryAndPhysics
//     // collision — undoes any spawnedNaturally side-effect that
//     // may have been applied by the receiver's local
//     // AmushroomMaster_C::form before Gap I-1 fuzzy-matched.
//     ue_wrap::prop::ForceRestoreDefaultCollision(localProp);
//   }
```

### 8b. New helper: `ue_wrap::prop::ForceRestoreDefaultCollision`

```cpp
// ue_wrap/prop.h additions
namespace ue_wrap::prop {

// Inc3 Stream A tactical shim: undoes a mushroom-style
// growing-state collision-off on a local actor. Reads
// Aprop_C.StaticMesh, calls UPrimitiveComponent::SetCollisionEnabled(
// QueryAndPhysics) via reflection. Idempotent. Game-thread only.
//
// Used by remote_prop::OnSpawn for Aprop_food_mushroom_C arrivals
// (and any other class whose receiver-side BP graph applied a
// one-shot collision-off via spawnedNaturally / similar
// natural-spawner-only side effect). Cost: one UFunction lookup +
// one ProcessEvent dispatch per call.
void ForceRestoreDefaultCollision(void* prop);

}  // namespace ue_wrap::prop
```

Implementation: identical pattern to the existing Stage 1 grab path
that calls `SetCollisionEnabled` on PHC.GrabbedComponent. Reuse the
cached UFunction pointer (one-time resolve on first call). Engine
FName is `SetCollisionEnabled` at `0x144205e10` (now commented in
IDB).

### 8c. Optionally: also restore SetActorHiddenInGame(false)

If the user's hands-on retest shows the rekey'd mushroom is still
INVISIBLE (rather than just falling through), add a paired
`SetActorHiddenInGame(false)` call. Engine FName at
`0x144147258` (now commented in IDB).

### 8d. Class filter

Use `ue_wrap::prop::GetClassName(localProp)` and filter for
`L"Aprop_food_mushroom"` prefix (matches both `mushroom_C` and
`mushroom7_C`). The collision restore is safe on both: 
- mature mushroom_C: makes collision QueryAndPhysics, already its
  authoritative target
- growing mushroom7_C: makes collision QueryAndPhysics, which
  would normally be wrong (growing mushrooms ARE supposed to be
  intangible) but on the receiver under wire-driven sync we treat
  ALL mushroom arrivals as host-authoritative-mature

### 8e. Echo suppression

Existing `MarkIncomingSpawn` + `ConsumeIncomingSpawn` already
prevents the receiver's Init POST observer from re-broadcasting on
wire-driven spawns, so no extra suppression needed.

---

## Section 9 — Implementation sketch (Option B, the architectural fix)

For when the user is ready to do the deeper fix:

### 9a. Hook receiver-side `AmushroomMaster_C::Spawn` POST → suppress body

Both spawner classes (`AmushroomMaster_C`,
`AmushroomSpawner_C`) have a `Spawn` UFunction that runs their
spawning logic. On the CLIENT, those should no-op. Hook them at
ProcessEvent level with a class-filter + role-filter:

```cpp
// In harness.cpp, alongside the existing Init POST observer:

class MushroomMasterSpawnSuppressor_PRE_Observer : public PEObserver {
  void OnPE_PRE(UObject* obj, UFunction* fn, void* params) override {
    if (g_session.role() != Role::Client) return;
    if (!g_session.connected()) return;
    if (!IsClassOrSubclass(obj, L"AmushroomMaster_C")) return;
    if (FName(fn) != L"Spawn"_fn) return;
    // The PRE observer sets a "skip this PE dispatch" flag the
    // detour reads to short-circuit the call before the BP body
    // runs. Pattern matches the existing K2_DestroyActor PRE
    // suppressor.
    SkipThisCall();
  }
};
```

Same shape for `AmushroomSpawner_C::Spawn` and (per Inc3 Stream B
list) `AundergroundGarbageSpawner_C::Spawn`, `Aprop_birdnest_C`
egg-spawn-timer, etc.

### 9b. Watermark gating

The receiver's natural spawners run at level-load BEFORE the
session reaches Connected. We MUST allow those initial spawns
to proceed (otherwise client has no mushrooms at all until host
broadcasts them, which is slow on a populated save). Gate on
`g_session.connectedFor > kSpawnerSuppressionGracePeriod` (5s) —
match the existing Inc2 broadcast watermark.

Then the receiver's existing local mushrooms get reconciled by
the host's snapshot-on-connect (Phase 5S0 Inc1) which fuzzy-matches
+ rekeys them to host-authoritative identities.

### 9c. Receiver's existing mushrooms

After the watermark fires, the client has its own initial mushroom
population (matched to host's via Gap I-1 fuzzy de-dupe). NEW
natural spawns are now host-only — they cross the wire as
PropSpawn broadcasts. Receiver's `OnSpawn` already spawns
`Aprop_food_mushroom_C` directly (skipping mushroom7_C entirely),
so no growing-state ever exists on the client.

### 9d. RULE 2 dedup

If Option C ships first, Option B's deployment must DELETE the
Option C shim (per RULE 2). The shim should be marked
"transitional crutch with retirement plan: removed when Stream B
ships" in its comment so the next agent knows to delete it.

---

## Section 10 — IDA IDB renames + comments performed

This session set comments on the relevant engine UFunction FName
entries (no native function renames — the BP UFunctions in
question have no native addresses, as established in prior RE
docs):

| Addr | FName | Comment |
|---|---|---|
| `0x144147240` | `SetActorEnableCollision` | Candidate target for Stream A force-mature shim |
| `0x144205e10` | `SetCollisionEnabled` | UPrimitiveComponent per-comp collision; the UFunction Init reads via GetCollisionEnabled |
| `0x1442174e8` | `SetWorldScale3D` | SceneComponent scale; mushroom7 small-scale via UCS |
| `0x144147258` | `SetActorHiddenInGame` | AActor hidden-flag; suspected spawnedNaturally call |
| `0x1441472a0` | `SetActorScale3D` | AActor scale UFunction (alt to SetWorldScale3D) |

IDB saved at
`D:\Projects\Programming\VOTV_MP\Game_0.9.0n\WindowsNoEditor\VotV\Binaries\Win64\VotV-Win64-Shipping.exe.i64`.

---

## Cross-refs

- `research/findings/props-lifecycle/votv-aprop-lifecycle-RE-2026-05-24.md` —
  prior RE: Init body, spawn pipeline, destroy paths, cascading
  callback risks. Section 1d documents the mushroomMaster pipeline.
  Section 4b cites mushroom growth manager double-registration as
  the Inc3 risk this RE addresses.
- `research/findings/physics-grab/votv-physics-interaction-deep-re-2026-05-23.md` —
  prior RE: SetCollisionEnabled usage in the Stage 1 grab path
  (proves the reflection pattern for Option C works).
- `src/votv-coop/include/ue_wrap/prop.h` — existing prop helpers
  (`IsDescendantOfProp`, `FindByKeyString`, `FindNearbySameClass`).
  Option C adds `ForceRestoreDefaultCollision`.
- `src/votv-coop/include/coop/net/protocol.h` L266–L285 — current
  PropSpawnPayload shape (Option A would extend this; Option B/C
  do not).
- `src/votv-coop/src/coop/remote_prop.cpp` — receiver `OnSpawn`,
  where Option C's collision-restore call would be inserted.
- [[project-coop-mushroom-desync-and-remedy]] — the bug report
  this RE answers.
- [[project-coop-aprop-lifecycle-re-inc3-scope]] — Inc3 Stream A/B
  scope; this RE confirms Stream B is the right primary track for
  mushrooms (with Stream A's per-class hook on `spawnedNaturally`
  not actually needed because spawnedNaturally is bypassed under
  Stream B's spawner suppression).
- [[feedback-re-related-functions]] — the rule this RE satisfies.
- [[feedback-never-rush-research-first]] — RE-first before code.
