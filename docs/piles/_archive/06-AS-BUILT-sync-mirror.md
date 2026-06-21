# 06 — AS-BUILT: morph/held pile sync-mirror

> What was coded for the grab→carry→throw→land sync, vs the `05-MORPH-SYNC-DESIGN.md` spec.
> Status lifecycle per the standing rule ([[feedback-docs-piles-living-knowledge-base]]):
> **DESIGN (05) → AS-BUILT (this doc) → VERIFIED**.

## ⛔ STATUS: HANDS-ON FAILED (2026-06-20) → REVERTED. Deployed build is the take-17 BIND (no morph).

The morph sync (take 18, proto v81, `97427CD0`) **FAILED the user's hands-on: "nothing works, no
sync-mirror host↔client" — and it was a REGRESSION (worse than take 17's working bind).** Reverted;
deployed = `EFD869CBF2D8788F` (proto **v80**, the take-17 adopt-BIND, morph fully removed).

### ROOT CAUSE (from the real logs — the decisive lesson for the next attempt)
The host grab armed the morph (`pile_morph: armed pending-morph E=4743`) but **the clump Init-POST
adopt NEVER FIRED** → no `RegisterPropMirror REBOUND`, no `PropConvert` sent (the "4 sent" earlier
were arm-log lines, not sends). The held clump streamed `eid=0` again (110× "no local match").
**The correlation gate `TryAdoptGrabbedClump` (pile_morph.cpp:143) keys on
`GetClumpHoldPlayer(clump) == grabbingPlayer`, but `clump.holdPlayer` is NULL at the clump's
Init-POST** — the BP `toClump()` sets `holdPlayer` *after* `FinishSpawningActor` (where Init-POST
runs), per the morph RE's own reconstruction (`votv-chippile-clump-morph-RE` §1.2, which was
explicitly "reconstructed from field shapes, not literal bytecode"). So the gate fails silently →
no adopt → no convert.

**And it was a REGRESSION because take 18 REPLACED the reliable `PropDestroy(E)` on grab (which
worked in take 17: host-grab → client pile vanishes) with the morph-arm. When the adopt misses, the
grab now does NOTHING** → "nothing works". The design's "the convert subsumes the destroy" only holds
if the convert reliably fires; it didn't. An audit + a build + a host-grab smoke could NOT catch this
— it is a RUNTIME-TIMING fact (when `holdPlayer` is set) that only a live probe or hands-on reveals.

### WHAT THE NEXT MORPH ATTEMPT MUST DO (do NOT guess again — MEASURE first)
1. **LIVE PROBE the clump runtime BEFORE coding the correlation** (RULE: RE/measure before guess;
   the morph has failed ~11×, every time on a guessed mechanism). Log at the clump's Init-POST:
   (a) does the Init-POST observer even FIRE for a `toClump`-spawned clump (EX_CallMath spawn —
   c527f31b warns EX_CallMath spawns can bypass the ProcessEvent observer)? (b) what is
   `holdPlayer` at that instant (confirm null)? (c) where does the clump SPAWN — at the grabbed
   pile's transform, or the player's hand? (d) how many ms after the grab does the clump Init fire
   (vs the 500ms TTL)?
2. **Pick the correlation from the measured data**, not the RE reconstruction. Candidates:
   - POSITION: store the grabbed pile's transform in `g_pendingMorph`; match the clump by proximity
     at Init-POST (robust IF the clump spawns at the pile's spot — measure (c)).
   - NEXT-CLUMP: single-slot latch + short TTL → the next clump Init-POST after a grab IS the
     grabbed clump (works for a single grab; ambiguous only for two simultaneous grabs).
   - DEFERRED holdPlayer: if Init-POST is too early, re-check holdPlayer 1 tick later.
3. **Do NOT replace the reliable destroy with an unreliable convert.** Either (a) make the convert
   provably reliable first, or (b) use a deferred-destroy: arm the morph + a deferred `PropDestroy(E)`;
   if the adopt fires within N frames, cancel the deferred destroy (the convert handles it); else send
   the destroy (take-17 fallback) so a missed morph never regresses the working grab.
4. The rest of the 05 design (eid-E re-skin, OnConvert in place, the audit's 7 fixes) was SOUND — the
   only break was the clump-adopt correlation + the destroy-replacement. Keep the 05/audit work as the
   basis; fix the correlation from measured data.

---

> (Below: the as-built record of the REVERTED take-18 implementation, kept for the next attempt's
> reference. The code is no longer in the tree — restore from git/the design if re-attempting.)

## What it does (recap)
On the restored adopt-BIND model (client adopts its save piles onto host eids `E`), a pile
grab morphs pile→clump→pile. The clump CARRIES `E` across all 3 morph UObjects; each peer
re-skins its OWN bound actor in place; `PropConvert{kind, oldEid==newEid==E}` broadcasts each
edge; the held clump streams under `E`. One actor per eid per peer — no second actor, no
doom/catalog/strip/thin-client.

## Files changed (as-built, per the implementing agent + verified in-tree)
| File | What was coded |
|---|---|
| `include/coop/net/protocol.h` | `PropConvertPayload`: `uint8_t kind` carved from `_pad[3]` (size 100B unchanged, static_assert holds); `namespace propconvert_kind { kToClump=0, kToPile=1 }`; doc rewrite for `oldEid==newEid==E`. **`kProtocolVersion 80 → 81`.** |
| `include/coop/pile_morph.h` + `src/coop/pile_morph.cpp` (NEW module) | The morph glue: `g_pendingMorph` (500ms TTL) / `g_pendingLand` (3s TTL) single-slot latches; `ArmPendingMorph`/`ArmPendingLand`; `TryAdoptGrabbedClump`/`TryAdoptLandedPile` (rebind `E` + broadcast `PropConvert{kind,E}`); host-side `g_morphIsClumpByEid` conflict map (`HostAcceptMorph`, §3.2 busy-eid drop); `SpawnLocalClump` (deferred spawn + `MarkIncomingSpawn` + physics-off + chipType + hit-notify-off); `OnDisconnect` teardown. |
| `src/coop/trash_collect_sync.{cpp,h}` | `OnPileGrabPre` arms `g_pendingMorph` instead of sending `PropDestroy(E)`; `EnsureHeldItemBroadcast` early-returns for a clump already eid-bound (no keyless PropSpawn); **RETIRED the entire clump death-watch** (`WatchedClump`/`g_watchedClumps`/`WatchClump`/`BroadcastConvertNear`/`TickWatchReleasedClumps`, ~150 LOC, RULE 2). |
| `src/coop/prop_lifecycle.cpp` | Morph adopt hook in `GrabObserver_Aprop_Init_POST_Body` at the pinned placement (after `MarkProcessedInit`, before takeObj/role/keyless): chipPile→`TryAdoptLandedPile`, else→`TryAdoptGrabbedClump`, each `return`-ing on adopt. |
| `src/coop/local_streams.cpp` | `ResolveHeldPropEid` gained the `ResolveMirrorEidByActor` 3rd fallback (the clump's `E`); `g_lastHeldEid` cache (per-tick O(1)); release edge arms `g_pendingLand` for an eid-bound clump (class-gated). |
| `src/coop/remote_prop.{cpp,h}` | `OnConvert` rewritten to **re-skin in place** branching on `kind` (spawn-new-bound-to-E → rebind → echo-destroy-old; spawn-before-destroy); `SpawnLocalPile` helper; **`RegisterPropMirror` REBIND-IN-PLACE** when `E` resolves to a different actor (`SetActor`). |
| `src/coop/remote_prop_spawn.cpp` | `OnSpawn` eid-dedup gated with `!fromConvert` (a convert always fresh-spawns the new rendering). |
| `src/coop/prop_element_tracker.{cpp,h}` | `RebindLocalElementActor(eid,newActor)` — keeps the host's `g_actorToPropElementId` reverse map consistent across a local morph. |
| `include/ue_wrap/prop.{h}` + `src/ue_wrap/prop.cpp` | `GetClumpHoldPlayer(clump)` — reflection-resolved-offset cached read (the morph-pair correlation key). |
| `src/coop/event_dispatch_entity.cpp` | PropConvert case comment/log refresh (no logic change; eid range-check already accepts `oldEid==newEid`). |
| `src/coop/subsystems.cpp` | Dropped the retired per-tick `TickWatchReleasedClumps`; added `pile_morph::OnDisconnect`. |
| `CMakeLists.txt` | + `src/coop/pile_morph.cpp`. |

## Divergences from the 05 DESIGN (decisions the implementer had to make — REVIEW THESE)
1. **`RegisterPropMirror` rebind is UNCONDITIONAL, not gated.** Design §2.5 assumed Install
   overwrites; it does not (rejects existing eids). Impl made `RegisterPropMirror` rebind in
   place via `SetActor` for ANY different actor — **removing HEAD's "a different LIVE actor keeps
   the eid" guard.** Load-bearing; the morph needs rebind-when-live (clump+pile briefly both
   live). RISK: broader than the morph; the adversarial audit is scrutinising whether it breaks
   any other `RegisterPropMirror` caller / a genuine live-conflict. **(Open: may need to GATE the
   rebind to the morph case.)**
2. **Host reverse-map drain hazard** — on the host's OWN pile morph, `E` is a LOCAL tracker
   element; the old pile's `K2_DestroyActor` PRE would drain `E` via `g_actorToPropElementId`.
   Impl added `RebindLocalElementActor` (non-mirror elements only) to re-point the reverse map
   before the destroy. Design didn't address the host-local-vs-client-mirror asymmetry.
3. **`OnSpawn` eid-dedup gated `!fromConvert`** — with `oldEid==newEid==E`, the ToPile `OnSpawn`
   would falsely converge the still-live clump as "existing"; gated so a convert always
   fresh-spawns. (Reused `OnSpawn` rather than a separate `SpawnLocalPile`, RULE 2.)
4. **Clump class gate** — `TryAdoptLandedPile` gated to `IsChipPile`; everything else routes to
   `TryAdoptGrabbedClump` (self-gates by `holdPlayer` match).

## Known risks to watch in the hands-on (the impl's own confidence notes)
- **Host own-grab ordering (pitfall #2):** the clump Init-POST adopt vs the old pile's destroy
  draining `E`. Impl claims order-robust (the later adopt re-Installs `E` if it was freed), but
  the happy-path ordering should be confirmed in a real test.
- **Throw-land detection (pitfall #4):** gated on release-edge + ≤200cm + chipType + 3s TTL.
  Correct by construction, but only a real grab→throw→land proves the proximity/chipType anchor
  matches (e.g. a long bounce landing >200cm could miss; a re-grab tail could mis-anchor).

## Adversarial audit (2026-06-20) — found 1 CRITICAL + 1 HIGH + 3 MEDIUM + 2 LOW; deploy BLOCKED until fixed
The 9-angle audit confirmed the core re-skin, the no-dupe invariant (host-grab path), perf, and the
router wiring are clean, but found real defects (fixes in flight — see status):
1. **CRITICAL — PropConvert eid range-check** (`event_dispatch_entity.cpp`): strict per-role
   `IsAllowedSenderEid` drops a CLIENT-initiated convert (client grabs a host-owned pile → host-range
   E + client sender slot → dropped on host + bystanders). Client-grab morphs never propagate — the
   exact failure a host-grab-only smoke would mask. FIX: accept EITHER range like the PropDestroy path.
2. **HIGH — `RegisterPropMirror` unconditional rebind**: orphans a live actor for the ~9 non-morph
   callers. FIX: gate the rebind behind a `rebindInPlace` param; only the 3 morph sites pass true.
3. **MEDIUM — throw-land fails on a real throw**: anchored on the held (kinematic) position + 200cm,
   but the pile lands ballistically meters away → never re-piles. FIX: track the flying clump's last
   LIVE position per-tick (anchor on the true landing point).
4. **MEDIUM — `g_morphIsClumpByEid` never erases** (slow in-session growth). FIX: erase on ToPile.
5. **MEDIUM — `SpawnLocalClump` ignores the variant class** (mirror renders base clump mesh). FIX:
   use `payload.pileClass`.
6. **LOW — host-grab Init-vs-destroy ordering** assumed-not-enforced. FIX: suppress the bare
   PropDestroy(E) while a morph is pending for E.
7. **LOW — OnConvert idempotency** classifies by `!IsChipPile`. FIX: positive clump-class check.

**STATUS UPDATE (deployed): all 7 fixes APPLIED + build clean; the CRITICAL (range-check either-range)
+ HIGH (`RegisterPropMirror` rebind gated to the 3 morph sites only; all other callers keep HEAD's
live-conflict guard) RE-VERIFIED in-file by me. Throw-land re-anchored on the clump's per-tick live
landing point (radius 300cm); map erases on ToPile; clump uses the variant class; bare PropDestroy(E)
suppressed while a morph is pending; OnConvert no-op uses a positive clump-class check. DEPLOYED
`votv-coop.dll 97427CD07B89F22C` x4 (SHA-verified), proto v81, runbook take-18. AWAITING USER HANDS-ON
-- NOT yet VERIFIED. Residual watch-points for the hands-on: long-throw land anchor (>300cm could miss),
and a peer where the released clump is kinematic (anchor = snap point).**

## Verification (to stamp VERIFIED)
Hands-on, both peers on the v81 build, New Game:
1. Host grabs a pile → the CLIENT sees the round clump in the host-puppet's hand (not just the
   pile vanishing). 2. Host throws → both see it re-pile (correct variant) at the same spot.
3. Client grabs/throws → the HOST mirrors it. 4. Spam-grab E → NO local-clump dupe accumulation,
   NO pile dupes. 5. FPS normal. Then this doc's status flips to VERIFIED (or back to a new
   diagnosis if it regresses — NEVER mark working from a smoke).
