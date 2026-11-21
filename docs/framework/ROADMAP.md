# Roadmap

## 0.2 implemented foundation

Module lifecycle, bounded entities/components, typed events, explicit actions, generic state/conditions, JSON wrapper, content validation, versioned state serialization, storage contract, headless demonstration, a small Echo Step integration, and the fixed-capacity component metadata/configuration registry are implemented and covered by focused tests. Deterministic schema export, a resource-manifest validator, and a registry-driven host CLI editor proof are available behind `ECHO_FRAMEWORK_BUILD_TOOLS`.

## 0.3 gameplay save integration

- The operator's 2026-11-21 PS5 diagnostic run passed directory checks, write, replacement, read-back and slot enumeration using the sandbox-compatible provider path.
- PROJECT ECHO now persists a bounded gameplay checkpoint through the platform save provider and offers Continue for its single autosave slot. Windows tests and PS5 static package validation pass; the operator reported gameplay saving works on PS5 on 2026-11-21.
- Volume and keyboard bindings now persist in a separate versioned `settings` slot. Windows round-trip and validation tests pass; the operator confirms settings persistence works on PS5. Keyboard rebinding itself remains unavailable to test there because PS5 keyboard input is disabled.
- Current checkpoint fields are room, player position/velocity/health/facing, Echo Step, completion/death progress, enemy health and bounded inventory. Version-one gameplay saves migrate by granting the newly introduced Maintenance Key, preserving their previous route access. Additional object/component state is intentionally not implicit.

## Remaining before broader game migration

- Extract a reusable application lifecycle coordinator when a second application needs it; today PROJECT ECHO's SDL adapter coordinates startup and shutdown while the existing SDL loop remains in control.
- Split state/actions/conditions into independently selectable compiled targets if a consumer requires finer granularity; currently these share the static core target.
- Design schema-specific migration tests for real content evolution.
- Extend the built-in-only editor proof with project-registered component types and additional runtime content integration. The game already instantiates Mira's built-in `Transform2D` from its startup config.
- Add localization and a font that can render it; Mira's existing English dialogue now uses bounded per-dialogue JSON, conditions, and the compiled inventory action.
- Build independent optional genre modules only when a game requires them; do not add placeholders.
- Migrate one feature at a time, keeping `src/game/` authoritative until an adapter has gameplay regression coverage.

The operator confirmed on 2026-11-20 that the controller-only framework build launches and runs on PS5, reported the PS5 save-provider diagnostic passing on 2026-11-21, and later confirmed gameplay autosaving and settings persistence work on the integrated build. These are operator-reported results; keyboard rebinding itself has not been tested on PS5.

On 2026-11-21, the operator also confirmed an earlier controller package works on PS5. The subsequent schema and dialogue integration requires a separate console test.

PROJECT ECHO now uses the bounded dialogue module for Engineer Mira, with the shared `echo::Game` still authoritative for inventory and progression. Reusable inventory, equipment, attributes, abilities, quests, RPG, combat, physics, movement, tile maps, camera, animation, audio, and UI modules remain future work; do not add empty placeholders.
