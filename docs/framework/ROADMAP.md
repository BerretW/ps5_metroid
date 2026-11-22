# Roadmap

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

- Extract a reusable application lifecycle coordinator when a second application needs it; today PROJECT ECHO's SDL adapter coordinates startup and shutdown while the existing SDL loop remains in control.
- Split state/actions/conditions into independently selectable compiled targets if a consumer requires finer granularity; currently these share the static core target.
- Design schema-specific migration tests for real content evolution.
- Extend the project editor beyond its current single game-specific component as real content requires it.
- Build independent optional genre modules only when a game requires them; do not add placeholders.
- Migrate one feature at a time, keeping `src/game/` authoritative until an adapter has gameplay regression coverage.

The A2 capability manifest, A3 manifest-backed editor, and A4 project-content validator are complete and tested on Windows. The reusable inventory/item-catalog module is integrated into Mira's key and Health Cell grants, conditional dialogue, consumable healing, checkpoint restore, HUD, and the reference game's selected module list. The bounded attributes module drives max health, movement speed, jump impulse, and Health Cell healing. The equipment module adds bounded slot loadouts and is integrated as Mira's persistent Kinetic Coil Body upgrade; equipped checkpoint slots are the source of truth for its modifiers. The abilities module validates compiled definitions and tracks Echo Step ownership, which the shared simulation uses for double jump and central-gate access. Branched quests evaluate authoritative facts, persist the route, and grant bounded rewards once inventory space is available. The cutscene module presents localized data-driven cards, pausing simulation until the player advances them; PROJECT ECHO uses it for the Central Chamber signal. Quest acceptance, independent journals, cutscene timelines, reusable combat/AI and world/map systems, and genre examples remain future work. Host-tool and headless results do not imply PS5 hardware verification.

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

PROJECT ECHO uses bounded dialogue, inventory, attributes, equipment, abilities, branched-quest and linear-cutscene modules, with the shared `echo::Game` still authoritative for gameplay progression and applying player attributes and ability effects. Quest acceptance, independent journals, timeline cutscenes, reusable combat/AI, physics, tile maps, camera, animation, audio, and UI modules remain future work; do not add empty placeholders.
