# Mirror-identity JOIN-WINDOW race — a PROBLEM CLASS (not one object's bug)

**Recognized 2026-06-24 (user architectural instinct). Rule-of-three MET 2026-06-24 (3 working instances) ->
the shared layer is now a clean EXTRACT candidate.** This is a cross-cutting class doc. Discipline: **make each
instance WORK first, generalize AFTER N>=3** -- done; the extract DESIGN is below, **design-review FIRST, then
refactor** (this is a refactor of THREE working+verified instances; regress is unacceptable).

## The class

Any entity that reaches a joining client via **TWO channels at once** -- (a) the transferred SAVE the client
loads, and (b) the host's connect-replay BROADCAST -- AND lacks a **stable cross-peer identity key** to tie
the two together, exhibits the SAME symptoms when the host mutates it DURING the join window
[save taken ... client load-100%]:

1. **WINDOW DUP** -- the save-channel delivers one state, the broadcast delivers another; with no exact key to
   reconcile them, the client keeps BOTH.
2. **MATERIALIZE-FAIL variants** -- the broadcast form may partially spawn (e.g. the kerfur fresh-spawn
   floating-camera) because the in-window adopt path can't run.
3. **IDENTITY-COLLISION** -- with no exact key, binding falls to POSITION-FUZZY; in a cluster the WRONG
   instance claims another's binding -> an orphan that never re-binds.

**The cure:** a **save-time EXACT-position key** -- both peers derive the same identity from the one
transferred save -> exact match (no fuzzy), retried at post-quiescence (the load-tail race). Exact position is
unique per instance -> kills the dup (1) AND the cluster collision (3).

## Instances (N=3 -- rule of three MET)

| | (1) piles `actorChipPile_C` | (2) kerfur fuzzy-gate | (3) kerfur forward off->active |
|---|---|---|---|
| symptom | save-time WINDOW DUP (host moves a pile in-window) | IDENTITY-COLLISION (a keyless kerfur steals a neighbor's actor) | WINDOW DUP (off-in-save kerfur the host turns ON in-window) |
| cross-peer key | KEYLESS -> save-time position | own `Aprop_Key` (keyless-never-steals gate) | save-time position carried on the **npc EntitySpawn** |
| channel the key rides | the prop snapshot (`matchX/Y/Z`) | n/a (anti-collision gate, no carry) | **npc EntitySpawn** (NOT KerfurConvert -- it fails mid-join) |
| reconcile | `pile_reconcile::SweepReconcileSaveTimeTwins` + `TryDestroyTwin` | `kerfur_prop_adoption` / `remote_prop_spawn` Gap-I-1 key gate | `kerfur_reconcile::SweepReconcileSaveTimeKerfurs` |
| safety | exact 1cm key + chipType + `>50%` valve (denominator = ALL live piles) | own-key != pending-key -> never steal | exact 1cm key + uniqueness + ambiguous-skip + non-mirror gate (**NO ratio valve** -- see lesson) |
| status | **VERIFIED + PUSHED** (`960e4650`) | **VERIFIED** (`8c96d7aa`) | **VERIFIED hands-on 17:23** (`7c67b00b`) |

## EXTRACT DESIGN (proposed -- DESIGN-REVIEW PENDING, do NOT code before sign-off)

Target: a shared `coop/mirror_identity_reconcile.{h,cpp}` that the three instances reuse via parameters/hooks,
without changing their verified behavior. Extract the COMMON core; keep the class-specific seams as hooks.

### COMMON core (parameterized, into the shared module)
- **Save-time exact-position capture** -- `g_blob*Xforms[slot][hostEid] = P_live` at the blob instant, with
  **self-seed of eid==0** (mint inline, idempotent). Today: `g_blobPileXforms` + `CollectTrackedPileTransforms`
  and `g_blobKerfurXforms` + `CollectTrackedKerfurTransforms` -- ONE generic captor parameterized by a **class
  predicate** (`IsChipPile` / `IsKerfurPropClass`) + the self-seed key (keyless `L""` / the real `Aprop_Key`).
- **Carry on the delivering broadcast** -- `hasMatchPos` + `matchX/Y/Z`, finite-validated on receipt. The
  PAYLOAD differs (PropSpawn vs EntitySpawn) -> the carry is a small per-channel stamp helper, but the
  finite-validate + the `GetSaveTimePosForEid`-style lookup are shared.
- **Post-quiescence reconcile sweep** -- `HasLoadTailQuiesced`-gated, **bracket-INDEPENDENT** (driven by a
  per-class client poll, NOT the pile divergence bracket -- the v1.1 / SnapshotBegin-lost lesson). FRESH
  GUObjectArray walk (the local save form is NOT key-resolvable in remote_prop maps -> walk is mandatory).
- **Position-uniqueness anti-collision** -- exact 1cm key; `ambiguous(>1)->skip` fail-safe; exact-claims-lock-
  before-fuzzy. Shared primitive.
- **The GUObjectArray-walk-for-save-loaded-forms primitive** (the local form not in key->actor maps).

### CLASS-SPECIFIC seams (stay as hooks/params -- do NOT fold into the core)
- **Transition model (the big seam):** pile stays one class (object<->object); kerfur CHANGES class
  active<->object (forward off->active AND reverse active->off). The reconcile's "what is the stale form vs the
  authoritative form" is a per-class hook, NOT shared code.
- **Key type:** pile keyless / pile chipType tie-break / kerfur `Aprop_Key` / kerfur `KerfurId` post-carry. A
  per-class identity descriptor (the secondary tie-break is a param: chipType / none / class).
- **Delivery channel:** kerfur = TWO sources (npc EntitySpawn carries the key + prop channel) vs pile = one
  channel. The channel the key rides is a param.
- **Retire trigger:** kerfur = npc-adopt-with-hasMatchPos (arm) -> quiescence sweep; pile = proxy-spawn
  twin-destroy. Per-class hook.

### CRITICAL LESSON — the `>50%` ratio valve is DENOMINATOR-DEPENDENT (do NOT port blindly)
This is EXACTLY the bug we just fixed (scope A v1->v1.1, 17:06). In `pile_reconcile` the valve denominator is
**ALL live native piles** (claimed + unclaimed) -> `>50%` genuinely flags a racing bracket. In
`kerfur_reconcile` the denominator is **ONLY non-mirror local off-props** (correctly-adopted ones are mirrors,
excluded) -> the denominator IS the stale set, so retiring the lone stale form is always 100% -> the valve
FALSE-ABORTED the correct case (17:06: "sweep-retire ABORTED -- 1 of 1 (>50%)"). **The abstraction must NOT
codify this mis-port.** The valve is either (a) PARAMETERIZED by "what counts as the denominator" (the full
in-universe set vs the stale-candidate set), or (b) a class-specific HOOK, NOT shared core. The kerfur racing
mode is "form not loaded -> 0 matches" (safe), never over-match -- so kerfur needs no ratio valve at all; its
safety is the exact key + uniqueness. A blind shared valve would re-introduce the 17:06 bug.

### MOVE-SCENARIO headroom (the likely 4th instance -- design with slack, do NOT block on it)
17:23 surfaced a SEPARATE, not-yet-diagnosed bug: a pile the user MOVED in the connect window appears to dup
(`docs/piles/` territory; possibly the 4th mirror-identity instance -- object changes POSITION in-window, not
just save-dup/forward). The L1 fix was the save-time-pos DUP; a pile spawned/moved AFTER the blob has no
`g_blobPileXforms` entry -> no key -> live-pose path -> can dup. **Design the core so a MOVE scenario fits:** the
identity key must survive a position change in-window (the save-time key is frozen at blob; a form that moves
post-blob needs either a re-capture or an identity that isn't its current position). Keep the captor + the key
descriptor open to "the form's identity is not necessarily its live position." Do NOT close the design to
object<->object same-position only.

### Extract acceptance (gate before declaring done)
1. The 3 verified instances STILL PASS their tests after extract (L1 pile-dup; kerfur fuzzy-gate collision;
   kerfur forward off->active 17:23). No behavior change.
2. Common core extracted (capture + carry + post-quiescence sweep + position-uniqueness + ambiguous-skip +
   GUObjectArray-walk); class seams stay hooks (transition, key-type, channel, retire-trigger).
3. The valve is NOT blind-shared -- parameterized by denominator OR a class hook (the 17:06 lesson encoded).
4. The design accepts the MOVE scenario (pile-move-in-window fits when diagnosed).
5. No regress to the SEPARATE classes: the reverse follow-ghost (retire-AUTHORITY, a different class -- do NOT
   pull into mirror-identity), OBS-2 (point arg-bug), the quiescence-gate (a shared primitive that MAY be
   reused). These are not mirror-identity; do not entangle them.

## NOT in this class (keep separate)
- **Reverse follow-ghost** (`docs/kerfur/05`): a retire-AUTHORITY problem (a stale local ACTIVE twin the ghost
  sweep missed), not a save-vs-broadcast identity race. Different layer.
- **OBS-2** (`docs/kerfur/06`): a point arg-slot bug. Not systemic.
- **Materialize-fail / camera** (symptom 2, `docs/kerfur/04` pending): a per-entity spawn-path concern.
