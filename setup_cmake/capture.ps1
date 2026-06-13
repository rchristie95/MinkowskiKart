# Passive capture of the MinkowskiKart window for renderer comparison.
# Brings the game window to the foreground (focus only, no input), then
# captures its screen rectangle. Usage:
#   powershell -File capture.ps1 -Prefix vk_base -Delays 25,40
param(
    [string]$Prefix = "shot",
    [int[]]$Delays = @(25, 40),
    [string]$OutDir = "C:\Users\robso\MinkowskiKart\build-dev\shots"
)
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName Microsoft.VisualBasic
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Cap {
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hwnd);
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }
$prev = 0
foreach ($d in $Delays)
{
    Start-Sleep -Seconds ($d - $prev)
    $prev = $d
    $proc = Get-Process MinkowskiKart -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if ($null -eq $proc) { Write-Output "no game window"; continue }
    try { [Microsoft.VisualBasic.Interaction]::AppActivate($proc.Id) } catch {}
    [Win32Cap]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 700
    $rect = New-Object Win32Cap+RECT
    [Win32Cap]::GetWindowRect($proc.MainWindowHandle, [ref]$rect) | Out-Null
    $w = $rect.Right - $rect.Left
    $h = $rect.Bottom - $rect.Top
    if ($w -le 0 -or $h -le 0) { Write-Output "bad window size"; continue }
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($rect.Left, $rect.Top, 0, 0,
        (New-Object System.Drawing.Size($w, $h)))
    $g.Dispose()
    $path = Join-Path $OutDir ("{0}_{1}s.png" -f $Prefix, $d)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output "saved $path"
}
