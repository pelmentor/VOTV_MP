# Held-pose stream for non-prop entities (clump / chipPile) — design 2026-05-27

Follow-up to:

- `research/findings/piles-trash/votv-chippile-clump-morph-RE-2026-05-27.md` (the morph
  round-trip RE — open question C was "what drives the held clump's
  per-tick world transform on the owning peer?").
- `research/findings/piles-trash/votv-garbage-trash-Inc2-Inc3-design-2026-05-27.md`
  (the Phase 5G STAGE 2 wire packet shape that this design extends).

Scope. Phase 5G STAGE 2 already replicates the SPAWN and DESTROY of the
three non-`Aprop_C` entities (`AtrashBitsPile_C`, `Aprop_garbageClump_C`,
`AactorChipPile_C`) plus the four `_erie / _leaves / _wetConcrete` variants.
What is still missing — and what this doc designs — is the **per-tick
transform stream** for an entity that is currently being CARRIED by a
player. The user-observable failure today: client picks up a chipPile, the
chipPile vanishes (replaced by a clump in the client's hand on the client),
the client walks 5 m, throws. On the host the clump appears at the pickup
pose and then teleports to the landing pose; the 5 m carry is invisible.

Out of scope. `AtrashBitsPile_C` is never carried (it's a stationary
litter cluster — no `holdPlayer` field, no pickup UFunctions). This doc
addresses only the two carried classes: `Aprop_garbageClump_C` and (rare,
but possible per the BP duck-typing) `AactorChipPile_C`.

The architecture matches RULE №1 — no per-tick high-frequency broadcast
flood; instead a one-shot reliable attach/detach pair turns the existing
puppet pose stream into the held entity's pose driver, naturally for
free. Per-tick state is exchanged only when the player is actively moving
the entity in a way the attach can't cover (drift correction). RULE 2 —
no parallel old/new paths: the existing STAGE 2 spawn/destroy pipeline is
the ONLY pipeline; this doc just adds an attachment dimension to it.
Scales to 4 peers per `[[project-coop-4-player-target]]` because the
attach payload carries `peerSessionId` and the receiver looks up the
corresponding puppet via `coop::players::Registry`.

---

## 1. Held-pose mechanism — answering open question C

The morph RE doc §4 enumerated three candidates:

- A. Clump's `ReceiveTick` reads `holdPlayer` and self-`SetActorLocation`s
  every tick.
- B. `mainPlayer_C::Hold Object` (a per-tick BP routine) does
  `holding_actor->SetActorLocation` every tick.
- C. The BP graph calls `K2_AttachToActor(clump, mainPlayer, "hand")` once
  at pickup; the engine's component attachment naturally drives the
  clump's world transform every frame from the parent socket.

### 1.1 Evidence we now have

The shipping native binary has **NO native code** for `toClump`,
`turnToPile`, `playerGrabbed`, `Hold Object`, `updateHold`, or
`throwHoldingProp` — every one of these is pure BP bytecode dispatched
through `ProcessEvent` on the BP class. IDA `lookup_funcs` returned `null`
for every name (confirmed 2026-05-27). Therefore the discriminator
between A/B/C is not in IDA-disassemblable native code; it lives in BP
bytecode reachable only through reflection of `UFunction.Script` or a live
`[probe] held_pose=1` flag at runtime.

What we DO know from the dumps:

- `Aprop_garbageClump_C::ReceiveTick(float)` exists
  (`prop_garbageClump.hpp:105`) — BP-overridden Tick. That alone doesn't
  prove candidate A; ReceiveTick could be doing landing-timer or
  delayOnHit bookkeeping while a separate attach drives the transform.
- `AmainPlayer_C::updateHold(bool& return, FString& rebug)` exists
  (`mainPlayer.hpp:475`) and `AmainPlayer_C::Hold Object` exists
  (`mainPlayer.hpp:464`) — both are BP-only. These are evidence for
  candidate B (player-driven held-pose update). The BP-VM name "Hold
  Object" with a space, and the `updateHold` return-by-ref `rebug`
  string (a typo of "debug"), are unambiguous markers of BP authorship.
- `mainPlayer_C::moveOutProp(AActor* Actor, float mult, AActor* addIgnore)`
  (`mainPlayer.hpp:441`) — BP routine, "move the prop out of a
  collision". Reads as the per-tick correction path for clipping; only
  meaningful in candidate A or B where the player code owns the
  transform, NOT in C (where the attach socket would prevent collision
  in the first place).
- The BP graph downstream of E-press (`InpActEvt_use`) eventually calls
  `clump->setKey()`, but the clump dump has NO `K2_AttachToActor`
  UFunction listed (a Grep against `prop_garbageClump.hpp` for "Attach"
  returns nothing). UE4's `K2_AttachToActor` is an `AActor` engine
  UFunction, so any BP can dispatch it on any actor — but typically the
  BP author's call to `AttachActorToActor` ends up as an
  ExecuteUbergraph_* opcode chain that DOES NOT appear in the SDK
  function table for the actor being attached. The absence in the
  clump's UFunction table is therefore inconclusive.

### 1.2 Probe-without-iteration verdict (RULE: deep RE forbids iteration)

The clean discriminator does NOT require code changes — it requires ONE
reflection probe. Per the deep-RE rule we add the diagnostic, run the
hands-on, then make ONE evidence-backed change.

Probe (one-time, `[probe] held_pose=1` gated):

1. On `Aprop_garbageClump_C::ReceiveTick` POST, log `(self,
   self.AttachParent, self.GetActorLocation, holdPlayer)` once per second
   on the OWNING peer (host's clump while host holds it).
2. If `AttachParent != null` and points to the player's skeletal mesh
   component → **candidate C** (attached). Subsequent design is "broadcast
   the attach once, let the puppet pose stream drive the world transform".
3. If `AttachParent == null` and the location is changing per tick →
   either A or B. Doesn't matter which for our wire layer; both are
   owning-peer-local and need a per-tick world transform broadcast.

`AttachParent` lives at `USceneComponent::AttachParent` (UE4.27 native
offset 0x1A8 on `USceneComponent`, well known and stable). The clump's
`RootComponent` is its `StaticMesh @ 0x0230` (a `UStaticMeshComponent`,
which derives from `USceneComponent`).

### 1.3 Design takes BOTH paths into account

We choose a design that is correct for ALL THREE candidates without
iterating, on the principle that the attach packet's REQUIRED data is the
same in every case and the per-tick stream is OPTIONAL.

- We send ONE reliable `HeldEntityAttach` on pickup. The receiver, on
  apply, calls `K2_AttachToActor(mirrorClump, hostPuppet, handBoneName)`
  with `bWeldSimulatedBodies=true` and disables clump physics simulation
  on its `StaticMesh`. This handles candidate C (matches the host's
  engine state) AND works for candidates A/B because the receiver's
  attach naturally pins the mirror to the puppet's hand socket — even
  if the HOST'S mechanism is per-tick SetActorLocation, the receiver
  doesn't need to replicate that; the receiver just needs the visual
  result, which is "the clump follows the puppet's hand". The puppet's
  hand pose is already streamed via `PoseSnapshot` (the puppet's
  animation evaluates locally on the receiver from the source pose), so
  the attach socket DOES move correctly per-tick on the receiver.
- We send a per-tick **unreliable** `HeldEntityPose` ONLY in the
  fallback case where the receiver-side attach fails (a sentinel `void*`
  bone name not found, GC race on the puppet, or the receiver chose to
  ignore the attach for testing). The unreliable stream is the safety
  net, not the primary mechanism. Per the design budget (~125 Hz pump
  on the host, 4 peers, worst case 4 clumps in 4 hands = 16 pose
  packets/tick = 76 B × 16 = ~150 KB/s, fits easily within the
  per-peer LAN budget identified in `[[project-coop-whole-map-sync]]`).

If the probe in §1.2 lands on candidate C clearly, we may DELETE the
fallback unreliable stream entirely in a follow-up commit (RULE 2 — no
migration baggage; if not needed, it goes). But the design ships with
both so we are not blocked on the probe.

---

## 2. Wire packet design

### 2.1 New `ReliableKind::HeldEntityAttach = 18`

Bumps `kProtocolVersion` from 9 to 10. Slot 18 is the next free after
`NonPropEntityDestroy = 17` (the Phase 5G STAGE 2 addition).

```cpp
struct HeldEntityAttachPayload {
    uint8_t  entityClass;        //  1 -- NonPropEntityClass (GarbageClump or ActorChipPile)
    uint8_t  peerSessionId;      //  1 -- the OWNING peer (host=0, joiners 1..)
    uint8_t  attached;           //  1 -- 1=attach to hand, 0=detach + launch
    uint8_t  flags;              //  1 -- bit0: hasLaunchImpulse, bit1: forceMorphPending
    uint32_t identity;           //  4 -- the entity's identity (sessionId for clump/chipPile)
    float    launchLinVelX, launchLinVelY, launchLinVelZ;  // 12 -- valid only if attached=0
    float    launchAngVelX, launchAngVelY, launchAngVelZ;  // 12 -- valid only if attached=0
    // Total: 32 bytes.
};
static_assert(sizeof(HeldEntityAttachPayload) == 32,
              "HeldEntityAttachPayload must be 32 bytes");
static_assert(sizeof(HeldEntityAttachPayload) <= 256 - 20 - 8,
              "HeldEntityAttachPayload must fit one reliable datagram");
```

Field rationale:

- `entityClass`: same enum that the existing `NonPropEntityStatePayload`
  uses; receiver routes to the right per-class map (`g_clientActorByIdentity`
  is already keyed `(kind, identity)`).
- `peerSessionId`: identifies whose puppet's hand the receiver should
  attach to. The receiver looks up the puppet via
  `coop::players::Registry::Get().Puppet(peerSessionId)`. When peer 0
  (host) is holding it on the host, peer 0's puppet on the client is
  the receiver target. Scales naturally to 4 peers.
- `attached`: 1 = attach (begin held-pose mirror); 0 = detach + launch
  (end held-pose mirror, apply impulse for inertia continuity).
- `flags`:
  - `bit0 hasLaunchImpulse`: when `attached=0`, whether the launch
    velocities are nonzero (set false for a passive drop = just-detach,
    no impulse — saves the receiver from a no-op `SetPhysicsLinearVelocity`
    call).
  - `bit1 forceMorphPending`: when set, the receiver knows the next
    `NonPropEntityDestroy(clump) + NonPropEntityState(chipPile)` pair
    that arrives within ~500 ms is the landing morph for this throw —
    used purely for diagnostic correlation; no behavior change. Cleared
    on the next attach=1 for the same identity.
- `identity`: the host-minted sessionId from the STAGE 2 broadcast.
  Stable across the entity's lifetime (chipPile-A → clump → chipPile-B
  are THREE different identities; the attach payload only covers the
  middle, clump, leg of the morph). Receiver looks up
  `g_clientActorByIdentity[MakeIdentityKey(kind, identity)]`.
- `launchLinVel / launchAngVel`: captured on the OWNING peer at the
  release edge via `ue_wrap::prop::GetPhysicsVelocity(clump)`. Same shape
  as the existing `PropReleasePayload` (linVel cm/s + angVel deg/s).

### 2.2 New `MsgType::HeldEntityPose = 9` (unreliable, fallback)

Bumps the `MsgType` enum (currently `PropPose = 8` is the highest).

```cpp
struct HeldEntityPoseSnapshot {
    uint8_t  entityClass;        //  1 -- NonPropEntityClass
    uint8_t  peerSessionId;      //  1 -- owning peer
    uint8_t  _pad[2];            //  2 -- align
    uint32_t identity;           //  4 -- sessionId
    float    x, y, z;            // 12 -- world cm
    float    pitch, yaw, roll;   // 12 -- FRotator
    // Total: 32 bytes.
};
static_assert(sizeof(HeldEntityPoseSnapshot) == 32,
              "HeldEntityPoseSnapshot must be 32 bytes");

struct HeldEntityPosePacket {
    PacketHeader            header;  // 20
    HeldEntityPoseSnapshot  pose;    // 32
};
static_assert(sizeof(HeldEntityPosePacket) == 52,
              "HeldEntityPosePacket must be 52 bytes");
```

Rate. **30 Hz** (every other tick at 60 Hz pump) — half of `PropPose`'s
~60 Hz. The visual delta between hand-position-at-tick-N and
hand-position-at-tick-N+1 at human walk speed is ≤ 5 cm; a 30 Hz stream
introduces ≤ 10 cm visual lag, smaller than the puppet-pose interp jitter
already in the system. Cuts wire load by 50% vs PropPose. If the attach
path works (candidate C), this stream is throttled to **0 Hz** (no
broadcast at all) — see §3.2.

Identity scheme. Same `(entityClass, identity)` tuple as STAGE 2 and as
the attach payload. NOT a `WireKey` — these classes have no save Key
(per the morph RE doc §3). The sessionId is the only stable identifier.

### 2.3 Why both packets (and not just unreliable per-tick)

Three reasons it can't be unreliable-only, per RULE 1:

1. **Drop-the-attach-edge is a desync sink.** Unreliable pose stream:
   the FIRST packet establishes the "this clump is now held" state on
   the receiver. If that packet drops (unreliable!), the receiver never
   triggers its attach-to-puppet path; the clump just sits at its
   pre-pickup pose until the receiver-side stream-stop timeout
   ironically RE-enables physics on a stationary clump. Reliable attach
   guarantees the receiver enters the held state exactly once per
   pickup.
2. **Detach edge MUST carry the launch impulse.** Even if the unreliable
   stream is dense enough to follow the hand, the very last packet
   before release tells the receiver "the clump is HERE" — but
   nothing tells it "the clump now flies with velocity V". The
   receiver's PhysX freezes the body at the last applied transform;
   without an impulse the clump bricks in mid-air. A reliable detach
   carries the inherited PhysX velocity (mirrors the existing
   `PropReleasePayload` mechanism) so the receiver can re-enable
   `SimulatePhysics` and `SetPhysicsLinearVelocity` for an identical
   launch trajectory.
3. **Throw-across-peer-boundary semantics.** If client throws AND the
   reliable detach is in flight, the host's view of the clump is still
   "client holds it". The reliable channel guarantees ordered delivery,
   so the host applies attach=1, then waits for attach=0; the morph
   destroy+spawn that follows MUST arrive after the detach for the
   landing chipPile to spawn on the host. A reliable channel preserves
   this; an unreliable does not.

### 2.4 Wire-budget audit (4-peer target)

Worst case: 4 peers, all simultaneously carrying a clump (extreme but
allowed). On the host process:

- Outgoing reliable: 1 attach when peer 0 picks up, 1 detach when peer 0
  releases. 2 packets per pickup-throw cycle. Burst rate at session
  start: 0; in normal play: ~1 cycle per 30 s. **Negligible.**
- Outgoing unreliable (fallback only): 30 Hz × 4 peers × 1 active
  clump-each = 120 packets/s × 52 B = **6.2 KB/s** out of the host. The
  per-peer LAN budget is ~300-400 KB/s out per `[[project-coop-whole-map-sync]]`;
  this fits with 1.5% utilization. If candidate C lands cleanly and we
  remove the unreliable stream, the outgoing rate drops to ~0.

---

## 3. Sender path (owning peer broadcasts)

### 3.1 `HeldEntityAttach` emission

New file `src/votv-coop/src/coop/held_entity_sync.cpp` (separate from
`non_prop_entity_sync.cpp` per the modular file-size rule: STAGE 2 is
"spawn/destroy of these entities", STAGE 3 is "carry/throw mobility of
these entities"; different concept, different file). Existing
`non_prop_entity_sync.cpp` is at 885 LOC `remote_prop.cpp`-ish neighborhood;
adding ~400 LOC to it would push it past 1100. New file gets us a clean
~300-400 LOC home for the held-pose layer.

Host-side POST observers on the OWNING peer (host=0 today; symmetric for
client when STAGE 3 makes it bidirectional):

| Observer | Class | UFunction | Purpose |
|---|---|---|---|
| O1 | `AactorChipPile_C` (+ 3 subclasses) | `toClump` | POST: read return-value (newly spawned clump); resolve its sessionId via `non_prop_entity_sync::IdentityForActor`; emit `HeldEntityAttach{attached=1, entityClass=GarbageClump, peerSessionId=Registry::LocalPeerId(), identity=clumpId}`. |
| O2 | `Aprop_garbageClump_C` (+ 3 subclasses) | `playerHandRelease_LMB` | POST: read clump's StaticMesh velocity via `prop::GetPhysicsVelocity`; emit `HeldEntityAttach{attached=0, identity=selfId, launchLinVel=v.linearCmS, launchAngVel=v.angularDegS, flags.hasLaunchImpulse=1}`. |
| O3 | `Aprop_garbageClump_C` (+ 3 subclasses) | `playerHandRelease_RMB` | Same as O2 (RMB-drop path; impulse is typically zero so `flags.hasLaunchImpulse=0`). |
| O4 | `Aprop_garbageClump_C` (+ 3 subclasses) | `unequpped` (sic — matches BP) | POST: fallback detach for unequip flows that bypass Release. Emit `HeldEntityAttach{attached=0, flags.hasLaunchImpulse=0}` (no impulse for unequip). |

Sender pump location (file:line).

- The observers fire on `ProcessEvent` from the game thread. Each
  observer body just calls `coop::held_entity_sync::EmitAttach(...)`
  which serializes the payload and calls `Session::SendHeldEntityAttach`.
- `held_entity_sync::EmitAttach` runs on the game thread. The `Session::
  Send*` methods are thread-safe (they enqueue on the reliable channel's
  mutex-guarded send queue).

Registration. In `held_entity_sync::Install()`, called from
`harness.cpp` near `coop::non_prop_entity_sync::Install()` (which is
called from `InstallGrabObservers()` at `src/votv-coop/src/harness/harness.cpp`
line ~457). The Install routine resolves each `_C` class via
`R::FindClass`, then each UFunction via `R::FindFunction`, then
`GT::RegisterPostObserver`. Idempotent latch (`g_held_installed`) so
per-tick re-call is O(1) once registered. Same shape as
`non_prop_entity_sync::Install` (lines 547-625 of that file).

### 3.2 `HeldEntityPose` emission (fallback only)

Per-tick, in the harness pump tick block where `SetLocalPropPose` is
called (`src/votv-coop/src/harness/harness.cpp` line ~461). After the
existing `PropPose` block:

```cpp
// FALLBACK held-pose stream for non-prop entities (clump / chipPile).
// Only active in non-C candidates (per the probe verdict, §1.2). When
// the attach mechanism on the receiver works (candidate C), this is a
// 0-Hz no-op.
coop::held_entity_sync::PumpLocalHeldPose(g_netLocal);
```

`PumpLocalHeldPose` body:

1. Read `holding_actor @ 0x0A20` off `g_netLocal` (the local mainPlayer).
2. If non-null AND `R::IsLive(holding_actor)` AND its class is in the
   non-prop entity table (via `non_prop_entity_sync::FindEntryForActor`),
   we have a held non-prop entity.
3. Throttle to 30 Hz using a steady_clock check (`stampMs % 33`-ish).
4. Build a `HeldEntityPoseSnapshot` from the entity's
   `GetActorLocation` / `GetActorRotation` + the cached identity (look
   up via the same `IdentityForActor` the broadcast path uses; cached
   in a host-side map so we don't allocate a new sessionId per tick).
5. Call `Session::SetLocalHeldPose(pose)` — mirrors the existing
   `SetLocalPropPose` (`coop/net/session.cpp:114`) — net-thread reads
   on each send tick.

Edge: held → not-held in one tick. The existing `g_lastHeldProp`
edge-detector pattern (`harness.cpp:501-533`) maps over directly. We add
a parallel `g_lastHeldNonProp` + `g_lastHeldNonPropIdentity` pair. On the
edge: send the reliable detach (O2-O4 already cover this through BP
observers, but the edge-detector is a belt-and-suspenders sender for the
case where BP didn't fire the expected UFunction — e.g. clump auto-
destroys via LifeSpan timeout while held; `holding_actor` clears via the
mainPlayer's `updateHold` BP graph, BUT no playerHandRelease fires). On
that fallback, send `HeldEntityAttach{attached=0, flags.hasLaunchImpulse=0}`.

Symmetric for client when STAGE 3 goes bidirectional. The
`Registry::LocalPeerId()` field already carries the client's session id
(1..3); the host receives + applies the same way.

### 3.3 Echo suppression on the sender

The sender's BP graph runs `toClump` / `playerHandRelease_LMB` normally;
these emit attach/detach. The receiver's BP graph (per the existing
STAGE 2 + Inc 3 mushroom-suppressor pattern) is gated:

- The client's `playerHandUse_LMB` on a wire-mirrored clump should NEVER
  fire (the client never PRESSES E on a mirror today; client-initiated
  remote actions are out of scope per STAGE 2's design doc). So the
  client doesn't accidentally emit a back-attach.
- If a future commit enables client-initiated pickup on a mirror, the
  client's `toClump` / `playerHandRelease_LMB` POST will fire. We then
  add a per-class incoming-mirror set in `held_entity_sync` — same shape
  as `non_prop_entity_sync::g_incomingWireSpawn` (`non_prop_entity_sync.cpp:160`)
  — and skip the broadcast when the actor is a wire mirror.

For STAGE 2.5 (this design), client-side broadcast is **role-gated
off** at the top of `EmitAttach`:

```cpp
auto* s = LoadSession();
if (!s || s->role() != coop::net::Role::Host) return;
```

This is RULE 2 compliant: when STAGE 3 lands, we DELETE this guard
(client can broadcast too); we don't keep a parallel "client-side path
disabled" branch.

---

## 4. Receiver path

### 4.1 `HeldEntityAttach` apply

File: `src/votv-coop/src/coop/held_entity_sync.cpp` (same file).

```cpp
void ApplyHeldEntityAttach(const HeldEntityAttachPayload& p) {
    // Game-thread post (engine UFunction calls must run on GT).
    HeldEntityAttachPayload copy = p;
    GT::Post([copy] { DoApplyHeldEntityAttach(copy); });
}

void DoApplyHeldEntityAttach(const HeldEntityAttachPayload& p) {
    // 1. Trust-boundary validation.
    if (p.entityClass == 0 ||
        p.entityClass >= static_cast<uint8_t>(NonPropEntityClass::kMax)) return;
    if (p.peerSessionId >= coop::players::kMaxPeers) return;
    // launch velocity finite + bounded (same shape as PropRelease validator)
    if (p.flags & kHeldFlag_HasLaunchImpulse) {
        const float vals[6] = {p.launchLinVelX, p.launchLinVelY, p.launchLinVelZ,
                               p.launchAngVelX, p.launchAngVelY, p.launchAngVelZ};
        for (float v : vals) if (!std::isfinite(v)) return;
    }

    // 2. Look up the mirror entity via STAGE 2's identity map.
    const auto kind = static_cast<NonPropEntityClass>(p.entityClass);
    void* mirror = non_prop_entity_sync::LookupMirrorByIdentity(kind, p.identity);
    if (!mirror || !R::IsLive(mirror)) {
        // The attach arrived BEFORE the mirror spawned. Queue it; drain on
        // each subsequent ApplyState that creates a mirror. Same shape as
        // item_activate::TickConnect's pending-per-peer queue.
        QueuePendingAttach(p);
        return;
    }

    // 3. Look up the owning peer's puppet (for attach=1 only).
    void* puppetActor = nullptr;
    if (p.attached) {
        auto* puppet = coop::players::Registry::Get().Puppet(p.peerSessionId);
        if (!puppet) {
            // Same race: puppet not spawned yet. Queue + drain.
            QueuePendingAttach(p);
            return;
        }
        puppetActor = puppet->GetActor();
        if (!puppetActor || !R::IsLive(puppetActor)) {
            QueuePendingAttach(p);
            return;
        }
    }

    // 4. Apply.
    void* mesh = ue_wrap::prop::GetStaticMesh(mirror);  // same accessor as PropPose
    if (p.attached) {
        // Disable physics on the mirror so the attach doesn't fight PhysX.
        if (mesh) ue_wrap::prop::SetSimulatePhysics(mesh, false);
        // Attach to the puppet's hand. UE4 has K2_AttachToActor on AActor
        // (Engine.AActor.K2_AttachToActor) with params:
        //   AActor* ParentActor, FName SocketName,
        //   EAttachmentRule LocationRule (KeepRelative),
        //   EAttachmentRule RotationRule (KeepRelative),
        //   EAttachmentRule ScaleRule (KeepRelative),
        //   bool bWeldSimulatedBodies = false
        // The host's BP attaches to a hand bone -- exact bone name unknown
        // until probe (§1.2). Hypothesis: "hand_R" (matches UE4 mannequin
        // convention); fallback: NAME_None (attach to root, transform-only).
        held_entity_sync::AttachToHand(mirror, puppetActor, kHandSocketName);
        // Cache for the detach path.
        g_clientHeldMirror[MakeIdentityKey(kind, p.identity)] =
            HeldMirrorEntry{mirror, mesh, puppetActor};
        UE_LOGI("held_entity_sync[client]: attached mirror=%p to puppet=%p (peer %u, identity %u)",
                mirror, puppetActor, p.peerSessionId, p.identity);
    } else {
        // Detach. Look up the cached entry; if not cached we still attempt a
        // best-effort detach (the engine's DetachFromActor is idempotent).
        held_entity_sync::DetachFromHand(mirror);
        if (mesh) {
            ue_wrap::prop::SetSimulatePhysics(mesh, true);
            if (p.flags & kHeldFlag_HasLaunchImpulse) {
                ue_wrap::FVector linVel{p.launchLinVelX, p.launchLinVelY, p.launchLinVelZ};
                ue_wrap::FVector angVel{p.launchAngVelX, p.launchAngVelY, p.launchAngVelZ};
                ue_wrap::prop::SetPhysicsLinearVelocity(mesh, linVel);
                ue_wrap::prop::SetPhysicsAngularVelocityInDegrees(mesh, angVel);
            }
        }
        g_clientHeldMirror.erase(MakeIdentityKey(kind, p.identity));
        UE_LOGI("held_entity_sync[client]: detached mirror=%p (impulse=%d)",
                mirror, (p.flags & kHeldFlag_HasLaunchImpulse) ? 1 : 0);
    }
}
```

Helpers needed in `ue_wrap`:

- `coop::held_entity_sync::AttachToHand(actor, parent, socketName)` —
  wraps `K2_AttachToActor`. Lazy-resolves the UFunction on first call;
  caches. Engine UFunction (lives on `AActor`); shouldn't be BP-defined
  so it's stable. SocketName param TBD by probe.
- `coop::held_entity_sync::DetachFromHand(actor)` — wraps
  `K2_DetachFromActor` or `DetachFromActor`. Engine UFunction; same
  lazy-resolve pattern.
- `ue_wrap::prop::SetSimulatePhysics`, `SetPhysicsLinearVelocity`,
  `SetPhysicsAngularVelocityInDegrees` already exist (used by
  `remote_prop.cpp:245, 269, 309` for the PropPose receiver). Reuse.

### 4.2 `HeldEntityPose` apply (fallback, unreliable)

Receiver pump in the harness `NetPumpTick` after the existing `remote_prop::Tick(session)` call:

```cpp
coop::held_entity_sync::Tick(g_session);
```

`Tick` body mirrors `coop::remote_prop::Tick` exactly (`remote_prop.cpp:253`):

1. `session.TryGetRemoteHeldPose(out, &isNew)`.
2. On first new pose or identity change: look up mirror via
   `non_prop_entity_sync::LookupMirrorByIdentity(kind, identity)`,
   `SetSimulatePhysics(false)`, cache.
3. Subsequent same-identity packets: `SetActorLocation` +
   `SetActorRotation` on the cached mirror.
4. Stream-stop timeout: > 500 ms since last apply → implicit release
   (re-enable physics + clear cache). Same threshold as PropPose
   (`remote_prop.cpp:304`).

This stream is OPTIONAL — when candidate C (attach) lands cleanly, the
attach pins the mirror to the puppet's socket and the unreliable stream
is a no-op (sender doesn't broadcast it; receiver's `TryGetRemoteHeldPose`
returns false; `Tick` short-circuits). Per RULE 2, if the probe confirms
candidate C, this fallback gets deleted in a follow-up commit — but it
ships in the initial cut to avoid blocking on the probe verdict.

### 4.3 Echo suppression on the receiver

The receiver's `K2_AttachToActor` call DOES dispatch through
`ProcessEvent` and IS observed if a future observer is registered on
that UFunction. We register NONE today (sender hooks fire on
`toClump`/`playerHandRelease_LMB`, not on `K2_AttachToActor`), so there's
no echo back. If a future Inc adds an observer on K2_AttachToActor for
some other reason, we add an incoming-attach set just like
`g_incomingWireSpawn`.

The `SetSimulatePhysics(false)` we call is on a UPrimitiveComponent and
we already do this in `remote_prop.cpp`; no observer is registered on
it; no echo possible.

The launch impulse application (`SetPhysicsLinearVelocity`) DOES have
diagnostic-only observers (`grab_observer.cpp:130`,
`GrabObserver_PrimComp_SetLinearVelocity_PRE`) but they ONLY log; they
don't emit any wire packet. No echo.

---

## 5. Release / throw integration

### 5.1 Sender — release edge

1. BP graph dispatches `playerHandRelease_LMB(player)` on the clump.
2. The BP body sets the launch velocity on the clump's StaticMesh via
   either `SetPhysicsLinearVelocity` (the explicit path) or by clearing
   the `holdPlayer` field + re-enabling physics simulation (the implicit
   path; PhysX inherits the kinematic-tracking velocity, same as the
   Aprop_C grab — see the v5 protocol comment in
   `protocol.h:23-35`).
3. Our POST observer O2 (registered on `playerHandRelease_LMB`) fires
   AFTER the BP body. At this point the clump's StaticMesh has its
   final inherited+impulse-summed velocity baked in (PhysX hasn't
   stepped yet within this frame).
4. We read `vel = ue_wrap::prop::GetPhysicsVelocity(clump.StaticMesh)`,
   emit `HeldEntityAttach{attached=0, identity=clumpId,
   launchLinVel=vel.linearCmS, launchAngVel=vel.angularDegS,
   flags.hasLaunchImpulse=1}`.
5. Within ~1 frame later (clump's BP ComponentHit fires when it lands
   somewhere), the morph runs: clump destroys + chipPile spawns. STAGE
   2's existing `NonPropEntityDestroy(clumpId)` and
   `NonPropEntityState(chipPileId)` cover this naturally.

### 5.2 Receiver — release apply

1. Wire receives `HeldEntityAttach{attached=0, ...}`.
2. `DoApplyHeldEntityAttach` (§4.1) detaches the mirror, re-enables
   `SimulatePhysics`, applies linVel + angVel via the same
   `SetPhysicsLinearVelocity` + `SetPhysicsAngularVelocityInDegrees`
   pair that `coop::remote_prop::OnRelease` uses for Aprop_C release
   (`remote_prop.cpp:314+`).
3. The mirror clump now flies on the client's local PhysX sim. The
   trajectory diverges slightly from the host's (different physics step
   timing, different jitter floor) — this is expected. The
   authoritative landing position is the host's `NonPropEntityState`
   broadcast of the new chipPile, which arrives next.
4. When the host's `NonPropEntityDestroy(clumpId)` arrives, the
   receiver's `DoApplyDestroy` calls `K2_DestroyActor` on the mirror
   clump (which by now has settled visually wherever its local sim took
   it — typically very close to the host's landing). The destroy
   includes the existing `MarkIncomingDestroy` echo-suppression at
   `non_prop_entity_sync.cpp:545`.
5. When the host's `NonPropEntityState(chipPileId, loc=hostLandingLoc)`
   arrives, the receiver spawns a fresh mirror chipPile at exactly the
   host's authoritative landing location. The visual transition is the
   client's mirror clump "snap" to the precise landing pose at the
   moment of destroy → spawn, which is acceptable.

### 5.3 Throw across peer boundary (STAGE 3 lookahead)

When STAGE 3 makes things bidirectional and the CLIENT throws a clump
that the HOST receives:

1. Client's `playerHandRelease_LMB` POST fires on the client (its O2).
2. Client emits `HeldEntityAttach{attached=0, peerSessionId=client,
   identity=clumpId, launchLinVel}`.
3. Host receives, applies via `DoApplyHeldEntityAttach`. Host looks up
   the mirror clump (under host's own `g_clientActorByIdentity`-equivalent
   for the OTHER direction; this needs a new bidirectional map STAGE 3
   introduces). Detach + impulse.
4. Client's BP graph also runs the morph (clump destroys, chipPile
   spawns). STAGE 3's bidirectional destroy + spawn pipeline replicates
   the morph back to the host.

The current STAGE 2.5 design (this doc) is ONE-DIRECTION (host →
client). The client never broadcasts. The bidirectional generalization
is one role-gate edit at `EmitAttach` (drop the `Role::Host` check) plus
the reverse identity map STAGE 3 needs.

---

## 6. Edge cases

### 6.1 Drop-attach race with NonPropEntityState

Possible ordering on a slow LAN or burst:

- Wire: `NonPropEntityState(clumpId, loc=hostHandPose)`,
- Wire: `HeldEntityAttach(clumpId, attached=1, peerSessionId=0)`.

Normally arrives in order (both reliable, same sender, same channel).
If the second arrives at the receiver BEFORE the first (theoretically
possible in a buffered queue OOO scenario — actually impossible per the
stop-and-wait reliable channel; documented for defense-in-depth):

- `DoApplyHeldEntityAttach` `LookupMirrorByIdentity` returns null.
- Path queues the attach via `QueuePendingAttach`.
- The state arrives next, `DoApplyState` spawns the mirror, calls
  `held_entity_sync::DrainPendingForIdentity(kind, identity)` at the
  end of the spawn path, which dequeues + re-applies the attach.

### 6.2 Two peers race-pick-up the same entity

Today: only host can pick up (STAGE 2.5 is host → client one-way). When
STAGE 3 enables client pickup:

- Both peers' BP graphs simultaneously process E-press on a chipPile
  with the same identity (the host's chipPile is mirrored on the client
  as the same identity).
- Both peers run `toClump` locally. Both emit attach packets to each
  other.
- Receiver's `DoApplyHeldEntityAttach` sees the LATER attach (highest
  reliable seq wins) — the FIRST one is overwritten by the second.

For STAGE 3 this needs a HOST-AUTHORITATIVE serializer: the canonical
pickup is whoever the HOST acknowledged first. The client speculatively
runs `toClump` but the host rejects the second one by NOT broadcasting an
attach for it. Client then receives the host's authoritative
`HeldEntityAttach{peerSessionId=otherPeer}` and undoes its speculative
pickup (re-spawns the chipPile mirror it just destroyed, destroys the
clump it just spawned). This is the standard MTA host-authoritative
pickup ARQ pattern; out of scope for STAGE 2.5 but the WIRE PACKET
SHAPE in §2 already supports it (peerSessionId field).

### 6.3 Throw across the peer boundary

Covered §5.3.

### 6.4 Timeout-implicit release

The PropPose stream uses a 500 ms gap as implicit release
(`remote_prop.cpp:304`). For HeldEntityPose (the OPTIONAL fallback
stream), same 500 ms timeout in `held_entity_sync::Tick`.

For the PRIMARY attach mechanism, NO timeout-implicit detach. The
reliable channel guarantees the detach arrives. If the channel
disconnects mid-carry, the existing `Session::Bye` path tears down all
mirrors via `non_prop_entity_sync::OnDisconnect` (a method we need to
add — STAGE 2 today does not clean up mirrors on disconnect; tracked as
its own follow-up). The held mirror's attachment naturally goes away
when the puppet actor is destroyed (UE4 auto-detaches children when the
parent dies).

### 6.5 Save during held

VOTV's save system writes `Aprop_garbageClump_C::skipSave1 @ 0x0254` so
clumps are NOT saved while held (per morph RE doc §3.1). The
chipPile-A that was picked up is destroyed at pickup time, so it ALSO
isn't saved. The player's mainPlayer carries a "held-item descriptor"
which the save persists; on load the held clump materializes from that.

For coop: the host's save is the source of truth (per
`[[project-coop-save-host-authoritative]]`). On client connect, the
host's snapshot replay (`non_prop_entity_sync::ReplaySnapshotForJoinedClient`
at line 639+) walks live actors and broadcasts state. For a clump that
the host is CURRENTLY HOLDING:

1. Snapshot replay sees the clump in GUObjectArray, broadcasts
   `NonPropEntityState{loc=clump.GetActorLocation()}`. Client spawns
   the mirror.
2. The HOST's harness pump immediately afterwards (~1 tick later)
   detects host is holding it via `holding_actor != null` and the
   class-table match. We add a one-shot "broadcast current held state
   on connect-edge" emit to `held_entity_sync` mirroring
   `weather_sync::QueueConnectBroadcast` (`harness.cpp:407`):

```cpp
// On the role==Host connect-edge tick (the same edge that fires
// QueueConnectBroadcast for weather):
coop::held_entity_sync::QueueConnectBroadcast();
```

`QueueConnectBroadcast` walks GUObjectArray for any non-prop entity
whose `holdPlayer @ 0x0240` is non-null (clump-side) — there's at most
one per peer, so O(n) once on connect. For each: resolve the holder's
peer id, emit `HeldEntityAttach{attached=1}`. Retries per-tick via
the standard `TickConnect` drain pattern until the reliable channel
accepts.

3. Client receives the attach, looks up the puppet (which by now is
   spawned via the existing puppet-spawn flow on first remote pose),
   attaches the mirror clump to the puppet's hand. Visual state is
   correct from connect onwards.

### 6.6 LifeSpan auto-destroy while held

`Aprop_garbageClump_C::LifeSpan @ 0x0258` — the clump self-destructs
after N seconds. On the host this fires the standard `K2_DestroyActor`
which we observe at `non_prop_entity_sync.cpp` → broadcasts
`NonPropEntityDestroy`. Receiver destroys the mirror. The clump is gone
from both views simultaneously.

The `HeldEntityAttach{attached=0}` is NOT emitted in this path (the BP
doesn't dispatch `playerHandRelease_LMB`; it just destroys). This is
fine — the receiver's destroy of the mirror naturally detaches it
(UE4's destroy chain detaches a destroyed child from its parent). No
launch impulse needed (it just vanishes).

### 6.7 Player dies while held

`AmainPlayer_C::ragdollMode(ragdoll, passOut, death)` (line 484 of the
dump) is the player-death entry. On death the BP graph drops all held
items. Specifically `mainPlayer_C::throwHoldingProp` or `Hold Object`
clears `holding_actor` AND clears the clump's `holdPlayer`. The BP
graph then dispatches `unequpped(player)` on the clump (our O4 catches
this) — emit `HeldEntityAttach{attached=0, flags.hasLaunchImpulse=0}`.

Edge case: if the death path skips `unequpped` (some BP graphs do —
ragdoll-death may just K2_DestroyActor the held item), the
`HeldEntityPose` stream's stream-stop timeout (500 ms in §4.2) plus
the eventual `NonPropEntityDestroy` from the natural destroy chain
covers it.

### 6.8 Holder disconnect

Holder's peer session dies. Host (assuming holder was a client)
receives the disconnect, runs `Session::OnDisconnect` handlers. We add
to `held_entity_sync::OnPeerDisconnect(peerSessionId)`:

1. Walk `g_hostHeldByPeer` (sender-side cache of "this peer is holding
   identity X" — populated by the EmitAttach path on the host's mirror
   of the client's local clump).
2. For each held entity by the disconnected peer: emit
   `HeldEntityAttach{attached=0, flags.hasLaunchImpulse=0}` to all
   OTHER peers (none, in 1v1; multiple in 4-peer).
3. On the host's OWN view: trigger the clump's
   `playerHandRelease_LMB(disconnectedPlayerStub)` so the host's BP
   graph runs the natural release (or call `unhook`/`unequpped` if
   that's the cleaner path — TBD via probe).

---

## 7. Implementation checklist

Each step is one commit. RULE 2: no parallel old/new paths between
commits; each commit leaves the codebase coherent.

1. **Probe pass (no shipped behaviour change).**
   - Add `[probe] held_pose=1` gated PRE on
     `Aprop_garbageClump_C::ReceiveTick` logging `(self, self.AttachParent,
     self.GetActorLocation, holdPlayer)`.
   - Resolve `USceneComponent::AttachParent @0x1A8` — add an accessor in
     `ue_wrap/engine.h` if not present.
   - Single hands-on test: host picks up a clump, walks ~5 m, throws.
     Examine log: is `AttachParent` non-null during the carry?
   - Decision: candidate C confirmed (attached) OR not (per-tick driver).

2. **Protocol bump + new packet shapes.**
   - `protocol.h`: bump `kProtocolVersion 9 → 10`.
   - Add `MsgType::HeldEntityPose = 9`.
   - Add `ReliableKind::HeldEntityAttach = 18`.
   - Define `HeldEntityAttachPayload` (32 B), `HeldEntityPoseSnapshot`
     (32 B), `HeldEntityPosePacket` (52 B), static_asserts per §2.
   - Define `kHeldFlag_HasLaunchImpulse = 0x01`,
     `kHeldFlag_ForceMorphPending = 0x02`.

3. **New file `coop/held_entity_sync.{h,cpp}`.**
   - Public API: `Install()`, `SetSession(Session*)`, `EmitAttach(...)`,
     `ApplyHeldEntityAttach(payload)`, `ApplyHeldEntityPose(snapshot)`,
     `Tick(session)`, `QueueConnectBroadcast()`,
     `OnPeerDisconnect(peerSessionId)`,
     `LookupHeldMirror(kind, identity)` (for STAGE 3 lookahead).
   - Internal: `DoApplyHeldEntityAttach` (game-thread bottom-half),
     `AttachToHand`, `DetachFromHand`, `g_clientHeldMirror` map,
     `g_pendingAttaches` deque, `g_hostHeldByPeer` map (sender-side
     cache for disconnect cleanup), drain functions.
   - Observers O1-O4 from §3.1; idempotent latch
     `g_held_installed`; same structure as
     `non_prop_entity_sync::Install`.

4. **Session API extension.**
   - `session.h`: declare `SetLocalHeldPose(bool, const HeldEntityPoseSnapshot&)`,
     `TryGetRemoteHeldPose(out, bool* isNew)`,
     `SendHeldEntityAttach(const HeldEntityAttachPayload&)`.
   - `session.cpp`: implement (mirror `SetLocalPropPose` /
     `TryGetRemotePropPose` / `SendPropRelease` patterns).
   - `Session::HandleDatagram`: route `MsgType::HeldEntityPose` to a new
     remote-held slot (mutex-guarded; latest-wins semantics same as
     PropPose); route `ReliableKind::HeldEntityAttach` to
     `coop::held_entity_sync::ApplyHeldEntityAttach`.

5. **Sender wiring.**
   - `harness.cpp` `NetPumpTick`: call
     `coop::held_entity_sync::PumpLocalHeldPose(g_netLocal)` after the
     existing `PropPose` block. Edge-detect held→not-held into the
     fallback detach emit.
   - `harness.cpp` connect-edge block (line ~407 neighborhood): call
     `coop::held_entity_sync::QueueConnectBroadcast()` after
     `weather_sync::QueueConnectBroadcast`.
   - `harness.cpp` per-tick TickConnect block: call
     `coop::held_entity_sync::TickConnect()` for queued broadcast retry.

6. **Receiver wiring.**
   - `harness.cpp` `NetPumpTick`: call
     `coop::held_entity_sync::Tick(g_session)` after
     `coop::remote_prop::Tick(g_session)`.
   - `non_prop_entity_sync::DoApplyState`: at the end of the spawn
     branch, call `held_entity_sync::DrainPendingForIdentity(kind, identity)`
     so a queued attach drains as soon as the mirror exists.

7. **Class table extension (concurrent with STAGE 2 follow-up).**
   - Add `Aprop_dirtball_C` to
     `non_prop_entity_sync::Install()` (carried over from the morph
     RE doc §6.4 — same class table extension already planned).

8. **Cleanup (RULE 2 audit-tied).**
   - If probe (step 1) confirms candidate C: REMOVE
     `HeldEntityPose` packet + `PumpLocalHeldPose` + `Tick` + their
     session-level state. Keep only the reliable attach/detach.
   - Bump protocol version 10 → 11 in the cleanup commit (the wire
     shape changed: MsgType removed).

9. **Audit pass (RULE: after shipping, audit with agents).**
   - Spawn parallel audit agents per CLAUDE.md's audit rule:
     - perf agent: walk `held_entity_sync::PumpLocalHeldPose` for any
       per-tick GUObjectArray scan (forbidden by audit precedent —
       the 120→60 fps drop from the per-`ProcessEvent` observer).
     - thread-safety agent: verify all `g_clientHeldMirror` /
       `g_pendingAttaches` access is mutex-guarded on the GT-poster
       boundary.
     - correctness agent: verify all 4 observers (O1-O4) handle the
       inheritance case (a `prop_garbageClump_erie_C` instance
       firing the parent class's `playerHandRelease_LMB` should
       dispatch O2 with `self` = subclass; FindEntryForActor matches
       on the subclass's UClass pointer).
   - Modularity / file-size check: new
     `held_entity_sync.{cpp,h}` must land < 800 LOC.

---

## 8. Open questions

- **Q1 (mid-impl-blocker):** Hand bone socket name on the
  `mainPlayer_C` skeletal mesh. Hypothesis: `"hand_R"` (UE4 mannequin
  convention) OR `NAME_None` (attach to root). Resolved by the probe in
  step 1 — read `self->AttachParent->AttachSocketName` (on
  `USceneComponent::AttachSocketName @0x1B0`) once during a host carry.
  **Until probe lands, the receiver implementation uses
  `NAME_None` (root-attach) — the puppet's actor root tracks the
  puppet's mainPlayer pose, so root-attach gives correct-ish carry,
  just slightly offset from the hand.**

- **Q2 (deferred):** Does the host's `Hold Object` UFunction explicitly
  call `K2_AttachToActor`, or does it do per-tick SetActorLocation?
  Either way the design works (§1.3). Probe verdict only affects
  cleanup step 8 (whether to delete the unreliable fallback).

- **Q3 (STAGE 3 only):** Does the client need to broadcast attach when
  it picks up a HOST-spawned mirror (the chipPile-A on the client is a
  wire mirror, not authoritative)? Or does the client send a
  `RemoteAction::PickupRequest` and wait for the host to authoritative-
  morph + broadcast? The latter is the proper MTA pattern. Tracked as
  STAGE 3 design — out of scope here.

- **Q4 (defensive):** Should we add a `HeldEntityAttach` echo-suppress
  set (mirror of `g_incomingWireSpawn`)? Today's receiver doesn't
  re-emit on attach apply, so the set is empty. But if a future Inc
  adds an observer on `K2_AttachToActor` for some other purpose, an
  attach apply WOULD trigger that observer with `self=mirror`. Cheap
  insurance: add the set now, document as "currently unused;
  future-proof".

---

## 9. Summary

Mechanism. The held-pose-stream problem reduces to TWO reliable
edge-events (pickup attach, release detach) plus an OPTIONAL unreliable
30 Hz per-tick fallback. The reliable attach uses
`K2_AttachToActor(mirrorClump, hostPuppet, hand)` so the existing puppet
pose stream automatically drives the mirror clump's world transform
through the puppet's animation evaluation — NO per-tick clump pose
broadcast needed in the happy path.

Wire packets.

- `ReliableKind::HeldEntityAttach = 18` (32 B): attach=1 on pickup,
  attach=0 on release with launch lin/ang velocity baked in.
- `MsgType::HeldEntityPose = 9` (52 B incl. header, fallback only):
  30 Hz unreliable pose stream for the non-candidate-C path.

Identity. The same `(entityClass, identity)` tuple from STAGE 2
(`NonPropEntityStatePayload`); host-minted monotonic sessionId for the
two carried classes (`Aprop_garbageClump_C`, `AactorChipPile_C`).

Release/throw. Reliable detach packet carries inherited PhysX linear +
angular velocity (matches the existing `PropReleasePayload` pattern at
`protocol.h:373`). Receiver detaches from puppet, re-enables
`SimulatePhysics`, applies the impulse. The landing morph (clump
destroy + new chipPile spawn) is the existing STAGE 2 pipeline —
authoritative landing position from the host's broadcast, not the
client's local sim.

4-peer scalability. Attach payload carries `peerSessionId`; receiver
looks up the corresponding puppet via `coop::players::Registry::Puppet(peerSessionId)`.
Outgoing wire load worst-case 16 unreliable poses/tick + 0-1
reliables/peer/cycle = well within the 300 KB/s LAN budget.

RULE 1 / 2 compliance. The fallback unreliable stream ships behind the
attach so we don't iterate-with-blame on the probe; if the probe lands
on candidate C, the fallback is DELETED in a follow-up commit (no
parallel paths). No suppressors, no crutches — the host's BP graph runs
naturally; the receiver mirrors the engine state via standard UE4
attachment + physics UFunctions only.
