---
name: echo-data-content
description: Define bounded JSON content, stable resource IDs, and versioned Echo Framework save data safely.
---

# Echo data and content workflow

## Procedure

1. Parse through `JsonDocument`; cap inputs and validate required fields/types before using them.
2. Use stable `namespace:type/name` content IDs and explicit required/optional dependency references.
3. Never interpret JSON strings as source code, shell commands, or dynamic module paths.
4. Keep yyjson pinned in `dependencies.ps5.json` and CMake; preserve strict UTF-8, file API disablement, and reader depth limit.
5. Define component metadata as static `ComponentTypeDescriptor` and `PropertyDescriptor` values; validate configs through `ComponentTypeRegistry` before calling compiled factories.
6. Use `exportComponentManifest` for deterministic schema output. Native tools are opt-in, host-only utilities; never load component code or modules from JSON.
7. Use `SaveCodec` for bounded versioned snapshots and explicit migrations. Keep paths and replacement behavior in platform `SaveStorage` implementations.
8. Do not claim PS5 persistence until the writable app-data path and replacement behavior are tested on hardware.
9. Test malformed JSON/UTF-8, missing references, duplicate IDs, size limits, schema bounds/dependencies, save round trips, version mismatches, and failed storage operations.

## References

- `framework/data/include/echo/framework/data.hpp`
- `framework/data/include/echo/framework/schema.hpp`
- `framework/data/src/data.cpp`
- `framework/data/src/schema.cpp`
- `framework/tests/data_tests.cpp`
- `docs/framework/DATA_FORMATS.md`
- `docs/framework/COMPONENT_SCHEMAS.md`
- `docs/framework/SAVE_SYSTEM.md`
