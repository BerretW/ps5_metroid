#requires -Version 5.1
param([switch]$Native, [switch]$Package, [switch]$Display, [switch]$Frame, [switch]$Cube, [switch]$Interactive)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build\vulkan-ps5"
$logs = Join-Path $build "logs"
$llvm = Join-Path $root ".deps\llvm-18.1.8\bin"
$sdk = Join-Path $root ".deps\vulkan-ps5\sdk\target"
$source = Join-Path $root ".deps\vulkan-ps5\vulkan-source"
$nativeDir = Join-Path $root ".deps\sources\boilerplate\tooling\native"
$archive = Join-Path $build "radv-target-visibility\src\amd\vulkan\libvulkan_radeon.a"
$work = Join-Path $build "radv-import-stubs"
$variant = if ($Interactive) { "cube-input" } elseif ($Cube) { "cube" } elseif ($Frame) { "frame" } elseif ($Display) { "display" } else { "instance" }
$useCube = $Cube -or $Interactive
$log = Join-Path $logs $(if ($Native) { "radv-$variant-link.log" } else { "radv-link-bound.log" })
$pin = "7f492b4c6f67a4e8c8c67a4272cfeef22abea9cc"
$mesaPin = "1ba9874f478657ccf6da72ce55f0260164cc0bd0"
if ($env:OS -ne "Windows_NT" -or -not [Environment]::Is64BitProcess -or
    [Environment]::OSVersion.Version.Build -lt 22000) {
    throw "Native Windows 11 x64 is required."
}
if ($Package -and -not $Native) { throw "-Package requires -Native." }
if ($Display -and -not $Native) { throw "-Display requires -Native." }
if ($Frame -and (-not $Native -or $Display)) { throw "-Frame requires -Native and cannot be combined with -Display." }
if ($Cube -and (-not $Native -or $Display -or $Frame)) { throw "-Cube requires -Native and cannot be combined with -Display or -Frame." }
if ($Interactive -and (-not $Native -or $Display -or $Frame -or $Cube)) {
    throw "-Interactive requires -Native and cannot be combined with -Display, -Frame or -Cube."
}
foreach ($path in @($archive, "$llvm\clang.exe", "$llvm\ld.lld.exe", "$llvm\llvm-nm.exe", "$llvm\llvm-readelf.exe",
                    "$sdk\lib\libps5platform.a", "$nativeDir\ps5-pie.ld")) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing prerequisite: $path" }
}
$revision = & git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $revision -ne $pin) { throw "PS5_Vulkan source is not at the pinned revision." }
$dirty = & git -C $source status --porcelain
if ($LASTEXITCODE -ne 0 -or $dirty) { throw "PS5_Vulkan source checkout must be clean." }
$mesa = Join-Path $root ".deps\vulkan-ps5\mesa-source"
$revision = & git -C $mesa rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $revision -ne $mesaPin) { throw "Mesa source is not at the pinned revision." }
$dirty = & git -C $mesa status --porcelain
if ($LASTEXITCODE -ne 0 -or $dirty) { throw "Mesa source checkout must be clean." }
$options = Get-Content -LiteralPath (Join-Path $build "radv-target-visibility\meson-info\intro-buildoptions.json") -Raw | ConvertFrom-Json
foreach ($name in @("c_args", "cpp_args")) {
    $optionRows = @($options | Where-Object { $_.name -eq $name })
    if ($optionRows.Count -ne 1 -or $optionRows[0].value -notcontains "-fno-visibility-from-dllstorageclass" -or
        $optionRows[0].value -contains "-fvisibility-nodllstorageclass=default") {
        throw "RADV compiler options do not preserve explicit hidden weak visibility: $name"
    }
}
New-Item -ItemType Directory -Force -Path $work, $logs | Out-Null

function Invoke-Logged([string]$Executable, [string[]]$Arguments) {
    $quoted = @($Arguments | ForEach-Object {
        if ($_ -match '[\r\n]') { throw "Invalid newline in argument." }
        '"' + ([regex]::Replace(([regex]::Replace($_, '(\\*)"', '$1$1\"')), '(\\+)$', '$1$1')) + '"'
    }) -join " "
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $Executable
    $info.Arguments = $quoted
    $info.WorkingDirectory = $root
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($info)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $output = $stdout.Result + $stderr.Result
        [IO.File]::AppendAllText($log, "`"$Executable`" $quoted`r`n$output`r`n")
        if ($process.ExitCode -ne 0) { throw "$Executable failed ($($process.ExitCode)); see $log.`n$output" }
        return $stdout.Result
    } finally { $process.Dispose() }
}
[IO.File]::WriteAllText($log, "")
if ($useCube) {
    $manifest = Get-Content -LiteralPath "$root\experiments\vulkan3d\dependencies.json" -Raw | ConvertFrom-Json
    if (-not $env:VULKAN_SDK) { throw "VULKAN_SDK must identify the installed Windows Vulkan SDK." }
    $glslang = Join-Path $env:VULKAN_SDK "Bin\glslangValidator.exe"
    $spirvVal = Join-Path $env:VULKAN_SDK "Bin\spirv-val.exe"
    if (-not (Test-Path -LiteralPath $glslang -PathType Leaf) -or
        -not (Test-Path -LiteralPath $spirvVal -PathType Leaf)) { throw "Missing Vulkan SDK shader tools." }
    if ((Get-FileHash -LiteralPath $glslang -Algorithm SHA256).Hash -ne $manifest.vulkanSdk.glslangSha256) {
        throw "glslangValidator does not match the recorded compiler SHA-256."
    }
    $header = New-Object Text.StringBuilder
    [void]$header.AppendLine("#pragma once")
    [void]$header.AppendLine("#include <stdint.h>")
    foreach ($stage in @("vert", "frag")) {
        $shader = "$root\experiments\vulkan3d\ps5\radv\cube.$stage"
        $spirv = Join-Path $work "$variant.$stage.spv"
        $shaderArgs = @("-V", "--target-env", "vulkan1.0", "-o", $spirv)
        if ($Interactive) { $shaderArgs += "-DECHO_CUBE_INTERACTIVE=1" }
        Invoke-Logged $glslang ($shaderArgs + @($shader)) | Out-Null
        Invoke-Logged $spirvVal @("--target-env", "vulkan1.0", $spirv) | Out-Null
        $bytes = [IO.File]::ReadAllBytes($spirv)
        if ($bytes.Length -lt 20 -or $bytes.Length % 4 -ne 0 -or
            [BitConverter]::ToUInt32($bytes, 0) -ne 0x07230203) { throw "Invalid SPIR-V: $spirv" }
        [void]$header.AppendLine("static const uint32_t echo_cube_$($stage)_spv[] = {")
        for ($i = 0; $i -lt $bytes.Length; $i += 4) {
            [void]$header.AppendFormat("0x{0:x8}u,", [BitConverter]::ToUInt32($bytes, $i))
            if (($i / 4) % 8 -eq 7) { [void]$header.AppendLine() }
        }
        [void]$header.AppendLine("};")
    }
    $headerName = if ($Interactive) { "cube_input_spirv.h" } else { "cube_spirv.h" }
    [IO.File]::WriteAllText((Join-Path $work $headerName), $header.ToString(), [Text.Encoding]::ASCII)
}
if ($Interactive) {
    $sdl = Join-Path $root ".deps\sources\sdl"
    $sdlPin = "ee4c47dc0d617b3bc8f35108f9956baf228a1322"
    $revision = & git -C $sdl rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $revision -ne $sdlPin) { throw "SDL source is not at the pinned revision." }
    $dirty = & git -C $sdl status --porcelain
    if ($LASTEXITCODE -ne 0 -or $dirty) { throw "SDL source checkout must be clean." }
    foreach ($path in @("$sdl\src\joystick\ps5\SDL_ps5joystick.h", "$sdl\LICENSE.txt",
                        "$sdk\lib\libScePad.so", "$sdk\lib\libSceUserService.so")) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing controller prerequisite: $path" }
    }
}
$headers = Invoke-Logged "$llvm\llvm-readelf.exe" @("--file-header", $archive)
$lines = @($headers -split "`r?`n")
$members = @($lines | Where-Object { $_ -match '^\s*Class:\s+ELF64$' }).Count
if ($members -eq 0 -or
    @($lines | Where-Object { $_ -match '^\s*Class:' }).Count -ne $members -or
    @($lines | Where-Object { $_ -match '^\s*Type:\s+REL\b' }).Count -ne $members -or
    @($lines | Where-Object { $_ -match '^\s*Machine:\s+Advanced Micro Devices X86-64$' }).Count -ne $members -or
    @($lines | Where-Object { $_ -match '^\s*Data:\s+2.s complement, little endian$' }).Count -ne $members) {
    throw "RADV archive has missing or incompatible object headers."
}
[IO.File]::AppendAllText($log, "RADV archive members: $members SHA-256: $((Get-FileHash $archive -Algorithm SHA256).Hash)`r`n")
$undefined = Invoke-Logged "$llvm\llvm-nm.exe" @("-A", "--undefined-only", $archive)
$relevant = @($undefined -split "`r?`n" | Where-Object { $_ -match '\bU (mkstemp|readlink)$' })
if ($relevant.Count -ne 2 -or
    @($relevant | Where-Object { $_ -match ':ac_gpu_info\.c\.o:\s+U readlink$' }).Count -ne 1 -or
    @($relevant | Where-Object { $_ -match ':aco_print_asm\.cpp\.o:\s+U mkstemp$' }).Count -ne 1) {
    throw "RADV filesystem imports differ from the audited optional diagnostic call sites."
}

$stubPaths = @()
foreach ($stub in @(
    @{ Name = "libSceAgc"; Source = "agc_canary_link_stub.c" },
    @{ Name = "libSceAgcDriver"; Source = "agc_driver_canary_link_stub.c" }
)) {
    $obj = Join-Path $work "$($stub.Name).o"
    $shared = Join-Path $work "$($stub.Name).so"
    Invoke-Logged "$llvm\clang.exe" @("--target=x86_64-sie-ps5", "-O2", "-fPIC",
        "-fno-visibility-from-dllstorageclass", "-fvisibility=default", "-fno-stack-protector",
        "-c", "$source\vendor\ps5\sdk\stubs\$($stub.Source)", "-o", $obj) | Out-Null
    Invoke-Logged "$llvm\ld.lld.exe" @("--shared", "-soname", "$($stub.Name).prx", "-o", $shared, $obj) | Out-Null
    $stubPaths += $shared
}
$compat = Join-Path $work "compat.o"
$probe = Join-Path $work $(if ($Native) { "$variant-probe.o" } else { "probe.o" })
Invoke-Logged "$llvm\clang.exe" @("--target=x86_64-sie-ps5", "-O2", "-fPIC", "-fno-stack-protector",
    "-isystem", "$sdk\include", "-Wall", "-Wextra", "-Werror", "-c",
    "$root\experiments\vulkan3d\ps5\radv\compat.c", "-o", $compat) | Out-Null
$probeSource = if ($Native) { "$root\experiments\vulkan3d\ps5\radv\instance_probe.c" } else {
    "$root\experiments\vulkan3d\ps5\radv_link_probe.c"
}
$probeFlags = if ($Interactive) { @("-DECHO_RADV_DISPLAY_PROBE", "-DECHO_RADV_FRAME_PROBE", "-DECHO_RADV_CUBE_PROBE", "-DECHO_RADV_CUBE_INTERACTIVE") } elseif ($Cube) { @("-DECHO_RADV_DISPLAY_PROBE", "-DECHO_RADV_FRAME_PROBE", "-DECHO_RADV_CUBE_PROBE") } elseif ($Frame) { @("-DECHO_RADV_DISPLAY_PROBE", "-DECHO_RADV_FRAME_PROBE") } elseif ($Display) { @("-DECHO_RADV_DISPLAY_PROBE") } else { @() }
Invoke-Logged "$llvm\clang.exe" (@("--target=x86_64-sie-ps5", "-O2", "-fPIC", "-fno-stack-protector",
    "-fno-visibility-from-dllstorageclass", "-femulated-tls", "-fdenormal-fp-math=ieee",
    "-isystem", "$sdk\include", "-I", "$root\.deps\vulkan-ps5\mesa-source\include", "-Wall", "-Wextra",
    "-Werror", "-c", $probeSource, "-o", $probe) + $probeFlags) | Out-Null
$objects = @($probe, $compat)
$entry = "echo_radv_link_probe"
if ($Native) {
    $entry = "_start"
    if ($Display -or $Frame -or $useCube) {
        $displayObject = Join-Path $work "$variant-capabilities.o"
        Invoke-Logged "$llvm\clang.exe" (@("--target=x86_64-sie-ps5", "-O2", "-fPIC", "-fno-stack-protector",
            "-fno-visibility-from-dllstorageclass", "-femulated-tls", "-fdenormal-fp-math=ieee",
            "-isystem", "$sdk\include", "-I", "$mesa\include", "-Wall", "-Wextra", "-Werror", "-c",
            "$root\experiments\vulkan3d\ps5\radv\display_probe.c", "-o", $displayObject) + $probeFlags) | Out-Null
        $objects += $displayObject
    }
    if ($Frame -or $useCube) {
        $frameObject = Join-Path $work "$variant-render.o"
        Invoke-Logged "$llvm\clang.exe" (@("--target=x86_64-sie-ps5", "-O2", "-fPIC", "-fno-stack-protector",
            "-fno-visibility-from-dllstorageclass", "-femulated-tls", "-fdenormal-fp-math=ieee",
            "-isystem", "$sdk\include", "-I", "$mesa\include", "-Wall", "-Wextra", "-Werror", "-c",
            "$root\experiments\vulkan3d\ps5\radv\frame_probe.c", "-o", $frameObject) + $probeFlags) | Out-Null
        $objects += $frameObject
    }
    if ($useCube) {
        $cubeObject = Join-Path $work "$variant-pipeline.o"
        Invoke-Logged "$llvm\clang.exe" (@("--target=x86_64-sie-ps5", "-O2", "-fPIC", "-fno-stack-protector",
            "-fno-visibility-from-dllstorageclass", "-femulated-tls", "-fdenormal-fp-math=ieee",
            "-isystem", "$sdk\include", "-I", "$mesa\include", "-I", $work, "-Wall", "-Wextra", "-Werror", "-c",
            "$root\experiments\vulkan3d\ps5\radv\cube_probe.c", "-o", $cubeObject) + $probeFlags) | Out-Null
        $objects += $cubeObject
    }
    if ($Interactive) {
        foreach ($name in @("cube_loop", "cube_pad")) {
            $obj = Join-Path $work "cube-input-$name.o"
            Invoke-Logged "$llvm\clang.exe" @("--target=x86_64-sie-ps5", "-O2", "-fPIC", "-fno-stack-protector",
                "-fno-visibility-from-dllstorageclass", "-femulated-tls", "-fdenormal-fp-math=ieee",
                "-isystem", "$sdk\include", "-I", "$mesa\include", "-I", "$sdl\src\joystick\ps5",
                "-Wall", "-Wextra", "-Werror", "-c", "$root\experiments\vulkan3d\ps5\radv\$name.c", "-o", $obj) | Out-Null
            $objects += $obj
        }
    }
    foreach ($name in @("app_crt", "app_cpp_runtime")) {
        $obj = Join-Path $work "$name.o"
        Invoke-Logged "$llvm\clang++.exe" @("--target=x86_64-sie-ps5", "-std=c++20", "-O2", "-fPIC",
            "-fno-stack-protector", "-fno-visibility-from-dllstorageclass", "-femulated-tls",
            "-fdenormal-fp-math=ieee", "-fno-exceptions", "-fno-rtti", "-nostdinc++",
            "-isystem", "$sdk\include\c++\v1", "-isystem", "$sdk\include", "-Wall", "-Wextra",
            "-c", "$nativeDir\$name.cpp", "-o", $obj) | Out-Null
        $objects += $obj
    }
}

$wraps = @("malloc", "calloc", "realloc", "free", "posix_memalign", "aligned_alloc", "memalign",
    "malloc_usable_size", "reallocf", "reallocarray", "getline", "getdelim",
    "pthread_create", "pthread_join", "pthread_detach", "fclose", "fflush")
$aliases = @("qsort_r", "mkstemps", "openlog", "popen", "pclose", "open_memstream", "__xuname",
    "__assert", "__memset_chk", "regcomp", "regexec", "regfree", "regerror", "localtime_r",
    "newlocale", "freelocale", "strtod_l", "strtof_l", "dladdr", "utimensat", "localeconv_l",
    "strtoll_l", "strtoull_l", "strtold_l", "snprintf_l", "sscanf_l", "asprintf_l", "strcoll_l",
    "strxfrm_l", "strftime_l", "wcscoll_l", "wcsxfrm_l", "btowc_l", "wctob_l", "iswctype_l",
    "mbrlen_l", "mbrtowc_l", "mbsrtowcs_l", "mbsnrtowcs_l", "wcrtomb_l", "wcsnrtombs_l",
    "mbtowc_l", "___mb_cur_max_l", "___runetype_l", "___tolower_l", "___toupper_l", "__runes_for_locale",
    "catopen", "catgets", "catclose", "backtrace", "backtrace_symbols_fd", "__cxa_thread_atexit_impl",
    "arc4random", "arc4random_buf", "arc4random_uniform", "gmtime_r", "statvfs", "fstatvfs",
    "futimens", "clock_nanosleep", "getaddrinfo", "freeaddrinfo", "if_nameindex", "if_freenameindex",
    "opendir", "fdopendir", "readdir", "rewinddir", "dirfd", "closedir", "nl_langinfo", "nl_langinfo_l",
    "getpwuid_r", "posix_fallocate", "access", "openat", "unlinkat", "fchmodat", "fstatat", "mkdirat",
    "renameat", "memfd_create")
$map = Join-Path $work "aliases.map"
$localNames = @($aliases) + @("mkstemp", "readlink", "echo_radv_*")
[IO.File]::WriteAllText($map, "{`n local:`n" + (($localNames | ForEach-Object { "  $_;" }) -join "`n") + "`n};`n")
$flags = @($wraps | ForEach-Object { "--wrap=$_" }) +
    @($aliases | ForEach-Object { "--defsym=$_=ps5_$_" }) +
    @("--defsym=mkstemp=echo_radv_mkstemp", "--defsym=readlink=echo_radv_readlink")
$output = Join-Path $work $(if ($Native) { "$variant-probe.llvm.elf" } else { "link-probe.elf" })
$padStubs = if ($Interactive) { @("$sdk\lib\libScePad.so", "$sdk\lib\libSceUserService.so") } else { @() }
$arguments = @("-m", "elf_x86_64", "-pie", "--eh-frame-hdr", "-z", "max-page-size=0x4000",
    "-mllvm", "-emulated-tls", "--hash-style=gnu", "-T",
    "$root\experiments\vulkan3d\ps5\runtime\runtime-layout.ld", "-L", $nativeDir, "-L", "$sdk\lib",
    "-e", $entry,
    "--error-limit=0", "--exclude-libs=ALL", "--version-script", $map,
    "--version-script", "$root\experiments\vulkan3d\ps5\runtime\runtime-symbols.map") + $flags +
    $objects + $stubPaths + @("--whole-archive", $archive, "--no-whole-archive", "--start-group",
    "$sdk\lib\libc++.a", "$sdk\lib\libc++abi.a", "$sdk\lib\libunwind.a",
    "$sdk\lib\libclang_rt.builtins-x86_64.a", "$sdk\lib\libps5platform.a", "--end-group", "--as-needed",
    "$sdk\lib\libkernel.so", "$sdk\lib\libSceLibcInternal.so", "$sdk\lib\libSceVideoOut.so",
    "$sdk\lib\libSceSystemService.so") + $padStubs + @("-o", $output)
Invoke-Logged "$llvm\ld.lld.exe" $arguments | Out-Null
$dynamicSymbols = Invoke-Logged "$llvm\llvm-readelf.exe" @("--dyn-syms", $output)
if ($dynamicSymbols -match '(?m)\bWEAK\s+DEFAULT\s+UND\s+\S+' -or
    $dynamicSymbols -match '(?m)\bUND\s+(mkstemp|readlink|strcasestr|link|symlink|qsort_s)\s*$') {
    throw "RADV ELF has unresolved weak symbols or known unsafe system imports."
}
if ($Native) {
    $tool = Join-Path $root "build\ps5\host\ps5-native-tool.exe"
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Missing native converter: $tool" }
    $converted = Join-Path $work "$variant-probe.sce.elf"
    Invoke-Logged $tool @("link", "--in", $output, "--out", $converted, "--stub-dir", "$sdk\lib",
        "--stub", $stubPaths[0], "--stub", $stubPaths[1], "--module-sdk", "0x02000009",
        "--companion-sdk", "0x08050001", "--file-name", "eboot.elf") | Out-Null
    Write-Host "Experimental native $variant probe converted: $converted"
    if ($Package) {
        $identity = Get-Content -LiteralPath "$root\experiments\vulkan3d\ps5\identity.json" -Raw | ConvertFrom-Json
        if ($identity.titleId -ne "PPSA91008" -or $identity.contentId -ne "UP9000-PPSA91008_00-ECHOVULKAN000001") {
            throw "Unexpected experimental identity."
        }
        $template = Join-Path $root "dist\vulkan-experiment\ps5-runtime-hold\PPSA91008"
        & "$root\scripts\test-ps5-package.ps1" -PackagePath $template -LogDirectory (Join-Path $logs "radv-template")
        $app = Join-Path $root "dist\vulkan-experiment\ps5-radv-$variant\PPSA91008"
        $marker = Join-Path $app "licenses\echo-radv-$variant.txt"
        $markerText = "PROJECT_ECHO_RADV_$($variant.ToUpperInvariant())_V1"
        if ((Test-Path -LiteralPath $app) -and
            (-not (Test-Path -LiteralPath $marker -PathType Leaf) -or
             (Get-Content -LiteralPath $marker -Raw).Trim() -ne $markerText)) {
            throw "Refusing to overwrite an unrecognized existing package: $app"
        }
        New-Item -ItemType Directory -Force -Path $app, "$app\sce_sys", "$app\sce_module", "$app\licenses" | Out-Null
        Invoke-Logged $tool @("self", "--sign", "--in", $converted, "--out", "$app\eboot.bin",
            "--magic", "0x1D3D154F") | Out-Null
        Copy-Item -LiteralPath "$template\sce_module\libc.prx" -Destination "$app\sce_module\libc.prx" -Force
        Copy-Item -LiteralPath "$template\sce_sys\icon0.png" -Destination "$app\sce_sys\icon0.png" -Force
        $param = Get-Content -LiteralPath "$template\sce_sys\param.json" -Raw | ConvertFrom-Json
        $param.localizedParameters.'en-US'.titleName = "PROJECT ECHO RADV $variant Probe"
        [IO.File]::WriteAllText("$app\sce_sys\param.json", ($param | ConvertTo-Json -Depth 8),
            (New-Object Text.UTF8Encoding($false)))
        Get-ChildItem -LiteralPath "$template\licenses" -File | Where-Object { $_.Name -ne "build-provenance.json" } |
            ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination "$app\licenses" -Force }
        Copy-Item -LiteralPath "$source\LICENSE" -Destination "$app\licenses\PS5_Vulkan.txt" -Force
        Copy-Item -LiteralPath "$mesa\docs\license.rst" -Destination "$app\licenses\Mesa-license.rst" -Force
        if ($Interactive) { Copy-Item -LiteralPath "$sdl\LICENSE.txt" -Destination "$app\licenses\SDL2.txt" -Force }
        $mesaLicenses = Join-Path $mesa "licenses"
        foreach ($license in Get-ChildItem -LiteralPath $mesaLicenses -Recurse -File) {
            $relative = $license.FullName.Substring($mesaLicenses.Length).TrimStart('\')
            $destination = Join-Path "$app\licenses\Mesa" $relative
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
            Copy-Item -LiteralPath $license.FullName -Destination $destination -Force
        }
        [IO.File]::WriteAllText($marker, $markerText)
        $provenance = [ordered]@{
            mesaRevision = $mesaPin
            vulkanRevision = $pin
            platformRevision = "b83202be73e930050e00de0f1b4d9ec46c0391bf"
            radvArchiveSha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
            ebootSha256 = (Get-FileHash -LiteralPath "$app\eboot.bin" -Algorithm SHA256).Hash
            purpose = $(if ($Interactive) { "RADV joystick-controlled perspective cube diagnostic" } elseif ($Cube) { "RADV static perspective cube and presentation diagnostic" } elseif ($Frame) { "RADV first-frame clear and presentation diagnostic" } elseif ($Display) { "RADV device and display capability probe; no rendering" } else {
                "RADV instance and physical-device enumeration only; no rendering" })
            hardware = "NOT TESTED"
        }
        if ($useCube) {
            $provenance["shaderCompilerSha256"] = (Get-FileHash -LiteralPath $glslang -Algorithm SHA256).Hash
            $provenance["vertexSpirvSha256"] = (Get-FileHash -LiteralPath "$work\$variant.vert.spv" -Algorithm SHA256).Hash
            $provenance["fragmentSpirvSha256"] = (Get-FileHash -LiteralPath "$work\$variant.frag.spv" -Algorithm SHA256).Hash
        }
        if ($Interactive) {
            $provenance["sdlRevision"] = $sdlPin
            $provenance["sdlPadHeaderSha256"] = (Get-FileHash -LiteralPath "$sdl\src\joystick\ps5\SDL_ps5joystick.h" -Algorithm SHA256).Hash
        }
        [IO.File]::WriteAllText("$app\licenses\build-provenance.json", ($provenance | ConvertTo-Json -Depth 4),
            (New-Object Text.UTF8Encoding($false)))
        & "$root\scripts\test-ps5-package.ps1" -PackagePath $app -LogDirectory (Join-Path $logs "radv-$variant") -ExperimentalRadv -ExperimentalRadvPad:$Interactive
        Write-Host "Experimental $variant probe package: $app"
        Write-Host "Manual console test only. Hardware execution is NOT TESTED."
    } else {
        Write-Host "Not packaged or hardware tested; executable format and imports still require validation. Commands: $log"
    }
} else {
    Write-Host "Link-only RADV ELF produced: $output"
    Write-Host "Not a native application, FSELF, package, or hardware validation. Commands: $log"
}
