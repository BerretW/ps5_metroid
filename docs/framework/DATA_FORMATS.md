# JSON and content

The wrapper in `echo/framework/data.hpp` keeps yyjson types private. `JsonDocument::parse` accepts UTF-8 memory, reports a byte offset for malformed input, and rejects empty or over-1-MiB input. yyjson 0.13.0 is pinned to commit `6447536015f3d600f3d65323b10976103b337ca7` in `dependencies.ps5.json`; it is MIT-licensed (`licenses/yyjson.txt`), CMake-buildable as a static C library, and configured with strict UTF-8 validation, file APIs disabled, and a depth limit of 64. Its default allocator is libc-backed; parser documents own their allocations and must not outlive their `JsonDocument`.

The content manifest has a required `resources` array. Each entry requires a namespaced stable ID and a known type. `dependsOn` lists required IDs; missing references and dependency cycles fail validation. `optionalDependencies` lists IDs whose absence is allowed; present dependencies still participate in cycle checks. Optional resources may be omitted by a consumer, but the manifest registry does not load graphics/audio itself.

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

`JsonConditionTree` decodes condition objects with bounded node/depth limits and owned keys; see the schema and example in [conditions](CONDITIONS.md). The tree must remain alive while its root condition is evaluated.
