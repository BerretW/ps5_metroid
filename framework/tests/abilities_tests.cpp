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
    using namespace echo::framework;
    constexpr char validAbilities[] = R"({
        "schemaVersion": 1,
        "definitions": [
            {"id": "echo:ability/echo_step"},
            {"id": "echo:ability/thermal_shield"}
        ]
    })";
    JsonDocument document{};
    JsonError jsonError{};
    check(static_cast<bool>(JsonDocument::parse(
              validAbilities, sizeof(validAbilities) - 1, document, jsonError)),
          "ability definitions parse from bounded JSON");
    AbilityModule module{};
    check(static_cast<bool>(module.load(document.root())) &&
              module.definitions().size() == 2,
          "ability module loads versioned definitions from JSON");
    document.reset();
    check(module.find("echo:ability/echo_step") != nullptr,
          "loaded ability IDs remain owned after releasing the JSON document");
    echo::framework::AbilitySet<1> abilities{};
    check(module.info().id == "echo.rpg.abilities" &&
              module.info().version == 1 &&
              module.info().provides.size() == 1 &&
              module.info().provides[0] == "echo.service.abilities",
          "ability module exposes stable metadata and service");
    check(abilities.unlock(module, "echo:ability/echo_step").code ==
              echo::framework::ErrorCode::InvalidState,
          "ability set requires an initialized module");

    ModuleContext context{};
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
    check(module.load({}).code == ErrorCode::InvalidState,
          "initialized ability definitions cannot be replaced");
    module.shutdown();
    check(!module.initialized(), "module shutdown clears initialized state");

    for (const std::string_view invalid : {
             R"({"schemaVersion":2,"definitions":[{"id":"echo:ability/echo_step"}]})",
             R"({"schemaVersion":1,"definitions":[{"id":"echo:ability/echo_step"},{"id":"echo:ability/echo_step"}]})",
             R"({"schemaVersion":1,"definitions":[{"id":"echo:item/not_an_ability"}]})",
         }) {
        JsonDocument invalidDocument{};
        check(static_cast<bool>(JsonDocument::parse(
                  invalid.data(), invalid.size(), invalidDocument, jsonError)),
              "invalid ability fixture parses");
        AbilityModule invalidModule{};
        check(invalidModule.load(invalidDocument.root()).code != ErrorCode::None &&
                  !invalidModule.initialize(context),
              "unsupported, duplicate and malformed ability data is rejected");
    }

    if (failures != 0) {
        std::cerr << failures << " framework abilities test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Echo Framework abilities tests passed\n";
    return EXIT_SUCCESS;
}
