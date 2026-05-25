#include "dev/restore_vitals.h"

#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "dev/common.h"
#include "ue_wrap/game_thread.h"
#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"
#include "ue_wrap/sdk_profile.h"

#include <windows.h>

#include <atomic>

namespace dev::restore_vitals {

namespace P = ue_wrap::profile;
namespace R = ue_wrap::reflection;
namespace GT = ue_wrap::game_thread;

namespace {

// Read-once at Init; the hotkey thread checks this before doing anything.
bool g_iniEnabled = false;

// Cached Session pointer for the broadcast. nullptr-safe at every callsite
// (a press before Session start is logged + no-op'd, NOT crashed).
std::atomic<coop::net::Session*> g_session{nullptr};

// VOTV's vitals top out at 100.0 (% units). Writing >100 doesn't visually
// over-fill the meter -- the BP graph clamps display at 100. Picking the
// exact UI cap rather than something larger so subsequent food consumption
// behaves identically to a player who reached max naturally (no hidden
// over-stored value that would gate hunger draining differently).
constexpr float kMaxVital = 100.0f;

bool KeyDown(int vk) { return (::GetAsyncKeyState(vk) & 0x8000) != 0; }

// Write a single float UPROPERTY on `target` by resolving the field name to
// an offset via reflection (BP-cooked offsets shift across recompiles --
// resolve every time, log + skip on miss). Game thread only.
void WriteFloatField(void* target, void* cls, const wchar_t* fieldName, float value) {
    const int32_t off = R::FindPropertyOffset(cls, fieldName);
    if (off < 0) {
        UE_LOGW("restore_vitals: field '%ls' not found on saveSlot_C", fieldName);
        return;
    }
    *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(target) + off) = value;
}

}  // namespace

void SetSession(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
}

void ApplyLocally() {
    void* slot = R::FindObjectByClass(P::name::SaveSlotClass);
    if (!slot) {
        UE_LOGW("restore_vitals: no UsaveSlot_C instance found (not in gameplay yet?)");
        return;
    }
    // The UClass for offset resolution is looked up by name; the instance is
    // a different pointer. FindPropertyOffset wants the CLASS, not the
    // instance (cf. ue_wrap/reflected_offset.cpp::Resolve for the same shape).
    void* cls = R::FindClass(P::name::SaveSlotClass);
    if (!cls) {
        UE_LOGW("restore_vitals: saveSlot_C class unresolvable");
        return;
    }
    // Four vitals from saveSlot.hpp: food @0x00E4, sleep @0x00E8, health
    // @0x0428, coffeePower @0x04D0. Offsets reflection-resolved.
    WriteFloatField(slot, cls, L"food",         kMaxVital);
    WriteFloatField(slot, cls, L"sleep",        kMaxVital);
    WriteFloatField(slot, cls, L"health",       kMaxVital);
    WriteFloatField(slot, cls, L"coffeePower",  kMaxVital);
    UE_LOGI("restore_vitals: vitals refilled (food/sleep/health/coffeePower = %.1f)", kMaxVital);
}

namespace {

// Hotkey + dispatch. Polls F3 (rising edge), applies locally on the game
// thread, and broadcasts a no-payload RestoreVitals reliable. The broadcast
// is best-effort -- if Session isn't connected, the local apply still works
// (the user still gets their refill in solo play).
DWORD WINAPI HotkeyThread(LPVOID) {
    bool prevF3 = false;
    for (;;) {
        // Foreground-window gate per [[feedback-deliver-results-fast]] pattern
        // (also documented in dev::common::IsOurWindowForeground): a same-box
        // host+client test would otherwise fire F3 in BOTH processes from a
        // single keypress because GetAsyncKeyState is global. We want the F3
        // press to be peer-attributed to whichever window has focus.
        const bool f3 = ::dev::IsOurWindowForeground() && KeyDown(VK_F3);
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
}

}  // namespace

void Init() {
    if (!::dev::MasterEnabled()) {
        UE_LOGI("restore_vitals: disabled by master switch ([dev] enabled=0)");
        return;
    }
    g_iniEnabled = ::dev::IsIniKeyTrue("devkeys");
    if (!g_iniEnabled) {
        UE_LOGI("restore_vitals: disabled (set [dev] devkeys=1 in votv-coop.ini to enable F3)");
        return;
    }
    if (HANDLE t = ::CreateThread(nullptr, 0, &HotkeyThread, nullptr, 0, nullptr)) {
        ::CloseHandle(t);  // detached
    }
    UE_LOGI("restore_vitals: ENABLED (F3 refills food/sleep/health/coffeePower on both peers)");
}

}  // namespace dev::restore_vitals
