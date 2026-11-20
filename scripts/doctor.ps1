#requires -Version 5.1
param(
    [ValidateSet("Windows", "PS5", "All")]
    [string]$Target = "Windows"
)

$ErrorActionPreference = "Stop"
$failures = 0

function Write-Check([string]$Name, [bool]$Passed, [string]$Detail, [bool]$Required = $true) {
    $state = if ($Passed) { "OK" } elseif ($Required) { "MISSING" } else { "OPTIONAL" }
    Write-Output ("[{0}] {1}: {2}" -f $state, $Name, $Detail)
    if (-not $Passed -and $Required) {
        $script:failures += 1
    }
}

function Get-CommandPath([string]$Name) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        return $null
    }
    return $command.Source
}

Write-Output ("PowerShell {0}" -f $PSVersionTable.PSVersion)
Write-Check "Git" ($null -ne (Get-CommandPath "git")) "required for the pinned SDL source"
Write-Check "CMake" ($null -ne (Get-CommandPath "cmake")) "required"
Write-Check "Ninja" ($null -ne (Get-CommandPath "ninja")) "required"

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vsInstall = $null
if (Test-Path $vswhere) {
    $vsInstall = & $vswhere -latest -products "*" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$vsDevCmd = if ($vsInstall) { Join-Path $vsInstall "Common7\Tools\VsDevCmd.bat" } else { $null }
Write-Check "Visual Studio C++ tools" ($vsDevCmd -and (Test-Path $vsDevCmd)) "native Windows compiler environment"

$clang = Get-CommandPath "clang"
$lld = Get-CommandPath "ld.lld"
Write-Check "Clang on PATH" ($null -ne $clang) "optional for Windows; PS5 uses isolated pinned LLVM" $false
Write-Check "LLD on PATH" ($null -ne $lld) "optional for Windows; PS5 uses isolated pinned LLVM" $false
Write-Check "PowerShell 7" ($null -ne (Get-CommandPath "pwsh")) "optional; scripts support Windows PowerShell 5.1" $false

if ($Target -eq "PS5" -or $Target -eq "All") {
    $root = Split-Path -Parent $PSScriptRoot
    Write-Check "Windows 11 x64" ([Environment]::Is64BitProcess -and
        $env:PROCESSOR_ARCHITECTURE -eq "AMD64" -and [Environment]::OSVersion.Version.Build -ge 22000) "native host only"
    foreach ($item in @(
        @("LLVM 18.1.8 Clang", ".deps\llvm-18.1.8\bin\clang.exe"),
        @("LLVM 18.1.8 LLD", ".deps\llvm-18.1.8\bin\ld.lld.exe"),
        @("SDK v0.42 headers", ".deps\sdk\ps5-payload-sdk\target\include\stdint.h"),
        @("SDK v0.42 stubs", ".deps\sdk\ps5-payload-sdk\target\lib\libSceLibcInternal.so"),
        @("Native app sources", ".deps\sources\boilerplate\tooling\native\app_crt.cpp")
    )) {
        Write-Check $item[0] (Test-Path -LiteralPath (Join-Path $root $item[1])) "restore with scripts\build-ps5.ps1 -RestoreDependencies"
    }
    Write-Check "curl.exe" ($null -ne (Get-CommandPath "curl.exe")) "native archive downloads"
    Write-Check "tar.exe" ($null -ne (Get-CommandPath "tar.exe")) "native archive extraction"
    Write-Output "[INFO] PS5 build uses native Windows Clang/LLD and native C++ packaging tools. No Bash, WSL or target executable is launched."
}

if ($failures -ne 0) {
    exit 1
}
