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
