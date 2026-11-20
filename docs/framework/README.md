# Echo Framework 0.2

Echo Framework is a statically composed C++20 foundation. PROJECT ECHO remains the reference game; framework core code has no SDL, renderer, audio, or PS5 API dependency.

## Implemented

- Fixed-capacity module registration, required/optional dependencies, topological initialization, cycle/missing-dependency errors, and reverse shutdown.
- Generation-based entity handles and compile-time component pools.
- Typed event listeners, explicit subscriptions, bounded FIFO delivery, and queued nested events.
- Explicit action handlers with typed parameters, capability gates, and active-game checks.
- Namespaced global/world/entity state, typed values, change observers, declarative conditions, and bounded JSON condition decoding.
- A bounded yyjson-backed JSON wrapper, content registry/manifest validation, versioned state saves, migration callback, and storage callbacks.
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

Capacities and parser limits are documented in [architecture](ARCHITECTURE.md). Save serialization is implemented, but no production Windows or PS5 save-storage backend is supplied. In particular, a writable PS5 application-data path and replacement semantics have not been verified; see [save system](SAVE_SYSTEM.md).
