# bug2 RE — intermittent unpossessed first-client mainPlayer_C (2026-05-30)

Status: **root CHARACTERIZED, exact faulting site NOT yet pinned** (could not
reproduce in 8 consecutive 4-peer smokes this session). A permanent firewall
"absorbed-fault localization" diagnostic is now shipped to pin it deterministically
the next time it trips. This doc is the standing analysis so the fix is immediate
once an IP is captured.

## Symptom (observed in the Inc5 run-1 4-peer smoke, CLIENT1 = folder _copy)
- `net stats: state=2 sent=1 recv=3401 puppet=0` on the FIRST-connected client only.
  recv high (net thread healthy) but `sent` stuck at 1 (never sends poses) and
  `puppet=0` (spawns no puppets).
- ~4046 `game_thread: posted task raised an exception (...absorbed...AV...)` lines,
  ~0.72 per net_pump::Tick at 125 Hz → roughly ONE absorbed AV per tick.
- RSS balloons to ~9.3 GB (despite the bug1 ParamFrame 64 KiB bound → a DIFFERENT
  leak path than bug1's ApplyToEngine ParamFrame).
- Intermittent: hit ~1/4 in the Inc5 batch (cold first smoke after a fresh build),
  then 0/8 in back-to-back warm repros this session. Likely correlates with COLD
  level-load contention widening the possession-race window on the fastest client.

## The gate-chain (HIGH confidence, 4 converging RE agents + direct trace)
1. The first client's LOCAL `mainPlayer_C` ends up UNPOSSESSED (`GetController()==null`).
2. `players::Registry::RescanLocal()` (players_registry.cpp:54-71) discriminates the
   local by `GetController(obj) != null` — an unpossessed local is INVISIBLE to it →
   `Registry::Local()` returns null every tick.
3. `net_pump::Tick` (net_pump.cpp:298-309): `if (!g_netLocal) g_netLocal = Local();`
   stays null → the ENTIRE pose-send block (298-397: ReadLocalPose, SetLocalPose,
   grab-state) is SKIPPED → `session.hasLocal_` stays false → the NetThread send
   fan-out (`if (have || haveProp)`) never fires → **`sent` stuck at 1**. ✓
4. The per-tick AV aborts `Tick` somewhere BEFORE the puppet-spawn loop (399-468),
   which is why `puppet=0` despite recv>0, and the partial allocation per absorbed
   fault leaks → balloon.

## The per-tick AV source — AMBIGUOUS (the part the diagnostic must settle)
Three candidate derefs, all plausible, NOT yet distinguished without the IP:
- **(A) `ue_wrap::puppet::GetSkeletalMeshComponent` (puppet.cpp:~99)** uses plain
  `R::IsLive(puppetActor)`, NOT `IsLiveByIndex` — a GC-recycled puppet passes the
  recycling hole the bug1 fix closed only for `RemotePlayer::valid()`.
- **(B) `sSetRelRotFn` (remote_player.cpp:513)** is a function-local `static void*`
  resolved once and NEVER re-validated → a GC'd/stale UFunction* drives a ParamFrame
  on garbage metadata.
- **(C) `g_netLocal` (net_pump.cpp:298)** is cleared via plain `R::IsLive(g_netLocal)`,
  NOT `IsLiveByIndex` — if the local pawn is GC-recycled it passes the recycling hole,
  is never cleared, and `GetController(g_netLocal)` derefs a foreign object every tick.
All three VIOLATE the bug1 recycling-hole principle (remote_player.h:140-149); the
captured IP says which one (or a game-exe ProcessEvent hit = a UFunction on a bad object).

## The UPSTREAM root (why unpossessed) — hypothesis, needs IDA/repro
RemotePlayer::Spawn() (remote_player.cpp:62-65) falls back to `FindObjectByClass(
mainPlayer_C)` when `Local()` is null (pre-possession window). On the first client,
if a puppet spawn's orphan BeginPlay runs VOTV's `intComs_gamemodeBeginPlay` BP, and
that BP calls a native `Possess` (which `UnPossess`es the current pawn first), the
local could be left unpossessed for the session. puppet.cpp:283-332 only restores the
`gamemode.mainPlayer` POINTER, NOT any PlayerController possession state. OPEN QUESTION
(needs IDA decompile of the BP-VM dispatch): does that BP actually call native Possess,
or only write the field? The inertPawn field-zeroing (engine.cpp:263-278) is per-spawned-
instance (NOT the CDO), so it does not clobber the local via shared state — ruling out
the CDO-mutation theory (also ruled out by intermittency).

## The diagnostic now shipped (permanent firewall upgrade)
`ue_wrap/game_thread.cpp` Pump() now wraps each task in `RunTaskSEH` whose SEH filter
captures `EXCEPTION_RECORD::ExceptionAddress` + the AV access address, and logs
`posted task FAULT code=0x... ip=0x... [module+0xRVA modbase=0x...] access=0x...`.
- Under /EHa the __except unwind still runs the task's C++ destructors → the
  load-bearing lock-release property is preserved.
- `/MAP` (CMakeLists) emits `build/votv-coop/Release/votv-coop.map`.
- `tools/maprva.py 0x<RVA>` maps the logged RVA → exact function+offset.

## Next-session protocol when bug2 trips the trap
1. Grep the failing client log for `posted task FAULT`. Take the RVA (the `+0x..`
   after the module name) and run `python tools/maprva.py 0x<RVA>`.
2. If module = votv-coop.dll → the function is one of (A)/(B)/(C) above (or another
   site) → apply IsLiveByIndex / re-validate at THAT exact deref (ONE evidence-backed
   change). The `access=` address confirms null vs. recycled-garbage.
3. If module = VotV-Win64-Shipping.exe → the fault is inside a ProcessEvent-dispatched
   UFunction on a bad object → harden the CALLER that passed the bad self/fn.
4. Separately, if the upstream possession race is in scope, IDA-decompile
   `intComs_gamemodeBeginPlay` to confirm/deny the native-Possess hypothesis.

## Why not fix speculatively now
RULE 1 / deep-RE discipline: fixing (A)/(B)/(C) without the IP confirming the site is
the iterative-shenanigans anti-pattern. They are genuine recycling-hole residuals worth
hardening, but the captured IP tells us WHICH is the live fault — fix that one with
evidence rather than shotgunning three guesses. The trap is set; the fix waits for proof.
