#requires -Version 5.1
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$RunTests
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build\windows"
$logDir = Join-Path $buildDir ".build-logs"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

function Invoke-CommandLine([string]$Stage, [string]$CommandLine) {
    $stdoutPath = Join-Path $logDir ($Stage + ".stdout.log")
    $stderrPath = Join-Path $logDir ($Stage + ".stderr.log")
    $process = Start-Process -FilePath $env:ComSpec -ArgumentList @("/d", "/c", $CommandLine) `
        -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
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

$configure = 'call "{0}" -no_logo -arch=x64 -host_arch=x64 >nul && cmake -S "{1}" -B "{2}" -G Ninja -DCMAKE_BUILD_TYPE={3} -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl' -f `
    $vsDevCmd, $root, $buildDir, $Configuration
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
        throw "Gameplay tests failed with exit code $testExitCode."
    }
}

Write-Output ("Build complete: {0}" -f (Join-Path $buildDir "project_echo.exe"))
