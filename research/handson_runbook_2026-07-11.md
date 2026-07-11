# Hands-on runbook — 2026-07-11 (take 2) — DLL `7625828E` (all 4 installs, hash-verified)

HEAD = 11 local commits + the spawn-defer/steal-gate commit pending (local main, NOT pushed).
Build clean; deploy hash-verified. **NOT LAN-smoked** (user at the PC — user-tests rule).

## [USER-V] confirmed live on 8CC08A89 (take-1 items — CLOSED 2026-07-11 ~12:20 session)
- Swing motion continuous (v109 HandPose stream) — USER-V.
- Crowbar dupe gone (setKey base-resolve x2) — USER-V.
- Overlay hp-0.0 silence — USER-V.
- (From C9E50E92 live run: health/decal overlay USER-V; yellow actions / save-picker /
  chat-leak / email-gate = AS-BUILT, live-run silent.)

## NEW in B65946B2 — the things to test
1. **Join-window placed props (SPAWN-BEFORE-QUIESCENCE defer)**: repeat the exact repro — while the
   client is connecting (connection window / loading), host R-hold places 2+ items (rocks/cblocks)
   from the hotbar on the ground. EXPECT: once the client finishes loading (~at the join settle),
   the items APPEAR on the client, and a host E-grab moves them on the client's screen.
   Log check (client): "[SPAWN-DEFER] CLIENT armed deferred spawn" x N during the load, then
   "[SPAWN-DEFER] CLIENT applying N deferred spawn(s) at the quiescence drain"; NO
   "grab_hook[destroy-seam]: CLIENT suppressed KEYED DESTROY" for those keys after the apply.
2. **Same-spot multi-placement (identity-steal gate)**: host places 2-3 SAME-type items (e.g.
   crowbars/rocks) within ~30 cm of each other while the client watches (steady state is fine).
   EXPECT: the client shows the SAME COUNT as the host; host grabbing each one moves the right one.
   Log check (client): any "fuzzy match ... is already mirror-bound to eid=... never
   position-stolen; fresh-spawning instead" line = the gate working as designed (INFO, not a bug).

## KNOWN-OPEN (do not re-report; lanes designed/queued)
- CLIENT melee damage = local-only (no cross-peer damage; silent crowbar door hits; door hit-open
  nudge unsynced). RE DONE: research/findings/votv-melee-damage-path-RE-2026-07-11.md — the
  damage-intent lane is the next build (design pass first; hook candidates ranked there).
- Container contents divergence (take-out ghosts / put-in losses / per-peer loot):
  docs/items/container.md §3 DESIGN ready — Inc-1 build queued.

## Log reads after the session
- Both peers: `[Error]`/`[Warn]` diff vs the known transient connect-burst set; any
  "setKey UFunction not found"; any "feed RESURRECT"; hand_item announce lines ("rel measured").
- Take 2: client "[SPAWN-DEFER]" arm/apply pairs (armed count == applied count); any
  "[SPAWN-DEFER] ... cap ... hit" (should never fire); "never position-stolen" INFO lines are
  the steal gate working, not an error.
