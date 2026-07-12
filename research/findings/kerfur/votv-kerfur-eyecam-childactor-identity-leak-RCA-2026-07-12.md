# Kerfur eye-cam floating-CCTV RCA — child actors leaked into the prop identity universe (2026-07-12)

Status: FIXED (child-actor exclusion, DLL `DE4B438A` after the audit-CRITICAL close), take-8
hands-on pending.
User report (take-7, 13:58): «У клиента камеры внутри 3 керфуров заспавнились и визуально в
воздухе cctv у них троих в груди. Клиент их включил/выключил и исправилось только на включении —
если просто выключить то камеры продолжат висеть в воздухе».

## The root (one sentence)

ChildActorComponent-owned actors (the kerfur eye camera `prop_camera_good_C`, a keyed
Aprop-lineage prop) entered our independent prop-identity universe on BOTH peers, while the game
itself excludes them from its world-object universe — `Aprop_C::ignoreSave = ignoreSav ||
IsChildActor()` (prop_base.json bytecode, EX_CallMath BooleanOR over the IsChildActor
EX_FinalFunction) — because the PARENT's SCS spawns/positions/destroys them on every peer.

## The mechanism (take-7 log-proven, scratchpad take7_{host,client}.log)

Both kerfur forms carry the camera as a true engine `UChildActorComponent`:
`kerfurOmega_C.cam` @0x05A0 (ChildActorClass=prop_camera_good_C, under the meshLag spring arm;
kerfurOmega.hpp + kerfurOmega.json L128856) and `prop_kerfurOmega.cam_GEN_VARIABLE` (under
cameraLag/cameraRoot; prop_kerfurOmega.json). `prop_camera_good_C` is a VISIBLE camera prop.

1. **Host enroll + broadcast:** the eye cams are keyed interactables, so the Init-POST catch +
   census enrolled them (`MarkPropElement`) and broadcast PropSpawn (`name='cam_b_1'`) — at the
   connect snapshot AND in waves at every kerfur toggle (host cam enrolls at 13:57:44 / 13:59:44 /
   14:00:24 = the toggle instants; a toggle creates the NEW form's fresh cam child).
2. **Joiner materializes floating CCTVs:** eye-cam Keys are per-peer random (the kerfur RE already
   proved `kerfurOmega::loadData` never restores Key — same class of fact: the cam child is never
   in any save blob at all, so its Key is minted fresh per boot per peer). Wire key matches
   nothing → fresh standalone mirror at the host cam's world position = a CCTV at the kerfur's
   chest. Client log: eid 9558/9567/9569 fresh camera mirrors = exactly the 3 visible CCTVs.
3. **Joiner's own eye cams doomed:** unclaimed keyed locals at the divergence sweep → destroyed
   (client log 13:57:38: `doomed 2 x 'prop_camera_good_C'`).
4. **Fuzzy steal:** Gap-I-1 30 cm sometimes bound a wire cam key onto a LIVE local eye cam
   (eid 9568/9575/9576/9580 `FUZZY MATCH ... rekeying`) — re-keying a kerfur's child actor.
5. The on/off asymmetry the user saw is (1)+(2) cycling: each toggle broadcast the new form's cam
   (new floating mirror at the same chest) — turn-on happened to land destroy-before-spawn.

Why it surfaced only now: take-7 was the FIRST kerfur-present join since the JOIN BARRIER; the
save also had kerfurs never before joined onto.

## The fix (per rule 1 — mirror the game's own rule; commit with this doc)

ONE predicate **`ue_wrap::engine::IsChildActor(actor)`** — raw read of the reflected
`AActor::ParentComponent` TWeakObjectPtr (offset resolved once via reflection; set weak ptr =
child actor; no ProcessEvent dispatch → off-GT-safe). Consulted at the four identity surfaces:

| Surface | File | Effect |
|---|---|---|
| The ONE enroll owner | prop_element_tracker.cpp `MarkPropElement` | never in the element table → key index, R2 baselines, reaper, sweep universe all exclude it (both peers) |
| Spawn broadcast catch | prop_lifecycle.cpp Init-POST | no known-keyed set entry, no PropSpawn broadcast (the eid=0 keyed broadcast bypassed enroll) |
| Destroy broadcast seam | prop_destroy_seam.cpp | a dying eye cam (every toggle) no longer broadcasts a keyed destroy |
| Fuzzy candidate walk | ue_wrap/prop.cpp `FindNearbySameClass` | a standalone same-class wire spawn can never steal a child actor within 30 cm |
| Census seed/re-seed walk | prop_census.cpp `SeedWalk_` | never in g_knownKeyedProps / outNewActors → the steady re-seed can't rediscover a toggle-fresh eye cam as "new" |
| THE one payload builder | prop_snapshot.cpp `BuildPropSpawnPayload_` | closes every outbound PropSpawn lane (connect drain + incremental express) |

**AUDIT CRITICAL on the first cut (confidence 90, fixed same session):** the first 4 surfaces
missed the steady re-seed incremental-express lane — `SeedWalk_` (raw GUObjectArray, keyed-only
filter) fed a toggle-fresh eye cam into `ExpressIncrementalSpawn` → `BuildPropSpawnPayload_`,
which builds a KEYED payload with `elementId=0` regardless of enrollment. Worse than pre-fix: the
gated destroy seam would never tear that mirror down — a PERMANENT floating CCTV per toggle. The
audit's verify pass also CONFIRMED the weak-ptr layout ({int32,int32}, matches device_screen.cpp's
prior use), thread-safety, hot-path cost (net win in the fuzzy walk: the skip runs before the
wstring allocs), and that K2_AttachToComponent does NOT set ParentComponent (stick unaffected).

NOT excluded (correct): user-placed standalone cameras (not child actors — take-7 showed
prop_camera_s/cursed syncing; they keep wire identity); props ATTACHED via K2_AttachToComponent
(the camera stick) — attach does not set ParentComponent.

File-size flag (audit item 7): `ue_wrap/engine.h` at ~1047 LOC (soft cap 800, already in the >800
queue) — extraction proposal: split by its own section comments (engine_spawn / engine_widgets /
engine_vitals / engine_bones), mirroring the engine_audio/engine_component extractions.
`ue_wrap/prop.cpp` at ~786 — watch.

## Take-7 adoption verdict (the run's original purpose)

2 NPC adoptions bound poll #1 (81/93 ms); 2 NPC + 1 prop kerfur had no blob twin → fresh-spawned
at sweep-latch (poll #12) — the fallback fired on a REAL join, so the K-6/npc_adoption
wait-then-fresh-spawn branch is LOAD-BEARING and stays. The adoption anti-smear candidates are
CLOSED (keep). RE-BIND ledger: 0 (second consecutive session).
