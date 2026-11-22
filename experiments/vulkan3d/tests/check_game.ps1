#requires -Version 5.1
param(
    [Parameter(Mandatory=$true)][string]$StartPath,
    [Parameter(Mandatory=$true)][string]$MotionPath
)
$ErrorActionPreference = "Stop"
function Read-Capture([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 32 -or $bytes.Length -gt 64MB) { throw "Invalid game capture size." }
    $header = [regex]::Match([Text.Encoding]::ASCII.GetString($bytes, 0, 32), '\AP6\n1280 720\n255\n')
    if (-not $header.Success -or $bytes.Length -ne $header.Length + 1280*720*3) {
        throw "Expected a 1280x720 RGB game capture."
    }
    return @{ Bytes = $bytes; Offset = $header.Length }
}
function Count-Player($Image, [int]$Left, [int]$Right) {
    $count = 0
    for ($y = 296*2; $y -lt 319*2; ++$y) {
        for ($x = $Left*2; $x -lt $Right*2; ++$x) {
            $index = $Image.Offset + ($y*1280+$x)*3
            $r = [int]$Image.Bytes[$index]; $g = [int]$Image.Bytes[$index+1]; $b = [int]$Image.Bytes[$index+2]
            if ([Math]::Abs($r-105) -le 3 -and [Math]::Abs($g-163) -le 3 -and [Math]::Abs($b-172) -le 3) {
                ++$count
            }
        }
    }
    return $count
}
$start = Read-Capture $StartPath
$moved = Read-Capture $MotionPath
$startNear = Count-Player $start 30 100
$endNear = Count-Player $moved 30 100
$endFar = Count-Player $moved 130 300
if ($startNear -lt 800 -or $endNear -gt 20 -or $endFar -lt 800) {
    throw "Vulkan gameplay motion failed: startNear=$startNear endNear=$endNear endFar=$endFar"
}
Write-Output "GPU readback PASS: actual gameplay player moved (start=$startNear final=$endFar pixels)."
