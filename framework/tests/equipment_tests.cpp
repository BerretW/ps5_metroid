#include "echo/framework/equipment.hpp"

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
    constexpr std::array<AttributeDefinition, 1> definitions{{
        {"echo:attribute/player.move_speed", 190, 100, 250},
    }};
    AttributeSet<1, 2> attributes{};
    check(static_cast<bool>(attributes.configure(definitions)),
          "equipment test configures its player attribute");

    EquipmentModule module{};
    EquipmentLoadout<1> loadout{};
    ModuleContext context{};
    check(module.info().id == "echo.rpg.equipment" &&
              module.info().required.size() == 1 &&
              module.info().required[0] == "echo.rpg.attributes" &&
              module.initialize(context),
          "equipment module declares its attributes dependency and initializes");
    check(static_cast<bool>(loadout.equip(
              module, "echo:equipment/kinetic_coil", attributes)) &&
              loadout.contains("echo:equipment/kinetic_coil") &&
              loadout.occupies("echo:slot/suit"),
          "a compiled suit item equips and claims its slot");
    std::int32_t speed{};
    check(static_cast<bool>(attributes.value("echo:attribute/player.move_speed", speed)) &&
              speed == 220,
          "equipping the kinetic coil applies its bounded movement modifier");
    check(loadout.equip(module, "echo:equipment/kinetic_coil", attributes).code ==
              ErrorCode::DuplicateId,
          "equipping an item twice is rejected");
    check(static_cast<bool>(loadout.unequip(module, "echo:slot/suit", attributes)) &&
              !loadout.contains("echo:equipment/kinetic_coil") &&
              static_cast<bool>(attributes.value("echo:attribute/player.move_speed", speed)) &&
              speed == 190,
          "unequipping removes the item's modifier");

    constexpr std::array<AttributeDefinition, 1> narrowBounds{{
        {"echo:attribute/player.move_speed", 190, 100, 210},
    }};
    AttributeSet<1, 2> boundedAttributes{};
    check(static_cast<bool>(boundedAttributes.configure(narrowBounds)) &&
              loadout.equip(module, "echo:equipment/kinetic_coil", boundedAttributes).code ==
                  ErrorCode::InvalidValue &&
              !loadout.contains("echo:equipment/kinetic_coil") &&
              static_cast<bool>(boundedAttributes.value(
                  "echo:attribute/player.move_speed", speed)) &&
              speed == 190,
          "an out-of-range equipment bonus fails without equipping or changing attributes");

    module.shutdown();
    check(!module.initialized(), "equipment module shuts down explicitly");

    if (failures != 0) {
        std::cerr << failures << " equipment test(s) failed\n";
        return 1;
    }
    std::cout << "All equipment tests passed\n";
    return 0;
}
