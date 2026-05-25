@echo off
REM probe-flashlight.bat -- one-click hands-on UE4SS Lua probe for Phase 5F.
REM
REM Resolves the four open RE flags from
REM research/findings/votv-flashlight-RE-2026-05-25.md sec 8:
REM   F-FL1 (updateFlashlight early-return guard)
REM   F-FL2 (flashlightStateChanged delegate timing)
REM   F-FL3 (canonical UFunction FName)
REM   F-FL5 (puppet light_R initial visibility at spawn)
REM
REM Deploys probe_flashlight.lua + dispatch main.lua to the DEV game copy,
REM disables the standalone proxy so UE4SS loads cleanly, sets the harness
REM scenario to load s_may2026 + run the flashlight probe, clears the UE4SS
REM log, and launches VOTV windowed.
REM
REM Usage:
REM   probe-flashlight.bat                       (default save: s_may2026)
REM   probe-flashlight.bat probe_flashlight      (no save preload)
REM   probe-flashlight.bat probe_flashlight:<slot>   (custom save)
REM
REM When done, run:  stop-flashlight-probe.bat
REM   (restores the standalone proxy for normal coop testing)

setlocal
set "ROOT=%~dp0"
set "SCENARIO=%~1"
if "%SCENARIO%"=="" set "SCENARIO=probe_flashlight:s_may2026"

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\probe-flashlight.ps1" -Scenario "%SCENARIO%"
if errorlevel 1 (
  echo.
  echo Probe launch FAILED. Check the error above. Common causes:
  echo   - UE4SS not installed in Game_0.9.0n_dev  -^>  tools\install-ue4ss.ps1
  echo   - Game_0.9.0n_dev/ missing entirely       -^>  symlink or copy from prod
  pause
  exit /b 1
)

echo.
echo Probe started. Follow the in-window USER SCRIPT instructions.
echo When fully done, run stop-flashlight-probe.bat to restore the standalone proxy.
pause
endlocal
