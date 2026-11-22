#requires -Version 5.1
param()
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$base = Join-Path $root "build\vulkan-ps5"
$target = Join-Path $base "radv-target-visibility"
$logs = Join-Path $base "logs"
$source = Join-Path $root ".deps\vulkan-ps5\mesa-source"
$sdk = Join-Path $root ".deps\vulkan-ps5\sdk"
$llvm = Join-Path $root ".deps\llvm-18.1.8"
$meson = Join-Path $root ".deps\vulkan-ps5\host-python\Scripts\meson.exe"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$cross = Join-Path $base "meson-ps5-cross.ini"
$revision = "1ba9874f478657ccf6da72ce55f0260164cc0bd0"
if ($env:OS -ne "Windows_NT" -or -not [Environment]::Is64BitProcess -or
    [Environment]::OSVersion.Version.Build -lt 22000) {
    throw "Native Windows 11 x64 is required."
}
foreach ($path in @($meson, $cross, "$base\host-tools-bin\mesa_clc.exe",
                    "$base\host-tools-bin\vtn_bindgen2.exe", "$base\host-tools-bin\prospero-lld.exe",
                    "$llvm\bin\ld.lld.exe", "$sdk\target\lib\libps5platform.a", $vswhere)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing prerequisite: $path" }
}
$head = & git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $head -ne $revision) { throw "Mesa source is not at the pinned revision." }
$dirty = & git -C $source status --porcelain
if ($LASTEXITCODE -ne 0 -or $dirty) { throw "Mesa source checkout must be clean." }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw "Visual Studio C++ tools were not found." }
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$settings = & $env:ComSpec /d /s /c "call `"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw "Could not initialize Visual Studio C++ tools." }
foreach ($line in $settings) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], "Process") }
}
$env:PATH = "$base\host-tools-bin;$root\.deps\vulkan-ps5\host-python\Scripts;$llvm\bin;$root\.deps\vulkan-ps5\host-llvm\bin;$env:PATH"
$env:ECHO_PS5_LLD = Join-Path $llvm "bin\ld.lld.exe"
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$log = Join-Path $logs "radv-visibility-build.log"
[IO.File]::WriteAllText($log, "")
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
    } finally { $process.Dispose() }
}
# The PS5 target's DLL-storage-class mode overrides even explicitly hidden weak
# dispatch declarations. Disable it so absent optional entrypoints resolve to NULL.
$include = (Join-Path $sdk "target\include").Replace("\", "\\").Replace("'", "\'")
$cArgs = "-Dc_args=['-isystem','$include','-fno-visibility-from-dllstorageclass','-fno-stack-protector','-fno-plt','-femulated-tls','-fdenormal-fp-math=ieee']"
$cppArgs = "-Dcpp_args=['-nostdinc++','-isystem','$include\\c++\\v1','-isystem','$include','-fno-visibility-from-dllstorageclass','-fno-stack-protector','-fno-plt','-femulated-tls','-fdenormal-fp-math=ieee','-fexceptions','-frtti']"
$options = @("-Dvulkan-drivers=amd", "-Dgallium-drivers=", "-Dplatforms=", "-Dradv-winsys=ps5",
    "-Dllvm=disabled", "-Damd-use-llvm=false", "-Dvideo-codecs=", "-Dbuildtype=debugoptimized",
    "-Db_ndebug=false", "-Dglx=disabled", "-Degl=disabled", "-Dgbm=disabled", "-Dopengl=false",
    "-Dgles1=disabled", "-Dgles2=disabled", "-Dvalgrind=disabled", "-Dlibunwind=disabled",
    "-Dzstd=disabled", "-Dzlib=disabled", "-Dexpat=disabled", "-Dxmlconfig=disabled",
    "-Dshader-cache=disabled", "-Dbuild-tests=false", "-Dvulkan-layers=", "-Dtools=", "-Dmesa-clc=system",
    "-Dradv-build-id=$revision", "-Dwrap_mode=nodownload", "--cross-file=$cross", $cArgs, $cppArgs)
if (Test-Path -LiteralPath (Join-Path $target "build.ninja")) {
    Invoke-Logged $meson (@("setup", "--reconfigure", $target, $source) + $options)
} else {
    Invoke-Logged $meson (@("setup", $target, $source) + $options)
}
Invoke-Logged "ninja.exe" @("-C", $target, "-j", "4", "-v", "src\amd\vulkan\libvulkan_radeon.a")
$archive = Join-Path $target "src\amd\vulkan\libvulkan_radeon.a"
if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) { throw "RADV archive was not produced." }
Write-Host "PS5 RADV archive: $archive"
Write-Host "SHA-256: $((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash)"
Write-Host "Archive build only; link, converter, package and hardware remain unverified."
