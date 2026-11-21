#include "echo/framework/attributes.hpp"

#include <algorithm>

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
    initialized_ = true;
    return true;
}

void AttributesModule::shutdown() noexcept {
    initialized_ = false;
}

CapabilityManifestFragment attributesCapabilityManifestFragment() noexcept {
    return {kManifestModules, {}, {}, {}, kManifestSchemas, {}};
}

} // namespace echo::framework
