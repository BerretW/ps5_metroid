#requires -Version 5.1
param(
    [Parameter(Mandatory = $true)][string]$PackagePath,
    [string]$LogDirectory
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$manifest = Get-Content -LiteralPath (Join-Path $root "dependencies.ps5.json") -Raw | ConvertFrom-Json
$tool = Join-Path $root "build\ps5\host\ps5-native-tool.exe"
$readelf = Join-Path $root ".deps\llvm-18.1.8\bin\llvm-readelf.exe"
$app = (Resolve-Path -LiteralPath $PackagePath).Path
$logDir = if ($LogDirectory) { [IO.Path]::GetFullPath($LogDirectory) } else { Join-Path $root "build\ps5\logs" }
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
foreach ($name in @("eboot.bin", "sce_sys\param.json", "sce_sys\icon0.png", "sce_module\libc.prx")) {
    if (-not (Test-Path -LiteralPath (Join-Path $app $name) -PathType Leaf)) { throw "Incomplete package: $name" }
}
$icon = [IO.File]::ReadAllBytes((Join-Path $app "sce_sys\icon0.png"))
if ($icon.Length -lt 24 -or [BitConverter]::ToString($icon, 0, 8) -ne "89-50-4E-47-0D-0A-1A-0A" -or
    [BitConverter]::ToString($icon, 16, 8) -ne "00-00-02-00-00-00-02-00") {
    throw "Application icon must be a 512x512 PNG."
}
# Game packages carry runtime assets; the Hello World package does not.
$assetDir = Join-Path $app "assets"
$assetCount = 0
if (Test-Path -LiteralPath $assetDir) {
    foreach ($asset in Get-ChildItem -LiteralPath $assetDir -Recurse -File) {
        if ($asset.Length -gt 4MB) { throw "Asset exceeds the 4 MiB runtime limit: $($asset.Name)" }
        $head = New-Object byte[] 36
        $stream = [IO.File]::OpenRead($asset.FullName)
        try { $read = $stream.Read($head, 0, $head.Length) } finally { $stream.Dispose() }
        if ($asset.Extension -eq ".png") {
            if ($read -lt 24 -or [BitConverter]::ToString($head, 0, 8) -ne "89-50-4E-47-0D-0A-1A-0A") {
                throw "Invalid PNG asset: $($asset.Name)"
            }
        } elseif ($asset.Extension -eq ".wav") {
            if ($read -lt 36 -or [Text.Encoding]::ASCII.GetString($head, 0, 4) -ne "RIFF" -or
                [Text.Encoding]::ASCII.GetString($head, 8, 8) -ne "WAVEfmt " -or
                [BitConverter]::ToUInt16($head, 20) -ne 1) {
                throw "Asset is not a PCM WAV file: $($asset.Name)"
            }
        } elseif ($asset.Extension -eq ".json" -and
                  $asset.FullName -eq (Join-Path $assetDir "framework\game.json")) {
            if ($asset.Length -gt 16KB) { throw "Framework game configuration exceeds 16 KiB." }
            try {
                $gameConfig = Get-Content -LiteralPath $asset.FullName -Raw | ConvertFrom-Json -ErrorAction Stop
            } catch {
                throw "Invalid framework game configuration: $($_.Exception.Message)"
            }
            if ($gameConfig.game -ne "echo.project_echo" -or
                @($gameConfig.modules) -notcontains "echo.state" -or
                @($gameConfig.modules) -notcontains "echo.narrative.dialogue" -or
                @($gameConfig.modules) -notcontains "echo.rpg.abilities" -or
                @($gameConfig.modules) -notcontains "echo.rpg.attributes" -or
                @($gameConfig.modules) -notcontains "echo.rpg.equipment" -or
                @($gameConfig.modules) -notcontains "echo.rpg.inventory" -or
                @($gameConfig.modules) -notcontains "echo.rpg.quests" -or
                @($gameConfig.modules) -notcontains "echo.narrative.cutscenes" -or
                @($gameConfig.modules) -notcontains "echo.project_echo" -or
                -not ($gameConfig.resources -is [array]) -or
                -not ($gameConfig.technician.transform.position -is [array]) -or
                $gameConfig.technician.transform.position.Count -ne 2 -or
                @($gameConfig.resources | Where-Object {
                    $_.id -eq "echo:ability/echo_step" -and $_.type -eq "ability"
                }).Count -ne 1 -or
                @($gameConfig.resources | Where-Object {
                    $_.id -eq "echo:dialogue/engineer" -and $_.type -eq "dialogue"
                }).Count -ne 1 -or
                @($gameConfig.resources | Where-Object {
                    $_.id -eq "echo:cutscene/central_signal" -and $_.type -eq "cutscene"
                }).Count -ne 1 -or
                @($gameConfig.resources | Where-Object {
                    $_.id -eq "echo:item/maintenance_key" -and $_.type -eq "item"
                }).Count -ne 1 -or
                @($gameConfig.resources | Where-Object {
                    $_.id -eq "echo:item/health_cell" -and $_.type -eq "item"
                }).Count -ne 1 -or
                @($gameConfig.resources | Where-Object {
                    $_.id -eq "echo:item/kinetic_coil" -and $_.type -eq "item"
                }).Count -ne 1 -or
                @($gameConfig.resources | Where-Object {
                    $_.id -eq "echo:item/scrap" -and $_.type -eq "item"
                }).Count -ne 1) {
                throw "Invalid framework game configuration: required game modules or resources are missing."
            }
            $mainQuest = @($gameConfig.quests.quests | Where-Object {
                $_.id -eq "echo:quest/main_route"
            })
            if ($gameConfig.quests.schemaVersion -ne 2 -or $mainQuest.Count -ne 1 -or
                @($mainQuest[0].objectives).Count -ne 3) {
                throw "Invalid framework game configuration: main quest is missing."
            }
            foreach ($fact in @("echo:fact/maintenance_key", "echo:fact/echo_step",
                                "echo:fact/exit_reached")) {
                if (@($mainQuest[0].objectives | Where-Object { $_.fact -eq $fact }).Count -ne 1) {
                    throw "Invalid framework game configuration: main quest fact is missing."
                }
            }
            $branch = $mainQuest[0].branch
            if ($null -eq $branch -or $branch.after -ne 1 -or
                [string]::IsNullOrWhiteSpace($branch.prompt) -or $branch.prompt.Length -gt 48 -or
                @($branch.choices).Count -ne 2) {
                throw "Invalid framework game configuration: main quest routes are missing."
            }
            foreach ($expectedRoute in @(
                @{ id = "coil"; fact = "echo:fact/kinetic_coil"; reward = "echo:item/shield_cell" },
                @{ id = "sentry"; fact = "echo:fact/sentry_defeated"; reward = "echo:item/med_kit" }
            )) {
                $route = @($branch.choices | Where-Object { $_.id -eq $expectedRoute.id })
                if ($route.Count -ne 1 -or $route[0].fact -ne $expectedRoute.fact -or
                    $route[0].rewardItem -ne $expectedRoute.reward -or
                    $route[0].rewardQuantity -ne 1 -or
                    [string]::IsNullOrWhiteSpace($route[0].label) -or
                    $route[0].label.Length -gt 48 -or
                    [string]::IsNullOrWhiteSpace($route[0].objective) -or
                    $route[0].objective.Length -gt 48) {
                    throw "Invalid framework game configuration: main quest route is missing."
                }
            }
            $centralCutscene = @($gameConfig.cutscenes.cutscenes | Where-Object {
                $_.id -eq "echo:cutscene/central_signal"
            })
            if ($gameConfig.cutscenes.schemaVersion -ne 1 -or
                $centralCutscene.Count -ne 1 -or
                @($centralCutscene[0].cards).Count -ne 3) {
                throw "Invalid framework game configuration: Central Chamber cutscene is missing."
            }
            foreach ($id in @("jump_visor", "vital_vest", "power_barrel", "sprint_module",
                              "med_patch", "med_kit", "shield_cell", "speed_charge",
                              "jump_charge", "blast_charge")) {
                if (@($gameConfig.resources | Where-Object {
                    $_.id -eq "echo:item/$id" -and $_.type -eq "item" -and -not $_.optional
                }).Count -ne 1) {
                    throw "Invalid framework game configuration: missing required item $id."
                }
            }
        } elseif ($asset.Extension -eq ".json" -and
                  $asset.FullName -eq (Join-Path $assetDir "framework\engineer.json")) {
            if ($asset.Length -eq 0 -or $asset.Length -gt 16KB) {
                throw "Invalid engineer dialogue: file must be between 1 byte and 16 KiB."
            }
            try {
                $dialogue = Get-Content -LiteralPath $asset.FullName -Raw | ConvertFrom-Json -ErrorAction Stop
            } catch {
                throw "Invalid engineer dialogue: $($_.Exception.Message)"
            }
            if ($dialogue.schemaVersion -ne 1 -or $dialogue.id -ne "echo:dialogue/engineer" -or
                $dialogue.version -lt 2 -or -not $dialogue.startNode -or
                -not ($dialogue.nodes -is [array]) -or
                $dialogue.nodes.Count -lt 1 -or $dialogue.nodes.Count -gt 64 -or
                @($dialogue.nodes | Where-Object { $_.id -eq $dialogue.startNode }).Count -ne 1) {
                throw "Invalid engineer dialogue: required metadata or start node is missing."
            }
            $echoStepChoices = @($dialogue.nodes | ForEach-Object { $_.choices } |
                Where-Object { $_.id -eq "ask_echo_step" })
            if ($echoStepChoices.Count -ne 1 -or
                $echoStepChoices[0].visibleWhen.op -ne "compare" -or
                $echoStepChoices[0].visibleWhen.key -ne "player.echo_step_unlocked" -or
                $echoStepChoices[0].visibleWhen.comparison -ne "eq" -or
                $echoStepChoices[0].visibleWhen.value -ne $true -or
                @($dialogue.nodes | Where-Object {
                    $_.id -eq $echoStepChoices[0].targetNode
                }).Count -ne 1) {
                throw "Invalid engineer dialogue: Echo Step advice choice is missing or not correctly gated."
            }
            $healthCellChoices = @($dialogue.nodes | ForEach-Object { $_.choices } |
                Where-Object { $_.id -eq "take_health_cell" })
            $healthCellConditions = @($healthCellChoices[0].visibleWhen.conditions)
            if ($healthCellChoices.Count -ne 1 -or
                $healthCellChoices[0].visibleWhen.op -ne "and" -or
                $healthCellConditions.Count -ne 2 -or
                @($healthCellConditions | Where-Object {
                    $_.op -eq "compare" -and $_.key -eq "player.needs_healing" -and
                    $_.comparison -eq "eq" -and $_.value -eq $true
                }).Count -ne 1 -or
                @($healthCellConditions | Where-Object {
                    $_.op -eq "compare" -and $_.key -eq "inventory.health_cell" -and
                    $_.comparison -eq "eq" -and $_.value -eq $false
                }).Count -ne 1 -or
                $healthCellChoices[0].onSelect.id -ne "echo.inventory.grant" -or
                $healthCellChoices[0].onSelect.parameters[0].value -ne "echo:item/health_cell" -or
                @($dialogue.nodes | Where-Object {
                    $_.id -eq $healthCellChoices[0].targetNode
                }).Count -ne 1) {
                throw "Invalid engineer dialogue: Health Cell offer is missing or incorrectly gated."
            }
            $kineticCoilChoices = @($dialogue.nodes | ForEach-Object { $_.choices } |
                Where-Object { $_.id -eq "take_kinetic_coil" })
            if ($kineticCoilChoices.Count -ne 1 -or
                $kineticCoilChoices[0].visibleWhen.op -ne "compare" -or
                $kineticCoilChoices[0].visibleWhen.key -ne "equipment.kinetic_coil" -or
                $kineticCoilChoices[0].visibleWhen.comparison -ne "eq" -or
                $kineticCoilChoices[0].visibleWhen.value -ne $false -or
                $kineticCoilChoices[0].onSelect.id -ne "echo.inventory.grant" -or
                $kineticCoilChoices[0].onSelect.parameters[0].value -ne "echo:item/kinetic_coil" -or
                @($dialogue.nodes | Where-Object {
                    $_.id -eq $kineticCoilChoices[0].targetNode
                }).Count -ne 1) {
                throw "Invalid engineer dialogue: Kinetic Coil offer is missing or incorrectly gated."
            }
        } elseif ($asset.Extension -eq ".json" -and
                  $asset.FullName -eq (Join-Path $assetDir "framework\items.json")) {
            if ($asset.Length -eq 0 -or $asset.Length -gt 16KB) {
                throw "Invalid item catalog: file must be between 1 byte and 16 KiB."
            }
            try {
                $itemCatalog = Get-Content -LiteralPath $asset.FullName -Raw |
                    ConvertFrom-Json -ErrorAction Stop
            } catch {
                throw "Invalid item catalog: $($_.Exception.Message)"
            }
            if ($itemCatalog.schemaVersion -ne 1 -or
                -not ($itemCatalog.items -is [array]) -or
                $itemCatalog.items.Count -lt 1 -or $itemCatalog.items.Count -gt 32 -or
                @($itemCatalog.items | Where-Object {
                    $_.id -eq "echo:item/maintenance_key" -and
                    $_.key -eq 1 -and $_.maxStack -eq 1 -and
                    $_.displayName -eq "Maintenance Key"
                }).Count -ne 1 -or
                @($itemCatalog.items | Where-Object {
                    $_.id -eq "echo:item/health_cell" -and
                    $_.key -eq 2 -and $_.maxStack -eq 3 -and
                    $_.displayName -eq "Health Cell"
                }).Count -ne 1 -or
                @($itemCatalog.items | Where-Object {
                    $_.id -eq "echo:item/kinetic_coil" -and
                    $_.key -eq 3 -and $_.maxStack -eq 1 -and
                    $_.displayName -eq "Kinetic Coil"
                }).Count -ne 1 -or
                @($itemCatalog.items | Where-Object {
                    $_.id -eq "echo:item/scrap" -and
                    $_.key -eq 4 -and $_.maxStack -eq 99 -and
                    $_.displayName -eq "Scrap"
                }).Count -ne 1) {
                throw "Invalid item catalog: schema or required project item definition is missing."
            }
            $newItems = @("jump_visor", "vital_vest", "power_barrel", "sprint_module",
                          "med_patch", "med_kit", "shield_cell", "speed_charge",
                          "jump_charge", "blast_charge")
            for ($index = 0; $index -lt $newItems.Count; $index++) {
                $id = $newItems[$index]
                $key = $index + 5
                $stack = if ($index -lt 4) { 1 } else { 3 }
                if (@($itemCatalog.items | Where-Object {
                    $_.id -eq "echo:item/$id" -and $_.key -eq $key -and $_.maxStack -eq $stack
                }).Count -ne 1) {
                    throw "Invalid item catalog: missing or mismatched item $id."
                }
            }
        } elseif ($asset.Extension -eq ".json" -and
                  $asset.FullName.StartsWith((Join-Path $assetDir "world\"), [StringComparison]::OrdinalIgnoreCase)) {
            if ($asset.Length -eq 0 -or $asset.Length -gt 32KB) {
                throw "Invalid world data: $($asset.Name) must be between 1 byte and 32 KiB."
            }
            try {
                $worldDocument = Get-Content -LiteralPath $asset.FullName -Raw | ConvertFrom-Json -ErrorAction Stop
            } catch {
                throw "Invalid world data: $($asset.Name): $($_.Exception.Message)"
            }
            if ($worldDocument.schemaVersion -ne 1) {
                throw "Invalid world data: $($asset.Name) must use schemaVersion 1."
            }
        } else {
            throw "Unexpected asset type: $($asset.Name)"
        }
        $assetCount++
    }
    if (-not (Test-Path -LiteralPath (Join-Path $assetDir "framework\game.json") -PathType Leaf)) {
        throw "Packaged framework game configuration is missing."
    }
    $packagedModules = @((Get-Content -LiteralPath (Join-Path $assetDir "framework\game.json") -Raw |
        ConvertFrom-Json).modules)
    if ($packagedModules -notcontains "echo.world") {
        throw "Invalid framework game configuration: required game modules or resources are missing."
    }
    $worldManifestPath = Join-Path $assetDir "world\world.json"
    if (-not (Test-Path -LiteralPath $worldManifestPath -PathType Leaf)) {
        throw "Packaged world manifest is missing."
    }
    $worldManifest = Get-Content -LiteralPath $worldManifestPath -Raw | ConvertFrom-Json
    $worldEntries = @(@($worldManifest.objects) | ForEach-Object { @{ id = $_; folder = "objects" } }) +
                    @(@($worldManifest.rooms) | ForEach-Object { @{ id = $_; folder = "rooms" } })
    if (@($worldManifest.rooms) -notcontains $worldManifest.startRoom) {
        throw "Invalid world data: the start room is not listed."
    }
    foreach ($entry in $worldEntries) {
        if ($entry.id -notmatch '^echo:(object|room)/([a-z0-9_]{1,40})$') {
            throw "Invalid world data: unsupported content ID $($entry.id)."
        }
        $file = Join-Path $assetDir ("world\" + $entry.folder + "\" + $Matches[2] + ".json")
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            throw "Invalid world data: missing file for $($entry.id)."
        }
        $content = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
        if ($content.id -ne $entry.id) {
            throw "Invalid world data: $($entry.id) file declares $($content.id)."
        }
        $sheets = @()
        if ($content.sprite -and $content.sprite.sheet) { $sheets += $content.sprite.sheet }
        foreach ($tileset in @($content.tilesets)) {
            if ($tileset -and $tileset.sheet) { $sheets += $tileset.sheet }
        }
        foreach ($sheet in $sheets) {
            if ($sheet -notmatch '^sprites/[a-z0-9_/]+\.png$' -or $sheet.Contains("..") -or
                -not (Test-Path -LiteralPath (Join-Path $assetDir ($sheet -replace '/', '\')) -PathType Leaf)) {
                throw "Invalid world data: $($entry.id) references missing sprite sheet $sheet."
            }
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $assetDir "framework\engineer.json") -PathType Leaf)) {
        throw "Packaged engineer dialogue is missing."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $assetDir "framework\items.json") -PathType Leaf)) {
        throw "Packaged item catalog is missing."
    }
    if (-not (Test-Path -LiteralPath (Join-Path $app "licenses\stb.txt") -PathType Leaf)) {
        throw "Packaged PNG decoder license is missing: licenses\stb.txt"
    }
}
$neededLibraries = @()
$knownSystemModules = @("libSceLibcInternal.prx", "libSceSystemService.prx", "libSceVideoOut.prx",
    "libkernel.prx", "libScePad.prx", "libSceUserService.prx", "libSceAudioOut.prx",
    "libSceKeyboard.prx", "libSceImeDialog.prx")
$param = Get-Content -LiteralPath (Join-Path $app "sce_sys\param.json") -Raw | ConvertFrom-Json
if ($param.titleId -notmatch '^PPSA[0-9]{5}$' -or (Split-Path -Leaf $app) -ne $param.titleId -or
    $param.contentId -notmatch ("^[A-Z]{2}[0-9]{4}-" + $param.titleId + "_00-[A-Z0-9]{16}$") -or
    -not $param.localizedParameters.'en-US'.titleName) {
    throw "Invalid title identity or metadata."
}
$runtime = Join-Path $app "sce_module\libc.prx"
if ((Get-FileHash -LiteralPath $runtime -Algorithm SHA256).Hash -ne $manifest.runtime.signedSha256 -or
    (Get-Item -LiteralPath $runtime).Length -ne $manifest.runtime.signedBytes) {
    throw "Runtime library is not the verified clean-room module."
}
foreach ($name in @("eboot.bin", "sce_module\libc.prx")) {
    $path = Join-Path $app $name
    $containerBytes = [IO.File]::ReadAllBytes($path)
    $expectedMagic = if ($name -eq "eboot.bin") { 0x1D3D154F } else { [Convert]::ToUInt32("EEF51454", 16) }
    if ($containerBytes.Length -lt 4 -or [BitConverter]::ToUInt32($containerBytes, 0) -ne $expectedMagic) {
        throw "Unexpected FSELF magic: $name"
    }
    $inspection = & $tool self --inspect --file $path
    if ($LASTEXITCODE -ne 0 -or ($inspection -join "`n") -notmatch '(?m)^integrity: valid$' -or
        ($inspection -join "`n") -match 'INVALID|encrypted$') { throw "FSELF validation failed: $name" }
    $inspection | Set-Content -LiteralPath (Join-Path $logDir "$($param.titleId)-$(Split-Path -Leaf $name)-self.log")
    $extracted = Join-Path $logDir "$($param.titleId)-$(Split-Path -Leaf $name).extracted.elf"
    # Runtime SELF intentionally omits the original debug section table.
    & $tool self --extract --file $path --out $extracted --strip-sections | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "FSELF extraction failed: $name" }
    $bytes = [IO.File]::ReadAllBytes($extracted)
    $expectedType = if ($name -eq "eboot.bin") { 0xFE10 } else { 0xFE18 }
    if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x7F -or $bytes[1] -ne 0x45 -or
        $bytes[2] -ne 0x4C -or $bytes[3] -ne 0x46 -or $bytes[4] -ne 2 -or $bytes[5] -ne 1 -or
        [BitConverter]::ToUInt16($bytes, 16) -ne $expectedType -or
        [BitConverter]::ToUInt16($bytes, 18) -ne 62 -or
        ($name -eq "eboot.bin" -and [BitConverter]::ToUInt64($bytes, 24) -eq 0)) {
        throw "Unexpected ELF class, byte order, machine, SCE type, or entry point: $name"
    }
    $phOffset = [BitConverter]::ToUInt64($bytes, 32)
    $phSize = [BitConverter]::ToUInt16($bytes, 54)
    $phCount = [BitConverter]::ToUInt16($bytes, 56)
    if ($phSize -ne 56 -or $phCount -eq 0 -or $phOffset + $phSize * $phCount -gt $bytes.Length) {
        throw "Invalid program header table: $name"
    }
    $entry = [BitConverter]::ToUInt64($bytes, 24)
    $entryIsExecutable = $false
    $hasProcessParameters = $false
    for ($index = 0; $index -lt $phCount; $index++) {
        $at = [int]($phOffset + $index * $phSize)
        $type = [BitConverter]::ToUInt32($bytes, $at)
        $flags = [BitConverter]::ToUInt32($bytes, $at + 4)
        $offset = [BitConverter]::ToUInt64($bytes, $at + 8)
        $address = [BitConverter]::ToUInt64($bytes, $at + 16)
        $fileSize = [BitConverter]::ToUInt64($bytes, $at + 32)
        $memorySize = [BitConverter]::ToUInt64($bytes, $at + 40)
        $alignment = [BitConverter]::ToUInt64($bytes, $at + 48)
        if ($offset -gt $bytes.Length -or $fileSize -gt ($bytes.Length - $offset)) {
            throw "Program segment is outside the file: $name"
        }
        if ($type -eq 1) {
            if ($fileSize -gt $memorySize -or $alignment -ne 0x4000 -or
                (($offset % $alignment) -ne ($address % $alignment))) {
                throw "Invalid PS5 load segment sizes or 16-KiB alignment: $name"
            }
            if (($flags -band 1) -ne 0 -and $entry -ge $address -and ($entry - $address) -lt $memorySize) {
                $entryIsExecutable = $true
            }
        }
        if ($type -eq 0x61000001) { $hasProcessParameters = $true }
    }
    if ($name -eq "eboot.bin" -and (-not $entryIsExecutable -or -not $hasProcessParameters)) {
        throw "Native application entry or process parameters are missing."
    }
    $elfInspection = & $readelf --file-header --program-headers --dynamic-table $extracted
    if ($LASTEXITCODE -ne 0) { throw "LLVM ELF inspection failed: $name" }
    $elfInspection | Set-Content -LiteralPath (Join-Path $logDir "$($param.titleId)-$(Split-Path -Leaf $name)-elf.log")
    if ($name -eq "eboot.bin") {
        foreach ($line in $elfInspection) {
            if ($line -match 'Shared library: \[([^\]]+)\]') {
                if ($Matches[1] -notin $knownSystemModules) { throw "Unpackaged or unverified runtime dependency: $($Matches[1])" }
                $neededLibraries += $Matches[1]
            }
        }
        if ("libSceLibcInternal.prx" -notin $neededLibraries -or "libkernel.prx" -notin $neededLibraries) {
            throw "Expected native system runtime imports are missing."
        }
    }
}
$report = [ordered]@{
    titleId = $param.titleId
    package = $app
    ebootSha256 = (Get-FileHash -LiteralPath (Join-Path $app "eboot.bin") -Algorithm SHA256).Hash
    runtimeSha256 = $manifest.runtime.signedSha256
    systemRuntimeModules = $neededLibraries
    assetFiles = $assetCount
    staticValidation = "PASS"
    hardwareExecution = "NOT TESTED"
}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logDir "$($param.titleId)-validation.json")
Write-Host "Static package validation PASS ($($param.titleId)); hardware NOT TESTED."
