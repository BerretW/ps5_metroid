param(
    [Parameter(Mandatory = $true)][string]$Validator,
    [Parameter(Mandatory = $true)][string]$GameConfig,
    [Parameter(Mandatory = $true)][string]$Dialogue,
    [Parameter(Mandatory = $true)][string]$ItemCatalog,
    [Parameter(Mandatory = $true)][string]$Manifest
)

$ErrorActionPreference = 'Stop'
$encoding = New-Object System.Text.UTF8Encoding($false)
$testDirectory = Join-Path ([System.IO.Path]::GetTempPath()) `
    ('echo-project-content-' + [guid]::NewGuid().ToString('N'))
if (Test-Path -LiteralPath $testDirectory) {
    throw 'Refusing to use a pre-existing validator test directory.'
}
[void][System.IO.Directory]::CreateDirectory($testDirectory)
$createdFiles = New-Object System.Collections.Generic.List[string]

function Write-JsonFixture([string]$name, $value) {
    $path = Join-Path $testDirectory $name
    $json = ConvertTo-Json -InputObject $value -Depth 32 -Compress
    [System.IO.File]::WriteAllText($path, $json, $encoding)
    $createdFiles.Add($path)
    return $path
}

function Invoke-Validator([string]$game, [string]$dialogue, [string]$manifest,
                          [bool]$shouldPass, [string]$name,
                          [string]$items = $ItemCatalog) {
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & $Validator $game $dialogue $items $manifest 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($shouldPass -and $exitCode -ne 0) {
        throw "$name should pass, but exited with $exitCode. $output"
    }
    if (-not $shouldPass -and $exitCode -eq 0) {
        throw "$name should fail, but was accepted."
    }
}

try {
    Invoke-Validator $GameConfig $Dialogue $Manifest $true 'checked-in content'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.modules += 'echo.unknown'
    $path = Write-JsonFixture 'unknown-module.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'unknown module'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.modules = @($config.modules | Where-Object {
        $_ -ne 'echo.rpg.abilities'
    })
    $path = Write-JsonFixture 'missing-abilities-module.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'missing abilities module'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.attributes.definitions[0].baseValue = 9
    $path = Write-JsonFixture 'invalid-attribute-bounds.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'out-of-range attribute default'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.abilities.definitions[0].id = 'echo:item/not_an_ability'
    $path = Write-JsonFixture 'invalid-ability-definition.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'invalid ability definition'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.loot.drops[0].item = 'echo:item/not_in_catalog'
    $path = Write-JsonFixture 'invalid-enemy-loot-item.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'unknown enemy loot item'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.loot.drops[1].saveSlot = $config.loot.drops[0].saveSlot
    $path = Write-JsonFixture 'duplicate-enemy-loot-slot.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'duplicate enemy loot save slot'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.quests.quests[0].objectives[1].fact = 'echo:fact/unknown'
    $path = Write-JsonFixture 'unknown-quest-fact.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'unknown quest fact'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $objectives = $config.quests.quests[0].objectives
    $config.quests.quests[0].objectives = @($objectives[1], $objectives[0], $objectives[2])
    $config.quests.quests[0].branch.after = 2
    $path = Write-JsonFixture 'reordered-quest.json' $config
    Invoke-Validator $path $Dialogue $Manifest $true 'reordered quest and later branch'

    $config.quests.quests[0].branch.after = 1
    $path = Write-JsonFixture 'branch-before-key.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'branch before key objective'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $objectives = $config.quests.quests[0].objectives
    $config.quests.quests[0].objectives = @($objectives[0], $objectives[2], $objectives[1])
    $config.quests.quests[0].branch.after = 2
    $path = Write-JsonFixture 'branch-after-exit.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'branch after exit objective'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.quests.quests[0].branch.after = 1.5
    $path = Write-JsonFixture 'fractional-branch-point.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'fractional branch point'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.quests.quests[0].objectives[0].text = 'INVALID?'
    $path = Write-JsonFixture 'invalid-quest-text.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'unsupported quest glyph'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.quests.quests[0].branch.choices[0].label = 'INVALID?'
    $path = Write-JsonFixture 'invalid-quest-branch-text.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'unsupported quest branch glyph'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.quests.quests[0].branch.choices[0].label = 'A' * 49
    $path = Write-JsonFixture 'oversized-quest-branch-label.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'oversized quest branch label'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.quests.quests[0].branch.choices[0].rewardItem = 'echo:item/not_in_catalog'
    $path = Write-JsonFixture 'invalid-quest-reward.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'unsupported quest reward'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.quests.quests[0].branch.choices[1].rewardItem = 'echo:item/scrap'
    $config.quests.quests[0].branch.choices[1].rewardQuantity = 3
    $path = Write-JsonFixture 'alternate-quest-reward.json' $config
    Invoke-Validator $path $Dialogue $Manifest $true 'different bounded quest reward'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.quests.quests[0].branch.choices[1].rewardQuantity = 4
    $path = Write-JsonFixture 'oversized-quest-reward.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'quest reward exceeds stack limit'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.cutscenes.cutscenes[0].cards[0].text = 'INVALID?'
    $path = Write-JsonFixture 'invalid-cutscene-text.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'unsupported cutscene glyph'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.cutscenes.cutscenes[0].cards = @($config.cutscenes.cutscenes[0].cards | Select-Object -First 2)
    $path = Write-JsonFixture 'incomplete-cutscene.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'incomplete Central Chamber cutscene'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.technician.interaction.horizontalReach = 129
    $path = Write-JsonFixture 'invalid-component.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'out-of-range component'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.technician | Add-Member -NotePropertyName transform -NotePropertyValue ([pscustomobject]@{ position = @(108, 280) })
    $path = Write-JsonFixture 'duplicate-engineer-position.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'engineer position outside the engineer object'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.scripts.bindings[0].kind = 'unknown_behavior'
    $path = Write-JsonFixture 'unknown-script-kind.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'unsupported Lua binding kind'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.scripts.bindings[1].target = $config.scripts.bindings[0].target
    $path = Write-JsonFixture 'duplicate-script-target.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'duplicate Lua binding target'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.resources = @($config.resources | Where-Object {
        $_.id -ne 'echo:item/maintenance_key'
    })
    $path = Write-JsonFixture 'missing-resource.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'missing resource dependency'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.resources = @($config.resources | Where-Object {
        $_.id -ne 'echo:ability/echo_step'
    })
    $path = Write-JsonFixture 'missing-ability-resource.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'missing ability resource'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.resources = @($config.resources | Where-Object {
        $_.id -ne 'echo:cutscene/central_signal'
    })
    $path = Write-JsonFixture 'missing-cutscene-resource.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'missing cutscene resource dependency'

    $dialogueData = Get-Content -LiteralPath $Dialogue -Raw | ConvertFrom-Json
    $dialogueData.id = 'echo:dialogue/not_engineer'
    $path = Write-JsonFixture 'wrong-dialogue-id.json' $dialogueData
    Invoke-Validator $GameConfig $path $Manifest $false 'dialogue ID mismatch'

    $dialogueData = Get-Content -LiteralPath $Dialogue -Raw | ConvertFrom-Json
    $dialogueData.nodes[1].choices[0].onSelect.id = 'echo.fake.execute'
    $path = Write-JsonFixture 'unknown-action.json' $dialogueData
    Invoke-Validator $GameConfig $path $Manifest $false 'uncompiled dialogue action'

    $dialogueData = Get-Content -LiteralPath $Dialogue -Raw | ConvertFrom-Json
    $dialogueData.nodes[1].choices[0].onSelect.parameters[0].value = 7
    $path = Write-JsonFixture 'wrong-action-type.json' $dialogueData
    Invoke-Validator $GameConfig $path $Manifest $false 'dialogue action parameter type'

    $manifestData = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
    $manifestData.framework.apiVersion = 99
    $path = Write-JsonFixture 'unsupported-api.json' $manifestData
    Invoke-Validator $GameConfig $Dialogue $path $false 'unsupported manifest API'

    $manifestData = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
    $manifestData.components = @($manifestData.components | Where-Object {
        $_.id -ne 'echo.project_echo:component/dialogue_interaction'
    })
    $path = Write-JsonFixture 'missing-component.json' $manifestData
    Invoke-Validator $GameConfig $Dialogue $path $false 'missing project component schema'

    $itemData = Get-Content -LiteralPath $ItemCatalog -Raw | ConvertFrom-Json
    $itemData.items = @($itemData.items | Where-Object {
        $_.id -ne 'echo:item/maintenance_key'
    })
    $path = Write-JsonFixture 'missing-item-definition.json' $itemData
    Invoke-Validator $GameConfig $Dialogue $Manifest $false 'missing item definition' $path

    $itemData = Get-Content -LiteralPath $ItemCatalog -Raw | ConvertFrom-Json
    $itemData.items = @($itemData.items | Where-Object {
        $_.id -ne 'echo:item/health_cell'
    })
    $path = Write-JsonFixture 'missing-health-cell.json' $itemData
    Invoke-Validator $GameConfig $Dialogue $Manifest $false 'missing Health Cell definition' $path

    $itemData = Get-Content -LiteralPath $ItemCatalog -Raw | ConvertFrom-Json
    $itemData.items = @($itemData.items | Where-Object {
        $_.id -ne 'echo:item/kinetic_coil'
    })
    $path = Write-JsonFixture 'missing-kinetic-coil.json' $itemData
    Invoke-Validator $GameConfig $Dialogue $Manifest $false 'missing Kinetic Coil definition' $path

    $itemData = Get-Content -LiteralPath $ItemCatalog -Raw | ConvertFrom-Json
    $itemData.items = @($itemData.items | Where-Object {
        $_.id -ne 'echo:item/scrap'
    })
    $path = Write-JsonFixture 'missing-scrap.json' $itemData
    Invoke-Validator $GameConfig $Dialogue $Manifest $false 'missing enemy loot definition' $path

    $itemData = Get-Content -LiteralPath $ItemCatalog -Raw | ConvertFrom-Json
    $itemData.items = @($itemData.items | Where-Object {
        $_.id -ne 'echo:item/jump_visor'
    })
    $path = Write-JsonFixture 'missing-jump-visor.json' $itemData
    Invoke-Validator $GameConfig $Dialogue $Manifest $false 'missing new loot definition' $path

    $itemData = Get-Content -LiteralPath $ItemCatalog -Raw | ConvertFrom-Json
    $itemData.items = @($itemData.items | Where-Object {
        $_.id -ne 'echo:item/shield_cell'
    })
    $path = Write-JsonFixture 'missing-quest-reward.json' $itemData
    Invoke-Validator $GameConfig $Dialogue $Manifest $false 'missing quest reward definition' $path

    $oversizedPath = Join-Path $testDirectory 'oversized-game.json'
    [System.IO.File]::WriteAllText($oversizedPath, (' ' * 16385), $encoding)
    $createdFiles.Add($oversizedPath)
    Invoke-Validator $oversizedPath $Dialogue $Manifest $false 'oversized game config'

    Write-Output 'Project content validator accepted valid content and rejected invalid fixtures.'
}
finally {
    foreach ($path in $createdFiles) {
        if (Test-Path -LiteralPath $path) {
            [System.IO.File]::Delete($path)
        }
    }
    if (Test-Path -LiteralPath $testDirectory) {
        [System.IO.Directory]::Delete($testDirectory, $false)
    }
}
