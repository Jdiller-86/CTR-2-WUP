@echo off
cd /d "%~dp0"
py -3 collect-hardware-results.py
if errorlevel 1 echo Collection did not finish. Keep the SD files and report the error above.
pause
