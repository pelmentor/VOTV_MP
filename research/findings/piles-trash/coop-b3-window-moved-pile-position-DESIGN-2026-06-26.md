# b3 — window-moved save-authoritative pile position: root + design (2026-06-26)

**Status: AS-BUILT (commit `10284a8a`, proto v90, deployed `BA9459985119`) — hands-on PENDING.** RULE-1 root
fix for the eid-3144 stuck-pile tail from the 16:42 hands-on. Backed by 4 parallel read-only RE traces (host
replay/gate, client bind/apply, kerfur #1 sweep, MTA precedent) + the 16:42 logs. Continues
`coop-grab-throw-and-join-window-bind-RE-2026-06-26.md` (the b2 verdict). Audit SHIP (3-place wire-in complete,
host-only validation, thread-safe, no clean-join/b2/#1/carry regression, value-map no UAF).

**AS-BUILT wire-in (10 files, +211):** `protocol.h` (kProtocolVersion 89->90; `ReliableKind::PropSnapPos=81`;
`PropSnapPosPayload` 28B) · `session_lanes.h` (PropSnapPos->Lane::Bulk; NOT pre-world-sendable, NOT relayable) ·
`event_feed.cpp` (master-router -> HandleEntityEvent) · `event_dispatch_entity.cpp` (PropSnapPos case: host-only
+ finite/bounds/eid-range validation -> ArmPendingPosCorrection; if HasLoadTailQuiesced -> ApplyPendingPosCorrections)
· `save_transfer.{h,cpp}` (`FlushDivergedPilePositionsForSlot`: iterate g_blobPileXforms[slot], resolve host
actor via element Registry, position-compare >4cm, SendReliableToSlot) · `subsystems.cpp` (call in
ConnectReplayForSlot after TriggerForSlot) · `pile_reconcile.{h,cpp}` (`g_pendingPosCorrection`,
Arm/ApplyPendingPosCorrection, cleared in Reset()) · `remote_prop_spawn.cpp` (Apply at RunDivergenceSweep_ after
SweepReconcileSaveTimeTwins). Mid-carry pile at ready -> old eid resolves null -> skipped (carry stream owns it).

## 1. The root, sharpened by RE (deeper than "dropped convert")

chipPiles are **save-authoritative**: both peers load them from the identical save, the save-identity bind maps
client-native ↔ host-eid **by ordinal** (`save_identity_map`), and **no position is sent over the wire** for a
save-loaded pile. The connect-snapshot (`prop_snapshot::BuildPropSpawnPayload_`, current-position-bearing for
KEYED props + NPCs) **deliberately excludes keyless save-authoritative chipPiles** (sending them would dup the
pile both peers already loaded). Proven in the 16:42 client log: the connect `OnSpawn` stream (16:42:15) is all
keyed props; **there is NO PropSpawn for eid 3144** between the bind (16:42:12) and the client's own grab
(16:42:48).

So a pile's ONLY cross-peer position channel is its **PropConvert** (grab→move→land). When the host moves a pile
during the join window, that convert is dropped (the B2 pre-world gate — `IsPreWorldSendableKind`,
`session_lanes.h:165`; PropConvert is not on the allowlist, no retry). Because the pile is otherwise
save-authoritative, **nothing else carries the moved position** → the client's native stays at the stale save
position with no correction channel. eid 2309 was corrected ONLY because its convert was *delivered* (later in
the window, channel ready) → convert-beat-spawn → proxy → PROXY-WINS. eid 3144's convert was *dropped* → no
channel → stuck until the client interacted.

**The precise root:** the connect-snapshot — the one mechanism that catches a joiner up to the host's CURRENT
state at world-ready — has a **HOLE for save-authoritative piles that diverged in-window**. It carries current
positions for keyed props and NPCs, but not for the one class we excluded on the assumption "the save IS the
current state" — which is false when the host moved one during the join.

## 2. #1 (kerfur) is NOT subsumed — correction to the "one mechanism" framing

RE (kerfur #1 trace) shows #1 is a **CLEANUP, not a delivery**, and shares only the ROOT with b3, not the fix:
- The turned-ON kerfur's active NPC state IS delivered to the joiner — via the **EntitySpawn** channel in the
  connect replay (`npc_pose_host.cpp` → `npc_mirror::OnEntitySpawn` → `npc_adoption`). The connect-snapshot
  WORKS for NPCs. The dropped `KerfurConvert` only failed to tear down the leftover **stale off-prop**; #1's
  quiescence sweep destroys that artifact. (Two independent channels: active-state delivered; stale-artifact
  cleaned.)
- The pile case has **NO surviving position channel** (save-authoritative, convert dropped), so b3 must
  **DELIVER** the position, not clean up.

Same root (in-window host mutation + dropped reliable), **different fix shape** because the kerfur active-state
has a fallback channel (EntitySpawn) and the pile position does not. b3 does not retire #1; they are
complementary. (RULE-2 is not triggered — there is no parallel old+new mechanism for the same job.)

## 3. b3 design (primary recommendation: a position correction for diverged save-authoritative piles)

**Close the connect-snapshot's save-authoritative hole.** At the joiner's world-ready, the host detects
save-authoritative piles whose CURRENT position diverges from that joiner's save-time position (i.e. moved
in-window) and sends a lightweight position correction to that slot; the client snaps the bound native at the
existing quiescence sweep.

### Host side (reuse the established per-joiner catch-up)
- Hook: `subsystems::ConnectReplayForSlot(slot)` (`subsystems.cpp:169`) — fires once per joiner at
  `ClientWorldReady`, AFTER `MarkSlotWorldReady` opens the gate (so `SendReliableToSlot` succeeds). Add one call
  alongside the existing snapshot/broadcast fan-out: `FlushDivergedPilePositionsForSlot(slot)`.
- Divergence detect (per tracked chipPile eid): compare `GetActorLocation(current host actor)` vs the joiner's
  save-time position `save_transfer::TryGetSaveTimePileXform(slot, eid)` (`save_transfer.cpp:514`, already
  per-joiner from the blob). Diverged (>1 cm) → send. **Pure position-divergence is the filter** (directly
  answers "did this pile move relative to what THIS joiner loaded"), robust regardless of convert history;
  `CtxForEid>0` (`trash_channel.cpp:561`) is an optional cheap pre-filter (was ever converted).
- Send: a new reliable `PropSnapPos { uint32 eid; float locXYZ; float rotPYR; }` via
  `Session::SendReliableToSlot(slot, ...)` (`session.cpp:189`).

### Client side (apply at quiescence, where the native is bound + settled)
- New `ReliableKind::PropSnapPos` — 3-place wire-in per [[feedback-reliablekind-router-checklist]] (enum/payload
  + family dispatcher case + `event_feed.cpp` master-router).
- On receipt: arm `g_pendingPosCorrection[eid] = {loc,rot}` (do NOT apply on-receipt — the native may not be
  bound/loaded yet).
- Apply in the existing quiescence sweep (`remote_prop_spawn::TickClientReconcile` /
  `SweepReconcileSaveTimeTwins`, where load-tail is settled and the bind has registered the native): for each
  pending, `actor = ResolveLiveActorByEid(eid)`; if live, `SetActorLocation/Rotation(actor, pos)` + a drift-verify
  log (mirror the b2 SNAP line). Clear after apply.

### Why this shape (RULE-1, consistent with (X) native-authoritative)
- The native stays a native — identity preserved (save-authoritative), only the POSITION corrected. No re-spawn,
  no convert replay, no proxy morph, no Install/dedup collision (the trap Option A hits — see §4).
- Idempotent and safe for the already-corrected case: eid 2309 (already a proxy@moved via PROXY-WINS) →
  `SetActorLocation(proxy, moved)` → drift 0, harmless.
- MTA-validated: MTA does a FULL current-state push at the explicit ready signal reading live getters
  (`CEntityAddPacket`, `CGame.cpp:1438`), so in-window mutations are captured for free. b3 is that lesson scoped
  to the one class our (X) model excludes — delivered as a position delta on the existing identity, not a full
  re-spawn (we already have the identity from the save).

## 4. Alternatives considered (and why not)
- **Option A — re-send the full grab+land convert pair at world-ready** (Agent: zero new client code, the
  ToClump `morphBoundNative` hand-off + ToPile re-skin already work). Rejected as primary: it morphs the
  save-loaded native into a RUNTIME proxy (loses the native-authoritative nature for a pile that should stay
  save-loaded), causes a momentary clump flash, and churns. A lone re-sent ToPile (no grab) is WORSE — Agent 2
  proved it hits `RegisterPropMirror(rebindInPlace=false)` → `MirrorManager::Install` REJECTS the duplicate eid
  (the bind already occupies it) → an orphan proxy at moved + the native still at old = a NEW dup. So Option A
  must re-send BOTH converts, with the flash/churn cost.
- **Embed current-pos in the save-identity bind payload** (snap at bind time). Elegant (the bind already runs
  per-eid at world-ready) but couples position into the identity map and applies before load-tail settle.
  Option C's decoupled correction + quiescence apply is cleaner and reuses the proven arm-at-signal/apply-at-
  quiescence pattern.
- **(c) host-gate generalization — defer ALL in-window host mutations until joiner quiescence.** The grand
  root (no mutation sent pre-world → nothing to drop), but much larger and riskier (queuing/holding the host's
  own gameplay mutations). b3 is the minimal targeted slice; (c) stays the optional long-term generalization.

## 5. Open verification points (flag honestly, settle at build)
1. **Is the client's save-loaded native chipPile Movable?** If its mesh is Static mobility, `SetActorLocation`
   silently no-ops ([[lesson-runtime-staticmeshactor-must-be-movable]]) — the drift log catches it (a no-op
   reads back the OLD pos → large drift). A grabbable physics pile should be Movable, but verify.
2. **Apply timing** — the correction MUST apply after the bind registers the native (use the quiescence sweep,
   not on-receipt), else `ResolveLiveActorByEid` misses.
3. **Carrying-at-ready edge** — if the host is still CARRYING the pile at the joiner's world-ready, current pos
   is the transient carry pos; b3 would snap to it. The post-world carry stream / land convert then corrects it
   (delivered, channel up). Acceptable; the common case (moved+landed before ready) is correct.

## 6. Verify gate (next hands-on, after build)
Move 2+ piles EARLY in the join window (before the joiner's channel is ready, like 3144 at :05-06) so their
converts are dropped → on join, with NO interaction, the moved piles render at the moved position on the client;
the b3 drift log shows the snap. #1/#2 stay PASS (no dup, identity correct). Clean-join / grab / throw / E-drop /
LMB unregressed.

## Cited source map (from the 4 RE traces)
`session_lanes.h:165` (IsPreWorldSendableKind allowlist) · `session.cpp:189,254` (world-ready gate +
SendReliableToSlot) · `event_feed.cpp:140-162` (ClientWorldReady → MarkSlotWorldReady + ConnectReplayForSlot) ·
`subsystems.cpp:169` (ConnectReplayForSlot — the b3 host hook) · `prop_snapshot.cpp:256,301-359`
(BuildPropSpawnPayload_ current-pos + chipPile matchPos; keyed-prop snapshot) · `save_transfer.cpp:514`
(TryGetSaveTimePileXform per-joiner save-time pos) · `trash_channel.cpp:561` (CtxForEid) ·
`remote_prop.cpp:969,1048-1113,744` (OnConvert branches; RegisterPropMirror rebindInPlace) ·
`mirror_manager.h:105` (Install duplicate-eid reject — the Option-A trap) · `remote_prop_spawn.cpp:1304,1412,1509`
(TickClientReconcile / SweepReconcileSaveTimeTwins / HasLoadTailQuiesced — the b3 client apply hook) ·
`kerfur_reconcile.cpp:47-105,123-164` + `npc_mirror.cpp:177-213` + `kerfur_convert.cpp:590-596,879`
(#1 is cleanup, EntitySpawn delivers the active kerfur) · `reference/mtasa-blue/.../CGame.cpp:1393-1459`,
`CRPCFunctions.cpp:110`, `CPlayerManager.cpp:159` (MTA full-current-state push at the client ready handshake).
