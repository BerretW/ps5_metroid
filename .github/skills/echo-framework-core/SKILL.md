---
name: echo-framework-core
description: Extend Echo Framework core services while preserving fixed-capacity, SDL-free, deterministic C++20 behavior.
---

# Echo Framework core workflow

## Activate when

Changing modules, entity handles/components, events, actions, state, or conditions.

## Procedure

1. Keep public core APIs under `framework/core/include/echo/framework/` and platform/game adapters outside the core.
2. Prefer fixed-capacity storage and explicit `Status` errors; do not add RTTI, exceptions, dynamic plugins, background threads, or hidden allocations.
3. Keep module IDs and state keys namespaced. Register dependencies and capability requirements explicitly.
4. Preserve deterministic dependency, event, and state-observer order. Nested event/state changes must remain queued and bounded.
5. Add focused headless tests in `framework/tests/core_tests.cpp` and run the Windows `-RunTests` workflow.
6. Document limits and lifetime rules in `docs/framework/`.

## References

- `framework/core/include/echo/framework/core.hpp`
- `framework/core/src/core.cpp`
- `framework/tests/core_tests.cpp`
- `docs/framework/ARCHITECTURE.md`
