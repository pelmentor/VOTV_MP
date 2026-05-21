# AUTONOMOUS_TESTING — test harness usage

**Living document.** Filled in as the harness is built (methodology
Phase 5.1). Skeleton at bootstrap.

## Intent

A scenario-driven test runner that boots VOTV with the coop mod, drives
the engine through scripted actions via synthetic input, runs for a fixed
number of frames, and exports a JSON report at exit. Lets us regression-
test every replication change without manual play.

See WP8 (methodology): an autonomous pass validates ONE process's code
paths — NOT coop-correctness, performance under load, or live-play
behavior. Autonomous pass = ship to live test, not to production.

## Available now

The framework launches the game, skips menus into gameplay, screenshots,
and reports — no manual clicking. Two pieces:

- **`tools/run-test.ps1`** — writes the scenario file, then launches the
  shipping exe windowed at a set resolution (UE4SS hooks it via its proxy).
  ```powershell
  ./tools/run-test.ps1 -Scenario newgame -ResX 1280 -ResY 720
  ```
  `-Scenario`: `newgame` (auto New Game into gameplay — most deterministic),
  `loadlast` (load most recent save), or `none` (launch only, drive by hand).

- **`tools/probes/coopTestHarness`** (UE4SS Lua, deployed via
  `deploy-probe.ps1 -Name coopTestHarness`) — reads the scenario on startup
  and:
  - skips to gameplay: invokes `ui_menu_C`'s NewGame event, falling back to
    `OpenLevel("untitled_1")`;
  - screenshots via console `HighResShot 1920x1080`;
  - reports current map/pawn state.
  In-game keybinds: `CTRL+8` skip-to-gameplay, `CTRL+9` screenshot,
  `CTRL+7` report.

This is **test tooling** and may depend on UE4SS freely; the shipping mod
does not (see `ARCHITECTURE.md` "Substrate").

> RUNTIME-VALIDATION (first run, 2026-05-22+): the skip-to-gameplay call
> chain is grounded in the reflection dump but unverified live. First launch
> confirms whether the NewGame-invoke or the OpenLevel fallback works;
> prune the loser (RULE No.1). Screenshots: confirm the output dir (the
> script prints the candidates).

## Planned

- `run-coop-test.ps1` — dual-process LAN test (host + client, aggregate
  reports) — Phase 5.2, after the session API exists.
- JSON report on exit (frame count, packet counts, crash flags) — Phase 5.1.
- Synthetic input for scripted actions (move/fire/transition) injected
  through UE's input pipeline so it survives an unfocused window
  (WP15/WP16).
