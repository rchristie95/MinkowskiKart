# setup_cmake.ps1
$ErrorActionPreference = "Stop"

$PROJECT_ROOT = $PSScriptRoot
$TEMP_DIR = Join-Path $PROJECT_ROOT "temp_downloads"
$CMAKE_DEST = Join-Path $PROJECT_ROOT ".build-tools\cmake"

if (-not (Test-Path $TEMP_DIR)) {
    New-Item -ItemType Directory -Path $TEMP_DIR -Force | Out-Null
}

$CMakeZip = Join-Path $TEMP_DIR "cmake.zip"
Write-Host "Downloading CMake..."
& curl.exe -L -C - -o $CMakeZip "https://github.com/Kitware/CMake/releases/download/v4.3.1/cmake-4.3.1-windows-x86_64.zip"

Write-Host "Extracting CMake..."
if (-not (Test-Path $CMAKE_DEST)) {
    New-Item -ItemType Directory -Path $CMAKE_DEST -Force | Out-Null
}
Expand-Archive -Path $CMakeZip -DestinationPath $CMAKE_DEST -Force

if (Test-Path $TEMP_DIR) {
    Remove-Item $TEMP_DIR -Recurse -Force | Out-Null
}

Write-Host "CMake setup complete!"
