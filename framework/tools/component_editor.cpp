#include "echo/framework/schema.hpp"
#include "capability_manifest_input.hpp"
#include "json_input.hpp"
#ifdef ECHO_PROJECT_COMPONENTS
#include "game/project_components.hpp"
#endif

#include <cstdio>
#include <cstring>

namespace {

using echo::framework::EditorControl;
using echo::framework::PropertyDescriptor;
using echo::framework::PropertyType;

const char* propertyTypeName(PropertyType type) noexcept {
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
    return "unknown";
}

const char* editorControlName(EditorControl control) noexcept {
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
    return "unknown";
}

void printIndent(std::size_t depth) {
    for (std::size_t index = 0; index < depth; ++index) {
        std::fputs("  ", stdout);
    }
}

void printEscaped(std::string_view text) {
    for (const unsigned char character : text) {
        if (character < 0x20U || character == 0x7fU) {
            std::printf("\\x%02X", static_cast<unsigned int>(character));
        } else {
            std::fputc(static_cast<int>(character), stdout);
        }
    }
}

bool inspectManifestProperties(echo::framework::JsonValue properties,
                               std::size_t depth);

bool inspectManifestProperty(echo::framework::JsonValue property,
                             std::size_t depth) {
    std::string_view name;
    std::string_view type;
    std::string_view label;
    std::string_view control;
    bool required{};
    bool readOnly{};
    const echo::framework::JsonValue ui = property.find("ui");
    if (!property.find("name").string(name) || !property.find("type").string(type) ||
        !property.find("required").boolean(required) ||
        !property.find("readOnly").boolean(readOnly) ||
        !ui.find("label").string(label) || !ui.find("control").string(control)) {
        return false;
    }

    printIndent(depth);
    printEscaped(label);
    std::fputs(" (", stdout);
    printEscaped(name);
    std::fputs("): ", stdout);
    printEscaped(type);
    std::fputs(" [", stdout);
    printEscaped(control);
    std::printf("]%s%s%s", required ? " required" : "",
                readOnly ? " read-only" : "",
                property.find("default").valid() ? " default" : "");

    double minimum{};
    double maximum{};
    const echo::framework::JsonValue minimumValue = property.find("minimum");
    const echo::framework::JsonValue maximumValue = property.find("maximum");
    if (minimumValue.valid() || maximumValue.valid()) {
        std::fputs(" range=[", stdout);
        if (minimumValue.valid() && minimumValue.number(minimum)) {
            std::printf("%.6g", minimum);
        } else {
            std::fputs("-inf", stdout);
        }
        std::fputc(',', stdout);
        if (maximumValue.valid() && maximumValue.number(maximum)) {
            std::printf("%.6g", maximum);
        } else {
            std::fputs("+inf", stdout);
        }
        std::fputc(']', stdout);
    }
    std::fputc('\n', stdout);

    std::string_view description;
    if (!ui.find("description").string(description)) return false;
    printIndent(depth + 1);
    printEscaped(description);
    std::fputc('\n', stdout);

    const echo::framework::JsonValue values = property.find("values");
    if (values.valid()) {
        printIndent(depth + 1);
        std::fputs("values: ", stdout);
        for (std::size_t index = 0; index < values.size(); ++index) {
            std::string_view value;
            if (!values.at(index).string(value)) return false;
            if (index != 0) std::fputs(", ", stdout);
            printEscaped(value);
        }
        std::fputc('\n', stdout);
    }

    const echo::framework::JsonValue properties = property.find("properties");
    if (properties.valid() && !inspectManifestProperties(properties, depth + 1)) {
        return false;
    }
    const echo::framework::JsonValue item = property.find("items");
    if (item.valid()) {
        printIndent(depth + 1);
        std::fputs("items:\n", stdout);
        if (!inspectManifestProperty(item, depth + 2)) return false;
    }
    return true;
}

bool inspectManifestProperties(echo::framework::JsonValue properties,
                               std::size_t depth) {
    for (std::size_t index = 0; index < properties.size(); ++index) {
        if (!inspectManifestProperty(properties.at(index), depth)) return false;
    }
    return true;
}

void inspectProperty(const PropertyDescriptor& property, std::size_t depth) {
    printIndent(depth);
    std::printf("%.*s: %s (%s)%s",
                static_cast<int>(property.name.size()), property.name.data(),
                propertyTypeName(property.type), editorControlName(property.editor),
                property.required ? " required" : "");
    if (property.hasDefault) {
        std::fputs(" default", stdout);
    }
    if (property.hasMinimum || property.hasMaximum) {
        std::fputs(" range=[", stdout);
        if (property.hasMinimum) {
            std::printf("%.6g", property.minimum);
        } else {
            std::fputs("-inf", stdout);
        }
        std::fputc(',', stdout);
        if (property.hasMaximum) {
            std::printf("%.6g", property.maximum);
        } else {
            std::fputs("+inf", stdout);
        }
        std::fputc(']', stdout);
    }
    std::fputc('\n', stdout);

    if (property.type == PropertyType::Object) {
        for (const PropertyDescriptor& child : property.fields.view()) {
            inspectProperty(child, depth + 1);
        }
    } else if (property.type == PropertyType::Array && property.element != nullptr) {
        printIndent(depth + 1);
        std::fputs("items:\n", stdout);
        inspectProperty(*property.element, depth + 2);
    }
}

} // namespace

int main(int argc, char** argv) {
    const bool manifestMode = argc > 1 && std::strcmp(argv[1], "--manifest") == 0;
    if ((!manifestMode && argc != 3 && argc != 4) ||
        (manifestMode && argc != 3 && argc != 5 && argc != 6)) {
        std::fprintf(stderr,
                     "Usage: echo_component_editor <component-id> <config.json> "
                     "[resource-manifest.json]\n"
                     "       echo_component_editor --manifest <capability-manifest.json> "
                     "[<component-id> <config.json> [resource-manifest.json]]\n");
        return 2;
    }

    echo::framework::ComponentTypeRegistry components{};
    echo::framework::Status status =
        echo::framework::registerCoreComponentTypes(components);
    if (!status) {
        echo::framework::tools::reportStatus(status);
        return 1;
    }
#ifdef ECHO_PROJECT_COMPONENTS
    status = echo::registerProjectComponentTypes(components);
    if (!status) {
        echo::framework::tools::reportStatus(status);
        return 1;
    }
#endif
    const int componentArgument = manifestMode ? 3 : 1;
    const int configurationArgument = componentArgument + 1;
    const int resourcesArgument = componentArgument + 2;
    const char* componentId = manifestMode ? nullptr : argv[componentArgument];
    echo::framework::JsonDocument manifestDocument{};
    echo::framework::JsonValue manifestComponents{};
    echo::framework::JsonValue manifestComponent{};
    if (manifestMode) {
        echo::framework::JsonError manifestError{};
        status = echo::framework::tools::readJsonFile(argv[2], manifestDocument,
                                                       manifestError);
        if (!status) {
            if (status.code == echo::framework::ErrorCode::ParseError) {
                std::fprintf(stderr, "Manifest JSON parse failed at byte %zu: %.*s\n",
                             manifestError.offset,
                             static_cast<int>(manifestError.message.size()),
                             manifestError.message.data());
            } else {
                echo::framework::tools::reportStatus(status);
            }
            return 1;
        }
        status = echo::framework::tools::validateCapabilityManifest(
            manifestDocument.root(), components, manifestComponents);
        if (!status) {
            echo::framework::tools::reportStatus(status);
            return 1;
        }
        if (argc == 3) {
            for (std::size_t index = 0; index < manifestComponents.size(); ++index) {
                std::string_view id;
                if (!manifestComponents.at(index).find("id").string(id)) {
                    echo::framework::tools::reportStatus(
                        {echo::framework::ErrorCode::InvalidState, "components",
                         "Validated component ID is unavailable."});
                    return 1;
                }
                const echo::framework::ComponentTypeDescriptor* listedDescriptor{};
                status = components.find(id, listedDescriptor);
                if (!status) {
                    echo::framework::tools::reportStatus(status);
                    return 1;
                }
                std::printf("%.*s  %.*s (v%u)\n",
                            static_cast<int>(id.size()), id.data(),
                            static_cast<int>(listedDescriptor->displayName.size()),
                            listedDescriptor->displayName.data(),
                            listedDescriptor->version);
            }
            return 0;
        }
        componentId = argv[componentArgument];
        status = echo::framework::tools::findManifestComponent(
            manifestComponents, componentId, manifestComponent);
        if (!status) {
            echo::framework::tools::reportStatus(status);
            return 1;
        }
    }

    const echo::framework::ComponentTypeDescriptor* descriptor = nullptr;
    status = components.find(componentId, descriptor);
    if (!status) {
        echo::framework::tools::reportStatus(status);
        return 1;
    }

    if (manifestMode) {
        std::string_view displayName;
        std::int64_t version{};
        if (!manifestComponent.find("displayName").string(displayName) ||
            !manifestComponent.find("version").integer(version)) {
            echo::framework::tools::reportStatus(
                {echo::framework::ErrorCode::InvalidState, componentId,
                 "Validated component metadata is unavailable."});
            return 1;
        }
        printEscaped(displayName);
        std::fputs(" (", stdout);
        printEscaped(componentId);
        std::printf(" v%lld)\n", static_cast<long long>(version));
        if (!inspectManifestProperties(manifestComponent.find("properties"), 1)) {
            echo::framework::tools::reportStatus(
                {echo::framework::ErrorCode::InvalidState, componentId,
                 "Validated component property metadata is unavailable."});
            return 1;
        }
    } else {
        std::printf("%.*s (%.*s v%u)\n",
                    static_cast<int>(descriptor->displayName.size()),
                    descriptor->displayName.data(),
                    static_cast<int>(descriptor->id.size()), descriptor->id.data(),
                    descriptor->version);
        for (const PropertyDescriptor& property : descriptor->properties) {
            inspectProperty(property, 1);
        }
    }

    echo::framework::JsonDocument configuration{};
    echo::framework::JsonError error{};
    status = echo::framework::tools::readJsonFile(argv[configurationArgument],
                                                   configuration, error);
    if (!status) {
        if (status.code == echo::framework::ErrorCode::ParseError) {
            std::fprintf(stderr, "JSON parse failed at byte %zu: %.*s\n", error.offset,
                         static_cast<int>(error.message.size()), error.message.data());
        } else {
            echo::framework::tools::reportStatus(status);
        }
        return 1;
    }

    echo::framework::ContentRegistry resources{};
    echo::framework::JsonDocument resourceDocument{};
    const echo::framework::ContentRegistry* resourceRegistry = nullptr;
    if (argc > resourcesArgument) {
        status = echo::framework::tools::readJsonFile(argv[resourcesArgument],
                                                       resourceDocument, error);
        if (!status) {
            if (status.code == echo::framework::ErrorCode::ParseError) {
                std::fprintf(stderr, "Resource JSON parse failed at byte %zu: %.*s\n",
                             error.offset, static_cast<int>(error.message.size()),
                             error.message.data());
            } else {
                echo::framework::tools::reportStatus(status);
            }
            return 1;
        }
        status = echo::framework::loadResourceManifest(resourceDocument.root(), resources);
        if (!status) {
            echo::framework::tools::reportStatus(status);
            return 1;
        }
        resourceRegistry = &resources;
    }

    echo::framework::ComponentInstance instance{};
    status = components.instantiate(componentId, configuration.root(), instance,
                                    resourceRegistry);
    if (!status) {
        echo::framework::tools::reportStatus(status);
        return 1;
    }
    std::printf("Configuration is valid; created %.*s (%zu bytes).\n",
                static_cast<int>(instance.typeId().size()), instance.typeId().data(),
                instance.size());
    return 0;
}
