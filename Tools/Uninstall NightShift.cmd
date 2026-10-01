@echo off
setlocal
if not exist "%~dp0..\payload\install.ps1" (
    echo.
    echo NightShift is not fully extracted.
    echo Right-click the downloaded ZIP and choose Extract All.
    echo Open the extracted NightShift-0.5.0 folder, then Install NightShift.exe.
    echo.
    pause
    exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\payload\install.ps1" -GamePath "%~1" -Uninstall -RemoveOwnedLoader
set "NightShiftExitCode=%ERRORLEVEL%"
if not "%NightShiftExitCode%"=="0" echo Operation stopped. Read the message above before trying again.
pause
exit /b %NightShiftExitCode%
