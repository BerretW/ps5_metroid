#include "echo/framework/schema.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace echo::framework {
namespace {

constexpr std::size_t kMaxComponentDependencies = 16;
constexpr std::size_t kMaxEnumValues = 64;

[[nodiscard]] std::string_view resourceTypeName(ResourceType type) noexcept {
    switch (type) {
    case ResourceType::Generic: return "generic";
    case ResourceType::Item: return "item";
    case ResourceType::Ability: return "ability";
    case ResourceType::Dialogue: return "dialogue";
    case ResourceType::Cutscene: return "cutscene";
    case ResourceType::Sprite: return "sprite";
    case ResourceType::Sound: return "sound";
    }
    return {};
}

[[nodiscard]] bool validPropertyName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 63) {
        return false;
    }
    for (const char character : name) {
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '_')) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string_view propertyTypeName(PropertyType type) noexcept {
    switch (type) {
    case PropertyType::Boolean: return "boolean";
    case PropertyType::Integer: return "integer";
    case PropertyType::Float: return "float";
    case PropertyType::String: return "string";
    case PropertyType::Enumeration: return "enum";
    case PropertyType::Vector2: return "vector2";
    case PropertyType::Color: return "color";
    case PropertyType::ResourceReference: return "resource";
    case PropertyType::EntityReference: return "entity";
    case PropertyType::Array: return "array";
    case PropertyType::Object: return "object";
    }
    return {};
}

[[nodiscard]] std::string_view editorControlName(EditorControl control) noexcept {
    switch (control) {
    case EditorControl::Automatic: return "auto";
    case EditorControl::Checkbox: return "checkbox";
    case EditorControl::Slider: return "slider";
    case EditorControl::NumericInput: return "numeric";
    case EditorControl::TextInput: return "text";
    case EditorControl::Dropdown: return "dropdown";
    case EditorControl::ColorPicker: return "color";
    case EditorControl::ResourceSelector: return "resource";
    case EditorControl::EntitySelector: return "entity";
    case EditorControl::VectorEditor: return "vector";
    case EditorControl::CollapsibleGroup: return "group";
    }
    return {};
}

[[nodiscard]] const PropertyDescriptor* findProperty(
    std::span<const PropertyDescriptor> properties, std::string_view name) noexcept {
    for (const PropertyDescriptor& property : properties) {
        if (property.name == name) {
            return &property;
        }
    }
    return nullptr;
}

[[nodiscard]] Status validatePropertySchema(const PropertyDescriptor& property,
                                            std::size_t depth) noexcept {
    if (depth >= kMaxSchemaDepth) {
        return {ErrorCode::InvalidValue, property.name, "Property schema nesting exceeds its limit."};
    }
    if (!validPropertyName(property.name) || property.label.empty() ||
        property.description.empty() || !validUtf8(property.label) ||
        !validUtf8(property.description) || !validUtf8(property.group) ||
        !validUtf8(property.unit)) {
        return {ErrorCode::InvalidValue, property.name, "Property metadata is missing or invalid."};
    }
    if ((property.hasMinimum && !std::isfinite(property.minimum)) ||
        (property.hasMaximum && !std::isfinite(property.maximum)) ||
        (property.hasMinimum && property.hasMaximum && property.minimum > property.maximum) ||
        (property.maximumLength != 0 && property.minimumLength > property.maximumLength)) {
        return {ErrorCode::InvalidValue, property.name, "Property limits are inconsistent."};
    }
    if ((property.type == PropertyType::String &&
         (property.minimumLength > kMaxSchemaStringBytes ||
          property.maximumLength > kMaxSchemaStringBytes)) ||
        (property.type == PropertyType::Array &&
         (property.minimumLength > kMaxSchemaArrayElements ||
          property.maximumLength > kMaxSchemaArrayElements))) {
        return {ErrorCode::InvalidValue, property.name, "Property length exceeds the framework limit."};
    }
    if (property.type == PropertyType::Enumeration) {
        if (property.enumValues.empty() || property.enumValues.size() > kMaxEnumValues) {
            return {ErrorCode::InvalidValue, property.name, "Enum properties require a bounded value list."};
        }
        for (std::size_t index = 0; index < property.enumValues.size(); ++index) {
            const std::string_view value = property.enumValues[index];
            if (value.empty() || !validUtf8(value)) {
                return {ErrorCode::InvalidValue, property.name, "Enum values must be valid non-empty UTF-8."};
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (property.enumValues[previous] == value) {
                    return {ErrorCode::DuplicateId, value, "Enum values must be unique."};
                }
            }
        }
    } else if (!property.enumValues.empty()) {
        return {ErrorCode::InvalidValue, property.name, "Only enum properties may declare enum values."};
    }
    if ((property.type == PropertyType::Array) != (property.element != nullptr) ||
        (property.type == PropertyType::Object) != (!property.fields.empty())) {
        return {ErrorCode::InvalidValue, property.name,
                "Arrays require an item schema and objects require field schemas."};
    }
    if (property.fields.size() > kMaxSchemaProperties) {
        return {ErrorCode::CapacityExceeded, property.name, "Object schema has too many properties."};
    }
    for (std::size_t index = 0; index < property.fields.size(); ++index) {
        const Status status = validatePropertySchema(property.fields[index], depth + 1);
        if (!status) {
            return status;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (property.fields[previous].name == property.fields[index].name) {
                return {ErrorCode::DuplicateId, property.fields[index].name,
                        "Object property names must be unique."};
            }
        }
    }
    if (property.element != nullptr) {
        const Status status = validatePropertySchema(*property.element, depth + 1);
        if (!status) {
            return status;
        }
    }
    if (property.hasDefault) {
        if (property.defaultValue.type != property.type) {
            return {ErrorCode::InvalidValue, property.name, "Default value type does not match the property."};
        }
        const PropertyDefault& value = property.defaultValue;
        switch (property.type) {
        case PropertyType::Boolean:
            break;
        case PropertyType::Integer:
            if ((property.hasMinimum && static_cast<double>(value.integer) < property.minimum) ||
                (property.hasMaximum && static_cast<double>(value.integer) > property.maximum)) {
                return {ErrorCode::InvalidValue, property.name, "Integer default is outside its range."};
            }
            break;
        case PropertyType::Float:
            if (!std::isfinite(value.number) ||
                (property.hasMinimum && value.number < property.minimum) ||
                (property.hasMaximum && value.number > property.maximum)) {
                return {ErrorCode::InvalidValue, property.name, "Float default is invalid or outside its range."};
            }
            break;
        case PropertyType::String:
            if (!validUtf8(value.string) || value.string.size() > kMaxSchemaStringBytes ||
                (property.maximumLength != 0 && value.string.size() > property.maximumLength) ||
                value.string.size() < property.minimumLength) {
                return {ErrorCode::InvalidValue, property.name, "String default violates its length or encoding constraints."};
            }
            break;
        case PropertyType::Enumeration:
            if (std::find(property.enumValues.begin(), property.enumValues.end(), value.string) ==
                property.enumValues.end()) {
                return {ErrorCode::InvalidValue, property.name, "Enum default is not an allowed value."};
            }
            break;
        case PropertyType::Vector2:
            for (std::size_t index = 0; index < 2; ++index) {
                if (!std::isfinite(value.components[index]) ||
                    (property.hasMinimum && value.components[index] < property.minimum) ||
                    (property.hasMaximum && value.components[index] > property.maximum)) {
                    return {ErrorCode::InvalidValue, property.name, "Vector default violates its numeric range."};
                }
            }
            break;
        case PropertyType::Color:
            for (std::size_t index = 0; index < 4; ++index) {
                if (!std::isfinite(value.components[index]) ||
                    value.components[index] < 0.0 || value.components[index] > 1.0 ||
                    (property.hasMinimum && value.components[index] < property.minimum) ||
                    (property.hasMaximum && value.components[index] > property.maximum)) {
                    return {ErrorCode::InvalidValue, property.name, "Color defaults use normalized RGBA values."};
                }
            }
            break;
        case PropertyType::ResourceReference:
            if (!isValidContentId(value.string)) {
                return {ErrorCode::InvalidValue, property.name, "Resource default ID is invalid."};
            }
            break;
        case PropertyType::EntityReference:
        case PropertyType::Array:
        case PropertyType::Object:
            return {ErrorCode::InvalidValue, property.name,
                    "This property type cannot use an implicit scalar default."};
        }
    }
    return Status::success();
}

[[nodiscard]] Status validateObject(JsonValue value,
                                    std::span<const PropertyDescriptor> properties,
                                    const ContentRegistry* resources,
                                    std::size_t depth) noexcept;

[[nodiscard]] Status validateValue(const PropertyDescriptor& property, JsonValue value,
                                   const ContentRegistry* resources,
                                   std::size_t depth) noexcept {
    if (depth >= kMaxSchemaDepth || !value.valid()) {
        return {ErrorCode::InvalidValue, property.name, "Property value is missing or too deeply nested."};
    }
    const auto validateRange = [&property](double number) noexcept {
        return std::isfinite(number) &&
               (!property.hasMinimum || number >= property.minimum) &&
               (!property.hasMaximum || number <= property.maximum);
    };
    bool boolean{};
    std::int64_t integer{};
    double number{};
    std::string_view text;
    switch (property.type) {
    case PropertyType::Boolean:
        return value.boolean(boolean)
                   ? Status::success()
                   : Status{ErrorCode::InvalidValue, property.name, "Expected a boolean value."};
    case PropertyType::Integer:
        return value.integer(integer) && validateRange(static_cast<double>(integer))
                   ? Status::success()
                   : Status{ErrorCode::InvalidValue, property.name, "Expected an in-range integer value."};
    case PropertyType::Float:
        return value.number(number) && validateRange(number)
                   ? Status::success()
                   : Status{ErrorCode::InvalidValue, property.name, "Expected an in-range finite number."};
    case PropertyType::String:
        if (!value.string(text) || !validUtf8(text) || text.size() > kMaxSchemaStringBytes ||
            text.size() < property.minimumLength ||
            (property.maximumLength != 0 && text.size() > property.maximumLength)) {
            return {ErrorCode::InvalidValue, property.name, "String value violates its size or encoding constraints."};
        }
        return Status::success();
    case PropertyType::Enumeration:
        if (!value.string(text) ||
            std::find(property.enumValues.begin(), property.enumValues.end(), text) ==
                property.enumValues.end()) {
            return {ErrorCode::InvalidValue, property.name, "Value is not in the enum list."};
        }
        return Status::success();
    case PropertyType::Vector2:
        if (!value.isArray() || value.size() != 2) {
            return {ErrorCode::InvalidValue, property.name, "Vector2 values must be two-number arrays."};
        }
        for (std::size_t index = 0; index < 2; ++index) {
            if (!value.at(index).number(number) || !validateRange(number)) {
                return {ErrorCode::InvalidValue, property.name, "Vector2 component is invalid or out of range."};
            }
        }
        return Status::success();
    case PropertyType::Color:
        if (!value.isArray() || value.size() != 4) {
            return {ErrorCode::InvalidValue, property.name, "Color values must be normalized RGBA arrays."};
        }
        for (std::size_t index = 0; index < 4; ++index) {
            if (!value.at(index).number(number) || number < 0.0 || number > 1.0 ||
                !validateRange(number)) {
                return {ErrorCode::InvalidValue, property.name, "RGBA components must be finite values from 0 to 1."};
            }
        }
        return Status::success();
    case PropertyType::ResourceReference: {
        if (!value.string(text) || !isValidContentId(text)) {
            return {ErrorCode::InvalidValue, property.name, "Resource reference ID is invalid."};
        }
        if (resources != nullptr) {
            ResourceInfo info{};
            const Status found = resources->find(text, info);
            if (!found) {
                return {ErrorCode::MissingDependency, property.name, "Resource reference does not resolve."};
            }
            if (property.resourceType != ResourceType::Generic &&
                info.type != property.resourceType) {
                return {ErrorCode::InvalidValue, property.name, "Resource reference has an incompatible type."};
            }
        }
        return Status::success();
    }
    case PropertyType::EntityReference: {
        if (!value.isObject() || value.size() != 2) {
            return {ErrorCode::InvalidValue, property.name, "Entity references require index and generation fields."};
        }
        bool hasIndex = false;
        bool hasGeneration = false;
        for (std::size_t memberIndex = 0; memberIndex < value.size(); ++memberIndex) {
            std::string_view key;
            JsonValue member{};
            if (!value.memberAt(memberIndex, key, member) ||
                (key != "index" && key != "generation")) {
                return {ErrorCode::InvalidValue, property.name,
                        "Entity references contain only index and generation fields."};
            }
            if (key == "index") {
                if (hasIndex) {
                    return {ErrorCode::DuplicateId, property.name,
                            "Entity reference index is duplicated."};
                }
                hasIndex = true;
            } else {
                if (hasGeneration) {
                    return {ErrorCode::DuplicateId, property.name,
                            "Entity reference generation is duplicated."};
                }
                hasGeneration = true;
            }
        }
        std::int64_t index{};
        std::int64_t generation{};
        if (!hasIndex || !hasGeneration || !value.find("index").integer(index) || index < 0 ||
            index >= EntityId::kInvalidIndex ||
            !value.find("generation").integer(generation) || generation <= 0 ||
            generation > std::numeric_limits<std::uint32_t>::max()) {
            return {ErrorCode::InvalidValue, property.name, "Entity reference handle is invalid."};
        }
        return Status::success();
    }
    case PropertyType::Array:
        if (!value.isArray() || value.size() < property.minimumLength ||
            value.size() > (property.maximumLength == 0
                                ? kMaxSchemaArrayElements
                                : property.maximumLength)) {
            return {ErrorCode::InvalidValue, property.name, "Array violates its item-count constraints."};
        }
        for (std::size_t index = 0; index < value.size(); ++index) {
            const Status itemStatus = validateValue(*property.element, value.at(index),
                                                    resources, depth + 1);
            if (!itemStatus) {
                return itemStatus;
            }
        }
        return Status::success();
    case PropertyType::Object:
        return validateObject(value, property.fields.view(), resources, depth + 1);
    }
    return {ErrorCode::InvalidValue, property.name, "Property type is not supported."};
}

[[nodiscard]] Status validateObject(JsonValue value,
                                    std::span<const PropertyDescriptor> properties,
                                    const ContentRegistry* resources,
                                    std::size_t depth) noexcept {
    if (!value.isObject() || properties.size() > kMaxSchemaProperties ||
        value.size() > kMaxSchemaProperties || depth >= kMaxSchemaDepth) {
        return {ErrorCode::InvalidValue, "properties",
                "Component configuration must be a bounded object."};
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        std::string_view key;
        JsonValue member{};
        if (!value.memberAt(index, key, member) || !validPropertyName(key)) {
            return {ErrorCode::InvalidValue, "properties", "Configuration property name is invalid."};
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            std::string_view previousKey;
            JsonValue previousValue{};
            if (!value.memberAt(previous, previousKey, previousValue)) {
                return {ErrorCode::InvalidValue, key, "Configuration object could not be enumerated."};
            }
            if (previousKey == key) {
                return {ErrorCode::DuplicateId, key, "Configuration object has a duplicate property."};
            }
        }
        const PropertyDescriptor* schema = findProperty(properties, key);
        if (schema == nullptr) {
            return {ErrorCode::InvalidValue, key, "Configuration contains an unsupported property."};
        }
        const Status status = validateValue(*schema, member, resources, depth + 1);
        if (!status) {
            return status;
        }
    }
    for (const PropertyDescriptor& property : properties) {
        if (property.required && !value.find(property.name).valid()) {
            return {ErrorCode::InvalidValue, property.name, "Required property is missing."};
        }
    }
    return Status::success();
}

[[nodiscard]] Status validateDescriptorSchema(const ComponentTypeDescriptor& descriptor) noexcept {
    if (!isValidContentId(descriptor.id) || descriptor.version == 0 ||
        descriptor.displayName.empty() || descriptor.description.empty() ||
        descriptor.category.empty() || !validUtf8(descriptor.displayName) ||
        !validUtf8(descriptor.description) || !validUtf8(descriptor.category) ||
        descriptor.properties.size() > kMaxSchemaProperties ||
        descriptor.dependencies.size() > kMaxComponentDependencies ||
        descriptor.factory == nullptr) {
        return {ErrorCode::InvalidValue, descriptor.id,
                "Component metadata, version, limits, and factory are required."};
    }
    for (std::size_t index = 0; index < descriptor.properties.size(); ++index) {
        const Status status = validatePropertySchema(descriptor.properties[index], 0);
        if (!status) {
            return status;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (descriptor.properties[previous].name == descriptor.properties[index].name) {
                return {ErrorCode::DuplicateId, descriptor.properties[index].name,
                        "Component property names must be unique."};
            }
        }
    }
    for (std::size_t index = 0; index < descriptor.dependencies.size(); ++index) {
        if (!isValidContentId(descriptor.dependencies[index].id)) {
            return {ErrorCode::InvalidValue, descriptor.dependencies[index].id,
                    "Component dependency ID is invalid."};
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (descriptor.dependencies[previous].id == descriptor.dependencies[index].id) {
                return {ErrorCode::DuplicateId, descriptor.dependencies[index].id,
                        "Component dependencies must be unique."};
            }
        }
    }
    return Status::success();
}

class ManifestWriter {
public:
    ManifestWriter(char* output, std::size_t capacity) noexcept
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
        if (failed_ || position_ == capacity_) {
            failed_ = true;
            return false;
        }
        output_[position_++] = value;
        return true;
    }

    [[nodiscard]] bool quoted(std::string_view value) noexcept {
        if (!validUtf8(value) || !character('"')) return false;
        for (const unsigned char characterValue : value) {
            switch (characterValue) {
            case '"': if (!append("\\\"")) return false; break;
            case '\\': if (!append("\\\\")) return false; break;
            case '\n': if (!append("\\n")) return false; break;
            case '\r': if (!append("\\r")) return false; break;
            case '\t': if (!append("\\t")) return false; break;
            default:
                if (characterValue < 0x20U) {
                    return false;
                }
                if (!character(static_cast<char>(characterValue))) return false;
                break;
            }
        }
        return character('"');
    }

    [[nodiscard]] bool integer(std::int64_t value) noexcept {
        char buffer[24]{};
        const int length = std::snprintf(buffer, sizeof(buffer), "%lld",
                                         static_cast<long long>(value));
        return length > 0 && static_cast<std::size_t>(length) < sizeof(buffer) &&
               append({buffer, static_cast<std::size_t>(length)});
    }

    [[nodiscard]] bool real(double value) noexcept {
        if (!std::isfinite(value)) return false;
        char buffer[32]{};
        const int length = std::snprintf(buffer, sizeof(buffer), "%.17g", value);
        return length > 0 && static_cast<std::size_t>(length) < sizeof(buffer) &&
               append({buffer, static_cast<std::size_t>(length)});
    }

    [[nodiscard]] std::size_t size() const noexcept { return position_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }

private:
    char* output_{};
    std::size_t capacity_{};
    std::size_t position_{};
    bool failed_{};
};

[[nodiscard]] bool writeDefault(ManifestWriter& writer,
                                const PropertyDefault& value) noexcept {
    switch (value.type) {
    case PropertyType::Boolean: return writer.append(value.boolean ? "true" : "false");
    case PropertyType::Integer: return writer.integer(value.integer);
    case PropertyType::Float: return writer.real(value.number);
    case PropertyType::String:
    case PropertyType::Enumeration:
    case PropertyType::ResourceReference:
        return writer.quoted(value.string);
    case PropertyType::Vector2:
    case PropertyType::Color: {
        const std::size_t count = value.type == PropertyType::Vector2 ? 2 : 4;
        if (!writer.character('[')) return false;
        for (std::size_t index = 0; index < count; ++index) {
            if (index != 0 && !writer.character(',')) return false;
            if (!writer.real(value.components[index])) return false;
        }
        return writer.character(']');
    }
    case PropertyType::Array: return writer.append("[]");
    case PropertyType::Object: return writer.append("{}");
    case PropertyType::EntityReference:
        return writer.append("null");
    }
    return false;
}

[[nodiscard]] bool writeProperties(ManifestWriter& writer,
                                   std::span<const PropertyDescriptor> properties) noexcept;

[[nodiscard]] bool writeProperty(ManifestWriter& writer,
                                const PropertyDescriptor& property) noexcept {
    if (!writer.append("{\"name\":") || !writer.quoted(property.name) ||
        !writer.append(",\"type\":") || !writer.quoted(propertyTypeName(property.type)) ||
        !writer.append(",\"required\":") || !writer.append(property.required ? "true" : "false") ||
        !writer.append(",\"readOnly\":") || !writer.append(property.readOnly ? "true" : "false")) {
        return false;
    }
    if (property.hasDefault &&
        (!writer.append(",\"default\":") || !writeDefault(writer, property.defaultValue))) {
        return false;
    }
    if (property.hasMinimum &&
        (!writer.append(",\"minimum\":") || !writer.real(property.minimum))) {
        return false;
    }
    if (property.hasMaximum &&
        (!writer.append(",\"maximum\":") || !writer.real(property.maximum))) {
        return false;
    }
    if (property.minimumLength != 0 &&
        (!writer.append(",\"minimumLength\":") ||
         !writer.integer(static_cast<std::int64_t>(property.minimumLength)))) {
        return false;
    }
    if (property.maximumLength != 0 &&
        (!writer.append(",\"maximumLength\":") ||
         !writer.integer(static_cast<std::int64_t>(property.maximumLength)))) {
        return false;
    }
    if (property.type == PropertyType::Enumeration) {
        if (!writer.append(",\"values\":[")) return false;
        for (std::size_t index = 0; index < property.enumValues.size(); ++index) {
            if ((index != 0 && !writer.character(',')) ||
                !writer.quoted(property.enumValues[index])) {
                return false;
            }
        }
        if (!writer.character(']')) return false;
    }
    if (property.type == PropertyType::ResourceReference &&
        property.resourceType != ResourceType::Generic) {
        if (!writer.append(",\"resourceType\":") ||
            !writer.quoted(resourceTypeName(property.resourceType))) {
            return false;
        }
    }
    if (property.type == PropertyType::Array) {
        if (!writer.append(",\"items\":") || !writeProperty(writer, *property.element)) {
            return false;
        }
    }
    if (property.type == PropertyType::Object) {
        if (!writer.append(",\"properties\":") ||
            !writeProperties(writer, property.fields.view())) {
            return false;
        }
    }
    if (!writer.append(",\"ui\":{\"control\":") ||
        !writer.quoted(editorControlName(property.editor)) ||
        !writer.append(",\"label\":") || !writer.quoted(property.label) ||
        !writer.append(",\"description\":") || !writer.quoted(property.description)) {
        return false;
    }
    if (!property.group.empty() &&
        (!writer.append(",\"group\":") || !writer.quoted(property.group))) {
        return false;
    }
    if (!property.unit.empty() &&
        (!writer.append(",\"unit\":") || !writer.quoted(property.unit))) {
        return false;
    }
    return writer.append("}}");
}

[[nodiscard]] bool writeProperties(ManifestWriter& writer,
                                   std::span<const PropertyDescriptor> properties) noexcept {
    if (!writer.character('[')) return false;
    for (std::size_t index = 0; index < properties.size(); ++index) {
        if ((index != 0 && !writer.character(',')) ||
            !writeProperty(writer, properties[index])) {
            return false;
        }
    }
    return writer.character(']');
}

[[nodiscard]] bool componentDependencyVisit(
    const ComponentTypeRegistry& registry, std::size_t index,
    std::array<std::uint8_t, kMaxComponentTypes>& marks) noexcept {
    if (marks[index] == 2) return true;
    if (marks[index] == 1) return false;
    marks[index] = 1;
    const ComponentTypeDescriptor* descriptor = registry.at(index);
    for (const ComponentDependency& dependency : descriptor->dependencies) {
        const ComponentTypeDescriptor* target = nullptr;
        if (!registry.find(dependency.id, target)) continue;
        for (std::size_t targetIndex = 0; targetIndex < registry.size(); ++targetIndex) {
            if (registry.at(targetIndex) == target &&
                !componentDependencyVisit(registry, targetIndex, marks)) {
                return false;
            }
        }
    }
    marks[index] = 2;
    return true;
}

[[nodiscard]] Status transformFactory(void*, const ComponentConfigView& config,
                                      ComponentInstance& output) noexcept {
    std::array<double, 2> position{};
    std::array<double, 2> scale{{1.0, 1.0}};
    double rotation{};
    bool visible = true;
    if (const Status status = config.getVector2("position", position); !status) return status;
    if (const Status status = config.getVector2("scale", scale); !status) return status;
    if (const Status status = config.getFloat("rotationDegrees", rotation); !status) return status;
    if (const Status status = config.getBoolean("visible", visible); !status) return status;
    const Transform2DComponent transform{
        static_cast<float>(position[0]), static_cast<float>(position[1]),
        static_cast<float>(scale[0]), static_cast<float>(scale[1]),
        static_cast<float>(rotation), visible};
    return output.store(transform);
}

} // namespace

const PropertyDescriptor* ComponentConfigView::property(std::string_view name) const noexcept {
    return findProperty(properties_, name);
}

Status ComponentConfigView::getBoolean(std::string_view name, bool& value) const noexcept {
    const PropertyDescriptor* schema = property(name);
    if (schema == nullptr || schema->type != PropertyType::Boolean) {
        return {ErrorCode::NotFound, name, "Boolean property is not registered."};
    }
    const JsonValue json = document_.find(name);
    if (json.valid()) {
        return json.boolean(value) ? Status::success()
                                   : Status{ErrorCode::InvalidValue, name, "Property is not boolean."};
    }
    if (schema->hasDefault) {
        value = schema->defaultValue.boolean;
        return Status::success();
    }
    return {ErrorCode::NotFound, name, "Boolean property has no value or default."};
}

Status ComponentConfigView::getInteger(std::string_view name, std::int64_t& value) const noexcept {
    const PropertyDescriptor* schema = property(name);
    if (schema == nullptr || schema->type != PropertyType::Integer) {
        return {ErrorCode::NotFound, name, "Integer property is not registered."};
    }
    const JsonValue json = document_.find(name);
    if (json.valid()) {
        return json.integer(value) ? Status::success()
                                   : Status{ErrorCode::InvalidValue, name, "Property is not an integer."};
    }
    if (schema->hasDefault) {
        value = schema->defaultValue.integer;
        return Status::success();
    }
    return {ErrorCode::NotFound, name, "Integer property has no value or default."};
}

Status ComponentConfigView::getFloat(std::string_view name, double& value) const noexcept {
    const PropertyDescriptor* schema = property(name);
    if (schema == nullptr || schema->type != PropertyType::Float) {
        return {ErrorCode::NotFound, name, "Float property is not registered."};
    }
    const JsonValue json = document_.find(name);
    if (json.valid()) {
        return json.number(value) ? Status::success()
                                  : Status{ErrorCode::InvalidValue, name, "Property is not numeric."};
    }
    if (schema->hasDefault) {
        value = schema->defaultValue.number;
        return Status::success();
    }
    return {ErrorCode::NotFound, name, "Float property has no value or default."};
}

Status ComponentConfigView::getString(std::string_view name, std::string_view& value) const noexcept {
    const PropertyDescriptor* schema = property(name);
    if (schema == nullptr ||
        (schema->type != PropertyType::String &&
         schema->type != PropertyType::Enumeration &&
         schema->type != PropertyType::ResourceReference)) {
        return {ErrorCode::NotFound, name, "String-like property is not registered."};
    }
    const JsonValue json = document_.find(name);
    if (json.valid()) {
        return json.string(value) ? Status::success()
                                  : Status{ErrorCode::InvalidValue, name, "Property is not a string."};
    }
    if (schema->hasDefault) {
        value = schema->defaultValue.string;
        return Status::success();
    }
    return {ErrorCode::NotFound, name, "String property has no value or default."};
}

Status ComponentConfigView::getVector2(std::string_view name,
                                       std::array<double, 2>& value) const noexcept {
    const PropertyDescriptor* schema = property(name);
    if (schema == nullptr || schema->type != PropertyType::Vector2) {
        return {ErrorCode::NotFound, name, "Vector2 property is not registered."};
    }
    const JsonValue json = document_.find(name);
    if (json.valid()) {
        if (!json.isArray() || json.size() != 2 ||
            !json.at(0).number(value[0]) || !json.at(1).number(value[1])) {
            return {ErrorCode::InvalidValue, name, "Vector2 property must contain two numbers."};
        }
        return Status::success();
    }
    if (schema->hasDefault) {
        value = {schema->defaultValue.components[0], schema->defaultValue.components[1]};
        return Status::success();
    }
    return {ErrorCode::NotFound, name, "Vector2 property has no value or default."};
}

JsonValue ComponentConfigView::getJson(std::string_view name) const noexcept {
    return document_.find(name);
}

void ComponentInstance::reset() noexcept {
    typeId_ = {};
    version_ = 0;
    bytes_.fill(std::byte{});
    size_ = 0;
}

Status ComponentTypeRegistry::add(const ComponentTypeDescriptor& descriptor) noexcept {
    const Status schemaStatus = validateDescriptorSchema(descriptor);
    if (!schemaStatus) return schemaStatus;
    if (findIndex(descriptor.id) >= 0) {
        return {ErrorCode::DuplicateId, descriptor.id, "Component type ID is already registered."};
    }
    if (count_ == descriptors_.size()) {
        return {ErrorCode::CapacityExceeded, descriptor.id, "Component type registry is full."};
    }
    descriptors_[count_++] = &descriptor;
    return Status::success();
}

Status ComponentTypeRegistry::validate() const noexcept {
    std::array<std::uint8_t, kMaxComponentTypes> marks{};
    for (std::size_t index = 0; index < count_; ++index) {
        const ComponentTypeDescriptor& descriptor = *descriptors_[index];
        for (const ComponentDependency& dependency : descriptor.dependencies) {
            const int target = findIndex(dependency.id);
            if (target < 0 && dependency.required) {
                return {ErrorCode::MissingDependency, dependency.id,
                        "Component requires an unregistered component type."};
            }
        }
    }
    for (std::size_t index = 0; index < count_; ++index) {
        if (!componentDependencyVisit(*this, index, marks)) {
            return {ErrorCode::DependencyCycle, descriptors_[index]->id,
                    "Required component dependencies contain a cycle."};
        }
    }
    return Status::success();
}

Status ComponentTypeRegistry::find(std::string_view id,
                                   const ComponentTypeDescriptor*& descriptor) const noexcept {
    const int index = findIndex(id);
    if (index < 0) {
        descriptor = nullptr;
        return {ErrorCode::NotFound, id, "Component type is not registered."};
    }
    descriptor = descriptors_[static_cast<std::size_t>(index)];
    return Status::success();
}

Status ComponentTypeRegistry::validateConfig(std::string_view id, JsonValue configuration,
                                             const ContentRegistry* resources) const noexcept {
    const Status registryStatus = validate();
    if (!registryStatus) return registryStatus;
    const ComponentTypeDescriptor* descriptor = nullptr;
    const Status found = find(id, descriptor);
    if (!found) return found;
    const Status schema = validateObject(configuration, descriptor->properties, resources, 0);
    if (!schema) return schema;
    if (descriptor->validate != nullptr) {
        const ComponentConfigView view{configuration, descriptor->properties};
        return descriptor->validate(descriptor->context, view, resources);
    }
    return Status::success();
}

Status ComponentTypeRegistry::instantiate(std::string_view id, JsonValue configuration,
                                          ComponentInstance& output,
                                          const ContentRegistry* resources) const noexcept {
    output.reset();
    const ComponentTypeDescriptor* descriptor = nullptr;
    const Status found = find(id, descriptor);
    if (!found) return found;
    const Status schema = validateConfig(id, configuration, resources);
    if (!schema) return schema;
    const ComponentConfigView view{configuration, descriptor->properties};
    const Status created = descriptor->factory(descriptor->context, view, output);
    if (!created) {
        output.reset();
        return created;
    }
    if (output.size_ == 0 || output.size_ > output.bytes_.size()) {
        output.reset();
        return {ErrorCode::InvalidState, id, "Component factory did not create a valid instance."};
    }
    output.typeId_ = descriptor->id;
    output.version_ = descriptor->version;
    return Status::success();
}

const ComponentTypeDescriptor* ComponentTypeRegistry::at(std::size_t index) const noexcept {
    return index < count_ ? descriptors_[index] : nullptr;
}

int ComponentTypeRegistry::findIndex(std::string_view id) const noexcept {
    for (std::size_t index = 0; index < count_; ++index) {
        if (descriptors_[index]->id == id) return static_cast<int>(index);
    }
    return -1;
}

Status registerCoreComponentTypes(ComponentTypeRegistry& registry) noexcept {
    static constexpr std::array<PropertyDescriptor, 4> properties{{
        {.name = "position", .label = "Position", .description = "Local position in logical world units.",
         .group = "Transform", .unit = "px", .type = PropertyType::Vector2,
         .editor = EditorControl::VectorEditor, .hasDefault = true,
         .defaultValue = {PropertyType::Vector2, false, 0, 0.0, {}, {0.0, 0.0, 0.0, 0.0}},
         .hasMinimum = true, .hasMaximum = true, .minimum = -100000.0, .maximum = 100000.0},
        {.name = "scale", .label = "Scale", .description = "Local scale on each axis.",
         .group = "Transform", .unit = {}, .type = PropertyType::Vector2,
         .editor = EditorControl::VectorEditor, .hasDefault = true,
         .defaultValue = {PropertyType::Vector2, false, 0, 0.0, {}, {1.0, 1.0, 0.0, 0.0}},
         .hasMinimum = true, .hasMaximum = true, .minimum = 0.0, .maximum = 1000.0},
        {.name = "rotationDegrees", .label = "Rotation", .description = "Clockwise local rotation.",
         .group = "Transform", .unit = "deg", .type = PropertyType::Float,
         .editor = EditorControl::NumericInput, .hasDefault = true,
         .defaultValue = {PropertyType::Float, false, 0, 0.0},
         .hasMinimum = true, .hasMaximum = true, .minimum = -3600.0, .maximum = 3600.0},
        {.name = "visible", .label = "Visible", .description = "Whether the entity is presented.",
         .group = "Presentation", .unit = {}, .type = PropertyType::Boolean,
         .editor = EditorControl::Checkbox, .hasDefault = true,
         .defaultValue = {PropertyType::Boolean, true}},
    }};
    static constexpr ComponentTypeDescriptor descriptor{
        "echo:component/transform2d", 1, "2D Transform",
        "Local position, scale, rotation, and visibility.",
        "Core", properties, {}, &transformFactory, nullptr, nullptr};
    return registry.add(descriptor);
}

Status exportComponentManifest(const ComponentTypeRegistry& registry,
                               char* output, std::size_t capacity,
                               std::size_t& written) noexcept {
    written = 0;
    if (output == nullptr || capacity == 0) {
        return {ErrorCode::InvalidArgument, {}, "Manifest output buffer is invalid."};
    }
    const Status validation = registry.validate();
    if (!validation) return validation;
    std::array<const ComponentTypeDescriptor*, kMaxComponentTypes> ordered{};
    for (std::size_t index = 0; index < registry.size(); ++index) {
        ordered[index] = registry.at(index);
    }
    std::sort(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(registry.size()),
              [](const auto* left, const auto* right) { return left->id < right->id; });

    ManifestWriter writer{output, capacity};
    bool ok = writer.append("{\"framework\":\"Echo Framework\",\"manifestVersion\":1,") &&
              writer.append("\"frameworkApiVersion\":1,\"components\":[");
    for (std::size_t index = 0; ok && index < registry.size(); ++index) {
        const ComponentTypeDescriptor& descriptor = *ordered[index];
        if (index != 0) ok = writer.character(',');
        ok = ok && writer.append("{\"id\":") && writer.quoted(descriptor.id) &&
             writer.append(",\"version\":") &&
             writer.integer(static_cast<std::int64_t>(descriptor.version)) &&
             writer.append(",\"displayName\":") && writer.quoted(descriptor.displayName) &&
             writer.append(",\"description\":") && writer.quoted(descriptor.description) &&
             writer.append(",\"category\":") && writer.quoted(descriptor.category) &&
             writer.append(",\"dependencies\":[");
        for (std::size_t dependencyIndex = 0;
             ok && dependencyIndex < descriptor.dependencies.size(); ++dependencyIndex) {
            const ComponentDependency& dependency = descriptor.dependencies[dependencyIndex];
            if (dependencyIndex != 0) ok = writer.character(',');
            ok = ok && writer.append("{\"id\":") && writer.quoted(dependency.id) &&
                 writer.append(",\"required\":") &&
                 writer.append(dependency.required ? "true" : "false") &&
                 writer.character('}');
        }
        ok = ok && writer.append("],\"properties\":") &&
             writeProperties(writer, descriptor.properties) && writer.character('}');
    }
    ok = ok && writer.append("]}");
    if (!ok || writer.failed()) {
        output[0] = '\0';
        return {ErrorCode::BufferTooSmall, {}, "Component manifest output buffer is too small."};
    }
    written = writer.size();
    return Status::success();
}

} // namespace echo::framework
