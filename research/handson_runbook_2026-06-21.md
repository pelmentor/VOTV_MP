# Hands-on runbook — take-21 (2026-06-21): Increment 1 host-grab pile sync + `[PILE]` logs

**Deployed:** `votv-coop.dll` md5 `2033e7c9…` (commit `1fc67aed`), **proto v82**, deployed to all 4
folders (host / copy / copy2 / dev). **Build clean, audit-folded, NOT yet verified** — this run is to
verify it (and to road-test the new `[PILE]` logs).

## What changed (Increment 1 of docs/piles/08 — HOST-grab direction only)
- A **host** grabbing a chipPile now syncs to clients: the pile re-skins to a clump → carries → throws →
  re-piles, all on the **same eid** (no proximity matching → a dense pile **cluster can't mis-bind**).
- The old "morph" is gone. Identity is the host-minted eid end-to-end.
- **NOT in this increment:** a **client** grabbing a pile (that's Increment 2 / v83 — still unbuilt).
  So test **host grabs** this round. A client grab won't sync yet — expected.

## The single thing this run must confirm — the LINCHPIN
The whole host-grab path hinges on the engine passing the source pile as `WorldContextObject` when the BP
spawns the clump. **If it holds**, a host E-press on a pile prints (host log):
`[PILE] HOST GRAB DETECTED eid=… src=…@(x,y,z) -> spawning clump (WorldContextObject->source linchpin OK)`
**If that line never appears on a host grab → the linchpin is false** → tell me; I escalate to IDA on the
real `BeginDeferredActorSpawnFromClass` `WorldContextObject` arg.

## Steps (you drive — I do not launch)
1. Launch host: `mp_host_game.bat`. **New Game** (fresh save — never an old slot).
2. Launch client: `mp_client_connect.bat`. Wait for the join to settle (world loaded, no churn).
3. Walk the **host** to a **cluster** of chipPiles (4+ piles close together — the worst case for the old bug).
4. **Host: aim at one pile, press E** (grab) → walk a step → **throw** (LMB) so the clump re-piles a bit away.
5. Repeat once or twice on **different** piles in the same cluster (to prove no neighbour mis-bind).
6. Watch the **client**: the grabbed pile should vanish→become a carried clump→fly→settle as a pile, matching
   the host, and the **other** piles in the cluster stay put.

## Reading the logs — grep `[PILE]` in BOTH logs; it tells the whole story
One channel, role-tagged. Host log + client log side by side:

| Where | Line | Means |
|---|---|---|
| HOST | `[PILE] HOST E-PRESS on pile … localEid=N … [TRACKED -> grab will sync]` | you aimed+pressed E; the pile is eid-tracked (good). `[UNTRACKED …]` = a tracking gap, grab won't sync. |
| HOST | `[PILE] HOST GRAB DETECTED eid=N src=…@(x,y,z) -> spawning clump …linchpin OK` | **THE linchpin line.** the grab was caught; which pile (eid+pos) vanished. |
| HOST | `[PILE] HOST GRAB(pile->clump) eid=N ctx=1 … broadcast PropConvert` | the convert went out to clients. |
| HOST | `[PILE] HOST CARRY eid=N clump in hand -> streaming carry pose` | the clump is being carried; pose streaming. |
| HOST | `[PILE] HOST THROW eid=N -> FLYING (… |v|=… cm/s)` | you threw it. |
| HOST | `[PILE] HOST LAND DETECTED eid=N … -> spawning pile …` then `[PILE] HOST LAND(clump->pile) eid=N ctx=3 …` | it re-piled; the final pile position broadcast. |
| CLIENT | `[PILE] CLIENT recv convert GRAB(pile->clump) eid=N … -> mirror FOUND, re-skinned to CLUMP … [SYNC-MIRROR OK]` | **the client mirrored the grab.** `NOT-FOUND [WARN …]` = the client had no mirror of that pile (desync before the grab). |
| CLIENT | `[PILE] CLIENT applied THROW eid=N -> clump mirror physics on …` | the client's clump flew. |
| CLIENT | `[PILE] CLIENT recv convert LAND(clump->pile) eid=N … [SYNC-MIRROR OK]` | the client re-piled to match. |
| EITHER | `[PILE] CLIENT DROP stale convert/carry/release eid=N …` | a late/out-of-order packet was correctly dropped (good — the cluster/stale-pose guard working). |
| EITHER | `[PILE] … destroy eid=N -> mirror removed (vanished here too)` | a pile/clump was removed on both peers. |

**PASS looks like:** for each host grab, a matching host `GRAB DETECTED`+`GRAB(...)` and a client
`recv convert GRAB … [SYNC-MIRROR OK]`, then the same for THROW + LAND, with the **same eid N** throughout,
and the cluster's other piles untouched. Tell me the eids + whether every grab got a `[SYNC-MIRROR OK]`.

**Watch for trouble:** a host `GRAB DETECTED` with **no** client `recv convert` (wire/dispatch issue); a
client `[WARN: no local mirror]` (the client never had that pile); any `[PILE] HOST clump spawned but
source has NO bound eid` (a pile-tracking gap — the grab won't sync). Paste any of those.

## Honest status
Increment 1 = HOST grabs only, **as-built, not verified**. The only thing that promotes docs/piles/08 to
VERIFIED is this hands-on (or a matching real cluster log). Client-grab direction (Increment 2) is next.
