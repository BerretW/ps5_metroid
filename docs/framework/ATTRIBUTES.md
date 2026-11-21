# Bounded attributes

`EchoFramework::Attributes` is a statically linked, SDL-free module backed by
`EchoFramework::Data`. `AttributeSet<AttributeCapacity, ModifierCapacity>`
copies compiled definitions into fixed storage; it does not allocate or load
attribute definitions or executable behavior from JSON.

Each definition has a stable content ID, base value, and inclusive minimum and
maximum. Values use signed 32-bit integers, while modifier arithmetic uses
signed 64-bit intermediates. Configuration is atomic: invalid, duplicate, or
over-capacity definitions leave the previous set unchanged. A modifier requires
a configured attribute, a stable source ID, and a nonzero amount; duplicate
sources and changes outside declared bounds fail without mutation.

PROJECT ECHO selects `echo.rpg.attributes` in
`assets/framework/game.json`. Its shared `echo::Game` configures four bounded
attributes for player max health, movement speed, jump impulse, and Health Cell
healing. `Game::addAttributeModifier` and `Game::removeAttributeModifier`
apply validated modifiers to those stats. The optional equipment module uses
the same bounded path for Mira's Kinetic Coil, which adds movement speed while
occupying the suit slot. Max-health reductions clamp current health; the other
attributes affect the next corresponding simulation or item use. Checkpoint
validation and the HUD use the configured maximum health.
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
