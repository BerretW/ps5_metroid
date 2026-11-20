---
name: game-testing
description: Test PROJECT ECHO's deterministic movement, combat, room graph, health and progression without a graphics window or console.
---

# Gameplay testing workflow

## Activate when

Changing simulation rules, map topology, collision, combat, or progression.

## Procedure

1. Add focused deterministic cases to `tests/game_tests.cpp`.
2. Test game logic through `echo::Game` and `echo::Input`; do not require SDL initialization.
3. Cover both the locked and unlocked route when changing ability gates.
4. Run `scripts/build-windows.ps1 -RunTests`.
5. Track controller, graphical, audio, and console checks separately from unit-test evidence.

## Project-specific lessons

- `echo_game` is shared by the Windows application and PS5 build. The headless tests are the fast regression guard for shared mechanics; they do not prove SDL input, rendering, sound or hardware behavior.
- Existing coverage includes movement, jumping, projectile combat, the five-room ability-gated route, enemy/hazard damage, death and respawn. Extend the focused test cases in `tests/game_tests.cpp` when changing those rules.
- The PS5 controller package is user-reported playable, but complete mappings/reconnect, audio, progression, logging and exit/relaunch remain separate console checks. Do not replace hardware evidence with unit-test results.
- `echo_dualsense_diagnostics` covers simulated input math and report serialization only. Follow `docs/DUALSENSE_HARDWARE_TESTS.md` for physical DualSense checks; build and unit-test success never marks output hardware supported.

## Constraints

- Do not use timing-dependent sleeps in gameplay tests.
- Do not treat source inspection as runtime verification.
- Preserve reproducible fixed-step simulation behavior.

## References and validation

- `src/game/game.hpp`, `src/game/game.cpp`
- `tests/game_tests.cpp`, `scripts/build-windows.ps1`
- `docs/STATUS.md`
