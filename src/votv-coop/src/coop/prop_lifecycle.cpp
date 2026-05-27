// coop/prop_lifecycle.cpp -- Aprop_C spawn/destroy/extract wire observers.
//
// Extracted from harness/harness.cpp (2026-05-25 modular refactor).
// See coop/prop_lifecycle.h for the public interface.

#include "coop/prop_lifecycle.h"

#include "coop/net/session.h"
#include "coop/remote_prop.h"
#include "ue_wrap/call.h"
#include "ue_wrap/engine.h"
#include "ue_wrap/fname_utils.h"
#include "ue_wrap/game_thread.h"
#include "ue_wrap/log.h"
#include "ue_wrap/prop.h"
#include "ue_wrap/reflection.h"
#include "ue_wrap/sdk_profile.h"
#include "ue_wrap/types.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace coop::prop_lifecycle {
namespace {

namespace P = ue_wrap::profile;
namespace R = ue_wrap::reflection;
namespace GT = ue_wrap::game_thread;

// Cached session pointer (set on Install/InstallInventory). Observers read
// role()/connected()/SendProp*() through this. nullptr until first Install.
//
// Audit C2 (2026-05-27): atomic Session* (was plain pointer). Observers fire
// from parallel-anim worker threads per game_thread.cpp's header comment; a
// plain-pointer deref races with harness calling SetSession(nullptr) on
// shutdown. item_activate.cpp + weather_sync.cpp already use atomic; this
// file had diverged. Mirrors item_activate's pattern exactly: helper LoadSession
// for the read-many sites, .store(memory_order_release) for the set sites.
std::atomic<coop::net::Session*> g_session_ptr{nullptr};

inline coop::net::Session* LoadSession() {
    return g_session_ptr.load(std::memory_order_acquire);
}

// Install idempotency.
bool g_propInitScanDone = false;
bool g_propDestroyObserverInstalled = false;
bool g_inventoryObserverInstalled = false;
std::vector<void*> g_registeredPropInitFns;

// Storage-container spawn fix (2026-05-25 RE): bracketed by takeObj PRE
// (set) and takeObj POST (clear) so the nested Aprop_C::Init POST observer
// defers its broadcast until loadData has restored the saved Key.
//
// Audit H12 (2026-05-27): std::atomic<bool> (was plain bool). The PRE/POST
// observers and the nested Init POST observer all run from parallel-anim
// worker threads per game_thread.cpp's header comment; plain-bool writes
// in the PRE + reads in the Init POST race per the C++ memory model. The
// PRE->POST sequencing within a single ProcessEvent dispatch is preserved
// by the same-thread execution order; relaxed memory order is sufficient
// (no other state depends on the bool's visibility ordering).
std::atomic<bool> g_takeObjInFlight{false};

// Init-processed dedupe set: subclass-aware Init scan registers observers
// on multiple Init UFunctions in the prop_C lineage. If a subclass Init
// calls super (BP "Parent" node), BOTH observers fire for the same actor.
// Cap at 256, clear on overflow (stale-skip is harmless; by then the
// stale actors have either been destroyed or stably broadcast).
//
// Audit Fix 5 (2026-05-27): the Init POST + K2_DestroyActor PRE observers
// can dispatch on a parallel-anim worker thread per ue_wrap/game_thread.h
// :118-120. Concurrent insert/erase/count on the unordered_set is UB
// (rehash invalidates iterators). Mutex held only for the brief set op;
// no engine calls under lock.
std::mutex g_processedInitMutex;
std::unordered_set<void*> g_processedInitActors;
constexpr size_t kProcessedInitCap = 256;

void MarkProcessedInit(void* actor) {
    if (!actor) return;
    std::lock_guard<std::mutex> lk(g_processedInitMutex);
    if (g_processedInitActors.size() >= kProcessedInitCap) g_processedInitActors.clear();
    g_processedInitActors.insert(actor);
}

bool HasProcessedInit(void* actor) {
    if (!actor) return false;
    std::lock_guard<std::mutex> lk(g_processedInitMutex);
    return g_processedInitActors.count(actor) > 0;
}

void UnmarkProcessedInit(void* actor) {
    if (!actor) return;
    std::lock_guard<std::mutex> lk(g_processedInitMutex);
    g_processedInitActors.erase(actor);
}

// Per-feature PropSpawn/PropDestroy retry queues retired 2026-05-27:
// reliable_channel.cpp now buffers internally so Send() always succeeds.
// The Enqueue*/DrainPending* helpers, the harness per-tick drain calls,
// and the OnDisconnect "dropped" counters all went with them (RULE 2).

// Forward declarations for observer callbacks.
void GrabObserver_Aprop_Init_POST(void* self, void* function, void* params);
void GrabObserver_Actor_K2DestroyActor_PRE(void* self, void* function, void* params);
void GrabObserver_PropInventory_TakeObj_PRE(void* self, void* function, void* params);
void GrabObserver_PropInventory_TakeObj_POST(void* self, void* function, void* params);

// ---- Synthesize Key for non-Aprop_C interactables -----------------------
//
// Aprop_C BP UCS auto-mints a NewGuid Key when ResetKey==true || Key==None.
// chipPile / clump / trashBitsPile BPs DO NOT (probe confirmed 2026-05-27:
// fresh-spawned clump.GetKey returns FName(NAME_None)). We bridge by calling
// the class's own `setKey(FName)` UFunction with a process-unique synthetic
// before the Init POST broadcast path checks for "None". Without this,
// chipPile/clump never broadcast at all -- the "None" guard skips them.
//
// Synthetic format: `cs_<process-low32>_<monotonic-counter>` (peer-namespace
// safe -- different peers mint different counters / process IDs; no
// collision in identity lookup).

std::atomic<uint64_t> g_synthKeyCounter{0};

// Per-class setKey UFunction cache. setKey is a BP UFunction on each
// keyed-interactable class; FindFunction is O(GUObjectArray) so cache
// after first resolve.
std::mutex g_setKeyFnMutex;
std::unordered_map<void*, void*> g_setKeyFnByClass;

void* ResolveSetKeyFn(void* cls) {
    if (!cls) return nullptr;
    std::lock_guard<std::mutex> lk(g_setKeyFnMutex);
    auto it = g_setKeyFnByClass.find(cls);
    if (it != g_setKeyFnByClass.end()) return it->second;
    void* fn = R::FindFunction(cls, P::name::PropSetKeyFn);  // "setKey"
    g_setKeyFnByClass[cls] = fn;  // cache null too (don't re-walk)
    return fn;
}

// Returns the (possibly-newly-minted) Key string. If actor's GetKey is
// already non-None, returns it unchanged. Otherwise, for non-Aprop_C
// keyed-interactables, mints a synthetic + calls setKey + returns the
// minted string. For Aprop_C-derived actors with None Key, returns
// "None" (caller skips -- BP UCS will mint on its own pass).
std::wstring EnsureKeyForBroadcast(void* self, const std::wstring& currentKey) {
    if (!currentKey.empty() && currentKey != L"None") return currentKey;
    if (ue_wrap::prop::IsDescendantOfProp(self)) {
        // Aprop_C: trust the BP's own UCS to mint. Skip.
        return currentKey;
    }
    if (!ue_wrap::prop::IsKeyedInteractable(self)) return currentKey;
    void* cls = R::ClassOf(self);
    void* setKeyFn = ResolveSetKeyFn(cls);
    if (!setKeyFn) {
        UE_LOGW("synth-key: setKey UFunction not found on '%ls' -- cannot mint",
                R::ClassNameOf(self).c_str());
        return currentKey;
    }
    const uint64_t n = g_synthKeyCounter.fetch_add(1, std::memory_order_relaxed) + 1;
    const uint32_t ptrLow32 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self));
    wchar_t buf[64];
    swprintf(buf, 64, L"cs_%08x_%llu", ptrLow32, static_cast<unsigned long long>(n));
    const R::FName keyFName = ue_wrap::fname_utils::StringToFName(buf);
    if (keyFName.ComparisonIndex == 0) {
        UE_LOGW("synth-key: StringToFName('%ls') -> NAME_None; cannot mint", buf);
        return currentKey;
    }
    ue_wrap::ParamFrame sk(setKeyFn);
    if (!sk.SetRaw(L"Key", &keyFName, sizeof(keyFName))) {
        UE_LOGW("synth-key: setKey 'Key' param not found on '%ls'",
                R::ClassNameOf(self).c_str());
        return currentKey;
    }
    if (!ue_wrap::Call(self, sk)) {
        UE_LOGW("synth-key: setKey ProcessEvent call failed on '%ls'",
                R::ClassNameOf(self).c_str());
        return currentKey;
    }
    UE_LOGI("synth-key: minted '%ls' for actor %p class='%ls'",
            buf, self, R::ClassNameOf(self).c_str());
    // Re-read via GetInteractableKey to confirm the field actually changed.
    return ue_wrap::prop::GetInteractableKeyString(self);
}

// Destroy a wire-suppressed intermediate-variant prop via K2_DestroyActor.
// When called from inside Aprop_C::Init POST, the engine is still
// executing FinishSpawningActor -- caller passes deferred=true to schedule
// via GT::Post so the calling BP graph's continuation completes first.
void DestroyLocalProp(void* actor, bool deferred) {
    if (!actor) return;
    auto doDestroy = [actor]() {
        static void* sActorCls = nullptr;
        static void* sDestroyFn = nullptr;
        if (!sActorCls || !R::IsLive(sActorCls)) {
            sActorCls = R::FindClass(P::name::ActorClassName);
            sDestroyFn = nullptr;
        }
        if (sActorCls && !sDestroyFn) {
            sDestroyFn = R::FindFunction(sActorCls, P::name::DestroyActorFn);
        }
        if (!sDestroyFn) {
            UE_LOGW("spawner-suppress: K2_DestroyActor UFunction unresolved -- cannot destroy local %p", actor);
            return;
        }
        if (!R::IsLive(actor)) {
            UE_LOGI("spawner-suppress: deferred destroy target %p no longer live (already destroyed elsewhere) -- skip",
                    actor);
            return;
        }
        // Mark BEFORE calling destroy so OUR PRE-observer skips broadcast.
        coop::remote_prop::MarkIncomingDestroy(actor);
        R::CallFunction(actor, sDestroyFn, nullptr);
    };
    if (deferred) {
        GT::Post(doDestroy);
    } else {
        doDestroy();
    }
}

// Forward declaration so the GT-thread-defer wrapper can refer to the body.
void GrabObserver_Aprop_Init_POST_Body(void* self);

void GrabObserver_Aprop_Init_POST(void* self, void* /*function*/, void* /*params*/) {
    auto* s = LoadSession();
    if (!self || !s) return;
    // Audit Fix 3 (2026-05-27): the observer body invokes UFunctions
    // (GetActorLocation/Rotation, GetKey on chipPile/clump). ProcessEvent
    // is game-thread-only per project invariant. The observer can fire on
    // a parallel-anim task-graph worker per ue_wrap/game_thread.h:118-120.
    // Defer the body to GT when off-thread; the actor pointer is captured
    // and re-validated via R::IsLive inside the deferred body.
    if (!GT::IsGameThread()) {
        GT::Post([self] { GrabObserver_Aprop_Init_POST_Body(self); });
        return;
    }
    GrabObserver_Aprop_Init_POST_Body(self);
}

void GrabObserver_Aprop_Init_POST_Body(void* self) {
    auto* s = LoadSession();
    if (!self || !s) return;
    // Gate order (audit L-1 2026-05-24): cheapest checks FIRST so the hot path
    // short-circuits with minimal work.
    if (!s->connected()) return;                      // pre-handshake save-load -> skip
    if (coop::remote_prop::ConsumeIncomingSpawn(self)) {
        UE_LOGI("grab_hook[Aprop.Init POST]: actor %p was wire-received -- skip broadcast (echo suppression)",
                self);
        return;
    }
    if (!R::IsLive(self)) return;
    if (!ue_wrap::prop::IsKeyedInteractable(self)) return;
    if (HasProcessedInit(self)) {
        UE_LOGI("grab_hook[Aprop.Init POST]: actor %p already processed -- skip (super-call dedupe)",
                self);
        return;
    }
    MarkProcessedInit(self);

    // Storage-container spawn fix (2026-05-25): defer broadcast for actors
    // spawned from inside a takeObj call. takeObj POST is the canonical
    // broadcaster (sees the saved-Key-restored actor after loadData).
    if (g_takeObjInFlight.load(std::memory_order_relaxed)) {
        UE_LOGI("grab_hook[Aprop.Init POST]: actor %p spawned inside takeObj -- defer to takeObj POST (Key not yet restored by loadData)",
                self);
        return;
    }

    // v5 Phase 5N Stream B: host-authoritative intermediate-variant
    // suppression. Client destroys local intermediate; mature variant
    // will arrive via the wire.
    const std::wstring cls = R::ClassNameOf(self);
    if (s->role() == coop::net::Role::Client) {
        if (IsWireSuppressedPropClass(cls)) {
            UE_LOGI("spawner-suppress[client.Init]: scheduling deferred destroy for local intermediate variant '%ls' actor=%p (host-authoritative; mature variant will arrive via wire)",
                    cls.c_str(), self);
            DestroyLocalProp(self, /*deferred=*/true);
            return;
        }
        // Aprop_C lineage stays host-authoritative (save-persisted; client's
        // local save-load is its OWN copy, doesn't write to host). For non-
        // Aprop_C interactables (chipPile/clump/trashBitsPile -- "transient
        // world litter" per RE, no save lineage), client interactions
        // (toClump morph spawn) MUST propagate to host so the peer sees
        // what the player just made. Fall through to broadcast in that
        // case; otherwise return.
        if (ue_wrap::prop::IsDescendantOfProp(self)) {
            return;  // Aprop_C: host-authoritative world spawn, skip client broadcast
        }
        // Non-Aprop_C interactable: fall through to broadcast.
    }

    // Host (always) + Client (for non-Aprop_C only) reach here.
    if (IsWireSuppressedPropClass(cls)) {
        UE_LOGI("spawner-suppress[host.Init]: skipping broadcast for intermediate-variant '%ls' actor=%p (host-authoritative; will broadcast mature variant on transform)",
                cls.c_str(), self);
        return;
    }

    coop::net::PropSpawnPayload p{};
    p.className.len = 0;
    for (size_t i = 0; i < cls.size() && i < 63; ++i) {
        p.className.data[p.className.len++] = static_cast<char>(cls[i]);
    }
    std::wstring keyStr = ue_wrap::prop::GetInteractableKeyString(self);
    // 2026-05-27: for non-Aprop_C interactables (chipPile/clump/trashBits)
    // whose BP doesn't auto-mint a NewGuid Key, synthesize one before the
    // None-skip guard. Probe confirmed clump.GetKey returns NAME_None on
    // fresh-spawn -- the None-skip would otherwise drop every chipPile
    // morph silently.
    keyStr = EnsureKeyForBroadcast(self, keyStr);
    // UE4 FName(NAME_None) stringifies to "None" -- treat it as unkeyed.
    // For Aprop_C this still defers (BP UCS will mint on a subsequent
    // Init pass after loadData / setKey). For non-Aprop_C we just minted
    // above; if STILL None here something went wrong with setKey.
    if (keyStr.empty() || keyStr == L"None") {
        UE_LOGI("grab_hook[Aprop.Init POST]: actor %p (class '%ls') has unset key '%ls' -- skip (unkeyed = non-syncable)",
                self, cls.c_str(), keyStr.c_str());
        return;
    }
    p.key.len = 0;
    for (size_t i = 0; i < keyStr.size() && i < 31; ++i) {
        p.key.data[p.key.len++] = static_cast<char>(keyStr[i]);
    }
    const auto loc = ue_wrap::engine::GetActorLocation(self);
    const auto rot = ue_wrap::engine::GetActorRotation(self);
    p.locX = loc.X; p.locY = loc.Y; p.locZ = loc.Z;
    p.rotPitch = ue_wrap::NormalizeAxis(rot.Pitch);
    p.rotYaw   = ue_wrap::NormalizeAxis(rot.Yaw);
    p.rotRoll  = ue_wrap::NormalizeAxis(rot.Roll);
    p.scaleX = 1.f; p.scaleY = 1.f; p.scaleZ = 1.f;
    p.physFlags = coop::net::propspawn_flags::kSimulatePhysics;
    if (ue_wrap::prop::IsHeavy(self))  p.physFlags |= coop::net::propspawn_flags::kIsHeavy;
    if (ue_wrap::prop::IsFrozen(self)) p.physFlags |= coop::net::propspawn_flags::kFrozen;
    p.initLinVelX = p.initLinVelY = p.initLinVelZ = 0.f;
    p.initAngVelX = p.initAngVelY = p.initAngVelZ = 0.f;
    UE_LOGI("grab_hook[Aprop.Init POST]: HOST broadcasting SPAWN cls='%ls' key='%ls' loc=(%.1f,%.1f,%.1f) heavy=%d frozen=%d",
            cls.c_str(), keyStr.c_str(), p.locX, p.locY, p.locZ,
            (p.physFlags & coop::net::propspawn_flags::kIsHeavy)  ? 1 : 0,
            (p.physFlags & coop::net::propspawn_flags::kFrozen)   ? 1 : 0);
    // 2026-05-27 reliable-channel rewrite: Send always succeeds (FIFO queue
    // internal to the channel). The previous EnqueuePropSpawnForRetry fallback
    // path retired as RULE 2 baggage.
    s->SendPropSpawn(p);
}

// K2_DestroyActor PRE -- bidirectional destroy broadcast (host + client),
// echo-suppressed via the remote_prop incoming-destroy set.
void GrabObserver_Actor_K2DestroyActor_PRE(void* self, void* /*function*/, void* /*params*/) {
    auto* s = LoadSession();
    if (!self || !s) return;
    // Evict from the Init-processed dedupe set BEFORE the role gate so
    // client-side Stream B destroys (deferred DestroyLocalProp on
    // mushroom7_C) also clear their entries.
    UnmarkProcessedInit(self);
    if (!s->connected()) return;
    if (coop::remote_prop::ConsumeIncomingDestroy(self)) {
        UE_LOGI("grab_hook[K2_DestroyActor PRE]: actor %p was wire-received destroy -- skip rebroadcast",
                self);
        return;
    }
    if (!ue_wrap::prop::IsKeyedInteractable(self)) return;
    const std::wstring keyStr = ue_wrap::prop::GetInteractableKeyString(self);
    // FName(NAME_None) stringifies to "None" -- symmetric with Init POST
    // (unkeyed props are non-syncable; don't broadcast a destroy for one).
    if (keyStr.empty() || keyStr == L"None") return;
    coop::net::WireKey wk{};
    wk.len = 0;
    for (size_t i = 0; i < keyStr.size() && i < 31; ++i) {
        wk.data[wk.len++] = static_cast<char>(keyStr[i]);
    }
    const char* roleStr =
        s->role() == coop::net::Role::Host ? "HOST" : "CLIENT";
    UE_LOGI("grab_hook[K2_DestroyActor PRE]: %s broadcasting DESTROY actor=%p key='%ls'",
            roleStr, self, keyStr.c_str());
    s->SendPropDestroy(wk);  // channel queues internally; always accepted
}

// PRE observer for propInventory_C::takeObj -- sets g_takeObjInFlight so
// the nested Aprop_C::Init POST observer defers its broadcast (Key is
// NewGuid pre-loadData).
void GrabObserver_PropInventory_TakeObj_PRE(void* self, void* /*function*/, void* /*params*/) {
    auto* s = LoadSession();
    if (!self || !s) return;
    if (!s->connected()) return;
    g_takeObjInFlight.store(true, std::memory_order_relaxed);
}

// POST observer for propInventory_C::takeObj -- the canonical broadcaster
// for container extracts (after loadData restored the saved Key).
void GrabObserver_PropInventory_TakeObj_POST(void* self, void* function, void* params) {
    // Clear the in-flight flag FIRST regardless of any early returns.
    g_takeObjInFlight.store(false, std::memory_order_relaxed);

    auto* s = LoadSession();
    if (!self || !params || !function || !s) return;
    // Cache the Object out-param offset. Atomic because the observer can
    // dispatch on either the game thread (typical) or a task-graph worker.
    static std::atomic<int32_t> sObjectOff{-2};
    int32_t off = sObjectOff.load(std::memory_order_acquire);
    if (off == -2) {
        const int32_t resolved = R::FindParamOffset(function, L"Object");
        sObjectOff.store(resolved >= 0 ? resolved : -1, std::memory_order_release);
        off = resolved >= 0 ? resolved : -1;
        UE_LOGI("grab_hook[takeObj POST]: resolved Object out-param offset = %d", resolved);
    }
    if (off < 0) return;
    void* spawnedActor = *reinterpret_cast<void**>(
        reinterpret_cast<uint8_t*>(params) + off);
    if (!spawnedActor || !R::IsLive(spawnedActor)) return;
    if (!s->connected()) {
        UE_LOGI("grab_hook[takeObj POST]: spawned %p but session not connected -- skipping broadcast",
                spawnedActor);
        return;
    }
    if (!ue_wrap::prop::IsKeyedInteractable(spawnedActor)) {
        UE_LOGW("grab_hook[takeObj POST]: spawned actor %p is NOT a keyed-interactable -- skipping",
                spawnedActor);
        return;
    }

    coop::net::PropSpawnPayload p{};
    const std::wstring cls = R::ClassNameOf(spawnedActor);
    p.className.len = 0;
    for (size_t i = 0; i < cls.size() && i < 63; ++i) {
        p.className.data[p.className.len++] = static_cast<char>(cls[i]);
    }
    const std::wstring keyStr = ue_wrap::prop::GetInteractableKeyString(spawnedActor);
    p.key.len = 0;
    for (size_t i = 0; i < keyStr.size() && i < 31; ++i) {
        p.key.data[p.key.len++] = static_cast<char>(keyStr[i]);
    }
    const auto loc = ue_wrap::engine::GetActorLocation(spawnedActor);
    const auto rot = ue_wrap::engine::GetActorRotation(spawnedActor);
    p.locX = loc.X; p.locY = loc.Y; p.locZ = loc.Z;
    p.rotPitch = ue_wrap::NormalizeAxis(rot.Pitch);
    p.rotYaw   = ue_wrap::NormalizeAxis(rot.Yaw);
    p.rotRoll  = ue_wrap::NormalizeAxis(rot.Roll);
    p.scaleX = 1.f; p.scaleY = 1.f; p.scaleZ = 1.f;
    p.physFlags = coop::net::propspawn_flags::kSimulatePhysics;
    if (ue_wrap::prop::IsHeavy(spawnedActor))  p.physFlags |= coop::net::propspawn_flags::kIsHeavy;
    if (ue_wrap::prop::IsFrozen(spawnedActor)) p.physFlags |= coop::net::propspawn_flags::kFrozen;
    p.initLinVelX = p.initLinVelY = p.initLinVelZ = 0.f;
    p.initAngVelX = p.initAngVelY = p.initAngVelZ = 0.f;

    UE_LOGI("grab_hook[takeObj POST]: SPAWN broadcast cls='%ls' key='%ls' loc=(%.1f, %.1f, %.1f) heavy=%d frozen=%d",
            cls.c_str(), keyStr.c_str(), p.locX, p.locY, p.locZ,
            (p.physFlags & coop::net::propspawn_flags::kIsHeavy)  ? 1 : 0,
            (p.physFlags & coop::net::propspawn_flags::kFrozen)   ? 1 : 0);
    s->SendPropSpawn(p);  // channel queues internally
}

}  // namespace

// ---- public API --------------------------------------------------------

void SetSession(coop::net::Session* session) {
    g_session_ptr.store(session, std::memory_order_release);
}

bool IsWireSuppressedPropClass(const std::wstring& cls) {
    return cls == P::name::PropMushroomGrowingClass;
}

// Enqueue*/DrainPending* functions retired 2026-05-27 -- the reliable
// channel buffers internally now (see reliable_channel.cpp). Callers just
// call Send* and always get true (unless payload-too-large / queue full
// at 4096 backlog).

void Install(coop::net::Session* session) {
    g_session_ptr.store(session, std::memory_order_release);
    // Audit Fix 1 (2026-05-27): composite atomic latch. InstallGrabObservers
    // runs at 125 Hz; until ALL inner flags resolve, every tick was calling
    // R::FindClass (a full GUObjectArray walk with std::wstring alloc per
    // entry -- the exact bomb that hit the retired non_prop_entity_sync
    // path. Same fix pattern.
    static std::atomic<bool> g_allInstalled{false};
    if (g_allInstalled.load(std::memory_order_acquire)) return;
    if (!g_propInitScanDone) {
        // Gate: wait for prop_C base class to load.
        void* propBase = R::FindClass(P::name::PropClass);
        if (propBase) {
            // One-shot GUObjectArray scan for Init UFunctions in prop_C lineage.
            const std::wstring kInitName(P::name::PropInitFn);
            const int32_t n = R::NumObjects();
            int registered = 0;
            for (int32_t i = 0; i < n; ++i) {
                void* obj = R::ObjectAt(i);
                if (!obj) continue;
                if (R::ClassNameOf(obj) != L"Function") continue;
                if (R::ToString(R::NameOf(obj)) != kInitName) continue;
                void* owningCls = R::OuterOf(obj);
                // Cover Aprop_C lineage AND the non-Aprop "prop-shaped"
                // garbage/trash bases (chipPile/clump/trashBitsPile) via the
                // 2026-05-27 IsKeyedInteractable extension. Same Init UFunction
                // protocol on all of them.
                if (!ue_wrap::prop::IsClassKeyedInteractable(owningCls)) continue;
                bool already = false;
                for (void* fn : g_registeredPropInitFns) {
                    if (fn == obj) { already = true; break; }
                }
                if (already) continue;
                if (GT::RegisterPostObserver(obj, GrabObserver_Aprop_Init_POST)) {
                    const std::wstring owner = R::ToString(R::NameOf(owningCls));
                    UE_LOGI("grab_hook: registered POST observer for %ls::Init @ %p (subclass-aware)",
                            owner.c_str(), obj);
                    g_registeredPropInitFns.push_back(obj);
                    ++registered;
                } else {
                    const std::wstring owner = R::ToString(R::NameOf(owningCls));
                    UE_LOGW("grab_hook: failed to register Init observer for %ls (observer table full)",
                            owner.c_str());
                }
            }
            UE_LOGI("grab_hook: subclass-aware Init scan: %d new registrations (total %zu Init UFunctions hooked across prop_C lineage)",
                    registered, g_registeredPropInitFns.size());
            if (!g_registeredPropInitFns.empty()) {
                g_propInitScanDone = true;
            }
        }
    }
    if (!g_propDestroyObserverInstalled) {
        if (void* actorCls = R::FindClass(P::name::ActorClassName)) {
            if (void* fn = R::FindFunction(actorCls, P::name::DestroyActorFn)) {
                if (GT::RegisterPreObserver(fn, GrabObserver_Actor_K2DestroyActor_PRE)) {
                    UE_LOGI("grab_hook: registered PRE observer for %ls.%ls @ %p (continuous destroy broadcast)",
                            P::name::ActorClassName, P::name::DestroyActorFn, fn);
                    g_propDestroyObserverInstalled = true;
                }
            } else {
                UE_LOGW("grab_hook: %ls.%ls UFunction not found -- destroy broadcast disabled",
                        P::name::ActorClassName, P::name::DestroyActorFn);
                g_propDestroyObserverInstalled = true;  // stop retry
            }
        }
    }
    // InstallInventory has its own atomic guard + early-out; not gated here.
    if (g_propInitScanDone && g_propDestroyObserverInstalled) {
        g_allInstalled.store(true, std::memory_order_release);
        UE_LOGI("prop_lifecycle: Install() complete -- subsequent calls are O(1) no-ops");
    }
}

void InstallInventory(coop::net::Session* session) {
    g_session_ptr.store(session, std::memory_order_release);
    // Audit Fix 2 (2026-05-27): atomic latch matches Install()'s. Without
    // this, the 125 Hz pump's InstallGrabObservers call runs R::FindClass
    // (a full GUObjectArray walk with std::wstring alloc per entry) on
    // every tick until propInventory_C loads. The plain-bool guard below
    // is read/written only from the GT, but it doesn't short-circuit the
    // GUObjectArray walk if propInventory_C is slow to load.
    static std::atomic<bool> s_done{false};
    if (s_done.load(std::memory_order_acquire)) return;
    if (g_inventoryObserverInstalled) {
        s_done.store(true, std::memory_order_release);
        return;
    }
    void* invCls = R::FindClass(P::name::PropInventoryClass);
    if (!invCls) return;  // not loaded yet -- retry next tick
    void* fn = R::FindFunction(invCls, P::name::PropInventoryTakeObjFn);
    if (!fn) {
        UE_LOGW("grab_hook: %ls.%ls UFunction not found -- Bug C disabled permanently this session",
                P::name::PropInventoryClass, P::name::PropInventoryTakeObjFn);
        g_inventoryObserverInstalled = true;  // stop the retry loop
        s_done.store(true, std::memory_order_release);
        return;
    }
    if (!GT::RegisterPostObserver(fn, GrabObserver_PropInventory_TakeObj_POST)) {
        UE_LOGW("grab_hook: failed to register takeObj POST observer (table full?)");
        return;
    }
    if (!GT::RegisterPreObserver(fn, GrabObserver_PropInventory_TakeObj_PRE)) {
        UE_LOGW("grab_hook: failed to register takeObj PRE observer (table full?) -- container extracts may double-broadcast with mismatched keys");
    } else {
        UE_LOGI("grab_hook: registered PRE observer for %ls.%ls (takeObj-in-flight bracket)",
                P::name::PropInventoryClass, P::name::PropInventoryTakeObjFn);
    }
    UE_LOGI("grab_hook: registered POST observer for %ls.%ls @ %p (Bug C inventory drop ready)",
            P::name::PropInventoryClass, P::name::PropInventoryTakeObjFn, fn);
    g_inventoryObserverInstalled = true;
    s_done.store(true, std::memory_order_release);
}

DisconnectStats OnDisconnect() {
    DisconnectStats s;
    {
        std::lock_guard<std::mutex> lk(g_processedInitMutex);
        s.initProcessedDropped = g_processedInitActors.size();
        g_processedInitActors.clear();
    }
    g_takeObjInFlight.store(false, std::memory_order_relaxed);
    return s;
}

}  // namespace coop::prop_lifecycle
