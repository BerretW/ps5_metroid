# Inventory and item catalog

`EchoFramework::Inventory` is a statically linked, SDL-free module. Its
`echo.rpg.inventory` module owns a bounded item catalog loaded from JSON;
`FixedInventory<ItemId, Capacity>` supplies deterministic fixed-capacity stacks
to a game-specific adapter. A data file can define metadata, but cannot load
code or choose a runtime factory.

## Item catalog

The catalog document is limited by the caller (PROJECT ECHO currently uses
the same 16 KiB limit as its other framework assets):

```json
{
  "schemaVersion": 3,
  "items": [
    {
      "id": "echo:item/maintenance_key",
      "key": 1,
      "displayName": "Maintenance Key",
      "maxStack": 1
    },
    {
      "id": "echo:item/health_cell",
      "key": 2,
      "displayName": "Health Cell",
      "maxStack": 3,
      "description": "ACTIVE: HEAL UP TO 2",
      "use": {
        "effect": "heal",
        "amount": 2,
        "attribute": "echo:attribute/health_cell.heal"
      }
    },
    {
      "id": "echo:item/kinetic_coil",
      "key": 3,
      "displayName": "Kinetic Coil",
      "maxStack": 1,
      "equipment": {
        "id": "echo:equipment/kinetic_coil",
        "slot": "echo:slot/suit"
      }
    },
    {
      "id": "echo:item/scrap",
      "key": 4,
      "displayName": "Scrap",
      "maxStack": 99
    }
  ]
}
```

Schema version 2 adds optional descriptions and use-effect descriptors; version
3 adds an optional equipment binding with stable equipment and slot IDs. Versions
1 and 2 remain supported. Equipment bindings must match an initialized,
compiled equipment definition; JSON cannot define modifiers or load behavior.
The catalog accepts at most 32 entries. IDs must be valid stable content IDs;
numeric keys must be unique, nonzero, and stable across save versions.
`displayName` is copied into fixed storage (at most 63 bytes) and `maxStack`
must fit a nonzero 16-bit value. Unknown or repeated fields, duplicate IDs or
keys, unsupported schema versions, and capacity violations return explicit
errors. A failed reload leaves the last valid catalog unchanged.

An item may also provide a bounded `description` and a `use` descriptor.
Compiled effect handlers support healing, shield duration, speed/jump boosts,
and bounded area damage; the catalog owns all effect parameters and strings.
Effects accept at most 1,000 units, durations are at most 480 fixed steps, and
effect IDs cannot load code. The optional description is used by the inventory
UI rather than an item-ID switch in presentation code.

## Inventory behavior

`FixedInventory` stores up to the consumer's compile-time slot capacity. Each
slot has an item key and a 16-bit quantity. Adding an amount is all-or-nothing;
existing stacks are filled in slot order before new slots are allocated. A
failed grant does not mutate the inventory. Removal rejects insufficient
quantities and compacts empty slots in stable order. `valid()` checks the slot
shape; game checkpoint restoration additionally checks item IDs and the
catalog-defined stack limits.

`SlotInventory<ItemId, Capacity>` uses the same stack operations but preserves
empty slots: removing an item never moves neighboring stacks. Its `count` is
the number of occupied slots, not the last occupied index. Grants fill existing
stacks, then reuse the first empty slots. The default `FixedInventory` retains
its compact behavior.

## Playable compartments

PROJECT ECHO selects `echo.rpg.inventory` in `assets/framework/game.json` and
loads `assets/framework/items.json`. Mira's compiled inventory action resolves
the requested stable item ID through that catalog before adding it.

- **Backpack:** 20 stable slots for miscellaneous items, keys and cell stacks.
- **Equipment:** separate Head, Body, Weapon and Accessory slots. Gear
  auto-equips only when its compatible slot is empty; otherwise it enters the
  backpack.
- **Active item:** one separate physical stack, not a backpack reference.
  It accepts Health Cells and the six new consumables, up to three per stack.

Open inventory with I on Windows or pause and press Triangle/Y on a controller.
Use arrows/WASD or D-pad; Enter/Space or Cross/A first selects a source, then
confirms its destination. Compatible swaps and complete cell-stack merges are
atomic. Overflow or incompatible equipment/active transfers leave both slots
unchanged. Escape/I or Circle/B cancels a selection before closing to pause.
Gameplay is frozen while the inventory is open; successful moves autosave.

The Maintenance Key opens the research lab regardless of its backpack position.
Mira offers a Health Cell when injured and none is owned across compartments.
New cells enter the backpack; move one stack to Active before pressing H/R1 to
restore up to two health. Full health does not consume a cell. Mira offers the
unique Kinetic Coil after acquiring the key. Moving it between Body and the
backpack applies/removes its speed bonus; owning an unequipped Coil still hides
the repeat grant.

Enemies drop Scrap when defeated: the Maintenance Tunnel enemy drops two and
the Power Station enemy drops three. Drops appear at the enemy's defeat position
and are collected by touching them. Scrap stacks to 99; if the backpack has no
room, the drop remains in the world until space is freed. The HUD shows the
carried Scrap total, and the normal pickup sound plays when it is collected.

## Enemy loot data

PROJECT ECHO's `game.json` declares the bounded version-1 `loot` table. Each
entry references a stable item resource and defines its stable `saveSlot`,
`enemySlot`, and quantity. The table must cover all 12 save slots exactly once;
the loader checks each item against the catalog and its stack limit. Enemy room
and position come from the loaded world spawn. Save-slot indices remain stable
because existing autosaves persist loot records by index.

Autosave format 8 retains inventory layout 2: all 20 slot pairs (including
holes), four equipment pairs and the active stack. Formats 1–7 migrate explicitly:
legacy keys remain in their bag positions, the Coil moves to Body, and the first
Health Cell stack moves to Active. Format 1 still receives the Maintenance Key
for its previously unlocked route. Formats 1–3 initialize loot records empty so
old defeated enemies do not grant retroactive drops. Legacy missing quantities
default to one; new-layout fields are mandatory. Format 6 also persists the
selected quest route and reward claim. See [save system](SAVE_SYSTEM.md).

Format 5 adds three enemy health values, ten individual drops and the remaining
durations of the speed, jump and shield effects. Format 8 also persists the
configured speed/jump magnitudes for an active or recently expired boost;
formats 1–7 migrate with the legacy magnitudes. Format 4's existing Scrap
drops are preserved; formats 1–3 still initialize those older drops empty.
New enemies are alive when an old save is migrated. See [controls](../CONTROLS.md)
for the enemies, item effects, and equipment slots.

Build and run the headless checks with:

```powershell
.\scripts\build-windows.ps1 -Configuration Debug -RunTests
```

The framework tests cover catalog ownership, schema/version and duplicate
validation, stack splitting, atomic capacity failures, deterministic removal,
stable holes, and static module metadata. Headless PROJECT ECHO tests cover
dialogue grants, active-only consumption, typed transfers, stack limits,
equipment effects, sparse checkpoints and legacy migration. Offscreen SDL tests
cover menu state, compartment rendering and platform prompts. These tests do
not establish PS5 hardware behavior.
