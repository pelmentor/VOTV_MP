---
name: project-bug-trash-chippile-uaf-crash
description: "2026-06-03 (protocol v28, UNCOMMITTED) -- TRASH MORPH SYNC WORKS, USER-CONFIRMED 'works at all times'. Whole cycle grab->both-piles-vanish->carry->throw->correct-variant-pile-lands is reliable via the OWNER-AUTHORITATIVE model (owner runs the REAL VOTV morph + broadcasts the RESULT; NOT 'make the peer's copy morph itself' = the flaky 3/10 part, deleted). 4 steps: GRAB (broadcast clump PropSpawn key=None+eid+ground-pos+chipType) -> SOURCE PILE removed (peer consumes its OWN copy by POSITION via FindNearestChipPile 300cm -- piles are SHARED WORLD OBJECTS loaded independently per peer, no cross-peer id, POSITION is the identity; reads true variant off the consumed pile) -> CARRY (pose-by-eid) -> THROW (owner death-watcher tracks lastPos, on clump death finds the re-piled chipPile + broadcasts it at authoritative pos via BroadcastLandedPileNear, + PropDestroy despawns the flying mirror; mirror gets collision so it lands during brief flight). Held-ball LEAK fix = liveness watcher + eid-routable OnDestroy (self-introduced ORDERING bug shipped first: UnregisterPropMirror BEFORE resolve -> null -> still duped; fixed resolve-first). Disconnect-while-holding LEAK fixed (null-mesh clump now ConsumeLocalActor'd on disconnect). Audit: 1 CRITICAL UAF fixed (FindNearestChipPile ClassOf-before-IsLive), perf OK, rest sound. NO new files. **NEXT SESSION (user EOD 2026-06-03): (1) user MIGHT have caught DUPE AGAIN (UNCONFIRMED -- re-test); likely the drop-without-throw-near-existing-pile spurious-dupe edge OR dense-clutter mis-pick (audit flagged both). (2) NO throw SOUND -- the clump mirror's OnRelease null-mesh branch doesn't fire thrown()/whoosh like Aprop's DrivePropThrown does. (3) NO landing SOUND when ball-clump lands/re-piles.** FOLLOW-UPS: remote_prop.cpp 825>800 extract coop/prop_drive; 3-peer relay untested; held ball spawns at EYES (pose). SEE '## OWNER-AUTHORITATIVE model summary' + '## Hardening'."
metadata: 
  node_type: memory
  type: project
  originSessionId: 86866d94-8692-44e2-b0a3-136eb16f4d11
---

# Trash chipPile use-after-free crash (2026-06-02) -- 2a reverted

## POSITION-CONSUME model SHIPPED (2026-06-03, UNCOMMITTED) — source pile + variant FIXED; throw re-pile HANDS-ON-PENDING

Did NOT do the architect's "spawn-mirror surgical fixes" (its premise was REFUTED by the
logs: chipPiles are NEVER mirrored cross-peer -- 0 actorChipPile OnSpawns on both peers,
only prop_garbageClump_C; so an eid-destroy can't resolve the other peer's pile). Instead
implemented POSITION-CONSUME (the only cross-peer identity for shared world objects; same
precedent as FindNearbySameClass's mushroom dedup):

1. **Source-pile consume (Bug 1) -- USER-CONFIRMED "disappears correctly on both peers":**
   the clump's PropSpawn broadcasts its GROUND location (log proved clump spawns at Z~6257,
   ~150cm BELOW the puppet Z~6405 = the pile spot, NOT the hands). Receiver OnSpawn (clump
   path, classW contains "garbageClump"): `ue_wrap::prop::FindNearestChipPile(loc, 300cm)`
   (new -- walks the chipPile base lineage since they're non-Aprop_C) -> if found, read its
   chipType (the TRUE variant, from our OWN pile) + `coop::remote_prop::ConsumeLocalActor`
   (echo-suppressed K2_DestroyActor) it. So this peer's own copy of the grabbed shared world
   pile disappears. Smoke: 4 CONSUME + 1 no-pile, 0 FAULT.
2. **Variant (Bug 2):** taken from the CONSUMED pile (reliable) not the wire byte (which
   arrived constant=3 -- morph-timing). Wire chipType kept as fallback when no pile resolves.
3. **Throw -> landed pile (Bug 3) -- two iterations; iter2 BUILT+smoke-verified, HANDS-ON-PENDING:**
   user repro: client grabs pile -> disappears on BOTH (consume works); throws -> ball
   re-piles correctly ON THE CLIENT (real VOTV) but on the HOST the ball mirror FALLS THROUGH
   + NEVER re-piles. Cause: bare-spawned mirror lacks a real clump's collision + convert flags.
   - **iter1 (canConvert self-repile) -- FLAKY 3/10, REVERTED:** primed the mirror's own
     ComponentHit BP (canConvert=true + delayOnHit=false) so it re-piles itself on landing.
     USER: "3/10 works AND the variant/texture MATCHES; 7/10 the ball doesn't make a pile --
     disappears on hit or falls through." The extra BP gates (impulse/slope/Max) make
     self-convert unreliable. RULE-2 removed `prop::SetClumpRepileFlags` (the canConvert poker).
   - **iter2 (EXPLICIT landed-pile broadcast) -- RELIABLE, SHIPPED:** the OWNER is
     authoritative for the landing. The death-watcher now tracks each clump's `lastPos` each
     tick; when the clump dies (re-piles), `BroadcastLandedPileNear(lastPos)` does
     `FindNearestChipPile(lastPos, 200cm)` -> the fresh re-piled chipPile -> mints an eid
     (MarkPropElement) + broadcasts a PropSpawn{class, authoritative pos, chipType from the
     pile, physFlags=0 settled} so the PEER spawns the landed pile at OUR position; then the
     existing PropDestroy(eid) despawns the flying mirror. The mirror still gets collision on
     release (`SetActorRootCollisionEnabled 3`) so it LANDS (not sinks) during its brief
     flight before the despawn. Smoke VERIFIED: client logged 8x `landed-pile BROADCAST
     cls='actorChipPile_C' ... variant=0/3 dist~20cm`, the HOST spawned 8 actorChipPile
     mirrors, 0 FAULT. New `engine_attach::SetActorRootCollisionEnabled` kept; `SetClumpRepileFlags`
     DELETED. **USER-CONFIRMED 2026-06-03: "works at all times"** -- host ball lands + the
     correct-variant pile appears at the authoritative position, reliably (the whole grab ->
     source-pile-vanishes-on-both -> carry -> throw -> landed-pile cycle now works). KNOWN
     edges (told the user): (1) clump dying WITHOUT re-piling (dropped/expired) near an
     EXISTING pile -> one spurious duplicate on the peer (rare; tighten radius or fresh-pile
     check); (2) dense-trash clutter -> nearest-within-radius could mis-pick; (3) spawner-
     divergent (random) piles may be at different per-peer positions -> consume could miss
     (placed world piles deterministic = fine); (4) 3-peer relay untested; (5) held ball
     still spawns at the EYES (pose offset, separate/untouched). Pile pos AUTHORITATIVE (from
     owner) = no per-sim divergence.

## Hardening (2026-06-03, post user-confirm) — disconnect-leak FIXED + audit UAF FIXED
- **Client disconnects while HOLDING a ball (user-raised edge) = was a REAL leak, FIXED:**
  the clump mirror has a NULL mesh (2a gate), so the per-slot disconnect's `if (d.mesh)
  DriveSimulate(..)` SKIPPED it -> the mirror was left as a frozen floating ball (the
  holder's death-watcher is gone with the holder, so nothing despawned it). FIX
  (remote_prop.cpp OnDisconnectForSlot + ForceRelease): null-mesh drive actor ->
  `ConsumeLocalActor` (echo-suppressed K2_DestroyActor) instead of leaving it. Re-smoke 0
  FAULT (the actual mid-hold-disconnect visual is a hands-on).
- **Audit (feature-dev:code-reviewer) found 1 CRITICAL UAF -- FIXED:** `FindNearestChipPile`
  read `R::ClassOf(obj)` (derefs obj's own memory) BEFORE `R::IsLive(obj)` -> AV on a
  GC-freed actor mid-scan. Reordered IsLive-first (matches FindNearbySameClass). Audit
  PASS on the rest: perf of the per-grab/throw GUObjectArray walks ACCEPTABLE (one-shot per
  event, not a hot path); thread-safe (GT-only); eid double-register safe; ConsumeLocalActor
  double-destroy safe; canConvert removal clean (0 dead refs). KNOWN minor edge re-confirmed:
  drop-without-throw near an existing pile -> one spurious duplicate (rare). FOLLOW-UP:
  remote_prop.cpp 825>800 soft cap -> extract `coop/prop_drive.{h,cpp}` (ActiveDrive + Tick +
  ResolveAndStartDrive + OnRelease + ForceRelease ~350 LOC); prop.cpp 615 watch.
- **The 9GB smoke FAIL was a TRANSIENT slow-boot** (host still handshaking, 0 trash activity,
  puppet=0) from this long session's many launches -- NOT trash; re-smoke = 5.1GB normal.

## NEXT SESSION open items (user EOD 2026-06-03, "Documentize, prepare for compact")
1. **POSSIBLE DUPE AGAIN -- UNCONFIRMED, user re-testing.** "мне кажется я поймал случаи
   дюпа опять". Most likely the audit's known edges: (a) drop-without-throw (clump expires/
   dropped, doesn't re-pile) near an EXISTING pile -> BroadcastLandedPileNear finds that
   pile + broadcasts it -> spurious duplicate on the peer; (b) dense-trash clutter ->
   FindNearestChipPile (200/300cm) mis-picks. To harden: only broadcast the landed pile if
   the found pile is VERY close to lastPos (it's the fresh re-pile, ~0cm) AND/OR check the
   pile is "fresh" (not pre-existing). Wait for the user's confirmed repro first.
2. **THROW WHOOSH (clump) -- DONE 2026-06-03, deployed, HANDS-ON-PENDING for audio.**
   User insight unblocked it: "we already have whoosh sound when throwing mannequin, so just
   add that to clumps too." The mannequin whoosh = `coop::remote_prop::DrivePropThrown` ->
   `Aprop_C::thrown(Player)` BP. The clump is `Aprop_garbageClump_C : AActor` (NON-Aprop_C,
   NO `thrown()`), so instead we play VOTV's OWN object-throw SoundWave
   `/Game/audio/effects/object_throw` (the default `object_*` interaction set the hand-grabbed
   mannequin uses; the parallel `gravigun_*` set is the gravity-gun tool only -- found by
   grepping the 8GB pak) positionally at the clump on release. NEW `coop/clump_throw_sound.cpp`
   `PlayThrowWhoosh` (FindObject object_throw as SoundWave->SoundCue fallback + a default
   20m/200m SpawnSoundAttenuation), called from `remote_prop::OnRelease` clump null-mesh branch
   gated on `linSpeed > kThrownLinVelThreshold` (same gate as the Aprop_C branch). NEW shared
   `ue_wrap::engine::PlaySoundAtLocation` wrapper (extracted the dispatch that lived inline in
   flashlight_click_sound -- RULE 2; flashlight refactored to use it, audit-confirmed
   BEHAVIOR-PRESERVING param-by-param). Audit: 0 CRITICAL; flashlight non-regressive; smoke
   PASS (5 flashlight observers install on both peers post-refactor, 0 FAULT, clump RELEASE
   path reached + correctly gated out at |v|=0 in the synthetic test). **HANDS-ON: a real
   throw (non-zero velocity) fires PlayThrowWhoosh -> watch for the whoosh + log
   `clump throw: object_throw whoosh played`. If object_throw is the WRONG sound, one-line
   swap to `gravigun_object_throw_Cue`.** Follow-up: remote_prop.cpp 849>800 -> extract
   coop/prop_drive (pre-existing, tracked).
3. **LANDING SOUND -- DONE 2026-06-03 (protocol v29), VERIFIED END-TO-END in smoke.** The
   owner's landed-pile broadcast now stamps `propspawn_flags::kFreshLanded` (0x08) + carries
   the clump's landing velocity in `initLinVel` (captured each tick via
   `engine::GetActorVelocity` in the death-watcher); the receiver's OnSpawn dispatches
   `ue_wrap::prop::TurnChipPileToPile(spawned, vel)` = `AactorChipPile_C::turnToPile(Velocity)`
   -- the SAME BP entry the real clump->pile landing calls (RE morph doc S2.1: sets spwnd,
   fires impact dust+SOUND, operates on `this`, spawns nothing -> no dupe). New per-class
   cached UFunction `ResolveTurnToPileFn` (null-safe on non-chipPile). Gating kFreshLanded
   is AIRTIGHT (audit-confirmed 2 ways: snapshot path only sets kSimulatePhysics|kIsHeavy|
   kFrozen AND skips key=None actors, so a connect chipPile can NEVER carry 0x08 -> no
   landing-sound storm on join). Files: protocol.h (v28->v29, kFreshLanded, initLinVel dual
   use), prop.{h,cpp} (TurnChipPileToPile, 641 LOC), trash_collect_sync.cpp (lastVel track +
   BroadcastLandedPileNear vel param, 214 LOC), remote_prop_spawn.cpp (OnSpawn turnToPile
   block, 494 LOC). Audit (code-reviewer): 0 CRITICAL/HIGH, all files under 800. Clump-test
   smoke PROVED the path end-to-end: host `landed-pile BROADCAST ... vel=(0,0,-507) dist=13`
   + client `landed pile -> turnToPile(vel=(0,0,-507)) [impact sound]`, 0 FAULT, RSS stable.
   **AUDIBLE result is HANDS-ON-PENDING** (smoke has no audio capture; the dispatch + safety
   are proven, the actual sound needs the user's ears).

## OWNER-AUTHORITATIVE model summary (the winning architecture)
The owner's machine runs the REAL VOTV morph; it broadcasts the RESULT for the peer to
mirror (NOT "make the peer's copy morph itself" -- that was the flaky 3/10 part). 4 steps:
GRAB (broadcast ball spawn + ground pos + variant) -> SOURCE PILE (peer consumes its own
copy by POSITION, reads true variant off it) -> CARRY (pose-by-eid stream) -> THROW (owner's
death-watcher finds the re-piled pile + broadcasts it at the authoritative pos + despawns the
mirror; mirror has collision so it lands during brief flight). Position = the cross-peer
identity for shared world objects (no shared id; same precedent as mushroom dedup).

## (SUPERSEDED -- pivoted to position-consume above) HANDS-ON action-replication — the REAL problem (2026-06-03)

User hands-on of the v28 fix (client grabs in host's world): **(1) the source pile does
NOT disappear in the host's world** when the client grabs it; **(2)** the thrown ball
appears on the host as the **WRONG type**; **(3)** sometimes the landed pile doesn't
appear at all and **falls through the ground**. 

**Root insight (the crux all 3 share):** the trash piles are **SHARED WORLD OBJECTS** --
both peers loaded the same world (`untitled_1`), so each peer independently has its OWN
copy of every placed pile at the same world position, with **NO cross-peer identity**
(key=None; each peer's pile is a distinct UObject with no shared id). The "spawn a mirror
of the clump" model therefore STRUCTURALLY cannot: remove peer B's own source pile (it's a
separate UObject B never grabbed), pick the right variant (a bare mirror defaults chipType),
or re-pile correctly on landing (raw SetActorSimulatePhysics with no collision -> sinks).

**The fix = ACTION REPLICATION (replicate the GRAB action, not the object):** when A grabs
pile P_a at world pos X (variant V), broadcast "GRAB near X"; peer B finds ITS OWN nearest
pile P_b (FindNearbySameClass) and calls `toClump()` on it -> B's own pile morphs away
(source removed), B gets a clump of the correct LOCAL variant (variant correct for free),
and on throw B's own clump re-piles via its own BP (landed pile + collision correct for
free). The held-ball is then driven to follow A's holder via the existing pose-by-eid
stream (bind B's clump under A's broadcast eid) or an attach. This REPLACES the spawn-mirror
clump model (RULE 2). **feature-dev:code-architect agent is designing it** -- open
questions it must resolve: is `toClump()` callable cross-peer via ParamFrame/Call + can we
capture its returned clump? position-match radius + no-match fallback (spawner-divergent
piles)? how to drive B's OWN clump (not a wire-spawned mirror) by A's pose? echo-suppress
(B's local toClump must not re-broadcast) + 3-peer relay? Blueprint pending.

## LEAK FIX 2026-06-03 (protocol v28) — held-ball dupe FIXED + VERIFIED (keep this)

The infinite grab/throw DUPE of the HELD BALL is fixed at the root and autonomously
verified. **Root cause (PROVEN by the user's hands-on log):** the held clump
(`Aprop_garbageClump_C`, key=None, identified by our eid) is mirrored via PropSpawn-by-eid,
but its DESTRUCTION (re-pile / LifeSpan expiry) is dispatched natively/BP-internally and
**NEVER reaches our `K2_DestroyActor` ProcessEvent observer** (the flashlight/doorOpen
trap). The session logged **4 distinct clump eids broadcast (one per grab) and ZERO
`broadcasting DESTROY`** -> the peer's clump mirrors accumulated forever.

**Two independent agents converged + REJECTED a reaper-broadcast** (the reaper's mass-purge
gate fires AFTER the reap loop -> a desynced client transition would broadcast KEYED-prop
destroys that delete the host's live keyed props; MTA never sweeps for destroys). The
correct shape: **eid-routable destroy (MTA `Packet_EntityRemove` resolves by element ID
only) + liveness-based detection** (the destroy edge is unobservable).

**The fix (3 parts, protocol v27->v28, no wire-struct change for the destroy):**
1. `remote_prop.cpp::OnDestroy` EID-ROUTABLE -- when wire key empty, resolve via
   `ResolveLiveActorByEid(elementId)`. **SELF-INTRODUCED REGRESSION (shipped first, then
   fixed):** I moved `UnregisterPropMirror(eid)` ABOVE the resolve, but the eid path
   resolves THROUGH the mirror Registry (`Registry::Get(eid)`), so draining first made it
   resolve null -> "OnDestroy: eid=N has no local actor" -> the ball KEPT piling up. FIX:
   resolve the actor FIRST, THEN UnregisterPropMirror, THEN destroy. (PropSpawn+PropDestroy
   share Lane::Bulk -> in-order, so rapid eid-REUSE across grab/throw despawns correctly.)
2. `prop_lifecycle.cpp` K2_DestroyActor PRE observer broadcasts PropDestroy(key=None, eid)
   for an unkeyed-but-eid'd prop (belt-and-suspenders; inert for the clump -- its destroy
   is unobservable).
3. **THE REAL LEAK FIX -- liveness watcher in `trash_collect_sync.{h,cpp}`:**
   `EnsureHeldItemBroadcast` registers each broadcast clump in a file-static
   `vector<WatchedClump>{actor, internalIdx, eid}`; `TickWatchReleasedClumps(Session*)`
   (per net_pump::Tick) broadcasts PropDestroy(key=None, eid) the tick `IsLiveByIndex` goes
   false + erases it; `OnDisconnect` clears. Owner-authoritative, O(1)/tick, bounded (cap
   32, dedup by eid).

**chipType variant byte (v28):** PropSpawnPayload steals one `_pad` byte for `chipType`
(14-value `enum_chipPileType` @0x0238 on chipPile/clump). `ue_wrap::prop::GetChipType/
SetChipType` resolve the offset via `FindPropertyOffset(cls,"chipType")` -- **SAFE on
Aprop_C** (0x0238 is StaticMesh there, but FindPropertyOffset returns -1 for a class with no
chipType prop -> SetChipType no-ops). Receiver applies after FinishSpawningActor + setTex()
repaint, guarded `if (chipType!=0)`. **BUT hands-on showed the value arrives constant=3** ->
likely the architect's action-replication makes this byte unnecessary (B uses its own pile's
variant); meanwhile it's harmless.

**Audits (2x feature-dev:code-reviewer): 0 CRITICAL.** Watcher UAF-free (IsLiveByIndex never
derefs; recycled slot = correct dead), thread-safe (GT-only), receiver idempotent (2nd
PropDestroy(eid) -> Registry::Get null -> no-op). Ordering fix correct (resolve-before-drain,
drain unconditional, lane-ordered eid-reuse safe). chipType memory-safe (verified -1-on-miss).
Clump-test smoke VERIFIED: host `watched clump eid=N went dead -> broadcast PropDestroy` +
client `OnDestroy: key '' eid=N -> destroying local actor` (0 'has no local actor'), 8 spawn/
8 destroy balanced, 0 FAULT. File-size: remote_prop.cpp 803>800 (extract drive OR mirror-
lifecycle later); net_pump.cpp 931>800 + event_feed.cpp 852>800 (pre-existing).

## (SUPERSEDED) HANDS-ON 2026-06-03 FAILED — v26 "resolved" was smoke-only; real grab/throw is BROKEN

User's FIRST real hands-on of the v26 clump sync. Verdict: "всё очень плохо". Bugs:
1. **Clump mirror spawns at the REMOTE peer's EYES** (wrong world pose — should float
   in front like the mannequin; appears inside the head/camera origin instead).
2. **Source ground pile does NOT despawn** when grabbed — it stays lying on the mirror.
3. **Throw -> a new pile appears -> INFINITE DUPE** ("дюпаешь до бесконечности") -> the
   memory LEAK + the jitter/stutter (remote puppet AND local first-person camera).
4. **Wrong TYPE** — trash piles have variants (`trashBitsPile_C`, `actorChipPile_C`,
   `prop_garbageClump_C`, different textures/colours) but the held mirror always spawns
   ONE type, ignoring which pile the user actually grabbed.

**LOG TRIAGE (client log 20506 lines, host 11245):**
- `door/light/container: sent`=0 and the 392 trash-pile OnSpawns were ALL at one
  timestamp [14:53:02] = the NORMAL connect snapshot (whole world ~5087 props), NOT the
  dupe. The interactable_sync feature is NOT the leak (bounded; see
  [[project-coop-interactable-state-sync]]).
- Client froze ~5s after connect in a **FAULT STORM: 1894 `game_thread: posted task
  FAULT ... access=0x0` (null-deref) in one second [14:53:07], with `net stats ...
  puppet=0`** (host puppet not spawned on the client). Posted-task null-derefs while
  puppet=0 -> something posts ~1894 apply/drive tasks that deref the null puppet right
  after the connect snapshot. May be a SEPARATE connect-time bug from the gameplay dupe
  (this log barely captured gameplay -- it froze at connect). recv=3750, sent=1.

**HYPOTHESIS for the dupe (VERIFY in code next):** the v26 sender broadcasts a NEW
`PropSpawn`(clump, key=None, eid) on the grab edge (`EnsureHeldItemBroadcast`, isAprop
branch). BUT the ground pile is ALREADY mirrored on the peer via the connect snapshot
(trashBitsPile_C etc. are tracked props). So the grab spawns a SECOND clump mirror
instead of DRIVING the existing pile mirror -> (a) grounded original stays = no-despawn,
(b) new floating ball at wrong pose = "eyes", (c) release leaves it = a pile, repeat =
dupe, (d) the spawned mirror's class is the hardcoded clump type = wrong type. **FIX
(root-cause, RULE 1): treat a grabbed world-pile EXACTLY like a normal grabbed prop --
stream PropPose by its EXISTING eid to drive the already-present mirror; do NOT
PropSpawn on grab.** Needs: confirm the pile's snapshot-mirror eid == the grab-broadcast
eid (if they differ, that's why OnSpawn doesn't dedup). Read `coop/trash_collect_sync.cpp`
(EnsureHeldItemBroadcast) + `coop/remote_prop_spawn.cpp` (OnSpawn eid dedup) +
`coop/net_pump.cpp` (held-edge) + `coop/remote_prop.cpp` (OnRelease).

**USER DECISIONS 2026-06-03:** debug the clump IN PLACE (don't disable); clump leak
FIRST, then re-hook doors. PAK question answered: unpacking helps for asset/variant
ENUMERATION (the wrong-type bug) but NOT for BP-graph logic/dispatch (UE4SS/IDA better
there) — it's diagnosis-only (RULE 3). Next session START HERE: read the 4 clump files,
confirm the spawn-vs-drive hypothesis, fix to drive-existing-mirror-by-eid.

---


## FINAL 2026-06-03 (HEAD=8c1488b, protocol v26): MANNEQUIN PROP-PIPELINE model -- the attach model below was REPLACED (RULE 2)

The held trash-clump now syncs via the EXISTING held-prop pose pipeline (PropSpawn /
PropPose / PropRelease) keyed by our EID -- the SAME path the mannequin uses. The v25
hand-attach model (the "## RESOLVED ... ATTACH MODEL" section below) was the WRONG
SHAPE and is fully removed: VOTV carries the clump via the physics grab (floating in
FRONT of the player, like the mannequin), NOT socketed to the hand. User's RULE-1
correction: "крепить к руке должны предметы из инвентаря; а clump надо как mannequin."

**THE BREAKTHROUGH (why the long invisible-clump dead-end happened):** a bare-spawned
`prop_garbageClump_C` is NOT invisible -- it renders the **'dirtball'** trash-ball mesh.
The whole "clump is invisible, needs mesh transfer" investigation was a PROBE-TOOL BUG:
`GetStaticMesh` is NOT a resolvable UFunction in UE4.27, so every probe mesh-read
returned false-null. Reading the asset via the REFLECTED FIELD OFFSET (`StaticMesh`
property @0x488 on UStaticMeshComponent, via `R::FindPropertyOffset`) showed the mesh
is present. Compounded by: the v25 hand-attach SnapToTarget'd the clump origin INSIDE
the puppet hand (occluded). So **NO mesh transfer is needed** -- the clump is visible
on its own; spawning it on the peer + streaming its world transform shows the trash ball
floating in front of the puppet.

**The fix (root-cause, no crutch):**
- WIRE v26: `PropPoseSnapshot` grows 56->60 with `uint32_t elementId`. Receiver resolves
  the mirror by KEY first, then by our EID (the clump streams key=None).
- SENDER (net_pump held-edge, unified): the clump rides `EnsureHeldItemBroadcast`
  (broadcasts PropSpawn with key=None + eid -- `isAprop` branch in trash_collect_sync)
  + the normal PropPose stream (eid stamped via `GetPropElementIdForActor`). Release
  reads the GENERIC root velocity (prop::GetPhysicsVelocity is 0 for the null-mesh clump).
- RECEIVER (remote_prop): `ResolveLiveActorByEid` (Registry::Get(eid)->mirror, validated
  with **IsLiveByIndex** -- the audit caught IsLive-on-a-GC-freeable-mirror as a UAF);
  `DriveTogglePhysics` (Aprop_C via StaticMesh; clump via the GENERIC root-component
  `engine::SetActorSimulatePhysics`/root-velocity, so the GetStaticMesh-null 2a-gate
  stays intact for the sender snapshot path); `ActiveDrive.lastEid` for re-grab detection
  (cleared at all 5 drive-clear sites); `OnSpawn` allows the None-key clump (eid gate +
  eid dedup + skips the key/position fuzzy dedup; fresh-spawn auto-skips setKey on None).
  Generic root-physics helpers live in ue_wrap/engine_attach.cpp (stripped to just those).
- RULE 2: DELETED coop/held_clump_sync.{h,cpp}, the HeldClumpGrab/Release ReliableKinds +
  payloads, the event_feed dispatch + ResolveHolderSlot, the net_pump wiring, the
  clump-shot test + mp.py clumpshot. Kept the `clumpvis` probe (it found the breakthrough).

**Verified:** LAN smoke -- host `prop-mirror=BROADCAST eid=N`; client `OnSpawn spawned
prop_garbageClump_C` + `GRAB-IN key='None' eid=N -> clump kinematic` + `drive #N` +
`clump RELEASE -> generic physics on`; **0 FAULT both peers, 0 held_clump leftovers**.
2 audit agents: perf clean (all new resolves O(1), gated to identity-change/edges); the
crash-safety agent found + I FIXED the UAF (IsLiveByIndex) + a lastEid clear bug.
**HANDS-ON PENDING for the full visual** -- the synthetic cold-grab clump self-frees ~1s
(falls, isn't physically held) so the autonomous drive is brief (3 frames); a REAL
grabbed clump persists -> floats in front + throws. Story-load-as-sandbox fixed
separately (fe4f229). Follow-ups: 3-peer client<->client clump relay; net_pump 921>800 ->
extract local_stream_pump; event_feed 819>800; retire the clumpvis diagnostic once stable.

---

## (SUPERSEDED) RESOLVED 2026-06-03 (HEAD=b97b693, protocol v25): ATTACH MODEL -- REPLACED by the mannequin model above

The held trash-clump now mirrors via the MTA attach model (the pivot the user
demanded: "no crutches, physics like the mannequin"). NOT the key path (the
clump is non-keyable -- proven). Built + autonomously verified end-to-end:

WIRE (protocol v25): `ReliableKind::HeldClumpGrab(21)` {senderElementId,
className} + `HeldClumpRelease(22)` {senderElementId, linVel, angVel}.

NEW FILES (Principle 7 split):
- `ue_wrap/engine_attach.cpp` (246 LOC) -- generic substrate:
  `AttachActorToPuppetHand` (puppet `mesh_playerVisible` @0x4F8 + hand bone;
  one-shot full bone DUMP + candidate match), `DetachActorFromParent`,
  `SetActorSimulatePhysics`, `Get/SetActorRootPhysicsVelocity`. All GENERIC
  (K2_GetRootComponent, NEVER the Aprop_C mesh offset) -> the GetStaticMesh-null
  invariant holds -> the 2a UAF is structurally unreachable.
- `coop/held_clump_sync.{h,cpp}` (197/83) -- per-slot held mirror + bounded
  released ring (cap 24); SendGrab/SendRelease (sender derefs ONLY the LIVE
  current heldActor on the edge) + ApplyGrab/ApplyRelease (receiver works on OUR
  stable mirror, IsLiveByIndex-gated) + OnDisconnect.

WIRED: net_pump held-edge -> `IsAttachClump` (non-Aprop_C keyed) routes to the
attach path + SKIPS the unmatchable PropPose stream (g_lastHeldWasClump tracks
the release dispatch). event_feed dispatches both kinds (shared `ResolveHolderSlot`
= self-echo + eid->slot + role-range trust, factored with ItemActivate).

CONFIRMED HAND BONE: the kel/kerfur puppet skeleton (`mesh_playerVisible`, 101
bones) names the right hand **`hand_R`** (bone index 29), left `hand_L` (52).
The bone dump is in the client log; the candidate list resolved it first-try (no
2nd build cycle). Encoded as the lead candidate.

VERIFIED (VOTVCOOP_RUN_CLUMP_TEST adapted): host grab -> client logs
`resolved puppet HAND bone='hand_R'` + `ApplyGrab slot 0 spawned+attached mirror
'prop_garbageClump_C'` -> release -> `ApplyRelease slot 0 detached+thrown`.
**4 LAN smokes, 0 FAULT both peers, RSS stable** (host ~5GB / client ~3.7GB, no
balloon). Two audit agents: **no CRITICAL FAIL, no UAF**; IsAttachClump proven
O(1)/tick (cached base-class ptrs, latched trash-class resolution, no
GUObjectArray walk). 2 hardening findings FIXED: (1) capture the mirror's
GUObjectArray index IMMEDIATELY post-spawn + bail on -1 (was captured after 2
intervening UFunction calls); (2) latch the bone-resolve no-match path so it
can't re-scan 101 bones per grab.

HANDS-ON PENDING (autonomous can't drive it): the host doesn't walk during the
test, so "clump FOLLOWS the moving hand" is mechanically guaranteed by the attach
but not visually confirmed; throw velocity on release reads 0 in the synthetic
test (cold-written grab isn't physics-dragged) -- a real hands-on throw will
carry velocity via GetActorRootPhysicsVelocity.

KNOWN v1 FOLLOW-UPS: (a) client<->client (3-peer) needs host RELAY of
HeldClumpGrab/Release (not in the relay whitelist -- 2-peer host<->client is
complete); (b) released-clump DESPAWN-sync (no key/eid for PropDestroy to carry;
the cap-24 ring bounds accumulation meanwhile); (c) transient
SpawnActor-refused-mid-transition drops that one grab (graceful WARN, no crash --
the autotest now waits 35s so the client world settles); (d) extract net_pump
(953>800) -> local_death_policy + event_feed (911>800) -> event_feed_routing
(both already queued, under the 1500 hard cap). The STORY-MODE-LOADS-AS-SANDBOX
bug (below) is SEPARATE + still open.

Everything below is the ORIGINAL crash saga (kept for the lessons).

---


## What happened
User hands-on: "after I tried picking up garbage chip pile thing - session died."
HOST log flooded (hundreds/sec, all at 20:00:40):
`game_thread: posted task FAULT code=0xC0000005 ip=votv-coop.dll+0x1A9E8 access=0x...004C; skipped, pump continues`
then the client dropped ("all peers gone") and the user closed both windows.

## Confirmed root cause (RVA map + offset, NOT guessed)
- Rebuilt the 2a DLL, ran `python tools/maprva.py 0x1A9E8` -> `?IsHeavy@prop@ue_wrap@@YA_NPEAX@Z (+0x8) [prop.obj]`.
- `Aprop_propData_heavy = 0x02CC` (sdk_profile.h:318). The faulting access `0x...B004C` = a freed prop `0x...ADD80` + `0x2CC` landing in an unmapped page.
- => `ue_wrap::prop::IsHeavy(freedProp)` reading the heavy flag at +0x2CC, every frame (stable address => a cached pointer to FREED memory read repeatedly).
- IsHeavy callers: prop.cpp:275 (FindNearest), prop_snapshot.cpp:232 (snapshot drain), prop_lifecycle.cpp:272 + :422 (PropSpawn broadcast). The per-tick/posted paths pass a freed chipPile.

## Why 2a triggered it ([[project-bug-client-weather-not-host-authoritative]] sibling -- the GetStaticMesh fix)
`trash sync 2a` (376951f) made `GetStaticMesh` resolve the StaticMesh per-CLASS
(reflection FindPropertyOffset) instead of the fixed Aprop_C offset 0x0238. That
returned the chipPile's REAL mesh (0x0228) where before it returned a garbage byte.
With a real mesh the chipPile MIRROR fully entered the prop spawn/drive/physics
paths as if it were a stable Aprop_C prop. But chipPile (`AactorChipPile_C : AActor`)
MORPHS to a clump (`toClump()`) on pickup and DESTROYS itself -> its pointer, now
cached in a snapshot/drive/broadcast path, is read after free -> IsHeavy UAF.
Pre-2a the chipPile never got a real mesh, so it never entered that path -> no crash
(user-confirmed: pre-2a "nothing changed", no crash).

## Fix applied
- `git revert 376951f` -> commit **a8170b4**. GetStaticMesh back to the fixed
  Aprop_C offset. Rebuilt + redeployed the safe DLL (3399680 bytes) to all 4 copies.
- 2a delivered NO visible benefit (user: "nothing changed") AND enabled the crash ->
  reverting loses nothing usable.

## LESSONS (constrain the real fix)
1. **chipPile / garbageClump are TRANSIENT, self-morphing/-freeing actors.** Do NOT
   treat them as stable physics props. The trash-ball CARRY (future) must use the
   MTA ATTACH model (K2_AttachToComponent to the holder's hand, like the ragdoll
   pelvis), NOT the physics pose-drive that 2a enabled. [[project-ragdoll-sync]]
2. **The prop field readers (IsHeavy/IsStatic/IsFrozen/GetStaticMesh) null-check but
   do NOT liveness-check.** A non-null FREED pointer passes `if(!prop)` and faults on
   the field read. Latent UAF reachable whenever a caller holds a prop pointer across
   a possible free. The targeted RULE-1 fix is at the CALLER (validate IsLive/
   IsLiveByIndex before iterating cached prop pointers in the snapshot drain /
   broadcast / drive), NOT a broad IsLive inside IsHeavy (principle 4: no broad
   "filter all orphans"). FOLLOW-UP when building the real trash fix.
3. The trash-STACK fix (Option B: trashBitsPile press-E collect mirroring) is
   `Aprop_C`-item based, stable, and does NOT need 2a -> unaffected by this revert.

## Process note (user feedback 2026-06-02)
User asked "you never use IDA mcp? why" -- correct nudge to VERIFY not guess. IDA is
for NATIVE code; the garbage/trash classes are pure BP (zero native impls), so the
right rung is reflection (CXX dump -- used) + UE4SS BP-dump / runtime probe for BP
semantics. This crash was root-caused by the .map RVA tool (tools/maprva.py), which
IS the verify-don't-guess discipline. Apply it BEFORE shipping, not after a crash.

## FULL TRASH-SYNC SAGA + the ARCHITECTURE DECISION (2026-06-02, read before resuming)

Goal: when a player presses E on a trash pile, the spawned item appears in the
OTHER player's hands. The spawned/held item is **`prop_garbageClump_C`** (a
"trash ball"), confirmed by a per-grab diagnostic. Four attempts, all banked
LOCAL (unpushed), origin still at 2e9011f:
- **2a (376951f, REVERTED a8170b4):** per-class real mesh + physics-drive the
  clump -> UAF CRASH (above).
- **v1 (b4b6bfe):** POST observer on `trashBitsPile_C::playerTryToCollect`.
  FAILED hands-on -- the observer INSTALLED but NEVER FIRED: playerTryToCollect
  is dispatched BP->BP (ProcessInternal), which bypasses our ProcessEvent detour.
- **v2 (3279e93):** detect on the net_pump held-prop EDGE (grabbing_actor -- the
  proven detection point; it's what streams the held pose). Force-mint key +
  broadcast. FAILED hands-on -- the items are NON-Aprop_C clumps, excluded by the
  Aprop_C crash-safety gate (`trash-mirror=no`).
- **v3 (fd4b1eb):** include non-Aprop_C; mirror the clump KINEMATICALLY (the
  INVERSE of 2a): `GetStaticMesh` returns **null for non-Aprop_C** (single safety
  gate -> every mesh/physics caller null-no-ops; audit "provably incapable of the
  2a UAF"), `ResolveAndStartDrive` allows a null-mesh kinematic SetActorLocation
  drive, `OnSpawn` indexes the mirror key (else Aprop_C-only FindByKeyString never
  resolves a clump mirror). **KEEP the GetStaticMesh-null gate -- it's good crash
  safety.** BUT v3 is a CRUTCH and DOESN'T WORK.
- **Autonomous clump-test (33e7f25, env `VOTVCOOP_RUN_CLUMP_TEST=1`):** host
  SpawnActor's a clump + writes grabbing_actor + sweeps it; client should mirror.
  Ran on the LAN pair, NO crash. **PROVED THE BLOCKER: the clump is NOT KEYABLE.**
  After EnsureKeyForBroadcast force-mints + `setKey` SUCCEEDS, `GetKey` RE-READS
  None ("Key still None after force-mint -- cannot mirror"). So the ENTIRE
  key-based mirror (v1/v2/v3) cannot broadcast a clump -- there is no stable key to
  mirror/pose-stream by.

### THE DECISION: MTA ATTACH MODEL (NOT YET BUILT)
User (rightly): kinematic = crutch; "no crutches, physics like the mannequin."
The mannequin works because it's a KEYED Aprop_C with a standard mesh that
PRE-EXISTS on both peers (pose-stream drives the existing mirror; physics-off-
while-held + on-release). The clump is NONE of those (non-Aprop_C, spawned-on-
grab, non-keyable). So the held clump must be synced by the **MTA ATTACH model**:
1. `ue_wrap::engine::AttachActorToPuppetHand(clump, puppet)` -- resolve the
   puppet's `Mesh` skeletal component + the HAND bone, `K2_AttachToComponent`
   (snap-to-socket) + a detach. DIRECT adaptation of `AttachActorToRagdollBody`
   (engine_playerragdoll.cpp:211, uses K2_AttachToComponent + FindBoneFName).
2. A small reliable wire signal: grab edge `{holderSlot, clumpClass}`, release
   `{holderSlot}`. NO key needed (attach by holder-identity).
3. Sender (net_pump held-edge): held non-Aprop_C clump -> send the attach signal,
   not the (impossible) key broadcast.
4. Receiver: spawn the clump mirror (eid-bound, no key) + AttachActorToPuppetHand
   to holderSlot's puppet; on release DETACH (it free-falls with its OWN physics
   = "physics like the mannequin"); clean up on holder disconnect.
5. Crash-safe: every attach/detach IsLive-gated on clump AND puppet; the
   GetStaticMesh-null gate stays (no mesh/physics ever touches the clump).
**ONE RE unknown to resolve FIRST (autonomously, no PC): the puppet HAND BONE
NAME** (VOTV skeleton -- hand_r / Hand_R / ...). Dump the puppet mesh bones with a
probe; worst case attach to the mesh root (follows the body) + refine to the hand.

### SEPARATE BUG -- RESOLVED 2026-06-03 (HEAD=fe4f229): STORY save loaded as SANDBOX
`engine::LoadStorySave` bypassed VOTV's menu, which is what normally sets
`mainGameInstance_C.GameMode` @0x01E1 (the `enum_gamemode` story/sandbox/... byte)
on load; our bypass set the save object + `loadObjects=1` + `open untitled_1` but
NOT GameMode, so it inherited the default (**mode 4, prefix 'b_'**). **RE finding:
VOTV stores a save's mode ONLY in the slot-name PREFIX** -- `Uui_saveSlots_C::
getSavePrefix(mode)` yields the prefix for that mode. New
`engine::ApplyGameModeFromSlot` (engine.cpp) calls getSavePrefix(0..7) on the
widget CDO, longest-prefix-matches the slot name, and writes GameMode @0x01E1
before the travel (NO hardcoded value -> correct for story/sandbox/any mode);
retried each boot poll until the widget loads, then latched. **MAPPING (smoke-
confirmed): 0='s_' 1='i_' 2/3='-' 4='b_' 5='SPOOKY_' 6='a_' 7='l_'.** The user's
slot 's_may2026' = prefix 's_' = **GameMode 0** (the buggy default was 4). Verified:
host boot logged `slot 's_may2026' prefix-matched GameMode=0 (was 4); set @0x01E1`,
widget WAS loaded at boot (no crash), smoke PASSED (no regression). New offset
`mainGameInstance_GameMode = 0x01E1` in sdk_profile.h. USER CONFIRMS story-vs-
sandbox visually next session (the value is logically correct -- loads each slot in
its own creation mode). Save slot is `votv-coop.ini` `save=` (default s_may2026).
