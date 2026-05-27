@echo off
REM Thin shim. Real launcher: tools/mp.py
python "%~dp0tools\mp.py" host %*
