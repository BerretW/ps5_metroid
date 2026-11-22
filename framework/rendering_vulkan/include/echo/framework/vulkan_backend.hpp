#pragma once
#include "echo/framework/rendering.hpp"

namespace echo::rendering {

class VulkanBackend final : public IRenderBackend {
public:
    VulkanBackend() noexcept;
    ~VulkanBackend() override;
    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;
    Status initialize(const BackendInit&) noexcept override;
    Capabilities capabilities() const noexcept override;
    Status createMesh(MeshView, MeshHandle&) noexcept override;
    Status createDynamicMesh(std::size_t vertexCapacity, std::size_t indexCapacity,
                             MeshHandle&) noexcept override;
    Status updateDynamicMesh(MeshHandle, MeshView) noexcept override;
    Status createTexture(std::uint32_t, std::uint32_t, std::span<const std::byte>,
                         TextureHandle&, TextureColorSpace = TextureColorSpace::Srgb) noexcept override;
    Status createMaterial(const Material&, MaterialHandle&) noexcept override;
    Status destroyMesh(MeshHandle) noexcept override;
    Status destroyTexture(TextureHandle) noexcept override;
    Status destroyMaterial(MaterialHandle) noexcept override;
    Status createTarget(std::uint32_t, std::uint32_t, TargetHandle&) noexcept override;
    Status beginFrame(const FrameState&) noexcept override;
    Status submit(const Draw&) noexcept override;
    Status endFrame() noexcept override;
    Status resize(std::uint32_t, std::uint32_t) noexcept override;
    Status captureNextFrame(const char* ppmPath) noexcept;
    Diagnostics diagnostics() const noexcept override;
    void shutdown() noexcept override;
private:
    struct State;
    State* state_{};
};

}
