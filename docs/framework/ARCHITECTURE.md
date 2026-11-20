# Architecture

`EchoFramework::Core` is a static C++20 library without SDL headers or platform calls. It contains module lifecycle, state, action, and condition implementations. `EchoFramework::Entities` and `EchoFramework::Events` are opt-in interface targets for header-only facilities. `EchoFramework::Data` is a separate static target and does not leak yyjson types through its public API.

PROJECT ECHO's SDL adapter loads `assets/framework/game.json`, selects compiled modules, validates resource IDs, and mirrors the game's Echo Step transition into framework state. `echo::Game` remains authoritative; framework state is a temporary projection, not a second gameplay save or progression source.

## Existing application boundary

The baseline simulation lives in `src/game/` and already has deterministic headless gameplay tests. SDL startup, input/controller handling, the fixed-step loop and shutdown remain in `src/platform/sdl/main.cpp`; rendering and PNG/WAV loading remain in their existing platform adapters. Native PS5 CRT/runtime and packaging remain under the existing platform build. None of those working systems was replaced.

`FrameworkIntegration` coordinates config validation, module initialization, new-game projection reset and reverse module shutdown. The host continues to own input, fixed update, rendering and audio. Save/restore is an explicit data API demonstrated headlessly, not an automatic gameplay lifecycle stage. A general platform-neutral application coordinator is still a roadmap item.

## Resource bounds

| Facility | Bound |
|---|---:|
| Registered modules | 32 |
| Entity/component capacity | Template parameter |
| Event listeners / queue | 32 / 64 by default |
| Event payload | 128 bytes by default |
| Game-state entries / observers / pending notifications | 128 / 16 / 64 |
| Actions / capabilities / request parameters | 32 / 16 / 16 |
| Condition nesting / logical children | 32 / 32 |
| Content resources / dependencies per resource | 64 / 8 |
| JSON document | 1 MiB |
| Game startup config | 16 KiB |
| Serialized save | 64 KiB |

Core registries use fixed storage; event payloads must be trivially copyable. yyjson allocates parser-owned memory through its default allocator, validates UTF-8, and is built statically with file APIs and unrelated utility APIs disabled and nesting capped at 64.

Capacities are compile-time constants or template parameters, not automatic growth policies. Increase a bound deliberately and test memory impact on both targets.
