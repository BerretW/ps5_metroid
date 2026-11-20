# Roadmap

## 0.2 implemented foundation

Module lifecycle, bounded entities/components, typed events, explicit actions, generic state/conditions, JSON wrapper, content validation, versioned state serialization, storage contract, headless demonstration, and a small Echo Step integration are implemented and covered by focused tests.

## Remaining before broader game migration

- Verify PS5 behavior of the new static yyjson-linked application on hardware; current compiler/linker/package checks do not prove runtime compatibility.
- Verify writable PS5 application-data storage and replacement semantics before supplying a persistent save backend.
- Decide and test a native Windows save-root provider.
- Add bounded JSON condition-tree decoding; conditions currently have a C++ data API only.
- Extract a reusable application lifecycle coordinator when a second application needs it; today PROJECT ECHO's SDL adapter coordinates startup and shutdown while the existing SDL loop remains in control.
- Split state/actions/conditions into independently selectable compiled targets if a consumer requires finer granularity; currently these share the static core target.
- Design schema-specific migration tests for real content evolution.
- Build independent optional genre modules only when a game requires them; do not add placeholders.
- Migrate one feature at a time, keeping `src/game/` authoritative until an adapter has gameplay regression coverage.

Dialogue, inventory, equipment, attributes, abilities, quests, RPG, combat, physics, movement, tile maps, camera, animation, audio, and UI are future modules, not 0.2 deliverables.
