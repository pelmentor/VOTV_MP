@echo off
REM mp_host_game.bat -- launch VOTV as the coop HOST (binds the UDP port).
REM
REM Deploys the standalone mod, configures the harness via environment variables
REM (VOTVCOOP_NET_ROLE / _PORT / _NICK) so no ini editing is needed, and launches
REM VOTV. The host waits for a client; the friend runs mp_client_connect.bat
REM <YOUR_IP> on their machine (or on this one, for a same-box test).
REM
REM Usage:
REM   mp_host_game.bat                  port=47621, nick=Host
REM   mp_host_game.bat 47700            custom port
REM   mp_host_game.bat 47621 MyNick     custom port + nickname
REM
REM Build first if needed:  cmake --build build\votv-coop --config Release
REM Restore UE4SS after testing: stop-coop.bat

setlocal
set "ROOT=%~dp0"
set "WIN64=%ROOT%Game_0.9.0n\WindowsNoEditor\VotV\Binaries\Win64"

set "PORT=%~1"
if "%PORT%"=="" set "PORT=47621"
set "NICK=%~2"
if "%NICK%"=="" set "NICK=Host"

echo Deploying standalone mod (UE4SS disabled)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\deploy-loader.ps1" -Standalone
if errorlevel 1 (
  echo.
  echo Deploy failed. Did you build?  cmake --build build\votv-coop --config Release
  pause
  exit /b 1
)

REM Write scenario.txt (play = hands-on with you in control). Newline-free.
<nul set /p "=play" > "%WIN64%\scenario.txt"
del "%WIN64%\votv-coop.log" 2>nul

REM Hand the harness its net config via env vars. The harness reads env BEFORE
REM votv-coop.ini, so this overrides any net.* keys without editing the ini.
set "VOTVCOOP_SCENARIO=play"
set "VOTVCOOP_NET_ROLE=host"
set "VOTVCOOP_NET_PORT=%PORT%"
set "VOTVCOOP_NET_NICK=%NICK%"

echo.
echo Launching VOTV as HOST (port=%PORT%, nick=%NICK%)
echo Tell your friend to run:  mp_client_connect.bat ^<your-LAN-IP^>
echo (Find your LAN IP with  ipconfig  -- e.g. 192.168.x.x)
echo.

start "" "%WIN64%\VotV-Win64-Shipping.exe" -windowed -ResX=1920 -ResY=1080

echo Running. Press F12 for a screenshot; F2 for the dev pos/cam overlay.
echo When done, run stop-coop.bat to restore UE4SS.
endlocal
