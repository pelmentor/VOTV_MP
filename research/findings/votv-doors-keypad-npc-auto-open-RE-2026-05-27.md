# RE: VOTV Doors + Keypads + NPC Auto-Open — Phase 5D extension
**Date**: 2026-05-27
**Target**: Alpha 0.9.0-n
**Method**: CXXHeaderDump static analysis. Builds on the 2026-05-25 doors+lights doc
(`research/findings/votv-doors-and-lightswitches-RE-2026-05-25.md`); identifies the
gaps the original audit did NOT cover (keypad state, NPC-auto-open mechanism) and
proposes a unified wire-protocol design for all three asks.

This doc INTENTIONALLY does not duplicate the 2026-05-25 work; consult it for door
class hierarchy, `doorOpen`/`doorClose` semantics, `Adoor_C` field layout, and the
overall door observer/apply increment plan.

---

## Section 1 — Recap of the 2026-05-25 doors+lights RE

The 2026-05-25 doc landed:

1. **Door class hierarchy.** `Adoor_C` (parent `AtriggerBase_C`, size 0x42D) is the
   primary interactable door. `Adoor_pryable_C` is a crowbar-pryable subclass.
   `AcargoliftDoor_C` is a pure prefab assembly holding two child `Adoor_C` actors.
   `Unav_door_C` is a `UNavArea` (navmesh policy, not an actor — not sync-relevant).
2. **Canonical open/close UFunctions.** `Adoor_C::doorOpen(bool bypassCheck)` and
   `Adoor_C::doorClose(bool bypassCheck)`. Both player-E-press and NPC-auto-open
   funnel through these. Hooking these POST on the sender + invoking on the receiver
   with `bypassCheck=true` gives full coverage. `Adoor_C::settime(bool opened)` is a
   no-animation snap for snapshot-on-connect.
3. **Light switch path.** `Alightswitch_C::use()` → `Atrigger_lightRoot_C::SetActive`
   → fan-out to all `AceilingLamp_C::SetActive` + `AambientLight_C::SetActive`.
   Authoritative state is the lamp's `IsActive` (save-persistent); the switch's
   `bool A` is a non-saved mirror. Hook `Atrigger_lightRoot_C::SetActive(bool)` POST
   for full coverage.
4. **Wire protocol skeleton.** Proposed `DoorState` (ReliableKind 9, 32 B),
   `LightState` (10, 32 B), `LockState` (11, 32 B) — IDs reserved in
   `protocol.h`'s `ReliableKind::ItemActivate=12` comment. Each carries a 23-byte
   `triggerKey` (FName Key string from `AtriggerBase_C::Key @0x0260`).
5. **Save persistence.** Doors and lamps both implement
   `getTriggerData`/`loadTriggerData`. Initial state covered by Phase 5S0
   snapshot-on-connect; doors/lights need no separate initial snapshot beyond what
   the save round-trip already gives.
6. **NPC auto-open mentioned but not deep-dived.** The doc noted `Adoor_C::sensor`
   (UBoxComponent @0x0308), `sensorOverlaps` array @0x0418, `ignoreNPC` flag @0x0388,
   and the `BndEvt__door_sensor_..._BeginOverlap/EndOverlap` handlers that route into
   `checkSensor()`. It concluded "NPC physically navigates through the door sensor
   volume as they pathfind ... the door itself opens reactively on sensor overlap."

The 2026-05-25 doc's coverage of pure door open/close is sufficient for ask 1. Gaps
for asks 2 and 3 are addressed below.

---

## Section 2 — NEW: Keypad RE (`ApasswordLock_C`)

Source: [passwordLock.hpp](../../Game_0.9.0n/WindowsNoEditor/VotV/Binaries/Win64/CXXHeaderDump/passwordLock.hpp).

### 2.1 — Class count

**ONE** keypad class: `ApasswordLock_C`. No subclasses found in the dump. The same
class handles both **digit-entry keypads** (`isCard=false`) and **keycard readers**
(`isCard=true`) via a single `isCard` flag (0x0390). Visual variants live in
the level placement (which child meshes are attached).

The 2026-05-25 doc treated keypad state as a simple `bool isAcc` + `bool Active`.
The actual surface is **much** richer. Full field map below.

### 2.2 — Full state-field map (corrected vs 2026-05-25)

```
class ApasswordLock_C : public AtriggerBase_C    // size 0x393

// --- inherited (offsets confirmed) ---
FName       Key                  @0x0260    // save / wire identity (from triggerBase)
TArray<...> Objects              @0x0230    // chained triggers (unused for lock semantics)

// --- new fields the 2026-05-25 doc did NOT enumerate ---
UBoxComponent* card              @0x0298    // card-reader hit zone (used when isCard=true)
UAudioComponent* Audio           @0x02A0    // beep/error audio
UBoxComponent* key_deny          @0x02A8    // "deny" indicator hit zone (red LED, etc.)
UBoxComponent* key_acc           @0x02B0    // "accept" indicator hit zone (green LED)
UBoxComponent* Key_0 .. Key_9    @0x02B8..0x0300   // ten digit-button hit zones
UBillboardComponent* Keys_0      @0x0308    // editor icon
UParticleSystemComponent* ParticleSystem3  @0x0310  // success/deny VFX
UStaticMeshComponent* cube       @0x0318    // keypad body mesh

ApasswordLock_C* Pair            @0x0320    // paired sibling lock (two locks gate one door)
FName            pair_key        @0x0328    // pair lookup by Key (resolved at BeginPlay)

bool             Active          @0x0330    // armed (false = lock disabled / story-bypass)
Adoor_C*         door            @0x0338    // resolved door pointer
FName            door_key        @0x0340    // door lookup by Key (resolved at BeginPlay)

bool             entering        @0x0348    // currently in entry mode (user focused on keypad)
bool             enterFalse      @0x0349    // currently in the "wrong code" rejection cooldown
bool             FALSE           @0x034A    // (state mirror; semantics unclear without UE4SS probe)

FString          password        @0x0350    // CORRECT password (level-baked; on game install)
bool             isReset         @0x0360    // "needs reset" -- code accepted but later force-relock
bool             protected       @0x0361    // (likely: lock is in a protected/non-toggleable state)
TArray<UPrimitiveComponent*> Keys_1  @0x0368  // resolved digit-button comps for iteration
int32            Num             @0x0378    // number of digits ENTERED so far (running progress)
bool             isAcc           @0x037C    // ACCEPTED -- code matched, door unlocked
bool             isDeny          @0x037D    // DENIED -- last attempt was wrong
FString          inPassword      @0x0380    // BUFFER of digits typed so far (UI display)
bool             isCard          @0x0390    // false = digit keypad, true = card reader
bool             isFocused       @0x0391    // a player is actively interacting (typing)
bool             particleSystemCrashesThisShitFuckYou  @0x0392   // crash-workaround flag (lol)
```

The state space is therefore *not* just `(Active, isAcc)`. The full canonical
authoritative tuple is:

```
{ Active, isAcc, isDeny, isReset, entering, isFocused, enterFalse, Num, inPassword }
```

Of these, the **save-persistent** subset is what `getTriggerData` /
`loadTriggerData` serializes (TArray<bool> bools + TArray<FString>). The doc level
of the save-format is not enumerated in headers — but at minimum `isAcc` is saved
(unlocked doors stay unlocked after reload). For Phase 5S0 connect-snapshot, the
client receives the save state and the lock's `isAcc`/`Active` are correct
on join. The transient state (typing progress: `Num`, `inPassword`, `isFocused`)
is NOT saved; that is also the state that matters for "show the keypad-being-entered
animation on the peer".

### 2.3 — Keypad → door linkage

Direct pointer: `ApasswordLock_C::door` (Adoor_C*, 0x0338), resolved at
`BeginPlay`/`intComs_gamemodeBeginPlay` from `door_key` (FName 0x0340). The lock
also installs itself into its door's `passlocks` TArray (Adoor_C::passlocks @0x0390).
This is **bidirectional**:

* Lock → Door: `ApasswordLock_C::door`. When the lock's `Open(bool Active)` is
  called and `Active=true`, the lock calls into `door` (presumably
  `door->doorOpen(bypassCheck=true)` — to be confirmed via UE4SS probe).
* Door → Lock: `Adoor_C::passlocks` is iterated by `Adoor_C::processKeys` to
  validate that ALL paired locks are `isAcc=true` before allowing a non-bypass open.

Multi-lock support: when one door has TWO keypads (e.g. both sides of a corridor),
each keypad has `Pair` (a sibling `ApasswordLock_C*`). `SetActive(bool isPairCall)`
propagates the armed/disarmed state to the partner with `isPairCall=true` to break
infinite recursion. **Coop implication**: when client unlocks a paired keypad on
side A, host's side A flips to `isAcc=true`, then host's BP propagates to side B
(via the pair pointer), then host's `Adoor_C::processKeys` sees both passlocks
accepted, then the door opens. The wire packet must carry the SIDE-A keypad's Key
(not the door's Key); the receiver re-runs `Open(true)` on that specific lock,
which internally propagates. This avoids us having to enumerate the pair on the
wire.

### 2.4 — UFunction surface (canonical hooks)

The relevant UFunctions on `ApasswordLock_C`:

1. **`void Open(bool Active)` @ passwordLock.hpp:78**
   — THE canonical "result" entry point. Called internally by the lock when the
   user successfully enters the correct password (or swipes the right card), and
   externally by story scripts. `Active=true` sets `isAcc=true` and triggers door
   open; `Active=false` triggers a relock (resets `isAcc`). **Hook this POST** for
   the sender-side observer; the broadcast is "this lock just transitioned to the
   `Active` value".

2. **`void inputNumber(int32 Num)` @ passwordLock.hpp:98**
   — Single-digit entry. Called from `playerAnykey(FKey, bool Pressed)` with the
   numeric key pressed. BP body (unconfirmed without UE4SS probe — RE Flag K-1):
   appends the digit to `inPassword`, increments the field `Num`, compares
   `inPassword` against `password`; on match call `Open(true)`; on overflow (4
   wrong digits typed) call `falseEnterEvent()`. **For coop**: we do NOT need to
   sync individual digit presses unless we want to mirror the typing UI on the peer
   (typing progress visualization). For the THREE asks the user stated, this is
   NOT required — the user wants lock state + entered-code STATE synced, where
   "entered-code state" means the digits-typed-so-far so the OTHER peer sees the
   keypad screen update. This means we DO want to sync at least `inPassword`
   buffer changes (or `Num` + a hash of `inPassword`) — see Section 5.

3. **`void falseEnterEvent()` @ passwordLock.hpp:73**
   — Called when an entry is rejected (wrong code typed). Triggers
   `enterFalse=true`, beeps, clears `Num`+`inPassword`, eventually clears
   `enterFalse`. **Hook this POST** to broadcast wrong-code-rejection so the peer
   sees the same red flash + beep.

4. **`void SetActive(bool isPairCall)` @ passwordLock.hpp:68**
   — Arms/disarms the lock itself (not the door). `Active` flips; when there is a
   `Pair` and `isPairCall=false`, propagates to the pair with `isPairCall=true`.
   **Hook this POST** for `Active`-state changes — though in normal play `Active`
   only changes via story events (which the save covers), so this is lower
   priority.

5. **`void Reset()` @ passwordLock.hpp:87**
   — Returns the lock to its powered-on no-entry-progress state. Clears `Num`,
   `inPassword`, `isAcc`, `isDeny`. Used after the door is re-closed or by
   `protected` cooldowns.

6. **`void focusOn()` @ passwordLock.hpp:95** and **`void unfocus()` @
   passwordLock.hpp:99**
   — Player begins / ends interacting with the keypad (the camera zooms in to the
   keypad face, mouse cursor becomes a number-button selector). `isFocused`
   field tracks this. **For coop** we have two options: (a) ignore focus state
   (each peer focuses independently — keypad is a non-blocking shared resource),
   or (b) host-only focus (only one peer at a time can be entering). Per RULE 1,
   the proper answer depends on UX intent. The user asked for "entered-code STATE"
   sync, which implies the input PROGRESS is mirrored on the spectating peer. So
   option (a) — both peers can focus independently, but typing on EITHER peer's
   keypad propagates the digit-buffer state to the other. The non-focused peer
   sees the UI buffer fill in real time without becoming "focused" themselves.

7. **`void open2()` @ passwordLock.hpp:91**
   — Secondary open path (BP-internal). Likely the deferred post-success animation
   chain (after the green LED + beep timeline). Not a hook target — `Open(bool)`
   covers it.

8. **`void player_use(AmainPlayer_C*, FHitResult)` @ passwordLock.hpp:84**
   — E-press entry. BP body (unconfirmed — RE Flag K-2): branches on `isCard` to
   either `focusOn()` (start typing) or process the held card (call `Open(true)`
   immediately if the card matches).

### 2.5 — RULE 1: NOT a "just sync the locked bool" carve-out

Per RULE 1, we sync the FULL state — typing progress (`inPassword` + `Num`),
wrong-code-rejection (`falseEnterEvent`), final accept/reject (`Open(bool)`),
and arming changes (`SetActive`). The user explicitly listed "entered-code state"
as one of the asks. The non-trivial path is digit-by-digit input replication; the
proper hook is `inputNumber(int32)` POST on the typing peer, broadcasting a
"digit pressed" event. The receiver runs `inputNumber(receivedDigit)` locally on
its mirror keypad. The local BP then progresses naturally (appends to
`inPassword`, increments `Num`, checks match, fires `Open(true)` or
`falseEnterEvent()` itself). **Crucially: we do NOT broadcast `Open` separately
when the typing path drove it — the receiver's local `inputNumber` execution will
naturally invoke its OWN `Open`/`falseEnterEvent` because the digit sequence
matches.** Echo-suppression then prevents the receiver's `Open` POST observer
from re-broadcasting.

`Open(bool)` broadcasts ONLY when the source is non-input — story scripts, card
swipes, or external bypass. This is enforced by: `Open` POST observer checks a
"we are mid-inputNumber-apply" flag set by the inputNumber receiver and skips
broadcast in that case.

This is the proper RULE 1 design: ONE inputNumber path for player digits, ONE
Open path for non-input-driven unlocks. No "just sync the locked bool" carve-out.

---

## Section 3 — NEW: NPC auto-open mechanism

### 3.1 — The actual code path (sensor-driven, not AI-direct-call)

Per the door header (`Adoor_C` lines 126–127), the door sensor box has TWO bound
overlap delegates:

```
BndEvt__door_sensor_..._ComponentBeginOverlapSignature  // any actor enters volume
BndEvt__door_sensor_..._ComponentEndOverlapSignature    // any actor leaves volume
```

Both update `sensorOverlaps` (TArray<AActor*> @0x0418) and call `checkSensor()`
(line 116). `checkSensor()`'s BP body (unconfirmed — RE Flag E-D3 in the original
doc): if `sensorOverlaps.Num() > 0` AND `!ignoreNPC` AND `!superClosed`, calls
`doorOpen(bypassCheck=false)`. If empty AND `autoclose`, calls
`doorClose(bypassCheck=false)`.

**The NPC does not call `doorOpen` directly.** The NPC's `UCharacterMovementComponent`
moves it through the world (driven by AI Controller pathfinding via the
NavigationSystem). When the NPC's physics body enters the door's sensor
UBoxComponent, the UE4 collision system fires the BeginOverlap delegate. The door
opens reactively.

**Confirmation from NPC headers**: I read `00000000npc.hpp` (base class — empty),
`npc_zombie.hpp` (full zombie surface, ~140 fields), and `kerfurOmega.hpp` (full
kerfur surface, ~80 fields). NEITHER class has any `door`, `targetDoor`, or
`OpenDoor` field. NEITHER has a UFunction with a name suggesting a direct door
call. NPC AI never references doors at the AI/perception level — pathfinding goes
through the navmesh, the door's `nav` (AnavModifierBox_C @0x0380) makes the door's
volume traversable for nav purposes, and the sensor overlap handles the actual
"open" trigger. This is consistent with the `ignoreNPC` flag's semantics —
because the trigger is just "any overlapping actor", the door needs an EXPLICIT
flag to disable NPC-triggered opens (otherwise it would auto-open for any actor,
including NPCs by default).

### 3.2 — NPC class filter inside `checkSensor` (RE Flag E-D3 narrowed)

The 2026-05-25 doc left open whether `checkSensor` filters its inputs. From the
field layout: there is no explicit "filter to ACharacter" flag on the door, and
`ignoreNPC` is a single boolean. The most plausible BP body:

```
checkSensor:
  if (superClosed || jammed) return;
  if (Num overlap > 0):
    // filter the relevant subset
    for (AActor* o : sensorOverlaps):
      if (o is AmainPlayer_C): wantOpen = true; break
      if (o is ACharacter && !ignoreNPC): wantOpen = true; break
      // ignore props, lights, etc.
    if (wantOpen) doorOpen(bypassCheck=false)
  else if (autoclose):
    doorClose(bypassCheck=false)
```

This implies the BeginOverlap delegate fires for every actor that touches the
sensor (even props rolling into the door), but `checkSensor`'s loop filters to
ACharacter-or-AmainPlayer_C. RE Flag E-D3 is therefore narrowed to: confirm the
ACharacter-class filter and the precise ignoreNPC interaction (does ignoreNPC
require ACharacter-but-not-AmainPlayer, or does it block ALL non-player ACharacters?).

### 3.3 — Implication for coop authority

NPC AI runs on **the host only** (per `[[project-coop-enemies-target-both]]`
direction + `npc_sync.cpp`'s host-authoritative model). The host's NPC physically
moves, overlaps the host's door sensor, host's `checkSensor` fires, host's
`doorOpen` runs. **The same `doorOpen` POST observer that catches player E-press
opens also catches NPC-triggered opens** — there is NO separate "open by NPC"
code path. So:

- If we already hook `Adoor_C::doorOpen` POST (per the 2026-05-25 plan), NPC
  auto-open is ALREADY covered. The broadcast fires on the host; the client
  receives `DoorState{action=open}`; client's door opens. Visual consistency
  achieved.
- The client's local NPC (driven by host pose stream, not local AI) might or
  might not physically overlap the client's door sensor. If it DOES, the client's
  sensor will also fire, and the client's `checkSensor` will call its OWN
  `doorOpen`. This double-fires the door open (host broadcast + local sensor).
  Idempotency is the safety net: `doorOpen` should early-return if `isOpened`
  is already true (RE Flag E-D4). RULE 1 path: confirm idempotency with a UE4SS
  probe; if NOT idempotent (re-runs the timeline), we add an explicit "incoming
  apply in progress" guard around the receiver's `doorOpen` call AND ALSO
  proactively suppress the client's local sensor on NPC overlaps.
- A safer design: add `ignoreNPC=true` to ALL doors on the **client side only**
  at session connect. Then the client's local sensor literally doesn't fire for
  the NPC puppet, and the door state is driven purely by the host broadcast.
  Cost: zero — the client's NPC is a puppet driven by host pose; there's no
  local pathfinding intent for it to "want to open" the door. Benefit: cleaner
  authority model, fewer race conditions. This IS the proposed approach. We
  DON'T flip `ignoreNPC` for the player's E-press path because the player still
  needs sensor-overlap behavior for, e.g., backwalking through a door (player's
  body overlap re-opens a closing door); but flipping ignoreNPC only blocks NPC
  overlaps, not player overlaps.

Actually — re-reading the field: `ignoreNPC` may filter NPCs from triggering the
open BUT the sensor still tracks them in `sensorOverlaps` for other purposes.
The exact semantics need confirmation. RE Flag E-D3 narrowed: confirm that
`ignoreNPC=true` on the receiver suppresses both the local doorOpen AND the
local doorClose (so the NPC walking through doesn't re-trigger the door's
autoclose timer either).

### 3.4 — Edge case: NPC + Player simultaneously trigger

Host scenario: NPC walks into the host's door sensor at the same tick a player
presses E. Host's `doorOpen` fires once (BP guards against double-fire when
`isOpened` is already mid-transition — `isMoving` flag @0x0351 implies this).
Host broadcasts ONCE. Fine.

Client scenario (puppet NPC + local player): client's puppet NPC walks into
client's door sensor (depending on host pose-stream synchrony) at the same tick
the local player presses E. Both `checkSensor` (from NPC overlap if `ignoreNPC=false`
on client) and `player_use → doorOpen` fire on the client. Two doorOpen POST
observer invocations. Echo-suppression-by-state (skip broadcast if the door's
`isOpened` was already true at PRE time) handles this; the second invocation
sees `isOpened=true` and broadcasts nothing.

This further argues for the "set `ignoreNPC=true` on client at connect" approach
to eliminate the NPC-overlap path entirely on the client.

---

## Section 4 — Gaps in the 2026-05-25 doc

1. **Keypad state surface vastly undersized.** The doc treated `ApasswordLock_C`
   as `(Active, isAcc, isCard, password)`. Actual: 12+ fields including
   `entering`, `enterFalse`, `inPassword`, `Num`, `isReset`, `isDeny`,
   `isFocused`, `protected`. The doc's proposed `LockStatePayload` only carries
   `isAccepted` — that won't sync the typing-progress UI the user asked for.

2. **Keypad UFunction surface incomplete.** The doc listed `Open(bool)` and
   `SetActive(bool)`. Missing: `inputNumber(int32)`, `falseEnterEvent()`,
   `Reset()`, `focusOn()`, `unfocus()`, `open2()`, `Pair`+`pair_key` mechanism.

3. **Paired locks (`Pair`+`pair_key`) not mentioned.** Some doors have TWO
   keypads (e.g. corridors with door access from both sides). The 2026-05-25
   design didn't account for the pair propagation in `SetActive(isPairCall)`.
   For coop the sender broadcasts the SIDE-A keypad's Key; receiver re-runs
   `Open(true)` which internally propagates via `Pair`. Echo-suppression handles
   the partner's downstream notification.

4. **NPC auto-open mechanism's specific code path was UNCONFIRMED**. The doc
   said "physically navigates through the door sensor volume" but didn't pin it
   down. This doc confirms: sensor UBoxComponent overlap delegate
   (`BndEvt__door_sensor_..._BeginOverlap`) → updates `sensorOverlaps` →
   `checkSensor()` → filtered iteration → `doorOpen(bypassCheck=false)`. NO
   direct NPC-AI-Controller call to door UFunctions. The same `doorOpen` POST
   hook proposed for the player path catches NPC-driven opens identically.

5. **Receiver-side handling of NPC-puppet overlaps not addressed**. Above doc's
   Inc4 just calls `doorOpen(bypassCheck=true)` on the receiver but doesn't
   discuss what happens when the receiver's puppet NPC ALSO overlaps the
   receiver's door sensor. Proposal: set `ignoreNPC=true` on all doors at the
   client side on connect (host keeps `ignoreNPC` as authored).

6. **`AbumpedOpenDoor` / `doorImpact` not in scope.** The door has a
   `faceSmasher` UBoxComponent (0x02D0) that fires `doorImpact(float B)` on
   collision with the panel mesh. This is the "running into a closing door
   knocks you back" damage path. Not coop-sync-relevant (it's instantaneous
   damage on the local player), but worth noting that it is NOT a door-open
   trigger — it's a player-damage event from being IN the panel sweep.

---

## Section 5 — Proposed wire packet shape (UNIFIED vs separate)

### 5.1 — Recommendation: THREE separate packets

Reserved IDs 9-11 (in protocol.h's existing comment) are explicitly queued for
DoorState / LightState / LockState. Keep them; add a fourth.

```
ReliableKind::DoorState     =  9   // unchanged from 2026-05-25 design
ReliableKind::LightState    = 10   // unchanged
ReliableKind::LockState     = 11   // ENHANCED: full keypad state (see below)
ReliableKind::LockKeyInput  = 18   // NEW: single-digit input event
```

(15-17 already used for RedSky / NonPropEntityState / NonPropEntityDestroy in v9
protocol, hence 18 for LockKeyInput.)

Why NOT one unified packet:

- DoorState is ~once per minute under heavy play (mostly NPC-driven). LockState
  is once per minute (only when a code is entered). LightState is ~once per
  minute. **LockKeyInput is dense** — 4-8 events in <2 seconds when typing.
  Their cadences differ; collapsing them into one packet either pessimizes the
  rare ones (extra discriminator byte) or bloats the common one. Three plus one
  separate types follows the existing pattern (Inc5 weather: WeatherState +
  LightningStrike separated for the same reason).

- Each packet carries ONLY the fields its action needs, no padding for
  cross-discriminator unions. Simpler de-serialization.

### 5.2 — `DoorStatePayload` (unchanged from 2026-05-25)

```cpp
struct DoorStatePayload {
    uint8_t triggerKeyLen;      // 1
    char    triggerKey[23];     // 23  — Adoor_C::Key string
    uint8_t action;             // 1   — 0=close, 1=open, 2=jam, 3=unjam, 4=settime-closed, 5=settime-open
    uint8_t bypassCheck;        // 1   — always 1 for coop (receiver skips lock validation)
    uint8_t peerSessionId;      // 1   — sender peer (host=0, joiners 1..)
    uint8_t _pad[5];            // 5
};  // 32 bytes
```

Actions 4/5 added: `settime(bool opened)` is the no-animation snap used for
Phase 5S0 snapshot-on-connect. Same packet type covers both live events and
connect-snapshot apply.

### 5.3 — `LightStatePayload` (unchanged from 2026-05-25)

32 bytes. Uses `Atrigger_lightRoot_C::Key` as identifier; receiver calls
`SetActive(bool)` on the lightRoot, which fans out to all lamps + ambs.

### 5.4 — `LockStatePayload` (ENHANCED — full keypad state)

The 2026-05-25 design carried `(triggerKey, isAccepted)` only. RULE 1: carry the
full transient state, not a "just the bool" carve-out.

```cpp
struct LockStatePayload {
    uint8_t triggerKeyLen;     // 1
    char    triggerKey[23];    // 23  — ApasswordLock_C::Key
    uint8_t stateBits;         // 1   — bit 0=Active, 1=isAcc, 2=isDeny, 3=isReset,
                               //       4=entering, 5=enterFalse, 6=isFocused, 7=protected
    uint8_t peerSessionId;     // 1   — host=0, joiners 1..
    uint8_t numDigitsEntered;  // 1   — running Num field (0..passwordLen)
    uint8_t _pad[5];           // 5
};  // 32 bytes
```

Carries the FULL transient state. Used for: SetActive arm/disarm, Open(bool),
falseEnterEvent (a single packet captures all of arming, accept, deny, focus,
typing-progress count). Does NOT carry `inPassword` content — that comes from
LockKeyInput events being replayed locally. The receiver applies stateBits by
WRITING the field offsets (these are non-BP-listener config bits; same pattern
as weather flags @0x044B). For the bits that DO have BP listeners (`isAcc`,
`isDeny`, `Active`), the receiver writes the field THEN calls the corresponding
UFunction (`Open(true)` for isAcc→1, `falseEnterEvent()` for isDeny→1,
`SetActive(false)` for Active→0, etc.) so the BP fan-out fires.

### 5.5 — `LockKeyInputPayload` (NEW)

```cpp
struct LockKeyInputPayload {
    uint8_t triggerKeyLen;     // 1
    char    triggerKey[23];    // 23  — ApasswordLock_C::Key
    uint8_t digit;             // 1   — 0..9 (the digit pressed)
    uint8_t peerSessionId;     // 1   — host=0, joiners 1..
    uint8_t _pad[6];           // 6
};  // 32 bytes
```

One packet per digit press. Receiver calls `inputNumber(digit)` on the local
`ApasswordLock_C` — this triggers the local BP's full update path: append to
`inPassword`, increment `Num`, compare to `password`, fire `Open(true)` or
`falseEnterEvent()` if appropriate. Receiver's `Open` / `falseEnterEvent` POST
observers DO fire but are suppressed by an "incoming inputNumber apply" flag set
right before the local `inputNumber` call.

Why this works without leaking the password to clients:

- The client's local `ApasswordLock_C` already has the `password` field — it's
  set at level/save load (the level data is identical on both peers). No
  trust-boundary issue.
- The client's local BP doing the match check produces the same result as the
  host's BP doing the match check, because `inPassword` is built identically on
  both peers (same digit sequence), `password` is identical, comparison is
  deterministic.

This also handles the "wrong code → both peers see red LED + beep" naturally —
client replays the sequence, hits the wrong-code branch, fires its OWN
`falseEnterEvent`, which plays its OWN beep audio.

---

## Section 6 — Hook strategy per ask

### Ask 1: Door open/close sync

**Confirmed from 2026-05-25**: Hook `Adoor_C::doorOpen(bool bypassCheck)` POST
AND `Adoor_C::doorClose(bool bypassCheck)` POST. Sender broadcasts `DoorState`.
Receiver applies via `doorOpen(bypassCheck=true)` / `doorClose(bypassCheck=true)`.
Echo-suppressed via "incoming apply in progress" state flag on the door pointer
during the receiver call.

**Symmetric authority**: either peer's open/close broadcasts; receiver applies.
NPC opens are captured automatically (same UFunction fires).

**Client-side ignoreNPC=true at connect** (this doc's addition): set
`Adoor_C::ignoreNPC=true` on every door in the scene at the client's
post-connect snapshot-apply phase. Eliminates the client's local puppet-NPC
sensor overlap from re-driving doorOpen. Host keeps `ignoreNPC` as level-
authored. Reverted at disconnect (door state survives via save).

### Ask 2: Keypad state + entered-code sync

Hook on the sender side (whichever peer is interacting):

- `ApasswordLock_C::inputNumber(int32 Num)` POST → broadcast `LockKeyInput` with
  the digit.
- `ApasswordLock_C::Open(bool Active)` POST → broadcast `LockState` with the
  full state tuple. Guarded by "we are NOT mid-inputNumber-apply" flag (because
  inputNumber's natural BP path will fire Open and the digit replay on the
  receiver will fire its own Open).
- `ApasswordLock_C::falseEnterEvent()` POST → broadcast `LockState` with isDeny
  bit. Same guard.
- `ApasswordLock_C::SetActive(bool isPairCall)` POST → broadcast `LockState`
  with the new `Active` bit. Guard skipped (SetActive isn't fired by
  inputNumber, it's story-script-driven).
- `ApasswordLock_C::Reset()` POST → broadcast `LockState` with cleared state.
- `ApasswordLock_C::focusOn()` POST → broadcast `LockState` with isFocused bit.
  (Optional — only needed if peers should see "the other player is at the
  keypad" UI indicator.)

Receiver applies by either:
- LockKeyInput: call `inputNumber(digit)` on the local lock (the natural BP
  fan-out drives `inPassword`/`Num`/`Open`/`falseEnterEvent`).
- LockState (non-input-driven): write the field bits, then call the appropriate
  UFunction to fire BP listeners (Open(true) when isAcc went 0→1; falseEnterEvent
  when isDeny went 0→1; SetActive when Active changed; Reset when transitioning
  to all-cleared).

### Ask 3: NPC-triggered auto-open sync

**No new packet needed.** The NPC-auto-open mechanism funnels through
`Adoor_C::doorOpen(bool)` POST — the exact same hook as ask 1. The host's
authoritative NPC AI drives the host's door, which broadcasts `DoorState`. The
client receives it and opens its mirror door. Done.

The only extra step is the client-side `ignoreNPC=true` flip at connect
(described in ask 1) to prevent the client's puppet NPC from triggering the
client's local sensor and producing a double-fire.

---

## Section 7 — Host-authoritative model breakdown

| State | Authority | Sync direction | Notes |
|---|---|---|---|
| Door open/close (player-triggered) | **Symmetric** | Either peer broadcasts | Echo-suppress via isOpened-was-already check |
| Door open/close (NPC-triggered) | **Host-authoritative** | Host broadcasts | NPC AI is host-only; client's puppet NPC has `ignoreNPC=true` set |
| Door jam / jamCancel | **Host-authoritative** (mostly) | Either peer broadcasts (rare; player jam tools too) | The `jam` BP-callable can also fire from player crowbar interactions |
| Lock keypad `Active` (arm state) | **Host-authoritative** | Host broadcasts | Driven by story events; client just receives |
| Lock keypad `isAcc` (unlocked) | **Symmetric** (during typing) | Either peer's `inputNumber` POST broadcasts | The local BP's match check decides; suppress Open broadcast when fired from inputNumber path |
| Lock keypad digit typing | **Symmetric** | Either peer's `inputNumber` POST broadcasts | LockKeyInput packet, one per digit |
| Lock keypad `isFocused` | **Per-peer local** OR symmetric | Optional broadcast | Both peers can focus independently; broadcast is "peer X is focused on this keypad" UI affordance |
| Lock keypad `Pair` propagation | **Receiver-side BP** | Not broadcast | Receiver's local `Open(true)` propagates via `Pair` pointer naturally |
| Light switch state | **Symmetric** | Either peer broadcasts | (2026-05-25 design unchanged) |

---

## Section 8 — Edge cases + open questions

### 8.1 — Open RE flags

**K-1**: `ApasswordLock_C::inputNumber(int32)` BP body — does it (a) immediately
compare `inPassword==password` after every digit, or (b) only after `Num` reaches
`password.Len()`? Affects whether LockKeyInput is sufficient or we need additional
"end-of-entry" signal. **Probe**: UE4SS Lua hook inputNumber PRE; observe `Num`
and `inPassword` before+after; type a code one digit at a time and observe when
`Open`/`falseEnterEvent` fires.

**K-2**: `ApasswordLock_C::player_use` BP body — branches on `isCard` flag? Does
`isCard=true` path call `Open(true)` immediately when the right card is held, or
does it call a sub-function?

**K-3**: `ApasswordLock_C::Open(bool Active)` invocation source. Beyond
`inputNumber`-driven path and player_use card-swipe path, does any external
trigger (story event, hack) call `Open`? Need to grep BP graphs (impossible
without UE4SS); proxy: search for `passlock->Open` calls in all
`ExecuteUbergraph_*` functions (we'd need a UE4SS Lua iteration).

**K-4**: Paired-lock semantics — does pressing 1 digit on side A propagate the
buffer to side B? If yes, the LockKeyInput packet's `triggerKey` resolution
needs to know which side's buffer is canonical. Practical assumption: side A
and side B have INDEPENDENT buffers (you type the code on whichever side you
went to; pair just shares the `Active` arm bit). Confirm via UE4SS.

**K-5**: `ApasswordLock_C::isReset` semantics — when does it flip back?
On door close? On a separate timer? Affects whether we need to broadcast
LockState transitions back to the unlocked-but-needs-reentry state.

**K-6**: The `falseEnterEvent` cooldown duration — what timer drives clearing
`enterFalse` back to false? If the receiver replays the wrong code (via
LockKeyInput packets), its local `falseEnterEvent` runs the same timer with
the same duration, so timing converges. But if the host receives a delayed
packet sequence (laggy LAN), the cooldown could end on host before client even
sees the rejection. Acceptable (UI cosmetic), but worth noting.

### 8.2 — Door state edge cases

**Door open during connect**: snapshot-on-connect (Phase 5S0) sends `DoorState`
with `action=settime-open` (action=5) for every door currently `isOpened`. The
receiver uses `settime(bool opened=true)` for a no-animation snap. Live events
during the snapshot replay window must NOT race the snapshot — sequence per
Phase 5S0's reliable ordering.

**Door mid-animation (`isMoving=true`)**: the snapshot uses `settime` which
ignores `isMoving` and snaps. Live events also fine — `doorOpen` is reentrant on
the timeline (RE Flag E-D4).

**Door with passlocks but `Active` arm bit on client is stale**: if the host's
keypad `Active=true` (armed) but the client hasn't received the connect-snapshot
LockState yet, and the host broadcasts a `DoorState{open}` first, the client's
door's `passlocks[0]->isAcc` is false on the receiver — but the receiver passes
`bypassCheck=true`, so `processKeys` is skipped. Safe.

### 8.3 — Keypad state edge cases

**Two peers typing simultaneously on the same keypad**: each peer's
`inputNumber` POST broadcasts its digit. The receiving peer's LOCAL keypad runs
its own `inputNumber`, which appends to its OWN `inPassword`. After both peers
have typed, both peers' `inPassword` buffers contain interleaved digits — wrong
code, `falseEnterEvent` fires. Acceptable behavior — concurrent input by two
players on one keypad SHOULD fail (real-world UX matches). If we want to gate
to one peer at a time, add a "lock claim" flag (host claims when its `isFocused`
goes 0→1, denies other peer's LockKeyInput while claimed).

**Peer disconnects mid-typing**: keypad's transient state stays as last-received.
Local `Reset()` will eventually clear it (next door close, or `enterFalse`
cooldown). Or proactively: on peer-disconnect, send a synthetic `Reset()` to
the keypad if `Num>0`. Low priority.

**Keypad `protected` flag**: unknown semantics. RE Flag K-7 — when does this
flip? Could be the "you tried 4 wrong codes, locked out for 60s" cooldown.
Both peers will see it if we sync the LockState bit. If the cooldown is
local-only (timer on host only), client's timer wouldn't run — needs
investigation.

### 8.4 — NPC-auto-open edge cases

**Door is locked (`Active` on a passlock, `isAcc=false`), NPC walks into
sensor**: host's `checkSensor` → `doorOpen(bypassCheck=false)` → `processKeys`
fails → door stays closed. No broadcast (POST observer skips if `isOpened`
didn't change). Client sees no event. Correct.

**Door opens for NPC on host, NPC leaves sensor on host, door autocloses on
host**: two `DoorState` events broadcast (open + close). Client applies both.
Smooth.

**NPC's puppet on client lags behind host's NPC by 100ms**: host's NPC enters
sensor → host's door opens → host broadcasts `DoorState{open}` → client door
opens (animation plays). Client's puppet NPC arrives 100ms later. Even with
`ignoreNPC=true` on the client, the puppet just walks through the (already-
open) door. Correct.

**Client's puppet NPC desyncs and walks through where the door SHOULD be but
isn't on client**: door is closed on client (broadcast lost). Puppet NPC walks
through the closed door visually. Bad. Mitigation: snapshot-on-connect already
covers initial state; for in-session, the reliable channel guarantees delivery
of `DoorState` packets (ARQ). The only way for desync to persist is a
catastrophic packet drop, which the existing reliable layer handles via retries.

### 8.5 — Per-peer keypad-state divergence after disconnect / reconnect

When a client reconnects (per `[[project-coop-mushroom-desync-and-remedy]]`),
the host re-sends the FULL world snapshot. Keypad state is save-persistent for
the `Active`/`isAcc` bits. Transient state (`Num`, `inPassword`, `entering`,
`isFocused`, `enterFalse`) is NOT save-persistent — on host's save side, those
fields reset after every reload. So on reconnect, the snapshot carries the
host's CURRENT transient values (which are usually all cleared anyway, because
nobody is actively typing). Send LockState for every keypad in the scene during
connect-snapshot; receiver applies. Same pattern as the door snapshot.

### 8.6 — `Adoor_pryable_C` crowbar subclass

Inherits all `Adoor_C` UFunctions. The `crowbarOpen(ApryingCrowbar_C*)` is a
SEPARATE entry point that bypasses the normal lock checks (force-open via the
crowbar minigame). Sender broadcasts `DoorState{action=open, bypassCheck=1}`.
Receiver applies. The CROWBAR-side state (the prying minigame progress) is NOT
covered here — that's per-peer (each peer drives their own crowbar). Future scope.

### 8.7 — `AcargoliftDoor_C`

A pure prefab with two child `Adoor_C` actors. Each child has its own Key. Sync
is via the children individually (broadcast two `DoorState` packets, one per
panel) — same as solo Adoor_C handling. The assembly actor itself has no UFunctions
to hook.

### 8.8 — Soltomia cleaning event

`soltomiaCleaning.hpp` has `FName doorOpen @0x02A0` and 4 Adoor_C* fields
(`door`, `jammedDoor`, `jammedDoor2`, `jammedDoor3`). This is a story-event
actor that opens/jams specific doors during the cleaning sequence. It calls
`door->doorOpen()` and `door->jam()` directly — captured by the same
`Adoor_C::doorOpen` / `Adoor_C::jam` POST observers. No extra wire needed.

### 8.9 — `ApowerControl_C::doorsOpen`

The power board iterates `doorsOpen` calling `doorOpen(bypassCheck=true)` on
each. Captured by the door observer. No extra wire needed.

### 8.10 — `Aevent_passwordGuesser_C`

A story actor that holds a single `ApasswordLock_C* passlock`. When triggered,
the BP body likely calls `passlock->Open(true)` to force-unlock during the
event. Captured by the Open POST observer. No extra wire.

---

## Section 9 — Increment plan (extends the 2026-05-25 plan)

The original 2026-05-25 Inc1-Inc7 covered door + light + simple lock state.
This doc extends to:

**Inc4b (between Inc4 and Inc5 from original plan): Client-side ignoreNPC flip**

- On connect, after door key-resolution but before any wire traffic, iterate
  all `Adoor_C` actors. If `IsClient()`, set `ignoreNPC=true` on each (reflection
  field write at offset 0x0388). Restore at disconnect.
- Files: `door_sync.cpp`.
- Test: autonomous LAN test, host spawns NPC near a door, verify client's door
  does NOT auto-open from the puppet's overlap; verify it DOES open when host's
  authoritative NPC reaches the door (via the DoorState broadcast).

**Inc6 (replaces 2026-05-25 Inc6 — full keypad state)**

- Add `ReliableKind::LockState=11`, `LockStatePayload` (32 B, stateBits +
  numDigitsEntered).
- Hook `ApasswordLock_C::Open(bool)`, `falseEnterEvent()`, `SetActive(bool)`,
  `Reset()` POST. Build `g_lockByKey` at connect.
- Guard each observer with the "we are mid-inputNumber-apply" flag.
- Receiver: lookup lock by Key; write `Active`/`isAcc`/`isDeny`/`isReset`/
  `protected` fields; call the relevant UFunction to fire BP listeners.

**Inc6b (NEW): Lock digit input replication**

- Add `ReliableKind::LockKeyInput=18`, `LockKeyInputPayload` (32 B, digit byte).
- Hook `ApasswordLock_C::inputNumber(int32)` POST. Broadcast LockKeyInput.
- Receiver: lookup lock; set "incoming inputNumber" flag; call
  `inputNumber(digit)` on the local lock; clear flag.
- Test: autonomous LAN test, client types a 4-digit code on a keypad, host's
  keypad UI mirrors digit-by-digit and naturally fires `Open(true)` when the
  code matches.

**Inc7 (extended): Snapshot-on-connect**

- Original Inc7 covered doors and lights. Extend to LockState: for every
  `ApasswordLock_C` in scene, send a LockState packet with the current stateBits.
  Use `settime`-like snap (just write the fields; no UFunction calls for the
  cleared-state case) for the typing buffer-cleared default.

**Optional Inc8: focus state UI indicator**

- Broadcast LockState with `isFocused` bit on `focusOn`/`unfocus` POST. Receiver
  shows a "PeerX is at this keypad" overlay above the keypad. Low priority — UX
  affordance only.

---

## Section 10 — Summary checklist for the three asks

- **Ask 1 — Door open/close sync.** Already designed in 2026-05-25. Hook
  `Adoor_C::doorOpen`/`doorClose` POST. ReliableKind::DoorState=9.
  Symmetric authority. NPC-driven opens captured automatically.
  Augment with: client-side `ignoreNPC=true` flip at connect.

- **Ask 2 — Keypad state + entered code sync.** NEW. Hook
  `ApasswordLock_C::Open`/`falseEnterEvent`/`SetActive`/`Reset` POST for the
  state transitions (ReliableKind::LockState=11, ENHANCED stateBits payload).
  Hook `inputNumber(int32)` POST for digit-by-digit typing
  (ReliableKind::LockKeyInput=18). Symmetric authority during typing;
  host-authoritative for SetActive (story events).

- **Ask 3 — NPC-triggered auto-open sync.** No new packet. NPC walks into
  door's sensor UBoxComponent on host → host's `checkSensor` →
  `doorOpen(false)` → broadcast via DoorState observer (same hook as ask 1).
  Client applies. Client's local puppet-NPC's overlap is muted by the
  `ignoreNPC=true` flip from ask 1's Inc4b.

All three asks share ONE underlying mechanism family (UFunction POST observer +
reliable packet + receiver re-invocation through reflection). No crutches; no
broad suppressors. Each hook targets the canonical entry point for that state
change. Open RE flags K-1..K-7 + E-D3..E-D6 documented for the UE4SS-probe pass
before implementation.
