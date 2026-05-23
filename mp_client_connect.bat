@echo off
REM mp_client_connect.bat -- launch VOTV as the coop CLIENT, connecting to a host.
REM
REM Deploys the standalone mod, configures the harness via environment variables
REM (VOTVCOOP_NET_ROLE / _PEER / _PORT / _NICK), and launches VOTV. The client
REM initiates the handshake to the host's IP:port.
REM
REM Usage:
REM   mp_client_connect.bat                       peer=127.0.0.1 (same-box test)
REM   mp_client_connect.bat 192.168.1.42          custom peer IP, port=47621
REM   mp_client_connect.bat 192.168.1.42 47700    custom peer IP + port
REM   mp_client_connect.bat 192.168.1.42 47621 Bob   peer + port + nickname
REM
REM Build first if needed:  cmake --build build\votv-coop --config Release
REM Restore UE4SS after testing: stop-coop.bat

setlocal
set "ROOT=%~dp0"
set "WIN64=%ROOT%Game_0.9.0n\WindowsNoEditor\VotV\Binaries\Win64"

set "PEER=%~1"
if "%PEER%"=="" set "PEER=127.0.0.1"
set "PORT=%~2"
if "%PORT%"=="" set "PORT=47621"
set "NICK=%~3"
if "%NICK%"=="" set "NICK=Client"

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
set "VOTVCOOP_NET_ROLE=client"
set "VOTVCOOP_NET_PEER=%PEER%"
set "VOTVCOOP_NET_PORT=%PORT%"
set "VOTVCOOP_NET_NICK=%NICK%"

echo.
echo Launching VOTV as CLIENT (peer=%PEER%:%PORT%, nick=%NICK%)
echo Make sure the host already ran  mp_host_game.bat  on %PEER%.
echo.

start "" "%WIN64%\VotV-Win64-Shipping.exe" -windowed -ResX=1920 -ResY=1080

echo Running. Press F12 for a screenshot; F2 for the dev pos/cam overlay.
echo When done, run stop-coop.bat to restore UE4SS.
endlocal
