#requires -Version 5.1
<#
.SYNOPSIS
    Regenerates PROJECT ECHO's original PNG sprites and WAV sound effects.
.DESCRIPTION
    All assets are drawn and synthesized procedurally by this script, so the repository
    contains no third-party art or audio. Output is written to assets\sprites and assets\sounds.
    Sounds are 48 kHz, mono, 16-bit PCM to match the runtime audio device without resampling.
#>
param(
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root "assets" }
$spriteDir = Join-Path $OutputDirectory "sprites"
$soundDir = Join-Path $OutputDirectory "sounds"
New-Item -ItemType Directory -Force -Path $spriteDir, $soundDir | Out-Null
Add-Type -AssemblyName System.Drawing

function New-Color([int]$r, [int]$g, [int]$b, [int]$a = 255) { [Drawing.Color]::FromArgb($a, $r, $g, $b) }

$palette = @{
    Helmet    = New-Color 174 211 207
    Suit      = New-Color 105 163 172
    SuitDark  = New-Color 62 106 115
    Boot      = New-Color 35 52 65
    Cyan      = New-Color 61 227 216
    CyanDark  = New-Color 23 120 128
    White     = New-Color 220 234 239
    Orange    = New-Color 255 164 83
    Yellow    = New-Color 255 226 140
    Red       = New-Color 238 82 92
    Shell     = New-Color 135 80 99
    ShellLite = New-Color 202 111 121
    ShellDark = New-Color 88 48 66
    Stone     = New-Color 37 61 74
    StoneLite = New-Color 62 106 115
    StoneDark = New-Color 26 44 56
    Rivet     = New-Color 51 79 91
    Lava      = New-Color 121 43 51
    Door      = New-Color 32 63 76
    DoorDark  = New-Color 19 39 49
}

function Save-Sprite([string]$name, [int]$width, [int]$height, [scriptblock]$draw) {
    $bitmap = New-Object Drawing.Bitmap($width, $height, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.Clear([Drawing.Color]::Transparent)
        $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::None
        $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::Half
        $brushes = @{}
        $fill = {
            param([Drawing.Color]$color, [int]$x, [int]$y, [int]$w, [int]$h)
            $key = $color.ToArgb()
            if (-not $brushes.ContainsKey($key)) { $brushes[$key] = New-Object Drawing.SolidBrush($color) }
            $graphics.FillRectangle($brushes[$key], $x, $y, $w, $h)
        }.GetNewClosure()
        & $draw $fill
        $path = Join-Path $spriteDir $name
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
        $bitmap.Save($path, [Drawing.Imaging.ImageFormat]::Png)
        foreach ($brush in $brushes.Values) { $brush.Dispose() }
        Write-Host "sprite $name ${width}x${height}"
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

# Player sheet: 8 frames of 24x32 facing right: idle0, idle1, run0..run3, jump, fall.
Save-Sprite "player.png" 192 32 {
    param($fill)
    $legs = @(
        @(0, 0, 0, 0), @(0, 0, 0, 0),
        @(-2, 0, 2, -1), @(0, -1, 0, 0), @(2, -1, -2, 0), @(0, 0, 0, -1),
        @(-1, -3, 2, -2), @(-3, 0, 3, 0)
    )
    for ($frame = 0; $frame -lt 8; $frame++) {
        $ox = $frame * 24
        $bob = if ($frame -eq 1 -or $frame -eq 3 -or $frame -eq 5) { 1 } else { 0 }
        $leg = $legs[$frame]
        # Legs and boots (back leg first so the front leg overlaps it).
        & $fill $palette.SuitDark ($ox + 13 + $leg[2]) (22 + $leg[3]) 4 7
        & $fill $palette.Boot ($ox + 12 + $leg[2]) (28 + $leg[3]) 6 3
        & $fill $palette.Suit ($ox + 6 + $leg[0]) (22 + $leg[1]) 4 7
        & $fill $palette.Boot ($ox + 5 + $leg[0]) (28 + $leg[1]) 6 3
        # Torso, shoulder pad and arm cannon.
        & $fill $palette.Suit ($ox + 5) (11 + $bob) 13 12
        & $fill $palette.SuitDark ($ox + 5) (20 + $bob) 13 3
        & $fill $palette.Helmet ($ox + 3) (11 + $bob) 6 6
        & $fill $palette.CyanDark ($ox + 9) (14 + $bob) 4 3
        & $fill $palette.Cyan ($ox + 10) (15 + $bob) 2 1
        & $fill $palette.SuitDark ($ox + 14) (15 + $bob) 9 5
        & $fill $palette.Helmet ($ox + 20) (16 + $bob) 3 3
        # Helmet with visor.
        & $fill $palette.Helmet ($ox + 6) (1 + $bob) 12 10
        & $fill $palette.SuitDark ($ox + 6) (9 + $bob) 12 2
        & $fill $palette.Cyan ($ox + 12) (4 + $bob) 6 3
        & $fill $palette.White ($ox + 13) (4 + $bob) 2 1
    }
}

# Enemy: 2 frames of 32x32, a crawling maintenance drone.
Save-Sprite "enemy.png" 64 32 {
    param($fill)
    for ($frame = 0; $frame -lt 2; $frame++) {
        $ox = $frame * 32
        $step = if ($frame -eq 0) { 0 } else { 2 }
        foreach ($legX in @(4, 12, 20, 26)) {
            & $fill $palette.ShellDark ($ox + $legX + $step - 1) 26 3 6
        }
        & $fill $palette.Shell ($ox + 1) 14 30 14
        & $fill $palette.ShellDark ($ox + 1) 24 30 4
        & $fill $palette.ShellLite ($ox + 5) 6 22 12
        & $fill $palette.Shell ($ox + 5) 15 22 3
        & $fill $palette.Orange ($ox + 9) (10 - $frame) 4 4
        & $fill $palette.Orange ($ox + 19) (10 - $frame) 4 4
        & $fill $palette.Yellow ($ox + 10) (11 - $frame) 2 2
        & $fill $palette.Yellow ($ox + 20) (11 - $frame) 2 2
        & $fill $palette.ShellLite ($ox + 14) 2 4 5
    }
}

# Tiles: solid wall (0..31) and hazard (32..63).
Save-Sprite "tiles.png" 64 32 {
    param($fill)
    & $fill $palette.Stone 0 0 32 32
    & $fill $palette.StoneLite 0 0 32 3
    & $fill $palette.StoneDark 0 29 32 3
    & $fill $palette.StoneDark 15 3 2 26
    & $fill $palette.StoneDark 0 15 32 2
    foreach ($rivet in @(@(5, 7), @(24, 7), @(5, 21), @(24, 21))) {
        & $fill $palette.Rivet $rivet[0] $rivet[1] 3 3
        & $fill $palette.StoneLite $rivet[0] $rivet[1] 1 1
    }
    & $fill $palette.Lava 32 9 32 23
    & $fill $palette.Red 32 9 32 2
    foreach ($spike in @(@(35, 5, 5, 10), @(47, 1, 5, 14), @(57, 6, 4, 9))) {
        & $fill $palette.Orange $spike[0] $spike[1] $spike[2] $spike[3]
        & $fill $palette.Yellow ($spike[0] + 1) ($spike[1] + 1) 2 ($spike[3] - 4)
    }
    & $fill $palette.Orange 38 20 4 3
    & $fill $palette.Orange 52 24 5 3
}

# Projectile: 2 frames of 12x8 plasma bolt facing right.
Save-Sprite "projectile.png" 24 8 {
    param($fill)
    for ($frame = 0; $frame -lt 2; $frame++) {
        $ox = $frame * 12
        & $fill $palette.Red ($ox + 0) 3 4 2
        & $fill $palette.Orange ($ox + 2) (1 + $frame) 8 (6 - 2 * $frame)
        & $fill $palette.Yellow ($ox + 5) 2 6 4
        & $fill $palette.White ($ox + 8) 3 3 2
    }
}

# Ability pickup: 2 frames of 32x32 pulsing echo core.
Save-Sprite "pickup.png" 64 32 {
    param($fill)
    for ($frame = 0; $frame -lt 2; $frame++) {
        $ox = $frame * 32
        $grow = $frame * 2
        & $fill (New-Color 23 59 67 200) ($ox + 2 - $grow / 2) (2 - $grow / 2) (28 + $grow) (28 + $grow)
        & $fill $palette.CyanDark ($ox + 6) 6 20 20
        & $fill $palette.Cyan ($ox + 8) 8 16 16
        & $fill $palette.White ($ox + 12) 12 8 8
        & $fill $palette.Cyan ($ox + 14) 14 4 4
        & $fill $palette.White ($ox + 15) 2 2 4
        & $fill $palette.White ($ox + 15) 26 2 4
        & $fill $palette.White ($ox + 2) 15 4 2
        & $fill $palette.White ($ox + 26) 15 4 2
    }
}

# Laboratory door: 44x48 bulkhead.
Save-Sprite "door.png" 44 48 {
    param($fill)
    & $fill $palette.Door 0 0 44 48
    & $fill $palette.StoneLite 0 0 44 3
    & $fill $palette.DoorDark 5 6 34 38
    & $fill $palette.Door 21 6 2 38
    & $fill $palette.StoneDark 8 10 10 30
    & $fill $palette.StoneDark 26 10 10 30
    & $fill $palette.Cyan 32 22 3 3
    & $fill $palette.CyanDark 9 3 26 2
}

# ---------------------------------------------------------------------------
# World data sheets (assets/world/*.json reference these by path).
# ---------------------------------------------------------------------------

# Enemy sheets: 4x2 frames of 32x32. Row 0 = walk (4 frames), row 1 = idle (columns 0-1)
# and guard (columns 2-3). Bodies stand on the frame bottom and face right.
function Draw-Drone($fill, [int]$ox, [int]$oy, [int]$frame, [string]$kind, [string]$pose, $colors) {
    $step = if ($pose -eq "walk") { @(0, 2, 0, -2)[$frame] } else { 0 }
    $bob = if ($pose -eq "idle" -and $frame -eq 1) { 1 } else { 0 }
    if ($kind -eq "sentry") {
        & $fill $colors.Dark ($ox + 6) ($oy + 24) 20 8
        & $fill $colors.Shell ($ox + 9) ($oy + 12 + $bob) 14 13
        & $fill $colors.Lite ($ox + 9) ($oy + 12 + $bob) 14 3
        & $fill $colors.Dark ($ox + 21) ($oy + 16 + $bob) 10 4
        & $fill $colors.Eye ($ox + 13) ($oy + 17 + $bob) 5 4
        if ($frame -eq 0) { & $fill $palette.Yellow ($ox + 14) ($oy + 18 + $bob) 2 2 }
        & $fill $colors.Lite ($ox + 15) ($oy + 6 + $bob) 2 6
        & $fill $colors.Eye ($ox + 14) ($oy + 4 + $bob) 4 2
        return
    }
    $wide = if ($kind -eq "heavy" -or $kind -eq "armored") { 1 } else { 0 }
    foreach ($legX in @(4, 12, 20, 26)) {
        $lift = if (($legX -eq 4 -or $legX -eq 20) -and $step -ne 0) { 1 } else { 0 }
        & $fill $colors.Dark ($ox + $legX + $step / 2 - 1) ($oy + 26 - $lift) 3 (6 - $lift)
    }
    $lean = if ($kind -eq "charger" -and $pose -eq "walk") { 2 } else { 0 }
    & $fill $colors.Shell ($ox + 1 - $wide) ($oy + 14 + $bob) (30 + 2 * $wide) 14
    & $fill $colors.Dark ($ox + 1 - $wide) ($oy + 24 + $bob) (30 + 2 * $wide) 4
    & $fill $colors.Lite ($ox + 5 + $lean) ($oy + 6 + $bob) 22 12
    & $fill $colors.Shell ($ox + 5 + $lean) ($oy + 15 + $bob) 22 3
    & $fill $colors.Eye ($ox + 9 + $lean) ($oy + 10 + $bob) 4 4
    & $fill $colors.Eye ($ox + 19 + $lean) ($oy + 10 + $bob) 4 4
    & $fill $palette.Yellow ($ox + 10 + $lean) ($oy + 11 + $bob) 2 2
    & $fill $palette.Yellow ($ox + 20 + $lean) ($oy + 11 + $bob) 2 2
    switch ($kind) {
        "patrol" { & $fill $colors.Lite ($ox + 14) ($oy + 2 + $bob) 4 5 }
        "heavy" {
            & $fill $colors.Dark ($ox + 3) ($oy + 18 + $bob) 26 2
            & $fill $colors.Lite ($ox + 8) ($oy + 2 + $bob) 3 5
            & $fill $colors.Lite ($ox + 21) ($oy + 2 + $bob) 3 5
        }
        "charger" {
            & $fill $palette.White ($ox + 24 + $lean) ($oy + 4 + $bob) 6 3
            & $fill $palette.White ($ox + 28 + $lean) ($oy + 2 + $bob) 2 3
            & $fill $palette.Orange ($ox + 2) ($oy + 18 + $bob) 4 2
        }
        "armored" {
            if ($pose -eq "guard") {
                & $fill $palette.White ($ox + 24) ($oy + 2) 7 26
                & $fill $palette.Cyan ($ox + 26) ($oy + 8 + $frame * 6) 3 6
            } else {
                & $fill $colors.Lite ($ox + 25) ($oy + 10 + $bob) 5 16
                & $fill $colors.Dark ($ox + 27) ($oy + 14 + $bob) 2 8
            }
        }
    }
}

$enemyColors = @{
    patrol  = @{ Shell = $palette.Shell; Lite = $palette.ShellLite; Dark = $palette.ShellDark; Eye = $palette.Orange }
    heavy   = @{ Shell = (New-Color 98 74 140); Lite = (New-Color 150 118 196); Dark = (New-Color 58 42 92); Eye = $palette.Red }
    charger = @{ Shell = (New-Color 168 86 52); Lite = (New-Color 226 132 74); Dark = (New-Color 102 46 32); Eye = $palette.Yellow }
    armored = @{ Shell = (New-Color 92 108 118); Lite = (New-Color 150 168 176); Dark = (New-Color 52 62 70); Eye = $palette.Cyan }
    sentry  = @{ Shell = (New-Color 84 96 70); Lite = (New-Color 136 156 104); Dark = (New-Color 46 56 40); Eye = $palette.Red }
}
foreach ($kind in @("patrol", "heavy", "charger", "armored", "sentry")) {
    $colors = $enemyColors[$kind]
    Save-Sprite "enemies/$kind.png" 128 64 {
        param($fill)
        for ($frame = 0; $frame -lt 4; $frame++) {
            Draw-Drone $fill ($frame * 32) 0 $frame $kind "walk" $colors
        }
        for ($frame = 0; $frame -lt 2; $frame++) {
            Draw-Drone $fill ($frame * 32) 32 $frame $kind "idle" $colors
            Draw-Drone $fill (64 + $frame * 32) 32 $frame $kind "guard" $colors
        }
    }
}

# Engineer: 2x2 frames of 28x40. Row 0 = idle, row 1 = talk.
Save-Sprite "characters/engineer.png" 56 80 {
    param($fill)
    $coat = New-Color 45 122 129
    $skin = New-Color 190 166 132
    $hair = New-Color 55 53 65
    for ($row = 0; $row -lt 2; $row++) {
        for ($frame = 0; $frame -lt 2; $frame++) {
            $ox = $frame * 28
            $oy = $row * 40
            $bob = if ($row -eq 0 -and $frame -eq 1) { 1 } else { 0 }
            & $fill $coat ($ox + 4) ($oy + 12 + $bob) 20 (28 - $bob)
            & $fill $palette.SuitDark ($ox + 4) ($oy + 30) 20 3
            & $fill $palette.Yellow ($ox + 16) ($oy + 18 + $bob) 4 2
            & $fill $skin ($ox + 6) ($oy + 1 + $bob) 16 15
            & $fill $hair ($ox + 7) ($oy + $bob) 14 5
            & $fill $palette.White ($ox + 8) ($oy + 7 + $bob) 3 3
            & $fill $palette.White ($ox + 17) ($oy + 7 + $bob) 3 3
            & $fill $palette.Boot ($ox + 7) ($oy + 38) 6 2
            & $fill $palette.Boot ($ox + 16) ($oy + 38) 6 2
            if ($row -eq 1) {
                & $fill $hair ($ox + 11) ($oy + 12) 6 (1 + $frame * 2)
                & $fill $coat ($ox + 22) ($oy + 10 - $frame * 2) 4 10
                & $fill $skin ($ox + 22) ($oy + 7 - $frame * 2) 4 4
            } else {
                & $fill $hair ($ox + 11) ($oy + 13 + $bob) 6 1
            }
        }
    }
}

# Enemy bolt: 2 frames of 12x8 facing right.
Save-Sprite "objects/enemy_bolt.png" 24 8 {
    param($fill)
    for ($frame = 0; $frame -lt 2; $frame++) {
        $ox = $frame * 12
        & $fill (New-Color 98 74 140) ($ox + 0) 3 4 2
        & $fill $palette.Red ($ox + 2) (1 + $frame) 8 (6 - 2 * $frame)
        & $fill (New-Color 255 150 190) ($ox + 5) 2 6 4
        & $fill $palette.White ($ox + 8) 3 3 2
    }
}

# Loot crate: 2 frames of 16x16 (second frame glints).
Save-Sprite "objects/loot.png" 32 16 {
    param($fill)
    for ($frame = 0; $frame -lt 2; $frame++) {
        $ox = $frame * 16
        & $fill (New-Color 91 67 37) ($ox + 0) 0 16 16
        & $fill (New-Color 190 133 57) ($ox + 1) 1 14 14
        & $fill (New-Color 235 190 93) ($ox + 3) 3 10 4
        & $fill (New-Color 111 77 42) ($ox + 5) 9 8 3
        if ($frame -eq 1) { & $fill $palette.White ($ox + 11) 2 2 2 }
    }
}

# Console prop: 2x2 frames of 32x32. Row 0 = idle (green), row 1 = alert (red).
Save-Sprite "props/console.png" 64 64 {
    param($fill)
    for ($row = 0; $row -lt 2; $row++) {
        $screen = if ($row -eq 0) { New-Color 70 190 120 } else { $palette.Red }
        for ($frame = 0; $frame -lt 2; $frame++) {
            $ox = $frame * 32
            $oy = $row * 32
            & $fill $palette.StoneDark ($ox + 4) ($oy + 12) 24 20
            & $fill $palette.Stone ($ox + 2) ($oy + 4) 28 12
            & $fill $palette.DoorDark ($ox + 5) ($oy + 6) 22 8
            & $fill $screen ($ox + 6) ($oy + 7) (8 + $frame * 6) 2
            & $fill $screen ($ox + 6) ($oy + 11) (16 - $frame * 6) 2
            & $fill $palette.Rivet ($ox + 8) ($oy + 20) 16 2
            & $fill $screen ($ox + 24) ($oy + 22) 2 2
        }
    }
}

# Crate prop: one 32x32 frame.
Save-Sprite "props/crate.png" 32 32 {
    param($fill)
    & $fill (New-Color 72 82 60) 2 6 28 26
    & $fill (New-Color 104 118 84) 2 6 28 3
    & $fill (New-Color 52 60 44) 15 9 2 23
    & $fill $palette.Yellow 6 14 6 2
    & $fill $palette.Yellow 20 14 6 2
}

# Station tileset: 8x2 tiles of 32x32.
# Row 0: metal wall, platform, hazard, grate, pipes, warning, light, dark panel.
# Row 1: lab wall, lab platform, power wall, power platform, central wall,
#        central platform, sub-floor, window.
Save-Sprite "tiles/station.png" 256 64 {
    param($fill)
    function Wall([int]$x, [int]$y, $base, $lite, $dark) {
        & $fill $base $x $y 32 32
        & $fill $lite $x $y 32 3
        & $fill $dark $x ($y + 29) 32 3
        & $fill $dark ($x + 15) ($y + 3) 2 26
        & $fill $dark $x ($y + 15) 32 2
        foreach ($rivet in @(@(5, 7), @(24, 7), @(5, 21), @(24, 21))) {
            & $fill $palette.Rivet ($x + $rivet[0]) ($y + $rivet[1]) 3 3
            & $fill $lite ($x + $rivet[0]) ($y + $rivet[1]) 1 1
        }
    }
    function Platform([int]$x, [int]$y, $base, $lite, $dark) {
        & $fill $dark $x $y 32 32
        & $fill $base $x $y 32 12
        & $fill $lite $x $y 32 3
        for ($stripe = 0; $stripe -lt 4; $stripe++) { & $fill $dark ($x + 3 + $stripe * 8) ($y + 6) 4 3 }
        & $fill $base ($x + 4) ($y + 12) 4 20
        & $fill $base ($x + 24) ($y + 12) 4 20
    }
    Wall 0 0 $palette.Stone $palette.StoneLite $palette.StoneDark
    Platform 32 0 $palette.Stone $palette.StoneLite $palette.StoneDark
    & $fill $palette.Lava 64 9 32 23
    & $fill $palette.Red 64 9 32 2
    foreach ($spike in @(@(67, 5, 5, 10), @(79, 1, 5, 14), @(89, 6, 4, 9))) {
        & $fill $palette.Orange $spike[0] $spike[1] $spike[2] $spike[3]
        & $fill $palette.Yellow ($spike[0] + 1) ($spike[1] + 1) 2 ($spike[3] - 4)
    }
    $back = New-Color 19 40 54
    $backLine = New-Color 27 54 70
    & $fill $back 96 0 32 32
    for ($line = 0; $line -lt 4; $line++) {
        & $fill $backLine (96 + $line * 8) 0 2 32
        & $fill $backLine 96 ($line * 8) 32 2
    }
    & $fill $back 128 0 32 32
    & $fill $palette.Door 128 6 32 7
    & $fill $palette.StoneLite 128 6 32 2
    & $fill $palette.Door 128 19 32 7
    & $fill $palette.StoneLite 128 19 32 2
    & $fill $palette.StoneDark 140 4 3 24
    & $fill $back 160 0 32 32
    for ($stripe = 0; $stripe -lt 4; $stripe++) {
        & $fill (New-Color 120 96 40) (160 + $stripe * 8) 12 4 8
        & $fill (New-Color 40 36 30) (164 + $stripe * 8) 12 4 8
    }
    & $fill $back 192 0 32 32
    & $fill $palette.StoneDark 202 2 12 6
    & $fill $palette.CyanDark 200 8 16 4
    & $fill (New-Color 61 227 216 90) 196 12 24 18
    & $fill (New-Color 15 30 41) 224 0 32 32
    & $fill (New-Color 22 42 56) 226 2 28 28
    & $fill (New-Color 15 30 41) 239 2 2 28
    Wall 0 32 (New-Color 34 74 72) (New-Color 64 140 128) (New-Color 22 50 50)
    Platform 32 32 (New-Color 34 74 72) (New-Color 64 140 128) (New-Color 22 50 50)
    Wall 64 32 (New-Color 58 48 86) (New-Color 108 88 150) (New-Color 36 30 58)
    Platform 96 32 (New-Color 58 48 86) (New-Color 108 88 150) (New-Color 36 30 58)
    Wall 128 32 (New-Color 44 64 96) (New-Color 96 130 180) (New-Color 28 40 64)
    Platform 160 32 (New-Color 44 64 96) (New-Color 96 130 180) (New-Color 28 40 64)
    & $fill $palette.StoneDark 192 32 32 32
    & $fill $palette.Stone 192 32 32 4
    & $fill $palette.Rivet 198 44 4 4
    & $fill $palette.Rivet 214 52 4 4
    & $fill $palette.StoneDark 224 32 32 32
    & $fill (New-Color 6 10 22) 228 36 24 24
    foreach ($star in @(@(232, 40), @(244, 44), @(236, 54), @(248, 56), @(240, 48))) {
        & $fill $palette.White $star[0] $star[1] 1 1
    }
}

# Decor tileset: 4 tiles of 16x16 with a 1 px margin and 2 px spacing.
# pipe (horizontal), lamp, arrow (right), screen. Rooms rotate and flip these.
Save-Sprite "tiles/decor.png" 72 18 {
    param($fill)
    $x = 1
    & $fill $palette.Door $x 5 16 6
    & $fill $palette.StoneLite $x 5 16 1
    & $fill $palette.StoneDark ($x + 7) 4 2 8
    $x = 19
    & $fill $palette.StoneDark ($x + 5) 1 6 3
    & $fill $palette.Yellow ($x + 3) 4 10 4
    & $fill (New-Color 255 226 140 80) ($x + 1) 8 14 8
    $x = 37
    & $fill $palette.Cyan ($x + 2) 7 8 2
    & $fill $palette.Cyan ($x + 9) 4 2 8
    & $fill $palette.Cyan ($x + 11) 5 2 6
    & $fill $palette.Cyan ($x + 13) 7 2 2
    $x = 55
    & $fill $palette.StoneDark $x 2 16 12
    & $fill (New-Color 70 190 120) ($x + 2) 4 12 8
    & $fill (New-Color 30 90 60) ($x + 3) 6 8 1
    & $fill (New-Color 30 90 60) ($x + 3) 9 6 1
}

$sampleRate = 48000

function Save-Sound([string]$name, [double]$seconds, [scriptblock]$sample) {
    $count = [int]($sampleRate * $seconds)
    $path = Join-Path $soundDir $name
    $stream = [IO.File]::Create($path)
    $writer = New-Object IO.BinaryWriter($stream)
    try {
        $writer.Write([Text.Encoding]::ASCII.GetBytes("RIFF"))
        $writer.Write([uint32](36 + $count * 2))
        $writer.Write([Text.Encoding]::ASCII.GetBytes("WAVEfmt "))
        $writer.Write([uint32]16)
        $writer.Write([uint16]1)
        $writer.Write([uint16]1)
        $writer.Write([uint32]$sampleRate)
        $writer.Write([uint32]($sampleRate * 2))
        $writer.Write([uint16]2)
        $writer.Write([uint16]16)
        $writer.Write([Text.Encoding]::ASCII.GetBytes("data"))
        $writer.Write([uint32]($count * 2))
        $random = New-Object Random(1337)
        $state = @{ Phase = 0.0; Random = $random }
        for ($index = 0; $index -lt $count; $index++) {
            $t = $index / $sampleRate
            $progress = $index / $count
            $value = [double](& $sample $t $progress $state)
            # Short fade-in and fade-out avoid clicks at sample boundaries.
            $edge = [Math]::Min(1.0, [Math]::Min($index / 96.0, ($count - $index) / 96.0))
            $value = [Math]::Max(-1.0, [Math]::Min(1.0, $value * $edge))
            $writer.Write([int16][Math]::Round($value * 32767))
        }
        Write-Host "sound $name ${seconds}s"
    } finally {
        $writer.Dispose()
    }
}

function Step-Phase($state, [double]$frequency) {
    $state.Phase += $frequency / $sampleRate
    $state.Phase -= [Math]::Floor($state.Phase)
    return $state.Phase
}
function Square([double]$phase) { if ($phase -lt 0.5) { 1.0 } else { -1.0 } }
function Noise($state) { $state.Random.NextDouble() * 2.0 - 1.0 }

Save-Sound "shot.wav" 0.09 {
    param($t, $p, $s)
    $phase = Step-Phase $s (1200 - 700 * $p)
    0.32 * (Square $phase) * (1 - $p)
}
Save-Sound "jump.wav" 0.13 {
    param($t, $p, $s)
    $phase = Step-Phase $s (280 + 520 * $p)
    0.45 * [Math]::Sin(2 * [Math]::PI * $phase) * (1 - $p * 0.8)
}
Save-Sound "hurt.wav" 0.22 {
    param($t, $p, $s)
    $phase = Step-Phase $s (190 - 110 * $p)
    (0.30 * (Square $phase) + 0.25 * (Noise $s)) * (1 - $p)
}
Save-Sound "enemy_hit.wav" 0.06 {
    param($t, $p, $s)
    $phase = Step-Phase $s 520
    (0.28 * (Noise $s) + 0.22 * (Square $phase)) * (1 - $p)
}
Save-Sound "enemy_defeat.wav" 0.38 {
    param($t, $p, $s)
    $phase = Step-Phase $s (640 * [Math]::Pow(0.12, $p))
    (0.30 * (Square $phase) + 0.20 * (Noise $s) * $p) * (1 - $p)
}
Save-Sound "pickup.wav" 0.42 {
    param($t, $p, $s)
    $notes = @(523.25, 659.25, 783.99, 1046.5)
    $note = $notes[[Math]::Min(3, [int][Math]::Floor($p * 4))]
    $phase = Step-Phase $s $note
    $local = ($p * 4) - [Math]::Floor($p * 4)
    0.38 * [Math]::Sin(2 * [Math]::PI * $phase) * (1 - 0.6 * $local) * (1 - 0.4 * $p)
}
Save-Sound "door.wav" 0.32 {
    param($t, $p, $s)
    $phase = Step-Phase $s (70 + 160 * $p)
    (0.36 * [Math]::Sin(2 * [Math]::PI * $phase) + 0.10 * (Noise $s)) * [Math]::Sin([Math]::PI * $p)
}
Save-Sound "gate_denied.wav" 0.26 {
    param($t, $p, $s)
    $phase = Step-Phase $s 150
    $gate = if ($p -lt 0.42 -or $p -gt 0.55) { 1.0 } else { 0.0 }
    0.30 * (Square $phase) * $gate * (1 - 0.5 * $p)
}

Write-Host "Assets written to $OutputDirectory"
