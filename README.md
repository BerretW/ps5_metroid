# PROJECT ECHO

PROJECT ECHO is a playable 2D sci-fi Metroidvania prototype and the reference integration game for **Echo Framework 0.2**, a modular C++20 foundation. Gameplay is shared code; the SDL2 application uses original procedurally generated PNG sprites and WAV sound effects (see [assets](docs/ASSETS.md)) rather than third-party game assets.

## What this repository is ready for

- **Play and develop on Windows 11 x64:** build and run the SDL2 game, run headless gameplay tests, and extend the shared simulation or SDL front end.
- **Build a PS5 application on Windows:** the native PowerShell pipeline cross-compiles, links and statically validates the application package. The `playable` milestone controller build has been reported playable on PS5 with firmware 13.60 and Kraken Mount 0.3.1; console upload is manual. The subsequent controller-symbol UI and map/back bindings need another hardware check.
- **Use the tests as a gameplay safety net:** simulation tests run without SDL or a window and cover movement, combat, room progression, ability unlocks, damage and respawn.
- **Build on a reusable framework core:** headless modules, entities/components, events, actions, typed state, conditions, bounded JSON/content validation and versioned state serialization are available independently of SDL. Dialogue, inventory, combat and other genre systems remain future modules.

This is a playable prototype, not a finished game. Besides the side-view route, the Landing Bay's west exit leads to a Diablo 2-style isometric Cargo Hold with twin-stick aiming (mouse on Windows, right stick on controllers), and Windows Options switches between 1280x720, 1920x1080 and fullscreen (PS5 is always fullscreen); see [controls](docs/CONTROLS.md). The SDL app uses platform save storage for a versioned gameplay autosave, a Continue menu entry, and separate persistent volume/keyboard settings. The operator confirms gameplay saving and settings persistence work on PS5; keyboard rebinding itself is untestable there while PS5 keyboard input is disabled. Full controller mappings/reconnect, audio, all-room progression, logging and exit/relaunch still need hardware verification. DualSense Lab provides live SDL diagnostics and guarded output tests; hardware-dependent results remain unverified until tested. See [Echo Framework](docs/framework/README.md), [save system](docs/framework/SAVE_SYSTEM.md), [DualSense Lab](docs/DUALSENSE_LAB.md), [compatibility](docs/DUALSENSE_COMPATIBILITY.md), [hardware checklist](docs/DUALSENSE_HARDWARE_TESTS.md), [acceptance status](docs/STATUS.md) and [PS5 compatibility](docs/PS5_COMPATIBILITY.md).

## Current technical assessment

- Native Windows CMake/Ninja build: supported through installed Visual Studio C++ tools.
- SDL2: pinned to the PS5 SDL fork commit `ee4c47dc0d617b3bc8f35108f9956baf228a1322` (SDL 2.30.12).
- Native Windows PS5 cross-compilation and application-directory packaging: PASS for standalone Hello World and PROJECT ECHO.
- Echo Framework core and data targets: see the current [framework validation status](docs/STATUS.md); save functionality has an operator-reported PS5 confirmation.
- PS5 hardware (user-reported, firmware 13.60 / Kraken Mount 0.3.1): Hello World and the `playable` milestone controller build launch; the user confirms the game is playable and supplied a gameplay screenshot. PS5 keyboard/IME are intentionally disabled in controller builds. The original keyboard-enabled build still fails with CE-108255-1. See [PS5 compatibility](docs/PS5_COMPATIBILITY.md).

See [setup](docs/SETUP.md), [architecture](docs/ARCHITECTURE.md), [Echo Framework](docs/framework/README.md), [controls](docs/CONTROLS.md), [DualSense Lab](docs/DUALSENSE_LAB.md), [PS5 compatibility](docs/PS5_COMPATIBILITY.md), and [acceptance status](docs/STATUS.md).

## Simple platform selector

Double-click [build.bat](build.bat), or run `.\build.bat` from a terminal:

- **W (or 1) - Windows:** Release build and tests, keyboard prompts and editable keyboard bindings. Existing Windows controller input remains supported.
- **P (or 2) - PS5:** restore pinned dependencies if needed, validate Hello first, then build the controller variant with keyboard/IME disabled. The game uses drawn PlayStation button symbols, touchpad for the map and circle to return from options or a paused game.
- **Q (or 0) - Cancel:** exit without building.

Use the letters on a Czech keyboard: its top row types `+ ě š` instead of `1 2 3` without Shift, so `choice` only beeps. Numpad digits also work.

The batch file only selects a platform and calls the existing PowerShell scripts. It reports failures, never runs a PS5 binary and never uploads to a console. The target itself selects its control scheme, so direct PowerShell builds behave the same way.

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

Talk to Engineer Mira in the Landing Bay, use left/right to choose a response, and confirm the Maintenance Key offer. Use it at the Research Laboratory door in the Maintenance Tunnel, collect Echo Step in the Lab, then backtrack through Maintenance and the Power Station to reach the Central Chamber.

## How it works

`src/game/` contains the deterministic, shared gameplay simulation. `src/platform/sdl/` handles the window, input, rendering, menus, HUD and audio; `src/input/` contains reusable diagnostics math and report serialization. The Windows application and headless tests link the same game library; PS5 uses the same gameplay code with a native SDL backend and a separate application toolchain/package. See [architecture](docs/ARCHITECTURE.md), [controls](docs/CONTROLS.md), and [development notes](docs/DEVELOPMENT.md).

`framework/core/` is a reusable SDL-free static library, `framework/data/` adds the pinned yyjson wrapper, content registry and save codec, and `framework/narrative/` supplies the dialogue module. The game adapter loads Mira's dialogue and interaction reach from `assets/framework/`, and her position from `assets/world/objects/engineer.json`, while `echo::Game` remains authoritative for gameplay. See [framework architecture](docs/framework/ARCHITECTURE.md), [dialogue format](docs/framework/NARRATIVE.md), and the [headless example](framework/examples/core_demo/).

## Repository map

- `src/game/` — gameplay state and simulation
- `src/platform/sdl/` — SDL application, input and rendering
- `src/input/` — controller diagnostic data and reporting
- `framework/` — reusable framework libraries, headless tests and example
- `tests/` — gameplay, integration, and PS5 package checks
- `assets/` — original sprites/sounds and the game module/content manifest
- `third_party/` — pinned vendored sources (stb_image PNG decoder)
- `scripts/` — Windows and PS5 build, diagnostics, validation and manual deployment helpers
- `platform/ps5/`, `cmake/`, `patches/` — PS5 target configuration and SDL adaptations
- `docs/` — setup, architecture, framework guides, controls, compatibility and evidence-based status
- `LICENSE`, `licenses/` — project and third-party license notices

Generated builds, packages, logs and restored dependencies are excluded from Git. Recreate them with the commands above; see [Windows setup](docs/SETUP.md) and [development notes](docs/DEVELOPMENT.md).

## License

Original PROJECT ECHO code is licensed under the [MIT License](LICENSE), copyright 2026 SpoiledMouse. Third-party components and SDL-derived material retain their own licenses; SDL2 uses the [zlib license](licenses/SDL2.txt) and the vendored stb_image decoder is [MIT or public domain](licenses/stb.txt). The SDL patches in this repository modify the upstream source.

PS5 builds combine the game with the upstream GPL-3.0-or-later CRT/runtime. Distributing PS5 binaries requires compliance with the applicable third-party license and source obligations; the project's MIT license does not replace them. Include the relevant license notices when distributing builds. See [PS5 compatibility](docs/PS5_COMPATIBILITY.md) for details.
