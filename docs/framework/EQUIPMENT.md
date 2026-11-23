# Bounded equipment

`EchoFramework::Equipment` is a statically linked, SDL-free module that
requires `EchoFramework::Attributes`. Definitions are loaded from bounded,
versioned JSON into fixed-capacity storage; data can select only equipment
definitions that the module understands and cannot load code. Each definition
has a stable equipment ID, a stable slot ID, and a bounded list of attribute
modifiers.

The game config's `equipment` object uses schema version 1:

```json
{
  "schemaVersion": 1,
  "definitions": [
    {
      "id": "echo:equipment/kinetic_coil",
      "slot": "echo:slot/suit",
      "modifiers": [
        {"attribute": "echo:attribute/player.move_speed", "amount": 30}
      ]
    }
  ]
}
```

The module accepts at most 32 definitions, 64 modifiers total, and 8 modifiers
per definition. IDs must be unique, valid content IDs; modifier amounts must be
nonzero 32-bit integers. Content is copied into module-owned storage, so the
input JSON document may be released after `load`.

`EquipmentLoadout<Capacity>` stores a fixed number of occupied slots. It rejects
unknown gear, duplicate equips, occupied slots, and capacity overflow.
Equipping applies modifiers through `AttributeSet`; if any modifier fails,
already-applied modifiers are rolled back before returning the error.
Unequipping removes the item's modifiers and preserves the loadout if a removal
fails. Gear-owned modifiers must be changed through the loadout, not through
the game's generic attribute modifier API.

## PROJECT ECHO

Mira offers `echo:equipment/kinetic_coil` once the player has the
Maintenance Key. The Coil occupies `echo:slot/suit` and adds 30 to
`echo:attribute/player.move_speed`, within that attribute's configured maximum.
The UI calls this slot **Body**; the framework ID remains `echo:slot/suit`.
The game has separate Head, Body, Weapon and Accessory storage. Enemy loot adds
Jump Visor (Head, jump +40), Vital Vest (Body, max health +1), Power Barrel
(Weapon, projectile damage +1), and Sprint Module (Accessory, speed +15).
Each item's equipment and slot IDs are declared in `assets/framework/items.json`
under catalog schema version 3. The compiled Equipment module remains the
allowlist for modifier definitions; a catalog binding is accepted only when its
equipment ID and slot match that compiled definition.
New gear auto-equips only into an empty
compatible slot, otherwise it enters the 20-slot backpack. Inventory transfers
allow unequipping/re-equipping the Coil, remove/apply its modifier atomically,
and clamp current horizontal speed when the bonus is removed.

Autosave format 8 stores equipment physically outside the backpack. Restore
reconstructs modifiers only from equipped slots; legacy format 1/2 checkpoints
move their backpack Coil to Body. The HUD displays `COIL ON` when equipped and
`COIL OFF` when unequipped; owning it in either compartment hides the repeat dialogue
offer. See [inventory](INVENTORY.md) for controls and compartment rules.

Build and run the headless checks on Windows with:

```powershell
.\scripts\build-windows.ps1 -Configuration Debug -RunTests
```

Tests cover module dependency metadata, slot occupancy, bounded modifier
application, duplicate equip, unequip, invalid-stat rollback, dialogue grant,
gameplay movement, and checkpoint re-equipping. PS5 package validation is
static and does not establish hardware behavior.
