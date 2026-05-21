# probes — throwaway UE4SS experiments

Short-lived UE4SS Lua mods used to derisk a question before committing to a
C++ implementation. Each probe is an **experiment**, not production: once it
has taught us what we need, it is superseded by code in `src/votv-coop/`
and deleted (RULE No.2 — no migration baggage / no parallel old+new paths).

Lua (not C++) on purpose: probes need fast iteration with no build step.
The shipping mod is C++ (`ue_wrap/` + `coop/`).

## Deploy

```powershell
./tools/deploy-probe.ps1 -Name <ProbeName>
```

Copies the probe into the game's UE4SS `Mods/` and enables it in
`mods.txt`. The game dir is gitignored, so the copy here under
`tools/probes/` is the source of truth.

## Probes

- **coopSpawnProbe** — Phase 2.1 "spawn the orphan" derisk. Spawns a 2nd
  `AmainPlayer_C` via `UWorld:SpawnActor` (our path; not split-screen /
  `CreatePlayer`) and watches it for the 60s gate window.
  Keybinds: `CTRL+P` spawn, `CTRL+O` report, `CTRL+K` destroy.
  Retire when: the C++ orphan in `src/votv-coop/` spawns + ticks, OR the
  experiment's findings are recorded in a research finding and the question
  is fully answered.
