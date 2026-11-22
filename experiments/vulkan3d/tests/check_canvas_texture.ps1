#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$ImagePath)
$ErrorActionPreference = "Stop"
$bytes = [IO.File]::ReadAllBytes($ImagePath)
$header = [regex]::Match([Text.Encoding]::ASCII.GetString($bytes, 0, [Math]::Min(32, $bytes.Length)),
    '\AP6\n(\d+) (\d+)\n255\n')
if (-not $header.Success -or [int]$header.Groups[1].Value -ne 1280 -or
    [int]$header.Groups[2].Value -ne 720 -or
    $bytes.Length -ne $header.Length + 1280 * 720 * 3) {
    throw "Expected a complete 1280x720 Vulkan canvas capture."
}
$index = $header.Length + ((60 * 2) * 1280 + (60 * 2)) * 3
$red = [int]$bytes[$index]
$green = [int]$bytes[$index + 1]
$blue = [int]$bytes[$index + 2]
if ($red -lt 245 -or $green -gt 8 -or $blue -lt 245) {
    throw "Textured canvas center pixel was not magenta: $red,$green,$blue"
}
function Read-Pixel([int]$X, [int]$Y) {
    $i = $header.Length + (($Y * 2) * 1280 + ($X * 2)) * 3
    return @([int]$bytes[$i], [int]$bytes[$i+1], [int]$bytes[$i+2])
}
$inside = Read-Pixel 160 105
$outside = Read-Pixel 190 80
if ($inside[0] -gt 8 -or $inside[1] -lt 245 -or $inside[2] -lt 245 -or
    ($outside[0] -lt 8 -and $outside[1] -gt 245 -and $outside[2] -gt 245)) {
    throw "Non-square rotation did not preserve logical dimensions: inside=$inside outside=$outside"
}
$spritePixels = 0
for ($y = 40; $y -lt 168; ++$y) {
    for ($x = 1000; $x -lt 1096; ++$x) {
        $spriteIndex = $header.Length + ($y * 1280 + $x) * 3
        $spriteR = [int]$bytes[$spriteIndex]
        $spriteG = [int]$bytes[$spriteIndex + 1]
        $spriteB = [int]$bytes[$spriteIndex + 2]
        if ($spriteR -gt 50 -and $spriteG -lt 25 -and $spriteB -gt 50) {
            ++$spritePixels
        }
    }
}
if ($spritePixels -lt 20) {
    throw "Original player atlas did not produce enough tinted pixels: $spritePixels"
}
Write-Output "Vulkan canvas texture, original player atlas, tint, rotation and GPU readback PASS."
