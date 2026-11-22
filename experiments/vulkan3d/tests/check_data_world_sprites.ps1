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
$world = Read-Capture $ImagePath
$fallback = Read-Capture $FallbackPath
$different = 0
for ($y = 120*2; $y -lt 330*2; ++$y) {
    for ($x = 0; $x -lt 640*2; ++$x) {
        $i = ($y*1280+$x)*3
        if ($world.Bytes[$world.Offset+$i] -ne $fallback.Bytes[$fallback.Offset+$i] -or
            $world.Bytes[$world.Offset+$i+1] -ne $fallback.Bytes[$fallback.Offset+$i+1] -or
            $world.Bytes[$world.Offset+$i+2] -ne $fallback.Bytes[$fallback.Offset+$i+2]) {
            ++$different
        }
    }
}
if ($different -lt 5000) { throw "JSON world sprites not visible: only $different pixels differ." }
Write-Output "GPU readback PASS: JSON world rendering differs from built-in fallback in $different pixels."
