# v115 — desk audio-effect mirror (DeskSndFx) + cursor stream v2 (2026-07-17)

Design of record for the v115 build. /qf auto-loop: 6 rounds, critic "that holds" at R6;
thread archived in the session scratchpad (`qf_thread_desk_audio.md`). All bytecode cites
= `research/bp_reflection/*` disasm/JSON this session; runtime cites = the user's live
v114 hands-on logs (2026-07-17 11:0x).

## The reports (user live-test, 6)

1. Observer hears NO keyboard click when the other peer types at the desk.
2. Observer hears NO cursor-movement loop sound.
3. Observer hears NO cooldown-denied beep (1/2/3/SHIFT on cooldown).
4. Observer hears NO dot-placed beep (1/2/3 ok).
5. The mirror cursor at times loses smoothness (jerks).
6. Momentum: the presser flicks the cursor + dismounts; the presser sees the glide
   continue; the observer's cursor freezes at release -> rest positions diverge.

## Root cause A (sounds): presser-local effects never cross

The whole unit-1 sound surface is played by presser-LOCAL BP paths [MEASURED disasm]:

- `ui_consolesAtlas.OnKeyDown` (PE-visible widget input, occupant machine only): gates
  `controllingCoordinatePanel -> bIsFocusable -> coord_isPing (swallow) -> active_coords`,
  then `audio_coordKeyPress.Play(0)` ALWAYS; `isCoordinateWorking(-1)` (plays
  `audio_coordFail` inside when a radar is broken); per-verb `playButtonSound(buttonlong4/
  buttonmetallic2/buttonlong5/buttonquick1)`; outcome beeps INSIDE `movePointToCursor`
  (`beepLong1` ok / `beep4` cooldown) and `useSearch` @83010 (same pair).
- `OnKeyUp`: gate `active_coords` ONLY; `audio_coordKeyPress.Play(0)` ALWAYS.
- 7 screen-button `ComponentBoundEvent`s on the desk actor = a SECOND entry surface to
  the same verbs (bypasses OnKeyDown; plays `buttonquick1` + the movePointToCursor beeps).
- `corrds_loop` (cursor-move loop): spaceRenderer uber @2045-2473, edge-guarded
  `corrds_loop.SetActive(OR(move_*) && soundActive, true)` -- keyed to HELD KEYS, not
  motion (the momentum glide is silent natively).
- `audio_coord_pingLoop`: `Activate(true)` in OnKeyDown; `SetActive(false,false)` in the
  presser-only ping FSM.

`playButtonSound`/`playPingSound` are EX_Context + **EX_LocalVirtualFunction** — script
targets, PE- AND Func-INVISIBLE (the lesson class). Their BODIES are
`channel.SetSound(cue)` (EX_FinalFunction) + `channel.Play(0)` (**EX_VirtualFunction**).

**The seam [MEASURED, exhaustive]:** a structural opcode census over ALL 286 dumped
assets resolved EVERY call site touching the 6 comps: every audible verb dispatches
**EX_VirtualFunction on a NATIVE target** (`AudioComponent:Play`,
`ActorComponent:SetActive`, `ActorComponent:Activate`). Native-target dispatch funnels
through `UFunction->Func` regardless of caller opcode (dispatch-visibility doc line 198;
K2_DestroyActor Func-patch precedent, runtime-measured 2026-07-14) -> **the Func-patch
catches the WHOLE class at one seam, zero BP-gate duplication**. Coverage invariant =
TARGET-NATIVENESS, not per-site opcode. Bonus census find: `ui_laptop` plays its OWN
same-named `laptop.audio_coordKeyPress` -- excluded for free by the pointer whitelist;
the laptop is a future extension of this lane.

## Root cause B (cursor): claim-coupled stream + fixed-window interp

- Momentum: the glide integrator (spaceRenderer uber @518-970) is UNGATED:
  `movement := Vector2DInterpTo(movement, dir*vel, dt, coordinateDrift=0.5)`;
  `setCoordinateLocation(cursor + movement)` -- runs after dismount until convergence.
  `coordinateDrift=0.5` [CDO] -> e-fold 2 s; a max flick reaches sub-visible speed in
  ~12.4 s. The v109 sender streamed only while `weHold` -> the tail was lost.
- Jerks: two code-measured mechanisms: (a) claim release/flap -> interp Reset + snap
  (the receiver was claim-gated; the live log shows 3 releases/14 s during the user's
  deliberate remount testing -- flap pathology UNPROVEN, instrumented with a WARN);
  (b) a sender frame with no new position still sends (same pos, new seq) -> the fixed
  33 ms window reopened per packet; at sender-fps dips this staircases.

## The build (proto 114 -> 115)

### A. DeskSndFx lane
- `ue_wrap/desk_audio.{h,cpp}` (NEW): the 6 comp ObjectProperty offsets (frozen wire
  order = protocol.h `DeskSndComp`), `AudioComponent::Sound` offset,
  `ActorComponent::bIsActive` (FindBoolProperty bitfield), the 4 UFunctions; instance
  comp-pointer cache refreshed ONLY from ticks (the detour hot path never walks;
  failure-backoff 1/s); `ReadCueName` (Sound -> FName, ASCII-only), `ReadLoopActive`
  (ground truth), `ReplayPlay` (SetSound+Play, per-name cue cache), `ReplaySetActive`
  (static mapping: ON->(true,true) [all native ON sites reset], OFF->(false,false)
  [engine ignores bReset on deactivate]).
- `coop/interactables/desk_snd_fx.{h,cpp}` (NEW): 3 Func-patch detours ->
  {flag check, <=6 ptr compares, ring push}; 16-slot GT ring drained by Tick -> the
  relayed `ReliableKind::DeskSndFx=105` (44 B {op,comp,cueLen,cue[40]}); receiver
  replays under the `ScopedWireApply` echo guard (our replay dispatch also funnels
  through ->Func); `g_armed` gate (no SP/menu sounds queue across a session start);
  fires/sec counters logged /60 s (the permanent-seam evidence gate, qf R1); loop
  bookkeeping: `g_wireLoopOn` (OnDisconnect force-off), HOST-only `g_loopFrom`
  attribution (leaver teardown = host broadcasts the OFF, qf R3), `g_pendingLoop`
  park+retry while the desk is unresolved (qf R3); host join re-assert from COMPONENT
  ground truth at ConnectReplayForSlot (covers host-authored loops, qf R5); ini-gated
  `[dev] desk_snd_selftest=1` e2e self-test (host fires one organic dispatch; the
  client log must show the apply -- the wire-dead-kind smoke killer, qf R5).
- Echo-guard coverage: desk_input `ApplyField`/scan replay/coordIsPing clears, the
  cursor mirror's `WriteCursorOnly` + `CallIntComsUnfocused`, all desk_snd_fx replays.
- RULE-2 retirements: `PlayScanEffects` keeps ONLY the spawnDirs visual (beep rides the
  fx lane -- measured through `useSearch @79696 -> playPingSound -> pingSound.Play`);
  `PlayPingSuccess` REMOVED from console_desk + its signal_catch_sync call site
  (measured @10027 -> same channel); dead cue resolves removed.

### B. desk_cursor_sync v2 (same datagram)
- Sender: streams while `weHold`, AND THROUGH release while the glide still moves the
  cursor (settle |dPos| < 0.25 px over 500 ms [residual < ~1 px]; cap 15 s [> the
  12.4 s worst-case decay, qf R2]; cut early when another peer claims).
- Receiver: DECOUPLED from the claim -- applies whichever slot streams (holder, else
  the last holder's tail) while samples are fresh (<700 ms); the release edge keeps the
  native `intComs_unfocused` dim replay but does NOT reset the interp; reset only at
  stream idle. `SR::ZeroMovement()` at the stream-START edge kills a residual local
  glide (the ungated integrator would co-write, qf R5). Claim-flap WARN
  (release+reclaim < 2 s) attributes any real occupancy flicker.
- Interp: identical-target dedupe (no window reopen on same-pos packets) + adaptive
  window = clamp(1.25 x EMA(position-change inter-arrival), 25, 80) ms.

## Residuals (named)
- R1: `PlaySound2D(buttonshort14)` (satellites-active error) is an EX_CallMath static
  -- not forwarded (a GameplayStatics-wide Func-patch for one rare error beep declined).
- R2: the ping FSM's stage sounds/rings remain presser-local (the pre-existing
  ping-visual axis miss; the pingLoop itself now mirrors).
- R3: laptop keyboard clicks = the same class on the laptop's own comps; future
  whitelist extension.
- R4: an observer's own mute setting is honored per-comp playback but a forwarded loop
  reflects the PRESSER's soundActive state at the edge.

## Verification state (as-built, 2026-07-17 s19)
- BUILT commit `c5ff11a4`, proto 115, DLL `e130383f11c8dee1` x4 (hash-verified).
- Perf audit: PASS, 0 CRITICAL (function-by-function table; both WARNs fixed pre-commit:
  desk_audio resolve-incomplete log-once + the cue NEGATIVE cache; console_desk.cpp 1005 LOC
  flagged >800 with the console_atlas extraction proposal -- REDUCED from 1030 by the
  retirements; event_dispatch_state.cpp 773, nearing).
- Correctness audit: 0 CRITICAL; 1 WARN fixed pre-commit (the flap WARN fired on an ordinary
  X->Y desk handoff -- now discriminates on the RELEASED slot); 1 NIT fixed (stale "spawnDirs
  + beep" log text).
- Smoke: PASS x2 (30 s + 75 s; RSS stable ~3.2 GB both peers). Seam counters measured live:
  Play=1169 SetActive=22012 Activate=559 per 60 s game-wide, deskHits = the selftest only ->
  the detour cost bound holds empirically. E2E self-test PROVEN: the first run's +5 s dispatch
  was dropped at the client's still-unresolved desk (world-load races the probe -- fixed to
  +20 s from the connected edge); run 2: host `SELFTEST organic dispatch fired` 12:39:49 ->
  client `desk_snd: applied op=0 comp=3 cue='newdesk_beep4' from slot 0` the same second.
  Zero WARN/ERROR from the new lanes in either log. `[dev] desk_snd_selftest` left OFF.
- NOT hands-on: v115 stacks as the FOURTH unverified proto layer (v112+v113+v114+v115);
  runbook `research/handson_runbook_2026-07-17_desk_v114.md` take 2 carries the v115 steps.
