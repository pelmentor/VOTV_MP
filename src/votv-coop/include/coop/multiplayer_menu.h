// coop/multiplayer_menu.h -- the MULTIPLAYER entry point in VOTV's main menu.
//
// Injects a native "MULTIPLAYER" UButton into VOTV's ui_menu_C, positioned just
// ABOVE button_start (NEW GAME), and opens the ImGui server browser
// (ui::server_browser) when it is clicked. Coop/gameplay layer (principle 7): it
// owns the FEATURE (which menu, where, what the click does) and talks to the
// engine only through ue_wrap (engine::InjectCanvasButton / WidgetIsHovered +
// reflection for the ui_menu_C class + field offsets).
//
// Click detection is a POLL, not a delegate bind: a POST observer on
// ui_menu_C::Tick (the menu's own per-frame game-thread tick) checks
// UButton::IsHovered() + a global VK_LBUTTON edge. A reflection-only DLL cannot
// bind the UButton::OnClicked FMulticastScriptDelegate (no UObject+UFunction to
// point it at), and the Tick observer is the one reliable game-thread tick that
// runs while the menu is up (net_pump does not run pre-gameplay). Mirrors the
// proven coop::save_button_disable pattern (same Tick observer, FindPropertyOffset
// field reads, isPause main-vs-pause discriminator).

#pragma once

namespace coop::multiplayer_menu {

// Resolve ui_menu_C + register the Tick observer (idempotent). Safe to call at
// boot: if the menu BP class is not loaded yet, a bounded retry re-attempts until
// it resolves. Gated off by [coop] multiplayer_menu=0 in votv-coop.ini.
void Init();

// TEST hook: inject the MULTIPLAYER button onto the live ui_menu_C right now,
// deterministically (used by coop::dev::menu_proceed to avoid the observer-timing
// race in the brief screenshot window). Game thread only.
void ForceInjectNow();

}  // namespace coop::multiplayer_menu
