#pragma once

#include "echo/framework/canvas2d.hpp"

#include <array>

namespace echo::rendering {

// Records a 640x360 canvas into a bounded dynamic mesh and ordered overlay draws.
// The backend and fill material must outlive the batch. Destroy cached materials
// before destroying their textures or the backend.
class CanvasBatch2D final : public ICanvas2D {
public:
    explicit CanvasBatch2D(IRenderBackend& backend) noexcept : backend_(backend) {}
    void setFillMaterial(MaterialHandle material) noexcept { fillMaterial_ = material; }

    [[nodiscard]] bool clear(Color2D color) noexcept override;
    [[nodiscard]] bool setWorldLighting(const FrameState& frame) noexcept override;
    [[nodiscard]] bool setWorldPass(bool enabled) noexcept override {
        worldPass_ = enabled;
        return true;
    }
    [[nodiscard]] bool fill(Rect2D bounds, Color2D color) noexcept override;
    [[nodiscard]] bool texture(TextureHandle texture, Rect2D bounds, Vec4 uvTransform,
                               Vec4 tint, float rotationDegrees = 0.0F) noexcept override;
    [[nodiscard]] bool textureWithLighting(
        TextureHandle texture, TextureHandle normalTexture, TextureHandle emissiveMask,
        Vec3 emission, Rect2D bounds, Vec4 uvTransform, Vec4 tint,
        float rotationDegrees = 0.0F) noexcept override;
    [[nodiscard]] MeshView mesh() const noexcept;
    [[nodiscard]] Vec4 background() const noexcept { return background_; }
    [[nodiscard]] const FrameState& lighting() const noexcept { return lighting_; }
    [[nodiscard]] Status flushMesh(MeshHandle mesh) noexcept;
    [[nodiscard]] Status submit(MeshHandle mesh) const noexcept;
    [[nodiscard]] Status destroyMaterials() noexcept;

private:
    struct Command {
        MaterialHandle material{};
        std::uint32_t firstIndex{}, indexCount{};
        bool worldPass{};
    };
    struct TextureMaterial {
        TextureHandle texture{};
        TextureHandle normalTexture{};
        TextureHandle emissiveMask{};
        Vec3 emission{};
        MaterialHandle material{};
    };

    [[nodiscard]] bool materialFor(TextureHandle texture, TextureHandle normalTexture,
                                   TextureHandle emissiveMask, Vec3 emission,
                                   MaterialHandle& output) noexcept;
    [[nodiscard]] bool quad(Rect2D bounds, Vec4 tint, Vec4 uvTransform,
                            MaterialHandle material, bool textured, float rotationDegrees) noexcept;

    IRenderBackend& backend_;
    std::array<Vertex, kMaxMeshVertices> vertices_{};
    std::array<std::uint32_t, kMaxMeshIndices> indices_{};
    std::array<Command, 512> commands_{};
    std::array<TextureMaterial, 64> materials_{};
    std::size_t vertexCount_{}, indexCount_{}, commandCount_{}, materialCount_{};
    MaterialHandle fillMaterial_{};
    Vec4 background_{};
    FrameState lighting_{};
    bool worldPass_{};
};

} // namespace echo::rendering
