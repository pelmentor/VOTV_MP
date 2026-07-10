# Hands-on runbook — 2026-07-10 eve: v108 (owner-effect flips + roach + eyer) — take 3

**Supersedes take 2 (`handson_runbook_2026-07-10_fixpass.md`)** — every still-pending item from it
is carried below; do NOT run take 2 separately.

**Deployed:** DLL `D9F35C9A` (hash-verified all 4 installs), proto **108**, HEAD `2221b5e5`.
Adds over take 2's `92C7FC96`: the anchor-audit lanes `08560687` (pinecone owner-mirror, sky-wisp
host-mirror, roach lane) + the OWNER-ENTITY eyer lane + F1 reorg `dbaf5099` + Content>Props
`2221b5e5`. Post-deploy smoke x4 PASS across the arc (0 ERROR both peers; 8/8 t3 cancels,
23/23 npc allowlist, roach parks + owner_entity armed on both peers).
**Probe note:** `[dev] rng_roll_census=1` stays ON in HOST + CLIENT_1 (organic census
accumulation); `[dev] vitals_keepalive_sec=180` stays in HOST.

## NEW in this build (v108 arc)

1. **F1 layout (everyone)**: F1 now reads Player / World / Content / Network / Administration /
   Cosmetics. "Game" is gone: Weather/Clock/Economy live under World; **Content** holds
   Entities (creature test spawns) / Props (the Q spawn-menu activator) / Events (the board).
   Non-dev peers still see only World>Rules, Network>Stats, Cosmetics. EXPECT: nothing missing,
   nothing new visible to non-dev.
2. **Eyer owner-entity lane (the headline)**: HOST presses F1 > Content > Entities > "Spawn Eyer
   (in front)". EXPECT: host gets a REAL eyer (it watches YOU, angers if you stare, can dash —
   native behavior); the CLIENT sees the same glowing eyes at the same spot (a harmless mirror:
   no anger, no dash, walk through it — collision off). Log (host):
   `owner_entity: OWN 'eyer_C' spawned locally seq=1 ... -- announced`; (client):
   `owner_entity: mirror 'eyer_C' materialized for slot=0 seq=1 ... (brain-parked, collision-off)`.
   When the host's eyer despawns/dies, the client's mirror vanishes within ~250 ms.
   ALSO WANTED (the F-2 verify gate): a NATURAL night eyer roll (or wait one out — 30-45 min
   cycles, 5% night rolls) producing the same `OWN 'eyer_C' spawned locally` line — the F1
   button cannot prove the native roll path (it re-enters ProcessEvent by construction).
3. **Roaches (shared infestation)**: find/force roaches (they build near food left out; the
   census shows the master lives near the base). EXPECT: both peers see the SAME roaches
   (positions update ~1/s on the client — slight steppiness is the known Inc-1 cadence); a
   client EATING or STOMPING one removes it for everyone within ~1-2 s. Log (client):
   `roach_sync: local roach consumed at (...) -> intent to host`; (host):
   `roach_sync: slot N consumed roach rawIdx=...`. Known gap: the dead-husk prop from a stomp
   shows only on the peer who stomped.
4. **Pinecone forest drops (owner-effect)**: as CLIENT, walk the forest until pinecones/sticks
   drop around you. EXPECT: the HOST (standing near you) sees the same drops fall and can grab
   them. Log (client): `host_spawn_watcher: MIRROR ambient spawn cls='prop_food_pinecone_C'...`
   — the line now fires on the CLIENT too (it was host-only before).
5. **Sky wisps identical**: when a color wisp appears high in the sky (rare — 20-60 min rolls),
   both peers should see the SAME wisp. (Was: each peer rolled its own.)

## CARRIED from take 2 (still pending hands-on, DLL differences don't affect them)

6. **Hand item camera-front** (`be98beb6`): hold-R pick up an item; the OTHER peer sees it
   floating in FRONT of your puppet's face (not at the shoulder). Retunable (3 constants).
7. **F2 rock**: CLIENT hold-R pickup -> walk -> hold-R place. Host sees vanish+appear, no dupe,
   no mid-air rock. Log: `[PROP-DROP] CLIENT authored drop intent` -> `HOST spawned placed prop`.
8. **serverbox mirror**: on a real/forced server break the client shows the same box down; no
   false "server down". (Disconnect-restore already real-log-verified.)
9. **email host-only**: story emails match the host's inbox on both peers; zero client-authored.
10. **peer-action chat**: an email delete prints "<nick> deleted an email: <subj>" on all peers.
11. **(passive) census**: keep playing — organic client exposure accumulates the T1 census.

## Honest status
Everything here is AS-BUILT + smoke-clean ONLY (the smokes prove install/arm + zero errors, not
gameplay). Items 2-5 exercise wire paths an idle smoke never fires — this runbook IS their first
real exercise. Item 2's NATURAL-roll half is the tracker's F-2 VERIFY gate (the button alone
cannot flip the eyers row to VERIFIED).
