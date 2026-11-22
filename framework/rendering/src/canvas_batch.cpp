#include "echo/framework/canvas_batch.hpp"

#include <cmath>
#include <cstdio>

namespace echo::rendering {
namespace {

float linear(std::uint8_t component) noexcept {
    const float value = static_cast<float>(component) / 255.0F;
    return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

Vec4 color(Color2D value) noexcept {
    return {linear(value.r), linear(value.g), linear(value.b),
            static_cast<float>(value.a) / 255.0F};
}

} // namespace

bool CanvasBatch2D::clear(Color2D value) noexcept {
    vertexCount_ = indexCount_ = commandCount_ = 0;
    background_ = color(value);
    lighting_ = FrameState{};
    worldPass_ = false;
    return true;
}

bool CanvasBatch2D::setWorldLighting(const FrameState& frame) noexcept {
    const auto valid = [](Vec4 value) noexcept {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
               std::isfinite(value.z) && std::isfinite(value.w);
    };
    if (!valid(frame.lightDirectionAmbient) || !valid(frame.lightColorIntensity) ||
        !valid(frame.ambientColor) || !valid(frame.lightScreenToWorld) ||
        !valid(frame.shadowInfo) || frame.shadowInfo.x < 0.0F ||
        frame.shadowInfo.x > static_cast<float>(kMax2DShadowSegments) ||
        std::floor(frame.shadowInfo.x) != frame.shadowInfo.x) {
        std::fprintf(stderr, "Canvas rejected invalid world lighting state\n");
        return false;
    }
    for (const PointLight& light : frame.pointLights) {
        if (!valid(light.positionRadius) || !valid(light.colorIntensity) ||
            !valid(light.attenuation) || !valid(light.directionCone)) {
            std::fprintf(stderr, "Canvas rejected invalid world light\n");
            return false;
        }
    }
    for (const Vec4& segment : frame.shadowSegments) {
        if (!valid(segment)) {
            std::fprintf(stderr, "Canvas rejected invalid world shadow segment\n");
            return false;
        }
    }
    lighting_.lightDirectionAmbient = frame.lightDirectionAmbient;
    lighting_.lightColorIntensity = frame.lightColorIntensity;
    lighting_.ambientColor = frame.ambientColor;
    lighting_.lightScreenToWorld = frame.lightScreenToWorld;
    lighting_.pointLights = frame.pointLights;
    lighting_.shadowInfo = frame.shadowInfo;
    lighting_.shadowSegments = frame.shadowSegments;
    return true;
}

bool CanvasBatch2D::fill(Rect2D bounds, Color2D value) noexcept {
    if (!fillMaterial_.valid()) return false;
    return quad(bounds, color(value), {0, 0, 1, 1}, fillMaterial_, false, 0.0F);
}

bool CanvasBatch2D::texture(TextureHandle textureHandle, Rect2D bounds, Vec4 uvTransform,
                            Vec4 tint, float rotationDegrees) noexcept {
    return textureWithLighting(textureHandle, {}, {}, {}, bounds, uvTransform, tint,
                               rotationDegrees);
}

bool CanvasBatch2D::textureWithLighting(TextureHandle textureHandle,
                                        TextureHandle normalTexture,
                                        TextureHandle emissiveMask, Vec3 emission,
                                        Rect2D bounds, Vec4 uvTransform, Vec4 tint,
                                        float rotationDegrees) noexcept {
    const auto validVector = [](Vec4 value) noexcept {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
               std::isfinite(value.z) && std::isfinite(value.w);
    };
    if (!validVector(uvTransform) || !validVector(tint) ||
        !std::isfinite(rotationDegrees) || !std::isfinite(emission.x) ||
        !std::isfinite(emission.y) || !std::isfinite(emission.z) ||
        uvTransform.z == 0.0F || uvTransform.w == 0.0F || !textureHandle.valid()) {
        std::fprintf(stderr, "Canvas rejected invalid texture draw\n");
        return false;
    }
    if (emission.x < 0.0F || emission.y < 0.0F || emission.z < 0.0F) {
        std::fprintf(stderr, "Canvas emission cannot be negative\n");
        return false;
    }
    for (float channel : {tint.x, tint.y, tint.z, tint.w}) {
        if (channel < 0.0F || channel > 1.0F) {
            std::fprintf(stderr, "Canvas tint must be normalized\n");
            return false;
        }
    }
    if (uvTransform.x < 0.0F || uvTransform.x > 1.0F ||
        uvTransform.y < 0.0F || uvTransform.y > 1.0F ||
        uvTransform.x + uvTransform.z < 0.0F || uvTransform.x + uvTransform.z > 1.0F ||
        uvTransform.y + uvTransform.w < 0.0F || uvTransform.y + uvTransform.w > 1.0F) {
        std::fprintf(stderr, "Canvas UV transform is outside the texture\n");
        return false;
    }
    MaterialHandle material{};
    if (!materialFor(textureHandle, normalTexture, emissiveMask, emission, material))
        return false;
    return quad(bounds, tint, uvTransform, material, true, rotationDegrees);
}

MeshView CanvasBatch2D::mesh() const noexcept {
    return {std::span<const Vertex>(vertices_.data(), vertexCount_),
            std::span<const std::uint32_t>(indices_.data(), indexCount_)};
}

Status CanvasBatch2D::flushMesh(MeshHandle meshHandle) noexcept {
    return backend_.updateDynamicMesh(meshHandle, mesh());
}

Status CanvasBatch2D::submit(MeshHandle meshHandle) const noexcept {
    for (std::size_t index = 0; index < commandCount_; ++index) {
        const Command& command = commands_[index];
        Draw draw{meshHandle, command.material};
        draw.firstIndex = command.firstIndex;
        draw.indexCount = command.indexCount;
        draw.overlay = true;
        draw.worldPass = command.worldPass;
        auto status = backend_.submit(draw);
        if (!status) return status;
    }
    return Status::success();
}

Status CanvasBatch2D::destroyMaterials() noexcept {
    while (materialCount_ != 0) {
        const auto status = backend_.destroyMaterial(materials_[materialCount_ - 1].material);
        if (!status) return status;
        --materialCount_;
        materials_[materialCount_] = {};
    }
    return Status::success();
}

bool CanvasBatch2D::materialFor(TextureHandle textureHandle,
                                TextureHandle normalTexture,
                                TextureHandle emissiveMask, Vec3 emission,
                                MaterialHandle& output) noexcept {
    for (std::size_t index = 0; index < materialCount_; ++index) {
        const TextureMaterial& cached = materials_[index];
        if (cached.texture == textureHandle && cached.normalTexture == normalTexture &&
            cached.emissiveMask == emissiveMask && cached.emission.x == emission.x &&
            cached.emission.y == emission.y && cached.emission.z == emission.z) {
            output = materials_[index].material;
            return true;
        }
    }
    if (materialCount_ == materials_.size()) {
        std::fprintf(stderr, "Canvas material capacity exceeded\n");
        return false;
    }
    Material material{};
    material.model = MaterialModel::Textured;
    material.alpha = AlphaMode::Blend;
    material.texture = textureHandle;
    material.normalTexture = normalTexture;
    material.metallicRoughnessTexture = emissiveMask;
    material.emission = emission;
    const auto status = backend_.createMaterial(material, output);
    if (!status) {
        std::fprintf(stderr, "Canvas material creation failed: %.*s\n",
                     static_cast<int>(status.detail.size()), status.detail.data());
        return false;
    }
    materials_[materialCount_++] = {textureHandle, normalTexture, emissiveMask,
                                    emission, output};
    return true;
}

bool CanvasBatch2D::quad(Rect2D bounds, Vec4 tint, Vec4 uvTransform,
                         MaterialHandle material, bool textured, float rotationDegrees) noexcept {
    if (bounds.w <= 0 || bounds.h <= 0) return true;
    if (vertexCount_ > vertices_.size() - 4 || indexCount_ > indices_.size() - 6) {
        std::fprintf(stderr, "Canvas geometry capacity exceeded\n");
        return false;
    }
    const bool merge = commandCount_ != 0 &&
                   commands_[commandCount_ - 1].material == material &&
                   commands_[commandCount_ - 1].worldPass == worldPass_;
    if (!merge && commandCount_ == commands_.size()) {
        std::fprintf(stderr, "Canvas draw capacity exceeded\n");
        return false;
    }
    const float left = static_cast<float>(bounds.x);
    const float top = static_cast<float>(bounds.y);
    const float right = left + static_cast<float>(bounds.w);
    const float bottom = top + static_cast<float>(bounds.h);
    std::array<Vec3, 4> positions{{
        {left, top, 0}, {right, top, 0}, {right, bottom, 0}, {left, bottom, 0}}};
    if (rotationDegrees != 0.0F) {
        constexpr float kDegreesToRadians = 0.01745329251994329577F;
        const float radians = rotationDegrees * kDegreesToRadians;
        const float cosine = std::cos(radians), sine = std::sin(radians);
        const float centerX = (left + right) * 0.5F;
        const float centerY = (top + bottom) * 0.5F;
        for (Vec3& position : positions) {
            const float x = position.x - centerX;
            const float y = position.y - centerY;
            position.x = centerX + x * cosine - y * sine;
            position.y = centerY + x * sine + y * cosine;
        }
    }
    constexpr std::array<Vec2, 4> uv{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    const auto firstVertex = static_cast<std::uint32_t>(vertexCount_);
    const auto firstIndex = static_cast<std::uint32_t>(indexCount_);
    for (std::size_t index = 0; index < positions.size(); ++index) {
        positions[index].x = positions[index].x / 320.0F - 1.0F;
        positions[index].y = positions[index].y / 180.0F - 1.0F;
        vertices_[vertexCount_++] = {positions[index], {0, 0, 1},
            textured ? Vec2{uvTransform.x + uv[index].x * uvTransform.z,
                            uvTransform.y + uv[index].y * uvTransform.w} : Vec2{}, tint,
            {1, 0, 0, 1}};
    }
    for (auto index : {0U, 1U, 2U, 2U, 3U, 0U}) {
        indices_[indexCount_++] = firstVertex + index;
    }
    if (merge) commands_[commandCount_ - 1].indexCount += 6;
    else commands_[commandCount_++] = {material, firstIndex, 6, worldPass_};
    return true;
}

} // namespace echo::rendering
