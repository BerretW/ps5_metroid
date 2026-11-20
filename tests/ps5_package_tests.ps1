#requires -Version 5.1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$package = Join-Path $root "dist\ps5\PPSA91007"
$validator = Join-Path $root "scripts\test-ps5-package.ps1"
$tool = Join-Path $root "build\ps5\host\ps5-native-tool.exe"
& $validator -PackagePath $package
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

    $elf = Join-Path $root "build\ps5\game\eboot.elf"
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
Write-Host "PS5 package tests PASS: isolated reports, valid package, digest corruption, signed misaligned ELF."
