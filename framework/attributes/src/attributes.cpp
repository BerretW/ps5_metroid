#include "echo/framework/attributes.hpp"

#include <algorithm>
#include <limits>

namespace echo::framework {
namespace {

constexpr std::array<std::string_view, 1> kAttributeServices{
    "echo.service.attributes"};
constexpr ModuleInfo kAttributesModuleInfo{
    "echo.rpg.attributes", "Attributes", 1, {}, {}, kAttributeServices, {}};
constexpr std::array<ManifestModuleDescriptor, 1> kManifestModules{{
    {"echo.rpg.attributes", "Attributes", 1, {}, {}, kAttributeServices, {}},
}};
constexpr std::array<ManifestSchemaDescriptor, 1> kManifestSchemas{{
    {"echo.rpg.attribute_set", 1},
}};

} // namespace

const ModuleInfo& AttributesModule::info() const noexcept {
    return kAttributesModuleInfo;
}

bool AttributesModule::initialize(ModuleContext&) noexcept {
    if (!loaded_) return false;
    initialized_ = true;
    return true;
}

void AttributesModule::shutdown() noexcept {
    initialized_ = false;
}

Status AttributesModule::load(JsonValue document) noexcept {
    if (initialized_) {
        return {ErrorCode::InvalidState, "echo.rpg.attributes",
                "Attribute content cannot change after module initialization."};
    }
    std::int64_t schemaVersion{};
    const JsonValue jsonDefinitions = document.find("definitions");
    if (!document.isObject() ||
        !document.find("schemaVersion").integer(schemaVersion) || schemaVersion != 1 ||
        !jsonDefinitions.isArray() || jsonDefinitions.size() == 0 ||
        jsonDefinitions.size() > kMaxDefinitions) {
        return {ErrorCode::InvalidValue, "attributes",
                "Attribute content requires schema version 1 and a bounded non-empty definitions array."};
    }

    std::array<StoredDefinition, kMaxDefinitions> storedDefinitions{};
    std::array<AttributeDefinition, kMaxDefinitions> definitions{};
    for (std::size_t index = 0; index < jsonDefinitions.size(); ++index) {
        const JsonValue entry = jsonDefinitions.at(index);
        std::string_view id;
        std::int64_t baseValue{};
        std::int64_t minimum{};
        std::int64_t maximum{};
        if (!entry.isObject() || !entry.find("id").string(id) ||
            !entry.find("baseValue").integer(baseValue) ||
            !entry.find("minimum").integer(minimum) ||
            !entry.find("maximum").integer(maximum) ||
            baseValue < std::numeric_limits<std::int32_t>::min() ||
            baseValue > std::numeric_limits<std::int32_t>::max() ||
            minimum < std::numeric_limits<std::int32_t>::min() ||
            minimum > std::numeric_limits<std::int32_t>::max() ||
            maximum < std::numeric_limits<std::int32_t>::min() ||
            maximum > std::numeric_limits<std::int32_t>::max() ||
            !isValidContentId(id) || id.size() >= storedDefinitions[index].id.size()) {
            return {ErrorCode::InvalidValue, "attributes.definitions",
                    "Attribute IDs and bounded values are invalid."};
        }
        std::copy(id.begin(), id.end(), storedDefinitions[index].id.begin());
        storedDefinitions[index].baseValue = static_cast<std::int32_t>(baseValue);
        storedDefinitions[index].minimum = static_cast<std::int32_t>(minimum);
        storedDefinitions[index].maximum = static_cast<std::int32_t>(maximum);
        definitions[index] = {
            id, storedDefinitions[index].baseValue,
            storedDefinitions[index].minimum, storedDefinitions[index].maximum};
    }

    AttributeSet<kMaxDefinitions, 1> validator{};
    const Status validation =
        validator.configure({definitions.data(), jsonDefinitions.size()});
    if (!validation) return validation;

    storedDefinitions_ = storedDefinitions;
    definitionCount_ = jsonDefinitions.size();
    for (std::size_t index = 0; index < definitionCount_; ++index) {
        const StoredDefinition& definition = storedDefinitions_[index];
        definitions_[index] = {
            definition.id.data(), definition.baseValue,
            definition.minimum, definition.maximum};
    }
    loaded_ = true;
    return Status::success();
}

CapabilityManifestFragment attributesCapabilityManifestFragment() noexcept {
    return {kManifestModules, {}, {}, {}, kManifestSchemas, {}};
}

} // namespace echo::framework
