#requires -Version 5.1
param([switch]$Cube, [switch]$Interactive)
$ErrorActionPreference = "Stop"
if ($Interactive) { $Cube = $true }
$root = Split-Path -Parent $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw "Visual Studio C++ tools were not found." }
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$settings = & $env:ComSpec /d /s /c "call `"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw "Could not initialize Visual Studio C++ tools." }
foreach ($line in $settings) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], "Process") }
}
$work = Join-Path $root "build\vulkan-ps5\radv-compat-tests"
New-Item -ItemType Directory -Force -Path $work | Out-Null
Push-Location $work
try {
    & cl.exe /nologo /W4 /WX /TC "$root\tests\vulkan_ps5_radv_compat_tests.c" `
        "$root\experiments\vulkan3d\ps5\radv\compat.c" "/Fe:$work\compat-tests.exe"
    if ($LASTEXITCODE -ne 0) { throw "RADV compatibility adapter host test failed to compile." }
    & "$work\compat-tests.exe"
    if ($LASTEXITCODE -ne 0) { throw "RADV compatibility adapter host test failed." }
    & cl.exe /nologo /W4 /WX /TC "/I$root\.deps\vulkan-ps5\mesa-source\include" `
        "/I$root\experiments\vulkan3d\ps5\radv" "$root\tests\vulkan_ps5_radv_display_tests.c" `
        "$root\experiments\vulkan3d\ps5\radv\display_probe.c" "/Fe:$work\display-tests.exe"
    if ($LASTEXITCODE -ne 0) { throw "RADV display capability host test failed to compile." }
    & "$work\display-tests.exe"
    if ($LASTEXITCODE -ne 0) { throw "RADV display capability host test failed ($LASTEXITCODE)." }
    & cl.exe /nologo /W4 /WX /TC /DECHO_RADV_FRAME_PROBE "/I$root\.deps\vulkan-ps5\mesa-source\include" `
        "/I$root\experiments\vulkan3d\ps5\radv" "$root\tests\vulkan_ps5_radv_display_tests.c" `
        "$root\experiments\vulkan3d\ps5\radv\display_probe.c" "/Fe:$work\frame-lifetime-tests.exe"
    if ($LASTEXITCODE -ne 0) { throw "RADV frame lifetime host test failed to compile." }
    & "$work\frame-lifetime-tests.exe"
    if ($LASTEXITCODE -ne 0) { throw "RADV frame lifetime host test failed ($LASTEXITCODE)." }
    & "$root\.deps\llvm-18.1.8\bin\clang.exe" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter `
        "-I$root\.deps\vulkan-ps5\mesa-source\include" "-I$root\experiments\vulkan3d\ps5\radv" `
        "$root\tests\vulkan_ps5_radv_frame_tests.c" "$root\experiments\vulkan3d\ps5\radv\frame_probe.c" `
        -o "$work\frame-tests.exe"
    if ($LASTEXITCODE -ne 0) { throw "RADV frame host test failed to compile." }
    & "$work\frame-tests.exe"
    if ($LASTEXITCODE -ne 0) { throw "RADV frame host test failed ($LASTEXITCODE)." }
    if ($Cube) {
        & "$root\.deps\llvm-18.1.8\bin\clang.exe" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter `
            -DECHO_RADV_CUBE_PROBE "-I$root\.deps\vulkan-ps5\mesa-source\include" `
            "-I$root\experiments\vulkan3d\ps5\radv" "$root\tests\vulkan_ps5_radv_frame_tests.c" `
            "$root\experiments\vulkan3d\ps5\radv\frame_probe.c" -o "$work\cube-frame-tests.exe"
        if ($LASTEXITCODE -ne 0) { throw "RADV cube lifetime test failed to compile." }
        & "$work\cube-frame-tests.exe"
        if ($LASTEXITCODE -ne 0) { throw "RADV cube lifetime test failed ($LASTEXITCODE)." }
        if (-not $env:VULKAN_SDK) { throw "VULKAN_SDK is required for the window-free GPU test." }
        & "$root\.deps\llvm-18.1.8\bin\clang.exe" -std=c11 -Wall -Wextra -Werror `
            "-I$env:VULKAN_SDK\Include" "-I$root\experiments\vulkan3d\ps5\radv" `
            "-I$root\build\vulkan-ps5\radv-import-stubs" "$root\tests\vulkan_ps5_radv_cube_gpu_tests.c" `
            "$root\experiments\vulkan3d\ps5\radv\cube_probe.c" "$env:VULKAN_SDK\Lib\vulkan-1.lib" `
            -o "$work\cube-gpu-tests.exe"
        if ($LASTEXITCODE -ne 0) { throw "RADV cube GPU test failed to compile." }
        & "$work\cube-gpu-tests.exe"
        if ($LASTEXITCODE -ne 0) { throw "RADV cube GPU test failed ($LASTEXITCODE)." }
    }
    if ($Interactive) {
        $clang = "$root\.deps\llvm-18.1.8\bin\clang.exe"
        $include = "-I$root\experiments\vulkan3d\ps5\radv"
        $mesaInclude = "-I$root\.deps\vulkan-ps5\mesa-source\include"
        & $clang -std=c11 -Wall -Wextra -Werror $mesaInclude $include `
            "-I$root\.deps\sources\sdl\src\joystick\ps5" "$root\tests\vulkan_ps5_radv_cube_pad_tests.c" `
            "$root\experiments\vulkan3d\ps5\radv\cube_pad.c" -o "$work\cube-pad-tests.exe"
        if ($LASTEXITCODE -ne 0) { throw "RADV cube pad test failed to compile." }
        & "$work\cube-pad-tests.exe"
        if ($LASTEXITCODE -ne 0) { throw "RADV cube pad test failed ($LASTEXITCODE)." }
        & $clang -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter $mesaInclude $include `
            "$root\tests\vulkan_ps5_radv_cube_loop_tests.c" "$root\experiments\vulkan3d\ps5\radv\cube_loop.c" `
            -o "$work\cube-loop-tests.exe"
        if ($LASTEXITCODE -ne 0) { throw "RADV cube loop test failed to compile." }
        & "$work\cube-loop-tests.exe"
        if ($LASTEXITCODE -ne 0) { throw "RADV cube loop test failed ($LASTEXITCODE)." }
        & $clang -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -DECHO_RADV_CUBE_PROBE `
            -DECHO_RADV_CUBE_INTERACTIVE $mesaInclude $include "$root\tests\vulkan_ps5_radv_frame_tests.c" `
            "$root\experiments\vulkan3d\ps5\radv\frame_probe.c" -o "$work\cube-interactive-lifetime-tests.exe"
        if ($LASTEXITCODE -ne 0) { throw "RADV interactive lifetime test failed to compile." }
        & "$work\cube-interactive-lifetime-tests.exe"
        if ($LASTEXITCODE -ne 0) { throw "RADV interactive lifetime test failed ($LASTEXITCODE)." }
        & $clang -std=c11 -Wall -Wextra -Werror -DECHO_RADV_CUBE_INTERACTIVE `
            "-I$env:VULKAN_SDK\Include" $include "-I$root\build\vulkan-ps5\radv-import-stubs" `
            "$root\tests\vulkan_ps5_radv_cube_gpu_tests.c" "$root\experiments\vulkan3d\ps5\radv\cube_probe.c" `
            "$env:VULKAN_SDK\Lib\vulkan-1.lib" -o "$work\cube-interactive-gpu-tests.exe"
        if ($LASTEXITCODE -ne 0) { throw "RADV interactive cube GPU test failed to compile." }
        & "$work\cube-interactive-gpu-tests.exe"
        if ($LASTEXITCODE -ne 0) { throw "RADV interactive cube GPU test failed ($LASTEXITCODE)." }
    }
} finally { Pop-Location }

$validator = Join-Path $root "scripts\test-ps5-package.ps1"
$logs = Join-Path $root "build\vulkan-ps5\logs\radv-tests"
$radv = Join-Path $root "dist\vulkan-experiment\ps5-radv-instance\PPSA91008"
$display = Join-Path $root "dist\vulkan-experiment\ps5-radv-display\PPSA91008"
$frame = Join-Path $root "dist\vulkan-experiment\ps5-radv-frame\PPSA91008"
$cubePackage = Join-Path $root "dist\vulkan-experiment\ps5-radv-cube\PPSA91008"
$interactivePackage = Join-Path $root "dist\vulkan-experiment\ps5-radv-cube-input\PPSA91008"
$runtime = Join-Path $root "dist\vulkan-experiment\ps5-runtime-hold\PPSA91008"
$production = Join-Path $root "dist\ps5-controller\PPSA91007"
function Expect-Rejection([scriptblock]$Action, [string]$Pattern) {
    try { & $Action } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw }
        Write-Host "Expected rejection: $Pattern"
        return
    }
    throw "Invalid package was accepted: $Pattern"
}
& $validator -PackagePath $radv -LogDirectory $logs -ExperimentalRadv
& $validator -PackagePath $display -LogDirectory $logs -ExperimentalRadv
& $validator -PackagePath $frame -LogDirectory $logs -ExperimentalRadv
if ($Cube) { & $validator -PackagePath $cubePackage -LogDirectory $logs -ExperimentalRadv }
if ($Interactive) { & $validator -PackagePath $interactivePackage -LogDirectory $logs -ExperimentalRadv -ExperimentalRadvPad }
Expect-Rejection { & $validator -PackagePath $radv -LogDirectory $logs } "unverified runtime dependency"
Expect-Rejection { & $validator -PackagePath $display -LogDirectory $logs } "unverified runtime dependency"
Expect-Rejection { & $validator -PackagePath $frame -LogDirectory $logs } "unverified runtime dependency"
if ($Cube) { Expect-Rejection { & $validator -PackagePath $cubePackage -LogDirectory $logs } "unverified runtime dependency" }
if ($Interactive) {
    Expect-Rejection { & $validator -PackagePath $interactivePackage -LogDirectory $logs -ExperimentalRadv } "expected system-module imports"
    Expect-Rejection { & $validator -PackagePath $interactivePackage -LogDirectory $logs -ExperimentalRadvPad } "requires -ExperimentalRadv"
}
Expect-Rejection { & $validator -PackagePath $runtime -LogDirectory $logs -ExperimentalRadv } "expected system-module imports"
Expect-Rejection { & $validator -PackagePath $production -LogDirectory $logs -ExperimentalRadv } "isolated experimental identity"
Write-Host "RADV adapter and package boundary tests PASS."
