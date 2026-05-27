# RE: puppet walk/run animation gate — root cause 2026-05-27

User report (post v0.0.1): walk/run anims STILL not playing on the puppet
despite Pawn/Movement/Character/Controller cache writes (commits 44b581b
+ 8d10476). RE re-investigation finds the **gate is not a "missing bool"
in the AnimBP — it is that BUA itself clobbers our writes every tick on
the mainPlayer_C orphan path.**

## 1 — Reconstructed BUA behaviour (kerfur regular_C, mainPlayer_C host)

Sources: `CXXHeaderDump/AnimBlueprint_kerfurOmega_regular.hpp` (FProperty
layout, exact offsets), `CXXHeaderDump/kerfurOmega.hpp:59` (NPC owner
class with `Anim` slot), `CXXHeaderDump/mainPlayer.hpp:255,341`
(`AnimInst @0x0C98`, `animInst_playerView @0x0E80`), IDA
`FAnimInstanceProxy_UpdateAnimationNode_FastPath @0x1414E0A70` comment:
*"native code does NOT write per-tick to any UPROPERTY at instance
offsets 0x2D60..0x2E50; all such writes are BP-VM driven (BUA bytecode
+ FastPath copies)."*

`UAnimBlueprint_kerfurOmega_regular_C::BlueprintUpdateAnimation(dt)`
(UFunction @ AnimBP class, BP bytecode — invoked once per AnimGraph
update via the BP VM) reconstructs as:

```
Pawn       = TryGetPawnOwner()                              // @0x2D70
Controller = Pawn ? Pawn.Controller : null                  // @0x2D78  (APawn::Controller @0x0258)
Character  = Cast<ACharacter>(Pawn)                         // @0x2DC0
Movement   = Pawn ? Pawn.GetMovementComponent() : null      // @0x2D80
spd        = Movement ? Movement.Velocity.Size() : 0        // @0x2E1C  (UMovementComponent::Velocity @0xC4)
animWalkAlpha / animWalkRate / footZ_R/L / floorFoot_R/L / pelvisLoc /
offsetCenter / lookAt / headLookAt / packed-bools …          // ~26 fields total
```

(The exact bytecode is not disassembled — BP opcodes for `K2Node_*`. The
shape is inferred from: the field set in the SDK dump tail, the 2026‑05‑23
diag in `memory/project_bug2_locomotion_anim.md` that observed
~26-of-26 fields populated on the SkelMeshActor satellite path, and the
empirical fact that writing `Pawn=satellite` alone re-derived Movement
naturally on the SkelMeshActor puppet.)

The BlendSpacePlayer @ +0x1180 reads `spd` via
`EvaluateGraphExposedInputs_…_AnimGraphNode_BlendSpacePlayer_F93AE2…`
(AnimBP UFunction @ class). State-machine transitions @ +0x1AC0 / +0x1CC8
read `spd` (+ likely a non-zero threshold) via their 7
`AnimGraphNode_TransitionResult` nodes.

## 2 — Why the wiring stopped working when puppet became `mainPlayer_C`

SkeletalMeshActor puppet (2026-05-23, worked): `TryGetPawnOwner()` returns
null (SkelMeshActor is not a Pawn). BUA either early-outs or does not
overwrite `Pawn`/`Movement`. Our `Pawn=satellite` + `Movement=satelliteCmc`
writes survive → `spd = satelliteCmc.Velocity.Size()` ≠ 0 → BlendSpace walks.

mainPlayer_C orphan puppet (2026-05-25 → today): `TryGetPawnOwner()`
returns the **orphan itself** (it IS an APawn/ACharacter). BUA, EVERY tick,
overwrites:

- `Pawn @0x2D70`     = orphan         (clobbers our `satellite`)
- `Controller @0x2D78` = orphan.Controller = **null** (auto-possess disabled, no PC) — clobbers our `localController_`
- `Movement @0x2D80`  = orphan.CMC    (clobbers our `satelliteCmc_`)
- `Character @0x2DC0` = orphan        (clobbers our `satellite`)
- `spd @0x2E1C`       = orphan.CMC.Velocity.Size() = **0**
  (puppet.cpp:359 disables orphan CMC tick; satellite's velocity never
  reaches this AnimInstance because Movement no longer points at it.)

Result: regardless of how many fields we pre-set, BUA runs after our
write each tick and resets the locomotion data feed to null/zero. The
Controller-cache theory (commit 8d10476) was correct in spirit (a real
gate) but the real failure is upstream: `spd=0` means no transition ever
fires regardless of the Controller value.

## 3 — Minimum fix (writes / engine state needed each tick)

Two equally root-cause paths. Pick ONE (RULE 2 — no parallel paths).

### Option A — restore the satellite pull (preferred, smallest delta)

Run our per-tick writes **AFTER** BUA, not before. BUA is invoked per
AnimGraph update on the parallel-anim worker; post-BUA hook = a POST
observer on `BlueprintUpdateAnimation` (UFunction name
`AnimBlueprint_kerfurOmega_regular_C::BlueprintUpdateAnimation`), which
re-writes the four pointers + `spd` directly:

| Field        | AnimBP offset | Value                                   |
|--------------|---------------|-----------------------------------------|
| `Pawn`       | 0x2D70        | `satellite_`                            |
| `Controller` | 0x2D78        | `localController_` (already cached)     |
| `Movement`   | 0x2D80        | `satelliteCmc_`                         |
| `Character`  | 0x2DC0        | `satellite_`                            |
| `spd`        | 0x2E1C        | `|streamed.vel|` (post-write override)  |

`Pawn/Movement/Character` writes are belt+braces; the load-bearing write
is `spd` since BUA's overwrite of `spd = orphan.CMC.Velocity.Size() = 0`
is what kills the transition. Writing `spd` directly post-BUA bypasses
the dependency chain.

Field offsets confirmed in `AnimBlueprint_kerfurOmega_regular.hpp` lines
82-105 (game-version surface: re-derive on a recook; the names are stable
so the existing reflected accessors in `reflected_offset.cpp` already
auto-adapt).

### Option B — drive the orphan's own CMC (no satellite, smaller surface)

Re-enable the orphan's CMC tick (puppet.cpp:359 currently disables it)
AND write `Movement.Velocity @+0xC4` per tick on the orphan's CMC instead
of the satellite's. BUA then re-derives `spd` correctly because
`Movement = orphan.CMC` matches reality. Risk: orphan CMC tick re-engages
gravity + walking integration; needs the gravity-disable + brake-zero
already used on the satellite, applied to the orphan CMC.

## 4 — Trap fields (writes that LOOK like fixes but aren't)

- `walkSpeed @0x2D68` — single float, NOT the BlendSpace X. Written by
  BUA from a separate path (likely `Character.GetCharacterMovement().
  MaxWalkSpeed`). Writing it does not move the BlendSpace.
- `animWalkAlpha @0x2D88 / animWalkRate @0x2D8C` — both are BUA OUTPUTS,
  consumed by the TwoWayBlend nodes (lines 117-118 of the AnimBP dump).
  The 2026-05-23 diag already disproved them as the gate (`local has
  animWalkAlpha=0.00 while walking`).
- `lookingAtPlayer @0x2E01` / `useLegIK @0x2E39` / `removeArms @0x2E3A`
  — head/IK/arm cosmetics; do not gate locomotion transitions.
- `kerfur @0x2E08` (`AkerfurOmega_C*`) — NPC owner backref for kerfur-
  specific BP paths (`State`, `speed_run`, `speed_walk`). Stays null on
  the mainPlayer host; the BP graph guards on it (`kerfur ? ... : ...`),
  so writing a fake AkerfurOmega_C would mostly select the wrong anim
  set. Leave null.
- `walkSpeedMultiplier @0x2E18` — multiplier applied AFTER `spd` is
  computed; useless if `spd` itself is 0.

## 5 — One-line root cause

`mainPlayer_C` orphan IS a Pawn ⇒ BUA's `TryGetPawnOwner()` returns the
(CMC-disabled) orphan every tick, BUA writes `spd = orphan.CMC.Velocity =
0` AFTER our Spawn() satellite writes, BlendSpace transition stays in
idle.
