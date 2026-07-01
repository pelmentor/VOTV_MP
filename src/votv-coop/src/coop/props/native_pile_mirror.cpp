// coop/props/native_pile_mirror.cpp -- see coop/props/native_pile_mirror.h.

#include "coop/props/native_pile_mirror.h"

#include "coop/element/element.h"
#include "coop/element/registry.h"  // Registry::Get().Get(eid) -> Element (SetSaveNative)
#include "coop/props/remote_prop.h"  // RegisterPropMirror

#include "ue_wrap/engine.h"
#include "ue_wrap/hot_path_guard.h"  // UE_ASSERT_GAME_THREAD
#include "ue_wrap/log.h"
#include "ue_wrap/prop.h"            // ResolvePileMesh
#include "ue_wrap/reflection.h"
#include "ue_wrap/types.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace coop::native_pile_mirror {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;

// Cached pile UClass per class name (FindClass is a ~237k-entry GUObjectArray walk; the trash family is
// a handful of distinct names). GT-serial -> no mutex.
std::unordered_map<std::wstring, void*> g_clsCache;

void* ResolvePileClass(const std::wstring& className) {
    if (className.empty()) return nullptr;
    auto it = g_clsCache.find(className);
    if (it != g_clsCache.end() && it->second) return it->second;
    void* cls = R::FindClass(className.c_str());
    if (cls) g_clsCache[className] = cls;  // do not cache a miss (could load later)
    return cls;
}

}  // namespace

void* Materialize(coop::element::ElementId eid, const std::wstring& className, uint8_t chipType,
                  const ue_wrap::FVector& loc, const ue_wrap::FVector& scale, int senderSlot,
                  bool skipBind, bool rebindInPlace) {
    UE_ASSERT_GAME_THREAD("native_pile_mirror::Materialize");
    void* cls = ResolvePileClass(className);
    if (!cls) {
        UE_LOGW("[PILE] native_pile_mirror: class '%ls' not loaded -- cannot materialize native eid=%u",
                className.c_str(), eid);
        return nullptr;
    }
    void* native = E::SpawnActor(cls, loc, /*inertPawn=*/false);
    if (!native) {
        UE_LOGW("[PILE] native_pile_mirror: SpawnActor('%ls') FAILED eid=%u", className.c_str(), eid);
        return nullptr;
    }
    // The PROVEN inert recipe (2026-06-30 collision-ON probe: 60s live + inert, hover GUI present):
    R::AddToRoot(native);                          // GC-pin -- a runtime spawn has no save/world ref
    E::SetActorTickEnabled(native, false);         // no autonomous per-frame ubergraph
    E::SetActorSimulatePhysics(native, false);     // kinematic resting pile (the host positions it)
    E::SetActorRootMovable(native);                // else SetActorLocation (host pose / b3) silently no-ops on a Static root
    // Skin the host's chipType visible mesh. The native's own UserConstructionScript already applied a
    // per-instance random mesh-component rotation (the native rotation we want) + spawned its Collision
    // component (the native collision we want) -- SetStaticMesh on the visible StaticMeshComponent swaps
    // only the mesh asset, leaving rotation + collision intact.
    if (void* comp = E::GetStaticMeshComponent(native)) {
        if (void* mesh = ue_wrap::prop::ResolvePileMesh(chipType, native)) E::SetStaticMesh(comp, mesh);
    }
    if (scale.X > 0.001f && scale.Y > 0.001f && scale.Z > 0.001f) E::SetActorScale3D(native, scale);

    if (!skipBind) {
        coop::remote_prop::RegisterPropMirror(eid, native, L"", className, senderSlot, rebindInPlace);
        if (auto* el = coop::element::Registry::Get().Get(eid)) el->SetSaveNative(true);  // mark bound-native
    }
    UE_LOGI("[PILE] native_pile_mirror: MATERIALIZED eid=%u native=%p class='%ls' chipType=%u "
            "(rooted, tick-off, kinematic, Movable, native collision) -- native hover GUI + rotation free",
            eid, native, className.c_str(), static_cast<unsigned>(chipType));
    return native;
}

}  // namespace coop::native_pile_mirror
