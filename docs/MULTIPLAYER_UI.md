# MULTIPLAYER_UI — coop menu design

**Living document.** Captures the user's vision for the in-game multiplayer
UI. The networking behind it is gated (methodology Phase 3+); this doc is
the design + the approach decision so we build the right thing when we get
there. Status: **design only, not yet built.**

## User vision (2026-05-22)

A multiplayer option surfaced **in VOTV's own main menu** (native, not a
debug overlay), with:

1. **Play as Host** → choose a save (new or existing) → load → start
   hosting (listen for one client; LAN-first).
2. **Connect** → type an IP → join the host's session.
3. **Server browser** (future) → list discoverable LAN/known hosts → join.

## Flows

```
Main menu
├── [Singleplayer]            (VOTV's existing buttons, untouched)
└── [Multiplayer]  (ours)
    ├── Host
    │   ├── pick save slot (existing) OR New Game
    │   ├── -> load world (host-authoritative; reuses VOTV's load path)
    │   └── -> open UDP listen socket, wait for client
    ├── Connect
    │   ├── enter host IP (+ port; default fixed)
    │   └── -> handshake -> client loads host's world state -> in game
    └── Server browser  (future: LAN broadcast discovery; later a registry)
```

Host = authoritative (owns save/world/progression). Client sends input +
receives state. Never the reverse. (Methodology Phase 3.1.)

## Approach decision — native UMG, built at runtime by our C++ mod

The menu must (a) look native/integrated, (b) not edit VOTV's menu asset
(principle 1 / anti-pattern A6), and (c) not bind us to UE4SS long-term
(see `ARCHITECTURE.md` "Substrate"). Options weighed:

| Approach | Native look | Edits assets? | UE4SS dependency | Verdict |
|---|---|---|---|---|
| **Runtime UMG widget via reflection** (our C++ mod constructs a `UUserWidget`, adds a Multiplayer button + panels, adds to the menu/viewport) | yes | no | none (works with or without UE4SS) | **chosen** |
| BP mod via UE4SS BPModLoader (cooked widget .pak) | yes | adds a new asset (allowed) but needs UE4.27 editor + cook | yes (BPModLoader) | rejected — ties us to UE4SS |
| ImGui overlay | no (debug look) | no | UE4SS's ImGui (or our own) | rejected for the menu — fine for dev/debug overlays only |

So: our C++ mod hooks the menu's construction (`ui_menu_C` BeginPlay /
construct), creates our own widget tree at runtime via reflection, and
wires its buttons to the `coop/` session API. No VOTV asset is modified; we
add our widget alongside. Styling is hand-built in code (more effort than a
designer asset, but substrate-agnostic and asset-clean).

Dev/debug overlays (connection stats, entity inspector) stay ImGui — that's
tooling, not the player-facing menu.

## Anchors (from reflection dump, game 0.9.0-n)

- Menu widget: `ui_menu_C` (buttons: NewGame, Resume, Save, Settings, Exit,
  …). We attach a Multiplayer entry near these without touching the asset.
- Save selection UI: `ui_saveSlots_C` / `uicomp_saveSlot*` (reuse for the
  Host "choose save" step).
- Load path: `UmainGameInstance_C::setSaveSlotObject` + open `untitled_1`;
  GameMode applies world state (`loadObjects`/`loadTriggers`).

## Gating

Build order: the menu's **networking** depends on the Phase 3 transport +
session model, which is gated behind the Phase 2 orphan gate. The menu
**shell** (buttons/panels/IP entry) could be prototyped earlier, but to
avoid a non-functional UI lingering (RULE No.1), we build it when the
session API it drives exists. Tracked in `ROADMAP.md`.
