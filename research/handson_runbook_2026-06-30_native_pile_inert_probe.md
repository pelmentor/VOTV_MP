# Hands-on runbook -- native-pile inertness probe (GO/NO-GO gate) -- 2026-06-30

## RUN 2 -- COLLISION-ON (the actual nativization recipe)
**Deployed:** `votv-coop.dll` **B99F49F4** (hash-verified all 4 folders). Build GREEN.
**Flag still armed:** `native_pile_inert_probe=1`.

RUN 1 (NoCollision) already passed: 39s flat-inert, no self-destruct/self-morph (4x past the original
~10s unrooted death window). RUN 2 flips to the REAL recipe -- **COLLISION-ON** -- because "the pile has
no contact-convert gate so collision-ON should hold" is an unverified assumption on the same axis as the
resolved confound. The probe now spawns the native **~2.5 m IN FRONT of you** with native collision, so
it also previews the payoff.

**Steps:** New Game (host or standalone), spawn in, stand ~70s. A pile appears ~2.5 m ahead.
- **Watch the log** for `[INERT-PROBE] VERDICT GO ... stayed INERT (live + pile) for 60s` (collision-ON
  contact path is inert -> proceed with the wide nativization), or a `NO-GO` (self-destruct/self-morph ->
  collision-ON triggers autonomy, rethink).
- **Bonus live preview** (tells us the payoff is real): **aim at the pile** -- does the native hover GUI
  appear? **walk into it** -- does it block you (collision)? Note what you see.

(If you let RUN 1 end at 39s, that's fine -- this run is the decisive one; let it reach the 60s VERDICT.)

---

## RUN 1 (superseded) -- NoCollision
**Was:** `votv-coop.dll` A6A5AF84. Result: 39s flat-inert (GO signal, session ended early).

## What this decides
Can we replace the bare `AStaticMeshActor` PROXY (no `int_player_C`, no collision -> no native hover GUI,
wrong rotation) with a **rooted real `actorChipPile_C` native** for resting/runtime/re-pile piles? If a
runtime-spawned, rooted, inert-forced real `chipPile` stays alive + unchanged for 60s, its live ubergraph
is inert -> nativizing gives the **hover GUI + rotation + collision + occlusion + movement-block FOR FREE**
(a real pile is `int_player_C` by construction). This probe excites the one unproven path: the live
ubergraph of a rooted runtime native.

## Steps (HOST or standalone New Game -- cleanest with no coop confound)
1. Launch the **HOST** instance (`Game_0.9.0n`) and **New Game** (fresh save), OR just New Game standalone.
   (Either is fine -- the probe runs on whatever instance has the flag once the world + player exist.)
2. Spawn into the world and just **stand there ~70 seconds**. Nothing to do -- the probe is autonomous.
3. The probe waits ~8s, spawns ONE rooted `actorChipPile_C` ~2m above you (it is NoCollision, so it just
   floats/exists -- you may see a pile appear; that's expected), and watches it for 60s.

## Read the result
Grep the host log for `[INERT-PROBE]`. The decisive line is the VERDICT:
- `VERDICT GO: ... stayed INERT (live + pile) for 60s` -> **GO**: nativize. We retire the bare proxy +
  the rotation-fix + the hover-GUI under RULE 2; native GUI/rotation/collision come free.
- `VERDICT NO-GO @t=Ns: ... went NOT-LIVE despite AddToRoot` -> the BP **self-destructs** (autonomy, not
  GC). Bare proxy is load-bearing.
- `VERDICT NO-GO @t=Ns: ... SELF-MORPHED to '<class>'` -> the BP **self-morphs**. Bare proxy is load-bearing.

NO-GO -> we push the held rotation-fix (`f79bbe84`) + finish the hover-GUI subsystem for the residual proxy.

## Cleanup after
Set `native_pile_inert_probe=0` in the ini (the probe self-cleans its actor; this just disarms re-runs).
