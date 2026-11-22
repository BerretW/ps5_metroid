#requires -Version 5.1
param(
    [Parameter(Mandatory=$true)][string]$PoweredPath,
    [Parameter(Mandatory=$true)][string]$RestoredPath
)
$ErrorActionPreference = "Stop"

function Read-Capture([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -gt 64MB -or $bytes.Length -lt 32) {
        throw "Cargo lighting GPU capture size is invalid: $Path"
    }
    $header = [regex]::Match([Text.Encoding]::ASCII.GetString($bytes, 0, 32),
        '\AP6\n(\d+) (\d+)\n255\n')
    if (-not $header.Success -or [int]$header.Groups[1].Value -ne 1280 -or
        [int]$header.Groups[2].Value -ne 720 -or
        $bytes.Length -ne $header.Length + 1280 * 720 * 3) {
        throw "Expected a complete 1280x720 Cargo Hold GPU capture: $Path"
    }
    return @{ Bytes = $bytes; Offset = $header.Length }
}

$powered = Read-Capture $PoweredPath
$restored = Read-Capture $RestoredPath
$poweredBytes = $powered.Bytes
$restoredBytes = $restored.Bytes
$changedWorldPixels = 0
$poweredLampLuma = 0.0
$restoredLampLuma = 0.0
$lampPixelCount = 0
$emissivePixels = 0
$emissiveGreen = 0.0
$emissiveBlue = 0.0

for ($y = 0; $y -lt 720; $y++) {
    for ($x = 0; $x -lt 1280; $x++) {
        $index = $powered.Offset + (($y * 1280 + $x) * 3)
        $pr = [int]$poweredBytes[$index]
        $pg = [int]$poweredBytes[$index + 1]
        $pb = [int]$poweredBytes[$index + 2]
        $rr = [int]$restoredBytes[$index]
        $rg = [int]$restoredBytes[$index + 1]
        $rb = [int]$restoredBytes[$index + 2]
        if ($y -lt 600 -and
            ([Math]::Abs($pr - $rr) + [Math]::Abs($pg - $rg) +
             [Math]::Abs($pb - $rb)) -ge 18) {
            $changedWorldPixels++
        }
        # The console is at [384,224] in Cargo Hold world space and projects
        # into this fixed camera region. Its emergency-circuit emissive mask
        # must remain cyan after restoring the main-circuit outage.
        if ($x -ge 20 -and $x -le 220 -and $y -ge 150 -and $y -le 340 -and
            $rg -gt ($rr * 1.35) -and $rb -gt ($rr * 1.35) -and
            $rg -gt 24 -and $rb -gt 24) {
            $emissivePixels++
            $emissiveGreen += $rg
            $emissiveBlue += $rb
        }
        # The main overhead spot is centered near logical screen (118,71).
        if ($x -ge 150 -and $x -le 340 -and $y -ge 55 -and $y -le 245) {
            $poweredLampLuma += 0.2126 * $pr + 0.7152 * $pg + 0.0722 * $pb
            $restoredLampLuma += 0.2126 * $rr + 0.7152 * $rg + 0.0722 * $rb
            $lampPixelCount++
        }
    }
}

if ($changedWorldPixels -lt 100) {
    throw "Restored Cargo main-power outage did not change world pixels: $changedWorldPixels"
}
$poweredLampLuma /= $lampPixelCount
$restoredLampLuma /= $lampPixelCount
if ($poweredLampLuma -le ($restoredLampLuma + 0.5)) {
    throw "Disabling the Cargo main spot did not darken its projected region: powered=$poweredLampLuma restored=$restoredLampLuma"
}
if ($emissivePixels -lt 4 -or
    ($emissiveGreen / $emissivePixels) -lt 35 -or
    ($emissiveBlue / $emissivePixels) -lt 35) {
    throw "Cargo emergency-circuit emissive display did not remain visible after save/restore: pixels=$emissivePixels"
}
Write-Output ("Cargo Hold Vulkan lighting readback PASS: restored power outage darkened the main spot " +
    "while the emergency-circuit emissive display remained visible ({0} changed world pixels)." -f
    $changedWorldPixels)
