# Hands-on runbook -- L5 FPS-hitch fixes LIVE verification (door-open + present-device + arcade late-buy)

**Deployed:** `votv-coop.dll` MD5 `34F6F5658F5E9D32619507C3D5BF1F40` (host + client + copy2 + dev), proto **v86** (no wire change).
**HEAD:** docs `960e4650`; the L5 FIX CODE is UNCOMMITTED working-tree (held until this hands-on):
- `interactable_channel.h` = take-3 fork B (60s staggered full-walk backstop; the 2s interactable full-walk killed).
- `device_screen.cpp` = beta (device/widget classes resolve from `ClassOf` at the interaction edge; the per-2s `FindClass` poll removed -- the device_occupancy hitch half).
- `subsystems.cpp` = ScopedWalkTimer instrumentation (`[WALK-TIME] sync:<name>`). KEEP until this hands-on passes.
- inis (gitignored, deployed) `perf_probe=1` + (for #4) `spawn_menu_unlock=1`.
**Push HELD.**

## What's already PROVEN (autonomous) vs what THIS hands-on adds

**PROVEN autonomously (260s smokes):**
- interactable 2s full-walk KILLED (`[WALK-TIME] sync:interactable` = only the staggered backstops, ~9ms/10s).
- device_occupancy per-2s FindClass walk KILLED (`[WALK-TIME] sync:device_occupancy` n=1 boot then silent; was n=87 x 30ms/2s).
- steady `[HITCH]` (>40ms) host 0 / client 1-stray; net_pump::Tick median 13-16ms (was 30-49ms). The periodic 2s stutter is structurally gone.

**NOT yet verified (the autonomous bot never opens a door / enters a device) -- THIS hands-on:**
- #2 door-open propagation (the interactable migration did not break detection LIVE).
- #3 present-device deny-gate claim still fires (beta classify path gives the same claim).
- #4 arcade LATE-BUY edge-resolve (the path alpha would have broken -- the whole reason we chose beta).

**Boundary:** structural PASS != L5 closed. These 3 live checks close it.

---

## #1 (DO FIRST -- the felt-hitch eyeball, no setup)
Just **play for ~60-90 s** (host and client both), walk around the base. The periodic ~2s stutter you reported for weeks should be **GONE** on both peers. This is the whole point -- if it still hitches every ~2s, STOP and tell me (the structural data says it shouldn't). A rare single stutter (GC) is the accepted cosmetic floor, not the 2s pattern.

---

## #2 door-open propagation (interactable take-3 did not break detect)
1. Host + client in-world (any fresh save).
2. **Host:** walk to any base door, press **E** to open it. Then close it. Open a few different doors.
3. **Client:** watch those doors -- they must **open/close in sync** (host-authoritative; the client renders host state).
4. Also **client:** press E on a door -> it opens on both (client request -> host opens + holds).
- **PASS:** doors propagate both ways, no flicker, no door stuck. (The take-3 backstop means a late-streamed door is detected within 60s; in normal play all doors are present at load.)
- **FAIL / oddity** (a door won't propagate, or vanishes, or a far door never syncs): tell me -- THEN we run the belt-and-suspenders **A/B**: pre-fix-DLL vs take-3 on ONE identical save, to split "this world has N doors" from "the migration missed one." (The in-run full-walk control already says no regression, but the A/B is the live tiebreak if you see weirdness.)

## #3 present-device deny-gate claim (beta classify path works for a resolved device)
The easy present device is the **base laptop** (the "meadow PC") or the **desk coords screen**.
1. Host + client in-world.
2. **One peer** walks to the base laptop / desk and presses **E** to enter the screen.
3. **The other peer** walks to the SAME device and presses E.
- **PASS:** the 2nd peer is **DENIED** (forced out + deny click) -- the device is busy. First peer keeps it. When the first exits, the second can enter.
- **FAIL:** both enter the same screen simultaneously (deny-gate didn't fire) -> the beta classify path broke the resolve. Tell me.

## #4 arcade LATE-BUY edge-resolve (THE beta-critical path -- what alpha would have broken)
This proves a device whose class loads AFTER world-settle (a bought/spawned arcade) still resolves + denies. Fast path via the spawn menu (no economy grind):
1. `spawn_menu_unlock=1` is already set in the deployed host ini `[dev]`. (If you want it off after, set 0.)
2. **Host:** in-world, press **Q** -> VOTV's sandbox prop-spawn menu opens (works in story mode). Find **`prop_arcade`** (arcade cabinet) and spawn one. (This is the LATE-LOAD: `prop_arcade_C` was never resolved at boot -- 7/8 device classes -- and loads NOW, minutes in.)
   - If the spawn menu has no arcade entry, fall back to the drone economy: order an arcade via the laptop shop (slower).
3. **Host:** walk up to the spawned arcade, press **E** to enter (`player.arcade := true`, teleport into the cabinet).
4. **Client:** walk to the SAME arcade, press E.
- **PASS:** the 2nd peer is **DENIED** on the arcade -- proving `prop_arcade_C` resolved at the interaction edge (from the spawned actor's `ClassOf`) even though it never loaded at boot. This is the exact late-buy case alpha (settle-bound poll-stop) would have stranded.
- **FAIL:** both enter the arcade (no deny) -> edge-resolve didn't fire for the late class. Tell me.

---

## What I grep after each (host log + client log)

**#1 felt hitch (sanity, already proven):**
- `[HITCH] frame = N ms` in steady state -- expect ~none >40ms; `[WALK-TIME] sync:device_occupancy` silent; `sync:interactable` only the ~9ms backstops.

**#2 door-open:**
- Host: `door: sent ON key='...'` / `door: host opened+held key='...'` on each open.
- Client: `door: applied ON key='...'` (the mirror applied). Cross-check the `keysHash` host-vs-client matches (cross-peer key stability).
- The take-3 backstop marker `door: backstop full-rescan (60s safety net) -- N live` proves the new scan path is the one running.

**#3 present-device claim:**
- The entering peer: `device_screen: device class '...' resolved at edge (lazy, no walk)` (FIRST entry of that device -- proves the beta edge-resolve fired) + `device_occupancy: HOST claimed '<key>'` / `CLIENT claim '<key>' requested`.
- The denied peer: `device_occupancy: entered '<key>' but slot N holds it -- immediate deny` or `DENIED E at busy device '<key>'`.

**#4 arcade late-buy:**
- `device_screen: device class 'prop_arcade_C' resolved at edge (lazy, no walk)` -- **THE beta proof**: the buyable class resolved at the interaction edge, NOT at boot (there is NO boot-time resolve for it -- the per-2s poll is gone). Also `device_screen: per-instance device 'prop_arcade_C' resolved at enter edge` (the no-bypass guard) if the enter path resolved it.
- `device_occupancy: ... '<arc_posKey>' ...` claim/deny lines (the per-instance arcade key `arc_<x>_<y>_<z>`).

---

## After all 4 PASS
I revert the ScopedWalkTimer instrumentation (subsystems.cpp) + the `perf_probe`/`spawn_menu_unlock` inis, commit take-3 (interactable_channel.h) + beta (device_screen.cpp) as the L5 fix, and you greenlight push. If any oddity -> I diagnose first (no blind fix).
