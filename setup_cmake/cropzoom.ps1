# Crop a region from a PNG and save it magnified (nearest neighbour), so
# per-pixel grain survives chat image downscaling.
param(
    [Parameter(Mandatory=$true)][string]$In,
    [Parameter(Mandatory=$true)][string]$Out,
    [int]$X = 0, [int]$Y = 0, [int]$W = 400, [int]$H = 300, [int]$Scale = 3
)
Add-Type -AssemblyName System.Drawing
$dw = $W * $Scale
$dh = $H * $Scale
$src = [System.Drawing.Image]::FromFile($In)
$bmp = New-Object System.Drawing.Bitmap -ArgumentList @([int]$dw, [int]$dh)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$destRect = New-Object System.Drawing.Rectangle -ArgumentList @(0, 0, [int]$dw, [int]$dh)
$srcRect = New-Object System.Drawing.Rectangle -ArgumentList @([int]$X, [int]$Y, [int]$W, [int]$H)
$g.DrawImage($src, $destRect, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)
$g.Dispose()
$src.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "saved $Out"
