#include "echo/framework/equipment.hpp"

#include <array>

namespace echo::framework {
namespace {

constexpr std::array<std::string_view, 1> kEquipmentDependencies{
    "echo.rpg.attributes"};
constexpr std::array<std::string_view, 1> kEquipmentServices{
    "echo.service.equipment"};
constexpr std::array<EquipmentAttributeModifier, 1> kKineticCoilModifiers{{
    {"echo:attribute/player.move_speed", 30},
}};
constexpr std::array<EquipmentAttributeModifier, 1> kJumpVisorModifiers{{
    {"echo:attribute/player.jump_impulse", 40},
}};
constexpr std::array<EquipmentAttributeModifier, 1> kVitalVestModifiers{{
    {"echo:attribute/player.max_health", 1},
}};
constexpr std::array<EquipmentAttributeModifier, 1> kPowerBarrelModifiers{{
    {"echo:attribute/player.projectile_damage", 1},
}};
constexpr std::array<EquipmentAttributeModifier, 1> kSprintModuleModifiers{{
    {"echo:attribute/player.move_speed", 15},
}};
constexpr std::array<EquipmentDefinition, 5> kEquipmentDefinitions{{
    {"echo:equipment/kinetic_coil", "echo:slot/suit", kKineticCoilModifiers},
    {"echo:equipment/jump_visor", "echo:slot/head", kJumpVisorModifiers},
    {"echo:equipment/vital_vest", "echo:slot/suit", kVitalVestModifiers},
    {"echo:equipment/power_barrel", "echo:slot/weapon", kPowerBarrelModifiers},
    {"echo:equipment/sprint_module", "echo:slot/accessory", kSprintModuleModifiers},
}};
constexpr ModuleInfo kEquipmentModuleInfo{
    "echo.rpg.equipment", "Equipment", 1, kEquipmentDependencies, {},
    kEquipmentServices, {}};
constexpr std::array<ManifestModuleDescriptor, 1> kManifestModules{{
    {"echo.rpg.equipment", "Equipment", 1, kEquipmentDependencies, {},
     kEquipmentServices, {}},
}};

} // namespace

const ModuleInfo& EquipmentModule::info() const noexcept {
    return kEquipmentModuleInfo;
}

bool EquipmentModule::initialize(ModuleContext&) noexcept {
    initialized_ = true;
    return true;
}

void EquipmentModule::shutdown() noexcept {
    initialized_ = false;
}

const EquipmentDefinition* EquipmentModule::find(std::string_view id) const noexcept {
    for (const EquipmentDefinition& definition : kEquipmentDefinitions) {
        if (definition.id == id) return &definition;
    }
    return nullptr;
}

std::span<const EquipmentDefinition> EquipmentModule::definitions() const noexcept {
    return kEquipmentDefinitions;
}

CapabilityManifestFragment equipmentCapabilityManifestFragment() noexcept {
    return {kManifestModules, {}, {}, {}, {}, {}};
}

} // namespace echo::framework
