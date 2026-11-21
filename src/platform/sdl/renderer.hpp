#pragma once

#include "game/game.hpp"
#include "input/dualsense_diagnostics.hpp"
#include "platform/sdl/inventory_menu.hpp"

#include <SDL.h>

#include <array>
#include <string_view>

namespace echo::platform {

enum class ControlScheme { Keyboard, PlayStation };

enum class Sprite { Player, Enemy, Tiles, Projectile, Pickup, Door, Count };

class Renderer {
public:
    explicit Renderer(ControlScheme controls) noexcept : controls_(controls) {}
    [[nodiscard]] bool create() noexcept;
    void destroy() noexcept;
    // Loads PNG sprites from `assetDirectory` (trailing separator required).
    // Missing sprites keep the built-in rectangle art. Returns the number loaded.
    int loadSprites(const char* assetDirectory) noexcept;
    [[nodiscard]] bool hasSprite(Sprite sprite) const noexcept;
    void drawMenu(int selection, bool continueAvailable = true) noexcept;
    void drawOptions(int selection, int volume, framework::DialogueLanguage language,
                     bool waitingForKey,
                     std::string_view jumpKey, std::string_view shootKey,
                     std::string_view interactKey) noexcept;
    void drawGame(const Game& game, bool showMap, bool paused,
                  std::string_view jumpKey, std::string_view shootKey,
                  std::string_view interactKey) noexcept;
    void drawInventory(const Game& game, const InventoryMenu& menu,
                       std::string_view message) noexcept;
    void drawDualSenseLab(const input::ControllerSnapshot& controller,
                          int connectedControllers) noexcept;

private:
    void text(int x, int y, std::string_view value, int scale, SDL_Color color) noexcept;
    void panel(SDL_Rect bounds, SDL_Color color) noexcept;
    bool sprite(Sprite sprite, int frame, SDL_Rect destination, bool flip = false) noexcept;
    void releaseSprites() noexcept;
    ControlScheme controls_;
    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    std::array<SDL_Texture*, static_cast<int>(Sprite::Count)> sprites_{};
};

} // namespace echo::platform
