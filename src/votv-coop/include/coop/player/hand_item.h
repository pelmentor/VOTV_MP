// coop/player/hand_item.h -- the hotbar HAND-ITEM display axis (v105).
//
// RULE-1 root fix for "peer's held item updates late/never" (user 2026-07-06;
// host log 12:40:05 proof): the item shown in a player's hand is NOT a world
// entity -- VOTV's updateHold destroys + respawns a fresh local actor on every
// quick-slot switch -- yet it rode the WORLD-prop pipeline
// (PropSpawn/PropPose/PropRelease). That mismatch produced: the host's
// PRE-QUIESCENCE express gate suppressing every host hand item forever (the
// client dropped 60 Hz keyed poses -- 'no local match' x1809), physics-release
// litter/dupes on switch-away, and a fresh key/mirror churn per switch.
//
// The proper model (MTA shape: a ped's current weapon is PLAYER STATE attached
// to the ped locally on every machine): the hand item is part of the player's
// EXPRESSION, like skin/nick color.
//   - owner polls mainPlayer.holding_actor (Aprop_C only -- the trash
//     clump/pile carry stays on its own lane) and broadcasts a small reliable
//     HandItem{class, name} on change;
//   - every peer keeps a DISPLAY-ONLY mirror (physics off, collision off,
//     spawn-echo suppressed) attached in front of the puppet;
//   - nothing per-tick on the wire; switch latency = one reliable message;
//   - late-join: the host stores per-slot state and replays it in the
//     ConnectReplayForSlot fanout.
//
// All functions game-thread only.

#pragma once

#include "coop/net/session.h"

#include <cstdint>

namespace coop::hand_item {

// Owner side, per tick (from local_streams::Tick). `holdingProp` is the local
// player's holding_actor IF it is a live Aprop_C descendant, else nullptr.
// Detects change (debouncing updateHold's 1-frame null flicker) and announces.
void TickOwner(coop::net::Session& session, void* holdingProp);

// Receiver side, per tick (same site): lazily spawn/replace/destroy the
// display mirrors so a state that arrives before the puppet exists (join) or
// a puppet respawn are absorbed without event ordering.
void TickMirrors();

// event_feed router entry. Parses, forgery-guards, stores, host-rebroadcasts.
bool HandleHandItem(coop::net::Session& session,
                    const coop::net::Session::ReliableMessage& msg);

// Host -> a just-world-ready joiner: replay every slot's current hand state
// (subsystems::ConnectReplayForSlot; session = the one cached by TickOwner).
void ReplayPeerStatesToSlot(int slot);

// Session lifecycle.
void Reset();                          // destroy all mirrors + clear states
void OnSlotDisconnected(uint8_t slot); // destroy that slot's mirror + state

}  // namespace coop::hand_item
