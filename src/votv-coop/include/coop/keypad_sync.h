// coop/keypad_sync.h -- password-keypad (ApasswordLock_C) mirror sync (protocol v33).
//
// Gameplay/network layer (principle 7): owns the wire protocol, the per-tick state
// poll, the receiver apply, the key->actor index, the deferred-apply retry, and the
// connect-snapshot. Talks to the engine ONLY through ue_wrap::passwordlock.
//
// WHY ITS OWN MODULE (not the interactable_sync toggle Channel): a keypad is NOT a
// 2-state toggle. It carries a typed digit BUFFER plus three state bools, and -- proven
// by 3 autonomous synth-probe rounds 2026-06-04 -- its native accept verb is UNREACHABLE
// by us (Open/open2/SetActive/isButtonUsed/processKeys are all inert even with the
// buffer filled + focusOn). The v31 attempt to force it into the toggle Channel
// ("poll isAcc + replay Open(want)") fail-cycled exactly because Open is a submit verb.
//
// THE MIRROR (RULE 1, mechanism empirically forced):
//   SENDER  (every peer, per net-pump tick): poll each indexed keypad's
//           (inPassword, isAcc, isDeny); broadcast KeypadSyncPayload on change.
//   RECEIVER: resolve by Key, then -- replay inputNumber(digit) for the typed-buffer
//           DELTA (native display+beep; Reset() on a clear) + DIRECT-WRITE isAcc/isDeny
//           (the accept verb is unreachable; the writes stick) + best-effort upd() to
//           repaint. (`Active` is NOT mirrored -- it drives a light -> purple-light bug.)
//           It NEVER calls a submit verb -> no fail-cycle, and it
//           never touches an idle keypad's local entry (only mirrors received edges).
// The gated door converges independently via the DoorState channel (no coupling).
//
// RE: research/findings/votv-keypad-passwordlock-accept-RE-and-coop-sync-2026-06-04.md.

#pragma once

#include <cstdint>

namespace coop::net {
class Session;
struct KeypadSyncPayload;
}  // namespace coop::net

namespace coop::keypad_sync {

// Resolve the passwordLock_C class + build the key->actor index; store the session
// pointer. Idempotent; retried every net-pump tick until the BP class loads. Game thread.
void Install(coop::net::Session* session);

// Receiver entry: a KeypadState packet arrived (payload already memcpy'd + range-checked
// by event_feed). Resolves the keypad by Key and applies on the game thread (deferring
// if it has not streamed in yet). Called from event_feed's reliable drain loop.
void OnReliable(const coop::net::KeypadSyncPayload& payload, uint8_t senderPeerSlot);

// HOST-only: snapshot the current state of every indexed keypad to a freshly connected
// client `peerSlot` (so an in-progress / already-unlocked keypad matches on join). The
// receiver idempotently skips already-matching ones. Net-pump connect edge. Game thread.
void QueueConnectBroadcastForSlot(int peerSlot);

// Per-tick pump: throttled index rebuild + deferred-apply retry, then the sender poll.
// Call every net-pump tick on the game thread.
void Tick();

// Session teardown: clear the per-session index + dedup + pending state.
void OnDisconnect();

}  // namespace coop::keypad_sync
