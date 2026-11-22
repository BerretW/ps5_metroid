#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$ImagePath)
$ErrorActionPreference = "Stop"
$bytes = [IO.File]::ReadAllBytes($ImagePath)
if ($bytes.Length -gt 64MB -or $bytes.Length -lt 32) { throw "GPU capture size is invalid." }
$header = [regex]::Match([Text.Encoding]::ASCII.GetString($bytes,0,32), '\AP6\n(\d+) (\d+)\n255\n')
if (-not $header.Success) { throw "GPU capture must be a P6 RGB image." }
$width = [int]$header.Groups[1].Value
$height = [int]$header.Groups[2].Value
if ($width -ne 960 -or $height -ne 540 -or $bytes.Length -ne $header.Length + $width*$height*3) {
    throw "Resize capture must be exactly 960x540 RGB."
}
function Count-Region([double]$X0,[double]$Y0,[double]$X1,[double]$Y1,[string]$Kind) {
    $count = 0
    for ($y = [int]($Y0*$height); $y -lt [int]($Y1*$height); ++$y) {
        for ($x = [int]($X0*$width); $x -lt [int]($X1*$width); ++$x) {
            $index = $header.Length + ($y*$width+$x)*3
            $r = [int]$bytes[$index]; $g = [int]$bytes[$index+1]; $b = [int]$bytes[$index+2]
            if (($Kind -eq "warm" -and $r -gt 40 -and $r -gt $b*1.2 -and $r -gt $g*1.05) -or
                ($Kind -eq "blue" -and $b -gt 50 -and $b -gt $r*1.2) -or
                ($Kind -eq "hud" -and $g -gt 140 -and $g -gt $r*1.2) -or
                ($Kind -eq "sprite" -and $g -gt 80 -and $g -gt $r*1.15 -and $b -gt 80)) {
                ++$count
            }
        }
    }
    return $count
}
$cubeWarm = Count-Region 0.43 0.4 0.57 0.63 "warm"
$cubeBlue = Count-Region 0.43 0.4 0.57 0.63 "blue"
$floorBlue = Count-Region 0.1 0.8 0.9 0.98 "blue"
$hud = Count-Region 0.02 0.02 0.18 0.18 "hud"
$sprite = Count-Region 0.87 0.03 0.96 0.24 "sprite"
if ($cubeWarm -lt 100 -or $cubeBlue -lt 100 -or $floorBlue -lt 200 -or $hud -lt 50 -or $sprite -lt 100) {
    throw "GPU scene reference regions failed: cubeWarm=$cubeWarm cubeBlue=$cubeBlue floor=$floorBlue hud=$hud sprite=$sprite"
}
function Check-AtlasPixel([double]$X, [double]$Y, [string]$Expected) {
    $xPixel = [int]($X * $width)
    $yPixel = [int]($Y * $height)
    $index = $header.Length + ($yPixel * $width + $xPixel) * 3
    $r = [int]$bytes[$index]; $g = [int]$bytes[$index+1]; $b = [int]$bytes[$index+2]
    $ok = switch ($Expected) {
        "red" { $r -gt 220 -and $g -lt 30 -and $b -lt 30 }
        "green" { $r -lt 30 -and $g -gt 220 -and $b -lt 30 }
        "blue" { $r -lt 30 -and $g -lt 30 -and $b -gt 220 }
        "yellow" { $r -gt 220 -and $g -gt 220 -and $b -lt 30 }
    }
    if (-not $ok) { throw "Atlas $Expected pixel failed at $xPixel,$yPixel`: $r,$g,$b" }
}
Check-AtlasPixel 0.085 0.79 "red"
Check-AtlasPixel 0.165 0.79 "green"
Check-AtlasPixel 0.335 0.79 "green"
Check-AtlasPixel 0.415 0.79 "red"
Check-AtlasPixel 0.585 0.79 "blue"
Check-AtlasPixel 0.665 0.79 "yellow"
Check-AtlasPixel 0.835 0.79 "yellow"
Check-AtlasPixel 0.915 0.79 "blue"
Check-AtlasPixel 0.48 0.94 "red"
Check-AtlasPixel 0.48 0.96 "blue"
Check-AtlasPixel 0.125 0.95 "blue"
Check-AtlasPixel 0.275 0.95 "yellow"
Write-Output "GPU reference regions PASS: scene and cropped/mirrored sprite atlas at 960x540."
