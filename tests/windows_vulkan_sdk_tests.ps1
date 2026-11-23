#requires -Version 5.1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $root "scripts\windows-vulkan-sdk.ps1")

$originalSdk = $env:VULKAN_SDK
try {
    $explicitSdk = Join-Path $env:TEMP ("echo-nonexistent-vulkan-sdk-" + [guid]::NewGuid().ToString("N"))
    $env:VULKAN_SDK = $explicitSdk
    Initialize-WindowsVulkanSdk
    if ($env:VULKAN_SDK -ne $explicitSdk) {
        throw "An explicitly selected SDK was overwritten."
    }

    $env:VULKAN_SDK = $null
    Initialize-WindowsVulkanSdk
    $expected = [Environment]::GetEnvironmentVariable("VULKAN_SDK", "User")
    if (-not $expected) {
        $expected = [Environment]::GetEnvironmentVariable("VULKAN_SDK", "Machine")
    }
    if ($env:VULKAN_SDK -ne $expected) {
        throw "Saved SDK settings were not restored in user-before-machine order."
    }

    $env:VULKAN_SDK = $explicitSdk
    $doctor = Join-Path $root "scripts\doctor.ps1"
    $shell = (Get-Process -Id $PID).Path
    $output = & $shell -NoProfile -File $doctor -Target Windows
    if ($LASTEXITCODE -eq 0 -or -not ($output -match '\[MISSING\] Vulkan import library')) {
        throw "The doctor did not report the missing Vulkan import library."
    }
    $output = & $shell -NoProfile -File $doctor -Target Windows -LegacySDL
    if ($LASTEXITCODE -ne 0 -or ($output -match '\[MISSING\] Vulkan')) {
        throw "The SDL-only doctor incorrectly required Vulkan."
    }
    Write-Output "Windows Vulkan SDK checks passed."
} finally {
    $env:VULKAN_SDK = $originalSdk
}
