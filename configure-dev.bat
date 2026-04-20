@echo off
setlocal EnableExtensions

set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"

set "TEMPLATE_DIR=%PROJECT_ROOT%\build"
set "BUILD_DIR=%PROJECT_ROOT%\build-dev"

if not exist "%TEMPLATE_DIR%\build.ninja" (
    echo Missing build template at "%TEMPLATE_DIR%\build.ninja".
    exit /b 1
)
if not exist "%TEMPLATE_DIR%\CMakeCache.txt" (
    echo Missing build template at "%TEMPLATE_DIR%\CMakeCache.txt".
    exit /b 1
)
if not exist "%TEMPLATE_DIR%\CMakeFiles\rules.ninja" (
    echo Missing build template at "%TEMPLATE_DIR%\CMakeFiles\rules.ninja".
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo Bootstrapping development build in "%BUILD_DIR%"...
robocopy "%TEMPLATE_DIR%" "%BUILD_DIR%" /E /XF * /NFL /NDL /NJH /NJS /NC /NS /NP >nul
if errorlevel 8 (
    echo.
    echo Failed to prepare build-dev directory structure.
    exit /b 1
)

copy /Y "%TEMPLATE_DIR%\build.ninja" "%BUILD_DIR%\build.ninja" >nul
copy /Y "%TEMPLATE_DIR%\CMakeCache.txt" "%BUILD_DIR%\CMakeCache.txt" >nul
if not exist "%BUILD_DIR%\CMakeFiles" mkdir "%BUILD_DIR%\CMakeFiles"
copy /Y "%TEMPLATE_DIR%\CMakeFiles\rules.ninja" "%BUILD_DIR%\CMakeFiles\rules.ninja" >nul
if not exist "%BUILD_DIR%\tmp" mkdir "%BUILD_DIR%\tmp"
> "%BUILD_DIR%\tmp\icon.rc" echo 100 ICON "../tools/windows_installer/icon.ico"

if not exist "%BUILD_DIR%\build.ninja" (
    echo.
    echo Failed to copy build.ninja into build-dev.
    exit /b 1
)
if not exist "%BUILD_DIR%\CMakeFiles\rules.ninja" (
    echo.
    echo Failed to copy rules.ninja into build-dev.
    exit /b 1
)

echo.
echo Development build files are ready in build-dev.
