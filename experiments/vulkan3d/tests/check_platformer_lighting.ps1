#requires -Version 5.1
param(
    [Parameter(Mandatory=$true)][string]$PoweredPath,
    [Parameter(Mandatory=$true)][string]$MovedPath,
    [Parameter(Mandatory=$true)][string]$ExpiredPath,
    [Parameter(Mandatory=$true)][string]$OutagePath
)
$ErrorActionPreference = "Stop"

function Read-Capture([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -gt 64MB -or $bytes.Length -lt 32) {
        throw "Platformer lighting GPU capture size is invalid: $Path"
    }
    $header = [regex]::Match([Text.Encoding]::ASCII.GetString($bytes, 0, 32),
        '\AP6\n(\d+) (\d+)\n255\n')
    if (-not $header.Success -or [int]$header.Groups[1].Value -ne 1280 -or
        [int]$header.Groups[2].Value -ne 720 -or
        $bytes.Length -ne $header.Length + 1280 * 720 * 3) {
        throw "Expected a complete 1280x720 platformer lighting capture: $Path"
    }
    return @{ Bytes = $bytes; Offset = $header.Length }
}

function Get-Region([hashtable]$Capture, [int]$Left, [int]$Top,
                    [int]$Right, [int]$Bottom) {
    $sumLuma = 0.0
    $sumBlueExcess = 0.0
    $count = 0
    for ($y = $Top; $y -lt $Bottom; $y++) {
        for ($x = $Left; $x -lt $Right; $x++) {
            $index = $Capture.Offset + ((2 * $y * 1280 + 2 * $x) * 3)
            $r = [int]$Capture.Bytes[$index]
            $g = [int]$Capture.Bytes[$index + 1]
            $b = [int]$Capture.Bytes[$index + 2]
            $sumLuma += 0.2126 * $r + 0.7152 * $g + 0.0722 * $b
            $sumBlueExcess += [Math]::Max(0, $b - [Math]::Max($r, $g))
            $count++
        }
    }
    return @{
        Luma = $sumLuma / $count
        BlueExcess = $sumBlueExcess / $count
    }
}

$powered = Read-Capture $PoweredPath
$moved = Read-Capture $MovedPath
$expired = Read-Capture $ExpiredPath
$outage = Read-Capture $OutagePath
$lampOn = Get-Region $powered 270 250 370 330
$lampOff = Get-Region $outage 270 250 370 330
$projectileSourceBefore = Get-Region $powered 302 272 318 288
$projectileSourceAfter = Get-Region $moved 302 272 318 288
$projectileDestinationBefore = Get-Region $powered 212 282 228 298
$projectileDestinationAfter = Get-Region $moved 212 282 228 298
$projectileDestinationExpired = Get-Region $expired 212 282 228 298
$emergencyDisplay = Get-Region $outage 340 275 400 320

if ($lampOn.Luma -le ($lampOff.Luma + 5.0)) {
    throw "Power-panel outage did not darken the player region: on=$($lampOn.Luma) off=$($lampOff.Luma)"
}
if ($emergencyDisplay.BlueExcess -lt 2.0) {
    throw "Independent emergency-circuit emissive indicator was not visible during the outage: blueExcess=$($emergencyDisplay.BlueExcess)"
}
if ($projectileSourceAfter.BlueExcess -ge ($projectileSourceBefore.BlueExcess - 3.0) -or
    $projectileDestinationAfter.BlueExcess -le ($projectileDestinationBefore.BlueExcess + 5.0)) {
    throw ("Projectile light did not move between its fixed-step positions: " +
        "source=$($projectileSourceBefore.BlueExcess)->$($projectileSourceAfter.BlueExcess), " +
        "destination=$($projectileDestinationBefore.BlueExcess)->$($projectileDestinationAfter.BlueExcess)")
}
if ($projectileDestinationExpired.BlueExcess -gt ($projectileDestinationBefore.BlueExcess + 5.0)) {
    throw ("Expired projectile light remained at its last GPU position: " +
        "baseline=$($projectileDestinationBefore.BlueExcess) expired=$($projectileDestinationExpired.BlueExcess)")
}
Write-Output ("Landing Bay Vulkan lighting PASS: switch outage darkens world, backup indicator remains emissive, " +
    "and projectile light moves then expires (lamp {0:F1}->{1:F1}, source blue {2:F1}->{3:F1}, " +
    "destination blue {4:F1}->{5:F1}->{6:F1})." -f
    $lampOn.Luma, $lampOff.Luma, $projectileSourceBefore.BlueExcess,
    $projectileSourceAfter.BlueExcess, $projectileDestinationBefore.BlueExcess,
    $projectileDestinationAfter.BlueExcess, $projectileDestinationExpired.BlueExcess)
