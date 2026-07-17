# L8 physMods (PhysModsState=108, v118) — impl design of record (2026-07-18)

8-round `/qf` impl pass (genuine "that holds" at R8/15; thread: scratchpad `qf_thread.md`).
Arch frame: `votv-signal-chain-all-units-DESIGN-2026-07-16.md` §L8 — **REVISED in three
majors** (see "Deviations"). Status: BUILT; **both audits (perf + correctness) independently converged on the SAME
CRITICAL** — the host's own poll-detected ops were misrouted through the remote-op apply
(the host's array already held the change -> the dup branch fired a PHANTOM REFUND spawn
per host plug, and no host-local change ever broadcast the canonical). FIXED at the root:
SendOp is client-only; the host's DrainLocalDiff broadcasts ONE canonical for any organic
diff (its live array IS the canonical). + audit MINORs: WriteArray return checked before
baseline/broadcast; ClassForByte latches only on a successful call (a transient failure
no longer permanently negative-caches the byte). Smoke #1 PASS with the join-canonical
chain proven in-log (host "canonical -> joiner slot 1" -> client "canonical adopted");
final smoke on the fixed bytes below. NOT hands-on.

## The measured fact base (fresh bytecode/SDK censuses)

| # | Fact | Evidence |
|---|---|---|
| F1 | physMods = TArray<TEnumAsByte<enum_physicalModules>> @0x12A0 on AanalogDScreenTest_C, fixed 12 slots, 0=empty; coldswapEnabled @0x14D3 | SDK analogDScreenTest.hpp:264,350 |
| F2 | The array is a **SET**: plugInModule's native dup-check (Array_Find/Contains) denies a byte already plugged => every module byte is unique | plugInModule body walk |
| F3 | plugInModule(holdActor, slot, player): isModuleAllowed -> dup-check + deny hints -> hot-plug explotano branch -> find-free elem write -> **K2_DestroyActor(the held module)** (NO prior hand release — the native path destroys in-hand) -> updPhysMods -> soundPlugModule. 13 call sites (12 socket BndEvt overlaps) | body + caller census |
| F4 | **The UNPLUG path exists** (the R1 reframe — "modules permanent" was FALSE): uber stmts 700-731, entry playerHitWith (hit the socket): gates (module present; ANY unit powered && !coldswapEnabled -> **explotano on unplug too**) -> lib.physModToActor(byte) -> BeginDeferred+FinishSpawn the module prop -> player."Hold Object" (born INTO THE HAND) -> physMods[slot] := 0 -> updPhysMods | stmt-level walk |
| F5 | physMods writers: plugInModule / the unplug path / setData (save-LOAD; **calls updPhysMods** => the joiner's consumers are poked at load) / gatherData (save marshal). No others | write census |
| F6 | updPhysMods body: per-slot Array_Get -> SetVisibility (socket visual = index-cosmetic) + Contains x4 -> SelectFloat -> **write:speed** (the cross-object wallunit tape-speed poke) + SetVisibility. NO player refs / spawns / audio => a **pure function of the array**, mirror-safe + idempotent | body walk |
| F7 | lib.physModToActor(byte) -> TSubclassOf<**Aprop_physModule_C**> — per-byte module classes under one base => a class-whitelist check = IsChildOf(base); the byte rides the CLASS (no scalar channel needed for births) | lib.hpp:37 |
| F8 | The plug CONSUME needs zero lane code: a CLIENT keyed destroy IS broadcast (prop_destroy_seam.cpp:102-153, bidirectional; episode-suppression is join-window-only) + parks the key; a host destroy fans natively. If the host twin already died at grab (the MOVE model), receivers no-op | at lines |
| F9 | The HOST's unplug-born held module is **expressed the same tick**: the spawn watcher's hand exclusion is ONLY hand_item::LocalHandActor() (the hotbar view husk, host_spawn_watcher.cpp:390) — the module actor itself goes through ExpressSpawnedProp and rides normal carry | at lines |
| F10 | The CLIENT's unplug-born module: invisible while held (the client-birth-at-spawn-seam doctrine); its DROP was gated OFF at prop_drop_intent.cpp:262 (`!parked && !reelEject -> continue`) = the v114 reel-eject local-ghost class | at line |
| F11 | A leaver's in-HAND item is ALREADY lost from the shared world today (grab = MOVE, host half destroyed; no restore machinery) => the held-module leave window is the INHERITED loss class | code survey |
| F12 | explotano spawns **explosion_C** (+shake_explosion_C) whose only damage call is addDamage targeting getMainPlayer/GetPlayerCharacter ONLY (zero actor enumeration) => the hot-op explosion is **entirely presser-local natively** (damage + VFX; a bystander is unaffected). Additionally player_damage's detection half is measured-deferred. The R3 "damage crosses via the choke" inference was RETRACTED on this measurement | explosion.json walk + player_damage.h:16-30 |
| F13 | coldswapEnabled = a GAME RULE (ui_gameRulesList.gamerule_coldswap); zero desk-BP writers; rules are session-static post-load and travel to the joiner inside the save blob => the explode gate cannot diverge mid-session | census + game_rules.h |

## The design (as built) — value-ops + host-canonical array

1. **DETECT:** a 1 Hz 12-byte poll on every peer (outcome-based: plug, unplug, deny and
   explosion branches all converge through the array — entry-agnostic, no seam needed);
   prime-on-first-connected-poll (a save-loaded array never diffs against zero); SP-half
   tracks silently.
2. **WIRE (PhysModsState=108, 16 B, Lane::Normal, NOT client-relayed, proto 118):**
   - op 0/1 = plug/unplug **value-ops {byte}** peer->host (HOST-TERMINAL), derived from the
     local diff (the SET property decomposes any diff into vanished + appeared bytes,
     slot-free);
   - op 2 = **canonical array {bytes[12]}** host->all (slot-0-gated) after every host apply
     (incl. the host's own organic changes — its ops route through the same apply);
     receivers **drain-before-adopt** (send pending local ops first — closes the
     eaten-edge race), then adopt WHOLESALE + prime + reflected updPhysMods under the
     shared desk wire guard; a pre-desk canonical PARKS and applies at resolve
     (structurally after setData);
   - op 3 = **deny {origOp, byte}** host->the-no-op-author: a dup PLUG gets a **REFUND**
     (the host spawns the byte's class at the desk via engine::SpawnActor; its watcher
     expresses + fans — the item is never lost); a raced UNPLUG makes the author destroy
     its local ghost (hand or the bounded untracked-actor sweep of that byte's class);
     the host also **reaps** the denied byte's kind-104 fresh birth inside a 10 s TTL
     (unambiguous: fresh module births come only from unplugs and the SET forbids two).
3. **HOST apply:** plug -> dup-deny+refund OR find-free write; unplug -> zero-where-found
   OR deny; then updPhysMods (guarded) + canonical broadcast + own-baseline follow.
4. **Client-birth whitelist widened:** prop_drop_intent's drain condition (`freshBirth`)
   and the OnReelEjectIntent host validation now accept IsChildOf(Aprop_physModule_C)
   alongside reels; the kind-104 intent semantics = "client fresh-birth author",
   still an explicit class gate, not a general door.
5. **JOIN:** save transfer seeds the array + setData pokes consumers natively (F5); the
   host also ships the canonical array in connect-replay (12 B ground truth over save
   drift). **LEAVER:** baseline reset OnDisconnect; the held-module window = F11's
   inherited class.
6. **Modules:** ue_wrap/desk/phys_mods.{h,cpp} (offset/updPhysMods/base-class/
   physModToActor probe cache) + coop/interactables/physmods_sync.{h,cpp}.

## Deviations from the arch doc §L8

- **{slot, moduleId} slot-deltas REPLACED by value-ops + the host-canonical array** — the
  arch sketch's slot-keyed events lose a module permanently on a concurrent same-slot
  find-free race and diverge layouts (index-cosmetic readers exist, F6); the SET property
  (F2) makes value-ops complete and the canonical array makes layout divergence
  structurally impossible.
- **The unplug path is first-class** (the arch doc, like round 1, assumed plug-only).
- **explotano's presser-locality is now MEASURED** (the arch line "damage rides the
  VictoryFloatMinusEquals choke" described a path that explosion_C's local-player-only
  targeting never enters in coop).

## Residuals (documented, accepted)

r2 the hot-op explosion is presser-only (native SP shape; a mirror-side VFX would be new
feature work). r3 a <1 s plug+unplug transient is invisible to the poll (the prop halves
still cross via their own seams). r4 the presser's socket visual + Contains-consumer
effects dip/correct <RTT on a raced op (updPhysMods idempotent, F6). r5 leaver-held module
= the inherited held-item loss. r6 poll cost ~12 raw bytes/s/peer. r9 the deny-sweep's
vanishing window (a cross-peer <2 s same-byte plug->unplug->drop->unplug chain could sweep
a legit mid-flight birth; log-visible; practically unreachable).

## /qf round map

R1 the unplug REFRAME (the unattributed 4th writer) + updPhysMods/explotano walks + the
index-reader census + setData poke. R2 the value-ops/canonical REDESIGN (the SET fact) +
leaver policy measured + the physModToActor codomain + entry attribution. R3
drain-before-adopt + the watcher-timing correction (hand exclusion = view husk only) +
speed single-writer + the damage-path question opened. R4 the deny/refund op + the destroy
seam at lines + idempotency. R5 the deny-destroy native precedent (plugInModule destroys
in-hand itself) + coldswap closed (session-static rule) + the deny-reap derivation. R6 the
r7 overstatement ADMITTED -> the deny-sweep + parked canonical + poll-resolve reuse. R7
the damage-path inference RETRACTED by measurement (explosion.json) + proto ownership +
sweep discrimination (r9). R8 "that holds".
