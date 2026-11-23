#include "echo/framework/equipment.hpp"

#include <array>
#include <limits>

namespace echo::framework {
namespace {

constexpr std::size_t kMaxModifiersPerDefinition = 8;
constexpr std::array<std::string_view, 1> kEquipmentDependencies{
    "echo.rpg.attributes"};
constexpr std::array<std::string_view, 1> kEquipmentServices{
    "echo.service.equipment"};
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
    if (!loaded_) return false;
    initialized_ = true;
    return true;
}

void EquipmentModule::shutdown() noexcept {
    initialized_ = false;
}

Status EquipmentModule::load(JsonValue document) noexcept {
    if (initialized_) {
        return {ErrorCode::InvalidState, "echo.rpg.equipment",
                "Equipment content cannot change after module initialization."};
    }
    std::int64_t schemaVersion{};
    const JsonValue jsonDefinitions = document.find("definitions");
    if (!document.isObject() ||
        !document.find("schemaVersion").integer(schemaVersion) || schemaVersion != 1 ||
        !jsonDefinitions.isArray() || jsonDefinitions.size() > kMaxDefinitions) {
        return {ErrorCode::InvalidValue, "equipment",
                "Equipment content requires schema version 1 and a bounded definitions array."};
    }

    std::array<StoredDefinition, kMaxDefinitions> storedDefinitions{};
    std::array<StoredModifier, kMaxModifiers> storedModifiers{};
    std::size_t modifierCount = 0;
    for (std::size_t index = 0; index < jsonDefinitions.size(); ++index) {
        const JsonValue entry = jsonDefinitions.at(index);
        std::string_view id;
        std::string_view slot;
        const JsonValue jsonModifiers = entry.find("modifiers");
        if (!entry.isObject() || !entry.find("id").string(id) ||
            !entry.find("slot").string(slot) || !jsonModifiers.isArray() ||
            jsonModifiers.size() > kMaxModifiersPerDefinition ||
            jsonModifiers.size() > kMaxModifiers - modifierCount ||
            !isValidContentId(id) || id.size() >= storedDefinitions[index].id.size() ||
            !isValidContentId(slot) || slot.size() >= storedDefinitions[index].slot.size()) {
            return {ErrorCode::InvalidValue, "equipment.definitions",
                    "Equipment IDs, slots or modifier counts are invalid."};
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (std::string_view(storedDefinitions[previous].id.data()) == id) {
                return {ErrorCode::DuplicateId, id,
                        "Equipment definition IDs must be unique."};
            }
        }
        StoredDefinition& definition = storedDefinitions[index];
        std::copy(id.begin(), id.end(), definition.id.begin());
        std::copy(slot.begin(), slot.end(), definition.slot.begin());
        definition.modifierOffset = modifierCount;
        definition.modifierCount = jsonModifiers.size();
        for (std::size_t modifierIndex = 0;
             modifierIndex < jsonModifiers.size(); ++modifierIndex) {
            const JsonValue jsonModifier = jsonModifiers.at(modifierIndex);
            std::string_view attributeId;
            std::int64_t amount{};
            if (!jsonModifier.isObject() ||
                !jsonModifier.find("attribute").string(attributeId) ||
                !jsonModifier.find("amount").integer(amount) || amount == 0 ||
                amount < std::numeric_limits<std::int32_t>::min() ||
                amount > std::numeric_limits<std::int32_t>::max() ||
                !isValidContentId(attributeId) ||
                attributeId.size() >= storedModifiers[modifierCount].attributeId.size()) {
                return {ErrorCode::InvalidValue, id,
                        "Equipment modifiers require a valid attribute ID and nonzero amount."};
            }
            for (std::size_t previous = 0; previous < modifierIndex; ++previous) {
                if (std::string_view(
                        storedModifiers[definition.modifierOffset + previous]
                            .attributeId.data()) == attributeId) {
                    return {ErrorCode::DuplicateId, attributeId,
                            "An equipment definition may modify an attribute only once."};
                }
            }
            StoredModifier& modifier = storedModifiers[modifierCount++];
            std::copy(attributeId.begin(), attributeId.end(),
                      modifier.attributeId.begin());
            modifier.amount = static_cast<std::int32_t>(amount);
        }
    }

    storedDefinitions_ = storedDefinitions;
    storedModifiers_ = storedModifiers;
    definitionCount_ = jsonDefinitions.size();
    modifierCount_ = modifierCount;
    for (std::size_t index = 0; index < modifierCount_; ++index) {
        modifierViews_[index] = {
            storedModifiers_[index].attributeId.data(),
            storedModifiers_[index].amount};
    }
    for (std::size_t index = 0; index < definitionCount_; ++index) {
        const StoredDefinition& definition = storedDefinitions_[index];
        definitions_[index] = {
            definition.id.data(), definition.slot.data(),
            {modifierViews_.data() + definition.modifierOffset,
             definition.modifierCount}};
    }
    loaded_ = true;
    return Status::success();
}

const EquipmentDefinition* EquipmentModule::find(std::string_view id) const noexcept {
    for (std::size_t index = 0; index < definitionCount_; ++index) {
        const EquipmentDefinition& definition = definitions_[index];
        if (definition.id == id) return &definition;
    }
    return nullptr;
}

std::span<const EquipmentDefinition> EquipmentModule::definitions() const noexcept {
    return {definitions_.data(), definitionCount_};
}

CapabilityManifestFragment equipmentCapabilityManifestFragment() noexcept {
    return {kManifestModules, {}, {}, {}, {}, {}};
}

} // namespace echo::framework
