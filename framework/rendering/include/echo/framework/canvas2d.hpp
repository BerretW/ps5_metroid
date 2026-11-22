#pragma once

#include "echo/framework/rendering.hpp"

#include <cstdint>

namespace echo::rendering {

struct Color2D {
    std::uint8_t r{}, g{}, b{}, a{255};
};

struct Rect2D {
    int x{}, y{}, w{}, h{};
};

// Coordinates use the caller's logical viewport. UVs are normalized, tint is linear RGBA,
// rotation is clockwise in degrees, and operations preserve submission order.
class ICanvas2D {
public:
    virtual ~ICanvas2D() = default;
    [[nodiscard]] virtual bool clear(Color2D color) noexcept = 0;
    [[nodiscard]] virtual bool fill(Rect2D bounds, Color2D color) noexcept = 0;
    [[nodiscard]] virtual bool texture(TextureHandle texture, Rect2D bounds,
                                       Vec4 uvTransform, Vec4 tint,
                                       float rotationDegrees = 0.0F) noexcept = 0;
};

} // namespace echo::rendering
