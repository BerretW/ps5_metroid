# Modules

Modules are ordinary statically linked C++ objects. The runtime registry selects among registered objects; it never loads a DLL, PRX, or other code from data files.

```cpp
constexpr std::array<std::string_view, 1> required{"echo.state"};

class MapModule final : public echo::framework::IModule {
public:
    const echo::framework::ModuleInfo& info() const noexcept override;
    bool initialize(echo::framework::ModuleContext&) noexcept override;
    void shutdown() noexcept override;
};
```

`ModuleInfo` includes a namespaced ID, display name, non-zero version, required/optional module IDs, provided service IDs, and `initializeAfter` ordering constraints. Register all compiled modules, setting `selected=false` for options not selected by the game config. Required dependencies must be both registered and selected; selected optional dependencies are ordered if present. Duplicate IDs, missing dependencies, cycles, and initialization failures return structured `Status` errors. Initialization is dependency-first; already initialized modules shut down in reverse order if a later module fails.

`ModuleContext::userData` is an explicit host-owned context pointer. Avoid global service lookup and do not retain temporary context data beyond the host lifetime.

Targets are `EchoFramework::Core` plus optional `EchoFramework::Entities`, `EchoFramework::Events`, and `EchoFramework::Data`. `ECHO_FRAMEWORK_ENABLE_DATA` controls the data target; tests and the example are selected with `ECHO_FRAMEWORK_BUILD_TESTS` and `ECHO_FRAMEWORK_BUILD_EXAMPLE`. The `EchoFramework::Core` target does not imply genre modules.

PROJECT ECHO's startup config lists `echo.state` and `echo.project_echo`; the adapter rejects unknown IDs so a data file cannot select code absent from the executable.
