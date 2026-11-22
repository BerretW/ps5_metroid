#pragma once

#include "platform/sdl/renderer.hpp"

namespace echo::platform {

// Successful uploads belong to the caller, including any completed before a failure.
[[nodiscard]] bool uploadCanvasTexture(rendering::IRenderBackend& backend, const char* path,
                                       rendering::TextureHandle& texture,
                                       std::uint32_t& width, std::uint32_t& height,
                                       rendering::TextureColorSpace colorSpace =
                                           rendering::TextureColorSpace::Srgb) noexcept;
[[nodiscard]] bool loadCanvasSprites(rendering::IRenderBackend& backend, const char* assetRoot,
                                     CanvasSprites& sprites) noexcept;
[[nodiscard]] bool loadCanvasWorldSprites(rendering::IRenderBackend& backend, const char* assetRoot,
                                          const WorldLayout& world, CanvasWorldSprites& sprites) noexcept;

} // namespace echo::platform
