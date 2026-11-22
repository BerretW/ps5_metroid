#pragma once

#include "echo/framework/rendering.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace echo::rendering {

struct Color2D {
    std::uint8_t r{}, g{}, b{}, a{255};
};

struct Rect2D {
    int x{}, y{}, w{}, h{};
};

[[nodiscard]] inline bool logicalViewport(int width, int height, int logicalWidth,
                                          int logicalHeight, Rect2D& viewport) noexcept {
    if (width <= 0 || height <= 0 || logicalWidth <= 0 || logicalHeight <= 0) return false;
    float scale = std::min(static_cast<float>(width) / logicalWidth,
                           static_cast<float>(height) / logicalHeight);
    if (scale >= 1.0F) scale = std::floor(scale);
    const int viewWidth = static_cast<int>(logicalWidth * scale);
    const int viewHeight = static_cast<int>(logicalHeight * scale);
    if (viewWidth <= 0 || viewHeight <= 0) return false;
    viewport = {(width - viewWidth) / 2, (height - viewHeight) / 2, viewWidth, viewHeight};
    return true;
}

[[nodiscard]] inline bool windowToLogical(int windowX, int windowY, int windowWidth,
                                          int windowHeight, int drawableWidth, int drawableHeight,
                                          int logicalWidth, int logicalHeight,
                                          float& x, float& y) noexcept {
    Rect2D viewport{};
    if (windowWidth <= 0 || windowHeight <= 0 ||
        !logicalViewport(drawableWidth, drawableHeight, logicalWidth, logicalHeight, viewport))
        return false;
    const float pixelX = static_cast<float>(windowX) * drawableWidth / windowWidth;
    const float pixelY = static_cast<float>(windowY) * drawableHeight / windowHeight;
    x = (pixelX - viewport.x) * logicalWidth / viewport.w;
    y = (pixelY - viewport.y) * logicalHeight / viewport.h;
    return true;
}

// Coordinates use the caller's logical viewport. UVs are normalized, tint is linear RGBA,
// rotation is clockwise in degrees, and operations preserve submission order.
class ICanvas2D {
public:
    virtual ~ICanvas2D() = default;
    [[nodiscard]] virtual bool clear(Color2D color) noexcept = 0;
    [[nodiscard]] virtual bool setWorldLighting(const FrameState&) noexcept { return true; }
    [[nodiscard]] virtual bool setWorldPass(bool) noexcept { return true; }
    [[nodiscard]] virtual bool fill(Rect2D bounds, Color2D color) noexcept = 0;
    [[nodiscard]] virtual bool texture(TextureHandle texture, Rect2D bounds,
                                       Vec4 uvTransform, Vec4 tint,
                                       float rotationDegrees = 0.0F) noexcept = 0;
    [[nodiscard]] virtual bool textureWithLighting(
        TextureHandle texture, TextureHandle normalTexture, TextureHandle emissiveMask,
        Vec3 emission, Rect2D bounds, Vec4 uvTransform, Vec4 tint,
        float rotationDegrees = 0.0F) noexcept {
        (void)normalTexture;
        (void)emissiveMask;
        (void)emission;
        return this->texture(texture, bounds, uvTransform, tint, rotationDegrees);
    }
};

} // namespace echo::rendering
