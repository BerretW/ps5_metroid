#include "echo/framework/attributes.hpp"

#include <array>
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
    constexpr std::array<AttributeDefinition, 2> definitions{{
        {"echo:attribute/player.max_health", 5, 1, 8},
        {"echo:attribute/player.move_speed", 190, 100, 250},
    }};
    AttributeSet<2, 2> attributes{};
    check(attributes.configure(definitions) && attributes.size() == 2,
          "bounded attribute set copies compiled definitions");
    std::int32_t value{};
    check(attributes.value("echo:attribute/player.max_health", value) && value == 5,
          "attribute value begins at its base");
    check(attributes.addModifier("echo:attribute/player.max_health",
                                 "echo:equipment/vitality", 2) &&
              attributes.value("echo:attribute/player.max_health", value) && value == 7,
          "a stable source adds an in-range modifier");
    check(attributes.addModifier("echo:attribute/player.max_health",
                                 "echo:equipment/vitality", 1).code ==
              ErrorCode::DuplicateId,
          "a modifier source cannot be applied twice");
    check(attributes.addModifier("echo:attribute/player.max_health",
                                 "echo:equipment/overload", 2).code ==
              ErrorCode::InvalidValue &&
              attributes.value("echo:attribute/player.max_health", value) && value == 7,
          "out-of-range modifiers fail without changing the attribute");
    check(attributes.removeModifier("echo:attribute/player.max_health",
                                    "echo:equipment/vitality") &&
              attributes.value("echo:attribute/player.max_health", value) && value == 5,
          "removing a modifier restores the previous value");
    check(attributes.value("echo:attribute/unknown", value).code == ErrorCode::NotFound,
          "unknown attributes fail explicitly");

    constexpr std::array<AttributeDefinition, 1> invalid{{
        {"echo:attribute/bad", 4, 5, 9},
    }};
    check(attributes.configure(invalid).code == ErrorCode::InvalidValue &&
              attributes.value("echo:attribute/player.max_health", value) && value == 5,
          "failed reconfiguration preserves the current attribute set");

    constexpr char attributesJson[] = R"({
        "schemaVersion": 1,
        "definitions": [
            {"id": "echo:attribute/player.max_health", "baseValue": 5, "minimum": 1, "maximum": 8},
            {"id": "echo:attribute/player.move_speed", "baseValue": 190, "minimum": 100, "maximum": 250}
        ]
    })";
    JsonDocument document{};
    JsonError jsonError{};
    check(static_cast<bool>(JsonDocument::parse(
              attributesJson, sizeof(attributesJson) - 1, document, jsonError)),
          "attribute definitions parse from bounded JSON");
    AttributesModule module{};
    check(static_cast<bool>(module.load(document.root())) &&
              module.definitions().size() == 2 &&
              module.definitions()[0].baseValue == 5,
          "attributes module loads bounded definitions from JSON");
    document.reset();
    check(module.definitions()[1].id == "echo:attribute/player.move_speed",
          "loaded attribute IDs remain owned after releasing the JSON document");
    ModuleContext context{};
    check(module.info().id == "echo.rpg.attributes" && module.initialize(context) &&
              module.initialized(),
          "attributes module exposes stable metadata and initializes");
    check(module.load({}).code == ErrorCode::InvalidState,
          "initialized attribute definitions cannot be replaced");
    module.shutdown();
    check(!module.initialized(), "attributes module shuts down explicitly");

    constexpr char invalidAttributesJson[] = R"({
        "schemaVersion": 1,
        "definitions": [
            {"id": "echo:attribute/player.max_health", "baseValue": 9, "minimum": 1, "maximum": 8}
        ]
    })";
    JsonDocument invalidDocument{};
    check(static_cast<bool>(JsonDocument::parse(
              invalidAttributesJson, sizeof(invalidAttributesJson) - 1,
              invalidDocument, jsonError)),
          "invalid attribute fixture parses");
    AttributesModule invalidModule{};
    check(invalidModule.load(invalidDocument.root()).code == ErrorCode::InvalidValue &&
              !invalidModule.initialize(context),
          "out-of-range attribute defaults are rejected before module initialization");

    if (failures != 0) {
        std::cerr << failures << " attribute test(s) failed\n";
        return 1;
    }
    std::cout << "All attribute tests passed\n";
    return 0;
}
