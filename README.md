# PROJECT ECHO

PROJECT ECHO is a playable 2D sci-fi Metroidvania prototype and the baseline for continued Windows development. Gameplay is shared C++20 code; the SDL2 application uses original placeholder geometry rather than third-party game assets.

## What this repository is ready for

- **Play and develop on Windows 11 x64:** build and run the SDL2 game, run headless gameplay tests, and extend the shared simulation or SDL front end.
- **Build a PS5 application on Windows:** the native PowerShell pipeline cross-compiles, links and statically validates the application package. The controller-only build has been reported playable on PS5 with firmware 13.60 and Kraken Mount 0.3.1; console upload is manual.
- **Use the tests as a gameplay safety net:** simulation tests run without SDL or a window and cover movement, combat, room progression, ability unlocks, damage and respawn.

This is a playable prototype, not a finished game. Settings do not persist, visuals are placeholders, and PS5 physical keyboard/IME are disabled because the keyboard-enabled build crashes during `sceKeyboardInit`. Full PS5 controller mappings/reconnect, audio, all-room progression, logging and exit/relaunch still need hardware verification. See [acceptance status](docs/STATUS.md) and [PS5 compatibility](docs/PS5_COMPATIBILITY.md).

## Current technical assessment

- Native Windows CMake/Ninja build: supported through installed Visual Studio C++ tools.
- SDL2: pinned to the PS5 SDL fork commit `ee4c47dc0d617b3bc8f35108f9956baf228a1322` (SDL 2.30.12).
- Native Windows PS5 cross-compilation and application-directory packaging: PASS for standalone Hello World and PROJECT ECHO.
- PS5 hardware (user-reported, firmware 13.60 / Kraken Mount 0.3.1): Hello World and the non-diagnostic PROJECT ECHO controller build launch; the user confirms the game is playable and supplied a gameplay screenshot. PS5 keyboard/IME are intentionally disabled in this build. The original keyboard-enabled build still fails with CE-108255-1. See [PS5 compatibility](docs/PS5_COMPATIBILITY.md).

See [setup](docs/SETUP.md), [architecture](docs/ARCHITECTURE.md), [controls](docs/CONTROLS.md), [PS5 compatibility](docs/PS5_COMPATIBILITY.md), and [acceptance status](docs/STATUS.md).

## Build and run on Windows

```powershell
.\scripts\doctor.ps1 -Target Windows
.\scripts\build-windows.ps1 -Configuration Debug -RunTests
.\build\windows\project_echo.exe
```

In VS Code, use **Build PROJECT ECHO (Debug)**, **Test PROJECT ECHO**, or the **PROJECT ECHO (Windows)** launch configuration. The first CMake configure fetches the pinned SDL source from GitHub.

## Build for PS5 on native Windows

The package tests use the original keyboard-enabled build as a static fixture. The last command builds the separate controller variant reported playable on hardware; do not install the keyboard-enabled fixture on the reported setup.

```powershell
.\scripts\build-ps5.ps1 -Target Hello -RestoreDependencies
.\scripts\build-ps5.ps1 -Target Game -RestoreDependencies
.\tests\ps5_package_tests.ps1
.\scripts\build-ps5.ps1 -Target Game -SkipPs5Keyboard
```

Dependencies are pinned in [dependencies.ps5.json](dependencies.ps5.json). LLVM is extracted locally; no machine-wide LLVM installation or POSIX host is used. The game command validates Hello before compiling SDL or the game.

Upload [dist/ps5-controller/PPSA91007](dist/ps5-controller/PPSA91007) manually as a complete native application directory for Kraken Mount. Its launcher is `eboot.bin`, with `sce_sys` and `sce_module/libc.prx`. This variant disables PS5 physical keyboard and IME, not Windows input. [scripts/deploy-ps5.ps1](scripts/deploy-ps5.ps1) defaults to validating this package and prints manual-upload instructions only; it never connects to the console. The original [dist/ps5/PPSA91007](dist/ps5/PPSA91007) remains separate.

## Prototype path

Landing Bay -> Maintenance Tunnel -> Power Station (locked) -> backtrack to the Research Laboratory -> collect Echo Step -> backtrack through Maintenance -> Power Station -> Central Chamber.

## How it works

`src/game/` contains the deterministic, shared gameplay simulation. `src/platform/sdl/` handles the window, input, rendering, menus, HUD and audio. The Windows application and headless tests link the same game library; PS5 uses the same gameplay code with a native SDL backend and a separate application toolchain/package. See [architecture](docs/ARCHITECTURE.md), [controls](docs/CONTROLS.md), and [development notes](docs/DEVELOPMENT.md).

## Repository map

- `src/game/` — gameplay state and simulation
- `src/platform/sdl/` — SDL application, input and rendering
- `tests/` — headless gameplay and PS5 package checks
- `scripts/` — Windows and PS5 build, diagnostics, validation and manual deployment helpers
- `platform/ps5/`, `cmake/`, `patches/` — PS5 target configuration and SDL adaptations
- `docs/` — setup, architecture, controls, compatibility and evidence-based status
- `LICENSE`, `licenses/` — project and third-party license notices

Generated builds, packages, logs and restored dependencies are excluded from Git. Recreate them with the commands above; see [Windows setup](docs/SETUP.md) and [development notes](docs/DEVELOPMENT.md).

## License

Original PROJECT ECHO code is licensed under the [MIT License](LICENSE), copyright 2026 SpoiledMouse. Third-party components and SDL-derived material retain their own licenses; SDL2 uses the [zlib license](licenses/SDL2.txt). The SDL patches in this repository modify the upstream source.

PS5 builds combine the game with the upstream GPL-3.0-or-later CRT/runtime. Distributing PS5 binaries requires compliance with the applicable third-party license and source obligations; the project's MIT license does not replace them. Include the relevant license notices when distributing builds. See [PS5 compatibility](docs/PS5_COMPATIBILITY.md) for details.
