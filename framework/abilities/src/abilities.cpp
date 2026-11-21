#include "echo/framework/abilities.hpp"

#include <algorithm>
#include <array>

namespace echo::framework {
namespace {

constexpr std::array<std::string_view, 1> kAbilityServices{
    "echo.service.abilities"};
constexpr ModuleInfo kAbilityModuleInfo{
    "echo.rpg.abilities", "Abilities", 1, {}, {}, kAbilityServices, {}};
constexpr std::array<ManifestModuleDescriptor, 1> kManifestModules{{
    {"echo.rpg.abilities", "Abilities", 1, {}, {}, kAbilityServices, {}},
}};

[[nodiscard]] bool validAbilityId(std::string_view id) noexcept {
    const std::size_t separator = id.find(':');
    if (separator == 0 || separator == std::string_view::npos ||
        id.find(':', separator + 1) != std::string_view::npos ||
        id.size() <= separator + 9 || id.size() >= kAbilityIdStorage) {
        return false;
    }
    constexpr char kAbilityType[] = "ability/";
    for (std::size_t index = 0; index < sizeof(kAbilityType) - 1; ++index) {
        if (id[separator + 1 + index] != kAbilityType[index]) return false;
    }
    const auto validCharacter = [](char character) noexcept {
        return (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') ||
               character == '_' || character == '-';
    };
    for (std::size_t index = 0; index < separator; ++index) {
        if (!validCharacter(id[index])) return false;
    }
    for (std::size_t index = separator + 9; index < id.size(); ++index) {
        if (!validCharacter(id[index])) return false;
    }
    return true;
}

} // namespace

const ModuleInfo& AbilityModule::info() const noexcept {
    return kAbilityModuleInfo;
}

bool AbilityModule::initialize(ModuleContext&) noexcept {
    initialized_ = false;
    if (definitions_.empty() || definitions_.size() > kMaxAbilityDefinitions) {
        return false;
    }
    for (std::size_t index = 0; index < definitions_.size(); ++index) {
        if (!validAbilityId(definitions_[index].id)) return false;
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (definitions_[previous].id == definitions_[index].id) return false;
        }
    }
    initialized_ = true;
    return true;
}

void AbilityModule::shutdown() noexcept {
    initialized_ = false;
}

const AbilityDefinition* AbilityModule::find(std::string_view id) const noexcept {
    for (const AbilityDefinition& definition : definitions_) {
        if (definition.id == id) return &definition;
    }
    return nullptr;
}

CapabilityManifestFragment abilitiesCapabilityManifestFragment() noexcept {
    return {kManifestModules, {}, {}, {}, {}, {}};
}

} // namespace echo::framework
