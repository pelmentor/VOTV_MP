@echo off
REM Thin shim. Real launcher: tools/mp.py
python "%~dp0tools\mp.py" client --peer %1 %2 %3 %4 %5
