# setup_dependencies.ps1
# Setup build tools and dependencies for MinkowskiKart on Windows

$ErrorActionPreference = "Stop"

$PROJECT_ROOT = $PSScriptRoot
$TEMP_DIR = Join-Path $PROJECT_ROOT "temp_downloads"
$BUILD_TOOLS_DIR = Join-Path $PROJECT_ROOT ".build-tools"

if (-not (Test-Path $TEMP_DIR)) {
    New-Item -ItemType Directory -Path $TEMP_DIR -Force | Out-Null
}

function Download-WithResume($Url, $OutPath) {
    Write-Host "Downloading $Url to $OutPath..."
    # Using curl.exe with -L (follow redirects) and -C - (resume)
    # This ensures that even if the download is interrupted by a server restart, it can resume.
    & curl.exe -L -C - -o $OutPath $Url
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 33) {
        throw "Failed to download $Url (curl exit code $LASTEXITCODE)"
    }
}

# 1. Download & Extract Dependencies (dependencies-win-x86_64)
$DepsDir = Join-Path $PROJECT_ROOT "dependencies-win-x86_64"
$DepsZip = Join-Path $TEMP_DIR "dependencies-win-x86_64.zip"
if (-not (Test-Path $DepsDir)) {
    Write-Host "Setting up dependencies-win-x86_64..."
    Download-WithResume "https://github.com/supertuxkart/dependencies/releases/download/1.4/dependencies-win-x86_64.zip" $DepsZip
    Write-Host "Extracting dependencies..."
    # Extract directly into $PROJECT_ROOT (the zip contains dependencies-win-x86_64 folder at its root)
    Expand-Archive -Path $DepsZip -DestinationPath $PROJECT_ROOT -Force
    Write-Host "Dependencies setup complete."
} else {
    Write-Host "dependencies-win-x86_64 already exists. Skipping."
}

# 2. Download & Extract llvm-mingw
$LLVMDir = Join-Path $BUILD_TOOLS_DIR "llvm-mingw"
$LLVMZip = Join-Path $TEMP_DIR "llvm-mingw.zip"
$ExpectedLLVMPath = Join-Path $LLVMDir "llvm-mingw-20260407-msvcrt-x86_64"
if (-not (Test-Path $ExpectedLLVMPath)) {
    Write-Host "Setting up llvm-mingw compiler..."
    Download-WithResume "https://github.com/mstorsjo/llvm-mingw/releases/download/20260407/llvm-mingw-20260407-msvcrt-x86_64.zip" $LLVMZip
    Write-Host "Extracting llvm-mingw..."
    if (-not (Test-Path $LLVMDir)) { New-Item -ItemType Directory -Path $LLVMDir -Force | Out-Null }
    Expand-Archive -Path $LLVMZip -DestinationPath $LLVMDir -Force
    Write-Host "llvm-mingw setup complete."
} else {
    Write-Host "llvm-mingw already exists. Skipping."
}

# 3. Download & Extract Ninja
$NinjaDir = Join-Path $BUILD_TOOLS_DIR "ninja"
$NinjaZip = Join-Path $TEMP_DIR "ninja.zip"
$NinjaExe = Join-Path $NinjaDir "ninja.exe"
if (-not (Test-Path $NinjaExe)) {
    Write-Host "Setting up Ninja build tool..."
    Download-WithResume "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip" $NinjaZip
    Write-Host "Extracting Ninja..."
    if (-not (Test-Path $NinjaDir)) { New-Item -ItemType Directory -Path $NinjaDir -Force | Out-Null }
    Expand-Archive -Path $NinjaZip -DestinationPath $NinjaDir -Force
    Write-Host "Ninja setup complete."
} else {
    Write-Host "Ninja already exists. Skipping."
}

# Clean up temp files
if (Test-Path $TEMP_DIR) {
    Remove-Item $TEMP_DIR -Recurse -Force
    Write-Host "Cleaned up temporary download files."
}

Write-Host "Build tools and dependencies setup successful!"
