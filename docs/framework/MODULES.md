# Modules

Modules are ordinary statically linked C++ objects. The runtime registry selects among registered objects; it never loads a DLL, PRX, or other code from data files.

```cpp
constexpr std::array<std::string_view, 1> required{"echo.state"};

class MapModule final : public echo::framework::IModule {
public:
    const echo::framework::ModuleInfo& info() const noexcept override;
    bool initialize(echo::framework::ModuleContext&) noexcept override;
    void shutdown() noexcept override;
};
```

`ModuleInfo` includes a namespaced ID, display name, non-zero version, required/optional module IDs, provided service IDs, and `initializeAfter` ordering constraints. Register all compiled modules, setting `selected=false` for options not selected by the game config. Required dependencies must be both registered and selected; selected optional dependencies are ordered if present. Duplicate IDs, missing dependencies, cycles, and initialization failures return structured `Status` errors. Initialization is dependency-first; already initialized modules shut down in reverse order if a later module fails.

`ModuleContext::userData` is an explicit host-owned context pointer. Avoid global service lookup and do not retain temporary context data beyond the host lifetime.

Targets are `EchoFramework::Core` plus optional `EchoFramework::Entities`, `EchoFramework::Events`, and `EchoFramework::Data`. `ECHO_FRAMEWORK_ENABLE_DATA` controls the data target; tests and the example are selected with `ECHO_FRAMEWORK_BUILD_TESTS` and `ECHO_FRAMEWORK_BUILD_EXAMPLE`. The `EchoFramework::Core` target does not imply genre modules.

The bounded dialogue service is provided by the optional `EchoFramework::Narrative` target and module ID `echo.narrative.dialogue` (`ECHO_FRAMEWORK_ENABLE_NARRATIVE`, on by default). It requires `EchoFramework::Data` for stable content-ID validation and advertises `echo.service.dialogue`. Definitions must be registered before module initialization and remain immutable and alive while sessions use them. See [narrative](NARRATIVE.md) for graph and action behavior.

The optional `EchoFramework::Inventory` target provides module ID `echo.rpg.inventory`, `ItemCatalog`, compact `FixedInventory` and stable `SlotInventory` storage. It requires `EchoFramework::Data`, validates copied bounded item definitions before initialization, and advertises the inventory/item services. See [inventory](INVENTORY.md) for the item schema, stack behavior, capacities, and save compatibility.

The optional `EchoFramework::Attributes` target provides module ID `echo.rpg.attributes` and the fixed-capacity `AttributeSet` utility. It requires `EchoFramework::Data` for stable content-ID validation. See [attributes](ATTRIBUTES.md) for bounds, modifier behavior, and the PROJECT ECHO integration.

The optional `EchoFramework::Equipment` target provides module ID `echo.rpg.equipment`, a bounded JSON equipment catalog, and fixed-capacity slot loadouts. It requires `echo.rpg.attributes`; equipping applies its declared bounded modifiers, and unequipping removes them. See [equipment](EQUIPMENT.md) for the schema, limits, and persistence behavior.

The optional `EchoFramework::Abilities` target provides module ID `echo.rpg.abilities`, requires `EchoFramework::Data`, validates compiled stable ability IDs, and tracks unlocked abilities in fixed-capacity sets. Ability definitions must remain alive for the module lifetime. See [abilities](ABILITIES.md) for unlock behavior and the PROJECT ECHO integration.

The optional `EchoFramework::Quests` target provides `echo.rpg.quests`, bounded JSON quest definitions and deterministic evaluation against host-provided facts. See [quests](QUESTS.md) for limits, lifetimes and persistence.

The optional `EchoFramework::Cutscenes` target provides `echo.narrative.cutscenes`, bounded localized scene definitions, and an explicit card-by-card playback state machine. See [cutscenes](CUTSCENES.md) for playback lifetime and PROJECT ECHO behavior.

The optional `EchoFramework::World` target provides `echo.world`: bounded JSON objects (sprite sheet, frame size, spawn, facing, animations, properties) and rooms (multiple tilesets, symbol legend with rotation/flips, layers, exits, placements), both with `parent` inheritance. See [world](WORLD.md) for merge rules, limits and the PROJECT ECHO contract.

PROJECT ECHO's startup config selects `echo.state`, `echo.narrative.dialogue`, `echo.narrative.cutscenes`, `echo.rpg.abilities`, `echo.rpg.attributes`, `echo.rpg.equipment`, `echo.rpg.inventory`, `echo.rpg.quests`, `echo.world`, and `echo.project_echo`. Inventory, abilities, attributes, equipment, quests, cutscenes and world are optional dependencies of the project adapter so older minimal configurations remain valid; the checked-in game selects them and requires the inventory catalog, Echo Step ability, main quest, Central Chamber signal scene and `assets/world` data. The adapter rejects unknown IDs so a data file cannot select code absent from the executable.
