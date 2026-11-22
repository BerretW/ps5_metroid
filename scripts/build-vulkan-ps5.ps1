#requires -Version 5.1
param([switch]$Probe)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build\vulkan-ps5"
$identity = Get-Content -LiteralPath (Join-Path $root "experiments\vulkan3d\ps5\identity.json") -Raw | ConvertFrom-Json
$production = Get-Content -LiteralPath (Join-Path $root "platform\ps5\sce_sys\param.json") -Raw | ConvertFrom-Json
if ($identity.titleId -eq $production.titleId -or $identity.contentId -eq $production.contentId -or
    $identity.contentId -notmatch ('^UP9000-' + [regex]::Escape($identity.titleId) + '_00-[A-Z0-9]{16}$')) {
    throw "Experimental identity collides with production or is malformed."
}
New-Item -ItemType Directory -Path $build -Force | Out-Null
$report = [ordered]@{
    titleId = $identity.titleId
    target = "x86_64-sie-ps5"
    driver = "Mesa RADV (not ps5vk)"
    status = "BLOCKED"
    missing = @(
        "Verified PS5 libvulkan_radeon.ps5.a and matching PROVENANCE.txt",
        "Pinned fork libps5platform.a with compatible libc++, libc++abi, libunwind",
        "ELF compiler builtins including emulated TLS (Windows COFF builtins are not compatible)",
        "Audited heap/thread wrappers, unwind linker script and native VK_KHR_display adapter"
    )
    packageValidation = "NOT RUN: no application ELF/FSELF produced"
    hardware = "NOT TESTED"
}
if ($Probe) {
    $llvm = Join-Path $root ".deps\llvm-18.1.8\bin"
    $include = Join-Path $root ".deps\sdk\ps5-payload-sdk\target\include"
    $vulkan = Join-Path $env:VULKAN_SDK "Include"
    $compiler = Join-Path $llvm "clang.exe"
    $linker = Join-Path $llvm "ld.lld.exe"
    foreach ($path in @($compiler,$linker,$include,$vulkan)) {
        if (-not (Test-Path -LiteralPath $path)) { throw "Probe prerequisite missing: $path. Restore the existing PS5 dependencies and install the experiment's Vulkan SDK." }
    }
    $source = Join-Path $root "experiments\vulkan3d\ps5\link_probe.c"
    $object = Join-Path $build "link_probe.o"
    $compileArgs = @("--target=x86_64-sie-ps5","-fPIC","-fno-stack-protector","-isystem",$include,"-I",$vulkan,"-c",$source,"-o",$object)
    & $compiler @compileArgs *> (Join-Path $build "compile.log")
    if ($LASTEXITCODE -ne 0) { throw "PS5 probe compilation failed; see $build\compile.log." }
    & (Join-Path $llvm "llvm-readelf.exe") -h $object *> (Join-Path $build "object-header.log")
    if ($LASTEXITCODE -ne 0) { throw "PS5 object header inspection failed." }
    $linkArgs = @("-m","elf_x86_64","-pie","-e","echo_vulkan_link_probe",$object,"-o",(Join-Path $build "link_probe.elf"))
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        & $linker @linkArgs *> (Join-Path $build "link.log")
        $linkExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedPreference
    }
    $linkText = Get-Content -LiteralPath (Join-Path $build "link.log") -Raw
    if ($linkExit -eq 0 -or $linkText -notmatch "undefined symbol: vkCreateInstance") {
        throw "Expected missing-driver link rejection was not observed; inspect $build\link.log."
    }
    $report["probe"] = "PS5 ELF object compiled; linking rejected undefined symbol: vkCreateInstance (no driver supplied)"
    $report["compiler"] = $compiler
    $report["compileArguments"] = $compileArgs
    $report["linker"] = $linker
    $report["linkArguments"] = $linkArgs
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $build "compatibility.json") -Encoding UTF8
if ($Probe) {
    Write-Output "Expected missing-driver failure reproduced; PS5 Vulkan remains BLOCKED. No package or console connection created."
} else {
    throw "PS5 Vulkan build BLOCKED. See $build\compatibility.json and docs\experiments\vulkan3d\PS5_BUILD.md. Use -Probe only to reproduce the missing-driver link failure."
}
