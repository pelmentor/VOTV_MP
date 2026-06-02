// coop/dev/force_weather.h -- snow toggle (host-authoritative), driven by the
// ImGui dev menu (Game > Weather > Snow).
//
// Snow is the unambiguous weather visual (intComs_triggerSnow fans out to 53 BP
// listeners: particles + ground accumulation + sky tint). Host-only: the host
// broadcasts a WeatherState packet and the client mirrors via ApplyFromHost; a
// client SetSnow is a no-op (weather is host-authoritative).
//
// 2026-06-01: the legacy F5 hotkey was RETIRED (RULE [[feedback-dev-features-in-
// imgui-menu]]: dev features live in the F1 menu, not ad-hoc hotkeys). The action
// now lives behind ui::dev_menu's Snow checkbox, which calls SetSnow().

#pragma once

namespace coop::net { class Session; }

namespace coop::dev::force_weather {

// Cache the Session pointer so SetSnow can verify host role. Called from harness
// boot. Mirrors restore_vitals::SetSession.
void SetSession(coop::net::Session* session);

// Set snow on/off. HOST-only -- on a client this logs + no-ops (weather is
// host-authoritative). Dispatches DebugForceSnow on the game thread; the host's
// POST observer broadcasts the WeatherState to clients. Safe off the game thread.
void SetSnow(bool on);

// Current snow toggle state (for the menu checkbox). Lock-free.
bool IsSnowOn();

}  // namespace coop::dev::force_weather
