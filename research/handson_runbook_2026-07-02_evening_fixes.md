# Hands-on runbook — 2026-07-02 evening batch (EHH + wedge + nameplate + model profile + SKINS + take-4)

**Deployed (take 4, LATE NIGHT):** DLL `94C3D016E482ABAE` (protocol **v93→v94** — BOTH
peers must run THIS dll; older ones are version-gated out) on all 4 installs +
re-cooked `rvi_scientist_v1sc.pak` `ED666BE5` (see item 5 below).
**Audits (2 agents on the diff): perf = PASS zero-critical** (all new paths O(1)/
event-edge; ini mutex never taken at frame rate; WARN: player_handshake.cpp 904>800 →
player_prefs_wire extraction QUEUED). **Correctness = 1 MED + 2 LOW found, ALL
root-fixed before this deploy:** (MED) nameplate ResetSlots ran on the bringup thread
→ the whole visibility store is now atomic any-thread (no assert trap, no race);
(LOW) unchecked fputs before the ini atomic swap → write errors now abort the swap;
(LOW) exists-check TOCTOU → re-checked per retry attempt. Wire bounds (byte-exact
offset traces incl. empty-skin), forgery guards, lane ordering (toggle can never
overtake its Join), builtin-skin safety = all PASS.
Take-4 adds, on top of everything take-3b had:
1. **F1 dev menu RESTORED on the host** — root cause found: the host's votv-coop.ini
   had lost its HEAD (the `[dev]` header, `enabled=1`, `devkeys=1` — everything above
   the appended probe keys) so the dev switch read absent. The ini has been rebuilt
   (your probe flags + guid + skin preserved) AND the underlying destroyer is fixed:
   WriteIniValue now aborts instead of rebuilding the file blind when the ini is
   read-locked, writes via an atomic .new+rename swap (no truncate window), and all
   ini access is serialized by one mutex.
2. **Nameplate OFF toggle (v94, synced)** — F1 > Cosmetics > Nameplate: "Show my
   nameplate to other players" checkbox. Off = your plate disappears on EVERY peer's
   screen; synced live (NameplateChange) AND to late joiners (Join/PlayerJoined prefs
   byte); persists in votv-coop.ini `nameplate=`; slot resets to visible on disconnect.
3. **Kerfur robot skins (builtin)** — the skins browser now also lists the game's own
   anthro-kerfur bodies (no pak needed): kerfur_omega (+_h/_m/_nc variants),
   kerfur_maid, kerfur_ariral, kerfur_ariral_suit, kerfur_keljoy, kerfur_mannequin,
   skerfuro, scrappy_keith, assbreather. They carry their own materials (no atlas
   texture) and animate on the same rig as every converter skin. No preview tiles yet
   (text tiles) — drop a `<name>.png/.bmp` next to the paks if you want previews.
4. **JOIN-JUMP fix** — "клиент прыгает по позициям где проходил хост": a joining
   client streamed its pose through the WHOLE load window (menu pawn, then the pawn
   being teleported through the HOST-SAVE positions as loadObjects applied). The
   sender now streams only when its own world is authoritative (the same coherence
   predicate as ClientWorldReady) — the host-side puppet spawns at the client's REAL
   first in-world position.

Take-2/3b changes (EHH pairing `2b2e0531`, wedge drain `d4833b9b`, nameplate box
`9180a386`, hygiene `9eda3faf`, v1-profile revert `e094093d`, SKINS v93 `bfee8f62`)
are all IN this DLL, still verdict PENDING. NO autonomous smoke ran (user at the PC —
this runbook IS the test).

## NEW take-4 tests (do these first)
1. **Dev menu back**: host F1 — Player/Game/Network categories are back (Cosmetics
   was the only one you could see). If still missing: read the top of
   votv-coop.ini — `[dev] enabled=1` + `devkeys=1` must be there.
2. **Nameplate toggle**: client F1 > Cosmetics > Nameplate → uncheck. Host: the
   client's floating name/health bar disappears (chat line "Nameplate: hidden...").
   Re-check → back. Then leave it OFF, disconnect, rejoin — the host must STILL not
   see it (the Join prefs byte). Host toggling its own works the same on the client.
3. **Kerfur skins**: F1 > Cosmetics > Skins → pick `kerfur_omega` — your body (look
   down) + your puppet on the other screen become the robot kerfur with its own
   textures. Try `kerfur_maid` / `skerfuro` for fun. Then back to a converter skin
   (hl_einstein/rvi) — the atlas texture must come back clean; then `dr_kel` —
   native body, no stuck atlas.
4. **Join-jump**: watch a client join from the host. The client puppet must NOT
   teleport-hop through places you (the host) have been — it should appear once,
   at the client's real spawn spot, when the client's world is up.
   Log marker (client): `net_pump: ClientWorldReady announced` — poses start only
   after this line now.

## The v93 SKINS system (from take 3b — still test if not yet done)
Every player now has a persistent body skin (`votv-coop.ini player_skin=`, written next
to `player_guid=`; a fresh identity gets `hl_einstein_v1sc` — so BY DEFAULT the HOST is
now ALSO a scientist; pick `dr_kel` in the browser if you want the host back to kel, it
persists).
1. **F1 → Cosmetics → Skins**: tile browser with previews (dr_kel + hl_einstein_v1sc +
   rvi_scientist_v1sc — previews read `<name>.png/.bmp` next to the pak; your placed
   .bmp files are exactly that). Current skin = green tile.
2. **Local first-person body**: look down — your OWN torso/legs wear YOUR skin (the
   immersion fix). Host default = scientist now (see above).
3. **Live change**: pick another skin mid-game — your body changes instantly, the OTHER
   peer sees your puppet change within a second, chat line "<nick> changed skin to ...".
4. **Persistence**: quit the client, rejoin — the skin came back from the ini.
5. **rvi_scientist_v1sc — RE-COOKED (late 2026-07-02)**: your first converted pak was
   broken (A-posed arm chunks; toe-half of each foot pinned to the pelvis) — the
   converter postmortem found 106/764 verts silently mis-skinned + a foreign repose
   profile. The pak on all 4 installs is now cooked FROM YOUR OWN manual pose
   (`..._my_pose_good.psk`, reproduced to 9e-5; toes ride the feet, the head prop rides
   the head; pak `ED666BE5`, old bad hash was `0AC43284`). Pick it — expect YOUR pose
   and proper limb tracking on both your body and your puppet on the other screen.
6. **dr_kel revert**: pick dr_kel — body back to kel WITHOUT the atlas texture stuck on
   it (the material override clear).
Log markers: `local_body: native kel mesh captured`, `client_model: ... -> skin '<name>'`,
`player_handshake: announced local skin`, `skin_registry: N skin(s) catalogued`.
If a skin shows KEL on the other peer: that peer's log will say
`client_model: skin '...' mesh NOT loadable (pak absent...)` — pak missing there.

## What changed
1. **EHH on E-drop (client)** — a cancelled use-PRESS now deterministically cancels its
   paired E-RELEASE (`_42`); no more condition re-derivation at release time.
2. **Pile ghost-wedge** — a GrabIntent for an eid that is dead on the host now broadcasts
   `PropDestroy(eid)`: every peer drains the stale row, the next aim re-resolves to the
   real pile. No more "host must hand-cycle the pile".
3. **Nameplate** — the translucent black backing box is gone (text outline carries
   readability). Restorable from git.
4. **Client model** — VERDICT IN (take 1): the v2 wide profile look was REJECTED
   in-game → the scientist is RE-COOKED on the **v1 narrow profile** (this pak,
   `AE49002C`; repose reproduces the v1 manual PSK at max 0.00009). v1 is the library
   DEFAULT again; v2 wide stays in `tools/client_model/profiles/` unused. The
   `hl_einstein_v1sc` rename stands.

## The test (host + client, normal pile play)
1. **Model look**: client's puppet on the host screen = the scientist back on the
   FAMILIAR v1 proportions (narrow, the look that passed 2026-07-02 morning). Confirm
   it matches what you liked.
2. **Nameplates**: no black rectangle behind nick/health bar; text still readable.
3. **E-drop**: carry a pile clump, E-drop it repeatedly — NO "EHHH" on release.
   Client log marker per drop: `[USE-RELEASE] paired E-release CANCELLED`.
4. **Wedge probe**: grab/drop piles rapidly, many cycles. If a grab is ever silently
   eaten, press E again — it must self-heal within one press (host log:
   `broadcasting PropDestroy(eid) so every peer drains its stale ghost row`). A pile
   should NEVER stay ungrabbable.
5. (Head fix already VERIFIED both peers this morning — no re-test needed.)

## If something's off
- EHH still sounds on drop → client log around the drop: is `[USE-RELEASE] paired
  E-release CANCELLED` present? Send the lines.
- A pile wedges → host log: `DENIED eid=... -- eid unresolvable` present? PropDestroy
  broadcast line present? Send both peers' lines for that eid.
- Model missing / kel skin on client puppet → host log `client_model:` lines (pak
  mount / LoadObject path) — the paths changed to hl_einstein_v1sc this build.

## Honest status
Take-4 (ini hardening + nameplate toggle + kerfur skins + join-jump gate): AS-BUILT,
compiled clean, deployed 4/4 (DLL `94C3D016`), NO smoke (user at PC — this runbook is
the test); two audit agents ran on the diff — perf PASS zero-critical, correctness
1 MED + 2 LOW all root-fixed pre-handoff (see header). Protocol v93→v94: BOTH peers
must run this DLL. Take-2/3b changes (EHH
pairing, wedge drain, nameplate box, v1-profile model, SKINS system) still verdict
PENDING, all in this DLL. rvi pak re-cooked from the user's own pose (`ED666BE5`).
QUEUED extractions: remote_player_spawn (841>800), puppet_diag (967>800),
player_handshake (~900 after v94). The wedge's UPSTREAM root (keyed-prop GC-churn
re-bind by KEY) is still the next thread.
