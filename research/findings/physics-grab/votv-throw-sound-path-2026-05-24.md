# VOTV throw-sound dispatch path — RE findings

**Date:** 2026-05-24
**Trigger:** new RULE `feedback_re_related_functions`: RE all related functions before implementing. User: "when an npc throws something there's a throw sound played - we must properly per rule 1 naturally emit that sound like when a local npc throws stuff".

## What sound actually fires when stuff is thrown in VOTV

USER CLARIFICATION 2026-05-24: **"The initial throw sound is not of collission system"** -- the throw sound the user is asking about is the AT-THROW whoosh, NOT the on-landing collision-impact sound. So Path A (collision sound) is NOT the answer; Path B (the `thrown` BP event) is the right dispatch.

Both paths still documented below for completeness:

### Path A: native physics impact sound (handled by UE4 engine, no BP code) -- NOT THE THROW SOUND

**Mechanism:** Every `UPrimitiveComponent` with a `UPhysicalMaterial` assigned has `DefaultImpactSound` (`USoundBase*`) at offset `+0x0030`. When the physics body collides with another surface above a velocity threshold, UE4's `FBodyInstance::OnHit` callback plays this sound automatically. **No BP code needed.**

Evidence:
- `Engine.hpp:16886`: `class USoundBase* DefaultImpactSound;`
- `Engine.hpp:16887`: `float LastImpactSoundTime;` (debounce so the same body doesn't spam-play)
- IDA strings `0x143bf4618 "impactSound"`, `0x1441f6cc8 "DefaultImpactSound"`, `0x1441f6ce0 "LastImpactSoundTime"` — confirms the runtime reflection / FName entries exist in the engine.

**Implication for coop:** when the receiver calls `AddImpulse` on a prop and re-enables `SimulatePhysics`, the prop falls, hits the floor, `FBodyInstance::OnHit` fires, `DefaultImpactSound` plays. **The landing-thud sound should already work** with the current implementation (commit `98f1a69`), assuming VOTV assigns a PhysicalMaterial to its prop static meshes (highly likely — it's standard UE4 practice).

### Path B: BP-defined throw-whoosh / event sound (if any)

**Mechanism:** `Aprop_C.thrown(AmainPlayer_C* Player)` (prop.hpp:166) is a BP event the prop graph CAN wire to play a throw-specific sound, spawn particle trail, etc. Whether VOTV's BP wires this to a sound is an open question — the BP bytecode lives in `.uasset` files, not in `.exe`, so it's not statically RE-able from IDA alone. A UE4SS Lua dump or hands-on test would settle it.

Caller path (host side):
- `mainPlayer_C.throwHoldingProp()` (mainPlayer.hpp:443) is the BP-pure player function that fires on throw input. It almost certainly calls `prop.thrown(self)` on the held prop. Being BP-pure, we can't ProcessEvent-observe it directly, but its OUTPUTS (the calls it makes inside its BP graph) DO dispatch through ProcessEvent — including `prop.thrown` and `AddImpulse`.

Evidence the `thrown` event exists and is called for throws (not just drops):
- The function is named `thrown` (past-tense event, signaling "this prop has just been thrown")
- Takes a `Player` param (so the prop knows who threw it)
- Distinct from `hit`, `unhooked`, `takenByPlayer` (other prop events on the same class)
- mainPlayer.hpp:751 has its own `void thrown(class AmainPlayer_C* Player);` — likely the player-side listener (delegate callback) that mirrors the prop's event

**Implication for coop:** if the receiver ALSO calls `prop.thrown(localPlayer)` after AddImpulse, any sound/particle/effect the BP wires to that event also fires on the receiver side. The cost is wrong attribution (the BP credits the local player for a remote throw) — semantically minor for sound/visual purposes.

### Path C: NPC-specific throw (per user's example "invisible alien")

VOTV NPCs like the alien throw stuff. They are not `AmainPlayer_C`, so they CANNOT call `prop.thrown(self)` with the right type. They must use one of:

- (most likely) call `AddImpulse` on the prop's mesh directly + let Path A's native impact sound fire on landing
- (possible) have their own NPC-specific throw function that plays a sound + calls AddImpulse
- (unlikely) call `prop.thrown(null)` and rely on the BP being null-safe

If the NPC uses ONLY native AddImpulse (Path A only), then a receiver that does the same will produce the same audio result. If the NPC uses a separate event for the whoosh sound, we'd need to find and dispatch that — TBD.

## Related throw-feature functions surveyed

| Function | Class | RE'd? | Notes |
|---|---|---|---|
| `Aprop_C.thrown(Player)` | Aprop_C BP | partial | BP body not statically RE-able; signature + name strongly imply sound + particle trail dispatch on call |
| `Aprop_C.hit()` | Aprop_C BP | partial | Likely the IMPACT BP event (different from native physics OnHit; this is a player-readable wrap) |
| `Aprop_C.impactSquishCPP(Component)` | Aprop_C native | **TODO** | CPP-suffix = native; squish damage on impact, likely plays a sound |
| `Aprop_C.impactDamageCPP(Damage, Hit, Actor, impact)` | Aprop_C native | **TODO** | CPP-suffix = native; damage on impact, likely plays a sound |
| `Aprop_C.hittedd123(Self, Other, Imp, Hit)` | Aprop_C BP | unknown | Collision callback (UE4 OnHit delegate signature). May play sound. |
| `Aprop_C.addDamage(...)` | Aprop_C BP | unknown | Damage accumulator on impact |
| `mainPlayer_C.throwHoldingProp()` | mainPlayer BP | partial | Pure inline; calls AddImpulse + prop.thrown internally; bytecode not visible statically |
| `mainPlayer_C.throwShit(comp, vel)` | mainPlayer BP | unknown | Generic-name throw -- may be the path NPCs/scripts call when throwing prop-class items |
| `mainPlayer_C.throwShovelItem(NewVel)` | mainPlayer BP | unknown | Shovel-specific |
| `mainPlayer_C.throwPath` input action | mainPlayer BP | input | The trajectory-preview key, not the actual throw |
| `mainPlayer_C.throwTrace[]` | mainPlayer field | data | TArray<UParticleSystemComponent*> -- the visual throw-arc particles (visual only, not audio) |
| `UPrimitiveComponent.AddImpulse(impulse, bone, bVel)` | engine native | **DONE** | The physics primitive; observable via PHC siblings; same exec-thunk pattern |
| `UPhysicalMaterial.DefaultImpactSound` | engine field | **DONE** | The natural impact-sound source; fired by FBodyInstance on collision; no BP code required |
| `UPhysicalMaterial.LastImpactSoundTime` | engine field | **DONE** | Debounce timer for impact-sound rate-limit |

## RE'd implementation decision

Given the RE evidence:

1. **Path A (native impact sound) is the highest-confidence "should work as-is"** — UE4 fires `DefaultImpactSound` automatically on the receiver when AddImpulse → prop falls → collision happens. NO additional code needed.

2. **Path B (`prop.thrown` BP event) is the explicit-call belt-and-suspenders** — calling it from the receiver after AddImpulse fires any BP-defined sound/particle dispatch the prop wires to the throw event. **Adding this is RULE-1 correct**: we're using the natural event the prop's BP exposes for this purpose, not a separate sound-trigger crutch.

3. **TODO follow-ups** if hands-on still has no sound:
   - RE `impactSquishCPP` + `impactDamageCPP` via IDA (native CPP-helpers; likely play sound on impact)
   - Test whether the prop being grabbed has a PhysicalMaterial assigned (UE4SS Lua probe)
   - If neither path fires sound, dump `Aprop_C.thrown`'s bytecode via UE4SS to see what it actually does

## Implementation plan (RE-grounded)

In `remote_prop::OnRelease(payload, localPlayer)`:
1. SetSimulatePhysics(true) on the released prop's mesh — re-enables Path A (engine impact sound).
2. If `impulse` non-zero: AddImpulse on the mesh — gives the prop velocity so Path A's collision-impact fires when it lands.
3. If `impulse` non-zero: call `prop.thrown(localPlayer)` — fires Path B's BP-defined sound/particle dispatch.

`localPlayer` ptr threaded from event_feed::Update via NetPumpTick (which has g_netLocal already cached).

## Cross-refs

- [[feedback-re-related-functions]] -- the rule that made me write this doc
- [[feedback-audit-every-time]] -- audit-fix cycle that surfaced 9+ bugs in the wire layer
- `research/findings/votv-physics-interaction-deep-re-2026-05-23.md` -- Stage 1 RE of the grab path
- `research/findings/physics-object-pickup-architecture-2026-05-23.md` -- original architect blueprint
