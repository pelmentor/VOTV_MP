# alarm (`trigger_alarm_C`) — the base emergency alarm: klaxon + red rotating lamps   (STATUS: AS-BUILT, e2e pending)

The base-wide alarm the radar fires when it scans an IMPORTANT object (the arir-crate /
signal-object class of scares): looping klaxon, every `alarmLamp_C` beacon on, ceiling
lamps flicker, the basement grate toggles, and the native activeEvents registry counts it
(no-save-during-event gate + tension). The player walks to the radar panel and presses
"b/Stop alarm".

User rule anchor (2026-07-05): «alarm это по сути тоже своего рода ивент, которому нужна
во время new peer join обработка» — this doc + the `alarm_sync` lane are that pass.

## 1. Native behavior (ground truth — all [bytecode] from `trigger_alarm.json` /
##    `panel_radar.json` / `analogDScreenTest.json` / `alarmLamp.json`, disasm 2026-07-05)

- **The actor**: ONE `trigger_alarm_C : AtriggerBase_C` placed in the map, registered
  under gamemode key `alarmTrigger` (both callers resolve it via
  `gamemode.getObjectFromKey(n'alarmTrigger')`). Fields: `active` (bool),
  `alarms` (TArray<`alarmLamp_C`*>, rebuilt by `processKeys` from keyed objects),
  `Audio1` (the looping klaxon AudioComponent).
- **ON path**: `analogDScreenTest` (the base computer screens actor — it is the LIVE
  screens class, not a test asset) radar sweep loop: for each scanned
  `gamemode.radarObjects` entry whose `comp_radarPoint.important == true` →
  `alarmTrigger->runTrigger(radar, 1)` [uber @1246-1250]. The radar ALARM module upgrade
  (`panel_radar.updModules` → `screens.radar_hasAlarm = Array_Contains(upgrades, ...)`)
  gates the feature [gating of the runTrigger path itself: ? — the hasAlarm AND is
  proven on the beep branch @1194; the important→runTrigger branch's upstream gate not
  fully traced].
- **Separate beep axis**: `radar_scanned.Num > radar_prevEnt && radar_hasAlarm &&
  radar_soundActiveAlarm` → one-shot 3D `arirCrateAlarm_s` at the computer [@1194-1198].
  NOT the alarm — a per-viewer local sound.
- **OFF path**: `panel_radar` interaction option "b/Stop alarm" → `runTrigger(self, 0)`
  [panel_radar uber @150-268].
- **`runTrigger(owner, index)`** [uber @939→]: IDEMPOTENT — `IntToBool(index) == active`
  → return, no side effects. On a real toggle: `active = index`; basementGrate prop →
  `setPropProps(false, false, !active, false)`; `lib_C::setEvent(active, false, self)`
  → the native activeEvents refcount (docs/COOP_EVENT_JOIN.md registry); each
  `alarms[i]->setActive(active)`; `Audio1->SetActive(active, reset=true)` (klaxon);
  if active → every `ceilingLamp_C` → `solar()` (emergency flicker).
- **`alarmLamp_C::setActive(v)`**: glow sprites + point light visibility + emissive
  material swap (`inst_alamp2_on/off`); its tick spins the `speen` beacon 360°/s from a
  RANDOM initial phase — beacon rotation is per-instance cosmetic noise natively.
- `trigger_eventt_arirShip` drives its OWN `alarms[]` (the arirShip event) — separate
  event, separate future pass; NOT this lane.

## 2. Sync-axis table

| axis | class | carried by |
|---|---|---|
| alarm ACTIVE state (klaxon + all lamps + grate + ceiling flicker + activeEvents count) | shared world state; either peer's radar/press toggles it | **`alarm_sync` lane** (1 Hz state poll + `ReliableKind::AlarmState`); each peer replays `runTrigger` natively — ONE reflected call reproduces the whole fanout |
| radar new-entity beep (`arirCrateAlarm_s`) | per-viewer (each peer's own scan cadence) | none — local by design |
| lamp beacon rotation phase | per-viewer cosmetic (random phase natively) | none — native randomness |
| `radarObjects` contents (what the radar can see at all) | derived world entities | already owned by the entity mirror lanes; not this doc |

## 3. Coop design (`src/votv-coop/src/coop/world/alarm_sync.{h,cpp}`)

**Dispatch fact that shapes everything** (docs/COOP_DISPATCH_VISIBILITY.md): both callers
invoke `runTrigger` as `EX_VirtualFunction` — INVISIBLE to ProcessEvent, same as every
screen/panel verb (visibility map line "every screen/panel device verb → POLL the state
field"). So NO hook: the lane POLLS `trigger_alarm_C.active` (FindBoolProperty real-bit,
cached instance revalidated IsLiveByIndex) at 1 Hz on BOTH peers.

- Poll sees a change that the lane itself did not apply (`g_expected`):
  - HOST → broadcast `AlarmState{active}` to all.
  - CLIENT → send `AlarmState{active}` to the host (its own scan fired ON early, or its
    player pressed Stop). The host applies natively; the host's own poll then detects the
    change and does the one canonical broadcast fanout.
- Receive `AlarmState` → apply = reflected `runTrigger(nullptr, active?1:0)` on the local
  trigger instance (ProcessEvent directly — WE dispatch it, so BP-internal invisibility
  is irrelevant), then latch `g_expected`. The bytecode's own idempotency check makes
  redundant applies free and breaks every echo loop.
- Both peers replay natively → lamps/klaxon/grate/ceiling/`setEvent` all converge,
  including the client's OWN activeEvents refcount (no-save gate + tension behave).
- **Late-join answer (COOP_EVENT_JOIN.md 3.4 row)**: host
  `QueueConnectBroadcastForSlot(slot)` sends the CURRENT state unconditionally at the
  world-ready edge — a mid-alarm joiner starts its klaxon on arrival; idempotent if the
  transferred save already carried it.
- **EventSnapshot dedup**: `trigger_alarm_C` is LANE-OWNED — `event_active_sync::
  SendJoinSnapshotForSlot` skips it with an INFO line instead of shipping an unmapped-row
  WARN to every mid-alarm joiner (the WARN stays meaningful as the Phase-2b fill signal
  for genuinely uncovered classes).
- Wire: `ReliableKind::AlarmState = 87`, `AlarmStatePayload{u8 active}` (4 B padded),
  protocol v101. Family: event_dispatch_world.

MTA precedent: shared-world toggles (doors/lights) sync symmetrically with server relay —
`CClientDoor`-shape state channel, not RPC replay; deliberately NOT host-suppressing the
client's radar scan (both scans read mirrored entities and converge; the lane reconciles).

## 4. Caveats / known quirks

- The ON moment can differ across peers by up to one radar sweep period (each peer's
  sweep angle is local) — the lane converges it within ~1 s of the first peer's edge.
- `ceilingLamp.solar()` only fires on the ON edge natively; whatever its recovery
  behavior is, it is identical SP behavior on every peer (replayed, not synthesized).
- The save persists trigger state via keys — a joiner's transferred save can arrive
  already-ON; the unconditional join send + idempotent apply absorb both orders.
- Autonomy CANNOT verify the audible/visual fanout (klaxon, lamp glow) — smoke asserts
  state + log lines only; hands-on stamps VERIFIED (the geometry-blind e2e lesson:
  [[lesson-e2e-assert-must-discriminate-the-axis]] — the assert here is the *state bit +
  applied log on the receiving peer*, which DOES discriminate this axis).
- Known narrow race (audit 2026-07-05, below reporting bar, recorded for honesty): two
  peers producing OPPOSITE local transitions inside the same ~1 s host poll window can
  net-cancel on the host (its field returns to the pre-round value → no broadcast),
  stranding the losing peer's state until the next real transition. Requires two peers
  toggling the alarm in opposite directions within one second — accept until seen live;
  the fix, if ever needed, is a host reconcile-echo after every client request.

## 5. Verification

- Static RE: complete (section 1; no runtime probes needed — every seam bytecode-proven).
- AS-BUILT: lane + wire + join answer shipped (see COOP_SYNC_MAP.md row for commit).
- PENDING: autonomous e2e (host dev-force `runTrigger(1)` → client log asserts
  `alarm_sync: applied active=1` + client `active` bit reads 1 → stop → both read 0);
  then hands-on for the audible/visual axes + a TRUE mid-alarm join.
