# Echo Framework 0.2

Echo Framework is a statically composed C++20 foundation. PROJECT ECHO remains the reference game; framework core code has no SDL, renderer, audio, or PS5 API dependency.

## Implemented

- Fixed-capacity module registration, required/optional dependencies, topological initialization, cycle/missing-dependency errors, and reverse shutdown.
- Generation-based entity handles and compile-time component pools.
- Typed event listeners, explicit subscriptions, bounded FIFO delivery, and queued nested events.
- Explicit action handlers with typed parameters, capability gates, and active-game checks.
- Namespaced global/world/entity state, typed values, change observers, declarative conditions, and bounded JSON condition decoding.
- A bounded yyjson-backed JSON wrapper, content registry/manifest validation, versioned state saves, migration callback, storage callbacks, and an opt-in Windows Local AppData provider.
- A headless example and a narrow PROJECT ECHO ability-unlock integration.

Feature modules for dialogue, inventory, combat, tile maps, and other genres are intentionally not included. Register only module implementations that the executable links. Runtime JSON may select registered modules, but cannot load code.

## Build and validate

From the repository root on Windows:

```powershell
.\scripts\build-windows.ps1 -Configuration Debug -RunTests
.\scripts\build-windows.ps1 -Configuration Release -RunTests
.\build\windows\framework\examples\core_demo\echo_framework_demo.exe
```

The PS5 controller application is cross-compiled by the existing native Windows pipeline. Its startup config is `assets/framework/game.json`; package success remains static evidence and is not hardware evidence.

## New game

Link `EchoFramework::Core` for modules, state, actions, and conditions. Link `EchoFramework::Entities` and `EchoFramework::Events` when using their header-only facilities. Link `EchoFramework::Data` only when JSON/content/save support is needed. See [modules](MODULES.md) and [porting](PORTING.md).

## Boundaries

Capacities and parser limits are documented in [architecture](ARCHITECTURE.md). Save serialization, Windows and PS5 storage providers, and a PS5 storage diagnostic are implemented. The operator reports that gameplay autosaving and settings persistence work on the integrated PS5 build. PROJECT ECHO persists a bounded checkpoint through one autosave slot, offers Continue, and stores volume/keybindings separately. Keyboard rebinding itself remains unavailable to test on PS5. See [save system](SAVE_SYSTEM.md) for what is included and remaining limits.
