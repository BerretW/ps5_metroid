---
name: sdl2-gameplay
description: Implement shared, fixed-step PROJECT ECHO gameplay with SDL2 input, software-friendly rendering and explicit SDL resource cleanup.
---

# SDL2 gameplay workflow

## Activate when

Changing the SDL application, rendering, controls, controller handling, or simulation loop.

## Procedure

1. Keep simulation changes in `src/game/`; SDL event translation belongs in `src/platform/sdl/`.
2. Preserve the 640x360 logical viewport and the 60 Hz fixed-step simulation.
3. Prefer SDL2's 2D renderer and ordinary draw calls; verify target-specific features before relying on them.
4. Keep keyboard and SDL game-controller actions mapped to the same `echo::Input`.
5. Log initialization failures and close controllers, audio devices, renderer, and window on shutdown.

## Project-specific lessons

- The shared `echo_game` simulation is the gameplay source of truth and is built by both Windows and PS5 targets. Keep OS/SDL calls out of `src/game/` so gameplay remains covered by headless tests.
- Preserve the 640x360 logical rendering path and the software-friendly SDL renderer; PS5 specifically selects software rendering and uses its patched native SDL backend.
- The PS5 release variant disables physical keyboard and IME because the keyboard-enabled build crashes at `sceKeyboardInit` on the reported setup. This does not disable Windows keyboard input. Keep PS5-only behavior behind its existing build options/patches; the exact crash cause remains unknown.
- PS5 controller playability is user-reported, but complete mappings and reconnect behavior are still unverified. Do not infer platform-wide input support from a successful build or one hardware session.
- The in-game hint bar currently shows keyboard labels on PS5. This is a known cosmetic mismatch; change it only as part of an explicit UX request, not as an incidental backend fix.

## Constraints

- Do not add OpenGL, Vulkan, or copyrighted game assets.
- Do not assume the PS5 port's renderer, audio, or controller driver works at runtime.
- Keep gameplay tests independent of a window and attached controller.

## References and validation

- `src/platform/sdl/`, `src/game/`
- `scripts/build-windows.ps1 -RunTests`
- `docs/CONTROLS.md`, `docs/PS5_COMPATIBILITY.md`
