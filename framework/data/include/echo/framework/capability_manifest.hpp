#pragma once

#include "echo/framework/schema.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace echo::framework {

inline constexpr std::uint32_t kCapabilityManifestVersion = 1;
inline constexpr std::uint32_t kFrameworkApiVersion = 1;
inline constexpr std::string_view kFrameworkVersion = "0.2.0";

struct ManifestModuleDescriptor {
    std::string_view id;
    std::string_view name;
    std::uint32_t version{};
    std::span<const std::string_view> required{};
    std::span<const std::string_view> optional{};
    std::span<const std::string_view> provides{};
    std::span<const std::string_view> initializeAfter{};
};

struct ManifestActionParameter {
    std::string_view name;
    std::string_view type;
    bool required{true};
};

struct ManifestActionDescriptor {
    std::string_view id;
    std::string_view capability;
    bool requiresActiveGame{true};
    std::span<const ManifestActionParameter> parameters{};
};

struct ManifestCapabilityDescriptor {
    std::string_view id;
};

struct ManifestEventDescriptor {
    std::string_view id;
    std::uint32_t version{};
    std::span<const ManifestActionParameter> payload{};
};

struct ManifestSchemaDescriptor {
    std::string_view id;
    std::uint32_t version{};
};

struct ManifestFeatureDescriptor {
    std::string_view id;
    std::string_view description;
    std::span<const std::string_view> constraints{};
};

struct CapabilityManifestFragment {
    std::span<const ManifestModuleDescriptor> modules{};
    std::span<const ManifestCapabilityDescriptor> capabilities{};
    std::span<const ManifestActionDescriptor> actions{};
    std::span<const ManifestEventDescriptor> events{};
    std::span<const ManifestSchemaDescriptor> contentSchemas{};
    std::span<const ManifestFeatureDescriptor> runtimeFeatures{};
};

[[nodiscard]] Status exportCapabilityManifest(
    const ComponentTypeRegistry& components,
    std::span<const CapabilityManifestFragment> fragments,
    char* output, std::size_t capacity, std::size_t& written) noexcept;

[[nodiscard]] CapabilityManifestFragment frameworkCapabilityManifestFragment() noexcept;
[[nodiscard]] CapabilityManifestFragment narrativeCapabilityManifestFragment() noexcept;

} // namespace echo::framework
