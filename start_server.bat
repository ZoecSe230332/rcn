@echo off
cd /d "%~dp0"
set "RCN_EXE=rcn.exe"
if not exist "%RCN_EXE%" set "RCN_EXE=build\Release\rcn.exe"

echo ======================================================
echo           rcn - Starting Server (Port 9999)
echo       Emergency Stop Keybind: [Ctrl + Alt + Esc]
echo       Press [Ctrl + C] in this window to stop
echo ======================================================
echo.
"%RCN_EXE%" start -p 9999
echo.
pause
