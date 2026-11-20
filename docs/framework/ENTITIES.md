# Entities and components

`EntityStore<N>` creates stable `{index, generation}` handles, destroys them, and checks liveness. Destroying a handle advances its generation; a reused slot therefore cannot make an old handle valid again.

`ComponentPool<T, N>` is a simple fixed-capacity pool intended for plain data components:

```cpp
echo::framework::EntityStore<256> entities;
echo::framework::ComponentPool<Position, 256> positions;
const auto player = entities.create();
positions.set(player, entities, Position{10.0F, 20.0F});
```

Queries validate both the entity store and component generation. A destroyed entity's stale component is inaccessible even before its storage slot is reused. Store/entity capacity is selected at compile time. No component registry, reflection, SDL coupling, movement model, or automatic serialization is imposed.

PROJECT ECHO's existing `Player`, `Enemy`, and `Projectile` remain unchanged. A future migration can introduce an adapter only when a gameplay task needs it.
