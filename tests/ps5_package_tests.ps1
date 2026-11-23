#requires -Version 5.1
param([switch]$Controller, [switch]$Vulkan)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if ($Controller -and $Vulkan) { throw "Choose -Controller or -Vulkan, not both." }
$packageVariant = if ($Vulkan) { "ps5-vulkan" } elseif ($Controller) { "ps5-controller" } else { "ps5" }
$gameBuildName = if ($Vulkan) { "game-vulkan" } elseif ($Controller) { "game-controller" } else { "game" }
$package = Join-Path $root ("dist\" + $packageVariant + "\PPSA91007")
$gameBuild = Join-Path $root ("build\ps5\" + $gameBuildName)
$validator = Join-Path $root "scripts\test-ps5-package.ps1"
$tool = Join-Path $root "build\ps5\host\ps5-native-tool.exe"
$testLogs = Join-Path $root ("build\ps5\logs\package-tests\" + $packageVariant)
& $validator -PackagePath $package -LogDirectory $testLogs -VulkanGame:$Vulkan
if ($Controller -or $Vulkan) {
    $report = Get-Content -LiteralPath (Join-Path $testLogs "PPSA91007-validation.json") -Raw |
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
        & $validator -PackagePath $fixture -LogDirectory $testLogs -VulkanGame:$Vulkan
    } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw }
        Write-Host "Expected rejection: $Pattern"
        return
    }
    throw "Invalid package was accepted: $Pattern"
}

try {
    $defaultReport = Join-Path $testLogs "PPSA91007-validation.json"
    $defaultReportHash = (Get-FileHash -LiteralPath $defaultReport -Algorithm SHA256).Hash
    $isolatedLogs = Join-Path $container "isolated-logs"
    & $validator -PackagePath $fixture -LogDirectory $isolatedLogs -VulkanGame:$Vulkan
    $isolatedReport = Get-Content -LiteralPath (Join-Path $isolatedLogs "PPSA91007-validation.json") -Raw | ConvertFrom-Json
    if ($isolatedReport.package -ne $fixture -or $isolatedReport.staticValidation -ne "PASS" -or
        (Get-FileHash -LiteralPath $defaultReport -Algorithm SHA256).Hash -ne $defaultReportHash) {
        throw "Isolated validation did not preserve the default package report."
    }
    if ($Vulkan) {
        $shader = Join-Path $fixture "assets\shaders\scene.frag.spv"
        $originalShader = [IO.File]::ReadAllBytes($shader)
        $invalidShader = [byte[]]$originalShader.Clone()
        $invalidShader[0] = 0
        [IO.File]::WriteAllBytes($shader, $invalidShader)
        Expect-Rejection "Invalid PS5 Vulkan SPIR-V asset"
        Remove-Item -LiteralPath $shader -Force
        Expect-Rejection "PS5 Vulkan shader is missing"
        [IO.File]::WriteAllBytes($shader, $originalShader)
        $stub = Join-Path $fixture "sce_module\libSceAgc.so"
        [IO.File]::WriteAllText($stub, "host-only fixture")
        Expect-Rejection "Host-only AGC link stubs must not be packaged"
        Remove-Item -LiteralPath $stub -Force
        $paramPath = Join-Path $fixture "sce_sys\param.json"
        $originalParam = [IO.File]::ReadAllBytes($paramPath)
        $alternateParam = [Text.Encoding]::UTF8.GetString($originalParam) | ConvertFrom-Json
        $alternateParam.contentId = "UP9000-PPSA91007_00-OTHERIDENTITY001"
        [IO.File]::WriteAllText($paramPath, ($alternateParam | ConvertTo-Json -Depth 8),
            (New-Object Text.UTF8Encoding($false)))
        Expect-Rejection "PS5 Vulkan game must retain the production identity"
        [IO.File]::WriteAllBytes($paramPath, $originalParam)
        $provenancePath = Join-Path $fixture "licenses\build-provenance.json"
        $originalProvenance = [IO.File]::ReadAllBytes($provenancePath)
        $invalidProvenance = [Text.Encoding]::UTF8.GetString($originalProvenance) | ConvertFrom-Json
        $invalidProvenance.vertexSpirvSha256 = "0" * 64
        [IO.File]::WriteAllText($provenancePath, ($invalidProvenance | ConvertTo-Json -Depth 8),
            (New-Object Text.UTF8Encoding($false)))
        Expect-Rejection "PS5 Vulkan package provenance does not match"
        [IO.File]::WriteAllBytes($provenancePath, $originalProvenance)
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
    $gameConfigData.quests.quests[0].branch.choices[0].rewardItem = "echo:item/unknown"
    [IO.File]::WriteAllText($gameConfig,
        (ConvertTo-Json -InputObject $gameConfigData -Depth 16),
        (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "main quest route is missing"
    [IO.File]::WriteAllBytes($gameConfig, $originalConfig)
    $gameConfigData = [Text.Encoding]::UTF8.GetString($originalConfig) | ConvertFrom-Json
    $gameConfigData.quests.quests[1].objectives[0].fact = "echo:fact/unknown"
    [IO.File]::WriteAllText($gameConfig,
        (ConvertTo-Json -InputObject $gameConfigData -Depth 16),
        (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "Cargo Hold repair quest is missing"
    [IO.File]::WriteAllBytes($gameConfig, $originalConfig)
    $gameConfigData = [Text.Encoding]::UTF8.GetString($originalConfig) | ConvertFrom-Json
    $gameConfigData.cutscenes.cutscenes[0].cards = @(
        $gameConfigData.cutscenes.cutscenes[0].cards | Select-Object -First 2)
    [IO.File]::WriteAllText($gameConfig,
        (ConvertTo-Json -InputObject $gameConfigData -Depth 16),
        (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "Central Chamber cutscene is missing"
    [IO.File]::WriteAllBytes($gameConfig, $originalConfig)
    $gameConfigData = [Text.Encoding]::UTF8.GetString($originalConfig) | ConvertFrom-Json
    $gameConfigData.resources = @($gameConfigData.resources | Where-Object {
        $_.id -ne "echo:cutscene/central_signal"
    })
    [IO.File]::WriteAllText($gameConfig,
        (ConvertTo-Json -InputObject $gameConfigData -Depth 16),
        (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "required game modules or resources are missing"
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
    $cargoDialogue = Join-Path $fixture "assets\framework\cargo_operator.json"
    $originalCargoDialogue = [IO.File]::ReadAllBytes($cargoDialogue)
    [IO.File]::WriteAllText($cargoDialogue, "{ not valid json")
    Expect-Rejection "Invalid Cargo Control dialogue"
    [IO.File]::WriteAllBytes($cargoDialogue, $originalCargoDialogue)
    $validCargoDialogue = [Text.Encoding]::UTF8.GetString($originalCargoDialogue)
    $invalidCargoCondition = $validCargoDialogue.Replace(
        '"key": "quest.cargo_emergency_powered"',
        '"key": "quest.unknown_power_state"')
    if ($invalidCargoCondition -eq $validCargoDialogue) {
        throw "Could not create an invalid Cargo Control dialogue fixture."
    }
    [IO.File]::WriteAllText($cargoDialogue, $invalidCargoCondition,
        (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "Cargo Control dialogue: repair task choices"
    [IO.File]::WriteAllBytes($cargoDialogue, $originalCargoDialogue)
    Remove-Item -LiteralPath $cargoDialogue -Force
    Expect-Rejection "Packaged Cargo Control dialogue is missing"
    [IO.File]::WriteAllBytes($cargoDialogue, $originalCargoDialogue)
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
    $enemySheet = Join-Path $fixture "assets\sprites\enemies\heavy.png"
    $originalEnemySheet = [IO.File]::ReadAllBytes($enemySheet)
    Remove-Item -LiteralPath $enemySheet -Force
    Expect-Rejection "references missing sprite sheet"
    [IO.File]::WriteAllBytes($enemySheet, $originalEnemySheet)
    $roomFile = Join-Path $fixture "assets\world\rooms\power_station.json"
    $originalRoom = [IO.File]::ReadAllBytes($roomFile)
    $roomText = [Text.Encoding]::UTF8.GetString($originalRoom)
    [IO.File]::WriteAllText($roomFile, $roomText.Replace('"echo:room/power_station"', '"echo:room/other_room"'),
        (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "file declares"
    [IO.File]::WriteAllBytes($roomFile, $originalRoom)
    $worldManifest = Join-Path $fixture "assets\world\world.json"
    $originalManifest = [IO.File]::ReadAllBytes($worldManifest)
    Remove-Item -LiteralPath $worldManifest -Force
    Expect-Rejection "Packaged world manifest is missing"
    [IO.File]::WriteAllBytes($worldManifest, $originalManifest)
    $engineerObject = Join-Path $fixture "assets\world\objects\engineer.json"
    $originalEngineer = [IO.File]::ReadAllBytes($engineerObject)
    $engineerJson = [Text.Encoding]::UTF8.GetString($originalEngineer) | ConvertFrom-Json
    $engineerJson.PSObject.Properties.Remove("spawn")
    [IO.File]::WriteAllText($engineerObject, ($engineerJson | ConvertTo-Json -Depth 8),
        (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "echo:object/engineer has no spawn position"
    [IO.File]::WriteAllBytes($engineerObject, $originalEngineer)
    $gameConfigPath = Join-Path $fixture "assets\framework\game.json"
    $originalGameConfig = [IO.File]::ReadAllBytes($gameConfigPath)
    $gameJson = [Text.Encoding]::UTF8.GetString($originalGameConfig) | ConvertFrom-Json
    $gameJson.technician | Add-Member -NotePropertyName transform -NotePropertyValue ([pscustomobject]@{ position = @(108, 280) })
    [IO.File]::WriteAllText($gameConfigPath, ($gameJson | ConvertTo-Json -Depth 12),
        (New-Object Text.UTF8Encoding($false)))
    Expect-Rejection "the engineer position belongs to echo:object/engineer"
    [IO.File]::WriteAllBytes($gameConfigPath, $originalGameConfig)
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
    & $validator -PackagePath $package -LogDirectory $testLogs -VulkanGame:$Vulkan
}
Write-Host "PS5 $packageVariant package tests PASS: project/yyjson licenses, SDL revision, isolated reports, corrupt PNG/config/dialogue/items, missing ability module/resource, invalid ability/Health Cell/Kinetic Coil/Cargo quest and dialogue conditions, missing item definitions/dialogue/catalog, world manifest/ID/sprite references, engineer spawn ownership, digest corruption, signed misaligned ELF."
