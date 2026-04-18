@echo off
setlocal

set "PROJECT_ROOT=%~dp0"
set "PROJECT_ROOT=%PROJECT_ROOT:\=/%"
:: Remove trailing slash
if "%PROJECT_ROOT:~-1%"=="/" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"

set "NINJA=%~dp0.build-tools\ninja\ninja.exe"
set "COMPILER_BIN=%~dp0.build-tools\llvm-mingw\llvm-mingw-20260407-msvcrt-x86_64\bin"
set "PATH=%COMPILER_BIN%;%PATH%"

echo Fixing hardcoded paths in build directory...
powershell -NoProfile -Command "Get-ChildItem -Path build -Recurse -File | ForEach-Object { $content = Get-Content $_.FullName -Raw; $newContent = $content -replace 'C:/stk', '%PROJECT_ROOT%' -replace 'C\$:/stk', '%PROJECT_ROOT%' -replace [regex]::Escape('C:\stk'), ('%PROJECT_ROOT%' -replace '/', '\'); if ($content -ne $newContent) { $newContent | Set-Content $_.FullName -NoNewline } }"

echo Creating missing icon.rc...
if not exist build\tmp mkdir build\tmp
echo 100 ICON \"%PROJECT_ROOT%/tools/windows_installer/icon.ico\" > build\tmp\icon.rc

echo Starting compilation...
"%NINJA%" -C build -j 4

if %ERRORLEVEL% equ 0 (
    echo.
    echo Compilation successful! Executable is in build\bin\supertuxkart.exe
) else (
    echo.
    echo Compilation failed!
)

pause
