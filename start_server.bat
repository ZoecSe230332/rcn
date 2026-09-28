@echo off
cd /d "%~dp0"
set "RCN_EXE=rcn.exe"
if not exist "%RCN_EXE%" set "RCN_EXE=build\Release\rcn.exe"

echo ======================================================
echo           rcn - Starting Server (Port 9999)
echo ======================================================
"%RCN_EXE%" start -p 9999
echo.
echo Server is active in the background on port 9999!
echo Other computers can now connect to this PC.
echo.
echo To pause:  double-click run.bat and select [3]
echo To stop:   double-click stop_server.bat
echo.
pause
