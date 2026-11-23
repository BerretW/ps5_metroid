# Echo Framework 0.2

Echo Framework is a statically composed C++20 foundation. PROJECT ECHO remains the reference game; framework core code has no SDL, renderer, audio, or PS5 API dependency.
The approved reusable-engine direction and phased delivery goals are recorded
in [master decisions](../MASTER_DECISIONS.md#md-001-reusable-data-driven-vulkan-engine).

## Implemented

- Fixed-capacity module registration, required/optional dependencies, topological initialization, cycle/missing-dependency errors, and reverse shutdown.
- Generation-based entity handles and compile-time component pools.
- Typed event listeners, explicit subscriptions, bounded FIFO delivery, and queued nested events.
- Explicit action handlers with typed parameters, capability gates, and active-game checks.
- Namespaced global/world/entity state, typed values, change observers, declarative conditions, and bounded JSON condition decoding.
- A bounded yyjson-backed JSON wrapper, content registry/manifest validation, versioned state saves, migration callback, storage callbacks, and an opt-in Windows Local AppData provider.
- A fixed-capacity component metadata registry with bounded typed instances, schema validation, deterministic JSON export, and native host-side content/schema tools.
- An optional bounded dialogue module with owned per-dialogue JSON loading, conditional choices, validated transitions, and registered action dispatch.
- An optional bounded inventory module with a versioned item catalog and deterministic compact or stable-slot stacks. PROJECT ECHO uses a 20-slot backpack, four separate equipment slots and one physical active-item stack.
- An optional bounded attributes module with versioned JSON definitions and fixed-capacity modifiers; PROJECT ECHO uses fifteen configured attributes for player health, movement, jumping, healing, projectile damage, movement physics, and combat tuning.
- An optional bounded equipment module with slot-checked loadouts and attribute modifiers; PROJECT ECHO integrates Mira's Kinetic Coil as a persistent suit upgrade.
- Headless examples and PROJECT ECHO integrations for ability unlock, Mira's dialogue, and her object-configured spawn.

Dialogue, inventory, attributes, and equipment are optional reusable modules. Combat, reusable abilities, quests, and world systems remain future work; do not add placeholders. Register only module implementations that the executable links. Runtime JSON may select registered modules, but cannot load code.

## Build and validate

From the repository root on Windows:

```powershell
.\scripts\build-windows.ps1 -Configuration Debug -RunTests
.\scripts\build-windows.ps1 -Configuration Release -RunTests
.\build\windows\framework\examples\core_demo\echo_framework_demo.exe
```

The PS5 controller application is cross-compiled by the existing native Windows pipeline. Its startup config is `assets/framework/game.json`; PROJECT ECHO links the dialogue, inventory, attributes, equipment, and abilities modules and loads Mira's dialogue, item catalog, attribute, equipment, and ability definitions at startup. The bounded player attributes drive health limits, movement speed, jump impulse, Health Cell healing, and projectile damage. Mira can give an injured player a catalog-backed Health Cell, or offer one persistent Kinetic Coil that auto-equips and increases movement speed. Echo Step ownership is tracked by the abilities module and controls double jump and central-gate access. This integration needs a separate PS5 hardware test.

## New game

Link `EchoFramework::Core` for module lifecycle, state, actions, and conditions. Link `EchoFramework::Entities` and `EchoFramework::Events` when using their header-only facilities. Link `EchoFramework::Data` for JSON/content/save support, `EchoFramework::Narrative` for dialogue, `EchoFramework::Inventory` for item catalogs and inventory stacks, `EchoFramework::Attributes` for bounded attribute values and modifiers, `EchoFramework::Equipment` for JSON-defined slot-checked gear, and `EchoFramework::Abilities` for versioned JSON ability definitions and fixed-capacity unlock sets. See [modules](MODULES.md), [narrative](NARRATIVE.md), [inventory](INVENTORY.md), [attributes](ATTRIBUTES.md), [equipment](EQUIPMENT.md), [abilities](ABILITIES.md), and [porting](PORTING.md).

Component descriptors and host tools are described in [component schemas](COMPONENT_SCHEMAS.md). The deterministic, bounded manifest contract is documented in [capability manifests](CAPABILITY_MANIFEST.md); the project exporter includes metadata from the executable's statically registered components and framework modules.

## Boundaries

Capacities and parser limits are documented in [architecture](ARCHITECTURE.md). Save serialization, Windows and PS5 storage providers, and a PS5 storage diagnostic are implemented. The operator reports that gameplay autosaving and settings persistence work on the integrated PS5 build. PROJECT ECHO persists a bounded checkpoint through one autosave slot, offers Continue, and stores volume/keybindings separately. Keyboard rebinding itself remains unavailable to test on PS5. See [save system](SAVE_SYSTEM.md) for what is included and remaining limits.
