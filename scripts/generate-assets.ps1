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
