# RE: VOTV password-keypad (`ApasswordLock_C`) accept/enter flow + correct coop sync

**Date**: 2026-06-04
**Target**: Alpha 0.9.0-n (`VotV-Win64-Shipping.exe`, imagebase 0x140000000)
**Method**: IDA Pro (dispatch architecture), CXXHeaderDump static analysis
(`CXXHeaderDump/passwordLock.hpp`), prior synthetic probe (`coop/dev/keypad_probe`),
field-semantics reasoning. Supersedes the keypad sync in
`votv-doors-keypad-npc-auto-open-RE-2026-05-27.md` (and the v31 shipped design).

> **Bottom line up front.** Our shipped keypad channel ("poll `isAcc` + replay
> `Open(bool)`") is wrong at the root: `Open(bool Active)` is **not** a state setter
> — it is the keypad's **submit/evaluate verb** (the physical ENTER button). Calling
> it on the receiver re-runs the evaluate-and-reject path, which is what produces the
> red/fail **cycle even when idle**. The keypad is a **pure Blueprint** (its verbs do
> not exist as native code in the exe — IDA-confirmed), so its accept logic is
> interpreted bytecode we cannot decompile; and those verbs dispatch via
> `CallFunction → ProcessInternal`, **bypassing our ProcessEvent detour**
> (IDA-confirmed at `0x141457601`), so no POST observer on them can ever fire. The
> correct sync is **edge replication of the digit/enter INPUT** — mirror each
> `inputNumber(digit)` and the ENTER/submit, echo-suppressed — so each peer's own BP
> evaluates the identical (same `password`, same digit sequence) and converges
> deterministically, instead of us forcing `isAcc` or calling `Open` out of context.

---

## 1 — Dispatch architecture (IDA-confirmed): why every keypad verb is invisible

The mod's ProcessEvent detour is on `UObject::ProcessEvent` @ **`0x141465930`**
(named `UObject_ProcessEvent` in the IDB). Inside it, BP bytecode is executed by
**`ProcessInternal`** @ **`0x141302DC0`** (called from PE at `0x141465c10`).

`ProcessInternal` (0x141302DC0) just invokes the UFunction's `Func` pointer at
`+216` (`result = (*(...)(a1 + 216))(...)` @ `0x141302e33`). For a BP UFunction that
`Func` is the bytecode interpreter (`ProcessLocalScriptFunction`), so the body runs
**without re-entering ProcessEvent**.

The BP-to-BP call opcode handler is `UObject::CallFunction` @ **`0x1414573C0`**
(GNatives handler for `EX_FinalFunction` / `EX_LocalFinalFunction` /
`EX_VirtualFunction`). Decompiled, its terminal dispatch is:

```
0x141457432:  v10 = 2;  v27 = 2;            // taken when (FuncFlags & 0x12051CC)==0
                                            // i.e. a normal, non-net, non-exec BP fn
...
0x1414575b6:  if ((v10 & 2) == 0) return sub_14146BC40(...);   // net path (NOT taken)
0x141457601:  return sub_141302DC0(a4, v8, a2, v5);            // ProcessInternal — TAKEN
```

**So a Blueprint verb calling another Blueprint verb goes straight to
`ProcessInternal` (0x141302DC0), never through `ProcessEvent` (0x141465930).** Our
detour, and therefore any `RegisterPostObserver`, is on ProcessEvent only.

Consequence for the keypad: `inputNumber`, `Open`, `falseEnterEvent`, `focusOn`,
`unfocus`, `player_use` — when invoked from the keypad's own BP graph or from the
interaction system's BP graph — are dispatched BP-internally and a POST observer on
them **never fires** (same trap that broke doors `sent=0` and the flashlight
`updateFlashlight`). This is the hard constraint every design below respects.

A reflected `Call(...)` WE make (via `ParamFrame`/`Call`, which calls
`ProcessEvent`) *does* reach the engine and runs the verb — that is why the receiver
*can* still invoke a verb; it just can't *observe* one.

---

## 2 — The keypad is a pure Blueprint: IDA cannot decompile its accept logic

Searched the exe string/symbol table for every keypad identifier:

| identifier | strings in exe |
|---|---|
| `passwordLock` / `ApasswordLock_C` | **0** |
| `inputNumber` | **0** |
| `falseEnterEvent` | **0** |
| `player_use` | **0** |
| `inPassword`, `isAcc`, `isDeny` | **0** |
| `focusOn` (lowercase, the keypad verb) | **0** |

`list_funcs(filter="*passwordLock*")` → empty; `lookup_funcs(ApasswordLock_C::Open …)`
→ all `Not found`. **None of the keypad's logic is native.** It is Blueprint
bytecode living in the cooked `.uasset` (loaded into the UFunction `Script` TArray at
runtime), not in the `.exe`. IDA static analysis of the binary therefore *cannot*
decompile `Open` / `inputNumber` / `falseEnterEvent` / `player_use` — there is
nothing native to read. (Where reflection/probe can't see a value either, it is
flagged "LIVE-PROBE" below.)

This is itself the key RE result: **the only way to read the exact BP accept graph is
a UE4SS live BP dump / Lua hook** (dev-tool, RULE 3). Everything in §3 is reconstructed
from the field+UFunction surface, the deterministic field semantics, and the prior
synthetic-probe result; the items that still need a live probe are listed in §6.

---

## 3 — The real accept/enter flow (reconstructed verb-by-verb)

Field map (`passwordLock.hpp`, all offsets confirmed in the dump):
`Key@0x0260` (FName, inherited from `AtriggerBase_C` — wire identity), `Active@0x0330`
(armed/locked), `door@0x0338`, `entering@0x0348`, `enterFalse@0x0349` (wrong-code
cooldown), `password@0x0350` (FString, correct code — present on BOTH peers from the
identical level/save), `Num@0x0378` (int32, digits entered so far), `isAcc@0x037C`
(accepted), `isDeny@0x037D` (denied/red), `inPassword@0x0380` (FString typed buffer),
`isCard@0x0390`, `isFocused@0x0391`. UFunctions of interest:
`player_use(AmainPlayer_C*, FHitResult)`, `focusOn()`, `unfocus()`,
`playerAnykey(FKey, bool Pressed)`, `inputNumber(int32 Num)`, `Open(bool Active)`,
`falseEnterEvent()`, `beep()`, `isButtonUsed(bool& failed)`, `processKeys(bool&)`,
`Reset()`, `SetActive(bool isPairCall)`, `open2()`.

### 3.1 The flow (digit-keypad, `isCard=false`)

1. **`player_use(player, hit)`** — the interaction system's E-press entry. On a digit
   keypad it calls **`focusOn()`** (camera zooms to the keypad face; cursor becomes a
   button selector). `isFocused → true`. *(LIVE-PROBE: confirm player_use→focusOn vs a
   direct number on hit.)*
2. While focused, the player aims at a digit hitbox (`Key_0..Key_9`, the per-digit
   `UBoxComponent`s) and clicks. The click routes (via `playerAnykey` / the focused-
   interaction handler) to **`inputNumber(digit)`**, which **appends the digit to
   `inPassword`** and **increments `Num`**. *(Probe-confirmed: synthetic
   `inputNumber(d)` grows `inPassword` by one digit and bumps `Num`.)*
3. The player aims at the **physical ENTER button** — a distinct `UBoxComponent`
   (the dump exposes `key_acc@0x02B0` "accept" and `key_deny@0x02A8" "deny" indicator
   zones; the ENTER hitbox is one of the keypad's button components, *not* a digit
   `Key_n`). Clicking it fires the **submit/evaluate** verb. The evaluate verb is
   **`Open(bool Active)`** invoked with the *result of the comparison*: it (or a
   wrapper that calls it) compares `inPassword == password`, and:
   - **match →** `isAcc → true`, green `key_acc` LED + `ParticleSystem3` success VFX,
     and it drives the gated **`door`** (`door->doorOpen(bypassCheck=true)`) +
     propagates to the **`Pair`** lock; `inPassword`/`Num` cleared.
   - **mismatch →** **`falseEnterEvent()`**: `enterFalse → true`, `isDeny → true`
     (red `key_deny` LED), `beep()` error tone, `inPassword`/`Num` cleared, a cooldown
     timer clears `enterFalse` after ~1 s.

   *(LIVE-PROBE: confirm ENTER binds to `Open(bool)` directly and that `Open`'s `bool
   Active` param is "did the code match" (the submit result) — NOT "force this final
   state". The name "Active" + the door-driving behaviour strongly indicate
   submit-result, matching the observed fail-cycle below.)*

### 3.2 What `Open(bool Active)` is — and is NOT

`Open(bool Active)` is the keypad's **submit / commit-result** verb (the ENTER
action's handler), **not** an `isAcc` setter. Evidence:
- The prior probe proved `inputNumber` alone never flips `isAcc` even after the full
  correct code is typed — so there is a **separate submit step** between "typed the
  code" and "accepted". The only verb in the surface that fits the submit step (drives
  door + pair + LEDs) is `Open` (`open2()` is the deferred post-success animation
  chain).
- The user's hands-on symptom: clicking the physical ENTER yields a **fail/red, in a
  repeating cycle, even when not pressing**. That is the *evaluate-and-reject* path
  firing repeatedly — i.e. a *submit* verb being invoked when the buffer doesn't match
  — which is exactly what calling `Open` out of context does (§4).

This is the precise correction to the prior doc/v31, which assumed
`Open(true)=set-accepted, Open(false)=relock`. It is really `Open(submitResult)` →
match-or-reject.

### 3.3 Card readers (`isCard=true`)

`player_use` branches on `isCard`: a card swipe evaluates the held keycard and calls
the same submit path (`Open`) with the match result. Out of scope for the digit-entry
bug, but the same edge-replication design covers it (the swipe is a single submit
edge). *(LIVE-PROBE.)*

---

## 4 — Why our shipped channel breaks local use AND fail-cycles when idle

The shipped `g_keypadAdapter` (`interactable_sync.cpp:464`) is:

```
ReadState  = TryReadAccepted  -> reads isAcc@0x037C
ApplyState = CallOpen(actor, want)  -> reflected ProcessEvent call of BP Open(bool)
```

driven by the generic Channel that (a) **polls** `isAcc` each net-pump tick and
broadcasts `KeypadState` on change, (b) on receive **calls `Open(want)`** whenever
`isAcc != want`, and (c) **connect-snapshots** every keypad by calling `Open(state)`.

Two independent failure modes, both rooted in "`Open` is submit, not a setter":

**(A) The receiver / snapshot calls `Open(bool)` out of context → evaluate-and-reject.**
`ApplyResolved` calls `ApplyState=CallOpen(actor, want)` whenever the polled `isAcc`
differs from the wanted value. But the receiver's keypad has an **empty/short
`inPassword`** (nobody typed on it). `Open` therefore runs the submit path with a
non-matching buffer → `falseEnterEvent()` → red `isDeny` + `beep`. And because the
host's `isAcc` and the client's `isAcc` keep disagreeing (the host accepted; the
client's `Open` *rejected* so its `isAcc` stays 0), the Channel's poll sees `isAcc`
**still != want every tick** and **re-applies `Open` forever** → the **idle fail
cycle**. The exact call chain: `Channel::Tick → PollAndBroadcast`/deferred-retry →
`ApplyResolved → a_.ApplyState → passwordlock::CallOpen → ProcessEvent → BP Open(bool)
→ compare empty inPassword → falseEnterEvent()`.
(`Open(false)` "relock" is equally bogus — there is no evidence `Open(false)` relocks;
it just submits with `Active=false`, another reject.)

**(B) Even the local accept is corrupted.** While a real local player is mid-entry,
the per-tick poll + the snapshot can fire `Open`/`KeypadState` on that same keypad,
re-evaluating a partial buffer and tripping `falseEnterEvent` underneath the player —
so the player "can input numbers but can't push accept": every time the real ENTER
would commit, our out-of-context `Open` has already reset the buffer / forced a reject
state, and the poll immediately fights the player's `isAcc` back to 0.

Root cause in one line: **we treat `isAcc` as the synced state and `Open(bool)` as its
setter, but `isAcc` is an *output* of a submit verb that has preconditions
(`inPassword==password`) the receiver/snapshot never satisfies.** Forcing the output by
re-firing the submit verb re-runs its reject branch. (This is the classic
"don't replicate a derived output by replaying the verb that derives it" error.)

That is why the channel is currently DISABLED in `ChannelForKind`/`Tick`/
`QueueConnect` (interactable_sync.cpp:496, 607, 614).

---

## 5 — The correct sync design (RULE 1)

### 5.1 Principle

Replicate the **player INPUT edges**, not the derived `isAcc` output. Each peer's
keypad has the *same* `password` (identical level/save) and runs the *same*
deterministic BP. If both peers see the same digit sequence and the same ENTER, both
peers' BPs reach the same `isAcc`/`isDeny` **on their own** — no forcing, no
out-of-context `Open`, no fail-cycle. This is exactly the MTA "replicate the input,
let each client's deterministic sim resolve" shape (keysync), and it is what the prior
doc's §2.5 / §5.5 already prescribed before the v31 poll regression replaced it.

Two edges to replicate:

1. **`inputNumber(digit)`** — one reliable packet per digit typed.
2. **The ENTER / submit** — one reliable packet when the player commits.

### 5.2 The blocker, and how to clear it

The senders (`inputNumber`, the submit verb) are **BP-internal → not
ProcessEvent-observable** (§1). So we **cannot** POST-observe them directly. Options,
in RULE-1 preference order:

- **Preferred — observe the FOCUSED-INPUT edge that IS dispatched via ProcessEvent.**
  The keypad is driven by the player interaction / focused-input system. The
  *interaction dispatch itself* (the engine routing a focused click to the keypad)
  may go through ProcessEvent even though the keypad's internal verbs don't. The
  candidate observable edge is the input action that the focused keypad consumes
  (an `InpActEvt_*` / `playerAnykey` dispatched by the engine input system, like the
  door's `player_use` we already POST-observe at `interactable_sync.cpp:542`). **If
  `playerAnykey(FKey, bool)` or `player_use` on `ApasswordLock_C` fires our POST
  observer** (LIVE-PROBE P1), we read the resulting `inPassword`/`Num` delta in the
  POST and emit the corresponding `inputNumber`/submit edge. This mirrors the proven
  door `player_use` same-frame-request hook.

- **Fallback — `inPassword`/`Num` delta poll (sender), bounded, focused-only.** If no
  input verb is observable, POLL `Num`+`inPassword` **only on keypads with
  `isFocused==true`** (so idle keypads are never touched). On `Num` increment, emit
  `inputNumber(newDigit)` for the appended digit; on `Num` reset-after-nonzero with
  `isAcc` 0→1 emit a SUBMIT-accept edge; on `isDeny` 0→1 emit a SUBMIT-reject edge.
  This polls *input*, never forces *output*. Cost is O(focused keypads) which is ~0–1.

In BOTH cases the **wire payload changes** from "isAcc on/off" to **digit/submit input
events** (see §5.4).

### 5.3 Receiver — replay the input, never force output

- On **`inputNumber(digit)`**: set an echo-suppress flag for this lock, then reflected-
  `Call` **`inputNumber(digit)`** on the local mirror. The local BP appends to *its*
  `inPassword`, bumps *its* `Num` — exactly mirroring the sender. **No `Open`, no
  `isAcc` write.**
- On **SUBMIT-accept**: reflected-`Call` the **submit verb** (`Open` — LIVE-PROBE the
  exact verb/param, §6 P2) **only after** the mirror's `inPassword` already equals
  `password` from the replayed digits, so the local BP's *own* compare matches and it
  flips `isAcc`, lights the green LED, opens the `door`, propagates to `Pair` — all
  natively, in-context. Because the buffer matches, the reject branch is unreachable →
  **no fail-cycle.**
- On **SUBMIT-reject**: optionally replay submit (buffer won't match → local
  `falseEnterEvent` runs → red LED + beep mirrored), OR just let it be (cosmetic).
- **Echo-suppression**: a per-lock "incoming-apply" flag set around each reflected
  call; the sender path (observer or focused-poll) skips emitting while the flag is set
  (identical to the Channel's existing `echo_` latch, but per-lock so two keypads don't
  cross-suppress).
- **Idempotency / dedup**: tag each input edge with a monotonic per-lock sequence; the
  receiver ignores already-applied/duplicate digits so a re-sent reliable packet can't
  double-append. (The current 2-state KeyedTogglePayload has no sequence — §5.4.)

This **cannot** break local use: idle/locally-typing keypads are never written by us
(no poll of idle keypads, no out-of-context `Open`), and the receiver only ever
*replays the same input the sender's player made*, letting the local BP decide.

### 5.4 Wire — replace `KeypadState` semantics

`KeypadState=25` currently carries `KeyedTogglePayload{key, action(on/off)}` (40 B).
Repurpose to an **input-edge** payload (still 40 B, fits one reliable datagram):

```
struct KeypadInputPayload {
    WireKey  key;       // 32  -- ApasswordLock_C::Key (cross-peer-stable; v31 proved 0xF75D…/14)
    uint8_t  kind;      // 1   -- 0=digit, 1=submit-accept, 2=submit-reject, 3=reset/clear
    uint8_t  digit;     // 1   -- 0..9 (when kind==0)
    uint8_t  seq;       // 1   -- per-lock monotonic sequence (dedup / ordering)
    uint8_t  _pad[5];   // 5
};                       // 40
```

`Key` cross-peer stability is already VERIFIED (v31 keysHash `0xF75D…`/14 stable
host vs client). Relayable like the other interactables (host fan-out to other
clients). Drop `DoorOpenRequest`'s isAcc-style reuse for keypads.

### 5.5 Connect-snapshot

A joiner loads its **own** save, which persists `isAcc`/`Active` for already-unlocked
keypads (the save round-trips `getTriggerData`/`loadTriggerData`). So an already-
accepted keypad is **already green + its door unlocked on the joiner from its own save
load** — *no snapshot needed for the steady state*. The v31 snapshot (calling
`Open(state)` per keypad) was the active harm: it re-submitted every keypad on connect
→ mass `falseEnterEvent`. **Remove keypad from the connect-snapshot entirely.** Only
*live* in-progress entry needs the wire; settled state comes from the save. (If a rare
host-only runtime unlock must reach a joiner whose save predates it, send a single
SUBMIT-accept edge for that one lock — but verify it's needed via LIVE-PROBE before
adding; default is no snapshot.)

### 5.6 Pair / door / NPC interplay

- **Pair**: the receiver's local `Open` propagates to `Pair` natively (the BP does it);
  do **not** replicate the pair separately — same as the prior doc §2.3.
- **Gated door**: when the receiver's local submit opens `door`, the DOOR channel
  (HostAuth) converges the door independently; no cross-channel coupling needed (the
  keypad adapter comment at interactable_sync.cpp:471 already notes this and it remains
  correct).
- This is unrelated to the door fail-cycle; doors stay on their own HostAuth channel.

---

## 6 — Items still needing a live UE4SS / probe pass (don't ship without these)

The keypad being pure BP means these MUST be confirmed live (UE4SS Lua hook on
`ApasswordLock_C`, or extend `coop/dev/keypad_probe`) before re-enabling:

- **P1 (observability).** Does a POST observer fire on **`playerAnykey`** or
  **`player_use`** for `ApasswordLock_C` on a real focused click? (Doors' `player_use`
  DOES fire — interactable_sync.cpp:558 — so there is precedent the interaction entry
  is observable even though internal verbs aren't.) Determines §5.2 preferred vs
  fallback.
- **P2 (the submit verb + its bool).** Confirm the physical ENTER button binds to
  **`Open(bool Active)`** (vs a wrapper), and that `Active` is the *compare result*
  (submit), NOT a forced final state. Hook `Open` PRE, read `inPassword`/`password`/
  the arg, click ENTER with a right and a wrong code.
- **P3 (digit→accept).** With a live focused keypad, type the correct code digit-by-
  digit via real clicks; confirm `inputNumber` fires per digit (BP-internal, won't show
  on our observer — read the `inPassword`/`Num` delta passively) and that `isAcc`
  flips ONLY at the ENTER submit, not at the last digit. (Prior synthetic probe already
  showed digits-alone don't accept — re-confirm with the real ENTER.)
- **P4 (reset semantics).** When/why `Num`/`inPassword` clear (on submit, on unfocus,
  on a timer) — for the SUBMIT/RESET edge definition + the dedup `seq` reset point.
- **P5 (card path).** `isCard=true` swipe → which submit verb, for the card adapter.

---

## 7 — Concrete change list (when P1–P4 are answered)

1. **Wire**: redefine the `KeypadState=25` payload to `KeypadInputPayload` (§5.4);
   keep 40 B / relayable. Bump protocol version.
2. **`ue_wrap/passwordlock`**: add `CallInputNumber(lock, digit)` (reflected
   `inputNumber`), keep `CallOpen` as the submit verb (rename to `CallSubmit` once P2
   confirms), add focused-state + `Num`/`inPassword` readers; keep `isAcc`/`Key`.
3. **Keypad sync** (its own file per the modular rule — `coop/keypad_sync.{h,cpp}`, NOT
   another generic Channel adapter: keypads are input-edge replication, a different
   shape from the door/light/container *state* poll → forcing it into the toggle
   Channel is what produced the bug). Sender = P1 observer (preferred) or focused-only
   `Num`/`inPassword` delta poll (fallback). Receiver = replay `inputNumber` then
   in-context submit, per-lock echo-suppress + `seq` dedup.
4. **Connect-snapshot**: keypad excluded (§5.5).
5. **Remove** the keypad adapter from `interactable_sync.cpp` (RULE 2 — no parallel
   broken path); the generic toggle Channel keeps doors/lights/containers only.

---

## 8 — IDA addresses + header lines cited

- ProcessEvent (detour point): **`0x141465930`** (`UObject_ProcessEvent`).
- ProcessInternal (BP bytecode dispatch, bypasses PE): **`0x141302DC0`** (invokes
  `Func+216` @ `0x141302e33`).
- `UObject::CallFunction` (BP→BP opcode handler): **`0x1414573C0`**; terminal
  `return ProcessInternal(...)` @ **`0x141457601`**; net-path branch (not taken for
  plain BP fns) @ `0x1414575b6`.
- No native keypad symbols/strings exist in the exe (§2) — the class is pure BP.
- Field/UFunction layout: `CXXHeaderDump/passwordLock.hpp` (isAcc@0x037C:37,
  isDeny@0x037D:38, inPassword@0x0380:39, password@0x0350:32, Num@0x0378:36,
  Active@0x0330:26, door@0x0338:27, Pair@0x0320:24, isFocused@0x0391:41;
  `Open`:78, `inputNumber`:98, `falseEnterEvent`:73, `player_use`:84, `focusOn`:95,
  `playerAnykey`:96, `processKeys`:63, `beep`:67, `isButtonUsed`:59, `open2`:91,
  `SetActive`:68, `Reset`:87).
- Broken shipped code: `src/votv-coop/src/coop/interactable_sync.cpp` (keypad adapter
  :464–484; disabled :496/:607/:614; fail-path `ApplyResolved → CallOpen` :377–392);
  `src/votv-coop/src/ue_wrap/passwordlock.cpp::CallOpen` :98–107.
