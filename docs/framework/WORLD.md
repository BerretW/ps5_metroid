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
| `sprite` | `sheet`, `frameWidth`, `frameHeight` and draw `offset` [x, y]. |
| `bounds` | Collision size [w, h]. |
| `spawn` | Default `position` [x, y] and `facing` (`left`/`right`). |
| `animations` | Map of name → `row`, `column` (first frame), `frames`, `fps`, `loop`. |
| `defaultAnimation` | Animation used when the game has no more specific state. |
| `properties` | Bounded scalar values (numbers, booleans, strings) used by the game. |

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
| `size` | `columns` (20–64) and `rows` (at most 16; PROJECT ECHO uses 12). |
| `background` | RGB clear colour. |
| `tilesets` | Any number (up to 16) of sheets: `id`, `sheet`, `tileWidth`, `tileHeight`, optional `margin` and `spacing`. |
| `tiles` | Legend: one map symbol → `tileset`, `x`, `y` (cell on that sheet), optional `rotation` (0/90/180/270), `flipX`, `flipY` and `collision` (`none`, `solid`, `hazard`). |
| `layers` | Up to 4 named layers drawn in order; each is a list of row strings. `.` and space are empty. |
| `exits` | `left`/`right` with target `room`, optional `requiresAbility` or `completesGame`. |
| `objects` | Up to 16 placements: `id`, `object`, optional `position`, `facing` and `properties`. |

Room inheritance merges tilesets by `id`, tiles by symbol and placements by
`id`; child `layers` replace the parent's layers. The PROJECT ECHO rooms
inherit `station_base`, which declares the station and decor tilesets and a
shared legend. Power Station, Research Lab and Central Chamber redefine only
`#` and `=` to use their own tileset row.

Only the `main` layer's tile collision is used by the simulation; other layers
are decoration.

## Limits

48 objects, 12 rooms, inheritance depth 8 (cycles rejected), 12 animations and
16 properties per object, 16 tilesets, 64 tile types, 4 layers and 16
placements per room, 8 properties per placement, 32 KiB per file.

## PROJECT ECHO contract

`src/game/world_layout.cpp` converts the resolved data into `echo::WorldLayout`
and rejects layouts the simulation cannot represent:

- The five rooms of the route, 12 rows each, are fixed; their size, tiles,
  exits, enemies, door, pickup and props come from JSON.
- Enemies need `behavior` and `health` properties (`speed`, `patrolMin`,
  `patrolMax` optional). Save slots are assigned in placement order; each room
  keeps its existing enemy count so checkpoints remain compatible.
- Doors and triggers use `target`, `arrive` and optional `requiresItem`.
- Exactly one pickup; `player`, `engineer`, `projectile_player`,
  `projectile_enemy` and `loot_crate` objects must exist.
- The engineer's `spawn.position`, `spawn.facing` and `bounds` in
  `objects/engineer.json` (or inherited from its parent) place and orient her in
  the Landing Bay; the spawn must fit the room and not overlap solid tiles.
  `framework/game.json` holds only her `technician.interaction` reach and
  rejects a legacy `technician.transform`. To try it, change the engineer's
  `spawn.position` (for example `[400, 280]`) or `facing` and restart.

If the selected `echo.world` data fails to load or validate, startup logs the
error and exits instead of silently running different content. The built-in
`defaultWorldLayout()` (identical to the shipped JSON) is used only by
configurations that do not select `echo.world`, such as headless tests.

## Assets

`scripts/generate-assets.ps1` regenerates all original sheets, including
`sprites/enemies/`, `sprites/characters/`, `sprites/objects/`, `sprites/props/`
and `sprites/tiles/`. The renderer loads every unique sheet referenced by the
world once and draws animated objects and tile layers, including rotation and
flips.

## Validation

- `framework/tests/world_tests.cpp`: inheritance, merge rules, cycles, limits
  and malformed data.
- `tests/game_tests.cpp`: a wider custom room, a moved engineer spawn and
  rejected invalid layouts.
- `tests/framework_integration_tests.cpp`: shipped JSON equals the built-in
  layout, including the engineer spawn.
- `tests/sdl_renderer_tests.cpp`: offscreen sheet loading and tile drawing.
- `scripts/test-ps5-package.ps1`: packaged manifest, IDs, sprite references
  and the engineer's object-owned spawn.

None of these checks prove rendering or loading on PS5 hardware.
