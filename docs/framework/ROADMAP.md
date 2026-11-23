# Roadmap

The project owner's accepted long-term direction is recorded in
[master decisions](../MASTER_DECISIONS.md#md-001-reusable-data-driven-vulkan-engine).
The framework is not yet a fully data-driven, general-purpose engine.
The decision applies to all existing and future game-specific content, not
only the Cargo example. Use its delivery sequence to prioritize a whole-codebase
content audit and migration; do not imply that planned Lua, native visual editing, 3D, animation, or
PBR capabilities are already implemented.

The owner selected Lua for a bounded prototype and a native Windows visual
editor. The first end-to-end authoring milestones must cover both enemy AI and
game UI; neither a production script runtime nor a general scene editor exists yet. The future
editor must validate and serialize the same versioned content consumed by the
game. Lua execution, resource limits, save migration and PS5 compatibility
must be verified separately before production integration.

An isolated native Windows Lua experiment exists in
`experiments/lua-prototype`. It is not linked into PROJECT ECHO and does not
provide an editor or a production scripting runtime. Its test contract accepts
integer distance/health inputs and produces bounded movement, firing and UI
visibility outputs; these are synthetic examples rather than game integration.
Each invocation uses a fresh VM, text-only input, no opened Lua libraries,
a 4 KiB source cap, a 256 KiB VM allocation cap and an instruction hook.
Errors do not publish output. Repeated host tests establish only local
repeatability, not cross-platform deterministic execution or a security
boundary for hostile third-party content.

An initial Windows native map workspace edits tile layers in PROJECT ECHO's real
rooms. A secondary property inspector edits player attributes, quest text and
stalker tuning. The map view is a symbolic grid, not the game's renderer;
object placement, scene management, UI layout and script behavior remain
unfinished. It is not connected to the Lua experiment. See
[the editor workflow](EDITOR.md).

To build and test the host experiment using the installed Visual Studio C++
tools:

```powershell
cmake -S .\experiments\lua-prototype -B .\build\lua-prototype
cmake --build .\build\lua-prototype --config Debug
ctest --test-dir .\build\lua-prototype -C Debug --output-on-failure
```

Lua source is fetched from the URL and SHA-256 pinned in
`dependencies.ps5.json`; this prototype does not add Lua to PS5 packages.
Before game integration, establish script lifecycle and entity/UI bindings,
atomic gameplay command application, stronger isolation and resource checks,
versioned script state and save migrations, editor workflows, deterministic
fixed-step tests, target ABI/linking and separate PS5 hardware evidence.

### AI and UI authoring milestones

1. Define versioned scene, entity, UI and script-binding schemas with stable
   IDs. The editor and runtime must share validators and compiled capability
   descriptors, rather than maintaining separate lists of supported behavior.
2. Replace the experiment's synthetic input/output with explicit per-entity
   lifecycle hooks, bounded snapshots and validated command buffers. Commit
   commands only after successful script execution; never expose raw engine
   pointers, host file access or dynamic native loading to scripts.
3. Implement a native Windows editor with scene hierarchy, inspector, 2D
   viewport and UI layout preview, script editing and actionable validation
   errors. Save the same content the runtime loads. Test round trips and
   failure handling before treating the editor as an authoring tool.
4. Integrate both a scripted enemy and a scripted UI interaction in PROJECT
   ECHO, with fixed-step headless tests and Windows visual/input checks.
   Replace the corresponding compiled game-specific behavior rather than
   keeping competing sources of truth.
5. Define persisted script state and explicit content/save migrations. Test
   budgets, failures, event ordering and reproducibility on both targets.
   Cross-build and statically validate PS5 before manual hardware testing;
   never treat either as evidence for the other.
6. Author a second small game without modifying PROJECT ECHO's game code, then
   extend the same editor/runtime contracts to 3D, animation and PBR.

## 0.2 implemented foundation

Module lifecycle, bounded entities/components, typed events, explicit actions, generic state/conditions, JSON wrapper, content validation, versioned state serialization, storage contract, headless demonstration, a small Echo Step integration, and the fixed-capacity component metadata/configuration registry are implemented and covered by focused tests. Deterministic schema export, a resource-manifest validator, and a registry-driven host CLI editor proof are available behind `ECHO_FRAMEWORK_BUILD_TOOLS`.

The first continuation milestone, a unified capability manifest, is implemented: framework and optional module fragments are merged with the live component registry, sorted deterministically, validated against declared action capabilities, and exported under explicit format/API versions.

The generic host editor has a manifest discovery/selection mode with manifest-backed property display. It checks compatibility and component versions against statically compiled registries; JSON cannot load modules or factories. The PROJECT ECHO host validator checks its actual game configuration and dialogue against the compiled module, action, resource, component, and text contracts. These host tools are opt-in and do not replace runtime validation.

## 0.3 gameplay save integration

- The operator's 2026-11-21 PS5 diagnostic run passed directory checks, write, replacement, read-back and slot enumeration using the sandbox-compatible provider path.
- PROJECT ECHO now persists a bounded gameplay checkpoint through the platform save provider and offers Continue for its single autosave slot. Windows tests and PS5 static package validation pass; the operator reported gameplay saving works on PS5 on 2026-11-21.
- Volume and keyboard bindings now persist in a separate versioned `settings` slot. Windows round-trip and validation tests pass; the operator confirms settings persistence works on PS5. Keyboard rebinding itself remains unavailable to test there because PS5 keyboard input is disabled.
- Current checkpoint fields are room, player position/velocity/health/facing, Echo Step, completion/death progress, enemy health and bounded inventory. Version-one gameplay saves migrate by granting the newly introduced Maintenance Key, preserving their previous route access. Additional object/component state is intentionally not implicit.
- Mira's bounded JSON dialogue supports English and Czech with a persisted language selection and a bitmap font for Czech glyphs. Windows tests and PS5 static validation pass; this revision still requires console testing.
- The project registers a dialogue-interaction component with bounded, configurable horizontal reach. It is used by the game and exposed to opt-in project-specific schema editor and export tools; existing configurations retain the old reach. Windows tests and PS5 static validation pass, while this revision needs a console run.

## Remaining before broader game migration

- Audit and move all remaining authored PROJECT ECHO tables from C++ into
  validated content, including player tuning, item-to-enum/equipment mappings,
  loot definitions, and project-specific quest constraints. Attributes,
  equipment, abilities, and consumable effect descriptors now load from bounded
  versioned JSON, but the wider migration remains incomplete.
- Remove game-specific world-interaction dispatch from `src/game/`; the Cargo
  terminal demonstrates the generic binding mechanism but is not the scope or
  endpoint of the data-driven work.
- Extract a reusable application lifecycle coordinator when a second application needs it; today PROJECT ECHO's SDL adapter coordinates startup and shutdown while the existing SDL loop remains in control.
- Split state/actions/conditions into independently selectable compiled targets if a consumer requires finer granularity; currently these share the static core target.
- Design schema-specific migration tests for real content evolution.
- Extend the project editor beyond its current single game-specific component as real content requires it.
- Build independent optional genre modules only when a game requires them; do not add placeholders.
- Migrate one feature at a time, keeping `src/game/` authoritative until an adapter has gameplay regression coverage.

The A2 capability manifest, A3 manifest-backed editor, and A4 project-content validator are complete and tested on Windows. The reusable inventory/item-catalog module is integrated into Mira's key and Health Cell grants, conditional dialogue, consumable healing, checkpoint restore, HUD, and the reference game's selected module list. Versioned item JSON now supplies consumable effect IDs, bounded effect parameters, and inventory descriptions; compiled handlers remain the effect algorithms. The bounded attributes module loads five stable-ID defaults and bounds from versioned JSON and drives max health, movement speed, jump impulse, Health Cell healing, and projectile damage. The equipment module loads bounded definitions from versioned JSON, provides slot loadouts, and is integrated as Mira's persistent Kinetic Coil Body upgrade; equipped checkpoint slots are the source of truth for its modifiers. The abilities module loads its versioned JSON definitions and tracks Echo Step ownership, which the shared simulation uses for double jump and central-gate access. Branched quests evaluate authoritative facts, persist the route, and grant bounded rewards once inventory space is available. The cutscene module presents localized data-driven cards, pausing simulation until the player advances them; PROJECT ECHO uses it for the Central Chamber signal. The world module loads data-driven objects, sprite animations and multi-tileset rooms with `parent` inheritance; PROJECT ECHO builds all five rooms, enemies, props and visuals from `assets/world`. Quest acceptance, independent journals, cutscene timelines, reusable combat/AI, room-count/topology changes beyond the fixed route, and genre examples remain future work. Host-tool and headless results do not imply PS5 hardware verification.

The slot-inventory refinement adds 20 stable backpack slots, separate
Head/Body/Weapon/Accessory slots and one physical active consumable stack.
Keyboard/controller inventory navigation supports atomic moves, compatible swaps
and bounded full-stack merges; the Coil can be unequipped and cells require the
active slot. Autosave format 3 preserves every compartment and explicitly migrates
formats 1/2. Both enemies now drop visible Scrap piles (two and three units);
touch pickup is capacity-safe, stack-limited and checkpoint-persistent.
Autosave format 4 preserves every compartment and loot state, explicitly
migrating formats 1–3. Windows tests and native PS5 static package validation
pass; this gameplay revision still needs a manual console run.
Autosave format 5 subsequently added the three new enemy states, their drops and
temporary item effects, with migration from formats 1–4. Autosave format 6
persists the selected main-quest route and its idempotent reward claim, migrating
formats 1–5 explicitly. The bounded branch module drives a coil-recovery or
sentry-defeat route in PROJECT ECHO; a full backpack delays the reward until
space is available.

The operator confirmed on 2026-11-20 that the controller-only framework build launches and runs on PS5, reported the PS5 save-provider diagnostic passing on 2026-11-21, and later confirmed gameplay autosaving and settings persistence work on the integrated build. These are operator-reported results; keyboard rebinding itself has not been tested on PS5.

On 2026-11-21, the operator also confirmed an earlier controller package works on PS5. The subsequent schema and dialogue integration requires a separate console test.

PROJECT ECHO uses bounded dialogue, inventory, attributes, equipment, abilities, branched-quest, linear-cutscene and world (objects, animations, tile rooms) modules, with the shared `echo::Game` still authoritative for gameplay progression and applying player attributes and ability effects. Quest acceptance, independent journals, timeline cutscenes, reusable combat/AI, physics, camera, audio, and UI modules remain future work; do not add empty placeholders.
