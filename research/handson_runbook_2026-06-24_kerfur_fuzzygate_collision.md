# Hands-on runbook — kerfur fuzzy-gate (anti-collision fix#1) + OBS-2 validation (one run does both)

**Deployed:** MD5 `2E250812959F23669E9AC4EED665176D` (short `2E250812`) on HOST + CLIENT + CLIENT2 + DEV.
Build clean (no warnings on touched files). Proto unchanged. **Code HELD uncommitted** (commit after this
hands-on). Supersedes the 14:05 build `75A92E57`. NOT pushed.

## What changed (this build = fuzzy-gate + the prior OBS-2 arg-fix/probe carried forward)
1. **Anti-collision fuzzy-gate (fix#1, 2 parts):**
   - `kerfur_prop_adoption.cpp` ResolvePending class+pose (500cm) match: SKIP any candidate whose own
     Aprop_Key is real (non-empty/non-None) AND != the pending kerfur's broadcast key -> it exact-BELONGS
     to a different host kerfur, never steal it. Ordering-independent (gates on identity, not bind state).
   - `remote_prop_spawn.cpp` OnSpawn Gap-I-1 (30cm) fuzzy: same skip, **kerfur-gated only** (mushroom/
     garbage spawners keep their divergent-key dedup unchanged).
2. **OBS-2 arg-slot fix + diagnostic probe** (from build `75A92E57`, still in): the deferred fresh-spawn
   fallback passes `deferKerfur` in the correct slot, and logs `[OBS-2 PROBE] TWIN PRESENT/ABSENT`.

## Why one run validates BOTH (the key interaction)
The 14:05 collision: xXPHX (a runtime-turned-off kerfur with a FRESH key, no own local off-twin) class+pose
fuzzy-grabbed Nrby's actor (206cm) -> two host eids (3147 + 4346) on ONE client actor -> 4 visible.
With the fuzzy-gate, xXPHX now SKIPS every keyed neighbor (their keys != xXPHX's key) -> no class+pose
match -> it **falls through to the fresh-spawn fallback** -- which is exactly where the OBS-2 arg-fix + probe
live. So this run drives xXPHX into the OBS-2 path that 14:05 never reached. Expect the probe to FIRE this
time.

## Setup
Same scenario as 14:05: host turns OFF a kerfur in the join window (active-at-blob -> off), 5 off-objects +
1 active on the host. Client connects. (User launches; Claude does not.) No ini flag.

## What to read in the CLIENT log (grep targets)
1. **Collision GONE (the fix#1 acceptance):** the deferred kerfur's eid and any save-off neighbor must bind to
   **DISTINCT actors**. Grep `RegisterPropMirror.*prop_kerfurOmega` and check the `actor=` addresses are all
   unique -- NO two kerfur eids sharing one actor (14:05 had eid 3147 + 4346 both on `…BBC91C0`).
   - Must NOT see: `bound LOCAL save kerfur prop ... eid=<deferred>` landing on a neighbor's actor.
   - May see (the gate acting): the deferred kerfur finds no class+pose match -> goes to fresh-spawn.
2. **OBS-2 probe fires (now reachable):** `[OBS-2 PROBE] eid=<deferred>: TWIN ABSENT ...` (correct -- the
   window-turned-off kerfur has no own local off-twin) -> then `kerfur-prop-adopt: ... fresh-spawning a mirror`
   -> `remote_prop::OnSpawn: spawned <ptr> of 'prop_kerfurOmega_C'` -> `RegisterPropMirror eid=<deferred>` to a
   NEW distinct actor. (If the probe says TWIN PRESENT instead, capture it -- means a same-key twin exists and
   the gate let it through, also fine.)
3. **Anti-collision skip log (if the Gap-I-1 path was hit):** `kerfur fuzzy match ... DIFFERENT key ... not
   stealing it, fresh-spawning instead`.

## Acceptance
- **PRIMARY (fix#1 -- must pass): the 5-vs-4 COLLISION is GONE.** No two kerfur eids share one client actor;
  the save-off neighbor (e.g. Nrby) keeps its OWN actor. This is the core success: the stolen-actor collision
  is eliminated.
- **BONUS (OBS-2 validated): the probe fires + the arg-fixed fresh-spawn registers a DISTINCT mirror** for the
  deferred kerfur -> it gets a fresh off-prop body -> **client shows 5 off-objects** (full fix of 14:05).
- **Acceptable partial (still a fix#1 success): client shows 4 off-objects + the deferred kerfur cleanly
  ABSENT (no body), NOT a collision.** If the body doesn't appear, that is the clean "symptom 2 -- no body
  yet" state (fix#2 active-at-blob->off gives the body). This is NOT a regression -- the collision (stolen
  actor) is what fix#1 had to kill, and it's gone.
- **No DUP:** the deferred kerfur must NOT render as BOTH a fresh off-prop AND a surviving active NPC (client
  != 6). The reverse ghost sweep should destroy the active twin (it did at 14:05:54). If a dup appears, capture
  it -- it's an active-NPC-cleanup-vs-fresh-spawn timing point for fix#2.
- **No regression:** the 4 save-off neighbors still exact-key bind to their own actors; reverse fix holds
  (ghost sweep destroys the active twin); mushroom/garbage props unaffected (the Gap-I-1 gate is kerfur-only).

## After the run
Point me at the client log (the `RegisterPropMirror.*kerfurOmega` actor addresses + any `[OBS-2 PROBE]` +
collision check). I read it:
- Collision gone + 5 objects -> fix#1 AND OBS-2 both validated -> commit both (remove the probe; arg-fix +
  fuzzy-gate stay).
- Collision gone + 4 + clean absence -> fix#1 validated, OBS-2 still pending a body -> commit fuzzy-gate, open
  fix#2 (active-at-blob->off body) design.
- Collision still present -> the gate missed a path; re-diagnose before commit.
