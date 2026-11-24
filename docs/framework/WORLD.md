# World objects, animations and rooms

`EchoFramework::World` (`echo.world`) loads bounded JSON definitions for every
game object and room, resolves `parent` inheritance, and validates references
before the game uses them. It has no SDL or file-system dependency: the
platform layer reads files and passes parsed `JsonValue` documents to
`WorldModule::loadManifest`, `addObject`, `addRoom` and `finalize`.

## Files and IDs

```
assets/world/world.json          manifest: startRoom, objects[], rooms[]
assets/world/objects/<name>.json echo:object/<name>
assets/world/rooms/<name>.json   echo:room/<name>
```

Every file has `"schemaVersion": 1`, an `id` matching its manifest entry and
at most 32 KiB. Sprite paths are relative to `assets/` (for example
`sprites/enemies/heavy.png`); absolute paths and `..` are rejected.

## Objects

| Field | Meaning |
|---|---|
| `parent` | Optional object ID to inherit from. |
| `abstract` | Template only; it cannot be placed in a room. |
| `category` | `player`, `enemy`, `npc`, `projectile`, `pickup`, `door`, `trigger`, `loot` or `prop`. |
| `sprite` | `sheet`, `frameWidth`, `frameHeight`, draw `offset` [x, y], optional `tint` and `rotation`. |
| `bounds` | Collision size [w, h]. |
| `spawn` | Default `position` [x, y] and `facing` (`left`/`right`). |
| `animations` | Map of name → either `row`, `column` (first frame) and `frames`, or `cells`; plus `fps`, `loop`, optional `tint`, `rotation`, `spin`. |
| `defaultAnimation` | Animation used when the game has no more specific state. |
| `properties` | Bounded scalar values (numbers, booleans, strings) used by the game. |

Animation frames come from consecutive cells (`row`, `column`, `frames`) or from an
explicit `cells` list of `[column, row]` pairs (1-16) that may sit anywhere on the sheet, in
any order and repeat (`"cells": [[0, 1], [2, 1], [1, 1]]`). `cells` excludes `row`, `column`
and `frames`; a child may replace one form with the other.

`tint` is `[r, g, b]` or `[r, g, b, a]` with channels 0-1. `rotation` is in degrees
(-360..360, clockwise) and `spin` adds degrees per second (-1440..1440) for continuous turning.
The sprite-level and animation-level values combine: tints multiply and rotations add.
Rotation pivots on the frame centre and reverses when the sprite is mirrored. Both are plain
data, so objects, children and animations can recolor or turn independently (shipped examples:
the Echo Step pickup spins in cyan, the stalker's idle cycles non-adjacent cells in red, the
Hangar door is tinted blue).

Inheritance merges field by field: a child replaces only the values it states.
Animations merge per animation and per field, so `enemy_patrol_heavy` sets only
`walk.fps` and keeps the parent's rows and frame counts. Properties merge by
key. Example:

```json
{
  "schemaVersion": 1,
  "id": "echo:object/enemy_patrol_heavy",
  "parent": "echo:object/enemy_patrol",
  "sprite": { "sheet": "sprites/enemies/heavy.png", "offset": [-1, -4] },
  "bounds": [30, 28],
  "animations": { "walk": { "fps": 6 } },
  "properties": { "health": 3, "speed": 52 }
}
```

A concrete object must resolve to a category, a sheet with a frame size,
bounds and at least one animation whose frames fit the sheet.

## Rooms

| Field | Meaning |
|---|---|
| `size` | `columns` (20–96) and `rows` (at most 48; PROJECT ECHO side-view rooms use 12). |
| `projection` | `side` (default), `top-down` or `isometric`; inherited like other room fields. |
| `background` | RGB clear colour. |
| `lighting` | Optional per-room ambient/directional lighting, a room day/night cycle and up to 16 point/spot definitions. |
| `tilesets` | Any number (up to 16) of sheets: `id`, `sheet`, `tileWidth`, `tileHeight`, optional `margin`, `spacing` and `material`. |
| `tiles` | Legend: one map symbol → `tileset`, `x`, `y` (cell on that sheet), optional `rotation` (0/90/180/270), `flipX`, `flipY` and `collision` (`none`, `solid`, `hazard`). |
| `layers` | Up to 4 named tile layers drawn in order; each is a list of row strings. Optional `scrollFactorX` (0–1, default 1) controls horizontal parallax in side-view rooms. `.` and space are empty. |
| `exits` | `left`/`right` with target `room`, optional `requiresAbility` or `completesGame`. |
| `objects` | Up to 16 placements: `id`, `object`, optional `position`, `facing` and `properties`. |

Room inheritance merges tilesets by `id`, tiles by symbol and placements by
`id`; child `layers` replace the parent's layers. The PROJECT ECHO rooms
inherit `station_base`, which declares the station and decor tilesets and a
shared legend. Power Station, Research Lab and Central Chamber redefine only
`#` and `=` to use their own tileset row.

### Lighting data

`game.json.worldLighting` defines world ambient and directional lighting. A
room's configured ambient or directional light overrides that world value.
Ambient `dayNight` may define `nightColor`, `nightIntensity` (0–8),
`periodSeconds` (10–86400) and optional `phaseSeconds`; interpolation follows
a smooth day-to-night-to-day cosine curve. Room ambient can have its own cycle.

`lighting.ambient` accepts linear RGB `color` and `intensity` (0–8).
`lighting.directional` accepts a non-zero 2D `direction`, linear RGB `color`
and `intensity` (0–8). Each entry in `lighting.lights` has a stable light `id`,
`type` (`point` or `spot`), world `position`, `radius`, linear `color`,
`intensity` (0–8), optional `attenuation`, and optional `enabled`, `parent`,
`circuit`, `shadow`, `persistence`, bounded `flicker`, or `fadeSeconds`
(0–30). A positive fade duration smoothly interpolates intensity when the
enabled state or power circuit changes. Spotlights
require `coneAngle` and either a fixed `direction` or `followsAim: true` with
`parent: "player"`; aim-following spotlights rotate with the player's current
aim (or horizontal facing in side-scrolling rooms). A room may define up to 16 lights;
the current Vulkan canvas evaluates at most four per frame. Circuit IDs must
refer to configured `echo.power` circuits.
`parent` may name a static room placement, `player`, `enemy.<placementId>` or
`roamer.<placementId>`; player, enemy and roamer parents follow their live
gameplay transforms. Prop placements have stable room-local IDs and may be moved
at runtime with `Game::setPropPosition`; both the prop draw and an attached light
use the current transform. Runtime prop positions reset for a new game and are
not part of `GameCheckpoint`. Other static placement parent transforms are
resolved once when world content is finalized; enabled, persistence and circuit
state are still checked at runtime.
`FrameworkIntegration::setWorldLightEnabled(roomId, lightId, enabled)` stores
overrides for `room` and `persistent` lights in framework state. Transient
lights reject persistent overrides.

Object `sprite.material` and room `tilesets[].material` can bind optional
`normalSheet` and `emissiveSheet` PNG paths. Maps must have the same dimensions
as their base sheet and use the same frame/tile UVs. `emissiveColor` is linear
RGB, `emissiveIntensity` is 0–8, and an optional `emissiveCircuit` gates the
emitter. Material definitions inherit with objects and tilesets; an explicit
empty `material` object clears inherited maps.

The current playable example is the Cargo Hold: its consoles toggle the main
or emergency power circuit independently. Its original emergency
point light is attached to the `power_panel` placement and follows its runtime
transform. Circuit requested/effective state is included in the game's saved
framework state.
The Vulkan canvas converts its NDC fragment positions back to logical pixels
before evaluating the bounded point/spot list. It keeps lights whose radius
intersects the visible 640x360 view, then selects the nearest four
deterministically. The room projection helpers map side, top-down and
isometric positions/directions consistently. `FrameState.lightScreenToWorld`
maps projected screen offsets back to world-space offsets before point-light
attenuation and spotlight-cone evaluation. Isometric light radii therefore
project to the correct ellipse instead of a screen-space circle; culling uses
a conservative screen bound and prioritization uses world-space distance.
`top-down` is currently a content and renderer fixture projection, not a new
gameplay movement mode. Windows GPU readback verifies projected point-light
attenuation and spotlight direction in addition to the three coordinate
transforms, zero-ambient blackout, UI isolation, directional normal-map
response and emissive-mask output.

The Cargo Hold GPU integration fixture captures the powered room, encodes the
game checkpoint together with circuit and persistent light state through the
versioned `SaveCodec`, decodes and restores it, then captures the main-circuit
outage. Pixel readback verifies that the overhead spot darkens while the
emergency-circuit emissive console remains visible. This is a Windows
GPU/save-codec integration result, not PS5 hardware evidence.

`CanvasBatch2D` exposes optional linear normal textures and emissive masks for
Vulkan world draws. Directional and point lights use the supplied normal, and
emission is added independently of world lighting. Runtime prop transforms can
drive attached lights, but there is no generic entity-component parent resolver
or authored moving-prop gameplay behavior. Point lights with `shadow: true`
cast bounded GPU shadows against visible solid-tile boundary edges and prop
rectangles. The renderer retains the 32 nearest eligible edges and logs when
the bound is exceeded. Immutable solid-tile boundary edges are cached per room
definition; the visible, light-prioritized segment list is rebuilt each frame
to account for camera and light movement. The list also includes the player,
living enemies, and room props as rectangular occluders. Projectiles and
particles do not cast shadows. Static light accumulation is still evaluated per
frame, there is no SDL lighting fallback or HDR bloom.
Windows Vulkan Debug and Release readbacks verify an illuminated sample, a
blocked sample, and an unaffected UI overlay. The GPU fixture is not PS5
hardware evidence.

Only the `main` layer's tile collision is used by the simulation; other layers
are decoration. Side-view layer 0 may use `scrollFactorX` below 1 for a distant
background; 0 stays fixed while 1 follows the camera. The Landing Bay's `back`
layer uses 0.35, while its terrain remains at 1. Object placements are drawn
after tile layers and continue to provide room props. Isometric rooms require
the default factor because their tile layers participate in depth sorting.

## Limits

48 objects, 12 rooms, inheritance depth 8 (cycles rejected), 12 animations and
16 properties per object, 16 tilesets, 64 tile types, 4 layers and 16
placements per room, 8 properties per placement, 32 KiB per file.

## PROJECT ECHO contract

`src/game/world_layout.cpp` converts the resolved data into `echo::WorldLayout`
and rejects layouts the simulation cannot represent:

- The six rooms (the five-room route plus the isometric Cargo Hold) are fixed;
  side-view rooms have 12 rows, while isometric rooms may have up to 48.
  Rooms have at most 96 columns. Their tiles, exits, enemies, door, pickup
  and props come from JSON. The shipped Cargo Hold is 96 by 48 cells.
- Enemies need `behavior` and `health` properties (`speed`, `patrolMin`,
  `patrolMax` optional). Save slots are assigned in placement order; each room
  keeps its existing enemy count so checkpoints remain compatible.
- Chargers require numeric `detectionRange` (0-4096 world pixels) and
  `chargeSpeed` (0-400 pixels/second); stalkers require `detectionRange` in
  the same range. Object properties are inherited and room placements may
  override them. Zero range disables pursuit, and zero charge speed stops a
  charger while it detects the player. Invalid or missing values fail world
  loading. For example, change a charger's `chargeSpeed` in its object JSON or
  placement and restart to try a different pursuit speed.
- Sentries require numeric `detectionRange` (0-4096 world pixels),
  `projectileSpeed` (0-600 pixels/second), and `fireInterval` (one 1/60-second
  fixed step through 60 seconds). These properties can also be inherited or
  overridden by a placement. A zero range prevents firing; a zero projectile
  speed produces stationary shots. The projectile pool remains bounded, and
  the interval starts only when a projectile is successfully spawned.
- Armored enemies require numeric `guardCycle` (0.05-60 seconds) and
  `guardStart` (at least zero and strictly less than the cycle), plus integer
  `guardReduction` (0-99 damage points). They patrol until the guard starts,
  stop and reduce damage while guarding, then resume after the cycle wraps.
  The minimum cycle accommodates the simulation's maximum 0.05-second update;
  phase transitions are observed at update boundaries. These properties also
  support inheritance and placement overrides. The guard timer resets on
  checkpoint restore, as before.
- `behavior: "stalker"` enemies may only be placed in isometric rooms, must
  start on open floor, are not saved and respawn whenever the room is entered.
  Saved (side-view) enemies cannot be placed in isometric rooms.
- Doors and triggers (`category: "door"` / `"trigger"`, at most 8 per world) accept these
  properties on the archetype or as placement overrides:
  - Conditions, all of which must hold: `requiresItem`, `requiresAbility`, `requiresCircuit`
    (the circuit must be powered) and `requiresFact` (set by a fired `setsFact` trigger, a
    main-quest fact, or `echo:fact/cargo_emergency_powered`). An unmet condition plays the
    denied feedback.
  - Destination: `target` plus either `arrive` (`"left"`/`"right"`), `arriveX` and `arriveY`, or
    `targetPlacement` (an object placement ID in the target room; its top-left position is used).
    `arrive`, `arriveX`/`arriveY` and `targetPlacement` are mutually exclusive. The player
    rectangle at a custom arrival must fit the target room and not overlap solid tiles.
  - Actions: `cutscene`, `setsFact` and `toggleCircuit`. A trigger may omit `target` when it
    defines at least one action; it then fires automatically on overlap and takes no arrival
    properties. `once` (Boolean) defaults to `true` for event-only triggers and `false` for
    transitions; fired state is a per-door bitmask stored in checkpoints and saves
    (`world.triggers_fired`, optional for older saves).
  - All IDs must be valid content IDs. Startup rejects references to circuits missing from the
    power module, cutscenes missing from the cutscene module, abilities missing from the
    ability module, and items missing from the item catalog.
  - Shipped examples: `trigger_power_survey` (event-only `setsFact` trigger) and
    `door_power_bypass` (Power Station → Maintenance Tunnel, `requiresCircuit` +
    `requiresFact` + `arriveX`/`arriveY`) require visiting the survey trigger and powering the
    Cargo Hold emergency circuit; `door_power_return` uses `targetPlacement` and `requiresAbility`.
  - `shmup` (a stage ID listed in `assets/shmup/stages.json`, defined in `assets/shmup/stages/*.json`) turns a door into the entrance of a
    SHMUP stage: it takes no `target` or arrival properties, still honours every condition, and
    unknown stages or reward items are rejected at startup. Example: `door_shmup_hangar` in the
    Landing Bay. See [SHMUP.md](SHMUP.md).
- Props may use placement properties `interaction: "toggle_power"` and
  `circuit` for the bounded in-game power-panel action. A prop may also declare
  `dialogue` with a stable dialogue resource ID. Optional
  `dialoguePowerCircuit` and `dialoguePowerState` properties project that
  circuit's current state to the named global dialogue condition key before
  starting the conversation. These bindings are validated against the loaded
  dialogue resources and power module; the simulation does not branch on a
  prop's placement ID.
- Exactly one pickup; `player`, `player_iso`, `engineer`, `projectile_player`,
  `projectile_enemy` and `loot_crate` objects must exist.
- The two player views inherit `bounds` and `spawn` from
  `objects/player_base.json`. The simulation uses the side-view player's
  resolved bounds for both projections, including collision, room transitions,
  interaction and checkpoint validation; both views must resolve to the same
  bounds. Each dimension must be finite and between 1 and 32 world units.
  The spawn position and facing initialize new games. Changes to bounds can
  make an existing saved position invalid, in which case restore is rejected.
  Player attribute definitions remain in `framework/game.json`; behavior and
  attribute IDs are still implemented by the project game.
- The engineer's `spawn.position`, `spawn.facing` and `bounds` in
  `objects/engineer.json` (or inherited from its parent) place and orient her in
  the Landing Bay; the spawn must fit the room and not overlap solid tiles.
  `framework/game.json` holds only her `technician.interaction` reach and
  rejects a legacy `technician.transform`. To try it, change the engineer's
  `spawn.position` (for example `[400, 280]`) or `facing` and restart.

If the selected `echo.world` data fails to load or validate, startup logs the
error and exits instead of silently running different content. The built-in
`defaultWorldLayout()` is used only by configurations that do not select
`echo.world`, such as headless tests. It retains the original smaller Cargo
Hold; the shipped JSON contains the expanded level.

## Isometric rooms

An isometric room keeps the same 32 px simulation grid, read as a flat floor
plane seen from above: there is no gravity, the player walks in eight screen
directions and shoots along the last direction, solid tiles are walls, and
hazard tiles are walkable pools that damage. The renderer projects grid point
(x, y) to screen (x − y, (x + y) / 2), so each cell becomes a 64x32 diamond;
the camera follows the player on both axes. Layer 0 is drawn flat as floor;
higher layers, actors and shots are depth-sorted by their front footprint
corner, and blocks that cover the player are drawn translucent.

Tilesets for isometric rooms use bottom-aligned cells: `iso_floor.png` has
64x32 diamonds and `iso_blocks.png` 64x64 blocks whose base diamond fills the
bottom 32 px. Isometric objects (`player_iso`, `enemy_stalker`) set their
sprite `offset` relative to the footprint centre. `player_iso` provides
`idle_`/`walk_` animations for directions `e`, `se`, `s`, `ne` and `n`; the
west-facing directions are mirrored.

## Assets

`scripts/generate-assets.ps1` regenerates all original sheets, including
`sprites/enemies/`, `sprites/characters/`, `sprites/objects/`, `sprites/props/`
and `sprites/tiles/`. The renderer loads every unique sheet referenced by the
world once and draws animated objects and tile layers, including rotation and
flips.

## Validation

- `framework/tests/world_tests.cpp`: inheritance, merge rules, cycles, limits,
  room projection and malformed data.
- `tests/game_tests.cpp`: a wider custom room, a moved engineer spawn,
  rejected invalid layouts and isometric movement, walls, pools, stalkers,
  diagonal shots and the Cargo Hold doorways.
- `tests/framework_integration_tests.cpp`: unchanged side rooms match the
  built-in layout; expanded Cargo dimensions, reachable placements, doorway,
  checkpoint compatibility and the engineer spawn are checked separately.
- `tests/sdl_renderer_tests.cpp`: offscreen sheet loading, tile drawing and an
  isometric frame that follows the player.
- `scripts/test-ps5-package.ps1`: packaged manifest, IDs, sprite references
  and the engineer's object-owned spawn.

None of these checks prove rendering or loading on PS5 hardware.
