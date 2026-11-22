#pragma once

#include "platform/sdl/renderer.hpp"

namespace echo::platform {

struct SpriteSheet {
    const char* file;
    int frameWidth;
    int frameHeight;
};

inline constexpr std::array<SpriteSheet, static_cast<int>(Sprite::Count)> kSpriteSheets{{
    {"sprites/player.png", 24, 32},
    {"sprites/enemy.png", 32, 32},
    {"sprites/tiles.png", 32, 32},
    {"sprites/projectile.png", 12, 8},
    {"sprites/pickup.png", 32, 32},
    {"sprites/door.png", 44, 48},
}};

} // namespace echo::platform
