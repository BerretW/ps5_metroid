#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$ImagePath)
$ErrorActionPreference = "Stop"
$bytes = [IO.File]::ReadAllBytes($ImagePath)
if ($bytes.Length -gt 64MB -or $bytes.Length -lt 32) {
    throw "2D shadow GPU capture size is invalid."
}
$header = [regex]::Match([Text.Encoding]::ASCII.GetString($bytes, 0, 32),
    '\AP6\n(\d+) (\d+)\n255\n')
if (-not $header.Success -or [int]$header.Groups[1].Value -ne 1280 -or
    [int]$header.Groups[2].Value -ne 720 -or
    $bytes.Length -ne $header.Length + 1280 * 720 * 3) {
    throw "Expected a complete 1280x720 2D shadow GPU capture."
}
function Get-Pixel([int]$X, [int]$Y) {
    $index = $header.Length + ((2 * $Y + 1) * 1280 + (2 * $X + 1)) * 3
    return @([int]$bytes[$index], [int]$bytes[$index + 1], [int]$bytes[$index + 2])
}
$litSide = Get-Pixel 220 180
$shadowSide = Get-Pixel 300 180
$unlitSurface = Get-Pixel 10 350
$ui = Get-Pixel 580 310
$litLevel = ($litSide | Measure-Object -Maximum).Maximum
$shadowLevel = ($shadowSide | Measure-Object -Maximum).Maximum
$ambientLevel = ($unlitSurface | Measure-Object -Maximum).Maximum
$uiLevel = ($ui | Measure-Object -Minimum).Minimum
if ($litLevel -lt $ambientLevel + 50 -or
    $shadowLevel -ge $litLevel - 100 -or
    $shadowLevel -gt $ambientLevel + 12) {
    throw "GPU shadow did not block point light on the far side of the occluder: lit=$litSide shadow=$shadowSide ambient=$unlitSurface"
}
if ($uiLevel -lt 240) {
    throw "World shadowing changed the unaffected UI pass: ui=$ui"
}
Write-Output "2D Vulkan shadow readback PASS: occluder blocks point light and UI remains unaffected."
