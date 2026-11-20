# Save system

`SaveCodec` serializes versioned JSON containing a game ID, format version, module IDs/versions, typed global/world/entity state, and an FNV-1a 64-bit integrity field over the canonical serialized prefix. Decode validates size (64 KiB), integrity, required fields, IDs, value types, game identity, module compatibility, and the complete candidate state before replacing destination values. An older format needs an explicit `SaveMigration`; newer or incompatible module versions are rejected.

The checksum detects accidental corruption, not malicious edits or authenticity. Saves must retain the encoder's exact byte representation.

`SaveService` accepts a host-provided `SaveStorage` table with read, temporary write, replace, temporary cleanup, and slot enumeration callbacks. Slot IDs are limited to 32 ASCII letters, digits, `_`, or `-`; they are not paths. The storage provider must ensure the temporary write cannot overwrite the current slot and that a failed replacement preserves the previous valid slot.

On Windows, `WindowsSaveStorage` places slots under `%LOCALAPPDATA%\PROJECT_ECHO\Saves`. It resolves the known-folder root, rejects reparse-point or non-user-owned directories/files, restricts slot names, creates unique temporary files without overwriting existing files, flushes their contents, and replaces slots through the Windows handle API. A failed replacement leaves the previous slot intact; save operations on one provider instance must be serialized. This provides atomic replacement on the same volume, but does not claim directory-entry durability across power loss. Windows tests exercise write, replacement, read, enumeration, and temporary cleanup in a unique test subdirectory.

On PS5, `Ps5SaveStorage` uses `/data/project_echo/saves` by default. It creates a private save subdirectory, restricts slot names to single safe identifiers, rejects symlink/non-regular save files, writes through an exclusive temporary file, flushes file contents, and replaces a slot with `rename`. Enumeration returns bounded, NUL-separated slot names. Calls on one provider instance must be serialized. This does not claim directory-entry durability across power loss or enforce a platform save quota.

The PS5 provider is compiled into an opt-in diagnostic package, not wired to gameplay. Build it with `scripts\build-ps5.ps1 -Target Game -StartupDiagnostics -SkipPs5Keyboard -SaveStorageDiagnostics`; it writes and replaces the reserved `framework_probe.sav`, reads it back, and enumerates slots. The probe intentionally leaves that reserved file in `/data/project_echo/saves` for inspection. The package passes native Windows static validation, but the probe has **not** been run on PS5 hardware; `/data/project_echo` being used for logs is not proof of save semantics. Treat PS5 persistence as unverified until the console run completes and the `PS5 save storage probe: PASS` result is collected from `/data/project_echo/logs/project_echo.log`.

The Windows provider is not yet wired to PROJECT ECHO gameplay. Tests also retain an in-memory storage fixture for the platform-neutral contract.

The serializer snapshots game state; it does not persist PROJECT ECHO's gameplay object graph or automatically include entity component pools.
