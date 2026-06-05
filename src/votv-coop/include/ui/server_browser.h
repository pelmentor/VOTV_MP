// ui/server_browser.h -- the MULTIPLAYER server browser (ImGui overlay surface).
//
// A third overlay surface alongside the F1 dev menu + the tilde scoreboard
// (ui/imgui_overlay.cpp drives all three). Opened from the native MULTIPLAYER
// button injected into VOTV's main menu (coop::multiplayer_menu) and rendered as
// a modal panel over the menu.
//
// The ROW MODEL is ported from MTA's CServerListItem (name / address / players /
// ping / version / world / locked) -- reference/mtasa-blue/Client/core/
// ServerBrowser/CServerList.h. The RENDERER is ImGui (not MTA's CEGUI): we already
// host an ImGui overlay in-process, so the table is a BeginTable, not a new GUI
// dependency. The live data feed (master-server fetch + LAN discovery + per-server
// ping) is P3; this surface owns the table + the Connect/Host/Direct-IP controls.
//
// Threading: Open/Close/Toggle/IsOpen are atomic (set from the game thread by the
// menu click poll; read by the render thread). Render() + the row list are
// render-thread only.

#pragma once

namespace ui::server_browser {

// Show / hide / flip the browser. Game-thread safe (atomic open flag).
void Open();
void Close();
void Toggle();
bool IsOpen();

// Draw the browser this frame. Render thread only (called from the ImGui present
// pass in ui/imgui_overlay.cpp when IsOpen()).
void Render();

}  // namespace ui::server_browser
