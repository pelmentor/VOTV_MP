// ui/scoreboard.h -- the tilde-key player-list overlay surface.
//
// A second UI surface on ui::imgui_overlay (the same generic ImGui host as the F1
// dev menu). Shown to EVERYONE on the tilde key (no dev gate) -- the server roster.
// Clients peek (hold tilde, passive, no cursor); the host gets an interactive board
// (toggle tilde, cursor) for the per-player action menu (KICK / BAN / TP TO ME).
//
// Principle 7: pure presentation. The roster facts come from coop::roster (a
// game-thread snapshot); this file only draws them.

#pragma once

namespace ui::scoreboard {

// Draw the player list. Called inside the overlay's ImGui frame (render thread)
// while the scoreboard is shown.
void Render();

// True if the local peer is the session host. The overlay reads this to decide
// the input model: host -> toggle + cursor (interactive); client -> hold + passive.
// Lock-free-ish (reads the roster snapshot under its mutex). Any thread.
bool LocalIsHost();

}  // namespace ui::scoreboard
