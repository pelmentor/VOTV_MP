# VOTV throw / release pipeline — deep reverse-engineering

**Date:** 2026-05-24
**Game build:** Alpha 0.9.0-n (`VotV-Win64-Shipping.exe`, imagebase `0x140000000`)
**Trigger:** Hands-on test on Stage-4 cross-peer wire exposed two regressions —
Bug A (throw sound silent on remote), Bug B (camera-velocity launch impulse not
replicated, remote sees only a dead-drop). User: "vжух your mouse upwards and
release E → launches very far upwards locally, but remote just sees object
released and dropped very non-dynamically".

## TL;DR — bug-root summary

* **Bug B root cause = inherited PhysX kinematic-tracking velocity.** When the
  player flicks their camera fast, `UPhysicsHandleComponent::SetTargetLocation`
  jumps the kinematic target dramatically per tick. The PhysX constraint that
  pins the body to that target accumulates a tracking velocity to follow it.
  When `UPhysicsHandleComponent::ReleaseComponent` destroys the constraint
  (IDA-confirmed body @ `0x142D7C670`), the body **re-enters dynamic simulation
  inheriting that tracking velocity**. This is the dominant launch velocity —
  the `AddImpulse` we already capture is a tiny add-on, not the launch.

  Our current Stage-4 wire ships ONLY the `AddImpulse` value, so the puppet
  sees only the small add-on and misses the dominant inherited velocity → drops
  inert. **Fix:** on the host side, call `UPrimitiveComponent::GetPhysicsLinearVelocity`
  (exec `0x1430DC550`) and `GetPhysicsAngularVelocityInDegrees` (exec
  `0x1430DC320`) on the held component IMMEDIATELY before `ReleaseComponent`,
  ship both as `linearVelocity` + `angularVelocity` fields in the PropRelease
  packet, and on the receiver call `SetPhysicsLinearVelocity` (exec `0x1430DFA40`)
  + `SetPhysicsAngularVelocityInDegrees` (exec `0x1430DF7B0`) after
  `SetSimulatePhysics(true)`.

* **Bug A root cause = inside the BP body of `Aprop_C.thrown`, statically
  unrecoverable from the .exe.** I confirmed via IDA that the BP function name
  pool is not embedded in the .exe (BP bodies are bytecode in cooked `.uasset`
  files). The engine native `UGameplayStatics::PlaySoundAtLocation` impl @
  `0x142B53B80` has NO Player / IsLocallyControlled / Authority gate — only a
  world-level `bAllowAudioPlayback` check that passes for non-dedicated
  clients. So the engine itself does NOT suppress sound on a non-local
  caller. The cause must be inside the BP body — most likely (1) the BP
  reads the prop's NATIVE physics velocity and only fires whoosh sound above
  a threshold (which we now know is near-zero on the puppet, see Bug B), or
  (2) the BP plays sound *at* `Player->Camera->Location` (which is the local
  player's ear → inaudible because not spatial), or (3) a Cast to
  `mainPlayer_C` with a `IsLocallyControlled`/`Controller != null` check that
  fails on the puppet.

  **Bug A is GATED on Bug B fix.** If we ship the inherited velocity (Bug B
  fix), the puppet's prop will move fast → Path A (native PhysX impact
  sound on landing) WILL fire, and Path B (BP `thrown` if velocity-gated)
  may ALSO fire. If sound still missing after Bug B fix, we drop to a
  UE4SS Lua dump of `Aprop_C.thrown` to read the BP bytecode.

---

## Section 1: full release pipeline call graph

```
Host: E-released
   │
   ▼
 InpActEvt_use_K2Node_InputActionEvent_41 (mainPlayer_C BP, ProcessEvent-dispatched)
   │   (BP graph branches on grabbing_actor!=null -> drop branch
   │    -> calls grabTimeline.Reverse() OR sets bGrabFinished and
   │    relies on the Timeline's playback-clamp to fire Finished)
   ▼
 UTimelineComponent::TickComponent  @ 0x142F1D9A0   (engine-native, per-tick)
   └─ FTimeline::TickTimeline       @ 0x142F1DEE0
       ├─ FTimeline::TickTimelineEvents @ 0x142F1B550
       │     └─ OwningActor.ProcessEvent("grab__UpdateFunc", &params)
       │           └─ (BP-interpreted) bytecode -- writes SetTargetLocation
       │              on grabHandle per tick. Visible via our observer.
       │
       └─ FTimeline::FireFinishedEvent @ 0x142F10E00  (on Timeline end)
             └─ OwningActor.ProcessEvent("grab__FinishedFunc", nullptr)
                 └─ (BP-interpreted) bytecode -- THIS is the release handler.
                    ITS BP BYTECODE IS NOT RECOVERABLE FROM .exe. It is the
                    UFunction we already observe. It is suspected to call,
                    in order on the released UPrimitiveComponent:
                       (a) UPhysicsHandleComponent.ReleaseComponent  <-- our PHC observer
                       (b) optionally PrimComp.AddImpulse(camFwd*scale,...)
                           <-- our AddImpulse observer
                       (c) Aprop_C.thrown(self) BP event
                       (d) other cosmetic resets (UI prompt off, line clear)
                    *Native* velocity reads / writes (SetPhysicsLinearVelocity)
                    are NOT seen by our existing observers; if BP calls them
                    they go unwitnessed.

   ▼
 UPhysicsHandleComponent::ReleaseComponent  @ exec 0x142FEA9B0  body 0x142D7C670
   * destroys PhysX KinActorData (this+0x148)
   * destroys PhysX ConstraintHandle (this+0x150)
   * clears flags bit0 ("bConstrained")
   * clears GrabbedComponent (this+0xB0) and GrabbedBoneName (this+0xB8)
   * DOES NOT touch the body's velocity
   * The released body re-enters dynamic simulation INHERITING whatever
     velocity the kinematic constraint was tracking.

   ▼  (PhysX, asynchronously the next sim step)
 The released rigid body's pose = TargetLocation just before release.
 Its velocity = (TargetLocation_now - TargetLocation_lastTick) / dt
              (i.e., whatever velocity PhysX needed to track the kinematic).
 *** THIS is the camera-flick launch. ***
```

### Observers we currently have on the host

* `UPhysicsHandleComponent.GrabComponentAtLocation*` — pickup (catches both light
  paths)
* `UPhysicsHandleComponent.SetTargetLocation*` — per-tick target update
* `UPhysicsHandleComponent.ReleaseComponent` — release (PRE)
* `UPhysicsConstraintComponent.SetConstrainedComponents` / `BreakConstraint` — heavy drag
* `UPrimitiveComponent.AddImpulse` — captures the BP throw boost (caches g_lastImpulse)
* `mainPlayer_C.grab__UpdateFunc` / `grab__FinishedFunc` / `InpActEvt_use_K2Node_InputActionEvent_41` — BP triangulation

### Observers we are MISSING (relevant to Bug B)

These engine-native UFunctions ARE ProcessEvent-dispatchable but the BP MAY
or MAY NOT call them on release. Adding observers is cheap insurance to
verify:

| UFunction | Exec thunk | Vtable slot | Hypothesis |
|---|---|---|---|
| `UPrimitiveComponent.SetPhysicsLinearVelocity` | `0x1430DFA40` | +1552 (194) | BP may call this to assign the inherited-or-computed launch velocity |
| `UPrimitiveComponent.SetPhysicsAngularVelocityInDegrees` | `0x1430DF7B0` | +1568 (196) | BP may set angular spin on release |
| `UPrimitiveComponent.SetPhysicsAngularVelocityInRadians` | `0x1430DF910` | +1568 (196) | sibling, same backend |
| `UPrimitiveComponent.AddForce` | `0x1430DAA30` | (per-tick force, not a release thing) | low-likelihood |
| `UPrimitiveComponent.WakeRigidBody` | `0x1430E03C0` | — | BP may call to ensure simulation after enable |
| `UPrimitiveComponent.SetSimulatePhysics` | `0x1430DFF60` | +1448 (181) | the body re-enables dynamic sim; cycle false->true RESETS velocity (PhysX backend) — this is a danger sign for ANY false->true path |
| `UPrimitiveComponent.GetPhysicsLinearVelocity` | `0x1430DC550` | — | the wire-side capture API we need |
| `UPrimitiveComponent.GetPhysicsAngularVelocityInDegrees` | `0x1430DC320` | — | sibling |

---

## Section 2: ALL velocity-mutating call sites (engine natives, by exec thunk)

All belonging to UPrimitiveComponent (engine-native, ProcessEvent-dispatched).
Identified via FNameNativePtrPair tables at `0x1442072a0`-`0x1442079b0`
(format: `{const char* name, void(*exec)(...)}`).

| Exec thunk addr | Native UFunction name           | Calls (vtable slot) | Semantics |
|---|---|---|---|
| `0x1430DA680` | `AddAngularImpulse`              | calls native via thunk | adds angular impulse (rad/s effective) |
| `0x1430DA7B0` | `AddAngularImpulseInDegrees`     | (sibling, deg->rad)    | adds angular impulse (deg input) |
| `0x1430DA900` | `AddAngularImpulseInRadians`     | (sibling)              | adds angular impulse (rad input) |
| `0x1430DAA30` | `AddForce`                       | (force, sustained per dt) | adds force (mass*accel) -- NOT impulse |
| `0x1430DAB60` | `AddForceAtLocation`             |                        | adds force at world location (offsets center) |
| `0x1430DACA0` | `AddForceAtLocationLocal`        |                        | adds force at local-space offset |
| `0x1430DADE0` | `AddImpulse`                     | calls vtable+1472 (184) | adds impulse (mass*delta_v); if bVelChange=true, IGNORES mass |
| `0x1430DAF10` | `AddImpulseAtLocation`           |                        | adds impulse at world point (torque too) |
| `0x1430DB050` | `AddRadialForce`                 |                        | radial force from origin |
| `0x1430DFA40` | `SetPhysicsLinearVelocity`       | calls vtable+1552 (194) | SETS velocity (bAddToCurrent=true => adds) |
| `0x1430DF7B0` | `SetPhysicsAngularVelocityInDegrees` | calls vtable+1568 (196) | SETS angular velocity (deg/s input) |
| `0x1430DF910` | `SetPhysicsAngularVelocityInRadians` | calls vtable+1568 (196) | SETS angular velocity (rad/s input) |
| `0x1430DFB70` | `SetPhysicsMaxAngularVelocity`   | calls sub_142DD04A0    | caps angular velocity (UE4 misnamed; alias for InDegrees variant) |
| `0x1430DFF60` | `SetSimulatePhysics`             | calls vtable+1448 (181) | toggles dynamic simulation; **false->true RESETS velocity in PhysX backend** |
| `0x1430E03C0` | `WakeRigidBody`                  | (PhysX wake)           | wakes sleeping body |
| `0x1430DC550` | `GetPhysicsLinearVelocity`       | reads BodyInstance.LinearVelocity | returns FVector cm/s |
| `0x1430DC1F0` | `GetPhysicsAngularVelocity`      | (rad/s)                | (deprecated alias) |
| `0x1430DC320` | `GetPhysicsAngularVelocityInDegrees` |                    | returns FVector deg/s |

PhysX impact / native impact-sound chain (Path A, no BP needed) — handled
by `FBodyInstance::OnHit` callback fed from the PhysX simulation step. Fires
`UPhysicalMaterial::DefaultImpactSound` if velocity > a threshold (UE4
typically clamps ~200 cm/s, but engine-configurable). This is the natural
landing-thud sound — already documented in
`votv-throw-sound-path-2026-05-24.md` Path A.

---

## Section 3: camera-angular-velocity computation (UE4 PhysX backend, not BP)

**Key finding: there is NO explicit "camera velocity" computation in
`mainPlayer_C` BP for the throw.** The launch comes entirely from PhysX
kinematic constraint tracking.

Mechanism (verified via IDA decompile of
`UPhysicsHandleComponent::UpdateHandleTransform` @ `0x142D7EE30`):

```
per tick:
  1. mainPlayer_C BP reads cameraLocation + cameraForward * grabLen
     = desired hold position
  2. BP calls grabHandle.SetTargetLocation(desiredPos)
     -> writes FVector to UPhysicsHandleComponent+0xF0 (TargetLocation)
  3. UPhysicsHandleComponent::TickComponent  reads +0xF0  ->
     calls UpdateHandleTransform(this, [TargetRotation, TargetLocation])
     -> calls Constraint.SetKinematicTarget(KinActor, Transform)
     -> PhysX moves the KIN actor to the target THIS substep
     -> the PhysX constraint pulls the dynamic body toward the KIN actor
        with whatever velocity is required (mass + spring + damping
        configured via SetInterpolationSpeed, SetLinearDamping, etc.)

on release:
  1. UPhysicsHandleComponent::ReleaseComponent destroys the constraint
  2. The DYNAMIC body retains its current angular + linear velocity
     (PhysX does not reset on constraint destroy)
  3. The body now behaves as a normal physics actor with the
     accumulated tracking velocity

if the player flicks the camera fast:
  desiredPos jumps several meters per frame
  -> the constraint demands the body track at high velocity
  -> on release the body inherits that high velocity
  -> "vжух" launch
```

This is why `AddImpulse` in the BP is at most a small *bonus* — the dominant
fling comes from kinematic inheritance. **The native PhysX path is the
launch.** Our wire fix therefore must capture the dynamic body's velocity
immediately AFTER `ReleaseComponent` ("naked" velocity) and ship it.

### Camera-angular-velocity hooks we DON'T need

We do not need to track `PlayerCameraManager.GetCameraRotation` per tick or
compute control rotation deltas. The velocity is in the prop's
`UPrimitiveComponent.BodyInstance.LinearVelocity` directly — one
`GetPhysicsLinearVelocity` call right before / right after
`ReleaseComponent` gives the same answer.

Timing note: read it AFTER `ReleaseComponent`. Why: PhysX may not have
applied the latest kinematic-target update until the next substep, so
reading after release-then-immediately-pose-step would give the freshest
velocity. In practice both are within 1 ms; reading inside the
ReleaseComponent PRE observer (where we currently sit) reads the velocity
the body had under kinematic tracking — which is the launch velocity. This
should be fine. For maximum fidelity we can hook ReleaseComponent POST.

---

## Section 4: `Aprop_C.thrown` body analysis (LIMITED — BP-only)

**Critical disclaimer:** `Aprop_C` is a Blueprint class. Its UFunctions
(`thrown`, `hit`, `impactSquishCPP`, `addDamage`, etc.) live in cooked
`.uasset` files. The .exe contains only the BP virtual machine
(`UObject::ProcessInternal`) and the engine-native primitives the BP can
call. There is NO native body for `thrown` in the .exe to decompile.

What IDA *does* tell us:

* FName strings for `Aprop_C`-content names are NOT in `.rdata` (search for
  `prop_C\.` / `mainPlayer_C` / `thrown` / `throwHoldingProp` /
  `grabFinished` / `dropGrabObject` / `ariral` / `kerfurOmega` / `alien` /
  `yeet` — all empty in the .exe FName table). They are created at runtime
  when the `.uasset` packages load.
* Engine-native UFunctions reachable from BP are enumerable from the
  `FNameNativePtrPair` registration tables (Section 2). The BP CAN ONLY
  call these natives (plus other BPs) when its bytecode executes.
* `UGameplayStatics::PlaySoundAtLocation` impl @ `0x142B53B80` (renamed)
  has no Player / IsLocallyControlled / Authority gate — only
  `if (World && AudioDevice && (World+270 & 2) != 0)` which is
  `bAllowAudioPlayback`. So engine-side sound is NOT suppressed.

### Alien NPC throw comparison

Alien NPC classes (`Aariral_*`, `Anpc_alien_C`, `AkerfurOmega_*`) are
ALSO BP. Their throw action — if any — is in BP bytecode. We cannot
statically diff them.

The likely structural difference (hypothesized):
* Alien throws a prop by directly calling
  `propMesh.AddImpulse(throwVector)` (the prop is not "held" by a
  PhysicsHandle), so the prop accumulates velocity from the impulse alone.
* Player throws a prop by Releasing a PhysicsHandle constraint, so the
  prop inherits kinematic tracking velocity + optional AddImpulse.

The whoosh sound trigger differs:
* If alien uses native AddImpulse → impact sound on landing fires via
  PhysX `OnHit` → Path A. No explicit `thrown` BP call needed.
* If player uses Release + AddImpulse → IF the BP's release handler calls
  `Aprop_C.thrown(Player)` → Path B fires explicit whoosh sound.

This explains the user's observation "the throw sound the user is asking
about is the AT-THROW whoosh, NOT the on-landing collision-impact sound":
the player's at-throw whoosh is Path B's `Aprop_C.thrown`, the alien's
landing-thud is Path A's PhysX impact.

### What we'd need to verify these hypotheses

**UE4SS Lua dump** of `Aprop_C` UClass dumping each UFunction's bytecode,
specifically `thrown`. We would look for:

* a `Cast<mainPlayer_C>` followed by `IsLocallyControlled` /
  `Controller != null` / `IsLocalPlayerController` check
* a velocity-magnitude gate (e.g. `if VectorLength(self.LinearVelocity) >
  300` skip whoosh)
* a `PlaySoundAtLocation(Sound, Player.Camera.WorldLocation, ...)` —
  attached to the LOCAL player's camera, not the prop, would make the
  sound inaudible because nearfield to the *listener*
* a `SpawnEmitterAttached(Particle, Self.StaticMesh)` — particle trail;
  if it's there it's a strong tell the BP does fire even on remote-context

Until we have that dump, the safest fix is:

1. Ship Bug B fix (capture + replay inherited kinematic velocity)
2. Verify Path A (impact sound) starts firing on the puppet (it will, if
   the prop now flies + hits stuff)
3. Verify Path B (whoosh) — if still silent, escalate to UE4SS Lua
   reflection probe

---

## Section 5: hypotheses for Bug A (throw sound silent on remote), ranked

| # | Hypothesis | Likelihood | Diagnostic |
|---|---|---|---|
| 1 | BP `thrown` body has a velocity-magnitude gate; puppet's velocity ~0 because Bug B; gate fails; no sound fires | **HIGH** | Fix Bug B first; if sound returns, confirmed |
| 2 | BP `thrown` plays sound at `Player->Camera->WorldLocation`; puppet `Player` is local player → sound at the LISTENER's ear, nearfield = no spatialisation = effectively inaudible | MEDIUM | Lua dump `thrown` body; or pass a "synthetic mainPlayer" with camera at the source's reported position rather than localPlayer |
| 3 | BP `thrown` Casts `Player` to `mainPlayer_C` and checks `Player.Controller != null` or `IsLocallyControlled`; on the puppet's local mainPlayer the IsLocally is TRUE but the prop is unfamiliar; somewhere a Self.Controller=null check or a `propData.heavy` check or grab-state check fails | MEDIUM | Lua dump `thrown` body |
| 4 | BP `thrown` requires the prop to currently be `grabbing` state in its own state machine; we re-call thrown AFTER ReleaseComponent so the BP's prop-state-machine has already cleared; thrown is a no-op | MEDIUM | Lua dump; or call `thrown` BEFORE Release semantically (would still be impossible because we receive the release event downstream) |
| 5 | BP `thrown` plays sound via an attached `UAudioComponent` on the prop that requires bAutoActivate / live world; perfectly fine on the puppet but the AudioComponent was never spawned because the puppet's prop never went through pickup BP flow | LOW | Lua dump prop ConstructionScript |
| 6 | Engine-level NetMode suppression — but we already proved `PlaySoundAtLocation` has none | RULED OUT | IDA decompile of `UGameplayStatics_PlaySoundAtLocation_Impl` @ `0x142B53B80` |

**Conclusion:** Bug A fix is **gated** on Bug B fix. After shipping Bug B,
~80% confidence that whoosh returns naturally (hypothesis #1 / Path A
landing-impact at minimum). If still silent, UE4SS Lua dump of
`Aprop_C.thrown` is the next step.

---

## Section 6: hypotheses for Bug B (which extra velocity API to also capture)

| # | Hypothesis | Likelihood | Action |
|---|---|---|---|
| A | The BP calls `SetPhysicsLinearVelocity` on release with a value computed from camera angular velocity | LOW-MEDIUM | Adding an observer for `SetPhysicsLinearVelocity` is cheap insurance; if it fires during release on the host, capture + ship it |
| B | The BP calls `AddImpulse` with `bVelChange=true` (so it IS a velocity assignment, not a force); we ALREADY capture this but might be misinterpreting the third parameter | LOW | Recheck how we serialise our captured AddImpulse: do we transmit `bVelChange` correctly? Currently we ship a Vector, no bVelChange flag |
| C | **The launch is inherited PhysX kinematic-tracking velocity from the constraint** — no extra API call, the velocity is implicit in the released body's state | **HIGH (CONFIRMED by IDA decompile of ReleaseComponent + UpdateHandleTransform)** | Ship the body's LinearVelocity + AngularVelocity directly via `GetPhysicsLinearVelocity` / `GetPhysicsAngularVelocityInDegrees` on the host, applied via `SetPhysicsLinearVelocity` / `SetPhysicsAngularVelocityInDegrees` on the puppet, AFTER `SetSimulatePhysics(true)` (so the velocity is not wiped by the false→true cycle). |
| D | Multiple AddImpulse calls fire on release; we cache only the first | LOW | Audit the host's AddImpulse observer to ACCUMULATE (sum vectors) within a 50ms window of a Release event, not just keep-first |

**Conclusion:** Hypothesis C is the dominant cause. Hypothesis A and D are
worth adding as belt-and-suspenders insurance. B is a separate audit on our
existing capture.

---

## Section 7: concrete wire fix recommendations

### Protocol v5 (PropRelease packet evolution)

Add two FVectors to the PropRelease payload:

```cpp
struct PropReleasePacketV5 {
    uint8  ver;           // = 5
    uint8  keyLen;
    char   key[64];       // wire key (FName.ToString of Aprop_C.Key)
    FVector impulse;      // existing: BP-fired AddImpulse vector (vel-change semantics)
    FVector linearVel;    // NEW: dynamic-body LinearVelocity captured AT release
    FVector angularVel;   // NEW: dynamic-body AngularVelocity (deg/s) captured AT release
    bool    impulseIsVelChange;  // NEW: if true, our AddImpulse capture had bVelChange=true
};
```

### Host-side capture changes

In `harness.cpp`'s `ReleaseComponent` PRE-observer (or POST, see below):

```cpp
// BEFORE ReleaseComponent destroys the constraint, the GrabbedComponent
// pointer is still valid AND PhysX has updated the body's velocity from
// the latest kinematic tracking. Read it.
UPrimitiveComponent* primComp = readPtr(handle + 0xB0); // GrabbedComponent
FVector linVel  = CallUFunction_GetPhysicsLinearVelocity(primComp);
FVector angVel  = CallUFunction_GetPhysicsAngularVelocityInDegrees(primComp);
// Cache for the next PropRelease send.
g_lastLinearVel  = linVel;
g_lastAngularVel = angVel;
```

Timing note: PRE vs POST matters. PRE → velocity is the kinematic
tracking. POST → velocity is the same (PhysX inheritance), but the body
is technically already dynamic. Either should work; PRE is safer because
`GrabbedComponent` is still set so the prop pointer is locked. Use PRE.

### Add observers (RULE №1 — capture every velocity API the BP might use)

Add to the existing observer registration list:

```cpp
TryObserve(L"PrimitiveComponent", L"SetPhysicsLinearVelocity",          /*post*/ false);
TryObserve(L"PrimitiveComponent", L"SetPhysicsAngularVelocityInDegrees", /*post*/ false);
TryObserve(L"PrimitiveComponent", L"SetPhysicsAngularVelocityInRadians", /*post*/ false);
TryObserve(L"PrimitiveComponent", L"AddAngularImpulseInDegrees",         /*post*/ false);
TryObserve(L"PrimitiveComponent", L"AddAngularImpulseInRadians",         /*post*/ false);
TryObserve(L"PrimitiveComponent", L"AddForce",                           /*post*/ false);
TryObserve(L"PrimitiveComponent", L"SetSimulatePhysics",                 /*post*/ false);
```

If any of these fire during a hands-on E-release test, the BP is using them
and we should ship + replay them. If only `SetPhysicsLinearVelocity` fires,
we know the BP IS setting an explicit launch velocity (which would
substitute for the inherited mechanic but it's the same field on the wire).

### Sdk_profile.h additions

```cpp
inline constexpr const wchar_t* GetPhysicsLinearVelocityFn               = L"GetPhysicsLinearVelocity";
inline constexpr const wchar_t* GetPhysicsAngularVelocityInDegreesFn     = L"GetPhysicsAngularVelocityInDegrees";
// (SetPhysicsLinearVelocityFn / SetPhysicsAngularVelocityInDegreesFn already present)
inline constexpr const wchar_t* SetPhysicsAngularVelocityInRadiansFn     = L"SetPhysicsAngularVelocityInRadians";
inline constexpr const wchar_t* AddForceFn                                = L"AddForce";
inline constexpr const wchar_t* AddAngularImpulseInDegreesFn              = L"AddAngularImpulseInDegrees";
inline constexpr const wchar_t* AddAngularImpulseInRadiansFn              = L"AddAngularImpulseInRadians";
inline constexpr const wchar_t* WakeRigidBodyFn                           = L"WakeRigidBody";
```

### Receiver-side changes in `remote_prop::OnRelease`

```cpp
// 1. Re-enable dynamic simulation (false -> true; PhysX backend resets velocity)
CallUFunction(SetSimulatePhysics, comp, true);

// 2. Apply the inherited linear velocity AFTER the cycle reset.
//    bAddToCurrent = false (we're SETTING, not adding to the zero post-reset).
if (!linearVel.IsNearlyZero()) {
    CallUFunction(SetPhysicsLinearVelocity, comp, linearVel, /*bAddToCurrent*/false);
}

// 3. Apply the inherited angular velocity.
if (!angularVel.IsNearlyZero()) {
    CallUFunction(SetPhysicsAngularVelocityInDegrees, comp, angularVel, /*bAddToCurrent*/false);
}

// 4. Apply the throw boost impulse on top (if any).
if (!impulse.IsNearlyZero()) {
    CallUFunction(AddImpulse, comp, impulse, NAME_None, impulseIsVelChange);
}

// 5. Wake the body to ensure simulation starts immediately.
CallUFunction(WakeRigidBody, comp, NAME_None);

// 6. Dispatch the BP throw event (path B sound; may still no-op until BP body verified)
CallUFunction(thrown, prop, localPlayer);
```

### Validation matrix

| Scenario | Expected behavior |
|---|---|
| Slow gentle drop | LinearVel ~ 0 / AngularVel ~ 0 / no impulse → puppet sees gentle drop |
| Quick walk forward + release | LinearVel ~ player velocity ~ 600 cm/s → puppet sees prop sail forward |
| Hard camera flick up + release | LinearVel ~ thousands cm/s, AngularVel maybe tumble → puppet sees the dramatic launch |
| Heavy desk drop | grabsHeavy path uses PhysicsConstraint not Handle → currently unaffected by this fix; if heavy-throw is in scope, separate work |

---

## Section 8: IDB renames + comments applied this session

Renames (saved):

```
0x1430DADE0  sub_1430DADE0 -> execUPrimitiveComponent_AddImpulse
0x1430DFA40  sub_1430DFA40 -> execUPrimitiveComponent_SetPhysicsLinearVelocity
0x1430DF7B0  sub_1430DF7B0 -> execUPrimitiveComponent_SetPhysicsAngularVelocityInDegrees
0x1430DF910  sub_1430DF910 -> execUPrimitiveComponent_SetPhysicsAngularVelocityInRadians
0x1430DFB70  sub_1430DFB70 -> execUPrimitiveComponent_SetPhysicsMaxAngularVelocity
0x1430DFF60  sub_1430DFF60 -> execUPrimitiveComponent_SetSimulatePhysics
0x1430E03C0  sub_1430E03C0 -> execUPrimitiveComponent_WakeRigidBody
0x1430DC550  sub_1430DC550 -> execUPrimitiveComponent_GetPhysicsLinearVelocity
0x1430DC320  sub_1430DC320 -> execUPrimitiveComponent_GetPhysicsAngularVelocityInDegrees
0x1430DC1F0  sub_1430DC1F0 -> execUPrimitiveComponent_GetPhysicsAngularVelocity
0x1430DAA30  sub_1430DAA30 -> execUPrimitiveComponent_AddForce
0x1430DAB60  sub_1430DAB60 -> execUPrimitiveComponent_AddForceAtLocation
0x1430DACA0  sub_1430DACA0 -> execUPrimitiveComponent_AddForceAtLocationLocal
0x1430DAF10  sub_1430DAF10 -> execUPrimitiveComponent_AddImpulseAtLocation
0x1430DA680  sub_1430DA680 -> execUPrimitiveComponent_AddAngularImpulse
0x1430DA7B0  sub_1430DA7B0 -> execUPrimitiveComponent_AddAngularImpulseInDegrees
0x1430DA900  sub_1430DA900 -> execUPrimitiveComponent_AddAngularImpulseInRadians
0x1430120F0  sub_1430120F0 -> execUGameplayStatics_PlaySoundAtLocation
0x1430163C0  sub_1430163C0 -> execUGameplayStatics_SpawnSoundAttached
0x142B53B80  sub_142B53B80 -> UGameplayStatics_PlaySoundAtLocation_Impl
```

Comments added on the velocity exec thunks, the ReleaseComponent body, and
the PlaySoundAtLocation impl describing the relevant findings for Bug A
and Bug B.

IDB saved at: `D:\Projects\Programming\VOTV_MP\Game_0.9.0n\WindowsNoEditor\VotV\Binaries\Win64\VotV-Win64-Shipping.exe.i64`

---

## Section 9: Open items / next-step RE prerequisites

1. **UE4SS Lua dump of `Aprop_C.thrown` BP body.** This is the only way
   to confirm hypotheses 1–5 for Bug A. Likely 30 minutes of probe work
   once UE4SS is wired up against the running game.
2. **Hands-on observer audit run** with the new
   `SetPhysicsLinearVelocity` / `SetPhysicsAngularVelocity*` observers
   added, to detect whether the BP `grab__FinishedFunc` actually calls
   them. If yes, the wire field set may be reducible to just
   linearVel / angularVel without needing the GetPhysicsLinearVelocity
   capture trick.
3. **`UPrimitiveComponent::SetSimulatePhysics` PhysX-backend RE.** We
   asserted "false→true resets velocity" but didn't decompile the
   backend (vtable slot 181) to confirm. If it does NOT reset, we don't
   need to re-apply velocity at all — just hand-off the dynamic body
   pre-poised. Quick to verify by reading `BodyInstance::SetInstanceSimulatePhysics`
   in the engine source / IDA.
4. **Alien NPC throw RE.** If we ever want alien-thrown props to also
   sync, we need to RE the alien's throw entrypoint — currently out of
   Stage-4 scope.

## Cross-refs

* `research/findings/votv-physics-interaction-deep-re-2026-05-23.md` — Stage 1 PhysicsHandle + Timeline RE.
* `research/findings/votv-throw-sound-path-2026-05-24.md` — Path A vs Path B sound design.
* `memory/project_physics_object_pickup.md` — current ship state + the user bug verbatim.
* `src/votv-coop/include/ue_wrap/sdk_profile.h` — the porting surface for the new UFunction names.
