# Hands-on runbook -- Path 1c: join-window pile DUP fix (in-window drop repro)

**Deployed:** `votv-coop.dll` SHA `F9B6589E1F62955F` (host + client + dev), proto **v86**.
**HEAD:** `124fbc9d` (1c `4c286cae` + NaN `e876aa09` + cues `c7b6581e` + **load-tail TIMING fix `124fbc9d`**, on `b2b3ad46` extraction). Push HELD.
**Test flag:** `pile_delta_probe=1` is set in both deployed inis -> the in-game JOIN-WINDOW cues + the
`[PILE-DELTA]`/`[PILE-CENSUS]` probe logs are ON.

> **TAKE 2 (after the load-tail fix).** Take 1 duped (client saw 8). The log proved the save-time KEY was
> perfect (matchKey == native@old bit-for-bit) but the twin-destroy ran at world-ready BEFORE the client's
> async native-pile load-tail finished -> the native@old wasn't loaded yet -> miss -> orphan -> dup. The fix
> RETRIES the save-time twin-destroy at the post-quiescence divergence sweep (~10 s after you join), where
> the late native IS loaded. **Consequence for the test: the dup may FLASH briefly at join, then RESOLVE to 4
> within ~10-15 s** (when the sweep fires). Judge the result AFTER it settles, not the instant the client
> spawns in.
**Status:** BUILT + deployed, log-clean compile. **NOT yet hands-on verified** -- this runbook IS the verification.

You (host) click `mp_host_game.bat`; the client clicks `mp_client_connect.bat`. Claude does not launch.
Claude greps the logs after.

---

## What changed (the fix under test)

A chipPile the HOST MOVES during the join-load window dups on the client. Root: the client builds ONE
pile from TWO channels that share no identity -- the scratch-save NATIVE @OLD + the connect-replay PROXY
@NEW -- and the 1cm twin-destroy matched the proxy's CURRENT pose, which is >1cm off for a moved pile, so
both survived = DUP.

Path 1c: the host now stamps each pile's join-snapshot with its **SAVE-TIME position** (the frozen value
both peers loaded from the same transferred save). The client twin-destroy matches the save-loaded native
against THAT, so a moved pile reconciles (native @old destroyed, proxy @new is the sole mirror). With the
fix the in-window dup must be **0**.

---

## THE in-window-drop repro (the gate)

This reproduces exactly the controlled hands-on that found the bug (2026-06-23): 2 kerfur + 4 pile, host
moves them in the join window.

1. **Host:** `mp_host_game.bat`. Fresh New Game (or a recent fresh save). Get in-world.
2. **Host:** stage **6 items on the path** by the office: **2 kerfur + 4 chipPiles** (grab trash, throw to
   make piles; spread them a bit so they're individually visible). Note where they sit.
3. **Client:** `mp_client_connect.bat`. The client now enters the **join window** (requests the host save,
   downloads ~18 MB, loads ~30-60 s). **You (host) see the window edges IN GAME as chat lines** (because
   `pile_delta_probe=1`):
   - `[1c-test] JOIN-WINDOW OPEN -- ... move/drop test piles NOW` appears the instant the joiner requests
     the save = window **opens**.
   - `[1c-test] JOIN-WINDOW CLOSED -- ... post-load` appears when the joiner hits world-ready = window
     **closes**. (Do NOT use "<nick> joined the game" as the edge -- it fires +5 s LATER and is ambiguous.)
4. **Host -- BETWEEN the two on-screen chat lines (after OPEN, before CLOSED):** drag/throw all **4
   chipPiles onto the asphalt** (well away from the path, >1 m). Move the 2 kerfur too (control). Drop
   EARLY -- right after the OPEN line -- so you're comfortably inside the window, not racing the CLOSED line.
5. **Client:** once loaded, look at BOTH the path AND the asphalt.

### PASS / FAIL (what the client must see -- AFTER the ~10-15 s sweep settles)
- **PASS:** **4 chipPiles total** -- all on the **asphalt** (the host's new position). The path has **no
  leftover piles**. A brief flash of 8 right at join that RESOLVES to 4 within ~15 s is the PASS path (the
  sweep removed the late natives@old). 2 kerfur (wherever they ended). No doubled piles after it settles.
- **FAIL:** **8 chipPiles PERSIST** past ~20 s -- 4 on the path (old/save) AND 4 on the asphalt (new). The
  sweep didn't resolve it -> grep `[PILE-1C] sweep-reconcile` (did it run? how many removed? did the valve
  abort?).
- Kerfur is the control: it never duped before and must not now (single channel).

---

## Secondary check -- host-DELETED-in-window (separate case)

Path 1c fixes the MOVE case. A pile the host **collects/deletes** in the window is a different path (no
proxy at all -> the client native is an orphan with no twin to match). That case is meant to be cleaned by
the existing divergence sweep (doom-removes un-mirrored natives), NOT by this fix. So ALSO test it:

6. On a second join (host re-stage 4 piles, client reconnect), **DELETE 2 piles** in the window (walk up,
   grab + put in a bin / let them despawn) instead of moving them.
7. **Client:** the 2 deleted piles must NOT appear as orphans on the path. If they DO linger, that's a
   separate (sweep) gap -- report it; it is a fast-follow, not a 1c regression.

---

## Confirming probe (optional, settles the float-exactness residual)

To confirm the save-time position the host recorded == the position the client loaded (the Edge-1
bit-exactness assumption), set `pile_delta_probe=1` in `votv-coop.ini [dev]` on BOTH peers before the run.
With it on, the `[PILE-DELTA]` / `[PILE-CENSUS]` lines log nearest-native deltas. A clean in-window MOVE
run should show the matched twins at ~0.0 cm (save-time key vs loaded native) and **0 leftover orphans**
after the sweep. (Flag lives in the ini, not the bat -- the established probe pattern.)

---

## What Claude greps after (host log + client log)

Host log:
- `save_transfer: slot N -- captured K keyed-prop keys + P pile save-time xforms at blob instant` --
  confirms the save-time map populated (P should be ~the live pile count, e.g. 4+ for the staged piles).
- `[PILE-1C] slot N world-ready -- JOIN-WINDOW CLOSED` -- the window-close marker (the moves must precede it).

Client log:
- `pile_reconcile: pile-bind index built -- M local chipPile candidate(s)` -- the join twin index.
- `[PILE] DESTROY native level-pile twin eid=E at (X,Y,Z) ...` -- a twin destroyed AT WORLD-READY (the
  in-time piles). The MOVED piles will MISS here (their native@old hasn't loaded yet) -> that's expected.
- **`[PILE-1C] sweep-reconcile -- N of M pending save-time twin(s) removed`** -- THE NEW FIX. At the post-
  quiescence sweep (~10 s after join), the late-loaded native@old gets destroyed. **N should equal the
  moved-pile count (4).** If `M>0` but `N=0`, or `sweep-reconcile ABORTED ... >50%`, that's the failure to
  chase.
- `[PILE-CENSUS] ... live orphan native(s)` -- with the fix, **0** (or down from 4 to 0) -- it runs AFTER
  the sweep-reconcile, so it reflects the removals.

---

## Honest scope
- 1c fixes the host-MOVED-in-window dup (the proven repro). Unmoved piles: unchanged. New (post-save)
  piles in window: no save-native twin -> no dup, unchanged. Deleted-in-window: relies on the existing
  sweep (secondary check above), NOT this fix.
- An UNSEEDED-at-save pile (no host eid at the scratch-save instant) is skipped by the map -> falls back to
  the live-pose match; if such a pile is ALSO moved in-window it could still dup. Piles are seeded at the
  host's session start, so this is rare; flag if seen.
