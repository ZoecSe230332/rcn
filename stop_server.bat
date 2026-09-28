@echo off
cd /d "%~dp0"
set "RCN_EXE=rcn.exe"
if not exist "%RCN_EXE%" set "RCN_EXE=build\Release\rcn.exe"

echo Stopping rcn server...
"%RCN_EXE%" server stop
echo Done.
pause
