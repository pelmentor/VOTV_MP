# Hands-on runbook — join-window MASS-MOVE pile dup (2026-07-01, TAKE 3 — the OWNER)

**Deployed DLL** `votv-coop.dll` sha256 `44937AC54FC0E2CD` (all 4 folders hash-verified). Built Release.
Host + client both changed (deploy everywhere). **No protocol bump** — reuses the existing PropSnapPos wire.

## Why take 3 (the architectural pivot)
Takes 1-4 (`fa8bc344`, `76257bb0`, `46e35edd`, `110b1bde`) each fixed a slice and left a new-named tail. Two
forensic traces proved this is INSTANCES of one class, not the owner. Root (18:17 trace): a host in-window pile
move keeps the SAME eid (same-eid morph), but the client identifies keyless piles by their FROZEN save-pos —
which LIES after a move. When E's @new mirror pointer GC-churns, `eidFree(E)` goes true and RE-BIND-by-position
uses E's stale save-pos to "re-create" E — grabbing the leftover @old copy and binding it to E (a correct bind
of the wrong actor). Once bound, every unbound-only retire is blind to it. And the host's position flush was a
ONE-SHOT, so a pile moved late in the join tail (e.g. eid 5271) got NO signal at all.

## The owner (docs/piles/12)
`PropSnapPos(E,@new)` is the host's authoritative "E is now at @new" — the client now USES it for IDENTITY, not
just to nudge the actor, and the host DELIVERS it for every move:
- **Host:** `FlushDivergedPilePositionsForSlot` is now LATE-ARMED — re-runs at 2 Hz for 25 s past each joiner's
  world-ready (deduped per-eid to actual position changes), so a pile moved any time in the join tail is
  delivered. (`save_transfer.cpp` `TickPileFlushLateArm`, called from host `TickHost`.)
- **Client:** on `PropSnapPos(E,@new)`: (1) retrack our save-time identity key to @new (`UpdateChipSavePosAndGetOld`)
  so RE-BIND-by-position searches @new and NEVER resurrects the @old copy; (2) if E genuinely moved (>50cm), arm
  a HOST-VACATE twin — the sweep retires whatever save-loaded native@old lingers, on the host's word (no fragile
  position-confirm guess, no >50% cap; those guesses are what GC pointer-reuse corrupted).

## Test (2 peers; you run hands-on, Claude does NOT launch)

### T1 — host clears a cluster DURING the join window (the repro)
1. Host: load a save with a CLUSTER of chip piles in one corner. Start hosting.
2. Client: begin connecting. **While the client is still joining**, the host grabs + throws MANY of the
   cluster's piles away — including some **late** in the join (keep clearing as the client finishes loading).
3. Client: finish joining; look at the original corner AND the new spots.
4. **Expect:** the original corner is EMPTY; each moved pile appears ONCE at its @new spot. No leftover @old,
   including for piles the host moved late in the window (take 2's residual).

### T2 — the previously-stuck 2 corner piles
1. Look specifically for the 1-2 piles that lingered at 18:17.
2. **Expect:** gone. If any remain, note whether it's bound (has a GUI prompt / host sees it snap on pickup) —
   that would be a residual for the next probe.

### T3 — air-hang on pickup (SEPARATE track — just report)
1. If you pick up a pile and it hangs in the air, report it.
2. **Expect:** likely resolved as a downstream effect of the dup fix (the host can now author the pile's identity);
   if it persists it's a separate carry/pose track.

### T4 — no regression on a CLEAN join
1. Client joins while the host does NOT touch the cluster.
2. **Expect:** the cluster renders once, correctly. The host-vacate twin only arms when the host actually moves a
   pile >50cm, so an untouched pile is never retired.

## Read in the logs
- HOST: `[PILE-B3] HOST slot N pos-correction eid=... (late-arm in-window move -> deliver the authoritative
  position)` — a pile moved AFTER the one-shot now gets delivered (take 2 gap closed).
- CLIENT: `save_identity_bind: identity key UPDATED eid=... @old -> @new (host PropSnapPos authoritative; RE-BIND
  now tracks @new)` then `[PILE-B3] CLIENT armed HOST-VACATE twin eid=...` then `[PILE-1C] sweep-reconcile ...
  confirmed-moved retired` covering those eids.
- Should NOT see `RE-BIND chipPile by position ... @save-pos=(<an old corner pos>)` re-binding a stale @old
  AFTER its identity key was updated to @new.

## Honest status
Built + deployed + hash-verified 4/4; NOT hands-on (user present -> user runs the repro). No autonomous smoke
(user on PC). A perf/correctness audit of the change is running. Note: `save_transfer.cpp` reached 808 LOC (8
over the 800 soft cap) — the b3 flush cluster is flagged for extraction to its own file as a follow-up.
