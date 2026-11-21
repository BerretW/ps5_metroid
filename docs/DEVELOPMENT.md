# Development notes

## Verified workflow

Use the native Windows entry points:

```powershell
.\scripts\doctor.ps1 -Target Windows
.\scripts\build-windows.ps1 -Configuration Debug -RunTests
```

The Windows app and gameplay tests compile the same `echo_game` library. The test executable does not initialize SDL or create a window.

`build.bat` is a thin interactive front end: Windows selects Release plus tests, while PS5 selects the no-keyboard controller build and restores missing pinned dependencies. It never launches a PS5 executable or deploys to the console.

The additional `echo_control_prompts` test uses SDL's dummy video driver and software rendering, not a desktop window or physical controller. It checks that Windows prompts follow rebound keys and PlayStation prompts render controller symbols independently of keyboard bindings.

`echo_dualsense_diagnostics` tests simulated axis normalization, dead-zone behavior, trigger/touch coordinate bounds, short effect-duration limits, and report escaping/status separation. The SDL renderer tests also cover the menu entry and live button/stick visualization. These tests do not require a physical controller. The integrated DualSense Lab is documented in [DUALSENSE_LAB.md](DUALSENSE_LAB.md); hardware claims must follow its checklist and remain NOT TESTED until confirmed.

## Playable development baseline

The project is at a playable prototype milestone and is suitable as the starting point for further Windows development. The Windows application builds and launches, while the shared simulation has headless tests for movement, combat, the five-room route, Echo Step progression, damage, death and respawn. Run the application and tests before and after gameplay changes:

```powershell
.\scripts\build-windows.ps1 -Configuration Debug -RunTests
.\build\windows\project_echo.exe
```

Gameplay belongs in `src/game/`; SDL-specific input and presentation belong in `src/platform/sdl/`. Keep simulation behavior testable without opening a window. The PS5 target reuses the gameplay library but has platform-specific SDL and packaging changes; see [PS5 compatibility](PS5_COMPATIBILITY.md).

## Prototype scope

The map has five connected rooms, tile-based floors/platforms/hazards, a maintenance enemy, a power-station enemy, projectiles, damage/invulnerability, respawn, a room camera, a map overlay, a menu, options, pause, and an Echo Step ability gate. Engineer Mira offers a short interaction-driven dialogue in the Landing Bay and gives the Maintenance Key, which is tracked in the bounded player inventory and gates the Research Laboratory door. Volume and keyboard bindings persist in a separate versioned settings slot; gameplay progress and inventory use the autosave slot.

Sprites and sounds are original, procedurally generated assets (see [ASSETS.md](ASSETS.md)); the player has idle/run/jump/fall animation frames. Final art, music, and comprehensive input-device QA are follow-up work.

## PS5 milestone

Native Windows Hello World and PROJECT ECHO PS5 artifacts now pass static validation. Use `scripts\build-ps5.ps1`, not the upstream WSL/Bash wrappers. The unchanged shared gameplay builds for both targets; PS5-specific adapter branches avoid unsupported full libc++ logging/localization and force software rendering.

Run `tests\ps5_package_tests.ps1` after packaging changes. It verifies a valid package, isolated validation reports, and rejection of both a corrupt FSELF digest and a correctly wrapped ELF with invalid load-segment alignment. Uploads remain manual. The `playable` milestone controller build is reported playable on the operator's PS5; the newer controller-symbol UI and touchpad/circle actions need another hardware check. The keyboard-enabled build still crashes at `sceKeyboardInit`. Record each remaining console result separately in [acceptance status](STATUS.md). The Windows build host does not execute PS5 binaries.
