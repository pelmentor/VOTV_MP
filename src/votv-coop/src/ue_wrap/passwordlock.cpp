// ue_wrap/passwordlock.cpp -- see ue_wrap/passwordlock.h. Engine access for VOTV
// password keypads (ApasswordLock_C).
//
// All field offsets are resolved from the live class via reflection
// (FindPropertyOffset) rather than hardcoded, so they stay correct across game
// builds (version-tagging rule). The known Alpha 0.9.0-n offsets are kept only as a
// logged fallback if the reflected walk ever fails to find the property.

#include "ue_wrap/passwordlock.h"

#include "ue_wrap/call.h"
#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"

#include <atomic>
#include <cstdint>

namespace ue_wrap::passwordlock {
namespace {

namespace R = reflection;

// Resolved once at EnsureResolved, then read-only. Published via the g_resolved
// release-store / acquire-load.
std::atomic<bool> g_resolved{false};

void*   g_lockCls    = nullptr;  // passwordLock_C UClass
int32_t g_keyOff     = -1;       // AtriggerBase_C::Key   (Alpha 0.9.0-n: 0x0260)
int32_t g_isAccOff   = -1;       // ApasswordLock_C::isAcc      (0x037C)
int32_t g_isDenyOff  = -1;       // ApasswordLock_C::isDeny     (0x037D)
int32_t g_inPwOff    = -1;       // ApasswordLock_C::inPassword (0x0380, FString)
void*   g_inputNumFn = nullptr;  // ApasswordLock_C::inputNumber(int32 Num)
void*   g_resetFn    = nullptr;  // ApasswordLock_C::Reset()
void*   g_updFn      = nullptr;  // ApasswordLock_C::upd()  (best-effort refresh; may be null)

// Documented Alpha 0.9.0-n fallbacks (CXXHeaderDump/passwordLock.hpp + triggerBase.hpp).
constexpr int32_t kKeyOffFallback    = 0x0260;
constexpr int32_t kIsAccOffFallback  = 0x037C;
constexpr int32_t kIsDenyOffFallback = 0x037D;
constexpr int32_t kInPwOffFallback   = 0x0380;

int32_t ResolveOff(void* cls, const wchar_t* name, int32_t fallback) {
    int32_t off = R::FindPropertyOffset(cls, name);
    if (off < 0) {
        UE_LOGW("passwordlock: reflected %ls offset not found -- using fallback 0x%04X", name, fallback);
        off = fallback;
    }
    return off;
}

std::wstring ReadFString(const void* obj, int32_t off) {
    if (!obj || off < 0) return std::wstring();
    const R::FString& s = *reinterpret_cast<const R::FString*>(
        reinterpret_cast<const char*>(obj) + off);
    if (!s.Data || s.Num <= 1 || s.Num > 4096) return std::wstring();
    return std::wstring(s.Data, s.Data + (s.Num - 1));  // Num counts the null terminator
}

bool ReadBool(const void* obj, int32_t off) {
    return (off >= 0) && *reinterpret_cast<const bool*>(reinterpret_cast<const char*>(obj) + off);
}
void WriteBool(void* obj, int32_t off, bool v) {
    if (obj && off >= 0) *reinterpret_cast<bool*>(reinterpret_cast<char*>(obj) + off) = v;
}

}  // namespace

bool EnsureResolved() {
    if (g_resolved.load(std::memory_order_acquire)) return true;

    void* lockCls = R::FindClass(L"passwordLock_C");
    if (!lockCls) return false;  // BP class not loaded yet -- caller retries

    // Key is declared on AtriggerBase_C; FindPropertyOffset does NOT climb to super,
    // so query the declaring class. The rest are declared on passwordLock_C.
    int32_t keyOff = -1;
    if (void* trigCls = R::FindClass(L"triggerBase_C")) keyOff = R::FindPropertyOffset(trigCls, L"Key");
    if (keyOff < 0) {
        UE_LOGW("passwordlock: reflected Key offset not found -- using fallback 0x%04X", kKeyOffFallback);
        keyOff = kKeyOffFallback;
    }
    const int32_t isAccOff  = ResolveOff(lockCls, L"isAcc",      kIsAccOffFallback);
    const int32_t isDenyOff = ResolveOff(lockCls, L"isDeny",     kIsDenyOffFallback);
    const int32_t inPwOff   = ResolveOff(lockCls, L"inPassword", kInPwOffFallback);

    void* inputNumFn = R::FindFunction(lockCls, L"inputNumber");
    if (!inputNumFn) {
        UE_LOGW("passwordlock: inputNumber UFunction not found -- not ready");
        return false;  // the receiver cannot mirror typing without it -- retry
    }
    void* resetFn = R::FindFunction(lockCls, L"Reset");  // may be absent on some builds
    void* updFn   = R::FindFunction(lockCls, L"upd");    // best-effort; tolerated null

    g_lockCls    = lockCls;
    g_keyOff     = keyOff;
    g_isAccOff   = isAccOff;
    g_isDenyOff  = isDenyOff;
    g_inPwOff    = inPwOff;
    g_inputNumFn = inputNumFn;
    g_resetFn    = resetFn;
    g_updFn      = updFn;
    g_resolved.store(true, std::memory_order_release);
    UE_LOGI("passwordlock: resolved passwordLock_C=%p Key@0x%04X isAcc@0x%04X "
            "isDeny@0x%04X inPassword@0x%04X inputNumber=%p Reset=%p upd=%p",
            lockCls, keyOff, isAccOff, isDenyOff, inPwOff, inputNumFn, resetFn, updFn);
    return true;
}

bool IsPasswordLock(void* obj) {
    if (!obj || !g_lockCls) return false;
    void* cls = R::ClassOf(obj);
    if (!cls) return false;
    void* bases[1] = { g_lockCls };
    return R::IsDescendantOfAny(cls, bases, 1);
}

std::wstring GetKeyString(void* lock) {
    if (!lock || g_keyOff < 0) return std::wstring();
    const R::FName& key = *reinterpret_cast<const R::FName*>(
        reinterpret_cast<const char*>(lock) + g_keyOff);
    return R::ToString(key);
}

bool ReadState(void* lock, State& out) {
    if (!lock || !g_resolved.load(std::memory_order_acquire)) return false;
    out.buffer = ReadFString(lock, g_inPwOff);
    out.isAcc  = ReadBool(lock, g_isAccOff);
    out.isDeny = ReadBool(lock, g_isDenyOff);
    return true;
}

bool CallInputNumber(void* lock, int32_t digit) {
    if (!lock || !g_inputNumFn || digit < 0 || digit > 9) return false;
    ParamFrame f(g_inputNumFn);
    if (!f.valid()) return false;
    // Param name "Num" is the live FProperty name (passwordLock.hpp:98 inputNumber(int32 Num)).
    f.Set<int32_t>(L"Num", digit);
    return Call(lock, f);
}

bool CallReset(void* lock) {
    if (!lock || !g_resetFn) return false;
    ParamFrame f(g_resetFn);
    if (!f.valid()) return false;
    return Call(lock, f);
}

void WriteAccepted(void* lock, bool v) { WriteBool(lock, g_isAccOff,  v); }
void WriteDenied(void* lock, bool v)   { WriteBool(lock, g_isDenyOff, v); }

void CallUpd(void* lock) {
    if (!lock || !g_updFn) return;
    ParamFrame f(g_updFn);
    if (f.valid()) Call(lock, f);
}

}  // namespace ue_wrap::passwordlock
