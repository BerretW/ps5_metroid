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
    if (!loaded_ || definitionCount_ == 0 ||
        definitionCount_ > kMaxAbilityDefinitions) {
        return false;
    }
    for (std::size_t index = 0; index < definitionCount_; ++index) {
        if (!validAbilityId(definitions_[index].id)) return false;
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (definitions_[previous].id == definitions_[index].id) return false;
        }
    }
    initialized_ = true;
    return true;
}

Status AbilityModule::load(JsonValue document) noexcept {
    if (initialized_) {
        return {ErrorCode::InvalidState, "echo.rpg.abilities",
                "Ability content cannot change after module initialization."};
    }
    std::int64_t schemaVersion{};
    const JsonValue jsonDefinitions = document.find("definitions");
    if (!document.isObject() ||
        !document.find("schemaVersion").integer(schemaVersion) || schemaVersion != 1 ||
        !jsonDefinitions.isArray() || jsonDefinitions.size() == 0 ||
        jsonDefinitions.size() > kMaxAbilityDefinitions) {
        return {ErrorCode::InvalidValue, "abilities",
                "Ability content requires schema version 1 and a bounded non-empty definitions array."};
    }

    std::array<std::array<char, kAbilityIdStorage>, kMaxAbilityDefinitions> ids{};
    for (std::size_t index = 0; index < jsonDefinitions.size(); ++index) {
        std::string_view id;
        if (!jsonDefinitions.at(index).find("id").string(id) ||
            !validAbilityId(id)) {
            return {ErrorCode::InvalidValue, "abilities.definitions",
                    "Ability definitions require valid stable ability IDs."};
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (std::string_view(ids[previous].data()) == id) {
                return {ErrorCode::DuplicateId, id,
                        "Ability definition IDs must be unique."};
            }
        }
        std::copy(id.begin(), id.end(), ids[index].begin());
    }

    ids_ = ids;
    definitionCount_ = jsonDefinitions.size();
    for (std::size_t index = 0; index < definitionCount_; ++index) {
        definitions_[index] = {ids_[index].data()};
    }
    loaded_ = true;
    return Status::success();
}

void AbilityModule::shutdown() noexcept {
    initialized_ = false;
}

const AbilityDefinition* AbilityModule::find(std::string_view id) const noexcept {
    for (std::size_t index = 0; index < definitionCount_; ++index) {
        const AbilityDefinition& definition = definitions_[index];
        if (definition.id == id) return &definition;
    }
    return nullptr;
}

CapabilityManifestFragment abilitiesCapabilityManifestFragment() noexcept {
    return {kManifestModules, {}, {}, {}, {}, {}};
}

} // namespace echo::framework
