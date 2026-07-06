# wallbuilder — draw a wall in the air, feed it bricks   (STATUS: RE)

The base-building family: the wallbuilder tool previews a grid-snapped box between two
aimed points and commits it as an UNFINISHED wall; players then throw bricks/cement into
it until it becomes a real wall. The wallfixer repairs damage. Three-actor chain again:

- **`prop_wallbuilder_C` : prop_C** — the TOOL. Rich local editing state (`editing`,
  `firstHit`+`firstNorm` grid-snapped to 12.5, `shape` 0-3 cycle, `material` cycle,
  `centralMode`, preview `Cube` ghost + `pivot_A/B` + `visTextComp`, `rot` component =
  the commit transform, `actorBuild` class = `customWall_unfinished_C`). `init()`:
  `isHeld = getMainPlayer().holding_actor == self`; **tick enabled ONLY while held**;
  pivot visibility follows. LMB press (uber @750): snap `firstHit`, `editing=true`, show
  ghost; tick (@2892 entry) re-aims the box; LMB release (@2199): `editing && isHit` →
  **`addWall()`** → hide ghost. RMB → `editShape()` (shape=(shape+1)%4, mesh swap via
  `lib_C::customWall_shape`, hint toast). `editMat` cycles material. First-person arm
  montage (FP_spongeProt) on use. [bytecode]
- **`addWall()`**: rejects zero-size (hint toast); deferred-spawn **`actorBuild`** at
  `rot.K2_GetComponentToWorld()` with `shape` + `material` ints; `cheat` flag →
  `buildWall(true)` instantly. [bytecode @0-821]
- **`customWall_unfinished_C` : actor_save_C** — the PERSISTENT construction site.
  `requirement`/`bricks`/`brickAdd` counters, `overlap` component BeginOverlap = the
  brick-feeding seam (props thrown into it), progress via `buildProg`/`nametagComponent`,
  water in/out handlers, impact damage/squish handlers, `buildWall()` → spawns the final
  **`customWall_C`** (`class` prop) and dies; `shape`/`material`/`wallType` +
  getData/loadData (progress persists in the save). [bytecode: props + fn census; uber
  not fully read — counters/overlap flow inferred from names, tagged [?] where used]
- **`prop_wallfixer_C` : prop_C** — the REPAIR sibling: `concrete` int (charges),
  hold-LMB `fixing` loop on a timer with montage, `updConcrete`, getData/loadData (the
  TOOL itself saves its charges). Acts on existing walls. [bytecode: fn census only]

## 2. Sync-axis table

| axis | owner | peers need | carried by |
|---|---|---|---|
| tool editing (ghost box, grid snap, shape/mat cycling, hints, montage) | owning player, 100% local by construction (tick gated on `isHeld` of the LOCAL holder) | nothing (arm pose already rides player pose) | none needed — genuinely per-viewer |
| wall COMMIT (addWall spawn: transform + shape + material) | MUST be host (persistent actor_save_C) | the unfinished wall appears | GAP today |
| brick feeding (overlap consumes thrown props; bricks++ toward requirement) | host (props are host-authoritative already; the consuming overlap must be on the host instance) | counter/progress display on the mirror | GAP today |
| buildWall swap (unfinished → customWall_C) | host (follows actor ownership) | swap mirrored | GAP today |
| wall damage/repair (wallfixer `fixing`, impact handlers) | host applies; fixer charges owner-local | wall state | intent → host |
| persistence / late-join | host save (actor_save_C, progress included) | joiner sees site from save | free once host-owned |

## 3. Coop design (DESIGN — not built)

Same **commit-intent → host** shape as the nailgun, with an even cleaner split: the
ENTIRE interactive part (preview/editing) is natively local-only — the game itself gates
the tool tick on the local holder. The only wire moment is the commit:

1. Owner edits freely (nothing synced), on `addWall` sends
   `WallCommit{transform, shape, material}` instead of (or suppressing) the local spawn.
2. Host validates + spawns `customWall_unfinished_C` with those three values — the
   native spawn takes exactly them, nothing else to reconstruct.
3. The site mirrors host→clients (spawn + `bricks` progress for the nametag). Brick
   feeding needs NO new intents: bricks are props, props are host-authoritative, the
   host instance's overlap consumes them natively; clients just see their thrown brick
   vanish + the counter tick up.
4. `buildWall` swap is a host-side native event; mirror follows (destroy + spawn final
   wall — final `customWall_C` is presumably also actor_save_C [?] — verify when built).
5. Wallfixer: `fixing` ticks apply repair as host intents; `concrete` charges stay
   owner-local (its own getData saves them — NOTE: a client's held fixer saving its
   charges into the HOST save only works if the item itself is host-owned prop — the
   held-prop ownership question is the prop lane's, not this doc's).

## 4. Caveats

- `init()` uses `getMainPlayer().holding_actor` — on a host, a puppet "holding" the tool
  never enables its tick (good: no ghost preview leakage), but it also means the tool's
  preview can NEVER be mirrored natively — if we ever want peers to see the ghost box,
  it's a custom display stream (v2+, likely never — SP polish).
- Grid snap (12.5) happens owner-side pre-commit; the host must NOT re-snap (transform
  arrives final) or walls shift by up to half a cell vs what the builder saw.
- `cheat` bool instant-builds — keep it out of the intent surface (host ignores client
  cheat flags; forgery guard).
- customWall_unfinished uber not fully disassembled — before building the lane, read the
  overlap→bricks flow + `buildWall` to confirm the counter seam and the swap identity
  (dupe matrix row).

## 5. Verification

2026-07-06 static RE: wallbuilder uber + addWall/init/editShape/ReceiveTick read;
setPivotLoc/setRot skimmed (preview math — local); customWall_unfinished = property +
function census only (uber pending); wallfixer = census only. No live probe. Sync NOT
BUILT.
