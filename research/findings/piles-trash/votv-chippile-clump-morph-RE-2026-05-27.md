# VOTV chipPile <-> garbageClump morph round-trip RE — 2026-05-27

Follow-up to `research/findings/piles-trash/votv-garbage-trash-interaction-RE-2026-05-27.md`
(§ "AactorChipPile_C and Aprop_garbageClump_C — runtime morph: a chipPile
spawns a clump and vice-versa during pickup/throw. This is a sibling-spawn
we do not observe today.").

Goal: full mechanics of the user-observed behaviour ---
> chipsPile lying on ground -> press E -> chipsPile transforms into ROUND
> clump (held in hand) -> throw -> on landing morphs back into a flat
> chipsPile in the native shape it had before.

Sources used (all paths relative to repo root):

- CXX header dump (decompiler-readable, UE4SS-generated):
  - `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/actorChipPile.hpp`
  - `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/actorChipPile_erie.hpp`
  - `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/actorChipPile_leaves.hpp`
  - `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/actorChipPile_wetConcrete.hpp`
  - `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/prop_garbageClump.hpp`
  - `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/prop_garbageClump_erie.hpp`
  - `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/prop_garbageClump_leaves.hpp`
  - `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/prop_garbageClump_wetConcrete.hpp`
  - `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/prop_dirtball.hpp`
  - `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/prop_garbageBag.hpp`
  - `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/enum_chipPileType_enums.hpp`
  - `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/lib_getFunc.hpp`
  - `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/mainPlayer.hpp`
  - `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/tool_garbageSpawner.hpp`
  - `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/prop.hpp` (for the `Aprop_C` canonical pickup-interface comparison)
- IDA Pro decompilation pass on `VotV-Win64-Shipping.exe`:
  - Confirmed via `mcp__ida-pro-mcp__lookup_funcs` + string scan that NO
    native impl of `turnToPile`, `toClump`, `playerGrabbed`,
    `playerTryToHold`, `checkPickup` exists in the shipping binary. Every
    one of these is pure BP bytecode, dispatched via `ProcessEvent` on the
    BP-compiled UClass. Their semantics below are reconstructed from
    (a) field shapes in the CXX dumps, (b) the canonical Aprop_C pickup
    interface, (c) cross-references to `Aprop_garbageClump_C::pile` (back-
    reference TSubclassOf<AactorChipPile_C>) and `AactorChipPile_C::clump`
    (forward TSubclassOf<Aprop_garbageClump_C>) which are the unambiguous
    runtime-morph wiring.
- Live behaviour (user-reported observed behaviour, ground truth).

Class hierarchy reminder (excerpt from the parent doc + extended this pass):

```
AActor
 +- AactorChipPile_C  (size 0x274)        + chipType, ignore[], spwnd, pile, clump
 |   +- AactorChipPile_erie_C             + restore Timeline (regrowth?)
 |   +- AactorChipPile_leaves_C           (no extra fields)
 |   +- AactorChipPile_wetConcrete_C      + dryTimer (Tick-driven dry-out)
 +- Aprop_garbageClump_C  (size 0x2F4)    + chipType, holdPlayer, canConvert,
 |                                          delayOnHit, Max, initLaunch,
 |                                          skipSave1, LifeSpan, pile, Hit, Slope
 |   +- Aprop_garbageClump_erie_C
 |   +- Aprop_garbageClump_leaves_C
 |   +- Aprop_garbageClump_wetConcrete_C  + dryTimer
 |   +- Aprop_dirtball_C  (size 0x2F4)    NEW (not in parent doc); empty subclass --
 |                                        gameplay-distinct "dirtball" reusing the
 |                                        whole clump throw/landing pipeline. The
 |                                        pile field still points back to a
 |                                        TSubclassOf<AactorChipPile_C>.
```

`Ulib_getFunc_C::getChipPileType(enum_chipPileType::Type, __WorldContext)
-> UStaticMesh*` (`lib_getFunc.hpp:19`) is the master enum-to-mesh table
used by both the chipPile's flat-on-ground mesh AND the clump's round-in-
hand mesh -- same chipType picks a flat-variant on chipPile and a round-
variant on clump. The enum has 14 values (`enum_chipPileType_enums.hpp:1`).

---

## 1. Pickup mechanism — chipPile -> clump

### 1.1 Dispatch chain (E-press on ground chipPile)

```
mainPlayer_C trace -> ActorChipPile detected as the lookAt
  -> InpActEvt_use_K2Node_InputActionEvent_41(Key)     [mainPlayer.hpp:559]
     -- E-press input action; main interact entry point. The BP graph
        downstream of this event does the lookAt + action selection.
  -> Hit-result is fed into the prop's action interface (BP duck-typed):
       chipPile.canBeCollected(out ignore)             [actorChipPile.hpp:47]
       chipPile.canPickup(out return)                  [actorChipPile.hpp:59]
       chipPile.canBeUsedHold(out return)              [actorChipPile.hpp:41]
     (Aprop_C exposes the SAME UFunction names -- chipPile is duck-typed
      into the pickable family WITHOUT inheriting from Aprop_C, see §3.)
  -> chipPile.playerTryToHold(Player, out collected)   [actorChipPile.hpp:49]
  ----> internally branches to chipPile.toClump()      [actorChipPile.hpp:64]
        which is the canonical morph entry. Return type is
        `Aprop_garbageClump_C*` -- toClump() *constructs* and returns the
        new clump (see §1.2).
```

The "E-press routes through `InpActEvt_use_K2Node_InputActionEvent_41`"
edge is the same path already used by Stage 1 grab_observer (see
`src/votv-coop/src/coop/grab_observer.cpp:147` for the existing
observer; it logs `grabbing_actor` for PHC light-grab props). chipPile
pickup is NOT a PHC grab -- there's no `grabbing_actor` set; instead
`holding_actor @ 0x0A20` is the mainPlayer field that the clump lands in.

### 1.2 What `toClump()` does (reconstructed from field shapes)

`toClump()` is declared as:

```cpp
class Aprop_garbageClump_C* toClump();           // actorChipPile.hpp:64
```

It returns the newly-spawned clump. Reconstructed body (RULE 1 -- this
is the proper canonical BP morph entry; we will hook the OUTER BP path,
not paper over):

```cpp
// PSEUDO -- reconstructed from field shapes; not the literal BP bytecode.
Aprop_garbageClump_C* AactorChipPile_C::toClump()
{
    // 1. Pick the clump class from the chipPile's `clump` TSubclassOf
    //    field. This is set by:
    //      (a) the BP CDO at compile time -- the default clump class for
    //          this chipPile lineage (the `_erie_C` chipPile has
    //          TSubclassOf<Aprop_garbageClump_erie_C> as default `clump`),
    //          OR
    //      (b) `setPropProps` / `loadData` overrides -- save-restored value
    //          (chipPile's `clump` field IS save-persisted -- see §3.3).
    TSubclassOf<Aprop_garbageClump_C> clumpCls = this->clump;  // @0x0260

    // 2. Spawn the clump at the chipPile's current world transform OR at
    //    the player's hand. Per the observed behaviour (clump appears in
    //    player's hand, not at the ground pose), the spawn transform is
    //    one of:
    //      (a) Player camera's forward-hand transform (hand IK position),
    //          followed by AttachToActor / SetActorRelativeLocation, OR
    //      (b) chipPile's world transform, followed by an attach-to-player
    //          step.
    //    The clump's `holdPlayer @ 0x0240` and `canConvert @ 0x0248` fields
    //    confirm path (b) is more likely: the clump is spawned in-world
    //    and tagged with `holdPlayer = the player` so its ReceiveTick can
    //    drive itself toward the player's hand each tick (Slope @ 0x02F0
    //    + canConvert + delayOnHit @ 0x0249 fields support an in-world
    //    held actor pattern, not a hand-bone attach).
    FTransform tr = this->GetActorTransform();
    Aprop_garbageClump_C* clump = WorldContext->SpawnActor(clumpCls, tr);

    // 3. Carry chipType across (variant identity preservation).
    clump->chipType = this->chipType;                 // @0x0238 both classes

    // 4. Carry the REVERSE-MORPH backlink. clump.pile points back to the
    //    SAME UClass we just morphed from, so turnToPile() in §2 can
    //    re-spawn the right native shape on landing.
    clump->pile = AactorChipPile_C::StaticClass();    // self class -> clump.pile @0x0260
    // (or `this->pile` if the chipPile's own `pile` field was overridden
    //  by save state -- TSubclassOf<AactorChipPile_C> @0x0258 on chipPile;
    //  this is the "respawn me as" hint. The clump prefers the saved
    //  override; CDO-default is the chipPile's own class.)

    // 5. Mark the player as the holder.
    clump->holdPlayer = Player;                       // @0x0240
    clump->canConvert = true;                         // @0x0248 -- "ready to
    //                                                   turnToPile on next
    //                                                   landed hit"

    // 6. Destroy the original chipPile. `physPreDestroyed` is the BP
    //    pre-destroy hook; the K2_DestroyActor call follows.
    this->physPreDestroyed();
    this->K2_DestroyActor();

    return clump;
}
```

Evidence the spawn-then-destroy interpretation (path (a)) is right, not a
SetActorHiddenInGame stash:

- chipPile has no `bHidden` / `cachedTransform` / "was-hidden" save flag.
- chipPile's `getData` (actorChipPile.hpp:20) writes Fstruct_save just like
  every other save-actor; if the chipPile were merely hidden during the
  clump's lifetime, its world position would already be saved correctly
  for a save-during-hold-clump scenario, but the player-held mechanic in
  VOTV is "what's in your hand is YOUR carry" not a world stash.
- `Aprop_garbageClump_C::skipSave1 @ 0x0254` strongly implies clumps are
  not normally saved (when you're holding it during save, the saved state
  is the ORIGINAL chipPile at its world spot, not a clump in your hand).
  Same pattern as inventory items: the held object's save state is its
  source pile, not the transient clump.
- Aprop_garbageClump_C's `LifeSpan @ 0x0258` suggests the clump has a
  TTL fallback (auto-revert / destroy if not converted within N seconds)
  -- only makes sense if clump is a transient, not a stashed thing.

The DESTROY half of "destroy chipPile + spawn clump" path also routes
through the existing `K2_DestroyActor` PRE observer in
`src/votv-coop/src/coop/prop_lifecycle.cpp` -- BUT that observer was
authored for `Aprop_C` subclasses (Init/destroy lifecycle wire packets);
the chipPile is NOT `Aprop_C` (per the parent RE doc, §1) so we currently
**route its destroy through the Phase 5G non-prop entity destroy path**
(`coop::non_prop_entity_sync::IdentityForDestroyingActor`,
`src/votv-coop/src/coop/non_prop_entity_sync.cpp:630`) -- which IS wired
into the K2_DestroyActor PRE observer's dispatcher. The destroy half is
already replicating correctly today (for the chipPile half of the morph).
What we MISS today is the SPAWN half: the clump's Init POST DOES fire (we
have it observed), but it broadcasts a NonPropEntityState whose
`p.locX/Y/Z` is the clump's spawn location -- which is **on the player's
hand on the host**. The receiver dutifully spawns the clump at that
location, but the receiver's player hand is somewhere else (host's
mainPlayer pose vs client's host-puppet pose drift by interpolation
buffer + occasional jitter), so the clump appears NOT in the held-pose
that the host sees, and worse, the clump's per-tick held-pose update is
NEVER subsequently broadcast (Init POST broadcasts on spawn; there's no
ReceiveTick observer for clump pose, so subsequent hand-position drift
isn't tracked).

This is the FIRST coop gap (§6.1).

### 1.3 Field transfers across morph (pickup edge)

| Source (chipPile) | Destination (clump) | Notes |
|---|---|---|
| `chipType` @ 0x0238 (uint8 enum) | `chipType` @ 0x0238 | Identical offset; identical enum. Master via `Ulib_getFunc_C::getChipPileType`. |
| `pile` @ 0x0258 (TSubclassOf<AactorChipPile_C>) | `pile` @ 0x0260 (TSubclassOf<AactorChipPile_C>) | The reverse-morph backlink. clump.pile is the class to spawn on landing. |
| (chipPile's own UClass) | `clump`-driven via chipPile's `clump` @ 0x0260 (TSubclassOf<Aprop_garbageClump_C>) -> spawned clump's actual UClass | Variant 1:1 mapping (chipPile_erie -> garbageClump_erie etc), see §5. |
| (no field) | `holdPlayer` @ 0x0240 (AmainPlayer_C*) | Set to the picking player. Critical: this IS the per-peer-divergent field that drives the clump's per-tick world transform on the host (see §4). |
| (no field) | `canConvert` @ 0x0248 (bool) | True after pickup; gates the landing -> turnToPile path. False during initial flight launch and during the `delayOnHit` window. |
| (chipPile.ignore[] @ 0x0240 -- TArray<AActor*> of damage-source ignores) | NOT carried | chipPile's `ignore` list is local to the ground pile (ignore which actors caused damage); clump has its own fresh damage state. |
| chipPile.spwnd @ 0x0250 | NOT carried | "did we already process initial spawn dust/sound?" flag, ground-pile-only. |

The variant mapping (chipPile_C -> garbageClump_C, chipPile_erie_C ->
garbageClump_erie_C, etc.) is encoded NOT by enum but by the chipPile's
own `clump` TSubclassOf<Aprop_garbageClump_C> CDO default. Each chipPile
subclass's BP class default object sets `clump` to its variant clump
sibling at compile time. See §5 for the full mapping.

---

## 2. Drop / throw mechanism — clump -> chipPile

### 2.1 Dispatch chain (release / LMB-throw on held clump)

The clump has TWO landing-trigger paths:

```
PATH A -- player input drops / throws the clump (LMB throw or release).
  mainPlayer_C: throwHoldingProp()                     [mainPlayer.hpp:443]
    or: Hold Object(useHold=false, ...)                [mainPlayer.hpp:464]
       (BP-graph: also clears holding_actor + holding_name)
    or: input_drop edge handler
  -> clump.playerHandRelease_LMB(Player)               [prop_garbageClump.hpp:71]
     or: clump.playerHandRelease_RMB(Player)
     or: clump.unequpped(Player)                       [actorChipPile.hpp:123, prop_garbageClump.hpp: -- BP-inherited via duck-type]
     (any of these clear holdPlayer + the BP graph sets initLaunch + canConvert
      and lets the clump fly per its physics. The clump's StaticMesh is
      simulated-physics; the launch is a SetPhysicsLinearVelocity from
      mainPlayer_C's `throwShit`/`throwHoldingProp` path.)

PATH B -- clump hits something (landing detection).
  StaticMesh -> ComponentHit -> the long BndEvt handler on clump's mesh:
    Aprop_garbageClump_C::BndEvt__prop_garbageClump_StaticMesh_K2Node_ComponentBoundEvent_0_ComponentHitSignature__DelegateSignature
                                                       [prop_garbageClump.hpp:103]
  -> Branches inside the BP graph on:
       canConvert == true   (only convert if we've been picked up)
       delayOnHit == false  (skip initial overlap with player's hand)
       NormalImpulse mag > Max
       Slope of Hit.Normal sane (not a wall)
  -> Calls clump.pile (TSubclassOf<AactorChipPile_C> @0x0260) as the
     "respawn me as" class
  -> Spawns chipPile via WorldContext->SpawnActor at the clump's current
     world transform (location = Hit.Location, rotation aligned to
     Hit.Normal).
  -> Calls clump.K2_DestroyActor().
```

The `turnToPile(FVector Velocity)` UFunction (`actorChipPile.hpp:62`) is
on the **CHIPPILE side, not the clump**. It is the chipPile's post-spawn
"I just got created from a clump landing" entry. Reconstructed body:

```cpp
// PSEUDO -- on CHIPPILE (the freshly-spawned one).
void AactorChipPile_C::turnToPile(FVector Velocity)
{
    // Velocity = the clump's pre-landing velocity. Used to drive impact
    // dust / sound and maybe align spread direction.
    this->spwnd = true;
    // Standard prop-side spawn flow runs from Init/UCS already; turnToPile
    // is the "I'm a re-spawn from a clump landing, not a fresh world
    // spawn" annotation. Likely fires impact dust + sets initial mesh.
}
```

Crucially: the `turnToPile` ufunction is called from the clump's hit
handler ON THE NEW CHIPPILE actor, not on the clump itself. The clump's
hit handler does the spawn then `turnToPile(velocity)` on the new
chipPile, then `K2_DestroyActor` on itself.

### 2.2 What determines the new chipPile's class

```
clump.pile -> TSubclassOf<AactorChipPile_C> @ 0x0260
```

This is the same field that was written during `toClump()` at pickup
(§1.2 step 4). Round-trip identity is preserved: pickup `chipPile_erie_C`
-> clump's `pile` field is `chipPile_erie_C` -> landing spawns
`chipPile_erie_C`. The native shape DOES round-trip, matching the
user-observed behaviour exactly.

### 2.3 What field transfers across morph (drop edge)

| Source (clump) | Destination (new chipPile) | Notes |
|---|---|---|
| `chipType` @ 0x0238 | `chipType` @ 0x0238 | Variant carried back. |
| `pile` @ 0x0260 | -> used as the SPAWN CLASS for the new chipPile | The class is the carrier; the value is "consumed" not "copied". |
| (no field) | `clump` @ 0x0260 (the new chipPile's clump field) | Set by the new chipPile's CDO/`UserConstructionScript` to its default; or, if there's a save-aware path, derived from the just-destroyed clump's class. The user-observed roundtrip suggests CDO-default is sufficient (a fresh chipPile would morph back into its CDO-default clump class on re-pickup -- which equals the just-destroyed clump's class anyway, because the mapping is 1:1 per the CDO chain). |
| (clump's holdPlayer, canConvert, delayOnHit, initLaunch, Max, LifeSpan, skipSave1, Hit, Slope) | NOT carried | clump-only transient state. |
| clump's world transform (Loc + Rot) | new chipPile's spawn transform | Aligned to Hit.Normal for ground-flushness. |

---

## 3. Identity continuity across morph

This is the critical question for our wire identity scheme.

### 3.1 Save-Key continuity — NO

Neither `AactorChipPile_C` nor `Aprop_garbageClump_C` is a subclass of
`Aactor_save_C` -- both inherit directly from `AActor`. They use a `setKey`
UFunction (chipPile has it: actorChipPile.hpp:130, clump has it:
prop_garbageClump.hpp:55) but the Key field, IF it exists, is BP-VAR-only
(not in the CXX struct dump) -- which means it's stored on the BP-VM-only
side of the actor, not at a stable native offset. The existing parent RE
doc (§ Wire sync today: NONE) corroborates: chipPile/clump don't carry a
save Key for the same reason -- they're transient world entities, not
save-persisted across world unload.

Save persistence works DIFFERENTLY for these two classes:

- **chipPile**: IS saved as part of the world snapshot (its `getData` /
  `loadData` are called during save/load). The save record stores
  position + rotation + chipType + pile/clump subclasses. When a save is
  loaded, a chipPile is re-spawned via the world snapshot replay system
  (the `prop_lifecycle` Aprop_C-style replay does NOT cover chipPile;
  this is what `coop::non_prop_entity_sync::ReplaySnapshotForJoinedClient`
  in `src/votv-coop/src/coop/non_prop_entity_sync.cpp:582` handles for
  connect-edge snapshots).
- **clump**: `skipSave1 @ 0x0254` flag exists -- the clump is normally NOT
  saved (transient). When held by player during save, the SP code saves
  the player's held-item descriptor (chipType + pile class + a marker)
  separately; on load, the held clump re-materializes from that descriptor
  + holdPlayer assignment. (Verification needed via runtime probe -- see
  §7 OPEN.)

### 3.2 Identity verdict for our wire scheme

- **chipPile-A -> clump -> chipPile-B** are THREE distinct UObjects.
  chipPile-A is destroyed during pickup. clump is destroyed during
  landing. chipPile-B is a fresh world-spawned UObject. Pointer continuity
  is BROKEN at both transitions.
- The **chipType** + **pile/clump TSubclassOf** fields preserve enough
  state for round-trip behaviour to look continuous to the player.
- **Our existing wire identity is per-UObject (host-minted sessionId,
  monotonic from `g_nextSessionId` in
  `src/votv-coop/src/coop/non_prop_entity_sync.cpp:144`).** This means
  the SAME logical "chipPile of grass clippings" gets THREE different
  sessionIds across one pickup-throw cycle:
  - chipPile-A: sessionId X (broadcast at Init)
  - clump: sessionId Y (broadcast at Init when toClump spawns it)
  - chipPile-B: sessionId Z (broadcast at Init when turnToPile spawns it)
- We BROADCAST destroys for both X (when chipPile-A dies) and Y (when
  clump dies) via the K2_DestroyActor PRE -> IdentityForDestroyingActor
  -> NonPropEntityDestroy path. Receiver applies them in order.
- The client mirror sees: spawn X -> destroy X + spawn Y (handheld) ->
  destroy Y + spawn Z. From the receiver's POV the visual result IS the
  morph round-trip, AS LONG AS the spawn locations are correct.

### 3.3 Save-Key continuity verdict

We do NOT need cross-morph identity continuity. Each of the three
distinct UObjects gets its own sessionId; the client mirror tracks each
separately. The morph is implicitly replicated by the existing Init POST +
K2_DestroyActor PRE observers ALREADY firing on all three classes (the
class table in `non_prop_entity_sync.cpp:553-562` already covers
`actorChipPile_C` + its 3 subclasses and `prop_garbageClump_C` + its 3
subclasses; we already broadcast on Init for each).

What we MISS today (gaps that block correct end-to-end behaviour):

- The clump's **held-pose stream** (see §4 + §6.1).
- The **echo of toClump/turnToPile across the wire** -- specifically, the
  RELATIONSHIP between the destroyed chipPile and the spawned clump
  (and vice versa). Today the client receives them as independent
  spawn/destroy events. Receiver-side this is OK functionally (the right
  actor exists at the right time) but means: if the client's network
  arrives reordered (destroy of chipPile-A arrives AFTER spawn of clump
  -- unlikely on reliable channel but possible across a packet-loss
  retry), the client briefly sees BOTH actors. Acceptable for Inc 2.
- The `Aprop_dirtball_C` subclass is NOT in our class table (`Install()`
  in `non_prop_entity_sync.cpp:553-562`). Dirtball pickups use the
  garbageClump pipeline (`prop_dirtball.hpp:4` declares it as
  `: public Aprop_garbageClump_C`) -- but with our table missing it,
  `FindEntryForActor` returns null on dirtball spawn -> no broadcast, no
  sync. See §6.4.

---

## 4. Held-pose mechanism — what drives the clump's world transform while held

Three candidate mechanisms (in order of evidence):

### 4.1 Candidate A: clump.ReceiveTick reads holdPlayer + computes hand pose

Strongest evidence:

- `Aprop_garbageClump_C::ReceiveTick(DeltaSeconds)` exists
  (prop_garbageClump.hpp:105).
- `holdPlayer` (AmainPlayer_C*) is at 0x0240 -- the holding player ref.
- `canConvert @ 0x0248` (bool) + `delayOnHit @ 0x0249` (bool) +
  `initLaunch @ 0x0250` (float) + `Max @ 0x024C` (float) -- the field
  cluster has the shape of a "while held, do per-tick stuff; on launch,
  apply velocity; on hit, convert" state machine.
- `Hit @ 0x0268` (FHitResult, 0x88 bytes) -- a per-tick or last-hit
  cached hit record.

Per-tick logic (reconstructed):

```cpp
// PSEUDO -- Aprop_garbageClump_C::ReceiveTick
void Aprop_garbageClump_C::ReceiveTick(float Delta) {
    if (this->holdPlayer && !this->canConvert) {
        // STILL IN HAND (canConvert flips true only AFTER initLaunch elapses).
        // Drive ourselves to the player's hand origin each tick.
        FTransform handTr = this->holdPlayer->GetHandHoldTransform();  // BP helper
        this->SetActorLocation(handTr.Location, sweep=false);
        this->SetActorRotation(handTr.Rotation);
        // Or equivalently: K2_AttachToActor(this, holdPlayer, "handSocket")
        // done once at toClump() and we just live attached. Either way,
        // the clump WORLD transform follows the player's hand bone each
        // tick on the OWNING peer.
    }
    if (this->canConvert && this->delayOnHit) {
        this->delayOnHit = (timeSinceLaunch < initLaunch);
    }
    // Lifetime: auto-destroy if LifeSpan elapsed (transient clump).
    if (this->LifeSpan > 0.f && timeAlive > LifeSpan) {
        this->K2_DestroyActor();
    }
}
```

### 4.2 Candidate B: mainPlayer drives the clump from `Hold Object` UFunction

Aprop_garbageClump_C has NO `K2_AttachToActor` UFunction in its dump
(garbage_sync.cpp confirmed via earlier grep). But mainPlayer_C has
`Hold Object(bool useHold, AActor* Manual, bool& collected)` UFunction
(mainPlayer.hpp:464) and `holding_actor @ 0x0A20` + `holding_name @
0x0A30` fields -- this is the manual-hold (heavy carry / chipPile)
path, distinct from `grabbing_actor @ 0x07D0` (PHC light grab).

Candidate B says: `Hold Object` runs in mainPlayer's tick path, reads
`holding_actor`, and calls `holding_actor->SetActorLocation(handTr)`.
This is consistent with the clump being inert (no Tick body of its own)
and the player driving the held object's world transform from outside.

The two candidates are observationally identical from a coop-sync POV:
EITHER WAY the world-transform of the clump is recomputed every tick on
the OWNING peer from the player's hand pose, and we need a per-tick wire
broadcast OR an attach-relationship sync.

### 4.3 Candidate C (recommended): host attaches clump to its mainPlayer; client mirrors the attach

Per UE4 semantics, `K2_AttachToActor(child, parent, socketName)` makes
child's world transform automatically follow parent's bone -- no per-tick
override needed. If the host's BP graph DOES call this somewhere in
`toClump` or `playerGrabbed` or `beginHoldingObject` (none of which are
in our IDA-visible code), then:

- On the host: clump is parented to mainPlayer_C; SetActorLocation calls
  on the host mainPlayer DON'T propagate to clump's transform stream
  because the clump's world transform = parent.bone + relative.
- On the client: we received a NonPropEntityState at the spawn instant
  (with the host's hand pose AT THAT MOMENT as `p.locX/Y/Z`), spawned an
  un-attached mirror clump there, and the mirror does NOT follow the
  client's view of the host's puppet hand. Drift starts immediately.

This is the LIKELY truth -- it explains why we don't see a per-tick clump
broadcast in any of the existing observer paths AND why the destroy-on-
landing currently produces visually-correct chipPile spawns even though
we've never specifically synced the clump's flight.

For coop fidelity we'd want the client mirror to ALSO be attached to its
local-view of the host's puppet hand. That requires a wire packet that
carries the "attach clump-Y to host-puppet's hand-socket on the client"
instruction. See §6.2.

### 4.4 Decision (for §6 implications)

Don't paper over by streaming per-tick clump pose (200 Hz waste). Instead:

1. Add a **clump attach packet** that fires once at clump spawn on the
   host: `{clumpSessionId, hostPeerSessionId, attachToHandSocket: true}`.
   Receiver attaches the mirror clump to the host-puppet actor's hand
   socket. Subsequent host-puppet pose stream (already running every
   frame via `coop::remote_player::PoseUpdate`) automatically drives the
   clump's world transform on the client.
2. Add a **detach + launch packet** that fires once at LMB-throw /
   release on the host: `{clumpSessionId, launchVel: FVector}`. Receiver
   detaches the mirror clump from host-puppet, applies the launch
   velocity. Physics then takes over on the client (sim runs locally;
   physical landing position may differ slightly from host's).
3. Treat the landing morph as a normal Init POST broadcast of the new
   chipPile (which we already do, see `OnInitPost` in
   `non_prop_entity_sync.cpp:378`) PLUS an explicit destroy of the
   clump (which we also already do via K2_DestroyActor PRE +
   IdentityForDestroyingActor).

The IMPORTANT bit: the new chipPile's world position on the client should
NOT come from the receiver's locally-simulated landing -- it should come
from the host's NonPropEntityState broadcast which carries the AUTHORITATIVE
landing position. This is already what we do; no change needed there.

---

## 5. Variant mapping — chipPile_X <-> garbageClump_X

Per the CXX dumps the variant tree is 1:1:

| chipPile class | clump class (CDO default) | dryTimer field? | restore Timeline? |
|---|---|---|---|
| `AactorChipPile_C` | `Aprop_garbageClump_C` | no | no |
| `AactorChipPile_erie_C` | `Aprop_garbageClump_erie_C` | no | YES (BP `restore` Timeline @ 0x0290; possibly used for regrowth or particle-restore) |
| `AactorChipPile_leaves_C` | `Aprop_garbageClump_leaves_C` | no | no |
| `AactorChipPile_wetConcrete_C` | `Aprop_garbageClump_wetConcrete_C` | YES (`dryTimer` on BOTH chipPile_wetConcrete and clump_wetConcrete) | no |

The mapping is encoded via:

- chipPile's `clump` TSubclassOf<Aprop_garbageClump_C> @0x0260 -- CDO default per subclass.
- clump's `pile` TSubclassOf<AactorChipPile_C> @0x0260 -- CDO default per subclass.

Both are RUNTIME-MUTABLE (not const) but the BP graph only writes them
in `toClump()` / `turnToPile()`. Save state CAN persist them, so a save
where someone painted a wet-concrete clump's `pile` to point at a
leaves-chipPile (via console or BP graph edge cases) would round-trip
that override. Our wire layer doesn't need to read these fields directly
from the wire -- the client receiver looks up the CLASS POINTER directly
from the wire's `variantClassHash16` (CRC32 low-16 of the class name
string) via `FindEntryForWireSpawn` in `non_prop_entity_sync.cpp:110`.
The CDO-default mapping is therefore preserved naturally; explicit overrides
are NOT preserved across the wire (they'd require a separate field-write).
This is acceptable -- explicit overrides are vanishingly rare in normal
gameplay.

The **fifth class to add to our table** is `Aprop_dirtball_C` (subclass
of `Aprop_garbageClump_C` -- size 0x2F4 same as parent, no new fields).
It's a separately-named gameplay clump that uses the same machinery; the
dirtball's `pile` field still points to a chipPile subclass (which one
exactly we'd need a runtime probe to know -- §7 OPEN-A).

---

## 6. Coop sync implications — what packets / hooks beyond STAGE 2

### 6.1 Gap: spawn-location drift on the held clump

Today: clump Init POST broadcasts `NonPropEntityState` with `loc = clump
world transform AT THE INSTANT toClump() finishes spawning it`. On the
host this is the host's mainPlayer hand pose. On the client mirror the
spawn lands at the host's-hand-AT-THAT-INSTANT, which is reasonably close
to where the client's view of the host puppet's hand is -- but no
subsequent updates -> immediate drift the moment the host's player moves.

### 6.2 Plan: a `HeldEntityAttach` packet (NEW, Inc 5)

Build on STAGE 2 (`non_prop_entity_sync`) -- add ONE new wire packet:

```cpp
// protocol.h, ReliableKind 11 (next free per the design doc free-list):
enum class ReliableKind : uint8_t { ..., HeldEntityAttach = 11, ... };

struct HeldEntityAttachPayload {
    uint8_t  entityClass;            // NonPropEntityClass (GarbageClump or ActorChipPile -- only clump uses this today; ActorChipPile reserved for future "held chipPile" if BP supports it)
    uint8_t  peerSessionId;          // owning player (0 = host, 1.. = client-N)
    uint8_t  attached;               // 1 = attach to player's hand; 0 = detach + launch
    uint8_t  _pad;
    uint32_t identity;               // the clump's sessionId
    float    launchVelX, launchVelY, launchVelZ;  // valid only when attached=0
    // Total: 20 B.
};
static_assert(sizeof(HeldEntityAttachPayload) == 20,
              "HeldEntityAttachPayload must be 20 bytes");
```

Host emits `HeldEntityAttach{attached=1, peerSessionId=ownerPeer}`
- in the SAME tick the clump's Init POST fires from `toClump()` --
  ideally right after the NonPropEntityState (the receiver applies
  spawn -> then immediately attaches).

Host emits `HeldEntityAttach{attached=0, launchVel=throwVel,
peerSessionId=ownerPeer}` in `playerHandRelease_LMB` / `throwHoldingProp`
POST. Receiver detaches its mirror clump from the host-puppet hand,
applies the launch velocity via `SetPhysicsLinearVelocity`. The clump's
local-sim landing is allowed to drift from the host's landing because
the AUTHORITATIVE chipPile spawn (the morph result) DOES carry the
host's world location via the upcoming NonPropEntityState (issued from
the new chipPile's Init POST as it spawns).

### 6.3 Hooks needed (beyond what STAGE 2 already has)

| # | Hook | Class | UFunction | Side | Purpose |
|---|---|---|---|---|---|
| H1 | `toClump` POST | `AactorChipPile_C` (+ subclasses via inheritance) | `toClump` | Host | Read the return-value (the spawned clump), correlate to the new clump's sessionId (which by then has been minted by its Init POST), emit `HeldEntityAttach{attached=1}`. POST timing avoids ordering issues. |
| H2 | `playerHandRelease_LMB` POST | `Aprop_garbageClump_C` (+ subclasses) | `playerHandRelease_LMB` | Host | Detect throw, capture velocity via the clump's StaticMesh.GetPhysicsLinearVelocity (or use the param-frame from `throwHoldingProp` -- needs a separate probe). Emit `HeldEntityAttach{attached=0, launchVel}`. |
| H3 | `playerHandRelease_RMB` POST | `Aprop_garbageClump_C` (+ subclasses) | `playerHandRelease_RMB` | Host | Same as H2 but for RMB-drop. May differ in launch velocity / orientation. |
| H4 | `unequpped` POST (the misspelled UFunction name -- intentional, matches BP) | `Aprop_garbageClump_C` (+ subclasses) | `unequpped` | Host | Fallback detach path: any unequip flow that bypasses the explicit Release pair. |
| H5 | `turnToPile` POST | `AactorChipPile_C` (+ subclasses) | `turnToPile` | Host | This is the new chipPile telling us "I just spawned from a clump landing". POST observer reads `self` = new chipPile; broadcasts its NonPropEntityState (Init POST may have already done this -- this is a belt-and-suspenders to ensure landing-position correctness even if a future BP refactor moves chipPile-from-clump spawn out of the Init path). |

H5 is OPTIONAL today -- the Init POST observer already covers the new
chipPile broadcast. H1-H4 are REQUIRED for the held-pose-following
behaviour the user expects.

### 6.4 Class table extension

`coop::non_prop_entity_sync::Install()` (`non_prop_entity_sync.cpp:547`)
currently registers 9 classes. Add 1: `Aprop_dirtball_C` (subclass of
`Aprop_garbageClump_C`). This is a one-line addition to the
`TryAddClass(...)` cascade, plus bumping `kExpectedClasses` from 9 to 10.

### 6.5 Existing observers that already fire (no additional work)

- `OnInitPost` on chipPile and clump base + 6 subclasses -- already broadcasting
  NonPropEntityState on every spawn (including post-morph spawns).
  See `non_prop_entity_sync.cpp:378`.
- `K2_DestroyActor` PRE via `prop_lifecycle` -- routes through
  `IdentityForDestroyingActor` (`non_prop_entity_sync.cpp:630`) so the
  destroy half of each morph half already broadcasts.

### 6.6 Single PRE-cancel suppressor we should consider for INCORRECT behaviour

If a future test reveals the client BP graph ALSO runs `toClump()` /
`turnToPile()` independently (e.g. on a chipPile/clump that the host
spawned but the client received over the wire, then the client's player
presses E on it -- the client's BP would try to do its own toClump,
producing a duplicate clump alongside the host's wire-mirrored clump),
the proper fix is:

- PRE-cancel `playerTryToHold` on the client for `AactorChipPile_C` /
  `Aprop_garbageClump_C` -- the host alone handles the morph; client's
  E-press routes to the host via a NEW packet (NOT chipPile-specific;
  this is the general "client-initiated remote-action" packet that
  we'll need for ALL host-authoritative pickup pipelines once Phase 5N
  / Phase 5G converges).

This is OUT OF SCOPE for the chipPile-morph fix; it requires the broader
"client-can-initiate-pickup" feature. Document as §7 OPEN-B.

---

## 7. Open questions needing runtime probe

### 7.1 OPEN-A: dirtball's chipPile mapping

`Aprop_dirtball_C` is empty (no new fields). Its CDO's `pile` field
default value is set in its BP class default. What chipPile class does
dirtball revert to on landing? Two hypotheses:

(a) Default `Aprop_garbageClump_C::pile` (which the CDO inherits if
    dirtball doesn't override) -- a regular `AactorChipPile_C` of some
    chipType.
(b) An override pointing at a dirtball-specific chipPile (no such
    subclass exists per the CXX dumps -- so (a) is more likely).

PROBE: `[probe] garbage_morph=1`-gated PRE on `Aprop_dirtball_C::Init`
that logs `*(TSubclassOf<AactorChipPile_C>*)((uint8*)self + 0x0260)` --
the CDO-inherited `pile` field. ONE hands-on test (host spawns a
dirtball via toolgun, throws it, observes the landing class via the
existing `non_prop_entity_sync` log line at chipPile spawn).

### 7.2 OPEN-B: clump's BP-VM-only fields (the "Key" question)

The CXX dumps don't show a `Key` UPROPERTY on `Aprop_garbageClump_C` or
`AactorChipPile_C`. But both implement `setKey(FName)` and `GetKey(FName&)`
UFunctions. Where is Key stored? Two hypotheses:

(a) In a BP-VM-only variable (stored in the BP class's UberGraphFrame +
    UProperty descriptors, accessible only via the BP property table,
    not at a native struct offset).
(b) The setKey/GetKey are no-ops or stubs (overridden Aprop_C-compatible
    interface ONLY for duck-typed BP-graph compatibility, with no actual
    storage backing them).

PROBE: `[probe] garbage_morph=1`-gated -- after a chipPile spawn,
`R::CallFunction(actor, GetKeyFn, &keyOut)` and log the result. If it's
`NAME_None`, hypothesis (b) is correct. If it's a meaningful name, (a)
is correct AND we should investigate writing it via setKey rather than
direct memory write.

### 7.3 OPEN-C: held-pose mechanism — attach vs. tick-driven

§4 lays out three candidates. The PROBE is straightforward:

- `[probe] garbage_morph=1`-gated PRE on `Aprop_garbageClump_C::ReceiveTick`,
  log `self->GetActorLocation()` once per second.
- During a hands-on test (host picks up clump, walks 5 m, the world location
  changes per tick), verify the change rate.
- If location changes per tick = candidate A or B (tick-driven).
- If location is "stuck" but the rendered location moves (component-relative
  via attach) = candidate C.

Discriminating A vs B doesn't matter for our wire layer -- both are
OWNING-PEER-LOCAL.

### 7.4 OPEN-D: throw-velocity capture path

mainPlayer_C has `throwHoldingProp()` (mainPlayer.hpp:443) and the
clump has `playerHandRelease_LMB/RMB`. The launch velocity flows from
one of:

- `mainPlayer_C::throwShit(InputPin, NewVel)` (line 470) -- explicit
  FVector NewVel param, dispatchable on a UPrimitiveComponent (the
  clump's StaticMesh). This is the most likely launch path.
- Or a direct `SetPhysicsLinearVelocity` on the clump's StaticMesh
  (covered by the existing grab_observer
  `GrabObserver_PrimComp_SetLinearVelocity_PRE`, see
  `grab_observer.cpp:130`).

PROBE: Re-use the existing `GrabObserver_PrimComp_SetLinearVelocity_PRE`
log + verify it fires with the clump's StaticMesh as the `self`
component at the LMB-throw moment. If so, we can capture launch
velocity from THAT existing observer + correlate to the clump's
sessionId via the StaticMesh's outer-actor pointer (a 1-line
`R::OuterOf(self->staticMesh)` resolution). No new observer needed for
H2/H3 in §6.3 -- just an enrichment of the existing one.

---

## 8. Summary — what changes in code

The MORPH ROUND-TRIP works in TODAY's `non_prop_entity_sync` with one
caveat (held-pose drift). The MINIMAL CHANGE to make it correct
end-to-end is:

1. **One new ReliableKind** -- `HeldEntityAttach` (20 B). Carries either
   "attach clump-N to peer-K's hand" or "detach + launch with velocity
   V". Bump `kProtocolVersion` 9 -> 10.
2. **Four new POST observers** on the host side, all in a NEW file
   `src/votv-coop/src/coop/held_entity_sync.cpp` (separate from
   `non_prop_entity_sync.cpp` per the modular file-size rule; the new
   feature is "held-state mobility" not "entity state"):
   - `AactorChipPile_C::toClump` POST -> emit attach (read return-value
     clump, look up its sessionId, attach to owning peer's puppet hand).
   - `Aprop_garbageClump_C::playerHandRelease_LMB` POST -> emit detach +
     launch.
   - `Aprop_garbageClump_C::playerHandRelease_RMB` POST -> emit detach +
     launch (RMB-drop path).
   - `Aprop_garbageClump_C::unequpped` POST -> emit detach (fallback for
     unequip flows that bypass the explicit Release pair).
3. **Receiver path** in the same file: `ApplyHeldEntityAttach` looks up
   the local mirror clump (via `g_clientActorByIdentity` in the existing
   `non_prop_entity_sync`), attaches/detaches to the host-puppet hand,
   applies launch velocity on detach.
4. **Class table extension**: add `Aprop_dirtball_C` to
   `non_prop_entity_sync::Install()` and bump `kExpectedClasses` to 10.
5. **Optional belt-and-suspenders**: add `turnToPile` POST observer
   broadcasting NonPropEntityState (only needed if a future regression
   moves the new chipPile spawn out of the Init path; ship without).

NO carve-outs / suppressions. The host's BP graph runs `toClump` and
`turnToPile` normally; the client's BP graph is gated by the existing
Inc 3 spawner suppression (no client-side natural chipPile spawning) +
the client-side morph happens only via wire mirror (the client never
presses E on a chipPile in this Inc -- when client-initiated remote
actions land in a later Inc, we'll add a `RemoteAction::Pickup` request
packet that the host applies authoritatively).

Round-trip verification scenario:

1. Host stands in front of a chipPile (let's say `actorChipPile_erie_C`),
   client connects, both see it.
2. Host presses E. Host BP: `toClump()` -> spawn
   `Aprop_garbageClump_erie_C`, destroy chipPile.
3. Host wire: NonPropEntityDestroy(chipPile, sessionId X), NonPropEntityState
   (clump, sessionId Y, variantHash=erie), HeldEntityAttach(Y, hostPeer=0, attached=1).
4. Client: destroy mirror-chipPile-X, spawn mirror-clump-Y at host pose,
   attach mirror-clump-Y to client's view of host-puppet's hand. Mirror
   clump follows host puppet's hand for entire carry.
5. Host throws (LMB). Host BP: launch clump, clump-tick eventually hits
   ground, BP spawns new chipPile (erie variant, from clump.pile), destroys
   clump.
6. Host wire: HeldEntityAttach(Y, attached=0, launchVel=V), NonPropEntityDestroy
   (clump Y), NonPropEntityState(new chipPile, sessionId Z, variantHash=erie).
7. Client: detach mirror-clump-Y, apply launchVel=V (clump flies). When
   the host's authoritative destroy + spawn-of-Z arrives, client destroys
   mirror-clump-Y and spawns mirror-chipPile-Z at the host's chosen
   landing pose. ROUND-TRIP COMPLETE.

The user-observed behaviour (chipPile -> round clump in hand -> throw ->
chipPile of original variant on landing) is replicated faithfully.
