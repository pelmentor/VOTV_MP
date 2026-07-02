# Hands-on runbook — 2026-07-02 evening batch (EHH + wedge + nameplate + model profile + SKINS + take-4/5/6)

## TAKE 6 (your 19:1x-19:2x reports: join-jump STILL + FPS storm + ungrabbable pile + random starter) — test THIS first

**Deployed (take 6 FINAL):** DLL `56B2F9CDBD049C89` on all 4 installs (protocol stays
v94; paks unchanged). Join-fix arc: `3bb22e3b`..`99a9a06e` (two hook attempts) →
**`614cade8` (the final mechanism)**; `c7a0f5de`+`c7f11957` (FPS storm), `8c13858f`
(wedge deny heal), `4570180e` (random starter skin). Perf audit PASS zero-critical.

**Your 20:0x/20:14 reports ("puppet peer нету вообще") — root found on the second
verdict and the mechanism REPLACED (`614cade8`):** gm `loadObjects` is called
BP-locally (`EX_LocalFinalFunction`), and script->script local calls dispatch through
`ProcessLocalScriptFunction` — they touch NEITHER ProcessEvent NOR `UFunction::Func`,
so ANY hook on loadObjects can never fire (both 20:07 and 20:14 logs: hook installed
on the live class, loadObjects ran — its props spawned — POST silent; the gate never
opened; the client never streamed; no puppet; every pile grab denied "puppet not
live"). The whole save_apply_gate hook module is DELETED (RULE 2). The pose gate now
OBSERVES loadObjects' effect instead of hooking its call: **load-tail quiescence**
(the population-stability signal the destructive divergence sweep already trusts,
which by construction flips only after loadObjects' spawn flux — including the player
teleport — has ended; resets per world). Host keeps the worldUp gate.

RETEST (client rejoin): client log must show `join_membership_sweep: ... DIVERGENCE
SWEEP` / quiescence firing, and the HOST must see the puppet appear ONCE at the
client's real spawn ~2-4 s after the client's world settles — no parked-spot
teleports, no missing puppet. Then pile grabs from the client must work again.

**VERDICT 20:2x (user): "щас работает" — puppet visible + pile interaction works on
`56B2F9CD` [hands-on V for the stream/puppet mechanism]. The kerfur-skin surprise at boot
was the client's own persisted 19:39 pick (kerfur_mannequin), by design — switched to
walter2_v1sc at 20:25.**

## NEW BUG 20:27 (user) — NEXT SESSION ENTRY: client-local DUP pile + still-live 4 Hz drain

User: "клиент взаимодействует с дюп пайлом своим локальным, который видит только он".
Evidence captured (client log 20:24-20:27, DLL `56B2F9CD`), full write-up in
docs/piles/12 status block:
- eid=4435: host moved the pile in the join window; the client's native NEVER BOUND
  (`save_identity_bind: ... 1 unbound chip [by position] ... 0 re-bound` every pass).
- => stale native@old (1672,-379,6124) lives = the client-only dup (no eid → native grab
  → no GrabIntent → host-invisible, the L1-orphan shape). The 20:27:25 `use click at
  (1357,-390)` with no GRAB-INTENT after it = him touching it.
- => the armed `[PILE-B3] pos-correction eid=4435 → (1424,-373)` can never apply and has
  NO pass cap (twins cap 40, destroys 8, pos-corrections NONE) → HasPendingWork pinned →
  `quiescence_drain` re-arms at 4 Hz forever (20:25-26 log) — the FPS-storm's SECOND leg
  (the first leg, hopeless steady-state twins, is fixed and confirmed gone from this log).
- FIX per rule 1 next session = the keyed/position RE-BIND thread (now 3 instances:
  eid 2947, 3129, 4435) + terminalize/cap unbindable pos-corrections + the deny-heal
  receive half (audit: RegisterPropMirror rebindInPlace=false → Install rejects the dup
  eid → the 8c13858f re-assert is currently a client-side NO-OP).

**Your four reports, each root-fixed from THIS session's logs (19:10-19:24):**
1. **"клиент всё ещё прыгает по координатам где хост был"** — take-4 gated the stream on
   world coherence, but the 19:10 log proved world-ready fires ~4 s BEFORE the game
   PLACES the pawn: the client streamed its parked spot (-37695,69978) — the map's
   pre-placement parking every load passes through (the host parks there too, hence
   "где хост был") — until gm `loadObjects` teleported it at :58. The stream now ALSO
   requires the pawn-placement latch: a Func-table POST hook on the game's own
   `loadObjects` (the function that applies `playerTransform` from the save). First
   streamed pose = your REAL placed position, both roles (host mid-world-change is
   gated the same way).
2. **"нестабильный ФПС у клиента"** — measured, not guessed: [HITCH-SRC] showed OUR
   net_pump::Tick at 21-22 ms, 4-5×/sec, through all of 19:23-19:24. Root: the
   join-window save-time maps never emptied, so EVERY host pile grab all session long
   stamped a save-time key, every LAND armed the client a HOPELESS reconcile twin, and
   each twin pinned the quiescence drain to 40 passes × 250 ms of full-GUObjectArray
   sweeps. The maps now retire when the join window truly closes (b3 late-flush expiry,
   ~25 s after world-ready) — steady-state pile play arms nothing.
3. **"pile с GUI взаимодействия, который нельзя взять вообще"** — host log 19:24: eid=3129
   denied 8× with "live actor is trashBitsPile_C, not a chipPile (cross-peer identity
   smear)" — a SILENT deny branch (take-2 covered only the not-live branch), so your
   client kept its stale pile-row forever. Now every terminal deny answers with truth:
   wrong-class → the host re-broadcasts the authoritative row (one debounced incremental
   PropSpawn) and your client re-binds the eid to the real entity. NOTE the smear's
   UPSTREAM (how one eid named different actors on two peers) is the next thread
   (keyed-prop GC-churn re-bind by KEY) — this fix un-wedges the interaction; the
   specific orphaned pile actor may still need the host to cycle it once.
4. **NEW FEATURE: random starter skin** — a NEW peer (no `player_skin=` in its ini) now
   rolls one of: walter_v1sc, sci_v1sc, rvi_scientist_v1sc, luther_v1sc,
   twhl_scientist2_v1sc, twhl_scientist3_v1sc — filtered to paks actually present in
   LogicMods/votv-coop on that install (fallback hl_einstein_v1sc if none). Persisted on
   first boot like the guid; existing inis keep their choice.

**Take-6 tests:**
1. **Join-jump**: client menu-join while you (host) walk around. Host screen: the client
   puppet must appear ONCE, at the client's real spawn, when its world is applied — no
   hop to a far point, no 3-second far-away freeze, no snap-back. Client log check:
   `save_apply_gate: gm loadObjects DONE` BEFORE the first pose goes out.
2. **FPS**: client plays piles (grab/drop a dozen) 15+ minutes after joining. The
   [HITCH-SRC] 21 ms storm must be gone from the log; grab/drop feels smooth. Host log:
   one `[PILE-09] slot N join window CLOSED (b3 late-flush expiry)` line ~25 s after the
   join, then NO more `[PILE-09] CLIENT armed pending save-time twin` on the client for
   steady-state drops.
3. **Wedged pile class**: if you find another GUI-but-ungrabbable pile: press E once,
   wait ~2 s, aim again and press E — it should either grab or visibly re-resolve
   (host log: "re-asserting the authoritative row"). Report if a pile stays wedged
   through that.
4. **Random starter**: delete `player_skin=` line from copy2's votv-coop.ini (or use a
   fresh install), boot — it should roll one of the six and persist it (log:
   "new identity rolled starter skin ...").

**To recheck 19:24's stuck pile specifically:** it heals on restart anyway (runtime state);
the deny-heal covers future occurrences live.

---

## TAKE 5 (assbreather locomotion kill + 14 MORE kerfur skins) — test after take-6

**Deployed (take 5):** DLL `D6903F22605BF503` on all 4 installs (protocol stays v94 —
no wire change; paks unchanged: hl_einstein `AE49002C`, rvi `ED666BE5`).

**Your two reports, both root-fixed measured-not-guessed:**
1. **"locomotion stopped ticking after assbreather, other skins broken after"** —
   `sk/assbreather` is a BROKEN GAME ASSET the game itself never loads: its root
   bone is `rootKerfur_010` (a Blender-duplicate leftover), which does NOT exist in
   kerfurOmegaV1_Skeleton. Applying it breaks the skeleton→mesh bone-0 mapping and
   poisons the body's AnimInstance; the instance survives later mesh swaps (same
   skeleton asset), so every skin after it stayed frozen. It is REMOVED from the
   builtin list; a 4-check census (skeleton import + root bone + every bone ∈
   skeleton + export name) now gates every entry —
   `tools/client_model/builtin_skin_census.py`, re-run at each game re-target.
   The antibreather LOOK is still there: `kerfur_antibreather` = the asset the
   game actually uses for its antibreather kerfur (census 4/4 OK).
   Your frozen session heals on RESTART (runtime-only poison; your ini is fine —
   it points at `femscientist`).
2. **"ты добавил НЕ ВСЕ скины anthro керфуров"** — верно: the old asset extraction
   was partial (3032 of 42941 pak files) and blind to skins outside kerfurAnthro/sk.
   Listed the REAL pak, extracted + censused everything: **25 verified builtins**
   (was 12 with 1 poisoned). NEW: kerfur_antibreather, kerfur_argplush,
   kerfur_alien, kerfur_fleshly, kerfur_skeleton, kerfur_vargskeleton,
   kerfur_maxwell, kerfur_erie, kerfur_erie_v4, kerfur_igetis, kerfur_monique,
   kerfur_krampus, kerfur_mynet, kerfur_furfur.

**Take-5 tests:**
1. RESTART both peers (heals the poisoned anim state from the old session).
2. F1 > Cosmetics > Skins: `assbreather` is GONE; 14 new kerfur_* tiles present
   (list scrolls). Pick `kerfur_antibreather` — the antibreather look, walking
   normally on both screens. Spot-check a few new ones (maxwell, skeleton,
   krampus...) — locomotion must keep ticking through EVERY switch, and a
   converter skin (rvi/femscientist) then dr_kel must come back clean after.

---

## Take 4 (previous deploy `94C3D016E482ABAE`, superseded by the DLL above)

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
   skerfuro, scrappy_keith, assbreather (REMOVED in take 5 — broken game asset, see
   the take-5 header). They carry their own materials (no atlas
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
