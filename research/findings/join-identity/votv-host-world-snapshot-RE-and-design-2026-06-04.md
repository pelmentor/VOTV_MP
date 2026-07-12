# RE + design: host-world snapshot for a fresh-save joiner (VOTV coop)

**Date**: 2026-06-04
**Target**: Alpha 0.9.0-n
**Author method**: 2 parallel Explore recon agents (current snapshot coverage + VOTV save/world
model) over the codebase + `CXXHeaderDump/*.hpp` + `research/findings/`, synthesized.
**User ask (2026-06-04)**: "when client joins with a different save, the objects from host's
world don't get mirrored" → "host world snapshot when client with a fresh save joins so peers
sync and mirror host world perfectly and **not save dependent** — host moved items, repositioned
objects, all transferred to newly joined peers properly."

This is the **world-state half** of [[project-ephemeral-client-host-authoritative-world]]
(inventory is the other half). It is the concrete first epic the user wants dug into.

---

## 1 — The bug, root-caused

The prop connect-snapshot (`prop_snapshot::TriggerForSlot` → PropSpawn packets) sends the host's
**current** transform for every tracked prop, and the joiner **converges by Key**: if it already
has an actor with the same `Aprop_C::Key` (FName @0x02E0), it writes `SetActorLocation/Rotation`
to the host value (`remote_prop_spawn.cpp:204` "de-duping, converging transform to host"). A
fuzzy fallback (`:254`) rekeys a same-class actor within 30 cm; otherwise it spawns fresh.

**Why it breaks for a different/fresh-save client:**
- **Hand-placed level props** carry a Key baked into cooked content → STABLE host↔client → these
  converge fine (a moved hand-placed box does transfer, *if* the client has the same level).
- **Dynamically-spawned props** (items the host dropped, looted from a container, crafted, or a
  natural spawner produced) get a Key minted at Init via `UKismetGuidLibrary::NewGuid()` —
  **session-unique, NOT stable across two different saves**. A fresh client has no actor with that
  Key, and (because the host *moved* it) no same-class actor within 30 cm → the snapshot **spawns
  the host's copy fresh**, while the client's own divergent props (its New-Game defaults / its own
  save's props) **remain** → missing-or-duplicated world, not a mirror.

So per-actor Key-matching is the wrong primitive when the two worlds were authored by **different
saves**. The clean fix (and the user's stated direction) is: make the client's baseline
**deterministic** (a fresh New Game) and **force the host's whole world onto it**, rather than
diff two arbitrary saves by Key.

---

## 2 — What already snapshots on connect (recon: agent A)

Connect edge = `net_pump.cpp:501-516` (HOST, slot ≥ 1), game-thread, in order:

| # | Call | Sends | Covers the user's "moved items"? |
|---|------|-------|------|
| 1 | `prop_snapshot::TriggerForSlot` | every tracked keyed prop's CURRENT loc/rot/phys | **partial** — only Key-matching props; breaks across different saves (§1) |
| 2 | `item_activate::QueueConnectBroadcastForSlot` | local flashlight | n/a |
| 3 | `weather_sync::QueueConnectBroadcastForSlot` | host weather state | yes (weather) |
| 4 | `interactable_sync::QueueConnectBroadcastForSlot` | ALL doors/lights/containers open/on state | yes (those trigger states) |
| 5 | `keypad_sync::QueueConnectBroadcastForSlot` | all keypads (buffer + isAcc/isDeny) | yes (new this session) |
| 6 | `balance_sync::OnClientConnect` | host Points | yes (money) |
| 7 | `item_activate::ReplayPeerStatesToSlot` | other peers' flashlights | n/a |

Props ride `coop::element::Registry` (Tier-3 adoption via the Init POST observer, so save-loaded
props ARE adopted on load). Identity = FName Key for keyed props; eid for non-keyable trash.
NPCs have their own `npc_sync` (spawn/destroy only, **no position stream, no connect snapshot**).

**Gaps with no sync at all today** (agent A): items INSIDE containers (intentional per-machine
carve-out), quest/signal progress, day/time-of-day clock, NPC positions on a late joiner,
sustained vitals/debuffs, base/placed-furniture (if VOTV has it — unconfirmed; likely Aprop_C).

---

## 3 — VOTV save/world model (recon: agent B) — the data a snapshot must carry

The whole persisted world lives in **`UsaveSlot_C`** (0xE41) and is rebuilt into live
`AmainGamemode_C` caches at load. The save-relevant arrays:

```
UsaveSlot_C:
  TArray<Fstruct_save>        objectsData         @0x0300   // every world prop (class+transform+key+state)
  TArray<Fstruct_triggerSave> triggers            @0x04A0   // every trigger (door/light/keypad/switch) state
  TArray<Fstruct_save>        grimeData           @0x0808   // dirt/grime props
  TArray<Fstruct_save>        trashPilesData      @0x0818   // trash piles
  TMap<FName,Fstruct_multiSave>          subLevelData_objects  @0x0CF8  // per-sublevel objects
  TMap<FName,Fstruct_multiSave_triggers> subLevelData_triggers @0x0D48  // per-sublevel triggers
  FIntVector savedtime @0x00B8 ; float Day @0x065C ; float totalTime @0x00E0 ; float moonPhase @0x08A4
  TArray<FName> passEvents @0x00C8 / allEvents @0x00F0 ; Fstruct_task Task @0x0128 ; catchedSignals @0x0098
  int32 Points @0x0090 ; float health @0x0428 / food @0x00E4 / sleep @0x00E8 ...
```

Per-object record (`Fstruct_save`, 0xF8): `class @0x00`, `FTransform transform @0x10`,
`FName key @0x40`, then per-type state arrays (mBool/Float/Int/...). Saved/loaded by each prop's
`Aprop_C::getData()`/`loadData(Fstruct_save)`. Trigger record (`Fstruct_triggerSave`, 0x118): the
same shape + `object_keys/object_index` links + typed arrays; `AtriggerBase_C::getTriggerData()`/
`loadTriggerData()`. **Containers store items as a non-actor `UpropInventory_C` @0x0398** (a list
of `Fstruct_save`), NOT child actors — they only become world actors when the container breaks.

Save write pipeline (`votv-save-path-RE-2026-05-30.md`): `mainGamemode::autosave()` →
`saveObjects()` (walk every `int_save_C` actor, `ignoreSave()` gate, `getData()` →
`objectsData[]`) → `saveTriggers()` → `saveToSlot()`.

**Fresh-New-Game baseline vs progressed save — the DELTA a snapshot must carry**: moved/removed/
added props; container contents; trigger states (open/locked/active); day/time; quest+signal
flags; grime/trash; player stats. Crucially the baseline is **deterministic** (Day 0, empty event
arrays, only hand-placed props/triggers at defaults) — so a fresh client + the host's
objectsData/triggers = an exact mirror, with none of the "diff two arbitrary saves" ambiguity.

---

## 4 — Design (RULE-1, MTA server-authoritative world)

**Principle**: don't reconcile two divergent saves by Key. Make the client a **deterministic
fresh baseline** and **apply the host's authoritative world records** on top. This is exactly the
VOTV load path (`loadData`/`loadTriggerData`) driven over the wire instead of from a file — which
is NOT "download the .sav" (the user's rejected idea); it's streaming the live authoritative
records and letting each client's own engine rebuild, the MTA shape.

### Phase A — Client fresh-boot in the host's GameMode (foundation)
- On connect (client role), start a **New Game** under the hood instead of loading a slot; never
  persist the client's save (redirect/suppress the client-role save writes). We already hook
  `LoadStorySave` + `getSavePrefix` (story-load-as-sandbox fix, HEAD fe4f229) — extend that.
- **Mechanism (RE'd 2026-06-04, engine.cpp:207-290 LoadStorySave):** LoadStorySave =
  `GameplayStatics::LoadGameFromSlot(SlotName,0)` → save obj → `GameInstance::setSaveSlotObject(save,
  SlotName)` + set `mainGameInstance.loadObjects=1` + `ApplyGameModeFromSlot` + travel `open
  untitled_1`. **Fresh-boot = the SAME machinery with a BLANK save instead of a disk-loaded one:**
  `GameplayStatics::CreateSaveGameObject(saveSlot_C)` → blank `UsaveSlot_C` → setSaveSlotObject(blank)
  → set the host's GameMode → travel untitled_1. The blank saveSlot IS the deterministic New-Game
  baseline. (Confirm CreateSaveGameObject + whether `loadObjects=1` must be 0 for a fresh boot so it
  doesn't try to restore empty arrays — likely just leave the blank save's empty arrays.)
- Boot in the **host's** `mainGamemode.GameMode @0x01E1` (sent on the connect handshake), never a
  default. Reuse `getSavePrefix(0..7)` driven by the host-sent mode (already wired in
  ApplyGameModeFromSlot, just feed the host mode instead of a slot prefix).
- *Still to RE:* where the client autosave/save writes happen (to suppress so nothing persists);
  whether a fresh client streams the same sublevels as the host's progressed save.

> **IMPLEMENTED + TESTED 2026-06-04.** `ue_wrap::engine::StartFreshGame(storyMode)` built
> (CreateSaveGameObject(saveSlot_C) blank save → setSaveSlotObject → loadObjects=1 →
> ApplyGameModeFromSlot → open untitled_1), gated by the `fresh_boot` ini in
> `BootStorySaveBlocking`. **Test (host=s_may2026, client `fresh_boot=1`):**
> - ✅ **No intro stall** — `created BLANK saveSlot` → `open untitled_1` → `in gameplay
>   (mainPlayer @ -37695,69978,6443)` in ~2 s, ONE open, no re-open loop. (The user confirmed I'd
>   conflated the day-0 "intro" with the startup menu — there is no blocking gameplay intro.)
> - ✅ **Host world MIRRORS onto the fresh client** — host snapshot sent 88 props + 42 lights +
>   56 containers + 14 keypads; the client converged them (`OnSpawn ... already resolves to live
>   actor -- converging transform to host`). The snapshot is tiny + clean (NOT a flood).
> - ✅ **Memory-stable ~4 GB across 3 runs (after a one-off first-run spike).** Run 1 ballooned to
>   9.7 GB and FAILED the 8 GB cap, but runs 2+3 were 4.07/4.06 GB (≈ the host's 3.8 GB) AND run 2
>   actually mirrored MORE (a later full drain of **2313 props** converged, not just the initial
>   88). So the run-1 spike was a **one-off transient** (a VOTV fresh-game streaming/generation
>   spike, or 2-instances-on-one-machine contention during the first cold boot), **NOT systematic
>   and NOT the snapshot** (88–2313 props is cheap; more spawns in run 2 used LESS memory).
> - **Correction:** I first theorized the player spawned "far from base" and a teleport would
>   constrain streaming — WRONG. КПП = (-37695,69978,6420) (sdk_profile.h:596) and the fresh game
>   spawns the player AT КПП, so the play-scenario KPP teleport is a **no-op**; it did NOT cause the
>   run-2/3 memory drop. The retry-teleport fix (wait for the player vs a one-shot that fired before
>   the late fresh-boot spawn) is still a valid robustness improvement, just not the memory cause.
>
> **STATUS: fresh-boot is VIABLE + validated** (gameplay, mirror, memory). `StartFreshGame` KEPT,
> gated `fresh_boot=0`. **Remaining before on-by-default:** (1) suppress the client's save WRITES
> so `coop_client_fresh` never persists to disk (the ephemeral half); (2) boot in the HOST's
> GameMode via the connect handshake (currently hardcoded story); (3) the user's hands-on confirm
> the world VISUALLY matches + that the run-1 memory spike doesn't recur on their real 2-machine
> setup (where 9.7 GB on one machine may be fine anyway — the 8 GB cap is the smoke's 2-on-1 limit).

### Phase B — Comprehensive host-world snapshot (the meat)
Drive the client's world to the host's authoritative records, reusing VOTV's own load verbs:
1. **Objects (`objectsData[]`)**: host enumerates its world props (the existing registry already
   adopts save-loaded props) and sends each as a record {class, transform, key, type-state}. Client
   applies: resolve-or-spawn by class, then `loadData(Fstruct_save)` so the prop restores to the
   host's exact transform + per-type state. Because the client baseline is fresh, **the client must
   first DROP its own non-baseline props** (anything not in the host set) — a reconcile/sweep step
   (this is what's missing today and why different-save mirroring fails).
2. **Triggers (`triggers[]`)**: doors/lights/keypads/switches via `loadTriggerData(Fstruct_triggerSave)`
   — supersedes the per-field interactable/keypad snapshots with the game's own restore (or keep the
   per-field ones for live edits and use loadTriggerData only for the initial bulk). Decide one
   authority to avoid double-apply.
3. **World scalars**: day/time (`savedtime`/`Day`/`totalTime`/`moonPhase`), quest/signal arrays,
   Points (already), stats — push host→client on connect; most are single fields/arrays.
4. **Grime/trash** (`grimeData[]`/`trashPilesData[]`): same record path as objects.
5. **Sublevels** (`subLevelData_*`): per-streamed-level object/trigger maps — handle once the active
   level's objects work (the bulk is the main level).

### Phase C — Stay-in-sync (live, post-connect)
The per-tick polls already cover doors/lights/containers/keypads/weather/balance + the prop
PropPose stream for held/moving props. Gaps to add later: a prop **move/teleport** edge for props
the host repositions without holding (drop at a new spot), NPC position stream, quest/time edges.

### What to send on the wire
A new reliable **WorldSnapshot** lane: chunked records (objects, then triggers, then scalars),
each record = the `Fstruct_save`/`Fstruct_triggerSave` fields we need (class name as WireKey-ish
string + transform + key + the typed state arrays we choose to carry). Chunk like
`prop_snapshot` already does (per-tick drain) to avoid a connect-burst stall. The client applies on
the game thread via the engine load verbs.

---

## 5 — First concrete increment (recommended) + how to verify autonomously

**Increment 1 — "host-moved props mirror to a fresh client" via reconcile + loadData**, WITHOUT
the full fresh-boot yet, to de-risk Phase B in isolation:
- Add a host **full-object enumeration** (class+transform+key+core state) to the connect snapshot
  (extends `prop_snapshot`), and a client **reconcile**: sweep client props whose Key is not in the
  host set (the divergent ones), then apply host records by resolve-or-spawn + transform set.
- **Autonomous verification is the hard part** (the smoke loads the SAME save on both, so there is
  no divergence to test). Options: (a) a probe that, on the host, programmatically MOVES a known
  prop a few meters after connect and logs whether the client's matching actor follows (tests the
  move-edge path); (b) a temporary "client loads a DIFFERENT slot" smoke variant to create real
  divergence and assert the client world converges to the host (best signal, needs a 2nd save in
  the test rig). Recommend building (b) into `tools/mp.py` as a `--client-save` override so the
  divergence case is reproducible in CI-style smokes.

**Foundation-first alternative**: do Phase A (fresh-boot) first since everything else assumes it;
but fresh-boot is hard to verify without a hands-on (the client's whole world changes), so it's the
one piece that genuinely needs the user back. Increment 1 (reconcile + loadData) is more
autonomously testable and delivers the user's literal ask (moved/repositioned objects transfer).

---

## 6 — Load-bearing unknowns to RE before coding each phase
- **`Aprop_C::loadData(Fstruct_save)` callable by us? — RESOLVED (mostly).** `ParamFrame::SetRaw`
  (call.h:33) writes raw bytes at any param's reflected offset, so loadData/loadTriggerData ARE
  callable by hand-building the blob (FTransform is POD; a TArray needs a {Data,Num,Max} header over
  a buffer we keep alive across the Call). BUT it's fiddly + risky (TArray lifetime, exact struct
  layout). **KEY REFRAME:** the user's literal ask (moved/repositioned objects = TRANSFORM) does NOT
  need loadData — the existing prop path already applies transform via `SetActorLocation/Rotation`.
  loadData is only needed to restore per-TYPE state (container contents, food growth, complex door
  state). So increment 1 = **reconcile + cross-save identity**, and per-type state via loadData is a
  later, optional layer. Don't build the Fstruct_save blob unless/until a feature needs type-state.
- **Reconcile safety**: dropping the client's divergent props must not nuke baseline level geometry
  or the client's own player/puppet — scope the sweep to `int_save_C`/keyed Aprop_C only, mirror the
  existing wire-suppression lists.
- **Fresh-boot**: the exact New-Game entry UFunction + the save-write suppression points.
- **Container contents**: whether to ride objects (if items materialize) or sync `UpropInventory_C`
  records (the inventory epic) — keep OUT of phase B; it's the inventory half.
- **Sublevel streaming**: does a fresh client stream the same sublevels as the host's progressed
  save, or does progress unlock levels? (affects which `subLevelData_*` apply).

---

## 7 — Cross-refs
- [[project-ephemeral-client-host-authoritative-world]] (the umbrella direction; inventory half).
- [[project-coop-interactable-state-sync]] (trigger-state snapshots this builds on).
- `research/findings/props-lifecycle/votv-aprop-lifecycle-RE-2026-05-24.md` (prop Init/Key/getData-loadData).
- `research/findings/saves/votv-save-path-RE-2026-05-30.md` (save write pipeline).
- `CXXHeaderDump/{prop,prop_container,triggerBase,struct_triggerSave,saveSlot,mainGamemode}.hpp`.
