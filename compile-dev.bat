@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"

set "NINJA=%PROJECT_ROOT%\.build-tools\ninja\ninja.exe"
set "CMAKE=%PROJECT_ROOT%\.build-tools\cmake\cmake-4.3.1-windows-x86_64\bin\cmake.exe"
set "COMPILER_BIN=%PROJECT_ROOT%\.build-tools\llvm-mingw\llvm-mingw-20260407-msvcrt-x86_64\bin"
set "PYTHON_BIN=%PROJECT_ROOT%\.build-tools\llvm-mingw\llvm-mingw-20260407-msvcrt-x86_64\python\bin"
set "DEPENDENCY_BIN=%PROJECT_ROOT%\dependencies-win-x86_64\bin"
set "PATH=%COMPILER_BIN%;%PYTHON_BIN%;%DEPENDENCY_BIN%;%PATH%"
if not exist "%NINJA%" (
    echo Missing bundled Ninja at "%NINJA%".
    exit /b 1
)
if not exist "%CMAKE%" (
    echo Missing bundled CMake at "%CMAKE%".
    exit /b 1
)

set "BUILD_DIR=%PROJECT_ROOT%\build-dev"
if not exist "%BUILD_DIR%\build.ninja" (
    call "%PROJECT_ROOT%\configure-dev.bat"
    if errorlevel 1 exit /b !ERRORLEVEL!
)

echo Refreshing development build files...
"%CMAKE%" -S "%PROJECT_ROOT%" -B "%BUILD_DIR%"
if errorlevel 1 exit /b !ERRORLEVEL!
python "%PROJECT_ROOT%\refresh-dev-build-names.py"
if errorlevel 1 exit /b !ERRORLEVEL!

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
    set "BUILD_EXIT=!ERRORLEVEL!"
    if not "!BUILD_EXIT!"=="0" (
        echo.
        echo Development build failed!
        exit /b !BUILD_EXIT!
    )
    call :sync_runtime
    if errorlevel 1 exit /b !ERRORLEVEL!
    echo.
    echo Development build successful! Executable is in build-dev\bin\MinkowskiKart.exe
    exit /b 0
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

if exist "%BUILD_DIR%\bin\MinkowskiKart.exe" (
    call :sync_runtime
    if errorlevel 1 exit /b %ERRORLEVEL%
)

echo.
if /I "%TARGET%"=="supertuxkart" (
    echo Development build successful! Executable is in build-dev\bin\MinkowskiKart.exe
) else (
    echo Development build successful!
)
exit /b 0

:sync_runtime
if not exist "%BUILD_DIR%\bin" mkdir "%BUILD_DIR%\bin"

robocopy "%DEPENDENCY_BIN%" "%BUILD_DIR%\bin" *.dll /NFL /NDL /NJH /NJS /NC /NS /NP >nul
if errorlevel 8 (
    echo.
    echo Failed to copy dependency DLLs into build-dev\bin.
    exit /b 1
)

for %%F in (libc++.dll libunwind.dll libwinpthread-1.dll) do (
    copy /Y "%COMPILER_BIN%\%%F" "%BUILD_DIR%\bin\%%F" >nul
    if errorlevel 1 (
        echo.
        echo Failed to copy %%F into build-dev\bin.
        exit /b 1
    )
)

exit /b 0
