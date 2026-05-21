# ARCHITECTURE — VOTV coop mod

**Living document.** Edit as understanding evolves (do not append-and-keep-
stale — that's what `research/findings/` is for).

## What this is, architecturally

A hook-only mod that runs as an **engine-extension layer on top of UE4.27 +
VOTV**. It injects via UE4SS, discovers the game's classes/functions
through UE reflection, and adds a second networked player by driving the
engine's own `APawn` / `APlayerController` systems. It does not modify any
original game file (principle 1). It augments single-player; it does not
replace it (principle 6).

## Layer stack (top = closest to gameplay)

```
┌─────────────────────────────────────────────────────────────┐
│ coop/ (gameplay + network layer)                            │
│   RemotePlayer · session/peer model · packet (de)serialize  │
│   input buffer · interpolation · entity manifest · RPCs     │
│   mod menu / debug overlay (ImGui)                          │
├─────────────────────────────────────────────────────────────┤
│ clean API boundary (headers in include/votv-coop/)          │
├─────────────────────────────────────────────────────────────┤
│ ue_wrap/ (engine-wrapper layer)                             │
│   one wrapper per UE/VOTV class: APawn, APlayerController,   │
│   ACharacter, UWorld(SpawnActor), UCharacterMovementComp,   │
│   VOTV pawn/controller. Reflection access, offsets,         │
│   UFunction thunks. NO network/gameplay/coop state.         │
├─────────────────────────────────────────────────────────────┤
│ UE4SS (injection · UFunction hooks · reflection · ImGui)    │
├─────────────────────────────────────────────────────────────┤
│ UE4.27 + Voices of the Void (unmodified)                    │
└─────────────────────────────────────────────────────────────┘
```

The **`ue_wrap` ↔ `coop` split is principle 7.** A file that BOTH touches
engine memory/reflection AND owns network state is a violation — split it
(WP17).

## Not an ASI

This mod is **not** an ASI (the GTA/MTA-era native-DLL-via-ASI-loader
pattern from the methodology's origin). It is a runtime DLL loaded into the
UE4 process. Today the loader is UE4SS's `dwmapi.dll` proxy; that proxy is
UE4SS's, not ours. See "Substrate" below for our own loading path later.

## How far we can reach into the engine

A DLL in the game process can reach **everything** — there is no sandbox:

- **Reflected surface (the easy 95%)**: UE exposes every `UClass`,
  property, and `UFunction` (Blueprint *and* native) via `GUObjectArray` /
  `GNames`. We can read/write any property, call any `UFunction`, and hook
  any `UFunction` at the `ProcessEvent` level (intercept every Blueprint
  event/native call). VOTV is heavily Blueprint, so most "deep core game
  functions" ARE reflected and directly reachable.
- **Raw native surface (the other 5%)**: anything not reflected — inlined
  engine internals, native helpers, raw memory — is reachable the same way
  any native mod reaches it: AOB/signature scanning + MinHook/Detours/vtable
  hooks + direct memory patching. The IDA Pro IDB is the tool for finding
  those sites.

So "can a UE4SS mod reach deep core functions?" — yes, both layers. UE4SS
just makes the reflected layer convenient; the raw layer is always
available because we are native code in-process.

## Substrate: UE4SS is a swappable dependency, not a permanent one

**Constraint (user, 2026-05-22): the shipping mod must not depend on UE4SS
long-term.** Using it now (fast iteration, reflection, ImGui, Lua probes)
is fine; the architecture must let us remove it cleanly later (RULE No.2 —
when we drop it, it goes fully; no UE4SS-and-not-UE4SS dual paths).

UE4SS provides three separable things, each replaceable by us:

| UE4SS gives | Our replacement when we drop it |
|---|---|
| Injection (`dwmapi.dll` proxy) | Our own proxy/loader DLL (same technique) |
| Reflection access (`GUObjectArray`/`GNames` resolved) | Resolve the globals ourselves via AOB sigs (patternsleuth-style) |
| `UFunction` hook engine (`ProcessEvent` hook) | Hook `ProcessEvent` ourselves (this is how UE4SS does it) |
| Lua + ImGui + bundled mods | Only used by **test tooling/probes**, not the shipping mod |

**The discipline that makes this cheap**: all engine/substrate access lives
behind `ue_wrap/`. The `coop/` gameplay-network layer never calls UE4SS
directly. Dropping UE4SS then means reimplementing the substrate *behind
`ue_wrap`* — `coop/` does not change. The CXX header dump is already, in
effect, our standalone SDK (the class/offset/signature knowledge we'd need
without UE4SS).

Test tooling (`tools/probes/*`, the Lua harness) MAY depend on UE4SS freely
— it is not shipped. Only `src/votv-coop` carries the no-UE4SS constraint.

## Networking model (planned — see methodology Phase 3)

- **Transport**: custom UDP, pure I/O at the bottom. Host-authoritative,
  LAN-first.
- **Sessions, not connections**: a host listening on a port + zero/one
  client. Per-session sequence counter.
- **3-layer split**: transport (bytes) → serialization (struct↔bytes) →
  application (route packets to handlers). Principle 7 applied to network.
- **Wire format is semantic** (entity class IDs, vec3 positions — never
  UE memory addresses or vtable pointers; anti-pattern A7).
- **Replicate input + authoritative state; re-derive the rest locally.**
  The receiving UE engine replays P2's input on the orphan pawn so anim,
  weapon, interaction "just work" (MTA keysync pattern, methodology 4.1).

## Parallel class hierarchy (principle 3)

```
coop::RemotePlayer  ──m_pEnginePawn──▶  APawn* (engine-owned)
   owns: net state, interp buffer,        owns: render, anim,
   input buffer, ownership                physics, collision
```

Mirrors MTA's `CClientPed::m_pPlayerPed -> CPlayerPed*`.

## The 7 principles (summary — full text in COOP_METHODOLOGY.md)

1. No modification of original game files.
2. Engine-extension paradigm.
3. Parallel class hierarchy mirroring engine structures.
4. Targeted crash fixes, not broad suppression.
5. Minimum viable subset (`COOP_SCOPE.md` is law).
6. Augment SP, never replace it.
7. Engine-wrapper layer vs gameplay/network layer.

## Diagrams

_TBD — add sequence diagrams for session handshake, pose-sync, and the
input-replication path as those phases land._
