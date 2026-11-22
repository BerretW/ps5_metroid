#requires -Version 5.1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root ".deps\vulkan-ps5\mesa-source"
$hostBuild = Join-Path $root "build\vulkan-ps5\mesa-host-fixed"
$clc = Join-Path $hostBuild "src\compiler\clc\mesa_clc.exe"
$bindgen = Join-Path $hostBuild "src\compiler\spirv\vtn_bindgen2.exe"
$output = Join-Path $root "build\vulkan-ps5\host-tests"

foreach ($path in @($clc, $bindgen, (Join-Path $source "src\amd\vulkan\cl\radv_gs_compute.cl"))) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing Mesa host test prerequisite: $path"
    }
}
New-Item -ItemType Directory -Path $output -Force | Out-Null
$minimal = Join-Path $output "minimal.cl"
[IO.File]::WriteAllText($minimal, "__kernel void echo_test(__global int *p) { p[0] = 7; }`n")

function Test-Shader([string]$Name, [string]$InputFile, [string[]]$ClangArgs) {
    $spirv = Join-Path $output "$Name.spv"
    $cpp = Join-Path $output "$Name.cpp"
    $header = Join-Path $output "$Name.h"
    foreach ($path in @($spirv, $cpp, $header)) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
    & $clc -o $spirv $InputFile -- @ClangArgs
    if ($LASTEXITCODE -ne 0) { throw "$Name SPIR-V compilation failed: $LASTEXITCODE" }
    $data = [IO.File]::ReadAllBytes($spirv)
    if ($data.Length -lt 20 -or $data.Length % 4 -ne 0 -or
        [BitConverter]::ToUInt32($data, 0) -ne 0x07230203) {
        throw "$Name compiler output is not aligned little-endian SPIR-V."
    }
    & $bindgen $spirv $cpp $header
    if ($LASTEXITCODE -ne 0) { throw "$Name binding generation failed: $LASTEXITCODE" }
    foreach ($path in @($cpp, $header)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
            throw "$Name binding output missing or empty: $path"
        }
    }
    Write-Output "PASS $Name"
}

Test-Shader "minimal" $minimal @("-cl-std=cl2.0")
Test-Shader "radv" (Join-Path $source "src\amd\vulkan\cl\radv_gs_compute.cl") @(
    "-I$(Join-Path $source 'include')",
    "-I$(Join-Path $source 'src\compiler\libcl')",
    "-I$(Join-Path $source 'src')",
    "-I$(Join-Path $source 'src\amd\vulkan')",
    "-cl-std=cl2.0",
    "-D__OPENCL_VERSION__=200",
    "-DHAVE___BUILTIN_FFS",
    "-DHAVE___BUILTIN_CLZ",
    "-DNDEBUG"
)
