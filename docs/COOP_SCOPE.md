# COOP_SCOPE — what coop replicates (and what it does not)

**Living document.** Append to sections as scope is decided; preserve the
audit trail (date + reason) when amending. **Anything not listed here is
NOT in scope and should not drive code or replication decisions.**

This is the discipline that lets coop ship (principle 5). Every "should we
replicate X?" gets a binary answer from this doc. Use the decision tree in
`docs/COOP_METHODOLOGY.md` ("should we replicate X?") to classify new
items.

> **Status**: skeleton. Scope is decided as Phase 1 reflection reveals
> what VOTV's gameplay actually consists of. Do not pre-populate from
> guesswork — fill in as each system is understood.

---

## Player count

- 2 players (host + one client), LAN-first. To be confirmed against VOTV's
  single-pawn world assumptions in Phase 2.

## In scope

- **Multiplayer menu (in VOTV's main menu)** — Host (choose save / New
  Game, load, listen) + Connect (enter IP, join) + server browser (future).
  Native UMG widget built at runtime by our C++ mod; no VOTV asset edited.
  Decided 2026-05-22 (user). Design: `docs/MULTIPLAYER_UI.md`. Build gated
  on the Phase 3 session API.

<!--
Template for an entry:
- **<system>** — replicated <how>. Owner: <host / per-machine>.
  Decided <YYYY-MM-DD>. Reason: <why>. Methodology phase: <4.x>.
-->

## Out of scope

_(empty — populate as decided, with date + reason)_

<!--
- **<system>** — NOT replicated. Reason: <re-derivable locally / breaks no
  one's experience / too costly>. Decided <YYYY-MM-DD>.
-->

## Undecided / parked

Candidate VOTV systems to classify during Phase 1 (this is a checklist of
*questions*, not a scope commitment):

- Player pose / movement (almost certainly in scope — Phase 3/4.1).
- Player input (almost certainly in scope — Phase 4.1).
- Held tool / equipment state (Phase 4.2).
- Base / facility state, machines, the satellite dish array & signals.
- Inventory, resources, progression / unlocks (host-side, Phase 4.6).
- NPCs / creatures / AI entities (Phase 4.3).
- Day/night cycle & world time (likely host-authoritative single value).
- Weather / environment events.
- Audio signals / the core signal-processing gameplay loop.
- Save / world state (host-authoritative — Phase 4.5).
- Cutscenes / scripted story events (Phase 4.4).
- UI / HUD (per-machine; not replicated, but reads shared host state).

## Amendment log

- 2026-05-21 — Created skeleton at project bootstrap.
- 2026-05-22 — Added multiplayer menu (host/connect/server-browser) to In
  scope per user; design in `docs/MULTIPLAYER_UI.md`.
