#requires -Version 5.1
param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$RunTests,
    [switch]$Clean
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build\vulkan-windows"
$manifest = Get-Content -LiteralPath (Join-Path $root "experiments\vulkan3d\dependencies.json") -Raw | ConvertFrom-Json
$sdk = $env:VULKAN_SDK
if (-not $sdk -or -not (Test-Path -LiteralPath (Join-Path $sdk "Include\vulkan\vulkan.h"))) {
    throw "Install the pinned native Windows Vulkan SDK $($manifest.vulkanSdk.version) and set VULKAN_SDK."
}
if ((Split-Path -Leaf $sdk.TrimEnd('\')) -ne $manifest.vulkanSdk.version) {
    throw "Vulkan SDK version must match experiments\vulkan3d\dependencies.json."
}
$glslang = Join-Path $sdk "Bin\glslangValidator.exe"
if ((Get-FileHash -LiteralPath $glslang -Algorithm SHA256).Hash -ne $manifest.vulkanSdk.glslangSha256) {
    throw "Pinned shader compiler SHA256 mismatch."
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products "*" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Native Visual Studio C++ tools are required." }
$dev = Join-Path $vs "Common7\Tools\VsDevCmd.bat"
$logs = Join-Path $build "logs"
New-Item -ItemType Directory -Path $logs -Force | Out-Null
function Invoke-Stage([string]$Name, [string]$Command) {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $env:ComSpec
    $info.Arguments = "/d /c " + $Command
    $info.WorkingDirectory = $root
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = [Text.Encoding]::UTF8
    $info.StandardErrorEncoding = [Text.Encoding]::UTF8
    $process = [Diagnostics.Process]::Start($info)
    $out = $process.StandardOutput.ReadToEndAsync()
    $err = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    [IO.File]::WriteAllText((Join-Path $logs "$Name.stdout.log"), $out.Result)
    [IO.File]::WriteAllText((Join-Path $logs "$Name.stderr.log"), $err.Result)
    Add-Content -LiteralPath (Join-Path $logs "commands.log") -Value $Command
    if ($process.ExitCode -ne 0) {
        Write-Host $out.Result
        Write-Host $err.Result
        throw "$Name failed with exit code $($process.ExitCode)"
    }
    Write-Host "$Name PASS (logs: $logs)"
}
$source = Join-Path $root "experiments\vulkan3d"
Invoke-Stage "include-prefix" ('call "{0}" -no_logo -arch=x64 -host_arch=x64 >nul && cl /nologo /showIncludes /Zs /I"{1}\Include" "{2}\ps5\link_probe.c"' -f $dev,$sdk,$source)
$includeText = [IO.File]::ReadAllText((Join-Path $logs "include-prefix.stdout.log"))
$match = [regex]::Match($includeText, '(?m)^(.+?):\s+[A-Za-z]:[\\/]')
if (-not $match.Success) { throw "Could not detect MSVC include prefix; header dependency tracking is unsafe." }
$prefix = $match.Groups[1].Value + ": "
if ($prefix -match '["&<>|]') { throw "Unexpected compiler include-prefix characters." }
$configure = 'call "{0}" -no_logo -arch=x64 -host_arch=x64 >nul && cmake -S "{1}" -B "{2}" -G Ninja -DCMAKE_BUILD_TYPE={3} -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl "-DECHO_MSVC_INCLUDE_PREFIX={4}"' -f $dev,$source,$build,$Configuration,$prefix
$yyjson = Join-Path $root "build\windows\_deps\yyjson-src"
if (Test-Path -LiteralPath $yyjson) { $configure += ' "-DFETCHCONTENT_SOURCE_DIR_YYJSON={0}"' -f $yyjson }
$sdl = Join-Path $root "build\windows\_deps\sdl2-src"
if (Test-Path -LiteralPath $sdl) { $configure += ' "-DFETCHCONTENT_SOURCE_DIR_SDL2={0}"' -f $sdl }
Invoke-Stage "configure" $configure
if ($Clean) {
    Invoke-Stage "clean" ('call "{0}" -no_logo -arch=x64 -host_arch=x64 >nul && cmake --build "{1}" --target clean' -f $dev,$build)
}
Invoke-Stage "build" ('call "{0}" -no_logo -arch=x64 -host_arch=x64 >nul && cmake --build "{1}" --verbose' -f $dev,$build)
if ($RunTests) {
    Invoke-Stage "test" ('ctest --test-dir "{0}" --output-on-failure' -f $build)
}
$package = Join-Path $root "dist\vulkan-experiment\windows"
New-Item -ItemType Directory -Path (Join-Path $package "assets\shaders"),(Join-Path $package "licenses") -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $build "echo_vulkan3d.exe"),(Join-Path $build "echo_vulkan_menu.exe"),(Join-Path $build "rendering-manifest.json") -Destination $package -Force
foreach ($asset in @("scene.json","reactor_sparks.json","player.png")) {
    Copy-Item -LiteralPath (Join-Path $build "assets\$asset") -Destination (Join-Path $package "assets\$asset") -Force
}
foreach ($directory in @("sprites", "framework", "world")) {
    Copy-Item -LiteralPath (Join-Path $build "assets\$directory") -Destination (Join-Path $package "assets") -Recurse -Force
}
foreach ($shader in @("scene.vert.spv","scene.frag.spv")) {
    Copy-Item -LiteralPath (Join-Path $build "assets\shaders\$shader") -Destination (Join-Path $package "assets\shaders\$shader") -Force
}
Copy-Item -LiteralPath (Join-Path $root "LICENSE") -Destination (Join-Path $package "licenses\Echo-MIT.txt") -Force
Copy-Item -LiteralPath (Join-Path $root "licenses\stb.txt"),(Join-Path $root "licenses\yyjson.txt"),(Join-Path $root "licenses\SDL2.txt") -Destination (Join-Path $package "licenses") -Force
Write-Output "Vulkan experiment built: $build\echo_vulkan3d.exe (no PS5 hardware claim)"
Write-Output "Independent Windows output: $package"
