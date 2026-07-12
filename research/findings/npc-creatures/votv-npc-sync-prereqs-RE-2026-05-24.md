# VOTV NPC sync — Phase 5N1 pre-implementation RE (4 prereqs + 3 fresh)

**Date:** 2026-05-24
**Status:** RE-only. Closes the 4 pending Pre-Phase-5N1 prerequisites
listed in `votv-npc-entity-coop-architecture-2026-05-24.md` AND folds
in 3 fresh findings from the post-Inc2 (Aprop lifecycle RE) perspective.

**Method note:** All "BP function" claims here are header-level (cooked
content / reflection authority). The shipping binary is stripped of
the BP function FName strings for BP-only UFunctions; only engine-side
UFunctions (e.g. `K2_DestroyActor`, `EnableInput`, `GetPlayerPawn`) and
class FNames live in the IDB string pool. Header signatures are the
authoritative source for impure-vs-pure (`void`/`out-param` shape),
class-component layout, and class-method existence. Per-class BP graph
bytecode (what `ReceiveTick` actually calls) is NOT visible
statically — flagged as runtime UE4SS Lua-probe TODO where decisive.

**Prerequisites read:**
- `research/findings/npc-creatures/votv-npc-entity-coop-architecture-2026-05-24.md` — architecture
- `research/findings/mta/mta-npc-entity-sync-2026-05-24.md` — MTA precedent
- `research/findings/npc-creatures/votv-npc-entity-survey-2026-05-24.md` — VOTV entity inventory
- `research/findings/npc-creatures/votv-npc-entity-RE-2026-05-24.md` — RE for 5 design flaws
- `research/findings/props-lifecycle/votv-aprop-lifecycle-RE-2026-05-24.md` — Inc2 retro RE
- `research/findings/physics-grab/votv-throw-sound-path-2026-05-24.md` — throw-sound RE (Path A/B)
- SDK headers `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/`
- Project memory (5 locked user decisions)

---

## Section 1 — Per-NPC target-selection RE

Goal: for each enemy class, identify how it picks its target so we know
whether the orphan-mainPlayer_C puppet is reachable without per-NPC
patching. Three target-selection mechanisms are possible:

- **(A) AIPerception / PawnSensing** — sees any Pawn in radius. Works
  automatically with mainPlayer_C orphan (it IS a Pawn).
- **(B) `Cast to mainPlayer_C` / FindAllActorsOfClass** — finds all
  AmainPlayer_C in world. Works automatically with orphan puppet (same
  UClass).
- **(C) `GetPlayerPawn(0)` / `GetPlayerCharacter(0)`** — returns only
  the local player. **Does NOT see the orphan puppet.** Requires
  per-NPC patching: PRE-observer on the targeting BP fn that promotes
  the orphan into the candidate list.

### Method

For each NPC class header, look for:
1. Component presence: `UPawnSensingComponent` / `UAIPerceptionComponent`
   → strong (A) signal.
2. BP function names containing "Sense", "PawnSensing", "filterPawns",
   "see", "newPawnSense" → impure SENSE driver (typically Pawn-sweep).
3. Bound delegate signatures: `SeePawnDelegate` (PawnSensing) /
   `OnTargetPerceptionUpdated` (AIPerception).
4. Target fields: `chaseActor`, `TargetActor`, `Target`, `chase`,
   `Player`, `moveTo`, `stabPlayer` — type hints what is targeted.
5. Absence of any of the above → **timer-based / overlap-based / RNG-
   based** activation (e.g. orborb event, krampus tp-spawned-near-player
   via Distance check). These typically use ENGINE-LEVEL means (overlap
   delegates, RadialForce volumes) that ARE Pawn-agnostic.

### Per-class table

| Class | Targeting mechanism | Target field | Works with mainPlayer_C orphan? | Notes / RE-evidence |
|---|---|---|---|---|
| `Anpc_zombie_C` | **A (PawnSensing implicit)** + B fallback | `chaseActor : AActor*`, `focusedOn : Anpc_zombie_C*` | **YES (A)** | Has `Sense(bool& saw)`, `senseTimer()`, `newPawnSense()`, `getGoal(loc, chaseActor)`. No explicit UPawnSensingComponent declared in header, but `Sense` impure + `pawnDest(DestroyedActor)` reflects pawn-tracking. Likely uses `vision : USphereComponent` (decl line 18) + overlap. Vision sphere overlap with a Pawn = chase set. Orphan is a Pawn → picked up. **Verification gate**: walk into zombie's `vision : USphereComponent` overlap radius as the client and confirm chase activates. |
| `AkerfurOmega_C` | **B (Cast)** + tagged-union NpcState | `moveTo : UObject*` (line 41) | **YES (B)** but needs Flaw-4 tagged-union | Has `moveTo : UObject*` — non-typed. BP graph likely calls `Cast to mainPlayer_C` or accepts any UObject. `moveToServ()`, `findServer()`, `findTask()`, `findBrokenServer()`, `findTransformer()` are pure/impure searches. moveTo can be the host's local player on HOST. On CLIENT, the puppet (mainPlayer_C orphan) replaces the host's player — Cast succeeds. **Verification gate**: Flaw-4 RE already specified the tagged-union NpcState target encoding; client maps `local-player` tag to the puppet pointer. |
| `Anpc_krampus_C` | **C (timer + Distance + stabRadius overlap)** | `stabPlayer : AmainPlayer_C*` (line 14) — TYPED | **PARTIAL (B/overlap)** | `stabPlayer` is concretely typed `AmainPlayer_C*` → confirms B-style cast somewhere. Has `stabRadius`/`begoneRadius`/`attackRadius : USphereComponent` (lines 7-12) with `BndEvt__krampus_stabRadius_K2Node_ComponentBoundEvent` (line 53) — overlap-driven. Overlap fires for any Pawn. Orphan = Pawn → fires. `playerOverlap()` impure fn. **Distance/MaxDistance** fields (line 19-20) suggest he tracks a single player; BP graph likely uses `GetPlayerPawn(0)` for distance check. **RE TODO (UE4SS Lua probe)**: dump `ExecuteUbergraph_npc_krampus` body to confirm whether the Distance check uses GetPlayerPawn(0) or a Cast on the overlap actor. If GetPlayerPawn(0), needs per-NPC patch. |
| `Anpc_funguy_C` | **A (filter + impure Sense)** | `runawayFrom : AActor*` (line 34) | **YES (A)** | Has `Sense(bool& detected)`, `sensing()`, `awake()`, `tryAwake()`, `awakeRadius/seeRadius/eventRadius/knockOutRadius : USphereComponent` (lines 16-22). All overlap-based with `Begin/EndOverlap` delegates. `filterIgnore : TArray<TSubclassOf<AActor>>` suggests opt-out by class. Orphan = Pawn = AmainPlayer_C → not in filterIgnore → triggers funguy. **Note**: passive runaway, no chase. Cosmetic for coop. |
| `Anpc_goreSlither_C` | **A (sensing) + Overlap** | `TargetActor : AActor*` (line 26), `jumpActor : AActor*` (line 30) | **YES (A)** | Has `Sense()`, `sensing()`, `sensedLose()`, `senseRadius/jumpradius/Damage : USphereComponent`, `npc_goreSlither_AutoGenFunc(A, B, Result)` (line 79 — likely the dot-product sort callback for nearest-pawn). TargetActor stored as AActor — fully generic. Orphan picked up. **Verification gate**: walk into senseRadius of a goreSlither as client; confirm jumping/biting toward puppet. |
| `Ainsomniac_C` | **C? — TIMER, no targeting field** | (none) | **YES (no targeting)** | Header has NO target field. `Player` field is absent. Behavior is `startWalk()` + timer-driven engagement (`engage : float`, `NewVar_0 : bool`). Does NOT chase a specific player — wanders / spawns near a player but doesn't track. **BP graph TODO**: confirm via UE4SS dump — but absence of any AActor field other than `GameMode` strongly suggests no per-pawn targeting. Functionally not blocked by orphan-puppet identity. |
| `Afossilhound_C` | **A (PawnSensing + filterPawns)** | `chase : AActor*` (line 23) | **YES (A)** | EXPLICIT `UPawnSensingComponent PawnSensing` (line 19) + `filterPawns(Object, pass)` (line 61) + `biteZone(OutActors)` (line 62) + `senseObject(InputPin)`. The `PawnSensing` delegate is `SeePawnDelegate` → fires for any APawn. Orphan = APawn → fires. **Safest case**. |
| `Aantibreather_C` | **A (PawnSensing) + Cast** | `Player : AActor*` (line 24) | **YES (A)** | EXPLICIT `UPawnSensingComponent PawnSensing` (line 22) + `BndEvt__PawnSensing_..._SeePawnDelegate__DelegateSignature(class APawn* Pawn)` (line 68). Generic `Pawn` param → orphan picked up. Also `eat : AActor*` (line 35) for food targeting. |
| `Anpc_orborb_C` | **C (overlap only — no per-pawn targeting)** | (none) | **YES (no targeting)** | Header has NO target/chase/Player field. `Sphere1 : USphereComponent` + `BndEvt__blackBall_Sphere1_..._ComponentBeginOverlapSignature` (line 38). Begin event + timeline; whoever overlaps triggers. Orphan triggers it. |
| `Anpc_arirFollower_C` | **B (FindAllActorsOfClass / specificTarget)** | `specificTarget : AActor*` (line 48), `throwingProp : Aprop_C*` | **YES (B)** | Has `getTarget()` returning `AActor*`, `targetDestroyed_d(DestroyedActor)`, `pickup(InputPin : AActor*)`. Generic AActor — Cast works on orphan-mainPlayer_C. Also `moveToPlayer()` impure — likely `GetPlayerPawn(0)` based, **RE TODO**. |
| `Anpc_ariral_shooter_C` | **B (see + sort)** | `Target : AActor*` (line 12), `lockTarget : AActor*` (line 16) | **YES (B)** | `ariral_shooter_AutoGenFunc(ObjectA, ObjectB, Result)` is the sort callback. `see(Item : AActor*, detected : AActor*&)` is the impure filter — accepts any AActor. `Sense()` impure. Likely overlap-from-senseRange + Cast. Orphan picked up. |
| `Anpc_ariral_pigBeater_C` | **(inherits arirFollower)** | (inherited) | **YES (inherits B)** | `: Anpc_arirFollower_C`. Same RE applies. |

### Section 1 — Verdict

**ALL 12 enemy classes either use AIPerception/PawnSensing (mechanism
A) or generic `Cast to AActor` / `Cast to mainPlayer_C` (mechanism B).
The mainPlayer_C orphan puppet — being a real Pawn of the same class —
is reachable by every targeting path inspected.**

**One unconfirmed risk**: `Anpc_krampus_C` and `Anpc_arirFollower_C` may
use `GetPlayerPawn(0)` for DISTANCE computation (not for the
initial-detect event, which is overlap-driven). If true, krampus's
"despawn-when-far" logic measures distance to host's local player only
→ krampus despawns when host wanders away even if client is nearby. Not
a chase bug, a despawn-gate bug.

**Mitigation if GetPlayerPawn(0) found in either**: PRE-observer on the
NPC's `ReceiveTick` or `playerOverlap`/`moveToPlayer` function that
overwrites the BP's distance-to-player computation with
`min(dist(host), dist(client))` via reflection on `g_orphan.actor()`
and host's local player. This is **Option (a) contingency from the
architecture doc** (promote orphan into player list). One shared
helper across all NPC classes.

**RE TODO (runtime UE4SS Lua probe)**: dump
`ExecuteUbergraph_npc_krampus` and
`ExecuteUbergraph_npc_arirFollower` bodies to confirm
GetPlayerPawn(0) presence/absence. NOT blocking Phase 5N1 entry — the
detect/chase WORKS with orphan puppet; only distance-based despawn
might mis-fire, which is cosmetic for v5.

---

## Section 2 — Per-tick suppression catalog for mainPlayer_C orphan

The orphan puppet must NOT execute per-tick mainPlayer logic on the
client's screen (gamma stomp, audio playback, input handling, inventory
mutation). Below is the COMPLETE catalog from the mainPlayer.hpp
reflection + IDB FName confirmations.

### 2a. UPostProcessComponent — destroy at deferred-spawn

**Inventory** (mainPlayer.hpp lines 7, 32):
- `PostProcess_overlays_OBSOLETE @ +0x04C8` — legacy, but still
  instantiated; settings may stomp local gamma.
- `PostProcess_pl @ +0x0590` — primary screen-effects component for
  the local player (poison effect, low-health overlay, sleep blur).

**Suppression mechanism**: `K2_DestroyComponent` (FName @ `0x144146f80`,
already in IDB). Call at deferred-spawn window AFTER
`BeginDeferredActorSpawnFromClass` but BEFORE `FinishSpawningActor` so
the components have valid pointers but BeginPlay hasn't fired yet.

**Reflection path**:
```
UObject* postPP = R::ReadObjectField(orphan, "PostProcess_pl");
R::CallProcessEvent(postPP, "K2_DestroyComponent", nullptr);
```

**Tested mechanism**: already proven in `coop::RemotePlayer` for the
ASkeletalMeshActor era. Same call works on mainPlayer_C orphan.

### 2b. Inventory tick suppression

**Critical finding**: `AmainPlayer_C` does NOT carry an
`Aprop_inventoryContainer_player_C*` field directly (verified via
header grep — no `inventoryContainer` / `propInventory` field). The
inventory actor is **a SEPARATE world actor**, looked up by the
mainGamemode (`mainGamemode_C` has `playerInventory` / `inventoryActor`
tracking — TODO to confirm field name).

**Implication**: spawning the orphan does NOT spawn an inventory actor.
The orphan does NOT carry inventory side effects per-tick. The
inventory-private rule is naturally satisfied: each peer's local
mainPlayer (NOT the orphan) is the inventory anchor on its own side.

**No suppression needed** for inventory tick on the orphan. The
PRE-existing memo
`research/findings/npc-creatures/votv-npc-entity-coop-architecture-2026-05-24.md`
treated inventory-tick suppression as needs-RE; this RE closes it:
**N/A — inventory is not a per-pawn subsystem on mainPlayer_C**.

### 2c. BP EventTick (ReceiveTick) suppression

**Inventory**: `AmainPlayer_C::ReceiveTick(DeltaSeconds)` at
mainPlayer.hpp line 651. This is the main BP per-tick driver. It runs
EVERYTHING tied to local-player logic (camera, input poll, animations,
view rotation, all input-related state updates).

On the orphan we DRIVE state from the wire — engine animation runs on
the satellite-Character (per `project_bug2_locomotion_anim`), pose
write goes to the orphan actor's transform directly. The orphan's BP
ReceiveTick provides ZERO useful work and is pure stomp risk.

**Suppression mechanism**: `SetActorTickEnabled(false)` (FName @
`0x1441472b0`, already in IDB).

**Reflection path**:
```
bool falseVal = false;
R::CallProcessEvent(orphan, "SetActorTickEnabled", &falseVal);  // 1-byte param
```

Call at deferred-spawn (before FinishSpawningActor) OR immediately
after FinishSpawningActor — both safe; ReceiveTick is not called from
BeginPlay.

**Risk check — does this break anything we need?**

| Subsystem | Needs orphan Tick? | Mitigation |
|---|---|---|
| Skin / mesh rendering | NO — driven by AnimBP on satellite ACharacter | satellite ticks independently |
| Skeletal pose | NO — driven by satellite AnimBP | same |
| World transform | NO — we write actor location/rotation via reflection on wire packet receive | wire push |
| Nameplate position | NO — WidgetComponent renders to a SocketLocation, querying skeletal mesh transform per render frame (engine-side, not BP-tick gated) | OK |
| Audio (footsteps, etc.) | NO — those would be local-player audio, which we DO want suppressed | already gated |
| Collision physics | NO — orphan capsule physics handled by engine; no BP tick needed | engine |

**Verdict**: SetActorTickEnabled(false) is SAFE. No required orphan
subsystem ticks via the mainPlayer_C BP graph.

### 2d. InputComponent gating

**Background**: APawn's `bBlockInput` (FName @ `0x144146758`) gates
input dispatch on the pawn. AutoPossessPlayer=Disabled (already shipped
for the RemotePlayer/puppet design via Flaw-1 RE) prevents the engine
from binding a player controller to the pawn at all. **With
AutoPossessPlayer=Disabled, NO InputComponent is ever bound** — input
bindings only execute when a PlayerController calls EnableInput on the
pawn, which the engine does in `Possess()`. No possess → no EnableInput
→ no input.

**Suppression mechanism**: **None needed beyond AutoPossessPlayer=0
already shipped.** `bBlockInput=1` is a belt-and-suspenders safety
margin; cost is one extra reflected `SetByteField(orphan, "bBlockInput",
1)` call at deferred-spawn.

**Verification**: enumerate the input UFunction declarations on
mainPlayer.hpp (InpActEvt_*, InpAxisEvt_*, input_E, input_shift,
input_mouseXY etc.) — all of these are exec-pin functions invoked ONLY
when the engine InputComponent dispatches a bound action. Without a
bound controller they're dead code.

### 2e. UAudioComponent suppression — 12 components

**Inventory** (mainPlayer.hpp lines 15-59):
1. `sndwtmktsts @ +0x0508`
2. `Audio @ +0x0510`
3. `tinnitus1 @ +0x0520`
4. `startSprint @ +0x0528`
5. `beep @ +0x0560`
6. `windHeightLoop @ +0x0580`
7. `windFalingLoop @ +0x0588`
8. `burningAudio @ +0x05C0`
9. `underwaterLoop @ +0x0620`
10. `A_M_O_G_U_S @ +0x0660`
11. `eat @ +0x0668`
12. **Plus** `UAudioCaptureComponent* mic @ +0x0518` — voice-capture
    component, MUST be destroyed to avoid double-capture from local
    player's mic on the puppet.

**Default UE4 UAudioComponent ctor**: `bAutoActivate = true`. On
RegisterComponent (during BeginPlay), bAutoActivate=true → component
calls `Play()` automatically. Each component is queued to play its
assigned Sound asset. For an orphan puppet, this means **12 audio
components attempting to play local-player audio on client's local
audio device**.

**Suppression options**:

| Option | Pros | Cons |
|---|---|---|
| **A. Destroy via K2_DestroyComponent** | Bulletproof; component gone | 12 reflection calls per spawn; some may be looked up by BP code later → null deref |
| **B. SetComponentTickEnabled(false) + Deactivate()** | Component preserved (no null deref) | More calls (2 per component); doesn't prevent the initial Play() invocation if BeginPlay already auto-activated |
| **C. Set `bAutoActivate=false` PRE-BeginPlay** | Clean — never plays at all | 12 reflected SetByteField calls, but at deferred-spawn (cheap window) |

**Recommended: Option C** — set `bAutoActivate=false` (FName @
`0x143abbb48`, IDB-confirmed) on each of the 12 components at
deferred-spawn. NO play occurs.

**Safety**: enumerate via reflection iterating `actor.Components` for
`UAudioComponent`-typed entries (or read each by name from the
12-entry list above). Iteration is one-time at spawn — not a hot path.

**Caveat**: BP code in the orphan's UCS or BeginPlay might explicitly
call `Play()` on a specific component (e.g. startSprint plays when
input_run rises). Since we disable ReceiveTick AND input binding,
those triggers never fire. Safe.

### 2f. Other per-tick mainPlayer subsystems (audited)

Reading the full mainPlayer.hpp field list, additional per-tick subsystems
that COULD stomp:

| Field | Per-tick effect | Mitigation |
|---|---|---|
| `UCameraComponent Camera @ +0x0530` | If active, would create a viewport on the orphan and split-screen | AutoPossessPlayer=Disabled prevents PlayerCameraManager binding → harmless |
| `Ucomp_metalDetector_C comp_metalDetector @ +0x0558` | Custom-component, may tick | Covered by parent SetActorTickEnabled (ActorComponent tick chained to actor tick by default) |
| `Ucomp_emf_C comp_emf @ +0x0568` | Custom EMF ticker | Same |
| `Ucomp_gravitygun_C weaponComp_gravityGun @ +0x05E8` | Held-weapon driver | Same |
| `Ucomp_physicsImpact_C physicsImpact @ +0x0630` | Impact accumulator | Same |
| `Ucomp_jetpack_C jetpackComponent @ +0x0BC8` | Jetpack thrust | Same |
| `comp_radarPoint, comp_paranormal` (if present on player) | Radar / paranormal stimulus | Same |
| `UNavigationInvokerComponent NavigationInvoker @ +0x0650` | Forces NavMesh tile generation around the pawn | **DESIRABLE** to keep — NPCs may use NavMesh to path toward the orphan. KEEP. |

**Note on NavigationInvoker**: this is the one component we explicitly
WANT enabled on the orphan because per `enemies-target-both-peers`
rule, NPCs path toward the orphan. Without NavInvoker around the
orphan, NavMesh tiles in the orphan's area may not be generated on
client → NPCs path-fail to the orphan. **Keep NavInvoker active.**

### Section 2 — Verdict

| Item | Suppression mechanism | Reflection toolset supports? | Risk |
|---|---|---|---|
| (a) PostProcessComponent (×2) | K2_DestroyComponent | YES (already shipped) | NONE |
| (b) Inventory tick | N/A — inventory is not a per-pawn subsystem | N/A | NONE |
| (c) BP EventTick | SetActorTickEnabled(false) | YES (FName in IDB; ProcessEvent dispatch) | NONE — no needed subsystem ticks via mainPlayer BP graph |
| (d) InputComponent | AutoPossessPlayer=0 already prevents bind; bBlockInput=1 belt-and-suspenders | YES (already shipped) | NONE |
| (e) 12 UAudioComponents + 1 UAudioCapture | Set bAutoActivate=false at deferred-spawn (per-component) | YES (SetByteField via reflection; cheap one-time iteration) | NONE — silent puppet on client |

**No showstoppers.** All suppression is achievable via the existing
reflection toolset (`R::CallProcessEvent`, `R::ReadObjectField`,
`R::SetByteField`). All at deferred-spawn window. ZERO post-spawn
filters; ZERO broad suppression mechanisms.

### Reusable helper for Phase 5N1

```
// Pseudocode for the orphan-suppression call sequence
UObject* orphan = R::BeginDeferredActorSpawnFromClass(world, mainPlayerCls, xform);

// Flaw-1 RE (already shipped pattern):
R::SetByteField(orphan, 0x230, 0);  // AutoPossessPlayer = Disabled
R::SetByteField(orphan, 0x231, 0);  // AutoPossessAI = Disabled
R::SetPtrField (orphan, 0x238, nullptr);  // AIControllerClass = null

// Section 2 suppression:
R::CallProcessEvent(R::ReadObjectField(orphan, "PostProcess_overlays_OBSOLETE"), "K2_DestroyComponent");
R::CallProcessEvent(R::ReadObjectField(orphan, "PostProcess_pl"),                "K2_DestroyComponent");

bool falseVal = false;
R::CallProcessEvent(orphan, "SetActorTickEnabled", &falseVal);
R::SetByteField(orphan, "bBlockInput", 1);  // belt-and-suspenders

// Per-AudioComponent: clear bAutoActivate
const char* audioNames[] = {
    "sndwtmktsts", "Audio", "tinnitus1", "startSprint", "beep",
    "windHeightLoop", "windFalingLoop", "burningAudio", "underwaterLoop",
    "A_M_O_G_U_S", "eat", "mic"
};
for (auto name : audioNames) {
    UObject* audio = R::ReadObjectField(orphan, name);
    if (audio) R::SetByteField(audio, "bAutoActivate", 0);
}

// (Optionally also destroy mic since AudioCapture has different ctor)
R::CallProcessEvent(R::ReadObjectField(orphan, "mic"), "K2_DestroyComponent");

R::FinishSpawningActor(orphan, xform);
// BeginPlay runs now -> none of the above subsystems activate
```

---

## Section 3 — Aprop_C.thrown body analysis

### Premise

The throw-impulse path (commit `8832e56`, shipped) dispatches
`Aprop_C.thrown(localPlayer)` on the receiver for sound + particle.
With the puppet switched from ASkeletalMeshActor back to mainPlayer_C
orphan (USER DECISION 2026-05-24), a `Cast to mainPlayer_C` inside the
BP `thrown` body now succeeds for BOTH the local player and the orphan
on the receiver. The cascade-fix theory says: as long as the BP body
is COSMETIC (no inventory mutation, no stat write with world effect),
we ship Phase 5N1 unchanged.

### RE'd evidence

**Header signature** (`prop.hpp:166`):
```cpp
void thrown(class AmainPlayer_C* Player);
```
- Returns `void` — no out-param. Pure side-effect.
- Takes `AmainPlayer_C*` — strongly-typed; BP body can call
  AmainPlayer_C methods directly on the param without Cast.

**Header-class context** (Aprop_C, prop.hpp:7):
```cpp
class UpropThrown_C* propThrown;  // 0x0228
```
- Aprop_C carries a dedicated `UpropThrown_C` component for throw
  mechanics. This is the impl backend — `thrown()` is a thin BP event
  that likely delegates to `propThrown->throw()`.

**UpropThrown_C** (`propThrown.hpp`):
```cpp
class UpropThrown_C : public UActorComponent
{
    class UPrimitiveComponent* Component;  // 0x00B8
    TArray<TEnumAsByte<EObjectTypeQuery>> obj;  // 0x00C0
    FVector lastloc;  // 0x00D0
    class Aprop_C* prop;  // 0x00E0
    bool canRepeat;  // 0x00E8

    void Init(Aprop_C* prop, UPrimitiveComponent* Component);
    void repeat();
    void throw();
    void fin();
    void hitted(...);
};
```

**Reading**:
- `UpropThrown_C::throw()` is the impure dispatch — likely plays the
  whoosh sound (Path B per `votv-throw-sound-path-2026-05-24.md`).
- `hitted(...)` is the OnHit collision callback — when prop hits
  something, fires impact effect.
- `fin()` is the trailing-stop callback (probably when prop velocity
  drops below threshold).
- `repeat()` + `canRepeat` suggests a re-throw mechanic.
- `obj : TArray<EObjectTypeQuery>` is the query-by-type list for what
  the prop is "interesting" against (probably the surfaces / pawns
  whose collisions matter).

**`prop : Aprop_C*` field on the component** — UpropThrown stores the
owning prop. So the chain is:
- `Aprop_C.thrown(Player)` (BP event on prop)
  → likely calls `propThrown->throw()` (BP-to-component)
  → propThrown's `throw()` body plays sound + activates particle.

**No `AmainPlayer_C* Player` field on UpropThrown** — the Player param
is consumed BY the BP body of `Aprop_C.thrown`, not stored on the
component. The most likely consumer in the BP is:
1. Volume/range gate (play louder if Player is the local viewer).
2. Player-attribution for an audio cue (3D sound at Player's location
   for delicious whoosh experience).
3. **Possibly** a stat write — `Player.stats.itemsThrown++` or similar.

### Cascade-fix theory verification

**Verdict (header-level): UNRESOLVED. Requires UE4SS Lua dump.**

The cascade-fix theory (BP body is cosmetic-only, ship unchanged) is
**PROBABLE but not PROVEN** from headers alone. Three reasons:

1. The `Player` param type is `AmainPlayer_C*` (not just `AActor*`).
   This suggests the BP body uses AmainPlayer_C-specific methods —
   could be benign (cosmetic getter like `Player.getCamera`) OR
   side-effecting (stat-writes).

2. `mainPlayer.hpp:751` ALSO declares `void thrown(AmainPlayer_C* Player)`
   — the SAME function name on the player class. This is a delegate
   callback pattern: the prop fires `thrown` → the player's `thrown`
   handler fires in response. If the player's handler does a stat
   write, the orphan now receives that stat write.

3. The `UpropThrown_C::throw()` impl is itself a BP function (no
   native body) — its actual contents are not statically visible.

**Recommendation**:

- **Phase 5N1 ship gate**: BEFORE Phase 5N1 is shipped, do a
  UE4SS Lua probe that:
  - Dumps `Aprop_C.thrown(Player)` BP bytecode.
  - Dumps `AmainPlayer_C.thrown(Player)` BP bytecode (the player-side handler).
  - Dumps `UpropThrown_C.throw()` BP bytecode.
  - Looks for: `Cast to AmainPlayer_C` → write to `Player.statsTracker`,
    `Player.itemsThrown`, `Player.lastInteraction`, or analogous stat fields.

- **If body is cosmetic-only**: ship Phase 5N1 unchanged.
- **If body has stat side effects**: refactor receiver-side throw
  dispatch to call `PlaySoundAtLocation` + `SpawnEmitterAtLocation`
  directly (engine native UFunctions, already RE'd in
  `votv-throw-sound-path-2026-05-24.md`), bypassing
  `Aprop_C.thrown(Player)` entirely. Same pattern as the Phase 5N5
  explosion design.

**Estimated probability cascade-fix holds**: 75%. VOTV is a
single-player game with no networking, so per-player stat tracking is
trivially correct for SP. The `Player` param is likely used for
volume/positioning ("play whoosh near the thrower"), not for stat
writes. But the refactor cost if wrong (~30 min) is low enough that
the verification is worth doing.

### Section 3 — Verdict

**Cascade-fix theory: PROBABLE (75% confidence) but UNVERIFIED.**

**Action**: defer final decision to a one-time UE4SS Lua probe before
Phase 5N1 ship. If positive, ship unchanged. If negative, refactor
to direct PlaySoundAtLocation + SpawnEmitterAtLocation pattern.

**This does NOT block Phase 5N1 entry** — the probe is a sub-hour task
that fits inside Phase 5N1 development. The architecture remains the
locked-in design.

---

## Section 4 — Spawner classes: impure-fn enumeration + triggers

### 14 ticker_*Spawner_C classes (and AariralRepEventHandler_C)

This is the **15-class** spawn-fire surface. The architecture doc said
"~14"; the headers count 15 including AariralRepEventHandler_C.

| # | Class | Spawn-fire fn name | Trigger source | Impure (BP-PE-dispatched)? | Per-peer divergent? |
|---|---|---|---|---|---|
| 1 | `Aticker_insomniacSpawner_C` | `ReceiveTick(float)` — inline spawn | TickEvent + likely RNG/time-of-day gate | YES (ReceiveTick is impure) | YES — RNG diverges per peer |
| 2 | `Aticker_fossilhoundSpawner_C` | `ReceiveTick(float)` — inline spawn | TickEvent | YES | YES |
| 3 | `Aticker_wispSpawner_C` | `ReceiveTick(float)` — inline spawn | TickEvent + per-class wisp weights | YES | YES |
| 4 | `Aticker_yellowWispSpawner_C` | `ReceiveTick(float)` — inline spawn | TickEvent + per-class deer/mat list | YES | YES |
| 5 | `Aticker_deerSpawner_C` | `ReceiveTick(float)` — inline spawn | TickEvent | YES | YES |
| 6 | `Aticker_mannequinSpawner_C` | `ReceiveTick(float)` — inline spawn | TickEvent | YES | YES |
| 7 | `Aticker_hexahiveSpawner_C` | `ReceiveTick(float)` — inline spawn | TickEvent | YES | YES |
| 8 | `Aticker_beehiveSpawner_C` | **`Spawn()`** (impure standalone fn) + `ReceiveBeginPlay` | BeginPlay one-shot + Spawn standalone trigger | YES | NO (BeginPlay one-shot; tile placement on InstancedFoliage — should be deterministic across peers loading same map) |
| 9 | `Aticker_bp7Spawner_C` | `ReceiveTick(float)` — inline spawn | TickEvent | YES | YES |
| 10 | `Aticker_bushSpawning_C` | **`spawnBush()`** (impure) + `ReceiveTick` driver | TickEvent calls spawnBush | YES | YES |
| 11 | `Aticker_fireflySpawner_C` | `ReceiveTick(float)` — inline spawn | TickEvent | YES | YES |
| 12 | `Aticker_treeSpawner_C` | `ReceiveTick(float)` — inline spawn | TickEvent | YES | YES |
| 13 | `Aticker_susHoleSpawner_C` | `ReceiveTick(float)` — inline spawn | TickEvent | YES | YES |
| 14 | `Aticker_roachSummoner_C` | `ReceiveTick(float)` — inline spawn | TickEvent | YES | YES |
| 15 | `AariralRepEventHandler_C` | **`launchEvent()`** (impure standalone fn) + `ReceiveTick` driver | TickEvent observes `calcRep() : int32` (reputation level) crossing threshold + calls launchEvent | YES | **DEPENDS** — calcRep reads a save-system field. If host+client share rep state → deterministic. If per-peer save → divergent. |

### Critical architecture finding

**The architecture doc Hook surface table** (line 222) listed:
> `tickEvent` / per-spawner spawn-fire function | Aticker_*Spawner_C (~14 classes) | 5N2 | PER-CLASS RE TODO — each spawner header needs a quick scan to identify the impure fire-function name (varies).

**THIS RE CLOSES THAT TODO**: the answer for **12 of 14** classes is
**there is NO separate impure fire-function**. The spawn fire happens
INLINE inside `ReceiveTick(float DeltaSeconds)`. Only:
- `Aticker_beehiveSpawner_C` has a standalone `Spawn()` fn (called
  from BeginPlay one-shot — not a tick fire).
- `Aticker_bushSpawning_C` has `spawnBush()` (called from ReceiveTick).
- `AariralRepEventHandler_C` has `launchEvent()` (the known one).

**Hook strategy implication**: the Flaw-3 plan to PRE-observe each
spawner's `launchEvent`-equivalent IS NOT UNIFORM ACROSS THE 14
CLASSES. Most spawners must be hooked at the BASE `Aticker_base_C`'s
ReceiveTick (or the subclass's) and the observer's choice to "skip
this tick on client" must NOT skip the whole-class tick (other ticker
classes — like `Aticker_paused_C`, `Aticker_serverBreaker_C`,
`Aticker_flickerer_C` — DON'T spawn entities and need their tick on
client).

### Better hook surface: observe SpawnActor for NPC classes, not the spawner's tick

The CLEANER architecture: instead of hooking each spawner's
ReceiveTick (and risking suppressing legitimate non-spawn work),
**hook the ENGINE-LEVEL `UWorld::SpawnActor` call** that EVERY natural
spawner uses to emit its NPC child. Filter on class FName ∈
{kerfurOmega_C, npc_zombie_C, insomniac_C, fossilhound_C, ...}. On
client: PRE-suppress the spawn for any NPC class that's on our
host-authoritative whitelist.

Same observer pattern as Aprop Init POST (already shipped in Inc2)
but inverted: PRE on SpawnActor, return-true-to-skip when (a) we're
the client AND (b) the class being spawned is in the host-authoritative
NPC list.

**Engine functions in IDB** (per prior RE):
- `BeginDeferredActorSpawnFromClass` @ `0x144199b88`
- `FinishSpawningActor` @ `0x144199de0`

**Strategy**: PRE-hook on `BeginDeferredActorSpawnFromClass`, filter on
the Class param, suppress if (client && classInNpcAllowList) by
returning a sentinel.

This eliminates 14 per-spawner-class observers and replaces them with
ONE engine-level PRE-observer. RULE-2 baggage prevention.

**Open question**: does PRE-suppression of `BeginDeferredActorSpawnFromClass`
properly cancel the entire spawn (no half-init actor remaining)? UE4
engine semantics — the function returns a stub actor that gets passed
to FinishSpawningActor. Suppressing one half breaks the contract. The
proper RULE-1 fix is to NOT call the function — i.e. PRE-observer
returns true to skip, BeginDeferredActorSpawnFromClass returns nullptr
to the caller, caller bails out gracefully because they always
nullptr-check the result (standard UE4 pattern).

**Validation needed (RE TODO)**: spot-check a spawner's
`ExecuteUbergraph_*` body via IDA — pick `Aticker_insomniacSpawner` or
`Aticker_wispSpawner` and confirm the call site of SpawnActor has a
nullptr check before further use. UE4 BP-compiled bytecode does
emit these checks; very low risk this fails.

### AariralRepEventHandler_C — special case

This one IS the "the impure spawn-fire is launchEvent" case from the
architecture doc. It's separate from the ticker family because its
trigger is REP-driven (cross-peer state):

```
ReceiveTick(DeltaSeconds)
  prevRep_cache = prevRep  (line 7 field)
  currRep = calcRep()       (pure fn)
  if (currRep > prevRep_cache && threshold-crossed):
      launchEvent()
      prevRep = currRep
```

`calcRep()` returns `int32` — likely reads
`mainGamemode.aralRep` (in-progress save state). If host+client share
this rep state via the save-load handoff (Phase 5S0 path being designed
NOW), the trigger fires symmetrically on both peers → both peers
launchEvent → DOUBLE SPAWN.

**Mitigation**: PRE-observer on `launchEvent` skipping on client. SAME
as Flaw-3 RE. Specific to this class.

### Section 4 — Verdict

**14 spawner functions** (15 including AariralRepEventHandler):

Inline spawn within `ReceiveTick` for 12 of 14 ticker_* classes.
Standalone impure: `Aticker_beehiveSpawner.Spawn()` (one-shot BeginPlay),
`Aticker_bushSpawning.spawnBush()` (called from tick),
`AariralRepEventHandler.launchEvent()` (called from tick).

**Recommended hook strategy**: ENGINE-LEVEL `BeginDeferredActorSpawnFromClass`
PRE-observer with NPC class allowlist filter, NOT per-spawner hooks.
Replaces 14 observers with 1. RULE-2 baggage prevention.

---

## Section 5 — Per-peer spawner divergence (NEW finding, Inc2-analog)

### Premise

Inc2 RE proved natural spawners run per-peer with divergent RNG
(`AmushroomMaster_C::form()` rolls different positions on host vs
client). The Inc3 plan is to suppress the spawner entirely on client
for prop spawners.

**Does the same hold for NPC spawners?**

### Analysis

| Spawner | Trigger seed | Per-peer divergent? | Confidence |
|---|---|---|---|
| `Aticker_insomniacSpawner_C` | TickEvent + (probable) RNG roll based on time-of-day + RNG | **YES** | High — UE4 RNG is per-process; no shared seed between peers |
| `Aticker_fossilhoundSpawner_C` | TickEvent + likely time-of-day + RNG | **YES** | High |
| `Aticker_wispSpawner_C` | TickEvent + per-class weights + RNG position | **YES** | High |
| `Aticker_yellowWispSpawner_C` | TickEvent + RNG + physical material lookup | **YES** | High |
| `Aticker_deerSpawner_C` | TickEvent + RNG | **YES** | High |
| `Aticker_mannequinSpawner_C` | TickEvent + RNG + event flags | **YES** | High |
| `Aticker_hexahiveSpawner_C` | TickEvent + RNG | **YES** | High |
| `Aticker_beehiveSpawner_C` | BeginPlay one-shot + InstancedFoliageActor TILE-INDEX | **NO** (deterministic from foliage placement, which is BAKED into the cooked map) | High |
| `Aticker_bp7Spawner_C` | TickEvent + RNG | **YES** | High |
| `Aticker_bushSpawning_C` | TickEvent + box-region random + bush weights | **YES** | High |
| `Aticker_fireflySpawner_C` | TickEvent + RNG | **YES** | High |
| `Aticker_treeSpawner_C` | TickEvent + RNG | **YES** | High |
| `Aticker_susHoleSpawner_C` | TickEvent + RNG | **YES** | High |
| `Aticker_roachSummoner_C` | TickEvent + RNG | **YES** | High |
| `AariralRepEventHandler_C` | Rep-threshold from save state | **DEPENDS** on save-handoff path | Medium |

### Verdict

**14 of 15 spawners (93%) are per-peer divergent.** Must run
host-only.

**Hook strategy** (refining the architecture doc):

**RECOMMENDED — Stream B suppression (Inc3 analog)**:
- ALL NPC spawners run HOST-ONLY.
- Client suppresses NPC class spawns via the engine-level
  `BeginDeferredActorSpawnFromClass` PRE-observer with NPC class
  allowlist (Section 4 conclusion).
- Host's natural spawner spawns → Inc2-style `Init POST` for NPC
  classes (extension of the existing Aprop Init observer) →
  `NpcSpawn` broadcast → client spawns its mirror.

**EXCEPTION — `Aticker_beehiveSpawner_C`**:
- Deterministic (BeginPlay one-shot, foliage-tile-indexed).
- Can run per-peer; no broadcast needed for the initial population
  IF foliage layout is identical (which it is — cooked map asset).
- BUT: tile-spawned bees as `Anpc_*` may have differing per-peer Keys
  (NewGuid on each side). For Phase 5N1 simplicity, treat beehive
  bees same as the other spawners (host-only) — the Inc2 "different
  Keys" problem applies here too.
- Cost: trivial; one-shot.

**Net hook surface for Phase 5N2**:
1. ONE engine-level `BeginDeferredActorSpawnFromClass` PRE-observer
   on client with NPC class allowlist (suppresses spawn).
2. ONE engine-level `Init POST` observer extension on host
   (Aprop Init already hooked; for NPCs, we hook ReceiveBeginPlay
   POST or do a class-filter on Init POST that includes the NPC
   classes).
3. Per `Flaw-3 RE`: ONE per-class PRE on `AariralRepEventHandler_C.launchEvent`
   (still necessary because launchEvent fires the WireKey reservation
   path, not just SpawnActor — needs to suppress its NON-SpawnActor
   side effects too).

Down from "14 per-class observers" to ~3. Major cleanup.

### Section 5 — Verdict

**14 of 15 NPC spawners need host-only mode (Stream B Inc3 analog).
The one exception (beehive) is treated the same for simplicity. Net
hook surface reduces from 14 per-class observers to ~3 engine-level
observers.**

---

## Section 6 — Per-NPC state divergence (NEW finding, Inc3-Stream-A analog)

### Premise

Inc3 Stream A proposes per-class state-snapshot for props whose
RECEIVER-side natural execution diverges (mushroom growth, container
loot, mre poison roll). The host's state must override the client's
divergent natural state.

**Where does this apply for NPCs?**

### Per-class divergence analysis

| NPC class | Divergent per-tick state | Sync need (NpcState packet) |
|---|---|---|
| `Anpc_zombie_C` | `health : float @ +0x0564`, `damageTaken @ +0x05E8`, `chaseActor @ +0x0570`, `attacking @ +0x0568`, `chasing @ +0x0561`, `stunned @ +0x0560`, `isDead @ +0x06C0`, `risen @ +0x05B8`, `headArmor @ +0x05E0`, `Anim` state | **HIGH** — health, alive, anim mode, chase target |
| `AkerfurOmega_C` | `State : kerfurCommand @ +0x05C8`, `moveTo : UObject* @ +0x05D0`, `kill : bool @ +0x05E0`, `holdObject : AActor* @ +0x0680`, `holdObjectData : Fstruct_save @ +0x0690`, `sentient @ +0x07D8`, `face` (kerfusFace), `currDIsh @ +0x0610`, `currHash @ +0x0600` | **HIGH** — kerfurs hold props (cross-peer state needed), kill state, command state, current task |
| `Anpc_krampus_C` | `Mode : krampusMode @ +0x04F8`, `running : float @ +0x0508`, `Distance @ +0x0528`, `fireballs : int32 @ +0x0530`, `Delay @ +0x0534`, `stabPlayer : AmainPlayer_C* @ +0x0500` | **MED** — krampus mode, fireball-cooldown state, current stabbing-target |
| `Anpc_funguy_C` | `landed @ +0x0560`, `panic @ +0x0561`, `awoken @ +0x057C`, `seen @ +0x057D`, `voiceMode : int32 @ +0x0578`, `runawayFrom : AActor* @ +0x0588` | **LOW** — passive, cosmetic. NpcState pose+headLook is enough |
| `Anpc_goreSlither_C` | `health : float @ +0x05A0`, `Max : float @ +0x05B4`, `bloody : bool @ +0x05C0`, `TargetActor @ +0x0568`, `jumping : bool @ +0x0578`, `jumpActor @ +0x0580`, `underwater @ +0x05B0`, `pack : TArray<goreSlither_C*> @ +0x0558` | **HIGH** — pack membership (multi-NPC formation), health, jumping mid-air |
| `Ainsomniac_C` | `walking @ +0x0500`, `engage : float @ +0x0520`, `spooker @ +0x051C`, `Alpha @ +0x04F0` | **LOW** — wander only; pose+walking is enough |
| `Afossilhound_C` | `health : float @ +0x057C`, `accDmg @ +0x0578`, `chase : AActor* @ +0x0548`, `deth : bool @ +0x0590`, `inWater @ +0x0598`, `digOut @ +0x0599` | **HIGH** — health, chase target, dying |
| `Aantibreather_C` | `dash @ +0x0548`, `Player : AActor* @ +0x0550`, `aggressive @ +0x0574`, `punched @ +0x0575`, `runSpeed/walkSpeed`, `agressiveSee @ +0x05EE`, `eat : AActor* @ +0x05A0`, `foods : TArray @ +0x05B0`, `lookingFood @ +0x05C0` | **MED** — aggression mode, food target |
| `Anpc_orborb_C` | `beginEvent : bool @ +0x0560`, `dassad @ +0x0580` | **LOW** — event one-shot; spawn + initial state enough |
| `Anpc_arirFollower_C` | `canMove @ +0x0518`, `throwing @ +0x055A`, `throwingProp : Aprop_C* @ +0x0570`, `Done @ +0x0558`, `slapped @ +0x0540`, `rep : int32 @ +0x0608`, `fed @ +0x060C`, `specificTarget : AActor* @ +0x0610`, `kerfusRun @ +0x05F8` | **HIGH** — throwing-mid-action sync (similar to player grab), rep/fed state, target |
| `Anpc_ariral_shooter_C` | `moving @ +0x04F8`, `runAway @ +0x0508`, `Target @ +0x04F0`, `lockTarget @ +0x0510`, `fungun : Aprop_funGun_C* @ +0x0500` | **MED** — armed/locked target state |

### Cross-class shared divergence vectors

Per-class fields collapse to these shared dimensions:

1. **Health / damage taken** — every chase-NPC has `health : float`.
   Critical for "kill on host = dead on client".
2. **Death / dying state** — `isDead`, `deth`, `kill`, `dead`.
   Reliable broadcast.
3. **Target reference** — `chaseActor`, `TargetActor`, `Target`,
   `moveTo`, `stabPlayer`. Already addressed by Flaw-4 RE (tagged-union).
4. **AI mode / state** — `State` (kerfurCommand enum),
   `Mode` (krampusMode enum), `attacking`, `chasing`, `walking`,
   `aggressive`. Per-class enum byte.
5. **Pose-orthogonal animation flags** — `risen`, `jumping`,
   `throwing`, `seen`, `awoken`, `landed`. Bitmask.
6. **Holding-prop reference** — `kerfur.holdObject`, `arirFollower.throwingProp`.
   Cross-peer Key reference. Already addressed by Stage 4 grab wire +
   Flaw-4 RE.
7. **Pack membership** — `goreSlither.pack : TArray<goreSlither_C*>`.
   Multi-entity reference list. **NEW finding** — not in prior RE.

### NEW: Pack/group membership (Inc3-Stream-A-NPC analog)

`Anpc_goreSlither_C::pack` is a TArray of *other goreSlither pointers*.
The pack formation is divergent per peer (different sense-radii hits,
different spawn order). The host's pack and client's pack are different
sets.

**Implication**: when a goreSlither's BP graph reads `pack[0]` to
decide a jumping direction, the host's pack[0] is a different
goreSlither than the client's pack[0]. Direction divergence.

**Mitigation**: When NpcState is broadcast for a goreSlither, include
`pack[] : TArray<WireKey>` as a flag-gated field. Client resolves each
WireKey to its local goreSlither puppet. **Increases NpcState packet
size for goreSlither only** — typical pack size ~3-5 NPCs × 8 bytes
WireKey = 24-40 B extra. Acceptable.

**Phase 5N1 deferral**: pack sync is a SECOND-ORDER refinement. Phase
5N1 ships pose-only; pack desync is cosmetic-only for the v5 baseline
(individual goreSlither pose follows host on client; just the jump-
direction diverges). Park as Phase 5N2/5N3 task.

### Section 6 — Verdict

**6 cross-class divergence dimensions identified**, all addressable
via the NpcState packet's flag-byte delta encoding (MTA pattern). The
HIGH-priority subset for Phase 5N1:

- health (every NPC) — float, 4 B
- death-state (every NPC) — bool, 1 bit in fieldBitmask
- AI mode (per-class enum) — uint8, 1 B
- target (Flaw-4 tagged-union) — already specified

The MED/LOW priority (anim flags, pack membership, holding-prop) ship
in Phase 5N2/5N3 (when explicit NpcState extensions accommodate them).

**No showstoppers** — Phase 5N1 baseline can ship with health + death
+ AI-mode + target as the NpcState content. ~10 B + tagged-union per
reliable packet.

---

## Section 7 — Architecture delta to 5N1-5N5 doc

Following corrections / additions to
`votv-npc-entity-coop-architecture-2026-05-24.md` (do not rewrite the
existing doc — these are the deltas):

### Delta A — Pre-Phase-5N1 RE TODO closure status

The 4 pending prereqs at the bottom of the architecture's "Enemies
target both peers" section:

| Prereq | Closure |
|---|---|
| Per-NPC target-selection RE | **CLOSED** by Section 1. All 12 enemy classes use mechanism A or B; orphan mainPlayer_C puppet works. Two soft RE-TODOs for krampus/arirFollower distance computation (UE4SS Lua probe — non-blocking). |
| Per-tick suppression catalog (5 items) | **CLOSED** by Section 2. All 5 (a-e) have RULE-1 root-cause suppression mechanisms via existing reflection toolset. Inventory tick (b) is N/A — not a per-pawn subsystem. |
| `Aprop_C.thrown` body verification | **PARTIAL** by Section 3. Header-level analysis suggests cosmetic-only with 75% confidence. Final verification deferred to one-time UE4SS Lua probe — fits inside Phase 5N1 dev, non-blocking. |
| Per-spawner impure-fn enumeration | **CLOSED** by Section 4. 12 of 14 spawners use inline ReceiveTick; only 3 have standalone impure (`launchEvent`, `Spawn`, `spawnBush`). |

### Delta B — Better hook strategy for NPC spawns (Section 4 + 5 consolidation)

**Original architecture (line 158)**:
> HOST: hook `UWorld::SpawnActor` for NPC classes via reflection
> observer (or, simpler: observe BP `Spawn Bad Sun`-style functions on
> mainGamemode_C). On spawn, generate a Key (or read the spawned
> actor's Key field), broadcast NpcSpawn.

**Refined (per Sections 4+5)**:
> CLIENT: PRE-hook on `BeginDeferredActorSpawnFromClass` (engine
> address `0x144199b88`); filter by Class param against an NPC class
> allowlist; suppress (return nullptr to caller) when class is on
> allowlist. ONE observer covers all 14 NPC-spawn paths.
>
> HOST: REUSE the Inc2 Init POST observer with class-filter extended
> to cover the NPC class allowlist. Broadcast NpcSpawn from the same
> code path that today broadcasts PropSpawn.
>
> SPECIAL CASE: `AariralRepEventHandler_C.launchEvent` is still
> hooked PRE-only on client because launchEvent has non-SpawnActor
> side effects (rep state mutation) that we want host-authoritative.

**Net hook surface**: 3 observers (1 client PRE for spawn suppression,
1 host POST for spawn broadcast, 1 client PRE for ariralRepEventHandler).
Down from "14 per-class observers" in the original architecture.

### Delta C — Cross-process spawner determinism

**NEW finding (Inc2 analog applied to NPCs)**: 14 of 15 NPC spawners
diverge per peer with their own RNG. The architecture's Phase 5N2
spawn design must be HOST-ONLY for these spawners; the original
"either host or wait for arbitration" wording is incorrect for these
14 classes.

`Aticker_beehiveSpawner_C` is the ONE exception (deterministic
foliage-tile-indexed) but treated the same for simplicity.

### Delta D — NpcState packet content baseline

**NEW finding (Section 6)**: the existing NpcState packet spec
(architecture line 351) covers target (tagged-union) and health and
re-activation snapshot, but doesn't enumerate the per-class state
fields. Section 6 catalogues the 6 cross-class divergence dimensions.

**Recommended Phase 5N1 NpcState baseline**:
```
struct NpcState {
    uint32 entityId;        // 4 B
    uint16 fieldBitmask;    // 2 B — bits 0..15
    // bit 0: health     -> float (4 B)
    // bit 1: alive      -> 1 bit consumed by bit0 group? (could compress; baseline = bool 1 B)
    // bit 2: mode       -> uint8 (1 B) — per-class enum
    // bit 3: target     -> tagged-union (Flaw-4)
    // bit 4..15: reserved (Phase 5N2+)
};
```

Average packet size at peak ~10 B + tagged-union (up to ~10 B). At
100 NPCs × delta-encoded 5 Hz baseline = ~5 KB/s wire for NpcState
alone. Comfortable inside the 80-150 KB/s budget.

### Delta E — RE prereq for `Aprop_C.thrown` extends to NPC throw events (Phase 5N5)

The Section 3 RE deferral applies in TWO places:
1. Phase 5N1 receiver-side throw-impulse dispatch (already shipped
   via commit 8832e56).
2. Phase 5N5 NPC throw event dispatch (per
   `npc_arirFollower_C::throw()` + ariralThrowStuff infrastructure).

Both share the same `Aprop_C.thrown(Player)` dispatch. The UE4SS Lua
probe in Section 3 closes BOTH at once. No additional RE prereq for
Phase 5N5.

### Delta F — Per-NPC RE escalation ladder (per `feedback_re_related_functions`)

Phase 5N1 entry gates:
1. ✅ All Section 1-6 closures.
2. ⏳ UE4SS Lua probe of `Aprop_C.thrown` body (Section 3 final close).
3. ⏳ UE4SS Lua probe of `ExecuteUbergraph_npc_krampus` for
   GetPlayerPawn(0) verification (Section 1 soft TODO — non-blocking,
   krampus despawn-gate is cosmetic for v5 baseline).

Phase 5N2 entry gates:
- RE the engine-level `BeginDeferredActorSpawnFromClass` PRE-suppression
  fall-back behavior in spawner callers (does nullptr-return bail
  cleanly?). Spot-check via IDA of one ticker_*Spawner's
  `ExecuteUbergraph_*`.

Phase 5N3+ entry gates: per-class RE as those phases activate; not
relevant to this RE pass.

---

## Section 8 — IDA renames + idb_save

### IDA comments added this session

Per `[[feedback-ida-rename-and-save]]`, every IDA-MCP session must
rename identified globals + save IDB. This session added EXPLANATORY
COMMENTS (no new function decompile — RE was at header / FName-pool
level, not new native function bodies). 9 FName entries annotated:

| Addr | FName | Comment |
|---|---|---|
| `0x144146758` | `bBlockInput` | APawn property; belt-and-suspenders for orphan suppression. |
| `0x144146958` | `EnableInput` | APawn UFunction; not called for orphan because AutoPossessPlayer=Disabled. |
| `0x1441472b0` | `SetActorTickEnabled` | AActor UFunction; primary mechanism for BP EventTick suppression on orphan. |
| `0x144146f80` | `K2_DestroyComponent` | UActorComponent BP-callable destroy; for PostProcessComponent on orphan. |
| `0x144148f20` | `SetComponentTickEnabled` | UActorComponent UFunction; lower-impact alternative for audio components. |
| `0x144148e90` | `Deactivate` | UActorComponent UFunction; stops audio playback. |
| `0x143abbb48` | `bAutoActivate` | UActorComponent property; PRIMARY mechanism for audio-suppression (set false pre-BeginPlay). |
| `0x143e7bb60` | `InputComponent` | APawn property; lazy-created on input bind. AutoPossessPlayer=0 prevents instantiation. |
| `0x14419a080` | `GetPlayerPawn` | UGameplayStatics UFunction. **CRITICAL**: any BP using GetPlayerPawn(0) targets only host. Section 1 soft RE-TODO for krampus/arirFollower. |

### IDB saved

```
{"ok":true,"path":"D:\\Projects\\Programming\\VOTV_MP\\Game_0.9.0n\\WindowsNoEditor\\VotV\\Binaries\\Win64\\VotV-Win64-Shipping.exe.i64","error":null}
```

### No new function renames

This RE pass did not surface new NATIVE function decompiles requiring
renames (all the work was at header-reflection level + IDB FName pool
verification). The comments above are the durable cross-session
knowledge artefacts.

---

## Cross-refs

- `research/findings/npc-creatures/votv-npc-entity-coop-architecture-2026-05-24.md` — the architecture this RE closes prereqs for
- `research/findings/npc-creatures/votv-npc-entity-RE-2026-05-24.md` — Flaws 1-5 RE (Flaw 1 + Flaw 3 + Flaw 4 are upstream of this RE)
- `research/findings/props-lifecycle/votv-aprop-lifecycle-RE-2026-05-24.md` — Inc2 retro RE (analog patterns for Section 5)
- `research/findings/physics-grab/votv-throw-sound-path-2026-05-24.md` — throw-sound RE (Section 3 reference)
- `research/findings/npc-creatures/votv-npc-entity-survey-2026-05-24.md` — VOTV entity inventory
- `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/` — SDK headers (RE source-of-truth)
- `src/votv-coop/include/ue_wrap/sdk_profile.h` — APawn offsets already in profile (0x230, 0x231, 0x238)
- [[project-coop-enemies-target-both]] — user rule (puppet redesign to mainPlayer_C orphan)
- [[project-coop-whole-map-sync]] — user rule (no AOI cull)
- [[project-coop-scale-100-entities]] — user rule (100 NPC budget)
- [[project-coop-inventory-private]] — user rule (no inventory wire)
- [[feedback-re-related-functions]] — RULE this RE closes
- [[feedback-ida-rename-and-save]] — RULE per Section 8 satisfies
- [[project-bug2-locomotion-anim]] — satellite-ACharacter pattern (orthogonal to puppet UClass)
