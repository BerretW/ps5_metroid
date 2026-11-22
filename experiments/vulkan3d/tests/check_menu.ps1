#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$ImagePath)
$ErrorActionPreference = "Stop"
$bytes = [IO.File]::ReadAllBytes($ImagePath)
if ($bytes.Length -lt 32 -or $bytes.Length -gt 64MB) { throw "Invalid menu capture size." }
$header = [regex]::Match([Text.Encoding]::ASCII.GetString($bytes, 0, 32), '\AP6\n(\d+) (\d+)\n255\n')
if (-not $header.Success -or [int]$header.Groups[1].Value -ne 1280 -or
    [int]$header.Groups[2].Value -ne 720 -or $bytes.Length -ne $header.Length + 1280*720*3) {
    throw "Expected a 1280x720 RGB menu capture."
}
function Check-Pixel([int]$X, [int]$Y, [int]$R, [int]$G, [int]$B, [string]$Name) {
    $index = $header.Length + (($Y * 2) * 1280 + ($X * 2)) * 3
    $actual = @([int]$bytes[$index], [int]$bytes[$index+1], [int]$bytes[$index+2])
    if ([Math]::Abs($actual[0]-$R) -gt 3 -or [Math]::Abs($actual[1]-$G) -gt 3 -or
        [Math]::Abs($actual[2]-$B) -gt 3) {
        throw "$Name pixel differs: $($actual -join ',')"
    }
}
Check-Pixel 20 20 7 12 22 "background"
Check-Pixel 80 70 17 29 45 "panel"
Check-Pixel 80 63 61 227 216 "panel accent"
Check-Pixel 120 94 220 234 239 "title glyph"
Check-Pixel 120 168 29 48 65 "selected row"
Check-Pixel 112 164 61 227 216 "selection marker"
Check-Pixel 128 169 61 227 216 "selected glyph"
Check-Pixel 130 196 115 145 158 "disabled Continue glyph"
Write-Output "GPU readback PASS: actual Echo menu background, panel, bitmap text and selection."
