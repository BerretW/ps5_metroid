#pragma once

#include "game/game.hpp"
#include "echo/framework/canvas2d.hpp"
#include "echo/framework/vfx.hpp"
#include "input/dualsense_diagnostics.hpp"
#include "platform/sdl/inventory_menu.hpp"
#include "platform/sdl/settings_persistence.hpp"

#include <SDL.h>

#include <array>
#include <span>
#include <string_view>

namespace echo::platform {

enum class ControlScheme { Keyboard, PlayStation };

enum class Sprite { Player, Enemy, Tiles, Projectile, Pickup, Door, Count };

struct CanvasSprite {
    rendering::TextureHandle texture{};
    std::uint32_t width{}, height{};
};
using CanvasSprites = std::array<CanvasSprite, static_cast<int>(Sprite::Count)>;

struct RenderDebugInfo {
    bool vulkan{};
    bool timingAvailable{};
    bool backendStatsAvailable{};
    unsigned fps{};
    unsigned frameTenthsMs{};
    std::uint32_t drawCalls{};
    std::uint32_t triangles{};
};

class DamageFlash {
public:
    static constexpr unsigned kDurationSteps = 12;
    static constexpr std::uint8_t kMaximumOpacity = 64;

    void trigger() noexcept { remaining_ = kDurationSteps; }
    void step() noexcept { if (remaining_ != 0) --remaining_; }
    void clear() noexcept { remaining_ = 0; }
    [[nodiscard]] std::uint8_t opacity() const noexcept {
        return static_cast<std::uint8_t>(kMaximumOpacity * remaining_ / kDurationSteps);
    }

private:
    unsigned remaining_{};
};

struct CanvasWorldSprite {
    std::array<char, 96> path{};
    CanvasSprite image{};
};

struct CanvasWorldSprites {
    static constexpr std::size_t Capacity = 48;
    std::array<CanvasWorldSprite, Capacity> sheets{};
    std::size_t count{};
};

class Renderer {
public:
    explicit Renderer(ControlScheme controls) noexcept : controls_(controls) {}
    [[nodiscard]] bool create() noexcept;
    void destroy() noexcept;
    // Resizes or fullscreens the Windows window; the 640x360 logical view is kept.
    // PS5 always fills its display, so this is a no-op there.
    bool applyDisplayMode(DisplayMode mode) noexcept;
    // Options rows: Windows lists DISPLAY before BACK, PS5 omits it.
    [[nodiscard]] static constexpr int optionCount() noexcept {
#ifdef __PROSPERO__
        return 7;
#else
        return 8;
#endif
    }
    static constexpr int kDisplayOption = 5;
#ifdef __PROSPERO__
    static constexpr bool kHasDisplayOption = false;
    static constexpr int kRenderDebugOption = 5;
#else
    static constexpr bool kHasDisplayOption = true;
    static constexpr int kRenderDebugOption = 6;
#endif
    // Logical-screen point isometric shots leave from; the camera keeps the player centred.
    static constexpr float kIsometricAimOriginX = 320.0F;
    static constexpr float kIsometricAimOriginY = 182.0F;
    // Converts window pixels (e.g. the mouse) to the 640x360 logical view.
    bool windowToLogical(int windowX, int windowY, float& x, float& y) const noexcept;
    // While mouse-aiming, confines and hides the system cursor and draws the isometric
    // crosshair at the logical cursor position. No-op for the cursor on PS5.
    void setMouseAim(bool active, float logicalX, float logicalY) noexcept;
    // Loads PNG sprites from `assetDirectory` (trailing separator required).
    // Missing sprites keep the built-in rectangle art. Returns the number loaded.
    int loadSprites(const char* assetDirectory) noexcept;
    [[nodiscard]] bool hasSprite(Sprite sprite) const noexcept;
    // Loads every sprite sheet referenced by world data. Returns the number of
    // unique sheets loaded; missing sheets fall back to the built-in art.
    int loadWorldSprites(const char* assetDirectory, const WorldLayout& world) noexcept;
    [[nodiscard]] int worldSheetCount() const noexcept { return worldSheetCount_; }
    void drawMenu(int selection, bool continueAvailable = true) noexcept;
    [[nodiscard]] bool recordMenu(rendering::ICanvas2D& sink, int selection,
                                  bool continueAvailable = true) noexcept;
    void drawOptions(int selection, int volume, framework::DialogueLanguage language,
                     bool waitingForKey,
                     std::string_view jumpKey, std::string_view shootKey,
                     std::string_view interactKey,
                     DisplayMode display = DisplayMode::Window720,
                     bool renderDebug = false) noexcept;
    [[nodiscard]] bool recordOptions(rendering::ICanvas2D& sink, int selection, int volume,
                                     framework::DialogueLanguage language, bool waitingForKey,
                                     std::string_view jumpKey, std::string_view shootKey,
                                     std::string_view interactKey,
                                     DisplayMode display = DisplayMode::Window720,
                                     bool renderDebug = false) noexcept;
    void drawGame(const Game& game, bool showMap, bool paused,
                  std::string_view jumpKey, std::string_view shootKey,
                  std::string_view interactKey,
                  std::span<const vfx::Particle> particles = {},
                  const RenderDebugInfo* debug = nullptr,
                  std::uint8_t damageFlashOpacity = 0) noexcept;
    [[nodiscard]] bool recordGame(rendering::ICanvas2D& sink, const Game& game, bool showMap, bool paused,
                                  std::string_view jumpKey, std::string_view shootKey,
                                  std::string_view interactKey,
                                  const CanvasSprites* sprites = nullptr,
                                  const CanvasWorldSprites* worldSprites = nullptr,
                                  std::span<const vfx::Particle> particles = {},
                                  const RenderDebugInfo* debug = nullptr,
                                  std::uint8_t damageFlashOpacity = 0) noexcept;
    void drawInventory(const Game& game, const InventoryMenu& menu,
                       std::string_view message) noexcept;
    [[nodiscard]] bool recordInventory(rendering::ICanvas2D& sink, const Game& game,
                                       const InventoryMenu& menu, std::string_view message) noexcept;
    void drawDualSenseLab(const input::ControllerSnapshot& controller,
                          int connectedControllers) noexcept;
    [[nodiscard]] bool recordDualSenseLab(rendering::ICanvas2D& sink,
                                          const input::ControllerSnapshot& controller,
                                          int connectedControllers) noexcept;

private:
    struct WorldSheet {
        std::array<char, 96> path{};
        SDL_Texture* texture{};
        int width{};
        int height{};
    };
    static constexpr int kMaxWorldSheets = 48;

    void text(int x, int y, std::string_view value, int scale, SDL_Color color) noexcept;
    void panel(SDL_Rect bounds, SDL_Color color) noexcept;
    void fillRecordedOrSdl(SDL_Rect bounds, SDL_Color color) noexcept;
    bool beginRecording(rendering::ICanvas2D& sink, SDL_Color background) noexcept;
    bool endRecording() noexcept;
    void drawMenuContent(int selection, bool continueAvailable) noexcept;
    void drawOptionsContent(int selection, int volume, framework::DialogueLanguage language,
                            bool waitingForKey, std::string_view jumpKey,
                            std::string_view shootKey, std::string_view interactKey,
                            DisplayMode display, bool renderDebug) noexcept;
    void drawGameContent(const Game& game, bool showMap, bool paused,
                         std::string_view jumpKey, std::string_view shootKey,
                         std::string_view interactKey,
                         std::span<const vfx::Particle> particles,
                         const RenderDebugInfo* debug,
                         std::uint8_t damageFlashOpacity) noexcept;
    void drawParticles(const Game& game, std::span<const vfx::Particle> particles) noexcept;
    void drawInventoryContent(const Game& game, const InventoryMenu& menu,
                              std::string_view message) noexcept;
    void drawDualSenseLabContent(const input::ControllerSnapshot& controller,
                                 int connectedControllers) noexcept;
    void outline(SDL_Rect bounds, SDL_Color color) noexcept;
    bool sprite(Sprite sprite, int frame, SDL_Rect destination, bool flip = false) noexcept;
    void releaseSprites() noexcept;
    void releaseWorldSprites() noexcept;
    [[nodiscard]] const WorldSheet* worldSheet(std::string_view path) const noexcept;
    [[nodiscard]] const CanvasSprite* canvasWorldSprite(std::string_view path) const noexcept;
    bool loadWorldSheet(const char* assetDirectory, std::string_view path) noexcept;
    bool drawObject(const framework::ObjectDefinition* object, std::string_view animation,
                    Uint64 ticks, Rect bounds, float cameraX, bool flip) noexcept;
    bool drawRoomLayers(const RoomLayout& room, float cameraX) noexcept;
    void drawSideWorld(const Game& game, bool paused) noexcept;
    // Diablo-style 2:1 projection with depth-sorted blocks, actors and shots.
    void drawIsometricWorld(const Game& game, bool paused) noexcept;
    ControlScheme controls_;
    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    std::array<SDL_Texture*, static_cast<int>(Sprite::Count)> sprites_{};
    std::array<WorldSheet, kMaxWorldSheets> worldSheets_{};
    int worldSheetCount_{};
    bool mouseAim_{};
    SDL_FPoint mouseAimPoint_{};
    rendering::ICanvas2D* drawSink_{};
    const CanvasSprites* canvasSprites_{};
    const CanvasWorldSprites* canvasWorldSprites_{};
    bool drawSinkFailed_{};
};

} // namespace echo::platform
