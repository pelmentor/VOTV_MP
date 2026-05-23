#include "ue_wrap/game_thread.h"

#include "ue_wrap/hook.h"
#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"

#include <windows.h>

#include <atomic>
#include <deque>
#include <mutex>

namespace ue_wrap::game_thread {
namespace {

// ProcessEvent's signature (x64 ABI). Matches reflection's ProcessEventFn.
using ProcessEventFn = void(__fastcall*)(void* self, void* function, void* params);

ProcessEventFn g_originalPE = nullptr;  // trampoline to the real ProcessEvent
void* g_hookTarget = nullptr;
bool g_installed = false;

std::atomic<unsigned long> g_gameThreadId{0};
std::atomic<unsigned long long> g_tasksRun{0};

// Single UFunction-pre-dispatch interceptor. Plain atomics (raw pointers, no
// std::function) so the hot ProcessEvent path does a single relaxed load and
// pointer compare -- no map lookup, no allocation. Set/cleared only at puppet
// spawn/teardown (rare; game thread).
std::atomic<void*> g_interceptUFunc{nullptr};
std::atomic<UFunctionInterceptor> g_interceptCb{nullptr};

// The posted-task queue. Pulled out under the lock, then run unlocked so a task
// may Post() without deadlocking.
std::mutex g_queueMutex;
std::deque<Task> g_queue;

// Re-entrancy guard: set while we are inside the pump, so a task that calls a
// UFunction (re-entering ProcessEvent -> this detour) skips draining and just
// forwards. Thread-local because only the game thread ever sets it, but a guard
// keeps it correct even if ProcessEvent were ever called cross-thread.
thread_local bool t_inPump = false;

void Pump() {
    for (;;) {
        Task task;
        {
            std::lock_guard<std::mutex> lk(g_queueMutex);
            if (g_queue.empty()) return;
            task = std::move(g_queue.front());
            g_queue.pop_front();
        }
        // A task running engine code can throw nothing we control; if it does,
        // letting it propagate would corrupt the engine's call. Swallow + log.
        try {
            task();
        } catch (...) {
            UE_LOGE("game_thread: a posted task threw; swallowed");
        }
        g_tasksRun.fetch_add(1, std::memory_order_relaxed);
    }
}

void __fastcall ProcessEventDetour(void* self, void* function, void* params) {
    // Record the game thread id the first time we run here. CAS so that if two
    // threads race the very first dispatch, exactly one wins (a plain load+store
    // could let a worker thread overwrite the real game thread id).
    if (g_gameThreadId.load(std::memory_order_relaxed) == 0) {
        unsigned long expected = 0;
        g_gameThreadId.compare_exchange_strong(expected, ::GetCurrentThreadId(),
                                               std::memory_order_relaxed, std::memory_order_relaxed);
    }

    // ProcessEvent is also called from task-graph WORKER threads (parallel anim,
    // etc.), not just the game thread. Posted tasks call engine UFunctions, which
    // are game-thread-only -- running them on a worker thread corrupts engine state
    // and crashes (seen as an AV on TaskGraphThreadHP). So drain the queue ONLY on
    // the recorded game thread (the first ProcessEvent caller, validated by the
    // self-test). Other threads just forward.
    if (!t_inPump && ::GetCurrentThreadId() == g_gameThreadId.load(std::memory_order_relaxed)) {
        bool hasWork;
        {
            std::lock_guard<std::mutex> lk(g_queueMutex);
            hasWork = !g_queue.empty();
        }
        if (hasWork) {
            t_inPump = true;
            Pump();
            t_inPump = false;
        }
    }

    // UFunction interceptor: pre-dispatch hook on a single target function. If
    // the interceptor returns true, the original ProcessEvent is SKIPPED -- the
    // UFunction's body is replaced for this call. Single pointer compare on the
    // hot path. The function-pointer load is `acquire` to pair with the `release`
    // store in SetInterceptor: that pairing guarantees that whenever we observe
    // a non-null target function, we also observe the matching callback store
    // that preceded it (no transient null-cb window on weakly-ordered ISAs).
    // On x86-64 (TSO) acquire is free; the ordering matters for a future ARM
    // port. The cb load can stay relaxed because the function-pointer load
    // already established the happens-before.
    void* iuf = g_interceptUFunc.load(std::memory_order_acquire);
    if (iuf && function == iuf) {
        UFunctionInterceptor cb = g_interceptCb.load(std::memory_order_relaxed);
        if (cb && cb(self, params)) return;  // skipped: do NOT forward
    }

    g_originalPE(self, function, params);
}

}  // namespace

bool Install() {
    if (g_installed) return true;

    void* pe = reinterpret_cast<void*>(reflection::ProcessEventAddr());
    if (!pe) {
        UE_LOGE("game_thread: ProcessEvent unresolved; resolve reflection first");
        return false;
    }
    if (!hook::Init()) return false;
    if (!hook::Install(pe, reinterpret_cast<void*>(&ProcessEventDetour),
                       reinterpret_cast<void**>(&g_originalPE))) {
        return false;
    }
    g_hookTarget = pe;
    g_installed = true;
    UE_LOGI("game_thread: ProcessEvent hooked; game-thread dispatcher live");
    return true;
}

void Uninstall() {
    if (!g_installed) return;
    hook::Uninstall(g_hookTarget);
    g_installed = false;
    g_hookTarget = nullptr;
    g_originalPE = nullptr;
}

bool IsInstalled() { return g_installed; }

void Post(Task task) {
    if (!task) return;
    std::lock_guard<std::mutex> lk(g_queueMutex);
    g_queue.push_back(std::move(task));
}

bool IsGameThread() {
    const unsigned long gt = g_gameThreadId.load(std::memory_order_relaxed);
    return gt != 0 && gt == ::GetCurrentThreadId();
}

unsigned long long TasksRun() { return g_tasksRun.load(std::memory_order_relaxed); }

void SetInterceptor(void* targetUFunction, UFunctionInterceptor cb) {
    // Order matters: set the callback FIRST, then publish the function pointer.
    // The detour loads the function pointer first; if it sees a valid function
    // it then loads the callback. Reverse ordering would risk a window where
    // function is set but callback is still null.
    g_interceptCb.store(cb, std::memory_order_relaxed);
    g_interceptUFunc.store(targetUFunction, std::memory_order_release);
}

void ClearInterceptor() {
    g_interceptUFunc.store(nullptr, std::memory_order_release);
    g_interceptCb.store(nullptr, std::memory_order_relaxed);
}

}  // namespace ue_wrap::game_thread
