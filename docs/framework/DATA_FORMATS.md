# JSON and content

The wrapper in `echo/framework/data.hpp` keeps yyjson types private. `JsonDocument::parse` accepts UTF-8 memory, reports a byte offset for malformed input, and rejects empty or over-1-MiB input. yyjson 0.13.0 is pinned to commit `6447536015f3d600f3d65323b10976103b337ca7` in `dependencies.ps5.json`; it is MIT-licensed (`licenses/yyjson.txt`), CMake-buildable as a static C library, and configured with strict UTF-8 validation, file APIs disabled, and a depth limit of 64. Its default allocator is libc-backed; parser documents own their allocations and must not outlive their `JsonDocument`.

The content manifest has a required `resources` array. Each entry requires a namespaced stable ID and a known type. `dependsOn` lists required IDs; missing references and dependency cycles fail validation. `optionalDependencies` lists IDs whose absence is allowed; present dependencies still participate in cycle checks. Optional resources may be omitted by a consumer, but the manifest registry does not load graphics/audio itself.

Resources may declare a `source` as a relative path under the asset root, for
example `"source": "framework/engineer.json"`. Paths use `/` separators and
reject absolute paths, empty segments, `.`/`..`, backslashes, and unsupported
characters. The runtime adapter resolves paths; JSON never selects code or
modules. Resource source paths are bounded to 127 bytes.

```json
{
  "resources": [
    { "id": "echo:ability/echo_step", "type": "ability" },
    { "id": "echo:dialogue/robot_intro", "type": "dialogue",
      "dependsOn": ["echo:ability/echo_step"] }
  ]
}
```

Supported resource types are `generic`, `item`, `ability`, `dialogue`, `cutscene`, `sprite`, and `sound`. IDs use `namespace:type/name`. The runtime startup configuration and headless example are `assets/framework/game.json` and `framework/examples/core_demo/game.json`. Their module lists select already compiled module IDs only.

Selected modules may also consume bounded, versioned definition blocks from
`game.json`. The Abilities module loads `abilities` (schema version 1, up to 32
stable IDs), the Attributes module loads `attributes` (schema version 1, up to
8 stable-ID definitions), and the Equipment module loads `equipment` (schema
version 1). PROJECT ECHO also loads a bounded `loot` table (schema version 1)
against its item catalog. These are data interpreted by compiled systems; they
cannot load native code. See [abilities](ABILITIES.md), [attributes](ATTRIBUTES.md),
[equipment](EQUIPMENT.md), and [inventory](INVENTORY.md) for schemas and bounds.

`JsonConditionTree` decodes condition objects with bounded node/depth limits and owned keys; see the schema and example in [conditions](CONDITIONS.md). The tree must remain alive while its root condition is evaluated.

Component configuration schemas are static C++ descriptors registered with `ComponentTypeRegistry`; they are not loaded as executable code. The registry validates property types, defaults, bounds, nested objects and arrays, resource references, and component dependencies. See [component schemas](COMPONENT_SCHEMAS.md) for supported property metadata and export tooling.
