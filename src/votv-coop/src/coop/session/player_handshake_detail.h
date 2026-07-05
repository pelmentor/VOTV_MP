// coop/session/player_handshake_detail.h -- INTERNAL (src-tree) shared header
// for the player_handshake module's two translation units:
//   player_handshake.cpp       -- the Join/PlayerJoined/AssignPeerSlot identity
//                                 handshake (owns g_skinBySlot + the nick/guid
//                                 side-tables)
//   player_handshake_prefs.cpp -- the live display-pref change family
//                                 (SkinChange / NameplateChange / NickColorChange
//                                 announce + forgery-guarded handle + rebroadcast)
// Split 2026-07-05 (modular file-size rule: player_handshake.cpp had grown to
// 1043 LOC when the v103 nick color landed). NOT part of the public module API
// (that is include/coop/session/player_handshake.h); nothing outside these two
// TUs may include this.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace coop::player_handshake {

// Store + live-apply a peer's skin: writes the identity handshake's
// g_skinBySlot side-table (owned by player_handshake.cpp) and re-skins the
// slot's puppet if already spawned. Game thread only.
void StoreSkinForSlot(int slot, std::string name);

// Parse one [u8 len][ASCII] skin field. Returns bytes consumed (0 =
// malformed/absent); `out` untouched unless a well-formed non-empty field
// validated as a skin name (IsValidSkinName -- it becomes a LoadObject
// package path component).
size_t ParseSkinField(const uint8_t* p, size_t remaining, std::string* out);

}  // namespace coop::player_handshake
