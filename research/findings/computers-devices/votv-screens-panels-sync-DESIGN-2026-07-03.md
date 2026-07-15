# Screens & panels sync — the design (2026-07-03)

User ask: "sync EVERY screen and panel (host authoritative), minimal latency, no FPS cost —
like the light switches? intent or request? decide per rule 1." Research agent report
(code+dump+MTA verified this session) distilled here. NO code shipped for this yet — this doc
IS the design; the gap list below is the build plan.

## The corrected premise

Light switches are NOT intent/request. The shipped shape (`coop/interactables/
interactable_channel.h` Channel + `interactable_sync.cpp` adapters) is **poll-delta-broadcast**:
the presser's game applies natively (zero latency), every peer polls the state field per tick
(bool read per indexed instance, no alloc), a delta broadcasts `KeyedTogglePayload` (Key +
resulting state), receivers replay the game's own `use()` reflected (idempotent, echo-impossible
via lastKnown prime), late joiners get a full connect-snapshot (ON and OFF). WHY polling: the
whole device layer (`use`/`player_use`/`setActiveInterface`/every screen verb) dispatches
`EX_LocalVirtualFunction` — invisible to ProcessEvent AND Func hooks (IDA-proven 2026-06-04) —
and polling catches EVERY writer (player, NPC, power scripts) uniformly.

Intent→host exists too, but only where it is NEEDED: doors (auto-revert would oscillate a
symmetric poll; client PRE-clears the native gate, sends DoorOpenRequest, host applies honoring
CanOpen guards, broadcasts DoorState) and shop orders (host re-commits `makeAnOrder` natively).

## The four proven shapes (pick PER DEVICE, never one-size)

1. **Symmetric poll-delta** (lights/containers/appliances/keypads/power breakers): state inert
   once set + locally applying is valid game logic. Zero local latency.
2. **Intent → host executes natively → broadcasts resulting state** (doors/orders): device
   auto-reverts, is host-simulated, or execution has once-only world side effects. One RTT.
3. **Host-auth roller** (weather/sky-signals): host-owned simulation, per-peer RNG killed.
4. **Claim + owner-stream** (DeviceClaim=51 + DeskState/DishAim): enterable screens — one
   occupant (host-arbitrated first-wins), the occupant streams 1-3 Hz while claimed, adopt
   snapshot at join, release snapshot for the next enterer. Single-simulator latch where a
   ticker has world side effects (CompState/refiner precedent — completion spawns theEvil).

MTA mapping verified (reference/mtasa-blue): garages = server-executes+broadcasts+join-array
(CMapInfoPacket) == our shape 1+connect-snapshot; element-data trust gate == our host relay;
CObjectSync one-elected-syncer == our claim-owner streams. MTA's verdict: panel semantic state
is discrete and belongs to the server; delegate syncing only for noisy continuous streams.

## Already shipped (v63-71 — do NOT rebuild)

DeviceClaim occupancy for ALL 8 enterable devices (device_occupancy.cpp; optimistic entry,
loser force-exit, deny sound); desk scalars DeskState=54 + DeskLogLine=65; dish aim
DishAimState=55; sky signals 52/53 (host-roller + full consume replay); refiner CompState=60/
CompData=61 (single-simulator); saved signals 58/59; emails 56/57; orders 39; points; sleep;
power breakers PowerControlState=36; keypads 25; doors 9; lights 10; containers/garage/
appliances/lockers 11/33/35/50; ATV 37; drone 38; turbine 49; dirt 30/31. Join-side: the v56
save-transfer already carries analogPanelsData, emails, tasks, saved signals, orders, Points.

## Shared vs per-player (principle 6)

- SHARED (sync): all 8 enterable devices — five families literally render ONE shared widget
  instance (ui_consolesAtlas / ui_console / ui_radar / ui_reactor / gamemode.laptop), so
  per-player sessions are impossible without editing assets (A6); occupancy IS the per-player
  routing. Breakers/reactor/generator/transformer outcomes = shared power, unambiguous.
- PER-PLAYER (never sync): screen-space UIs (clipboard, inventory, draw paper, texture picker),
  ATM session (its Points effect already syncs), in-progress typed Command line, minigame
  in-progress internals (transformer sine/rotators, arcade, fuse puzzle) — outcomes only.

## THE GAP LIST (the build plan, priority order)

1. **Reactor rod state** (Apanel_reactor_C / ui_reactor — shared base power; RE flag U7):
   claim exists; add a claim-owner keyed stream + adopt snapshot next to power_sync. PROBE
   AUTONOMY FIRST (does the reactor tick fight a symmetric write?) to pick shape 1 vs 4.
2. **Generator Agenerator_C** (IsBroken/opened/upgradeLevel) + **transformer minigame OUTCOME
   edge** (is*Complete @0x0360-62; puzzle internals stay occupant-local) — keyed state module.
3. **SAT console LogText release-snapshot** (+ optional live tail) — Apanel_SATconsole_C
   LogText @0x02E0 (whole scrollback; all consoles share ONE ui_console_C); chunk via the
   existing blob lane. Designed in the base-computers RE §5.3, never shipped.
4. **TV Aprop_tv2_C** — keyed state (powered/Channel/PlayMode/volume/brightness/URL; replay
   useChannel/openLink reflected). Playback-position divergence acceptable v1; media/stream
   CONTENT sync = its own future feature (also covers radios — radio on/off already rides
   ItemActivate=12).
5. **Laptop tasks live roll** (U6): host-roller + Task/taskNew struct mirror (today join-only).
6. **serverBox minigame monitors**: needs its own RE pass before design (in-world widget
   interaction, not enterable).
7. Re-affirmed claim-only forever: radar view (derives from synced world), arcade, fuse/sine
   minigame internals, typed command lines.

Every item fits an existing shape + module template (index+poll+snapshot+OnReliable next to
keypad_sync/power_sync, or claim-owner stream next to the desk/dish modules). No new
architecture, no new hook seams. Wire cost: edge-only or 1-3 Hz claim-gated — the "minimal
latency, no FPS" ask is structurally satisfied by the shapes.

---

## 2026-07-15 UPDATE — cursor SHIPPED + a measured DIVERGENCE CLUSTER (one root)

**SHIPPED (gap-list-adjacent):** the desk coords-panel LIVE cursor is now a proper unreliable pose stream
(`MsgType::DeskCursorPose=36`, `coop/interactables/desk_cursor_sync`, 50ms LerpWindow interp,
`console_desk::WriteCursorOnly`), commit `60bedde6`, kProtoVer 108->109. User TAKE = SMOOTH. The reliable
`DishAimState` is now committed-coords-only + connect-snapshotted.

**MEASURED CLUSTER (next session — the real gap):** user stress-testing exposed that MOST desk/console/
computer OUTPUT is generated PER-PEER, not holder/host-owned + mirrored. **ONE root**, five symptoms:

1. **coordLog** — `console_state_sync.cpp:481` `ProduceLogLines` runs on **EVERY peer**, and
   `IsAnimatedLogLine` (:327) FILTERS `CR:[` (+CDOWN/AREASCAN/APPROX/ANALYSIS) off the wire (assumed to
   "regenerate per peer" — FALSE for CR: needs a real drag). After the v109 cursor fix the host's mirrored
   cursor MOVES → its BP appends LOCAL `MOVE_*` lines → host shipped 78 vs client 13. Fix: gate
   `ProduceLogLines` to the HOLDER, un-filter `CR:`, SUPPRESS non-holder local log generation.
2. **detector/decode %** (`017.77` vs `017.84`, energy consumption, downloaded) — the download/decode
   machine simulates per-peer → drifts. Host-authoritative sim + mirror ALL scalars.
3. **frequency / polarity** — the freq/polarity filter values + detector-status params are on NO wire lane.
4. **dishes** — aim/slew computed per-peer; diverge initially + more after a catch moves them.
5. **stationary PC** (`Alaptop_C`/portable-PC) — power (system-block button) + screen not mirrored.

**Fix approach (design, /qf it):** the shape-4 (claim/host-owner-stream) done PROPERLY — the holder/host
owns the simulation + the log; non-holders MIRROR the full state and SUPPRESS local generation. See
`[[project-desk-console-sync-2026-07-15]]` + `[[lesson-smooth-mirror-wakes-dormant-per-peer-generation]]`.

**⚠ Also open:** a CLIENT CRASH exiting to main menu (world-teardown; not desk_cursor-attributed) + the
cursor sync DEGRADING to ~5fps mid-session. Logs: `scratchpad/desk_divergence_crash_{HOST,CLIENT_1}_0715.log`.
