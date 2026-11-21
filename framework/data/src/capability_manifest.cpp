#include "echo/framework/capability_manifest.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace echo::framework {
namespace {

constexpr std::size_t kMaxManifestModules = 64;
constexpr std::size_t kMaxManifestCapabilities = 128;
constexpr std::size_t kMaxManifestActions = 128;
constexpr std::size_t kMaxManifestEvents = 128;
constexpr std::size_t kMaxManifestSchemas = 64;
constexpr std::size_t kMaxManifestFeatures = 64;
constexpr std::size_t kComponentManifestBufferSize = 64 * 1024;
constexpr std::array<std::string_view, 5> kConditionOperators{
    "and", "compare", "exists", "not", "or"};
constexpr std::array<std::string_view, 7> kResourceTypes{
    "ability", "cutscene", "dialogue", "generic", "item", "sound", "sprite"};
constexpr std::array<std::string_view, 3> kCompatibilityConstraints{
    "Unknown fields are rejected.", "Content is data-only; native code is never loaded.",
    "Only statically registered factories and action handlers execute."};
constexpr std::array<ManifestSchemaDescriptor, 2> kFrameworkSchemas{{
    {"echo.content.resource_manifest", 1},
    {"echo.data.component_config", 1},
}};
constexpr std::array<std::string_view, 2> kFrameworkFeatureConstraints{
    "Fixed-capacity registries reject overflow.", "Content references use stable IDs."};
constexpr std::array<ManifestFeatureDescriptor, 1> kFrameworkFeatures{{
    {"echo.runtime.bounded_content", "Content and component schemas have explicit size limits.",
     kFrameworkFeatureConstraints},
}};

class Writer {
public:
    Writer(char* output, std::size_t capacity) noexcept
        : output_(output), capacity_(capacity) {}

    [[nodiscard]] bool append(std::string_view value) noexcept {
        if (failed_ || value.size() > capacity_ - position_) {
            failed_ = true;
            return false;
        }
        std::memcpy(output_ + position_, value.data(), value.size());
        position_ += value.size();
        return true;
    }

    [[nodiscard]] bool character(char value) noexcept {
        return append({&value, 1});
    }

    [[nodiscard]] bool quoted(std::string_view value) noexcept {
        if (!validUtf8(value) || !character('"')) return false;
        for (const unsigned char character : value) {
            switch (character) {
            case '"': if (!append("\\\"")) return false; break;
            case '\\': if (!append("\\\\")) return false; break;
            case '\n': if (!append("\\n")) return false; break;
            case '\r': if (!append("\\r")) return false; break;
            case '\t': if (!append("\\t")) return false; break;
            default:
                if (character < 0x20U || !this->character(static_cast<char>(character))) {
                    return false;
                }
                break;
            }
        }
        return character('"');
    }

    [[nodiscard]] bool unsignedInteger(std::uint32_t value) noexcept {
        char digits[10]{};
        std::size_t count{};
        do {
            digits[count++] = static_cast<char>('0' + value % 10U);
            value /= 10U;
        } while (value != 0U);
        while (count != 0) {
            if (!character(digits[--count])) return false;
        }
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return position_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    char* output_{};
    std::size_t capacity_{};
    std::size_t position_{};
    bool failed_{};
};

template <typename Descriptor, std::size_t Capacity>
[[nodiscard]] Status collectDescriptors(
    std::span<const CapabilityManifestFragment> fragments,
    std::array<const Descriptor*, Capacity>& output,
    std::size_t& count, std::span<const Descriptor> CapabilityManifestFragment::*member,
    std::string_view subject) noexcept {
    count = 0;
    for (const CapabilityManifestFragment& fragment : fragments) {
        for (const Descriptor& descriptor : fragment.*member) {
            if (count == output.size()) {
                return {ErrorCode::CapacityExceeded, subject,
                        "Capability manifest descriptor limit was exceeded."};
            }
            output[count++] = &descriptor;
        }
    }
    std::sort(output.begin(), output.begin() + static_cast<std::ptrdiff_t>(count),
              [](const Descriptor* left, const Descriptor* right) {
                  return left->id < right->id;
              });
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0 && output[index - 1]->id == output[index]->id) {
            return {ErrorCode::DuplicateId, output[index]->id,
                    "Capability manifest contains a duplicate stable ID."};
        }
    }
    return Status::success();
}

[[nodiscard]] bool validMetadataStrings(
    std::span<const std::string_view> values) noexcept {
    if (values.size() > 32) return false;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (values[index].empty() || !validNamespaceKey(values[index])) return false;
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (values[previous] == values[index]) return false;
        }
    }
    return true;
}

[[nodiscard]] bool validParameters(
    std::span<const ManifestActionParameter> parameters) noexcept {
    if (parameters.size() > 32) return false;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        const auto validName = [](std::string_view name) {
            if (name.empty() || name.size() > 63) return false;
            for (const char character : name) {
                if (!((character >= 'a' && character <= 'z') ||
                      (character >= 'A' && character <= 'Z') ||
                      (character >= '0' && character <= '9') || character == '_')) {
                    return false;
                }
            }
            return true;
        };
        const ManifestActionParameter& parameter = parameters[index];
        const bool validType = parameter.type == "boolean" || parameter.type == "integer" ||
                               parameter.type == "number" || parameter.type == "string";
        if (!validName(parameter.name) || !validType) return false;
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (parameters[previous].name == parameter.name) return false;
        }
    }
    return true;
}

[[nodiscard]] bool writeStringArray(Writer& writer,
                                    std::span<const std::string_view> values,
                                    bool sortValues = true) noexcept {
    if (!writer.character('[')) return false;
    std::array<std::string_view, 32> ordered{};
    if (values.size() > ordered.size()) return false;
    std::copy(values.begin(), values.end(), ordered.begin());
    if (sortValues) {
        std::sort(ordered.begin(),
                  ordered.begin() + static_cast<std::ptrdiff_t>(values.size()));
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        if ((index != 0 && !writer.character(',')) || !writer.quoted(ordered[index])) {
            return false;
        }
    }
    return writer.character(']');
}

[[nodiscard]] bool writeParameters(
    Writer& writer, std::span<const ManifestActionParameter> parameters) noexcept {
    if (!writer.character('[')) return false;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if ((index != 0 && !writer.character(',')) ||
            !writer.append("{\"name\":") || !writer.quoted(parameters[index].name) ||
            !writer.append(",\"type\":") || !writer.quoted(parameters[index].type) ||
            !writer.append(",\"required\":") ||
            !writer.append(parameters[index].required ? "true}" : "false}")) {
            return false;
        }
    }
    return writer.character(']');
}

[[nodiscard]] Status validateFragments(
    std::span<const ManifestModuleDescriptor* const> modules,
    std::span<const ManifestCapabilityDescriptor* const> capabilities,
    std::span<const ManifestActionDescriptor* const> actions,
    std::span<const ManifestEventDescriptor* const> events,
    std::span<const ManifestSchemaDescriptor* const> schemas,
    std::span<const ManifestFeatureDescriptor* const> features) noexcept {
    for (const ManifestModuleDescriptor* module : modules) {
        if (!validNamespaceKey(module->id) || module->name.empty() ||
            !validUtf8(module->name) || module->version == 0 ||
            !validMetadataStrings(module->required) ||
            !validMetadataStrings(module->optional) ||
            !validMetadataStrings(module->provides) ||
            !validMetadataStrings(module->initializeAfter)) {
            return {ErrorCode::InvalidValue, module->id,
                    "Capability manifest module metadata is invalid."};
        }
    }
    for (const ManifestCapabilityDescriptor* capability : capabilities) {
        if (!validNamespaceKey(capability->id)) {
            return {ErrorCode::InvalidValue, capability->id,
                    "Capability manifest capability metadata is invalid."};
        }
    }
    for (const ManifestActionDescriptor* action : actions) {
        if (!validNamespaceKey(action->id) ||
            (!action->capability.empty() && !validNamespaceKey(action->capability)) ||
            !validParameters(action->parameters)) {
            return {ErrorCode::InvalidValue, action->id,
                    "Capability manifest action metadata is invalid."};
        }
        if (!action->capability.empty()) {
            const bool declared = std::any_of(
                capabilities.begin(), capabilities.end(),
                [action](const ManifestCapabilityDescriptor* capability) {
                    return capability->id == action->capability;
                });
            if (!declared) {
                return {ErrorCode::NotFound, action->capability,
                        "Capability manifest action references an undeclared capability."};
            }
        }
    }
    for (const ManifestEventDescriptor* event : events) {
        if (!validNamespaceKey(event->id) || event->version == 0 ||
            !validParameters(event->payload)) {
            return {ErrorCode::InvalidValue, event->id,
                    "Capability manifest event metadata is invalid."};
        }
    }
    for (const ManifestSchemaDescriptor* schema : schemas) {
        if (!validNamespaceKey(schema->id) || schema->version == 0) {
            return {ErrorCode::InvalidValue, schema->id,
                    "Capability manifest content schema metadata is invalid."};
        }
    }
    for (const ManifestFeatureDescriptor* feature : features) {
        bool validConstraints = feature->constraints.size() <= 32;
        for (std::size_t index = 0; validConstraints &&
             index < feature->constraints.size(); ++index) {
            validConstraints = !feature->constraints[index].empty() &&
                               validUtf8(feature->constraints[index]);
            for (std::size_t previous = 0; validConstraints && previous < index; ++previous) {
                validConstraints = feature->constraints[previous] != feature->constraints[index];
            }
        }
        if (!validNamespaceKey(feature->id) || feature->description.empty() ||
            !validUtf8(feature->description) || !validConstraints) {
            return {ErrorCode::InvalidValue, feature->id,
                    "Capability manifest runtime feature metadata is invalid."};
        }
    }
    return Status::success();
}

[[nodiscard]] bool extractComponents(std::string_view json,
                                     std::string_view& components) noexcept {
    constexpr std::string_view marker = "\"components\":[";
    const std::size_t markerOffset = json.find(marker);
    if (markerOffset == std::string_view::npos) return false;
    const std::size_t start = markerOffset + marker.size() - 1;
    std::size_t depth{};
    bool inString{};
    bool escaped{};
    for (std::size_t offset = start; offset < json.size(); ++offset) {
        const char character = json[offset];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inString = false;
            }
            continue;
        }
        if (character == '"') {
            inString = true;
        } else if (character == '[') {
            ++depth;
        } else if (character == ']' && --depth == 0) {
            components = json.substr(start, offset - start + 1);
            return true;
        }
    }
    return false;
}

} // namespace

CapabilityManifestFragment frameworkCapabilityManifestFragment() noexcept {
    return {.contentSchemas = kFrameworkSchemas,
            .runtimeFeatures = kFrameworkFeatures};
}

Status exportCapabilityManifest(
    const ComponentTypeRegistry& components,
    std::span<const CapabilityManifestFragment> fragments,
    char* output, std::size_t capacity, std::size_t& written) noexcept {
    written = 0;
    if (output == nullptr || capacity == 0) {
        return {ErrorCode::InvalidArgument, {}, "Manifest output buffer is invalid."};
    }
    output[0] = '\0';

    std::array<const ManifestModuleDescriptor*, kMaxManifestModules> modules{};
    std::array<const ManifestCapabilityDescriptor*, kMaxManifestCapabilities> capabilities{};
    std::array<const ManifestActionDescriptor*, kMaxManifestActions> actions{};
    std::array<const ManifestEventDescriptor*, kMaxManifestEvents> events{};
    std::array<const ManifestSchemaDescriptor*, kMaxManifestSchemas> schemas{};
    std::array<const ManifestFeatureDescriptor*, kMaxManifestFeatures> features{};
    std::size_t moduleCount{}, capabilityCount{}, actionCount{}, eventCount{}, schemaCount{},
        featureCount{};
    Status status = collectDescriptors(fragments, modules, moduleCount,
                                       &CapabilityManifestFragment::modules, "modules");
    if (!status) return status;
    status = collectDescriptors(fragments, capabilities, capabilityCount,
                                &CapabilityManifestFragment::capabilities, "capabilities");
    if (!status) return status;
    status = collectDescriptors(fragments, actions, actionCount,
                                &CapabilityManifestFragment::actions, "actions");
    if (!status) return status;
    status = collectDescriptors(fragments, events, eventCount,
                                &CapabilityManifestFragment::events, "events");
    if (!status) return status;
    status = collectDescriptors(fragments, schemas, schemaCount,
                                &CapabilityManifestFragment::contentSchemas, "contentSchemas");
    if (!status) return status;
    status = collectDescriptors(fragments, features, featureCount,
                                &CapabilityManifestFragment::runtimeFeatures, "runtimeFeatures");
    if (!status) return status;
    status = validateFragments({modules.data(), moduleCount},
                               {capabilities.data(), capabilityCount},
                               {actions.data(), actionCount}, {events.data(), eventCount},
                               {schemas.data(), schemaCount}, {features.data(), featureCount});
    if (!status) return status;

    std::array<char, kComponentManifestBufferSize> componentBuffer{};
    std::size_t componentBytes{};
    status = exportComponentManifest(components, componentBuffer.data(),
                                     componentBuffer.size(), componentBytes);
    if (!status) return status;
    std::string_view componentArray{};
    if (!extractComponents({componentBuffer.data(), componentBytes}, componentArray)) {
        return {ErrorCode::InvalidState, "components",
                "The component schema exporter produced an invalid manifest."};
    }

    Writer writer{output, capacity};
    bool ok = writer.append("{\"manifestVersion\":") &&
              writer.unsignedInteger(kCapabilityManifestVersion) &&
              writer.append(",\"framework\":{\"name\":\"Echo Framework\",\"version\":") &&
              writer.quoted(kFrameworkVersion) && writer.append(",\"apiVersion\":") &&
              writer.unsignedInteger(kFrameworkApiVersion) &&
              writer.append(",\"minimumApiVersion\":") &&
              writer.unsignedInteger(kFrameworkApiVersion) &&
              writer.append("},\"compatibility\":{\"supportedManifestVersions\":[") &&
              writer.unsignedInteger(kCapabilityManifestVersion) &&
              writer.append("],\"apiVersion\":") &&
              writer.unsignedInteger(kFrameworkApiVersion) &&
              writer.append(",\"constraints\":[");
    for (std::size_t index = 0; ok && index < kCompatibilityConstraints.size(); ++index) {
        if (index != 0) ok = writer.character(',');
        ok = ok && writer.quoted(kCompatibilityConstraints[index]);
    }
    ok = ok && writer.append("]},\"modules\":[");
    for (std::size_t index = 0; ok && index < moduleCount; ++index) {
        const auto& module = *modules[index];
        if (index != 0) ok = writer.character(',');
        ok = ok && writer.append("{\"id\":") && writer.quoted(module.id) &&
             writer.append(",\"name\":") && writer.quoted(module.name) &&
             writer.append(",\"version\":") && writer.unsignedInteger(module.version) &&
             writer.append(",\"selectable\":true,\"required\":") &&
             writeStringArray(writer, module.required) &&
             writer.append(",\"optional\":") && writeStringArray(writer, module.optional) &&
             writer.append(",\"provides\":") && writeStringArray(writer, module.provides) &&
             writer.append(",\"initializeAfter\":") &&
             writeStringArray(writer, module.initializeAfter) && writer.character('}');
    }
    ok = ok && writer.append("],\"capabilities\":[");
    for (std::size_t index = 0; ok && index < capabilityCount; ++index) {
        if (index != 0) ok = writer.character(',');
        ok = ok && writer.quoted(capabilities[index]->id);
    }
    ok = ok && writer.append("],\"components\":") && writer.append(componentArray) &&
         writer.append(",\"actions\":[");
    for (std::size_t index = 0; ok && index < actionCount; ++index) {
        const auto& action = *actions[index];
        if (index != 0) ok = writer.character(',');
        ok = ok && writer.append("{\"id\":") && writer.quoted(action.id) &&
             writer.append(",\"capability\":") && writer.quoted(action.capability) &&
             writer.append(",\"requiresActiveGame\":") &&
             writer.append(action.requiresActiveGame ? "true" : "false") &&
             writer.append(",\"parameters\":") && writeParameters(writer, action.parameters) &&
             writer.character('}');
    }
    ok = ok && writer.append("],\"conditionOperators\":[");
    for (std::size_t index = 0; ok && index < kConditionOperators.size(); ++index) {
        if (index != 0) ok = writer.character(',');
        ok = ok && writer.quoted(kConditionOperators[index]);
    }
    ok = ok && writer.append("],\"resourceTypes\":[");
    for (std::size_t index = 0; ok && index < kResourceTypes.size(); ++index) {
        if (index != 0) ok = writer.character(',');
        ok = ok && writer.quoted(kResourceTypes[index]);
    }
    ok = ok && writer.append("],\"events\":[");
    for (std::size_t index = 0; ok && index < eventCount; ++index) {
        const auto& event = *events[index];
        if (index != 0) ok = writer.character(',');
        ok = ok && writer.append("{\"id\":") && writer.quoted(event.id) &&
             writer.append(",\"version\":") && writer.unsignedInteger(event.version) &&
             writer.append(",\"payload\":") && writeParameters(writer, event.payload) &&
             writer.character('}');
    }
    ok = ok && writer.append("],\"contentSchemas\":[");
    for (std::size_t index = 0; ok && index < schemaCount; ++index) {
        if (index != 0) ok = writer.character(',');
        ok = ok && writer.append("{\"id\":") && writer.quoted(schemas[index]->id) &&
             writer.append(",\"version\":") &&
             writer.unsignedInteger(schemas[index]->version) && writer.character('}');
    }
    ok = ok && writer.append("],\"runtimeFeatures\":[");
    for (std::size_t index = 0; ok && index < featureCount; ++index) {
        const auto& feature = *features[index];
        if (index != 0) ok = writer.character(',');
        ok = ok && writer.append("{\"id\":") && writer.quoted(feature.id) &&
             writer.append(",\"description\":") && writer.quoted(feature.description) &&
             writer.append(",\"constraints\":") &&
             writeStringArray(writer, feature.constraints) && writer.character('}');
    }
    ok = ok && writer.append("]}");
    if (!ok || writer.failed()) {
        output[0] = '\0';
        return {ErrorCode::BufferTooSmall, {}, "Capability manifest output buffer is too small."};
    }
    written = writer.size();
    return Status::success();
}

} // namespace echo::framework
