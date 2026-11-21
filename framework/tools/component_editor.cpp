#include "echo/framework/schema.hpp"
#include "json_input.hpp"

#include <cstdio>

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
    if (argc != 3 && argc != 4) {
        std::fprintf(stderr,
                     "Usage: echo_component_editor <component-id> <config.json> "
                     "[resource-manifest.json]\n");
        return 2;
    }

    echo::framework::ComponentTypeRegistry components{};
    echo::framework::Status status =
        echo::framework::registerCoreComponentTypes(components);
    if (!status) {
        echo::framework::tools::reportStatus(status);
        return 1;
    }
    const echo::framework::ComponentTypeDescriptor* descriptor = nullptr;
    status = components.find(argv[1], descriptor);
    if (!status) {
        echo::framework::tools::reportStatus(status);
        return 1;
    }

    std::printf("%.*s (%.*s v%u)\n",
                static_cast<int>(descriptor->displayName.size()),
                descriptor->displayName.data(),
                static_cast<int>(descriptor->id.size()), descriptor->id.data(),
                descriptor->version);
    for (const PropertyDescriptor& property : descriptor->properties) {
        inspectProperty(property, 1);
    }

    echo::framework::JsonDocument configuration{};
    echo::framework::JsonError error{};
    status = echo::framework::tools::readJsonFile(argv[2], configuration, error);
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
    if (argc == 4) {
        status = echo::framework::tools::readJsonFile(argv[3], resourceDocument, error);
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
    status = components.instantiate(argv[1], configuration.root(), instance,
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
