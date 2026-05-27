@echo off
REM Autonomous LAN smoke (used by Claude). Real driver: tools/mp.py smoke
python "%~dp0tools\mp.py" smoke %*
