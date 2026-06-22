# Increment 2 (chipPile CLIENT-grab) — the FULL CHAIN, AS-BUILT + [V harness]

**Date:** 2026-06-23. **Status:** AS-BUILT, **[V harness]** (the autonomous log-truth harness asserted the
whole chain against real 2-peer smoke logs — NOT a hands-on). Proto **v85**. SUPERSEDES the host-side
`votv-increment2-clientgrab-host-side-DESIGN-2026-06-22.md` (that was the host arm only, verified via a
synthetic intent). This is the complete client-initiated chain: a CLIENT aims at a host-mirrored pile,
grabs it, carries it, throws it, and it re-piles — every step verified.

## What a client now does (end to end)

1. **Aim + grab (recognition).** The client's `OnPileGrabPre` (the InpActEvt_use PRE observer, the real
   E-press seam) runs a **camera-ray cone** (`trash_proxy::EidForAimedPileProxy(camLoc, camFwd, 400cm,
   minDot=0.94)`) — the most-centered PILE-form proxy within reach of the view-camera forward. On a hit it
   `trash_channel::SendGrabIntent(eid)`. A player already carrying instead `SendThrowIntent` (the E-press
   toggle). No suppress-native needed (the bare AStaticMeshActor proxy fails every native `int_player_C`
   cast, so the local grab no-ops on its own).
2. **Host executes the grab** (already shipped 2026-06-22, `81e8e687`): `OnGrabIntent` validates (not
   carrying, sender holds nothing, the eid resolves to a live chipPile) → runs `playerGrabbed(Player=puppet)`
   on the requester's puppet → broadcasts `PropConvert{kToClump}` → registers `puppet_carry_drive`.
3. **Carry visibility (NEW, v85 — the host-authoritative per-eid pose stream).** The puppet's tick does NOT
   drive the PHC (probe `32ccd1bc`), so `puppet_carry_drive::Tick` kinematically positions the clump at the
   puppet hand AND publishes its pose into a **host-originated `MsgType::TrashCarryPose` batch**
   (`TrashClumpPoseSnapshot{eid,xyz,rot,ctx}`). Every client (including the grabber) drains it in
   `trash_clump_pose_stream::TickApplyAndDrive` and drives the proxy through a per-eid `ActiveDrive` (the
   extracted fixed-delay snapshot interp) — so the clump renders MOVING on all peers.
4. **Throw (NEW, v85).** The carrying client's E-press → `SendThrowIntent` → host `OnThrowIntent`:
   `ReleaseMainPlayerGrabIfHolding(puppet)` (clears `grabbing_actor` so the re-pile gate reads not-held) →
   `SimulatePhysics(true)` → `SetPhysicsVelocity(aim*600 + 400 up)` (AFTER SimulatePhysics, per the RE) →
   `NoteThrown` (stop hand-drive, keep streaming the FLIGHT pose). The clump flies under physics, its OWN
   ground-hit ubergraph re-piles (`BeginDeferred(chipPile)`) → the existing thunk → `OnHostConvert{kToPile}`
   → settle → LAND COMMIT → the client snaps the proxy to the authoritative landed pile.

## The two non-obvious facts this build is built on (the gating discoveries)

- **A bare AStaticMeshActor proxy can NEVER become `lookAtActor`.** `mainPlayer.json LookAtFunction @3250`
  hard-filters the trace hit on `DoesImplementInterface(hit, int_player_C)`; our stock-engine proxy fails it.
  So "read lookAtActor / lookatActorCurrent to recognize the aimed pile" is a DEAD END — confirmed at
  runtime: the proxy's QueryOnly collision never registered in the game's `TraceTypeQuery1` trace either
  (the worn pile mesh has no simple collision body; the real pile is hit via a separate `garbageCollider`
  component). **The trace approach is RETIRED (RULE 2).** Recognition is the camera-ray cone — reliable, no
  asset-collision dependency. (`votv-clientgrab-interaction-trace-RE-2026-06-22` has the full LookAtFunction
  + ubergraph trace; the collision premise it raised was disproven by the harness `trace-gate hit=0`.)
- **A client only drives pose SLOT 0, and the relay never echoes a pose to its origin**
  (`remote_prop.cpp:603-609`, `session_relay.cpp:37`). So a host-driven puppet-held clump that the GRABBER
  itself must see CANNOT ride the per-slot/relay pose pipeline. It must be HOST-ORIGINATED — hence the new
  per-eid `TrashCarryPose` batch (the `StoreRemoteWorldActorBatch` pattern). This scales to N simultaneous
  client grabs (one per peer); the per-slot model could carry only one host-authoritative moving prop.

## As-built file map (v85)

- **protocol.h** — `TrashClumpPoseSnapshot` (32 B: eid + xyz + pitch/yaw/roll + ctx + pad),
  `MsgType::TrashCarryPose=34`, `kMaxTrashCarryBatchEntries=8`; `ThrowIntentPayload` simplified to `{eid}`
  (the host derives the throw direction from the puppet's synced aim — RULE 2, no client-velocity field);
  `kProtocolVersion=85`.
- **coop/active_drive.h** (NEW) — the fixed-delay snapshot interp (ActiveDrive + BeginLerpToPose /
  AdvanceLerp / ResetDriveState / NowMs / LerpAngle), EXTRACTED from remote_prop.cpp (1375→1222 LOC) so the
  carry stream reuses the SAME proven interp (RULE 2). Zero behaviour change.
- **coop/net/session_trashcarry.cpp** (NEW) + session.h/.cpp — the `TrashCarryPose` batch send/receive
  (clone of session_worldactor.cpp; identical mutex discipline).
- **coop/puppet_carry_drive.{cpp,h}** — `Tick(Session&)` drives the hand pose AND publishes the carry/flight
  batch; a `flying` flag + `NoteThrown` for the throw arc; the settle-window guard (a clump consumed by a
  re-pile with a pending settle must NOT `ReleaseClientHold` — that would cancel the settle); a
  `g_wasStreaming` idle gate (no per-tick session lock when nobody carries).
- **coop/trash_clump_pose_stream.{cpp,h}** (NEW) — CLIENT drain + per-eid `unordered_map<eid, ActiveDrive>`
  + ctx-gate (`IsInboundStreamCtxFresh`, the pile-jump/double-grab-cue guard) + teardown.
- **coop/trash_channel.{cpp,h}** — `OnThrowIntent` (the host throw); the CLIENT carry-state toggle
  (`g_clientPendingGrab`/`g_clientCarry`, `SendGrabIntent`/`SendThrowIntent`/`NoteClientConvertObserved`/
  `ClientCarryEid`/`ClearClientCarry`); **`ReleaseClientHold(Session&, E)` now broadcasts `PropDestroy(eid)`**
  on a carry abort (audit HIGH fix — see below).
- **coop/trash_proxy.{cpp,h}** — `EidForAimedPileProxy` (the camera-ray cone) + `ProxyActorForEid` (the
  pose-stream target resolver); `RetireProxy` evicts the per-eid carry drive; the QueryOnly collision +
  `EidForPileProxyActor` + the `lookatActorCurrent` reflected-offset/accessor are RETIRED (RULE 2).
- **coop/trash_collect_sync.cpp** — `OnPileGrabPre` client branch = the camera cone + the grab/throw toggle
  + a self-heal (carrying-but-no-proxy → clear the stale toggle); `DebugSendThrowIntent` (harness).
- **coop/remote_prop.cpp** — `OnConvert` calls `ClearDriveForEid` (ToPile) + `NoteClientConvertObserved`;
  `OnDestroy` calls `ClearClientCarry` (the HIGH fix's client half).
- **event_dispatch_state.cpp** — the ThrowIntent dispatch case (MODULAR NOTE: this file is ~778 LOC; the
  NEXT trash kind, PileResyncRequest in phase 3, MUST extract the trash-intent cases to
  `event_dispatch_trash.cpp`, per the file's own standing note + the audit).

## The audit HIGH (found + fixed pre-handoff)

A clump LOST mid-carry (the clump actor dies without re-piling, or the puppet goes not-live) previously left
the CLIENT permanently stuck: `ReleaseClientHold` cleared host state but broadcast nothing, so the client's
`g_clientCarry` never cleared → every E-press routed to a throw of the dead eid (host-denied) → the client
could never grab again, and its proxy froze mid-air. **Fix:** `ReleaseClientHold(Session&, E)` now broadcasts
`PropDestroy(eid)` (the trash entity genuinely vanished on the host) → `OnDestroy` retires the proxy (drive
cleared) + `ClearClientCarry` (toggle cleared). Plus a client self-heal in `OnPileGrabPre`. Verified: the
normal flow shows 0 `ReleaseClientHold` / 0 DENIED (the carry ends via the clean land COMMIT).

## Harness verdict (the autonomous log-truth proof)

`VOTVCOOP_RUN_GRAB_INTENT_TEST=1 python tools/mp.py smoke` — the client teleports facing a mirrored pile,
injects a REAL InpActEvt_use (the camera cone recognizes it), carries ~6 s re-facing, injects InpActEvt_use
again (the throw toggle), holds for the re-pile. `tools/pile-test-assert.ps1` **VERDICT PASS (0 CRITICAL
fail)**, deployed SHA `BB94A120A969A51E`, proto v85. The decisive invariants:
`clientgrab-recognition` (2 camera-cone fires, the REAL E-press path — NOT the bypass) ·
`grab-intent-roundtrip` (1 RECEIVED→SUCCESS) · `puppet-hand-drive` (6 carry-publish) ·
`carry-pose-published` (7 HOST PUBLISH) · `carry-pose-applied` (9 CLIENT APPLY) ·
`throw-intent-roundtrip` (1 RECEIVED→SUCCESS) · `throw-repile` (1 RE-PILE thunk + 1 LAND COMMIT) ·
`client-render-matches-host` (drift 0 cm) · `no-crash-or-error`. Client log confirms the toggle:
`CLIENT E-PRESS aimed at pile proxy (camera-ray cone)` → `carry CONFIRMED` → `E-PRESS while carrying` →
`recv convert LAND`.

## Known limitations / the FAITHFUL next increment

The camera-ray cone ignores **wall occlusion** (you can grab a pile you're aimed at through a thin wall),
and the mirrored pile proxy still does **not block player movement** (the pre-existing phase-1
walk-through-pile regression). BOTH are cleanly + faithfully solved by a future **`garbageCollider`-analog
SHAPE component** on the proxy (a runtime-attached USphere/UBox set to block the pawn + the interaction
channel) — which would also restore the occlusion-correct trace recognition. That is the documented next
increment; it needs its own RE (can we attach + register a component on a runtime AStaticMeshActor via
reflection?) + a probe. Also deferred: the WHOOSH-on-throw cue; the `event_dispatch_trash.cpp` extraction.
