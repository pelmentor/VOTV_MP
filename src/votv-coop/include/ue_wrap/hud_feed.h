// ue_wrap/hud_feed.h -- the on-screen coop event/chat feed (engine-wrapper layer).
//
// A screen-space UMG overlay (a single multi-line UTextBlock added to the viewport)
// showing the last N coop lines: player joins, disconnects, errors (and, later, chat).
// Engine-wrapper layer (principle 7): it builds + drives the widget via reflection
// (engine::SpawnHudFeedWidget / SetWidgetText) and holds NO coop/network state -- the
// coop layer (coop::event_feed) decides WHAT lines to show and calls Push().
//
// Every call touches a UFunction or the widget object -> GAME THREAD ONLY.

#pragma once

#include <string>

namespace ue_wrap::hud_feed {

// Build the overlay and add it to the viewport. `outer` is a persistent object (the
// GameInstance) so the widget survives level loads. Idempotent (no-op if already up).
// Returns false if the widget could not be built.
bool Init(void* outer);

bool IsInitialized();

// Append a line to the feed. Lines auto-expire after kLineTtlMs (default 10 s)
// just like real chat -- a "X joined the game" line is interesting for a moment
// and then clutters the screen forever otherwise. Tick() does the expiry.
void Push(const std::wstring& line);

// Drop any expired lines and repaint if anything changed. Call from a periodic
// game-thread context (e.g. NetPumpTick at ~60 Hz). Cheap when nothing expired:
// a single steady_clock read + a deque-front timestamp compare.
void Tick();

// Drop the overlay (session end / shutdown).
void Shutdown();

}  // namespace ue_wrap::hud_feed
