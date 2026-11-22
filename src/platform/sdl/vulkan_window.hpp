#pragma once

#include "echo/framework/canvas2d.hpp"
#include "platform/sdl/settings_persistence.hpp"

#include <SDL.h>
#include <SDL_vulkan.h>

namespace echo::platform {

[[nodiscard]] inline bool applyVulkanDisplayMode(SDL_Window* window, DisplayMode mode) noexcept {
    if (window == nullptr || mode >= DisplayMode::Count) return false;
    if (mode == DisplayMode::Fullscreen)
        return SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) == 0;
    if (SDL_SetWindowFullscreen(window, 0) != 0) return false;
    const bool large = mode == DisplayMode::Window1080;
    SDL_SetWindowSize(window, large ? 1920 : 1280, large ? 1080 : 720);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    return true;
}

[[nodiscard]] inline bool vulkanWindowToLogical(SDL_Window* window, int windowX, int windowY,
                                                float& x, float& y) noexcept {
    if (window == nullptr) return false;
    int width{}, height{}, drawableWidth{}, drawableHeight{};
    SDL_GetWindowSize(window, &width, &height);
    SDL_Vulkan_GetDrawableSize(window, &drawableWidth, &drawableHeight);
    return rendering::windowToLogical(windowX, windowY, width, height,
                                      drawableWidth, drawableHeight, 640, 360, x, y);
}

} // namespace echo::platform
