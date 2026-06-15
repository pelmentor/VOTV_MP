# Killer Wisp host-authoritative targeting -- RE ground truth + AS BUILT (2026-06-15)

Session 18. Supersedes the targeting portions of `votv-killerwisp-coop-design-2026-06-13.md`
(that doc's tear/kill receiver design still stands). Shipped commit `1ae92a1a`.

User report that drove this: "Killer wisp doesn't target PEERS at all, neither other enemies"
+ "the wisp can kill other entities like kerfur - which also needs to be sync-mirrored."

---

## 1. RE GROUND TRUTH (bytecode, `research/bp_reflection/killerwisp.json`; two RE passes)

### 1a. Acquisition (CHASE) already includes puppets + NPCs -- it is NOT the gap
`scanForActors` writes `Target` @0x0610 (APawn*): `SphereOverlapActors(self.loc, radius 5000,
[obj_pawn])` -> per-overlap allow-set `Array_Contains([mainPlayer_C, kerfurOmega_C,
fossilhound_C], GetClassHierarchy(cand))` -> `LineTraceSingleForObjects` LOS -> nearest by
`GetDistanceTo` -> `target := AsPawn`. Our client puppets are `mainPlayer_C` orphans WITH
class-default capsule collision (puppet.cpp never disables it; only the CMC+actor ticks), so a
puppet is a valid overlap + allow-set hit. **The wisp CHASES puppets/kerfurs/fossilhounds when
nearest.** AIPerception delegates are stubs that never write Target (red herring).

### 1b. The GAP: grab/kill is hard-bound to player 0 (the host)
- **Grab-arm block `@5596-5989`:** `SphereOverlapActors(self.loc, radius **550**, [Pawn])` then
  `Array_Contains(overlap, GetPlayerPawn(0))` then `canReach()` -> `grab := true`. The gate tests
  **`GetPlayerPawn(0)` (the host's own pawn), NOT `Target`.** So `grab` arms only when the HOST is
  within 550u -- never for a puppet/NPC the host is far from.
- **`canReach()`** = a LINE-TRACE LOS test (NOT navmesh): player-arm traces to `GetPlayerPawn(0)`
  loc, NPC-arm to `target` loc, on `lib_obj.obj_statDyn` channels, returns false if the trace HITS
  (blocked). Pre-guards on `killed | grab | (player: getMainPlayer().isRagdoll)`.
- **Player grab/kill** (`Capture @12313` cast Target->mainPlayer SUCCEEDS): lift to `playerGrab`
  socket + `fatality` montage + kill via `GetPlayerCharacter(0)` / `getMainPlayer().AddPlayerDamage`
  -- **always the HOST**, even when Target is a puppet (the "false-grab").
- **NPC kill** (`Capture` cast FAILS -> `@12821`): `icast(target)->int_objects_C` then
  `addDamage(self, **1000.0**, ...)`. NO grab/lift/socket/tear. The NPC then dies via its OWN BP:
  `kerfurOmega::addDamage(>1)->death()@11568->...->self.K2_DestroyActor()@15068` (immediate);
  `fossilhound`: accumulates, `health<=0 -> deth:=true (0x0590) -> ... -> self.K2_DestroyActor()`.
  Both self-destroys are **`EX_VirtualFunction "K2_DestroyActor"` = native branch = PE-INVISIBLE**
  -> our `NpcDestroy_PRE` ProcessEvent observer NEVER fires -> the kerfur/fossilhound death does
  NOT auto-mirror (the client keeps a ghost mirror). The wisp never K2_DestroyActor's its Target
  (its own K2_DestroyActor calls destroy SELF: unrendered-despawn + out-of-world Z<-5000).
- No second/tighter "attach" distance. Radii in the asset: 5000 (acquire) / 550 (grab) / 500 /
  450 (finalize).

### 1c. enum_gamemode is irrelevant to the wisp (separate from the Q-menu RE)

---

## 2. AS BUILT (commit 1ae92a1a; NO protocol change -- reuses v72 WispGrab/WispTear + EntityDestroy)

`ue_wrap/wisp.{h,cpp}`: `InGrabRange(wisp, target)` = 3D distance <= 550 (the @5702 radius).
`coop/wisp_attack_sync.cpp` `Tick` (host-only, walks the tracked Npc set, NOT GUObjectArray):
- Classify `st.target` {host / puppet / npc} via `players::Registry::IsLocal/IsPuppet` +
  `npc_sync::GetNpcIdForActor` (all map-key lookups -- no deref).
- `inRange = victim && R::IsLive(victim) && !st.harmless && InGrabRange(actor, victim)`.
  **`R::IsLive(victim)` is REQUIRED** -- the BP does not null Target the frame its target dies, so
  the deref in InGrabRange (GetActorLocation) / the enroll (InternalIndexOf) would UAF without it
  (the HIGH bug the audit caught + fixed).
- **PUPPET victim:** trigger = `inRange` (NOT the host-proximity `grab` flag). On the rising edge ->
  existing `RelayGrab` (WispGrab->victim slot = the puppet's client ragdoll-dies via
  wisp_tear_mirror::OnWispGrab; WispTear->all = tear montage on the wisp mirror; host wisp despawns
  +3.5s to break the re-grab loop; `g_relayed` latch = one relay per wisp). If the BP ALSO
  false-grabbed the host (`st.grab`): `CallReleasePlayer` + the AddPlayerDamage PRE-cancel +
  per-tick HP-pin protect the host (gated on `(grab||tryGrab) && isPuppet`).
- **NPC victim:** `g_npcKillWatch` (key=victim eid; {actor, idx, deadline}). Enroll when a wisp is
  `inRange` of a tracked NPC. `DischargeNpcKillWatch` each Tick: `GetNpcIdForActor(actor)!=eid` ->
  drop (recycling/already-drained guard); else `IsLiveByIndex` -> keep (or drop at the 6s
  deadline); else DEAD -> `npc_sync::SyncDestroyedNpcActor(actor)` -> EntityDestroy -> the mirror
  despawns. Idempotent with kerfur_convert + the PE observer (both erase the actor->eid map entry,
  so the watch drops without a double-broadcast). The actor uses `SyncDestroyedNpcActor`'s
  map-key-only contract (safe on a freed pointer).
- **HOST victim:** NOT specially handled (the BP kills the host natively = SP). DEFERRED: mirroring
  the host-death tear to clients (cosmetic).

Audited SAFE (recycling/idempotency/thread/perf/eid). wisp_attack_sync.cpp 289 LOC, wisp.cpp 245.

---

## 3. DEFERRED follow-ons (next session, after hands-on)
1. **canReach LOS (MED divergence):** the v1 puppet trigger is distance-only -- it drops the BP's
   `canReach` LineTrace LOS, so a thin-wall grab-through is possible. Faithful fix = a
   `ue_wrap::wisp::CanReach(wisp,target)` LineTraceSingleForObjects (1b shape: Sphere->target,
   obj_statDyn channels, "reachable"==not blocked) ANDed into `inRange`. Strict improvement over
   not-attacking, so shipped distance-only first.
2. **WispTear NPC-victim tear ANIMATION + host-victim tear-mirror:** the kerfur DEATH (despawn)
   mirrors now; the wisp's tear MONTAGE on an NPC kill (and on a host kill) is cosmetic-deferred.
   Needs `WispTearPayload` v78: widen 8->12B (`uint8 victimKind {Player=0,Npc=1}` + 3 pad +
   `uint32 victimRef` = slot-or-eid); branch `wisp_tear_mirror::OnWispTear` on victimKind
   (Player -> PlayTearOnWisp + socket-hold puppet; Npc -> PlayTearOnWisp only). The
   [[feedback-reliablekind-router-checklist]] reduces to a payload-size bump (WispTear already
   exists; event_feed size-check auto-tracks; relay whitelist unchanged -- host-only).
3. **MTA precedent for a future general "nearest of all players" AI helper:** `CPedSync::Find
   PlayerCloseToPed` (reference/mtasa-blue/Server/.../CPedSync.cpp:188) + health->death propagation.

## 4. HANDS-ON (user, next session)
- Peer ALONE near a wisp (host far) -> the wisp grabs+kills the peer (peer ragdoll-dies). Host log:
  `wisp_attack: RELAYED grab`.
- Wisp kills a kerfur -> the kerfur despawns on BOTH peers (no ghost). Host log:
  `wisp_attack: NPC victim eid=N died (wisp kill...) -- mirrored EntityDestroy`.
- Dev button "Spawn killerWisp" (F1 > Game > Entities) drops one near you.
