# Puppet leg-IK during airborne — RE 2026-05-27

Context: puppet legs stretch to ground while source is airborne. Goal: find
what gates kerfur AnimBP leg IK and what to drive on the puppet AnimInstance.

Authoritative sources used:
- `Game_0.9.0n/.../CXXHeaderDump/AnimBlueprint_kerfurOmega_regular.hpp` — full
  AnimInstance layout incl. all AnimGraphNode offsets + public vars (size 0x2E4A).
- `CXXHeaderDump/AnimGraphRuntime.hpp:531-544,629-646` — base node layouts.
- `CXXHeaderDump/Engine.hpp:6980-7003,9917,15496` — ACharacter / UCharacterMovement.
- `CXXHeaderDump/mainPlayer.hpp` — player owner (no `bIsFalling` public var; jump
  is `input_jump@0x09A1`, `recentlyJumped@0x0F03`; uses `event_landed`/`OnLanded`).
- IDA: BP-evaluator function bodies are NOT in the shipping .exe strings
  (BP `Ubergraph` lives in the cooked pak `.uexp`); reflected layout is the
  truth source. Confirmed via `find_regex` misses on `useLegIK|kerfur|isInAir`.

## 1. IK nodes in `AnimBlueprint_kerfurOmega_regular_C`

Four `FAnimNode_TwoBoneIK` instances, no `FAnimNode_LegIK` / `Fabrik` /
`ControlRig` (regular.hpp:11,13,26,27):
- `AnimGraphNode_TwoBoneIK_3` @ 0x0370
- `AnimGraphNode_TwoBoneIK_2` @ 0x0570
- `AnimGraphNode_TwoBoneIK_1` @ 0x0DC0
- `AnimGraphNode_TwoBoneIK`   @ 0x0FA0

Each is `FAnimNode_TwoBoneIK : FAnimNode_SkeletalControlBase` (AnimGraphRuntime
.hpp:629). Per-node alpha gate inherited from base @ +0x29/+0x2C:
`bAlphaBoolEnabled` (bool) and `Alpha` (float). Two are likely arm-IK (grab
pose, driven by `grabAlpha@0x2DA8`); two are foot-IK. ModifyBone nodes (7
total) handle bone offsets — `floorFoot_R@0x2DD0`/`floorFoot_L@0x2DDC`,
`footZ_R@0x2DC8`/`footZ_L@0x2DCC`, `pelvisLoc@0x2DE8`, `offsetCenter@0x2DF4`
are the BUA-written outputs that feed the IK targets.

## 2. The single gate driving foot IK alpha

`bool useLegIK @ 0x2E39` (regular.hpp:109) is the BP-exposed bool the AnimBP
authors wired into the EvaluateGraphExposedInputs hooks for the foot
TwoBoneIK nodes (each AnimGraphNode_TwoBoneIK_xxxxxxxxxx evaluator
recomputes `bAlphaBoolEnabled`/`Alpha` from BP vars before each evaluation).
The CXX dump lists the evaluator pair
`TwoBoneIK_F655A51E…` and `TwoBoneIK_01A02256…` (regular.hpp:119,122) —
these are the two foot IKs (arms-IK alphas are driven by `grabAlpha`).

`useLegIK` was already added to our reflected-offset table at
`src/votv-coop/src/ue_wrap/reflected_offset.cpp:110` and is currently FORCED
TO TRUE on puppet setup at `src/votv-coop/src/ue_wrap/puppet.cpp:497`. That
write is the proximate cause of "legs stretch to ground while airborne" —
the satellite ACharacter is grounded permanently (CMC tick disabled) so
`floorFoot_*` traces always succeed and the foot-IK pulls the visible bones
to the grounded trace hit.

There is also `bool rise @ 0x2E38` (regular.hpp:108) — present only in the
`regular` variant (not in `skerfuro` / `vendingFigura`). Without the BP graph
we can't 100% pin its semantics, but its placement adjacent to `useLegIK`
and its absence from non-locomoting variants is consistent with a "rising
from fall / push pelvis up during landing recovery" IK-tweak bool. SECOND
field to clear while airborne (defensive) — single byte, free.

## 3. State machine (jump animation candidate)

Two `FAnimNode_StateMachine` instances: `_1 @ 0x1AC0` (size 0xB0) +
`@ 0x1CC8`. Together they carry 8 `FAnimNode_TransitionResult` slots
(regular.hpp:31-36,48-49) feeding 4 `FAnimNode_StateResult` (`_4@0x1638` …
`@0x1C98`) and 2 `FAnimNode_SequencePlayer` (`_1@0x15B8`, `@0x1BC0`). Driven
inputs are the same AnimBP vars (`spd`, `walkSpeedMultiplier`, `rise`).
Without BP graph dumps the state names are opaque; mainPlayer publishes
`event_landed` + `OnLanded` + `recentlyJumped` (mainPlayer.hpp:362,674,675)
but the AnimBP itself reads only its own bools. **Conclusion:** the puppet's
state-machine has no exposed jump-state input we can drive directly;
gating IK is the right RULE-1 path.

## 4. Recommendation (minimal, RULE-1)

While the source is airborne, write on the puppet AnimInstance:
- `useLegIK = false` — offset = `reflected_offset::AnimBP_kerfur_useLegIK()`,
  type bool, value 0 (currently always true).
- `rise = false` — add a new reflected-offset accessor
  `AnimBP_kerfur_rise()` resolving field name `L"rise"`, type bool, value 0.

On landing, restore both to true. This zeroes the foot-IK alpha at the
per-evaluator EvaluateGraphExposedInputs read, no field-write race, no
SceneProxy concerns.

## 5. Wire-level addition to `PoseSnapshot`

Single bit is enough today; reserve a byte for future expansion. Add to
`PoseSnapshot` (protocol bump):

```
uint8_t  stateBits;   // bit0 isInAir, bit1 crouched(reserved), bit2 KO(reserved), bit3..7 reserved
uint8_t  _pad[3];
```

Sender reads `Movement->MovementMode == EMovementMode::MOVE_Falling` (=3) on
the LOCAL player's CMC at `+0x0168` (Engine.hpp:9917). Receiver clears
`useLegIK`/`rise` while bit0 is set, restores when clear. Bumps PoseSnapshot
from 28 to 32 bytes (still 8-aligned, PosePacket grows 48→52).
