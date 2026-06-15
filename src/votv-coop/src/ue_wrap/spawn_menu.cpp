// ue_wrap/spawn_menu.cpp -- see header.

#include "ue_wrap/spawn_menu.h"

#include "coop/players_registry.h"
#include "ue_wrap/call.h"
#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"
#include "ue_wrap/sdk_profile.h"

#include <cstdint>

namespace ue_wrap::spawn_menu {

namespace R = ue_wrap::reflection;
namespace P = ue_wrap::profile;

namespace {

// The PRESSED-edge spawnmenu input UFunction on mainPlayer_C. Resolved+cached on
// first Open(); re-resolved if the cache is empty (a level reload recreates the
// class object, but the BP UClass name is stable so the resolved pointer remains
// valid for the session -- we still re-find defensively if it's null).
void* g_inpSpawnmenuPressedFn = nullptr;

// VOTV's two spawnmenu input events: _2 = pressed (opens), _3 = released (closes).
// We only ever want OPEN, so target _2 unconditionally (see findings doc / the
// ExecuteUbergraph_mainPlayer @12077 trace).
constexpr const wchar_t* kInpSpawnmenuPressedFn =
    L"InpActEvt_spawnmenu_K2Node_InputActionEvent_2";

bool ResolveFn() {
    if (g_inpSpawnmenuPressedFn) return true;
    void* cls = R::FindClass(P::name::MainPlayerClass);
    if (!cls) return false;
    g_inpSpawnmenuPressedFn = R::FindFunction(cls, kInpSpawnmenuPressedFn);
    return g_inpSpawnmenuPressedFn != nullptr;
}

}  // namespace

bool Open() {
    void* localPlayer = coop::players::Registry::Get().Local();
    if (!localPlayer) {
        UE_LOGW("spawn_menu::Open: no local mainPlayer_C (not in a world yet) -- "
                "spawn menu not opened");
        return false;
    }
    if (!ResolveFn()) {
        UE_LOGW("spawn_menu::Open: could not resolve mainPlayer.%ls -- spawn menu "
                "not opened", kInpSpawnmenuPressedFn);
        return false;
    }
    // Diagnostic (2026-06-15): the BP open ubergraph (@12104) NO-OPs the open if
    // `activeInterface` is already valid (another UI is up). This is the prime suspect for
    // "the dispatch logs success but no menu shows" -- so report it. Reflection-resolved
    // offset, cached; a miss just skips the diagnostic (the open still dispatches below).
    {
        static int32_t sActiveIfaceOff = -2;  // -2 = unresolved, -1 = not found, >=0 = offset
        if (sActiveIfaceOff == -2) {
            void* cls = R::FindClass(P::name::MainPlayerClass);
            sActiveIfaceOff = cls ? R::FindPropertyOffset(cls, L"activeInterface") : -1;
        }
        if (sActiveIfaceOff >= 0) {
            void* iface = *reinterpret_cast<void* const*>(
                reinterpret_cast<const uint8_t*>(localPlayer) + sActiveIfaceOff);
            if (iface) {
                UE_LOGW("spawn_menu::Open: mainPlayer.activeInterface is SET (%p) -- the BP open "
                        "guard will likely NO-OP the spawn menu (close any open UI first). THIS is "
                        "the gate, not gamemode.", iface);
            } else {
                UE_LOGI("spawn_menu::Open: activeInterface is null (open guard clear) -- dispatching");
            }
        }
    }
    // The input event takes one FKey `Key` param, but its only use is being copied
    // into the persistent frame -- the open ubergraph (@12077) never reads it for
    // gating (only IsValid(activeInterface) + isBuoyant guard the open). ParamFrame
    // allocates the correctly-sized, ZEROED frame, so the empty/default FKey we
    // pass is exactly what a non-keyboard trigger would supply. The vanilla guards
    // still apply: a no-op if another interface is open or the player is swimming.
    ue_wrap::ParamFrame frame(g_inpSpawnmenuPressedFn);
    if (!frame.valid()) {
        UE_LOGW("spawn_menu::Open: malformed UFunction frame for %ls",
                kInpSpawnmenuPressedFn);
        return false;
    }
    if (!ue_wrap::Call(localPlayer, frame)) {
        UE_LOGW("spawn_menu::Open: ProcessEvent dispatch failed");
        return false;
    }
    UE_LOGI("spawn_menu::Open: dispatched mainPlayer.%ls (spawn menu open requested)",
            kInpSpawnmenuPressedFn);
    return true;
}

}  // namespace ue_wrap::spawn_menu
