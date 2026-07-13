// coop/creatures/kerfur_form_assembler.cpp -- see kerfur_form_assembler.h.
//
// Increment 1 (observe-only) of docs/COOP_VM_DISPATCH_PLAN.md S3. Registers the two
// conversion verbs (dropKerfurProp / spawnKerfuro) with the EX_Local* substrate and,
// on each caught 0x45 dispatch, LOGS the Context class -- proving the PERMANENT
// substrate delivers the catch end-to-end through the registration API and turning
// the inferred kerfur variant family into a MEASURED set.
//
// It ALSO carries the CONTAINMENT COUNTER the /qf pass (2026-07-13, user's 8 Qs + 3
// escalation rounds) forced into this increment: the 2a capture design rests on
// "FinishSpawningActor + K2_DestroyActor fire SYNCHRONOUSLY inside the 0x45 bracket"
// -- currently INFERRED from the 2026-06-12 kismet disasm, NOT measured through the
// live bracket. Rather than build 2a on that inference, this increment MEASURES it
// for free: the substrate publishes the active-verb window (ue_wrap::vm_dispatch::
// CurrentThreadVerb), and this module registers its OWN post-hooks on the two natives
// and counts each kerfur-relevant spawn/destroy as IN-window vs OUT-of-window. 2a is
// HALT-gated on the verdict (all form spawns + self-destroys land in-window).
//
// Attribution is deterministic, per the /qf convergence:
//   - SPAWN: class-filter the finished *Result -- a kerfur-form successor
//     (kerfurOmega_C / prop_kerfurOmega_C family) is B; the floppy (prop_floppyDisc_C)
//     is distinguished by class, NOT conflated. "in-window AND form-successor", not
//     "in-window".
//   - DESTROY: the invariant is IDENTITY, not class -- a conversion verb destroys
//     ITSELF, so the K2_DestroyActor whose dying actor == the bracket's Context is the
//     verb's own victim (the seam 2b suppresses), told apart from unrelated kerfur
//     churn by pointer identity.
// Both seams are pinned INDEPENDENTLY (spawn does not imply destroy).
//
// The COUNTERS accrue whenever the session is active, independent of the verbose log
// -- and a one-line summary is ALWAYS dumped at OnDisconnect -- so a run can never end
// having measured nothing. Only the per-catch VERBOSE lines are gated behind [dev]
// vm_dispatch_log (+ a cap). Every line is role-tagged [HOST]/[CLIENT] so the gate
// set is not authority-blind. NO capture / suppress / converge yet (2a-2c); the verb
// bodies run entirely unchanged.

#include "coop/creatures/kerfur_form_assembler.h"

#include "coop/config/config.h"
#include "coop/net/session.h"

#include "ue_wrap/game_thread.h"
#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"
#include "ue_wrap/sdk_profile_names.h"
#include "ue_wrap/ufunction_hook.h"
#include "ue_wrap/vm_dispatch.h"

#include <atomic>
#include <cstdint>

namespace coop::kerfur_form_assembler {
namespace {

namespace R  = ue_wrap::reflection;
namespace GT = ue_wrap::game_thread;
namespace vm = ue_wrap::vm_dispatch;
namespace P  = ue_wrap::profile;

// Verb ids echoed back in the substrate Bracket (also the window's verbId).
constexpr int kVerbTurnOff = 1;  // dropKerfurProp -- NPC -> prop (destroys the NPC self)
constexpr int kVerbTurnOn  = 2;  // spawnKerfuro   -- prop -> NPC (destroys the prop self)

coop::net::Session* g_session = nullptr;

// Kerfur-form + floppy class pointers (resolved lazily on the GT for the spawn
// class-filter). Null until resolved -> a spawn never mis-attributes.
std::atomic<void*> g_npcClass{nullptr};     // kerfurOmega_C     (turn-ON successor + turn-OFF victim)
std::atomic<void*> g_propClass{nullptr};    // prop_kerfurOmega_C (turn-OFF successor + turn-ON victim)
std::atomic<void*> g_floppyClass{nullptr};  // prop_floppyDisc_C  (the incidental floppy spawn)
std::atomic<bool>  g_classesResolved{false};

// The two native seams (Func-patched once, process-lifetime). Own hooks -- idempotent
// + multi-patch, so no collision with host_spawn_watcher's FinishSpawningActor patch.
void* g_finishSpawnFn = nullptr;
void* g_destroyFn     = nullptr;
std::atomic<bool> g_seamsInstalled{false};

// ---- containment counters (accrue whenever the session is enabled) ----
std::atomic<std::uint64_t> g_spawnFormInWindow{0};   // kerfur-form successor spawned INSIDE a bracket
std::atomic<std::uint64_t> g_spawnFormOutWindow{0};  // kerfur-form successor spawned OUTSIDE any bracket
std::atomic<std::uint64_t> g_spawnFloppyInWindow{0}; // the floppy spawned inside a bracket (class-distinguished)
std::atomic<std::uint64_t> g_destroySelfInWindow{0}; // dying actor == bracket Context (the verb's self-destroy)
std::atomic<std::uint64_t> g_destroyOtherInWindow{0};// a kerfur-class actor != Context destroyed inside a bracket (anomaly)
std::atomic<std::uint64_t> g_destroyKerfurOutWindow{0}; // a kerfur-class actor destroyed OUTSIDE any bracket
std::atomic<std::uint64_t> g_catchTurnOff{0};        // 0x45 dropKerfurProp entries caught
std::atomic<std::uint64_t> g_catchTurnOn{0};         // 0x45 spawnKerfuro entries caught

// Verbose per-catch log cap (the measurement counters above are UNCAPPED + always on).
constexpr int kLogCap = 128;
std::atomic<int> g_logged{0};

bool LogVerbose() { return coop::config::IsIniKeyTrue("vm_dispatch_log"); }
bool IsHostRole() { return g_session && g_session->role() == coop::net::Role::Host; }
const char* RoleTag() { return IsHostRole() ? "HOST" : "CLIENT"; }

// Is `cls` a kerfur-form class (either the NPC or prop family)?
bool IsKerfurFormClass(void* cls) {
    void* bases[2] = {g_npcClass.load(std::memory_order_relaxed),
                      g_propClass.load(std::memory_order_relaxed)};
    if (!bases[0] || !bases[1]) return false;
    return R::IsDescendantOfAny(cls, bases, 2);
}
bool IsFloppyClass(void* cls) {
    void* fb = g_floppyClass.load(std::memory_order_relaxed);
    return fb && R::IsDescendantOfAny(cls, &fb, 1);
}

// ---- the substrate ENTRY callback (observe-only) -------------------------------
void OnVerbEntry(const vm::Bracket& b) {
    if (b.verbId == kVerbTurnOff) g_catchTurnOff.fetch_add(1, std::memory_order_relaxed);
    else                          g_catchTurnOn.fetch_add(1, std::memory_order_relaxed);

    if (!LogVerbose()) return;
    if (g_logged.fetch_add(1, std::memory_order_relaxed) >= kLogCap) return;
    const wchar_t* verb = (b.verbId == kVerbTurnOff) ? L"dropKerfurProp" : L"spawnKerfuro";
    std::wstring cls = R::ClassNameOf(b.ctx);
    UE_LOGI("[kerfur_asm][%s] VERB %ls id=%d Context=%p class=%ls depth=%d (observe-only)",
            RoleTag(), verb, b.verbId, b.ctx, cls.c_str(), b.depth);
}

// ---- the SPAWN seam (FinishSpawningActor post-hook) ----------------------------
// spawnedResult = *Result = the finished actor (ufunction_hook contract).
void OnFinishSpawn(void* /*context*/, void* /*sourceObject*/, void* spawnedResult) {
    if (!vm::IsEnabled() || !spawnedResult) return;  // no cost in solo SP
    void* cls = R::ClassOf(spawnedResult);
    if (!cls) return;
    const bool isForm   = IsKerfurFormClass(cls);
    const bool isFloppy = !isForm && IsFloppyClass(cls);
    if (!isForm && !isFloppy) return;  // unrelated spawn -- not our measurement

    const vm::ActiveVerb av = vm::CurrentThreadVerb();
    if (isForm) {
        if (av.active) g_spawnFormInWindow.fetch_add(1, std::memory_order_relaxed);
        else           g_spawnFormOutWindow.fetch_add(1, std::memory_order_relaxed);
    } else {  // floppy
        if (av.active) g_spawnFloppyInWindow.fetch_add(1, std::memory_order_relaxed);
    }
    if (LogVerbose() && g_logged.fetch_add(1, std::memory_order_relaxed) < kLogCap) {
        std::wstring cn = R::ClassNameOf(spawnedResult);
        UE_LOGI("[kerfur_asm][%s] SPAWN %ls actor=%p class=%ls %s verbId=%d depth=%d",
                RoleTag(), isForm ? L"FORM" : L"floppy", spawnedResult, cn.c_str(),
                av.active ? "IN-WINDOW" : "out-of-window", av.verbId, av.depth);
    }
}

// ---- the DESTROY seam (K2_DestroyActor post-hook) ------------------------------
// context = the dying actor (ufunction_hook contract).
void OnDestroy(void* context, void* /*sourceObject*/, void* /*result*/) {
    if (!vm::IsEnabled() || !context) return;  // no cost in solo SP
    void* cls = R::ClassOf(context);
    if (!cls || !IsKerfurFormClass(cls)) return;  // only kerfur-class destroys interest us

    const vm::ActiveVerb av = vm::CurrentThreadVerb();
    const char* kind;
    if (!av.active) {
        g_destroyKerfurOutWindow.fetch_add(1, std::memory_order_relaxed);
        kind = "out-of-window";
    } else if (context == av.ctx) {  // IDENTITY invariant: the verb's own self-destroy
        g_destroySelfInWindow.fetch_add(1, std::memory_order_relaxed);
        kind = "IN-WINDOW self";
    } else {
        g_destroyOtherInWindow.fetch_add(1, std::memory_order_relaxed);
        kind = "IN-WINDOW non-self(anomaly)";
    }
    if (LogVerbose() && g_logged.fetch_add(1, std::memory_order_relaxed) < kLogCap) {
        std::wstring cn = R::ClassNameOf(context);
        UE_LOGI("[kerfur_asm][%s] DESTROY actor=%p class=%ls %s verbId=%d ctx=%p depth=%d",
                RoleTag(), context, cn.c_str(), kind, av.verbId, av.ctx, av.depth);
    }
}

// Resolve the classes + Func-patch the two seams. GT-only (FindClass/FindFunction +
// InstallPostHook). Retries until everything binds; latches once installed.
void EnsureSeamsInstalled() {
    if (g_seamsInstalled.load(std::memory_order_acquire)) return;
    if (!GT::IsGameThread()) return;

    if (!g_classesResolved.load(std::memory_order_relaxed)) {
        if (!g_npcClass.load(std::memory_order_relaxed))
            g_npcClass.store(R::FindClass(L"kerfurOmega_C"), std::memory_order_relaxed);
        if (!g_propClass.load(std::memory_order_relaxed))
            g_propClass.store(R::FindClass(L"prop_kerfurOmega_C"), std::memory_order_relaxed);
        if (!g_floppyClass.load(std::memory_order_relaxed))
            g_floppyClass.store(R::FindClass(L"prop_floppyDisc_C"), std::memory_order_relaxed);
        // Need the two form bases before the spawn/destroy filters are meaningful; the
        // floppy is non-fatal (a null floppy class just leaves floppy spawns uncounted).
        if (!g_npcClass.load(std::memory_order_relaxed) ||
            !g_propClass.load(std::memory_order_relaxed))
            return;  // retry next tick
        g_classesResolved.store(true, std::memory_order_relaxed);
    }

    if (!g_finishSpawnFn) {
        if (void* gsCls = R::FindClass(P::name::GameplayStaticsClass))
            g_finishSpawnFn = R::FindFunction(gsCls, P::name::FinishSpawningActorFn);
    }
    if (!g_destroyFn) {
        if (void* actorCls = R::FindClass(P::name::ActorClassName))
            g_destroyFn = R::FindFunction(actorCls, P::name::DestroyActorFn);
    }
    if (!g_finishSpawnFn || !g_destroyFn) return;  // retry next tick

    const bool a = ue_wrap::ufunction_hook::InstallPostHook(g_finishSpawnFn, &OnFinishSpawn);
    const bool b = ue_wrap::ufunction_hook::InstallPostHook(g_destroyFn, &OnDestroy);
    g_seamsInstalled.store(true, std::memory_order_release);  // latch regardless (idempotent hooks)
    UE_LOGI("[kerfur_asm] containment seams installed: FinishSpawningActor=%d K2_DestroyActor=%d "
            "(npc=%p prop=%p floppy=%p)", a ? 1 : 0, b ? 1 : 0,
            g_npcClass.load(std::memory_order_relaxed), g_propClass.load(std::memory_order_relaxed),
            g_floppyClass.load(std::memory_order_relaxed));
}

void DumpSummary(const char* when) {
    UE_LOGI("[kerfur_asm][%s] CONTAINMENT SUMMARY (%s): catch{off=%llu on=%llu} "
            "spawn{formIn=%llu formOut=%llu floppyIn=%llu} "
            "destroy{selfIn=%llu otherIn=%llu kerfurOut=%llu} -- IN-window good, OUT/anomaly = 2a-HALT",
            RoleTag(), when,
            (unsigned long long)g_catchTurnOff.load(std::memory_order_relaxed),
            (unsigned long long)g_catchTurnOn.load(std::memory_order_relaxed),
            (unsigned long long)g_spawnFormInWindow.load(std::memory_order_relaxed),
            (unsigned long long)g_spawnFormOutWindow.load(std::memory_order_relaxed),
            (unsigned long long)g_spawnFloppyInWindow.load(std::memory_order_relaxed),
            (unsigned long long)g_destroySelfInWindow.load(std::memory_order_relaxed),
            (unsigned long long)g_destroyOtherInWindow.load(std::memory_order_relaxed),
            (unsigned long long)g_destroyKerfurOutWindow.load(std::memory_order_relaxed));
}

}  // namespace

void Install(coop::net::Session* session) {
    g_session = session;
    vm::RegisterVirtualVerb(L"dropKerfurProp", kVerbTurnOff, &OnVerbEntry);
    vm::RegisterVirtualVerb(L"spawnKerfuro",   kVerbTurnOn,  &OnVerbEntry);
    vm::SetEnabled(true);  // gate ACTIVITY on session-active (both roles -- the client
                           // needs the bracket to observe its OWN conversion; plan R9).
}

void Tick() {
    vm::TickResolvePending();  // GT FName resolve for the two verbs (no-op once armed)
    EnsureSeamsInstalled();    // GT class + Func-seam bind (retries until latched)
}

void OnDisconnect() {
    DumpSummary("session-end");  // ALWAYS -- the measurement is never behind the log gate
    vm::SetEnabled(false);
    g_logged.store(0, std::memory_order_relaxed);  // fresh verbose budget next session
}

}  // namespace coop::kerfur_form_assembler
