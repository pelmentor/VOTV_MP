--[[
  coopSpawnProbe -- Phase 2.1 "spawn the orphan" derisk experiment.

  Goal: answer the cheapest, highest-value question in the project --
  can a SECOND AmainPlayer_C exist in the world and tick without crashing?
  This is the UE form of the methodology's "spawn the orphan" (Phase 2.1).

  IMPORTANT -- this is NOT split-screen and NOT UGameplayStatics::CreatePlayer.
  It spawns the pawn directly via UWorld:SpawnActor on OUR path (the same
  primitive the shipping coop::RemotePlayer will use). The spawned pawn is
  unpossessed (no ULocalPlayer, no second viewport). We only want to know
  if the engine + VOTV's single-player assumptions tolerate a 2nd pawn.

  This is a throwaway EXPERIMENT (UE4SS v3.0.1 Lua). It will be superseded
  by the C++ orphan in src/votv-coop once the lesson is learned (RULE No.2).

  Keybinds (all with CTRL):
    CTRL+P  -> spawn the orphan near the local player, then auto-report
               its alive/position state every 5s for 60s (the gate window)
    CTRL+O  -> report orphan state once, on demand
    CTRL+K  -> destroy the orphan

  What to watch tomorrow:
    - Does the orphan spawn (valid actor)?  [SpawnActor result]
    - Does the game survive its BeginPlay?  [VOTV BeginPlay may assume it
      is THE player -- GetPlayerController(0), camera setup, registering
      with mainGamemode singletons. A native crash here is EXPECTED data,
      not failure -- it tells us the first per-site fix (principle 4).]
    - Does it stay alive 60s while you move around?  [the Phase 2 gate]
  If it crashes: grab UE4SS.log + the crash dump (CrashDump.EnableDumping=1).
]]

local UEHelpers = require("UEHelpers")

local TAG = "[coop-probe]"
OrphanPawn = nil   -- global so it survives across keybind callbacks

local function log(msg) print(TAG .. " " .. msg .. "\n") end

-- Report the orphan's liveness + position. Must touch UObjects on the
-- game thread.
local function ReportOnce(label)
    ExecuteInGameThread(function()
        if OrphanPawn and OrphanPawn:IsValid() then
            local ok, loc = pcall(function() return OrphanPawn:K2_GetActorLocation() end)
            if ok and loc then
                log(string.format("%s alive=true pos=(%.0f, %.0f, %.0f)", label, loc.X, loc.Y, loc.Z))
            else
                log(label .. " alive=true (could not read location)")
            end
        else
            log(label .. " alive=FALSE (orphan invalid / destroyed / never spawned)")
        end
    end)
end

-- Self-rescheduling 5s status pings across the 60s gate window.
local function ScheduleGateReports()
    local elapsedMs = 0
    local function tick()
        elapsedMs = elapsedMs + 5000
        ReportOnce(string.format("t+%02ds", elapsedMs // 1000))
        if elapsedMs < 60000 then ExecuteWithDelay(5000, tick) end
    end
    ExecuteWithDelay(5000, tick)
end

local function SpawnOrphan()
    ExecuteInGameThread(function()
        local ok, err = pcall(function()
            local pc = UEHelpers.GetPlayerController()
            if not pc or not pc:IsValid() or not pc.Pawn or not pc.Pawn:IsValid() then
                log("no valid local pawn -- load into a save first."); return
            end
            local src = pc.Pawn
            local cls = src:GetClass()
            local world = UEHelpers.GetWorld()
            if not world or not world:IsValid() then log("no valid world."); return end

            -- Spawn 200 units in front-ish (offset on X) to avoid spawning
            -- inside the local player's capsule.
            local loc = src:K2_GetActorLocation()
            local rot = src:K2_GetActorRotation()
            loc.X = loc.X + 200.0

            log(string.format("spawning a 2nd %s ...", cls:GetFName():ToString()))
            OrphanPawn = world:SpawnActor(cls, loc, rot)

            if OrphanPawn and OrphanPawn:IsValid() then
                log("SPAWNED: " .. OrphanPawn:GetFullName())
                log("watching for 60s (the Phase 2 gate). Move around; report pings every 5s.")
                ScheduleGateReports()
            else
                log("SpawnActor returned an INVALID actor (collision? spawn rules?).")
            end
        end)
        if not ok then log("spawn error: " .. tostring(err)) end
    end)
end

local function DestroyOrphan()
    ExecuteInGameThread(function()
        if OrphanPawn and OrphanPawn:IsValid() then
            local ok, err = pcall(function() OrphanPawn:K2_DestroyActor() end)
            if ok then log("orphan destroyed.") else log("destroy error: " .. tostring(err)) end
            OrphanPawn = nil
        else
            log("no valid orphan to destroy.")
        end
    end)
end

RegisterKeyBind(Key.P, {ModifierKey.CONTROL}, SpawnOrphan)
RegisterKeyBind(Key.O, {ModifierKey.CONTROL}, function() ReportOnce("on-demand") end)
RegisterKeyBind(Key.K, {ModifierKey.CONTROL}, DestroyOrphan)

log("loaded. CTRL+P spawn orphan | CTRL+O report | CTRL+K destroy.")
