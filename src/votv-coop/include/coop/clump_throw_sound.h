// coop/clump_throw_sound.h -- 3D positional throw WHOOSH at a thrown trash clump.
//
// The trash CLUMP (prop_garbageClump_C) is a plain AActor with NO `thrown()`
// UFunction (unlike Aprop_C props, whose Aprop_C::thrown(Player) BP plays the
// throw whoosh -- that's what coop::remote_prop::DrivePropThrown fires for the
// mannequin). So on the receiver, when a peer's thrown clump mirror is released,
// we play VOTV's OWN object-throw sound (/Game/audio/effects/object_throw -- the
// same default-object-interaction sound the hand-grabbed mannequin uses) at the
// clump's world location via the shared ue_wrap::engine::PlaySoundAtLocation.
// User request 2026-06-03: "we already have whoosh sound when throwing mannequin,
// so just add that to clumps too."
//
// The caller (remote_prop::OnRelease, the clump null-mesh branch) gates this on
// throw speed (the same coop::net::kThrownLinVelThreshold the Aprop_C path uses)
// so a passive drop doesn't whoosh. Game thread only (dispatches a UFunction).

#pragma once

namespace coop::clump_throw_sound {

// Play the object-throw whoosh at `clumpActor`'s world location. Resolves the
// sound asset + a 3D attenuation on first use (retried until the asset loads).
// No-op if `clumpActor` is null or the asset isn't resolved yet. Game thread only.
void PlayThrowWhoosh(void* clumpActor);

}  // namespace coop::clump_throw_sound
