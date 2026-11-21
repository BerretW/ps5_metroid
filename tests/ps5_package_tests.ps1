#requires -Version 5.1
param([switch]$Controller)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$packageVariant = if ($Controller) { "ps5-controller" } else { "ps5" }
$gameBuildName = if ($Controller) { "game-controller" } else { "game" }
$package = Join-Path $root ("dist\" + $packageVariant + "\PPSA91007")
$gameBuild = Join-Path $root ("build\ps5\" + $gameBuildName)
$validator = Join-Path $root "scripts\test-ps5-package.ps1"
$tool = Join-Path $root "build\ps5\host\ps5-native-tool.exe"
& $validator -PackagePath $package
if ($Controller) {
    $report = Get-Content -LiteralPath (Join-Path $root "build\ps5\logs\PPSA91007-validation.json") -Raw |
        ConvertFrom-Json
    if (@($report.systemRuntimeModules) -contains "libSceKeyboard.prx" -or
        @($report.systemRuntimeModules) -contains "libSceImeDialog.prx") {
        throw "Controller package must not import PS5 keyboard or IME modules."
    }
}
$projectLicense = Join-Path $package "licenses\PROJECT_ECHO.txt"
if (-not (Test-Path -LiteralPath $projectLicense -PathType Leaf) -or
    (Get-FileHash -LiteralPath $projectLicense -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath (Join-Path $root "LICENSE") -Algorithm SHA256).Hash) {
    throw "Package is missing the current PROJECT ECHO license."
}
$sdlVersion = (Get-Content -LiteralPath (Join-Path $root "dependencies.ps5.json") -Raw | ConvertFrom-Json).sdl.version
$jsonLicense = Join-Path $package "licenses\yyjson.txt"
if (-not (Test-Path -LiteralPath $jsonLicense -PathType Leaf) -or
    (Get-FileHash -LiteralPath $jsonLicense -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath (Join-Path $root "licenses\yyjson.txt") -Algorithm SHA256).Hash) {
    throw "Package is missing the current yyjson license."
}
$sdlRevision = Join-Path $gameBuild "sdl\include\SDL2\SDL_revision.h"
$expectedRevision = '#define SDL_REVISION "SDL-' + $sdlVersion + '-no-vcs"'
if (-not ((Get-Content -LiteralPath $sdlRevision) -ccontains $expectedRevision)) {
    throw "SDL revision must not be derived from the parent Git repository."
}
$container = Join-Path $root ("build\ps5\validation-tests\" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $container | Out-Null
Copy-Item -LiteralPath $package -Destination $container -Recurse
$fixture = Join-Path $container "PPSA91007"

function Expect-Rejection([string]$Pattern) {
    try {
        & $validator -PackagePath $fixture
    } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw }
        Write-Host "Expected rejection: $Pattern"
        return
    }
    throw "Invalid package was accepted: $Pattern"
}

try {
    $defaultReport = Join-Path $root "build\ps5\logs\PPSA91007-validation.json"
    $defaultReportHash = (Get-FileHash -LiteralPath $defaultReport -Algorithm SHA256).Hash
    $isolatedLogs = Join-Path $container "isolated-logs"
    & $validator -PackagePath $fixture -LogDirectory $isolatedLogs
    $isolatedReport = Get-Content -LiteralPath (Join-Path $isolatedLogs "PPSA91007-validation.json") -Raw | ConvertFrom-Json
    if ($isolatedReport.package -ne $fixture -or $isolatedReport.staticValidation -ne "PASS" -or
        (Get-FileHash -LiteralPath $defaultReport -Algorithm SHA256).Hash -ne $defaultReportHash) {
        throw "Isolated validation did not preserve the default package report."
    }
    $asset = Join-Path $fixture "assets\sprites\player.png"
    $original = [IO.File]::ReadAllBytes($asset)
    [IO.File]::WriteAllBytes($asset, [Text.Encoding]::ASCII.GetBytes("not a png image, only text data"))
    Expect-Rejection "Invalid PNG asset"
    [IO.File]::WriteAllBytes($asset, $original)
    $gameConfig = Join-Path $fixture "assets\framework\game.json"
    $originalConfig = [IO.File]::ReadAllBytes($gameConfig)
    [IO.File]::WriteAllText($gameConfig, "{ not valid json")
    Expect-Rejection "Invalid framework game configuration"
    [IO.File]::WriteAllBytes($gameConfig, $originalConfig)
    $gameConfigData = [Text.Encoding]::UTF8.GetString($originalConfig) | ConvertFrom-Json
    $gameConfigData.modules = @($gameConfigData.modules | Where-Object {
        $_ -ne "echo.rpg.abilities"
    })
    [IO.File]::WriteAllText($gameConfig,
        (ConvertTo-Json -InputObject $gameConfigData -Depth 16),
        (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "required game modules or resources are missing"
    [IO.File]::WriteAllBytes($gameConfig, $originalConfig)
    $gameConfigData = [Text.Encoding]::UTF8.GetString($originalConfig) | ConvertFrom-Json
    $gameConfigData.quests.quests[0].objectives[0].fact = "echo:fact/unknown"
    [IO.File]::WriteAllText($gameConfig,
        (ConvertTo-Json -InputObject $gameConfigData -Depth 16),
        (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "main quest fact is missing"
    [IO.File]::WriteAllBytes($gameConfig, $originalConfig)
    $gameConfigData = [Text.Encoding]::UTF8.GetString($originalConfig) | ConvertFrom-Json
    $gameConfigData.resources = @($gameConfigData.resources | Where-Object {
        $_.id -ne "echo:ability/echo_step"
    })
    [IO.File]::WriteAllText($gameConfig,
        (ConvertTo-Json -InputObject $gameConfigData -Depth 16),
        (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "required game modules or resources are missing"
    [IO.File]::WriteAllBytes($gameConfig, $originalConfig)
    $dialogue = Join-Path $fixture "assets\framework\engineer.json"
    $originalDialogue = [IO.File]::ReadAllBytes($dialogue)
    [IO.File]::WriteAllText($dialogue, "{ not valid json")
    Expect-Rejection "Invalid engineer dialogue"
    [IO.File]::WriteAllBytes($dialogue, $originalDialogue)
    $validDialogue = [Text.Encoding]::UTF8.GetString($originalDialogue)
    $invalidCondition = $validDialogue.Replace('"key": "player.echo_step_unlocked"',
        '"key": "player.unknown_flag"')
    if ($invalidCondition -eq $validDialogue) {
        throw "Could not create an invalid Echo Step dialogue fixture."
    }
    [IO.File]::WriteAllText($dialogue, $invalidCondition, (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "Echo Step advice choice"
    $invalidHealthCellCondition = $validDialogue.Replace(
        '"key": "player.needs_healing"', '"key": "player.unknown_flag"')
    if ($invalidHealthCellCondition -eq $validDialogue) {
        throw "Could not create an invalid Health Cell dialogue fixture."
    }
    [IO.File]::WriteAllText($dialogue, $invalidHealthCellCondition,
        (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "Health Cell offer"
    $invalidCoilCondition = $validDialogue.Replace(
        '"key": "equipment.kinetic_coil"', '"key": "player.unknown_flag"')
    if ($invalidCoilCondition -eq $validDialogue) {
        throw "Could not create an invalid Kinetic Coil dialogue fixture."
    }
    [IO.File]::WriteAllText($dialogue, $invalidCoilCondition,
        (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "Kinetic Coil offer"
    [IO.File]::WriteAllBytes($dialogue, $originalDialogue)
    Remove-Item -LiteralPath $dialogue -Force
    Expect-Rejection "Packaged engineer dialogue is missing"
    [IO.File]::WriteAllBytes($dialogue, $originalDialogue)
    $itemCatalog = Join-Path $fixture "assets\framework\items.json"
    $originalItemCatalog = [IO.File]::ReadAllBytes($itemCatalog)
    $itemDocument = [Text.Encoding]::UTF8.GetString($originalItemCatalog) | ConvertFrom-Json
    $itemDocument.items = @($itemDocument.items | Where-Object {
        $_.id -ne "echo:item/health_cell"
    })
    [IO.File]::WriteAllText($itemCatalog,
        (ConvertTo-Json -InputObject $itemDocument -Depth 16),
        (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "Invalid item catalog"
    [IO.File]::WriteAllBytes($itemCatalog, $originalItemCatalog)
    $itemDocument = [Text.Encoding]::UTF8.GetString($originalItemCatalog) | ConvertFrom-Json
    $itemDocument.items = @($itemDocument.items | Where-Object {
        $_.id -ne "echo:item/scrap"
    })
    [IO.File]::WriteAllText($itemCatalog,
        (ConvertTo-Json -InputObject $itemDocument -Depth 16),
        (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "Invalid item catalog"
    [IO.File]::WriteAllBytes($itemCatalog, $originalItemCatalog)
    $itemDocument = [Text.Encoding]::UTF8.GetString($originalItemCatalog) | ConvertFrom-Json
    $itemDocument.items = @($itemDocument.items | Where-Object {
        $_.id -ne "echo:item/kinetic_coil"
    })
    [IO.File]::WriteAllText($itemCatalog,
        (ConvertTo-Json -InputObject $itemDocument -Depth 16),
        (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "Invalid item catalog"
    [IO.File]::WriteAllBytes($itemCatalog, $originalItemCatalog)
    $itemDocument = [Text.Encoding]::UTF8.GetString($originalItemCatalog) | ConvertFrom-Json
    $itemDocument.items = @($itemDocument.items | Where-Object {
        $_.id -ne "echo:item/jump_visor"
    })
    [IO.File]::WriteAllText($itemCatalog,
        (ConvertTo-Json -InputObject $itemDocument -Depth 16),
        (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "Invalid item catalog"
    [IO.File]::WriteAllBytes($itemCatalog, $originalItemCatalog)
    [IO.File]::WriteAllText($itemCatalog, "{ not valid json")
    Expect-Rejection "Invalid item catalog"
    [IO.File]::WriteAllBytes($itemCatalog, $originalItemCatalog)
    Remove-Item -LiteralPath $itemCatalog -Force
    Expect-Rejection "Packaged item catalog is missing"
    [IO.File]::WriteAllBytes($itemCatalog, $originalItemCatalog)
    $eboot = Join-Path $fixture "eboot.bin"
    $inspection = & $tool self --inspect --file $eboot
    if ($LASTEXITCODE -ne 0) { throw "Fixture inspection failed." }
    $digestLine = $inspection | Where-Object { $_ -match '^digest: [0-9a-f]{64}$' }
    if (@($digestLine).Count -ne 1) { throw "Fixture digest is missing." }
    $digest = $digestLine.Substring(8)
    $bytes = [IO.File]::ReadAllBytes($eboot)
    $hex = [BitConverter]::ToString($bytes).Replace("-", "").ToLowerInvariant()
    $position = $hex.IndexOf($digest, [StringComparison]::Ordinal)
    if ($position -lt 0 -or $position % 2 -ne 0) { throw "Fixture digest was not found." }
    $bytes[$position / 2] = $bytes[$position / 2] -bxor 1
    [IO.File]::WriteAllBytes($eboot, $bytes)
    Expect-Rejection "FSELF validation failed"

    $elf = Join-Path $gameBuild "eboot.elf"
    $bytes = [IO.File]::ReadAllBytes($elf)
    $header = [int][BitConverter]::ToUInt64($bytes, 32)
    $address = [BitConverter]::ToUInt64($bytes, $header + 16)
    [BitConverter]::GetBytes([uint64]($address + 8)).CopyTo($bytes, $header + 16)
    $invalidElf = Join-Path $container "misaligned.elf"
    [IO.File]::WriteAllBytes($invalidElf, $bytes)
    & $tool self --sign --in $invalidElf --out $eboot --magic 0x1D3D154F | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Fixture signing failed." }
    Expect-Rejection "Invalid PS5 load segment"
} finally {
    Remove-Item -LiteralPath $fixture -Recurse -Force
    $isolatedLogs = Join-Path $container "isolated-logs"
    if (Test-Path -LiteralPath $isolatedLogs) { Remove-Item -LiteralPath $isolatedLogs -Recurse -Force }
    $invalidElf = Join-Path $container "misaligned.elf"
    if (Test-Path -LiteralPath $invalidElf) { Remove-Item -LiteralPath $invalidElf -Force }
    Remove-Item -LiteralPath $container -Force
    & $validator -PackagePath $package
}
Write-Host "PS5 $packageVariant package tests PASS: project/yyjson licenses, SDL revision, isolated reports, corrupt PNG/config/dialogue/items, missing ability module/resource, invalid ability/Health Cell/Kinetic Coil conditions, missing item definitions/dialogue/catalog, digest corruption, signed misaligned ELF."
