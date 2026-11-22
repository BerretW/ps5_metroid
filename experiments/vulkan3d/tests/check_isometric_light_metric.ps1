#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$ImagePath)
$ErrorActionPreference = "Stop"
$bytes = [IO.File]::ReadAllBytes($ImagePath)
if ($bytes.Length -gt 64MB -or $bytes.Length -lt 32) {
    throw "Isometric light metric GPU capture size is invalid."
}
$header = [regex]::Match([Text.Encoding]::ASCII.GetString($bytes, 0, 32),
    '\AP6\n(\d+) (\d+)\n255\n')
if (-not $header.Success -or [int]$header.Groups[1].Value -ne 1280 -or
    [int]$header.Groups[2].Value -ne 720 -or
    $bytes.Length -ne $header.Length + 1280 * 720 * 3) {
    throw "Expected a complete 1280x720 isometric light metric GPU capture."
}
function Get-Pixel([int]$X, [int]$Y) {
    $index = $header.Length + ((2 * $Y + 1) * 1280 + (2 * $X + 1)) * 3
    return @([int]$bytes[$index], [int]$bytes[$index + 1], [int]$bytes[$index + 2])
}
$center = Get-Pixel 320 180
$projectedWorldNear = Get-Pixel 344 180
$projectedWorldOutside = Get-Pixel 320 216
$unlitSurface = Get-Pixel 100 100
$spotWorldInside = Get-Pixel 504 201
$spotWorldOutside = Get-Pixel 489 200
if (($center | Measure-Object -Maximum).Maximum -lt 100 -or
    ($projectedWorldNear | Measure-Object -Maximum).Maximum -lt
        ($unlitSurface | Measure-Object -Maximum).Maximum + 25) {
    throw "Isometric light did not illuminate its center and projected world-space interior: center=$center near=$projectedWorldNear"
}
if ([Math]::Abs(($projectedWorldOutside | Measure-Object -Maximum).Maximum -
                ($unlitSurface | Measure-Object -Maximum).Maximum) -gt 2 -or
    ($projectedWorldNear | Measure-Object -Maximum).Maximum -le
        ($projectedWorldOutside | Measure-Object -Maximum).Maximum + 25) {
    throw "Isometric point-light attenuation used a screen-space circle instead of the projected world-space ellipse: near=$projectedWorldNear outside=$projectedWorldOutside"
}
if (($spotWorldInside | Measure-Object -Maximum).Maximum -lt
        ($unlitSurface | Measure-Object -Maximum).Maximum + 25 -or
    ($spotWorldOutside | Measure-Object -Maximum).Maximum -gt
        ($unlitSurface | Measure-Object -Maximum).Maximum + 2) {
    throw "Isometric spotlight cone did not follow its world-space direction: inside=$spotWorldInside outside=$spotWorldOutside"
}
Write-Output "Isometric Vulkan light metric readback PASS: point attenuation and spotlight cone follow world-space distances and directions after projection."
