// ue_wrap/puppet.h -- the remote-player visible body, as a "skin-puppet".
//
// Engine-wrapper layer (principle 7). A puppet is a BARE actor (ASkeletalMeshActor)
// that wears the LOCAL player's exact skin (mesh_playerVisible's USkeletalMesh) and
// runs the same body AnimBP (UAnimBlueprint_kerfurOmega_regular_C), but is owned and
// driven entirely by us -- no pawn, no controller, no CharacterMovement.
//
// Root cause this replaces: a 2nd mainPlayer_C orphan posed as a "stick" because
// its AnimBP samples idle/zero owner-movement state with no controller, and it
// dragged the protagonist class's whole single-player tick surface (hijack/gamma
// cascade) along with it. Two converging agents + the CXX dump established the
// AnimBP poses purely from its own public variables and does NOT require a
// possessing controller (the SAME AnimBP is shared by NPC classes), so the right
// shape is OUR object driven by OUR data (RULE 1 root-cause; RULE 3 parallel class
// hierarchy). The AIController-poses-the-body path is retired (RULE 2).
//
// Every function calls a UFunction or touches engine memory -> game thread only.

#pragma once

#include "ue_wrap/types.h"

namespace ue_wrap::puppet {

// Read the local player's third-person skin asset (mainPlayer_C::mesh_playerVisible
// -> USkeletalMesh*). `mainPlayerPawn` is a live mainPlayer_C. Returns the skin
// USkeletalMesh*, or nullptr if not resolvable.
void* GetMeshPlayerVisibleAsset(void* mainPlayerPawn);

// The local player's mesh_playerVisible component (the skin/anim source). Returns
// the component, or null.
void* GetMeshPlayerVisibleComponent(void* mainPlayerPawn);

// The local player's capsule half-height (ACharacter). The pawn's actor origin is
// the capsule CENTRE; subtract this to get the FEET Z (where a puppet's mesh root
// must sit so it stands on the ground instead of floating). 0 if unreadable.
float GetCapsuleHalfHeight(void* mainPlayerPawn);

// Read the AnimBP CLASS the local player's body actually runs, straight off its
// mesh_playerVisible component (AnimClass @0x6A8) -- the authoritative, always-
// present class pointer. (Looking the Blueprint-generated class up by leaf name
// fails; the live component is the source of truth.) Returns the UClass*, or null.
void* GetMeshPlayerVisibleAnimClass(void* mainPlayerPawn);

// Spawn a puppet (ASkeletalMeshActor) at `loc` wearing `skeletalMeshAsset` and
// running `animClass` (both copied from the local player's working body), set to
// always-tick its pose and visible. Returns the actor, or nullptr. Logs a
// local-vs-puppet AnimBP state diff for diagnosis.
void* SpawnPuppet(const FVector& loc, void* skeletalMeshAsset, void* animClass);

// The puppet's USkeletalMeshComponent (first SkeletalMeshComponent child),
// cached per actor. nullptr if none.
void* GetSkeletalMeshComponent(void* puppetActor);

// Drive the puppet's pose from a network snapshot value:
//   * walkSpeed/spd (locomotion BlendSpace inputs; speed in cm/s, 0 = idle).
//   * headLookAt (FRotator): the AnimBP-exposed head-bone look direction.
//     - headPitch: streamed view pitch, degrees. Wire contract is the
//       canonical FRotator axis range (-180, 180]; live values from VOTV's
//       camera-clamped source sit well inside (-89, 89).
//     - headYawDelta: head yaw RELATIVE to body (degrees). Currently always
//       streamed as 0 -- the source actor's body yaw already lags the camera
//       inside VOTV's Character, so the natural "head leads body" effect
//       reproduces on the puppet for free when we stream actor yaw. Param
//       kept for a future enhancement where the head's yaw lead is sent
//       explicitly (e.g. free-look key without turning the body).
// No-op if the live AnimInstance isn't resolved yet.
void DriveAnimBP(void* puppetActor, float speed, float headPitch, float headYawDelta);

// DIAGNOSTIC (the "diff observable state" rule): read the live AnimInstance off a
// SkeletalMeshComponent and log its key pose-driver variables. Call on the LOCAL
// working body and on the puppet to see exactly which variable differs if the
// puppet still doesn't pose. No-op-safe on null.
void DumpAnimState(const wchar_t* label, void* skeletalMeshComponent);

// Bug 2 root-cause fix (Plan B1, confirmed via shipped read-back diagnostic):
//
// The kerfur AnimBP's BlueprintUpdateAnimation runs every frame and writes
// `spd = 0` on a null-Pawn puppet (no early-out on null). Our end-of-frame
// DriveAnimBP writes get clobbered by the NEXT frame's BUA before
// EvaluateGraphExposedInputs reads spd for the BlendSpace -- net result: the
// AnimGraph always sees spd=0 -> idle pose -> "puppet slides without legs".
//
// Fix: intercept BUA via the ue_wrap::game_thread UFunction interceptor.
// For a registered puppet AnimInstance, write the network-pushed `spd` and
// SKIP the original BUA (so it can't clobber). For all other AnimInstances
// (local mainPlayer, NPC kerfurs), BUA runs unchanged. This puts our write
// at exactly the same tick-order position BUA would have written -- no race
// against EvaluateGraphExposedInputs; the AnimGraph evaluation that follows
// reads our spd directly.
//
// Generalises forward: same hook is the natural home for ragdoll state and
// any other puppet-side AnimBP variable a future feature needs to push from
// the network (see [[project-ragdoll-sync]]).

// Mark `animInstance` (a live UAnimBlueprint_kerfurOmega_regular_C*) as a
// puppet. On the first registration ever, also resolves the BUA UFunction
// pointer and installs the game_thread interceptor. Idempotent.
void RegisterPuppetAnimInstance(void* animInstance);

// Unregister `animInstance`. When the last puppet unregisters, the
// interceptor is cleared (so the hot ProcessEvent path goes back to a
// single-pointer-compare with a null target -- still cheap, but cleaner).
void UnregisterPuppetAnimInstance(void* animInstance);

// Push the network-pushed locomotion speed for a registered puppet. Called
// each Tick by RemotePlayer::ApplyToEngine. Stored game-thread-local; read by
// the BUA interceptor the next time BUA fires for this AnimInstance.
void SetPuppetSpeed(void* animInstance, float speed);

}  // namespace ue_wrap::puppet
