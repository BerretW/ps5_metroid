# Bounded abilities

`EchoFramework::Abilities` is a statically linked, SDL-free module that
requires `EchoFramework::Data` for its capability-manifest contract. It loads
versioned JSON definitions with stable `namespace:ability/name` IDs, validates
them, and copies IDs into fixed-capacity module-owned storage. JSON defines
content only; it cannot add code or select uncompiled modules.

The schema is:

```json
{
  "schemaVersion": 1,
  "definitions": [
    {"id": "echo:ability/echo_step"}
  ]
}
```

`AbilityModule` accepts 1–32 unique IDs, with IDs shorter than 128 bytes.
`AbilitySet<Capacity>` tracks unlocked IDs in fixed storage and reports unknown
IDs, duplicate unlocks, and capacity exhaustion explicitly. The framework
records ownership; reusable or game-specific effects remain in simulation code.

## PROJECT ECHO

`assets/framework/game.json` selects `echo.rpg.abilities` and declares
`echo:ability/echo_step`. Startup and the host content validator both load the
same definition. Collecting Echo Step unlocks it through the module; the shared
game uses that ownership to enable double jump and central-gate access. Existing
boolean checkpoint fields remain unchanged, and restore rebuilds the ability
set from the saved unlock state.

Run the headless Windows checks with:

```powershell
.\scripts\build-windows.ps1 -Configuration Debug -RunTests
```

Gameplay and module tests verify unlock validation, capacity, pickup effects,
and checkpoint reconstruction. PS5 build and package checks are static and do
not establish hardware behavior.
