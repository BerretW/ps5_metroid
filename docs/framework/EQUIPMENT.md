# Bounded equipment

`EchoFramework::Equipment` is a statically linked, SDL-free module that
requires `EchoFramework::Attributes`. Definitions are compiled into the
executable; JSON cannot add gear or modifiers. Each definition has a stable
equipment ID, a stable slot ID, and a bounded list of attribute modifiers.

`EquipmentLoadout<Capacity>` stores a fixed number of occupied slots. It rejects
unknown gear, duplicate equips, occupied slots, and capacity overflow.
Equipping applies modifiers through `AttributeSet`; if any modifier fails,
already-applied modifiers are rolled back before returning the error.
Unequipping removes the item's modifiers and preserves the loadout if a removal
fails. Gear-owned modifiers must be changed through the loadout, not through
the game's generic attribute modifier API.

## PROJECT ECHO

Mira offers the compiled `echo:equipment/kinetic_coil` once the player has the
Maintenance Key. The Coil occupies `echo:slot/suit` and adds 30 to
`echo:attribute/player.move_speed`, within that attribute's compiled maximum.
The UI calls this slot **Body**; the framework ID remains `echo:slot/suit`.
The game has separate Head, Body, Weapon and Accessory storage. Enemy loot adds
Jump Visor (Head, jump +40), Vital Vest (Body, max health +1), Power Barrel
(Weapon, projectile damage +1), and Sprint Module (Accessory, speed +15).
New gear auto-equips only into an empty
compatible slot, otherwise it enters the 20-slot backpack. Inventory transfers
allow unequipping/re-equipping the Coil, remove/apply its modifier atomically,
and clamp current horizontal speed when the bonus is removed.

Autosave format 5 stores equipment physically outside the backpack. Restore
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
