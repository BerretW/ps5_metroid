# Master decisions

## MD-001: Reusable data-driven Vulkan engine

- **Decided:** 2026-11-23
- **Decision owner:** project owner
- **Status:** accepted; implementation is incremental

PROJECT ECHO is the reference game, not the limit of the engine. The long-term
goal is a reusable engine in which another game can be authored from validated
content and reusable runtime systems instead of adding game-specific branches
to `src/game/` or the framework.

The engine will use Vulkan as its graphics backend and is intended to support
2D, isometric 2D, and 3D games, including animation and physically based
rendering (PBR). These are target capabilities, not claims about the current
implementation.

This direction applies to **all authored game-specific content**, including
existing PROJECT ECHO content, not only new features. World composition,
objects, player/enemy tuning, items and loot, equipment, abilities, quests,
interactions, dialogue, and presentation data should use explicit, versioned
schemas and stable content IDs. Generic runtime systems may interpret that
data; project-specific adapters must remain separate from reusable engine
code. Data must never load arbitrary native code or select a module that was
not compiled into the application.

Keep reusable algorithms in C++ when they are engine behavior; move project
values, identifiers, tables, and per-game rules out of those algorithms. Use
scripts only for behavior that genuinely needs author-written logic. A game
must not need a new C++ branch just because its content defines another item,
enemy, room interaction, or quest.

Lua is a candidate for behavior that is too complex or expressive for authored
data. It is not yet a binding dependency or an approved runtime contract. Before
adopting it, a bounded prototype must demonstrate deterministic fixed-step
behavior, a restricted host API, explicit resource limits, error reporting,
content validation, and a safe save/migration story on the supported builds.

The target is a reusable engine for multiple kinds of games, not a guarantee
that every conceivable game can be made without custom work. Preserve C++20,
deterministic simulation, the native Windows PS5 build pipeline, the stable
PPSA91007 identity, and manual-only PS5 deployment. A successful build or
static validation is not evidence of PS5 runtime support.

### Delivery sequence

1. **Audit and classify all authored definitions.** Inventory game-specific
   constants and tables in `src/game/`, reusable framework libraries, platform
   adapters, and validators. Separate authorable data from algorithms and
   save-format invariants; do not stop at Cargo or at content added after this
   decision.
2. **Build shared content foundations.** Use bounded, versioned schemas and
   manifest-declared asset sources for all content types. Remove duplicate
   project tables from both the game and reusable framework modules. Migrate
   every current reference-game definition while preserving the game and
   keeping schemas, validators, editors, and tests aligned.
3. **Evaluate scripting.** Prototype Lua behind a narrow, deterministic,
   resource-bounded engine API. Decide whether to adopt it only after
   headless tests and Windows/PS5 build integration meet the constraints above.
4. **Complete the Vulkan 2D path.** Separate simulation, world/presentation
   data, and renderer ownership; support side-view and isometric projection
   through shared engine contracts.
5. **Add 3D capabilities.** Establish asset import/runtime contracts, skeletal
   animation, materials, and PBR in independently testable milestones.
6. **Prove reuse.** Build at least one small non-PROJECT ECHO sample from
   reusable engine facilities and data, then document what still requires
   project-specific code.

Each milestone must include focused headless tests, updated framework
documentation, and evidence-based platform status. Do not trade stable existing
gameplay for architectural work: keep PROJECT ECHO playable and migrate one
feature family at a time, but apply each data-foundation change across the
whole game rather than building a Cargo-only path.

### Starting audit

The current codebase already loads rooms, object placements, dialogues,
quests, power circuits, item stack catalogs, cutscenes, and much of the visual
content from JSON. It still embeds authored data in C++, including:

- enemy AI tuning and fallback values for optional gameplay modules in
  `src/game/game.cpp`;
- stable item and enemy save-slot mappings in `src/game/game.cpp`;
- project-specific quest ID/order/reward validation in
  `src/game/project_quests.hpp`;
- fixed room, enemy, and save-slot assumptions across `src/game/`.

These are migration work, not target architecture. Since this decision was
accepted, equipment definitions have moved from framework C++ into versioned
JSON, and the shared dialogue binding and source-asset loading now use the
resource manifest. The initial interaction work is one reusable mechanism,
not the scope of the decision.

The attribute defaults and bounds have moved from `src/game/game.cpp` into
versioned `game.json` content, loaded by the generic bounded Attributes module.
The Echo Step ability definition has also moved from `src/game/project_abilities.hpp`
into versioned `game.json` content loaded by the bounded Abilities module.
Consumable item effects, inventory descriptions, and equipment/slot bindings
now come from the bounded version-3 item catalog; compiled handlers implement
the effects and the Equipment module allowlists the supported modifiers. Enemy
loot items, quantities, and save-slot-to-enemy-slot associations now come from
the bounded version-1 loot table in `game.json`; runtime spawning remains a
generic simulation algorithm. Side-view movement physics tuning now comes from
the version-1 Attributes block; the shared simulation retains algorithms and
fallback values for configurations that do not select that optional module.
Player weapon speed, firing interval, and invulnerability duration now also
come from the bounded Attributes block. Charger detection range and charge
speed, stalker detection range, and sentry range, projectile speed and fire
interval, and the armored enemy's guard cycle and damage reduction now come
from inherited world-object properties with optional placement overrides.
Reusable behavior composition, stable save-slot definitions, stable item
save-key mapping, project quest constraints, and fixed world/save assumptions
remain to be audited and migrated.
