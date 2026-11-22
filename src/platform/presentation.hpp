#pragma once

#include "echo/framework/power.hpp"
#include "echo/framework/rendering.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace echo::platform {

enum class Sprite { Player, Enemy, Tiles, Projectile, Pickup, Door, Count };

struct CanvasSprite {
    rendering::TextureHandle texture{};
    std::uint32_t width{};
    std::uint32_t height{};
};

using CanvasSprites = std::array<CanvasSprite, static_cast<std::size_t>(Sprite::Count)>;

struct CanvasWorldSprite {
    std::array<char, 96> path{};
    CanvasSprite image{};
    CanvasSprite normal{};
    CanvasSprite emissive{};
    framework::WorldMaterialSpec material{};
};

struct CanvasWorldSprites {
    static constexpr std::size_t Capacity = 48;
    std::array<CanvasWorldSprite, Capacity> sheets{};
    std::size_t count{};
};

} // namespace echo::platform
