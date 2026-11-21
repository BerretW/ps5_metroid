#pragma once

#include "echo/framework/capability_manifest.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>

namespace echo::framework::tools {

[[nodiscard]] inline bool validManifestPropertyName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 63) return false;
    for (const char character : name) {
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '_')) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool validManifestProperty(
    JsonValue property, std::size_t depth) noexcept;

[[nodiscard]] inline bool validManifestProperties(
    JsonValue properties, std::size_t depth) noexcept {
    if (!properties.isArray() || properties.size() > kMaxSchemaProperties ||
        depth >= kMaxSchemaDepth) {
        return false;
    }
    for (std::size_t index = 0; index < properties.size(); ++index) {
        const JsonValue property = properties.at(index);
        std::string_view name;
        if (!property.find("name").string(name) ||
            !validManifestPropertyName(name) ||
            !validManifestProperty(property, depth)) return false;
        for (std::size_t previous = 0; previous < index; ++previous) {
            std::string_view previousName;
            if (!properties.at(previous).find("name").string(previousName) ||
                previousName == name) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] inline bool validManifestProperty(
    JsonValue property, std::size_t depth) noexcept {
    if (!property.isObject() || depth >= kMaxSchemaDepth) return false;
    std::string_view name;
    std::string_view type;
    bool required{};
    bool readOnly{};
    const JsonValue ui = property.find("ui");
    std::string_view control;
    std::string_view label;
    std::string_view description;
    if (!property.find("name").string(name) || !validManifestPropertyName(name) ||
        !property.find("type").string(type) ||
        !property.find("required").boolean(required) ||
        !property.find("readOnly").boolean(readOnly) || !ui.isObject() ||
        !ui.find("control").string(control) || control.empty() ||
        !ui.find("label").string(label) || label.empty() || !validUtf8(label) ||
        !ui.find("description").string(description) || description.empty() ||
        !validUtf8(description)) {
        return false;
    }
    std::string_view optionalText;
    if ((ui.find("group").valid() &&
         (!ui.find("group").string(optionalText) || !validUtf8(optionalText))) ||
        (ui.find("unit").valid() &&
         (!ui.find("unit").string(optionalText) || !validUtf8(optionalText)))) {
        return false;
    }

    constexpr std::array<std::string_view, 11> propertyTypes{
        "boolean", "integer", "float", "string", "enum", "vector2",
        "color", "resource", "entity", "array", "object"};
    constexpr std::array<std::string_view, 11> editorControls{
        "auto", "checkbox", "slider", "numeric", "text", "dropdown",
        "color", "resource", "entity", "vector", "group"};
    if (std::find(propertyTypes.begin(), propertyTypes.end(), type) ==
            propertyTypes.end() ||
        std::find(editorControls.begin(), editorControls.end(), control) ==
            editorControls.end()) {
        return false;
    }

    const bool arrayType = type == "array";
    const bool objectType = type == "object";
    const JsonValue items = property.find("items");
    const JsonValue fields = property.find("properties");
    if ((arrayType != items.isObject()) ||
        (objectType != fields.isArray()) ||
        (!arrayType && items.valid()) ||
        (!objectType && fields.valid()) ||
        (objectType && fields.size() == 0)) {
        return false;
    }

    const JsonValue values = property.find("values");
    if (type == "enum") {
        if (!values.isArray() || values.size() == 0 || values.size() > 64) {
            return false;
        }
        for (std::size_t valueIndex = 0; valueIndex < values.size(); ++valueIndex) {
            std::string_view value;
            if (!values.at(valueIndex).string(value) || value.empty() ||
                !validUtf8(value)) {
                return false;
            }
            for (std::size_t previous = 0; previous < valueIndex; ++previous) {
                std::string_view previousValue;
                if (!values.at(previous).string(previousValue) ||
                    previousValue == value) {
                    return false;
                }
            }
        }
    } else if (values.valid()) {
        return false;
    }

    const JsonValue minimum = property.find("minimum");
    const JsonValue maximum = property.find("maximum");
    double minimumValue{};
    double maximumValue{};
    if ((minimum.valid() &&
         (!minimum.number(minimumValue) || !std::isfinite(minimumValue))) ||
        (maximum.valid() &&
         (!maximum.number(maximumValue) || !std::isfinite(maximumValue))) ||
        (minimum.valid() && maximum.valid() && minimumValue > maximumValue)) {
        return false;
    }
    const JsonValue minimumLength = property.find("minimumLength");
    const JsonValue maximumLength = property.find("maximumLength");
    std::int64_t minimumLengthValue{};
    std::int64_t maximumLengthValue{};
    const std::int64_t lengthLimit = type == "array"
                                         ? static_cast<std::int64_t>(kMaxSchemaArrayElements)
                                         : static_cast<std::int64_t>(kMaxSchemaStringBytes);
    if ((minimumLength.valid() &&
         (!minimumLength.integer(minimumLengthValue) || minimumLengthValue < 0 ||
          minimumLengthValue > lengthLimit)) ||
        (maximumLength.valid() &&
         (!maximumLength.integer(maximumLengthValue) || maximumLengthValue <= 0 ||
          maximumLengthValue > lengthLimit)) ||
        (minimumLength.valid() && maximumLength.valid() &&
         minimumLengthValue > maximumLengthValue)) {
        return false;
    }

    const JsonValue resourceType = property.find("resourceType");
    if (resourceType.valid()) {
        std::string_view resource;
        constexpr std::array<std::string_view, 7> resourceTypes{
            "generic", "item", "ability", "dialogue", "cutscene", "sprite", "sound"};
        if (type != "resource" || !resourceType.string(resource) ||
            std::find(resourceTypes.begin(), resourceTypes.end(), resource) ==
                resourceTypes.end()) {
            return false;
        }
    }

    return (!arrayType || validManifestProperty(items, depth + 1)) &&
           (!objectType || validManifestProperties(fields, depth + 1));
}

[[nodiscard]] inline Status validateCapabilityManifest(
    JsonValue root, const ComponentTypeRegistry& registry,
    JsonValue& componentSchemas) noexcept {
    componentSchemas = {};
    if (!root.isObject()) {
        return {ErrorCode::InvalidValue, "manifest",
                "Capability manifest root must be an object."};
    }

    std::int64_t manifestVersion{};
    if (!root.find("manifestVersion").integer(manifestVersion) ||
        manifestVersion != kCapabilityManifestVersion) {
        return {ErrorCode::UnsupportedVersion, "manifestVersion",
                "Capability manifest version is not supported."};
    }
    const JsonValue framework = root.find("framework");
    const JsonValue compatibility = root.find("compatibility");
    std::int64_t apiVersion{};
    std::int64_t minimumApiVersion{};
    std::int64_t compatibilityApiVersion{};
    if (!framework.isObject() || !compatibility.isObject() ||
        !framework.find("apiVersion").integer(apiVersion) ||
        !framework.find("minimumApiVersion").integer(minimumApiVersion) ||
        !compatibility.find("apiVersion").integer(compatibilityApiVersion) ||
        apiVersion != kFrameworkApiVersion ||
        compatibilityApiVersion != kFrameworkApiVersion ||
        minimumApiVersion <= 0 ||
        minimumApiVersion > static_cast<std::int64_t>(kFrameworkApiVersion)) {
        return {ErrorCode::UnsupportedVersion, "framework.apiVersion",
                "Capability manifest requires an unsupported framework API."};
    }

    const JsonValue supportedVersions =
        compatibility.find("supportedManifestVersions");
    if (!supportedVersions.isArray() || supportedVersions.size() > 16) {
        return {ErrorCode::InvalidValue, "compatibility.supportedManifestVersions",
                "Supported manifest versions must be a bounded array."};
    }
    bool supportsCurrentVersion{};
    for (std::size_t index = 0; index < supportedVersions.size(); ++index) {
        std::int64_t supportedVersion{};
        if (!supportedVersions.at(index).integer(supportedVersion) ||
            supportedVersion <= 0) {
            return {ErrorCode::InvalidValue, "compatibility.supportedManifestVersions",
                    "Supported manifest versions must be positive integers."};
        }
        supportsCurrentVersion = supportsCurrentVersion ||
                                 supportedVersion == kCapabilityManifestVersion;
    }
    if (!supportsCurrentVersion) {
        return {ErrorCode::UnsupportedVersion, "manifestVersion",
                "Capability manifest does not declare support for this format version."};
    }

    componentSchemas = root.find("components");
    if (!componentSchemas.isArray() ||
        componentSchemas.size() > kMaxComponentTypes) {
        componentSchemas = {};
        return {ErrorCode::CapacityExceeded, "components",
                "Capability manifest component list is missing or exceeds its limit."};
    }

    std::string_view previousId;
    for (std::size_t index = 0; index < componentSchemas.size(); ++index) {
        const JsonValue component = componentSchemas.at(index);
        std::string_view id;
        std::string_view displayName;
        std::int64_t version{};
        const JsonValue properties = component.find("properties");
        if (!component.isObject() || !component.find("id").string(id) ||
            !isValidContentId(id) ||
            !component.find("displayName").string(displayName) ||
            displayName.empty() || !validUtf8(displayName) ||
            !component.find("version").integer(version) || version <= 0 ||
            version > std::numeric_limits<std::uint32_t>::max() ||
            !validManifestProperties(properties, 0)) {
            componentSchemas = {};
            return {ErrorCode::InvalidValue, "components",
                    "Capability manifest contains invalid component metadata."};
        }
        if (index != 0 && previousId >= id) {
            componentSchemas = {};
            return {previousId == id ? ErrorCode::DuplicateId : ErrorCode::InvalidValue,
                    id, "Component IDs must be unique and sorted in the manifest."};
        }
        previousId = id;

        const ComponentTypeDescriptor* descriptor{};
        if (!registry.find(id, descriptor)) {
            componentSchemas = {};
            return {ErrorCode::NotFound, id,
                    "Manifest component is not compiled into this editor."};
        }
        if (descriptor->version != static_cast<std::uint32_t>(version)) {
            componentSchemas = {};
            return {ErrorCode::UnsupportedVersion, id,
                    "Manifest component version does not match the compiled descriptor."};
        }
    }
    return Status::success();
}

[[nodiscard]] inline Status findManifestComponent(
    JsonValue componentSchemas, std::string_view id,
    JsonValue& component) noexcept {
    component = {};
    for (std::size_t index = 0; index < componentSchemas.size(); ++index) {
        JsonValue candidate = componentSchemas.at(index);
        std::string_view candidateId;
        if (candidate.find("id").string(candidateId) && candidateId == id) {
            component = candidate;
            return Status::success();
        }
    }
    return {ErrorCode::NotFound, id,
            "Component is not listed in the capability manifest."};
}

} // namespace echo::framework::tools
