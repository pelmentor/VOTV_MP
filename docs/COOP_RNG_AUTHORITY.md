# COOP_RNG_AUTHORITY.md — living RNG host-authority tracker

**Purpose.** The single living home for "which of VOTV's RNG / non-deterministic systems are
host-authoritative in coop, which still diverge, and the progress of closing each gap." This is the
TRACKER (update it every session); the point-in-time audit snapshot is
`research/findings/votv-rng-host-authority-audit-2026-07-10.md` (the evidence + file:line cites).

**How to use.** Each RNG system has a STATUS. Work proceeds **tier by tier**, each tier gets its own
`/qf 15` (QUESTION → DESIGN → IMPL) **before** any build (user directive 2026-07-10). Update the
row STATUS + the per-tier progress block as work lands. Never mark a row VERIFIED without a real
hands-on or a matching live log (docs-piles discipline).

---

## The principle (per rule 1)
**Any RNG that changes SHARED world state must be rolled by the HOST; a peer rolling its own
diverges.** Three correct shapes:
1. **MIRROR** — host owns roll+state, broadcasts on change; client SUPPRESSES its own roll (mirror
   step 3). Precedents: time_sync, weather_sync, event_fire_sync, npc_sync, serverbox_sync,
   console_state_sync.
2. **INTENT** — discrete client action → host performs the RNG-bearing action authoritatively
   (`sv.request` archetype; device_occupancy, DoorOpenRequest, signal-catch, balance-spend).
3. **SEED** — a *seeded* `RandomStream` is reproducible if the seed is shared; replicate the seed
   (only garbagePileSpawner, radiotower, xmaslight qualify). All other game RNG is unseeded → not
   reproducible.
Cosmetic-local RNG (no shared consequence) is LEFT ALONE.

**STATUS legend:** `DONE-MIRROR` / `DONE-INTENT` / `CONVERGENT` (peer-symmetric but safe by design)
· `OPEN-DIVERGES` (needs a shape) · `NEEDS-PROBE` (static-inferred; confirm client-liveness first)
· `COSMETIC-LOCAL` (leave) · `SEED-OPP` (seed-replication opportunity).

---

## PROGRESS DASHBOARD (update each session)

| Tier | scope | rows | DONE | OPEN | /qf QUESTION | /qf DESIGN | /qf IMPL | build |
|---|---|---|---|---|---|---|---|---|
| **T1** | gameplay divergence | 4 groups | 0 | 4 | ✅ converged 2026-07-10 (9 rounds; PRE-REGISTRATION below) | ⬜ | ⬜ | ⬜ probe first |
| **T2** | world consistency | 3 groups | 0 | 3 | ⬜ | ⬜ | ⬜ | ⬜ |
| **T3** | cosmetic-local | — | n/a (leave) | — | — | — | — | — |
| **SEED** | seed replication | 3 | 0 | 3 | ⬜ | ⬜ | ⬜ | ⬜ |
| **prior** | already host-auth | ~30 | ~30 | 0 (email N=1 closed) | — | — | — | ✅ shipped |

**Gating measurement for ALL of T1/T2 (do FIRST, next session):** a LIVE client-side roll probe —
log every un-suppressed `BeginDeferredActorSpawnFromClass` + `mainGamemode`/`daynightCycle`
RNG-consuming vf call that fires on the CLIENT during a LAN session. Converts every NEEDS-PROBE row
below into a measured OPEN or a struck row. The static list is the UPPER BOUND, not the confirmed set.

**Recommended entry point (next session):** Tier 1 → the **structural-suppression vs allowlist**
root decision (it unlocks the whole spawner group at once) is the first `/qf 15` QUESTION pass; the
live probe is that pass's gating measurement. The `calculateAreaError → QuitGame` roll (T1-2) is
small + scary enough to verify on its own in parallel.

---

## TIER 1 — gameplay divergence (peers see a different world)

### T1-1 · Uncovered creature/entity spawners — STATUS: NEEDS-PROBE / OPEN-DIVERGES
`npc_sync` allowlist covers 15 (zombie, kerfurOmega, krampus, funguy, goreSlither, insomniac,
fossilhound, antibreather, orborb, arirFollower, ariralShooter, ariralPigBeater, killerwisp,
ventCrawler, wisp) + piramid2 + arirShip. UNCOVERED spawners (each ticker rolls per-peer):

| spawner | spawns | RNG decides | status |
|---|---|---|---|
| ticker_deerSpawner | deer_C | gate[0.02] + pos + variant | NEEDS-PROBE |
| ticker_mannequinSpawner | wMannequinSpawn_C/prop | Array_Shuffle+Random which+where | NEEDS-PROBE |
| ticker_hexahiveSpawner | hexahive | gate[0.02] + pos | NEEDS-PROBE |
| ticker_eyers | eyer_C | gate[0.05] + pos (night) | NEEDS-PROBE |
| ticker_bp7Spawner | bp7 | branch coin + pos | NEEDS-PROBE |
| ticker_roachSummoner | roach | timing + pos | NEEDS-PROBE |
| grayBoarSpawner / boarInvasion | grayboar_C, ariral_shooter, firetank | count+pos+variant | NEEDS-PROBE |
| ghostcarSpawner | ghostcar_C | pos + yaw jitter | NEEDS-PROBE |
| bunnySpawner | ominousBunny/superAngryBunny | which variant + pos | NEEDS-PROBE |
| bloodSkeletonSpawner | blood skeleton | pos + timing | NEEDS-PROBE |
| arirBusterSpawner | fakeRadarWalker_C | pos + timing | NEEDS-PROBE |
| greenFireSpawner | greenfire_C | pos + timing | NEEDS-PROBE |
| furfurAltarSpawner | paranormalSpot_C | pos + timing | NEEDS-PROBE |
| hillRollerSpawner | prop/propThrown/nail | which prop + pos | NEEDS-PROBE |
| ufoDropper | kerfurOmega/fallingBody/lampPost | Array_Random which drop + where | NEEDS-PROBE |
| ticker_yellowWispSpawner | killerwisp_C (covered class) | spawner's own gate+pos | **DONE-MIRROR** (2026-07-10 correction: ambient_spawner_suppress.cpp cancels its ReceiveTick on the client; wisp class allowlisted) |
**Root decision (the /qf):** broaden the allowlist (add each) vs STRUCTURAL — client runs NO
world-spawn ticker; host owns all spawns; allowlist becomes only the MIRROR set. Structural is the
rule-1 root (allowlist inherently lags 15/40).

**2026-07-10 fact-base correction (the audit missed a shipped module):**
`src/coop/session/ambient_spawner_suppress.cpp` ALREADY client-cancels 4 rollers via per-class
PE-visible fn PRE-interceptors (mushroomMaster.Spawn, mushroomSpawner.Spawn,
pineconeSpawner.ReceiveTick, ticker_yellowWispSpawner.ReceiveTick), and `host_spawn_watcher.cpp`
MIRRORS the pinecone-family outputs (keyless PropSpawn + death-watch) — the full suppress+mirror
shape is SHIPPED for ambient flora. Color wisps (ticker_wispSpawner) are deliberately per-peer
(product decision, comment in that file). Consequences: (i) mechanism precedent =
fn-cancel-at-the-class, the structural option generalizes it at the driver layer; (ii) the module
sits in the WRONG folder (coop/session/ is not spawn-authority) — the move is owed to the T1 design;
(iii) the probe analysis must JOIN against this module's targets + the npc allowlist (from live
source, at analysis time) so cancelled classes read DONE, not false-live.

### T1-2 · mainGamemode rare weighted rolls — STATUS: NEEDS-PROBE / OPEN-DIVERGES
| roll | consequence | status |
|---|---|---|
| calculateAreaError `RandomBoolWithWeight[0.01]` → **QuitGame** | one peer force-quit to menu | NEEDS-PROBE (HIGH — verify standalone) |
| wakeup / createDream `[0.25]/[0.5]` | dream/story state | NEEDS-PROBE |
| gen_gear `[0.1]`, addHallFood, trySpawnInsomniac `[0.001]` | ambient spawns | NEEDS-PROBE |
| sky/meteor/ground spawn positions (RandomFloat[50000,65000] → BeginDeferredSpawn) | where sky objects land | NEEDS-PROBE |
**Question:** is the client's `mainGamemode` roll-machinery live or already frozen (time_sync /
event zeroing may or may not stop these)? The probe answers it.

### T1-3 · Server minigame type — STATUS: OPEN-DIVERGES
serverbox_sync mirrors IsBroken STATE but not `getRandomServerMinigameType` /
`launchServerMinigame[0.005]/[0.2]` — the break-minigame VARIANT a player faces. Two peers on the
same server can get different variants. Shape: INTENT (fix interaction → host rolls variant).

### T1-4 · Loot content RNG — STATUS: NEEDS-PROBE
`actorChipPile` chipType+count+scatter; `prop_garbageClump` break contents; `trashBitsPile`;
`prop_food` spoilage. Trash-pile IDENTITY is host-auth (element engine) but the CONTENT roll may
still be per-peer. Confirm the host owns the chipType roll.

---

## TIER 2 — world consistency (shared, lower stakes)

### T2-5 · Signal calibration / scramble — STATUS: OPEN-DIVERGES  (the `sv.request` residual)
Sky-signal GENERATION host-auth (console_state_sync); CATCH host-mediated (signal_catch_sync). RESIDUAL:
`dish` calibration drift (RandomFloat losePrec), `coordRadarDish`/`radiotower` periodic `Array_Shuffle`
scramble, `ticker_dishUncalib`/`ticker_disher`. Two peers' dish calibration + radar order diverge.

### T2-6 · Ambient wildlife / flora spawners — STATUS: PARTIAL (2026-07-10 correction)
**DONE-MIRROR already** (ambient_spawner_suppress + host_spawn_watcher pinecone mirror, shipped
pre-audit; the 07-10 audit missed the module): mushroomMaster, mushroomSpawner, pineconeSpawner
(outputs mirrored as keyless PropSpawn). **Deliberately per-peer** (product decision): color wisps
(ticker_wispSpawner). **Still OPEN / NEEDS-PROBE:** ticker_beehiveSpawner, ticker_treeSpawner
(walkingTree), ticker_bushSpawning (growingPlant), ticker_susHoleSpawner,
birchSpawner/autumnLeafSpawner. Real shared actors, low gameplay stakes.

### T2-7 · Seed-replication opportunity — STATUS: SEED-OPP
`garbagePileSpawner` (garbage layout+types), `radiotower.generateGizmos` (décor), `xmaslight`
(pattern) use a SEEDED stream. Replicate the `seed` member host→client → identical rolls, no
suppression. Cheapest exact fix; shape-3.

---

## TIER 3 — cosmetic-local (LEAVE ALONE)
mainPlayer view-bob/footstep/voice jitter; ATV exhaust/backfire fx; ticker_flickerer; firefly_sync
(peer-symmetric but additive→union); xmaslight pattern (unless shared-décor matters); footstep anim
notifies; weatherFogController / newsky / spaceRenderer visual params; grime/greenfire/campfire fx.
No shared consequence → forcing host-auth would be gratuitous (rule-1: not a bug).

---

## PRIOR — already host-authoritative (reference; DONE)
serverbox (IsBroken + breaker-kill), scheduled/story events (event_fire_sync), join-event registry
+ cues, weather rain/snow/fog/redsky/lightning, world time (TimeScale=0), sky/moon, NPC+kerfur+
pyramid AI (allowlist + AI-neutralize + pose one-way), wisp aggro, drone, turbine, garbage-spawner
bodies, sky-signal generation, balance/wallet, email APPEND (host-gate, 2026-07-09). INTENT: alarm,
signal-catch, device-occupancy, comp, kerfur-commands, balance-spend. CONVERGENT-by-design: power
breakers, saved-signals CRDT, window/grime min-wins, email-delete, doors. The "we touch it but each
peer rolls" BUG class = N=1 (email append) → CLOSED.

---

## T1 PRE-REGISTRATION (2026-07-10 — the /qf 15 QUESTION pass converged; written BEFORE the probe build)

**The probe instrument (v9)** — ini-gated `[dev]`, read-only, both roles, resident:
- **(a)** unfiltered BeginDeferredActorSpawnFromClass pass-through census at the existing npc_sync
  silent-pass point — class + location, role-stamped, JOIN-EPISODE-tagged (see gates below);
  per-class summaries inside the join window.
- **(b)** driver-census: native Func-patch on K2_SetTimerDelegate, K2_SetTimer, Delay,
  RetriggerableDelay, DelayFrames, SetActorTickInterval (engine natives, patched at boot →
  client-side install-before-arm holds: a client's world loads AT JOIN, no pre-armed state).
  First-sight-per-class + relaxed atomic counters (pointer-hash flat set; allocation-free steady
  state; NameOf only on first sight); self-instrumented overhead counters; TLS coop-origin latch
  excludes our own machinery's arms; role-stamped; JOIN-EPISODE-tagged.
- **(c)** PERIODIC (~10 min) one-shot censuses of live ticker_base_C descendants: existence +
  tick-enabled + tick interval.
- **(d)** global QuitGame native log (7 sites measured in mainGamemode: 6 ubergraph +
  1 calculateAreaError body; the native patch is global so any other asset's call is also caught).
  Caveat: static-library Func context = the library CDO, NOT the caller → attribution by timing at
  analysis (autonomous smokes contain no user-initiated quits).
- **(e)** PERIODIC paired world class-histogram censuses as per-actor (class, ptr, InternalIndex)
  SETS, diffed actor-wise, wire-explained actors subtracted per-actor via the mirror registries —
  the seam-agnostic backstop (catches native-path spawns; count cancellation impossible).

**Semantics (pre-registered):**
- Positive assertions come from ARMED-state + driver calls + histogram residue — never from
  fire-absence.
- Records inside the world-load episode (world_load_episode latch) are JOIN-EPISODE and EXCLUDED
  from fork arithmetic (fail-safe: a stuck latch over-tags, which can never manufacture a positive;
  a fully-tagged log is visibly broken and the run repeats).
- Suppressed-set JOIN at ANALYSIS time against the LIVE source (kNpcAllowlist +
  ambient_spawner_suppress targets) — nothing hand-copied into the probe.
- Streaming admissibility: (a)/(b)/(d) stream to the log; a crash leaves the last completed (c)/(e)
  pair + streamed records as a PARTIAL census. POSITIVES always admissible; ABSENCE claims require
  the full exposure.
- **Shared-world definition:** the spawned class is a physical world actor a peer can see/interact
  with (creature, prop, vehicle, structure); excludes UI widgets, fx/sound-only, camera/viewmodel-
  local. All 16 T1-1 table rows above classify Y (shared-world) under this definition. Definition
  changes require a CHANGELOG entry — no silent reclassification.
- **Exposure minimum:** >= 1 full in-game day-night cycle on both peers + a standard join, at
  NATURAL 1x time — clock acceleration FORBIDDEN for census runs (it decouples the real-second
  delay axis from the game-clock gate axis). Shorter runs = PARTIAL census; phase-gated rows stay
  CONDITIONAL.
- Under-exposed / event-gated / dormant-on-both-roles rows are CONDITIONAL and EXCLUDED from the
  fork arithmetic (they never count toward "targeted").

**The fork rule (pre-registered):**
- **>= 3 distinct** client-side positively-measured live un-suppressed shared-world spawner classes
  → **STRUCTURAL** (client runs no autonomous world-spawn ticker; allowlist becomes the mirror set).
- **<= 2**, both event-gated rarities → targeted per-class adds.
- Positively-measured < 3 while many rows remain CONDITIONAL → **DEFER** (extend resident
  accumulation) — no default to targeted.
- **Callable-check:** the fork may be called only when >= 2/3 of the pre-registered shared-world
  rows are adjudicated non-CONDITIONAL; otherwise a longer run is mandated. (Exclusion can only
  push toward targeted/DEFER, so a mostly-CONDITIONAL census must not be allowed to call the fork.)
  **Pinned arithmetic:** denominator = the 16 T1-1 rows as listed at pre-registration; the
  adjudication vocabulary is live / armed / confirmed-starved / **DONE-suppressed** (a class the
  analysis-time join marks already-suppressed — e.g. yellowWisp — counts as adjudicated
  non-CONDITIONAL). No analysis-time re-basing of the denominator.
- Orthogonal lanes regardless of fork: any client-side QuitGame liveness → freeze that mainGamemode
  roll lane; the seeded-3 (garbagePileSpawner, radiotower, xmaslight) → seed replication (T2-7).

**Named accepted blind spot:** native-path AND transient-lifetime actors. Static argument: every
audited VOTV spawner is BP-authored; no native transient spawner is evidenced. If the pre-work
dumps reveal a spawn native or re-arm driver OUTSIDE the BeginDeferred family / the 6 patched
natives, the pre-registered response is **AMEND THE INSTRUMENT BEFORE BUILD** (widen (a)/(b)) —
never silently file a KNOWN row under the blind spot.

**RULE-2 pre-commitment:** if the fork lands STRUCTURAL, ambient_spawner_suppress's 4 per-class
cancels dissolve into the structural suppress set in the SAME change (no parallel suppress paths);
the mirror halves (host_spawn_watcher pinecone lane) stay. The module's folder move (out of
coop/session/ into the spawn-authority home) is owed REGARDLESS of the fork.
**PRODUCT-EXEMPTION list (STRUCTURAL must not silently overrule it):** ticker_wispSpawner color
wisps stay PER-PEER (recorded user/product decision in ambient_spawner_suppress.cpp). The
structural design carries an explicit exemption set; extending it is a DESIGN-pass decision with
the user, never an implementation-time default.

**Instrument scope (pinned):** channels (a)-(e) adjudicate **T1-1 and T1-2** (spawns, drivers,
quits, histograms). **T1-3** (server minigame type) is ALREADY adjudicated statically: the roll is
per-interaction, serverbox_sync does not touch it → OPEN by construction, shape = INTENT (no probe
needed). **T1-4** (loot content: chipType/count, garbageClump contents, spoilage) lands as
property writes on existing actors — INVISIBLE to (a)-(e); its adjudication path is its own static
RE (pre-work gate 5: who rolls chipType, at which seam, on which role) + a targeted follow-up
channel or hands-on diff if the RE leaves it open. The tier's probe does NOT silently cover it.

**Static pre-work gates (execute before the probe build):**
1. Dump + classify cockroachMaster, propSpawner_editor, prop_notebook_busterSpawner — BOTH the
   spawn native AND the re-arm driver (a fifth mechanism widens (b) first).
2. Locate gen_gear (not in mainGamemode exports; likely daynightCycle).
3. Extract per-spawner interval/delay literals from the 28 ticker dumps (sizes expected
   fires-per-cycle per row — feeds the callable-check; non-gating for liveness).
4. wakeup/createDream trampoline-offset segment walk (attribution only; consequence observability
   already covered by (a)/(b)/(d); also decide their PLAYER-LOCAL reclassification in the DESIGN
   pass — dreams are personal experience, plausibly not shared world state).
5. T1-4 loot-content RE: who rolls chipType/count (actorChipPile), garbageClump break contents,
   prop_food spoilage — which function, which seam, which role runs it in coop. Adjudicates T1-4
   (outside the (a)-(e) instrument's scope — see Instrument scope above).

---

## CHANGELOG
- **2026-07-10 (late)** — T1 `/qf 15` QUESTION pass CONVERGED (11 rounds — critic "that holds" at
  R11; 3 self-refutations by measurement; R10-R11 pinned the callable-check arithmetic, the
  instrument scope (T1-3 static/INTENT, T1-4 gate-5 RE), and the color-wisp PRODUCT-EXEMPTION).
  PRE-REGISTRATION section added (probe v9 + fork rule + blind spot + RULE-2 fate).
  Fact-base corrections: ambient_spawner_suppress discovered (audit missed it) → T1-1 yellowWisp
  row flipped DONE-MIRROR, T2-6 split (mushroom/pinecone DONE-MIRROR, color wisps product-per-peer);
  mainGamemode ubergraph native census measured (66 spawn pairs, 62 delays, 20 timer arms,
  7 QuitGame, 24 weighted rolls); ticker re-arm heterogeneity measured; 15/17 spawners
  BeginDeferred-based (2 undumped chains gated as pre-work).
- **2026-07-10** — doc created. 3-agent audit → tiers seeded. All T1/T2 rows NEEDS-PROBE/OPEN.
  Next session: live client-roll probe → then `/qf 15` per tier (start T1 structural-suppression).
