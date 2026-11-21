# Bounded abilities

`EchoFramework::Abilities` is a statically linked, SDL-free module that
requires `EchoFramework::Data` for its capability-manifest contract. The host
provides a bounded span of compiled `AbilityDefinition` values with stable
`namespace:ability/name` IDs. The definitions must remain alive while the
module uses them; JSON cannot add code or ability definitions.

`AbilityModule` validates the definition count, ID shape, and uniqueness during
initialization. `AbilitySet<Capacity>` tracks unlocked IDs in fixed storage and
reports unknown IDs, duplicate unlocks, and capacity exhaustion explicitly.
The framework records ownership; game-specific effects remain in the game's
simulation.

## PROJECT ECHO

The project selects `echo.rpg.abilities` and registers `echo:ability/echo_step`.
Collecting Echo Step unlocks it through the module; the shared game uses that
ownership to enable double jump and central-gate access. The existing boolean
checkpoint fields remain unchanged, and restore rebuilds the ability set from
the saved unlock state.

Run the headless Windows checks with:

```powershell
.\scripts\build-windows.ps1 -Configuration Debug -RunTests
```

Gameplay and module tests verify unlock validation, capacity, pickup effects,
and checkpoint reconstruction. PS5 build and package checks are static and do
not establish hardware behavior.
