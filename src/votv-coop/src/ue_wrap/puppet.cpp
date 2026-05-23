#include "ue_wrap/puppet.h"

#include "ue_wrap/engine.h"
#include "ue_wrap/game_thread.h"
#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"
#include "ue_wrap/sdk_profile.h"

#include <cstdint>
#include <unordered_map>

namespace ue_wrap::puppet {

namespace P = profile;
namespace R = reflection;
namespace E = engine;
namespace GT = game_thread;

namespace {

// Read/write a UObject* / POD at a fixed byte offset (raw engine memory access is
// allowed in the wrapper layer; offsets come from sdk_profile.h, verified vs the
// CXX dump).
inline void* ReadPtr(void* base, size_t off) {
    return base ? *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(base) + off) : nullptr;
}
template <class T>
inline T ReadAt(void* base, size_t off) {
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(base) + off);
}
template <class T>
inline void WriteAt(void* base, size_t off, T value) {
    *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(base) + off) = value;
}

// puppet actor -> its cached SkeletalMeshComponent (avoid a GUObjectArray walk
// per Drive() frame).
std::unordered_map<void*, void*> g_meshComp;

// Bug 2 / Plan B1 state. Game-thread-only access, no lock: BUA interceptor
// fires on the game thread (ProcessEvent), Register/Unregister/SetPuppetSpeed
// are all called from game-thread code (RemotePlayer::Spawn / Destroy / Tick).
// AnimInstance pointer -> last-streamed speed (cm/s). Presence implies "this
// AnimInstance is a puppet -> intercept BUA, skip original".
std::unordered_map<void*, float> g_puppetSpeed;

// Cached pointer to UAnimBlueprint_kerfurOmega_regular_C::BlueprintUpdateAnimation.
// Resolved lazily on first RegisterPuppetAnimInstance (the kerfur class isn't
// guaranteed loaded before gameplay enters). Stays cached for the session.
void* g_buaUFunc = nullptr;

bool BUAInterceptor(void* self, void* /*params*/) {
    auto it = g_puppetSpeed.find(self);
    if (it == g_puppetSpeed.end()) return false;  // not a puppet -> let original BUA run

    // We are running BEFORE the original BUA body. Write spd directly: the
    // original would write spd=0 (Pawn null on a puppet -> velocity branch dead),
    // we write spd=streamed_speed instead. EvaluateGraphExposedInputs fires
    // immediately after this in the same tick, reads OUR spd, feeds it to the
    // BlendSpace as the X coordinate. The AnimGraph then poses the actual walk.
    //
    // animWalkRate=1.0 kept (a default of 0 would freeze the BlendSpace's
    // sample interpolation; safer to explicitly set). animWalkAlpha
    // intentionally NOT written -- the spawn-time local dump showed
    // animWalkAlpha=0 even on a WALKING local player, so it's NOT the
    // locomotion gate (an earlier hypothesis was wrong; the diagnostic
    // disproved it). spd is the only locomotion-driving variable.
    //
    // Skipping the original is safe for the puppet: its side-effects on
    // other AnimBP variables (lookAt, footZ_R/L, floorFoot_*, pelvisLoc,
    // etc.) drive IK paths we've already disabled (useLegIK=false,
    // lookingAtPlayer=false set in SpawnPuppet). The puppet poses correctly
    // without BUA running -- only the locomotion variable was missing.
    *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(self) + P::off::AnimBP_kerfur_spd) = it->second;
    *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(self) + P::off::AnimBP_kerfur_animWalkRate) = 1.f;
    return true;  // skip original BUA -> no clobber
}

// The live AnimInstance running on a SkeletalMeshComponent (comp + AnimScriptInstance).
void* LiveAnimInstance(void* skeletalMeshComponent) {
    return ReadPtr(skeletalMeshComponent, P::off::USkeletalMesh_AnimScriptInstance);
}

}  // namespace

void* GetMeshPlayerVisibleAsset(void* mainPlayerPawn) {
    if (!mainPlayerPawn) return nullptr;
    void* comp = ReadPtr(mainPlayerPawn, P::off::AmainPlayer_mesh_playerVisible);
    if (!comp) {
        UE_LOGW("puppet: local mesh_playerVisible component null");
        return nullptr;
    }
    void* meshAsset = ReadPtr(comp, P::off::USkinnedMesh_SkeletalMesh);
    UE_LOGI("puppet: local skin = %ls (comp=%p asset=%p)",
            R::ClassNameOf(meshAsset).c_str(), comp, meshAsset);
    // Snapshot the LOCAL working body's AnimBP state so SpawnPuppet's puppet dump
    // can be diffed against it (the "diff observable state" rule): if the puppet
    // is still a stick, the diff pinpoints which variable to set.
    DumpAnimState(L"local", comp);
    return meshAsset;
}

void* GetMeshPlayerVisibleComponent(void* mainPlayerPawn) {
    if (!mainPlayerPawn) return nullptr;
    return ReadPtr(mainPlayerPawn, P::off::AmainPlayer_mesh_playerVisible);
}

float GetCapsuleHalfHeight(void* mainPlayerPawn) {
    if (!mainPlayerPawn) return 0.f;
    void* capsule = ReadPtr(mainPlayerPawn, P::off::ACharacter_CapsuleComponent);
    if (!capsule) return 0.f;
    return ReadAt<float>(capsule, P::off::UCapsuleComponent_CapsuleHalfHeight);
}

void* GetMeshPlayerVisibleAnimClass(void* mainPlayerPawn) {
    if (!mainPlayerPawn) return nullptr;
    void* comp = ReadPtr(mainPlayerPawn, P::off::AmainPlayer_mesh_playerVisible);
    if (!comp) return nullptr;
    void* animClass = ReadPtr(comp, P::off::USkeletalMesh_AnimClass);
    UE_LOGI("puppet: local AnimClass = %ls (%p)", R::ClassNameOf(animClass).c_str(), animClass);
    return animClass;
}

void* GetSkeletalMeshComponent(void* puppetActor) {
    if (!puppetActor) return nullptr;
    // If the puppet was destroyed (level change), its cached component is freed
    // too -- drop the stale entry instead of returning a dangling pointer that
    // DriveAnimBP would then read at +0x6B0 (AV).
    if (!R::IsLive(puppetActor)) { g_meshComp.erase(puppetActor); return nullptr; }
    auto it = g_meshComp.find(puppetActor);
    if (it != g_meshComp.end()) {
        // The actor can outlive its child component for a tick mid-tear-down
        // (UE finalizes sub-objects first). A live actor with a dying cached
        // component must NOT return the dying pointer -- the caller would then
        // read AnimScriptInstance @+0x6B0 on freed memory (AV). Treat as a
        // cache miss; drop the entry and fall through to re-resolve. Post-ship
        // audit catch: callers shouldn't have to repeat this guard themselves
        // (current single caller DriveAnimBP does, but a future caller would
        // not know to).
        if (R::IsLive(it->second)) return it->second;
        g_meshComp.erase(it);
    }
    void* comp = nullptr;
    for (const auto& c : R::ChildObjectsOf(puppetActor)) {
        if (c.className == P::name::SkeletalMeshComponentClass) { comp = c.object; break; }
    }
    if (comp) g_meshComp[puppetActor] = comp;
    return comp;
}

void DumpAnimState(const wchar_t* label, void* skeletalMeshComponent) {
    void* anim = LiveAnimInstance(skeletalMeshComponent);
    if (!anim) {
        UE_LOGW("puppet: [%ls] AnimInstance NULL (comp=%p) -> mesh has no live AnimBP "
                "(would render as a static/reference-pose stick)", label, skeletalMeshComponent);
        return;
    }
    const float spd = ReadAt<float>(anim, P::off::AnimBP_kerfur_spd);
    const float walkSpeed = ReadAt<float>(anim, P::off::AnimBP_kerfur_walkSpeed);
    // animWalkAlpha + animWalkRate: kept in the dump as observable AnimBP state.
    // The Plan A hypothesis that animWalkAlpha gates idle-vs-walk was DISPROVED
    // by the 2026-05-23 spawn diagnostic -- the LOCAL has animWalkAlpha=0.00
    // while WALKING. spd is the actual locomotion driver (BlendSpace X input),
    // which Plan B1's BUA interceptor pushes from the network speed.
    const float animWalkAlpha = ReadAt<float>(anim, P::off::AnimBP_kerfur_animWalkAlpha);
    const float animWalkRate = ReadAt<float>(anim, P::off::AnimBP_kerfur_animWalkRate);
    void* pawn = ReadPtr(anim, P::off::AnimBP_kerfur_Pawn);
    void* ctrl = ReadPtr(anim, P::off::AnimBP_kerfur_Controller);
    void* movement = ReadPtr(anim, P::off::AnimBP_kerfur_Movement);
    void* kerfur = ReadPtr(anim, P::off::AnimBP_kerfur_kerfur);
    const bool useLegIK = ReadAt<bool>(anim, P::off::AnimBP_kerfur_useLegIK);
    const bool isFace = ReadAt<bool>(anim, P::off::AnimBP_kerfur_isFace);
    const bool lookingAtPlayer = ReadAt<bool>(anim, P::off::AnimBP_kerfur_lookingAtPlayer);
    UE_LOGI("puppet: [%ls] AnimInstance=%ls(%p) spd=%.1f walkSpeed=%.1f "
            "animWalkAlpha=%.2f animWalkRate=%.2f "
            "Pawn=%p Controller=%p Movement=%p kerfur=%p "
            "useLegIK=%d isFace=%d lookingAtPlayer=%d",
            label, R::ClassNameOf(anim).c_str(), anim, spd, walkSpeed,
            animWalkAlpha, animWalkRate,
            pawn, ctrl, movement, kerfur, useLegIK, isFace, lookingAtPlayer);
}

void* SpawnPuppet(const FVector& loc, void* skeletalMeshAsset, void* animClass) {
    if (!skeletalMeshAsset) {
        UE_LOGE("puppet: SpawnPuppet no skin asset");
        return nullptr;
    }
    void* cls = R::FindClass(P::name::SkeletalMeshActorClass);
    if (!cls) {
        UE_LOGE("puppet: SkeletalMeshActor class not found");
        return nullptr;
    }
    // A SkeletalMeshActor is NOT a pawn -> no auto-possess, no hijack surface.
    void* actor = E::SpawnActor(cls, loc, /*inertPawn=*/false);
    if (!actor) {
        UE_LOGE("puppet: SpawnActor(SkeletalMeshActor) failed");
        return nullptr;
    }
    void* comp = GetSkeletalMeshComponent(actor);
    if (!comp) {
        UE_LOGE("puppet: spawned actor %p has no SkeletalMeshComponent", actor);
        return actor;
    }

    // Wear the local player's skin + run the body AnimBP. SetAnimClass also flips
    // AnimationMode to UseAnimBlueprint and instantiates the AnimInstance. animClass
    // is read off the LOCAL working body (lookup-by-name fails for BP-generated
    // classes), so it's guaranteed valid.
    E::SetSkeletalMesh(comp, skeletalMeshAsset);
    if (animClass) {
        E::SetAnimClass(comp, animClass);
    } else {
        UE_LOGW("puppet: no AnimClass -> body renders in reference pose (T-pose)");
    }
    E::SetAnimTickAlways(comp);          // pose even when not the rendered viewpoint
    E::SetComponentVisible(comp, true);

    // Suppress the floor-trace leg IK (it needs world/owner context a bare actor
    // lacks; left on it splays the legs). Idle by default (spd/walkSpeed = 0).
    // Also disable the head-look IK: the AnimBP defaults lookingAtPlayer=1 which
    // makes the head track "the player", but on a puppet there's no valid local
    // player to read -> the head ends up twisted to the side. The PUPPET's head
    // should follow the SOURCE PLAYER's view direction (already encoded in the
    // streamed yaw), not chase a phantom local-player target.
    if (void* anim = LiveAnimInstance(comp)) {
        WriteAt<bool>(anim, P::off::AnimBP_kerfur_useLegIK, false);
        WriteAt<bool>(anim, P::off::AnimBP_kerfur_lookingAtPlayer, false);
        WriteAt<float>(anim, P::off::AnimBP_kerfur_walkSpeedMultiplier, 1.f);
    }

    UE_LOGI("puppet: spawned actor=%p comp=%p at (%.0f,%.0f,%.0f)",
            actor, comp, loc.X, loc.Y, loc.Z);
    DumpAnimState(L"puppet", comp);
    return actor;
}

void DriveAnimBP(void* puppetActor, float speed, float headPitch, float headYawDelta) {
    void* comp = GetSkeletalMeshComponent(puppetActor);
    // The actor slot can still pass IsLive for a tick while its child component is
    // already being torn down (UE finalizes sub-objects first). Re-check the
    // component before reading AnimScriptInstance @ +0x6B0.
    if (!comp || !R::IsLive(comp)) return;
    void* anim = LiveAnimInstance(comp);
    if (!anim) return;

    // Push the streamed speed via the BUA-interceptor path (Bug 2 fix). The
    // interceptor writes `spd` at the right tick-order position (BEFORE
    // EvaluateGraphExposedInputs reads it); writing from here at end-of-frame
    // does NOT survive the next frame's BUA tick (confirmed via shipped
    // read-back diagnostic 2026-05-23). SetPuppetSpeed is the supported entry
    // point and is registered for this AnimInstance by RemotePlayer::Spawn.
    SetPuppetSpeed(anim, speed);

    // Drive the puppet head bone from the streamed view. headLookAt is NOT
    // written by BUA (BUA only touches velocity-derived vars), so end-of-frame
    // writes here survive into the next AnimGraph evaluation.
    //   Pitch: streamed view pitch -- head tilts up/down with the source.
    //   Yaw:   head yaw delta vs body. Currently 0 -- the source's body lag
    //          already encodes the head-leads-body lean; replicate it by
    //          streaming actor yaw (not camera yaw) into bodyRotation. The
    //          parameter is kept for a future "free-look" mode.
    // With lookingAtPlayer=false (set in SpawnPuppet), the AnimBP graph uses
    // headLookAt as the explicit head-look rotation rather than chasing a
    // phantom camera target.
    const FRotator headLook{headPitch, headYawDelta, 0.f};
    WriteAt<FRotator>(anim, P::off::AnimBP_kerfur_headLookAt, headLook);
}

void RegisterPuppetAnimInstance(void* animInstance) {
    if (!animInstance) return;
    // Lazy: resolve BUA UFunction + install the interceptor on the first puppet
    // ever registered. The kerfur AnimBP class isn't guaranteed loaded before
    // gameplay (we spawn the puppet AFTER mainPlayer_C exists, so the AnimBP
    // class is definitely live by then). One-shot; reuses for every subsequent
    // puppet.
    if (!g_buaUFunc) {
        void* cls = R::FindClass(P::name::AnimBPKerfurRegularClass);
        if (!cls) {
            UE_LOGE("puppet: AnimBP class '%ls' not found -- BUA interceptor NOT installed; "
                    "puppet locomotion will not animate",
                    P::name::AnimBPKerfurRegularClass);
            return;
        }
        g_buaUFunc = R::FindFunction(cls, P::name::BlueprintUpdateAnimationFn);
        if (!g_buaUFunc) {
            UE_LOGE("puppet: BlueprintUpdateAnimation UFunction not found on '%ls' -- "
                    "BUA interceptor NOT installed",
                    P::name::AnimBPKerfurRegularClass);
            return;
        }
        GT::SetInterceptor(g_buaUFunc, &BUAInterceptor);
        UE_LOGI("puppet: BUA interceptor installed (UFunc=%p) -- puppet AnimInstances "
                "now bypass BlueprintUpdateAnimation's null-Pawn clobber",
                g_buaUFunc);
    }
    g_puppetSpeed[animInstance] = 0.f;
    UE_LOGI("puppet: registered AnimInstance %p (now %zu puppet(s) intercepted)",
            animInstance, g_puppetSpeed.size());
}

void UnregisterPuppetAnimInstance(void* animInstance) {
    if (!animInstance) return;
    if (g_puppetSpeed.erase(animInstance) == 0) return;
    UE_LOGI("puppet: unregistered AnimInstance %p (%zu puppet(s) remaining)",
            animInstance, g_puppetSpeed.size());
    // Clear the interceptor when the last puppet exits -- keeps the hot
    // ProcessEvent path's pointer-compare against a null target (still cheap;
    // also avoids a stale interceptor running if the BUA UFunction itself is
    // GC'd, though FindFunction returns a UObject* that the engine pins).
    if (g_puppetSpeed.empty()) {
        GT::ClearInterceptor();
        UE_LOGI("puppet: BUA interceptor cleared (no puppets registered)");
    }
}

void SetPuppetSpeed(void* animInstance, float speed) {
    auto it = g_puppetSpeed.find(animInstance);
    if (it != g_puppetSpeed.end()) it->second = speed;
}

}  // namespace ue_wrap::puppet
