# Ambient-spawner ANCHOR audit + roach/eyer RE — 2026-07-10 eve (durable RE)

Bytecode dumps: `research/bp_reflection/*.json` (UAssetAPI; StackNode resolved via
Imports[-idx-1], EX_* nodes walked). Shipped consumers: `08560687` (v108 lanes) +
`dbaf5099` (owner-entity). Tracker rows: docs/COOP_RNG_AUTHORITY.md T1-1/T2-6.

## The anchor reads (what decides a spawner's coop tier)

| spawner | anchor (measured) | tier consequence |
|---|---|---|
| `pineconeSpawner_C` | `GetPlayerCameraManager -> K2_GetActorLocation` + random offset, sphere-trace for `#wood`; products prop_food_pinecone / prop_stick / prop_crystal(+_explode) / prop_C(named), SetLifeSpan each | **OWNER-EFFECT** (was wrongly cancelled on clients) |
| `ticker_fireflySpawner_C` | `GetPlayerCameraManager` + 3000-10000 units, trace to `#grass`, `SpawnEmitterAtLocation` | OWNER-EFFECT confirmed (firefly_sync v51 already correct) |
| `ticker_wispSpawner_C` | `MakeVector(+-60000-70000, +-60000-70000, 80000)` ABSOLUTE; interval 1200-3600 s; weighted map wisp_C=100 + 8 variants (wisp_b 5, red 1, bl .5, w .1, g 1, o .25, p .25, blu .5) -- variants are DIRECT ACharacter subclasses (SDK wisp_b.hpp), NOT wisp_C children | **world-anchored** (the old "per-peer decor" call REVERSED) |
| `ticker_yellowWispSpawner_C` | `p = ProjectPointToNavigation(p + RotateAngleAxis(random))` -- a navmesh RANDOM-WALK point; night-gated (timeZ), capped by gamemode.killerWisps; also spawns a random member of its `deer` array near p with SetLifeSpan | world roamer (suppression correct); product killerwisp_C allowlisted |
| `ticker_eyers` | roll every 1800-2700 s, `RandomBoolWithWeight[0.05]`, night timeZ window; direction `RandomFloatInRange(0,360)` -> `K2_ProjectPointToNavigation`; writes `Distance`=10000-15000 onto the spawn | world-anchored ROLL, but the ENTITY stalks the LOCAL player -> **OWNER-ENTITY** (user rule) |
| `ticker_roachSummoner` | tick -> `summonRoach` on `gamemode.cockroachMaster` (@0x910) | see roach section |

**Method note:** the anchor read takes minutes per class and reversed two wrong tier calls the
same day. Static `$type` in the dump CANNOT predict PE-visibility (see the lesson) -- but the
anchor (who the spawn position derives from) is fully static-readable.

## cockroachMaster_C (roach infestation) — component sim, not actors

- Plain `AActor` child; roaches = `TArray<UStaticMeshComponent*> roaches` @0x238, `Count` @0x230,
  `maxAmount` @0x280 (CDO=128). ONE master, held by mainGamemode @0x910.
- `addRoach(Location, Size, bypassCheck)`: player-cam distance gates (<10000 roll / <=2000 spawn,
  bannedMats trace) SKIPPED when bypassCheck -- the perfect reflected mirror driver. Appends.
- `deleteRoach(IndexToRemove, crush)`: squish sound if crush, K2_DestroyComponent, `Array_Set`
  slot to null (NO shift -- verified in the dump; index-stable).
- `calc(currentIndex)` (from ReceiveTick): per-roach gravity trace + wander; flees
  `prop_roachRepel_C` (250u); seeks `prop_food_C` (300u, celsius>10) and EATS it --
  `VictoryFloatMinusEquals` on the food's `foodData` + `K2_DestroyActor` when depleted + grows
  the roach's scale. THE shared-state mutation that forces host authority.
- `tryCrush(Loc, Object)`: roach within 10u -> spawn `prop_deadRoach_C` (SetLifeSpan 300) +
  deleteRoach. The husk's BeginDeferred is INSIDE the script fn (chipPile-family EX shape) --
  not catchable at the PE POST.
- Timers: exactly 3 `EX_BindDelegate` -- `addRoachTimer`, `spawnNestTimer`, `CustomEvent`
  (looping K2_SetTimerDelegate; timers fire independently of actor tick -> the t3 cancels).
- `spawnNest`: near a random `prop_food_C` within 500u (player-cam involved), spawns
  `prop_cockroachNest_C` on a traced surface.
- Interaction events (steppedOn/actionOptionIndex/impactSquishCPP/eaten) are NOT tick-driven --
  they stay native on a parked master (the roach_sync consumption-intent design relies on it).

## eyer_C (the night Eyes stalker)

`Aeyer_C : ACharacter` (SDK eyer_DUPL_1.hpp): 2 eye particle systems + billboards, `eyerTeeth`
skeletal mesh + show-teeth timeline, `anger`/`isAngry`/`isLooking()`/`angrify()`, `dash` +
`timerDash` + `killsphere` (overlap kill), `comp_paranormal` + `comp_radiation` +
`comp_radarPoint`. The whole behavior loop reads the LOCAL player -> per-peer ownership is the
only shape that preserves the encounter for every peer (docs/COOP_RNG_AUTHORITY.md decision (4)).
Also spawned by `spawnBlackFog` (event_trigger) via mainGamemode -- host-auth events, so those
ghosts are host-owned announces.
