#pragma once

#include "game/game.hpp"
#include "echo/framework/canvas2d.hpp"
#include "echo/framework/power.hpp"
#include "echo/framework/vfx.hpp"
#include "input/dualsense_diagnostics.hpp"
#include "platform/presentation.hpp"
#include "platform/presentation/presentation_types.hpp"
#include "platform/sdl/inventory_menu.hpp"

#include <SDL.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace echo::platform {

class Renderer {
public:
    explicit Renderer(ControlScheme controls) noexcept : controls_(controls) {}
    void setPowerModule(const framework::PowerModule* power) noexcept {
        powerModule_ = power;
    }
    void setGameState(const framework::GameState* state) noexcept { gameState_ = state; }
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
    void loadMenuGlyph(const char* assetDirectory) noexcept;
    [[nodiscard]] int worldSheetCount() const noexcept { return worldSheetCount_; }
    void drawMenu(int selection, bool continueAvailable = true) noexcept;
    [[nodiscard]] bool recordMenu(rendering::ICanvas2D& sink, int selection,
                                  bool continueAvailable = true, bool modelWindow = false) noexcept;
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
                  std::uint8_t damageFlashOpacity = 0,
                  std::span<const vfx::Particle> impacts = {},
                  std::span<const vfx::Particle> explosions = {},
                  std::span<const vfx::Particle> fog = {}) noexcept;
    [[nodiscard]] bool recordGame(rendering::ICanvas2D& sink, const Game& game, bool showMap, bool paused,
                                  std::string_view jumpKey, std::string_view shootKey,
                                  std::string_view interactKey,
                                  const CanvasSprites* sprites = nullptr,
                                  const CanvasWorldSprites* worldSprites = nullptr,
                                  std::span<const vfx::Particle> particles = {},
                                  const RenderDebugInfo* debug = nullptr,
                                  std::uint8_t damageFlashOpacity = 0,
                                  std::span<const vfx::Particle> impacts = {},
                                  std::span<const vfx::Particle> explosions = {},
                                  std::span<const vfx::Particle> fog = {}) noexcept;
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
    struct StaticTileShadowEdge {
        std::uint16_t ax{}, ay{}, bx{}, by{};
    };
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
    void drawMenuContent(int selection, bool continueAvailable, bool modelWindow = false) noexcept;
    void drawOptionsContent(int selection, int volume, framework::DialogueLanguage language,
                            bool waitingForKey, std::string_view jumpKey,
                            std::string_view shootKey, std::string_view interactKey,
                            DisplayMode display, bool renderDebug) noexcept;
    void drawGameContent(const Game& game, bool showMap, bool paused,
                         std::string_view jumpKey, std::string_view shootKey,
                         std::string_view interactKey,
                         std::span<const vfx::Particle> particles,
                         const RenderDebugInfo* debug,
                         std::uint8_t damageFlashOpacity,
                         std::span<const vfx::Particle> impacts,
                         std::span<const vfx::Particle> explosions,
                         std::span<const vfx::Particle> fog) noexcept;
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
    [[nodiscard]] const CanvasWorldSprite* canvasWorldSprite(
        std::string_view path, const framework::WorldMaterialSpec& material) const noexcept;
    [[nodiscard]] bool drawWorldTexture(
        const CanvasWorldSprite& sheet, const framework::WorldMaterialSpec& material,
        rendering::Rect2D bounds, rendering::Vec4 uv, rendering::Vec4 tint,
        float rotationDegrees = 0.0F) noexcept;
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
    SDL_Texture* menuGlyph_{};
    std::array<SDL_Texture*, static_cast<int>(Sprite::Count)> sprites_{};
    std::array<WorldSheet, kMaxWorldSheets> worldSheets_{};
    int worldSheetCount_{};
    bool mouseAim_{};
    SDL_FPoint mouseAimPoint_{};
    rendering::ICanvas2D* drawSink_{};
    const framework::PowerModule* powerModule_{};
    const framework::GameState* gameState_{};
    const CanvasSprites* canvasSprites_{};
    const CanvasWorldSprites* canvasWorldSprites_{};
    bool drawSinkFailed_{};
    bool drawingWorld_{};
    bool lightOverflowLogged_{};
    bool powerQueryFailureLogged_{};
    bool lightStateWarningLogged_{};
    bool lightFadeFailureLogged_{};
    bool shadowOverflowLogged_{};
    const framework::RoomDefinition* lightFadeRoom_{};
    std::array<framework::LightFadeTransition, framework::kMaxRoomLights> lightFades_{};
    const framework::RoomDefinition* shadowGeometryRoom_{};
    std::unique_ptr<StaticTileShadowEdge[]> staticTileShadowEdges_{};
    std::size_t staticTileShadowEdgeCount_{};
    Uint64 lightFadeTicks_{};
    int worldOffsetX_{};
    int worldOffsetY_{};
};

} // namespace echo::platform
