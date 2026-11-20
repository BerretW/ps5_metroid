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

Child spans borrow their storage: keep the child conditions alive through evaluation. JSON condition-tree decoding is not implemented; consumers currently construct trees through the C++ API.
