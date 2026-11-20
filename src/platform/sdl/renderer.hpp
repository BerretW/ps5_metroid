#pragma once

#include "game/game.hpp"
#include "input/dualsense_diagnostics.hpp"

#include <SDL.h>

#include <string_view>

namespace echo::platform {

enum class ControlScheme { Keyboard, PlayStation };

class Renderer {
public:
    explicit Renderer(ControlScheme controls) noexcept : controls_(controls) {}
    [[nodiscard]] bool create() noexcept;
    void destroy() noexcept;
    void drawMenu(int selection) noexcept;
    void drawOptions(int selection, int volume, bool waitingForKey,
                     std::string_view jumpKey, std::string_view shootKey,
                     std::string_view interactKey) noexcept;
    void drawGame(const Game& game, bool showMap, bool paused,
                  std::string_view jumpKey, std::string_view shootKey,
                  std::string_view interactKey) noexcept;
    void drawDualSenseLab(const input::ControllerSnapshot& controller,
                          int connectedControllers) noexcept;

private:
    void text(int x, int y, std::string_view value, int scale, SDL_Color color) noexcept;
    void panel(SDL_Rect bounds, SDL_Color color) noexcept;
    ControlScheme controls_;
    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
};

} // namespace echo::platform
