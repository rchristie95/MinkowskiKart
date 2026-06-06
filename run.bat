@echo off
setlocal

set "PROJECT_ROOT=%~dp0"
set "BIN_DIR=%PROJECT_ROOT%build-dev\bin"
if not exist "%BIN_DIR%\MinkowskiKart.exe" set "BIN_DIR=%PROJECT_ROOT%build\bin"

:: Add bin directory to PATH so DLLs are found
set "PATH=%BIN_DIR%;%PATH%"

echo Starting SuperTuxKart...
cd /d "%BIN_DIR%"
.\MinkowskiKart.exe --login=rchristie95 --password=Telly612223! %*

if %ERRORLEVEL% neq 0 (
    echo.
    echo Game exited with error code %ERRORLEVEL%
    pause
)
