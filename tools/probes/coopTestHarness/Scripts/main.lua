--[[
  coopTestHarness -- autonomous test/launch automation (UE4SS v3.0.1 Lua).

  Purpose: make iterating on coop fast -- launch the game, skip the menus,
  drop straight into gameplay, screenshot, and report, with no manual
  clicking. Pairs with tools/run-test.ps1 (which launches the game windowed
  and writes the scenario file this mod reads).

  This is test tooling (an EXPERIMENT, Lua); the shipping mod is C++ in
  src/votv-coop. Retire/fold into the real harness once the C++ side exists.

  Scenario: read from Mods/coopTestHarness/scenario.txt (one word):
    newgame   -> auto-start a New Game into gameplay (most deterministic)
    loadlast  -> attempt to load the most recent save
    none      -> do nothing automatic (manual keybinds only)
  Falls back to "none" if the file is missing.

  Keybinds (CTRL):
    CTRL+8 -> run the skip-to-gameplay action now (on demand)
    CTRL+9 -> screenshot (HighResShot to the game's Saved/Screenshots)
    CTRL+7 -> report current state (map, local pawn, validity)

  RUNTIME-VALIDATION NOTE (first run, 2026-05-22+): the exact "skip to
  gameplay" call chain is grounded in the reflection dump but UNVERIFIED at
  runtime. SkipToGameplay() tries, in order: (1) invoke ui_menu_C's NewGame
  bound event (mirrors a real click, uses VOTV's own path); (2) fall back to
  OpenLevel("untitled_1"). Confirm which works and prune the other (RULE 1).
]]

local UEHelpers = require("UEHelpers")

local TAG = "[coop-harness]"
local GAMEPLAY_MAP = "untitled_1"
local NEWGAME_FN = "BndEvt__button_NewGame_K2Node_ComponentBoundEvent_0_OnButtonClickedEvent__DelegateSignature"
local AUTO_DELAY_MS = 8000  -- wait for the menu to settle before acting

local function log(m) print(TAG .. " " .. m .. "\n") end

-- Read the scenario file next to this script.
local function ReadScenario()
    local path = "Mods/coopTestHarness/scenario.txt"
    local f = io.open(path, "r")
    if not f then return "none" end
    local s = (f:read("*l") or "none"):gsub("%s+", "")
    f:close()
    return s == "" and "none" or s
end

local function GetLocalPawn()
    local pc = UEHelpers.GetPlayerController()
    if pc and pc:IsValid() and pc.Pawn and pc.Pawn:IsValid() then return pc.Pawn end
    return nil
end

local function Screenshot()
    ExecuteInGameThread(function()
        local ok, err = pcall(function()
            local ksl = UEHelpers.GetKismetSystemLibrary()
            local world = UEHelpers.GetWorldContextObject()
            -- HighResShot writes to <Game>/Saved/Screenshots/WindowsNoEditor/
            ksl:ExecuteConsoleCommand(world, "HighResShot 1920x1080", nil)
        end)
        log(ok and "screenshot requested (HighResShot 1920x1080)" or ("screenshot error: " .. tostring(err)))
    end)
end

local function Report()
    ExecuteInGameThread(function()
        local pawn = GetLocalPawn()
        if pawn then
            local ok, n = pcall(function() return pawn:GetClass():GetFName():ToString() end)
            log("state: in gameplay, local pawn = " .. (ok and n or "?"))
        else
            log("state: no local pawn (likely at menu / loading)")
        end
    end)
end

local function SkipToGameplay()
    ExecuteInGameThread(function()
        if GetLocalPawn() then log("already in gameplay; skip-to-gameplay is a no-op."); return end

        -- (1) Preferred: drive VOTV's own New Game flow via the menu widget.
        local menu = FindFirstOf("ui_menu_C")
        if menu and menu:IsValid() and menu[NEWGAME_FN] then
            local ok, err = pcall(function() menu[NEWGAME_FN](menu) end)
            if ok then log("invoked ui_menu_C NewGame; entering gameplay."); return end
            log("NewGame invoke failed: " .. tostring(err) .. " -- falling back to OpenLevel.")
        else
            log("ui_menu_C / NewGame event not found -- falling back to OpenLevel.")
        end

        -- (2) Fallback: open the gameplay map directly.
        local ok, err = pcall(function()
            local gs = UEHelpers.GetGameplayStatics()
            gs:OpenLevel(UEHelpers.GetWorldContextObject(), FName(GAMEPLAY_MAP), true, "")
        end)
        log(ok and ("OpenLevel(" .. GAMEPLAY_MAP .. ") issued.") or ("OpenLevel error: " .. tostring(err)))
    end)
end

-- Keybinds
RegisterKeyBind(Key.NUM_EIGHT, {ModifierKey.CONTROL}, SkipToGameplay)
RegisterKeyBind(Key.NUM_NINE,  {ModifierKey.CONTROL}, Screenshot)
RegisterKeyBind(Key.NUM_SEVEN, {ModifierKey.CONTROL}, Report)

-- Auto-run scenario
local scenario = ReadScenario()
log("loaded. scenario='" .. scenario .. "'. CTRL+8 skip-to-gameplay | CTRL+9 shot | CTRL+7 report.")
if scenario == "newgame" or scenario == "loadlast" then
    ExecuteWithDelay(AUTO_DELAY_MS, function()
        log("auto: running scenario '" .. scenario .. "' after " .. (AUTO_DELAY_MS // 1000) .. "s.")
        SkipToGameplay()
    end)
end
