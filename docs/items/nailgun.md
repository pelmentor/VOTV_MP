# nailgun / nail — pin props to the world (and to each other)   (STATUS: RE)

The construction fastener family: shoot (or hand-press) a nail through a prop into a
surface — a PhysicsConstraint pins them; nails persist in the save. Three actors:

- **`prop_nailgun_C` : prop_C** — the gun ITEM. One handler (`playerHandUse_RMB`, uber
  @10): `player.tryGetNail()` (consumes a nail from the player's inventory, returns its
  class) → deferred-spawn **`nailProjectile_C`** at the CAMERA transform with
  `force = cameraForward × 10000`, `nailType = <consumed nail class>`. No other state —
  the gun itself is stateless. [bytecode]
- **`prop_nail_C` : prop_C** — the nail ITEM (hand-held, `type` int + `nailClass`).
  LMB (uber @10): `player.useArm()` trace → deferred-spawn `nailClass` at hit loc+normal
  with camera rotation → **`attach(false)`** on the spawned actor → on success the held
  item DESTROYS ITSELF (consumed). [bytecode]
- **`nailProjectile_C` : Actor** — TRANSIENT ballistic (not saved). BeginPlay @2086:
  velocity := `force`. Tick @15: rotate to velocity, line-trace `lastLoc → now`
  (statDynPhys set); on hit: deferred-spawn `nailType` at hit −fwd (`length=30`) →
  **`nailNail(false)`**, then if the hit component simulates physics →
  `AddForceAtLocation(velocity/mass)` (the shot IMPULSE), destroy self. [bytecode]
- **`nail_C` : actor_save_C** — the DEPLOYED nail (persistent). Same two-end family as
  hook_C: `actor_A/B`, `comp_A/comb_B`, `actor_A_key/actor_B_key` (getKey),
  `pinLoc_A/B`+`pinRot_A/B`+`pinNormal` (actor-local), `PhysicsConstraint`, `nailed`,
  `broken`, `static`, `depth/depthOut`, `nailDrop` (pickup class), `addDamage` →
  `broken=true` path, constraint-broken delegate. getData/loadData + `processKeys`
  (getObjectFromKey + frameDelay retry — same restore shape as hook). [bytecode]

### The two attach modes (both SELF-TRACING — the coop gift)

- **`attach(a)`** — single-surface pin: traces along the nail's OWN axis (`n()` gives
  the segment), first hit → `actor_A` + AttachToActor + `actor_A_key`; no hit → destroy
  + `fail=true`. [bytecode @0-900]
- **`nailNail(a)`** — TWO-body pin: two traces along its own axis; first hit = `actor_A`/
  `comp_A` (attach + pinLoc_A/pinNormal in actor_A space), second = `actor_B`/`comb_B`
  (Landscape → null → `static=true`); `nailed=true`;
  `PhysicsConstraint.SetConstrainedComponents(comp_A, comb_B-or-own-mesh)`;
  `stickNoise()`. Fail → destroy. [bytecode @0-6673]

**Neither needs a HitResult** — a nail actor placed at the right TRANSFORM re-derives
everything by tracing. loadData restores exactly this way. This makes host-side replay
of a client's shot a native one-liner: spawn at reported transform → call the same fn.

## 2. Sync-axis table

| axis | owner | peers need | carried by |
|---|---|---|---|
| gun/nail ITEMS (hand, inventory, ammo `tryGetNail`) | owning player | hand visuals only | prop + inventory lanes (existing) |
| projectile flight (~0.05-0.3 s at 10000 u/s) | firer today (LOCAL-ONLY) | muzzle flash / tracer at most | cosmetic; skip v1 or a fire-event |
| shot impulse on dynamic props (AddForceAtLocation) | MUST be host (prop authority) | prop motion via prop lane | GAP today |
| nail_C spawn + constraint (persistent world mutation) | MUST be host | mirrored nail actor | GAP today |
| nail damage/broken/detach + nailDrop pickup spawn | host (it owns the actor) | state + pickup via prop lane | follows host ownership |
| persistence / late-join | host save (actor_save_C native) | joiner gets nails from save | free once host-owned |

## 3. Coop design (DESIGN — not built)

Pure **commit-intent → host-authoritative** case (no player-attached phase at all —
the missing half that hook can't exercise):

1. Owner presses the trigger: ammo consume + montage stay OWNER-LOCAL (`tryGetNail` is
   the player's own inventory). Owner sends a small reliable intent:
   `NailShot{cameraTransform, nailTypeRow}` (gun) or `NailPlant{transform, nailClass}`
   (hand nail). Owner does NOT let its local spawn survive (suppress or never-spawn —
   exact seam picked at build; the spawn sites are the two BeginDeferredActorSpawn
   calls above).
2. Host validates + replays NATIVELY: spawn `nailProjectile_C` with the same
   force/transform (gun path — impulse and nail spawn then happen host-side, natively)
   or spawn `nailClass` + `attach(false)` (plant path). Self-tracing does the rest.
3. `nail_C` mirrors host→clients like other host-owned world actors (spawn event +
   transform + type; the constraint re-derives via the same native call on... NO — the
   MIRROR must NOT re-run nailNail: it would create a second live constraint fighting
   the prop lane. Mirror = display-only mesh at the streamed/committed pose, constraint
   lives host-side only; prop motion arrives via the prop lane).
4. Late-join: host save carries nails natively (actor_save_C) — same answer as hook's
   anchored phase.

Divergence-of-outcome note (host replays the trace, not the client's hit): host and
client may resolve slightly different hit points under motion. Acceptable — host is
truth, the client sees the hosted outcome; identical to how MTA treats weapon impact.

## 4. Caveats

- The projectile applies the impulse CLIENT-LOCAL today — in coop without the handoff a
  client's nailgun shot would shove props only on their screen (classic prop divergence).
- `tryGetNail` couples the shot to inventory — the intent must be sent only when the
  owner's consume succeeded, or ammo desyncs from outcomes.
- `nail_C.addDamage` → broken → constraint break + `nailDrop` pickup spawn — pickup
  spawn is a dupe seam; host-only once the actor is host-owned.
- Nail variants: `prop_nail.type` int + per-type `nailClass` — enumerate the variants
  (census) before shipping the intent row map.

## 5. Verification

2026-07-06 static bytecode RE only (prop_nailgun/prop_nail/nailProjectile ubers +
nail_C attach/nailNail/processKeys/setDepth/assign read; nail uber skimmed for
broken/destroy paths). No live probe. Sync NOT BUILT.
