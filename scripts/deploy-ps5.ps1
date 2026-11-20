#requires -Version 5.1
param(
    [string]$PackagePath = (Join-Path (Split-Path -Parent $PSScriptRoot) "dist\ps5-controller\PPSA91007"),
    [string]$LogDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) "build\ps5\logs\controller-release")
)
$ErrorActionPreference = "Stop"
& (Join-Path $PSScriptRoot "test-ps5-package.ps1") -PackagePath $PackagePath -LogDirectory $LogDirectory
Write-Host "Manual upload only. Copy the complete PPSA91007 title directory into your native-app loader scan root (currently Kraken Mount)."
Write-Host "Keep eboot.bin, sce_sys and sce_module\libc.prx together. Do not upload the intermediate LLVM ELF to a payload loader."
Write-Host "No connection to a console was made and no console files were modified."
