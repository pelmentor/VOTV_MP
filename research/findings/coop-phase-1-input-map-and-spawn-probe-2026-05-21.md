# Finding: player input vocabulary (1.3) + Phase 2.1 spawn-orphan probe

**Date**: 2026-05-21
**Game version**: Alpha 0.9.0-n
**Phase**: 1.3 (input dispatch) + Phase 2.1 experiment prepared
**Source**: `CXXHeaderDump/mainPlayer.hpp` reflection; UE4SS v3.0.1 Lua API.

## Input dispatch path (1.3)

The player uses a stock `APlayerController`; all gameplay input is bound on
the pawn `AmainPlayer_C` as Blueprint input-action events (`InpActEvt_*`).
This is the **keysync vocabulary** to replicate for Phase 4.1 (input
replication, MTA-style): if we drive these on the orphan, the engine's own
systems produce movement/anim/interaction "for free".

### Movement / look
- `forward`, `back`, `left`, `right` (axis-style action events; multiple
  K2Node ids -> bound for both press/release and menu contexts)
- `jump`, `crouch`, `run` (sprint)
- `noclip`, `ragdoll`, `dismount` (vehicle/seat exit)

### Interaction / items
- `fire` (use / primary), `drop`, `rotate`, `lockObject`
- `inventory`, `hotbar_1` .. `hotbar_10`, `scrollUp`, `scrollDown`
- `flashlight`, `activateEquipment`, `alt` (modifier)

### System / meta (NOT replicated as player input — local or host-auth)
- `escape`, `spawnmenu`, `cheatmenu`, `quicksave`, `backupsave`,
  `debugtp`, `superscreenshot`, `AnyKey`, `LeftMouseButton`

Look (mouse) is not an action event here — it drives the controller's
control rotation / the pawn camera (`Camera` @0x0530). Pose + control
rotation will come via the pose/aim channel, not the action-event channel.

**4.1 design note**: replicate the *action-event state* (pressed/released
bits) + the analog look (control rotation / aim). Equipment-derived state
(`equipped_*` bools, `holding_actor`) is owned state, replicated separately
(4.2), not derivable from a single input frame.

## Phase 2.1 spawn-orphan probe (prepared, runnable tomorrow)

Wrote `tools/probes/coopSpawnProbe` (deployed + enabled in the game). It
spawns a 2nd `AmainPlayer_C` via `UWorld:SpawnActor(class, loc, rot)` --
OUR path, the same primitive the shipping `coop::RemotePlayer` will use.
NOT `UGameplayStatics::CreatePlayer` (that path carries `ULocalPlayer` +
split-screen viewport baggage; we are building networked coop, not
split-screen).

- `CTRL+P` spawn near the local player + auto-report alive/pos every 5s for
  the 60s gate window.
- `CTRL+O` report once. `CTRL+K` destroy.

The orphan is spawned **unpossessed** (no local player). The experiment
answers: (a) does `SpawnActor<AmainPlayer_C>` return a valid actor; (b)
does the game survive its `BeginPlay` (VOTV BP may assume it is THE player
-- `GetPlayerController(0)`, camera setup, registering with `AmainGamemode_C`
singletons); (c) does it survive 60s. A native crash in (b) is **expected
data**: it pinpoints the first per-site SP-assumption fix (principle 4).
CrashDump is enabled, so a crash yields `UE4SS.log` + a dump.

## Next (autonomous, no game needed)

- 1.6 level-load entry (`UGameplayStatics::OpenLevel` / streaming) +
  post-load callback VOTV uses.
- 1.7 `save_main_C` layout + the load `UFunction`.
- 1.8 UI/screen stack (`ui_UI_C`, `ui_menu_C`, `ui_console_C`).

## Next (needs the game — tomorrow)

- Run `CTRL+P`; record spawn result, BeginPlay survival, 60s stability,
  and any crash dump. This is the Phase 2 gate's first data point.
