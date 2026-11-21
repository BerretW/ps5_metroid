#include "echo/framework/abilities.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

int failures{};

void check(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    constexpr std::array<echo::framework::AbilityDefinition, 2> definitions{{
        {"echo:ability/echo_step"},
        {"echo:ability/thermal_shield"},
    }};
    echo::framework::AbilityModule module{definitions};
    echo::framework::AbilitySet<1> abilities{};
    check(module.info().id == "echo.rpg.abilities" &&
              module.info().version == 1 &&
              module.info().provides.size() == 1 &&
              module.info().provides[0] == "echo.service.abilities",
          "ability module exposes stable metadata and service");
    check(abilities.unlock(module, "echo:ability/echo_step").code ==
              echo::framework::ErrorCode::InvalidState,
          "ability set requires an initialized module");

    echo::framework::ModuleContext context{};
    check(module.initialize(context), "valid ability definitions initialize");
    check(abilities.unlock(module, "echo:ability/unknown").code ==
              echo::framework::ErrorCode::NotFound,
          "unknown abilities are rejected");
    check(abilities.unlock(module, "echo:ability/echo_step") &&
              abilities.contains("echo:ability/echo_step"),
          "registered ability unlocks into the fixed-capacity set");
    check(abilities.unlock(module, "echo:ability/echo_step").code ==
              echo::framework::ErrorCode::DuplicateId,
          "duplicate unlocks are rejected");
    check(abilities.unlock(module, "echo:ability/thermal_shield").code ==
              echo::framework::ErrorCode::CapacityExceeded,
          "ability set rejects capacity overflow");
    abilities.reset();
    check(!abilities.contains("echo:ability/echo_step"),
          "reset clears the unlocked ability set");
    module.shutdown();
    check(!module.initialized(), "module shutdown clears initialized state");

    constexpr std::array<echo::framework::AbilityDefinition, 2> duplicates{{
        {"echo:ability/echo_step"},
        {"echo:ability/echo_step"},
    }};
    echo::framework::AbilityModule duplicateModule{duplicates};
    check(!duplicateModule.initialize(context),
          "duplicate compiled ability definitions fail initialization");

    constexpr std::array<echo::framework::AbilityDefinition, 1> malformed{{
        {"echo:item/not_an_ability"},
    }};
    echo::framework::AbilityModule malformedModule{malformed};
    check(!malformedModule.initialize(context),
          "malformed stable ability IDs fail initialization");

    if (failures != 0) {
        std::cerr << failures << " framework abilities test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Echo Framework abilities tests passed\n";
    return EXIT_SUCCESS;
}
