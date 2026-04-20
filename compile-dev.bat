@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"

set "NINJA=%PROJECT_ROOT%\.build-tools\ninja\ninja.exe"
if not exist "%NINJA%" (
    echo Missing bundled Ninja at "%NINJA%".
    exit /b 1
)

set "BUILD_DIR=%PROJECT_ROOT%\build-dev"
if not exist "%BUILD_DIR%\build.ninja" (
    call "%PROJECT_ROOT%\configure-dev.bat"
    if errorlevel 1 exit /b !ERRORLEVEL!
)

if /I "%~1"=="clean" (
    echo Cleaning development build...
    "%NINJA%" -C "%BUILD_DIR%" -t clean
    exit /b !ERRORLEVEL!
)

if /I "%~1"=="full" (
    echo Cleaning development build...
    "%NINJA%" -C "%BUILD_DIR%" -t clean
    if errorlevel 1 exit /b !ERRORLEVEL!
    echo.
    echo Starting full development rebuild...
    "%NINJA%" -C "%BUILD_DIR%" -j 4 supertuxkart
    exit /b !ERRORLEVEL!
)

if "%~1"=="" (
    set "TARGET=supertuxkart"
) else (
    set "TARGET=%*"
)

echo Starting incremental development build for %TARGET%...
"%NINJA%" -C "%BUILD_DIR%" -j 4 %TARGET%
set "BUILD_EXIT=%ERRORLEVEL%"

if not "%BUILD_EXIT%"=="0" (
    echo.
    echo Development build failed!
    exit /b %BUILD_EXIT%
)

echo.
if /I "%TARGET%"=="supertuxkart" (
    echo Development build successful! Executable is in build-dev\bin\supertuxkart.exe
) else (
    echo Development build successful!
)
