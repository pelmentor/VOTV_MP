# physgun / gravity gun — grab, drag, rotate, freeze props at range   (STATUS: RE)

The manipulator league (user ask 2026-07-06). One BP family, three shipped forms:
`prop_physgun_C` (hard/kinematic manipulator, `superGun` flag unlocks scale + static
mobility), **`prop_physgun_soft_C` : prop_physgun_C** (`soft=true` constraint-drive
variant + its own charges/time state), and **`prop_gravgun_C`** — which turned out NOT
to be a manipulator at all (see below).

## 1. Native behavior (ground truth) [bytecode]

### prop_physgun_C : prop_C — the manipulator

- **Grab (LMB, uber @759)**: `hold=true`; camera line-trace 10000; accepts `prop_C`
  (plus `ragdoll_C`; `superGun`/`soft` accept ANYTHING); resolves attach-root →
  `objectPickup` + root `component`, stores hit `distance` + `bone` (skeletal).
  Then: `component.BodyInstance.bSimulatePhysics := true` (RAW field write!), saves
  `compBodyInst` for restore; superGun → `SetMobility(Movable)` on STATIC things.
  - **Hard mode** (base gun): physics OFF (`SetSimulatePhysics(false)` / per-bone
    `SetPhysicsAssetShapeSimulatePhysics(bone,false)`) → the object is dragged
    KINEMATICALLY.
  - **Soft mode** (`soft`): `softConstraint` softPivot↔component(bone), linear drive
    strength = clamp(1000/mass×50, 0..100) — springy carry, physics stays on.
- **Input capture while holding (@3383)**: binds ITSELF onto the player's input
  delegates — `input_scrollDown/Up`, `input_E`, `input_mouseXY`, `input_shift`,
  `input_altUse`. Release unbinds all (@6010).
- **Hold tick (@6904)**: beam FX; `pivot` = camera + fwd×distance; hard mode drives
  `objectPickup.K2_SetActorTransform(ComposeTransforms(rel, pivotWorld))` every tick;
  soft mode moves the constraint reference instead. `shiftHold` → rotation snapped to
  45°; `altHold` → location snapped to 5-grid.
- **RMB rotate (@9804)**: `rotate=true` + `player.deactivateMouseInput=true` → mouseXY
  spins the pivot (object rotates in hand).
- **Scroll (@9444/@9766)**: `distance ±= 10` (min 50) — push/pull the held object.
  **alt+scroll (non-soft): pivot scale ±0.1 → the object SCALES** (superGun feature).
- **E = FREEZE (@11103)**: `compBodyInst.bSimulatePhysics := false` then release —
  restore path then leaves physics OFF: the object hangs frozen in the air. (This is
  the "frozen prop" state the HOOK unfreezes — the two items are coupled through it.)
- **Release (@6010/@9165)**: unbind inputs, restore `bSimulatePhysics` from
  `compBodyInst` (or the freeze), superGun restores mobility, soft breaks constraint.
- No getData beyond prop_C's own — the gun itself is stateless between grabs.

### prop_physgun_soft_C : prop_physgun_C

`soft=true` preset + `charges`/`time` ints and its OWN getData/loadData + microwaveElec
(charge state persists). Same manipulator core, springy.

### prop_gravgun_C : prop_C — NOT a manipulator (surprise)

The uber contains NO force/impulse/grab application at all: `attract(Condition)` only
toggles `isActive` + beam/sound FX; tick pitches the hold-loop sound by the tracked
`obj` velocity; `microwaveElec` accumulates `charges` 1/s while active, and at
**>= 250 charges it deferred-spawns a `prop_physgun_soft_C` at its own transform and
destroys itself** (@831). The gravgun is a CHRYSALIS: an inert artifact that
microwave-charges into the soft physgun. Any perceived "pushing props" needs a live
check — nothing in this BP does it. [bytecode; the user-reported "толкает пропы" =
UNCONFIRMED against this dump]

## 2. Sync-axis table

| axis | owner | peers need | carried by |
|---|---|---|---|
| grab/release/freeze on a PROP | prop authority = HOST must execute the mutation (bSimulatePhysics flips, mobility, freeze persistence) | see the prop grabbed/frozen | GAP — same family as the existing grab/carry lane (puppet_carry_drive precedent) |
| held-object continuous drag (tick transform writes / constraint ref) | the MANIPULATING player simulates aim; but the WRITES land on a host-owned prop | prop pose at cadence | the prop lane already streams host prop poses — the write side must move host-ward |
| rotate/scale/snap modifiers | manipulating player (input state) | final transforms only | folded into the drag stream |
| beam FX + sounds | cosmetic | beam from puppet to object | display mirror (like chat bubbles: cheap, per-viewer) |
| freeze state persistence | host (prop save) | frozen props stay frozen for all | host-side execution gives it free |
| gravgun charge → physgun_soft conversion | host (identity swap of a saved prop!) | new actor appears | prop-lane identity handoff; flag at build |

## 3. Coop design (DESIGN — not built)

The physgun is NOT a new pattern — it is the **grab/carry problem at range**: continuous
kinematic writes by one player onto a host-authoritative prop. The prop lane already
answers the close-range version (grab intents + host-side carry drive + pose stream
back). The physgun rides the same rails:

1. Grab/release/freeze = eid-addressed intents (like grenade arm; forgery-guarded).
2. The drag: owner streams its pivot (camera + distance + rel + modifiers) at cadence;
   the HOST applies the native transform-compose to the prop (the same math, host-side);
   the prop pose returns to everyone via the existing prop stream. Client prediction of
   its own drag can come later if latency feels bad (MTA does owner-predicted vehicles
   the same way).
3. Freeze (bSimulatePhysics=false persistence) becomes correct automatically once the
   flips happen host-side.
4. Beam FX = cosmetic per-player display state (same slot family as the hook's
   player-phase aux state).
5. Scale (superGun) mutates a saved prop property — host-side by the same rule.
6. gravgun: nothing to sync live (FX + a charge counter that persists via its own
   getData); the @831 conversion is a host-side identity swap — make sure the prop
   identity map survives a class-swapping respawn (same family as grenade's rename).

## 4. Caveats

- The input-delegate self-binding (@3383) is on the OWNER's player only — a mirror must
  never install those binds (it would steal the viewer's scroll/E). Display mirror = FX
  only, zero input surface.
- RAW `BodyInstance.bSimulatePhysics :=` writes (not the setter!) — our
  no-raw-write-of-setter-managed-fields rule applies to OUR code; the game itself doing
  it means reads of that flag elsewhere may be stale — verify on the shipping binary if
  freeze-sync misbehaves.
- physgun vs hook coupling: hook's attach_a UNFREEZES frozen props; both features must
  resolve against the same host-side freeze state or they fight.
- `prop_physgun_s` variant asset unexamined (likely the superGun preset [?]).

## 5. Verification

2026-07-06 static RE: physgun uber read in full (grab/tick/release/freeze/rotate/scale
paths); physgun_soft = class diff + fn census; gravgun uber read in full (no force
code — conversion@250 confirmed). No live probe. Sync NOT BUILT.
