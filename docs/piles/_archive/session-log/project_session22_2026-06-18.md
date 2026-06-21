---
name: project-session22-2026-06-18
description: "Session 22 — R1-R4 MTA-divergence SHIPPED (da233ecb) + PART 1/2/3 pile/kerfur reliability SHIPPED (1272b0a3) + the GROUND-TRUTH log diagnosis of why peer pile interactions still don't sync + the INVISIBLE-HANDLE host-authoritative pile model (the user's directive = NEXT SESSION's directed work)."
metadata:
  node_type: memory
  type: project
  originSessionId: 86866d94-8692-44e2-b0a3-136eb16f4d11
---

# Session 22 (2026-06-18) — MTA-divergence shipped, pile/kerfur reliability, the INVISIBLE-HANDLE model (NEXT SESSION)

## STATE (compact-ready)
- **HEAD `1272b0a3`, working tree CLEAN** (no uncommitted edits). Deployed DLL SHA **`4ebb8137`** x4 (all Game_0.9.0n* folders).
- Two arcs SHIPPED this session:
  - **`da233ecb`** `[coop R1-R4]` — the MTA-divergence refactor (incremental per-prop streaming + membership-bounded sweep, retired the reconcile-once latch). joinchurn smoke PASS. 2-agent audit zero crit/high. (Detail: [[project-mta-divergence-roadmap-2026-06-17]], now marked SHIPPED.)
  - **`1272b0a3`** `[coop pile/kerfur reliability]` — PART 1 (host death-watch) + PART 2 (client host-authority gate) + PART 3 (kerfur convert echo fix). Adversarial audit zero crit/high.
- **NOT FIXED (next session):** peer pile interactions still don't mirror-sync. The user clarified the architecture (the INVISIBLE-HANDLE model, below). User said "documentize, prepare for compact" — so this is documented, NOT further implemented.

## THE ARC
1. Compact-prep of the R1-R4 plan → user "Go implement all" → R0-R4 shipped (`da233ecb`, joinchurn PASS).
2. User: pile regressions + kerfur turn-on/off dupe + **"you keep breaking stuff. I don't want DUPES at all. AS RELIABLE AS POSSIBLE. NO CRUTCHES."**
3. RCA (2 converging agents): the pile dupes were PRE-EXISTING host-authority gaps the 4s full re-snapshot was MASKING; R1 removed the re-snapshot (correct per MTA) → exposed them. PART 3 (kerfur dupe) was a GENUINE R1 regression (`RegisterHostPropSilent` didn't `MarkKnownKeyedProp` → R1's steady re-seed re-broadcast the converged kerfur prop with its real key → dupe on a client fuzzy-miss).
4. Shipped PART 1/2/3 (`1272b0a3`).
5. User: **"Peers interacting with piles is not mirrored-synced at all."** → READ THE TEST LOGS (ground truth, below).
6. User clarified the **INVISIBLE-HANDLE** model + "documentize, prepare for compact."

## THE LOG DIAGNOSIS (ground truth — read the user's hands-on test logs, do NOT re-derive by guessing)
The host + client `votv-coop.log` from the user's 11:45 test revealed the REAL problem (my PART 1/2/3 were necessary but NOT sufficient — they fixed dupes + host destroys, but the SYNC of held clumps was still broken on BOTH sides):

1. **Host held clumps are suppressed FOREVER.** `trash_collect.cpp:218` PRE-QUIESCENCE guard (`!HasLoadTailQuiesced() && IsInDivergenceUniverseUnclaimed(heldActor)`) fires on the HOST repeatedly (`held item 'prop_garbageClump_C' grabbed PRE-QUIESCENCE -- NOT expressing`). ROOT: `HasLoadTailQuiesced()` is **HOST-PERMANENTLY-FALSE** (the host never arms the join sweep — `kerfur_convert.cpp:578` / the load-bearing signal facts) → `!false = true` forever → the host NEVER expresses its own held clumps → host pile interactions (grab→clump→throw) never reach the client. PRE-EXISTING bug, was masked by the retired 4s re-snapshot. (This guard is a CLIENT join-window guard; it has no business firing on the host.)
2. **Both peers flood `eid=0` PropPose.** A suppressed clump has no eid, but the held-pose stream still streams it → the other peer logs `remote_prop: incoming PropPose key 'None' eid=0 -- no local match` (HUNDREDS). No identity → no mirror → no sync. (Host side: suppressed by the guard above; client side: suppressed by my PART 2.)
3. **WORKING (confirmed in the logs):** PART 1 host death-watch IS broadcasting (`net_pump: host death-watch -- broadcast N explicit PropDestroy`); OnPileGrabPre IS firing (`trash_collect: pile grab (E-press on chipPile eid=N) -> PropDestroy`); R1 incremental PropSpawn IS firing.

## >>> THE INVISIBLE-HANDLE MODEL (user's DIRECTIVE — NEXT SESSION's directed work) <<<
User, verbatim: **"make peers interacting with piles be just invisible handles interacting with host's piles which then broadcast to other peers"** + **"every peer will ALSO have invisible handles but they will just RECEIVE host's data."**

= a FULLY HOST-AUTHORITATIVE pile/clump lifecycle (the **K-5 kerfur model generalized to ALL piles/clumps**):
- The **HOST owns EVERY pile/clump IDENTITY (eid)** and is the SOLE authority to spawn / destroy / morph.
- A PEER (client) interacting with a pile is a remote **HANDLE**: it RELAYS its grab/throw to the host; the host performs the authoritative action on its OWN pile + broadcasts to ALL peers.
- **EVERY peer (including the interacting one) just RECEIVES the host's data + mirrors it.** NO peer EVER AUTHORS a pile/clump entity (mint eid + SendPropSpawn/PropConvert) — that author-and-broadcast is the dupe root the whole session chased.

### NEXT-SESSION implementation plan (verify each against current code; build a SMOKE):
- **FIX A (immediate, host→client; the logs prove it):** gate the PRE-QUIESCENCE guard (`trash_collect_sync.cpp:218`) to **CLIENT-only** (`s->role()==Client && !HasLoadTailQuiesced() && IsInDivergenceUniverseUnclaimed`). It is a client join-window guard; on the host `HasLoadTailQuiesced` is perma-false so it suppresses host clumps forever. The host has no join-window divergence — its clumps are its own authoritative entities and MUST be expressed. SMALL + clearly correct (the guard's "off+grab host dupe" case is a CLIENT grabbing a save-loaded prop pre-quiescence — host-irrelevant). This alone restores host pile interactions syncing to clients.
- **The client invisible-handle relay (the main build):**
  - Client grab: `OnPileGrabPre` already fires on the client + sends `PropDestroy(host pile eid)` (removes the host original). ADD: relay a grab-request → the HOST spawns a host-owned clump (host-range eid) "driven by client slot N" + broadcasts `PropSpawn` → ALL peers (incl. the grabbing client) mirror the host's clump. The client's LOCAL BP-morph clump stays SUPPRESSED (PART 2) + ADOPTS the host's clump (claim/rebind, never author — [[feedback-recurring-bug-is-architectural]]).
  - The client DRIVES the host clump's pose: its hand position streams the pose keyed on the **HOST eid** (NOT eid=0). Host applies + relays to the other peers. (This is MTA's "syncer" concept — the nearest/interacting peer streams the entity's pose, but the HOST owns identity.)
  - Client throw: relay a throw-request (landing pos) → host destroys the clump + spawns a pile (host eid) → broadcasts. Client adopts.
- **Fix the `eid=0` flood:** the held-pose stream must NEVER stream a clump with eid=0 (no identity). The session-21 gate `pp.key.len>0 || pp.elementId!=0` (local_streams.cpp ~310) was meant to do this — VERIFY why the logs still show eid=0 floods (a different pose path? the gate bypassed?). With the relay the client drives the HOST eid (not 0) → the flood disappears at the source.
- **BUILD AN INTERACTION SMOKE — the verification gap that let ALL of this ship.** `joinchurn` tests JOIN only, NOT interactions; that is why 3 rounds of pile changes shipped broken (see [[feedback-interaction-smoke-not-join-smoke]]). Build a smoke that programmatically triggers a peer pile grab/throw (a dev trigger that grabs a pile, like `kerfur_toggle`'s trigger) + asserts: the result mirrors on the other peer, NO dupe, NO eid=0 flood, no `no local match` warns. If a programmatic grab isn't feasible, hand off with a hands-on runbook + the exact log lines.

### Reuse / precedent (don't reinvent):
- **K-5 kerfur** (`kerfur_entity` + the `MarkPropElement` client-mint gate `prop_element_tracker.cpp:328-335`): the client NEVER mints a kerfur identity; it requests the host; the host owns the eid. THE precedent — generalize it to piles/clumps.
- Existing wire kinds: `PropSpawn` / `PropDestroy` / `PropConvert` (host→client), the held-pose stream (`local_streams.cpp`), `OnPileGrabPre` (the grab edge, both roles, `trash_collect_sync.cpp:354`), `BroadcastConvertNear` (the clump→pile convert — make it HOST-only, or a request from clients), `remote_prop::ResolveMirrorEidByActor`.
- MTA: `CStaticFunctionDefinitions.cpp` createObject/destroyElement run SERVER-side then `BroadcastOnlyJoined`; a client never creates/destroys a shared entity locally + broadcasts.

## DEBT / NOTES
- File sizes >800 soft (none >1500 hard): `net_pump.cpp` 955, `prop_lifecycle.cpp` 899, `prop_element_tracker.cpp` 870. Queued extractions: `coop/world_reseed.cpp` (the net_pump reaper/re-seed/death-watch block) + `coop/divergence_sweep.cpp`.
- `tools/mp.py joinchurn` still PASSES (the join path is intact); it just doesn't cover interactions.
- The HUD dev-feature request ("for client make dev feature - HUD allowed") is STILL PENDING clarification — ambiguous (game native HUD vs our coop ImGui overlay `src/ui/hud.cpp` vs `showhud`). Ask before building.
- [[feedback-recurring-bug-is-architectural]] (piles fixed 3x this session → the patch LEVEL was wrong → the invisible-handle model is the architectural fix), [[feedback-interaction-smoke-not-join-smoke]] (the verification gap), [[feedback-follow-mta-architecture]] (host-authoritative = MTA). READ FIRST.
