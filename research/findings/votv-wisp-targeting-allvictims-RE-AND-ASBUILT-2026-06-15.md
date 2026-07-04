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
- **PUPPET victim (v1 shape; the TRIGGER + the abort timing are SUPERSEDED by v2 -- sec 6):**
  v1 fired RelayGrab on the `inRange` (550u, distance-only) rising edge; v2 (`769d02f7`) arms a
  CLOSING window at 550u+LOS and fires at 200u contact / 2.5 s LOS-gated timeout. The relay
  itself is unchanged (WispGrab->victim slot = the client's ragdoll death via
  wisp_tear_mirror::OnWispGrab; WispTear->all = tear montage; host wisp despawns +3.5s;
  `g_relayed` latch). Host false-grab protection: AddPlayerDamage PRE-cancel + per-tick HP pin
  (v1) + v2's rising-edge `CallReleasePlayer` gated `!playerDamaged` + the `canRagdoll=false`
  belt (a post-d1 releasePlayer would LETHALLY ragdoll the host; the drop notify kills
  unconditionally -- an HP pin cannot stop a ragdoll-death, the BP's own gate can).
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

## 3. DEFERRED follow-ons -- reconciled 2026-07-04 (v2 `769d02f7`)
1. **canReach LOS -- SHIPPED in v2.** `ue_wrap::wisp::CanReach` (wisp.cpp: LineTraceSingleForObjects
   on the {WorldStatic,WorldDynamic} obj_statDyn set via the KSL CDO; param names SDK-verified
   vs Engine.hpp:13152) is ANDed into the closing ARM edge AND re-verified at the fire
   (wisp_attack_sync.cpp) -- a blocked hover times out WITHOUT killing through the wall and
   re-arms when visible. Also used by the aggro selector's candidate eligibility.
2. **WispTear NPC-victim tear ANIMATION + host-victim tear-mirror -- STILL OPEN** (verified
   2026-07-04: `WispTearPayload` still 8 B at protocol.h:3204-3207). The PLAYER-victim
   socket-hold half of the old plan shipped WITHOUT the widen (victimSlot already rides
   WispTear; wisp_grab_hold::EngagePuppet). What remains needing the 8->12 victimKind widen:
   the tear montage on an NPC kill + the host-victim tear mirror (cosmetic).
3. **MTA "nearest player" helper -- SUPERSEDED by the v2 aggro selector**, which is
   deliberately NOT nearest: uniform RANDOM + stickiness among eligible players (user
   2026-07-03: the BP's nearest-pick made the host the perpetual victim). The MTA cite stays
   the shape precedent for host-authoritative victim selection.

## 4. HANDS-ON (user, next session)
- Peer ALONE near a wisp (host far) -> the wisp grabs+kills the peer (peer ragdoll-dies). Host log:
  `wisp_attack: RELAYED grab`. **[V matching real log 2026-07-03: autotest_kwisp_probe --
  SpawnKillerWispOnClient with the host teleported 8000u away: Target=PUPPET acquired, RELAYED
  grab fired, client `WispGrab accepted ... ragdoll DEATH fired ok=1`, 0 errors.]** (Pre-dates
  the v2 choreography -- the v2 items below re-test this scenario WITH the visuals.)
- v2 (2026-07-04, pending): BOTH players near the wisp -> it picks a victim at RANDOM (host log
  `wisp_aggro: wisp eid=N picked victim slot=S (2 eligible...)`), swoops (`CLOSING` ->
  `CONTACT`), and if the victim is the CLIENT: the victim sees itself grabbed + lifted riding
  the wisp (camera decoupled, own body visible), every other screen shows the puppet held at
  the wisp's socket rising ~5 m over 3.5 s + the tear montage, then the ragdoll death. Repeat
  spawns should spread kills across both players.
- v2 (pending): host stands in 550u while the wisp's victim is the CLIENT -> the host is NOT
  killed and NOT ragdolled (rising-edge release + canRagdoll belt); host log shows
  `canRagdoll=false forced` then `restored`.
- Wisp kills a kerfur -> the kerfur despawns on BOTH peers (no ghost). Host log:
  `wisp_attack: NPC victim eid=N died (wisp kill...) -- mirrored EntityDestroy`. [still pending]
- Dev button "Spawn killerWisp" (F1 > Game > Entities) drops one near you (nearest = YOU --
  use "Spawn killerwisp on client" to bait the puppet).

## 5. 2026-07-03 LATE-EVE UPDATE -- user repro + probe + the movement fact + v2 plan

**User repro (hands-on)**: dev-spawned a killerwisp on the client -> the wisp IGNORED the client
and flew to kill the HOST (both players in radius). User: "u killer wisp osobyj kill sequence" --
correct: sec 1b's player-0 hard-binding means a peer victim never gets the native grab/lift.

**NEW [V bytecode fact]**: the chase MOVEMENT is `Target`-bound -- `moveToTarg` issues
`CreateMoveToProxyObject(self, self, Vec(0,0,0), I:target, 5.0, false)` (ubergraph, two sites);
`GetPlayerCharacter(0)` appears ONLY in grab-arm distance checks + the grab/kill/damage/ragdoll
choreography. So "flew to the host" = the SELECTION picked the host (nearest-pick [RD] with both
in radius, or an unconfirmed perception-path bias), NOT a movement hard-bind.

**Probe verdict (autotest_kwisp_probe, `cd5947af`)**: with the host OUT of radius the whole June
chain works -- acquisition -> relay -> client death ok=1. The two real gaps (choreography +
aggro selection) were then BUILT as v2 -- sec 6.

## 6. v2 AS-BUILT (`769d02f7`, 2026-07-04 night; DLL `7BCE41C4B6DC9C99` 4/4; NO wire change)

- **AGGRO SELECTOR** (wisp_attack_sync.cpp): host-authoritative Target owner while >=1 player
  (host pawn / live puppet) is within the native 5000u + CanReach-visible. Policy = UNIFORM
  RANDOM + STICKINESS (re-roll on death/leave/>5500 hysteresis); raw `Target` write re-asserted
  per tick (no setter -- the BP's own inline write class). Hands-off during a native host kill
  (pick==host or no pick, with tryGrab/grab/killed up); STAYS ON through a false-grab (the BP
  arms on host PROXIMITY, not Target). No eligible player -> the native scan owns (kerfur/
  fossilhound hunting preserved; documented divergence: players preferred over a nearer kerfur).
- **TWO-STAGE CLOSE**: 550u+LOS arms a Closing window; the relay fires at 200u contact or the
  2.5 s hover timeout, LOS re-verified at fire (never through a wall; re-arms when visible).
- **GRAB CHOREOGRAPHY** (NEW coop/creatures/wisp_grab_hold.{h,cpp}, every peer): the victim
  client replays the native Capture player template (bytecode-exact, killerwisp.json @12313
  region: CMC SetMovementMode(MOVE_None) -> K2_AttachToComponent(wisp.Mesh,'playerGrab') [snap,
  not the native KeepWorld -- our Capture-equivalent fires at <=200u, not MoveTo contact] ->
  mainPlayer.Mesh visible -> held=1 -> bUseControllerRotationYaw=0 -> lag.bUsePawnControlRotation
  =0) against its LOCAL wisp mirror; detach + restore right before the scheduled ragdoll death
  (native releasePlayer order). Host + third peers snap the victim PUPPET to the wisp's
  'playerGrab' socket per tick AFTER the puppet pose apply (the net_pump call-site ordering IS
  the mechanism -- an attach would fight RemotePlayer::Tick every frame). The host LIFTS the
  real wisp 150 cm/s across the grab window; the pose stream carries the native signature rise
  to every screen. Holds self-release on liveness (the +3.5 s despawn is the canonical edge).
- **AUDIT FIXES** (2 agents, folded pre-commit): `IsKillerWisp` candidate-class SELF-RESOLVE --
  the entire lane (v1 included!) only resolved when the dev probe's unguarded ReadState ran
  ([[lesson-gated-probe-verify-the-gate]]); the false-grab abort moved to the native grab's
  RISING edge gated `!playerDamaged` (a post-d1 releasePlayer = ragdollMode(...,playerDamaged) =
  LETHAL to the host); NEW `canRagdoll=false` belt across the false-grab window
  (engine_mainplayer::SetMainPlayerCanRagdoll, canRagdoll@0xD10) -- the montage drop notify's
  unconditional ragdollMode(true,false,true) cannot be stopped by an HP pin, only by the BP's
  own pre-condition gate; restored on the falling edge / OnDisconnect.
- **VERIFY STATE (honest)**: build + deploy hash 4/4; generic LAN smoke PASS x2 (stability/RSS
  only); **the choreography E2E PROBE RUN IS STILL PENDING** -- three night attempts died to
  environment (parallel audit-agent machine load x2; a poisoned s_1234, see
  [[lesson-s1234-host-slot-stateful-coop-backup]]; a cold FRESH-join outliving the probe window,
  now widened to ~4.4 min). Hands-on (sec 4) remains the visual verdict.

Sec 3 item 2 (WispTearPayload 8->12 victimKind widen for the NPC/host-victim tear cosmetics)
remains OPEN (protocol.h:3204 still 8 B, re-checked 2026-07-04). Detail + status: memory
[[project-killerwisp-vs-peers-open-2026-07-03]] + [[project-pause-guard-2026-07-04]] (same
commit: the coop no-pause invariant).
