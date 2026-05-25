--[[
  probe_flashlight.lua -- resolve Phase 5F open RE flags hands-on.

  Open flags this probe resolves (from
  research/findings/votv-flashlight-RE-2026-05-25.md sec 8):

    F-FL1: AmainPlayer_C::updateFlashlight() body behavior. Does it
           early-return when !hasFlashlight? Does it branch on
           crankFlashlight? -> hook PRE+POST, log state before/after.
    F-FL2: flashlightStateChanged multicast delegate -- does it fire
           after state change with the NEW value? -> hook + log.
    F-FL3: Confirm UFunction FName "Flashlight Update" (with space)
           and "updateFlashlight" actually exist on mainPlayer_C.
           Walk the class function list and print exact FNames.
    F-FL5: Puppet light_R initial visibility at spawn -- on or off?
           -> spawn an orphan mainPlayer_C, immediately read
           light_R.bVisible / GetVisibleFlag.

  Strategy: hookless polling of the canonical bool plus opportunistic
  RegisterHook on updateFlashlight (BP function, so it may not be
  hookable -- if RegisterHook fails, fall back to delta polling).

  All log lines tagged [FL-PROBE] for grep.

  USER: walk somewhere visible; press F to toggle the flashlight a few
  times. If you have a crank lantern variant equipped, toggle that too.
  CTRL+5 forces a snapshot.
]]

local UEHelpers = require("UEHelpers")

local M = {}
local TAG = "[FL-PROBE]"
local function log(m) print(TAG .. " " .. m .. "\n") end
local function logf(fmt, ...) log(string.format(fmt, ...)) end

local function safe(label, fn)
    local ok, err = pcall(fn)
    if not ok then
        local es = type(err) == "string" and err or "<non-string>"
        log("ERR " .. label .. ": " .. es)
    end
    return ok
end

local function fnameStr(fn)
    if not fn then return "nil" end
    local ok, s = pcall(function() return fn:ToString() end)
    return ok and s or "?"
end

local function fullName(obj)
    if not obj or not obj:IsValid() then return "nil" end
    local ok, n = pcall(function() return obj:GetFullName() end)
    return ok and n or "?"
end

local function shortName(obj)
    if not obj or not obj:IsValid() then return "nil" end
    local ok, n = pcall(function() return obj:GetFName():ToString() end)
    return ok and n or "?"
end

-- ============================================================
-- F-FL3 enumeration: walk mainPlayer_C class Children list and
-- print every UField (function OR property) whose name contains
-- "flash"/"torch"/"lamp"/"light"/"crank". Children is a UField*
-- linked list -- properties and functions are interleaved, so the
-- output mixes both. User must visually filter property hits (e.g.
-- "flashlight" the bool member) from UFunction hits (e.g.
-- "updateFlashlight" or "Flashlight Update"). The latter are
-- what ProcessEvent dispatches on.
-- ============================================================
local function EnumerateFlashlightFns()
    log("=== F-FL3: mainPlayer_C Children (props + functions mixed) ===")
    safe("e-fns", function()
        local mp = UEHelpers.GetPlayerController().Pawn
        if not (mp and mp:IsValid()) then log("no local pawn"); return end
        local cls = mp:GetClass()
        local children = cls.Children
        local hops, hits = 0, 0
        while children and children:IsValid() and hops < 5000 do
            hops = hops + 1
            local nm = fnameStr(children:GetFName())
            local lc = nm:lower()
            if lc:find("flash") or lc:find("torch") or lc:find("lamp")
               or lc:find("light") or lc:find("crank") then
                hits = hits + 1
                -- Try to identify if it's a function vs property by class name.
                local kindOk, kindCls = pcall(function() return children:GetClass():GetFName():ToString() end)
                local kind = kindOk and kindCls or "?"
                logf("  CHILD: %-40s [%s]", nm, kind)
            end
            children = children.Next
        end
        logf("walked %d Children, %d flashlight-related", hops, hits)
    end)
end

-- Enumerate component members named light_R, lag_fl, etc. on the local pawn.
local function EnumerateFlashlightMembers()
    log("=== F-FL5 prereq: mainPlayer_C members ===")
    safe("e-mem", function()
        local mp = UEHelpers.GetPlayerController().Pawn
        if not (mp and mp:IsValid()) then return end
        for _, n in ipairs({ "light_R", "lag_fl", "flashlight", "hasFlashlight",
                             "crankFlashlight", "flashlightMode",
                             "flashlightStateChanged" }) do
            local ok, v = pcall(function() return mp[n] end)
            if ok then
                if type(v) == "boolean" then
                    logf("  mp.%s = %s (bool)", n, tostring(v))
                elseif type(v) == "number" then
                    logf("  mp.%s = %s (num)", n, tostring(v))
                elseif type(v) == "userdata" then
                    local valid = false
                    pcall(function() valid = v:IsValid() end)
                    logf("  mp.%s = %s (obj, valid=%s)", n, fullName(v), tostring(valid))
                else
                    logf("  mp.%s = %s (%s)", n, tostring(v), type(v))
                end
            else
                logf("  mp.%s = <READ FAILED>", n)
            end
        end
    end)
end

-- ============================================================
-- F-FL1 / F-FL2 / F-FL5: continuous delta poll.
-- ============================================================
local prev = {
    flashlight = nil, hasFlashlight = nil, crankFlashlight = nil,
    flashlightMode = nil, input_flashlight = nil, flashlightTypeChanged = nil,
    light_R_visible = nil,
    equipment_class = nil,
    puppet_light_R_visible = nil,
}

local function GetVisible(comp)
    if not comp or not comp:IsValid() then return nil end
    -- Try a few common visibility accessors -- whichever UE4.27 exposes.
    local v = nil
    pcall(function() v = comp:IsVisible() end)
    if v ~= nil then return v end
    pcall(function() v = comp.bVisible end)
    if v ~= nil then return v end
    pcall(function() v = comp:GetVisibleFlag() end)
    return v
end

local function PollOnce()
    safe("poll", function()
        local pc = UEHelpers.GetPlayerController()
        if not (pc and pc:IsValid() and pc.Pawn and pc.Pawn:IsValid()) then return end
        local mp = pc.Pawn

        local cur = {}
        cur.flashlight            = mp.flashlight
        cur.hasFlashlight         = mp.hasFlashlight
        cur.crankFlashlight       = mp.crankFlashlight
        cur.flashlightMode        = mp.flashlightMode
        cur.input_flashlight      = mp.input_flashlight
        cur.flashlightTypeChanged = mp.flashlightTypeChanged
        cur.light_R_visible       = GetVisible(mp.light_R)

        -- Currently-held actor (helps distinguish flashlight _a vs _b vs _c).
        -- mainPlayer.hpp: AActor* holding_actor @0x0A20.
        do
            local ok, hold = pcall(function() return mp.holding_actor end)
            if ok and hold and hold:IsValid() then
                cur.equipment_class = shortName(hold:GetClass())
            else
                cur.equipment_class = "nil"
            end
        end

        -- Orphan puppet light_R state, if Orphan exists (sibling probe).
        if _G.OrphanPawn and _G.OrphanPawn:IsValid() then
            cur.puppet_light_R_visible = GetVisible(_G.OrphanPawn.light_R)
        end

        local changed = {}
        for k, v in pairs(cur) do
            if prev[k] ~= v then
                table.insert(changed, string.format("%s:%s->%s",
                             k, tostring(prev[k]), tostring(v)))
                prev[k] = v
            end
        end
        if #changed > 0 then logf("DELTA %s", table.concat(changed, " ")) end
    end)
end

-- ============================================================
-- F-FL1 active hook attempt -- RegisterHook on mainPlayer_C BP funcs.
-- May fail on BP functions; that's data. Try both naming candidates.
-- ============================================================
local function TryRegisterHooks()
    log("=== F-FL1/F-FL3: RegisterHook attempts ===")
    -- Both candidates from the RE doc: BP function call name (`Flashlight
    -- Update` with literal space) and the native function (`updateFlashlight`).
    local candidates = {
        "/Game/Blueprints/Player/mainPlayer.mainPlayer_C:updateFlashlight",
        "/Game/Blueprints/Player/mainPlayer.mainPlayer_C:Flashlight Update",
        "/Game/Blueprints/Player/mainPlayer.mainPlayer_C:Flashlight_Update",
    }
    for _, path in ipairs(candidates) do
        local ok, err = pcall(function()
            RegisterHook(path, function(Context, ...)
                local mp = Context:get()
                local fl = "?"
                pcall(function() fl = tostring(mp.flashlight) end)
                logf("HOOK-PRE  %s flashlight=%s hasFL=%s crankFL=%s mode=%s",
                     path:match(":(.+)$"), fl,
                     tostring(mp.hasFlashlight),
                     tostring(mp.crankFlashlight),
                     tostring(mp.flashlightMode))
            end, function(Context, ...)
                -- POST fires AFTER the function returns. Per RE doc, the
                -- flashlightStateChanged delegate fires INSIDE updateFlashlight
                -- before return -- so light_R.visible here reflects the delegate's
                -- post-broadcast state. F-FL2 answer = "if light_R.visible at POST
                -- matches the new `flashlight` bool, the delegate broadcast was
                -- timely AND fired with the new value".
                local mp = Context:get()
                local fl = "?"
                local vis = "?"
                pcall(function() fl = tostring(mp.flashlight) end)
                pcall(function() vis = tostring(GetVisible(mp.light_R)) end)
                logf("HOOK-POST %s flashlight=%s light_R.visible=%s (F-FL2: must match)",
                     path:match(":(.+)$"), fl, vis)
            end)
        end)
        if ok then
            logf("  RegisterHook OK: %s", path)
        else
            logf("  RegisterHook FAILED %s: %s", path, tostring(err))
        end
    end
end

-- ============================================================
-- F-FL5 puppet light_R initial state. Requires an orphan.
-- ============================================================
local function SpawnOrphanAndCheckLight()
    log("=== F-FL5: spawn orphan + read light_R initial state ===")
    safe("spawn", function()
        local pc = UEHelpers.GetPlayerController()
        if not (pc and pc.Pawn and pc.Pawn:IsValid()) then log("no local pawn"); return end
        local src = pc.Pawn
        local cls = src:GetClass()
        if cls:GetFName():ToString() ~= "mainPlayer_C" then
            log("local pawn is not mainPlayer_C"); return
        end
        local world = UEHelpers.GetWorld()
        local loc = src:K2_GetActorLocation()
        local rot = src:K2_GetActorRotation()
        loc.X = loc.X + 300.0
        local orphan = world:SpawnActor(cls, loc, rot)
        if not (orphan and orphan:IsValid()) then log("orphan spawn FAILED"); return end
        _G.OrphanPawn = orphan
        logf("orphan spawned: %s", fullName(orphan))

        -- Read light_R immediately, then again @1s and @3s to see if
        -- BeginPlay/component init flips it.
        local function readNow(label)
            local ok, vis = pcall(function() return GetVisible(orphan.light_R) end)
            local fl = "?"
            pcall(function() fl = tostring(orphan.flashlight) end)
            local hasFL = "?"
            pcall(function() hasFL = tostring(orphan.hasFlashlight) end)
            logf("[F-FL5] %s: light_R.visible=%s flashlight=%s hasFlashlight=%s",
                 label, ok and tostring(vis) or "<read fail>", fl, hasFL)
        end
        ExecuteInGameThread(function() readNow("T+0") end)
        ExecuteWithDelay(1000, function() ExecuteInGameThread(function() readNow("T+1s") end) end)
        ExecuteWithDelay(3000, function() ExecuteInGameThread(function() readNow("T+3s") end) end)
    end)
end

-- ============================================================
-- Manual snapshot keybind (CTRL+5)
-- ============================================================
local function ManualSnapshot()
    log("=== CTRL+5 MANUAL SNAPSHOT ===")
    EnumerateFlashlightMembers()
    PollOnce()
end

-- ============================================================
-- Continuous polling (250 ms).
-- ============================================================
local function StartPoller()
    LoopAsync(250, function()
        PollOnce()
        return false
    end)
end

function M.Run()
    log("flashlight probe starting")
    log("USER: 1) press F to toggle flashlight repeatedly (resolves F-FL1/F-FL2)")
    log("      2) press F WITHOUT a flashlight equipped -- the `flashlight`")
    log("         bool MUST NOT change (proves F-FL1 early-return guard)")
    log("      3) if you have a crank lantern (_c), equip + toggle it too (F-FL4)")
    log("      4) CTRL+P spawns an orphan -> light_R initial state logged (F-FL5)")
    log("      5) CTRL+5 forces a snapshot")
    log("NOTE: orphan spawn may auto-possess and displace your pawn -- reconnect if so")

    ExecuteInGameThread(EnumerateFlashlightFns)
    ExecuteInGameThread(EnumerateFlashlightMembers)
    ExecuteInGameThread(TryRegisterHooks)
    ExecuteWithDelay(1000, function() ExecuteInGameThread(StartPoller) end)

    RegisterKeyBind(Key.NUM_FIVE, { ModifierKey.CONTROL }, function()
        ExecuteInGameThread(ManualSnapshot)
    end)
    RegisterKeyBind(Key.P, { ModifierKey.CONTROL }, function()
        ExecuteInGameThread(SpawnOrphanAndCheckLight)
    end)
    log("keybinds: CTRL+5 snapshot, CTRL+P spawn-orphan-and-check-light")
end

return M
