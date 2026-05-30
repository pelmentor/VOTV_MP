#include "coop/dev/restore_vitals.h"

#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/shutdown.h"
#include "coop/ini_config.h"
#include "ue_wrap/game_thread.h"
#include "ue_wrap/log.h"
#include "ue_wrap/vitals.h"

#include <windows.h>

#include <atomic>

namespace coop::dev::restore_vitals {

namespace GT = ue_wrap::game_thread;
namespace V = ue_wrap::vitals;

namespace {

// Cached Session pointer for the broadcast. nullptr-safe at every callsite
// (a press before Session start is logged + no-op'd, NOT crashed).
std::atomic<coop::net::Session*> g_session{nullptr};

// VOTV's vitals top out at 100.0 (% units). Writing >100 doesn't visually
// over-fill the meter -- the BP graph clamps display at 100. Picking the
// exact UI cap rather than something larger so subsequent food consumption
// behaves identically to a player who reached max naturally (no hidden
// over-stored value that would gate hunger draining differently).
// 2026-05-25 NIGHT (user retest +1): writing coffeePower=100 produced a
// screen-shaking post-coffee vanilla effect (the BP keys an effect off
// coffeePower > 0). F3 only tops up food/sleep/health, leaving coffeePower as-is.
constexpr float kMaxVital = 100.0f;

// The GameInstance->save_gameInst->saveSlot resolution + the cached-offset writes
// now live in ue_wrap::vitals (extracted 2026-05-30, P7). This file owns only the
// dev hotkey + the coop broadcast.

bool KeyDown(int vk) { return (::GetAsyncKeyState(vk) & 0x8000) != 0; }

}  // namespace

void SetSession(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
}

void ApplyLocally() {
    // Game-thread only (saveSlot writes touch BP state). ue_wrap::vitals caches
    // the resolution after the first call; a press before the save is registered
    // returns false on each field and is logged, not crashed.
    const bool food   = V::Write(V::Field::Food,   kMaxVital);
    const bool sleep  = V::Write(V::Field::Sleep,  kMaxVital);
    const bool health = V::Write(V::Field::Health, kMaxVital);
    if (food && sleep && health) {
        UE_LOGI("restore_vitals: vitals refilled (food/sleep/health = %.1f)", kMaxVital);
    } else {
        UE_LOGW("restore_vitals: refill incomplete (food=%d sleep=%d health=%d) -- "
                "save not registered / still booting?", food, sleep, health);
    }
}

namespace {

// Hotkey + dispatch. Polls F3 (rising edge), applies locally on the game
// thread, and broadcasts a no-payload RestoreVitals reliable. The broadcast
// is best-effort -- if Session isn't connected, the local apply still works
// (the user still gets their refill in solo play).
DWORD WINAPI HotkeyThread(LPVOID) {
    bool prevF3 = false;
    while (!coop::shutdown::IsShuttingDown()) {
        // Foreground-window gate per [[feedback-deliver-results-fast]] pattern
        // (also documented in coop::ini_config::IsOurWindowForeground): a same-box
        // host+client test would otherwise fire F3 in BOTH processes from a
        // single keypress because GetAsyncKeyState is global. We want the F3
        // press to be peer-attributed to whichever window has focus.
        const bool f3 = ::coop::ini_config::IsOurWindowForeground() && KeyDown(VK_F3);
        if (f3 && !prevF3) {
            // Local apply runs on the game thread (saveSlot writes touch BP
            // state). Broadcast is wire-thread-safe.
            GT::Post([] { ApplyLocally(); });
            auto* s = g_session.load(std::memory_order_acquire);
            if (s) {
                // No payload: the action is fixed (max-out the 4 fields).
                const bool sent = s->SendReliable(coop::net::ReliableKind::RestoreVitals, nullptr, 0);
                if (!sent) UE_LOGW("restore_vitals: broadcast failed (channel busy or not connected)");
            }
        }
        prevF3 = f3;
        ::Sleep(8);
    }
    return 0;
}

}  // namespace

void Init() {
    if (!::coop::ini_config::MasterEnabled()) {
        UE_LOGI("restore_vitals: disabled by master switch ([dev] enabled=0)");
        return;
    }
    if (!::coop::ini_config::IsIniKeyTrue("devkeys")) {
        UE_LOGI("restore_vitals: disabled (set [dev] devkeys=1 in votv-coop.ini to enable F3)");
        return;
    }
    if (HANDLE t = ::CreateThread(nullptr, 0, &HotkeyThread, nullptr, 0, nullptr)) {
        ::CloseHandle(t);  // detached
    }
    UE_LOGI("restore_vitals: ENABLED (F3 refills food/sleep/health on both peers)");
}

}  // namespace coop::dev::restore_vitals
