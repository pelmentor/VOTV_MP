# RE workflow — how to use UE4SS + tools/probes/ during development

CLAUDE.md RULE 3: the shipping mod is standalone (`xinput1_3.dll` + `votv-coop.dll`, no UE4SS at runtime). **However, UE4SS is explicitly approved as a development tool.** This document captures the workflow we use to leverage UE4SS during reverse-engineering, hypothesis-testing, and rapid iteration — without ever shipping it.

The standalone constraint is preserved by having **two game copies**:

| Path | Role | UE4SS? | Use for |
|------|------|--------|---------|
| `Game_0.9.0n/` | DEV copy | yes (xinput1_3.dll proxy IS UE4SS + loads votv-coop.dll alongside) | RE work, Live View, Lua probes, BP graph dumping, hands-on testing of the deployed DLL when convenient |
| `Game_0.9.0n_copy/` | PROD copy | no | LAN test (`lan-test.ps1` clients), hands-on testing of the truly-standalone shipping path |

The `_copy/` Win64 folder has only `xinput1_3.dll` + `votv-coop.dll` — same files we'd distribute to an end user. The DEV copy adds `UE4SS.dll`, `UE4SS-settings.ini`, the Mods/ tree with default UE4SS Lua mods + our own `coopTestHarness`, and `UE4SS.log`.

## What UE4SS gives us for free

### 1. Live View (the GUI UObject inspector) — biggest win

UE4SS ships a Dear-ImGui-based GUI that lets you browse every UObject in `GUObjectArray`, expand any object's FProperty tree, and **edit values live**. It already runs in our DEV copy — `GuiConsoleEnabled = 1` + `GuiConsoleVisible = 1` in `UE4SS-settings.ini`. Press the dump-keybind to toggle the window.

**Use it for:**

- Picking a specific UObject by name (e.g., `mainPlayer_C` instance) and watching every field tick-by-tick as the game runs. Vital for diagnosing animation BlendSpace inputs (`spd`, `Velocity`, `Controller`, etc.) — would have caught the Controller=null issue from earlier today in seconds vs the multi-hour RE.
- Editing FProperty values directly to test hypotheses ("what if I set `AnimBP_kerfur_Controller` = my local PC?").
- Inspecting field offsets we suspect may have shifted between game versions.

### 2. GUIUFunctionCaller (2026-05-25: enabled)

Experimental UE4SS feature: lets you CALL any UFunction from the Live View GUI by clicking the function in the object's UFunction list and filling param values into form fields. Setting in `[ExperimentalFeatures] GUIUFunctionCaller = 1`.

**Use it for:**

- Test-firing UFunctions before we wire them in our DLL. E.g., before writing the storage-extract observer, click `propInventory_C` → call `takeObj(0, true)` from the GUI to see what actually happens.
- Validating ParamFrame marshaling: if the GUI call succeeds with `inString` but our reflection call fails with `InString`, we've found a param-name mismatch.

### 3. ObjectDumper (`F8` shortcut → dump all UObjects to file)

Dumps every UObject in `GUObjectArray` (~250k entries) to a text file at `UE4SS_ObjectDump_*.txt`. We have two snapshots already in the dev copy (`UE4SS_ObjectDump_MAIN_MENU.txt` and `UE4SS_ObjectDump_GAMEPLAY_SAVE.txt`).

**Use it for:**

- Discovering what UObjects exist in a given game state (main menu vs gameplay vs save-load transition).
- Finding the FNAME of a class we suspect exists but can't locate (e.g., "is there an `Aprop_storage_locker_C`?" — grep the dump).

### 4. CXX Header Generator (`F7` shortcut → dump all class layouts to cooked C++ headers)

The output is at `Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/*.hpp`. We already use this for offset derivation throughout `sdk_profile.h`. Re-dump after a VOTV update to re-derive offsets.

### 5. Lua probe scripting — `tools/probes/`

Our existing probes (`tools/probes/*.lua`) run inside the UE4SS Lua VM in the dev copy. They give us autonomous RE that doesn't require building C++ code.

**Use it for:**

- Listing every UFunction matching a pattern (e.g., all `On*` lifecycle hooks).
- Per-tick property dumps (e.g., AnimBP cached fields, prop holder state).
- Sanity-checking offsets the SDK dump claims.
- Cross-version validation: run the probe on a new VOTV build to spot offset shifts before they break our DLL.

### 6. BPModLoader (UE4SS bundled mod) — bytecode inspection

`BPModLoaderMod` exposes APIs to read compiled Blueprint bytecode. Lets us read the actual BP graph of a UFunction (vs inferring from the header signature).

**Use it for:**

- Understanding what a BP UFunction actually does at the bytecode level. Critical for the upcoming Phase 5N1 NPC sync (14 NPC BP classes to RE) and the events system (~80 events).

## Open-source-borrow strategy (RULE 3-clean)

We **port** reflection patterns + AOB signatures from UE4SS's open-source repo (`reference/RE-UE4SS/`) into our standalone `src/votv-coop/src/ue_wrap/reflection.cpp` — with attribution. Not depend on at runtime.

**Examples of patterns worth porting:**

- AOB signatures for `GUObjectArray`, `GNames`, `ProcessEvent`, `StaticFindObject` across many UE4 versions (UE4SS has battle-tested these).
- FProperty traversal algorithms (UE4SS handles every FProperty subtype; ours currently handles the common ones).
- FName decoder logic (UE4SS handles all the FName encoding variants including UTF-16 names).

The porting workflow:
1. Read the relevant UE4SS source (`reference/RE-UE4SS/UE4SS/src/SigScanner/*.cpp` or `reference/RE-UE4SS/UE4SS/src/UnrealVersionedContainer/UnrealVersion.cpp`).
2. Adapt the algorithm into our standalone `ue_wrap/` style (no UE4SS types, no UE4SS headers).
3. Test with a Lua probe in the dev copy to confirm parity.
4. Comment in our source with `// Algorithm adapted from RE-UE4SS (MIT-licensed); see reference/RE-UE4SS/...`.

## What we don't borrow

- UE4SS's runtime mod loader — we have our own (proxy DLL → `votv-coop.dll`).
- UE4SS's BPModLoader pak-mounting — Phase 7+ revisit only (see `docs/MULTIPLAYER_UI.md` + the 3 architecture findings docs from 2026-05-25).
- UE4SS's Lua VM — we don't need scripting in production; if we ever need it for chat commands etc., a tiny embedded interpreter (e.g., MyJS or a custom DSL) keeps the standalone DLL self-contained.
- UE4SS's UI framework (Dear ImGui via UE4SS) — useful for the future MP menu debug overlay (CLAUDE.md "Mod menu / debug overlay: Dear ImGui (UE4SS ships an ImGui integration)") but that integration is via OUR linked ImGui, not via UE4SS at runtime.

## The "two copies" hygiene rule

**Always deploy to BOTH copies** (`Game_0.9.0n/` and `Game_0.9.0n_copy/`) when iterating on the shipping DLL. The `tools/deploy-loader.ps1` script does this. The DEV copy lets you launch with UE4SS for inspection; the PROD copy lets you sanity-check that the DLL still runs WITHOUT UE4SS (the standalone invariant).

**Never** assume what works in the DEV copy will also work in the PROD copy. UE4SS's presence changes the load order (UE4SS hooks `ProcessEvent` before we do; the global `GUObjectArray` cache is populated by UE4SS at startup; some classes are loaded earlier because UE4SS forces them). The PROD copy is the source of truth for "does our shipping DLL work?".

The `lan-test.ps1` autonomous test runs against `Game_0.9.0n_copy/` ONLY for both host and client — by design, so we always validate the standalone path.

## Future RE sessions: open Live View first

The session-resume rule (CLAUDE.md "Reading order after a session reset"): we read MEMORY.md + the latest project checkpoint + CLAUDE.md before starting work.

Add a step for RE-heavy sessions: **launch the DEV copy with UE4SS, open Live View, find the relevant UObject, before writing the first line of code.** The 5-10 minutes spent setting up the inspector pays for itself in the first hypothesis check.

Examples:
- Puppet anim RE: open Live View → expand `mainPlayer_C` → expand its `AnimInstance` → watch `Pawn`, `Controller`, `Movement`, `Character`, `spd`, `Velocity` live as you walk. The Controller=null issue from 2026-05-25 would have been visible in one frame.
- Storage extract RE: open Live View → expand `propInventory_C` → call `takeObj` from the GUIUFunctionCaller → inspect the returned actor's Key field directly.
- NPC sync RE: Live View → filter by `*kerfur*` → see the spawner-spawn relationships, the active-tick states, the per-instance variant divergences.
