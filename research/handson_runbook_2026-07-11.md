# Hands-on runbook — 2026-07-11 (take 1) — DLL `8CC08A89` (all 4 installs, hash-verified)

HEAD `fe6178d6` (10 commits today, local main, NOT pushed). Build clean; deploy hash-verified.
**NOT LAN-smoked** (user was at the PC all day — user-tests rule); the user's live session earlier
today ran DLL `C9E50E92` (same code minus the HandPose stream, the setKey fixes, and the hp-0.0
silence) and is the evidence base for the [USER-V] tags below.

## Verified LIVE today already (on C9E50E92 — no re-test needed unless regressions appear)
- Health/decal overlay works (user drove the damage findings WITH it). Yellow action lines,
  save-picker instant open, chat-menu-leak, email-join-gate: shipped in that DLL; user reported
  no recurrence — treat as [AS-BUILT, live-run silent] not [V] until explicitly confirmed.

## NEW in 8CC08A89 — the things to test
1. **Swing motion (v109 HandPose stream)**: host R-HOLDs a rock/keljoy, LMB-swings while the client
   watches the puppet — the item must move CONTINUOUSLY through the swing (was: 1 fps lurch).
   Also check idle hold still sits correctly (measured transform, face direction included).
2. **Crowbar dupe (setKey base-resolve)**: repeat the exact repro — host Q-spawns a crowbar,
   R-places it; client hold-R picks it up, walks, R-places it. EXPECT: exactly ONE crowbar
   everywhere at every step. Log check: NO "setKey UFunction not found" lines on either peer
   (grep both votv-coop.logs); client destroy broadcasts must carry the SAME key the spawn used.
3. **Overlay**: plain props no longer show "hp 0.0"; breakables/creatures/decals still show pools.

## KNOWN-OPEN (do not re-report; lanes designed/queued)
- CLIENT melee damage = local-only (no cross-peer damage; silent crowbar door hits; door hit-open
  nudge unsynced). RE DONE: research/findings/votv-melee-damage-path-RE-2026-07-11.md — the
  damage-intent lane is the next build (design pass first; hook candidates ranked there).
- Container contents divergence (take-out ghosts / put-in losses / per-peer loot):
  docs/items/container.md §3 DESIGN ready — Inc-1 build queued.

## Log reads after the session
- Both peers: `[Error]`/`[Warn]` diff vs the known transient connect-burst set; any
  "setKey UFunction not found"; any "feed RESURRECT"; hand_item announce lines ("rel measured").
