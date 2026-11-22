#pragma once

#include "echo/framework/rendering.hpp"
#include "echo/framework/schema.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace echo::rendering {

struct Transform3DComponent {
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0F, 1.0F, 1.0F};
};

struct MaterialComponent {
    std::array<char, 64> id{};
    std::array<char, 64> textureId{};
    MaterialModel model{MaterialModel::Unlit};
    AlphaMode alpha{AlphaMode::Opaque};
    Vec4 baseColor{1, 1, 1, 1};
    Vec3 emission{};
    float metallic{};
    float roughness{0.5F};
};

struct PointLightComponent {
    Vec3 color{1.0F, 1.0F, 1.0F};
    float intensity{1.0F};
    float radius{1.0F};
    bool enabled{true};
};

struct CameraComponent {
    Vec3 position{0.0F, 0.0F, 5.0F};
    Vec3 target{};
    Vec3 up{0.0F, 1.0F, 0.0F};
    float fovRadians{1.04719755F};
    float nearPlane{0.1F};
    float farPlane{100.0F};
};

[[nodiscard]] framework::Status registerRenderingComponentTypes(
    framework::ComponentTypeRegistry& registry) noexcept;

inline constexpr std::size_t kMaxSceneMaterials = 32;
inline constexpr std::size_t kMaxSceneEntities = 64;

struct SceneMaterial {
    std::string_view id;
    std::string_view textureId;
    Material material{};
};

struct SceneEntity {
    std::string_view id;
    std::string_view meshId;
    std::string_view materialId;
    std::uint32_t parentIndex{UINT32_MAX};
    Transform3DComponent transform{};
    bool visible{true};
    float spinRadiansPerSecond{};
};

struct SceneCamera {
    CameraComponent camera{};
};

struct SceneDefinition {
    std::string_view id;
    SceneCamera camera{};
    std::span<const SceneMaterial> materials{};
    std::span<const SceneEntity> entities{};
    Vec4 directionalAmbient{-0.4F, -1, -0.3F, 0.18F};
    Vec4 directionalColorIntensity{1, 1, 1, 1};
    std::array<PointLight, 4> points{};
};

class SceneDocument {
public:
    SceneDocument() noexcept = default;
    SceneDocument(const SceneDocument&) = delete;
    SceneDocument& operator=(const SceneDocument&) = delete;
    SceneDocument(SceneDocument&&) = delete;
    SceneDocument& operator=(SceneDocument&&) = delete;

    [[nodiscard]] framework::Status parse(const char* data, std::size_t size,
                                          framework::JsonError& error,
                                          const framework::ContentRegistry* resources = nullptr) noexcept;
    [[nodiscard]] const SceneDefinition* definition() const noexcept {
        return loaded_ ? &definition_ : nullptr;
    }

private:
    void reset() noexcept;
    [[nodiscard]] framework::Status parseCamera(framework::JsonValue value) noexcept;
    [[nodiscard]] framework::Status parseMaterials(framework::JsonValue value) noexcept;
    [[nodiscard]] framework::Status parseEntities(framework::JsonValue value) noexcept;
    [[nodiscard]] framework::Status parseLights(framework::JsonValue value) noexcept;
    [[nodiscard]] framework::Status validateReferences(
        const framework::ContentRegistry* resources) const noexcept;

    framework::JsonDocument document_{};
    std::array<SceneMaterial, kMaxSceneMaterials> materials_{};
    std::array<SceneEntity, kMaxSceneEntities> entities_{};
    std::size_t materialCount_{};
    std::size_t entityCount_{};
    SceneCamera camera_{};
    FrameState lighting_{};
    SceneDefinition definition_{};
    bool loaded_{};
};

} // namespace echo::rendering
