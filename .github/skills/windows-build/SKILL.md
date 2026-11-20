---
name: windows-build
description: Configure, build and diagnose the native Windows CMake/Ninja target using PowerShell and Visual Studio C++ tools.
---

# Windows build workflow

## Activate when

Changing CMake, native Windows dependencies, build scripts, VS Code tasks, or compiler diagnostics.

## Procedure

1. Run `scripts/doctor.ps1 -Target Windows`.
2. Use `scripts/build-windows.ps1` for the supported CMake/Ninja configure and build.
3. Run the included CTest gameplay suite after the build.
4. For the usual regression check, run `scripts/build-windows.ps1 -Configuration Debug -RunTests`; launch `build/windows/project_echo.exe` for the SDL application.
5. Keep dependencies pinned and prefer the existing Visual Studio C++ toolchain for the Windows target. SDL is fetched at the exact revision in the root `CMakeLists.txt`.

## Project-specific lessons

- PROJECT ECHO is a playable prototype and the Windows build is the main development baseline. Keep gameplay in the shared `src/game/` library; keep SDL windowing, input and rendering in `src/platform/sdl/`.
- Gameplay tests are headless and do not require SDL initialization, a window, or a controller. Use them as the first regression check, but do not treat them as proof of interactive input or rendering behavior.
- PS5-only SDL patches and configuration belong to the separate PS5 build. Do not apply those patches to the Windows SDL source or change shared gameplay just to work around a PS5 backend issue.
- Build products, restored dependencies and logs are ignored under `build/`, `dist/`, `.deps/` and `logs/`; do not commit them.

## Constraints

- Do not make WSL, MinGW, MSYS2, Cygwin, Docker, or a VM a required build environment.
- Keep local-only paths and device settings out of checked-in files.
- Do not suppress CMake or compiler errors.

## References and validation

- `CMakeLists.txt`, `scripts/doctor.ps1`, `scripts/build-windows.ps1`
- `.vscode/tasks.json`, `.vscode/launch.json`
- `docs/SETUP.md`
