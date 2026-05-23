// ue_wrap/game_thread.h -- run work safely on the engine's game thread.
//
// Engine-wrapper layer (principle 7). UObject::ProcessEvent (and therefore
// reflection::CallFunction) MUST run on the game thread -- calling a UFunction
// from our boot thread races the engine and crashes. This module gives us a
// guaranteed game-thread execution context by hooking ProcessEvent itself: the
// engine calls ProcessEvent constantly during play, always on the game thread,
// so our detour is a free per-call game-thread callback.
//
// The detour drains a posted-task queue (reentrancy-guarded so a task that
// calls CallFunction -- re-entering ProcessEvent -- does not recurse the pump)
// then forwards to the real ProcessEvent via the trampoline.
//
// Requires reflection::Resolve() to have found ProcessEvent first.

#pragma once

#include <functional>

namespace ue_wrap::game_thread {

using Task = std::function<void()>;

// Install the ProcessEvent hook (the game-thread anchor). Idempotent.
// Returns false if ProcessEvent is unresolved or the hook fails to install.
bool Install();

// Remove the hook. After this, posted tasks no longer run.
void Uninstall();

bool IsInstalled();

// Queue `task` to run on the game thread, inside the next ProcessEvent call.
// Thread-safe; returns immediately. Tasks run in FIFO order. A task may Post
// further tasks (they run on a subsequent pump, not the current one).
void Post(Task task);

// True if the caller is on the game thread (known once the detour has run at
// least once). Useful for asserting before touching engine state directly.
bool IsGameThread();

// Number of tasks the dispatcher has executed (diagnostics / self-test).
unsigned long long TasksRun();

// Pre-dispatch UFunction interceptor. When ProcessEvent fires with
// `function == targetUFunction`, the detour calls `cb(self, params)` and, if it
// returns true, returns WITHOUT forwarding to the original ProcessEvent --
// effectively replacing the UFunction's body for this dispatch. If cb returns
// false, the original runs normally. Multiple interceptors are NOT supported;
// a second SetInterceptor overwrites.
//
// Use case: substitute a UFunction body for SPECIFIC objects (filter on `self`
// inside cb). Born from Bug 2 -- the kerfur AnimBP's BlueprintUpdateAnimation
// writes spd=0 on a null-Pawn puppet, clobbering our streamed locomotion data.
// Intercepting BUA for the puppet's AnimInstance lets us write spd OURSELVES
// in BUA's slot, with no race against EvaluateGraphExposedInputs.
//
// Performance: the detour does ONE pointer compare per ProcessEvent dispatch
// (ProcessEvent is hot -- thousands of calls/sec -- so no map lookup, no
// allocation, no virtual). Game-thread-only (interceptors must NOT post tasks
// or call CallFunction recursively without re-entrancy care; the detour's
// t_inPump guard does NOT shield interceptor execution from re-entry).
using UFunctionInterceptor = bool(*)(void* self, void* params);
void SetInterceptor(void* targetUFunction, UFunctionInterceptor cb);
void ClearInterceptor();

}  // namespace ue_wrap::game_thread
