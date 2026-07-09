# Hands-on runbook — F2 Increment 1 (client places a keyed prop → host sees it)

**Deployed:** DLL `1A4C5BEF3107E50E`, proto **v106**, all 4 folders (HOST / CLIENT_1 / CLIENT_2 / CLIENT_3).
HEAD `556a91d7` (pushed origin/main). Built + agent-audited (0 CRITICAL) + LAN join smoke x2 PASS.
**Status: NOT hands-on-verified** — the smoke can't drive hold-R input, so the place→host-visible path is
unproven. This is the test.

## What was broken (your report)
"клиент кладёт камень и он не появляется у хоста" — a client picks up a rock (hold-R) and places it
(hold-R again); the placed rock is INVISIBLE on the host. (The rest — F1 host-moved-in-join-window, the
UI trio — you already tested OK.)

## The one test that matters
1. Host launches its world; client joins (2 peers).
2. On the **client**, walk to a rock (or any keyed world prop). **Hold R** to pick it up INTO HAND (the
   world rock disappears, a hand display shows). *This half already worked — the host's rock disappears too.*
3. Aim somewhere and **hold R again** to PLACE it on the ground.
4. **PASS** = the placed rock APPEARS on the **host** at the spot the client placed it, and there is **no
   dup** (exactly one rock on each peer, same place). Also check the placing client still sees exactly one
   rock (no dup on the placer).

## Second pass (dup / stability)
5. Repeat pickup→place a few times with the same rock. Each place should re-appear on the host, still no dup.
   (An audit-fixed edge — the park FIFO — only mattered after 64 same-key cycles; a handful is plenty to
   confirm the common path.)

## What I'll read in the logs afterward (your job = just play; I grep)
- **Client** log, on each place: `[PROP-DROP] CLIENT authored drop intent key='...' cls='...' loc=(...)`
- **Host** log, on each place: `[PROP-DROP] HOST spawned client-placed prop key='...' cls='...' at (...)`
  immediately followed by a `host_spawn_watcher: spawn-seam adopted ...` (the broadcast).
- If the rock is invisible on the host: look for the client `[PROP-DROP] CLIENT authored` line.
  - **Present** but host has no rock → the host spawn or broadcast failed (host-side).
  - **Absent** → the client didn't detect the place as parked (the pickup-destroy didn't cross, or the
    +1-tick Key restore took longer than 8 ticks, or `LocalHandActor`/InEpisode mis-gated).
- Dup on the host → a same-key destroy after the spawn (the husk-destroy ordering), or the dup-guard missed.

## Known limits (expected, NOT bugs to report)
- **Physics fidelity**: the host's re-spawned rock is a bare Aprop (identity + position + scale faithful);
  it may not perfectly reproduce the client's exact physics settle/roll. Position + visibility is Inc-1's bar.
- **E-grab (physics grab, not hold-R)**: NOT covered by Inc-1 — that's the E-physics recovery lane (Inc-2).
  Inc-1 is specifically the **hold-R pickup → hold-R place** path (your workflow).
- **Rapid frantic cycling**: Inc-1 targets the clean single place; rapid interleave hardening is Inc-2.

## If it works
Tell me "F2 works" (or which sub-case failed) and I'll flip the finding to VERIFIED + start Inc-2
(rapid-cycle + E-physics recovery). If it fails, the log lines above localize it in one grep.
