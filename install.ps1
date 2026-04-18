# MinkowskiKart Installer
# Right-click this file and choose "Run with PowerShell"

$ErrorActionPreference = "Stop"
$ProgressPreference = "Continue"

$REPO = "rchristie95/MinkowskiKart"
$ZIP_NAME = "MinkowskiKart-windows.zip"
$DEFAULT_INSTALL = "$env:LOCALAPPDATA\MinkowskiKart"

function Write-Header {
    Clear-Host
    Write-Host ""
    Write-Host "  ╔══════════════════════════════════════╗" -ForegroundColor Cyan
    Write-Host "  ║       MinkowskiKart  Installer       ║" -ForegroundColor Cyan
    Write-Host "  ╚══════════════════════════════════════╝" -ForegroundColor Cyan
    Write-Host ""
}

function Write-Step($n, $msg) {
    Write-Host "  [$n] $msg" -ForegroundColor Yellow
}

function Write-OK($msg) {
    Write-Host "      OK: $msg" -ForegroundColor Green
}

function Write-Fail($msg) {
    Write-Host ""
    Write-Host "  ERROR: $msg" -ForegroundColor Red
    Write-Host ""
    Write-Host "  Press any key to exit..."
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
    exit 1
}

# ── Header ──────────────────────────────────────────────────────────────────
Write-Header

# ── Choose install location ──────────────────────────────────────────────────
Write-Host "  Install location (press Enter for default):" -ForegroundColor White
Write-Host "  Default: $DEFAULT_INSTALL" -ForegroundColor DarkGray
Write-Host ""
$installDir = Read-Host "  Path"
if ([string]::IsNullOrWhiteSpace($installDir)) { $installDir = $DEFAULT_INSTALL }
$installDir = $installDir.Trim('"').TrimEnd('\')

Write-Host ""
Write-Host "  Installing to: $installDir" -ForegroundColor White
Write-Host ""

# ── Fetch latest release info ─────────────────────────────────────────────────
Write-Step 1 "Finding latest release on GitHub..."
try {
    $apiUrl = "https://api.github.com/repos/$REPO/releases/latest"
    $headers = @{ "User-Agent" = "MinkowskiKart-Installer" }
    $release = Invoke-RestMethod -Uri $apiUrl -Headers $headers
    $asset = $release.assets | Where-Object { $_.name -eq $ZIP_NAME } | Select-Object -First 1
    if (-not $asset) { Write-Fail "Could not find '$ZIP_NAME' in the latest release." }
    $downloadUrl = $asset.browser_download_url
    $fileSize = [math]::Round($asset.size / 1MB, 0)
    Write-OK "Found release $($release.tag_name) ($fileSize MB)"
} catch {
    Write-Fail "Could not reach GitHub. Check your internet connection.`n      $_"
}

# ── Download ──────────────────────────────────────────────────────────────────
$zipPath = "$env:TEMP\MinkowskiKart-install.zip"
Write-Step 2 "Downloading game ($fileSize MB) - this may take a few minutes..."
try {
    $wc = New-Object System.Net.WebClient
    $wc.DownloadFile($downloadUrl, $zipPath)
    Write-OK "Download complete"
} catch {
    Write-Fail "Download failed: $_"
}

# ── Extract ───────────────────────────────────────────────────────────────────
Write-Step 3 "Extracting to $installDir..."
try {
    if (Test-Path $installDir) { Remove-Item $installDir -Recurse -Force }
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
    Expand-Archive -Path $zipPath -DestinationPath $installDir -Force
    Remove-Item $zipPath -Force
    Write-OK "Extracted successfully"
} catch {
    Write-Fail "Extraction failed: $_"
}

# ── Desktop shortcut ──────────────────────────────────────────────────────────
Write-Step 4 "Creating desktop shortcut..."
try {
    $wsh = New-Object -ComObject WScript.Shell
    $shortcut = $wsh.CreateShortcut("$env:USERPROFILE\Desktop\MinkowskiKart.lnk")
    $shortcut.TargetPath = "$installDir\MinkowskiKart.bat"
    $shortcut.WorkingDirectory = $installDir
    # Use the exe icon if available
    if (Test-Path "$installDir\supertuxkart.exe") {
        $shortcut.IconLocation = "$installDir\supertuxkart.exe,0"
    }
    $shortcut.Description = "MinkowskiKart - Relativistic Racing"
    $shortcut.Save()
    Write-OK "Shortcut created on Desktop"
} catch {
    Write-Host "      (Could not create shortcut - you can launch via $installDir\MinkowskiKart.bat)" -ForegroundColor DarkGray
}

# ── Done ──────────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "  ╔══════════════════════════════════════╗" -ForegroundColor Green
Write-Host "  ║   Installation complete! Have fun.   ║" -ForegroundColor Green
Write-Host "  ╚══════════════════════════════════════╝" -ForegroundColor Green
Write-Host ""
Write-Host "  Launch: double-click 'MinkowskiKart' on your Desktop" -ForegroundColor White
Write-Host ""

$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
