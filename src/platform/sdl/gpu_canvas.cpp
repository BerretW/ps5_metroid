#include "platform/sdl/gpu_canvas.hpp"

#include <algorithm>
#include <array>
#include <new>

namespace echo::platform {
namespace {

rendering::Status canvasError(rendering::ErrorCode code, const char* detail) noexcept {
    return {code, "echo.platform.gpu_canvas", detail};
}

} // namespace

rendering::Status GpuCanvas::initialize() noexcept {
    if (batch_ || white_.valid() || fill_.valid() || mesh_.valid())
        return canvasError(rendering::ErrorCode::InvalidState, "canvas already initialized");
    constexpr std::array<std::byte, 4> white{
        std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    auto status = backend_.createTexture(1, 1, white, white_);
    if (!status) return status;
    rendering::Material material{};
    material.model = rendering::MaterialModel::VertexColor;
    material.alpha = rendering::AlphaMode::Blend;
    material.texture = white_;
    status = backend_.createMaterial(material, fill_);
    if (!status) return status;
    status = backend_.createDynamicMesh(rendering::kMaxMeshVertices,
                                        rendering::kMaxMeshIndices, mesh_);
    if (!status) return status;
    batch_.reset(new (std::nothrow) rendering::CanvasBatch2D(backend_));
    if (!batch_) return canvasError(rendering::ErrorCode::CapacityExceeded, "canvas allocation failed");
    batch_->setFillMaterial(fill_);
    return rendering::Status::success();
}

rendering::Status GpuCanvas::loadSprites(const char* assetRoot) noexcept {
    if (!batch_) return canvasError(rendering::ErrorCode::InvalidState, "canvas not initialized");
    if (std::any_of(sprites_.begin(), sprites_.end(),
                    [](const CanvasSprite& sprite) { return sprite.texture.valid(); }))
        return canvasError(rendering::ErrorCode::InvalidState, "sprites already loaded");
    if (!loadCanvasSprites(backend_, assetRoot, sprites_))
        return canvasError(rendering::ErrorCode::StorageFailure, "sprite upload failed; see SDL log");
    return rendering::Status::success();
}

rendering::Status GpuCanvas::loadWorldSprites(const char* assetRoot, const WorldLayout& world) noexcept {
    if (!batch_) return canvasError(rendering::ErrorCode::InvalidState, "canvas not initialized");
    if (worldSprites_.count != 0)
        return canvasError(rendering::ErrorCode::InvalidState, "world sprites already loaded");
    if (!loadCanvasWorldSprites(backend_, assetRoot, world, worldSprites_))
        return canvasError(rendering::ErrorCode::StorageFailure, "world sprite upload failed; see SDL log");
    return rendering::Status::success();
}

rendering::Status GpuCanvas::beginFrame(rendering::FrameState frame) noexcept {
    if (!batch_) return canvasError(rendering::ErrorCode::InvalidState, "canvas not initialized");
    auto status = batch_->flushMesh(mesh_);
    if (!status) return status;
    frame.clearColor = batch_->background();
    return backend_.beginFrame(frame);
}

rendering::Status GpuCanvas::endFrame() noexcept {
    if (!batch_) return canvasError(rendering::ErrorCode::InvalidState, "canvas not initialized");
    auto status = batch_->submit(mesh_);
    return status ? backend_.endFrame() : status;
}

rendering::Status GpuCanvas::shutdown() noexcept {
    rendering::Status status = rendering::Status::success();
    if (batch_) {
        status = batch_->destroyMaterials();
        if (!status) return status;
        batch_.reset();
    }
    if (fill_.valid()) {
        status = backend_.destroyMaterial(fill_);
        if (!status) return status;
        fill_ = {};
    }
    if (mesh_.valid()) {
        status = backend_.destroyMesh(mesh_);
        if (!status) return status;
        mesh_ = {};
    }
    for (auto& sprite : sprites_) {
        if (sprite.texture.valid()) {
            status = backend_.destroyTexture(sprite.texture);
            if (!status) return status;
            sprite = {};
        }
    }
    for (std::size_t index = 0; index < worldSprites_.count; ++index) {
        auto& sprite = worldSprites_.sheets[index].image;
        if (sprite.texture.valid()) {
            status = backend_.destroyTexture(sprite.texture);
            if (!status) return status;
            sprite = {};
        }
    }
    worldSprites_ = {};
    if (white_.valid()) {
        status = backend_.destroyTexture(white_);
        if (!status) return status;
        white_ = {};
    }
    return status;
}

} // namespace echo::platform
