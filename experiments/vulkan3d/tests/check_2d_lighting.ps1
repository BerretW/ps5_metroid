#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$ImagePath)
$ErrorActionPreference = "Stop"
$bytes = [IO.File]::ReadAllBytes($ImagePath)
if ($bytes.Length -gt 64MB -or $bytes.Length -lt 32) {
    throw "2D lighting GPU capture size is invalid."
}
$header = [regex]::Match([Text.Encoding]::ASCII.GetString($bytes, 0, 32),
    '\AP6\n(\d+) (\d+)\n255\n')
if (-not $header.Success -or [int]$header.Groups[1].Value -ne 1280 -or
    [int]$header.Groups[2].Value -ne 720 -or
    $bytes.Length -ne $header.Length + 1280 * 720 * 3) {
    throw "Expected a complete 1280x720 2D lighting GPU capture."
}
function Get-Pixel([int]$X, [int]$Y) {
    $index = $header.Length + ((2 * $Y + 1) * 1280 + (2 * $X + 1)) * 3
    return @([int]$bytes[$index], [int]$bytes[$index + 1], [int]$bytes[$index + 2])
}
$side = Get-Pixel 90 80
$cameraMoved = Get-Pixel 230 80
$topDown = Get-Pixel 320 80
$spot = Get-Pixel 430 80
$spotOutsideCone = Get-Pixel 400 110
$attenuated = Get-Pixel 110 80
$edge = Get-Pixel 138 80
$blackout = Get-Pixel 240 160
$flatNormal = Get-Pixel 230 210
$litNormal = Get-Pixel 310 210
$emissive = Get-Pixel 410 210
$ui = Get-Pixel 580 310
if ($side[0] -lt 180 -or $side[1] -gt 30 -or $side[2] -gt 30 -or
    $cameraMoved[0] -lt 180 -or $cameraMoved[1] -gt 30 -or $cameraMoved[2] -gt 30) {
    throw "Side-view point-light positions or camera-following failed: side=$side moved=$cameraMoved"
}
if ([Math]::Abs($side[0] - $cameraMoved[0]) -gt 8) {
    throw "Camera motion changed the same world light's brightness: side=$side moved=$cameraMoved"
}
if ($topDown[1] -lt 180 -or $topDown[0] -gt 30 -or $topDown[2] -gt 30) {
    throw "Top-down point light did not illuminate its projected surface: $topDown"
}
if ($spot[2] -lt 55 -or $spot[0] -gt 30 -or $spotOutsideCone[2] -gt 12) {
    throw "Isometric spotlight cone readback failed: inside=$spot outside=$spotOutsideCone"
}
if ($side[0] -le $attenuated[0] -or $attenuated[0] -le $edge[0] -or $edge[0] -gt 12) {
    throw "Point-light attenuation or radius boundary failed: center=$side half=$attenuated edge=$edge"
}
if ($blackout[0] -gt 8 -or $blackout[1] -gt 8 -or $blackout[2] -gt 8) {
    throw "Zero ambient left non-emissive world geometry lit: $blackout"
}
if ($flatNormal[0] -gt 8 -or $litNormal[0] -lt 180) {
    throw "2D normal-map directional response failed: flat=$flatNormal oriented=$litNormal"
}
if ($emissive[1] -lt 180 -or $emissive[0] -gt 8 -or $emissive[2] -gt 8) {
    throw "Emissive mask did not remain visible independently of world lighting: $emissive"
}
if ($ui[0] -lt 245 -or $ui[1] -lt 245 -or $ui[2] -lt 245) {
    throw "World darkness affected the UI overlay: $ui"
}
Write-Output "2D Vulkan lighting readback PASS: three projections, point attenuation, spotlight, directional normal maps, emissive masks, camera invariance, blackout and UI isolation."
