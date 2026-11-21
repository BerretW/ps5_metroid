# Inventory and item catalog

`EchoFramework::Inventory` is a statically linked, SDL-free module. Its
`echo.rpg.inventory` module owns a bounded item catalog loaded from JSON;
`FixedInventory<ItemId, Capacity>` supplies deterministic fixed-capacity stacks
to a game-specific adapter. A data file can define metadata, but cannot load
code or choose a runtime factory.

## Item catalog

The version-one document is limited by the caller (PROJECT ECHO currently uses
the same 16 KiB limit as its other framework assets):

```json
{
  "schemaVersion": 1,
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
      "maxStack": 3
    }
  ]
}
```

The catalog accepts at most 32 entries. IDs must be valid stable content IDs;
numeric keys must be unique, nonzero, and stable across save versions.
`displayName` is copied into fixed storage (at most 63 bytes) and `maxStack`
must fit a nonzero 16-bit value. Unknown or repeated fields, duplicate IDs or
keys, unsupported schema versions, and capacity violations return explicit
errors. A failed reload leaves the last valid catalog unchanged.

## Inventory behavior

`FixedInventory` stores up to the consumer's compile-time slot capacity. Each
slot has an item key and a 16-bit quantity. Adding an amount is all-or-nothing;
existing stacks are filled in slot order before new slots are allocated. A
failed grant does not mutate the inventory. Removal rejects insufficient
quantities and compacts empty slots in stable order. `valid()` checks the slot
shape; game checkpoint restoration additionally checks item IDs and the
catalog-defined stack limits.

PROJECT ECHO selects `echo.rpg.inventory` in `assets/framework/game.json` and
loads `assets/framework/items.json`. Mira's compiled inventory action resolves
the requested stable item ID through that catalog before adding it. The
Maintenance Key opens the research lab; when the player is injured and has no
Health Cell, Mira offers one. Press H on Windows or R1 on PS5 to consume a cell
and restore up to two health; it is not consumed at full health. Cells stack to
three. The game checkpoint keeps its stable item keys and writes optional
per-slot quantities; older checkpoints without quantity fields continue to
decode as quantity one.

Build and run the headless checks with:

```powershell
.\scripts\build-windows.ps1 -Configuration Debug -RunTests
```

The framework tests cover catalog ownership, schema/version and duplicate
validation, stack splitting, atomic capacity failures, deterministic removal,
and static module metadata. Headless PROJECT ECHO tests cover the catalog-backed
dialogue grant, conditional Health Cell offer, healing and consumption. These
tests do not establish PS5 hardware behavior.
