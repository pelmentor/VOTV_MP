@echo off
REM stop-flashlight-probe.bat -- restore the standalone proxy after a flashlight probe.
REM
REM probe-flashlight.bat renames xinput1_3.dll -> xinput1_3.dll.probe-disabled
REM so UE4SS can load cleanly. This script puts it back.

setlocal
set "ROOT=%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\probe-flashlight.ps1" -Restore
endlocal
