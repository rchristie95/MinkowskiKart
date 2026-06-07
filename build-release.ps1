<#
.SYNOPSIS
    Build a signed release APK + AAB for Minkowski Kart.

.DESCRIPTION
    Wraps android/make.sh via Git Bash / MSYS2 bash, wiring in the
    release keystore that lives in store\keystore\.

    Prerequisites:
      - Git for Windows (provides bash.exe)  — OR — MSYS2 / Cygwin bash
      - Android SDK installed (set ANDROID_SDK_ROOT or SDK_PATH below)
      - Android NDK installed inside the SDK (set STK_NDK_VERSION if needed)
      - The keystore file at store\keystore\minkowski-kart-release.keystore

.PARAMETER Version
    The human-readable version name, e.g. "1.0.0".  Defaults to "1.0.0".

.PARAMETER VersionCode
    The integer version code (must be incremented for every Play upload).
    Defaults to 1.

.PARAMETER Arch
    CPU architecture to build: all | arm64-v8a | armeabi-v7a | x86_64 | x86
    Defaults to "all" (builds a fat APK for all architectures).

.PARAMETER BashExe
    Path to bash.exe.  The script tries to find it automatically.

.EXAMPLE
    .\build-release.ps1 -Version "1.0.0" -VersionCode 1
#>

param(
    [string] $Version     = "1.0.0",
    [int]    $VersionCode = 1,
    [string] $Arch        = "all",
    [string] $BashExe     = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ── 1. Locate bash.exe ────────────────────────────────────────────────────────
$bashCandidates = @(
    "C:\Program Files\Git\bin\bash.exe",
    "C:\Program Files\Git\usr\bin\bash.exe",
    "C:\msys64\usr\bin\bash.exe",
    "C:\cygwin64\bin\bash.exe"
)

if ($BashExe -eq "") {
    foreach ($c in $bashCandidates) {
        if (Test-Path $c) { $BashExe = $c; break }
    }
}

if ($BashExe -eq "" -or -not (Test-Path $BashExe)) {
    Write-Error @"
Cannot find bash.exe. Please install Git for Windows from https://git-scm.com
or pass -BashExe 'C:\path\to\bash.exe' explicitly.
"@
    exit 1
}
Write-Host "Using bash: $BashExe" -ForegroundColor Cyan

# ── 2. Resolve paths ──────────────────────────────────────────────────────────
$RepoRoot  = $PSScriptRoot

# Load local credentials from .mk_local.env (gitignored — never committed)
$EnvFile = Join-Path $RepoRoot ".mk_local.env"
if (-not (Test-Path $EnvFile)) {
    throw "Missing .mk_local.env — copy .mk_local.env.example and fill in your credentials."
}
foreach ($line in (Get-Content $EnvFile)) {
    if ($line -match '^\s*([^#=\s]+)\s*=\s*(.+)\s*$') {
        Set-Variable -Name $Matches[1] -Value $Matches[2] -Scope Script
    }
}

$Keystore  = Join-Path $RepoRoot "store\keystore\minkowski-kart-release.keystore"
$MakeScript = Join-Path $RepoRoot "android\make.sh"

if (-not (Test-Path $Keystore)) {
    Write-Error "Keystore not found at: $Keystore`nRun the one-time setup: .\store\keystore\README.md explains how."
    exit 1
}
if (-not (Test-Path $MakeScript)) {
    Write-Error "Build script not found at: $MakeScript"
    exit 1
}

# Convert Windows path to POSIX path for bash
function To-PosixPath([string] $p) {
    # "C:\foo\bar" -> "/c/foo/bar"
    $p = $p -replace '\\', '/'
    if ($p -match '^([A-Za-z]):(.*)') {
        $drive = $Matches[1].ToLower()
        $rest  = $Matches[2]
        return "/$drive$rest"
    }
    return $p
}

$KeystorePosix   = To-PosixPath $Keystore
$MakeScriptPosix = To-PosixPath $MakeScript

# ── 3. Locate Android SDK ─────────────────────────────────────────────────────
$SdkPath = $env:ANDROID_SDK_ROOT
if (-not $SdkPath) { $SdkPath = $env:ANDROID_HOME }
if (-not $SdkPath) {
    $defaultSdk = Join-Path $RepoRoot "android\android-sdk"
    if (Test-Path $defaultSdk) { $SdkPath = $defaultSdk }
}
# Common Android Studio installation location
if (-not $SdkPath -or -not (Test-Path $SdkPath)) {
    $localSdk = "$env:LOCALAPPDATA\Android\Sdk"
    if (Test-Path $localSdk) { $SdkPath = $localSdk }
}
if (-not $SdkPath -or -not (Test-Path $SdkPath)) {
    Write-Error @"
Android SDK not found. Set the ANDROID_SDK_ROOT environment variable, or
install Android Studio and ensure ANDROID_HOME is set.
"@
    exit 1
}
$SdkPosix = To-PosixPath $SdkPath
Write-Host "Android SDK: $SdkPath" -ForegroundColor Cyan

# ── 3b. Detect installed NDK version ─────────────────────────────────────────
$NdkRoot = Join-Path $SdkPath "ndk"
$InstalledNdk = Get-ChildItem $NdkRoot -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    Select-Object -First 1 -ExpandProperty Name

if (-not $InstalledNdk) {
    Write-Error "No NDK found under $NdkRoot. Install an NDK via Android Studio (SDK Manager → SDK Tools → NDK)."
    exit 1
}
Write-Host "Using NDK: $InstalledNdk" -ForegroundColor Cyan

# ── 4. Build ──────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "============================================================" -ForegroundColor Green
Write-Host "  Building Minkowski Kart Release $Version (code $VersionCode)" -ForegroundColor Green
Write-Host "  Architecture: $Arch" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
Write-Host ""

$env:BUILD_TYPE       = "release"
$env:COMPILE_ARCH     = $Arch
$env:PROJECT_VERSION  = $Version
$env:PROJECT_CODE     = "$VersionCode"
$env:STK_KEYSTORE     = $KeystorePosix
$env:STK_STOREPASS    = $MK_KEYSTORE_PASS
$env:STK_ALIAS        = $MK_KEYSTORE_ALIAS
$env:STK_KEYPASS      = $MK_KEYSTORE_PASS
$env:SDK_PATH         = $SdkPosix
$env:STK_NDK_VERSION  = $InstalledNdk
# NDK_PATH is the *parent* dir; make.sh appends /<version> to it
$env:NDK_PATH         = (To-PosixPath (Join-Path $SdkPath "ndk"))

# Run make.sh inside bash — must cd into android/ first; make.sh uses relative
# paths (./gradlew, banner.png) that are relative to the android/ directory.
$AndroidDirPosix = To-PosixPath (Join-Path $RepoRoot "android")
& $BashExe --login -c "cd '$AndroidDirPosix' && chmod +x '$MakeScriptPosix' && '$MakeScriptPosix'"

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}

# ── 5. Report output locations ───────────────────────────────────────────────
Write-Host ""
Write-Host "============================================================" -ForegroundColor Green
Write-Host "  BUILD SUCCESSFUL" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green

$releaseApk = Get-ChildItem "$RepoRoot\android\build\outputs\apk\release\*.apk" -ErrorAction SilentlyContinue | Select-Object -First 1
$releaseAab = Get-ChildItem "$RepoRoot\android\build\outputs\bundle\release\*.aab" -ErrorAction SilentlyContinue | Select-Object -First 1

if ($releaseApk) { Write-Host "APK: $($releaseApk.FullName)" -ForegroundColor Yellow }
if ($releaseAab) { Write-Host "AAB: $($releaseAab.FullName)" -ForegroundColor Yellow }

Write-Host ""
Write-Host "Upload the AAB (preferred) or APK to Google Play Console." -ForegroundColor Cyan
Write-Host "Remember to increment -VersionCode for every upload." -ForegroundColor Cyan
