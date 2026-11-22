#pragma once

#include "echo/framework/gltf.hpp"

#include <array>

namespace echo::platform {

class MenuGlyph {
public:
    explicit MenuGlyph(rendering::IRenderBackend& backend) noexcept : backend_(backend) {}
    [[nodiscard]] rendering::Status load(const char* assetRoot) noexcept;
    [[nodiscard]] rendering::Status submit(float seconds) noexcept;
    [[nodiscard]] rendering::Status shutdown() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const rendering::FrameState& frame() const noexcept { return frame_; }

private:
    rendering::IRenderBackend& backend_;
    rendering::FrameState frame_{};
    rendering::MeshHandle mesh_{};
    std::array<rendering::MaterialHandle, 17> materials_{};
    std::array<rendering::TextureHandle, 49> textures_{};
    std::array<rendering::Submesh, 32> submeshes_{};
    std::array<std::uint32_t, 32> primitiveMaterials_{};
    std::size_t materialCount_{}, textureCount_{}, submeshCount_{};
    rendering::Vec3 center_{};
    float scale_{1.0F};
    bool ready_{};
};

} // namespace echo::platform
