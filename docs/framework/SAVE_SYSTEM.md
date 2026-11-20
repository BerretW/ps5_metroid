# Save system

`SaveCodec` serializes versioned JSON containing a game ID, format version, module IDs/versions, typed global/world/entity state, and an FNV-1a 64-bit integrity field over the canonical serialized prefix. Decode validates size (64 KiB), integrity, required fields, IDs, value types, game identity, module compatibility, and the complete candidate state before replacing destination values. An older format needs an explicit `SaveMigration`; newer or incompatible module versions are rejected.

The checksum detects accidental corruption, not malicious edits or authenticity. Saves must retain the encoder's exact byte representation.

`SaveService` accepts a host-provided `SaveStorage` table with read, temporary write, replace, temporary cleanup, and slot enumeration callbacks. Slot IDs are limited to 32 ASCII letters, digits, `_`, or `-`; they are not paths. The storage provider must ensure the temporary write cannot overwrite the current slot and that a failed replacement preserves the previous valid slot. The core does not claim that a platform rename is atomic or crash-safe.

No persistent storage backend is currently shipped. Windows save-root policy and PS5 writable app-data behavior have not been verified in this project. The PS5 app uses `/data/project_echo` for existing logs, but this is not evidence that a save path, quota, replacement, or durability contract is safe. Do not write saves on PS5 until that runtime path has been tested. Tests use an in-memory storage fixture.

The serializer snapshots game state; it does not persist PROJECT ECHO's gameplay object graph or automatically include entity component pools.
