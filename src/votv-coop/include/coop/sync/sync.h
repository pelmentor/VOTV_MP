// coop/sync/sync.h -- the consolidated world-sync module facade.
//
// THE single conceptual sync engine (sync-consolidation refactor 2026-06-27,
// research/findings/sync-consolidation-refactor-PLAN-2026-06-27.md). One module,
// one identity owner, one create-or-adopt, one reconcile, one destroy funnel,
// one authority model -- behind this clean API. Callers (event dispatch, net
// pump, the engine hooks) talk to sync THROUGH this surface; the eid-bind /
// divergence-valve / purge-escalation / mirror-teardown complexity is private.
//
// FOUNDATION DISCOVERY (plan section 1): the identity owner (`SyncRegistry`) is
// ALREADY played by `element::Registry` (the sole eid<->actor array, host/peer
// ranges, RegisterMirror/Get/FreeId/EidForActor) + `MirrorManager<T>` (the
// per-kind manager) + `Element` (the entity). So this module does NOT rebuild a
// registry -- it adds the missing seams on top: CreateOrAdopt (collision-
// reconcile create), SyncReconcile (the non-one-shot identity reconcile),
// SyncDestroyQueue (deferred GT-safe retire), SyncAuthority (relay, not predict).
//
// ASSEMBLY STATE (live): sync_reconcile (DONE) + sync_create CreateOrAdopt
// (building). Router / destroy funnel / authority forthcoming -- see the plan's
// AS-BUILT ledger. This umbrella re-exports each piece as it lands so callers
// include ONE header.

#pragma once

#include "coop/sync/sync_reconcile.h"   // RunIdentityReconcile / OnReconcileTick (the non-one-shot reconcile)
#include "coop/sync/sync_create.h"      // CreateOrAdopt (the one collision-reconcile create/bind path)
