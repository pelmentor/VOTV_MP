// coop/session/save_apply_gate.cpp -- see header.

#include "coop/session/save_apply_gate.h"

#include "coop/player/players_registry.h"  // Registry::Get().Local() -- the pawn the stamp anchors
#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"
#include "ue_wrap/ufunction_hook.h"

#include <cstdint>

namespace coop::save_apply_gate {
namespace {

namespace R = ue_wrap::reflection;

// Game-thread only (pump tick + BP Func dispatch) -> plain statics, no atomics.
//
// The hook must be latched PER GM-CLASS OBJECT, not per process (20:07 verdict: the gm
// CLASS was already loaded at the client's MENU, the hook landed on THAT object, the
// travel reloaded the class -- BP class objects are per-world-load -- and the world's
// loadObjects dispatched through a NEW UFunction our dead patch never saw: POST never
// fired, the gate stayed closed, the client never streamed, no puppet, every GrabIntent
// denied "puppet not live"). So: remember WHICH class we hooked and revalidate its
// liveness per tick (O(1) IsLiveByIndex); when the class dies at a travel, re-resolve +
// re-hook the reloaded class -- the same per-world re-install burn pattern as the
// head-fix BUA Func hook (one 16-slot table entry per actual gm reload; idempotent for
// an unchanged class).
void*   g_hookedCls     = nullptr;  // the gm class object our Func patch lives on
int32_t g_hookedClsIdx  = -1;       // its GUObjectArray slot (liveness revalidation)
bool    g_everInstalled = false;    // first-install edge (the pre-session stamp one-shot)
// True once ANY pump tick ran without the gameplay GM class RESOLVABLE (menu / pre-world).
// Discriminates the "pawn already live at first install" cases: with a pre-world tick on
// record the world came up DURING this session (menu-join; its loadObjects is coming) ->
// no install-time stamp. A first session started from INSIDE a world (browser-flow host /
// in-world joiner) never sees a pre-world tick -> its world predates the hook -> stamp.
bool    g_sawPreWorldTick = false;
void*   g_appliedAnchor = nullptr;  // gm instance (POST path) or the pawn itself (pre-session stamp)
int32_t g_anchorIdx     = -1;       // its GUObjectArray slot -- IsLiveByIndex revalidation
void*   g_appliedPawn   = nullptr;  // the local pawn the placement belongs to

void OnLoadObjectsPost(void* src, void* /*res*/) {
    // gm::loadObjects RETURNED: the save is applied -- objects spawned AND the player
    // teleported to the save's playerTransform (or legitimately left at the map spawn
    // for a zero transform). Stamp the placement for the CURRENT local pawn.
    void* pawn = coop::players::Registry::Get().Local();
    g_appliedAnchor = src;
    g_anchorIdx     = src ? R::InternalIndexOf(src) : -1;
    g_appliedPawn   = pawn;
    if (pawn)
        UE_LOGI("save_apply_gate: gm loadObjects DONE (gm=%p) -- save applied; local pawn %p "
                "is PLACED, pose stream unlocked for this world", src, pawn);
    else
        // A loadObjects with no possessed local pawn would keep the stream gated. Not an
        // expected flow (possession precedes the load tail on every observed path) -- loud
        // so a real occurrence is diagnosable from one log line.
        UE_LOGW("save_apply_gate: gm loadObjects DONE (gm=%p) but NO local pawn resolved -- "
                "pose stream stays gated until the pawn's world re-runs loadObjects", src);
}

}  // namespace

void EnsureInstalled() {
    // O(1) fast path: our patch is bound to the CURRENT (live) gm class object. Dies at a
    // travel (BP class objects are per-world-load) -> falls through to re-resolve+re-hook.
    if (g_hookedCls && R::IsLiveByIndex(g_hookedCls, g_hookedClsIdx)) return;
    // Throttle the resolve path to ~1 Hz (perf audit 2026-07-02): FindClass is a full
    // GUObjectArray walk (no name->class cache) and this path is live through the whole
    // menu-join window AND through every travel (the firefly_sync %-throttle precedent).
    // Call #1 attempts immediately; ~1 s re-hook granularity is safe -- a reloaded gm
    // class appears at map open, loadObjects fires at the load tail >=4 s later. Also
    // bounds the two failure branches below (fn-null / hook-refused) to 1 walk/s.
    static uint32_t s_throttle = 0;
    if ((s_throttle++ % 64) != 0) return;  // call #1 attempts immediately (0 % 64 == 0)
    void* gmCls = R::FindClass(L"mainGamemode_C");
    if (!gmCls) {
        g_sawPreWorldTick = true;  // ticking before any gameplay world this session
        return;                    // retry (throttled) -- the class loads with the map
    }
    void* fn = R::FindFunction(gmCls, L"loadObjects");
    if (!fn) {
        static bool s_warned = false;
        if (!s_warned) {
            s_warned = true;
            UE_LOGW("save_apply_gate: mainGamemode_C::loadObjects UFunction NOT resolved -- "
                    "pawn-placement latch unavailable (pose stream will stay gated!)");
        }
        return;
    }
    if (!ue_wrap::ufunction_hook::InstallPostHook(fn, &OnLoadObjectsPost)) {
        static bool s_failed = false;
        if (!s_failed) {
            s_failed = true;
            UE_LOGW("save_apply_gate: InstallPostHook(loadObjects) REFUSED (Func table full / "
                    "bad slot) -- pawn-placement latch unavailable");
        }
        return;
    }
    g_hookedCls    = gmCls;
    g_hookedClsIdx = R::InternalIndexOf(gmCls);
    if (g_everInstalled) {
        // A travel reloaded the gm class; the patch is re-armed on the NEW class object.
        // No stamp here -- the reloaded world's own loadObjects POST decides placement
        // (through the gap the gate stayed correctly closed: the old gm anchor died).
        UE_LOGI("save_apply_gate: gm class reloaded -> loadObjects POST latch RE-hooked "
                "(cls=%p fn=%p); awaiting this world's loadObjects", gmCls, fn);
        return;
    }
    g_everInstalled = true;
    // First install. Pre-session world: a pawn already live NOW with no pre-world tick on
    // record means the world predates the hook entirely (first session started from INSIDE
    // a world: browser-flow host / in-world joiner) -- its load is definitionally done,
    // stamp with the pawn itself as the liveness anchor. Otherwise (menu install with the
    // class preloaded, or a post-travel first tick after menu ticks) this world's
    // loadObjects POST decides.
    void* pawn = coop::players::Registry::Get().Local();
    if (pawn && !g_sawPreWorldTick) {
        g_appliedAnchor = pawn;
        g_anchorIdx     = R::InternalIndexOf(pawn);
        g_appliedPawn   = pawn;
        UE_LOGI("save_apply_gate: loadObjects POST latch installed (cls=%p fn=%p); pre-session "
                "world (no pre-world tick this session) -> local pawn %p stamped as placed",
                gmCls, fn, pawn);
    } else {
        UE_LOGI("save_apply_gate: loadObjects POST latch installed (cls=%p fn=%p); awaiting "
                "this world's loadObjects for the placement stamp (pawn=%p preWorldTick=%d)",
                gmCls, fn, pawn, g_sawPreWorldTick ? 1 : 0);
    }
}

bool IsSaveAppliedFor(void* localPawn) {
    return localPawn && localPawn == g_appliedPawn &&
           g_appliedAnchor && R::IsLiveByIndex(g_appliedAnchor, g_anchorIdx);
}

}  // namespace coop::save_apply_gate
