// coop/kerfur_reconcile.h -- the join-window kerfur off->active dup RETIRE (scope A, 2026-06-24).
//
// THE forward off->active dup root (census-pinned 2026-06-24, docs/kerfur/03 + 07): a kerfur that was
// OFF in the transferred save but the host turns ON in the join-load window. The host no longer expresses
// it as an off-prop (it is now an active NPC on the npc channel), so the joining client's OWN save-loaded
// local off-prop is never adopted/claimed as a host mirror -- it SURVIVES beside the active NPC (the dup,
// census 15:43: an UNCLAIMED prop_kerfurOmega_C). The divergence sweep does NOT retire it (verified on a
// clean bracket: it doomed 80 trash + 1 swinger + 1 mushroom but NOT the kerfur off-prop -- a kerfur
// off-prop that got fuzzy-adopted is a host MIRROR, sweep-exempt; one that stayed local should be doomed
// but the bracket races / it was mis-claimed). So we retire it explicitly, keyed by the SAVE-TIME position
// the host carries on the KerfurConvert (kerfur_entity::BindFormActor stamps it from g_blobKerfurXforms).
//
// This is L1 pile_reconcile's shape applied to the kerfur object channel (the THIRD mirror-identity
// window-race instance -- save-time exact key + post-quiescence reconcile + position uniqueness). It is
// kept in its own file (RULE 2026-05-25 one-feature-per-file); the shared mirror-identity layer is
// extracted AFTER this works (fix-then-generalize, [[feedback-fix-then-generalize-mirror-identity]]).
//
// CLIENT-only. Game-thread only (the KerfurConvert apply + the divergence sweep both run on the GT drain).

#pragma once

#include "coop/element/element.h"  // ElementId
#include "ue_wrap/types.h"         // ue_wrap::FVector

namespace coop::kerfur_reconcile {

// Drop all pending retires (bracket open/close + session end). Mirrors pile_reconcile::Reset.
void Reset();

// A host kerfur just turned ON in the join window (KerfurConvert toForm=NPC carried `saveTimePos`).
// Retire the client's STALE LOCAL off-prop (a non-mirror live prop_kerfurOmega_C within 1 cm of the
// save-time key) -- the off-prop the host no longer expresses, the visible dup beside the new active NPC.
// Returns the number retired (0 or 1; an ambiguous >1 within 1 cm is refused -- never destroy the wrong
// one). On a MISS (the local off-prop has not async-loaded yet -- KerfurConvert can arrive before the
// client's load-tail drains), call ArmPendingRetire so the post-quiescence sweep retries. `kerfurId` is
// the broadcast K (the pending-retire key). Game thread.
int RetireLocalOffPropAtSaveTime(uint32_t kerfurId, const ue_wrap::FVector& saveTimePos);

// Record a save-time retire that MISSED at KerfurConvert-apply (its local off-prop had not loaded yet),
// for a retry at the post-quiescence sweep where the late native is present. Idempotent per kerfurId.
void ArmPendingRetire(uint32_t kerfurId, const ue_wrap::FVector& saveTimePos);

// Post-quiescence retry of the save-time retires that MISSED at apply. Call from RunDivergenceSweep_
// (which runs ONLY after load-tail quiescence) next to pile_reconcile::SweepReconcileSaveTimeTwins. For
// each pending (K -> save-time key) a FRESH GUObjectArray walk finds the now-loaded LOCAL off-prop within
// 1 cm and retires it. SAFETY: a >50%-of-live-local-off-prop-kerfurs abort-valve (mirrors the sweep's own
// valve) refuses a removal that big = a racing/incomplete bracket. Returns the count retired; clears the
// pending set. Game thread.
int SweepReconcileSaveTimeKerfurs();

}  // namespace coop::kerfur_reconcile
