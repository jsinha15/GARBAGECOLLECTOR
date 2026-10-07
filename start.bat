@echo off
rem Double-click to build (if needed), start HeapLens and open the UI in your browser.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start.ps1" %*
pause
