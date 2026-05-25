<#
.SYNOPSIS
  Hands-on UE4SS Lua probe of VOTV flashlight + item activation (Phase 5F Inc1).

.DESCRIPTION
  Disables the standalone coop proxy temporarily (so UE4SS can load cleanly),
  deploys probe_flashlight.lua + the dispatch main.lua, sets the harness
  scenario to load s_may2026 + run the flashlight probe, clears the UE4SS
  log, and launches the dev game copy.

  The probe resolves four open RE flags from
  research/findings/votv-flashlight-RE-2026-05-25.md sec 8:
    F-FL1 (updateFlashlight early-return guard)
    F-FL2 (flashlightStateChanged delegate timing)
    F-FL3 (canonical UFunction FName: updateFlashlight vs "Flashlight Update")
    F-FL5 (puppet light_R initial visibility at spawn)

  Restore-proxy step: pass -Restore after testing is done.

.EXAMPLE
  ./tools/probe-flashlight.ps1
  # In game: press F a few times with flashlight equipped, then unequip and
  # press F again (must NOT toggle). CTRL+P to spawn orphan. CTRL+5 to snapshot.

  ./tools/probe-flashlight.ps1 -Restore
  # After testing: re-enable standalone proxy.
#>
[CmdletBinding()]
param(
    [switch]$Restore,
    [string]$Scenario = 'probe_flashlight:s_may2026'
)
$ErrorActionPreference = 'Stop'

$dev = "$PSScriptRoot\..\Game_0.9.0n_dev\WindowsNoEditor\VotV\Binaries\Win64"
if (-not (Test-Path $dev)) { throw "Dev copy not found at $dev" }
$dev = (Resolve-Path $dev).Path

if ($Restore) {
    $disabled = Join-Path $dev 'xinput1_3.dll.probe-disabled'
    if (Test-Path $disabled) {
        Rename-Item $disabled 'xinput1_3.dll' -Force
        Write-Host "standalone proxy RESTORED" -ForegroundColor Green
    } else {
        Write-Host "no .probe-disabled file found; nothing to restore" -ForegroundColor Yellow
    }
    exit 0
}

# 1) Validate the deploy target FIRST. If UE4SS isn't installed we abort
#    here -- BEFORE touching the proxy -- so the user isn't left with a
#    half-applied state (proxy disabled, probe undeployed).
$src = Join-Path $PSScriptRoot 'probes\coopTestHarness\Scripts'
$dst = Join-Path $dev 'Mods\coopTestHarness\Scripts'
if (-not (Test-Path $dst)) {
    throw "Probe destination missing: $dst`n  UE4SS likely not installed in the dev game copy. Run: tools\install-ue4ss.ps1"
}
if (-not (Test-Path (Join-Path $src 'probe_flashlight.lua'))) {
    throw "probe_flashlight.lua not found at $src -- repo state corrupted?"
}

# 2) Disable standalone proxy temporarily so UE4SS loads cleanly.
#    Wrapped in try/catch -- if anything downstream throws, we MUST
#    restore the proxy before re-throwing so the user isn't stuck.
$proxy = Join-Path $dev 'xinput1_3.dll'
$proxyWasDisabled = $false
if (Test-Path $proxy) {
    Rename-Item $proxy 'xinput1_3.dll.probe-disabled' -Force
    $proxyWasDisabled = $true
    Write-Host "standalone proxy disabled (will be restored with -Restore)" -ForegroundColor Cyan
}

try {
    # 3) Deploy the flashlight probe + dispatch main.lua
    Copy-Item (Join-Path $src 'probe_flashlight.lua') (Join-Path $dst 'probe_flashlight.lua') -Force
    Copy-Item (Join-Path $src 'main.lua')             (Join-Path $dst 'main.lua')             -Force
    Write-Host "probe deployed" -ForegroundColor Cyan
} catch {
    # Roll back the proxy disable on failure so the user isn't stuck.
    if ($proxyWasDisabled) {
        $disabled = Join-Path $dev 'xinput1_3.dll.probe-disabled'
        if (Test-Path $disabled) {
            Rename-Item $disabled 'xinput1_3.dll' -Force
            Write-Host "deploy failed -- proxy restored before re-throwing" -ForegroundColor Yellow
        }
    }
    throw
}

# 3) Set scenario
Set-Content -Path (Join-Path $dev 'Mods\coopTestHarness\scenario.txt') `
            -Value $Scenario -NoNewline
Write-Host "scenario = $Scenario" -ForegroundColor Cyan

# 4) Clear UE4SS log so the only [FL-PROBE] entries are this run's
Remove-Item (Join-Path $dev 'UE4SS.log') -Force -ErrorAction SilentlyContinue
Write-Host "UE4SS.log cleared" -ForegroundColor Cyan

# 5) Launch the game
$exe = Join-Path $dev 'VotV-Win64-Shipping.exe'
$proc = Start-Process -FilePath $exe -ArgumentList @('-ResX=1280','-ResY=720','-windowed') `
                      -WorkingDirectory $dev -PassThru
Write-Host ""
Write-Host "Launched PID $($proc.Id)" -ForegroundColor Green
Write-Host ""
Write-Host "============================== USER SCRIPT ===============================" -ForegroundColor Yellow
Write-Host " 1. Wait ~45 seconds for save to load (you should be in gameplay)."
Write-Host ""
Write-Host " 2. F-FL1 + F-FL2 + F-FL3 (all in one):"
Write-Host "    - With a FLASHLIGHT EQUIPPED: press F a few times. Watch your"
Write-Host "      flashlight turn on/off in-world."
Write-Host "    - Now UNEQUIP the flashlight (put it back in bag). Press F again"
Write-Host "      a few times. NOTHING should happen visually -- this proves the"
Write-Host "      early-return guard in updateFlashlight."
Write-Host ""
Write-Host " 3. F-FL4 (optional): if you have a crank lantern (_c variant) in"
Write-Host "    your bag, equip and toggle it -- separate code path."
Write-Host ""
Write-Host " 4. F-FL5: press CTRL+P. An orphan mainPlayer_C is spawned ~3 m to"
Write-Host "    your side. The probe logs its light_R.visible at T+0/+1s/+3s."
Write-Host "    NOTE: orphan may auto-possess and displace you -- reconnect if so."
Write-Host ""
Write-Host " 5. CTRL+5 forces a state snapshot (use anytime to capture)."
Write-Host ""
Write-Host " 6. When done, CLOSE THE GAME WINDOW. Then ping me -- I'll read the"
Write-Host "    UE4SS.log (grep [FL-PROBE]) and lock down Inc2/Inc3."
Write-Host ""
Write-Host " 7. When fully finished, restore the standalone proxy:"
Write-Host "      stop-flashlight-probe.bat"
Write-Host "    (or: ./tools/probe-flashlight.ps1 -Restore)"
Write-Host "==========================================================================" -ForegroundColor Yellow
