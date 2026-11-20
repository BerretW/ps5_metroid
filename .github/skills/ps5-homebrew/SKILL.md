---
name: ps5-homebrew
description: Validate PS5 homebrew SDK, ABI, runtime and packaging claims for PROJECT ECHO without assuming Linux or WSL support.
---

# PS5 homebrew workflow

## Activate when

Working on the PS5 toolchain, application metadata, runtime libraries, or console compatibility.

## Procedure

1. Read `docs/PS5_COMPATIBILITY.md` and the exact pinned SDK/boilerplate sources it cites.
2. Verify each prerequisite, compiler target, linker, runtime module, and package format from upstream source.
3. Keep target-specific calls in a platform adapter; do not add them to `src/game/`.
4. Use only the native Windows PowerShell pipeline. Restore/build the standalone Hello first with `scripts/build-ps5.ps1 -Target Hello -RestoreDependencies`, then run `scripts/doctor.ps1 -Target PS5` and build the game with `scripts/build-ps5.ps1 -Target Game -RestoreDependencies`.
5. For the hardware-tested PROJECT ECHO controller package, use `scripts/build-ps5.ps1 -Target Game -SkipPs5Keyboard`. Run `tests/ps5_package_tests.ps1` after packaging changes.
6. Record static validation and actual console results separately in `docs/STATUS.md`; state the exact package variant, title ID and hardware setup when reporting a hardware result.

## Project-specific lessons

- SDKs and CRTs are not interchangeable. The application uses the native-app boilerplate `_start`, not the PS5Dev payload `payload_main` entry point. Use compatible public SDK headers/import stubs with the selected application runtime; do not add payload CRT/libc or silently switch to a payload ABI.
- The game build validates standalone Hello before building SDL/game. Keep Hello independent of SDL: it is the minimal target/toolchain check.
- SDL patches are applied to a build-local source copy. Keep patch files in `patches/`, do not edit `.deps` or upstream checkouts as the source of truth, and preserve the separated diagnostics, controller and original outputs.
- On the reported firmware 13.60 / Kraken Mount 0.3.1 setup, the keyboard-enabled app crashes at or immediately in `sceKeyboardInit`. The exact cause is unproven. The controller build with `-SkipPs5Keyboard` launches and is user-reported playable; it intentionally has no PS5 keyboard/IME support. Do not claim the crash is fixed or infer the cause from a successful controller build.
- A successful compile, ELF/FSELF check or package validation is static evidence only; it does not prove launch, input, audio, progression or filesystem behavior on hardware. Never execute PS5 binaries on Windows.

## Constraints

- Do not invoke WSL, containers, virtual machines, or undocumented shell commands.
- Do not claim firmware compatibility or hardware execution from a compile alone.
- Do not use proprietary SDK files or download unauthorized SDKs.

## References and validation

- `scripts/doctor.ps1`, `scripts/build-ps5.ps1`
- `docs/PS5_COMPATIBILITY.md`, `docs/STATUS.md`
- Validate a package only with documented, available static tools; record hardware tests separately.
