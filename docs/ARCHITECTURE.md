# Architecture

- `src/game/`: deterministic 60 Hz movement, tile collision, combat, room transitions, health, hazards, and ability progression.
- `src/platform/sdl/`: SDL lifecycle, keyboard/controller mapping, optional synthesized shot audio, logical-resolution renderer, menu, HUD, map, and pause overlay.
- `tests/`: headless gameplay checks linked only to the shared simulation.
- `scripts/`: native Windows builds, PS5 ELF/FSELF packaging and validation, diagnostics, and manual-upload instructions.
- `cmake/`: native Windows host-tool build and PS5 cross-toolchain.
- `platform/ps5/`: isolated cross-build configuration and stable application identity.
- `patches/`: pinned SDL native-application fixes applied only to a build-local source copy.

Rooms are tile-span data in `src/game/game.cpp`. The renderer reads the same room and entity state as the simulation and applies a horizontal camera independently from physics. Window rendering uses SDL's logical 640x360 viewport and integer scaling.

The application targets SDL2's software-friendly drawing APIs. Windows still tries acceleration then falls back to software. PS5 selects software rendering and the source-built PS5 SDL backend. Its framebuffer patch separates driver-owned pixels from SDL's public window surface and returns the required format/pixel/pitch outputs.

The PS5 CRT is the native-application boilerplate `_start`, not either SDK's payload entry point. The target uses SDK C/C++ headers, static SDL, system import stubs and the reproduced clean-room runtime. Full libc++, payload libc and payload CRT archives are not linked. Normal PS5 logging uses stdio under `/data/project_echo/logs`; the opt-in startup diagnostic build instead adds pre-SDL kernel I/O and notifications. Windows logging is unchanged. The controller-only PS5 application is user-reported playable; the keyboard-enabled build still crashes at `sceKeyboardInit`. Evidence and remaining checks are recorded in [acceptance status](STATUS.md).
