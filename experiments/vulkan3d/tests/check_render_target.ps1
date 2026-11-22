#requires -Version 5.1
param(
    [Parameter(Mandatory=$true)][string]$ImagePath,
    [Parameter(Mandatory=$true)][string]$SampleImagePath
)
$ErrorActionPreference = "Stop"
$bytes = [IO.File]::ReadAllBytes($ImagePath)
if ($bytes.Length -gt 1MB -or $bytes.Length -lt 32) { throw "Render-target capture size is invalid." }
$header = [regex]::Match([Text.Encoding]::ASCII.GetString($bytes,0,32), '\AP6\n(\d+) (\d+)\n255\n')
if (-not $header.Success) { throw "Render-target capture must be a P6 RGB image." }
$width = [int]$header.Groups[1].Value
$height = [int]$header.Groups[2].Value
if ($width -ne 128 -or $height -ne 128 -or $bytes.Length -ne $header.Length + $width*$height*3) {
    throw "Render-target capture must be exactly 128x128 RGB."
}
function Get-Pixel([int]$X,[int]$Y) {
    $index = $header.Length + ($Y*$width+$X)*3
    return @([int]$bytes[$index],[int]$bytes[$index+1],[int]$bytes[$index+2])
}
$center = Get-Pixel 64 64
$background = Get-Pixel 4 4
if ($center[0] -gt 40 -or $center[1] -gt 40 -or $center[2] -lt 200) {
    throw "Nearest opaque draw did not win the depth test at the target center: $($center -join ',')."
}
if ($background[0] -gt 80 -or $background[1] -gt 80 -or $background[2] -gt 80) {
    throw "Render-target clear color was not preserved outside geometry: $($background -join ',')."
}
$sampleBytes = [IO.File]::ReadAllBytes($SampleImagePath)
if ($sampleBytes.Length -gt 1MB -or $sampleBytes.Length -lt 32) { throw "Render-target sample capture size is invalid." }
$sampleHeader = [regex]::Match([Text.Encoding]::ASCII.GetString($sampleBytes,0,32), '\AP6\n(\d+) (\d+)\n255\n')
if (-not $sampleHeader.Success) { throw "Render-target sample capture must be a P6 RGB image." }
$sampleWidth = [int]$sampleHeader.Groups[1].Value
$sampleHeight = [int]$sampleHeader.Groups[2].Value
if ($sampleWidth -ne 128 -or $sampleHeight -ne 128 -or
    $sampleBytes.Length -ne $sampleHeader.Length + $sampleWidth*$sampleHeight*3) {
    throw "Render-target sample capture must be exactly 128x128 RGB."
}
$sampleCenterOffset = $sampleHeader.Length + (64*$sampleWidth+64)*3
$sampleCenter = @(
    [int]$sampleBytes[$sampleCenterOffset],
    [int]$sampleBytes[$sampleCenterOffset+1],
    [int]$sampleBytes[$sampleCenterOffset+2]
)
if ($sampleCenter[0] -gt 40 -or $sampleCenter[1] -gt 40 -or $sampleCenter[2] -lt 200) {
    throw "Sampling the completed target did not preserve its center color: $($sampleCenter -join ',')."
}
$sampleBackgroundOffset = $sampleHeader.Length + (4*$sampleWidth+4)*3
$sampleBackground = @(
    [int]$sampleBytes[$sampleBackgroundOffset],
    [int]$sampleBytes[$sampleBackgroundOffset+1],
    [int]$sampleBytes[$sampleBackgroundOffset+2]
)
if ($sampleBackground[0] -gt 80 -or $sampleBackground[1] -gt 80 -or $sampleBackground[2] -gt 80) {
    throw "Composited target clear color was not preserved outside geometry: $($sampleBackground -join ',')."
}
Write-Output "GPU render-target color, depth ordering, sampling, and readback PASS."
