# Hands-on runbook — OBS-2 (missing save-kerfur OBJECT on the client) — arg-slot fix + diagnostic probe

**Deployed:** MD5 `75A92E578850C31CA200E5A7823F7B23` (short `75A92E57`) on HOST + CLIENT + CLIENT2 + DEV.
Build clean (no warnings). Proto unchanged (no wire change). **Code HELD uncommitted** until this hands-on
picks the fix shape (per the user's "absent -> commit arg-fix; present -> add widening, re-verify, then
commit"). Doc: `docs/kerfur/06-...md`. NOT pushed.

## What changed (this build)
1. **OBS-2 ROOT FIX** (`coop/kerfur_prop_adoption.cpp` ResolvePending quiescence branch): the deferred-kerfur
   fresh-spawn fallback now calls `OnSpawn(e.payload, 0, localPlayer, /*fromConvert=*/false,
   /*deferKerfur=*/false)` -- deferKerfur in the CORRECT slot. Before, `false` hit param #4 (fromConvert),
   leaving deferKerfur at its default `true` -> the "fresh-spawn" re-entered the K-6 defer, silently
   re-armed the already-pending eid, spawned nothing, and the entry was popped -> the kerfur was DROPPED.
2. **OBS-2 DIAGNOSTIC PROBE** (same branch, runs ONLY when a kerfur reaches the fresh-spawn fallback):
   before fresh-spawning it scans all untracked live kerfur-prop actors near the pending pose and logs
   `[OBS-2 PROBE]` = TWIN PRESENT (with nearest leaf class + whether its class matches the pending class)
   or TWIN ABSENT. This decides whether the arg-fix alone is complete or BindAsMirror also needs a
   class-family widening.

## Why this repro is NON-DETERMINISTIC (read first)
OBS-2 only manifests when a kerfur **loses an index-seed race** and gets DEFERRED (its OnSpawn arrives a few
lines before the key-index re-seed). Last session that hit 1 of 5 kerfurs (s1l33p). On a given connect, all 5
may instead resolve by exact-key (no defer, no missing object, fix path NOT exercised). **So do a few connect
cycles** until you see a kerfur defer (`kerfur-prop-adopt: armed deferred adoption`). A run with zero defers is
inconclusive for the fix but still confirms no regression (all 5 bind, count stays 5).

## Setup
- Same **5-kerfur save** that produced the 5-vs-4 split (5 turned-OFF kerfur OBJECTs in the world; the active
  one is fine).
- Host launches that save; client connects. (User launches both per the named-window launchers; Claude does
  NOT launch -- user is present and testing.)
- No ini flag needed -- the probe fires automatically on the deferred fresh-spawn path.

## Steps (per connect cycle)
1. Host loads the 5-kerfur save, fully settles.
2. Client connects; let the join load fully quiesce (~10-20 s after connect; watch for the prop divergence
   sweep / "load tail quiesced" lines settling).
3. **Count the kerfur OBJECTs visible on the CLIENT** (turned-off kerfur props in the world). Expect **5**.
4. If the client shows 5 with NO duplicate -> good for that cycle. If it shows 4 (still missing) or 6+ (a
   DUP) -> capture, that's the signal.
5. Repeat connect a few times to catch a cycle where a kerfur defers.

## What to read in the CLIENT log (grep targets)
- `kerfur-prop-adopt: armed deferred adoption eid=N` -> a kerfur was DEFERRED this cycle (the OBS-2-prone
  path is being exercised). If this never appears, no defer happened -> reconnect.
- For each deferred eid, at quiescence look for the THREE lines in order:
  1. `[OBS-2 PROBE] eid=N: TWIN PRESENT ...` **or** `TWIN ABSENT ...`  <- the decision line
  2. `kerfur-prop-adopt: eid=N ... no local twin ... fresh-spawning a mirror`
  3. `remote_prop::OnSpawn: spawned <ptr> of 'prop_kerfurOmega_C' at (...)` **then**
     `remote_prop::RegisterPropMirror: eid=N bound to actor=... cls='prop_kerfurOmega_C'`
- The pre-fix bug signature (must be GONE): an OnSpawn header for the kerfur followed by NOTHING (no spawn,
  no RegisterPropMirror). With the fix, the fresh-spawn must now produce the `spawned` + `RegisterPropMirror`
  pair.

## Acceptance (decides the fix shape)
- **TWIN ABSENT + client shows 5, no dup** -> arg-slot fix ALONE closes OBS-2. -> I commit the arg-fix
  (and remove the probe).
- **TWIN PRESENT (class MISMATCH) + client shows a DUP (6 kerfur objects, two co-located)** -> arg-fix alone
  trades the miss for a dup; the probe proved the twin exists with a different (skin) leaf class. -> I ADD a
  BindAsMirror class-FAMILY match (bind the twin instead of fresh-spawning), you re-verify (5, no dup), then
  I commit.
- **TWIN PRESENT (class SAME)** -> unexpected (BindAsMirror should have bound it); capture the probe line --
  it means the exact-class compare isn't the miss reason and we investigate the pose/claimed gate.
- **No regression:** the other 4 (exact-key path) still bind; count is never < 5 on the bound ones.
- **NPC path untouched:** the active kerfur (symptom-2 camera path) is unaffected -- ignore it here.

## After the run
Paste / point me at the client log (or the `[OBS-2 PROBE]` + the deferred-eid lines). I read it, pick the
shape per the acceptance table, and commit (arg-fix alone, or arg-fix + widening after a re-verify). Probe is
removed before the final commit (diagnostic only).
