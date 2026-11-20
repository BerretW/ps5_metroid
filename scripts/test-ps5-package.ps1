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
                @($gameConfig.modules) -notcontains "echo.project_echo" -or
                -not ($gameConfig.resources -is [array])) {
                throw "Invalid framework game configuration: required game modules or resources are missing."
            }
        } else {
            throw "Unexpected asset type: $($asset.Name)"
        }
        $assetCount++
    }
    if (-not (Test-Path -LiteralPath (Join-Path $assetDir "framework\game.json") -PathType Leaf)) {
        throw "Packaged framework game configuration is missing."
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
