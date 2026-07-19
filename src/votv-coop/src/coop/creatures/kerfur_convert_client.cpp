// coop/kerfur_convert_client.cpp -- the CLIENT half of the kerfur conversion
// feature: conversion-ghost custody (claim/park, cleanup-reap, take-by-eid) +
// the KerfurConvert wire apply. Extracted VERBATIM from kerfur_convert.cpp
// (2026-07-19 s27 cut); the feature narrative + kismet ground truth live in
// kerfur_convert.h / votv-kerfur-convert-RE-2026-06-12.md. Interfaces:
// kerfur_convert_client.h. Class pointers + the session arrive via SetClasses/
// SetSession from kerfur_convert::Install (two-layer handoff; see the header).

#include "coop/creatures/kerfur_convert_client.h"

#include "coop/element/mirror_manager.h"
#include "coop/element/npc.h"
#include "coop/element/prop.h"
#include "coop/element/registry.h"
#include "coop/net/protocol.h"
#include "coop/net/session.h"
#include "coop/creatures/npc_mirror.h"
#include "coop/creatures/kerfur_entity.h"
#include "coop/props/prop_lifecycle.h"
#include "coop/props/remote_prop.h"
#include "coop/props/remote_prop_spawn.h"
#include "ue_wrap/engine/engine.h"
#include "ue_wrap/actors/kerfur.h"
#include "ue_wrap/core/log.h"
#include "ue_wrap/actors/puppet.h"
#include "ue_wrap/core/reflection.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace coop::kerfur_convert_client {
namespace {

namespace R = ue_wrap::reflection;

std::atomic<coop::net::Session*> g_session{nullptr};

coop::net::Session* LoadSession() {
    return g_session.load(std::memory_order_acquire);
}

// Resolved kerfur refs (pushed by kerfur_convert::Install via SetClasses every
// attempt; read-only on the game thread here).
void* g_kerfurNpcClass  = nullptr;
void* g_kerfurPropClass = nullptr;
void* g_floppyClass     = nullptr;

// ---- client-side apply (kerfur redesign K-4b) --------------------------------
// Materialize (or adopt this peer's claimed local ghost into) a kerfur mirror of `form` at host-range
// `eid`. `adoptEid` (D2 relay) = the converting mirror's eid (the host's KerfurConvert oldEid); the parked
// ghost tagged with it is adopted DETERMINISTICALLY (no fuzzy position miss = no flash). kInvalidId = no
// eid-keyed adopt (the rejected-restore path). NPC: adopt the parked turn-on ghost (by eid, then position)
// else fresh-spawn. PROP: adopt the frozen turn-off ghost (by eid directly, else the synthetic-PropSpawn
// Gap-I-1 fuzzy match) else fresh-spawn. The prop mirror carries a deterministic SYNTHETIC key from eid.
void MaterializeKerfurMirror(bool toNpc, coop::element::ElementId eid, coop::element::ElementId adoptEid,
                             const std::wstring& classW,
                             float lx, float ly, float lz, float rp, float ry, float rr,
                             void* localPlayer) {
    if (toNpc) {
        // D2 relay: adopt the eid-tagged ghost DETERMINISTICALLY (v91: the 500cm position fallback is gone --
        // the initiator's ghost is always tagged with the converting eid, so a position miss never happens; a
        // non-initiator has no ghost at this eid -> fresh-spawn below, eid-deduped, the correct action).
        void* ghost = (adoptEid != coop::element::kInvalidId)
                          ? TakeParkedGhostByEid(static_cast<uint32_t>(adoptEid), /*wantNpc=*/true)
                          : nullptr;
        if (ghost) {
            if (coop::npc_mirror::AdoptExistingNpcAsMirror(ghost, static_cast<uint32_t>(eid), classW)) {
                UE_LOGI("kerfur_convert[client]: adopted parked turn-on ghost as NPC mirror eid=%u (by eid)", eid);
                return;
            }
            // adopt failed (dup-eid race) -> fall through to fresh-spawn beside it (cleanup reaps ghost)
        }
        void* actorClass = R::FindClass(classW.c_str());
        if (!actorClass) {
            UE_LOGW("kerfur_convert[client]: cannot resolve class '%ls' for NPC materialize eid=%u",
                    classW.c_str(), eid);
            return;
        }
        coop::npc_mirror::SpawnFreshNpcMirror(classW, actorClass, static_cast<uint32_t>(eid),
                                              lx, ly, lz, rp, ry, rr);
    } else {
        const std::string key8 = "coopkerfur#" + std::to_string(static_cast<unsigned>(eid));
        // D2 relay: if our own turn-off parked a prop ghost tagged with the converting eid, adopt THAT exact
        // actor as the host-eid mirror DIRECTLY (no fuzzy position match -> no miss -> no orphan-destroy flash).
        // The ghost is already frozen (claim) and at the conversion site; snap it to the authoritative pose.
        if (adoptEid != coop::element::kInvalidId) {
            if (void* ghost = TakeParkedGhostByEid(static_cast<uint32_t>(adoptEid), /*wantNpc=*/false)) {
                std::wstring key8w(key8.begin(), key8.end());
                coop::remote_prop::RegisterPropMirror(eid, ghost, key8w, classW, /*senderSlot=*/0,
                                                      /*rebindInPlace=*/false);
                ue_wrap::engine::SetActorLocation(ghost, ue_wrap::FVector{lx, ly, lz});
                ue_wrap::engine::SetActorRotation(ghost, ue_wrap::FRotator{rp, ry, rr});
                // HANG-IN-AIR ROOT FIX (2026-06-30, hands-on 14:42). The claim NO LONGER freezes this ghost
                // (ClaimConversionGhosts dropped the SetActorSimulatePhysics(false) -- it was RULE-2 baggage from
                // the Gap-I-1 fuzzy era; the eid adopt finds the ghost wherever it settled). So the off-prop
                // already FELL + settled by LOCAL physics during the host round-trip (SP-like, no air-hang); the
                // adopt is just the authoritative-pose snap above (sub-meter, same floor). No physics toggle here.
                UE_LOGI("kerfur_convert[client]: adopted parked turn-off ghost as PROP mirror eid=%u (by eid -- "
                        "deterministic, no fuzzy miss; settled by local physics, snapped to host pose)", eid);
                return;
            }
        }
        // No eid-tagged ghost (a non-initiator peer, or the rejected-restore path): drive the PropSpawn
        // receiver (exact-key/fuzzy-adopt/fresh-spawn + mirror register). Synthetic deterministic key from eid.
        coop::net::PropSpawnPayload sp{};
        sp.className.len = 0;
        for (size_t i = 0; i < classW.size() && i < 63; ++i)
            sp.className.data[sp.className.len++] = static_cast<char>(classW[i]);
        sp.key.len = 0;
        for (size_t i = 0; i < key8.size() && i < 31; ++i) sp.key.data[sp.key.len++] = key8[i];
        sp.propName.len = 0;
        sp.locX = lx; sp.locY = ly; sp.locZ = lz;
        sp.rotPitch = rp; sp.rotYaw = ry; sp.rotRoll = rr;
        sp.scaleX = sp.scaleY = sp.scaleZ = 1.f;
        sp.physFlags = 0;
        sp.chipType = 0;
        sp.elementId = static_cast<uint32_t>(eid);
        // deferKerfur=false: this is the CONVERSION materialize -- the client's parked ghost is ready
        // NOW (claim/adopt), so OnSpawn must run the inline fuzzy-adopt, NOT defer to the join-time
        // kerfur_prop_adoption poll (which is for un-converted save-loaded twins). K-6.
        // ROOT-1 FIX (2026-06-29, 10:30 hands-on -- the turn-off DOUBLE): the prior call
        // `OnSpawn(sp, 0, localPlayer, /*deferKerfur=*/false)` bound `false` to param #4 (fromConvert),
        // leaving deferKerfur at its DEFAULT true -> this synthetic kerfur PropSpawn ARMED the join-window
        // fuzzy adopter (remote_prop_spawn.cpp:763 -> kerfur_prop_adoption::Arm) instead of adopting inline.
        // That adopter can't match the local conversion ghost (fresh per-load key fails its anti-collision
        // gate) -> it fresh-spawned a SECOND prop beside the orphaned ghost -> the visible double. This is the
        // IDENTICAL arg-slot bug already fixed at kerfur_prop_adoption.cpp:171-179 (the "OBS-2 ROOT FIX"),
        // never fixed at this 2nd call site. Pass fromConvert=false EXPLICITLY so deferKerfur=false lands right.
        coop::remote_prop_spawn::OnSpawn(sp, /*senderSlot=*/0, localPlayer,
                                         /*fromConvert=*/false, /*deferKerfur=*/false);
    }
}

// ---- conversion-ghost CLAIM + ADOPT (2026-06-15 hands-on) ---------------------
// The client's own toggle spawns the NEW-form actor through the PE-invisible
// EX_CallMath path -- a live UNTRACKED ghost we cannot prevent. DESTROYING it (the
// earlier sweep) only traded the dupe for a destroy/respawn pop AND still lost the
// race to a grab: an untracked ghost, once grabbed, is broadcast as a NEW client
// entity the host mirrors -> the cascade behind "A LOT of dupes on host" (proven by
// eid 44116: a turn-off ghost prop, grabbed, broadcast client-range, then toggled).
// ROOT FIX: never leave an untracked ghost. The instant the poll detects our toggle,
// CLAIM the ghost (park the NPC so it stops local AI/physics; the PROP is left to settle by local physics),
// then ADOPT it as the host mirror when the authoritative entity arrives -- BY EXACT srcEid (tagged below):
//   - NPC (turn-on):  npc_mirror::OnEntitySpawn adopts the ghost by EXACT eid
//                     (TakeParkedGhostByEid(convertFromEid), AdoptExistingNpcAsMirror) -- no respawn pop.
//   - PROP (turn-off): MaterializeKerfurMirror adopts the ghost by EXACT eid (TakeParkedGhostByEid). The prop
//                     is NOT frozen (2026-06-30 14:42): the old SetActorSimulatePhysics(false) held it inside
//                     Gap-I-1's 30 cm FUZZY window, but the eid adopt finds it wherever it settled -- so the
//                     freeze was RULE-2 baggage that pinned it at the NPC's standing height (the air-hang). It
//                     now falls + settles by LOCAL physics like single-player; the adopt snaps it to the host pose.
// A ghost the host never confirms (sentient-kerfur reject / dropped request) is
// destroyed by CleanupParkedGhosts after a timeout, so it can never be grabbed.
struct ParkedGhost {
    void*    actor;
    int32_t  idx;
    uint8_t  toProp;  // 1 = prop ghost (turn-off result), 0 = NPC ghost (turn-on result)
    uint32_t srcEid;  // (D2 relay, 2026-06-27) the eid of the mirror that converted -- the host's KerfurConvert
                      // carries this as oldEid, so the ghost is adopted by STABLE EID, not fuzzy position
                      // (the position-match miss = the 15:01 "appears-freezes-disappears" flash root).
    std::chrono::steady_clock::time_point armed;
};
std::vector<ParkedGhost> g_parkedGhosts;  // GT-only (poll + OnEntitySpawn are both game thread)

// TakeParkedGhostByEid is a PUBLIC API (declared in the header) -> defined in the namespace proper
// (after the anonymous namespace closes), NOT here. g_parkedGhosts (anon-ns file static)
// is still visible there within this TU.

// Collect the actors bound to WIRE MIRROR Elements (IsMirror()==true) of the requested type(s).
// THIS is the authoritative "the host owns this actor" signal -- NOT the per-actor
// prop_element_tracker / npc_sync reverse maps. A Gap-I-1 fuzzy-match rekey (prop adopt) and an
// npc_mirror Install (NPC adopt) bind the ghost as a MIRROR Element but do NOT update those reverse
// maps, so the maps falsely read a just-adopted ghost as untracked. (That exact staleness made the
// cleanup destroy a freshly Gap-I-1-adopted prop -> the poll saw the prop "die" -> a SPURIOUS
// turn-on -> churn; caught by the 2026-06-15 kerfurtoggle autonomous test.) Built once per caller
// (small vector-pointer copy under the manager's leaf mutex), then O(1) lookups.
void CollectMirrorActors(bool wantProp, bool wantNpc, std::unordered_set<void*>& out) {
    if (wantProp) {
        std::vector<coop::element::Prop*> v;
        coop::element::MirrorManager<coop::element::Prop>::Instance().Snapshot(v);
        for (auto* el : v)
            if (el && el->IsMirror() && el->GetActor()) out.insert(el->GetActor());
    }
    if (wantNpc) {
        std::vector<coop::element::Npc*> v;
        coop::element::MirrorManager<coop::element::Npc>::Instance().Snapshot(v);
        for (auto* el : v)
            if (el && el->IsMirror() && el->GetActor()) out.insert(el->GetActor());
    }
}

}  // namespace

// Find + park/freeze the client's own just-spawned conversion ghost(s): UNTRACKED live
// kerfur(s) of the new form near `pos`. NPC turn-on spawns one kerfurOmega; prop
// turn-off can drop the prop AND its carried floppy -> claim all near. One cold-path
// GUObjectArray walk, only on a detected client toggle.
void ClaimConversionGhosts(uint32_t srcEid, bool wantNpc, float x, float y, float z) {
    void* bases[2];
    size_t nBases = 0;
    if (wantNpc) {
        if (g_kerfurNpcClass) { bases[0] = g_kerfurNpcClass; nBases = 1; }
    } else if (g_kerfurPropClass) {
        bases[0] = g_kerfurPropClass; nBases = 1;
        if (g_floppyClass) { bases[1] = g_floppyClass; nBases = 2; }
    }
    if (nBases == 0) return;
    // The host's authoritative kerfurs of this form are WIRE MIRRORS -- skip them; the only
    // non-mirror kerfur of the new form at the conversion site is our own local ghost.
    std::unordered_set<void*> mirrors;
    CollectMirrorActors(/*wantProp=*/!wantNpc, /*wantNpc=*/wantNpc, mirrors);
    const int32_t n = R::NumObjects();
    constexpr float kR2 = 500.f * 500.f;  // the new form spawns at the kerfur's own transform
    // K-6 over-claim fix: the conversion verb spawns EXACTLY ONE actor per base class (1
    // prop_kerfurOmega body +, for turn_off, optionally 1 prop_floppyDisc) AT the conversion position
    // (RE redesign sec 3). So claim ONLY the NEAREST untracked candidate PER base class -- NOT every
    // kerfur prop within 5 m. The old "claim all within radius" grabbed the player's PRE-EXISTING
    // save-loaded kerfur props and the 4 s orphan-reaper then DESTROYED them ("kerfur props removed
    // entirely"; proven from the 2026-06-16 client log "claimed 4 local conversion ghost(s)"). The
    // join-time kerfur_prop_adoption makes those pre-existing props mirrors (-> excluded just below),
    // so this nearest-only rule is the belt-and-suspenders for a conversion racing the adoption poll.
    void* bestObj[2] = {nullptr, nullptr};
    float bestD2[2]  = {kR2, kR2};
    for (int32_t i = 0; i < n; ++i) {
        void* obj = R::ObjectAt(i);
        if (!obj) continue;
        void* cls = R::ClassOf(obj);
        if (!cls) continue;
        if (!R::IsLive(obj)) continue;
        if (R::NameStartsWith(R::NameOf(obj), L"Default__")) continue;
        if (mirrors.count(obj)) continue;  // a host wire mirror -- not our ghost
        int b = -1;  // which base class this actor is (nearest tracked per base)
        for (size_t bi = 0; bi < nBases; ++bi)
            if (R::IsDescendantOfAny(cls, &bases[bi], 1)) { b = static_cast<int>(bi); break; }
        if (b < 0) continue;
        const ue_wrap::FVector loc = ue_wrap::engine::GetActorLocation(obj);
        const float dx = loc.X - x, dy = loc.Y - y, dz = loc.Z - z;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < bestD2[b]) { bestD2[b] = d2; bestObj[b] = obj; }
    }
    const auto now = std::chrono::steady_clock::now();
    int claimed = 0;
    for (size_t bi = 0; bi < nBases; ++bi) {
        void* obj = bestObj[bi];
        if (!obj) continue;
        if (wantNpc) {
            ue_wrap::puppet::DisableCharacterTicks(obj);  // park: the streamed pose becomes authoritative
            ue_wrap::kerfur::NeutralizeAiTimers(obj);      // stop the kerfur AI timers on the ghost
        }
        // PROP (turn-off) ghost: do NOT freeze its physics. The old `SetActorSimulatePhysics(false)` here held
        // the dropped prop inside the Gap-I-1 30cm FUZZY window; adopt is now DETERMINISTIC by srcEid
        // (TakeParkedGhostByEid finds it by eid wherever it settled), so the freeze was RULE-2 baggage that
        // pinned the prop at the NPC's STANDING height for the ~0.4s host round-trip = the "hang in the air then
        // snap" (2026-06-30 14:42 hands-on). Let it fall + settle by LOCAL physics like single-player;
        // MaterializeKerfurMirror rebinds it in place by eid (a sub-meter snap to the host's settled pose).
        g_parkedGhosts.push_back(ParkedGhost{obj, R::InternalIndexOf(obj),
                                             wantNpc ? uint8_t(0) : uint8_t(1), srcEid, now});
        ++claimed;
    }
    if (claimed)
        UE_LOGI("kerfur_convert[client]: claimed %d local conversion ghost(s) (%s) nearest-per-class near (%.0f,%.0f,%.0f) -- parked for host adoption",
                claimed, wantNpc ? "NPC turn-on" : "prop turn-off", x, y, z);
}

// Reap parked ghosts each poll: drop ones that DIED or got ADOPTED (now tracked by the
// host's authoritative entity), and destroy ones the host never confirmed after the
// timeout (a rejected sentient-kerfur / dropped request) so they cannot be grabbed into
// a client-eid dupe. Cheap no-op when the list is empty. Game thread.
void CleanupParkedGhosts() {
    if (g_parkedGhosts.empty()) return;
    bool haveProp = false, haveNpc = false;
    for (const auto& g : g_parkedGhosts) { if (g.toProp) haveProp = true; else haveNpc = true; }
    // "Adopted" == bound as a WIRE MIRROR (Gap-I-1 RegisterPropMirror / npc_mirror Install). The
    // per-actor reverse maps do NOT reflect that, so we ask the MirrorManager directly.
    std::unordered_set<void*> mirrors;
    CollectMirrorActors(haveProp, haveNpc, mirrors);
    const auto now = std::chrono::steady_clock::now();
    constexpr long kOrphanTimeoutMs = 4000;  // host replies within ~RTT; 4 s un-adopted == orphan
    for (auto it = g_parkedGhosts.begin(); it != g_parkedGhosts.end();) {
        void* actor = it->actor;
        if (!actor || !R::IsLiveByIndex(actor, it->idx)) { it = g_parkedGhosts.erase(it); continue; }
        if (mirrors.count(actor)) { it = g_parkedGhosts.erase(it); continue; }  // adopted as a wire mirror -> success
        const long ageMs = static_cast<long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - it->armed).count());
        if (ageMs >= kOrphanTimeoutMs) {
            UE_LOGI("kerfur_convert[client]: orphan conversion ghost actor=%p (%s) un-adopted after %ldms -- destroying (host rejected/dropped the toggle)",
                    actor, it->toProp ? "prop" : "NPC", ageMs);
            if (it->toProp) coop::prop_lifecycle::DestroyLocalProp(actor, /*deferred=*/false);
            else coop::npc_mirror::DestroyLocalNpcActor(actor);
            it = g_parkedGhosts.erase(it);
            continue;
        }
        ++it;
    }
}

void OnKerfurConvert(const coop::net::KerfurConvertBroadcastPayload& p, void* localPlayer) {
    auto* s = LoadSession();
    if (!s) return;
    // CLIENT-only apply: the host already converged inline (silent host register + KerfurConvert
    // send), so it never applies its own broadcast. The slot-0 (host-origin) gate is in
    // event_dispatch_state.
    if (s->role() != coop::net::Role::Client) return;
    const coop::element::ElementId oldEid = static_cast<coop::element::ElementId>(p.oldEid);
    const coop::element::ElementId newEid = static_cast<coop::element::ElementId>(p.newEid);
    const bool toNpc = (p.toForm == 0);  // toForm: 0 = NPC (turn-on), 1 = prop (turn_off)
    std::wstring classW;
    classW.reserve(p.newClassName.len);
    for (uint8_t i = 0; i < p.newClassName.len && i < 63; ++i)
        classW.push_back(static_cast<wchar_t>(static_cast<unsigned char>(p.newClassName.data[i])));

    if (p.rejected) {
        // Host refused (sentient/kill). If our old-form mirror still exists, keep it (no-op). If we
        // optimistically converted locally (the poll detected our own conversion + parked a ghost) the
        // old mirror is gone -> RESTORE it at oldEid in the form-to-restore (toForm = the OLD form).
        if (coop::element::Registry::Get().Get(oldEid)) {
            UE_LOGI("kerfur_convert[client]: KerfurConvert rejected K=%u oldEid=%u -- mirror intact, no-op",
                    p.kerfurId, p.oldEid);
            return;
        }
        UE_LOGI("kerfur_convert[client]: KerfurConvert rejected K=%u oldEid=%u -- restoring local mirror (converted optimistically)",
                p.kerfurId, p.oldEid);
        // Restore the OLD form fresh (kInvalidId = no eid-keyed adopt: the parked ghost is the rejected NEW
        // form, wrong form to adopt; the cleanup timeout reaps it).
        MaterializeKerfurMirror(toNpc, oldEid, coop::element::kInvalidId, classW, p.locX, p.locY, p.locZ,
                                p.rotPitch, p.rotYaw, p.rotRoll, localPlayer);
        return;
    }

    // SUCCESS. Destroy the OLD-form mirror at oldEid (old form = the opposite of toForm), then
    // materialize/adopt the NEW form at the authoritative newEid.
    if (!toNpc) {
        // new form is prop -> old form was NPC
        coop::net::EntityDestroyPayload dp{};
        dp.elementId = static_cast<uint32_t>(oldEid);
        coop::npc_mirror::OnEntityDestroy(dp);
    } else {
        // new form is NPC -> old form was prop (eid-only teardown of the old prop mirror). K-5: evict
        // the old prop mirror from the client held-pose map BEFORE OnDestroy frees its actor (the map
        // self-heals on read too, but evicting on the known teardown keeps it tight).
        if (auto* oldEl = coop::element::Registry::Get().Get(oldEid))
            coop::kerfur_entity::ForgetKerfurPropMirror(oldEl->GetActor());
        coop::net::PropDestroyPayload dp{};
        dp.key.len = 0;
        dp.elementId = static_cast<uint32_t>(oldEid);
        coop::remote_prop::OnDestroy(dp, localPlayer);
    }
    // D2 relay: adopt the initiator's parked ghost by the converting eid (oldEid) -- deterministic, no flash.
    MaterializeKerfurMirror(toNpc, newEid, /*adoptEid=*/oldEid, classW, p.locX, p.locY, p.locZ,
                            p.rotPitch, p.rotYaw, p.rotRoll, localPlayer);
    // NOTE (scope A v1): the off->active dup RETIRE is NOT triggered here. A join-window turn-on's
    // KerfurConvert SendReliable FAILS (the joiner isn't ready for reliable gameplay mid save-transfer --
    // hands-on 16:37), so this apply never runs for the case that needs the retire. The retire is armed
    // from the npc EntitySpawn instead (npc_mirror::OnEntitySpawn, the channel that reaches the joiner)
    // and run by the quiescence sweep below. A steady-state turn-on (this path) already retires the old
    // form via the OnDestroy(oldEid) above -- nothing to retire here.
    UE_LOGI("kerfur_convert[client]: applied KerfurConvert K=%u %s oldEid=%u -> newEid=%u class='%ls'",
            p.kerfurId, toNpc ? "->NPC(turn-on)" : "->prop(turn_off)", p.oldEid, p.newEid, classW.c_str());
}

// (D2 relay) Take (find + remove) the parked ghost tagged with `srcEid` of the requested form, returning its
// actor or nullptr. Deterministic eid correlation replaces the fuzzy position adopt -> the local ghost is
// ALWAYS adopted by the host's authoritative KerfurConvert(oldEid) / EntitySpawn(convertFromEid), never
// orphaned -> destroyed (no flash). PUBLIC (header-declared) -> external linkage, so defined here outside the
// anonymous namespace; g_parkedGhosts (anon-ns file static) is still visible within this TU.
void* TakeParkedGhostByEid(uint32_t srcEid, bool wantNpc) {
    const uint8_t wantProp = wantNpc ? uint8_t(0) : uint8_t(1);
    for (auto it = g_parkedGhosts.begin(); it != g_parkedGhosts.end(); ++it) {
        if (it->srcEid != srcEid || it->toProp != wantProp) continue;
        void* actor = it->actor;
        const int32_t idx = it->idx;
        g_parkedGhosts.erase(it);
        return (actor && R::IsLiveByIndex(actor, idx)) ? actor : nullptr;
    }
    return nullptr;
}

void SetSession(coop::net::Session* session) {
    g_session.store(session, std::memory_order_release);
}

void SetClasses(void* npcClass, void* propClass, void* floppyClass) {
    g_kerfurNpcClass  = npcClass;
    g_kerfurPropClass = propClass;
    g_floppyClass     = floppyClass;
}

void OnDisconnect() {
    g_parkedGhosts.clear();  // GT-only conversion-ghost claim list
}

}  // namespace coop::kerfur_convert_client
