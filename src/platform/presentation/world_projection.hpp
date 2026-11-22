#pragma once

#include "game/game.hpp"
#include "echo/framework/rendering.hpp"

#include <cmath>
#include <cstdint>

namespace echo::platform {

[[nodiscard]] inline rendering::Vec2 projectIsometric(float x, float y) noexcept {
    return {x - y, (x + y) * 0.5F};
}

[[nodiscard]] inline int playerAnimationFrame(const Player& player, std::uint64_t ticks) noexcept {
    if (!player.onGround) return player.velocity.y < 0.0F ? 6 : 7;
    if (std::abs(player.velocity.x) > 20.0F)
        return 2 + static_cast<int>((ticks / 90U) % 4U);
    return static_cast<int>((ticks / 500U) % 2U);
}

} // namespace echo::platform
