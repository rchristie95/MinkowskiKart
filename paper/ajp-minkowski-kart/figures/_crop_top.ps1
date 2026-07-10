# Crop full width, keep the top $Frac of the height (strips the in-game
# "Screenshot saved to..." toast baked into the bottom of auto-shot frames).
param(
    [Parameter(Mandatory=$true)][string]$In,
    [Parameter(Mandatory=$true)][string]$Out,
    [double]$Frac = 0.87
)
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile($In)
$w = $src.Width
$h = [int]([math]::Floor($src.Height * $Frac))
$bmp = New-Object System.Drawing.Bitmap -ArgumentList @([int]$w, [int]$h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$srcRect = New-Object System.Drawing.Rectangle -ArgumentList @(0, 0, [int]$w, [int]$h)
$dstRect = New-Object System.Drawing.Rectangle -ArgumentList @(0, 0, [int]$w, [int]$h)
$g.DrawImage($src, $dstRect, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose(); $src.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "saved $Out ($w x $h)"
