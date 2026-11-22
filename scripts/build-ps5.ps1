#requires -Version 5.1
param(
    [ValidateSet("Hello", "Game")]
    [string]$Target = "Hello",
    [switch]$RestoreDependencies,
    [switch]$StartupDiagnostics,
    [switch]$SkipPs5Keyboard,
    [switch]$SaveStorageDiagnostics,
    [switch]$Vulkan
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$manifest = Get-Content -LiteralPath (Join-Path $root "dependencies.ps5.json") -Raw | ConvertFrom-Json
$deps = Join-Path $root ".deps"
$work = Join-Path $root "build\ps5"
$logs = Join-Path $work "logs"
if ($StartupDiagnostics) {
    if ($Target -ne "Game") { throw "-StartupDiagnostics requires -Target Game." }
    $logs = Join-Path $logs $(if ($SkipPs5Keyboard) { "startup-diagnostic-no-keyboard" } else { "startup-diagnostic" })
} elseif ($SkipPs5Keyboard) {
    if ($Target -ne "Game") { throw "-SkipPs5Keyboard requires -Target Game." }
    $logs = Join-Path $logs "controller-release"
}
if ($SaveStorageDiagnostics) {
    if ($Target -ne "Game" -or -not $StartupDiagnostics -or -not $SkipPs5Keyboard) {
        throw "-SaveStorageDiagnostics requires -Target Game -StartupDiagnostics -SkipPs5Keyboard."
    }
    $logs = Join-Path $work "logs\save-storage-diagnostic"
}
if ($Vulkan) {
    if ($Target -ne "Game" -or -not $SkipPs5Keyboard -or $StartupDiagnostics -or $SaveStorageDiagnostics) {
        throw "-Vulkan requires -Target Game -SkipPs5Keyboard without SDL startup/save diagnostics."
    }
    $logs = Join-Path $work "logs\vulkan-game"
}
$llvm = Join-Path $deps "llvm-18.1.8"
$sdk = Join-Path $deps "sdk\ps5-payload-sdk"
$boilerplate = Join-Path $deps "sources\boilerplate"
$zlib = Join-Path $deps "sources\zlib-1.3.2"
New-Item -ItemType Directory -Force -Path $logs | Out-Null

if ($env:OS -ne "Windows_NT" -or -not [Environment]::Is64BitProcess -or
    $env:PROCESSOR_ARCHITECTURE -ne "AMD64" -or [Environment]::OSVersion.Version.Build -lt 22000 -or
    (Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion").InstallationType -ne "Client") {
    throw "Only native Windows 11 x64 is supported."
}
foreach ($tool in @("cmake", "ninja", "git", "curl.exe", "tar.exe")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool is required." }
}

function Quote-Native([string]$Value) {
    if ($Value -match "[`r`n]") { throw "Newlines are not allowed in command arguments." }
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}

function Invoke-Native([string]$Stage, [string]$Executable, [string[]]$Arguments) {
    $line = ($Arguments | ForEach-Object { Quote-Native $_ }) -join " "
    Add-Content -LiteralPath (Join-Path $logs "commands.log") -Value ('"{0}" {1}' -f $Executable, $line)
    $stdout = Join-Path $logs "$Stage.stdout.log"
    $stderr = Join-Path $logs "$Stage.stderr.log"
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $Executable
    $info.Arguments = $line
    $info.WorkingDirectory = $root
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($info)
    $outputTask = $process.StandardOutput.ReadToEndAsync()
    $errorTask = $process.StandardError.ReadToEndAsync()
    # MSVC may leave a telemetry child alive after CMake exits.
    $process.WaitForExit()
    [IO.File]::WriteAllText($stdout, $outputTask.Result)
    [IO.File]::WriteAllText($stderr, $errorTask.Result)
    if ($process.ExitCode -ne 0) {
        Get-Content -LiteralPath $stdout, $stderr -Tail 50 | ForEach-Object { Write-Host $_ }
        throw "$Stage failed (exit $($process.ExitCode)). See $logs."
    }
    return [IO.File]::ReadAllText($stdout)
}

function Get-Archive($Dependency) {
    $downloads = Join-Path $deps "downloads"
    New-Item -ItemType Directory -Force -Path $downloads | Out-Null
    $path = Join-Path $downloads $Dependency.archive
    if (-not (Test-Path -LiteralPath $path)) {
        if (-not $RestoreDependencies) { throw "Missing $path. Rerun with -RestoreDependencies." }
        Invoke-Native "download-$($Dependency.archive)" "curl.exe" @("--fail", "--location",
            "--silent", "--show-error", $Dependency.url, "--output", $path) | Out-Null
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $Dependency.sha256) {
        throw "Checksum mismatch: $path. Refusing to extract or execute it."
    }
    return $path
}

function Restore-Source([string]$Name, $Dependency) {
    $path = Join-Path $deps "sources\$Name"
    if (-not (Test-Path -LiteralPath $path)) {
        if (-not $RestoreDependencies) { throw "Missing $path. Rerun with -RestoreDependencies." }
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
        Invoke-Native "clone-$Name" "git" @("-c", "core.autocrlf=false", "clone",
            "--no-checkout", $Dependency.url, $path) | Out-Null
        Invoke-Native "checkout-$Name" "git" @("-C", $path, "-c", "core.autocrlf=false",
            "checkout", "--detach", $Dependency.commit) | Out-Null
    }
    $revision = (Invoke-Native "revision-$Name" "git" @("-C", $path, "rev-parse", "HEAD")).Trim()
    $dirty = Invoke-Native "status-$Name" "git" @("-C", $path, "status", "--porcelain")
    if ($revision -ne $Dependency.commit -or $dirty.Trim()) {
        throw "$Name must be a clean checkout of $($Dependency.commit); existing changes are not overwritten."
    }
}

$llvmArchive = Get-Archive $manifest.llvm
$sdkArchive = Get-Archive $manifest.sdk
$zlibArchive = Get-Archive $manifest.zlib
Restore-Source "boilerplate" $manifest.boilerplate
if (-not (Test-Path -LiteralPath (Join-Path $llvm "bin\llvm-readelf.exe"))) {
    New-Item -ItemType Directory -Force -Path $llvm | Out-Null
    $entries = @("bin/clang.exe", "bin/clang++.exe", "bin/clang-cl.exe", "bin/ld.lld.exe",
        "bin/lld-link.exe", "bin/llvm-ar.exe", "bin/llvm-ranlib.exe", "bin/llvm-readelf.exe",
        "bin/llvm-nm.exe", "bin/*.dll", "lib/clang/18/include") |
        ForEach-Object { "$($manifest.llvm.prefix)/$_" }
    Invoke-Native "extract-llvm" "tar.exe" (@("-xf", $llvmArchive, "-C", $llvm, "--strip-components=1") + $entries) | Out-Null
}
if (-not (Test-Path -LiteralPath (Join-Path $sdk "target\lib\libSceLibcInternal.so"))) {
    Expand-Archive -LiteralPath $sdkArchive -DestinationPath (Join-Path $deps "sdk")
}
if (-not (Test-Path -LiteralPath (Join-Path $zlib "CMakeLists.txt"))) {
    Invoke-Native "extract-zlib" "tar.exe" @("-xf", $zlibArchive, "-C", (Join-Path $deps "sources")) | Out-Null
}

$clangVersion = Invoke-Native "clang-version" (Join-Path $llvm "bin\clang.exe") @("--version")
if ($clangVersion -notmatch "clang version 18\.1\.8") { throw "LLVM version does not match the manifest." }
Invoke-Native "lld-version" (Join-Path $llvm "bin\ld.lld.exe") @("--version") | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) { throw "Native Visual Studio C++ Build Tools are required for the host utilities." }
$vs = (Invoke-Native "vswhere" $vswhere @("-latest", "-products", "*", "-requires",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath")).Trim()
if (-not $vs -or $vs -match '["%!\r\n]') { throw "Invalid or missing Visual Studio C++ installation." }
$devcmd = Join-Path $vs "Common7\Tools\VsDevCmd.bat"
# Import the developer environment into this process only; do not log environment values.
$setup = New-Object Diagnostics.ProcessStartInfo
$setup.FileName = $env:ComSpec
$setup.Arguments = '/d /c call "' + $devcmd + '" -no_logo -arch=x64 -host_arch=x64 >nul && set'
$setup.UseShellExecute = $false
$setup.RedirectStandardOutput = $true
$setup.RedirectStandardError = $true
$process = [Diagnostics.Process]::Start($setup)
$environmentText = $process.StandardOutput.ReadToEnd()
$setupError = $process.StandardError.ReadToEnd()
$process.WaitForExit()
if ($process.ExitCode -ne 0) { throw "Visual Studio environment setup failed: $setupError" }
foreach ($line in ($environmentText -split "`r?`n")) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], "Process") }
}

$hostBuild = Join-Path $work "host"
Invoke-Native "host-configure" "cmake" @("-S", (Join-Path $root "cmake\ps5-host"),
    "-B", $hostBuild, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_C_COMPILER=cl", "-DCMAKE_CXX_COMPILER=cl",
    "-DBOILERPLATE_SOURCE=$boilerplate", "-DZLIB_SOURCE=$zlib") | Out-Null
Invoke-Native "host-build" "cmake" @("--build", $hostBuild, "--verbose") | Out-Null
$nativeTool = Join-Path $hostBuild "ps5-native-tool.exe"
Invoke-Native "host-tool-format" (Join-Path $llvm "bin\llvm-readelf.exe") @("--file-header", $nativeTool) | Out-Null
$builder = Join-Path $hostBuild "libc-builder.exe"
$runtime = Join-Path $work "runtime"
New-Item -ItemType Directory -Force -Path $runtime | Out-Null
foreach ($suffix in @("a", "b")) {
    $raw = Join-Path $runtime "libc-$suffix.raw.elf"
    $signed = Join-Path $runtime "libc-$suffix.prx"
    Invoke-Native "runtime-build-$suffix" $builder @(
        (Join-Path $boilerplate "tooling\native\runtime\api-surface.txt"),
        (Join-Path $boilerplate "tooling\native\runtime\imports.txt"), $raw) | Out-Null
    if ((Get-FileHash -LiteralPath $raw -Algorithm SHA256).Hash -ne $manifest.runtime.rawSha256) {
        throw "Clean-room runtime raw hash does not match upstream."
    }
    Invoke-Native "runtime-sign-$suffix" $nativeTool @("self", "--sign", "--in", $raw, "--out", $signed) | Out-Null
    if ((Get-FileHash -LiteralPath $signed -Algorithm SHA256).Hash -ne $manifest.runtime.signedSha256 -or
        (Get-Item -LiteralPath $signed).Length -ne $manifest.runtime.signedBytes) {
        throw "Clean-room runtime FSELF does not match upstream."
    }
}

$targetBuild = Join-Path $work "hello"
Invoke-Native "hello-configure" "cmake" @("--fresh", "-S", (Join-Path $root "platform\ps5"), "-B", $targetBuild,
    "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $root 'cmake\ps5-windows.cmake')",
    "-DLLVM_ROOT=$llvm", "-DPS5_SDK_ROOT=$sdk", "-DBOILERPLATE_SOURCE=$boilerplate") | Out-Null
Invoke-Native "hello-build" "cmake" @("--build", $targetBuild, "--target", "hello", "--verbose", "--", "-d", "keeprsp") | Out-Null
$intermediate = Join-Path $targetBuild "hello.llvm.elf"
$elf = Join-Path $targetBuild "eboot.elf"
Invoke-Native "hello-readelf" (Join-Path $llvm "bin\llvm-readelf.exe") @("--file-header", "--program-headers",
    "--dynamic-table", "--dyn-symbols", $intermediate) | Out-Null
Invoke-Native "hello-convert" $nativeTool @("link", "--in", $intermediate, "--out", $elf,
    "--stub-dir", (Join-Path $sdk "target\lib"), "--module-sdk", "0x02000009",
    "--companion-sdk", "0x08050001", "--file-name", "eboot.elf") | Out-Null

$app = Join-Path $root "dist\ps5-hello\PPSA99999"
New-Item -ItemType Directory -Force -Path $app, (Join-Path $app "sce_sys"), (Join-Path $app "sce_module") | Out-Null
Invoke-Native "hello-sign" $nativeTool @("self", "--sign", "--in", $elf, "--out",
    (Join-Path $app "eboot.bin"), "--magic", "0x1D3D154F") | Out-Null
Copy-Item -LiteralPath (Join-Path $runtime "libc-a.prx") -Destination (Join-Path $app "sce_module\libc.prx")
foreach ($name in @("param.json", "icon0.png", "pic0.dds", "pic1.dds", "snd0.at9")) {
    Copy-Item -LiteralPath (Join-Path $boilerplate "sce_sys\$name") -Destination (Join-Path $app "sce_sys\$name")
}
& (Join-Path $PSScriptRoot "test-ps5-package.ps1") -PackagePath $app -LogDirectory $logs
Write-Host "Native Windows Hello World package built and statically validated: $app"
Write-Host "No PS5 binary was executed by this script. See docs\STATUS.md for reported hardware results."

if ($Target -eq "Game") {
    Restore-Source "sdl" $manifest.sdl
    $patch = Join-Path $work "sdl2-native-ps5.patch"
    if ($StartupDiagnostics) {
        $patch = Join-Path $work "sdl2-native-ps5-diagnostic.patch"
    } elseif ($SkipPs5Keyboard) {
        $patch = Join-Path $work "sdl2-native-ps5-no-keyboard.patch"
    }
    $patchText = [IO.File]::ReadAllText((Join-Path $root "patches\sdl2-native-ps5.patch")).Replace("`r`n", "`n")
    if ($StartupDiagnostics) {
        $patchText += "`n" + [IO.File]::ReadAllText((Join-Path $root "patches\sdl2-ps5-diagnostic.patch")).Replace("`r`n", "`n")
    } elseif ($SkipPs5Keyboard) {
        $patchText += "`n" + [IO.File]::ReadAllText((Join-Path $root "patches\sdl2-ps5-no-keyboard.patch")).Replace("`r`n", "`n")
    }
    [IO.File]::WriteAllText($patch, $patchText, (New-Object Text.UTF8Encoding($false)))
    $patchHash = (Get-FileHash -LiteralPath $patch -Algorithm SHA256).Hash
    $sdlSource = Join-Path $work ("sdl-source-" + $patchHash.Substring(0, 12))
    if (-not (Test-Path -LiteralPath $sdlSource)) {
        New-Item -ItemType Directory -Force -Path $sdlSource | Out-Null
        Get-ChildItem -LiteralPath (Join-Path $deps "sources\sdl") -Force |
            Where-Object { $_.Name -ne ".git" } |
            ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $sdlSource -Recurse -Force }
        Invoke-Native "sdl-patch" "git" @("-C", $sdlSource, "apply", "--recount", $patch) | Out-Null
    }
    Invoke-Native "sdl-patch-check" "git" @("-C", $sdlSource, "apply", "--recount", "--reverse", "--check", $patch) | Out-Null
    $gameBuildName = if ($Vulkan) {
        "game-vulkan"
    } elseif ($SaveStorageDiagnostics) {
        "game-save-storage-diagnostic"
    } elseif ($SkipPs5Keyboard -and $StartupDiagnostics) {
        "game-diagnostic-no-keyboard"
    } elseif ($SkipPs5Keyboard) {
        "game-controller"
    } elseif ($StartupDiagnostics) {
        "game-diagnostic"
    } else {
        "game"
    }
    $gameBuild = Join-Path $work $gameBuildName
    $gameSdk = $sdk
    $gameOptions = @()
    if ($Vulkan) {
        $gameSdk = Join-Path $deps "vulkan-ps5\sdk"
        foreach ($module in @("kernel", "SceLibcInternal", "SceSystemService", "SceVideoOut",
                              "ScePad", "SceUserService", "SceAudioOut", "SceKeyboard", "SceImeDialog")) {
            $baseStub = Join-Path $sdk "target\lib\lib$module.so"
            $gameStub = Join-Path $gameSdk "target\lib\lib$module.so"
            if (-not (Test-Path -LiteralPath $gameStub -PathType Leaf) -or
                (Get-FileHash -LiteralPath $baseStub -Algorithm SHA256).Hash -ne
                (Get-FileHash -LiteralPath $gameStub -Algorithm SHA256).Hash) {
                throw "PS5 Vulkan SDK import stub does not match the pinned game SDK: $module"
            }
        }
        & (Join-Path $PSScriptRoot "probe-vulkan-radv-link.ps1") -Native -PrepareGame
        $radvInputs = Join-Path $root "build\vulkan-ps5\radv-import-stubs\game-link-inputs.cmake"
        if (-not (Test-Path -LiteralPath $radvInputs -PathType Leaf)) {
            throw "Verified RADV link inputs were not generated."
        }
        $mesaSource = Join-Path $deps "vulkan-ps5\mesa-source"
        $gameOptions += @("-DECHO_PS5_VULKAN=ON", "-DECHO_RADV_LINK_INPUTS=$radvInputs",
                          "-DECHO_PS5_MESA_SOURCE=$mesaSource")
        $vulkanManifest = Get-Content -LiteralPath (Join-Path $root "experiments\vulkan3d\dependencies.json") -Raw |
            ConvertFrom-Json
        if (-not $env:VULKAN_SDK -or
            (Split-Path -Leaf $env:VULKAN_SDK.TrimEnd('\')) -ne $vulkanManifest.vulkanSdk.version) {
            throw "The pinned native Windows Vulkan SDK is required for PS5 shaders."
        }
        $glslang = Join-Path $env:VULKAN_SDK "Bin\glslangValidator.exe"
        $spirvVal = Join-Path $env:VULKAN_SDK "Bin\spirv-val.exe"
        if (-not (Test-Path -LiteralPath $glslang -PathType Leaf) -or
            -not (Test-Path -LiteralPath $spirvVal -PathType Leaf) -or
            (Get-FileHash -LiteralPath $glslang -Algorithm SHA256).Hash -ne $vulkanManifest.vulkanSdk.glslangSha256) {
            throw "Pinned Vulkan shader compiler or SPIR-V validator is missing or mismatched."
        }
        $shaderBuild = Join-Path $gameBuild "shaders"
        New-Item -ItemType Directory -Force -Path $shaderBuild | Out-Null
        foreach ($stage in @("vert", "frag")) {
            $shaderSource = Join-Path $root "experiments\vulkan3d\assets\shaders\scene.$stage"
            $shaderOutput = Join-Path $shaderBuild "scene.$stage.spv"
            $shaderArguments = @("-V", "--target-env", "vulkan1.0", "-o", $shaderOutput)
            if ($stage -eq "frag") { $shaderArguments += "-DECHO_OUTPUT_SRGB=1" }
            Invoke-Native "shader-$stage" $glslang ($shaderArguments + @($shaderSource)) | Out-Null
            Invoke-Native "shader-$stage-validate" $spirvVal @("--target-env", "vulkan1.0", $shaderOutput) | Out-Null
        }
    }
    $configureArguments = @("--fresh", "-S", (Join-Path $root "platform\ps5"), "-B", $gameBuild,
        "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $root 'cmake\ps5-windows.cmake')",
        "-DLLVM_ROOT=$llvm", "-DPS5_SDK_ROOT=$gameSdk", "-DBOILERPLATE_SOURCE=$boilerplate", "-DECHO_PS5_GAME=ON",
        "-DSDL_SOURCE=$sdlSource", "-DECHO_PS5_STARTUP_DIAGNOSTICS=$($StartupDiagnostics.IsPresent)",
        "-DECHO_PS5_SAVE_STORAGE_DIAGNOSTICS=$($SaveStorageDiagnostics.IsPresent)",
        "-DECHO_PS5_NO_KEYBOARD=$($SkipPs5Keyboard.IsPresent)") + $gameOptions
    Invoke-Native "game-configure" "cmake" $configureArguments | Out-Null
    Invoke-Native "game-build" "cmake" @("--build", $gameBuild, "--target", "project_echo_ps5",
        "--verbose", "--", "-d", "keeprsp") | Out-Null
    $gameIntermediate = Join-Path $gameBuild "project_echo_ps5.llvm.elf"
    $gameElf = Join-Path $gameBuild "eboot.elf"
    $gameInspection = Invoke-Native "game-readelf" (Join-Path $llvm "bin\llvm-readelf.exe") @("--file-header", "--program-headers",
        "--dynamic-table", "--dyn-symbols", $gameIntermediate)
    if ($Vulkan -and ($gameInspection -match '(?m)\bWEAK\s+DEFAULT\s+UND\s+\S+' -or
        $gameInspection -match '(?m)\bUND\s+(mkstemp|readlink|strcasestr|link|symlink|qsort_s)\s*$')) {
        throw "PS5 Vulkan ELF has unresolved weak symbols or unsafe imports."
    }
    $convertArguments = @("link", "--in", $gameIntermediate, "--out", $gameElf,
        "--stub-dir", (Join-Path $gameSdk "target\lib"), "--module-sdk", "0x02000009",
        "--companion-sdk", "0x08050001", "--file-name", "eboot.elf")
    if ($Vulkan) {
        foreach ($name in @("libSceAgc.so", "libSceAgcDriver.so")) {
            $convertArguments += @("--stub", (Join-Path $root "build\vulkan-ps5\radv-import-stubs\$name"))
        }
    }
    Invoke-Native "game-convert" $nativeTool $convertArguments | Out-Null
    $gameAppName = if ($Vulkan) {
        "dist\ps5-vulkan\PPSA91007"
    } elseif ($SaveStorageDiagnostics) {
        "dist\ps5-save-diagnostic\PPSA91007"
    } elseif ($SkipPs5Keyboard -and $StartupDiagnostics) {
        "dist\ps5-diagnostic-no-keyboard\PPSA91007"
    } elseif ($SkipPs5Keyboard) {
        "dist\ps5-controller\PPSA91007"
    } elseif ($StartupDiagnostics) {
        "dist\ps5-diagnostic\PPSA91007"
    } else {
        "dist\ps5\PPSA91007"
    }
    $gameApp = Join-Path $root $gameAppName
    $vulkanMarker = Join-Path $gameApp "licenses\echo-vulkan-game.txt"
    if ($Vulkan -and (Test-Path -LiteralPath $gameApp) -and
        (-not (Test-Path -LiteralPath $vulkanMarker -PathType Leaf) -or
         (Get-Content -LiteralPath $vulkanMarker -Raw).Trim() -ne "PROJECT_ECHO_PS5_VULKAN_V1")) {
        throw "Refusing to overwrite an unrecognized PS5 Vulkan package: $gameApp"
    }
    New-Item -ItemType Directory -Force -Path $gameApp, (Join-Path $gameApp "sce_sys"),
        (Join-Path $gameApp "sce_module"), (Join-Path $gameApp "licenses") | Out-Null
    if ($Vulkan) { [IO.File]::WriteAllText($vulkanMarker, "PROJECT_ECHO_PS5_VULKAN_V1`n") }
    Invoke-Native "game-sign" $nativeTool @("self", "--sign", "--in", $gameElf, "--out",
        (Join-Path $gameApp "eboot.bin"), "--magic", "0x1D3D154F") | Out-Null
    Copy-Item -LiteralPath (Join-Path $runtime "libc-a.prx") -Destination (Join-Path $gameApp "sce_module\libc.prx")
    Copy-Item -LiteralPath (Join-Path $root "platform\ps5\sce_sys\param.json") -Destination (Join-Path $gameApp "sce_sys\param.json")
    Copy-Item -LiteralPath (Join-Path $root "LICENSE") -Destination (Join-Path $gameApp "licenses\PROJECT_ECHO.txt")
    Copy-Item -LiteralPath (Join-Path $boilerplate "LICENSE") -Destination (Join-Path $gameApp "licenses\boilerplate.txt")
    Copy-Item -LiteralPath (Join-Path $deps "sources\sdl\LICENSE.txt") -Destination (Join-Path $gameApp "licenses\SDL2.txt")
    if ((Get-FileHash -LiteralPath (Join-Path $root "third_party\stb\stb_image.h") -Algorithm SHA256).Hash -ne $manifest.stb.sha256) {
        throw "third_party\stb\stb_image.h does not match the pinned stb version."
    }
    Copy-Item -LiteralPath (Join-Path $root "licenses\stb.txt") -Destination (Join-Path $gameApp "licenses\stb.txt")
    Copy-Item -LiteralPath (Join-Path $root "licenses\yyjson.txt") -Destination (Join-Path $gameApp "licenses\yyjson.txt")
    # Replace only the game's own asset folder so stale files never ship.
    $packagedAssets = Join-Path $gameApp "assets"
    if (Test-Path -LiteralPath $packagedAssets) { Remove-Item -LiteralPath $packagedAssets -Recurse -Force }
    Copy-Item -LiteralPath (Join-Path $root "assets") -Destination $packagedAssets -Recurse
    if ($Vulkan) {
        $shaderAssets = Join-Path $packagedAssets "shaders"
        New-Item -ItemType Directory -Force -Path $shaderAssets | Out-Null
        foreach ($stage in @("vert", "frag")) {
            Copy-Item -LiteralPath (Join-Path $shaderBuild "scene.$stage.spv") -Destination $shaderAssets
        }
        Copy-Item -LiteralPath (Join-Path $deps "vulkan-ps5\vulkan-source\LICENSE") -Destination (Join-Path $gameApp "licenses\PS5_Vulkan.txt") -Force
        Copy-Item -LiteralPath (Join-Path $deps "vulkan-ps5\platform-source\LICENSE") -Destination (Join-Path $gameApp "licenses\PS5_Platform.txt") -Force
        Copy-Item -LiteralPath (Join-Path $deps "vulkan-ps5\mesa-source\docs\license.rst") -Destination (Join-Path $gameApp "licenses\Mesa-license.rst") -Force
        Copy-Item -LiteralPath (Join-Path $deps "vulkan-ps5\llvm-source\LICENSE.TXT") -Destination (Join-Path $gameApp "licenses\LLVM.txt") -Force
        $mesaLicenses = Join-Path $deps "vulkan-ps5\mesa-source\licenses"
        foreach ($license in Get-ChildItem -LiteralPath $mesaLicenses -Recurse -File) {
            $relative = $license.FullName.Substring($mesaLicenses.Length).TrimStart('\')
            $destination = Join-Path $gameApp "licenses\Mesa\$relative"
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
            Copy-Item -LiteralPath $license.FullName -Destination $destination -Force
        }
        $provenance = [ordered]@{
            mesaRevision = $vulkanManifest.ps5Mesa.commit
            vulkanRevision = $vulkanManifest.ps5Vulkan.commit
            platformRevision = $vulkanManifest.ps5Platform.commit
            radvArchiveSha256 = (Get-FileHash -LiteralPath (Join-Path $root "build\vulkan-ps5\radv-target-visibility\src\amd\vulkan\libvulkan_radeon.a") -Algorithm SHA256).Hash
            ebootSha256 = (Get-FileHash -LiteralPath (Join-Path $gameApp "eboot.bin") -Algorithm SHA256).Hash
            shaderCompilerSha256 = (Get-FileHash -LiteralPath $glslang -Algorithm SHA256).Hash
            vertexSpirvSha256 = (Get-FileHash -LiteralPath (Join-Path $shaderAssets "scene.vert.spv") -Algorithm SHA256).Hash
            fragmentSpirvSha256 = (Get-FileHash -LiteralPath (Join-Path $shaderAssets "scene.frag.spv") -Algorithm SHA256).Hash
            hardware = "NOT TESTED"
        }
        [IO.File]::WriteAllText((Join-Path $gameApp "licenses\build-provenance.json"),
            ($provenance | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding($false)))
    }
    Add-Type -AssemblyName System.Drawing
    $icon = New-Object Drawing.Bitmap(512, 512)
    $graphics = [Drawing.Graphics]::FromImage($icon)
    $brush = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(61, 227, 216))
    try {
        $graphics.Clear([Drawing.Color]::FromArgb(7, 12, 22))
        $graphics.FillRectangle($brush, 116, 112, 44, 288)
        $graphics.FillRectangle($brush, 160, 112, 236, 44)
        $graphics.FillRectangle($brush, 160, 234, 188, 44)
        $graphics.FillRectangle($brush, 160, 356, 236, 44)
        $icon.Save((Join-Path $gameApp "sce_sys\icon0.png"), [Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $brush.Dispose()
        $graphics.Dispose()
        $icon.Dispose()
    }
    & (Join-Path $PSScriptRoot "test-ps5-package.ps1") -PackagePath $gameApp -LogDirectory $logs -VulkanGame:$Vulkan
    if ($StartupDiagnostics) {
        Write-Host "Diagnostic build: note the last ECHO DIAG notification or retrieve project_echo-startup.log."
        Write-Host "On initialization failure, this build stays open for log collection; close it manually."
        if ($SkipPs5Keyboard) { Write-Host "Diagnostic limitation: PS5 hardware keyboard and IME are disabled; controller path is unchanged." }
    }
    if ($SaveStorageDiagnostics) {
        Write-Host "Save-storage diagnostic writes/replaces the reserved framework_probe.sav in /data/project_echo/saves."
        Write-Host "After running, inspect /data/project_echo/logs/project_echo.log for probe results."
    }
    if ($SkipPs5Keyboard -and -not $StartupDiagnostics) {
        Write-Host "PS5 controller build: physical keyboard and IME are disabled by explicit request."
    }
    Write-Host "PROJECT ECHO compiled, linked, packaged and statically validated: $gameApp"
    Write-Host "Static validation does not establish PS5 runtime compatibility. Hardware testing is manual."
}
