# Autonomous PATH B — deterministic floor proof on the timing fix (2026-06-25)

**STATUS: PREPARED, NOT RUN. Waits for the user's "отошёл / go" signal (user is in a live session;
`mp.py kill` would kill his game + overwrite the host log). DO NOTHING on the peers until then.**

Built binary to deploy when go: **`votv-coop.dll` MD5 `BCDD46DA`** (HEAD `5e91519a`, timing fix + floor
+ dev forcing/toggle). Currently deployed on peers: `A47CA882` (stays until the run).

## What this proves
With the timing fix (commit `5e91519a`) the claim sweep now WAITS for the world-reload purge to drain +
re-seed before adjudicating, so the natives are LIVE + registered at sweep time deterministically. Then the
host-skip forcing flag leaves them UNCLAIMED -> a controlled, repeatable over-destroy condition. One binary,
one variable changed (floor on/off) = a clean controlled experiment:

| Run | HOST ini `[dev]` | CLIENT ini `[dev]` | Expected client log | Proves |
|---|---|---|---|---|
| **A (before)** | `force_chippile_unclaim=1` | `disable_completeness_floor=1` | sweep FIRING (load tail quiesced) → **`... unclaimed locals destroyed`** with ~870 chipPiles in the doomed histogram; piles WIPED | the catastrophe is real AND the timing fix made it deterministic (sweep fired post-reseed on live natives) |
| **B (after)** | `force_chippile_unclaim=1` | *(no floor flag)* | sweep FIRING (load tail quiesced) → **`completeness FLOOR kept ~870 unclaimed 'actorChipPile_C'`** → `claim sweep -- ... 0 unclaimed locals destroyed`; piles SURVIVE | the floor is the net |

HOST keeps `force_chippile_unclaim=1` for BOTH runs (it's the injection); only the CLIENT floor toggle
changes between A and B.

## Topology (from tools/mp.py:49-51)
- HOST peer = `Game_0.9.0n\WindowsNoEditor\VotV\Binaries\Win64\votv-coop.ini`
- CLIENT peer = `Game_0.9.0n_copy\WindowsNoEditor\VotV\Binaries\Win64\votv-coop.ini`
- The smoke launches both via the named-window launchers; `mp.py kill` overwrites BOTH logs -> live-session-unsafe.

## INI rules (CRITICAL — IsIniKeyTrue is exact-match)
`IsIniKeyTrue` reads ONLY `votv-coop.ini` in the DLL dir and matches an EXACT `key=1` line after
whitespace-normalize. An inline `; comment` on the same line BREAKS the match. So each flag goes ALONE on
its own line under a `[dev]` section. The flags are `static const` (latched once per session) -> they MUST
be in the ini BEFORE the peer launches.

## EXACT STEPS (execute only on "go")

### 0. Deploy the timing-fix binary to the test peers
```
pwsh tools/deploy-all.ps1          # pushes build/votv-coop/Release/votv-coop.dll (BCDD46DA) to all 4
```
Verify all 4 targets report the BCDD46DA bytes (idempotent skip-if-identical is fine; an error is not).

### 1. RUN A (before — floor OFF -> expect WIPE)
- HOST `Game_0.9.0n\...\votv-coop.ini`, under `[dev]`: bare line `force_chippile_unclaim=1` (the injection).
- CLIENT `Game_0.9.0n_copy\...\votv-coop.ini`, under `[dev]`: bare line `disable_completeness_floor=1`
  (the floor toggle — client side only). The client does NOT get `force_chippile_unclaim` (that is a
  host-side flag; the sweep + floor live on the client).
- Run: `python tools/mp.py smoke --duration 200`
  (200 s: the sweep now fires ~60-70 s after connect — it waits the ~50 s purge drain + ~2 s quiesce — so
  the run must outlast that; a 30 s smoke would end before the sweep.)
- Grep the CLIENT log (`Game_0.9.0n_copy\...\votv-coop.log`):
  - `force_overdestroy_test: ARMED` (host) + `completeness FLOOR DISABLED` (client) — flags took.
  - `divergence sweep FIRING (load tail quiesced` — the gate WAITED for the real reload (NOT
    "ABSOLUTE ceiling" / "no-progress deadline" — those would mean it fired on a backstop, inconclusive).
  - `claim sweep -- N in-universe ... M unclaimed locals destroyed` with N in the HUNDREDS and ~870 in the
    doomed-class histogram = WIPE. This is the BEFORE.

### 2. RUN B (after — floor ON -> expect KEEP)
- CLIENT ini: REMOVE the `disable_completeness_floor=1` line (HOST keeps `force_chippile_unclaim=1`).
- Run: `python tools/mp.py smoke --duration 200`
- Grep the CLIENT log:
  - `force_overdestroy_test: ARMED` (host); NO `FLOOR DISABLED` (client floor live).
  - `divergence sweep FIRING (load tail quiesced` — same gate behavior.
  - `completeness FLOOR kept ~870 unclaimed 'actorChipPile_C' -- host census 870, claimed only 0` +
    `completeness floor KEPT ~870 of ~870 doomed` + `claim sweep -- ... 0 unclaimed locals destroyed` = KEEP.
    Piles survive. This is the AFTER.

### 3. CLEANUP (mandatory after both runs)
- Remove `force_chippile_unclaim=1` from the HOST ini.
- Remove `disable_completeness_floor=1` from the CLIENT ini (if any left).
- Confirm both inis are back to their pre-test state (no `[dev]` test flags).
- `mp.py kill` any lingering windows.

## Acceptance (Phase 0 VERIFIED requires ALL)
1. Run A shows the WIPE (floor off) — proves the catastrophe is real + now deterministic.
2. Run B shows `completeness FLOOR kept` + piles survive (floor on) — proves the floor.
3. BOTH show `sweep FIRING (load tail quiesced)` — proves the TIMING fix waited for the real reload (the
   sweep adjudicated the fully-reloaded world, not a mid-purge trough or a deadline backstop).
Until all three hold on a real run, Phase 0 stays UNVERIFIED (built + audited only).

## Notes
- pile-09 is NOT in BCDD46DA's tree? CHECK: HEAD `5e91519a` is on top of `8d44fe33` which HAS pile-09 (v89).
  So BCDD46DA INCLUDES pile-09. That is FINE for this proof (pile-09 is the move-dup self-seed; it does not
  touch the sweep timing or the floor). If a CLEAN-attribution floor+timing binary (no pile-09) is wanted
  later, rebuild on a pile-09-reverted tree. For the floor/timing proof, pile-09 presence is irrelevant.
- Push HELD throughout. Deploy BCDD46DA only as step 0 of the run, never before "go".
