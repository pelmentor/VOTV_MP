# Hands-on runbook -- nativization INCREMENT 1 (spawn-seam) NO-REGRESSION baseline -- 2026-06-30

**Deployed:** `votv-coop.dll` **75CB1762** (hash-verified all 4 folders). Build GREEN. Inert probe DISARMED.

## Purpose
Bank the FOUNDATION green BEFORE touching the delicate carry-land re-pile path (increment 2). This deploy
should behave **identically to normal pile play** -- increment 1 only nativizes the uncommon steady-state
host-only pile spawn; the common paths (save-loaded piles, grab/throw/carry, re-pile) are unchanged. The
point is clean attribution: if increment 2 later breaks carry-land, we know it's the re-pile swap, not the
foundation.

## What increment 1 changed (the delta under test)
- `native_pile_mirror::Materialize` -- new: a rooted real chipPile native for steady-state host-only PILE
  spawns (replaces the bare proxy there). Clump + in-bracket piles still proxy.
- Leak fix: `RemoveFromRoot` before destroy (so a rooted native doesn't leak) -- a no-op on the existing
  unrooted save-loaded natives / proxies.
- `pile_hover_gui` REMOVED (native GUI is the real fix; the subsystem was never relied on).

## No-regression test (normal pile play across a join)
1. Host a save with chipPiles; client joins. Client sees the placed piles (save-loaded -> native bind, as
   before). **Check: no duplicate piles, no missing piles, no crash on join.**
2. Client aims at a pile + presses E (grab). **Check: it grabs host-auth (clump in hand), NO second pile
   left behind (no dup).**
3. Carry + throw. The clump flies + lands + re-piles. **Check: the re-piled pile appears, NO dup, the carry
   is smooth (no 2fps teleport), no crash.**
4. Repeat grab/throw 3-4x on a couple of piles. **Check: no accumulating duplicates, no crash, FPS stable.**
5. (If easy) host moves/deletes a pile -> client mirrors the move/delete. **Check: no orphan, no crash.**

## Green = baseline banked
"Green" = pile mechanics behave exactly as the prior shipping build -- grab/throw/carry/re-pile/destroy all
work, no dup, no crash, no missing piles, FPS stable. (A re-piled pile having NO hover GUI is EXPECTED here
-- it's still a proxy; increment 2 makes it native.) On green, I commit increment 1 as the foundation, then
build increment 2 (re-pile -> native) as an isolated delta on top.

## NOT a pass
Any: a duplicate pile after grab/throw, a missing pile, a crash, a carry hitch/teleport regression, FPS
drop. Report the symptom + paste the host+client `[PILE]` log lines around it -- that attributes it to the
foundation cleanly, before the carry-land swap is ever touched.
