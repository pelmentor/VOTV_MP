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

## Planned tools (under `tools/`)

- `run-test.ps1` — single-process autonomous test (boot, drive, report).
- `run-coop-test.ps1` — dual-process LAN test (host + client, aggregate
  both JSON reports).
- Launch scripts living in the game install (`Game/coop-host.bat`,
  `Game/coop-client.bat`) — force-added past `.gitignore`.

## Scenarios

_(TBD — one row per scenario: name, cmdline flags, scripted actions,
frame count, pass criteria.)_

## Synthetic input

VOTV is UE4. Synthetic input is injected through UE4SS / UE's input
pipeline (not OS-level), so it survives an unfocused window (WP15/WP16).
_(Document the exact mechanism once built.)_
