# Game state

`GameState` stores boolean, signed integer, finite double, and bounded UTF-8 string values under namespaced keys. Keys use dot-separated namespaces such as `story.generator_repaired`; empty, unnamespaced, and overlong keys are rejected.

Scopes are `Global`, `World`, and `Entity`. Entity-scoped addresses include an `EntityId`; the store does not decide which world or entity is active. Set/get/get-or-default/contains/erase return explicit outcomes. Setting an unchanged value does not emit a notification.

Change observers run synchronously in registration order. Mutations from an observer are queued and delivered FIFO instead of recursively; the 64-entry pending queue fails the attempted mutation with `CapacityExceeded` when full. Observers must be removed before their context is destroyed. `replaceValues` is a validated bulk replacement for save restoration and intentionally does not emit per-key notifications.

PROJECT ECHO's `echo::Game` remains authoritative for ability ownership. The SDL adapter mirrors the `player.echo_step_unlocked` transition and emits an `AbilityUnlocked` event; this mirror resets on a new game and is not yet a persistent progression system.
