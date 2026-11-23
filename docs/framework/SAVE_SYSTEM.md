# Save system

`SaveCodec` serializes versioned JSON containing a game ID, format version, module IDs/versions, typed global/world/entity state, and an FNV-1a 64-bit integrity field over the canonical serialized prefix. Decode validates size (64 KiB), integrity, required fields, IDs, value types, game identity, module compatibility, and the complete candidate state before replacing destination values. An older format needs an explicit `SaveMigration`; newer or incompatible module versions are rejected.

The checksum detects accidental corruption, not malicious edits or authenticity. Saves must retain the encoder's exact byte representation.

`SaveService` accepts a host-provided `SaveStorage` table with read, temporary write, replace, temporary cleanup, and slot enumeration callbacks. Slot IDs are limited to 32 ASCII letters, digits, `_`, or `-`; they are not paths. The storage provider must ensure the temporary write cannot overwrite the current slot and that a failed replacement preserves the previous valid slot.

On Windows, `WindowsSaveStorage` places slots under `%LOCALAPPDATA%\PROJECT_ECHO\Saves`. It resolves the known-folder root, rejects reparse-point or non-user-owned directories/files, restricts slot names, creates unique temporary files without overwriting existing files, flushes their contents, and replaces slots through the Windows handle API. A failed replacement leaves the previous slot intact; save operations on one provider instance must be serialized. This provides atomic replacement on the same volume, but does not claim directory-entry durability across power loss. Windows tests exercise write, replacement, read, enumeration, and temporary cleanup in a unique test subdirectory.

On PS5, `Ps5SaveStorage` uses `/data/project_echo/saves` by default. It creates a private save subdirectory, restricts slot names to single safe identifiers, rejects symlink/non-regular save files, writes through an exclusive temporary file, flushes file contents, and replaces a slot with `rename`. Enumeration returns bounded, NUL-separated slot names. Calls on one provider instance must be serialized. This does not claim directory-entry durability across power loss or enforce a platform save quota.

The SDL application now uses the platform provider for one `autosave` slot. It stores a bounded gameplay checkpoint (room, player position/velocity/health/facing, ability, completion/death progress, enemy health, uncollected enemy loot and inventory), exposes **Continue** in the main menu when the slot exists, and rejects corrupt, incompatible or invalid checkpoints without modifying the running game. Version-one checkpoints migrate by granting the Maintenance Key because that route was previously open without it. It saves when a new game starts, on room/death/ability/completion/enemy-defeat/inventory changes, every 30 seconds during play, when returning to the main menu, and on orderly application shutdown. Starting a new game replaces the single autosave. Abrupt termination can lose movement since the last successful autosave; there are no manual slots or platform save quotas.

The JSON dialogue integration does not change the autosave version or stored module list. An active conversation is not saved; after Continue, speaking to Mira starts it again and her choices reflect the restored inventory.

Slot-based inventory introduced gameplay autosave **format 3**, with
`inventory.layout = 2`. It requires all 20 backpack item/quantity pairs,
an occupied-slot count, four equipment pairs and one active-item pair.
Empty slots remain at their original indices. Decode rejects missing fields,
invalid stack sizes, incompatible gear/active items and duplicate unique items
before changing the game. Successful inventory transfers autosave immediately.
Formats 1 and 2 migrate through the codec's explicit callback: version 1 keeps
its Maintenance Key grant, while version 2 retains its existing inventory.
Legacy compact inventories have at most eight slots and optional quantities
(default one); game restore moves their Coil to Body and their first Health Cell
stack to Active. Equipment is rebuilt from equipped slots, not merely owned
backpack items. Settings retain their independent existing format.

Enemy loot extends autosave to **format 4**. Each fixed enemy drop records its
active state, room, Scrap stack and position, so a defeat or pickup cannot
duplicate or erase a drop across Continue. Formats 1–3 migrate with empty loot
records; old defeated enemies do not produce retroactive Scrap.

Format 5 adds three enemy health values, ten bounded new loot records and
remaining speed, jump and shield effect durations. Migration from format 4
preserves its two Scrap drops; formats 1–3 retain their existing migration.
All older formats start the new enemies alive. New loot records derive their
item and enemy save-slot association from the versioned `game.json` loot table,
and their room from the loaded world spawn. The active state and horizontal
position are persisted; on Continue, the configured table reconstructs item
stacks for the extra records and `Game::restore` validates the complete set.
Enemy movement phases and projectiles are not saved and restart on Continue.

The app also stores volume and keyboard bindings in a separate `settings` slot, leaving `autosave` untouched. Settings load at startup, save after changes and again on orderly shutdown, and are range/type checked before applying. Both slots use the same platform storage directory and versioned `SaveCodec`, but distinct game/schema IDs. The operator confirms settings persistence works on the PS5 build; keyboard rebinding itself remains unavailable to test there because PS5 keyboard input is disabled.

The PS5 provider uses `/data/project_echo/saves`. Its separate diagnostic package is still available with `scripts\build-ps5.ps1 -Target Game -StartupDiagnostics -SkipPs5Keyboard -SaveStorageDiagnostics`; it exercises the reserved `framework_probe.sav` slot and intentionally leaves it for inspection. Initial console runs showed `lstat` and `opendir` return EPERM (1), and 4 KiB `getdents` returned EINVAL (22). The provider now falls back from `lstat` to `open(O_NOFOLLOW | O_NONBLOCK)` plus `fstat`, and enumerates using ordered 64 KiB directory-reader variants with bounds-checked records. The operator's fourth diagnostic run reported `enumerate: method=getdents64k` and `PS5 save storage probe: PASS`, confirming the provider's directory, write, replacement, read-back and enumeration path on hardware. On 2026-11-21, the operator also confirmed that gameplay autosaving works on the PS5 using the integrated controller build; this is a single operator-reported hardware test.

The serializer snapshots explicitly selected gameplay state; it does not automatically persist arbitrary gameplay objects or entity component pools. Tests also retain an in-memory storage fixture for the platform-neutral contract.
