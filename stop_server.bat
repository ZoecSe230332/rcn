@echo off
cd /d "%~dp0"
set "RCN_EXE=rcn.exe"
if not exist "%RCN_EXE%" set "RCN_EXE=build\Release\rcn.exe"

echo ======================================================
echo              rcn - Stopping Server
echo ======================================================
echo Sending stop signal to server daemon...
"%RCN_EXE%" server stop 2>nul

:: Brief delay to allow daemon to cleanly notify clients and shutdown
ping 127.0.0.1 -n 2 >nul

:: Verify daemon process is gone; if still running, force terminate
tasklist /FI "IMAGENAME eq rcn.exe" 2>nul | find /I "rcn.exe" >nul
if not errorlevel 1 (
    echo Terminating remaining rcn processes...
    taskkill /F /IM rcn.exe >nul 2>&1
)

echo.
echo Server stopped successfully! All clients disconnected.
echo ======================================================
pause
