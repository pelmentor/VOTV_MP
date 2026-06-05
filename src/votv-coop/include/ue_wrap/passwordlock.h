// ue_wrap/passwordlock.h -- standalone engine access for VOTV password keypads
// (ApasswordLock_C). Principle-7 engine-wrapper layer: it wraps the reflection /
// struct-offset / UFunction-thunk details of a keypad actor. NO network logic, NO
// gameplay/coop state -- coop::keypad_sync owns those and drives the keypad mirror
// through here.
//
// A keypad is an AtriggerBase_C descendant that gates a door. Cross-peer identity is
// the inherited AtriggerBase_C::Key FName (deterministic placed-trigger key, save-
// persistent; v31 proved it stable, keysHash 0xF75D.../14).
//
// HOW THE MIRROR WORKS (RE-and-probe-proven 2026-06-04, 3 autonomous synth rounds --
// research/findings/votv-keypad-passwordlock-accept-RE-and-coop-sync-2026-06-04.md):
//   * Every keypad verb (Open / inputNumber / focusOn / isButtonUsed / processKeys /
//     SetActive) dispatches BP-internally (CallFunction -> ProcessInternal, bypassing
//     our ProcessEvent detour), so a POST observer NEVER fires on them -> the sync
//     POLLS state, it cannot observe verbs.
//   * The NATIVE ACCEPT is unreachable by us: with inPassword == password, calling
//     Open(true) / open2 / SetActive / isButtonUsed / processKeys -- even after
//     focusOn() -- does NOT flip isAcc (proven inert on the real keyed+door keypad).
//     The accept needs the genuine focused-click interaction we cannot synthesize.
//     => the receiver must NOT call a "submit" verb (that was the v31 fail-cycle).
//   * inputNumber(digit) IS callable and APPENDS to inPassword natively (display +
//     beep come for free) -- so the receiver mirrors TYPING by replaying inputNumber.
//   * isAcc / isDeny / Active DIRECT FIELD WRITES stick -- so the receiver mirrors the
//     accept/deny/lock STATE by writing those bools (the gated door is the door
//     channel's job; upd() is a best-effort visual refresh after the writes).
//
// RE: research/findings/votv-keypad-passwordlock-accept-RE-and-coop-sync-2026-06-04.md.

#pragma once

#include <cstdint>
#include <string>

namespace ue_wrap::passwordlock {

// Resolve the passwordLock_C UClass + the field offsets (Key / isAcc / isDeny /
// inPassword / Active) + the inputNumber / Reset / upd UFunctions. Idempotent; true
// once everything resolved (false while the BP class is not yet loaded -- the caller
// retries on a later tick). Game thread.
bool EnsureResolved();

// True iff `obj`'s class is passwordLock_C or a subclass. Cheap (bounded super walk;
// no allocation). False if not yet resolved.
bool IsPasswordLock(void* obj);

// The keypad's AtriggerBase_C::Key FName as a wide string. Empty on failure
// (null / not resolved); L"None" if unkeyed.
std::wstring GetKeyString(void* lock);

// The full mirror-relevant state of one keypad: the typed buffer (inPassword) + the
// three state bools. The sender polls this each tick and broadcasts on change.
struct State {
    std::wstring buffer;   // inPassword -- the digits typed so far (display)
    bool isAcc = false;    // accepted (momentary green flash on a correct submit)
    bool isDeny = false;   // denied (momentary red flash on a wrong submit)
};

// Read `lock`'s mirror state into `out`. False if the read could not be made
// (null / not resolved); leaves `out` untouched on failure. Game thread.
bool ReadState(void* lock, State& out);

// --- Receiver apply primitives (game thread) -------------------------------------
// Replay one typed digit (0..9): dispatches inputNumber(digit), which APPENDS to
// inPassword the native way (display + beep). This is how the receiver mirrors typing
// -- NOT a submit. False on null / unresolved UFunction / out-of-range digit.
bool CallInputNumber(void* lock, int32_t digit);

// Clear the typed buffer natively (Reset() -- empties inPassword/Num via the engine's
// own FString management, so no hand-written FString / leak). Used when the sender's
// buffer reset (post-submit clear / backspace) before re-mirroring. False on null /
// unresolved.
bool CallReset(void* lock);

// Direct field writes of the accept/deny bools (the verified mirror mechanism -- the
// native accept verb is unreachable, see the header note). Safe: plain bool writes, no
// submit path, so they CANNOT trigger the v31 fail-cycle. No-op if not resolved.
// NOTE: there is deliberately NO WriteActive -- `Active` drives a LIGHT via the keypad's
// powerChanged(...active_light), and writing it on a mirror turned a host light purple
// (hands-on 2026-06-04). The lock state we mirror is isAcc/isDeny only.
void WriteAccepted(void* lock, bool v);
void WriteDenied(void* lock, bool v);

// Best-effort visual refresh after the field writes: dispatches the keypad's own upd()
// verb (if present) so a tick/event-driven LED/material can repaint from the freshly-
// written state. Low-risk (an "update" verb, not a gameplay/submit verb); a no-op if
// upd() is absent. Whether it actually repaints the LED is a hands-on check.
void CallUpd(void* lock);

}  // namespace ue_wrap::passwordlock
