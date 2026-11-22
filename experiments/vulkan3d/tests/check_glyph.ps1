#requires -Version 5.1
param(
    [Parameter(Mandatory=$true)][string]$Flat,
    [Parameter(Mandatory=$true)][string]$Tilted
)
$ErrorActionPreference = "Stop"
$first = [IO.File]::ReadAllBytes($Flat)
$second = [IO.File]::ReadAllBytes($Tilted)
$header = [regex]::Match([Text.Encoding]::ASCII.GetString($first, 0, 32), '\AP6\n1280 720\n255\n')
if (-not $header.Success -or $first.Length -ne $header.Length + 1280 * 720 * 3 -or
    $second.Length -ne $first.Length) {
    throw "Invalid glyph GPU captures."
}
$changed = 0
$outside = 0
for ($y = 0; $y -lt 720; ++$y) {
    for ($x = 0; $x -lt 1280; ++$x) {
        $i = $header.Length + ($y * 1280 + $x) * 3
        $delta = [Math]::Max([Math]::Abs([int]$first[$i] - [int]$second[$i]),
            [Math]::Max([Math]::Abs([int]$first[$i + 1] - [int]$second[$i + 1]),
                        [Math]::Abs([int]$first[$i + 2] - [int]$second[$i + 2])))
        if ($delta -ge 8) {
            if ($x -ge 732 -and $x -lt 1080 -and $y -ge 320 -and $y -lt 600) {
                ++$changed
            } else {
                ++$outside
            }
        }
    }
}
if ($changed -lt 1000 -or $outside -ne 0) {
    throw "Normal map did not change the glyph inside its menu window: changed=$changed outside=$outside"
}
Write-Output "Embedded glTF normal-map GPU difference PASS: $changed pixels."
