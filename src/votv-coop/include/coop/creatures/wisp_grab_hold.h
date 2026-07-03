// coop/wisp_grab_hold.h -- the killerwisp grab-window body placement, on EVERY peer.
//
// v2 of the killerwisp-vs-peers lane (2026-07-03; v1 relay = commit 1ae92a1a). The
// native kill sequence is hard-bound to player 0 (Capture: socket attach + fatality +
// the lift) -- a puppet victim used to get a flat 3.5 s death while the wisp mimed the
// tear alone and the victim's screen showed nothing but a timer. This module puts the
// BODIES where the native choreography puts them, on every screen:
//
//   * VICTIM peer (EngageSelf, from wisp_tear_mirror::OnWispGrab): replay the native
//     Capture player template against the LOCAL player and the LOCAL wisp MIRROR
//     (ue_wrap::wisp::ApplyGrabToLocalPlayer -- socket attach + MOVE_None + held +
//     camera decouple). The tear montage (WispTear) and the scheduled ragdoll death
//     (wisp_tear_mirror) complete the sequence; the death calls ReleaseSelf FIRST
//     (native releasePlayer shape: detach, then ragdoll).
//   * HOST + THIRD peers (EngagePuppet, from wisp_attack_sync::RelayGrab on the host /
//     wisp_tear_mirror::OnWispTear elsewhere): hold the victim's PUPPET at the local
//     wisp actor's 'playerGrab' socket -- a per-tick SetActorLocation follow (the
//     carry-drive shape, puppet_carry_drive precedent), NOT an engine attach: the
//     puppet pose apply (RemotePlayer::Tick) would fight an attach every frame, so the
//     hold instead OVERWRITES after it. Tick() therefore MUST run after net_pump's
//     g_puppets[].Tick loop -- the call site IS the ordering.
//
// No release wire exists on purpose: the hold self-releases on per-tick liveness
// guards (wisp died/despawned -- the host's +3.5 s despawn is the canonical edge, its
// EntityDestroy tears the client mirrors down the same second -- or the puppet left).
// The victim-side grab window is bounded by the kill deadline it rode in with.
//
// One-owner note (anti-smear): wisp_attack_sync owns WHO gets attacked (the aggro
// selector) and the host-side lift; wisp_tear_mirror owns the tear montage + the
// victim death; THIS module owns only the grab-window body placement. Game-thread only.

#pragma once

#include <cstdint>

namespace coop::wisp_grab_hold {

// VICTIM peer: our own player was grabbed (WispGrab accepted). Attach the local player
// to the local mirror of wisp `wispEid` (retry each Tick until it resolves, bounded by
// `killDelayMs` -- the mirror can race the reliable message by a frame). Game thread.
void EngageSelf(uint32_t wispEid, uint32_t killDelayMs);

// HOST/THIRD peer: hold the puppet of `victimSlot` at wisp `wispEid`'s 'playerGrab'
// socket until the wisp or the puppet goes away. Idempotent per (slot). Game thread.
void EngagePuppet(uint32_t wispEid, uint8_t victimSlot);

// VICTIM peer: undo the self grab (detach + restore movement/camera flags). Called by
// wisp_tear_mirror right before the ragdoll death fires, and by OnDisconnect (a
// mid-grab session teardown must not strand the player in MOVE_None). Safe no-op when
// not engaged. Game thread.
void ReleaseSelf();

// Per-tick pump: victim-side attach retry + every puppet hold (resolve wisp + puppet,
// snap the puppet to the socket, self-release on liveness misses). MUST be called
// AFTER the puppet pose apply loop (see the header note). Cheap no-op when idle.
void Tick();

// Peer `slot` disconnected -- drop its puppet hold (the puppet actor is going away).
void OnPeerLeft(uint8_t slot);

// Full reset (session teardown). Releases a live self-grab first.
void OnDisconnect();

}  // namespace coop::wisp_grab_hold
