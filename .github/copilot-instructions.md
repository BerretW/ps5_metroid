# PROJECT ECHO project rules

- Keep gameplay independent of SDL and platform APIs; put shared simulation in `src/game/`.
- Use the fixed `echo::kFixedStep` update for gameplay and keep rendering side-effect free.
- Preserve C++20 and the pinned PS5 SDL fork revision in `CMakeLists.txt`.
- Do not add proprietary Sony SDKs, WSL, containers, or Linux-only required developer steps.
- Preserve the verified native Windows PS5 pipeline and stable PPSA91007 application identity.
- PS5 uploads are manual; do not add automatic console connections or deployment.
- Never infer PS5 runtime support from a successful Windows build or from source code alone.
- Add deterministic, window-free gameplay tests to `tests/` for simulation changes.
- Keep original game assets and avoid copying protected game art, audio, maps, or characters.
- Keep console addresses and other machine-specific settings out of version control.
- Run `scripts/build-windows.ps1 -RunTests` after gameplay or build changes.
