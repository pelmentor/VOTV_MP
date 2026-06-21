# Hands-on runbook — trash-proxy PHASE 1 (the dup fix + km-walk robustness) — 2026-06-21, take-23

**Deployed:** `votv-coop.dll` SHA `f2344bab` to all 4 copies (host / copy / copy2 / dev). Proto **v82**
(UNCHANGED — none of this touches the wire). Code HEAD `8a17faeb` (+ harness `f1177589`). Build CLEAN (Release).
**Autonomous LAN chippile smoke: FUNCTIONALLY GREEN by log** — 875 client proxies, the dup GONE (0 `mirror
NOT-FOUND`), the host-driven grab→carry→throw→re-pile cycle mirrored cross-peer on eid=6264 (HIGH-1
spawn-on-convert fired), 0 proxy/drive errors, no crash/OOM, 300 s stable. (The smoke proves the MECHANISM;
this hands-on proves the FEEL + the visual.)

> This SUPERSEDES take-22 (the s38 re-pile thunk on `BA79E705`). The thunk + triple-cue fix (s38, A+B)
> are FOLDED IN and were log-GREEN on `BA79E705`. Phase 1 adds the **host-authoritative `AStaticMeshActor`
> proxy** (the real dup fix) on top.

## What phase 1 changes (vs the `BA79E705` you last tested)

The client no longer mirrors a host pile/clump with a **real `actorChipPile_C` / `prop_garbageClump_C`
blueprint**. It now spawns an **`AStaticMeshActor` WE own** (no BP, `AddToRoot`, our eid->actor registry)
and **re-skins it in place** on every convert. Because that actor never self-morphs, never GCs, never goes
stale-index, the "mirror NOT-FOUND -> spawn fresh -> orphan the original = DUP" path is **structurally
unreachable**. Plus three robustness fixes for the long carry:

- **HIGH-1** — a convert that beats its spawn now renders the correct form (clump vs pile); a stale trailing
  spawn can no longer flip a carried clump back to a pile.
- **HIGH-2** — the clump now wears the correct **per-chipType material** (verified `setTex` bytecode:
  `SetMaterial(0, getChipPileType(chipType).GetMaterial(0))` on the fixed dirtball mesh).
- **km-walk lerp + freeze** — the proxy is **interpolated** between host poses (smooth, not teleport-jerky),
  and a network hitch **FREEZES** it at the last pose instead of dropping it to physics. It releases ONLY on
  an explicit reliable edge (throw / re-pile / disconnect). **This is the "walk a few km and it stays a
  perfect sync mirror" fix.**

Phase 1 is **NoCollision**: the carried/mirrored trash is a kinematic host-driven follower; **your local
player passes THROUGH mirrored trash** (you cannot bump or grab the OTHER peer's mirrored pile yet). That is
the accepted phase-1 limitation — collision + client-grab is phase 2.

## The test (do this on a FRESH save with chipPiles, or one of your recent pile saves)

Two peers (host + client). Both load the same recent save that HAS chipPiles (the garbage litter).

1. **JOIN — the dup check (the headline).** Client joins. Look at a host pile cluster on BOTH screens.
   - PASS: each host pile shows as exactly ONE pile on the client. NO doubled piles, NO piles that
     "don't disappear". Walk around the cluster on the client — still one-for-one.
2. **GRAB + CARRY — the mirror + the km-walk.** HOST grabs a pile (it becomes a clump in hand) and
   WALKS — ideally far (out of the room, down the road, as far as you like).
   - PASS (client view): the clump tracks the host's hand SMOOTHLY (interpolated, not teleport-stuttering),
     stays the right per-chipType look, and NEVER desyncs/drops no matter how far the host walks. A brief
     network hitch should FREEZE the clump in place (not drop it), then resume smoothly.
3. **THROW / DROP.** HOST throws the clump.
   - PASS (client view): the clump freezes at release for the brief flight, then SNAPS to the landed
     pile where the host's pile rests (the host's authoritative landing). One pile, right place.
     (The mid-air freeze-then-snap is a known phase-1 characteristic — phase 2 may stream the arc.)
4. **RE-PILE (no throw).** HOST grabs a pile and drops/re-piles it in place (the s38 thunk path).
   - PASS: converts cleanly, NO ~5 s vanish-return, NO dup. One pile.
5. **Reverse it.** CLIENT cannot grab a host's mirrored pile in phase 1 (NoCollision; the look-trace passes
   through). That's expected — do NOT treat "client can't grab the other peer's pile" as a bug. (Each peer
   can still grab ITS OWN local piles; those broadcast to the other as proxies.)
6. **Disconnect mid-carry.** While the host carries a clump, drop the client (or vice-versa).
   - PASS: no leaked floating ball, no crash; the proxy is cleanly retired on the surviving peer.

## What to read in the log (client log is where the proxy lives)

GREEN markers (client log):
- `trash_proxy: SPAWN eid=... pile/clump ... (AStaticMeshActor, rooted, NoCollision)` — proxies created at join.
- `CLIENT recv convert GRAB(pile->clump) eid=... -> PROXY re-skinned IN PLACE ... [SYNC-MIRROR OK -- no spawn-fresh, no dup]`
- `CLIENT recv convert LAND(clump->pile) eid=... -> PROXY re-skinned IN PLACE ...`
- `remote_prop: slot 0 drive #N -> target(...) [proxy]` — the lerp drive following the carry.
- `trash_proxy: RETIRE eid=...` on destroy/disconnect (no leak).

RED flags (report these):
- `mirror NOT-FOUND` / `no local mirror of E existed` — the staleness path that caused the dup (should be GONE).
- any `trash_proxy: ... FAILED`, `SetComponentMaterial unresolved`, `proxy spawn-on-convert FAILED`.
- a doubled pile visible on screen; a clump that drops to the floor on a network hitch (should FREEZE).
- RSS climbing without bound during a long carry (the freeze must not leak).

## Honest status
Phase 1 is **AS-BUILT + autonomously smoked** (see the smoke result line at the top) — **NOT yet hands-on
verified**. The autonomous smoke proves the proxy lifecycle (spawn -> convert re-skin -> drive -> retire) +
no crash/leak; it CANNOT prove the km-walk FEEL or the visual dup-gone — that is THIS hands-on. Mark phase 1
VERIFIED only after step 1 (dup gone) + step 2 (smooth km-carry) pass on your screen.
