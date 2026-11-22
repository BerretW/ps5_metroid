# Windows setup

## Required

- Windows 11 x64
- Git
- CMake 3.24 or newer
- Ninja
- Visual Studio C++ Build Tools with the x64 compiler and Windows SDK
- Windows PowerShell 5.1 or PowerShell 7

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

## Build, test, run

For an interactive platform selector, double-click `build.bat` in the repository root, or run `.\build.bat`. Press **W** (or **1**) for Windows Release plus tests, **P** (or **2**) for PS5, then choose **S** for SDL or **V** for Vulkan. Press **Q** to cancel either menu. On Czech or other layouts where the top row does not type digits, use the letters or the numpad; an unaccepted key only beeps. The selector calls the existing PowerShell scripts; all listed prerequisites still apply. The Vulkan option uses the prerequisites and native build path documented in [PS5 Vulkan build](experiments/vulkan3d/PS5_BUILD.md#ps5-vulkan-game-candidate).

```powershell
.\scripts\build-windows.ps1 -Configuration Debug -RunTests
.\build\windows\project_echo.exe
```

Use `-Configuration Release` for an optimized build. CMake downloads only the exact SDL source revision declared in the root `CMakeLists.txt`; the SDL license is Zlib.

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
.\tests\ps5_package_tests.ps1
```

The first restore downloads the official LLVM portable archive (about 982 MB), SDK v0.42 (about 9 MB), zlib source and pinned boilerplate source. The game additionally requires pinned SDL source. PacBrew archives were inspected but are not build dependencies: the software backend needs neither Mesa nor libsamplerate. Subsequent builds reuse caches and verify download hashes and Git revisions. No global LLVM installation, WSL, Bash, MinGW or Linux host is required.

Outputs are separate from the existing Windows executable: `dist\ps5-hello\PPSA99999` and `dist\ps5\PPSA91007`. Build commands and validation reports are in `build\ps5\logs`; exact compilation flags are also retained in `build\ps5\game\compile_commands.json` and Ninja response files.

See [PS5 compatibility](PS5_COMPATIBILITY.md) before manual transfer. Do not upload a generic LLVM PIE to an ELF payload loader. The current operator uses Kraken Mount 0.3.1: transfer the complete native application directory, including its FSELF `eboot.bin` and clean-room `libc.prx`. Use `.\scripts\build-ps5.ps1 -Target Game -SkipPs5Keyboard` for the separate controller build without diagnostic messages. The operator confirms this build launches and is playable on firmware 13.60; physical PS5 keyboard and IME are intentionally disabled. The original keyboard-enabled game build fails on launch.
