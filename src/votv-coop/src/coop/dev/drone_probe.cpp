// coop/dev/drone_probe.cpp -- see coop/dev/drone_probe.h.

#include "coop/dev/drone_probe.h"

#include "coop/ini_config.h"
#include "ue_wrap/game_thread.h"
#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace coop::dev::drone_probe {
namespace {

namespace R  = ue_wrap::reflection;
namespace GT = ue_wrap::game_thread;

bool ProbeEnabled() {
    static const bool s_enabled = coop::ini_config::IsIniKeyTrue("drone_probe");
    return s_enabled;
}

// ---- observer identification table -----------------------------------------
// One shared callback identifies which UFunction fired by comparing the dispatched
// `function` ptr against the cached set. A silent verb = BP-internal (the trap).
struct WatchedFn { void* fn = nullptr; const char* name = nullptr; bool isTick = false; };
std::array<WatchedFn, 32> g_watched{};
size_t g_watchedCount = 0;
std::atomic<bool> g_receiveTickLogged{false};

void OnDroneVerb(void* self, void* function, void* /*params*/) {
    if (!ProbeEnabled()) return;
    for (size_t i = 0; i < g_watchedCount; ++i) {
        if (g_watched[i].fn != function) continue;
        const std::wstring cls = (self && R::IsLive(self)) ? R::ClassNameOf(self) : std::wstring(L"?");
        if (g_watched[i].isTick) {
            // ReceiveTick fires every frame -> log ONCE (confirms the observer mechanism
            // works AND the drone is ticking), then stay silent.
            if (!g_receiveTickLogged.exchange(true)) {
                UE_LOGI("[drone_probe] OBSERVER-OK: %s fired (self=%p cls='%ls') -- "
                        "ProcessEvent observers work + the drone ticks",
                        g_watched[i].name, self, cls.c_str());
            }
            return;
        }
        UE_LOGI("[drone_probe] VERB FIRED: %s (self=%p cls='%ls') -- ProcessEvent-DISPATCHED = OBSERVABLE",
                g_watched[i].name, self, cls.c_str());
        return;
    }
}

void RegisterOn(const wchar_t* className, const wchar_t* fnName, const char* label, bool isTick) {
    void* cls = R::FindClass(className);
    if (!cls) return;  // class not loaded yet (caller retries) / not present
    void* fn = R::FindFunction(cls, fnName);
    if (!fn) {
        UE_LOGW("[drone_probe] UFunction '%ls::%ls' not found (BP may name it differently)", className, fnName);
        return;
    }
    for (size_t i = 0; i < g_watchedCount; ++i) if (g_watched[i].fn == fn) return;  // dedup
    if (!GT::RegisterPostObserver(fn, &OnDroneVerb)) {
        UE_LOGW("[drone_probe] RegisterPostObserver failed for %s (table full?)", label);
        return;
    }
    if (g_watchedCount < g_watched.size())
        g_watched[g_watchedCount++] = WatchedFn{fn, label, isTick};
}

bool g_installed = false;

// ---- state poll cache ------------------------------------------------------
void* g_gmCache = nullptr;
void* ResolveGamemode() {
    if (g_gmCache && R::IsLive(g_gmCache)) return g_gmCache;
    g_gmCache = R::FindObjectByClass(L"mainGamemode_C");
    return g_gmCache;
}

int32_t Off(void* cls, const wchar_t* name) { return cls ? R::FindPropertyOffset(cls, name) : -1; }

template <typename T>
T ReadAt(void* obj, int32_t off) {
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(obj) + off);
}

// Transition tracking (so we log on CHANGE, not every tick).
int g_lastActive = -2, g_lastHasOrder = -2, g_lastHasSack = -2, g_lastFlyingType = -2, g_lastPickedUp = -2;
bool g_dronePresenceLogged = false;
bool g_droneAbsenceLogged  = false;
int  g_radarLast = -2;
uint64_t g_tickCounter = 0;

}  // namespace

void Install() {
    if (!ProbeEnabled() || g_installed) return;
    if (!R::FindClass(L"drone_C")) return;  // drone BP not loaded yet -- retry next tick

    // Drone verbs (POST -- observe AFTER they ran). A FIRED log = observable; SILENCE
    // during a real delivery = BP-internal (the doorOpen/flashlight trap) -> we must
    // poll the flags (state dump below) instead of hooking that verb.
    RegisterOn(L"drone_C", L"ReceiveTick",      "drone.ReceiveTick",     /*isTick*/true);
    RegisterOn(L"drone_C", L"triggerFly",       "drone.triggerFly",      false);
    RegisterOn(L"drone_C", L"beginFly",         "drone.beginFly",        false);
    RegisterOn(L"drone_C", L"dropSack",         "drone.dropSack",        false);
    RegisterOn(L"drone_C", L"soundAlarm",       "drone.soundAlarm",      false);
    RegisterOn(L"drone_C", L"checkOrders",      "drone.checkOrders",     false);
    RegisterOn(L"drone_C", L"compileOrder",     "drone.compileOrder",    false);
    RegisterOn(L"drone_C", L"player_use",       "drone.player_use",      false);
    RegisterOn(L"drone_C", L"sendShop",         "drone.sendShop",        false);
    RegisterOn(L"drone_C", L"ReceiveBeginPlay", "drone.ReceiveBeginPlay",false);
    // Console call path (the #1-risk question: is the client's call edge hookable?).
    RegisterOn(L"droneConsole_C", L"player_use",        "console.player_use",        false);
    RegisterOn(L"droneConsole_C", L"playerHandUse_LMB", "console.playerHandUse_LMB", false);
    RegisterOn(L"droneConsole_C", L"isButtonUsed",      "console.isButtonUsed",      false);
    // Cargo spawn -> does the box fire the Aprop_C Init the prop pipeline observes? ([#3])
    RegisterOn(L"orderPlace_C",                  L"spawnOrder",      "orderPlace.spawnOrder", false);
    RegisterOn(L"prop_container_orderbox_C",     L"Init",            "orderbox.Init",         false);
    RegisterOn(L"prop_container_giftbox_C",      L"Init",            "giftbox.Init",          false);
    RegisterOn(L"prop_inventoryContainer_drone_C", L"ReceiveBeginPlay", "itembox.BeginPlay",  false);
    // Auto-drive schedule + gift injection ([#8],[#9]).
    RegisterOn(L"daynightCycle_C", L"sendDriveBox",       "daynight.sendDriveBox",     false);
    RegisterOn(L"daynightCycle_C", L"Make Default Order", "daynight.MakeDefaultOrder", false);
    RegisterOn(L"daynightCycle_C", L"createNewTask",      "daynight.createNewTask",    false);

    g_installed = true;
    UE_LOGI("[drone_probe] installed %zu observers (drone/console/orderPlace/cargo/daynight). "
            "Call the drone console for a delivery -- a FIRED line = observable, silence = BP-internal.",
            g_watchedCount);
}

void Tick() {
    if (!ProbeEnabled()) return;
    void* gm = ResolveGamemode();
    if (!gm) return;
    void* gmCls = R::ClassOf(gm);
    const int32_t offDrone = Off(gmCls, L"drone");
    if (offDrone < 0) return;

    void* drone = ReadAt<void*>(gm, offDrone);
    if (!drone || !R::IsLive(drone)) {
        if (!g_droneAbsenceLogged) {
            g_droneAbsenceLogged = true;
            UE_LOGI("[drone_probe] mainGamemode.drone is NULL/dead right now (gm=%p) -- "
                    "drone may be dormant/un-spawned; watching for it.", gm);
        }
        return;
    }
    g_droneAbsenceLogged = false;
    if (!g_dronePresenceLogged) {
        g_dronePresenceLogged = true;
        UE_LOGI("[drone_probe] DRONE PRESENT [#1]: mainGamemode.drone=%p cls='%ls' "
                "(if this ptr stays stable all session -> singleton placed actor, identity OK)",
                drone, R::ClassNameOf(drone).c_str());
    }

    void* dCls = R::ClassOf(drone);
    const int active   = Off(dCls, L"Active")     >= 0 ? (int)ReadAt<uint8_t>(drone, Off(dCls, L"Active"))     : -1;
    const int hasOrder = Off(dCls, L"hasOrder")   >= 0 ? (int)ReadAt<uint8_t>(drone, Off(dCls, L"hasOrder"))   : -1;
    const int hasSack  = Off(dCls, L"hasSack")    >= 0 ? (int)ReadAt<uint8_t>(drone, Off(dCls, L"hasSack"))    : -1;
    const int flying   = Off(dCls, L"flyingType") >= 0 ? (int)ReadAt<int32_t>(drone, Off(dCls, L"flyingType")) : -2;
    const int picked   = Off(dCls, L"pickedUp")   >= 0 ? (int)ReadAt<uint8_t>(drone, Off(dCls, L"pickedUp"))   : -1;

    if (active != g_lastActive || hasOrder != g_lastHasOrder || hasSack != g_lastHasSack ||
        flying != g_lastFlyingType || picked != g_lastPickedUp) {
        UE_LOGI("[drone_probe] STATE [#4]: Active=%d hasOrder=%d hasSack=%d flyingType=%d pickedUp=%d",
                active, hasOrder, hasSack, flying, picked);
        g_lastActive = active; g_lastHasOrder = hasOrder; g_lastHasSack = hasSack;
        g_lastFlyingType = flying; g_lastPickedUp = picked;
    }

    // radarObjects membership ([#10]) -- TArray<AActor*> = { void** Data; int32 Num; ... }.
    const int32_t offRadar = Off(gmCls, L"radarObjects");
    if (offRadar >= 0) {
        void** data = ReadAt<void**>(gm, offRadar);
        const int32_t num = ReadAt<int32_t>(gm, offRadar + 8);
        int present = 0;
        if (data && num > 0 && num < 8192) {
            for (int32_t i = 0; i < num; ++i) if (data[i] == drone) { present = 1; break; }
        }
        if (present != g_radarLast) {
            UE_LOGI("[drone_probe] RADAR [#10]: drone %s mainGamemode.radarObjects (Num=%d) -- "
                    "%s on the local radar panel",
                    present ? "IS IN" : "is NOT in", num, present ? "blips" : "no blip");
            g_radarLast = present;
        }
    }

    // saveSlot order/economy dump ([#6]) -- throttled (~ every 300 ticks).
    if ((g_tickCounter++ % 300) == 0) {
        const int32_t offSave = Off(gmCls, L"saveSlot");
        void* save = offSave >= 0 ? ReadAt<void*>(gm, offSave) : nullptr;
        if (save && R::IsLive(save)) {
            void* sCls = R::ClassOf(save);
            const int32_t offOrders = Off(sCls, L"orders");        // TArray<Fstruct_storeOrder>
            const int32_t offActive = Off(sCls, L"droneOrder");    // Fstruct_storeOrder { items[]@0; time@0x10 }
            const int32_t offPoints = Off(sCls, L"Points");
            const int32_t offDaily  = Off(sCls, L"dailyDelivery");
            const int ordersNum  = offOrders >= 0 ? ReadAt<int32_t>(save, offOrders + 8)  : -1;
            const int activeItems = offActive >= 0 ? ReadAt<int32_t>(save, offActive + 8) : -1;
            const int points     = offPoints >= 0 ? ReadAt<int32_t>(save, offPoints)      : -1;
            const int daily      = offDaily  >= 0 ? (int)ReadAt<uint8_t>(save, offDaily)  : -1;
            UE_LOGI("[drone_probe] SAVE [#6]: orders.Num=%d droneOrder.items=%d Points=%d dailyDelivery=%d",
                    ordersNum, activeItems, points, daily);
        }
    }
}

}  // namespace coop::dev::drone_probe
