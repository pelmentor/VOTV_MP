# RE: VOTV Storage Lockers ("ящички для хранения") — Open/Close Door Sync Plan

**Date**: 2026-05-27
**Target**: Alpha 0.9.0-n
**Method**: CXXHeaderDump static analysis + cross-reference with
`votv-storage-container-spawn-RE-2026-05-25.md`,
`votv-doors-and-lightswitches-RE-2026-05-25.md`,
`votv-aprop-lifecycle-RE-2026-05-24.md`. IDA decompile attempted — locker
UFunctions are all BP-defined (no native bodies in the .exe), as expected for
pure-BP `Aprop_C` / `AActor`-only subclasses; the actual lid/door animation
runs inside ProcessEvent-dispatched UFunctions and the engine's `UTimelineComponent`
tick. Trigger key for the research: user verbatim "ящички для хранения" — the
real wall-mounted personal lockers and the closed-lid prop containers (cabinets,
drawers, suitcases, etc.) which both expose a player-operated open/close state.

---

## Section 0 — Scoping the term "locker"

The Russian "ящичек для хранения" maps to two distinct UE4 class families in
VOTV. Both are in scope because both have a player-operated open/close door
animation that must reflect on the peer:

1. **`Alocker_C` family** — the dedicated wall-mounted personal locker with
   a single hinged door, a `UTimelineComponent` lid animation, and a
   `bool opened` state field. This is what most players literally mean by
   "locker". Inherits `AActor` (NOT `Aprop_C` — it is a fixed-place world
   actor with no physics-grab path). Has its own `Open(bool opened)` UFunction.
2. **`Aprop_container_C` family** — the closed-lid storage prop family
   (wardrobe, fileCabs, drawers, ffDrawers, desk, bedsidetable, suitcase,
   crate, oldBarrel, cardboardBox, etc. — 32+ variants). Inherits `Aprop_C`.
   Opens via the `openContainer()` UFunction. Lid animation is BP-driven (the
   header shows no Timeline member — implementation hides inside the
   ubergraph or is data-driven through `Audio` + a static mesh swap).

There is also a third, narrower family worth noting (out of immediate scope but
flagged for completeness):

3. **`Aprop_safeDoor_C` + `Aprop_swinger_C` family** — physics-hinged doors on
   safes, transformers, the village gate, the crematorium. Has its own
   `Open(bool Damage)` / `Close()` UFunctions, `bool opened`, `bool Locked`,
   plus a `UPhysicsConstraintComponent` for the actual hinge. These are
   adjacent to the regular `Adoor_C` family (already covered in
   `votv-doors-and-lightswitches-RE-2026-05-25.md`) and are NOT what the user
   asked about (the user said "lockers", not safes/gates), so they're listed
   for completeness in Section 7 but the wire-protocol design is in Section
   8 for the two primary families only.

---

## Section 1 — Class Hierarchy Summary

### 1.1 Alocker_C family (the dedicated locker)

**`Alocker_C` : `AActor`** — file
[locker.hpp:4](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/locker.hpp#L4),
size 0x2B8.

Direct `AActor` subclass — does **NOT** inherit `Aprop_C` (so the
`Aprop_C::Init` POST observer does NOT fire for lockers) and does **NOT**
inherit `Aactor_save_C` (no automatic save chain). Pre-placed level actor.

Subclasses (all paper-thin):
- **`Alocker_death_C` : `Alocker_C`** —
  [locker_death.hpp:4](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/locker_death.hpp#L4),
  size 0x2B8. Adds only two `intComs_*` UFunction overrides; no new fields.
  Behaves like base for our purposes.
- **`Alocker_personal_C` : `Alocker_C`** —
  [locker_personal.hpp:4](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/locker_personal.hpp#L4),
  size 0x2B8. Empty subclass. Pure variant.

Adjacent classes (NOT player-interactable as lockers, but related):
- **`AlockerCorpse_C` : `AActor`** —
  [lockerCorpse.hpp:4](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/lockerCorpse.hpp#L4).
  A pile of 21 `UChildActorComponent* gib*` members. This is the "corpse
  falls out of the locker" story prop — visually a heap, NOT openable. NOT a
  locker.
- **`Alockerguy_C` : `AActor`** —
  [lockerguy.hpp:4](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/lockerguy.hpp#L4).
  A `UTimelineComponent A` + a `UStaticMeshComponent`. The story
  "guy hiding in a locker" easter egg. Driven by `mainGamemode.ticker_lockerhead()`
  ([mainGamemode.hpp:534](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/mainGamemode.hpp#L534)).
  NOT a locker the player opens; out of scope.
- **`Atrigger_lockerLooker_C` : `AtriggerBase_C`** —
  [trigger_lockerLooker.hpp:4](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/trigger_lockerLooker.hpp#L4).
  A peek-from-locker AI trigger volume. Not a locker.
- **`Aprop_plankLockLocker_C` : `Aprop_C`** —
  [prop_plankLockLocker.hpp:4](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/prop_plankLockLocker.hpp#L4).
  Wooden planks nailed across a locker. Holds `Alocker_C* locker @0x0370`
  back-pointer. Removed via crowbar/damage. When removed, the locker becomes
  openable (the locker's `blockedBy` int counts plank refs). This IS
  coop-relevant as a destruction event (already covered by the existing
  `PropDestroy` wire packet from `Aprop_C` lifecycle), and removing the last
  plank unblocks the locker (see Section 5d).
- **`AbatchSpawner_waterlocker_C` : `AbatchSpawner_C`** —
  [batchSpawner_waterlocker.hpp:4](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/batchSpawner_waterlocker.hpp#L4).
  Static-mesh array batch spawner; not a locker, just a level decoration
  named for the location.

### 1.2 Aprop_container_C family (closed-lid storage props)

**`Aprop_container_C` : `Aprop_C`** — file
[prop_container.hpp:4](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/prop_container.hpp#L4),
size 0x42A.

Inherits `Aprop_C` (size 0x363 base). Therefore inherits `Aprop_C::Init` /
`Aprop_C::Key @0x02E0` / save persistence semantics (via the `getData`/
`loadData` chain in `Aprop_C`). All 32+ concrete variants:

| Class | Parent | Adds |
|---|---|---|
| `Aprop_container_C` | `Aprop_C` | base (see Section 2.2) |
| `Aprop_container_wardrobe_C` | `Aprop_container_C` | `obstacle @0x0430` (nav-blocker child) |
| `Aprop_container_drawers_C` | `Aprop_container_C` | `obstacle @0x0430` |
| `Aprop_container_fileCabs_C` | `Aprop_container_C` | `obstacle @0x0430` |
| `Aprop_container_ffDrawers_C` | `Aprop_container_C` | `obstacle @0x0430` |
| `Aprop_container_desk_C` | `Aprop_container_C` | `obstacle @0x0430` |
| `Aprop_container_minifridge_C` | `Aprop_container_C` | `obstacle @0x0430` |
| `Aprop_container_sbox_C` | `Aprop_container_C` | `obstacle @0x0430` |
| `Aprop_container_barrel_C` | `Aprop_container_C` | `obstacle @0x0430` |
| `Aprop_container_bedsidetable_C` | `Aprop_container_C` | empty |
| `Aprop_container_suitcase_C` | `Aprop_container_C` | empty |
| `Aprop_container_itemBox_C` | `Aprop_container_C` | empty |
| `Aprop_container_oldbox_C` | `Aprop_container_C` | empty |
| `Aprop_container_oldbox_kerfurJoints_C` | `Aprop_container_oldbox_C` | empty |
| `Aprop_container_crate_C` | `Aprop_container_C` | empty |
| `Aprop_container_oldCrate_C` | `Aprop_container_C` | empty |
| `Aprop_container_oldCrate_S_C` | `Aprop_container_oldCrate_C` | empty |
| `Aprop_container_oldBarrel_C` | `Aprop_container_C` | empty |
| `Aprop_container_oldBarrel_notap_C` | `Aprop_container_oldBarrel_C` | empty |
| `Aprop_container_oldBarrel_vert_C` | `Aprop_container_oldBarrel_C` | empty |
| `Aprop_container_giftbox_C` | `Aprop_container_C` | empty |
| `Aprop_container_gembox_C` | `Aprop_container_C` | empty |
| `Aprop_container_fishman_C` | `Aprop_container_C` | empty |
| `Aprop_container_singularity_C` | `Aprop_container_C` | empty |
| `Aprop_container_pbasket_C` | `Aprop_container_C` | empty |
| `Aprop_container_wboot_C` | `Aprop_container_C` | empty |
| `Aprop_container_wbedtable_C` | `Aprop_container_C` | empty |
| `Aprop_container_garbagebin2_C` | `Aprop_container_C` | empty |
| `Aprop_container_bin2_C` | `Aprop_container_C` | empty |
| `Aprop_container_cardboardBox_C` | `Aprop_container_C` | empty |
| `Aprop_container_cardboardBox2_C` | `Aprop_container_C` | empty |
| `Aprop_container_cardboardBox_2x2x2_C` | `Aprop_container_cardboardBox_C` | empty |
| `Aprop_container_cardboardBox_4x2x2_C` | `Aprop_container_cardboardBox_C` | empty |
| `Aprop_container_cardboardBox_4x4x2_C` | `Aprop_container_cardboardBox_C` | empty |
| `Aprop_container_cardboardBox_4x4x4_C` | `Aprop_container_cardboardBox_C` | empty |
| `Aprop_container_cardboardBox_8x4x4_C` | `Aprop_container_cardboardBox_C` | `obstacle @0x0430` |
| `Aprop_container_cardboardBox_8x8x4_C` | `Aprop_container_cardboardBox_C` | `obstacle @0x0430` |
| `Aprop_container_cardboardBox_8x8x8_C` | `Aprop_container_cardboardBox_C` | `obstacle @0x0430` |
| `Aprop_container_orderbox_C` | `Aprop_container_C` | adds `player_use` + `broken` overrides |
| `Aprop_arirContainer_C` | `Aprop_container_C` | adds `slapperSummoner` |
| `Aprop_arirContainer_v2_C` | `Aprop_arirContainer_C` | adds alarm + `slapper` |

Note: `Aprop_arirContainer_in_C` is the INSIDE-volume actor (different class,
inherits `Aprop_C` directly — it is what spawns when the player enters the
container, NOT an openable storage actor). Out of scope.

Out-of-family containers (storage but NOT `Aprop_container_C` lineage,
NOT player-openable in the same lid sense):
- `Aprop_openContainer_*` family (bucket, plate, bowl, basket, scrapbox,
  wastebasket, fridgeshelf*, ffWdL, ffWdLS, easterbasket) — inherits
  `Aprop_openContainer_C`. These are ALWAYS open (no lid). Items physically
  rest inside; no open/close UFunction. Out of scope for door sync.
  Already partially handled by the existing garbage-sync wire layer.
- `Aprop_inventoryContainer_player_C` — the player's pockets (per-peer
  private per `[[project-coop-inventory-private]]`). Never world-rendered as
  an openable actor. Out of scope.
- `Aprop_inventoryContainer_atv_C` / `_drone_C` — vehicle storage.
  Distinct future-scope (vehicle replication).
- `Aprop_safe_C` family — bank-style safes; door is a separate
  `Aprop_safeDoor_C` swinger. See Section 7.

### 1.3 Identity / save persistence

- **`Alocker_C`** has neither a `Key` field nor `getData`/`loadData` methods.
  It inherits `intComs_gamemodeMakeKeys` via the world-actor interface but
  does NOT override it; combined with the absence of save methods this means
  **`Alocker_C` open state is NOT save-persistent**. Lockers always start
  CLOSED on map load / save reload. However, the locker DOES carry a
  `FName triggerKey @0x0288` (the FName of an external trigger to fire when
  opened — see Section 5d) which is set at level placement and is stable
  per actor (level-deterministic, same on both peers). **For coop identity,
  we use the actor's PathName** (UObject::GetPathName) because trigger keys
  may collide across multiple lockers in the same level. The actor's full
  PathName (e.g. `Persistent_Level.locker_C_42`) is what UE assigns at
  level cook time and is identical on both peers because both peers load the
  same .umap. This is the same pattern the Phase 5D door RE doc proposes for
  `Adoor_C::Key` (level-stable FName), differing only in source: doors get a
  cooked `triggerBase.Key`, lockers get the cooked UObject path.
- **`Aprop_container_C`** inherits `Aprop_C::Key @0x02E0` — a NewGuid-on-spawn
  string-FName, save-persistent via `Aprop_C::loadData` restoring the saved
  Key. Phase 5S0 Inc1+Inc2 snapshot-on-connect + Init POST coverage already
  keys these actors via the existing PropSpawn pipeline. Open state is
  transient (not save-persistent — see Section 4).

---

## Section 2 — State Fields + Offsets

### 2.1 Alocker_C state (file `locker.hpp`)

```
0x0228  UStaticMeshComponent* stair                 — internal step mesh
0x0230  UStaticMeshComponent* door                  — the hinged door mesh
0x0238  UBoxComponent*        Box                   — interaction trigger volume
0x0240  UAudioComponent*      Audio                 — open/close sound
0x0248  UArrowComponent*      Axis                  — hinge axis marker
0x0250  UStaticMeshComponent* StaticMesh            — the locker body
0x0258  USceneComponent*      DefaultSceneRoot
0x0260  float                 a_a_<guid>            — Timeline 'A' alpha (0..1)
0x0264  TEnumAsByte<ETimelineDirection::Type> a__Direction_<guid>
0x0268  UTimelineComponent*   A                     — the open/close timeline
0x0270  bool                  opened                — *** canonical state ***
0x0271  bool                  head                  — head-poking-out state
0x0274  int32                 blockedBy             — ref-count of plank-locks (0 = unblocked)
0x0278  bool                  debugPeek
0x0279  bool                  noMeat
0x0280  AActor*               triggerOnOpen         — external trigger to fire when opened
0x0288  FName                 triggerKey            — fallback key lookup for triggerOnOpen
0x0290  AmainGamemode_C*      GameMode
0x0298  TArray<Aprop_plankLockLocker_C*> BlockedbyPlanks
0x02A8  TArray<FName>                    BlockedbyPlanks_Keys
```

**Canonical state field: `bool opened @ 0x0270`.** True = door fully open,
false = closed. Timeline `A` drives the lid animation; `a_a_*` is its
alpha (0.0 closed, 1.0 open).

`head` (0x0271) is a separate boolean for "locker head pokes out" (the
sciency horror element where the locker briefly opens to reveal a face).
Independent of `opened`. Driven by `npcOpen()`. Marked out-of-scope for
the door-sync feature — that's an NPC scare effect, not a player open.

`blockedBy` (0x0274) is the ref-count of plank-locks remaining. If
`blockedBy > 0`, the locker CANNOT open — the `player_use` BP body must
gate-check this. Removing the last plank decrements to 0; only then is
the locker openable.

### 2.2 Aprop_container_C state (file `prop_container.hpp`)

```
0x0370  UAudioComponent*           audio_locked       — locked-jiggle sound
0x0378  UBoxComponent*             usableVolume
0x0380  UAudioComponent*           Audio              — open/close sound
0x0388  UArrowComponent*           Spawn              — item spawn anchor
0x0390  UBoxComponent*             Overlap
0x0398  UpropInventory_C*          propInventory      — item storage component
0x03A0  FDataTableRowHandle        lootEntry
0x03B0  FName                      spawnLoot
0x03B8  TArray<float>              massData
0x03C8  TArray<float>              volumeData
0x03D8  TArray<FName>              nameData
0x03E8  bool                       Locked             — keypad/quest lock state
0x03EC  FVector                    Volume
0x03F8  bool                       fridge             — visual variant flag
0x03FC  float                      overlapDelay
0x0400  USoundBase*                sound_open
0x0408  USoundBase*                sound_close
0x0410  FVector                    velLinear          — physics carry-over for thrown contents
0x041C  FVector                    velAngular
0x0428  bool                       accurateInertia
0x0429  bool                       ignoreImpulseThings
```

**CRITICAL FINDING: no `bool opened` field on `Aprop_container_C`.**

The container has `Locked` (lock state — keypad/quest gating) but no
canonical OPEN field. The lid animation appears to be a transient effect:
the `openContainer()` UFunction is called, the lid pops open visually, the
UI for the inventory shows, and then the lid pops closed when the UI is
dismissed (or possibly stays open while the UI is up). The
`UTimelineComponent` driving the lid is NOT a declared member field —
it must be inside the BP graph or driven via a different mechanism
(possibly the Audio component's onFinish, or the inventory UI's
onShow/onHide). **No persistent "open/closed" state on the container at
all.** Implication: for `Aprop_container_C` we cannot sync a steady-state
open/closed bool — we sync the TRANSIENT EVENT (`openContainer()` fired).

This means: `Aprop_container_C` open-sync = "the other peer's container
plays its open animation at the same moment". Same shape as a one-shot
lid-pop event — animation plays locally on receive. Same pattern as
LightningStrike (a discrete event packet).

`bool Locked @ 0x03E8` is a separate concern (keypad/quest gating, NOT
the lid state). Whether to sync `Locked` is in scope for the lock-state
wire packet design (Section 8.3), not the open-event wire packet.

---

## Section 3 — Open/Close UFunction Inventory

### 3.1 Alocker_C UFunctions (`locker.hpp`)

All UFunctions on `Alocker_C` are BP-defined (the class is a pure BP — IDA
returns no native symbol matching "locker"). ProcessEvent dispatches every
call, so PRE/POST observers via our existing reflection layer will fire for
all of them.

| UFunction | Signature | BP-callable? | Hookable? | Purpose |
|---|---|---|---|---|
| `Open(bool opened)` | `void Open(bool opened)` ([locker.hpp:162](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/locker.hpp#L162)) | Yes (BlueprintCallable) | YES — POST observer | **Canonical open/close entry.** The `opened` arg sets `bool opened @0x0270` AND drives Timeline direction (Forward if true, Reverse if false). |
| `npcOpen()` | `void npcOpen()` ([locker.hpp:68](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/locker.hpp#L68)) | Yes | YES | Triggers the `head` poke-out (NPC scare). Out of scope for door sync; SEPARATE wire packet later if NPC scares need syncing. |
| `Play(USoundBase* NewSound)` | helper | Yes | YES | Plays sound through `Audio`. Driven by `Open` / `npcOpen`. NOT a sync point. |
| `a__UpdateFunc()` | Timeline 'A' update tick ([locker.hpp:72](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/locker.hpp#L72)) | No (engine-internal) | YES (but high-frequency) | Per-tick Timeline update; do NOT observe this (would fire ~60Hz during the 0.5–1.0 s open animation). |
| `a__FinishedFunc()` | Timeline 'A' finished ([locker.hpp:71](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/locker.hpp#L71)) | No | YES | Fires once when the lid animation completes. Alternative POST hook point if `Open()` PRE/POST has gotchas (e.g. if `Open()` returns BEFORE the animation finishes — likely, since Open is just a Timeline.Play call). |
| `player_use(AmainPlayer_C*, FHitResult)` | E-press dispatch ([locker.hpp:144](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/locker.hpp#L144)) | Yes | YES | The interaction entry. BP body almost certainly: read `opened` → call `Open(!opened)`. Verify under RE Flag L-D2. |
| `actionOptionIndex(...)` | radial-menu dispatch ([locker.hpp:161](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/locker.hpp#L161)) | Yes | YES | Alternative interaction (right-click radial → action). |
| `crowbarOpen(ApryingCrowbar_C*)` | crowbar force-open ([locker.hpp:141](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/locker.hpp#L141)) | Yes | YES | Force-open via crowbar (e.g. when stuck). Routes through `Open(true)`. |
| `padlock_lock(Aprop_padlock_C*)` | padlock attach ([locker.hpp:135](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/locker.hpp#L135)) | Yes | YES | Padlock attached. Affects openability. |
| `padlock_unlock(Aprop_padlock_C*)` | padlock removed ([locker.hpp:136](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/locker.hpp#L136)) | Yes | YES | Padlock removed. |

**Recommended canonical hook: `Alocker_C::Open(bool opened)` POST.**

Rationale: it is the single funnel through which ALL open/close transitions
must pass (player_use, crowbarOpen, npcOpen, gamemode reset). It carries the
target state as its arg (no need to read `opened` after the call — though
the post-call value will match, since `Open()`'s first thing is presumably
to write `self.opened = opened`). The receiver invokes the same `Open(bool)`
UFunction with the received arg.

### 3.2 Aprop_container_C UFunctions (`prop_container.hpp`)

| UFunction | Signature | BP-callable? | Hookable? | Purpose |
|---|---|---|---|---|
| `openContainer()` | `void openContainer()` ([prop_container.hpp:52](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/prop_container.hpp#L52)) | Yes | YES — POST observer | **Canonical open-event entry.** Plays the lid animation + shows inventory UI. Lid is transient. |
| `player_use(AmainPlayer_C*, FHitResult)` | inherited from `Aprop_C` | Yes | YES | E-press dispatch. BP body routes to `openContainer()` if unlocked. |
| `playerUsedOn(...)` | interface ([prop_container.hpp:49](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/prop_container.hpp#L49)) | Yes | YES | Alternative use path (when holding something — e.g. drop into container). May NOT play the open animation (could just stuff item in). RE Flag L-C3. |
| `actionOptionIndex(...)` | radial-menu ([prop_container.hpp:46](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/prop_container.hpp#L46)) | Yes | YES | Right-click radial. |
| `extract(int32 Index)` | UI extract ([prop_container.hpp:38](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/prop_container.hpp#L38)) | Yes | YES (already observed for inventory) | Takes one item out. Already observed by the inventory pipeline (see `votv-storage-container-spawn-RE-2026-05-25.md`). Does NOT animate lid by itself. |
| `broken()` | destruction handler ([prop_container.hpp:43](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/prop_container.hpp#L43)) | Yes | YES (already covered) | Container destroyed; ejects loot. Covered by existing `PropDestroy` packet. |
| `spawned()` | loot-roll on world spawn ([prop_container.hpp:36](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/prop_container.hpp#L36)) | Yes | YES | Initial loot roll. Out of scope for door sync. |

**Recommended canonical hook: `Aprop_container_C::openContainer()` POST.**

Rationale: same single-funnel argument. Any path that visually opens the
lid funnels here (player_use, scripted-open via gamemode). No `Close` /
`closeContainer` UFunction exists in the header — the lid closes
automatically (timer or UI-close-driven). For sync, we therefore emit a
ONE-SHOT "open event" packet on `openContainer()` POST; the receiver
invokes the same UFunction locally to play the lid animation + show
nothing (the receiver does NOT show the inventory UI — that's a
player-local UI thing per `[[project-coop-inventory-private]]`).

**Open question (RE Flag L-C1): does `openContainer()` BP body always show
the inventory UI?** If yes, calling it on the receiver would pop up the
inventory UI on the receiver's screen — that violates inventory privacy.
The receiver must call a function that plays the lid animation WITHOUT
showing UI. Two options to resolve under RULE 1 (the proper fix):

Option (a): the receiver does NOT invoke `openContainer()`. Instead the
receiver directly plays the audio + drives the lid Timeline. The lid is
animated by the BP body of `openContainer()`; we can't easily replicate
that on the receiver without finding the right field/sound.

Option (b): the receiver invokes `openContainer()` BUT with a guard that
suppresses the UI. The UI-show step is presumably keyed on
`gameMode.localPlayer.lookAtActor == this` or `actor.GetInstigator() == localPlayer`
or similar. If we can identify the UI-show predicate in the BP body, we
inject a "this is a remote-driven open" flag that bypasses just the UI
step. This is the proper RULE 1 fix.

**Decision:** Section 5e / Section 9 propose Option (b) with a fallback to
a lighter Option (c): on the receiver, invoke `Play(sound_open)` + drive
the lid Timeline directly via reflection. The Timeline component is NOT a
declared member (per the header), so this would require UE4SS Lua / IDA
probe to find it at runtime — flagged for RE follow-up.

### 3.3 Cross-family interaction notes

- **`Aprop_plankLockLocker_C` destruction** unblocks an `Alocker_C` (because
  the locker's `blockedBy` int decrements when a plank dies, via
  `Aprop_plankLockLocker_C::ReceiveDestroyed`). The plank destruction is
  ALREADY covered by `PropDestroy`, so the receiver automatically sees its
  plank gone and re-evaluates lockability the next time the user hits E
  on the locker. No additional wire packet for plank-removed → unblock.
- **`Aprop_padlock_C` lock/unlock** affects locker openability. Both
  `padlock_lock`/`padlock_unlock` are present on the locker. Future scope
  for full padlock sync (currently out of scope; flag L-P1).

---

## Section 4 — Animation Mechanism

### 4.1 Alocker_C — Timeline-driven, deterministic

The lid animation is a `UTimelineComponent A @ 0x0268` with alpha `a_a_*
@ 0x0260` and direction `a__Direction_*` @ 0x0264. `Open(bool opened)`
sets the bool field + plays the Timeline forward (open) or reverse (close).
`a__UpdateFunc` ticks during animation, presumably driving the `door` SMC's
`SetRelativeRotation`. `a__FinishedFunc` fires once at completion.

**Implication: receiver-side animation is FREE.** Receiver calls
`Open(opened)` via reflection; the same Timeline plays locally with the
same curve, the same audio fires from `Audio @ 0x0240`. No alpha streaming
needed across the wire. Same shape as `Adoor_C::doorOpen` (the doors RE
doc). The state-machine timing is deterministic per the BP body.

Edge case: if Open is called while the Timeline is mid-play (e.g. player
spam-presses E during the animation), behaviour depends on the BP guard.
RE Flag L-A1.

### 4.2 Aprop_container_C — driven indirectly

The lid animation mechanism is NOT a declared `UTimelineComponent` member
field. The header shows `sound_open @ 0x0400` and `sound_close @ 0x0408`
sound assets (`USoundBase*`) — these are the cues to play, not the
animation driver. The lid animation must be:
- A Timeline inside the BP graph (driven via BP nodes, no public member);
- A SetRelativeRotation lerp on a child component (the lid mesh) inside the
  ubergraph;
- OR a sequence asset / level sequence (less likely for a per-prop animation).

This means receiver-side `openContainer()` invocation WILL play the animation
correctly if the BP-internal Timeline runs (it should — same BP body
running locally). The UI-show side-effect remains the concern (RE Flag L-C1
in §3.2).

### 4.3 Aprop_swinger_C (for completeness, see §7) — Physics-constraint-driven

Physics-hinged door. `Open(bool Damage)` releases the swinger constraint
limits and may apply a `swingOutOnDamageForce` impulse. The receiver-side
`Open` call would re-run the same constraint release + impulse, giving a
visually similar but not identical swing arc (physics simulation diverges
peer-to-peer). For full fidelity we'd need to stream the door's physics
pose during the swing (PropPose-style for the swinger's StaticMesh). Out
of immediate scope.

---

## Section 5 — Interaction Surface

### 5.1 E-press path (the primary)

Standard VOTV interaction:
1. Player aims at the locker (or container front); `AmainPlayer_C::useArm(FHitResult& OutHit)`
   ([mainPlayer.hpp:468](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/mainPlayer.hpp#L468))
   resolves `lookAtActor @ 0x0AA0` to the locker/container.
2. Player presses E. `AmainPlayer_C::input_E(bool Pressed)`
   ([mainPlayer.hpp:187](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/mainPlayer.hpp#L187))
   fires.
3. The interaction interface dispatches:
   - For `Alocker_C`: `player_use(player, hit)` on the locker
     ([locker.hpp:144](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/locker.hpp#L144)).
     BP body (RE Flag L-D2): if `blockedBy == 0` AND no `Aprop_padlock_C` attached,
     calls `Open(!self.opened)`. Otherwise plays `audio_locked` sound (presumably
     via reflection; the locker has no `audio_locked` audio member — that's
     the container's field — so for the locker the lock-out behaviour may
     be different. RE Flag L-D3).
   - For `Aprop_container_C`: `player_use(player, hit)` on the container
     (inherited from `Aprop_C`). BP body: if `!Locked`, calls
     `openContainer()`. If `Locked`, plays `audio_locked`.

### 5.2 NPC interaction path

`Alocker_C::npcOpen()` — fires the head-poke-out animation (NOT the same
as `Open`). Plot-driven; NPC AI/event scripts call this. Different bool
(`head` @ 0x0271). This is a separate animation track. **For coop, we DO
want this synced** (so the scare lands on both peers), but it's a SEPARATE
wire packet from the player-driven door sync. Phase 2 scope.

`Aprop_container_C` has no NPC-open path (no `npcOpen`-equivalent UFunction
in the header). Containers are player-only.

### 5.3 Right-click radial / actionOptionIndex

Both classes implement `actionOptionIndex(player, hit, action, lookAtComp)`.
Holding F (or whatever the radial bind is) opens a context menu; the
selected action calls into `actionOptionIndex` with an enum. The action
likely funnels to `Open` (locker) / `openContainer` (container). Hooking
the canonical state-flip UFunctions catches this path too.

### 5.4 Auto-open / story-scripted paths

- `Atrigger_lockerLooker_C` (the trigger volume — see §1.1) likely calls
  `Alocker_C::npcOpen()` when activated. Story-only; out of scope for
  player-driven door sync but again would be picked up if/when we sync
  npcOpen.
- `mainGamemode.ticker_lockerhead()` — periodic ticker that may toggle
  `head` state on a designated locker. Story scripting; out of scope.

### 5.5 Crowbar force-open path (locker only)

`Alocker_C::crowbarOpen(ApryingCrowbar_C* pryingCrowbar)`
([locker.hpp:141](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/locker.hpp#L141))
is fired by the crowbar mini-game when it succeeds against a locker. BP
body presumably bypasses the `blockedBy` check and calls `Open(true)`. Hooks
on `Open` POST will catch this path.

---

## Section 6 — Save Persistence + Identity

### 6.1 Alocker_C — NOT save-persistent for `opened`

Confirmed: `Alocker_C` has no `getData`/`loadData`/`getTriggerData`/
`loadTriggerData` methods. The `opened` bool resets to `false` on map
re-load.

**Identity for coop wire**: `Alocker_C` is a LEVEL-PLACED ACTOR (not
runtime-spawned). The UObject PathName (e.g.
`Persistent_Level.locker_C_42`) is identical on both peers because both
load the same cooked .umap. Use this as the wire identifier (CRC32 of
the path string fits in 4 bytes; or carry the truncated path string in a
fixed-size WireKey-like buffer).

Alternative: use `triggerKey @ 0x0288` (FName) — but this may be empty for
lockers that don't fire an external trigger, AND may not be unique across
lockers in a level (multiple lockers could share a trigger). PathName is
safer.

### 6.2 Aprop_container_C — KEY is save-persistent, but `opened` is transient

`Aprop_container_C` inherits `Aprop_C::Key @ 0x02E0`. The Key is restored
by `Aprop_C::loadData` from the save file (see lifecycle RE doc). For
RUNTIME-SPAWNED containers (rare for lockers but possible — `Aprop_C::Init`
generates `NewGuid` first, then `loadData` overwrites with the saved UUID).
For LEVEL-PLACED containers, the save system also assigns the Key via
`intComs_gamemodeMakeKeys`.

The `openContainer` event is transient (lid pops, then closes) so there is
nothing to save for the open state itself — it is naturally a one-shot
wire event. Use the `Aprop_C::Key` string as the wire identifier (same
identifier the existing PropPose / PropSpawn / PropDestroy packets use,
already plumbed in `WireKey`).

### 6.3 Phase 5S0 (snapshot-on-connect) coverage

For containers: existing Phase 5S0 covers them as part of the prop snapshot
batch (an enumerated `Aprop_C` at session connect). The open event is
transient and does NOT need a snapshot — there is no persistent open state.

For lockers: NOT covered by Phase 5S0 (which is `Aprop_C`-focused).
**`Alocker_C` is not an `Aprop_C`.** A late-join client would join with
all lockers closed (matches the host's default "lockers closed on map
load" semantics). HOWEVER, if the host has opened a locker mid-session
and it is currently open on the host, the joining client would not know.
Two correct paths:
- **Path A: snapshot at connect.** Add an `Alocker_C` enumeration to the
  connect snapshot batch. For each locker with `opened == true`, send a
  `LockerStatePayload` (see §8) with the desired `opened=true` so the
  client opens its local mirror. Simple, deterministic, covers all cases.
- **Path B: ignore at connect.** Acceptable if and only if locker open
  state isn't gameplay-critical (which it kind of isn't — the player can
  re-open the locker visually). RULE 1 says fix it properly: choose Path A.

---

## Section 7 — Variants Catalogue

### 7.1 Locker variants (Alocker_C family)
| Class | File | Inherits | Notes |
|---|---|---|---|
| `Alocker_C` | locker.hpp | AActor | Base; canonical state at 0x0270 |
| `Alocker_death_C` | locker_death.hpp | Alocker_C | Empty body; story-variant |
| `Alocker_personal_C` | locker_personal.hpp | Alocker_C | Empty body; personal-storage variant |

**Three variants.** All use the SAME `Open(bool)` UFunction (inherited).
Sync identical.

### 7.2 Aprop_container_C variants

**32+ concrete subclasses.** See full table in §1.2. All use the same
`openContainer()` UFunction (inherited from `Aprop_container_C`). Sync
identical.

`Aprop_container_orderbox_C` and the `Aprop_arirContainer_*` variants
override some inherited UFunctions (own `player_use`, own `broken` /
`actionOptionIndex` / story-specific logic), but they DO NOT override
`openContainer()`. Hooks remain correct.

### 7.3 Adjacent (out-of-scope for this RE pass)

- `Aprop_safeDoor_C` family (swinger doors) — physics-hinged separate
  RE pass.
- `Aprop_swinger_C` family (transformerDoor, villageGate, crematorDoor) —
  same physics-hinge mechanism; see §4.3.
- `Aprop_safe_C` (the safe body itself) — has `bool Locked @ 0x03A0`,
  `opened()` UFunction (item spawn), and embeds `UChildActorComponent* door`
  that resolves to an `Aprop_safeDoor_C`. Door sync would route through the
  `Aprop_safeDoor_C` swinger path.
- `Aprop_treasureChest_C` — no openable lid in the same sense (Open() releases
  physics constraints — the visible gold piles physically lift out). Out of
  scope.

---

## Section 8 — Proposed Wire Packet Shape

Two distinct packet shapes — one per family — because their identity
mechanisms differ (PathName CRC for lockers vs Key string for props) and
their state semantics differ (steady-state bool for lockers vs one-shot
event for containers).

### 8.1 `LockerStatePayload` (for Alocker_C)

```cpp
// ReliableKind::LockerState = 18 (next free after door/light/lock 9..11
// reservations and Phase 5G entity 16/17). Reliable channel — discrete
// state event, must not be lost.
struct LockerStatePayload {
    uint32_t lockerPathHash;  // 4 -- CRC32(UObject::GetPathName) of the locker actor
    uint8_t  peerSessionId;   // 1 -- sender peer id (host=0)
    uint8_t  opened;          // 1 -- 0=closed, 1=open (the target state arg to Open)
    uint8_t  _pad[2];         // 2 -- 8-byte align
};
static_assert(sizeof(LockerStatePayload) == 8, "LockerStatePayload must be 8 bytes");
static_assert(sizeof(LockerStatePayload) <= 256 - 20 - 8,
              "LockerStatePayload must fit one reliable datagram");
```

**Why PathHash, not the full path string**: VOTV's level paths are
deterministic (cooked .umap → identical asset path on both peers). A
CRC32 collision across the < 100 lockers in a map is essentially impossible.
Saves 16+ bytes vs carrying the path string. Lookup on receiver: at session
connect, enumerate all `Alocker_C` instances via reflection, build
`g_lockerByPathHash : unordered_map<uint32_t, Alocker_C*>`.

**Why no animation alpha / no timestamp**: per §4.1 the animation plays
locally on the receiver via the same Timeline; we don't need to stream
intermediate alpha. Receiver invokes `Open(opened)` on its local mirror;
the lid plays the canonical 0.5–1.0 s animation; both peers diverge by a
few frames of network jitter, which is acceptable for a door (vs e.g. a
puppet pose which streams continuously).

### 8.2 `ContainerOpenEventPayload` (for Aprop_container_C)

```cpp
// ReliableKind::ContainerOpenEvent = 19. Reliable channel — discrete event,
// must not be lost.
struct ContainerOpenEventPayload {
    WireKey key;              // 32 -- Aprop_C::Key (existing FName string)
    uint8_t peerSessionId;    // 1 -- sender peer id (host=0)
    uint8_t _pad[7];          // 7 -- 8-byte align
};
static_assert(sizeof(ContainerOpenEventPayload) == 40,
              "ContainerOpenEventPayload must be 40 bytes");
static_assert(sizeof(ContainerOpenEventPayload) <= 256 - 20 - 8,
              "ContainerOpenEventPayload must fit one reliable datagram");
```

**Why WireKey (not PathHash)**: containers can be RUNTIME-SPAWNED
(propThroughGamemode, drops from inventory, etc.) and have NewGuid keys —
PathName won't exist for them. WireKey is what the existing prop wire
layer uses; reuse it for consistency. Lookup on receiver:
`coop::prop_wrap::FindByKeyString(key)` — already implemented.

**Why no `opened` bit**: there is no persistent open state on
`Aprop_container_C` (per §2.2). The lid is a transient pop. The packet is
a one-shot "lid-pop event". The receiver always plays the open animation
on receive; the lid auto-closes locally per the BP body's own logic.

### 8.3 `ContainerLockStatePayload` (for Aprop_container_C — `Locked` field)

This is OPTIONAL / separate from the open-event packet. `bool Locked` is a
persistent state (quest/keypad lock), DISTINCT from the lid animation. If
the user wants lock-state synced (open question per the user's scope —
"opens and closes" was the wording, suggesting only the lid animation),
add this:

```cpp
// ReliableKind::ContainerLockState = 20. Reliable. Sender = host
// authoritative for quest progression locks.
struct ContainerLockStatePayload {
    WireKey key;             // 32
    uint8_t peerSessionId;   // 1 -- host=0
    uint8_t locked;          // 1 -- 0=unlocked, 1=locked
    uint8_t _pad[6];         // 6
};
static_assert(sizeof(ContainerLockStatePayload) == 40, "must be 40 bytes");
```

**This is OUT OF SCOPE for the immediate user request**, queued as future.

### 8.4 ID allocation summary

Free `ReliableKind` slots now: 9, 10, 11 (reserved for Phase 5D doors but
not shipped), 18, 19, 20+. Per RULE 2 (no migration baggage), the locker
packets should NOT squat the door reservations even though those aren't
shipped — when the door RE doc lands as code, slot 9 is the door packet.
Use 18 / 19 / (20) for lockers + containers.

---

## Section 9 — Hook Proposal

Mirroring the existing `prop_lifecycle.cpp` / `item_activate.cpp` patterns
(POST observer on the canonical state-flip UFunction + echo-suppression
via in-flight flag + receiver-side reflection invoke).

### 9.1 New file: `coop/locker_sync.cpp` + `.h`

Per the modular file-size rule (CLAUDE.md), the new feature gets its own
subsystem file under `src/votv-coop/src/coop/` rather than growing
`harness.cpp` or `prop_lifecycle.cpp` further.

### 9.2 Sender-side: locker `Open(bool)` POST observer

```cpp
// At session install: enumerate all Alocker_C UFunctions via reflection,
// register POST observer on Open(bool opened).
//
// At fire time:
//   if (incoming_apply_flag_set) return;  // echo-suppress
//   self = params->self  // Alocker_C*
//   opened = params->opened  // bool arg
//   pathHash = CRC32(R::GetObjectPathName(self))
//   LockerStatePayload pl{pathHash, role::id, opened ? 1 : 0, ...};
//   g_session->SendReliable(ReliableKind::LockerState, pl);
//
// Symmetric (both peers send when their local player opens a locker).
```

### 9.3 Sender-side: container `openContainer()` POST observer

```cpp
// At session install: register POST observer on Aprop_container_C::openContainer.
// (Subclasses inherit the UFunction; observer fires for all 32+ variants.)
//
// At fire time:
//   if (g_incoming_container_open_set.contains(self)) return;  // echo-suppress
//   key = R::GetFNameString(self + offsetof(Aprop_C, Key))   // 0x02E0
//   ContainerOpenEventPayload pl{key, role::id, ...};
//   g_session->SendReliable(ReliableKind::ContainerOpenEvent, pl);
```

### 9.4 Receiver-side: apply

```cpp
// On ReliableKind::LockerState reception:
//   pl = parse
//   actor = g_lockerByPathHash[pl.lockerPathHash]   // built at connect
//   if (!actor) { log "missing locker for pathHash=..."; return; }
//   MarkIncomingLockerOpen(actor);
//   R::CallUFunction(actor, "Open", &pl.opened);
//   ClearIncomingLockerOpen(actor);  // POST observer guard
//
// On ReliableKind::ContainerOpenEvent reception:
//   pl = parse
//   actor = prop_wrap::FindByKeyString(pl.key)
//   if (!actor) { log "missing container for key=..."; return; }
//   MarkIncomingContainerOpen(actor);
//   R::CallUFunction(actor, "openContainer", nullptr);
//   ClearIncomingContainerOpen(actor);
```

### 9.5 At-session-connect: build pathHash lookup for lockers

```cpp
// At connect / scene-ready, enumerate the level for Alocker_C instances:
//   for each actor in GUObjectArray with class == Alocker_C (or subclass):
//     pathHash = CRC32(GetObjectPathName(actor))
//     g_lockerByPathHash[pathHash] = actor
//
// Re-enumerate on level transition (mainGamemode.BeginPlay POST or
// equivalent re-bind signal).
```

Use the existing reflection scan infrastructure (`ue_wrap::reflection`
helpers). Cost: one O(N) walk at level load; N=~10–50 lockers per map.

### 9.6 At-connect snapshot (Phase 5S0 hook)

```cpp
// Host-side: on client connect, after the existing prop snapshot batch,
// enumerate all lockers; for each with self.opened == true, send a
// LockerStatePayload{pathHash, host_id, opened=1}.
//
// No equivalent needed for containers (open is transient).
```

### 9.7 Echo-suppression sets

Two new in-flight sets in `locker_sync.cpp`:

```cpp
static std::unordered_set<void*> g_incoming_locker_open;
static std::unordered_set<void*> g_incoming_container_open;
```

Both protected by the same atomic discipline as
`prop_lifecycle::g_takeObjInFlight` (atomic if cross-thread, or
single-threaded ProcessEvent dispatch makes plain `bool` OK — match
whichever convention the surrounding code uses).

### 9.8 Reuse of existing infrastructure

- `WireKey` already defined — reuse for `ContainerOpenEventPayload`.
- `coop::prop_wrap::FindByKeyString` already implemented.
- `R::CallUFunction` (reflection-based UFunction invoke) already implemented.
- The session reliable channel (`SendReliable` / kind dispatch) — add two
  new kinds (18, 19) + drain handlers.
- The `incoming-*-set` echo-suppression pattern — copy from
  `prop_lifecycle.cpp` / `item_activate.cpp`.

No new infrastructure required.

---

## Section 10 — Edge Cases + Open Questions

### 10.1 Open RE flags (questions to resolve before shipping)

**L-D1**: `Alocker_C::Open(bool opened)` BP body — is `self.opened` written
inside the BP body OR via direct field write OUTSIDE `Open()` (e.g.
`player_use` sets the bool then calls `Open` which only animates)? Affects
whether reading the bool POST is reliable. Verify via UE4SS Lua probe:
hook `Open` PRE and POST, log `opened` field at both. If POST shows
opened=arg, then `Open` writes the field — good. If POST shows opened=old,
then we must trust the arg directly (also fine, the packet carries the arg).

**L-D2**: `Alocker_C::player_use` BP body — confirm it dispatches to
`Open(!self.opened)` (toggle). If it dispatches differently (e.g. always
`Open(true)` and only `npcOpen` closes), our toggle assumption is wrong.

**L-D3**: `Alocker_C` `audio_locked` — the locker has NO `audio_locked`
member field (unlike `Aprop_container_C`). When the player presses E on a
plank-blocked locker, what plays the lock-out sound? Probably nothing; or
a sound triggered through the gamemode. Not relevant to door sync but
worth knowing.

**L-A1**: Spam-press behavior — what if Open is called while the Timeline
is mid-play? Does the Timeline restart, reverse, or no-op? Affects how
fast the packet flow can be. Probably OK either way (idempotent at the
state level), but worth knowing.

**L-C1**: `Aprop_container_C::openContainer()` BP body — does it always
show the player's inventory UI? If yes, calling it on the receiver shows
the receiver's inventory UI (privacy violation). PROPER fix: identify the
UI-show predicate inside the BP body (likely `Player.Controller.IsLocal()`
or `Player == GetGameMode().localPlayer`) — if the BP already guards on
"only the player who triggered this", a remote-driven invoke with
`Player = nullptr` would naturally skip the UI. Most likely path.

**L-C2**: `Aprop_container_C` lid-close mechanism — what closes the lid
on the player who opened it? If it's UI-onClose-driven, the receiver
(who doesn't show the UI) would see the lid stay open. Two outs:
- The receiver's `openContainer` invocation doesn't run the UI-show step
  (per L-C1 fix), but DOES start the lid Timeline, which has its own
  auto-close after N seconds (typical pattern).
- Add a `ContainerCloseEvent` packet (mirror of the open event) so
  closes are explicitly synced. Adds 1 more ReliableKind.

**L-C3**: `Aprop_container_C::playerUsedOn` (drop-item-into-container path)
— does this play the lid animation? Likely NOT (the drop is silent into
the propInventory). Confirm under probe. If it doesn't, our hook on
`openContainer` does the right thing (no extra packet for drops).

**L-C4**: `Aprop_container_orderbox_C` overrides `player_use` and
`broken`. Does it bypass `openContainer()` for some interaction paths?
Read the order-box BP body to confirm sync hooks cover all paths.

### 10.2 Concurrency / collisions

- Both peers open the same locker simultaneously (within network jitter).
  Both send `LockerState{opened=true}`. Receiver applies; Timeline already
  playing or about to play. No state divergence (both end opened).
- Player A opens, Player B closes within the network round-trip. Last
  packet wins. The brief visual divergence (one peer sees opened-then-closed,
  the other sees closed-then-opened) is acceptable for doors.

### 10.3 Late-join

Covered in §6.3. Path A (host snapshot of all `opened==true` lockers at
connect) is the chosen approach.

### 10.4 Locker-spawning lockers?

Lockers are pre-placed level actors — they should not be runtime-spawned.
Containers CAN be runtime-spawned (drops). The existing PropSpawn pipeline
covers container spawn. Locker enumeration is one-shot at level load.

### 10.5 Save-load mid-session (rare)

If the host re-loads a save mid-session, all lockers reset to closed (per
§6.1). The client's `Alocker_C*` pointers may also be invalidated. Need
to re-enumerate `g_lockerByPathHash` on level transition. Phase 5S0 should
already handle this for the prop case; mirror for lockers.

### 10.6 Items inside the locker

The locker's `asContainer(Aprop_container_C*& container)` UFunction
([locker.hpp:56](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/locker.hpp#L56))
returns a nested `Aprop_container_C` that holds the locker's items.
**Items-inside sync is already covered** by the existing
PropSpawn/PropDestroy/PropPose pipeline for the nested container —
because the nested container is an `Aprop_C`, its loot extraction routes
through the existing `UpropInventory_C::takeObj` POST observer. No
locker-specific item-inside packet needed.

### 10.7 The `Aprop_swinger_C` adjacency

Note that `Aprop_safeDoor_C` (a swinger) and the regular `Aprop_swinger_C`
family (transformerDoor, villageGate, crematorDoor) ARE physics-hinged
doors with `Open(bool Damage)` / `Close()` UFunctions. These are a
DIFFERENT family from the lockers and out of immediate scope, but they
will need similar sync. The wire shape would be:
`SwingerStatePayload {pathHash, peerSessionId, opened, _pad}` —
essentially identical to `LockerStatePayload`. Flagged for a future
RE/wire pass.

---

## Section 11 — File Reference (for shipping the hook)

Files to create:
- `src/votv-coop/include/coop/locker_sync.h`
- `src/votv-coop/src/coop/locker_sync.cpp`

Files to modify:
- `src/votv-coop/include/coop/net/protocol.h` — add `ReliableKind::LockerState = 18`
  and `ContainerOpenEvent = 19`, plus the two payload structs +
  static_asserts. Bump `kProtocolVersion` 9 → 10.
- `src/votv-coop/src/coop/net/session.cpp` — add reliable-drain dispatch
  to the new `locker_sync` handlers.
- `src/votv-coop/src/harness/harness.cpp` (or wherever the session-install
  fans out subsystem installs) — call `locker_sync::Install(session)` at
  session install.
- `src/votv-coop/src/coop/prop_snapshot.cpp` (or wherever Phase 5S0
  snapshot lives) — host-side: enumerate `Alocker_C`, send `LockerState`
  for each `opened==true`.

Key offsets (write into `sdk_profile.h` or local const table; matches
the Aprop_C / other existing tables):

```cpp
// Alocker_C
constexpr ptrdiff_t kAlocker_opened_offset      = 0x0270;  // bool
constexpr ptrdiff_t kAlocker_head_offset        = 0x0271;  // bool
constexpr ptrdiff_t kAlocker_blockedBy_offset   = 0x0274;  // int32
constexpr ptrdiff_t kAlocker_triggerKey_offset  = 0x0288;  // FName
// Aprop_container_C
constexpr ptrdiff_t kAprop_container_Locked_offset = 0x03E8;  // bool
// (Aprop_container_C uses inherited Aprop_C::Key at 0x02E0 -- already in profile)
```

Functions to resolve in reflection (UFunction pointers):
- `Alocker_C::Open` (arg: `bool opened`)
- `Aprop_container_C::openContainer` (no args)
- (Optional, observe-only for diagnostics) `Alocker_C::npcOpen`,
  `Alocker_C::a__FinishedFunc`

---

## Section 12 — Summary

- Two families of "storage lockers" in VOTV: dedicated **`Alocker_C`**
  (3 variants) and prop **`Aprop_container_C`** family (32+ variants).
- `Alocker_C` has a clean `bool opened @ 0x0270` + `Open(bool)` UFunction —
  ideal hook target. Animation is Timeline-driven, plays locally on the
  receiver for free. Not save-persistent; identity via cooked UObject
  path hash.
- `Aprop_container_C` has NO persistent `opened` field; the lid is a
  transient event. Hook the `openContainer()` UFunction as a one-shot
  event packet; identity via existing `Aprop_C::Key`. One privacy gotcha
  (`openContainer` may show the player UI — see RE Flag L-C1) needs
  resolution before shipping; the proper RULE 1 fix is to find the
  UI-show predicate inside the BP body and ensure receiver-driven invokes
  bypass it.
- Both families fit cleanly into the existing reflection-based
  POST-observer + echo-suppression-set pattern used by
  `prop_lifecycle.cpp` and `item_activate.cpp`. No new infrastructure.
- 6 open RE flags (L-D1..L-D3, L-A1, L-C1..L-C4) listed for confirmation
  via UE4SS Lua probe before shipping — none gate the design, just
  details.
