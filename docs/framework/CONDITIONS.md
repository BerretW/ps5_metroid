# Conditions

Conditions are data structures evaluated against `GameState`, not source text or expressions passed to `eval`. Implemented leaf operations are key existence and typed comparisons. Integer/number values support ordering; equality and inequality require the same value type. Logical AND, OR, and NOT compose child spans.

```cpp
const auto hasAbility = echo::framework::Condition::compare(
    {echo::framework::StateScope::Global, {}, "player.echo_step_unlocked"},
    echo::framework::CompareOperator::Equal,
    echo::framework::StateValue::fromBoolean(true));
const auto result = echo::framework::evaluate(hasAbility, state);
if (!result.status) {
    // Handle result.status.detail.
}
```

Logical groups require 1–32 children, NOT exactly one, and nesting is capped at 32. Missing values evaluate false. Invalid comparisons and malformed groups return a diagnostic status rather than becoming true.

Child spans borrow their storage: keep the child conditions alive through evaluation. `JsonConditionTree` decodes bounded JSON into owned condition storage; the tree is deliberately non-copyable and non-movable so its child spans and key views stay valid. Keep it alive while evaluating the pointer returned by `root()`.

The JSON schema uses `op` values `exists`, `compare`, `and`, `or`, and `not`. Leaves require a `scope` (`global`, `world`, or `entity`) and namespaced `key`. Entity scope also requires an `entity` object with unsigned `index` and non-zero `generation`. Compare leaves use `comparison` values `eq`, `ne`, `lt`, `lte`, `gt`, or `gte`, plus a typed JSON `value`; ordering comparisons require numeric values. Logical groups use a non-empty `conditions` array, while NOT uses one `condition` object.

```json
{
  "op": "and",
  "conditions": [
    { "op": "exists", "scope": "global", "key": "story.generator_repaired" },
    {
      "op": "compare", "scope": "world", "key": "world.level",
      "comparison": "gte", "value": 2
    },
    {
      "op": "not",
      "condition": {
        "op": "compare", "scope": "global", "key": "player.health",
        "comparison": "eq", "value": 0
      }
    }
  ]
}
```

Decoding is limited to 128 total nodes, 32 children per logical group, 32 nesting levels, and 63-byte state keys. Failed parsing leaves no accessible root. It copies keys and string values, so the source `JsonDocument` may be released after decoding.
