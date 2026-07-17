// coop/desk_input_sync.h -- v112: the CLAIM-FREE field-granular desk INPUT lane
// (the BUGS-v111 fix; /qf-converged 2026-07-16, 12 rounds --
// research/findings/computers-devices/votv-desk-input-lane-DESIGN-2026-07-16.md).
//
// THE AXIS (measured, client log 2026-07-16): the desk claim engages only on the
// intComs activeInterface edge; the download unit's physical +1/+5/+15/toggle/dir
// buttons are WORLD-SPACE presses that never set it -> the v111 occupant-intent
// lane never engaged (client input dead, cooldown erased, sounds starved). Desk
// verbs are EX_Local* PE-INVISIBLE -> the POLL is the canonical change detector.
//
// The model: every INPUT-class scalar (knob speeds, filter toggles, polarity dir,
// volume, select, maxLevel, the four unit power toggles) is shared LAST-WRITER
// state. A 250 ms per-field poll detects the presser's local change
// -> a field-granular DeskInput delta (client -> host; the host applies + relays
// to every peer EXCEPT the originator -- an echo would revert a newer local value).
// Receivers apply through the field's native side-effect path (patch +
// WriteScalars' upd chain; setter-effect replication for the 5 setter-managed
// fields) and PRIME their own poll baseline for exactly that field in the same
// GT task (echo-proof).
//
// COOLDOWN: charge = any UPWARD poll jump (all charging verbs are measured
// cooldown<=0-gated -> no downward writes exist) -> a CooldownCharge delta with
// the presser's observed value; decay stays native per-peer (dt-scaled). The
// SAME jump classifies the SHIFT quick-scan: only the scan charges to the FULL
// coord_maxCooldown (dots charge to half; the ENTER ping never touches it) ->
// newValue > maxCooldown/2 + 0.01 -> also a DeskScanEvent; mirrors replay the
// accepted-branch EFFECTS only (spawnDirs + beepLong1; never useSearch -- its
// own gate would refuse on per-peer decay jitter).
//
// CoordIsPing (v116 ROOT FIX): the field still rides the wire, but receivers
// treat it as BOOKKEEPING ONLY -- never written into the machine. It is the
// native ping FSM's RUN-FLAG (a latent tick machine, analogd uber @82980
// IFNOT(coord_isPing) -> the @80105 stage engine with ==1.0 latches at
// @79979): the v112 raw apply WOKE a phantom parallel sim on every observer
// (measured 2026-07-17 -- divergent verdicts, a phantom host ARM, double
// coordLog authorship). ONE machine runs per ping: the presser's, organically.
// Observers keep no ping display (the pre-existing R2 residual). The rising
// edge arms the host's ping attribution; device_occupancy consults
// PingActiveSlot() to deny desk claims to other slots for the ping's duration
// (replaces the native OnKeyDown coord_isPing swallow the raw write used to
// provide by accident: a non-holder's DishAim never ships, so the presser's
// committed dots can't be stomped mid-FSM).
//
// One concept = one folder: lives with the other desk/device interactables.

#pragma once

#include <cstdint>

namespace coop::net {
class Session;
struct DeskInputPayload;
struct DeskScanEventPayload;
}

namespace coop::desk_input_sync {

void Install(coop::net::Session* session);

// Game thread, per pump tick (internally throttled to the 250 ms poll).
void Tick();

// Wire appliers (event feed drain; GT).
void OnDeskInput(const coop::net::DeskInputPayload& p, uint8_t senderSlot);
void OnDeskScan(const coop::net::DeskScanEventPayload& p, uint8_t senderSlot);

// Re-prime every poll baseline from the CURRENT desk fields -- called by the
// join-adopt apply (console_state_sync) after it seeds the scalars, so the
// seeded values never read as local edges.
void PrimeBaselines();

// HOST: a peer left -- if it was the live ping setter, clear the attribution
// (the desk-claim deny would otherwise outlive the leaver). No machine write:
// since v116 no peer's machine carries a wire-applied coord_isPing.
void OnPeerLeft(int slot);

// HOST: the slot whose ping FSM is currently running (0 = the host itself),
// 0xFF when none. device_occupancy's desk-claim arbitration consults this.
uint8_t PingActiveSlot();

// HOST, connect edge (ConnectReplayForSlot): re-derive the ping attribution
// from the MACHINE's ground truth. A solo host's rising edge is absorbed into
// the unwired baseline (Tick's !connected() branch never diffs), so a client
// joining mid-host-ping would otherwise see no FSM-hold (audit 2026-07-17
// CRIT-1). Reads coord_isPing; TRUE -> setter = 0 (only the host's own FSM
// can be running when no peer was connected to author a delta).
void SeedPingAttributionFromMachine();

void OnDisconnect();

}  // namespace coop::desk_input_sync
