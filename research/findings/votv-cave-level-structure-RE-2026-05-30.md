# VOTV cave / dimensions / dreams / backrooms structure RE — one UWorld + streaming sublevels (2026-05-30)

## Question

User (2026-05-30): VOTV has the cave/mine, plus "dimensions" — a procedural
backrooms you're teleported to when below the main map, a cave-like dimension of
"stacked overlapping rocks (not a real modeled map)", and (clarified) **sleep
dreams** = minigames when you sleep in bed (succeed to sleep; fail = wake up
ragdolled out of bed). The **backrooms is a separate system from the dreams.**

Coop question: how do peers travelling in/out of these sync? The make-or-break
fact: **separate `UWorld` (OpenLevel map travel = the hard nightmare case) or
same persistent `UWorld` (in-world teleport / streaming sublevel = no travel
machinery)?**

## Method + an honest correction trail

Static RE off the two UE4SS object dumps (`UE4SS_ObjectDump_GAMEPLAY_SAVE.txt`
~83 MB, player on surface; `..._MAIN_MENU.txt` ~68 MB) **plus string-grep of the
cooked `.pak`** (`VotV-WindowsNoEditor.pak`, 8.2 GB) to see assets that aren't
loaded in either snapshot.

This finding was revised TWICE as the user corrected my reads — recorded as a
caution against trusting one snapshot: (1) I first concluded a blanket "zero
coop work"; the user noted the procedural dimensions, which a surface snapshot
never captures. (2) I then conflated the dreams with the backrooms; the user
clarified dreams = sleep minigames, backrooms = separate. The pak grep then
revealed the streaming-sublevel layer that neither object dump showed. **Lesson:
an object dump proves what IS loaded; it cannot disprove on-demand content — for
that you must read the pak or probe at runtime.**

## Bedrock fact (CONFIRMED): ONE persistent gameplay UWorld

- Gameplay UWorld instances: **1** — `World /Game/maps/untitled_1.Untitled_1`
  (menu is its own `/Game/menu`; never simultaneous). ULevel instances: **1**
  (`untitled_1...:PersistentLevel`, 50,637 placed actors).
- The only full UWorld swap in the lifecycle is boot/menu->gameplay
  `open untitled_1` (and a mid-session save-LOAD re-`open`) — both already
  handled by the dead-Prop-Element reaper + world-change re-seed.
- The many `/Game/maps/untitled_0..115`, `tutorial*`, `sandbox`, junk-named maps
  in the pak are dev iterations / other modes, NOT runtime-loaded dimensions.

## The four "other place" systems (all inside untitled_1)

### 1. Modeled mine/cave — baked in, ZERO coop work (CONFIRMED)
60 `caveSegment_*` StaticMeshActors + `elevatorOfDeath` elevator + `cargoLift*` +
`ladder*`, placed directly in `untitled_1:PersistentLevel`, resident even while
topside. Descending is moving coordinates within untitled_1. Existing whole-map
pose/prop sync already covers a peer down the mine. **No work.**

### 2. Dimension SUBLEVELS — streaming sublevels of untitled_1 (CONFIRMED to exist; load mechanism strongly inferred)
The pak has `/Game/maps/sublevels/`: **`sl_caves`, `sl_goop`, `sl_woid`,
`sl_poopDim`, `sl_alexpdim`, `sl_place`, `sl_SaveTest`** (+ deprecated
`sl_backroomsObsolete`, and `/Game/maps/sl_cavesObsolete`). Each has its own
`sl_*_C` level blueprint. The user's "cave dimension of stacked rocks" is most
likely **`sl_caves`** (distinct from the baked mine in system 1).
- These are UE **streaming sublevels** (the `/maps/sublevels/` convention), loaded
  on demand via `LoadStreamLevel` (the engine fn IS in our SDK). That is why the
  surface snapshot showed **0 `ULevelStreaming` instances** — none was active;
  `untitled_1` does not statically register them, so they're dynamic/Kismet loads
  whose handle exists only while loaded.
- **A streaming sublevel loads INTO the persistent UWorld — it is NOT an
  `OpenLevel` world swap.** The player stays in `untitled_1`; the sublevel's
  actors are added/removed. So entering a dimension = NO world swap, NO mass
  purge of persistent props, the reaper/re-seed/world-context machinery is NOT
  triggered. (Strong inference; runtime-confirm below.)

### 3. Sleep DREAMS — procedural sleep minigames (CONFIRMED)
`/Game/objects/dreams/dream_*` (`dream_room`, `dream_boulders`/`dreamBoulder`,
`dream_climb/jump/run/ufo/mann/wend/burger/fill`) are spawnable BP **actors**.
Flow (matches the user): `mainGamemode_C` `bed`/`bedSleepProb`/`fellAsleep`/
`isSleep`/`sleepingPawn` -> `createDream`/`processDream` (procedural,
`RandomIntegerInRange` in the `gen` fns — global RNG, NOT stream-seeded) ->
succeed = sleep, or fail = `mainPlayer_C::wakeup` + `ragdollMode`/`isRagdoll`/
`AutoRagdollGetup` (thrown out of bed). Spawned in untitled_1; no world swap.

### 4. The BACKROOMS — separate, below-map, likely procedural (PARTIAL — runtime residual)
Gated by `mainGamemode_C` `backroomsEnabled`/`canBackrooms`/`backroomsPass`/
`cheatEnableBackrooms`; entered when below-map (`killZheight` threshold) via
`mainPlayer_C::teleportWObackrooms` (a **teleport**, not a travel). The only
baked backrooms sublevel is **`sl_backroomsObsolete`** (deprecated) — consistent
with the user's "procedural" description: the current backrooms appears to be
generated, NOT a baked sublevel (no current backrooms map/geometry in the pak
under a clear name, and no distinctly-named generator blueprint surfaced). The
current generation mechanism is **not statically visible** (BP bytecode) — this
is the main RESIDUAL.

## Coop implications (corrected — bounded work, NOT the level-travel nightmare)

Unifying point: **it is all ONE UWorld** (persistent `untitled_1` + in-world
teleports + streaming sublevels). So there is **NO `OpenLevel` world swap, NO
mass purge, NO lockstep travel, and NO engine requirement to force-co-locate the
party.** The feared nightmare (separate-level peers needing the MTA
Dimension/Interior partitioning we cut) does **not** apply.

The real, bounded work — reuses existing actor-spawn/mirror + reliable-event
machinery:
1. **Host-authoritative "who is in which place" state** (surface / a dimension /
   backrooms), pushed to peers over the reliable channel.
2. **Make the place's geometry exist locally on each client** so a peer there
   renders correctly (the peer's POSE already syncs — same world):
   - Baked dimension sublevels (`sl_caves`, etc.): each client `LoadStreamLevel`s
     the SAME sublevel — geometry is identical, **no procedural replication**.
     Cleanest case.
   - Procedural backrooms / sleep dreams: host generates + **replicates the
     spawned actors** (per-actor spawn sync — can't share a seed since RNG isn't
     stream-based). Reuses the Prop/Npc `MirrorManager` shape
     ([[feedback-registry-register-mirror-pattern]]).
3. **Design decision — shared vs per-player place.** Lean SHARED for
   dimensions/backrooms (genre + our whole-map/no-AOI choice); sleep dreams may
   be **personal/paused** (a solo minigame) and might need no sync at all — a
   gameplay-design call, not an engine constraint (one world either way).

This is **sublevel-streaming + actor/state-sync scope, NOT level-travel scope.**

## Residual RE (runtime/bytecode only — reflection has hit its limit)

1. **Confirm same-world** for a dimension and the backrooms: enter one
   (`cheatEnableBackrooms`, or hands-on below-map / sleep) and verify
   `engine::g_worldContext` + world name are UNCHANGED and the reaper logs NO
   mass-purge episode. (A world swap would change both.)
2. **Current backrooms mechanism:** procedural generator vs a (renamed) sublevel;
   dump the live backrooms actor set, count, parent level, any stored seed.
3. **Per-dimension load path:** confirm `sl_caves`/`sl_goop`/etc. are
   `LoadStreamLevel`'d into untitled_1 (vs anything exotic).
4. **Do sleep dreams need coop sync at all?** (personal-minigame vs shared).
   Autonomous smoke can't reach below-map/sleep -> dev-probe/hands-on.

## Verdict

Modeled mine: zero work. Dimensions/backrooms/dreams: **same UWorld, no level
travel** (sublevel streaming + in-world teleport) — a genuine but BOUNDED future
coop feature (host-auth place-membership + per-client sublevel load / procedural
actor replication), reusing existing machinery. NOT a separate-world rewrite.
