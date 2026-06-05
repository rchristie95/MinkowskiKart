# MinkowskiKart - Package for Distribution
# Right-click -> Run with PowerShell

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$preferredBuildBin = Join-Path $projectRoot "build-dev\bin"
$fallbackBuildBin  = Join-Path $projectRoot "build\bin"
$stageDir     = Join-Path $projectRoot "_package_staging"
$zipOut       = Join-Path $projectRoot "MinkowskiKart-windows.zip"

if (Test-Path (Join-Path $preferredBuildBin "MinkowskiKart.exe")) {
    $buildBin = $preferredBuildBin
}
elseif (Test-Path (Join-Path $fallbackBuildBin "MinkowskiKart.exe")) {
    $buildBin = $fallbackBuildBin
}
else {
    $buildBin = $preferredBuildBin
}

Write-Host ""
Write-Host "================================================" -ForegroundColor Cyan
Write-Host "  MinkowskiKart - Package for Distribution" -ForegroundColor Cyan
Write-Host "================================================" -ForegroundColor Cyan
Write-Host ""

# Check build exists
if (-not (Test-Path "$buildBin\MinkowskiKart.exe")) {
    Write-Host "ERROR: MinkowskiKart.exe not found at:" -ForegroundColor Red
    Write-Host "  $buildBin" -ForegroundColor Red
    Write-Host "Build the project first." -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

# Clean staging dir
Write-Host "Cleaning staging directory..." -ForegroundColor Yellow
if (Test-Path $stageDir) { Remove-Item $stageDir -Recurse -Force }
New-Item -ItemType Directory -Path $stageDir | Out-Null

# Copy exe + DLLs
Write-Host "Copying game binary and DLLs..." -ForegroundColor Yellow
Copy-Item "$buildBin\*" $stageDir
if (Test-Path "$stageDir\supertuxkart.pdb") { Remove-Item "$stageDir\supertuxkart.pdb" }
if (Test-Path "$stageDir\MinkowskiKart.pdb") { Remove-Item "$stageDir\MinkowskiKart.pdb" }

# Copy data
Write-Host "Copying data directory..." -ForegroundColor Yellow
Copy-Item (Join-Path $projectRoot "data") $stageDir -Recurse

# Copy stk-assets
Write-Host "Copying stk-assets (~750MB, may take a few minutes)..." -ForegroundColor Yellow
Copy-Item (Join-Path $projectRoot "stk-assets") $stageDir -Recurse

# Write launcher batch
Write-Host "Creating launcher..." -ForegroundColor Yellow
@'
@echo off
cd /d "%~dp0"
start "" "MinkowskiKart.exe" --root-data=data --login=rchristie95 --password=Telly612223!
'@ | Set-Content "$stageDir\MinkowskiKart.bat" -Encoding ASCII

# Create zip
Write-Host ""
Write-Host "Creating zip archive (this may take several minutes)..." -ForegroundColor Yellow
if (Test-Path $zipOut) { Remove-Item $zipOut }

Add-Type -Assembly "System.IO.Compression.FileSystem"
[System.IO.Compression.ZipFile]::CreateFromDirectory($stageDir, $zipOut, [System.IO.Compression.CompressionLevel]::Optimal, $false)

# Clean staging
Remove-Item $stageDir -Recurse -Force

$sizeMB = [math]::Round((Get-Item $zipOut).Length / 1MB, 0)
Write-Host ""
Write-Host "================================================" -ForegroundColor Green
Write-Host "  Package ready: MinkowskiKart-windows.zip ($sizeMB MB)" -ForegroundColor Green
Write-Host ""
Write-Host "  Next steps:" -ForegroundColor White
Write-Host "    1. Go to https://github.com/rchristie95/MinkowskiKart/releases/new"
Write-Host "    2. Tag: v1.0  (or bump version each release)"
Write-Host "    3. Upload: MinkowskiKart-windows.zip"
Write-Host "    4. Publish the release"
Write-Host "    5. Send your friend install.ps1"
Write-Host "================================================" -ForegroundColor Green
Write-Host ""
Read-Host "Press Enter to exit"
