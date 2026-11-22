#requires -Version 5.1
param(
    [ValidateSet("Vulkan", "Dependencies", "Runtime")]
    [string]$Target = "Vulkan",
    [switch]$RestoreDependencies,
    [switch]$RuntimeHold,
    [switch]$Probe
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build\vulkan-ps5"
$cache = Join-Path $root ".deps\vulkan-ps5"
$logs = Join-Path $build "logs"
$manifest = Get-Content -LiteralPath (Join-Path $root "experiments\vulkan3d\dependencies.json") -Raw | ConvertFrom-Json
$base = Get-Content -LiteralPath (Join-Path $root "dependencies.ps5.json") -Raw | ConvertFrom-Json
$identity = Get-Content -LiteralPath (Join-Path $root "experiments\vulkan3d\ps5\identity.json") -Raw | ConvertFrom-Json
$production = Get-Content -LiteralPath (Join-Path $root "platform\ps5\sce_sys\param.json") -Raw | ConvertFrom-Json
if ($env:OS -ne "Windows_NT" -or -not [Environment]::Is64BitProcess -or
    $env:PROCESSOR_ARCHITECTURE -ne "AMD64" -or [Environment]::OSVersion.Version.Build -lt 22000) {
    throw "Native Windows 11 x64 is required."
}
if ($RuntimeHold -and ($Target -ne "Runtime" -or $Probe)) {
    throw "-RuntimeHold requires -Target Runtime and cannot be combined with -Probe."
}
if ($identity.titleId -notmatch '^PPSA[0-9]{5}$' -or
    $identity.titleId -eq $production.titleId -or $identity.contentId -eq $production.contentId -or
    $identity.contentId -notmatch ('^UP9000-' + [regex]::Escape($identity.titleId) + '_00-[A-Z0-9]{16}$')) {
    throw "Experimental identity collides with production or is malformed."
}
New-Item -ItemType Directory -Path $logs -Force | Out-Null

function Quote-Native([string]$Value) {
    if ($Value -match "[`r`n]") { throw "Newlines are not allowed in command arguments." }
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}

function Invoke-Stage([string]$Stage, [string]$Executable, [string[]]$Arguments, [switch]$AllowFailure) {
    $line = ($Arguments | ForEach-Object { Quote-Native $_ }) -join " "
    Add-Content -LiteralPath (Join-Path $logs "commands.log") -Value ('"{0}" {1}' -f $Executable, $line)
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $Executable
    $info.Arguments = $line
    $info.WorkingDirectory = $root
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($info)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $result = [pscustomobject]@{ ExitCode = $process.ExitCode; Output = $stdout.Result; Error = $stderr.Result }
        [IO.File]::WriteAllText((Join-Path $logs "$Stage.stdout.log"), $result.Output)
        [IO.File]::WriteAllText((Join-Path $logs "$Stage.stderr.log"), $result.Error)
        if ($Stage -match '(build|link)$') {
            [IO.File]::AppendAllText((Join-Path $logs "native-build-history.log"),
                "$Stage : `"$Executable`" $line`r`n" + $result.Output + $result.Error + "`r`n")
        }
        if ($result.ExitCode -ne 0 -and -not $AllowFailure) {
            Get-Content -LiteralPath (Join-Path $logs "$Stage.stdout.log"), (Join-Path $logs "$Stage.stderr.log") -Tail 25 |
                ForEach-Object { Write-Host $_ }
            throw "$Stage failed (exit $($result.ExitCode)); see $logs."
        }
        return $result
    } finally {
        $process.Dispose()
    }
}

function Restore-Source([string]$Name, $Dependency, [string[]]$SparsePaths = @()) {
    if ($Dependency.commit -notmatch '^[0-9a-f]{40}$' -or
        $Dependency.url -notmatch '^https://github\.com/(mihawk-99/(PS5_PayloadSDK|PS5_Mesa)|llvm/llvm-project)(\.git)?$') {
        throw "Invalid pinned source: $Name."
    }
    $path = Join-Path $cache $Name
    if (-not (Test-Path -LiteralPath $path)) {
        if (-not $RestoreDependencies) { throw "Missing $path; use -RestoreDependencies." }
        Invoke-Stage "init-$Name" "git" @("init", $path) | Out-Null
        Invoke-Stage "remote-$Name" "git" @("-C", $path, "remote", "add", "origin", $Dependency.url) | Out-Null
        $fetch = @("-C", $path, "-c", "core.longpaths=true", "fetch", "--depth=1")
        if ($SparsePaths.Count) { $fetch += "--filter=blob:none" }
        Invoke-Stage "fetch-$Name" "git" ($fetch + @("origin", $Dependency.commit)) | Out-Null
        if ($SparsePaths.Count) {
            Invoke-Stage "sparse-init-$Name" "git" @("-C", $path, "sparse-checkout", "init", "--cone") | Out-Null
            Invoke-Stage "sparse-set-$Name" "git" (@("-C", $path, "sparse-checkout", "set") + $SparsePaths) | Out-Null
        }
        Invoke-Stage "checkout-$Name" "git" @("-C", $path, "-c", "core.longpaths=true", "-c", "core.autocrlf=false",
            "-c", "core.symlinks=false", "checkout", "--detach", $Dependency.commit) | Out-Null
    }
    $revision = (Invoke-Stage "revision-$Name" "git" @("-C", $path, "rev-parse", "HEAD")).Output.Trim()
    $dirty = (Invoke-Stage "status-$Name" "git" @("-C", $path, "status", "--porcelain")).Output.Trim()
    if ($revision -ne $Dependency.commit -or $dirty) {
        throw "$Name must be a clean checkout of $($Dependency.commit); existing changes are not overwritten."
    }
    return $path
}

function Materialize-Headers([string]$Source, [string]$Destination) {
    $links = @{}
    $entries = (Invoke-Stage "sdk-header-links" "git" @("-C", $Source, "ls-files", "-s")).Output -split "\r?\n"
    foreach ($entry in $entries) {
        if ($entry -match '^120000 [0-9a-f]+ 0\t(include/(freebsd|khronos|ps5)/[-a-zA-Z0-9._/]+)$') {
            $links[$Matches[1]] = $true
        }
    }
    $sourceRoot = [IO.Path]::GetFullPath((Join-Path $Source "include")) + "\"
    foreach ($relative in $links.Keys) {
        $current = $relative
        $seen = @{}
        while ($links.ContainsKey($current)) {
            if ($seen.ContainsKey($current)) { throw "Cyclic SDK header link: $relative." }
            $seen[$current] = $true
            $linkPath = Join-Path $Source $current.Replace('/', '\')
            $target = [IO.File]::ReadAllText($linkPath).Trim()
            if ($target -notmatch '^[-a-zA-Z0-9._/]+$') { throw "Invalid SDK header link: $relative." }
            $resolved = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $linkPath) $target.Replace('/', '\')))
            if (-not $resolved.StartsWith($sourceRoot, [StringComparison]::OrdinalIgnoreCase)) {
                throw "SDK header link escapes the pinned include tree: $relative."
            }
            $current = "include/" + $resolved.Substring($sourceRoot.Length).Replace('\', '/')
        }
        $installed = $relative -replace '^include/(freebsd|khronos)/', '' -replace '^include/', ''
        Copy-Item -LiteralPath (Join-Path $Source $current.Replace('/', '\')) -Destination (Join-Path $Destination $installed.Replace('/', '\')) -Force
    }
}

function Inspect-Archive([string]$Name, [string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "Missing target archive: $Path." }
    $members = (Invoke-Stage "$Name-members" (Join-Path $llvm "bin\llvm-ar.exe") @("t", $Path)).Output -split "\r?\n" |
        Where-Object { $_.Trim() }
    $headers = (Invoke-Stage "$Name-headers" (Join-Path $llvm "bin\llvm-readelf.exe") @("--file-header", $Path)).Output
    foreach ($pattern in @('Class:\s+ELF64', 'Data:\s+2.s complement, little endian',
            'Type:\s+REL \(Relocatable file\)', 'Machine:\s+Advanced Micro Devices X86-64')) {
        if (-not $members.Count -or [regex]::Matches($headers, $pattern).Count -ne $members.Count) {
            throw "$Name contains a non-ELF64/x86-64 relocatable member or an incomplete inspection."
        }
    }
    return [ordered]@{ path = $Path; sha256 = (Get-FileHash -LiteralPath $Path).Hash.ToLowerInvariant(); members = $members.Count }
}

$llvm = Join-Path $root ".deps\llvm-18.1.8"
$baseSdk = Join-Path $root ".deps\sdk\ps5-payload-sdk"
$sdk = Join-Path $cache "sdk"
$report = [ordered]@{
    titleId = $identity.titleId
    target = "x86_64-sie-ps5"
    driver = "Mesa RADV (not ps5vk)"
    status = "BLOCKED"
    missing = @("Integrated, reproducible native Mesa host-tool/RADV build pipeline",
        "Hardware-tested RADV imports and VK_KHR_display integration")
    packageValidation = "Default Vulkan renderer NOT BUILT; separate RADV instance probe has its own static report"
    hardware = "NOT TESTED"
}
$reportPath = Join-Path $build "compatibility.json"
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($Probe) {
    if ($Target -ne "Vulkan" -or $RestoreDependencies) { throw "-Probe cannot be combined with other build modes." }
    if (-not $env:VULKAN_SDK) { throw "VULKAN_SDK is required for the public API probe." }
    $object = Join-Path $build "link_probe.o"
    $compileArgs = @("--target=x86_64-sie-ps5", "-fPIC", "-fno-stack-protector", "-isystem",
        (Join-Path $baseSdk "target\include"), "-I", (Join-Path $env:VULKAN_SDK "Include"), "-c",
        (Join-Path $root "experiments\vulkan3d\ps5\link_probe.c"), "-o", $object)
    Invoke-Stage "probe-compile" (Join-Path $llvm "bin\clang.exe") $compileArgs | Out-Null
    Invoke-Stage "probe-header" (Join-Path $llvm "bin\llvm-readelf.exe") @("--file-header", $object) | Out-Null
    $linkArgs = @("-m", "elf_x86_64", "-pie", "-e", "echo_vulkan_link_probe", $object,
        "-o", (Join-Path $build "link_probe.elf"))
    $link = Invoke-Stage "probe-link" (Join-Path $llvm "bin\ld.lld.exe") $linkArgs -AllowFailure
    if ($link.ExitCode -eq 0 -or $link.Error -notmatch "undefined symbol: vkCreateInstance") {
        throw "Expected missing-driver link rejection was not observed."
    }
    $report["probe"] = "PS5 ELF object compiled; linking rejected undefined symbol: vkCreateInstance (no driver supplied)"
    $report["compileArguments"] = $compileArgs
    $report["linkArguments"] = $linkArgs
} else {
    foreach ($path in @((Join-Path $llvm "bin\clang.exe"), (Join-Path $baseSdk "target\include"))) {
        if (-not (Test-Path -LiteralPath $path)) { throw "Restore the existing native PS5 dependencies first: $path." }
    }
    $archive = Join-Path $root (".deps\downloads\" + $base.sdk.archive)
    if ((Get-FileHash -LiteralPath $archive).Hash -ne $base.sdk.sha256) { throw "SDK v0.42 checksum mismatch." }
    $source = Restore-Source "platform-source" $manifest.ps5Platform
    $rtSource = Restore-Source "llvm-source" $manifest.compilerRt @("compiler-rt", "cmake", "llvm/cmake", "llvm/include")
    New-Item -ItemType Directory -Path $sdk -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $baseSdk "target") -Destination $sdk -Recurse -Force
    $include = Join-Path $sdk "target\include"
    foreach ($directory in @("freebsd", "khronos")) {
        Get-ChildItem -LiteralPath (Join-Path $source "include\$directory") -Force |
            ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $include -Recurse -Force }
    }
    Copy-Item -LiteralPath (Join-Path $source "include\ps5") -Destination $include -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $source "platform\include\ps5platform") -Destination $include -Recurse -Force
    Materialize-Headers $source $include
    $toolchain = Join-Path $root "cmake\vulkan\ps5-runtime-windows.cmake"
    $common = @("-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_C_FLAGS_RELEASE=",
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain", "-DLLVM_ROOT=$llvm", "-DPS5_SDK_ROOT=$sdk")
    $platformBuild = Join-Path $build "platform"
    Invoke-Stage "platform-configure" "cmake" (@("-S", (Join-Path $root "experiments\vulkan3d\ps5\platform"),
        "-B", $platformBuild, "-DPLATFORM_SOURCE=$source") + $common) | Out-Null
    Invoke-Stage "platform-build" "cmake" @("--build", $platformBuild, "--verbose") | Out-Null
    Copy-Item -LiteralPath (Join-Path $platformBuild "libps5platform.a") -Destination (Join-Path $sdk "target\lib")
    $rtBuild = Join-Path $build "builtins"
    Invoke-Stage "builtins-configure" "cmake" (@("-S", (Join-Path $rtSource "compiler-rt\lib\builtins"),
        "-B", $rtBuild, "-DLLVM_RUNTIMES_BUILD=ON", "-DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON",
        "-DCOMPILER_RT_BAREMETAL_BUILD=OFF", "-DCOMPILER_RT_INCLUDE_TESTS=OFF") + $common) | Out-Null
    Invoke-Stage "builtins-build" "cmake" @("--build", $rtBuild, "--verbose") | Out-Null
    $builtins = Get-ChildItem -LiteralPath $rtBuild -Recurse -Filter "libclang_rt.builtins-x86_64.a"
    if (@($builtins).Count -ne 1) { throw "Expected exactly one PS5 ELF builtins archive." }
    Copy-Item -LiteralPath $builtins.FullName -Destination (Join-Path $sdk "target\lib")
    $inventory = [ordered]@{}
    foreach ($name in @("libc++.a", "libc++abi.a", "libunwind.a", "libps5platform.a", "libclang_rt.builtins-x86_64.a")) {
        $inventory[$name] = Inspect-Archive ($name -replace '[^a-zA-Z0-9]', '-') (Join-Path $sdk "target\lib\$name")
    }
    $symbolArgs = @("--defined-only", (Join-Path $sdk "target\lib\libclang_rt.builtins-x86_64.a"))
    $symbols = (Invoke-Stage "builtins-symbols" (Join-Path $llvm "bin\llvm-nm.exe") $symbolArgs).Output
    if ($symbols -notmatch '\b[TW] __emutls_get_address\b') { throw "ELF builtins lack emulated TLS support." }
    $report["runtimeArchives"] = $inventory
    $report["platformRevision"] = $manifest.ps5Platform.commit
    $report["compilerRtRevision"] = $manifest.compilerRt.commit
    $report["baseSdkSha256"] = $base.sdk.sha256
    $report["runtimeCompatibility"] = "ELF archive members statically verified; combined native app/runtime/hardware compatibility NOT TESTED"
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
    if ($Target -eq "Runtime") {
        if ($identity.saveRoot -ne "/data/echo-vulkan-lab" -or $identity.startupLog -ne "/data/echo-vulkan-lab/startup.log") {
            throw "Runtime probe paths must match the isolated experimental identity."
        }
        & (Join-Path $PSScriptRoot "build-ps5.ps1") -Target Hello
        $runtimeBuildName = if ($RuntimeHold) { "runtime-probe-hold" } else { "runtime-probe" }
        $runtimeBuild = Join-Path $build $runtimeBuildName
        $boilerplate = Join-Path $root ".deps\sources\boilerplate"
        $holdOption = if ($RuntimeHold) { "ON" } else { "OFF" }
        Invoke-Stage "runtime-configure" "cmake" (@("-S", (Join-Path $root "experiments\vulkan3d\ps5\runtime"),
            "-B", $runtimeBuild, "-DBOILERPLATE_SOURCE=$boilerplate",
            "-DECHO_PS5_RUNTIME_HOLD=$holdOption") + $common) | Out-Null
        Invoke-Stage "runtime-link" "cmake" @("--build", $runtimeBuild, "--verbose") | Out-Null
        $intermediate = Join-Path $runtimeBuild "echo_vulkan_runtime.llvm.elf"
        Invoke-Stage "runtime-readelf" (Join-Path $llvm "bin\llvm-readelf.exe") @("--file-header", "--program-headers",
            "--dynamic-table", "--dyn-symbols", $intermediate) | Out-Null
        $nativeTool = Join-Path $root "build\ps5\host\ps5-native-tool.exe"
        $elf = Join-Path $runtimeBuild "eboot.elf"
        Invoke-Stage "runtime-convert" $nativeTool @("link", "--in", $intermediate, "--out", $elf,
            "--stub-dir", (Join-Path $sdk "target\lib"), "--module-sdk", "0x02000009",
            "--companion-sdk", "0x08050001", "--file-name", "eboot.elf") | Out-Null
        $appRoot = if ($RuntimeHold) { "dist\vulkan-experiment\ps5-runtime-hold" } else { "dist\vulkan-experiment" }
        $app = Join-Path $root (Join-Path $appRoot $identity.titleId)
        New-Item -ItemType Directory -Force -Path $app, (Join-Path $app "sce_sys"),
            (Join-Path $app "sce_module"), (Join-Path $app "licenses") | Out-Null
        Invoke-Stage "runtime-sign" $nativeTool @("self", "--sign", "--in", $elf, "--out", (Join-Path $app "eboot.bin"),
            "--magic", "0x1D3D154F") | Out-Null
        Copy-Item -LiteralPath (Join-Path $root "build\ps5\runtime\libc-a.prx") -Destination (Join-Path $app "sce_module\libc.prx")
        $production.titleId = $identity.titleId
        $production.contentId = $identity.contentId
        $production.conceptId = $identity.conceptId
        $production.localizedParameters.'en-US'.titleName = "PROJECT ECHO Vulkan Runtime Probe"
        $paramJson = $production | ConvertTo-Json -Depth 8
        $paramPath = Join-Path $app "sce_sys\param.json"
        [IO.File]::WriteAllText($paramPath, $paramJson, (New-Object Text.UTF8Encoding($false)))
        $paramBytes = [IO.File]::ReadAllBytes($paramPath)
        if ($paramBytes.Length -lt 3 -or
            [BitConverter]::ToString($paramBytes, 0, 3) -eq "EF-BB-BF") {
            throw "Kraken Mount requires sce_sys\param.json as UTF-8 without a BOM."
        }
        Add-Type -AssemblyName System.Drawing
        $icon = New-Object Drawing.Bitmap(512, 512)
        $graphics = [Drawing.Graphics]::FromImage($icon)
        $brush = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(255, 180, 65))
        try {
            $graphics.Clear([Drawing.Color]::FromArgb(14, 18, 30))
            $graphics.FillRectangle($brush, 96, 96, 64, 320)
            $graphics.FillRectangle($brush, 352, 96, 64, 320)
            $graphics.FillRectangle($brush, 160, 352, 192, 64)
            $icon.Save((Join-Path $app "sce_sys\icon0.png"), [Drawing.Imaging.ImageFormat]::Png)
        } finally {
            $brush.Dispose(); $graphics.Dispose(); $icon.Dispose()
        }
        foreach ($notice in @(@((Join-Path $root "LICENSE"), "PROJECT_ECHO.txt"),
                @((Join-Path $boilerplate "LICENSE"), "boilerplate.txt"),
                @((Join-Path $source "LICENSE"), "PS5_PayloadSDK.txt"),
                @((Join-Path $rtSource "compiler-rt\LICENSE.TXT"), "compiler-rt.txt"),
                @((Join-Path $source "platform\src\regex\COPYRIGHT.musl"), "musl-regex.txt"))) {
            Copy-Item -LiteralPath $notice[0] -Destination (Join-Path $app ("licenses\" + $notice[1]))
        }
        $allocatorSource = [IO.File]::ReadAllText((Join-Path $source "platform\src\dlmalloc\malloc.c"))
        $allocatorNotice = [regex]::Match($allocatorSource, '(?s)\A/\*\s*(Copyright.*?)(?=\r?\n\* Version)')
        if (-not $allocatorNotice.Success) { throw "Pinned dlmalloc copyright/license notice was not found." }
        [IO.File]::WriteAllText((Join-Path $app "licenses\dlmalloc.txt"), $allocatorNotice.Groups[1].Value)
        & (Join-Path $PSScriptRoot "test-ps5-package.ps1") -PackagePath $app -LogDirectory $logs
        $validation = Get-Content -LiteralPath (Join-Path $logs ($identity.titleId + "-validation.json")) -Raw | ConvertFrom-Json
        $expectedModules = @("libkernel.prx", "libSceLibcInternal.prx")
        $unexpectedModules = @($validation.systemRuntimeModules | Where-Object { $_ -notin $expectedModules })
        if (@($validation.systemRuntimeModules).Count -ne 2 -or $unexpectedModules.Count) {
            throw "Runtime preflight must import only libkernel and libSceLibcInternal, never SDL/keyboard/IME/GPU modules."
        }
        $report["runtimeCompatibility"] = "Native _start application linked, converted and packaged; heap/C++/thread/TLS hardware checks NOT TESTED"
        $report["runtimeProbe"] = [ordered]@{ package = $app; ebootSha256 = (Get-FileHash (Join-Path $app "eboot.bin")).Hash;
            staticValidation = "PASS"; hardware = "NOT TESTED"; vulkan = "NOT INITIALIZED";
            termination = $(if ($RuntimeHold) { "DIAGNOSTIC HOLD; close manually" } else { "normal CRT exit" }) }
        $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $app "licenses\build-provenance.json") -Encoding UTF8
    }
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
if ($Probe) {
    Write-Output "Expected missing-driver failure reproduced; PS5 Vulkan remains BLOCKED."
} elseif ($Target -eq "Runtime") {
    Write-Output "Native PS5 runtime preflight built and statically validated: $app"
    Write-Output "Heap/C++/thread/TLS checks require a manual console run. This is NOT Vulkan rendering."
    if ($RuntimeHold) { Write-Output "Diagnostic hold remains open on success; close it manually on the console." }
} elseif ($Target -eq "Dependencies") {
    Write-Output "PS5 runtime archives cross-built/verified natively on Windows: $sdk"
    Write-Output "This is not a Vulkan application or PS5 hardware result; see $reportPath."
} else {
    throw "PS5 Vulkan remains BLOCKED after runtime preparation; see $reportPath."
}
