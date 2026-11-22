#requires -Version 5.1
param(
    [Parameter(Mandatory=$true)][string]$ImagePath,
    [Parameter(Mandatory=$true)][string]$FallbackPath
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
$sprite = Read-Capture $ImagePath
$fallback = Read-Capture $FallbackPath
$different = 0
for ($y = 292*2; $y -lt 318*2; ++$y) {
    for ($x = 30*2; $x -lt 100*2; ++$x) {
        $i = ($y*1280+$x)*3
        if ($sprite.Bytes[$sprite.Offset+$i] -ne $fallback.Bytes[$fallback.Offset+$i] -or
            $sprite.Bytes[$sprite.Offset+$i+1] -ne $fallback.Bytes[$fallback.Offset+$i+1] -or
            $sprite.Bytes[$sprite.Offset+$i+2] -ne $fallback.Bytes[$fallback.Offset+$i+2]) {
            ++$different
        }
    }
}
if ($different -lt 300) { throw "Game player sprite not visible: only $different pixels differ." }
Write-Output "GPU readback PASS: gameplay player sprite differs from fallback in $different pixels."
