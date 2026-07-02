// coop/player/client_model.h -- the remote CLIENT puppet's custom body mesh.
//
// Goal (docs/COOP_CLIENT_MODEL.md): remote CLIENT puppets render a custom
// character model (a cooked USkeletalMesh shipped in scientist.pak) while the
// HOST puppet stays Dr. Kel. The custom mesh rides the SAME
// kerfurOmegaV1_Skeleton as the stock kel skin, so the local anthro AnimBP
// drives it 1:1 -- only the SkeletalMesh swaps; the AnimClass stays the local
// one. Role gate lives at the puppet spawn site (net_pump): slot 0 == host
// (kel), slots >= 1 == clients (custom mesh).
//
// Gameplay-layer POLICY (principle 7): decides WHICH mesh a client puppet wears
// + caches the loaded asset. The actual pak load is ue_wrap::asset_load. Graceful
// degrade: nullptr when the pak is absent -> the puppet keeps the kel skin.
// Game thread only.

#pragma once

namespace coop::client_model {

// The custom client-puppet USkeletalMesh, lazily loaded on first call from the
// auto-mounted pak, then cached for the process. Returns nullptr if the
// pak/asset is missing (the caller keeps the default kel skin). One-shot: a null
// result is remembered so a missing pak is not re-probed on every puppet spawn.
void* GetClientPuppetMesh();

// The custom mesh's body texture (the 19-tile atlas cooked by
// tools/client_model/atlas.py + ue_tex.py), lazily loaded + cached like the
// mesh. Null when the pak lacks it.
void* GetClientPuppetTexture();

// Bind the custom texture onto a spawned client puppet: slot-0 MID +
// SetTextureParameterValue('tex') on BOTH body components (the two-body
// invariant -- puppet.cpp spawn notes). The slot-0 material inst_kel4_body is a
// MIC of mat_object_sk whose diffuse is the 'tex' texture param, so no cooked
// material is needed. Returns false (no-op) when the texture or components are
// unavailable -- the puppet then renders the custom mesh with the stock kel
// material (mis-mapped but harmless). Game thread only.
bool ApplyClientPuppetTexture(void* puppetActor);

}  // namespace coop::client_model
