@echo off
setlocal

set "PROJECT_ROOT=%~dp0"
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"

set "NINJA=%PROJECT_ROOT%\.build-tools\ninja\ninja.exe"
set "COMPILER_BIN=%PROJECT_ROOT%\.build-tools\llvm-mingw\llvm-mingw-20260407-msvcrt-x86_64\bin"
set "DEPENDENCY_BIN=%PROJECT_ROOT%\dependencies-win-x86_64\bin"
set "PATH=%COMPILER_BIN%;%DEPENDENCY_BIN%;%PATH%"

if not exist "%NINJA%" (
    echo Missing bundled Ninja at "%NINJA%".
    exit /b 1
)

if not exist "%PROJECT_ROOT%\build\build.ninja" (
    echo Missing preconfigured build files in "%PROJECT_ROOT%\build".
    exit /b 1
)

if not exist "%PROJECT_ROOT%\build\tmp" mkdir "%PROJECT_ROOT%\build\tmp"
> "%PROJECT_ROOT%\build\tmp\icon.rc" echo 100 ICON "../tools/windows_installer/icon.ico"

echo Starting compilation...
pushd "%PROJECT_ROOT%"
"%NINJA%" -C build -j 4
set "BUILD_EXIT=%ERRORLEVEL%"
popd

if not "%BUILD_EXIT%"=="0" (
    echo.
    echo Compilation failed!
    exit /b %BUILD_EXIT%
)

echo.
echo Compilation successful! Executable is in build\bin\supertuxkart.exe
