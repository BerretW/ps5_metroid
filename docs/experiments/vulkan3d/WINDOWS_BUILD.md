# Native Windows build

Prerequisites: Windows x64, installed Visual Studio C++ tools, CMake 3.24+,
Ninja, Git, and official Vulkan SDK **1.4.363.1**. The installed
`glslangValidator` is 16.6.0; the manifest pins its locally measured executable
SHA-256. `VULKAN_SDK` must name this SDK. No dependency installer changes global
configuration, and no PS5 toolchain is selected.

```powershell
.\scripts\build-vulkan-windows.ps1 -Configuration Debug -RunTests
.\scripts\build-vulkan-windows.ps1 -Configuration Release -RunTests
.\build\vulkan-windows\echo_vulkan3d.exe
.\build\vulkan-windows\echo_vulkan3d.exe --frames 120 --validation --resize-test --probe --capture build\vulkan-windows\frame.ppm
.\build\vulkan-windows\echo_vulkan3d.exe --frames 60 --validation --isometric --capture build\vulkan-windows\isometric.ppm
.\dist\vulkan-experiment\windows\echo_vulkan3d.exe --1080p --validation
.\dist\vulkan-experiment\windows\echo_vulkan_menu.exe --validation
.\dist\vulkan-experiment\windows\echo_vulkan_menu.exe --game --sprites --validation
.\dist\vulkan-experiment\windows\echo_vulkan_menu.exe --game --sprites --data-world --validation
```

The standalone CMake entry is `experiments/vulkan3d`, not the production root.
Shader modules compile and validate during the build with native
`glslangValidator` and `spirv-val`. Existing pinned yyjson sources may be reused
without touching the original build. Logs and compiler commands are retained
in `build/vulkan-windows/logs` and `compile_commands.json`.
The separate menu test also uses the same pinned SDL sources as the game.

This host's MSVC installation has only Czech compiler messages. The observed
CMake/Ninja include-prefix encoding mismatch caused stale header-dependent
objects. The experiment script measures the actual UTF-8 `/showIncludes`
prefix and passes it to CMake, preserving dependency tracking without changing
the original game's build or installing language packs. LLVM 18.1.8 `clang-cl`
was evaluated but rejected by the installed MSVC 14.51 STL, which requires
Clang 20 or newer; no compiler-version check is bypassed.

Space toggles perspective/isometric cameras; Escape exits. The application
draws a procedural textured cube, floor, alpha quad, original Echo sprite and
numeric FPS overlay. A bounded integration run uses real presentation, depth
and GPU transfers; CPU/virtual Vulkan devices are explicitly rejected.
`--probe` exercises stale handles, referenced texture lifetime and unsupported
capability rejection. `--resize-test` changes the client area to 960x540.
PPM capture is GPU readback **before** presentation, not a CPU scene renderer.

`echo_vulkan_menu.exe` records the real game's main menu and built-in gameplay
and draws them on Vulkan at 1280x720. Up/down changes
menu selection; Enter on NEW GAME starts the built-in simulation. Arrows move,
Space jumps, J shoots, and Escape returns to the menu or closes the diagnostic.
`--game` starts directly in the fallback scene; `--game --sprites` uploads and
draws the six original built-in sprite sheets. `--game --sprites --data-world`
loads the packaged framework configuration and JSON world, uploads its referenced
sprite sheets, and records the configured starting room through the same canvas.
This remains a separate Windows diagnostic, not the production game: dialogue,
sound, controller input, saves, other menu actions and additional screens are not
integrated. CTest checks the built-in sprite and compares a JSON-world GPU capture
against fallback art; SDL tests compare recorded fallback screens with original
SDL output and exercise atlas and world-tile submission.

Missing Vulkan runtime/layers/shaders or failed allocation returns an explicit
error. No software fallback is allowed. The debug run requires
`VK_LAYER_KHRONOS_validation`; driver/overlay warnings are retained separately
from validation errors. Integration runs create a real window and require a
working interactive Windows session and hardware GPU.

The build stages an executable-relative asset directory, shaders, manifest and
license notices in `dist/vulkan-experiment/windows`. This directory is separate
from both production outputs; copy the entire directory, not just the executable.
The default client target is 1280x720; `--1080p` selects 1920x1080. Resizing
recreates the presentation resources. No Vulkan SDK is required merely to run
the package without validation, but an installed hardware Vulkan driver and
the Microsoft Visual C++ x64 redistributable are. Release imports were inspected:
Vulkan loader, Windows/UCRT, MSVCP140 and VCRUNTIME140/140_1. Debug binaries use
the development-only Visual Studio debug CRT; do not redistribute those DLLs.
The staging directory reflects whichever configuration was built most recently.

CTest also checks a 960x540 GPU readback with a dependency fixture. Broad image
regions must retain at least 100 warm cube pixels, 100 blue cube pixels, 200
floor pixels, 50 HUD pixels and 100 original-sprite cyan pixels, using bounded
channel tolerances. This detects empty/incorrect scene output without requiring
bit-identical vendor rasterization. It is not a Vulkan conformance test or a
complete lighting/depth image oracle. CPU Vulkan devices cannot satisfy the
integration test.
