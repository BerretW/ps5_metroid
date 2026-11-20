# Acceptance status

Statuses are evidence-based: **PASS**, **FAIL**, or **NOT TESTED**. Update after running validation; a source implementation alone does not count as a runtime result.

**Milestone: PLAYABLE prototype.** The native Windows application builds and launches, the shared gameplay simulation passes its headless tests, and the PS5 controller build is reported playable by the operator. This is a development baseline, not a claim that every feature or platform behavior is complete.

| # | Acceptance criterion | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Native Windows compile and launch | PASS | Debug and Release CMake/Ninja builds succeeded; the Release SDL app opened a `PROJECT ECHO` window, initialized video/controller subsystems, and opened an audio device. |
| 2 | Move, jump, and shoot | PASS (simulation) | Headless tests pass for horizontal movement, jumping, and projectile combat. Interactive keyboard play was not verified. |
| 3 | Five traversable rooms | PASS (simulation) | Headless progression test reaches the Central Chamber through the five-room route. |
| 4 | Ability unlock changes access | PASS (simulation) | Test confirms the locked gate rejects entry, the Lab requires backtracking, Echo Step is acquired, and the gate then opens. |
| 5 | Enemies damage player and can be defeated | PASS (simulation) | Tests cover enemy damage, projectile defeat, hazard damage, death, and respawn. |
| 6 | Main menu and pause | NOT TESTED | Main menu was visually confirmed; pause/options interaction was not exercised because foreground input was detected. |
| 7 | Controller input on supported targets | NOT TESTED (complete acceptance) | User reports the PS5 controller build is playable, but the input device, complete mappings and reconnect behavior have not been separately verified. |
| 8 | Structurally valid PS5 application package | PASS (static) | Native Windows Hello World and PROJECT ECHO compile, link, convert and package. ELF64/SCE entry, segment bounds/alignment, FSELF digest, exact runtime hashes and title-directory structure validate. |
| 9 | PROJECT ECHO launch on actual PS5 hardware | PASS (controller build, user-reported) | Firmware 13.60, Kraken Mount 0.3.1: user confirms the non-diagnostic controller build launches and is playable. Supplied screenshot shows Landing Bay gameplay, player, platforms and HUD. The original keyboard-enabled build still crashes with CE-108255-1. |
| 10 | No WSL, Docker, Linux VM, or proprietary Sony SDK required | PASS (both builds) | Native Windows LLVM 18.1.8/LLD, MSVC host utilities, CMake/Ninja, PowerShell and public SDK target files build both PS5 artifacts. |

Native cross-build verification: 2026-11-20. The isolated Release Windows regression executable rebuilt successfully and its complete headless gameplay test target passed, including after startup-diagnostic changes. The original `build/windows/project_echo.exe` was not overwritten. PS5 upload and launch remain manual. Hello and controller-build hardware success are operator reports, not execution performed by the build host.

- PROJECT ECHO package: `dist/ps5/PPSA91007`, `eboot.bin` SHA256 `a62fcdd17e127686e8aaed19d6f40bf3da1c0c44ebba475a6e8c6cd228c746ac`.
- Hello World package: `dist/ps5-hello/PPSA99999`, `eboot.bin` SHA256 `205372ec8df455f3397709001ee50285750f41ebd8dbed7d50942908bd6885d6`.
- Clean-room `libc.prx`: SHA256 `e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036`, 1,284,674 bytes; independently reproduced twice.
- Evidence: `build/ps5/logs/*-validation.json`, compiler/linker logs, `game/compile_commands.json` and retained Ninja response files.

The startup diagnostic reached the call to `sceKeyboardInit` on hardware. A separate experimental build (`-Target Game -StartupDiagnostics -SkipPs5Keyboard`, output `dist/ps5-diagnostic-no-keyboard/PPSA91007`) omits PS5 hardware keyboard and IME calls, retaining the controller path. The operator reports this build launches and is playable, but it is not a permanent fix or full acceptance test. Initialization errors are reported explicitly and the diagnostic process stays open for collection. See [PS5 compatibility](PS5_COMPATIBILITY.md#hardware-crash-investigation-kraken-mount-031).

At the operator's request, `-Target Game -SkipPs5Keyboard` produces the non-diagnostic, statically validated controller build in `dist/ps5-controller/PPSA91007`, without physical PS5 keyboard or IME support. Its `eboot.bin` SHA256 is `eff47dcc2a39b3e7b92e226d9851fafdb6078055763c6d50221e7af8072d70bd`. The operator confirmed on 2026-11-20 that this build is playable and supplied a gameplay screenshot. The original PS5 package and user-tested diagnostic are preserved separately. Full controller mappings/reconnect, audio playback, five-room progression and exit/relaunch remain unverified; the exact underlying keyboard-path fault remains undiagnosed.
