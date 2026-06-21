# Pile sync — the complete history (every scheme, what worked, what broke it)

> The blow-by-blow evolution of `actorChipPile_C` coop sync, from first sync to the
> 2026-06-20 save-strip revert. Reconstructed from git history (`git log`/`git show`),
> the findings docs in `findings/`, and the session memory `project_session21..32`.
> Commit hashes are on `main`; the s28-s32 work is UNCOMMITTED on top of HEAD `1272b0a3`.

## The core difficulty (why this took 30+ sessions)

A pile has **no cross-peer identity** (keyless; bytecode-proven — see README). So the host
and the joiner cannot agree on "which pile is which" except by **position**, and position is
unstable: the join load + the same-world "shadow-drain" free and re-create pile actors, so any
binding made to a pile pointer/position at one instant rots moments later. Every scheme is an
answer to "how does the client's pile become the host's pile without making two?" — and they
differ in whether they ADOPT the client's existing pile or DESTROY it and FRESH-SPAWN a mirror.

## Per-stage timeline

| When | Commit(s) | Scheme | How a client pile became a host mirror | Verdict |
|---|---|---|---|---|
| 06-02/03 | `33e7f259`,`b97b693e`,`8c1488b7` | clump hand-attach → clump via pose pipeline by eid | (clumps only; carried-ball sync) | early |
| 06-08 | `0ffba394`,`d75873ed` + the `votv-clump-*-06-08` RE | clump/pile dupe RE; observable grab hook | — | RE groundwork |
| 06-09/10 | (in batch `77225106`, 06-10) + `votv-snapshot-adoption-root-causes-2026-06-10`, `votv-client-world-divergence-architecture-2026-06-09` | **class-sweep + fresh-spawn** | client class-swept ALL ~870 local piles, host fresh-spawned 870 mirrors; the P2 claim sweep destroyed unclaimed locals | worked but heavy |
| **~06-12/13** | **batch `77225106` ("v43-v57: save-transfer join + world sync sweep")** | **★ STALE SAVE → RECONCILE ★** the scheme the USER remembers working | client loads host's STALE `.sav` (its own copy of every pile); host streams a keyless eid per pile; the **world-sync sweep / bracket reconcile** binds the client's OWN pile to the host eid + sweeps residue | **★ WORKED ★** (user-confirmed) |
| 06-15 | `de10514c` (+reverts `68767d25`/`145dab9e`, re-enable `38a41dfc`, sweep-skip `f5397a4e`) | **LIVE-save switch** (ship a live capture, not the stale `.sav`) — to fix a KERFUR dupe | same bracket reconcile, but now the joiner loads a LIVE capture | kerfur-motivated; began destabilizing the pile path (timing regressions, multiple reverts in one day) |
| 06-16 | `c527f31b` | **steady-world re-seed** mirrors mid-session-spawned piles (EX_CallMath spawns the Init hook misses) | adopt-by-position at bracket, + the re-seed mints eids for new piles | piles mirror in steady world |
| 06-16 | kerfur arc `7a429abe`..`8850355b` (K-0..K-6) | KerfurEntity stable-id; conversion adopt | (kerfur, but shares the prop mirror + sweep machinery) | kerfur fixes, pile path collateral |
| 06-17 | `068ee016` (session-21) | join-churn "band-aid" (reconcile-once latch) | adopt-by-position kept; latch stops the steady re-seed re-bracketing ~10x/join | band-aid (later retired) |
| 06-18 | `da233ecb` (R1-R4) | MTA-divergence refactor: incremental per-pile PropSpawn + membership-bounded sweep; retire the latch | **adopt-by-position UNCHANGED** (still `EnsurePileBindIndex`) | adopt intact |
| **06-18** | **`1272b0a3` (HEAD, session 22)** | **host-authoritative death-watch + client host-authority gate (P2)** | **adopt-by-position STILL PRESENT** (`remote_prop_spawn.cpp:498-563` `EnsurePileBindIndex`: match own pile within 30cm + chipType + class → `RegisterPropMirror(eid, ownPile)`) | **last COMMITTED state; the adopt reconcile is intact here** |
| 06-18→20 | UNCOMMITTED s23-s31 | invisible-handle → position-keyed → eid-bind-timing → static-object → confirm-by-clump → reaper → **thin-client doom** | **adopt DELETED**; client owns ZERO save piles (P1 mint-gate), every pile is a FRESH host mirror, a quiescence-clocked **doom** destroys the save originals | **REGRESSION** (the doom wedges, never fires → dupe) |
| 06-20 | UNCOMMITTED s32 | **save-strip** (host strips pile records from the transferred save) | client loads ZERO piles; host stream is sole source | **WORSE** (client had NO piles — the mirror sync is adopt-based, nothing to adopt). **REVERTED.** |

## ★ The working scheme (restore this): "stale save → reconcile" ★

The state the user means by *"June 12-13, fable-5 did a good job sync-mirroring piles"* and
*"we had a scheme — stale save — reconcile"*:

1. **Stale save transfer.** On join the host ships its (stale, on-disk) `.sav`. The joiner
   loads it via the game's own `loadObjects`, so it materializes the SAME ~870 piles at the
   SAME positions the host had. (Later changed to a LIVE capture on 06-15 for kerfurs — that
   switch is orthogonal to the pile reconcile, which works with either.)
2. **Host mints a keyless eid per pile** in `SeedWalk_` and streams it eid-only (key.len=0) in
   the join snapshot bracket.
3. **The reconcile = ADOPT-BY-POSITION at the bracket** (`EnsurePileBindIndex`, intact at HEAD
   `1272b0a3` `remote_prop_spawn.cpp:498-563`): for each host eid-only pile expression, match
   the client's OWN nearest save-loaded pile within **30 cm + equal chipType + exact class**;
   on match → `RecordClaim` + `UnmarkKnownKeyedProp` (drop the client's own minted identity) +
   `ReconcileToHostPhysics` + `RegisterPropMirror(eid, ownPile)`. **The client's existing pile
   BECOMES the host mirror — no second actor is created.** This is the no-dupe mechanism.
4. **Residue removal = the deferred divergence sweep** (`RunDivergenceSweep_`, quiescence-gated,
   >50% world-wipe valve): destroys only local piles the host's bracket did NOT claim (piles the
   host collected / no longer has).

That's it: stale (or live) save gives the client its own piles; the bracket reconciles them onto
host eids by position; the sweep removes the residue. No fresh-spawn, no doom, no re-stream
round-trip. The user tested this ~06-12/13 and it mirror-synced piles well.

## The regression chain (what replaced the adopt, and why it duped)

The adopt-by-position reconcile was progressively dismantled across s23-s31 (UNCOMMITTED),
because edge cases (the shadow-drain rotting the position bind; the keyless identity not
surviving a cross-process reload) were "fixed" by replacing adopt with destroy+fresh-spawn:

1. **Position-bind seed (v83, s23-24):** tried to make the bind survive the drain by caching a
   `PileSeed` and re-binding — the bind still rotted (no keyless identity survives the reload).
2. **Thin-client doom (s30-31):** gave up on adopt entirely — the client owns ZERO save piles
   (P1 mint-gate: `prop_element_tracker.cpp` now returns for `isKerfur || isChipPile`), every
   pile is a FRESH host mirror, and a quiescence-clocked **doom** (`DoomNonMirrorChipPiles_`,
   `prop_adoption.cpp:598`) is supposed to destroy the client's save originals. **The doom never
   fires** (`liveMirrors=758 < expected=868`, the gate never converges after the shadow-drain
   frees the mirrors) → save original + fresh mirror coexist = the JOIN DUPE.
3. **Save-strip (s32):** stripped the piles from the transferred save so the client loads none.
   But the mirror sync is adopt-based — with no save pile to adopt, the client had **NO piles at
   all**. Reverted same day.

The fix the user is pointing to: **stop fighting the keyless identity with destroy+fresh-spawn;
restore the adopt-by-position reconcile** (which simply re-uses the pile the client already
loaded). The drain-rot edge case is solved separately and cheaply (re-run the adopt after the
drain / re-stream the freed mirrors), NOT by throwing out adopt. See
[03-RESTORATION-PLAN.md](03-RESTORATION-PLAN.md).
