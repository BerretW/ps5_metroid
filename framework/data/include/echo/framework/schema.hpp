#pragma once

#include "echo/framework/data.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>

namespace echo::framework {

inline constexpr std::size_t kMaxComponentTypes = 64;
inline constexpr std::size_t kMaxComponentInstanceBytes = 256;
inline constexpr std::size_t kMaxSchemaProperties = 32;
inline constexpr std::size_t kMaxSchemaDepth = 8;
inline constexpr std::size_t kMaxSchemaStringBytes = 4096;
inline constexpr std::size_t kMaxSchemaArrayElements = 128;

enum class PropertyType : std::uint8_t {
    Boolean,
    Integer,
    Float,
    String,
    Enumeration,
    Vector2,
    Color,
    ResourceReference,
    EntityReference,
    Array,
    Object
};

enum class EditorControl : std::uint8_t {
    Automatic,
    Checkbox,
    Slider,
    NumericInput,
    TextInput,
    Dropdown,
    ColorPicker,
    ResourceSelector,
    EntitySelector,
    VectorEditor,
    CollapsibleGroup
};

struct PropertyDefault {
    PropertyType type{PropertyType::Boolean};
    bool boolean{};
    std::int64_t integer{};
    double number{};
    std::string_view string{};
    std::array<double, 4> components{};
};

struct PropertyDescriptor;

class PropertyDescriptorList {
public:
    constexpr PropertyDescriptorList() noexcept = default;

    template <std::size_t Count>
    constexpr PropertyDescriptorList(
        const std::array<PropertyDescriptor, Count>& properties) noexcept;

    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr const PropertyDescriptor& operator[](
        std::size_t index) const noexcept;
    [[nodiscard]] const PropertyDescriptor* begin() const noexcept;
    [[nodiscard]] const PropertyDescriptor* end() const noexcept;
    [[nodiscard]] std::span<const PropertyDescriptor> view() const noexcept;

private:
    const PropertyDescriptor* data_{};
    std::size_t size_{};
};

struct PropertyDescriptor {
    std::string_view name;
    std::string_view label;
    std::string_view description;
    std::string_view group;
    std::string_view unit;
    PropertyType type{PropertyType::String};
    EditorControl editor{EditorControl::Automatic};
    ResourceType resourceType{ResourceType::Generic};
    bool required{};
    bool readOnly{};
    bool hasDefault{};
    PropertyDefault defaultValue{};
    bool hasMinimum{};
    bool hasMaximum{};
    double minimum{};
    double maximum{};
    std::size_t minimumLength{};
    std::size_t maximumLength{};
    std::span<const std::string_view> enumValues{};
    PropertyDescriptorList fields{};
    const PropertyDescriptor* element{};
};

template <std::size_t Count>
constexpr PropertyDescriptorList::PropertyDescriptorList(
    const std::array<PropertyDescriptor, Count>& properties) noexcept
    : data_(properties.data()), size_(Count) {}

inline constexpr const PropertyDescriptor& PropertyDescriptorList::operator[](
    std::size_t index) const noexcept {
    return data_[index];
}

inline const PropertyDescriptor* PropertyDescriptorList::begin() const noexcept {
    return data_;
}

inline const PropertyDescriptor* PropertyDescriptorList::end() const noexcept {
    return size_ == 0 ? data_ : data_ + size_;
}

inline std::span<const PropertyDescriptor> PropertyDescriptorList::view() const noexcept {
    return {data_, size_};
}

struct ComponentDependency {
    std::string_view id;
    bool required{true};
};

class ComponentConfigView {
public:
    [[nodiscard]] Status getBoolean(std::string_view name, bool& value) const noexcept;
    [[nodiscard]] Status getInteger(std::string_view name, std::int64_t& value) const noexcept;
    [[nodiscard]] Status getFloat(std::string_view name, double& value) const noexcept;
    [[nodiscard]] Status getString(std::string_view name, std::string_view& value) const noexcept;
    [[nodiscard]] Status getVector2(std::string_view name, std::array<double, 2>& value) const noexcept;
    [[nodiscard]] JsonValue getJson(std::string_view name) const noexcept;

private:
    ComponentConfigView(JsonValue document,
                        std::span<const PropertyDescriptor> properties) noexcept
        : document_(document), properties_(properties) {}

    [[nodiscard]] const PropertyDescriptor* property(std::string_view name) const noexcept;
    JsonValue document_{};
    std::span<const PropertyDescriptor> properties_{};
    friend class ComponentTypeRegistry;
};

class ComponentInstance {
public:
    void reset() noexcept;

    template <typename Component>
    [[nodiscard]] Status store(const Component& value) noexcept {
        static_assert(std::is_trivially_copyable_v<Component>,
                      "Component instances must be trivially copyable.");
        if (sizeof(Component) > bytes_.size()) {
            return {ErrorCode::CapacityExceeded, typeId_,
                    "Component instance exceeds the fixed storage limit."};
        }
        std::memcpy(bytes_.data(), &value, sizeof(Component));
        size_ = sizeof(Component);
        return Status::success();
    }

    template <typename Component>
    [[nodiscard]] bool read(Component& value) const noexcept {
        static_assert(std::is_trivially_copyable_v<Component>,
                      "Component instances must be trivially copyable.");
        if (size_ != sizeof(Component)) {
            return false;
        }
        std::memcpy(&value, bytes_.data(), sizeof(Component));
        return true;
    }

    [[nodiscard]] std::string_view typeId() const noexcept { return typeId_; }
    [[nodiscard]] std::uint32_t version() const noexcept { return version_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    std::string_view typeId_{};
    std::uint32_t version_{};
    std::array<std::byte, kMaxComponentInstanceBytes> bytes_{};
    std::size_t size_{};
    friend class ComponentTypeRegistry;
};

using ComponentFactory = Status (*)(void*, const ComponentConfigView&,
                                    ComponentInstance&) noexcept;
using ComponentConfigValidator = Status (*)(void*, const ComponentConfigView&,
                                             const ContentRegistry*) noexcept;

struct ComponentTypeDescriptor {
    std::string_view id;
    std::uint32_t version{};
    std::string_view displayName;
    std::string_view description;
    std::string_view category;
    std::span<const PropertyDescriptor> properties{};
    std::span<const ComponentDependency> dependencies{};
    ComponentFactory factory{};
    ComponentConfigValidator validate{};
    void* context{};
};

class ComponentTypeRegistry {
public:
    [[nodiscard]] Status add(const ComponentTypeDescriptor& descriptor) noexcept;
    [[nodiscard]] Status validate() const noexcept;
    [[nodiscard]] Status find(std::string_view id,
                              const ComponentTypeDescriptor*& descriptor) const noexcept;
    [[nodiscard]] Status validateConfig(std::string_view id, JsonValue configuration,
                                        const ContentRegistry* resources = nullptr) const noexcept;
    [[nodiscard]] Status instantiate(std::string_view id, JsonValue configuration,
                                     ComponentInstance& output,
                                     const ContentRegistry* resources = nullptr) const noexcept;
    [[nodiscard]] const ComponentTypeDescriptor* at(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return count_; }

private:
    [[nodiscard]] int findIndex(std::string_view id) const noexcept;

    std::array<const ComponentTypeDescriptor*, kMaxComponentTypes> descriptors_{};
    std::size_t count_{};
};

struct Transform2DComponent {
    float x{};
    float y{};
    float scaleX{1.0F};
    float scaleY{1.0F};
    float rotationDegrees{};
    bool visible{true};
};

[[nodiscard]] Status registerCoreComponentTypes(ComponentTypeRegistry& registry) noexcept;
[[nodiscard]] Status exportComponentManifest(const ComponentTypeRegistry& registry,
                                             char* output, std::size_t capacity,
                                             std::size_t& written) noexcept;

} // namespace echo::framework
