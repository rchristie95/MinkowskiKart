# connect_wan_both.ps1
# Connects both the Pixel 8 (Android via ADB) and this Windows PC to the
# persistent VPS game server at play.robsonchristie.com:2759 simultaneously.
# No local gameplay tunnels required - both devices connect directly over WAN.

$ErrorActionPreference = "Stop"
$RepoRoot  = $PSScriptRoot
$ADB       = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
$DevSerial = "48161FDJHS0DRL"
$PackageName  = "org.supertuxkart.stk_dbg"
$ActivityName = "$PackageName/$PackageName.SuperTuxKartActivity"

$VpsHost  = "play.robsonchristie.com"
$VpsPort  = 2759
$ServerId = 3

$AndroidLogin = "android1"
$AndroidPwd   = "minkowski2026!"
$AndroidKart  = "curie"

$SshKey = "$env:USERPROFILE\.ssh\minkowski_ovh_ed25519"
$VpsUser = "debian@51.195.235.177"

$BIN_DIR = Join-Path $RepoRoot "build-dev\bin"
if (-not (Test-Path "$BIN_DIR\MinkowskiKart.exe")) {
    $BIN_DIR = Join-Path $RepoRoot "build\bin"
}
$Exe = Join-Path $BIN_DIR "MinkowskiKart.exe"

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  DUAL WAN CONNECTION: Android + Windows -> VPS" -ForegroundColor Cyan
Write-Host "  Server: $VpsHost`:$VpsPort  (ID $ServerId)" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

# --- Pre-flight checks ---
if (-not (Test-Path $ADB)) { throw "ADB not found at $ADB" }
if (-not (Test-Path $Exe))  { throw "MinkowskiKart.exe not found at $Exe" }

$devices = & $ADB devices
if (-not ($devices -match $DevSerial)) {
    throw "Pixel 8 ($DevSerial) not found. Connect via USB and enable USB Debugging."
}
Write-Host "[+] Pixel 8 detected." -ForegroundColor Green

# --- Wake + unlock the device ---
Write-Host "[*] Waking and unlocking Pixel 8..." -ForegroundColor Yellow
& $ADB -s $DevSerial shell input keyevent KEYCODE_WAKEUP | Out-Null
& $ADB -s $DevSerial shell wm dismiss-keyguard 2>$null | Out-Null
Start-Sleep -Milliseconds 800

# --- ADB reverse for matchmaking API auth (port 8000 -> local loopback) ---
# Game traffic uses WAN directly; only auth pings 8000 if configured locally.
Write-Host "[*] Setting up adb reverse for matchmaking API..." -ForegroundColor Yellow
& $ADB -s $DevSerial reverse tcp:8000 tcp:8000 2>$null | Out-Null

# --- Stop any existing game instances ---
Write-Host "[*] Stopping any running game instances..." -ForegroundColor Yellow
& $ADB -s $DevSerial shell am force-stop $PackageName | Out-Null
Stop-Process -Name MinkowskiKart -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

# --- Initialize android1 profile ---
Write-Host "[*] Initializing Android profile '$AndroidLogin'..." -ForegroundColor Yellow
& $ADB -s $DevSerial logcat -c | Out-Null
$initArgs = "--init-user --login=$AndroidLogin --password=$AndroidPwd"
& $ADB -s $DevSerial shell "am start --user 0 -n $ActivityName --es args '\"$initArgs\"'" | Out-Null

Write-Host "[*] Waiting for profile initialization (up to 45s)..." -ForegroundColor Yellow
$deadline     = (Get-Date).AddSeconds(45)
$initialized  = $false
while ((Get-Date) -lt $deadline) {
    $log  = & $ADB -s $DevSerial logcat -d -s "MinkowskiKart:I" "*:S" 2>$null
    $text = $log -join "`n"
    if ($text -match "Done saving user, leaving") {
        Write-Host "[+] Android profile initialized!" -ForegroundColor Green
        $initialized = $true
        break
    }
    if ($text -match "Timed out trying login") {
        throw "Android login timed out. Check credentials or network."
    }
    Start-Sleep -Seconds 2
}
if (-not $initialized) {
    Write-Warning "Profile init timed out - attempting to connect anyway."
}

# --- Stop game before proper launch ---
& $ADB -s $DevSerial shell am force-stop $PackageName | Out-Null
Start-Sleep -Milliseconds 500

# --- Launch Android client -> VPS ---
Write-Host "[*] Creating launch script for Android to preserve argument quotes..." -ForegroundColor Yellow
$scriptContent = "am start --user 0 -n $ActivityName --es args `"--login=$AndroidLogin --password=$AndroidPwd --connect-now=$VpsHost`:$VpsPort --server-id=$ServerId --kart=$AndroidKart`""
Set-Content -Path "launch_stk.sh" -Value $scriptContent -Encoding ASCII
& $ADB -s $DevSerial push launch_stk.sh /data/local/tmp/ | Out-Null
& $ADB -s $DevSerial shell "sh /data/local/tmp/launch_stk.sh" | Out-Null
Write-Host "[+] Android client launched." -ForegroundColor Green

# Brief pause so Android starts loading shaders before PC floods network
Start-Sleep -Seconds 2

# --- Launch Windows PC client -> VPS ---
Write-Host "[*] Launching Windows PC client -> $VpsHost`:$VpsPort ..." -ForegroundColor Cyan
$oldPath = $env:PATH
$env:PATH = "$BIN_DIR;$oldPath"
try {
    Start-Process -FilePath $Exe `
        -ArgumentList "--connect-now=$VpsHost`:$VpsPort --server-id=$ServerId" `
        -WorkingDirectory $BIN_DIR
} finally {
    $env:PATH = $oldPath
}
Write-Host "[+] Windows PC client launched." -ForegroundColor Green

Write-Host ""
Write-Host "============================================================" -ForegroundColor Green
Write-Host "  BOTH CLIENTS LAUNCHED!" -ForegroundColor Green
Write-Host "  Android : $AndroidLogin @ $VpsHost`:$VpsPort" -ForegroundColor Green
Write-Host "  Windows : rchristie95 @ $VpsHost`:$VpsPort" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
Write-Host ""
Write-Host "[*] Checking API for player count (ignoring cert revocation)..." -ForegroundColor Yellow
$response = curl.exe -s -X POST https://online.robsonchristie.com/api/v2/server/get-all/ --ssl-no-revoke
Write-Host $response
Write-Host "Done." -ForegroundColor Green
