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
echo ======================================================
echo.
echo   [1] Start Server (Listen for incoming inputs on this PC)
echo   [2] Connect Client (Send mouse/keyboard to remote PC)
echo   [3] Pause
echo   [4] Resume
echo   [5] View Server Logs
echo   [6] Stop Server / Daemon
echo   [7] Exit
echo.
echo ======================================================
set /p choice="Select an option [1-7] (default 1): "
if "%choice%"=="" set choice=1
if "%choice%"=="1" goto start_server
if "%choice%"=="2" goto start_client
if "%choice%"=="3" goto do_pause
if "%choice%"=="4" goto do_resume
if "%choice%"=="5" goto do_log
if "%choice%"=="6" goto do_stop
if "%choice%"=="7" exit /b 0

echo Invalid choice.
pause
goto menu

:start_server
echo.
set /p port="Enter port to listen on [default 9999]: "
if "%port%"=="" set port=9999
echo Starting rcn server on port !port!...
"!RCN_EXE!" start -p !port!
echo.
echo Server is running! Clients can now connect to this PC.
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
echo Connecting to !host!:!port! (capturing keyboard and mouse)...
echo Note: When active, local keyboard and mouse are forwarded to the remote host.
"!RCN_EXE!" connect -s !host! -p !port! -d keyboard mouse
echo.
pause
goto menu

:do_pause
echo.
echo Sending pause command...
"!RCN_EXE!" server pause 2>nul
"!RCN_EXE!" client pause 2>nul
echo.
pause
goto menu

:do_resume
echo.
echo Sending resume command...
"!RCN_EXE!" server resume 2>nul
"!RCN_EXE!" client resume 2>nul
echo.
pause
goto menu

:do_log
echo.
echo --- Server Logs ---
"!RCN_EXE!" server log
echo.
pause
goto menu

:do_stop
echo.
echo Stopping daemon...
"!RCN_EXE!" server stop 2>nul
"!RCN_EXE!" client stop 2>nul
echo Done.
echo.
pause
goto menu
