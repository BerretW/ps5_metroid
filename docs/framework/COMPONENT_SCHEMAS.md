# Component schemas

`echo/framework/schema.hpp` defines static metadata for component types and their JSON configuration. Register descriptors during program setup; descriptors and the spans they reference must outlive the registry. Factories are ordinary compiled function pointers. The registry does not load code, allocate instances dynamically, or retain JSON documents.

## Limits and validation

- At most 64 component types, 32 properties per component/object, and 16 component dependencies per type.
- Schema nesting is limited to 8 levels. Strings are capped at 4 KiB, arrays at 128 entries, and enum value lists at 64 entries.
- Property metadata supports booleans, integers, finite numbers, UTF-8 strings, enums, 2D vectors, normalized RGBA colors, resource IDs, entity handles, bounded arrays, and nested objects.
- Required fields, unknown fields, duplicate object keys, numeric/length constraints, resource types/existence, and required/optional component dependency cycles are checked before a factory is called.
- Factories write trivially copyable component values into a fixed 256-byte `ComponentInstance`. The descriptor version is stored with each instance; this does not itself migrate component data.
- Manifest export sorts component types by stable ID. Property and dependency ordering follows the static descriptor order.

Resource-reference existence is checked only when `validateConfig` or `instantiate` receives a `ContentRegistry`. Without a registry, IDs are syntax-checked but not resolved.

PROJECT ECHO uses the built-in `Transform2D` descriptor to instantiate Mira's `technician.transform` from [game.json](../../assets/framework/game.json). The current game accepts a visible, unscaled, unrotated transform whose position fits inside the Landing Bay; invalid content fails explicitly during startup or game configuration.

## Host tools

Configure a native host build with data and tools enabled, then build the tools:

```powershell
cmake -S . -B build/framework-tools -DECHO_FRAMEWORK_ENABLE_DATA=ON -DECHO_FRAMEWORK_BUILD_TOOLS=ON -DBUILD_TESTING=OFF
cmake --build build/framework-tools --config Release
```

`echo_schema_export <output.json>` exports the statically registered built-in framework schemas. It currently contains `echo:component/transform2d`; it does not discover or load game modules. To export project-specific descriptors, register them in a host-side executable and call `exportComponentManifest`.

`echo_content_validate <resource-manifest.json>` parses one bounded UTF-8 resource manifest and validates stable IDs, types, dependencies, and cycles through `ContentRegistry`. It does not validate game-specific component instances or load their resources.

`echo_component_editor <component-id> <config.json> [resource-manifest.json]` prints fields and editor hints by walking the registered descriptor, then validates and instantiates the supplied JSON through the same registry. Its proof-of-concept registry currently contains only the built-in `Transform2D`; project-specific editors must register their compiled descriptors rather than add hardcoded property lists.

Tools are native host utilities and intentionally fail configuration under a cross-compiling toolchain. The JSON manifest API itself is part of `EchoFramework::Data` and remains available to native and PS5 application code.
