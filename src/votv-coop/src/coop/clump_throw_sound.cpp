// coop/clump_throw_sound.cpp -- see coop/clump_throw_sound.h.

#include "coop/clump_throw_sound.h"

#include "ue_wrap/engine.h"
#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"
#include "ue_wrap/sdk_profile.h"

namespace coop::clump_throw_sound {
namespace {

namespace R = ue_wrap::reflection;
namespace P = ue_wrap::profile;

}  // namespace

void PlayThrowWhoosh(void* clumpActor) {
    if (!clumpActor) return;

    // VOTV's default object-throw sound -- /Game/audio/effects/object_throw, the
    // same `object_*` interaction set the hand-grabbed mannequin uses (the parallel
    // `gravigun_*` set is for the gravity-gun tool only). Resolve once, retried
    // until the asset is loaded. Try SoundWave first (the asset has no `_Cue`
    // suffix, like the flashlight click); fall back to SoundCue just in case.
    static void* sSound       = nullptr;
    static void* sAttenuation = nullptr;
    if (!sSound) {
        sSound = R::FindObject(L"object_throw", P::name::SoundWaveClass);
        if (!sSound) sSound = R::FindObject(L"object_throw", L"SoundCue");
    }
    // 3D attenuation: sphere, default 20m full-volume / 200m falloff so the whoosh
    // carries across a normal gameplay distance (a throw is a "look at me" event,
    // unlike the puppet flashlight click which we deliberately shrank). The wrapper
    // AddToRoot's it so UE GC can't reap it between throws.
    if (!sAttenuation) {
        ue_wrap::engine::SoundAttenuationConfig cfg{};  // default sphere 20m/200m
        sAttenuation = ue_wrap::engine::SpawnSoundAttenuation(cfg);
    }
    if (!sSound) {
        UE_LOGW("clump throw: 'object_throw' sound asset not resolved yet -- no whoosh");
        return;
    }

    const ue_wrap::FVector loc = ue_wrap::engine::GetActorLocation(clumpActor);
    ue_wrap::engine::PlaySoundAtLocation(clumpActor, sSound, loc, sAttenuation);
    UE_LOGI("clump throw: object_throw whoosh played at (%.0f, %.0f, %.0f) attenuation=%p",
            loc.X, loc.Y, loc.Z, sAttenuation);
}

}  // namespace coop::clump_throw_sound
