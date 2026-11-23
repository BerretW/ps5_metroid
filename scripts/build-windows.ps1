#requires -Version 5.1
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$RunTests,
    [switch]$LegacySDL,
    [switch]$VulkanGpuTests
)

$ErrorActionPreference = "Stop"
if ($LegacySDL -and $VulkanGpuTests) {
    throw "-VulkanGpuTests requires the Vulkan build, not -LegacySDL."
}
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build\windows"
$logDir = Join-Path $buildDir ".build-logs"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

function Invoke-CommandLine([string]$Stage, [string]$CommandLine) {
    $stdoutPath = Join-Path $logDir ($Stage + ".stdout.log")
    $stderrPath = Join-Path $logDir ($Stage + ".stderr.log")
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $env:ComSpec
    $info.Arguments = "/d /c " + $CommandLine
    $info.WorkingDirectory = $root
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    # Start-Process -Wait also waits for lingering MSVC telemetry descendants.
    $process.WaitForExit()
    [IO.File]::WriteAllText($stdoutPath, $stdout.Result)
    [IO.File]::WriteAllText($stderrPath, $stderr.Result)
    if ($process.ExitCode -eq 0) {
        foreach ($path in @($stdoutPath, $stderrPath)) {
            if (Test-Path -LiteralPath $path) {
                Select-String -Path $path -Pattern 'CMake (Warning|Error)|error C\d+|warning C\d+|FAILED:|Test #[0-9]+|[0-9]+% tests passed|Build files have been written|Configuring done|Generating done' |
                    ForEach-Object { Write-Host $_.Line }
            }
        }
    } else {
        Write-Host ("{0} failed with exit code {1}" -f $Stage, $process.ExitCode)
        foreach ($path in @($stdoutPath, $stderrPath)) {
            if (Test-Path -LiteralPath $path) {
                Get-Content -LiteralPath $path -Tail 60 | ForEach-Object { Write-Host $_ }
            }
        }
    }
    return $process.ExitCode
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "Visual Studio Installer's vswhere.exe was not found. Install the Visual Studio C++ Build Tools workload."
}

$vsInstall = & $vswhere -latest -products "*" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) {
    throw "Visual Studio C++ tools were not found. Install the native C++ build tools."
}
$vsDevCmd = Join-Path $vsInstall "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $vsDevCmd)) {
    throw "VsDevCmd.bat was not found under the Visual Studio installation."
}
foreach ($tool in @("cmake", "ninja", "git")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool is required. Run scripts/doctor.ps1 for the native Windows prerequisites."
    }
}

$includeProbePath = Join-Path $logDir "include-prefix.c"
[IO.File]::WriteAllText($includeProbePath, "#include <stddef.h>`n", [Text.Encoding]::ASCII)
try {
    $includeProbe = 'call "{0}" -no_logo -arch=x64 -host_arch=x64 >nul && cl /nologo /showIncludes /Zs "{1}"' -f `
        $vsDevCmd, $includeProbePath
    $includeProbeExitCode = Invoke-CommandLine "include-prefix" $includeProbe
    if ($includeProbeExitCode -ne 0) {
        throw "MSVC include-prefix detection failed with exit code $includeProbeExitCode."
    }
    $includeOutput = [IO.File]::ReadAllText((Join-Path $logDir "include-prefix.stdout.log"))
    $includePrefixMatch = [regex]::Match(
        $includeOutput, '(?m)^(.+?):\s+[A-Za-z]:[\\/]')
    if (-not $includePrefixMatch.Success) {
        throw "Could not detect the MSVC include prefix; header dependency tracking is unsafe."
    }
    $includePrefix = $includePrefixMatch.Groups[1].Value + ": "
    if ($includePrefix -match '["&<>|]') {
        throw "MSVC returned an unsupported include prefix."
    }
} finally {
    Remove-Item -LiteralPath $includeProbePath -Force -ErrorAction SilentlyContinue
}

$testOptions = ""
if ($RunTests) {
    $testOptions = " -DECHO_FRAMEWORK_BUILD_TESTS=ON -DECHO_FRAMEWORK_BUILD_TOOLS=ON"
}
$graphicsBackend = if ($LegacySDL) { "SDL" } else { "Vulkan" }
$testOptions += " -DECHO_GRAPHICS_BACKEND=$graphicsBackend -DECHO_VULKAN_GPU_TESTS=$($VulkanGpuTests.IsPresent)"
$configure = 'call "{0}" -no_logo -arch=x64 -host_arch=x64 >nul && cmake -S "{1}" -B "{2}" -G Ninja -DCMAKE_BUILD_TYPE={3} -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl "-DECHO_MSVC_INCLUDE_PREFIX={4}"{5}' -f `
    $vsDevCmd, $root, $buildDir, $Configuration, $includePrefix, $testOptions
$configureExitCode = Invoke-CommandLine "configure" $configure
if ($configureExitCode -ne 0) {
    throw "CMake configure failed with exit code $configureExitCode."
}

$build = 'call "{0}" -no_logo -arch=x64 -host_arch=x64 >nul && cmake --build "{1}" --config {2}' -f `
    $vsDevCmd, $buildDir, $Configuration
$buildExitCode = Invoke-CommandLine "build" $build
if ($buildExitCode -ne 0) {
    throw "Windows build failed with exit code $buildExitCode."
}

if ($RunTests) {
    $test = 'call "{0}" -no_logo -arch=x64 -host_arch=x64 >nul && ctest --test-dir "{1}" --output-on-failure -C {2}' -f `
        $vsDevCmd, $buildDir, $Configuration
    $testExitCode = Invoke-CommandLine "test" $test
    if ($testExitCode -ne 0) {
        throw "Windows tests failed with exit code $testExitCode."
    }
}

if (-not $LegacySDL) {
    Write-Output ("Build complete: Vulkan {0}" -f
        (Join-Path $buildDir "project_echo_vulkan.exe"))
} else {
    Write-Output ("Build complete: legacy SDL {0}" -f (Join-Path $buildDir "project_echo.exe"))
}
