// coop/kerfur_reconcile.cpp -- see coop/kerfur_reconcile.h. The kerfur off->active dup RETIRE (scope A).

#include "coop/kerfur_reconcile.h"

#include "coop/element/mirror_manager.h"
#include "coop/element/prop.h"
#include "coop/kerfur_entity.h"        // IsKerfurPropClass
#include "coop/net/session.h"
#include "coop/npc_sync.h"             // GetSession (the shared client gate)
#include "coop/prop_element_tracker.h" // UnmarkKnownKeyedProp (silence the K2_DestroyActor PRE observer)
#include "ue_wrap/engine.h"            // GetActorLocation + DestroyActor
#include "ue_wrap/hot_path_guard.h"    // UE_ASSERT_GAME_THREAD
#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace coop::kerfur_reconcile {
namespace {

namespace R = ue_wrap::reflection;

// Save-time retires that MISSED at KerfurConvert-apply (their local off-prop had not async-loaded yet).
// Keyed by the broadcast K; retried at the post-quiescence sweep. Game-thread only (no mutex).
std::unordered_map<uint32_t, ue_wrap::FVector> g_pendingRetire;

constexpr float kRetireR2Cm = 1.0f;  // 1 cm^2 -- exact (the save round-trip is bit-for-bit; both peers
                                     // loaded the same save, so the local off-prop sits at the host's
                                     // captured save-time position). Position uniqueness (no two kerfurs
                                     // share a save position to <1cm) makes the match unambiguous in
                                     // practice; the ambiguous->skip guard below keeps it FAIL-SAFE if two
                                     // off-kerfurs ever sat <1cm apart (audit M3: skip both, dup persists,
                                     // never retire the wrong one) -- a coverage gap, never a wrong destroy.

bool IsClient() {
    auto* s = coop::npc_sync::GetSession();
    return s && s->role() == coop::net::Role::Client;
}

// A live LOCAL (non-mirror) off-form kerfur candidate found in one GUObjectArray walk.
struct LocalOffProp { void* actor; int32_t idx; float x, y, z; };

// Walk the GUObjectArray once for live LOCAL off-form kerfurs -- prop_kerfurOmega_C (+ skins) that are
// NOT host-driven wire mirrors. A host kerfur off-prop mirror (IsMirror()) is legit host state and must
// never be retired; only the client's own STALE save-loaded local off-prop (the dup) is a candidate.
void CollectLocalOffPropKerfurs(std::vector<LocalOffProp>& out) {
    // The authoritative "host owns this actor" set: prop MIRROR Elements (the same signal kerfur_convert
    // uses -- the per-actor reverse maps go stale on a fuzzy-adopt rekey).
    std::unordered_set<void*> mirrors;
    {
        std::vector<coop::element::Prop*> v;
        coop::element::MirrorManager<coop::element::Prop>::Instance().Snapshot(v);
        for (auto* el : v)
            if (el && el->IsMirror() && el->GetActor()) mirrors.insert(el->GetActor());
    }
    const int32_t n = R::NumObjects();
    for (int32_t i = 0; i < n; ++i) {
        void* obj = R::ObjectAt(i);
        if (!obj) continue;
        void* cls = R::ClassOf(obj);
        if (!cls || !coop::kerfur_entity::IsKerfurPropClass(cls)) continue;  // off-form kerfurs only
        if (!R::IsLive(obj)) continue;
        if (R::NameStartsWith(R::NameOf(obj), L"Default__")) continue;       // CDO
        if (mirrors.count(obj)) continue;                                    // host-driven mirror -> keep
        // RESIDUAL (audit M2, safe in scope): a save-off-prop mid-adoption (bound by kerfur_prop_adoption
        // but not yet a Mirror Element) is NOT excluded here. It cannot be wrongly retired in THIS path: a
        // retire only fires for a kerfur the host turned ON -- so the host is NOT expressing it as an
        // off-prop, so nothing is adopting THAT kerfur's off-prop concurrently, and a DIFFERENT kerfur's
        // in-flight adoption sits at a different save position -> outside the 1cm exact-key match.
        const ue_wrap::FVector loc = ue_wrap::engine::GetActorLocation(obj);
        out.push_back(LocalOffProp{obj, R::InternalIndexOf(obj), loc.X, loc.Y, loc.Z});
    }
}

// Drop the actor's local eid FIRST (silence the K2_DestroyActor PRE observer -- no stray PropDestroy on a
// superseded client eid; the same fresh-mirror invariant pile_reconcile + the adopt-bind path enforce),
// then destroy it. Game thread.
void RetireActor(void* actor) {
    coop::prop_element_tracker::UnmarkKnownKeyedProp(actor);
    ue_wrap::engine::DestroyActor(actor);
}

}  // namespace

void Reset() {
    g_pendingRetire.clear();
}

void ArmPendingRetire(uint32_t hostEid, const ue_wrap::FVector& saveTimePos) {
    UE_ASSERT_GAME_THREAD("kerfur_reconcile::ArmPendingRetire");
    const bool isNew = (g_pendingRetire.find(hostEid) == g_pendingRetire.end());
    g_pendingRetire[hostEid] = saveTimePos;  // idempotent per host eid (a re-announce refreshes the key)
    if (isNew)  // log once per kerfur (not on every connect-snapshot re-announce)
        UE_LOGI("kerfur_reconcile: ARMED off->active retire eid=%u at save-time (%.1f,%.1f,%.1f) "
                "(from npc EntitySpawn -- the channel that reaches the joiner; quiescence sweep will retire "
                "the stale local off-prop)", hostEid, saveTimePos.X, saveTimePos.Y, saveTimePos.Z);
}

int SweepReconcileSaveTimeKerfurs() {
    UE_ASSERT_GAME_THREAD("kerfur_reconcile::SweepReconcileSaveTimeKerfurs");
    if (g_pendingRetire.empty()) return 0;
    if (!IsClient()) { g_pendingRetire.clear(); return 0; }
    const size_t pendingN = g_pendingRetire.size();

    // FRESH walk of live LOCAL off-form kerfurs (the late natives are present post-quiescence). Match each
    // pending save-time key to a now-loaded local off-prop within 1 cm; claim-track so two keys never
    // claim one actor; an ambiguous (>1) or absent (0) key is skipped (never retire the wrong one).
    std::vector<LocalOffProp> cands;
    CollectLocalOffPropKerfurs(cands);
    std::vector<bool> consumed(cands.size(), false);
    std::vector<void*> toRetire;
    toRetire.reserve(pendingN);
    for (const auto& [hostEid, key] : g_pendingRetire) {
        (void)hostEid;
        int matchCount = 0, matchIdx = -1;
        for (int i = 0; i < static_cast<int>(cands.size()); ++i) {
            if (consumed[i]) continue;
            const float dx = cands[i].x - key.X, dy = cands[i].y - key.Y, dz = cands[i].z - key.Z;
            if (dx * dx + dy * dy + dz * dz > kRetireR2Cm) continue;
            if (!R::IsLiveByIndex(cands[i].actor, cands[i].idx)) continue;
            ++matchCount;
            matchIdx = i;
        }
        if (matchCount == 1) { consumed[matchIdx] = true; toRetire.push_back(cands[matchIdx].actor); }
    }

    // SAFETY VALVE (mirrors pile_reconcile + RunDivergenceSweep_'s >50% valve): retiring MORE THAN HALF
    // the live local off-prop kerfurs is not a handful of window turn-ons -- it is a racing/incomplete
    // bracket. Refuse it; the off-props stay.
    if (!cands.empty() && static_cast<int>(toRetire.size()) * 2 > static_cast<int>(cands.size())) {
        UE_LOGW("kerfur_reconcile: sweep-retire ABORTED -- %zu of %zu live local off-prop kerfur(s) "
                "(>50%%); racing/incomplete bracket, not divergence -- keeping all",
                toRetire.size(), cands.size());
        g_pendingRetire.clear();
        return 0;
    }

    for (void* actor : toRetire) RetireActor(actor);
    UE_LOGI("kerfur_reconcile: sweep-retire -- %zu of %zu pending save-time kerfur retire(s) at "
            "post-quiescence (the late-loaded stale local off-prop destroyed; the active NPC is the sole "
            "form -- join-window off->active dup fixed)",
            toRetire.size(), pendingN);
    g_pendingRetire.clear();
    return static_cast<int>(toRetire.size());
}

}  // namespace coop::kerfur_reconcile
