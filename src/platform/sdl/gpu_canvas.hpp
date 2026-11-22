#pragma once

#include "echo/framework/canvas_batch.hpp"
#include "platform/sdl/canvas_assets.hpp"

#include <memory>

namespace echo::platform {

class GpuCanvas {
public:
    explicit GpuCanvas(rendering::IRenderBackend& backend) noexcept : backend_(backend) {}
    [[nodiscard]] rendering::Status initialize() noexcept;
    [[nodiscard]] rendering::Status loadSprites(const char* assetRoot) noexcept;
    [[nodiscard]] rendering::Status loadWorldSprites(const char* assetRoot,
                                                      const WorldLayout& world) noexcept;
    [[nodiscard]] rendering::CanvasBatch2D& batch() noexcept { return *batch_; }
    [[nodiscard]] const CanvasSprites& sprites() const noexcept { return sprites_; }
    [[nodiscard]] const CanvasWorldSprites& worldSprites() const noexcept { return worldSprites_; }
    [[nodiscard]] rendering::Status beginFrame() noexcept;
    [[nodiscard]] rendering::Status endFrame() noexcept;
    [[nodiscard]] rendering::Status shutdown() noexcept;

private:
    rendering::IRenderBackend& backend_;
    std::unique_ptr<rendering::CanvasBatch2D> batch_{};
    rendering::TextureHandle white_{};
    rendering::MaterialHandle fill_{};
    rendering::MeshHandle mesh_{};
    CanvasSprites sprites_{};
    CanvasWorldSprites worldSprites_{};
};

} // namespace echo::platform
