# Finding: xinput1_3.dll proxy auto-loads the standalone mod (no UE4SS, no injection)

**Date**: 2026-05-22
**Game version**: Alpha 0.9.0-n
**Phase**: standalone foundation (RULE No.3)
**Status**: DONE. Shipping auto-loader proven end-to-end; replaces the dev
injector (`tools/inject.ps1`).

## Result

The mod now auto-loads on game start with **no UE4SS and no injection step**.
Dropping two files next to `VotV-Win64-Shipping.exe`:

- `xinput1_3.dll` -- the proxy loader, and
- `votv-coop.dll` -- the mod payload

is enough. Live run (PID 31556, UE4SS disabled):

```
XINPUT1_3.dll loaded from the GAME dir (our proxy, NOT System32)
votv-coop.dll loaded   <- proxy's worker thread LoadLibrary'd it
marker: "votv-coop standalone bootstrap loaded into PID 31556 (no UE4SS)"
UE4SS.dll ABSENT; only dwmapi.dll present is C:\Windows\System32\dwmapi.dll
process ALIVE, responding=True; OMEGA WARNING splash rendered normally
```

## Why xinput1_3.dll

Verified in the shipping exe import table (IDA `imports_query module=*xinput*`):
VOTV imports **exactly two** symbols from `XINPUT1_3`, by name:
`XInputGetState` (ord 2) and `XInputSetState` (ord 3). Nothing else.

- VOTV does **not** ship its own `xinput1_3.dll` (none in `Win64/`), so it
  normally loads the System32 one. The exe directory wins the DLL search
  order, so our `xinput1_3.dll` placed in `Win64/` is loaded instead.
- It is **distinct from UE4SS's `dwmapi.dll`** proxy, so the two coexist in
  dev (different proxy DLLs, no contention).
- System32 has `XInput1_4.dll` (guaranteed on Win8+), which exports the same
  two functions with identical signatures -> ideal static forwarder target.

## How it works

1. **Static export forwarders** (in `src/loader/xinput_proxy.cpp` via
   `#pragma comment(linker, "/export:XInputGetState=xinput1_4.XInputGetState,@2")`
   and the `SetState` equivalent). `dumpbin /exports` confirms:
   `XInputGetState (forwarded to xinput1_4.XInputGetState)` etc. The loader
   resolves these at bind time, independent of our code. We forward ONLY the
   two VOTV imports (RULE No.2 -- no speculative baggage); ordinals mirror the
   real xinput1_3 so an ordinal importer would also resolve. The forward
   target is by NAME on xinput1_4's side, so xinput1_4's ordinals don't matter,
   and there's no name collision with our own `xinput1_3`.
2. **Payload load off the loader lock**: `DllMain(PROCESS_ATTACH)` spins a
   worker thread that resolves the proxy's own directory and
   `LoadLibraryW("votv-coop.dll")`. Never calls LoadLibrary inside DllMain.
   This is exactly what `inject.ps1` did via CreateRemoteThread, made automatic.

The `.def`-file forwarder form failed to link (LNK2001 unresolved
`XInputGetState`/`XInputSetState`) with this MSVC toolset; the `/export:`
linker directive is the reliable mechanism and is what's used.

## Build / deploy

- Build: `cmake --build build/votv-coop --config Release` -> builds both
  `votv-coop.dll` (payload) and `xinput1_3.dll` (proxy). See
  `src/votv-coop/CMakeLists.txt` (target `xinput1_3`, `PREFIX ""` so the file
  is named exactly `xinput1_3.dll`).
- Deploy: `tools/deploy-loader.ps1` (copies both DLLs to `Win64/`;
  `-Standalone` also disables UE4SS for a clean proof; `-Remove` uninstalls and
  restores UE4SS). Replaces `tools/inject.ps1` for the standalone path.

## Next

1. `ue_wrap` reflection: AOB-resolve `GUObjectArray` / `GNames` /
   `ProcessEvent` (IDA) -> the mod's own SDK access, no UE4SS. CXX dump = types.
2. Port the validated orphan spawn (Phase 2.1) into C++ behind
   `coop::RemotePlayer`, driven through `ue_wrap`.
