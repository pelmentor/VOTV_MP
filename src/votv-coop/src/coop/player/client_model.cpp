#include "coop/player/client_model.h"

#include "ue_wrap/asset_load.h"
#include "ue_wrap/engine.h"
#include "ue_wrap/log.h"
#include "ue_wrap/puppet.h"

namespace coop::client_model {

namespace {
// Package `hl_einstein_v1sc` (the model's ORIGINAL name, user 2026-07-02; was
// "scientist") at mount root /Game/Mods/VOTVCoop/ (auto-mounted from
// Content/Paks/LogicMods/votv-coop/hl_einstein_v1sc.pak). The OBJECT name is still
// `kerfurOmega_KelSkin` -- the cook spliced the model into the kel-skin template
// and did not rename the export (docs/COOP_CLIENT_MODEL.md 6a). The full
// package.object path disambiguates from the game's OWN kerfurOmega_KelSkin
// (a different package: /Game/meshes/kerfurAnthro/sk/...).
constexpr const wchar_t* kClientMeshPath =
    L"/Game/Mods/VOTVCoop/hl_einstein_v1sc.kerfurOmega_KelSkin";
// The atlas texture cooked alongside the mesh (same pak, own package).
constexpr const wchar_t* kClientTexPath =
    L"/Game/Mods/VOTVCoop/tex_hl_einstein_v1sc.tex_hl_einstein_v1sc";

void* g_mesh = nullptr;
bool  g_tried = false;
void* g_tex = nullptr;
bool  g_texTried = false;
}  // namespace

void* GetClientPuppetMesh() {
    if (g_tried) return g_mesh;  // cache incl. a null failure: don't re-probe a missing pak per spawn
    g_tried = true;
    g_mesh = ue_wrap::asset_load::LoadObjectByPath(kClientMeshPath);
    if (g_mesh)
        UE_LOGI("client_model: custom client puppet mesh ready (%p) -- client puppets wear it", g_mesh);
    else
        UE_LOGW("client_model: no custom client mesh (pak absent or load failed) -- client "
                "puppets keep the default kel skin");
    return g_mesh;
}

void* GetClientPuppetTexture() {
    if (g_texTried) return g_tex;
    g_texTried = true;
    g_tex = ue_wrap::asset_load::LoadObjectByPath(kClientTexPath);
    if (!g_tex)
        UE_LOGW("client_model: no custom client texture (pak absent or load failed) -- custom "
                "mesh renders with the stock kel material");
    return g_tex;
}

bool ApplyClientPuppetTexture(void* puppetActor) {
    namespace E = ue_wrap::engine;
    namespace Pup = ue_wrap::puppet;
    void* tex = GetClientPuppetTexture();
    if (!tex || !puppetActor) return false;
    void* comps[2] = { Pup::GetMeshPlayerVisibleComponent(puppetActor),
                       Pup::GetNativeBodyMeshComponent(puppetActor) };
    int bound = 0;
    for (void* comp : comps) {
        if (!comp) continue;
        void* mid = E::CreateDynamicMaterialInstance(comp, 0);
        if (mid && E::SetTextureParameterValue(mid, L"tex", tex)) ++bound;
    }
    UE_LOGI("client_model: custom texture %p bound on %d/2 puppet body slots (puppet=%p)",
            tex, bound, puppetActor);
    return bound == 2;
}

}  // namespace coop::client_model
