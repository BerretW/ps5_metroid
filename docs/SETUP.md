# Windows setup

## Required

- Windows 11 x64
- Git
- CMake 3.24 or newer
- Ninja
- Visual Studio C++ Build Tools with the x64 compiler and Windows SDK
- Windows PowerShell 5.1 or PowerShell 7
- Vulkan SDK with `glslangValidator` and `spirv-val` for the default Vulkan game build (not required for `-LegacySDL`)

The Windows target builds with the native Visual Studio C++ compiler. LLVM/Clang and LLD are not required for that target. The separate PS5 build uses locally extracted official Windows LLVM 18.1.8, SDK v0.42 target files and native MSVC-built packaging utilities.

Check the environment:

```powershell
.\scripts\doctor.ps1 -Target Windows
```

If needed, install the missing standalone tools with WinGet:

```powershell
winget install --id Git.Git --exact
winget install --id Kitware.CMake --exact
winget install --id Ninja-build.Ninja --exact
winget install --id Microsoft.PowerShell --exact
```

Install Visual Studio Build Tools from Microsoft's official installer and select the **Desktop development with C++** workload. Review installation selections before applying them.

Install the official Vulkan SDK for the default Windows build, for example:

```powershell
winget install --id KhronosGroup.VulkanSDK --exact
```

Select the SDK headers, x64 import library, `glslangValidator`, and `spirv-val`
if the installer offers component choices. A Vulkan driver or runtime alone
does not provide these development files. The installer sets `VULKAN_SDK`;
open a new terminal if necessary, or set `$env:VULKAN_SDK` to the SDK directory
for the current terminal. The Windows build and doctor also read the saved user
or machine value when the current process has no `VULKAN_SDK`. An explicitly
set process value takes precedence. Run the doctor again to check the files.
For an SDL-only environment, use `.\scripts\doctor.ps1 -Target Windows -LegacySDL`.
The separate PS5 Vulkan pipeline requires the specific SDK version pinned in
`experiments\vulkan3d\dependencies.json`.

## Build, test, run

For an interactive platform selector, double-click `build.bat` in the repository root, or run `.\build.bat`. Press **W** (or **1**) for Windows Vulkan Release plus tests, **P** (or **2**) for PS5, then choose **S** for SDL or **V** for Vulkan. Press **Q** to cancel either menu. On Czech or other layouts where the top row does not type digits, use the letters or the numpad; an unaccepted key only beeps. The selector calls the existing PowerShell scripts; all listed prerequisites still apply. The PS5 Vulkan option uses the prerequisites and native build path documented in [PS5 Vulkan build](experiments/vulkan3d/PS5_BUILD.md#ps5-vulkan-game-candidate).

```powershell
.\scripts\build-windows.ps1 -Configuration Debug -RunTests
.\build\windows\project_echo_vulkan.exe
```

Use `-Configuration Release` for an optimized build. CMake downloads only the exact SDL source revision declared in the root `CMakeLists.txt`; the SDL license is Zlib.
The build compiles and validates shaders into the executable's `assets\shaders`
directory. Add `-VulkanGpuTests` to `-RunTests` to launch the production game,
render, resize and render another menu frame, and close without accessing save data; this requires a
Windows GPU and display.
For the legacy SDL-only Windows build use `-LegacySDL -RunTests` instead.
The default configuration creates only the Vulkan game executable; the SDL
compatibility tests still compile the legacy renderer until the presentation
extraction is complete. A previous `project_echo.exe` may remain in an existing
build directory and is not evidence that the default build rebuilt it.

Logs are written to `logs\project_echo.log`. Print the latest lines with:

```powershell
.\scripts\collect-logs.ps1
```

No console IP, FTP credentials, or other local device settings are required for the Windows target.

## Native Windows PS5 build

```powershell
.\scripts\build-ps5.ps1 -Target Hello -RestoreDependencies
.\scripts\doctor.ps1 -Target PS5
.\scripts\build-ps5.ps1 -Target Game -RestoreDependencies
.\tests\ps5_package_tests.ps1 -Vulkan
```

The first restore downloads the official LLVM portable archive (about 982 MB), SDK v0.42 (about 9 MB), zlib source and pinned boilerplate source. The game additionally requires pinned SDL source. The default Vulkan build also requires the prepared RADV archive and host tools described in [PS5 Vulkan build](experiments/vulkan3d/PS5_BUILD.md#ps5-vulkan-game-candidate); the SDL software backend needs neither Mesa nor libsamplerate. Subsequent builds reuse caches and verify download hashes and Git revisions. No global LLVM installation, WSL, Bash, MinGW or Linux host is required.

Outputs are separate from the Windows executable: `dist\ps5-hello\PPSA99999` and `dist\ps5-vulkan\PPSA91007`. The explicit legacy controller build uses `dist\ps5-controller\PPSA91007`. Build commands and validation reports are in `build\ps5\logs`; compilation flags are retained in the respective build directories.

See [PS5 compatibility](PS5_COMPATIBILITY.md) before manual transfer. Do not upload a generic LLVM PIE to an ELF payload loader. The current operator uses Kraken Mount 0.3.1: transfer one complete native application directory, including its FSELF `eboot.bin` and clean-room `libc.prx`. For the legacy controller build use `.\scripts\build-ps5.ps1 -Target Game -LegacySDL -SkipPs5Keyboard`. Earlier controller and Vulkan builds were reported playable; physical PS5 keyboard and IME are disabled in these variants. The original keyboard-enabled game build fails on launch.
