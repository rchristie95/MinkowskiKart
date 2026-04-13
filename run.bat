@echo off
setlocal

set "PROJECT_ROOT=%~dp0"
set "BIN_DIR=%PROJECT_ROOT%build\bin"

:: Add bin directory to PATH so DLLs are found
set "PATH=%BIN_DIR%;%PATH%"

echo Starting SuperTuxKart...
cd /d "%BIN_DIR%"
.\supertuxkart.exe --root-data=../../data %*

if %ERRORLEVEL% neq 0 (
    echo.
    echo Game exited with error code %ERRORLEVEL%
    pause
)
