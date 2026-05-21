# votv-coop — the mod source

Hook-only coop mod for Voices of the Void (UE4.27), loaded via UE4SS.

## Subtrees (principle 7 — see `docs/COOP_METHODOLOGY.md` / `CLAUDE.md`)

```
include/votv-coop/
  ue_wrap/    Engine-wrapper layer headers. One class per UE/VOTV class.
              Reflection access, struct offsets, UFunction thunks.
              NO network logic, NO gameplay logic, NO coop state.
  coop/       Gameplay/network layer headers. RemotePlayer, sessions,
              packets, interpolation, input buffers, mod menu.
src/
  ue_wrap/    Engine-wrapper implementations.
  coop/       Gameplay/network implementations.
```

A file that BOTH dereferences engine memory/reflection AND owns network
state violates principle 7 — split it (WP17): engine-touching code to
`ue_wrap/`, network state to `coop/`, communicating via a clean header API.

## Build

Builds as a UE4SS C++ mod (a DLL loaded by UE4SS into the running game).
`CMakeLists.txt` is a placeholder until the UE4SS C++ mod SDK is wired up
in Phase 2 (the SDK comes from `reference/RE-UE4SS/`). No code is committed
in the bootstrap commit — first hook lands in Phase 2.
