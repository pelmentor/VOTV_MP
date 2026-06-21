// coop/trash_proxy.cpp -- see coop/trash_proxy.h.

#include "coop/trash_proxy.h"

#include "coop/element/element_deleter.h"
#include "coop/element/mirror_manager.h"
#include "coop/element/prop.h"
#include "coop/remote_prop.h"       // ClearAnyDriveFor (evict the pose drive before destroying a proxy)
#include "ue_wrap/engine.h"
#include "ue_wrap/hot_path_guard.h"  // UE_ASSERT_GAME_THREAD
#include "ue_wrap/log.h"
#include "ue_wrap/prop.h"            // ResolvePileMesh
#include "ue_wrap/reflection.h"
#include "ue_wrap/types.h"          // FVector / FRotator

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace coop::trash_proxy {
namespace {

namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;

struct ProxyEntry {
    void* actor = nullptr;
};

// eid -> proxy. GAME-THREAD only (every entry point runs on the net-pump game
// thread), so a plain map with no mutex is correct.
std::unordered_map<coop::element::ElementId, ProxyEntry> g_proxies;

void* g_smaClass  = nullptr;  // AStaticMeshActor UClass (the proxy class)
void* g_chipBase  = nullptr;  // actorChipPile_C     (IsTrashProxyClass base)
void* g_clumpBase = nullptr;  // prop_garbageClump_C (IsTrashProxyClass base)

// The fixed dirtball clump mesh (cached). May be null if no clump asset is
// resident yet on this client -- the caller then falls back to the pile mesh so a
// proxy is never invisible.
void* ResolveClumpMesh() {
    static void* sDirtball = nullptr;
    if (!sDirtball || !R::IsLive(sDirtball)) sDirtball = R::FindObject(L"dirtball", L"StaticMesh");
    return sDirtball;
}

// Skin the proxy's StaticMeshComponent for the requested form. SetStaticMesh
// recomputes the component's bounds + collision body in the SAME call (atomic --
// no window where the visual is the new form but the bound is the old). Clump ->
// dirtball (falls back to the pile mesh if dirtball isn't resident); pile -> the
// chipType pile mesh (ResolvePileMesh itself last-good-caches).
void SkinProxy(void* actor, uint8_t chipType, bool isClump) {
    void* comp = E::GetStaticMeshComponent(actor);
    if (!comp) return;
    void* mesh = isClump ? ResolveClumpMesh() : nullptr;
    if (!mesh) mesh = ue_wrap::prop::ResolvePileMesh(chipType, actor);  // pile, or clump-fallback
    if (mesh) E::SetStaticMesh(comp, mesh);
}

}  // namespace

bool IsTrashProxyClass(const std::wstring& className) {
    if (className.empty()) return false;
    if (!g_chipBase)  g_chipBase  = R::FindClass(L"actorChipPile_C");
    if (!g_clumpBase) g_clumpBase = R::FindClass(L"prop_garbageClump_C");
    void* cls = R::FindClass(className.c_str());
    if (!cls) return false;
    void* const bases[2] = { g_chipBase, g_clumpBase };
    return R::IsDescendantOfAny(cls, bases, 2);
}

bool IsClumpClass(const std::wstring& className) {
    if (className.empty()) return false;
    if (!g_clumpBase) g_clumpBase = R::FindClass(L"prop_garbageClump_C");
    void* cls = R::FindClass(className.c_str());
    if (!cls) return false;
    void* const bases[1] = { g_clumpBase };
    return R::IsDescendantOfAny(cls, bases, 1);
}

void* SpawnProxy(coop::element::ElementId eid, uint8_t chipType, bool isClump,
                 const ue_wrap::FVector& loc, const ue_wrap::FRotator& rot) {
    UE_ASSERT_GAME_THREAD("trash_proxy::SpawnProxy");
    if (!g_smaClass) g_smaClass = R::FindClass(L"StaticMeshActor");
    if (!g_smaClass) {
        UE_LOGW("trash_proxy: StaticMeshActor class unresolved -- cannot spawn proxy eid=%u", eid);
        return nullptr;
    }
    // Re-spawn convergence (the OWNER re-ingesting at a re-bracket, or a duplicate
    // spawn packet): re-skin the existing live proxy instead of leaking a second.
    if (auto it = g_proxies.find(eid);
        it != g_proxies.end() && it->second.actor && R::IsLive(it->second.actor)) {
        SkinProxy(it->second.actor, chipType, isClump);
        return it->second.actor;
    }
    void* actor = E::SpawnActor(g_smaClass, loc, /*inertPawn=*/false);
    if (!actor) {
        UE_LOGW("trash_proxy: SpawnActor(StaticMeshActor) failed eid=%u", eid);
        return nullptr;
    }
    E::SetActorRotation(actor, rot);
    R::AddToRoot(actor);                                        // never GC'd -> never stale -> no dup
    E::SetActorRootCollisionEnabled(actor, /*ECollisionEnabled::NoCollision=*/0);  // phase 1 follower
    SkinProxy(actor, chipType, isClump);
    g_proxies[eid] = ProxyEntry{ actor };
    UE_LOGI("[PILE] trash_proxy: SPAWN eid=%u %s chipType=%u actor=%p "
            "(AStaticMeshActor, rooted, NoCollision)",
            eid, isClump ? "clump" : "pile", static_cast<unsigned>(chipType), actor);
    return actor;
}

void* ReskinProxy(coop::element::ElementId eid, uint8_t chipType, bool isClump) {
    UE_ASSERT_GAME_THREAD("trash_proxy::ReskinProxy");
    auto it = g_proxies.find(eid);
    if (it == g_proxies.end()) return nullptr;
    void* actor = it->second.actor;
    if (!actor || !R::IsLive(actor)) return nullptr;  // a rooted proxy is never stale -- defensive
    SkinProxy(actor, chipType, isClump);              // in place -> binding untouched -> no dup
    return actor;
}

void RetireProxy(coop::element::ElementId eid) {
    UE_ASSERT_GAME_THREAD("trash_proxy::RetireProxy");
    auto it = g_proxies.find(eid);
    if (it == g_proxies.end()) return;
    void* actor = it->second.actor;
    g_proxies.erase(it);
    // Evict any pose-drive entry FIRST so neither remote_prop::Tick nor ForceRelease
    // touches the actor we are about to destroy (ForceRelease runs early in
    // DisconnectAll; a stale g_drives entry to a freed actor would UAF / leak).
    coop::remote_prop::ClearAnyDriveFor(actor);
    // Destroy -> RemoveFromRoot -> unbind. GC runs on the game thread and cannot
    // interleave this synchronous sequence, so the "un-rooted, not-yet-destroyed"
    // window is moot; this is the conventional/defensive order (destroy marks
    // PendingKill, then un-root makes the memory GC-reapable -- a rooted PendingKill
    // actor would never be reaped = leak).
    if (actor && R::IsLive(actor)) {
        E::DestroyActor(actor);
        R::RemoveFromRoot(actor);
    }
    // Unbind the Prop mirror (deferred dtor outside the manager mutex -- the
    // documented teardown pattern; ~Prop -> ~Element -> Registry::UnregisterMirror).
    coop::element::ElementDeleter::Get().Enqueue(
        coop::element::MirrorManager<coop::element::Prop>::Instance().Take(eid));
    UE_LOGI("[PILE] trash_proxy: RETIRE eid=%u actor=%p (destroyed, un-rooted, unbound)", eid, actor);
}

bool IsProxy(coop::element::ElementId eid) {
    return g_proxies.find(eid) != g_proxies.end();
}

void OnDisconnect() {
    UE_ASSERT_GAME_THREAD("trash_proxy::OnDisconnect");
    if (g_proxies.empty()) return;
    const size_t n = g_proxies.size();
    // Snapshot eids first -- RetireProxy mutates the map.
    std::vector<coop::element::ElementId> eids;
    eids.reserve(n);
    for (const auto& kv : g_proxies) eids.push_back(kv.first);
    for (auto eid : eids) RetireProxy(eid);
    UE_LOGI("[PILE] trash_proxy: OnDisconnect retired %zu proxy mirror(s)", n);
}

}  // namespace coop::trash_proxy
