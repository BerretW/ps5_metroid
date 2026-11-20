---
name: dualsense-integration
description: Integrate SDL2 controller input and safe DualSense diagnostics in PROJECT ECHO across Windows and native PS5.
---

# DualSense integration workflow

## Activate when

Changing SDL game-controller input, DualSense capabilities, haptics, LEDs, sensors, touchpad behavior, controller diagnostics or hardware test reporting.

## Procedure

1. Inspect the pinned SDL version and the actual platform driver before using an API.
2. Keep shared simulation rules in `src/game/`; SDL controller polling and presentation belong in `src/platform/sdl/`.
3. Query per-device capabilities. SDL API declarations do not establish that the Windows or PS5 backend implements them.
4. Keep API availability separate from observed input and operator-confirmed output. Never infer physical success from a zero return code or a successful build.
5. Make output explicit, low intensity, time-limited and stoppable on screen exit, disconnect and shutdown.
6. Never silently substitute trigger rumble for adaptive trigger resistance.
7. Do not fabricate touch contacts, sensor readings, battery percentages, speaker routing or microphone input.
8. Keep reports bounded and structured; do not capture/store audio or generate continuous logs.
9. Run headless Windows tests, both Windows configurations when relevant, and the native Windows PS5 build/package validation. Record actual console testing separately.

## PROJECT ECHO findings

- SDL2 is pinned to `ee4c47dc0d617b3bc8f35108f9956baf228a1322`.
- The PS5 SDL driver maps game buttons, sticks, triggers, ordinary rumble and lightbar output. It does not expose touch contacts or sensors; trigger rumble returns `SDL_Unsupported()`.
- The PS5 touchpad click is a button and is not touch-coordinate support. The local PS5 pad-state structure containing touch/sensor fields is not evidence that the driver forwards them.
- PS5 keyboard and IME remain disabled in controller builds due to the reported startup crash; do not re-enable `sceKeyboardInit`.
- The DualSense Lab's output tests are explicit and capped at 250 ms. Successful API calls remain hardware-unverified until the operator confirms the effect.
- Exported diagnostics use `/data/project_echo/logs/dualsense_report.json` on PS5 and `logs/dualsense_report.json` on Windows.

## References and validation

- `src/input/dualsense_diagnostics.*`
- `src/platform/sdl/main.cpp`, `src/platform/sdl/renderer.cpp`
- `tests/dualsense_diagnostics_tests.cpp`, `docs/DUALSENSE_COMPATIBILITY.md`
- `scripts/build-windows.ps1 -Configuration Debug -RunTests`
- `scripts/build-ps5.ps1 -Target Game -SkipPs5Keyboard`
