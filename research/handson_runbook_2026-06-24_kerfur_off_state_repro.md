# Hands-on runbook -- KERFUR off-state non-replication REPRO (host turn-off -> client nothing)

**Deployed:** `votv-coop.dll` MD5 `34F6F5658F5E9D32619507C3D5BF1F40` (host + client), proto v86.
**Goal:** ONE host+client log of the repro so we can localize the break (the convert path is logged at
every step -- the first MISSING line in the chain is the break). NO fix until the log names the step.
**You launch** (`mp_host_game.bat` + `mp_client_connect.bat`). Claude greps after. Claude does NOT launch.

## The repro (minimal)

1. **Host:** `mp_host_game.bat`. Fresh save / recent fresh save, get in-world. Make sure there is at least
   one **kerfur** (active NPC) in the base. If none, turn one ON first (or buy/spawn one).
2. **Client:** `mp_client_connect.bat`. Let it fully join (world loaded, host puppet visible).
3. **CONTROL (the working path):** with the client watching, **host turns a kerfur ON** (if you have an off
   one) OR confirm an existing active kerfur is **visible on the client**. This is the path that works --
   confirms the NPC channel is fine and the client is receiving.
4. **THE BUG:** **host walks to an ACTIVE kerfur, opens its radial menu, picks `turn_off`.** The host now
   sees the off **kerfur object** (prop). 
5. **Client:** look where that kerfur was. **Expected bug: nothing there (no off object).**
6. (Optional 2nd data point) **host turns it back ON** -> does the client see the NPC reappear? (turn-on is
   a different converge branch; tells us if only turn_off is broken or both.)

Do this with a **normal base kerfur** (NOT in the flesh room -- the flesh-room off-prop spawns at Z=20000
by design and is a separate case we've already accounted for).

## What Claude greps after (host log + client log) -- the decision tree

The break is the FIRST missing line in this chain (host turn_off):

**HOST log:**
1. `kerfur_convert: POLL turn_off (kerfur NPC eid=N died invisibly) -> host broadcasts destroy+prop`
   -- the poll detected the conversion. **MISSING -> break #1** (poll never saw it: the host kerfur was
   not tracked as a live `Npc` mirror / not cached in g_kerfurWatch).
2. then EITHER:
   - `kerfur_convert: turn_off converge -- no new kerfur prop near (x,y,z) ... (no broadcast)` -- **break
     #2** (converge could not find the spawned prop within 5 m -> no broadcast -> client gets nothing), OR
   - `kerfur_entity: BindFormActor K=.. ... -> newEid=.. (KerfurConvert broadcast)` -- the host DID
     broadcast. Good -> the break is downstream.

**CLIENT log:**
3. `kerfur_convert[client]: applied KerfurConvert K=.. ->prop(turn_off) oldEid=.. -> newEid=.. class='..'`
   -- the client received + applied. **MISSING while the host broadcast -> break #3** (wire/routing/slot-0
   gate dropped it). **PRESENT but still no object visible -> break #4** (materialize: the synthetic
   PropSpawn `OnSpawn` fresh-spawn failed, or `cannot resolve class '...'`).

Paste both logs (or just say "done" and Claude greps the deployed log files). Each break has a different
fix shape -- we pick it only AFTER the log names the step.

## Note
The convert path is fully built + looks correct statically (docs/kerfur/01-...), so this is most likely a
subtle runtime gap (poll caching / converge timing) -- the log disambiguates. perf_probe/pile inis being on
does not affect this test.
