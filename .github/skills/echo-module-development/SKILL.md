---
name: echo-module-development
description: Add statically linked Echo Framework modules with explicit dependencies, selectable registration, tests, and CMake integration.
---

# Echo module development

## Procedure

1. Give each module a stable namespaced ID, human-readable name, non-zero version, and static `ModuleInfo`.
2. List required/optional dependencies, provided services, and initialization ordering constraints; do not discover dependencies through globals.
3. Implement `IModule::initialize`/`shutdown` without exceptions or runtime loading. Keep optional modules out of the selected set unless the game configuration requests them.
4. Add a CMake target only for real functionality. Link optional targets explicitly; runtime config selects code already present in the executable.
5. Test successful ordering, missing dependencies, cycles, failure rollback, and reverse shutdown. Do not create empty genre modules.
6. Update `docs/framework/MODULES.md` and the relevant service guide.

## References

- `framework/core/include/echo/framework/core.hpp`
- `framework/tests/core_tests.cpp`
- `framework/examples/core_demo/`
- `docs/framework/MODULES.md`
