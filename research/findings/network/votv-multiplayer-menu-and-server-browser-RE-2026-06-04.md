# RE + design: MULTIPLAYER menu entry (in ui_menu_C, above NEW GAME) + MTA server browser

**Date**: 2026-06-04
**Target**: Alpha 0.9.0-n
**Method**: 2 Explore recon agents (UMG injection mechanics + MTA CServerBrowser) over
`CXXHeaderDump/*.hpp`, `src/votv-coop/src/ue_wrap/engine_widget.cpp`, `reference/mtasa-blue/
Client/core/ServerBrowser/*`, + the existing design docs (MULTIPLAYER_UI.md,
votv-master-server-mta-adaptation, votv-gns-p2p-masterserver-plan).
**User ask (2026-06-04)**: "make votv main menu new option MULTIPLAYER on top of NEW GAME. Then
we take the server browser from mta and wire it."

Builds on MULTIPLAYER_UI.md (chosen approach = runtime UMG via reflection, no asset edit) +
COOP_SCOPE.md (multiplayer menu + server browser are in scope).

---

## HALF 1 — Inject "MULTIPLAYER" into ui_menu_C, above NEW GAME

### The menu (ui_menu.hpp)
`Uui_menu_C : UUserWidget`. Buttons live in **`canvMenu`** (`UCanvasPanel` @0x0330 — absolute
positioning, NOT a vertical list). **`button_start`** @0x02E0 is NEW GAME (bound event
`BndEvt__button_NewGame...`); it opens the gamemode submenu `umg_gamemode` (Uui_gamemode_C) →
`openStorymode/openInfinite/openSandbox`. Save picker = `umg_saveSlots` (Uui_saveSlots_C). The
widget also has `Tick(FGeometry, float)` (fires every frame while the menu is up — our click poll
anchor) and `Construct()` (one-shot on menu open — our inject anchor).

### What we already have (engine_widget.cpp)
NewObject via `GameplayStatics::SpawnObject`; create UUserWidget/UWidgetTree/UTextBlock/
UVerticalBox; `AddChildToVerticalBox`; `SetText` (Conv_StringToText + KismetTextLibrary); text
outline + drop-shadow; `AddToViewport`/`SetPositionInViewport`. We do NOT yet have: UButton
creation, UCanvasPanel child-add, CanvasPanelSlot geometry read/set, UMG click handling.

### Inject recipe (cited UMG.hpp)
1. `button = SpawnObject(UButton)` ; `text = SpawnObject(UTextBlock)` ("MULTIPLAYER", styled like
   `tex_btnStart`).
2. `UContentWidget::SetContent(button, text)` (UMG.hpp:469 — UButton is a UContentWidget; nests the
   text as the button's child).
3. `slot = UCanvasPanel::AddChildToCanvas(canvMenu, button)` (UMG.hpp:328 → returns a
   `UCanvasPanelSlot`).
4. Position ABOVE button_start: resolve `button_start` (read the field off the live menu instance),
   read its slot geometry — `UCanvasPanelSlot::GetPosition/GetSize/GetLayout` (UMG.hpp:348-351) —
   then on OUR slot `SetPosition({startX, startY - startH - pad})` + `SetSize(startSize)` +
   `SetAnchors`/`SetAlignment` to match (UMG.hpp:338-346). To find button_start's slot: read
   `canvMenu`'s `Slots` (UPanelWidget::Slots TArray) and match the one whose Content==button_start
   (or just mirror button_start's slot fields).
5. VISIBILITY: a freshly-spawned UButton inherits the game's default `WidgetStyle` (FButtonStyle
   @+0x128), so `SetContent(text)` alone should render a native-looking button; `SetBackgroundColor`
   (UMG.hpp:310) optional; if it renders invisible, wrap in a UBorder with a brush. (Verify on the
   first hands-on — the button is only on screen at the menu, which the gameplay smoke skips.)

### CLICK detection — POLL (recommended; delegate-bind is impractical for pure reflection)
Register a **POST observer on `ui_menu_C::Tick`** (it already fires every menu frame; observer API
= `ue_wrap::game_thread::RegisterPostObserver`). In the callback, poll `UButton::IsHovered()`
(UMG.hpp) + a left-click edge via `GetAsyncKeyState(VK_LBUTTON)` (freecam.cpp already uses
GetAsyncKeyState for MMB — proven). On hover+click rising-edge → fire our handler (GT::Post). This
avoids binding the `OnClicked` FMulticastScriptDelegate (which needs a UObject+UFunction we don't
have in a reflection-only DLL). Same poll pattern handles the sub-panel (Host/Connect/Browse)
buttons.

### Flow (MULTIPLAYER_UI.md)
MULTIPLAYER click → our panel (own UserWidget overlay OR injected sub-canvas): **Host** (pick save
or New Game → start listening — reuse engine::LoadStorySave/StartFreshGame + Session::Start as
Host), **Connect** (enter IP → Session::Start as Client → the fresh-boot + world-mirror we just
built), **Browse** (the server browser, Half 2). Host = authoritative.

---

## HALF 2 — Port MTA's server browser (data model + query; not CEGUI/Lua)

### Data model (MTA CServerList.h:95-310 CServerListItem)
Per server: name, host IP:port, players_current, players_max, ping, gamemode, map, version,
passworded. Plus freshness state (revision counter + last-seen). Container = address:port-keyed
list with sort. KEEP these fields; SKIP MTA's ASE-specific extras (serials, build type, special
flags, master restriction flags).

### Sources (tabs)
- **Internet** = master-server HTTP fetch. MTA: `CServerListInternet::Refresh/Pulse` →
  `CMasterServerManager` (queries N masters in parallel for redundancy, falls back to on-disk cache)
  → `CRemoteMasterServer` HTTP GET, versioned binary parse. **Ours**: a JSON `GET /v0/servers`
  (votv-master-server-mta-adaptation) — same two-phase shape (master fetch → optional per-server
  query), JSON instead of ASE-binary.
- **LAN** = UDP broadcast (`CServerListLAN`: broadcast a query, parse responders every 2 s). **Ours**:
  we already have GNS LAN discovery (PR-4) — reuse it for the LAN tab.
- **Favourites / Recent** = XML cache (defer to v0.1).

### Per-server query / ping (CServerList.cpp:490-661)
Per-item state machine: WaitingToSendQuery → Query (UDP to game_port+123, ASE) → ParseQuery
(ping + live player count) → timeout/retry/offline. Rate-limited (2-50 queries/s, visible rows
prioritized). **Ours**: v0 can SKIP per-server query (the master's JSON already carries
players_current + a ping_target); v0.1 adds a GNS connectionless ping for live counts. Keep the
rate-limited pulse-loop shape.

### Our v0 (matches votv-master-server-mta-adaptation + votv-gns-p2p-masterserver-plan PR-5)
- **Master server** (external HTTPS, separate hosting, user-controlled): stateless JSON aggregator.
  `GET /v0/servers` → `[{name,host_ip,port,version,players_current,players_max,world,mode,
  ping_target,last_seen_unix}]`; `POST /v0/announce` (host heartbeat every 30-60 s, validates source
  IP == host_ip); `POST /v0/leave` (drop on Stop). Expires entries > 5 min.
- **Client** (`src/votv-coop/src/coop/net/lobby_client.{cpp,h}`): HTTPS GET (libcurl — check if
  vendored; else add) + JSON parse (picojson/nlohmann single-header) + background fetch + on-disk
  cache (`lobby_cache.json`, load first so the browser opens populated).
- **Host announcer** (`lobby_announcer.{cpp,h}`): periodic POST loop, started by Session::Start when
  role==Host, opt-in via a "list my game publicly" checkbox (privacy: default OFF, per COOP_SCOPE).
- **Browser UI**: the server-list TABLE is best as ImGui (our UI direction + the master-server doc) —
  sortable rows (name/ping/players/version/world + Connect). Reached FROM the native MULTIPLAYER
  panel's "Browse" button. (The native UMG is the menu ENTRY; the list itself is ImGui — simpler than
  a UMG sortable table, and the browser is a modal over the menu.)

---

## Build phasing (each verifiable; menu visuals are hands-on — the gameplay smoke skips the menu)

- **P1 — MULTIPLAYER button (the immediate ask):** `ue_wrap/menu_inject.{cpp,h}` (or coop/ui/
  multiplayer_menu): on `ui_menu_C` construct/first-sight, inject the styled UButton above
  button_start; a `ui_menu_C::Tick` POST observer polls the click. Handler stub logs "MULTIPLAYER
  clicked". Adds the UButton/UCanvasPanel/CanvasPanelSlot primitives to engine_widget. **Verify:**
  compiles + installs; a menu-screenshot scenario (boot to menu, don't load a save, capture) OR the
  user's hands-on sees the button.
- **P2 — Host/Connect panel:** the sub-panel + wire to Session::Start(Host) / Session::Start(Client,
  IP) (+ the save picker reuse + the fresh-boot client we just built).
- **P3 — Browser (Half 2):** lobby_client + lobby_announcer + the ImGui table + the master server
  (deploy doc). LAN tab reuses GNS discovery; Internet tab hits the master.

P1 is the concrete "MULTIPLAYER on top of NEW GAME" deliverable; P2/P3 wire the flows + the browser.

## Load-bearing unknowns / risks
- Injected-button VISIBILITY (default WidgetStyle vs needing a brush) — confirm on first hands-on.
- The menu is NOT in the gameplay smoke → P1 needs a boot-to-menu screenshot scenario for autonomous
  visual proof, or hands-on.
- `SpawnObject(UButton)` outer + lifetime (use canvMenu / the menu widget as outer so it's GC-rooted
  with the menu; re-inject on each menu construct since the menu widget is recreated per menu open).
- libcurl/TLS for the master GET (check vendored deps) — or use a minimal HTTPS via the platform.

## Zero open ports (user constraint, 2026-06-05) -- master = list + signaling + TURN relay

User: "I don't want people to have to open any ports at all." This is a hard
requirement, and it splits cleanly:

- **Server-browser query = already no-ports.** Host sends an OUTBOUND heartbeat
  `POST /v0/announce` to the master (~30 s); the browser does an OUTBOUND
  `GET /v0/servers`. Player counts/ping come from the stored heartbeat, NOT a
  client->host probe (that's MTA's old ASE model, which needed an open query port
  -- we deliberately don't port it). No inbound port on either side.
- **Game connection = needs the P2P/relay path (planned, not yet built).** Today's
  `coop::net::Session` uses GNS direct IP (`CreateListenSocketIP` /
  `ConnectByIPAddress`) -> that DOES need port-forwarding / same-LAN. The no-ports
  path is GNS-native:
  - **ICE/STUN hole-punch** crosses most home NATs (full/restricted-cone) with zero
    forwarding; the master is the SIGNALING channel (candidate exchange).
  - **TURN relay fallback** for symmetric NAT / CGNAT / mobile (hole-punch can't
    cross) -- traffic relays through a server, guaranteeing connectivity at the cost
    of relayed bandwidth. We CANNOT use Steam Datagram Relay (needs a Steam app id;
    we're a mod), so we run our OWN STUN+TURN on the user's VPS.
  - GNS's ICE is already wired in CMakeLists (`ENABLE_ICE`) but currently OFF --
    the build comment defers it to "the PR-6 commit that adds the first ICE caller
    (P2P signaling via the master server)". So no-open-ports is PLANNED
    (votv-gns-p2p-masterserver-plan-2026-05-28.md), and the direct-IP path is a
    today-only stopgap.

**Design consequence:** the VPS master is NOT just a server list -- it is
**list + P2P signaling + TURN relay**. Pure hole-punch covers ~80% of players for
free; the VPS TURN relay covers the rest, so NOBODY opens a port. The browser's
"Direct connect: IP" field stays as a power-user / LAN escape hatch.

## Build status (2026-06-05) -- P1 button + P3-minimal browser SHIPPED (uncommitted)

Built + deployed + autonomously captured + audited this session. BOTH screenshots
delivered (button at the menu top above NEW GAME: research/menu_shots/v4_1.png; the
ImGui browser: research/menu_shots/02_server_browser.png).
- `ue_wrap/engine_widget.cpp`: `InjectCanvasButton(refButton, label, refText, outBtn)`
  -- spawns a UButton, clones NEW GAME's FButtonStyle (the two FSlateSound TSharedPtr
  fields ZEROED to avoid aliasing a refcount) + tex_btnStart's FSlateFontInfo, then
  INSERTS it at the TOP of refButton's VerticalBox. **KEY RE CORRECTION:** button_start
  is NOT a direct child of canvMenu -- it's in a **UVerticalBox** (canvMenu is the
  outer CanvasPanel). The first attempt (AddChildToCanvas + clone the canvas slot)
  put the button off-screen (the slot-clone misread VerticalBoxSlot bytes as
  FAnchorData). The working approach inserts INTO the VBox at index 0 via the
  canonical reorder (UMG has no insert-at-index): snapshot children -> ClearChildren
  (detaches; objects survive, referenced by ui_menu_C fields) -> re-add OUR button
  first + the originals after. Non-destructive (13 items re-added in order; snapshot
  caps at 128 children -> deliberate bottom-append fallback if larger, never a wipe).
- `coop/multiplayer_menu.cpp`: ui_menu_C::Tick POST observer (mirrors
  save_button_disable) -> `DoInject` on the MAIN menu (isPause==0, throttled self-heal)
  -> poll IsHovered + VK_LBUTTON press-edge -> open the browser. Bounded retry-resolve.
  `ForceInjectNow()` = a TEST hook for deterministic inject in the screenshot window.
- `ui/server_browser.cpp`: the ImGui browser surface (MTA column model:
  Name/Players/Ping/World/Version + lock, Refresh/Host/Direct-IP/Connect). Wired into
  imgui_overlay as a 3rd interactive surface (`VOTVCOOP_BROWSER_OPEN=1` for capture).
  Connect/Host/Refresh are logged stubs (P2/P3 wire Session::Start + the master).

Audits (3, all passed): perf 0 CRIT/0 WARN (steady-state ~zero, exactly zero during
gameplay); correctness 2 fixes (g_prevLmb first-tick prime; FSlateSound zeroing);
SP-safety on the menu reorder 0 CRIT -- fixes applied: removed dead canvas-approach
code + the resolve-guard that still required it (latent abort), 64->128 deliberate
snapshot cap. Regression-verified after cleanup (menu intact, button at top).

**OMEGA-gate capture lesson:** VOTV's main menu cursor is RAW-INPUT driven (the
engine recenters the OS cursor every frame at the menu -- that's why imgui_overlay
hooks SetCursorPos), so synthetic OS clicks (mouse_event/SetCursorPos/PostMessage)
do NOT activate a menu button. The content-warning ("OMEGA WARNING" = ui_menu_C's
`canvas_begin` screen, gated by `beginn`@0x4B0) blocks the main menu in an
autonomous run. To reach the main menu for the button screenshot, added a TEST-ONLY
dev helper `coop/dev/menu_proceed.cpp` (env VOTVCOOP_MENU_PROCEED=1, NEVER ships on)
that fires the begin-screen Proceed button's OnClicked handler via reflection (walk
canvas_begin -> UButtons -> read OnClicked[0].FunctionName -> Call the non-exit one).

## Cross-refs
- docs/MULTIPLAYER_UI.md ; research/findings/mta/votv-master-server-mta-adaptation-2026-05-28.md ;
  research/findings/network/votv-gns-p2p-masterserver-plan-2026-05-28.md ; reference/mtasa-blue/Client/core/
  ServerBrowser/ ; CXXHeaderDump/{ui_menu,UMG,Button,CanvasPanel,CanvasPanelSlot}.hpp ;
  src/votv-coop/src/ue_wrap/engine_widget.cpp.
