// coop/world/event_active_sync.cpp -- see coop/world/event_active_sync.h.
//
// Bytecode ground truth (verified 2026-07-04, votv-active-events-registry-RE-2026-07-04.md):
//   - lib_C::setEvent(ctx, active, ambient): active=true -> gamemode.activeEvents += 1 +
//     Array_Add(activeEvents_senders, ctx); active=false -> -= 1 + Array_RemoveItem + clamp>=0.
//     The sender registers ITSELF (its __WorldContext) -- ClassOf(sender) names the event class.
//   - lib_C::getEvent = activeEvents > 0 OR outside-base-box (the native no-save-during-event
//     gate reads this; SP does not even pause mid-event).
//   - ~95 classes call setEvent (census in the RE finding) -- creature controllers, story events,
//     pranks, ambience. A handful can be active simultaneously (refcount, not a bool).

#include "coop/world/event_active_sync.h"

#include "coop/net/session.h"

#include "ue_wrap/game_thread.h"
#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace coop::event_active_sync {
namespace {

namespace R  = ue_wrap::reflection;
namespace GT = ue_wrap::game_thread;

std::atomic<coop::net::Session*> g_session{nullptr};

// ---- resolution (game thread; lazy, 2 s retry throttle; capped LOUD latch -- the
// event_fire_sync pattern: a member lookup that fails 5x on a loaded class never succeeds) -----
void* g_gmCls = nullptr;
int32_t g_offActiveEvents = -1;    // mainGamemode.activeEvents (int refcount)
int32_t g_offSenders = -1;         // mainGamemode.activeEvents_senders (TArray<UObject*>)
std::chrono::steady_clock::time_point g_nextResolve{};
int g_postClassAttempts = 0;
bool g_resolveLatched = false;
constexpr int kMaxPostClassAttempts = 5;

void ResolvePass() {
    if (g_resolveLatched) return;
    const auto now = std::chrono::steady_clock::now();
    if (now < g_nextResolve) return;
    g_nextResolve = now + std::chrono::seconds(2);
    if (!g_gmCls) g_gmCls = R::FindClass(L"mainGamemode_C");
    if (!g_gmCls) return;  // world not loaded yet -- keep trying
    if (g_offActiveEvents < 0) g_offActiveEvents = R::FindPropertyOffset(g_gmCls, L"activeEvents");
    if (g_offSenders < 0) g_offSenders = R::FindPropertyOffset(g_gmCls, L"activeEvents_senders");
    if (g_offActiveEvents >= 0 && g_offSenders >= 0) {
        g_resolveLatched = true;
        UE_LOGI("event_active: resolved (activeEvents=0x%X activeEvents_senders=0x%X)",
                g_offActiveEvents, g_offSenders);
        return;
    }
    if (++g_postClassAttempts >= kMaxPostClassAttempts) {
        g_resolveLatched = true;
        UE_LOGW("event_active: resolution INCOMPLETE after %d passes on a loaded mainGamemode_C "
                "(activeEvents=0x%X activeEvents_senders=0x%X) -- latched OFF; game version "
                "mismatch?",
                g_postClassAttempts, g_offActiveEvents, g_offSenders);
    }
}

bool Resolved() { return g_offActiveEvents >= 0 && g_offSenders >= 0; }

// Live mainGamemode instance (cached; revalidated by internal index -- freed-memory misreads).
void* g_gm = nullptr;
int32_t g_gmIdx = -1;

void* Gamemode() {
    if (!g_gm || !R::IsLiveByIndex(g_gm, g_gmIdx)) {
        g_gm = nullptr;
        g_gmIdx = -1;
        if (!g_gmCls) return nullptr;
        for (void* obj : R::FindObjectsByClass(L"mainGamemode_C")) {
            if (obj && R::IsLive(obj) && !R::NameStartsWith(R::NameOf(obj), L"Default__")) {
                g_gm = obj;
                g_gmIdx = R::InternalIndexOf(obj);
                break;
            }
        }
    }
    return g_gm;
}

// UE4 TArray<UObject*> header (8-byte pointer elements).
struct RawPtrArray {
    void** Data;
    int32_t Num;
    int32_t Max;
};

// ---- host poll state (game thread) -----------------------------------------------------------
struct ActiveEntry {
    int32_t objIdx;         // sender's GUObjectArray internal index (recycled-slot-safe liveness)
    std::string className;  // ClassOf(sender) name, narrowed (the event's implementation class)
    long long firstSeenMs;  // steady-clock ms when the poll first saw it (elapsedSec source)
};
std::unordered_map<void*, ActiveEntry> g_active;  // sender ptr -> entry
void* g_polledGm = nullptr;                       // the instance the membership belongs to
int32_t g_polledGmIdx = -1;
bool g_primed = false;
long long g_lastPollMs = 0;
constexpr long long kPollIntervalMs = 1000;  // event phases run seconds-to-minutes; 1 Hz is generous

long long NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string Narrow(const std::wstring& w) {
    std::string s;
    s.reserve(w.size());
    for (wchar_t c : w) s.push_back((c > 0 && c < 128) ? static_cast<char>(c) : '?');
    return s;
}

int ReadRefcount(void* gm) {
    return *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(gm) + g_offActiveEvents);
}

void HostPollTick() {
    if (!Resolved()) return;  // before Gamemode(): the latched-OFF failure mode must not keep a cache warm nothing reads
    void* gm = Gamemode();
    if (!gm) return;
    // World/save reload minted a new gamemode -> the old membership's pointers dangle. Drop and
    // re-prime against the new instance (already-active events log BEGIN fresh -- correct: they
    // ARE in flight in the new world).
    if (gm != g_polledGm || !R::IsLiveByIndex(g_polledGm, g_polledGmIdx)) {
        g_active.clear();
        g_polledGm = gm;
        g_polledGmIdx = g_gmIdx;
        g_primed = false;
    }
    auto* arr = reinterpret_cast<RawPtrArray*>(reinterpret_cast<uint8_t*>(gm) + g_offSenders);
    if (arr->Num < 0 || arr->Num > 4096) return;  // sanity: ~95 registrant classes, few concurrent
    const long long now = NowMs();
    if (!g_primed) {
        g_primed = true;
        UE_LOGI("event_active: host poll primed (n=%d active)", ReadRefcount(gm));
    }
    // BEGIN edges: senders in the array we aren't tracking yet.
    for (int32_t i = 0; i < arr->Num; ++i) {
        void* obj = arr->Data ? arr->Data[i] : nullptr;
        if (!obj || g_active.count(obj)) continue;
        if (!R::IsLive(obj)) continue;  // freshly read from the engine array; defensive
        ActiveEntry e;
        e.objIdx = R::InternalIndexOf(obj);
        e.className = Narrow(R::ClassNameOf(obj));
        e.firstSeenMs = now;
        UE_LOGI("event_active: BEGIN class=%s n=%d (senders=%d)",
                e.className.c_str(), ReadRefcount(gm), arr->Num);
        g_active.emplace(obj, std::move(e));
    }
    // END edges: tracked senders gone from the array (deregistered), or dead without
    // deregistering (destroyed actor; the game's own clamp guards the same case).
    std::vector<void*> ended;
    for (auto& [obj, e] : g_active) {
        bool present = false;
        if (arr->Data)
            for (int32_t i = 0; i < arr->Num; ++i)
                if (arr->Data[i] == obj) { present = true; break; }
        const bool live = R::IsLiveByIndex(obj, e.objIdx);
        if (present && live) continue;
        UE_LOGI("event_active: END class=%s n=%d elapsed=%llds%s",
                e.className.c_str(), ReadRefcount(gm), (now - e.firstSeenMs) / 1000,
                live ? "" : " (sender died unregistered)");
        ended.push_back(obj);
    }
    for (void* obj : ended) g_active.erase(obj);
}

}  // namespace

void Install(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
}

void Tick() {
    if (!GT::IsGameThread()) return;
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected() || s->role() != coop::net::Role::Host) return;
    const long long now = NowMs();
    if (now - g_lastPollMs < kPollIntervalMs) return;
    g_lastPollMs = now;
    ResolvePass();
    HostPollTick();
}

void LogJoinSnapshotForSlot(int slot) {
    auto* s = g_session.load(std::memory_order_acquire);
    if (!s || !s->connected() || s->role() != coop::net::Role::Host) return;
    // Phase 0: log only -- the exact entries Phase 1's EventSnapshot would carry to this joiner.
    if (g_active.empty()) {
        UE_LOGI("event_active: join-edge slot=%d -- 0 in flight (no EventSnapshot needed)", slot);
        return;
    }
    const long long now = NowMs();
    for (const auto& [obj, e] : g_active) {
        UE_LOGI("event_active: join-edge slot=%d WOULD snapshot class=%s elapsed=%llds "
                "(Phase 0 log only -- no wire)",
                slot, e.className.c_str(), (now - e.firstSeenMs) / 1000);
    }
}

void OnDisconnect() {
    g_active.clear();
    g_polledGm = nullptr;
    g_polledGmIdx = -1;
    g_primed = false;
    g_gm = nullptr;
    g_gmIdx = -1;
    g_session.store(nullptr, std::memory_order_release);
}

}  // namespace coop::event_active_sync
