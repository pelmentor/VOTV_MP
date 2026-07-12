# Local anim drive RE — drop the satellite, write puppet CMC directly

Tracking commit (state at RE): HEAD=3abffd0. Companion finding:
[`votv-puppet-locomotion-gate-RE-2026-05-27.md`](votv-puppet-locomotion-gate-RE-2026-05-27.md).
Sources: IDA (`VotV-Win64-Shipping.exe.i64`),
`Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/` (Engine.hpp,
AnimBlueprint_kerfurOmega_regular.hpp, mainPlayer.hpp).

## 1 — Minimal data flow that drives the LOCAL body's BlendSpace

Local `mainPlayer_C` IS a possessed `ACharacter`. Per-tick chain:

1. Input → `APawn::Controller @0x0258` (a real `APlayerController`).
2. `UCharacterMovementComponent::TickComponent` runs (CMC at
   `ACharacter::CharacterMovement @0x0288`, Engine.hpp:6971). Walking
   physics integrates input, **writes**:
   - `UMovementComponent::Velocity @+0x00C4` (FVector, Engine.hpp:15427)
   - `UCharacterMovementComponent::MovementMode @+0x0168`
     (TEnumAsByte<EMovementMode>, Engine.hpp:9917 — `MOVE_Walking=1`,
     `MOVE_Falling=3`).
3. `USkeletalMeshComponent::TickComponent` runs (on `mesh_playerVisible
   @0x04F8`, mainPlayer.hpp:13). The anim worker dispatches
   `UAnimBlueprint_kerfurOmega_regular_C::BlueprintUpdateAnimation`
   (UFunction, AnimBlueprint_kerfurOmega_regular.hpp:120) via the BP VM.
4. BUA bytecode does (reconstructed from the AnimBP UPROPERTY tail
   AnimBlueprint_kerfurOmega_regular.hpp:82-113 plus the IDB note on
   `FAnimInstanceProxy_UpdateAnimationNode_FastPath` @0x1414E0A70 that
   *"native code does NOT write per-tick to any UPROPERTY at instance
   offsets 0x2D60..0x2E50; all such writes are BP-VM driven"*):

   ```
   Pawn       = TryGetPawnOwner()                    @0x2D70
   Controller = Pawn ? Pawn.Controller : null        @0x2D78
   Character  = Cast<ACharacter>(Pawn)               @0x2DC0
   Movement   = Pawn ? Pawn.GetMovementComponent()   @0x2D80
   spd        = Movement ? Movement.Velocity.Size()  @0x2E1C   ← LOAD-BEARING
   walkSpeed  = MaxWalkSpeed-derived                 @0x2D68
   useLegIK / rise / animWalkAlpha / animWalkRate / footZ_R/L /
     floorFoot_R/L / pelvisLoc / offsetCenter / lookAt / headLookAt /
     packed-bools …                                  @ remaining tail
   ```

5. The BlendSpacePlayer node @+0x1180 reads `spd` (AnimGraph FastPath
   copy or `EvaluateGraphExposedInputs_AnimGraphNode_BlendSpacePlayer_*`
   UFunction, AnimBlueprint_kerfurOmega_regular.hpp:123). The two
   `FAnimNode_StateMachine`s @+0x1AC0 / +0x1CC8 evaluate their
   `FAnimNode_TransitionResult` rules — those rules read `spd` (and
   likely `Movement` for an `IsValid()` guard).

**The only load-bearing field for locomotion is `Movement.Velocity`** (BUA
collapses it to `spd`). `Movement.MovementMode` is what BUA-derived
fields like `useLegIK`/`rise` are written from (see §2).

## 2 — IK airborne gate — exact mechanism

`useLegIK @0x2E39` and `rise @0x2E38` (AnimBlueprint_kerfurOmega_regular.hpp:108-109)
are the BP-side gates for the foot-IK TwoBoneIK nodes (the AnimBP graph
runs `TwoBoneIK_3 / TwoBoneIK_2 / TwoBoneIK_1 / TwoBoneIK` @+0x0370/0x0570/
0x0DC0/0x0FA0 with their alphas driven from `useLegIK` via a TwoWayBlend).

BUA writes both bools from
`Character.GetCharacterMovement().MovementMode == MOVE_Walking` (and
inverse for `rise`). The 2026-05-23 host-puppet diag (in the
puppet-locomotion-gate-RE finding) saw the local LOCAL set
`useLegIK=true` when grounded, `false` while jumping — matches the local
behaviour the user described (no IK stretch in air). The discriminator
is purely the CMC `MovementMode` byte at +0x168: `MOVE_Falling=3` ⇒ BUA
writes `useLegIK=false`. Source: Engine.hpp:9917 enum + the existing
shipped `kStateBitInAir` packing in `harness::ReadLocalPose` (which
reads exactly `cmc[+0x168] == MOVE_Falling`).

## 3 — Verdict on writing puppet CMC directly (CMC tick stays disabled)

**YES — works, and it is the same data-flow the LOCAL uses.** Rationale:

- The puppet IS a `mainPlayer_C` → `TryGetPawnOwner()` on the puppet's
  AnimInstance returns the puppet itself → `Pawn.GetMovementComponent()`
  returns `puppet.CharacterMovement` (= the orphan CMC at +0x288).
- BUA reads `Movement.Velocity` as a raw FProperty load at
  `CMC + 0xC4` (Engine.hpp:15427). There is no native helper in that
  read — the BP VM `K2Node_StructMemberGet` for `Velocity` decompiles to
  a flat offset load. Writing `puppet.CMC.Velocity` from the wire each
  game-thread tick survives because:
  1. CMC tick is disabled (`SetComponentTickEnabled(cmc, false)` already
     in puppet.cpp:359) → nothing else writes Velocity.
  2. BUA *reads* Velocity, it does not write it.
- BUA reads `MovementMode` similarly via `GetMovementMode` (or the
  enum-property accessor); collapses to a load at `CMC + 0x168`. Writing
  it directly drives `useLegIK`/`rise` natively → IK airborne gate
  works without our own observer override.
- Foot-IK trace and pelvis offset on a grounded puppet still pulls the
  feet down to ground correctly because they trace from the
  SkeletalMeshComponent's world transform, which we already drive via
  `SetActorLocation`.

The satellite was needed when the puppet was a `SkeletalMeshActor`
(TryGetPawnOwner=null). On the `mainPlayer_C` orphan that constraint is
gone — the orphan IS its own Pawn/Movement source.

## 4 — POST-BUA observer: what remains needed

**Nothing locomotion-related.** Drop the observer's pointer redirects
(`Pawn / Controller / Movement / Character`) AND the `spd` override AND
the `useLegIK / rise` override. BUA on the orphan, with our directly-
written `puppet.CMC.Velocity + MovementMode`, produces the same field
values BUA produces on the LOCAL.

Controller-cache: not needed. The 2026-05-27 Controller-gate hypothesis
(prior finding §3 Option A) does not survive scrutiny — the AnimBP is
class-shared with `AkerfurOmega_C` NPCs (mainPlayer.hpp + kerfurOmega.hpp)
which run on an AIController; the BP graph cannot assume
`Controller != null` without breaking the NPC case. So the gate must be
purely `spd > threshold` (plus possibly `Movement != null`, which is
satisfied because BUA writes `Movement = orphan.CMC` and orphan.CMC is
non-null).

The observer can be retired entirely. If a residual concern remains
(e.g. some BP graph elsewhere reads `lookingAtPlayer`), keep a tiny
POST observer that ONLY writes the cosmetic head/lookAt fields the
`DriveAnimBP` path currently writes — no locomotion state.

## 5 — Minimum code-change list

Drop (RULE 2 — fully, no parallel paths):

- `coop::RemotePlayer::satellite_`, `satelliteCmc_`, `localController_`,
  `puppetAnim_` (all members in remote_player.h).
- `ue_wrap::engine::SpawnSatelliteCharacter` + `GetCharacterMovementComponent`
  + `SetMovementVelocity` call sites tied to the satellite — keep
  `SetMovementVelocity` (we now call it on `puppet.CMC`).
- `RemotePlayer::Spawn`: the entire satellite block (lines 368-449 of
  remote_player.cpp).
- `RemotePlayer::ApplyToEngine`: the satellite drive block (lines 781-798).
- `RemotePlayer::Destroy`: the satellite teardown (lines 686-690).
- `coop::OnBUAPost` body's pointer redirects + spd override + IK override
  (lines 138-176). If observer is kept (cosmetic only) it shrinks to the
  `lookingAtPlayer`/`headLookAt` writes already in
  `puppet::DriveAnimBP`. Easier: retire the observer (call site
  `EnsureBUAObserverInstalled` in Spawn).
- `puppet::SpawnPuppetMainPlayer`: CMC tick stays disabled — no change
  there. The post-spawn AnimInstance writes for `removeArms /
  lookingAtPlayer / walkSpeedMultiplier` stay (cosmetic seeds).

Add (in `RemotePlayer::ApplyToEngine`, replacing the satellite block):

```cpp
// Drive puppet's own CMC directly (CMC tick stays disabled).
if (void* cmc = ReadPtr(actor_, P::off::ACharacter_CharacterMovement)) {
    const float yawRad = curYaw_ * 0.01745329252f;
    ue_wrap::FVector vel{ std::cos(yawRad) * curSpeed_,
                          std::sin(yawRad) * curSpeed_, 0.f };
    // UMovementComponent::Velocity @ +0xC4
    *reinterpret_cast<ue_wrap::FVector*>(static_cast<uint8_t*>(cmc) +
                                        0xC4) = vel;
    // UCharacterMovementComponent::MovementMode @ +0x168
    const uint8_t mm = (curStateBits_ & coop::net::kStateBitInAir)
                          ? P::off::kMOVE_Falling /* 3 */
                          : 1 /* MOVE_Walking */;
    *(static_cast<uint8_t*>(cmc) + 0x168) = mm;
}
```

Stays (unchanged):

- `puppet::SpawnPuppetMainPlayer` body (CMC tick disable, GameMode null,
  actor tick disable, PostProcess/mic destroy, mesh skin+anim apply).
- `RemotePlayer::ApplyToEngine` `SetActorLocation / SetActorRotation /
  lag_fl pitch / DriveAnimBP` for head bone.
- `harness::ReadLocalPose` packing `stateBits.bit0 = kStateBitInAir`
  from local CMC.MovementMode == MOVE_Falling.

Net: ~120 LOC dropped (satellite plumbing + observer), ~12 LOC added
(direct CMC writes). One less actor in the world per puppet. One less
UFunction hook installed per session.

## 6 — One-line verdict

**Direct writes to `puppet.CMC.Velocity@+0xC4` and `puppet.CMC.MovementMode@+0x168`
per game-thread tick reproduce the LOCAL anim drive natively — drop the
satellite + the BUA observer entirely.**
