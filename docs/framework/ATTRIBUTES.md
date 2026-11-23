# Bounded attributes

`EchoFramework::Attributes` is a statically linked, SDL-free module backed by
`EchoFramework::Data`. The module loads bounded, versioned attribute data from
JSON and copies it into fixed-capacity storage; it never loads executable
behavior. `AttributeSet<AttributeCapacity, ModifierCapacity>` applies the
loaded definitions and modifiers without allocation.

The game config's `attributes` object uses schema version 1:

```json
{
  "schemaVersion": 1,
  "definitions": [
    {
      "id": "echo:attribute/player.move_speed",
      "baseValue": 190,
      "minimum": 100,
      "maximum": 250
    }
  ]
}
```

The module accepts at most 8 definitions. IDs must be unique valid content
IDs, and each signed 32-bit base value must lie within its inclusive bounds.
The module owns its copied IDs, so the parsed JSON document may be released
after `load`.

Each definition has a stable content ID, base value, and inclusive minimum and
maximum. Values use signed 32-bit integers, while modifier arithmetic uses
signed 64-bit intermediates. Configuration is atomic: invalid, duplicate, or
over-capacity definitions leave the previous set unchanged. A modifier requires
a configured attribute, a stable source ID, and a nonzero amount; duplicate
sources and changes outside declared bounds fail without mutation.

PROJECT ECHO selects `echo.rpg.attributes` in
`assets/framework/game.json`, which defines five attributes for player max
health, movement speed, jump impulse, Health Cell healing, and projectile
damage. These authored defaults and bounds no longer live in `src/game/game.cpp`.
`Game::addAttributeModifier` and `Game::removeAttributeModifier` apply
validated modifiers to those stats. The optional equipment module uses the
same bounded path for gear modifiers. Max-health reductions clamp current
health; the other attributes affect the corresponding simulation or item use.
Checkpoint validation and the HUD use the configured maximum health.
Configurations that do not select the optional module preserve the existing
gameplay defaults.

Build and run the headless checks on Windows with:

```powershell
.\scripts\build-windows.ps1 -Configuration Debug -RunTests
```

Framework tests cover metadata, lifecycle, base values, modifier addition and
removal, duplicate sources, bounds, and failed reconfiguration. Gameplay tests
verify that configured modifiers affect deterministic movement. These results
do not establish PS5 hardware behavior.
