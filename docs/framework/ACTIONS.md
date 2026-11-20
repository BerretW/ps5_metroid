# Actions

Actions are explicit registered handlers, not scripts. JSON can name a known action, but cannot create or evaluate code. Requests carry a namespaced action ID and a bounded span of typed `{name, value}` parameters.

```cpp
actions.addCapability("echo.state.write");
actions.registerAction("echo.state.set_flag", "echo.state.write",
                       &echo::framework::ActionRegistry::setStateFlag, &state);
actions.setGameActive(true);

auto key = echo::framework::StateValue{};
echo::framework::StateValue::fromString("story.generator_repaired", key);
const std::array args{
    echo::framework::ActionParameter{"key", key},
    echo::framework::ActionParameter{"value",
        echo::framework::StateValue::fromBoolean(true)}
};
auto result = actions.execute({"echo.state.set_flag", args});
```

Handlers return `ActionResult` with a structured status and optional typed value. The registry reports unknown actions, disabled capabilities, malformed parameters, inactive game state, and handler failure distinctly. Handlers own domain validation; no `give_item` action is registered because an inventory module does not exist.
