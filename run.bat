@echo off
setlocal enabledelayedexpansion
title rcn - Remote Input Daemon
cd /d "%~dp0"

set "RCN_EXE=rcn.exe"
if not exist "!RCN_EXE!" (
    if exist "build\Release\rcn.exe" (
        set "RCN_EXE=build\Release\rcn.exe"
    ) else (
        echo [ERROR] rcn.exe not found!
        echo Please build the project first.
        pause
        exit /b 1
    )
)

:menu
cls
echo ======================================================
echo              rcn - Remote Input Daemon
echo       Emergency Stop Keybind: [Ctrl + Alt + Esc]
echo ======================================================
echo.
echo   [1] Start Server (Listen for incoming inputs on this PC)
echo   [2] Connect Client (Send mouse/keyboard to remote PC)
echo   [3] Force Stop / Clean Reset
echo   [4] Exit
echo.
echo ======================================================
set /p choice="Select an option [1-4] (default 1): "
if "%choice%"=="" set choice=1
if "%choice%"=="1" goto start_server
if "%choice%"=="2" goto start_client
if "%choice%"=="3" goto do_stop
if "%choice%"=="4" exit /b 0

echo Invalid choice.
pause
goto menu

:start_server
echo.
set /p port="Enter port to listen on [default 9999]: "
if "%port%"=="" set port=9999
echo.
echo Starting rcn server on port !port!...
echo Press [Ctrl + C] in this window to stop the server.
echo.
"!RCN_EXE!" start -p !port!
echo.
pause
goto menu

:start_client
echo.
set /p host="Enter server IP address (e.g. 192.168.1.100 or 127.0.0.1): "
if "!host!"=="" (
    echo [ERROR] Server IP cannot be empty!
    pause
    goto menu
)
set /p port="Enter server port [default 9999]: "
if "!port!"=="" set port=9999
echo.
echo Connecting to !host!:!port!...
echo [NOTE] Local keyboard and mouse are forwarded while connected.
echo [HOTKEY] Press Ctrl + Alt + Esc at ANY time for Emergency Stop!
echo Press [Ctrl + C] to disconnect.
echo.
"!RCN_EXE!" connect -s !host! -p !port!
echo.
pause
goto menu

:do_stop
echo.
echo Terminating any active rcn processes and releasing inputs...
tasklist /FI "IMAGENAME eq rcn.exe" 2>nul | find /I "rcn.exe" >nul
if not errorlevel 1 (
    taskkill /F /IM rcn.exe >nul 2>&1
)
echo Done. All inputs released and processes stopped.
echo.
pause
goto menu
