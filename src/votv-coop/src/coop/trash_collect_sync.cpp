// coop/trash_collect_sync.cpp -- see coop/trash_collect_sync.h.
//
// One function: EnsureHeldItemBroadcast. net_pump's held-prop send calls it on
// the new-held edge. The collect itself (trashBitsPile_C::playerTryToCollect)
// is BP-internal (ProcessInternal) so it can't be observed; we instead act on
// the grabbing_actor the send already resolves -- a freshly-spawned, auto-
// grabbed Aprop_C with Key=None gets a stable Key + a PropSpawn here, then the
// existing pose stream mirrors it into the collector's hands.

#include "coop/trash_collect_sync.h"

#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/prop_element_tracker.h"
#include "coop/prop_synth_key.h"
#include "ue_wrap/engine.h"
#include "ue_wrap/log.h"
#include "ue_wrap/prop.h"
#include "ue_wrap/reflection.h"
#include "ue_wrap/types.h"

#include <string>

namespace coop::trash_collect_sync {
namespace {

namespace R  = ue_wrap::reflection;
namespace PT = coop::prop_element_tracker;

}  // namespace

bool EnsureHeldItemBroadcast(void* heldActor, coop::net::Session* s) {
    if (!heldActor || !s || !s->connected()) return false;
    if (!R::IsLive(heldActor)) return false;
    // Aprop_C ONLY. The transient self-morphing chip/clump (non-Aprop_C) must
    // never be broadcast/driven here -- that is the reverted-2a use-after-free
    // ([[project-bug-trash-chippile-uaf-crash]]). They need the attach model.
    if (!ue_wrap::prop::IsDescendantOfProp(heldActor)) return false;

    // Only freshly-spawned UNKEYED items need a mint + spawn broadcast. An
    // already-keyed held actor is a normal world prop the peer already has (the
    // pose stream alone mirrors it) -- AND after our own mint the Key is
    // non-None, so this is the idempotency guard against re-broadcasting.
    std::wstring keyStr = ue_wrap::prop::GetInteractableKeyString(heldActor);
    if (!keyStr.empty() && keyStr != L"None") return false;

    const std::wstring cls = R::ClassNameOf(heldActor);
    keyStr = coop::prop_synth_key::EnsureKeyForBroadcast(heldActor, keyStr, /*mintForAprop=*/true);
    if (keyStr.empty() || keyStr == L"None") {
        UE_LOGW("trash_collect: held item %p cls='%ls' Key still None after force-mint -- cannot mirror",
                heldActor, cls.c_str());
        return false;
    }
    // Create the Prop Element shadow + dedupe latch so the item's eventual
    // K2_DestroyActor unwinds it through the normal destroy path.
    PT::MarkProcessedInit(heldActor);
    PT::MarkPropElement(heldActor, keyStr, cls);

    coop::net::PropSpawnPayload p{};
    p.className.len = 0;
    for (size_t i = 0; i < cls.size() && i < 63; ++i)
        p.className.data[p.className.len++] = static_cast<char>(cls[i]);
    p.key.len = 0;
    for (size_t i = 0; i < keyStr.size() && i < 31; ++i)
        p.key.data[p.key.len++] = static_cast<char>(keyStr[i]);

    const ue_wrap::FVector  loc = ue_wrap::engine::GetActorLocation(heldActor);
    const ue_wrap::FRotator rot = ue_wrap::engine::GetActorRotation(heldActor);
    p.locX = loc.X; p.locY = loc.Y; p.locZ = loc.Z;
    p.rotPitch = ue_wrap::NormalizeAxis(rot.Pitch);
    p.rotYaw   = ue_wrap::NormalizeAxis(rot.Yaw);
    p.rotRoll  = ue_wrap::NormalizeAxis(rot.Roll);
    p.scaleX = p.scaleY = p.scaleZ = 1.f;
    p.physFlags = coop::net::propspawn_flags::kSimulatePhysics;
    if (ue_wrap::prop::IsHeavy(heldActor))  p.physFlags |= coop::net::propspawn_flags::kIsHeavy;
    if (ue_wrap::prop::IsFrozen(heldActor)) p.physFlags |= coop::net::propspawn_flags::kFrozen;
    p.initLinVelX = p.initLinVelY = p.initLinVelZ = 0.f;
    p.initAngVelX = p.initAngVelY = p.initAngVelZ = 0.f;
    {
        const coop::element::ElementId eid = PT::GetPropElementIdForActor(heldActor);
        p.elementId = (eid == coop::element::kInvalidId) ? 0u : eid;
    }
    UE_LOGI("trash_collect: BROADCAST held unkeyed item cls='%ls' key='%ls' loc=(%.1f,%.1f,%.1f) "
            "-- held-pose stream now mirrors it into the collector's hands",
            cls.c_str(), keyStr.c_str(), p.locX, p.locY, p.locZ);
    s->SendPropSpawn(p);
    return true;
}

}  // namespace coop::trash_collect_sync
