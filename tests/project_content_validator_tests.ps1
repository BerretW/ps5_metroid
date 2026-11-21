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
    $json = ConvertTo-Json -InputObject $value -Depth 32
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
    $config.quests.quests[0].objectives[1].fact = 'echo:fact/unknown'
    $path = Write-JsonFixture 'unknown-quest-fact.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'unknown quest fact'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.quests.quests[0].objectives[0].text = 'INVALID?'
    $path = Write-JsonFixture 'invalid-quest-text.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'unsupported quest glyph'

    $config = Get-Content -LiteralPath $GameConfig -Raw | ConvertFrom-Json
    $config.technician.interaction.horizontalReach = 129
    $path = Write-JsonFixture 'invalid-component.json' $config
    Invoke-Validator $path $Dialogue $Manifest $false 'out-of-range component'

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

    $oversizedPath = Join-Path $testDirectory 'oversized-game.json'
    [System.IO.File]::WriteAllText($oversizedPath, (' ' * 16385), $encoding)
    $createdFiles.Add($oversizedPath)
    Invoke-Validator $oversizedPath $Dialogue $Manifest $false 'oversized game config'

    Write-Output 'Project content validator accepted valid content and rejected 17 invalid cases.'
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
