// ue_wrap/ufunction_hook.cpp -- see ue_wrap/ufunction_hook.h.

#include "ue_wrap/ufunction_hook.h"

#include "ue_wrap/log.h"
#include "ue_wrap/sdk_profile.h"

#include <windows.h>

#include <cstdint>

namespace ue_wrap::ufunction_hook {
namespace {

namespace P = ue_wrap::profile;

// The native exec-thunk ABI (UE4): (Context->*Func)(Stack, Result) -- RCX=Context,
// RDX=FFrame*, R8=Result. IDA-confirmed in UFunction::Invoke (0x141302DC0):
// `(*(Function+0xD8))(Object, &Frame, Result)`. Func is void-returning (the out-value
// flows through *Result, not the return), so a void thunk preserves the contract.
using NativeFuncPtr = void(__fastcall*)(void* context, void* stack, void* result);

struct Slot {
    void*              ufunction = nullptr;
    NativeFuncPtr      original  = nullptr;
    PostNativeCallback cb        = nullptr;
};

// A handful of patches is all this mechanism is for (the chipPile re-pile is the
// first). Each slot owns a distinct STAMPED thunk (NativeThunk<N>) so it closes over
// its slot index as a compile-time constant -- no per-call table lookup, no dependence
// on FFrame::CurrentNativeFunction (@+0x88). Writes happen on the game thread (install)
// and the thunks run on the game thread (native dispatch is GT) -> no cross-thread race.
constexpr int kMaxNativeHooks = 4;
Slot g_slots[kMaxNativeHooks];
int  g_slotCount = 0;

// SEH-only callback dispatch (no C++ destructors in this frame -- MSVC forbids mixing
// __try/__except with C++ unwind; same contract as game_thread::RunObserverSEH). We are
// DEEP inside the engine's spawn call, so a fault in the gameplay cb must be absorbed,
// not crash the engine. Returns 0 clean, 1 if a fault was caught.
int RunCbSEH(PostNativeCallback cb, void* src, void* result) {
    __try {
        cb(src, result);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;
    }
}

// Re-entrancy guard: the gameplay cb (a host convert) must NOT spawn an actor synchronously -- it doesn't
// (OnHostConvert only re-binds the Element + queues a reliable packet) -- but if a future cb ever did, its
// BeginDeferred would re-enter THIS thunk. Skip the cb on re-entry so a nested spawn can never double-fire
// the convert. Thread-local (native dispatch is game-thread, but the guard stays correct regardless). The
// forward ALWAYS runs (a re-entrant spawn must still proceed).
thread_local bool t_inCb = false;

template <int N>
void __fastcall NativeThunk(void* context, void* stack, void* result) {
    Slot& s = g_slots[N];
    // FFrame::Object (@0x18) = the actor whose bytecode is executing = the SOURCE entity
    // for a spawn issued from its ubergraph (the re-piling clump). Read BEFORE forwarding
    // (the original steps params off the bytecode stream but never touches Object).
    void* srcObj = stack
        ? *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(stack) + P::off::FFrame_Object)
        : nullptr;
    // Transparent forward: steps the params, runs the impl, writes *result.
    s.original(context, stack, result);
    if (!s.cb || t_inCb) return;   // re-entrant cb -> skip (no double-convert from a nested spawn)
    // *Result = the native fn's RESULT_PARAM (the spawned actor for BeginDeferred). NULL-safe:
    // a failed spawn leaves it null -> the cb gets null + logs it (never derefs blind).
    void* spawned = result ? *reinterpret_cast<void**>(result) : nullptr;
    t_inCb = true;
    const int rc = RunCbSEH(s.cb, srcObj, spawned);
    t_inCb = false;
    if (rc != 0) {
        UE_LOGE("ufunction_hook: post-native cb AV absorbed (slot %d, ufn=%p src=%p result=%p) -- "
                "engine continues", N, s.ufunction, srcObj, spawned);
    }
}

NativeFuncPtr ThunkFor(int n) {
    switch (n) {
        case 0: return &NativeThunk<0>;
        case 1: return &NativeThunk<1>;
        case 2: return &NativeThunk<2>;
        case 3: return &NativeThunk<3>;
        default: return nullptr;
    }
}
static_assert(kMaxNativeHooks == 4, "ThunkFor must enumerate every slot");

}  // namespace

bool InstallPostHook(void* ufunction, PostNativeCallback cb) {
    if (!ufunction || !cb) return false;
    // Idempotent: the same (ufunction, cb) re-install is a no-op (the caller's Install
    // retries each world-gated pass until the class resolves).
    for (int i = 0; i < g_slotCount; ++i) {
        if (g_slots[i].ufunction == ufunction && g_slots[i].cb == cb) return true;
    }
    if (g_slotCount >= kMaxNativeHooks) {
        UE_LOGE("ufunction_hook: table full (%d slots) -- cannot patch ufn=%p", kMaxNativeHooks, ufunction);
        return false;
    }
    auto* funcSlot = reinterpret_cast<NativeFuncPtr*>(
        reinterpret_cast<uint8_t*>(ufunction) + P::off::UFunction_Func);
    NativeFuncPtr original = *funcSlot;
    if (!original) {
        // Func is the native exec thunk (set at StaticRegisterNatives) -- never null for a
        // native UFunction. Null here = the offset is wrong for this build -> REFUSE (a bad
        // write would corrupt an unrelated UFunction field).
        UE_LOGE("ufunction_hook: ufn=%p Func @0x%zX reads null -- offset wrong for this build? NOT patching",
                ufunction, static_cast<size_t>(P::off::UFunction_Func));
        return false;
    }
    const int n = g_slotCount;
    g_slots[n].ufunction = ufunction;
    g_slots[n].original  = original;
    g_slots[n].cb        = cb;
    g_slotCount = n + 1;     // slot fully populated before the thunk can be reached
    // 8-byte aligned pointer swap (UFunction+0xD8 is 8-aligned; the UFunction lives in the
    // writable UE4 object pool). Atomic on x64; GT-only dispatch means no torn read anyway.
    *funcSlot = ThunkFor(n);
    UE_LOGI("ufunction_hook: patched ufn=%p Func @0x%zX (orig=%p -> thunk slot %d) -- standalone "
            "UFunction::Func hook (catches EX_CallMath calls invisible to ProcessEvent)",
            ufunction, static_cast<size_t>(P::off::UFunction_Func), original, n);
    return true;
}

}  // namespace ue_wrap::ufunction_hook
