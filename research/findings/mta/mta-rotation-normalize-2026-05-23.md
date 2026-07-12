# MTA precedent — rotation normalization at the wire boundary (2026-05-23)

## Context

Hands-on bug from the VOTV coop mod: when the host looks even slightly **below
horizontal**, the host's puppet on the client side freezes (position, yaw,
everything). Looking back up triggers a "teleport flying" snap. Two
independent agents converged on the root cause: UE4's
`AController::GetControlRotation` returns the RAW `ControlRotation` field,
which the input system accumulates as **unnormalized `[0, 360)`** — looking
10° down reads back as `Pitch = 350.0`, not `-10.0`. Our `ValidatePose`
checked `pitch in (-90, 90)`; `350 > 90` failed → the ENTIRE pose packet was
dropped (`session.cpp` line 193) → all fields frozen. Return-to-up triggers
the `kSnapBaseCm=1000` snap path in `RemotePlayer::SetTargetPose` →
"teleport flying."

Per CLAUDE.md: before designing any coop feature fix, check MTA's precedent
AND document it. This is that document.

## What MTA does

**File:** `reference/mtasa-blue/Shared/sdk/net/SyncStructures.h:607-661`
(`SCameraRotationSync`)

MTA packs the player rotation and camera rotation into a 12-bit quantized
angle over the explicit range `(-PI, PI]` and **WRAPS** (not clamps) on the
way to the wire:

```cpp
struct SCameraRotationSync : public ISyncStructure
{
    void Read(NetBitStreamInterface& stream) override
    {
        SFloatAsBitsSync<12> plrRotation(-PI, PI, false, true);   // last `true` = bWrapInsteadOfClamp
        SFloatAsBitsSync<12> camRotation(-PI, PI, false, true);
        stream.Read(&plrRotation);
        stream.Read(&camRotation);
        data.fPlayerRotation = plrRotation.data.fValue;
        data.fCameraRotation = camRotation.data.fValue;
    }
    // ... Write() mirrors Read() with the same Sync types
};
```

The relevant pattern is the last template parameter `bWrapInsteadOfClamp=true`
in `SFloatAsBitsSync<NumBits>`. Two policies are possible at the wire
boundary:

- **Clamp**: out-of-range value is forced to the nearest in-range value.
  Loses information; gives a wrong (but valid-shaped) value to the peer.
- **Wrap**: out-of-range value modulo's into the in-range value
  (`fmod(angle, 2π)` and shift into `(-π, π]`). Lossless for cyclic
  quantities. Correct for an angle.

MTA uses Wrap for rotations. This is the pattern.

**Source-side getter** (where MTA reads what it writes to the wire):

- `Client/mods/deathmatch/logic/CClientGame.cpp:2935` (camera rotation read
  for the local player keysync) and `:3108` (player rotation). These call
  `g_pGame->GetCamera()->GetCameraRotation()` and the local ped's rotation
  accessors. The GTA SA wrappers internally hold normalized values (CCameraSA
  near `m_fCameraRotation` calls `WrapAround` when accumulating mouse deltas)
  — but MTA's netcode does NOT trust that; the wire wrap in
  `SCameraRotationSync` is a SECOND layer of normalization.

So MTA's defense in depth:

1. **Getter** normalizes in the engine wrapper (CCameraSA::WrapAround).
2. **Wire** wraps anyway (`bWrapInsteadOfClamp=true`).

The receiver decodes back into `(-π, π]` and uses it directly; the deltas
math (yaw error / shortest-arc) is in normalized space throughout.

## Application to VOTV

UE4 does NOT normalize control rotation. `AController::GetControlRotation`
returns the field as-is, which the input subsystem accumulates as `[0, 360)`.
The engine-wrapper layer (`ue_wrap::engine::GetControlRotation`) is a
straight UFunction marshal — it does not (and should not, per principle 7 —
no policy in the wrapper) normalize.

So we mirror MTA's policy at the wire-boundary layer (`coop/`), not in the
engine wrapper:

1. **`ue_wrap/types.h`**: add `NormalizeAxis(float deg) -> (-180, 180]`,
   the degree-mode equivalent of UE4's `FRotator::NormalizeAxis`. Lives in
   `ue_wrap` because it's a property of the engine type, not of coop.
2. **`harness::ReadLocalPose` (the wire boundary)**: apply `NormalizeAxis`
   to BOTH yaw and pitch before they enter the PoseSnapshot. This is the
   sender-side normalization; equivalent to MTA's wire-wrap call.
3. **`coop::net::ValidatePose`**: widen the pitch invariant from
   `(-90, 90)` to `(-180, 180]` to MATCH the FRotator axis contract.
   Still rejects out-of-range (not a crutch — the validator's job remains
   "trust-boundary rejection of garbage / hostile values"). The previous
   `(-90, 90)` bound made an incorrect domain assumption.

We don't quantize (LAN/WAN bandwidth is cheap for 24-byte poses at 60 Hz;
MTA quantized to fit GTA:SA's NetBitStream budget). The wire wrap policy is
the relevant precedent, not the bit-packing.

## Receiver-side consequence (already correct)

`RemotePlayer::SetTargetPose` computes `errorYaw_ = OffsetDegrees(curYaw_,
snap.yaw)` where `OffsetDegrees` wraps the delta into `(-180, 180]`. That
math is correct regardless of whether snap.yaw arrives normalized — but
normalized inputs make `errorYaw_` cleanly symmetric around zero, which
makes the linear LERP arcs predictable (and means a future feature that
inspects `curYaw_` directly doesn't have to re-wrap).

## Lesson

When a wire field carries an angle, **normalize at the sender boundary**
into the canonical mathematical range, and validate against that same
range on the receiver. Don't trust an engine-side accessor's domain
assumptions to match your wire contract — engine getters often return raw
unnormalized values (UE4) or normalized in a different range (GTA:SA's
radians vs UE4's degrees). The wire contract is yours; explicit it.

## Files touched (commit)

- `src/votv-coop/include/ue_wrap/types.h` — add `NormalizeAxis(float deg)`.
- `src/votv-coop/src/harness/harness.cpp` — wrap yaw + pitch in
  `ReadLocalPose` via `NormalizeAxis`.
- `src/votv-coop/include/coop/net/protocol.h` — widen `ValidatePose`'s
  yaw/pitch invariants to `(-180, 180]`, fix the wrong-domain comment.

## References

- `reference/mtasa-blue/Shared/sdk/net/SyncStructures.h:607-661`
  (`SCameraRotationSync`).
- `reference/mtasa-blue/Client/mods/deathmatch/logic/CClientGame.cpp:2935,3108`
  (sender-side rotation reads).
- UE4 source: `FRotator::NormalizeAxis` in `Runtime/Core/Public/Math/RotatorVectorRegister.h`
  (degree-mode wrap into `(-180, 180]`) — our `ue_wrap::NormalizeAxis` is
  the same formula.
