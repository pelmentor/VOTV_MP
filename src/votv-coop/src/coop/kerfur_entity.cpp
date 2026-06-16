// coop/kerfur_entity.cpp -- see coop/kerfur_entity.h.
//
// K-3: the host-side KerfurId authority table + the client K<->eid maps + the class/eid predicates.
// No wire change; the conversion still runs the old kerfur_convert path. K-4 wires BindFormActor +
// the KerfurConvert broadcast; K-5 wires the client-mint class-gate + KerfurHoldRequest.

#include "coop/kerfur_entity.h"

#include "coop/element/registry.h"
#include "coop/net/session.h"
#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace coop::kerfur_entity {
namespace {

namespace R = ue_wrap::reflection;

std::atomic<coop::net::Session*> g_session{nullptr};
coop::net::Session* LoadSession() { return g_session.load(std::memory_order_acquire); }

// Resolved kerfur base classes (set by kerfur_convert::Install via SetKerfurClasses). Atomic so the
// class-gate predicates can read them from any thread; written once on the game thread at resolve.
std::atomic<void*> g_kerfurNpcClass{nullptr};
std::atomic<void*> g_kerfurPropClass{nullptr};

// HOST authority table. The KerfurEntity unique_ptr reserves K in the Registry (AllocHostId); ~Element
// frees K when the record is erased. The reverse maps are leaf bookkeeping into the same K.
struct KerfurRecord {
    std::unique_ptr<coop::element::KerfurEntity> elem;  // reserves K (host range) for the kerfur's life
    Form                     form        = Form::Npc;
    coop::element::ElementId currentEid  = coop::element::kInvalidId;  // the live Npc/Prop mirror eid
    void*                    actor       = nullptr;
    int32_t                  idx         = -1;
    std::string              npcClassName;
    std::string              propClassName;
};

std::mutex g_mutex;  // guards every table below (game thread in practice; locked for the worker-thread predicates)
// Host:
std::unordered_map<coop::element::ElementId, KerfurRecord> g_byKerfurId;
std::unordered_map<void*, coop::element::ElementId>        g_actorToKerfurId;
std::unordered_map<coop::element::ElementId, coop::element::ElementId> g_eidToKerfurId;  // currentEid -> K
// Client:
std::unordered_map<coop::element::ElementId, coop::element::ElementId> g_clientKToEid;   // K -> currentEid
std::unordered_map<coop::element::ElementId, coop::element::ElementId> g_clientEidToK;   // currentEid -> K
std::unordered_map<coop::element::ElementId, Form>                     g_clientForm;     // K -> form

std::string NarrowAscii(const std::wstring& w) {
    std::string s;
    s.reserve(w.size());
    for (wchar_t c : w) s.push_back(static_cast<char>(c));  // class names are ASCII
    return s;
}

}  // namespace

void SetSession(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
}

void SetKerfurClasses(void* npcClass, void* propClass) {
    if (npcClass)  g_kerfurNpcClass.store(npcClass, std::memory_order_release);
    if (propClass) g_kerfurPropClass.store(propClass, std::memory_order_release);
}

bool IsKerfurClass(void* cls) {
    if (!cls) return false;
    void* bases[2];
    size_t n = 0;
    if (void* npc  = g_kerfurNpcClass.load(std::memory_order_acquire))  bases[n++] = npc;
    if (void* prop = g_kerfurPropClass.load(std::memory_order_acquire)) bases[n++] = prop;
    if (n == 0) return false;  // classes not resolved yet -> never falsely gate
    return R::IsDescendantOfAny(cls, bases, n);
}

bool IsKerfurActor(void* actor) {
    return actor && IsKerfurClass(R::ClassOf(actor));
}

coop::element::ElementId AllocKerfurId(void* actor, coop::element::ElementId currentEid,
                                       Form form, const std::wstring& className) {
    if (!actor) return coop::element::kInvalidId;
    auto* s = LoadSession();
    if (!s || s->role() != coop::net::Role::Host) return coop::element::kInvalidId;  // host = sole authority
    std::lock_guard<std::mutex> lk(g_mutex);
    // Idempotent per actor (a re-scan / repeated registration returns the existing K).
    auto ait = g_actorToKerfurId.find(actor);
    if (ait != g_actorToKerfurId.end()) return ait->second;

    auto ent = std::make_unique<coop::element::KerfurEntity>();
    const coop::element::ElementId k = coop::element::Registry::Get().AllocHostId(ent.get());
    if (k == coop::element::kInvalidId) {
        UE_LOGW("kerfur_entity: AllocHostId exhausted -- cannot reserve KerfurId for actor %p", actor);
        return coop::element::kInvalidId;
    }
    KerfurRecord rec;
    rec.elem       = std::move(ent);
    rec.form       = form;
    rec.currentEid = currentEid;
    rec.actor      = actor;
    rec.idx        = R::InternalIndexOf(actor);
    const std::string cn = NarrowAscii(className);
    if (form == Form::Npc) rec.npcClassName = cn; else rec.propClassName = cn;

    g_actorToKerfurId[actor] = k;
    if (currentEid != coop::element::kInvalidId) g_eidToKerfurId[currentEid] = k;
    g_byKerfurId[k] = std::move(rec);
    UE_LOGI("kerfur_entity: reserved KerfurId=%u for %s kerfur actor=%p currentEid=%u class='%ls'",
            k, form == Form::Npc ? "NPC" : "prop", actor, currentEid, className.c_str());
    return k;
}

coop::element::ElementId GetKerfurIdForActor(void* actor) {
    if (!actor) return coop::element::kInvalidId;
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_actorToKerfurId.find(actor);
    return it == g_actorToKerfurId.end() ? coop::element::kInvalidId : it->second;
}

coop::element::ElementId GetKerfurIdForEid(coop::element::ElementId currentEid) {
    if (currentEid == coop::element::kInvalidId) return coop::element::kInvalidId;
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_eidToKerfurId.find(currentEid);
    if (it != g_eidToKerfurId.end()) return it->second;
    auto cit = g_clientEidToK.find(currentEid);
    return cit == g_clientEidToK.end() ? coop::element::kInvalidId : cit->second;
}

coop::element::ElementId GetCurrentEidForKerfurId(coop::element::ElementId kerfurId) {
    if (kerfurId == coop::element::kInvalidId) return coop::element::kInvalidId;
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_byKerfurId.find(kerfurId);
    if (it != g_byKerfurId.end()) return it->second.currentEid;
    auto cit = g_clientKToEid.find(kerfurId);
    return cit == g_clientKToEid.end() ? coop::element::kInvalidId : cit->second;
}

Form GetFormForKerfurId(coop::element::ElementId kerfurId) {
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_byKerfurId.find(kerfurId);
    if (it != g_byKerfurId.end()) return it->second.form;
    auto cit = g_clientForm.find(kerfurId);
    return cit == g_clientForm.end() ? Form::Npc : cit->second;
}

void RegisterClientKerfur(coop::element::ElementId kerfurId, coop::element::ElementId currentEid,
                          Form form) {
    if (kerfurId == coop::element::kInvalidId || currentEid == coop::element::kInvalidId) return;
    std::lock_guard<std::mutex> lk(g_mutex);
    // Drop a stale eid->K entry if this K moved forms (K-4 reuses K across forms).
    auto prev = g_clientKToEid.find(kerfurId);
    if (prev != g_clientKToEid.end() && prev->second != currentEid) {
        g_clientEidToK.erase(prev->second);
    }
    g_clientKToEid[kerfurId]  = currentEid;
    g_clientEidToK[currentEid] = kerfurId;
    g_clientForm[kerfurId]    = form;
    UE_LOGI("kerfur_entity[client]: tracking KerfurId=%u currentEid=%u form=%s",
            kerfurId, currentEid, form == Form::Npc ? "NPC" : "prop");
}

bool IsKerfurEid(coop::element::ElementId currentEid) {
    if (currentEid == coop::element::kInvalidId) return false;
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_eidToKerfurId.count(currentEid) != 0 || g_clientEidToK.count(currentEid) != 0;
}

void ReleaseKerfurId(coop::element::ElementId kerfurId) {
    if (kerfurId == coop::element::kInvalidId) return;
    std::unique_ptr<coop::element::KerfurEntity> drained;  // free K's Element OUTSIDE the lock
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_byKerfurId.find(kerfurId);
        if (it != g_byKerfurId.end()) {
            if (it->second.actor) g_actorToKerfurId.erase(it->second.actor);
            if (it->second.currentEid != coop::element::kInvalidId)
                g_eidToKerfurId.erase(it->second.currentEid);
            drained = std::move(it->second.elem);
            g_byKerfurId.erase(it);
        }
        // Client side (no Registry element to free).
        auto cit = g_clientKToEid.find(kerfurId);
        if (cit != g_clientKToEid.end()) { g_clientEidToK.erase(cit->second); g_clientKToEid.erase(cit); }
        g_clientForm.erase(kerfurId);
    }
    // drained's ~KerfurEntity (-> ~Element -> Registry::FreeId(K)) fires here, outside g_mutex.
}

void OnDisconnect() {
    std::unordered_map<coop::element::ElementId, KerfurRecord> drained;  // free K Elements outside the lock
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        drained.swap(g_byKerfurId);
        g_actorToKerfurId.clear();
        g_eidToKerfurId.clear();
        g_clientKToEid.clear();
        g_clientEidToK.clear();
        g_clientForm.clear();
    }
    // drained's KerfurEntity dtors (FreeId) fire here, outside g_mutex.
}

}  // namespace coop::kerfur_entity
