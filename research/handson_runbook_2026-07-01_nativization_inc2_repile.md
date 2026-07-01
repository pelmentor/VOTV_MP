# Hands-on runbook 2026-07-01 -- pile nativization INCREMENT 2 (re-pile LAND -> native)

**Deployed `votv-coop.dll` SHA256 = `2217732B...` (4 folders hash-verified OK). Build EXIT 0, Release.**
HEAD before this test = `abfaaed8` (increment 1); increment-2 code is UNCOMMITTED WIP on disk (commit gate = this test green).

## What changed vs increment 1 (the OBSERVABLE win)
Increment 1 only nativized the uncommon steady-state host-only PropSpawn pile -- near-invisible.
Increment 2 nativizes **the re-pile LAND**: when a carried CLUMP proxy settles into a resting pile
(`OnConvert kToPile` on a proxy), the client now RETIRES the proxy and MATERIALIZES a rooted real
`actorChipPile_C` native in its place (rebind-in-place first, destroy the proxy actor after -- the exact
inverse of the already-proven ToClump native->clump morph hand-off).

Result: **a re-piled pile is now a real native** -> it has the native hover GUI on aim, its own varied
(random) rotation, and real collision. This is the path the user's actual GUI-less proxy piles came from.

New primitive: `trash_proxy::RetireProxyActorOnly(eid)` -- destroys+un-roots the proxy ACTOR without
unbinding its Element (the caller already rebound the Element onto the native). Contrast `RetireProxy`
(full teardown, deletes the Element) -- using that here would delete the Element the native now owns.

## HONEST scope (what is NOT yet native)
- **Join-window host piles that already existed at join** are STILL bare proxies (they come through the
  in-bracket path, untouched by increment 2). Those still have NO hover GUI / identity rotation until the
  RULE-2 cleanup (bind-the-loaded-native). So: a pile that was ALREADY on the ground when you joined =
  still proxy; a pile you (or the host) GRAB + THROW + re-pile after joining = becomes native.
- The in-hand / flying CLUMP is intentionally still a proxy (that is correct -- clumps stay proxies).

## Setup (Alex triggers; Claude does NOT launch)
1. `votv-coop.ini [dev]`: `native_pile_inert_probe` must be `0` (probe disarmed -- it is; leave it).
2. Launch host via `mp_host_game.bat`, client via `mp_client_connect.bat` (named windows). Fresh save / New Game.

## TEST -- the re-pile nativization (do this on the CLIENT's view)
1. On the client, walk up to a trash **clump** or grab a **pile** (E) -> carry it.
2. Throw / drop it so it re-piles (lands as a pile).
3. **AIM the client crosshair at the landed pile.** EXPECT: the **native hover GUI** appears (the pile's
   name / interact prompt), exactly like a real single-player pile. (Before increment 2: no GUI on a
   re-piled pile.)
4. EXPECT: the landed pile's **rotation looks varied/natural** (not the same identity orientation every time).
5. Walk into it -- EXPECT it **blocks movement** (native collision), not walk-through.
6. **Full cycle:** grab that now-native pile again (E). EXPECT it converts to a carried clump (the morph
   hand-off), carries smoothly, and on the next land re-nativizes. Repeat a few times.

## What to READ in the CLIENT log (paste the tail if anything looks off)
- `[PILE] CLIENT ToPile LAND eid=<N> ... -> NATIVIZED native=<ptr> at (x,y,z) host=(x,y,z) drift=<D>cm`
  -- **drift should be ~0** (the native rendered at the host's landed rest). This is the "client renders
  the host pose" gate.
- `[PILE] native_pile_mirror: MATERIALIZED eid=<N> ... (rooted, tick-off, kinematic, Movable, native collision)`
- `[PILE] trash_proxy: RETIRE-ACTOR-ONLY eid=<N> ... Element KEPT` -- the proxy actor was destroyed, Element kept.
- On re-grab: `[PILE] CLIENT convert GRAB(pile->clump) eid=<N> -> bound save-loaded NATIVE pile GRABBED ->
  handed to runtime clump proxy` -- the morph hand-off firing on the now-native pile.

## FAIL signals -> tell Claude, do NOT commit
- **DUP:** the re-piled pile shows TWICE (a native + a leftover proxy/twin). Would mean RetireProxyActorOnly
  didn't destroy the proxy, or the rebind created a second entity.
- **MISSING:** the pile vanishes after landing (Materialize returned null -> should fall back to a proxy
  re-skin; look for `native materialize FAILED, fell back to proxy re-skin`).
- **WRONG SPOT / big drift** in the LAND log (native not at the host rest).
- **Carry stutter / 2fps teleport** during carry (the historical carry-land risk).
- **Crash / hang / OOM** on either peer; FPS drop.

## On GREEN
Claude commits increment 2 (explicit paths: remote_prop.cpp, trash_proxy.h/.cpp, this runbook -- NEVER
`git add -A`; the pre-existing research/ WIP is not ours), then proceeds to the RULE-2 cleanup
(retire f79bbe84 rotation-fix + the cone + convert the in-bracket join-window pile path to bind-not-destroy,
which nativizes the LAST proxy-pile source).
