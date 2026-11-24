#include "platform/sdl/renderer.hpp"

#include "platform/sdl/assets.hpp"
#include "platform/presentation/bitmap_text.hpp"
#include "platform/presentation/sprite_sheets.hpp"
#include "platform/presentation/ui_presentation.hpp"
#include "platform/presentation/world_projection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>
#include <string>

#if defined(__PROSPERO__) && defined(ECHO_PS5_STARTUP_DIAGNOSTICS)
extern "C" void echo_ps5_video_checkpoint(const char* message);
#define ECHO_RENDER_CHECKPOINT(...)                                         \
    do {                                                                    \
        char echoDiagnostic[192];                                           \
        std::snprintf(echoDiagnostic, sizeof(echoDiagnostic), __VA_ARGS__); \
        echo_ps5_video_checkpoint(echoDiagnostic);                          \
    } while (false)
#else
#define ECHO_RENDER_CHECKPOINT(...) \
    do {                            \
    } while (false)
#endif

namespace echo::platform {
namespace {

constexpr SDL_Color kBackground{7, 12, 22, 255};
constexpr SDL_Color kPanel{17, 29, 45, 255};
constexpr SDL_Color kPanelLight{29, 48, 65, 255};
constexpr SDL_Color kText{220, 234, 239, 255};
constexpr SDL_Color kMuted{115, 145, 158, 255};
constexpr SDL_Color kCyan{61, 227, 216, 255};
constexpr SDL_Color kOrange{255, 164, 83, 255};
constexpr SDL_Color kRed{238, 82, 92, 255};
constexpr SDL_Color kGreen{107, 221, 153, 255};

void setColor(SDL_Renderer* renderer, SDL_Color color) noexcept {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

class LegacyUiCanvas final : public rendering::ICanvas2D {
public:
    explicit LegacyUiCanvas(SDL_Renderer* renderer) noexcept : renderer_(renderer) {}
    bool clear(rendering::Color2D) noexcept override { return false; }
    bool fill(rendering::Rect2D bounds, rendering::Color2D color) noexcept override {
        const SDL_Rect rect{bounds.x, bounds.y, bounds.w, bounds.h};
        return SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a) == 0 &&
               SDL_RenderFillRect(renderer_, &rect) == 0;
    }
    bool texture(rendering::TextureHandle, rendering::Rect2D, rendering::Vec4,
                 rendering::Vec4, float) noexcept override { return false; }

private:
    SDL_Renderer* renderer_;
};

} // namespace

bool Renderer::create() noexcept {
#ifdef __PROSPERO__
    // The PS5 backend scans out a fixed display-sized surface and centres smaller windows.
    // Prefer a display-sized window so the game fills the screen, but a full-size software
    // framebuffer failed on hardware (CE-108255-1 at 06e), so fall back to the 1280x720 window
    // that is hardware-verified.
    SDL_DisplayMode display{};
    const int displayResult = SDL_GetDesktopDisplayMode(0, &display);
    const bool hasDisplay = displayResult == 0 && display.w >= 640 && display.h >= 360;
    ECHO_RENDER_CHECKPOINT("ECHO DIAG 06a: desktop mode result=%d %dx%d@%d fmt=0x%X",
                           displayResult, display.w, display.h, display.refresh_rate,
                           static_cast<unsigned>(display.format));

    // An explicit driver index makes SDL report the driver's real error instead of the
    // generic "Couldn't find matching render driver".
    int softwareIndex = -1;
    for (int index = 0; index < SDL_GetNumRenderDrivers(); ++index) {
        SDL_RendererInfo info{};
        if (SDL_GetRenderDriverInfo(index, &info) == 0 && info.name != nullptr &&
            SDL_strcmp(info.name, "software") == 0) {
            softwareIndex = index;
            break;
        }
    }

    const int sizes[2][2] = {{hasDisplay ? display.w : 1920, hasDisplay ? display.h : 1080},
                             {1280, 720}};
    for (const auto& size : sizes) {
        ECHO_RENDER_CHECKPOINT("ECHO DIAG 06b: before window %dx%d", size[0], size[1]);
        window_ = SDL_CreateWindow("PROJECT ECHO", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   size[0], size[1], 0);
        ECHO_RENDER_CHECKPOINT("ECHO DIAG 06c: window=%s %s", window_ != nullptr ? "ok" : "null",
                               window_ != nullptr ? "" : SDL_GetError());
        if (window_ == nullptr) {
            continue;
        }
        ECHO_RENDER_CHECKPOINT("ECHO DIAG 06d: before software renderer (driver %d)",
                               softwareIndex);
        renderer_ = SDL_CreateRenderer(window_, softwareIndex, SDL_RENDERER_SOFTWARE);
        ECHO_RENDER_CHECKPOINT("ECHO DIAG 06e: renderer=%s %dx%d %s",
                               renderer_ != nullptr ? "ok" : "null", size[0], size[1],
                               renderer_ != nullptr ? "" : SDL_GetError());
        if (renderer_ != nullptr) {
            break;
        }
        SDL_Log("PS5 software renderer failed for %dx%d window: %s", size[0], size[1],
                SDL_GetError());
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    if (window_ == nullptr) {
        return false;
    }
#else
    window_ = SDL_CreateWindow("PROJECT ECHO", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               1280, 720, SDL_WINDOW_RESIZABLE);
    if (window_ == nullptr) {
        return false;
    }
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer_ == nullptr) {
        SDL_Log("Accelerated renderer unavailable, using SDL software renderer: %s", SDL_GetError());
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    }
#endif
    if (renderer_ == nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }

    SDL_RenderSetLogicalSize(renderer_, 640, 360);
    SDL_RenderSetIntegerScale(renderer_, SDL_TRUE);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    ECHO_RENDER_CHECKPOINT("ECHO DIAG 06f: logical size/blend configured");
    return true;
}

void Renderer::destroy() noexcept {
    releaseSprites();
    releaseWorldSprites();
    staticTileShadowEdges_.reset();
    staticTileShadowEdgeCount_ = 0;
    shadowGeometryRoom_ = nullptr;
    if (menuGlyph_ != nullptr) {
        SDL_DestroyTexture(menuGlyph_);
        menuGlyph_ = nullptr;
    }
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

void Renderer::loadMenuGlyph(const char* assetDirectory) noexcept {
    if (menuGlyph_ != nullptr) {
        SDL_DestroyTexture(menuGlyph_);
        menuGlyph_ = nullptr;
    }
    if (renderer_ == nullptr || assetDirectory == nullptr) return;
    std::array<char, 512> path{};
    const int written = std::snprintf(path.data(), path.size(), "%smenu/glyph.png", assetDirectory);
    if (written <= 0 || written >= static_cast<int>(path.size())) {
        SDL_Log("Menu glyph path is too long");
        return;
    }
    menuGlyph_ = loadPngTexture(renderer_, path.data());
}

void Renderer::releaseSprites() noexcept {
    for (SDL_Texture*& texture : sprites_) {
        if (texture != nullptr) {
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
    }
}

bool Renderer::windowToLogical(int windowX, int windowY, float& x, float& y) const noexcept {
    if (renderer_ == nullptr) return false;
    SDL_RenderWindowToLogical(renderer_, windowX, windowY, &x, &y);
    return std::isfinite(x) && std::isfinite(y);
}

void Renderer::setMouseAim(bool active, float logicalX, float logicalY) noexcept {
    mouseAimPoint_ = {std::clamp(logicalX, 0.0F, 639.0F), std::clamp(logicalY, 0.0F, 359.0F)};
    if (active == mouseAim_) return;
    mouseAim_ = active;
#ifndef __PROSPERO__
    if (window_ != nullptr) SDL_SetWindowGrab(window_, active ? SDL_TRUE : SDL_FALSE);
    SDL_ShowCursor(active ? SDL_DISABLE : SDL_ENABLE);
#endif
}

bool Renderer::applyDisplayMode(DisplayMode mode) noexcept {
#ifdef __PROSPERO__
    (void)mode;
    return true;
#else
    if (window_ == nullptr || mode >= DisplayMode::Count) return false;
    if (mode == DisplayMode::Fullscreen) {
        return SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN_DESKTOP) == 0;
    }
    if (SDL_SetWindowFullscreen(window_, 0) != 0) return false;
    const bool large = mode == DisplayMode::Window1080;
    SDL_SetWindowSize(window_, large ? 1920 : 1280, large ? 1080 : 720);
    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    return true;
#endif
}

int Renderer::loadSprites(const char* assetDirectory) noexcept {
    releaseSprites();
    if (renderer_ == nullptr || assetDirectory == nullptr) {
        return 0;
    }
    int loaded = 0;
    for (std::size_t index = 0; index < kSpriteSheets.size(); ++index) {
        char path[512];
        const int written = std::snprintf(path, sizeof(path), "%s%s", assetDirectory, kSpriteSheets[index].file);
        if (written <= 0 || written >= static_cast<int>(sizeof(path))) {
            continue;
        }
        SDL_Texture* texture = loadPngTexture(renderer_, path);
        int width = 0;
        int height = 0;
        if (texture != nullptr &&
            (SDL_QueryTexture(texture, nullptr, nullptr, &width, &height) != 0 ||
             height != kSpriteSheets[index].frameHeight || width % kSpriteSheets[index].frameWidth != 0)) {
            SDL_Log("Sprite sheet has unexpected dimensions %dx%d: %s", width, height, path);
            SDL_DestroyTexture(texture);
            texture = nullptr;
        }
        sprites_[index] = texture;
        loaded += texture != nullptr ? 1 : 0;
    }
    return loaded;
}

bool Renderer::hasSprite(Sprite sprite) const noexcept {
    return sprites_[static_cast<std::size_t>(sprite)] != nullptr;
}

void Renderer::releaseWorldSprites() noexcept {
    for (WorldSheet& sheet : worldSheets_) {
        if (sheet.texture != nullptr) SDL_DestroyTexture(sheet.texture);
        sheet = {};
    }
    worldSheetCount_ = 0;
}

const Renderer::WorldSheet* Renderer::worldSheet(std::string_view path) const noexcept {
    for (int index = 0; index < worldSheetCount_; ++index) {
        const WorldSheet& sheet = worldSheets_[static_cast<std::size_t>(index)];
        if (std::string_view(sheet.path.data()) == path) return &sheet;
    }
    return nullptr;
}

const CanvasWorldSprite* Renderer::canvasWorldSprite(
    std::string_view path, const framework::WorldMaterialSpec& material) const noexcept {
    if (canvasWorldSprites_ == nullptr) return nullptr;
    for (std::size_t index = 0; index < canvasWorldSprites_->count; ++index) {
        const CanvasWorldSprite& sheet = canvasWorldSprites_->sheets[index];
        const framework::WorldMaterialSpec& candidate = sheet.material;
        if (std::string_view(sheet.path.data()) == path &&
            std::string_view(candidate.normalSheet.data()) ==
                std::string_view(material.normalSheet.data()) &&
            std::string_view(candidate.emissiveSheet.data()) ==
                std::string_view(material.emissiveSheet.data()) &&
            std::string_view(candidate.emissiveCircuit.data()) ==
                std::string_view(material.emissiveCircuit.data()) &&
            candidate.emissiveColor == material.emissiveColor &&
            candidate.emissiveIntensity == material.emissiveIntensity) {
            return &sheet;
        }
    }
    return nullptr;
}

bool Renderer::drawWorldTexture(const CanvasWorldSprite& sheet,
                                const framework::WorldMaterialSpec& material,
                                rendering::Rect2D bounds, rendering::Vec4 uv,
                                rendering::Vec4 tint, float rotationDegrees) noexcept {
    rendering::Vec3 emission{};
    if (material.emissiveSheet[0] != '\0') {
        bool emissive = true;
        if (material.emissiveCircuit[0] != '\0') {
            if (powerModule_ == nullptr) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "Emissive material requires an unavailable power module");
                drawSinkFailed_ = true;
                return false;
            }
            const framework::Status powerStatus =
                powerModule_->isPowered(material.emissiveCircuit.data(), emissive);
            if (!powerStatus) {
                if (!powerQueryFailureLogged_) {
                    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                 "Emissive material power query failed: %.*s",
                                 static_cast<int>(powerStatus.detail.size()),
                                 powerStatus.detail.data());
                    powerQueryFailureLogged_ = true;
                }
                drawSinkFailed_ = true;
                return false;
            }
        }
        if (emissive) {
            emission = {material.emissiveColor[0] * material.emissiveIntensity,
                        material.emissiveColor[1] * material.emissiveIntensity,
                        material.emissiveColor[2] * material.emissiveIntensity};
        }
    }
    return drawSink_->textureWithLighting(
        sheet.image.texture, sheet.normal.texture, sheet.emissive.texture, emission,
        bounds, uv, tint, rotationDegrees);
}

bool Renderer::loadWorldSheet(const char* assetDirectory, std::string_view path) noexcept {
    if (path.empty() || worldSheet(path) != nullptr) return true;
    if (worldSheetCount_ == kMaxWorldSheets || path.size() >= WorldSheet{}.path.size() ||
        !framework::WorldModule::validSpritePath(path)) {
        return false;
    }
    char fullPath[512];
    const int written = std::snprintf(fullPath, sizeof(fullPath), "%s%.*s", assetDirectory,
                                      static_cast<int>(path.size()), path.data());
    if (written <= 0 || written >= static_cast<int>(sizeof(fullPath))) return false;
    ECHO_RENDER_CHECKPOINT("ECHO DIAG 08s: sheet %d %s", worldSheetCount_, fullPath);
    SDL_Texture* texture = loadPngTexture(renderer_, fullPath);
    int width = 0;
    int height = 0;
    if (texture == nullptr ||
        SDL_QueryTexture(texture, nullptr, nullptr, &width, &height) != 0) {
        if (texture != nullptr) SDL_DestroyTexture(texture);
        ECHO_RENDER_CHECKPOINT("ECHO DIAG 08s: sheet failed %s", SDL_GetError());
        SDL_Log("World sprite sheet could not be loaded: %s", fullPath);
        return false;
    }
    WorldSheet& sheet = worldSheets_[static_cast<std::size_t>(worldSheetCount_++)];
    std::copy(path.begin(), path.end(), sheet.path.begin());
    sheet.texture = texture;
    sheet.width = width;
    sheet.height = height;
    return true;
}

int Renderer::loadWorldSprites(const char* assetDirectory, const WorldLayout& world) noexcept {
    releaseWorldSprites();
    if (renderer_ == nullptr || assetDirectory == nullptr) return 0;
    const auto loadObject = [&](const framework::ObjectDefinition* object) {
        if (object != nullptr) (void)loadWorldSheet(assetDirectory, object->sprite().sheet.data());
    };
    const WorldArchetypes& archetypes = world.archetypes;
    for (const framework::ObjectDefinition* object :
         {archetypes.player, archetypes.playerIso, archetypes.engineer, archetypes.pickup,
          archetypes.projectile, archetypes.enemyProjectile, archetypes.loot}) {
        loadObject(object);
    }
    for (const EnemySpawn& enemy : world.enemies) loadObject(enemy.archetype);
    for (const EnemySpawn& roamer : world.roamerList()) loadObject(roamer.archetype);
    for (const DoorLayout& door : world.doorList()) {
        if (door.visible) loadObject(door.archetype);
    }
    for (const PropLayout& prop : world.propList()) loadObject(prop.archetype);
    forEachShmupObject(world.shmup, loadObject);
    for (const RoomLayout& room : world.rooms) {
        if (room.definition == nullptr) continue;
        for (const framework::TilesetSpec& tileset : room.definition->tilesets()) {
            (void)loadWorldSheet(assetDirectory, tileset.sheet.data());
        }
    }
    return worldSheetCount_;
}

bool Renderer::drawObject(const framework::ObjectDefinition* object,
                          std::string_view animationName, Uint64 ticks, Rect bounds,
                          float cameraX, bool flip) noexcept {
    if (object == nullptr) return false;
    const framework::SpriteSheetSpec& spec = object->sprite();
    const WorldSheet* sheet = worldSheet(spec.sheet.data());
    const CanvasWorldSprite* worldImage = drawSink_ != nullptr
        ? canvasWorldSprite(spec.sheet.data(), spec.material) : nullptr;
    const CanvasSprite* image = worldImage != nullptr ? &worldImage->image : nullptr;
    const framework::AnimationSpec* animation = object->animationOrDefault(animationName);
    if ((drawSink_ != nullptr && (image == nullptr || !image->texture.valid())) ||
        (drawSink_ == nullptr && sheet == nullptr) ||
        animation == nullptr || animation->frames <= 0 ||
        spec.frameWidth <= 0 || spec.frameHeight <= 0) {
        return false;
    }
    const auto frames = static_cast<Uint64>(animation->frames);
    const Uint64 frame = animation->loop
        ? (ticks * static_cast<Uint64>(std::max(animation->fps, 0)) / 1000U) % frames
        : frames - 1U;
    const framework::AnimationCell cell = animation->cellAt(static_cast<std::int32_t>(frame));
    const SDL_Rect source{static_cast<int>(cell.column) * spec.frameWidth,
                          static_cast<int>(cell.row) * spec.frameHeight, spec.frameWidth, spec.frameHeight};
    // Sprite and animation tints multiply; rotations add, and spin advances with time.
    const std::array<float, 4> tint{spec.tint[0] * animation->tint[0], spec.tint[1] * animation->tint[1],
                                    spec.tint[2] * animation->tint[2], spec.tint[3] * animation->tint[3]};
    const float turn = std::fmod(spec.rotation + animation->rotation +
                                     animation->spin * static_cast<float>(ticks % 3600000U) / 1000.0F,
                                 360.0F);
    // Mirroring flips the rotation direction so the sprite still matches the collision box.
    const float angle = flip ? -turn : turn;
    const int sheetWidth = drawSink_ != nullptr ? static_cast<int>(image->width) : sheet->width;
    const int sheetHeight = drawSink_ != nullptr ? static_cast<int>(image->height) : sheet->height;
    if (source.x < 0 || source.y < 0 || source.x + source.w > sheetWidth ||
        source.y + source.h > sheetHeight) {
        return false;
    }
    // Mirroring keeps the frame aligned with the collision box.
    const int offsetX = flip
        ? static_cast<int>(bounds.width) - spec.frameWidth - spec.offsetX
        : spec.offsetX;
    const SDL_Rect destination{static_cast<int>(bounds.x - cameraX) + offsetX +
                                   (drawingWorld_ ? worldOffsetX_ : 0),
                               static_cast<int>(bounds.y) + spec.offsetY +
                                   (drawingWorld_ ? worldOffsetY_ : 0),
                               spec.frameWidth, spec.frameHeight};
    if (drawSink_ != nullptr) {
        rendering::Vec4 uv{};
        const auto status = rendering::spriteUvTransform(
            image->width, image->height,
            {static_cast<std::uint32_t>(source.x), static_cast<std::uint32_t>(source.y),
             static_cast<std::uint32_t>(source.w), static_cast<std::uint32_t>(source.h),
             flip, false}, uv);
        if (!status || drawSinkFailed_ ||
            !drawWorldTexture(*worldImage, spec.material,
                              {destination.x, destination.y, destination.w, destination.h},
                              uv, {tint[0], tint[1], tint[2], tint[3]}, angle)) {
            drawSinkFailed_ = true;
            return false;
        }
        return true;
    }
    const auto channel = [](float value) {
        return static_cast<Uint8>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F);
    };
    SDL_SetTextureColorMod(sheet->texture, channel(tint[0]), channel(tint[1]), channel(tint[2]));
    SDL_SetTextureAlphaMod(sheet->texture, channel(tint[3]));
    const int drawn = SDL_RenderCopyEx(renderer_, sheet->texture, &source, &destination,
                                       static_cast<double>(angle), nullptr,
                                       flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    // Sheets are shared between objects, so the modulation must not leak.
    SDL_SetTextureColorMod(sheet->texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(sheet->texture, 255);
    return drawn == 0;
}

bool Renderer::drawRoomLayers(const RoomLayout& room, float cameraX) noexcept {
    const framework::RoomDefinition* definition = room.definition;
    if (definition == nullptr) return false;
    for (std::size_t layer = 0; layer < definition->layers().size(); ++layer) {
        const float layerCameraX = cameraX * definition->layers()[layer].scrollFactorX;
        const int firstColumn = std::max(0, static_cast<int>(layerCameraX) / kTileSize);
        const int lastColumn = std::min(room.columns - 1, firstColumn + 640 / kTileSize + 1);
        for (int row = 0; row < definition->rows(); ++row) {
            for (int column = firstColumn; column <= lastColumn; ++column) {
                const framework::TileType* tile = definition->tile(layer, column, row);
                if (tile == nullptr) continue;
                const framework::TilesetSpec& tileset = definition->tilesets()[tile->tilesetIndex];
                const WorldSheet* sheet = worldSheet(tileset.sheet.data());
                const CanvasWorldSprite* worldImage = drawSink_ != nullptr
                    ? canvasWorldSprite(tileset.sheet.data(), tileset.material) : nullptr;
                const SDL_Rect source{
                    tileset.margin + tile->column * (tileset.tileWidth + tileset.spacing),
                    tileset.margin + tile->row * (tileset.tileHeight + tileset.spacing),
                    tileset.tileWidth, tileset.tileHeight};
                const SDL_Rect destination{static_cast<int>(column * kTileSize - layerCameraX),
                                           row * kTileSize, kTileSize, kTileSize};
                const CanvasSprite* image = worldImage != nullptr ? &worldImage->image : nullptr;
                const int sheetWidth = drawSink_ != nullptr
                    ? (image != nullptr ? static_cast<int>(image->width) : 0)
                    : (sheet != nullptr ? sheet->width : 0);
                const int sheetHeight = drawSink_ != nullptr
                    ? (image != nullptr ? static_cast<int>(image->height) : 0)
                    : (sheet != nullptr ? sheet->height : 0);
                if ((drawSink_ != nullptr && (image == nullptr || !image->texture.valid())) ||
                    (drawSink_ == nullptr && sheet == nullptr) ||
                    source.x + source.w > sheetWidth || source.y + source.h > sheetHeight) {
                    // Keep collision visible when a tileset is missing.
                    if (tile->collision == framework::TileCollision::Hazard) {
                        panel(destination, SDL_Color{121, 43, 51, 255});
                    } else if (tile->collision == framework::TileCollision::Solid) {
                        panel(destination, SDL_Color{37, 61, 74, 255});
                    }
                    continue;
                }
                const int flags = (tile->flipX ? SDL_FLIP_HORIZONTAL : 0) |
                                  (tile->flipY ? SDL_FLIP_VERTICAL : 0);
                if (drawSink_ != nullptr) {
                    rendering::Vec4 uv{};
                    const auto status = rendering::spriteUvTransform(
                        image->width, image->height,
                        {static_cast<std::uint32_t>(source.x), static_cast<std::uint32_t>(source.y),
                         static_cast<std::uint32_t>(source.w), static_cast<std::uint32_t>(source.h),
                         tile->flipX, tile->flipY}, uv);
                    if (!status || drawSinkFailed_ ||
                        !drawWorldTexture(
                            *worldImage, tileset.material,
                            {destination.x + (drawingWorld_ ? worldOffsetX_ : 0),
                             destination.y + (drawingWorld_ ? worldOffsetY_ : 0),
                             destination.w, destination.h},
                            uv, {1, 1, 1, 1}, static_cast<float>(tile->rotation))) {
                        drawSinkFailed_ = true;
                    }
                    continue;
                }
                const SDL_Rect shifted{destination.x + (drawingWorld_ ? worldOffsetX_ : 0),
                                       destination.y + (drawingWorld_ ? worldOffsetY_ : 0),
                                       destination.w, destination.h};
                SDL_RenderCopyEx(renderer_, sheet->texture, &source, &shifted,
                                 static_cast<double>(tile->rotation), nullptr,
                                 static_cast<SDL_RendererFlip>(flags));
            }
        }
    }
    return true;
}

bool Renderer::sprite(Sprite sprite, int frame, SDL_Rect destination, bool flip) noexcept {
    if (drawingWorld_) {
        destination.x += worldOffsetX_;
        destination.y += worldOffsetY_;
    }
    const auto index = static_cast<std::size_t>(sprite);
    const SpriteSheet& sheet = kSpriteSheets[index];
    if (drawSink_ != nullptr) {
        if (canvasSprites_ == nullptr || !(*canvasSprites_)[index].texture.valid()) return false;
        const CanvasSprite& texture = (*canvasSprites_)[index];
        const int frames = static_cast<int>(texture.width / static_cast<std::uint32_t>(sheet.frameWidth));
        rendering::Vec4 uv{};
        const auto status = rendering::spriteUvTransform(texture.width, texture.height,
            {static_cast<std::uint32_t>((frame % frames) * sheet.frameWidth), 0,
             static_cast<std::uint32_t>(sheet.frameWidth), static_cast<std::uint32_t>(sheet.frameHeight),
             flip, false}, uv);
        if (!status || (!drawSinkFailed_ &&
            !drawSink_->texture(texture.texture, {destination.x, destination.y, destination.w, destination.h},
                                uv, {1, 1, 1, 1}, 0.0F))) {
            drawSinkFailed_ = true;
        }
        return true;
    }
    SDL_Texture* texture = sprites_[index];
    if (texture == nullptr) {
        return false;
    }
    int width = 0;
    SDL_QueryTexture(texture, nullptr, nullptr, &width, nullptr);
    const int frames = std::max(1, width / sheet.frameWidth);
    const SDL_Rect source{(frame % frames) * sheet.frameWidth, 0, sheet.frameWidth, sheet.frameHeight};
    return SDL_RenderCopyEx(renderer_, texture, &source, &destination, 0.0, nullptr,
                            flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE) == 0;
}

void Renderer::panel(SDL_Rect bounds, SDL_Color color) noexcept {
    if (drawSink_ == nullptr) setColor(renderer_, color);
    fillRecordedOrSdl(bounds, color);
}

void Renderer::fillRecordedOrSdl(SDL_Rect bounds, SDL_Color color) noexcept {
    if (drawingWorld_) {
        bounds.x += worldOffsetX_;
        bounds.y += worldOffsetY_;
    }
    if (drawSink_ != nullptr) {
        if (!drawSinkFailed_ && !drawSink_->fill({bounds.x, bounds.y, bounds.w, bounds.h},
                                                  {color.r, color.g, color.b, color.a})) {
            drawSinkFailed_ = true;
        }
    } else {
        SDL_RenderFillRect(renderer_, &bounds);
    }
}

void Renderer::text(int x, int y, std::string_view value, int scale, SDL_Color color) noexcept {
    if (drawSink_ == nullptr) setColor(renderer_, color);
    drawBitmapText(x, y, value, scale, [this, color](rendering::Rect2D bounds) noexcept {
        fillRecordedOrSdl({bounds.x, bounds.y, bounds.w, bounds.h}, color);
    });
}

void Renderer::drawMenu(int selection, bool continueAvailable) noexcept {
    SDL_SetRenderDrawColor(renderer_, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer_);
    drawMenuContent(selection, continueAvailable);
    if (menuGlyph_ != nullptr) {
        int width{}, height{};
        if (SDL_QueryTexture(menuGlyph_, nullptr, nullptr, &width, &height) != 0 ||
            width <= 0 || height <= 0) {
            SDL_Log("Menu glyph dimensions unavailable: %s", SDL_GetError());
            SDL_RenderPresent(renderer_);
            return;
        }
        const float scale = std::min(174.0F / static_cast<float>(width),
                                     140.0F / static_cast<float>(height));
        const int scaledWidth = std::max(1, static_cast<int>(width * scale));
        const int scaledHeight = std::max(1, static_cast<int>(height * scale));
        SDL_Rect destination{366 + (174 - scaledWidth) / 2, 160 + (140 - scaledHeight) / 2,
                             scaledWidth, scaledHeight};
        if (SDL_RenderCopy(renderer_, menuGlyph_, nullptr, &destination) != 0)
            SDL_Log("Menu glyph render failed: %s", SDL_GetError());
    }
    SDL_RenderPresent(renderer_);
}

bool Renderer::beginRecording(rendering::ICanvas2D& sink, SDL_Color background) noexcept {
    if (drawSink_ != nullptr) return false;
    drawSink_ = &sink;
    drawSinkFailed_ = !sink.clear({background.r, background.g, background.b, background.a});
    return true;
}

bool Renderer::endRecording() noexcept {
    drawSink_ = nullptr;
    return !drawSinkFailed_;
}

bool Renderer::recordMenu(rendering::ICanvas2D& sink, int selection, bool continueAvailable, bool modelWindow) noexcept {
    if (!beginRecording(sink, kBackground)) return false;
    if (!drawSinkFailed_) drawMenuContent(selection, continueAvailable, modelWindow);
    return endRecording();
}

void Renderer::drawMenuContent(int selection, bool continueAvailable, bool modelWindow) noexcept {
    LegacyUiCanvas legacy{renderer_};
    rendering::ICanvas2D& canvas = drawSink_ != nullptr ? *drawSink_ : legacy;
    if (!drawMainMenu(canvas, controls_, selection, continueAvailable, modelWindow)) {
        drawSinkFailed_ = true;
        if (drawSink_ == nullptr) SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Menu drawing failed: %s", SDL_GetError());
    }
}

void Renderer::drawOptions(int selection, int volume,
                           framework::DialogueLanguage language, bool waitingForKey,
                           std::string_view jumpKey, std::string_view shootKey,
                           std::string_view interactKey, DisplayMode display,
                           bool renderDebug) noexcept {
    SDL_SetRenderDrawColor(renderer_, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer_);
    drawOptionsContent(selection, volume, language, waitingForKey, jumpKey, shootKey,
                       interactKey, display, renderDebug);
    SDL_RenderPresent(renderer_);
}

bool Renderer::recordOptions(rendering::ICanvas2D& sink, int selection, int volume,
                             framework::DialogueLanguage language, bool waitingForKey,
                             std::string_view jumpKey, std::string_view shootKey,
                             std::string_view interactKey, DisplayMode display,
                             bool renderDebug) noexcept {
    if (!beginRecording(sink, kBackground)) return false;
    if (!drawSinkFailed_) {
        drawOptionsContent(selection, volume, language, waitingForKey, jumpKey, shootKey,
                           interactKey, display, renderDebug);
    }
    return endRecording();
}

void Renderer::drawOptionsContent(int selection, int volume,
                                  framework::DialogueLanguage language, bool waitingForKey,
                                  std::string_view jumpKey, std::string_view shootKey,
                                  std::string_view interactKey, DisplayMode display,
                                  bool renderDebug) noexcept {
    LegacyUiCanvas legacy{renderer_};
    rendering::ICanvas2D& canvas = drawSink_ != nullptr ? *drawSink_ : legacy;
    const OptionsPresentation options{selection, volume, language, waitingForKey,
                                      jumpKey, shootKey, interactKey, display,
                                      renderDebug, kHasDisplayOption};
    if (!drawOptionsMenu(canvas, controls_, options)) {
        drawSinkFailed_ = true;
        if (drawSink_ == nullptr) SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Options drawing failed: %s", SDL_GetError());
    }
}

void Renderer::drawSideWorld(const Game& game, bool paused) noexcept {
    const float cameraX = game.cameraX();
    const RoomLayout& room = game.currentRoom();
    const WorldLayout& world = game.world();
    const bool layered = drawRoomLayers(room, cameraX);
    for (int column = 0; !layered && column < game.roomColumns(); ++column) {
        const int screenX = static_cast<int>(column * kTileSize - cameraX);
        if (screenX < -kTileSize || screenX > 640) {
            continue;
        }
        for (int row = 0; row < kRoomRows; ++row) {
            const Tile tile = game.tileAt(column, row);
            if (tile == Tile::Empty) {
                continue;
            }
            const int screenY = row * kTileSize;
            if (sprite(Sprite::Tiles, tile == Tile::Hazard ? 1 : 0, {screenX, screenY, kTileSize, kTileSize})) {
                continue;
            }
            if (tile == Tile::Hazard) {
                panel({screenX, screenY + 9, kTileSize, 23}, SDL_Color{121, 43, 51, 255});
                panel({screenX + 3, screenY + 5, 5, 10}, kOrange);
                panel({screenX + 15, screenY + 1, 5, 14}, kRed);
                panel({screenX + 25, screenY + 6, 4, 9}, kOrange);
            } else {
                panel({screenX, screenY, kTileSize, kTileSize}, SDL_Color{37, 61, 74, 255});
                panel({screenX, screenY, kTileSize, 3}, SDL_Color{62, 106, 115, 255});
                panel({screenX + 5, screenY + 9, 2, 2}, SDL_Color{51, 79, 91, 255});
            }
        }
    }

    const Uint64 ticks = SDL_GetTicks64();
    for (const PropLayout& prop : world.propList()) {
        if (prop.room != game.roomId() || prop.archetype == nullptr) continue;
        Vec2 position{};
        if (!game.propPosition(prop.room, prop.id.data(), position)) continue;
        (void)drawObject(prop.archetype, "idle", ticks,
                         {position.x, position.y, prop.archetype->width(),
                          prop.archetype->height()},
                         cameraX, prop.facingLeft);
    }
    for (const DoorLayout& door : world.doorList()) {
        if (door.room != game.roomId() || !door.visible) continue;
        const int doorX = static_cast<int>(door.bounds.x - cameraX);
        const int doorY = static_cast<int>(door.bounds.y);
        if (!drawObject(door.archetype, "idle", ticks, door.bounds, cameraX, false) &&
            !sprite(Sprite::Door, 0, {doorX, doorY, 44, 48})) {
            panel({doorX, doorY, 44, 48}, SDL_Color{32, 63, 76, 255});
            panel({doorX + 5, doorY + 6, 34, 36}, SDL_Color{19, 39, 49, 255});
            panel({doorX + 32, doorY + 22, 3, 3}, kCyan);
        }
    }
    if (game.roomId() == world.abilityRoom && !game.abilityCollected()) {
        const Rect pickup = game.abilityPickup();
        const int pickupX = static_cast<int>(pickup.x - cameraX);
        if (!drawObject(world.archetypes.pickup, "idle", ticks, pickup, cameraX, false) &&
            !sprite(Sprite::Pickup, static_cast<int>((ticks / 300U) % 2U),
                    {pickupX - 6, static_cast<int>(pickup.y) - 10, 32, 32})) {
            panel({pickupX - 7, static_cast<int>(pickup.y) - 7, 34, 34}, SDL_Color{23, 59, 67, 255});
            panel({pickupX, static_cast<int>(pickup.y), 20, 20}, kCyan);
            panel({pickupX + 6, static_cast<int>(pickup.y) - 6, 8, 8}, kText);
        }
    }

    if (game.roomId() == RoomId::LandingBay) {
        const Rect technician = game.technicianBounds();
        const int x = static_cast<int>(technician.x - cameraX);
        const int y = static_cast<int>(technician.y);
        const bool talking = game.dialogueActive();
        const bool facingLeft = world.archetypes.engineer != nullptr &&
                                world.archetypes.engineer->facing() == framework::Facing::Left;
        if (!drawObject(world.archetypes.engineer, talking ? "talk" : "idle", ticks,
                        technician, cameraX, facingLeft)) {
            panel({x + 4, y + 12, 20, 28}, SDL_Color{45, 122, 129, 255});
            panel({x + 6, y + 1, 16, 15}, SDL_Color{190, 166, 132, 255});
            panel({x + 7, y, 14, 5}, SDL_Color{55, 53, 65, 255});
            panel({x + 8, y + 7, 3, 3}, kText);
            panel({x + 17, y + 7, 3, 3}, kText);
            panel({x + 7, y + 38, 6, 2}, SDL_Color{35, 52, 65, 255});
            panel({x + 16, y + 38, 6, 2}, SDL_Color{35, 52, 65, 255});
        }
        if (game.technicianNearby() && !game.dialogueActive()) {
            text(x - 1, y - 12, "ENGINEER", 1, kCyan);
        }
    }

    for (std::size_t index = 0; index < game.enemies().size(); ++index) {
        const Enemy& enemy = game.enemies()[index];
        if (!enemy.alive || enemy.room != game.roomId()) {
            continue;
        }
        const int x = static_cast<int>(enemy.bounds.x - cameraX);
        const int y = static_cast<int>(enemy.bounds.y);
        const int width = static_cast<int>(enemy.bounds.width);
        const bool guarding = enemy.guarding();
        const std::string_view animation =
            enemy.type == EnemyType::Sentry ? "idle" : guarding ? "guard" : "walk";
        const bool flip = enemy.type == EnemyType::Sentry
            ? game.player().position.x < enemy.bounds.x : enemy.speed < 0.0F;
        if (drawObject(world.enemies[index].archetype, animation, ticks, enemy.bounds,
                       cameraX, flip)) {
            continue;
        }
        if (!sprite(Sprite::Enemy, static_cast<int>((ticks / 180U) % 2U),
                    {x + (width - 32) / 2, y + static_cast<int>(enemy.bounds.height) - 32, 32, 32})) {
            panel({x, y + 8, width, 20}, SDL_Color{135, 80, 99, 255});
            panel({x + 4, y + 2, 20, 14}, SDL_Color{202, 111, 121, 255});
            panel({x + 8, y + 7, 4, 4}, kOrange);
            panel({x + 18, y + 7, 4, 4}, kOrange);
        }
        if (enemy.type != EnemyType::Patrol) {
            const SDL_Color marker = enemy.type == EnemyType::Charger ? kOrange :
                                     enemy.type == EnemyType::Armored
                                         ? (guarding ? kText : kCyan)
                                         : kRed;
            panel({x, y - 5, width, 3}, marker);
        }
    }

    for (const LootDrop& drop : game.lootDrops()) {
        if (!drop.active || drop.room != game.roomId()) continue;
        const int x = static_cast<int>(drop.position.x - cameraX);
        const int y = static_cast<int>(drop.position.y);
        if (!drawObject(world.archetypes.loot, "idle", ticks,
                        {drop.position.x, drop.position.y, 16.0F, 16.0F}, cameraX, false)) {
            panel({x - 2, y - 2, 20, 20}, SDL_Color{91, 67, 37, 255});
            panel({x, y, 16, 16}, SDL_Color{190, 133, 57, 255});
            panel({x + 3, y + 3, 10, 4}, SDL_Color{235, 190, 93, 255});
            panel({x + 5, y + 9, 8, 3}, SDL_Color{111, 77, 42, 255});
        }
        std::array<char, 8> amount{};
        std::snprintf(amount.data(), amount.size(), "x%u",
                      static_cast<unsigned>(drop.stack.quantity));
        text(x + 18, y + 2, amount.data(), 1, kText);
    }

    for (const Projectile& projectile : game.projectiles()) {
        if (projectile.active && projectile.room == game.roomId()) {
            const SDL_Rect bounds{static_cast<int>(projectile.bounds.x - cameraX), static_cast<int>(projectile.bounds.y),
                                  static_cast<int>(projectile.bounds.width), static_cast<int>(projectile.bounds.height)};
            if (!drawObject(world.archetypes.projectile, "fly", ticks, projectile.bounds,
                            cameraX, projectile.velocityX < 0.0F) &&
                !sprite(Sprite::Projectile, static_cast<int>((ticks / 60U) % 2U),
                        {bounds.x + bounds.w / 2 - 6, bounds.y + bounds.h / 2 - 4, 12, 8},
                        projectile.velocityX < 0.0F)) {
                panel(bounds, kOrange);
            }
        }
    }
    for (const Projectile& projectile : game.enemyProjectiles()) {
        if (projectile.active && projectile.room == game.roomId() &&
            !drawObject(world.archetypes.enemyProjectile, "fly", ticks, projectile.bounds,
                        cameraX, projectile.velocityX < 0.0F)) {
            panel({static_cast<int>(projectile.bounds.x - cameraX),
                   static_cast<int>(projectile.bounds.y),
                   static_cast<int>(projectile.bounds.width),
                   static_cast<int>(projectile.bounds.height)}, kRed);
        }
    }

    const Player& player = game.player();
    const int playerX = static_cast<int>(player.position.x - cameraX);
    const int playerY = static_cast<int>(player.position.y);
    const bool flashing = (ticks / 90U) % 2U == 0U;
    const std::string_view playerAnimation =
        !player.onGround ? (player.velocity.y < 0.0F ? "jump" : "fall")
        : std::abs(player.velocity.x) > 20.0F ? "run" : "idle";
    if (player.health > 0 && (flashing || !paused) &&
        !drawObject(world.archetypes.player, playerAnimation, ticks,
                    {player.position.x, player.position.y, world.playerSize.x, world.playerSize.y},
                    cameraX, player.facing < 0) &&
        !sprite(Sprite::Player, playerAnimationFrame(player, ticks), {playerX - 2, playerY - 2, 24, 32}, player.facing < 0)) {
        panel({playerX, playerY + 5, 20, 23}, SDL_Color{105, 163, 172, 255});
        panel({playerX + 3, playerY, 14, 10}, SDL_Color{174, 211, 207, 255});
        panel({playerX + (player.facing > 0 ? 13 : 3), playerY + 3, 5, 3}, kCyan);
        panel({playerX + 5, playerY + 27, 4, 3}, SDL_Color{35, 52, 65, 255});
        panel({playerX + 12, playerY + 27, 4, 3}, SDL_Color{35, 52, 65, 255});
    }
}

void Renderer::drawIsometricWorld(const Game& game, bool paused) noexcept {
    const RoomLayout& room = game.currentRoom();
    const WorldLayout& world = game.world();
    const framework::RoomDefinition* definition = room.definition;
    const Player& player = game.player();
    const Uint64 ticks = SDL_GetTicks64();

    // Grid (x, y) maps to screen (x - y, (x + y) / 2): one 32 px cell is a 64x32 diamond.
    const rendering::Vec2 focus = projectIsometric(player.position.x + world.playerSize.x * 0.5F,
                                                    player.position.y + world.playerSize.y * 0.5F);
    const float cameraX = std::floor(focus.x - 320.0F);
    const float cameraY = std::floor(focus.y - 196.0F);
    const auto screenOf = [&](float x, float y) noexcept {
        const rendering::Vec2 point = projectIsometric(x, y);
        return SDL_Point{static_cast<int>(std::floor(point.x - cameraX)),
                         static_cast<int>(std::floor(point.y - cameraY))};
    };
    const auto visible = [](const SDL_Rect& rect) noexcept {
        return rect.x + rect.w > 0 && rect.x < 640 && rect.y + rect.h > 0 && rect.y < 360;
    };
    const auto cellColor = [](Tile tile) noexcept {
        return tile == Tile::Hazard ? SDL_Color{86, 196, 74, 255}
             : tile == Tile::Solid  ? SDL_Color{60, 84, 98, 255}
                                    : SDL_Color{34, 48, 58, 255};
    };

    // Tiles are bottom-aligned to their cell diamond, so taller tiles rise as blocks.
    const auto drawTile = [&](std::size_t layer, int column, int row, Uint8 alpha) noexcept {
        const SDL_Point top = screenOf(static_cast<float>(column * kTileSize),
                                       static_cast<float>(row * kTileSize));
        const framework::TileType* tile =
            definition != nullptr ? definition->tile(layer, column, row) : nullptr;
        if (tile == nullptr) {
            if (definition != nullptr) return;
            const Tile cell = game.tileAt(column, row);
            if (layer == 0) {
                panel({top.x - 16, top.y + 8, 32, 16}, cellColor(cell == Tile::Solid ? Tile::Empty : cell));
            } else if (cell == Tile::Solid) {
                panel({top.x - 24, top.y - 24, 48, 48}, cellColor(cell));
            }
            return;
        }
        const framework::TilesetSpec& tileset = definition->tilesets()[tile->tilesetIndex];
        const WorldSheet* sheet = worldSheet(tileset.sheet.data());
        const CanvasWorldSprite* worldImage = drawSink_ != nullptr
            ? canvasWorldSprite(tileset.sheet.data(), tileset.material) : nullptr;
        const SDL_Rect destination{top.x - tileset.tileWidth / 2,
                                   top.y + kTileSize - tileset.tileHeight,
                                   tileset.tileWidth, tileset.tileHeight};
        if (!visible(destination)) return;
        const SDL_Rect source{
            tileset.margin + tile->column * (tileset.tileWidth + tileset.spacing),
            tileset.margin + tile->row * (tileset.tileHeight + tileset.spacing),
            tileset.tileWidth, tileset.tileHeight};
        const CanvasSprite* image = worldImage != nullptr ? &worldImage->image : nullptr;
        const int sheetWidth = drawSink_ != nullptr
            ? (image != nullptr ? static_cast<int>(image->width) : 0)
            : (sheet != nullptr ? sheet->width : 0);
        const int sheetHeight = drawSink_ != nullptr
            ? (image != nullptr ? static_cast<int>(image->height) : 0)
            : (sheet != nullptr ? sheet->height : 0);
        if ((drawSink_ != nullptr && (image == nullptr || !image->texture.valid())) ||
            (drawSink_ == nullptr && sheet == nullptr) ||
            source.x + source.w > sheetWidth || source.y + source.h > sheetHeight) {
            const Tile cell = tile->collision == framework::TileCollision::Hazard ? Tile::Hazard
                            : tile->collision == framework::TileCollision::Solid  ? Tile::Solid
                                                                                   : Tile::Empty;
            panel(layer == 0 ? SDL_Rect{top.x - 16, top.y + 8, 32, 16}
                             : SDL_Rect{top.x - 24, top.y - 24, 48, 48},
                  cellColor(cell));
            return;
        }
        if (drawSink_ != nullptr) {
            rendering::Vec4 uv{};
            const auto status = rendering::spriteUvTransform(
                image->width, image->height,
                {static_cast<std::uint32_t>(source.x), static_cast<std::uint32_t>(source.y),
                 static_cast<std::uint32_t>(source.w), static_cast<std::uint32_t>(source.h),
                 tile->flipX, tile->flipY}, uv);
            if (!status || drawSinkFailed_ ||
                !drawWorldTexture(
                    *worldImage, tileset.material,
                    {destination.x + (drawingWorld_ ? worldOffsetX_ : 0),
                     destination.y + (drawingWorld_ ? worldOffsetY_ : 0),
                     destination.w, destination.h},
                    uv, {1, 1, 1, static_cast<float>(alpha) / 255.0F},
                    static_cast<float>(tile->rotation))) {
                drawSinkFailed_ = true;
            }
            return;
        }
        SDL_SetTextureAlphaMod(sheet->texture, alpha);
        const SDL_Rect shifted{destination.x + (drawingWorld_ ? worldOffsetX_ : 0),
                               destination.y + (drawingWorld_ ? worldOffsetY_ : 0),
                               destination.w, destination.h};
        SDL_RenderCopy(renderer_, sheet->texture, &source, &shifted);
        if (alpha != 255) SDL_SetTextureAlphaMod(sheet->texture, 255);
    };

    const int rows = game.roomRows();
    const int columns = game.roomColumns();
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) drawTile(0, column, row, 255);
    }

    // Everything standing on the floor is sorted by the front corner of its footprint.
    enum class Kind : std::uint8_t { Block, Prop, Roamer, Projectile, Player };
    struct Drawable {
        float depth;
        Kind kind;
        std::uint16_t index;
        std::uint8_t layer;
    };
    std::array<Drawable, static_cast<std::size_t>(kMaxLayoutColumns) * kMaxLayoutRows *
                             (framework::kMaxRoomLayers - 1) + 64> drawables{};
    std::size_t count = 0;
    const auto push = [&](float depth, Kind kind, std::size_t index, std::size_t layer) noexcept {
        if (count == drawables.size()) return;
        drawables[count++] = {depth, kind, static_cast<std::uint16_t>(index),
                              static_cast<std::uint8_t>(layer)};
    };
    const std::size_t layerCount = definition != nullptr ? definition->layers().size() : 2U;
    for (std::size_t layer = 1; layer < layerCount; ++layer) {
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const bool present = definition != nullptr
                    ? definition->tile(layer, column, row) != nullptr
                    : game.tileAt(column, row) == Tile::Solid;
                if (present) {
                    push(static_cast<float>((column + row + 2) * kTileSize), Kind::Block,
                         static_cast<std::size_t>(row * kMaxLayoutColumns + column), layer);
                }
            }
        }
    }
    const auto frontDepth = [](const Rect& bounds) noexcept {
        return bounds.x + bounds.width + bounds.y + bounds.height;
    };
    for (std::size_t index = 0; index < world.propList().size(); ++index) {
        const PropLayout& prop = world.propList()[index];
        if (prop.room != game.roomId() || prop.archetype == nullptr) continue;
        Vec2 position{};
        if (!game.propPosition(prop.room, prop.id.data(), position)) continue;
        const SDL_Point anchor = screenOf(
            position.x + prop.archetype->width() * 0.5F,
            position.y + prop.archetype->height() * 0.5F);
        const auto& spec = prop.archetype->sprite();
        const int spriteX = anchor.x + (prop.facingLeft
            ? -spec.frameWidth - spec.offsetX : spec.offsetX);
        const SDL_Rect spriteBounds{spriteX, anchor.y + spec.offsetY,
                                    spec.frameWidth, spec.frameHeight};
        const SDL_Rect shadowBounds{anchor.x - 12, anchor.y - 2, 24, 4};
        const SDL_Rect fallbackBounds{anchor.x - 10, anchor.y - 18, 20, 16};
        if (!visible(spriteBounds) && !visible(shadowBounds) &&
            !visible(fallbackBounds)) continue;
        push(position.x + position.y + prop.archetype->width() +
                 prop.archetype->height(),
             Kind::Prop, index, 0);
    }
    const std::span<const Enemy> roamers = game.roamers();
    for (std::size_t index = 0; index < roamers.size(); ++index) {
        if (roamers[index].alive) push(frontDepth(roamers[index].bounds), Kind::Roamer, index, 0);
    }
    for (std::size_t index = 0; index < game.projectiles().size(); ++index) {
        const Projectile& projectile = game.projectiles()[index];
        if (projectile.active && projectile.room == game.roomId()) {
            push(frontDepth(projectile.bounds), Kind::Projectile, index, 0);
        }
    }
    const Rect playerBounds{player.position.x, player.position.y, world.playerSize.x, world.playerSize.y};
    const float playerDepth = frontDepth(playerBounds);
    if (player.health > 0) push(playerDepth, Kind::Player, 0, 0);
    std::sort(drawables.begin(), drawables.begin() + static_cast<std::ptrdiff_t>(count),
              [](const Drawable& left, const Drawable& right) noexcept {
                  if (left.depth != right.depth) return left.depth < right.depth;
                  if (left.kind != right.kind) return left.kind < right.kind;
                  if (left.layer != right.layer) return left.layer < right.layer;
                  return left.index < right.index;
              });

    const auto anchorOf = [&](const Rect& bounds) noexcept {
        return screenOf(bounds.x + bounds.width * 0.5F, bounds.y + bounds.height * 0.5F);
    };
    const SDL_Point playerAnchor = anchorOf(playerBounds);
    const SDL_Rect playerScreen{playerAnchor.x - 16, playerAnchor.y - 44, 32, 48};
    const bool flashing = (ticks / 90U) % 2U == 0U;
    for (std::size_t item = 0; item < count; ++item) {
        const Drawable& drawable = drawables[item];
        switch (drawable.kind) {
        case Kind::Block: {
            const int column = drawable.index % kMaxLayoutColumns;
            const int row = drawable.index / kMaxLayoutColumns;
            const SDL_Point top = screenOf(static_cast<float>(column * kTileSize),
                                           static_cast<float>(row * kTileSize));
            // Like Diablo II, blocks in front of the player fade so the hero stays visible.
            const SDL_Rect blockScreen{top.x - 32, top.y - 32, 64, 64};
            const bool occludes = player.health > 0 && drawable.depth > playerDepth &&
                                  SDL_HasIntersection(&blockScreen, &playerScreen) == SDL_TRUE;
            drawTile(drawable.layer, column, row, occludes ? 110 : 255);
            break;
        }
        case Kind::Prop: {
            const PropLayout& prop = world.propList()[drawable.index];
            Vec2 position{};
            if (!game.propPosition(prop.room, prop.id.data(), position)) break;
            const SDL_Point anchor = screenOf(
                position.x + prop.archetype->width() * 0.5F,
                position.y + prop.archetype->height() * 0.5F);
            panel({anchor.x - 12, anchor.y - 2, 24, 4}, SDL_Color{0, 0, 0, 90});
            if (!drawObject(prop.archetype, "idle", ticks,
                            {static_cast<float>(anchor.x), static_cast<float>(anchor.y), 0.0F, 0.0F},
                            0.0F, prop.facingLeft)) {
                panel({anchor.x - 10, anchor.y - 18, 20, 16}, SDL_Color{31, 57, 68, 255});
                panel({anchor.x - 6, anchor.y - 15, 12, 6}, kCyan);
            }
            break;
        }
        case Kind::Roamer: {
            const Enemy& roamer = roamers[drawable.index];
            const framework::ObjectDefinition* archetype = nullptr;
            std::size_t ordinal = 0;
            for (const EnemySpawn& spawn : world.roamerList()) {
                if (spawn.room != game.roomId()) continue;
                if (ordinal++ == drawable.index) {
                    archetype = spawn.archetype;
                    break;
                }
            }
            const SDL_Point anchor = anchorOf(roamer.bounds);
            const float dx = (player.position.x - player.position.y) -
                             (roamer.bounds.x - roamer.bounds.y);
            const float distance = std::hypot(player.position.x - roamer.bounds.x,
                                              player.position.y - roamer.bounds.y);
            panel({anchor.x - 10, anchor.y - 2, 20, 4}, SDL_Color{0, 0, 0, 90});
            if (!drawObject(archetype, distance < 300.0F ? "walk" : "idle", ticks,
                            {static_cast<float>(anchor.x), static_cast<float>(anchor.y), 0.0F, 0.0F},
                            0.0F, dx < 0.0F)) {
                panel({anchor.x - 11, anchor.y - 18, 22, 14}, SDL_Color{110, 76, 150, 255});
                panel({anchor.x + 4, anchor.y - 14, 4, 4}, kRed);
            }
            break;
        }
        case Kind::Projectile: {
            const SDL_Point anchor = anchorOf(game.projectiles()[drawable.index].bounds);
            panel({anchor.x - 2, anchor.y - 1, 4, 2}, SDL_Color{0, 0, 0, 110});
            panel({anchor.x - 3, anchor.y - 17, 6, 6}, kOrange);
            panel({anchor.x - 1, anchor.y - 15, 2, 2}, kText);
            break;
        }
        case Kind::Player: {
            if (!flashing && paused) break;
            const float aimX = player.aim.x - player.aim.y;
            const float aimY = (player.aim.x + player.aim.y) * 0.5F;
            constexpr float kEighth = 0.78539816F;
            int sector = static_cast<int>(std::lround(std::atan2(aimY, aimX) / kEighth));
            if (sector < -3) sector = 4;
            // Sectors run clockwise from east; western rows mirror their eastern twins.
            constexpr std::array<std::string_view, 8> kDirections{"e", "se", "s", "se",
                                                                  "e", "ne", "n", "ne"};
            const std::size_t slot = static_cast<std::size_t>((sector + 8) % 8);
            const bool flip = slot >= 3 && slot <= 5;
            const bool walking = std::abs(player.velocity.x) + std::abs(player.velocity.y) > 1.0F;
            std::array<char, 12> animation{};
            std::snprintf(animation.data(), animation.size(), "%s_%.*s",
                          walking ? "walk" : "idle",
                          static_cast<int>(kDirections[slot].size()), kDirections[slot].data());
            panel({playerAnchor.x - 9, playerAnchor.y - 2, 18, 4}, SDL_Color{0, 0, 0, 90});
            if (!drawObject(world.archetypes.playerIso, animation.data(), ticks,
                            {static_cast<float>(playerAnchor.x),
                             static_cast<float>(playerAnchor.y), 0.0F, 0.0F},
                            0.0F, flip)) {
                panel({playerAnchor.x - 8, playerAnchor.y - 30, 16, 28},
                      SDL_Color{105, 163, 172, 255});
                panel({playerAnchor.x - 6, playerAnchor.y - 36, 12, 10},
                      SDL_Color{174, 211, 207, 255});
            }
            break;
        }
        }
    }
    if (!paused && mouseAim_) {
        const int reticleX = static_cast<int>(std::lround(mouseAimPoint_.x));
        const int reticleY = static_cast<int>(std::lround(mouseAimPoint_.y));
        const int offsetX = drawingWorld_ ? worldOffsetX_ : 0;
        const int offsetY = drawingWorld_ ? worldOffsetY_ : 0;
        panel({reticleX - 7 - offsetX, reticleY - 1 - offsetY, 5, 2}, kOrange);
        panel({reticleX + 3 - offsetX, reticleY - 1 - offsetY, 5, 2}, kOrange);
        panel({reticleX - 1 - offsetX, reticleY - 7 - offsetY, 2, 5}, kOrange);
        panel({reticleX - 1 - offsetX, reticleY + 3 - offsetY, 2, 5}, kOrange);
        panel({reticleX - 1 - offsetX, reticleY - 1 - offsetY, 2, 2}, kText);
    } else if (!paused) {
        const float screenAimX = player.aim.x - player.aim.y;
        const float screenAimY = (player.aim.x + player.aim.y) * 0.5F;
        const float length = std::sqrt(screenAimX * screenAimX + screenAimY * screenAimY);
        if (length > 0.001F) {
            const int reticleX = static_cast<int>(std::lround(
                Renderer::kIsometricAimOriginX + screenAimX / length * 34.0F));
            const int reticleY = static_cast<int>(std::lround(
                Renderer::kIsometricAimOriginY + screenAimY / length * 34.0F));
            panel({reticleX - 4, reticleY - 1, 3, 2}, kOrange);
            panel({reticleX + 2, reticleY - 1, 3, 2}, kOrange);
            panel({reticleX - 1, reticleY - 4, 2, 3}, kOrange);
            panel({reticleX - 1, reticleY + 2, 2, 3}, kOrange);
        }
    }
}

void Renderer::drawShmup(const Game& game, bool paused) noexcept {
    const ShmupSession& session = game.shmup();
    const ShmupStage* stage = session.stage();
    if (stage == nullptr) return;
    panel({0, 0, 640, 360}, SDL_Color{stage->background[0], stage->background[1],
                                       stage->background[2], 255});
    const Uint64 ticks = SDL_GetTicks64();
    // Parallax layers come from JSON world objects; the scroll distance drives them statelessly.
    bool layersDrawn = stage->layerCount > 0;
    for (std::size_t index = 0; index < stage->layerCount; ++index) {
        const ShmupLayer& layer = stage->layers[index];
        if (layer.visual.archetype == nullptr) {
            layersDrawn = false;
            continue;
        }
        const framework::SpriteSheetSpec& spec = layer.visual.archetype->sprite();
        const float tile = static_cast<float>(spec.frameWidth);
        float offset = std::fmod(session.scroll() * layer.scrollFactor, tile);
        if (offset < 0.0F) offset += tile;
        for (float x = -offset; x < 640.0F; x += tile) {
            if (!drawObject(layer.visual.archetype, "idle", ticks,
                            {x, layer.y, tile, static_cast<float>(spec.frameHeight)}, 0.0F, false)) {
                layersDrawn = false;
            }
        }
    }
    // Fallback for stages without object-backed layers: three procedural star layers.
    constexpr std::array<float, 3> kStarSpeed{0.25F, 0.55F, 1.0F};
    for (std::size_t layer = 0; layer < kStarSpeed.size() && !layersDrawn; ++layer) {
        for (std::uint32_t star = 0; star < 28; ++star) {
            const std::uint32_t hash = (star + 1U) * 2654435761U + static_cast<std::uint32_t>(layer) * 40503U;
            const float baseX = static_cast<float>((hash >> 7) % 700U);
            const int y = static_cast<int>((hash >> 17) % 360U);
            float x = std::fmod(baseX - session.scroll() * kStarSpeed[layer], 700.0F);
            if (x < 0.0F) x += 700.0F;
            const int size = layer == 2 ? 3 : layer == 1 ? 2 : 1;
            const std::uint8_t shade = static_cast<std::uint8_t>(90 + layer * 50);
            panel({static_cast<int>(x) - 30, y, size + (layer == 2 ? 3 : 0), size},
                  SDL_Color{shade, shade, static_cast<std::uint8_t>(shade + 20), 255});
        }
    }

    for (const ShmupEnemyState& enemy : session.enemies()) {
        if (!enemy.active) continue;
        const ShmupEnemyType& type = stage->enemyTypes[enemy.type];
        const SDL_Color body = enemy.flash > 0.0F
            ? SDL_Color{255, 255, 255, 255}
            : SDL_Color{type.color[0], type.color[1], type.color[2], 255};
        const int x = static_cast<int>(enemy.x);
        const int y = static_cast<int>(enemy.y);
        const int w = static_cast<int>(type.width);
        const int h = static_cast<int>(type.height);
        if (drawObject(type.visual.archetype, "idle", ticks, {enemy.x, enemy.y, type.width, type.height},
                       0.0F, false)) {
            if (enemy.flash > 0.0F) outline({x, y, w, h}, SDL_Color{255, 255, 255, 255});
            continue;
        }
        panel({x, y + h / 4, w, h / 2}, body);
        panel({x + w / 4, y, w / 2, h}, body);
        panel({x + 2, y + h / 2 - 2, 5, 4}, SDL_Color{20, 10, 14, 255});
    }

    const ShmupBossState& boss = session.boss();
    if (boss.active) {
        const ShmupBoss& definition = stage->boss;
        const int x = static_cast<int>(boss.x);
        const int y = static_cast<int>(boss.y);
        const int w = static_cast<int>(definition.width);
        const int h = static_cast<int>(definition.height);
        const bool shifting = boss.transitionLeft > 0.0F &&
                              static_cast<int>(boss.transitionLeft * 12.0F) % 2 == 0;
        const std::size_t phaseIndex =
            std::min<std::size_t>(boss.phase, definition.phaseCount > 0 ? definition.phaseCount - 1 : 0);
        const std::string_view animation = boss.flash > 0.0F || shifting
            ? std::string_view(definition.hitAnimation.data())
            : std::string_view(definition.phases[phaseIndex].animation.data());
        if (!drawObject(definition.visual.archetype, animation, ticks,
                        {boss.x, boss.y, definition.width, definition.height}, 0.0F, false)) {
            const SDL_Color body = boss.flash > 0.0F || shifting
                ? SDL_Color{255, 255, 255, 255}
                : SDL_Color{definition.color[0], definition.color[1], definition.color[2], 255};
            panel({x, y + h / 6, w, h * 2 / 3}, body);
            panel({x + w / 6, y, w * 2 / 3, h}, body);
            panel({x - 10, y + h / 2 - 6, 14, 12}, kOrange);
            const std::uint8_t core = static_cast<std::uint8_t>(80 + boss.phase * 70);
            panel({x + w / 3, y + h / 3, w / 3, h / 3}, SDL_Color{core, 40, 60, 255});
        }
    }

    for (const ShmupBullet& shot : session.playerShots()) {
        if (!shot.active) continue;
        if (drawObject(stage->player.shot.archetype, "idle", ticks, {shot.x, shot.y - 1.0F, 12.0F, 6.0F},
                       0.0F, false)) {
            continue;
        }
        panel({static_cast<int>(shot.x), static_cast<int>(shot.y), 12, 4}, kCyan);
    }
    for (const ShmupBullet& bullet : session.enemyShots()) {
        if (!bullet.active) continue;
        const int size = static_cast<int>(bullet.size);
        if (drawObject(stage->enemyShot.archetype, "idle", ticks,
                       {bullet.x - 8.0F, bullet.y - 8.0F, 16.0F, 16.0F}, 0.0F, false)) {
            continue;
        }
        panel({static_cast<int>(bullet.x) - size / 2, static_cast<int>(bullet.y) - size / 2, size, size},
              SDL_Color{255, 214, 120, 255});
        panel({static_cast<int>(bullet.x) - size / 4, static_cast<int>(bullet.y) - size / 4,
               std::max(1, size / 2), std::max(1, size / 2)},
              SDL_Color{255, 90, 70, 255});
    }

    const ShmupShip& ship = session.ship();
    const bool blink = session.invulnerable() > 0.0F &&
                       static_cast<int>(session.invulnerable() * 14.0F) % 2 == 0;
    if (!blink) {
        const int x = static_cast<int>(ship.x);
        const int y = static_cast<int>(ship.y);
        const int w = static_cast<int>(stage->player.width);
        const int h = static_cast<int>(stage->player.height);
        if (!drawObject(stage->player.visual.archetype, "idle", ticks,
                        {ship.x, ship.y, stage->player.width, stage->player.height}, 0.0F, false)) {
            panel({x, y + h / 3, w, h / 3}, kCyan);
            panel({x + w / 4, y, w / 3, h}, SDL_Color{40, 150, 170, 255});
            panel({x - 6, y + h / 2 - 2, 6, 4}, kOrange);
        }
    }

    panel({0, 0, 640, 22}, SDL_Color{4, 10, 17, 218});
    text(10, 7, stage->name.data(), 1, kText);
    for (int index = 0; index < stage->player.health; ++index) {
        panel({150 + index * 14, 6, 10, 9}, index < session.health() ? kRed : SDL_Color{55, 60, 67, 255});
    }
    std::array<char, 24> score{};
    std::snprintf(score.data(), score.size(), "SCORE %05d", session.score());
    text(560, 7, score.data(), 1, kMuted);
    if (boss.active) {
        const ShmupBoss& definition = stage->boss;
        panel({120, 334, 400, 14}, SDL_Color{4, 10, 17, 230});
        panel({123, 337, static_cast<int>(394.0F * session.bossHealthFraction()), 8},
              boss.transitionLeft > 0.0F ? kOrange : kRed);
        // Tick marks show where each later boss phase begins.
        for (std::size_t phase = 1; phase < definition.phaseCount; ++phase) {
            panel({123 + static_cast<int>(394.0F * definition.phases[phase].healthBelow), 335, 2, 12}, kText);
        }
        std::array<char, 48> label{};
        std::snprintf(label.data(), label.size(), "%s  PHASE %u/%u", definition.name.data(),
                      static_cast<unsigned>(boss.phase + 1), static_cast<unsigned>(definition.phaseCount));
        text(120, 322, label.data(), 1, kText);
    }
    if (paused) {
        panel({240, 150, 160, 40}, SDL_Color{4, 10, 17, 230});
        text(284, 166, "PAUSED", 2, kText);
    }
}

void Renderer::drawParticles(const Game& game,
                             std::span<const vfx::Particle> particles) noexcept {
    const Player& player = game.player();
    float cameraX = game.cameraX();
    float cameraY = 0.0F;
    if (game.isometric()) {
        const float focusX = player.position.x + game.world().playerSize.x * 0.5F;
        const float focusY = player.position.y + game.world().playerSize.y * 0.5F;
        const float focusScreenX = focusX - focusY;
        const float focusScreenY = (focusX + focusY) * 0.5F;
        cameraX = std::floor(focusScreenX - 320.0F);
        cameraY = std::floor(focusScreenY - 196.0F);
    }

    const auto channel = [](float value) noexcept {
        value = std::clamp(value, 0.0F, 1.0F);
        const float srgb = value <= 0.0031308F
            ? value * 12.92F
            : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
        return static_cast<Uint8>(std::lround(srgb * 255.0F));
    };
    for (const vfx::Particle& particle : particles) {
        if (!std::isfinite(particle.position.x) || !std::isfinite(particle.position.y) ||
            !std::isfinite(particle.scale) || particle.scale <= 0.0F ||
            !std::isfinite(particle.color.x) || !std::isfinite(particle.color.y) ||
            !std::isfinite(particle.color.z) || !std::isfinite(particle.color.w)) {
            continue;
        }
        float screenX = particle.position.x - cameraX;
        float screenY = particle.position.y - cameraY;
        if (game.isometric()) {
            const float projectedX = particle.position.x - particle.position.y;
            const float projectedY = (particle.position.x + particle.position.y) * 0.5F;
            screenX = projectedX - cameraX;
            screenY = projectedY - cameraY;
        }
        if (!std::isfinite(screenX) || !std::isfinite(screenY) ||
            screenX < -16.0F || screenX > 656.0F ||
            screenY < -16.0F || screenY > 376.0F) {
            continue;
        }
        const int size = static_cast<int>(
            std::lround(std::clamp(particle.scale * 6.0F, 1.0F, 16.0F)));
        const SDL_Rect bounds{
            static_cast<int>(std::floor(screenX - size * 0.5F)),
            static_cast<int>(std::floor(screenY - size * 0.5F)), size, size};
        panel(bounds, {channel(particle.color.x), channel(particle.color.y),
                       channel(particle.color.z), channel(particle.color.w)});
    }
}

void Renderer::drawGame(const Game& game, bool showMap, bool paused,
                        std::string_view jumpKey, std::string_view shootKey,
                        std::string_view interactKey,
                        std::span<const vfx::Particle> particles,
                        const RenderDebugInfo* debug,
                        std::uint8_t damageFlashOpacity,
                        std::span<const vfx::Particle> impacts,
                        std::span<const vfx::Particle> explosions,
                        std::span<const vfx::Particle> fog) noexcept {
    SDL_SetRenderDrawColor(renderer_, 8, 15, 26, 255);
    SDL_RenderClear(renderer_);
    drawGameContent(game, showMap, paused, jumpKey, shootKey, interactKey, particles, debug,
                    damageFlashOpacity, impacts, explosions, fog);
    SDL_RenderPresent(renderer_);
}

bool Renderer::recordGame(rendering::ICanvas2D& sink, const Game& game, bool showMap, bool paused,
                          std::string_view jumpKey, std::string_view shootKey,
                          std::string_view interactKey, const CanvasSprites* sprites,
                          const CanvasWorldSprites* worldSprites,
                          std::span<const vfx::Particle> particles,
                          const RenderDebugInfo* debug,
                          std::uint8_t damageFlashOpacity,
                          std::span<const vfx::Particle> impacts,
                          std::span<const vfx::Particle> explosions,
                          std::span<const vfx::Particle> fog) noexcept {
    if ((worldSheetCount_ != 0 && worldSprites == nullptr) || (sprites == nullptr &&
        std::any_of(sprites_.begin(), sprites_.end(), [](SDL_Texture* texture) { return texture != nullptr; }))) {
        SDL_Log("Recording game with loaded SDL textures is not supported");
        return false;
    }
    if (sprites != nullptr) {
        for (std::size_t index = 0; index < sprites->size(); ++index) {
            const CanvasSprite& texture = (*sprites)[index];
            const SpriteSheet& sheet = kSpriteSheets[index];
            if (texture.texture.valid() &&
                (texture.width > 4096 || texture.height > 4096 ||
                 texture.width < static_cast<std::uint32_t>(sheet.frameWidth) ||
                 texture.width % static_cast<std::uint32_t>(sheet.frameWidth) != 0 ||
                 texture.height != static_cast<std::uint32_t>(sheet.frameHeight))) {
                SDL_Log("Invalid canvas sprite sheet dimensions");
                return false;
            }
        }
    }
    if (worldSprites != nullptr) {
        if (worldSprites->count == 0 || worldSprites->count > worldSprites->sheets.size()) {
            SDL_Log("Invalid canvas world sprite count");
            return false;
        }
        for (std::size_t index = 0; index < worldSprites->count; ++index) {
            const CanvasWorldSprite& sheet = worldSprites->sheets[index];
            const auto terminator = std::find(sheet.path.begin(), sheet.path.end(), '\0');
            if (terminator == sheet.path.end()) {
                SDL_Log("Invalid canvas world sprite path");
                return false;
            }
            const std::string_view path(sheet.path.data(),
                                        static_cast<std::size_t>(terminator - sheet.path.begin()));
            if (path.empty() || !framework::WorldModule::validSpritePath(path) ||
                !sheet.image.texture.valid() || sheet.image.width == 0 ||
                sheet.image.height == 0 || sheet.image.width > 4096 ||
                sheet.image.height > 4096) {
                SDL_Log("Invalid canvas world sprite sheet");
                return false;
            }
            const auto validMap = [&](const std::array<char, 96>& mapPath,
                                      const CanvasSprite& map) noexcept {
                const auto mapEnd = std::find(mapPath.begin(), mapPath.end(), '\0');
                if (mapEnd == mapPath.end()) return false;
                const std::string_view mapName(
                    mapPath.data(), static_cast<std::size_t>(mapEnd - mapPath.begin()));
                if (mapName.empty()) {
                    return !map.texture.valid() && map.width == 0 && map.height == 0;
                }
                return framework::WorldModule::validSpritePath(mapName) &&
                       map.texture.valid() && map.width == sheet.image.width &&
                       map.height == sheet.image.height;
            };
            const auto circuitEnd = std::find(sheet.material.emissiveCircuit.begin(),
                                              sheet.material.emissiveCircuit.end(), '\0');
            const std::string_view circuit(
                sheet.material.emissiveCircuit.data(),
                static_cast<std::size_t>(circuitEnd - sheet.material.emissiveCircuit.begin()));
            if (circuitEnd == sheet.material.emissiveCircuit.end() ||
                !validMap(sheet.material.normalSheet, sheet.normal) ||
                !validMap(sheet.material.emissiveSheet, sheet.emissive) ||
                !std::isfinite(sheet.material.emissiveIntensity) ||
                sheet.material.emissiveIntensity < 0.0F ||
                sheet.material.emissiveIntensity > 8.0F ||
                (sheet.material.emissiveSheet[0] == '\0' &&
                 (sheet.material.emissiveIntensity != 1.0F || !circuit.empty())) ||
                (!circuit.empty() &&
                 (!framework::isValidContentId(circuit) ||
                  circuit.find(":power/") == std::string_view::npos)) ||
                std::any_of(sheet.material.emissiveColor.begin(),
                            sheet.material.emissiveColor.end(),
                            [](float channel) noexcept {
                                return !std::isfinite(channel) || channel < 0.0F ||
                                       channel > 1.0F;
                            })) {
                SDL_Log("Invalid canvas world material maps or emission");
                return false;
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                const CanvasWorldSprite& candidate = worldSprites->sheets[previous];
                if (std::string_view(candidate.path.data()) == path &&
                    std::string_view(candidate.material.normalSheet.data()) ==
                        std::string_view(sheet.material.normalSheet.data()) &&
                    std::string_view(candidate.material.emissiveSheet.data()) ==
                        std::string_view(sheet.material.emissiveSheet.data()) &&
                    std::string_view(candidate.material.emissiveCircuit.data()) ==
                        std::string_view(sheet.material.emissiveCircuit.data()) &&
                    candidate.material.emissiveColor == sheet.material.emissiveColor &&
                    candidate.material.emissiveIntensity == sheet.material.emissiveIntensity) {
                    SDL_Log("Duplicate canvas world sprite material");
                    return false;
                }
            }
        }
    }
    if (!beginRecording(sink, SDL_Color{8, 15, 26, 255})) return false;
    canvasSprites_ = sprites;
    canvasWorldSprites_ = worldSprites;
    if (!drawSinkFailed_) drawGameContent(game, showMap, paused, jumpKey, shootKey, interactKey,
                                          particles, debug, damageFlashOpacity, impacts,
                                          explosions, fog);
    canvasSprites_ = nullptr;
    canvasWorldSprites_ = nullptr;
    return endRecording();
}

void Renderer::drawGameContent(const Game& game, bool showMap, bool paused,
                               std::string_view jumpKey, std::string_view shootKey,
                               std::string_view interactKey,
                               std::span<const vfx::Particle> particles,
                               const RenderDebugInfo* debug,
                               std::uint8_t damageFlashOpacity,
                               std::span<const vfx::Particle> impacts,
                               std::span<const vfx::Particle> explosions,
                               std::span<const vfx::Particle> fog) noexcept {
    const RoomLayout& room = game.currentRoom();
    const WorldLayout& world = game.world();

    if (game.shmupActive()) {
        drawShmup(game, paused);
        return;
    }

    const Vec2 shakeOffset = game.cameraShakeOffset();
    worldOffsetX_ = static_cast<int>(shakeOffset.x);
    worldOffsetY_ = static_cast<int>(shakeOffset.y);
    if (drawSink_ != nullptr) {
        rendering::FrameState lighting{};
        lighting.lightDirectionAmbient = {0, 0, 0, 1.0F};
        lighting.lightColorIntensity = {1, 1, 1, 0};
        lighting.ambientColor = {1, 1, 1, 1};

        const framework::RoomProjection projection = room.projection;
        const bool isometric = projection == framework::RoomProjection::Isometric;
        if (isometric) {
            lighting.lightScreenToWorld = {0.5F, 1.0F, -0.5F, 1.0F};
        }
        float cameraX = game.cameraX();
        float cameraY = 0.0F;
        if (isometric) {
            const float focusX = game.player().position.x + game.world().playerSize.x * 0.5F;
            const float focusY = game.player().position.y + game.world().playerSize.y * 0.5F;
            cameraX = std::floor((focusX - focusY) - 320.0F);
            cameraY = std::floor((focusX + focusY) * 0.5F - 196.0F);
        }
        const auto screenPosition = [&](float x, float y) noexcept {
            const framework::WorldPoint projected = framework::projectWorldPoint(
                projection, {x, y}, {cameraX, cameraY});
            return rendering::Vec2{projected.x + static_cast<float>(worldOffsetX_),
                                   projected.y + static_cast<float>(worldOffsetY_)};
        };
        std::array<float, 4> priorities{};
        std::size_t selectedLights = 0;
        std::size_t consideredLights = 0;
        const auto addLight = [&](rendering::PointLight light, float screenX,
                                  float screenY) noexcept {
            const float radius = light.positionRadius.w;
            const float screenRadius = radius * (isometric ? 1.41421356F : 1.0F);
            if (radius <= 0.0F || screenX + screenRadius < 0.0F ||
                screenX - screenRadius > 640.0F ||
                screenY + screenRadius < 0.0F || screenY - screenRadius > 360.0F) {
                return;
            }
            ++consideredLights;
            const float screenDx = screenX - 320.0F;
            const float screenDy = screenY - 180.0F;
            const float worldDx = lighting.lightScreenToWorld.x * screenDx +
                                  lighting.lightScreenToWorld.y * screenDy;
            const float worldDy = lighting.lightScreenToWorld.z * screenDx +
                                  lighting.lightScreenToWorld.w * screenDy;
            const float priority = worldDx * worldDx + worldDy * worldDy;
            std::size_t slot = 0;
            while (slot < selectedLights && priorities[slot] <= priority) ++slot;
            if (slot >= lighting.pointLights.size()) return;
            if (selectedLights < lighting.pointLights.size()) ++selectedLights;
            for (std::size_t move = selectedLights - 1; move > slot; --move) {
                priorities[move] = priorities[move - 1];
                lighting.pointLights[move] = lighting.pointLights[move - 1];
            }
            priorities[slot] = priority;
            lighting.pointLights[slot] = light;
        };

        const framework::RoomLighting* roomLighting =
            room.definition != nullptr ? &room.definition->lighting() : nullptr;
        const framework::RoomLighting emptyRoomLighting{};
        const framework::RoomLighting& roomLightingValue =
            roomLighting != nullptr ? *roomLighting : emptyRoomLighting;
        const Uint64 lightingTicks = SDL_GetTicks64();
        const float lightingDeltaSeconds = lightFadeTicks_ == 0
            ? 0.0F
            : static_cast<float>(std::min<double>(
                  static_cast<double>(lightingTicks - lightFadeTicks_) * 0.001, 30.0));
        lightFadeTicks_ = lightingTicks;
        if (lightFadeRoom_ != room.definition) {
            lightFadeRoom_ = room.definition;
            lightFades_ = {};
        }
        std::array<float, 3> ambientColor{};
        float ambientIntensity{};
        const framework::Status ambientStatus = framework::sampleAmbientLighting(
            game.world().worldLighting, roomLightingValue,
            static_cast<double>(SDL_GetTicks64()) * 0.001,
            ambientColor, ambientIntensity);
        if (!ambientStatus) {
            if (!powerQueryFailureLogged_) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                             "World ambient sampling failed: %.*s",
                             static_cast<int>(ambientStatus.detail.size()),
                             ambientStatus.detail.data());
                powerQueryFailureLogged_ = true;
            }
            drawSinkFailed_ = true;
        } else {
            lighting.ambientColor = {ambientColor[0], ambientColor[1],
                                     ambientColor[2], 1.0F};
            lighting.lightDirectionAmbient.w = ambientIntensity;
        }
        const framework::WorldLighting& worldLighting = game.world().worldLighting;
        if (roomLightingValue.hasDirectional || worldLighting.hasDirectional) {
            const float directionX = roomLightingValue.hasDirectional
                ? roomLightingValue.directionalX : worldLighting.directionalX;
            const float directionY = roomLightingValue.hasDirectional
                ? roomLightingValue.directionalY : worldLighting.directionalY;
            const std::array<float, 3>& color = roomLightingValue.hasDirectional
                ? roomLightingValue.directionalColor : worldLighting.directionalColor;
            const float intensity = roomLightingValue.hasDirectional
                ? roomLightingValue.directionalIntensity
                : worldLighting.directionalIntensity;
            const framework::WorldPoint projectedDirection =
                framework::projectWorldDirection(projection, {directionX, directionY});
            lighting.lightDirectionAmbient.x = projectedDirection.x;
            lighting.lightDirectionAmbient.y = projectedDirection.y;
            lighting.lightDirectionAmbient.z = -1.0F;
            lighting.lightColorIntensity = {color[0], color[1], color[2], intensity};
        }
        if (roomLighting != nullptr) {
            for (std::size_t index = 0; index < roomLighting->lightCount; ++index) {
                const framework::WorldLightDefinition& source = roomLighting->lights[index];
                bool enabled = source.enabled;
                if (source.persistence != framework::LightPersistence::Transient) {
                    if (gameState_ == nullptr) {
                        if (!lightStateWarningLogged_) {
                            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                                        "Persistent world-light overrides are unavailable; using content defaults");
                            lightStateWarningLogged_ = true;
                        }
                    } else {
                        std::array<char, 64> stateKey{};
                        if (!framework::worldLightStateKey(
                                room.definition->id(), source.id.data(),
                                stateKey.data(), stateKey.size())) {
                            SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                         "Persistent world light has an invalid state identity");
                            drawSinkFailed_ = true;
                            continue;
                        }
                        const framework::StateScope stateScope =
                            source.persistence == framework::LightPersistence::Persistent
                                ? framework::StateScope::Global
                                : framework::StateScope::World;
                        framework::StateValue stateValue{};
                        if (gameState_->get({stateScope, {}, stateKey.data()}, stateValue)) {
                            if (stateValue.type != framework::ValueType::Boolean) {
                                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                             "Persistent world-light state is not boolean");
                                drawSinkFailed_ = true;
                                continue;
                            }
                            enabled = stateValue.boolean;
                        }
                    }
                }
                bool lightActive = enabled;
                if (lightActive && source.circuit[0] != '\0') {
                    if (powerModule_ == nullptr) {
                        if (!powerQueryFailureLogged_) {
                            SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                         "World light requires an unavailable power module");
                            powerQueryFailureLogged_ = true;
                        }
                        drawSinkFailed_ = true;
                        continue;
                    }
                    bool powered{};
                    const framework::Status powerStatus =
                        powerModule_->isPowered(source.circuit.data(), powered);
                    if (!powerStatus) {
                        if (!powerQueryFailureLogged_) {
                            SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                         "World light power circuit query failed: %.*s",
                                         static_cast<int>(powerStatus.detail.size()),
                                         powerStatus.detail.data());
                            powerQueryFailureLogged_ = true;
                        }
                        drawSinkFailed_ = true;
                        continue;
                    }
                    lightActive = powered;
                }
                float fadeIntensity{};
                if (!lightFades_[index].update(
                        lightActive ? source.intensity : 0.0F,
                        lightingDeltaSeconds, source.fadeSeconds, fadeIntensity)) {
                    if (!lightFadeFailureLogged_) {
                        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                     "World light fade state received invalid values");
                        lightFadeFailureLogged_ = true;
                    }
                    drawSinkFailed_ = true;
                    continue;
                }
                if (fadeIntensity <= 0.0F) continue;
                float x = source.hasResolvedPosition ? source.resolvedX : source.x;
                float y = source.hasResolvedPosition ? source.resolvedY : source.y;
                if (source.hasResolvedPosition && room.definition != nullptr) {
                    Vec2 position{};
                    if (game.propPosition(game.roomId(), source.parent.data(), position)) {
                        for (const PropLayout& prop : game.world().propList()) {
                            if (prop.room == game.roomId() &&
                                std::string_view(prop.id.data()) == source.parent.data()) {
                                x += position.x - prop.position.x;
                                y += position.y - prop.position.y;
                                break;
                            }
                        }
                    }
                }
                if (!source.hasResolvedPosition && source.parent[0] != '\0') {
                    const std::string_view parent(source.parent.data());
                    if (parent == "player") {
                        x += game.player().position.x;
                        y += game.player().position.y;
                    } else if (parent.starts_with("enemy.")) {
                        const std::string_view entityId(parent.data() + 6, parent.size() - 6);
                        const Enemy* entity = nullptr;
                        for (const Enemy& candidate : game.enemies()) {
                            if (std::string_view(candidate.id.data()) == entityId &&
                                candidate.room == game.roomId() && candidate.alive) {
                                entity = &candidate;
                                break;
                            }
                        }
                        if (entity == nullptr) continue;
                        x += entity->bounds.x;
                        y += entity->bounds.y;
                    } else if (parent.starts_with("roamer.")) {
                        const std::string_view entityId(parent.data() + 7, parent.size() - 7);
                        const Enemy* entity = nullptr;
                        for (const Enemy& candidate : game.roamers()) {
                            if (std::string_view(candidate.id.data()) == entityId &&
                                candidate.alive) {
                                entity = &candidate;
                                break;
                            }
                        }
                        if (entity == nullptr) continue;
                        x += entity->bounds.x;
                        y += entity->bounds.y;
                    } else if (room.definition != nullptr) {
                        const framework::ObjectPlacement* placement =
                            room.definition->findPlacement(parent);
                        if (placement != nullptr) {
                            x += placement->x;
                            y += placement->y;
                        }
                    }
                }
                const rendering::Vec2 screen = screenPosition(x, y);
                rendering::PointLight light{};
                light.positionRadius = {screen.x, screen.y, source.radius * 0.5F,
                                        source.radius};
                const Uint64 ticks = SDL_GetTicks64();
                std::uint32_t phaseHash = 2166136261U;
                for (const unsigned char character : std::string_view(source.id.data())) {
                    phaseHash = (phaseHash ^ character) * 16777619U;
                }
                const float flickerPhase =
                    static_cast<float>(phaseHash % 6283U) / 1000.0F;
                const float flickerWave = std::sin(
                    static_cast<float>(ticks) * 0.001F * source.flickerFrequency *
                    6.28318530718F + flickerPhase);
                const float flicker = 1.0F - source.flickerAmount *
                    (0.5F + 0.5F * flickerWave);
                light.colorIntensity = {source.color[0], source.color[1], source.color[2],
                                        fadeIntensity * flicker};
                light.attenuation = {source.attenuation[0], source.attenuation[1],
                                     source.attenuation[2],
                                     source.castsShadows ? 1.0F : 0.0F};
                if (source.type == framework::WorldLightType::Spot) {
                    const Vec2 worldDirection = source.followsAim
                        ? (projection == framework::RoomProjection::Side
                               ? Vec2{static_cast<float>(game.player().facing), 0.0F}
                               : game.player().aim)
                        : Vec2{source.directionX, source.directionY};
                    float directionX = worldDirection.x;
                    float directionY = worldDirection.y;
                    const float length = std::hypot(directionX, directionY);
                    if (length > 0.0001F) {
                        constexpr float kDegreesToRadians = 0.01745329251994329577F;
                        light.directionCone = {
                            directionX / length, directionY / length, 0,
                            std::cos(source.coneAngle * kDegreesToRadians * 0.5F)};
                    }
                }
                addLight(light, screen.x, screen.y);
            }
        }
        const auto addProjectileLight = [&](const Projectile& projectile, bool hostile) noexcept {
            if (!projectile.active || projectile.room != game.roomId()) return;
            const float x = projectile.bounds.x + projectile.bounds.width * 0.5F;
            const float y = projectile.bounds.y + projectile.bounds.height * 0.5F;
            const rendering::Vec2 screen = screenPosition(x, y);
            rendering::PointLight light{};
            const float radius = hostile ? 72.0F : 88.0F;
            light.positionRadius = {screen.x, screen.y, radius * 0.5F, radius};
            light.colorIntensity = hostile ? rendering::Vec4{1.0F, 0.12F, 0.06F, 1.4F}
                                           : rendering::Vec4{0.25F, 0.72F, 1.0F, 1.5F};
            light.attenuation = {1.0F, 0.0F, 0.0F, 0};
            addLight(light, screen.x, screen.y);
        };
        for (const Projectile& projectile : game.projectiles()) {
            addProjectileLight(projectile, false);
        }
        for (const Projectile& projectile : game.enemyProjectiles()) {
            addProjectileLight(projectile, true);
        }
        for (const vfx::Particle& particle : explosions) {
            if (particle.age < 0.0F || particle.lifetime <= particle.age ||
                !std::isfinite(particle.position.x) || !std::isfinite(particle.position.y) ||
                !std::isfinite(particle.color.x) || !std::isfinite(particle.color.y) ||
                !std::isfinite(particle.color.z) || !std::isfinite(particle.color.w)) continue;
            const rendering::Vec2 screen = screenPosition(particle.position.x, particle.position.y);
            rendering::PointLight light{};
            const float radius = 54.0F + particle.scale * 24.0F;
            light.positionRadius = {screen.x, screen.y, radius * 0.5F, radius};
            const float intensity = std::clamp(particle.color.w, 0.0F, 1.0F) * 1.8F;
            light.colorIntensity = {particle.color.x, particle.color.y,
                                    particle.color.z, intensity};
            light.attenuation = {1.0F, 0.0F, 0.0F, 0};
            addLight(light, screen.x, screen.y);
        }
        bool casts2DShadows = false;
        float shadowRadius = 0.0F;
        for (const rendering::PointLight& light : lighting.pointLights) {
            if (light.positionRadius.w <= 0.0F || light.attenuation.w <= 0.5F) continue;
            casts2DShadows = true;
            shadowRadius = std::max(shadowRadius, light.positionRadius.w *
                (isometric ? 1.41421356F : 1.0F));
        }
        if (casts2DShadows && room.definition != nullptr) {
            std::array<float, rendering::kMax2DShadowSegments> shadowPriorities{};
            std::size_t selectedSegments{};
            std::size_t candidateCount{};
            const auto segmentPriority = [&](const rendering::Vec4& segment) noexcept {
                const float centerX = (segment.x + segment.z) * 0.5F;
                const float centerY = (segment.y + segment.w) * 0.5F;
                float nearest = std::numeric_limits<float>::max();
                for (const rendering::PointLight& light : lighting.pointLights) {
                    if (light.positionRadius.w <= 0.0F ||
                        light.attenuation.w <= 0.5F) continue;
                    const float dx = centerX - light.positionRadius.x;
                    const float dy = centerY - light.positionRadius.y;
                    nearest = std::min(nearest, dx * dx + dy * dy);
                }
                return nearest;
            };
            const auto appendEdge = [&](float ax, float ay, float bx, float by) noexcept {
                const rendering::Vec2 a = screenPosition(ax, ay);
                const rendering::Vec2 b = screenPosition(bx, by);
                if (std::max(a.x, b.x) < -shadowRadius ||
                    std::min(a.x, b.x) > 640.0F + shadowRadius ||
                    std::max(a.y, b.y) < -shadowRadius ||
                    std::min(a.y, b.y) > 360.0F + shadowRadius) {
                    return;
                }
                ++candidateCount;
                const rendering::Vec4 segment{a.x, a.y, b.x, b.y};
                const float priority = segmentPriority(segment);
                std::size_t slot = 0;
                while (slot < selectedSegments &&
                       shadowPriorities[slot] <= priority) ++slot;
                if (slot >= lighting.shadowSegments.size()) return;
                if (selectedSegments < lighting.shadowSegments.size()) {
                    ++selectedSegments;
                }
                for (std::size_t move = selectedSegments - 1; move > slot; --move) {
                    shadowPriorities[move] = shadowPriorities[move - 1];
                    lighting.shadowSegments[move] =
                        lighting.shadowSegments[move - 1];
                }
                shadowPriorities[slot] = priority;
                lighting.shadowSegments[slot] = segment;
            };
            const auto solidAt = [&](int column, int row) noexcept {
                return column >= 0 && row >= 0 &&
                       column < room.definition->columns() &&
                       row < room.definition->rows() &&
                       room.definition->collision(column, row) ==
                           framework::TileCollision::Solid;
            };
            if (!staticTileShadowEdges_) {
                constexpr std::size_t edgeCapacity =
                    framework::kMaxRoomColumns * framework::kMaxRoomRows * 4;
                staticTileShadowEdges_.reset(
                    new (std::nothrow) StaticTileShadowEdge[edgeCapacity]);
                if (!staticTileShadowEdges_) {
                    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                 "Static tile shadow-edge cache allocation failed");
                    drawSinkFailed_ = true;
                }
            }
            if (staticTileShadowEdges_ &&
                shadowGeometryRoom_ != room.definition) {
                shadowGeometryRoom_ = room.definition;
                staticTileShadowEdgeCount_ = 0;
                for (int row = 0; row < room.definition->rows(); ++row) {
                    for (int column = 0; column < room.definition->columns(); ++column) {
                        if (!solidAt(column, row)) continue;
                        const float left = static_cast<float>(column * kTileSize);
                        const float top = static_cast<float>(row * kTileSize);
                        const float right = left + static_cast<float>(kTileSize);
                        const float bottom = top + static_cast<float>(kTileSize);
                        const auto cacheEdge = [&](float ax, float ay, float bx,
                                                   float by) noexcept {
                            constexpr std::size_t edgeCapacity =
                                framework::kMaxRoomColumns *
                                framework::kMaxRoomRows * 4;
                            if (staticTileShadowEdgeCount_ == edgeCapacity) {
                                SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                             "Static tile shadow-edge cache capacity exceeded");
                                drawSinkFailed_ = true;
                                return;
                            }
                            staticTileShadowEdges_[staticTileShadowEdgeCount_++] =
                                {static_cast<std::uint16_t>(ax),
                                 static_cast<std::uint16_t>(ay),
                                 static_cast<std::uint16_t>(bx),
                                 static_cast<std::uint16_t>(by)};
                        };
                        if (!solidAt(column, row - 1)) cacheEdge(left, top, right, top);
                        if (!solidAt(column + 1, row)) cacheEdge(right, top, right, bottom);
                        if (!solidAt(column, row + 1)) cacheEdge(left, bottom, right, bottom);
                        if (!solidAt(column - 1, row)) cacheEdge(left, top, left, bottom);
                    }
                }
            }
            if (staticTileShadowEdges_) {
                for (std::size_t edge = 0; edge < staticTileShadowEdgeCount_; ++edge) {
                    const StaticTileShadowEdge& segment = staticTileShadowEdges_[edge];
                    appendEdge(static_cast<float>(segment.ax), static_cast<float>(segment.ay),
                               static_cast<float>(segment.bx), static_cast<float>(segment.by));
                }
            }
            const auto appendRectangle = [&](float x, float y, float width,
                                             float height) noexcept {
                if (width <= 0.0F || height <= 0.0F) return;
                const float right = x + width;
                const float bottom = y + height;
                appendEdge(x, y, right, y);
                appendEdge(right, y, right, bottom);
                appendEdge(x, bottom, right, bottom);
                appendEdge(x, y, x, bottom);
            };
            const Player& player = game.player();
            const float playerWidth = world.playerSize.x;
            const float playerHeight = world.playerSize.y;
            appendRectangle(player.position.x, player.position.y,
                            playerWidth, playerHeight);
            for (const Enemy& enemy : game.enemies()) {
                if (!enemy.alive || enemy.room != game.roomId()) continue;
                appendRectangle(enemy.bounds.x, enemy.bounds.y,
                                enemy.bounds.width, enemy.bounds.height);
            }
            for (const Enemy& roamer : game.roamers()) {
                if (!roamer.alive || roamer.room != game.roomId()) continue;
                appendRectangle(roamer.bounds.x, roamer.bounds.y,
                                roamer.bounds.width, roamer.bounds.height);
            }
            for (const PropLayout& prop : world.propList()) {
                if (prop.room != game.roomId() || prop.archetype == nullptr) continue;
                Vec2 position{};
                if (!game.propPosition(prop.room, prop.id.data(), position)) continue;
                appendRectangle(position.x, position.y,
                                prop.archetype->width(), prop.archetype->height());
            }
            lighting.shadowInfo.x = static_cast<float>(selectedSegments);
            if (candidateCount > lighting.shadowSegments.size() &&
                !shadowOverflowLogged_) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                            "2D shadow geometry exceeded the bounded segment list; nearest 32 edges are used");
                shadowOverflowLogged_ = true;
            }
        }
        if (consideredLights > lighting.pointLights.size() && !lightOverflowLogged_) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                        "2D light capacity exceeded; nearest four lights are rendered");
            lightOverflowLogged_ = true;
        }
        if (!drawSink_->setWorldLighting(lighting) || !drawSink_->setWorldPass(true)) {
            drawSinkFailed_ = true;
        }
    }
    panel({0, 0, 640, 360},
          SDL_Color{room.background[0], room.background[1], room.background[2], 255});
    drawingWorld_ = true;
    if (game.isometric()) {
        drawIsometricWorld(game, paused);
    } else {
        drawSideWorld(game, paused);
    }
    if (!showMap) {
        if (game.roomId() == RoomId::CargoHold) drawParticles(game, fog);
        drawParticles(game, particles);
        drawParticles(game, impacts);
        drawParticles(game, explosions);
    }
    drawingWorld_ = false;
    if (drawSink_ != nullptr && !drawSink_->setWorldPass(false)) drawSinkFailed_ = true;
    worldOffsetX_ = 0;
    worldOffsetY_ = 0;
    if (damageFlashOpacity != 0) {
        panel({0, 0, 640, 360}, SDL_Color{255, 24, 18, damageFlashOpacity});
    }
    const Player& player = game.player();

    panel({12, 12, 184, 38}, SDL_Color{4, 10, 17, 218});
    text(22, 19, "VITALS", 1, kMuted);
    for (int index = 0; index < game.maximumHealth(); ++index) {
        panel({22 + index * 22, 32, 16, 8}, index < player.health ? kRed : SDL_Color{55, 60, 67, 255});
    }
    panel({470, 12, 158, 38}, SDL_Color{4, 10, 17, 218});
    text(481, 18, game.roomName(), 1, kText);
    text(481, 32, player.hasDoubleJump ? "ECHO STEP ONLINE" : "ECHO STEP LOCKED", 1,
         player.hasDoubleJump ? kCyan : kMuted);
    panel({206, 12, 252, 38}, SDL_Color{4, 10, 17, 218});
    const bool hasMaintenanceKey = game.hasItem(ItemId::MaintenanceKey);
    std::array<char, 40> inventoryText{};
    std::snprintf(inventoryText.data(), inventoryText.size(), "KEY %s BAG %02u/20 COIL %s",
                  hasMaintenanceKey ? "YES" : "NO",
                  static_cast<unsigned>(game.inventory().size()),
                  game.equipmentEquipped("echo:equipment/kinetic_coil") ? "ON" : "OFF");
    text(218, 18, inventoryText.data(), 1, kMuted);
    const ItemStack activeItem = game.inventorySlot(Game::kActiveItemSlot);
    const std::string_view activeName = game.itemName(activeItem.item);
    std::snprintf(inventoryText.data(), inventoryText.size(), "ACTIVE %.*s X%u SCRAP %u",
                  static_cast<int>(activeName.size()), activeName.data(),
                  static_cast<unsigned>(activeItem.quantity),
                  static_cast<unsigned>(game.itemQuantity(ItemId::Scrap)));
    text(218, 32, inventoryText.data(), 1, activeItem.quantity != 0 ? kCyan : kMuted);
    const framework::QuestProgress quest = game.questProgress();
    if (quest.total != 0) {
        text(12, 54, "OBJECTIVE", 1, kMuted);
        const std::string_view status =
            quest.completed != quest.total ? quest.objective :
            game.questRewardClaimed() ? "MISSION COMPLETE" : "REWARD NEEDS BAG SPACE";
        text(76, 54, status, 1, kCyan);
    }
    if (game.roomId() == RoomId::CargoHold) {
        framework::QuestProgress cargoQuest{};
        const framework::Status cargoStatus = game.cargoQuestProgress(cargoQuest);
        if (!cargoStatus) {
            if (!cargoQuestFailureLogged_) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cargo quest progress failed: %s",
                             cargoStatus.detail.data());
                cargoQuestFailureLogged_ = true;
            }
        } else if (cargoQuest.total != 0) {
            const std::string_view objective =
                cargoQuest.completed == cargoQuest.total
                    ? "CARGO EMERGENCY POWER RESTORED"
                    : cargoQuest.objective;
            text(12, 82, "CARGO TASK", 1, kMuted);
            text(76, 82, objective, 1, kCyan);
        }
    }
    if (!paused && !game.dialogueActive() && activeItem.item != ItemId::None) {
        const int useHintY = game.roomId() == RoomId::CargoHold ? 94 : 54;
        text(481, useHintY, controls_ == ControlScheme::PlayStation ? "R1 USE" : "H USE",
             1, kCyan);
    }
    if (game.roomId() == RoomId::CargoHold) {
        bool mainPowered{};
        bool emergencyPowered{};
        const framework::Status mainStatus = powerModule_ != nullptr
            ? powerModule_->isPowered("echo:power/cargo_main", mainPowered)
            : framework::Status{framework::ErrorCode::MissingDependency, "echo.power",
                                "Power module is not available."};
        const framework::Status emergencyStatus = powerModule_ != nullptr
            ? powerModule_->isPowered("echo:power/cargo_emergency", emergencyPowered)
            : framework::Status{framework::ErrorCode::MissingDependency, "echo.power",
                                "Power module is not available."};
        panel({470, 54, 158, 26}, SDL_Color{4, 10, 17, 218});
        if (mainStatus && emergencyStatus) {
            text(481, 56, mainPowered ? "MAIN POWER ON" : "MAIN POWER OFF",
                 1, mainPowered ? kCyan : kOrange);
            text(481, 66, emergencyPowered ? "EMERGENCY ON" : "EMERGENCY OFF",
                 1, emergencyPowered ? kCyan : kOrange);
        } else {
            if (!powerQueryFailureLogged_) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Power panel status query failed: %s",
                             !mainStatus ? mainStatus.detail.data()
                                         : emergencyStatus.detail.data());
                powerQueryFailureLogged_ = true;
            }
            text(481, 60, "POWER STATUS ERROR", 1, kOrange);
        }
    }

    if (game.gateBlocked()) {
        panel({188, 274, 264, 42}, SDL_Color{4, 10, 17, 230});
        text(211, 290, "POWER GATE NEEDS ECHO STEP", 1, kOrange);
    }
    if (game.accessBlocked()) {
        panel({188, 274, 264, 42}, SDL_Color{4, 10, 17, 230});
        text(211, 290, "LAB DOOR NEEDS ACCESS KEY", 1, kOrange);
    }
    if (quest.choiceRequired) {
        panel({122, 258, 396, 62}, SDL_Color{4, 10, 17, 242});
        text(142, 267, "CHOOSE A ROUTE", 1, kCyan);
        for (std::size_t index = 0; index < quest.choices.size(); ++index) {
            const int y = 282 + static_cast<int>(index) * 12;
            if (index == game.questChoiceSelection()) panel({142, y, 4, 7}, kCyan);
            text(154, y, quest.choices[index].label.data(), 1, kText);
        }
        text(142, 306,
             controls_ == ControlScheme::PlayStation
                 ? "LEFT / RIGHT SELECT   CROSS CONFIRM"
                 : "LEFT / RIGHT SELECT   INTERACT CONFIRM",
             1, kMuted);
    }
    if (game.technicianNearby() && !game.dialogueActive()) {
        text(520, 285, controls_ == ControlScheme::PlayStation ? "\x03" : interactKey, 2, kCyan);
    }
    if (!paused && !game.dialogueActive() &&
        (game.nearbyPowerPanel(player.position) != nullptr ||
         game.nearbyDialogueProp() != nullptr)) {
        text(520, 285, controls_ == ControlScheme::PlayStation ? "\x03" : interactKey,
             2, kCyan);
    }
    bool nearDoor = false;
    for (const DoorLayout& door : world.doorList()) {
        nearDoor = nearDoor || (door.room == game.roomId() && door.visible &&
                                player.position.x > door.bounds.x - 70.0F &&
                                player.position.x < door.bounds.x + door.bounds.width + 26.0F);
    }
    if (nearDoor) {
        text(520, 285, controls_ == ControlScheme::PlayStation ? "\x03" : interactKey, 2, kCyan);
    }
    if (controls_ == ControlScheme::PlayStation) {
        text(12, 340, game.isometric()
                          ? "\x05 MOVE 8-WAY  \x02 FIRE  \x03 USE  \x07 MAP  \x06 PAUSE"
                          : "\x05 MOVE  \x01 JUMP  \x02 FIRE  \x03 USE  \x07 MAP  \x06 PAUSE",
             2, kMuted);
    } else {
        int hintX = 12;
        const auto hint = [&](std::string_view value) {
            text(hintX, 340, value, 1, kMuted);
            hintX += static_cast<int>(value.size()) * 6;
        };
        hint(game.isometric() ? "ARROWS MOVE 8-WAY  " : "ARROWS MOVE  ");
        if (!game.isometric()) {
            hint(jumpKey);
            hint(" JUMP  ");
        }
        hint(shootKey);
        hint(" FIRE  ");
        hint(interactKey);
        hint(" USE  M MAP  ESC PAUSE");
    }

    if (showMap) {
        panel({151, 92, 338, 172}, SDL_Color{4, 10, 17, 238});
        text(174, 109, "FACILITY MAP", 2, kText);
        constexpr std::array<SDL_Rect, kRoomCount> rooms{{
            {170, 170, 48, 28}, {236, 170, 65, 28}, {320, 170, 58, 28},
            {236, 124, 65, 28}, {397, 170, 74, 28}, {176, 210, 36, 20},
        }};
        panel({218, 182, 18, 3}, kMuted);
        panel({301, 182, 19, 3}, kMuted);
        panel({269, 152, 3, 18}, kMuted);
        panel({192, 198, 3, 12}, kMuted);
        panel({378, 182, 19, 3}, player.hasDoubleJump ? kCyan : kRed);
        for (int index = 0; index < static_cast<int>(rooms.size()); ++index) {
            panel(rooms[static_cast<std::size_t>(index)],
                  static_cast<int>(game.roomId()) == index ? kCyan : kPanelLight);
        }
        text(172, 238, player.hasDoubleJump ? "GATE OPEN" : "RED LINE LOCKED", 1,
             player.hasDoubleJump ? kGreen : kOrange);
    }

    if (game.completed()) {
        panel({128, 128, 384, 106}, SDL_Color{4, 10, 17, 242});
        text(194, 153, "SIGNAL RESTORED", 3, kCyan);
        text(185, 196, "FACILITY EXIT REACHED", 1, kText);
    }
    if (game.dialogueActive()) {
        panel({64, 210, 512, 106}, SDL_Color{4, 10, 17, 242});
        panel({64, 210, 512, 3}, kCyan);
        text(82, 220, game.dialogueSpeaker(), 2, kText);
        text(82, 246, game.dialogueText(), 1, kMuted);
        if (game.dialogueChoiceCount() != 0) {
            std::array<char, 16> ordinal{};
            std::snprintf(ordinal.data(), ordinal.size(), "%u/%u",
                          static_cast<unsigned>(game.selectedDialogueChoice() + 1),
                          static_cast<unsigned>(game.dialogueChoiceCount()));
            text(82, 268, ordinal.data(), 1, kMuted);
            text(124, 268, game.dialogueChoiceText(game.selectedDialogueChoice()), 1, kCyan);
            text(82, 298, controls_ == ControlScheme::PlayStation
                              ? "\x05 CHOOSE" : "LEFT/RIGHT CHOOSE", 1, kMuted);
            text(474, 298, "SELECT", 1, kCyan);
        } else {
            text(486, 298, "NEXT", 1, kCyan);
        }
        text(538, 296, controls_ == ControlScheme::PlayStation ? "\x03" : interactKey, 2, kCyan);
    }
    if (paused) {
        panel({172, 125, 296, 132}, SDL_Color{4, 10, 17, 238});
        text(245, 146, "PAUSED", 3, kText);
        text(195, 199, controls_ == ControlScheme::PlayStation ? "\x01 RESUME  \x04 MENU" : "ENTER RESUME  ESC MENU", 1, kMuted);
        text(245, 224, controls_ == ControlScheme::PlayStation
                          ? "L1 INVENTORY" : "I INVENTORY", 1, kCyan);
    }
    if (game.cutsceneActive()) {
        panel({64, 210, 512, 106}, SDL_Color{4, 10, 17, 244});
        panel({64, 210, 512, 3}, kCyan);
        std::array<char, 24> cardNumber{};
        std::snprintf(cardNumber.data(), cardNumber.size(), "SIGNAL %u/%u",
                      static_cast<unsigned>(game.cutsceneCardIndex() + 1),
                      static_cast<unsigned>(game.cutsceneCardCount()));
        text(82, 220, cardNumber.data(), 1, kMuted);
        text(82, 240, game.cutsceneSpeaker(), 1, kCyan);
        text(82, 260, game.cutsceneText(), 1, kText);
        text(82, 298, controls_ == ControlScheme::PlayStation
                          ? "\x03 CONTINUE" : interactKey, 1, kMuted);
    }
    if (debug != nullptr) {
        panel({470, 68, 158, 53}, SDL_Color{4, 10, 17, 230});
        text(478, 74, debug->vulkan ? "RENDER: VULKAN" : "RENDER: SDL", 1, kCyan);
        std::array<char, 40> line{};
        if (debug->timingAvailable) {
            std::snprintf(line.data(), line.size(), "FPS %u  FRAME %u.%u MS",
                          debug->fps, debug->frameTenthsMs / 10, debug->frameTenthsMs % 10);
            text(478, 88, line.data(), 1, kText);
        } else {
            text(478, 88, "FPS: SAMPLING", 1, kMuted);
        }
        if (debug->backendStatsAvailable) {
            std::snprintf(line.data(), line.size(), "DRAWS %u  TRI %u",
                          debug->drawCalls, debug->triangles);
            text(478, 102, line.data(), 1, kText);
        } else {
            text(478, 102, "CANVAS 640X360", 1, kMuted);
        }
    }
}

void Renderer::drawInventory(const Game& game, const InventoryMenu& menu,
                             std::string_view message) noexcept {
    SDL_SetRenderDrawColor(renderer_, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer_);
    drawInventoryContent(game, menu, message);
    SDL_RenderPresent(renderer_);
}

bool Renderer::recordInventory(rendering::ICanvas2D& sink, const Game& game,
                               const InventoryMenu& menu, std::string_view message) noexcept {
    if (!beginRecording(sink, kBackground)) return false;
    if (!drawSinkFailed_) drawInventoryContent(game, menu, message);
    return endRecording();
}

void Renderer::drawInventoryContent(const Game& game, const InventoryMenu& menu,
                                    std::string_view message) noexcept {
    LegacyUiCanvas legacy{renderer_};
    rendering::ICanvas2D& canvas = drawSink_ != nullptr ? *drawSink_ : legacy;
    if (!drawInventoryMenu(canvas, controls_, game, menu.selected, menu.source, message)) {
        drawSinkFailed_ = true;
        if (drawSink_ == nullptr) SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Inventory drawing failed: %s", SDL_GetError());
    }
}

void Renderer::drawDualSenseLab(const input::ControllerSnapshot& controller,
                                int connectedControllers) noexcept {
    SDL_SetRenderDrawColor(renderer_, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer_);
    drawDualSenseLabContent(controller, connectedControllers);
    SDL_RenderPresent(renderer_);
}

bool Renderer::recordDualSenseLab(rendering::ICanvas2D& sink,
                                  const input::ControllerSnapshot& controller,
                                  int connectedControllers) noexcept {
    if (!beginRecording(sink, kBackground)) return false;
    if (!drawSinkFailed_) drawDualSenseLabContent(controller, connectedControllers);
    return endRecording();
}

void Renderer::outline(SDL_Rect bounds, SDL_Color color) noexcept {
    if (drawSink_ == nullptr) {
        setColor(renderer_, color);
        SDL_RenderDrawRect(renderer_, &bounds);
        return;
    }
    panel({bounds.x, bounds.y, bounds.w, 1}, color);
    panel({bounds.x, bounds.y + bounds.h - 1, bounds.w, 1}, color);
    panel({bounds.x, bounds.y + 1, 1, bounds.h - 2}, color);
    panel({bounds.x + bounds.w - 1, bounds.y + 1, 1, bounds.h - 2}, color);
}

void Renderer::drawDualSenseLabContent(const input::ControllerSnapshot& controller,
                                       int connectedControllers) noexcept {
    panel({12, 12, 310, 310}, kPanel);
    panel({330, 12, 298, 310}, kPanel);
    panel({12, 12, 616, 3}, kCyan);
    text(24, 24, "DUALSENSE LAB", 2, kText);

    if (controller.connected) {
        const std::string_view name(controller.name.data());
        text(24, 48, {name.data(), std::min<std::size_t>(name.size(), 34)}, 1, kCyan);
        std::array<char, 96> deviceLine{};
        SDL_snprintf(deviceLine.data(), deviceLine.size(), "ID %d  BATTERY %.*s",
                     controller.instanceId, static_cast<int>(input::batteryName(controller.battery).size()),
                     input::batteryName(controller.battery).data());
        text(24, 62, deviceLine.data(), 1, kMuted);
    } else {
        text(24, 50, "NO CONTROLLER CONNECTED", 1, kOrange);
    }
    std::array<char, 48> deviceCountLine{};
    SDL_snprintf(deviceCountLine.data(), deviceCountLine.size(), "SDL DEVICES %d", connectedControllers);
    text(24, 76, deviceCountLine.data(), 1, kMuted);

    const auto button = [&controller](SDL_GameControllerButton id) noexcept {
        const auto index = static_cast<std::size_t>(id);
        return index < controller.buttons.size() && controller.buttons[index];
    };
    const auto buttonLabel = [this, &button](int x, int y, int width, std::string_view label,
                                             SDL_GameControllerButton id, bool mapped = true) {
        const SDL_Color fill = !mapped ? SDL_Color{39, 43, 51, 255}
                                : button(id) ? SDL_Color{33, 91, 73, 255}
                                             : kPanelLight;
        panel({x, y, width, 16}, fill);
        text(x + 3, y + 4, label, 1, !mapped ? kMuted : button(id) ? kGreen : kText);
        if (!mapped) {
            text(x + width - 18, y + 4, "N/A", 1, kMuted);
        }
    };
    const auto triggerLabel = [this, &controller](int x, int y, std::string_view label,
                                                  SDL_GameControllerAxis axis) {
        const bool pressed = controller.axes[axis] > 16000;
        panel({x, y, 48, 16}, pressed ? SDL_Color{33, 91, 73, 255} : kPanelLight);
        text(x + 3, y + 4, label, 1, pressed ? kGreen : kText);
    };

    text(24, 88, "CONTROLS", 1, kMuted);
    triggerLabel(24, 101, "L2", SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    buttonLabel(78, 101, 48, "L1", SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    buttonLabel(208, 101, 48, "R1", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    triggerLabel(262, 101, "R2", SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    buttonLabel(48, 123, 48, "L3", SDL_CONTROLLER_BUTTON_LEFTSTICK);
    buttonLabel(238, 123, 48, "R3", SDL_CONTROLLER_BUTTON_RIGHTSTICK);

    buttonLabel(94, 151, 34, "UP", SDL_CONTROLLER_BUTTON_DPAD_UP);
    buttonLabel(57, 171, 36, "LEFT", SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    buttonLabel(94, 171, 34, "DOWN", SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    buttonLabel(129, 171, 42, "RIGHT", SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    buttonLabel(229, 145, 44, "TRI", SDL_CONTROLLER_BUTTON_Y);
    buttonLabel(192, 163, 38, "SQR", SDL_CONTROLLER_BUTTON_X);
    buttonLabel(273, 163, 38, "CIR", SDL_CONTROLLER_BUTTON_B);
    buttonLabel(229, 181, 44, "X", SDL_CONTROLLER_BUTTON_A);

    buttonLabel(45, 205, 72, "OPTIONS", SDL_CONTROLLER_BUTTON_START);
    buttonLabel(125, 205, 84, "TOUCHPAD", SDL_CONTROLLER_BUTTON_TOUCHPAD);
    buttonLabel(217, 205, 70, "CREATE", SDL_CONTROLLER_BUTTON_BACK,
                controller.connected && controls_ != ControlScheme::PlayStation);
    buttonLabel(45, 225, 72, "PS", SDL_CONTROLLER_BUTTON_GUIDE,
                controller.connected && controls_ != ControlScheme::PlayStation);
    buttonLabel(125, 225, 84, "MUTE", SDL_CONTROLLER_BUTTON_MISC1,
                controller.connected && controls_ != ControlScheme::PlayStation);
    text(24, 250, "LEFT STICK", 1, kMuted);
    text(184, 250, "RIGHT STICK", 1, kMuted);

    const auto stick = [this, &controller](int x, int y, SDL_GameControllerAxis axisX,
                                           SDL_GameControllerAxis axisY) {
        const SDL_Rect bounds{x - 19, y - 19, 38, 38};
        outline(bounds, kMuted);
        if (drawSink_ != nullptr) {
            panel({x - 19, y, 39, 1}, kMuted);
            panel({x, y - 19, 1, 39}, kMuted);
        } else {
            SDL_RenderDrawLine(renderer_, x - 19, y, x + 19, y);
            SDL_RenderDrawLine(renderer_, x, y - 19, x, y + 19);
        }
        const auto raw = input::normalizeStick(controller.axes[axisX], controller.axes[axisY]);
        const auto value = input::applyRadialDeadZone(raw, controller.deadZone);
        const SDL_Rect dot{x + static_cast<int>(value.x * 15.0F) - 3,
                           y + static_cast<int>(value.y * 15.0F) - 3, 6, 6};
        panel(dot, kCyan);
    };
    stick(82, 286, SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY);
    stick(232, 286, SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY);

    text(342, 25, "LIVE VALUES", 1, kMuted);
    const auto valueLine = [this](int x, int y, const char* label, float value) {
        std::array<char, 64> line{};
        SDL_snprintf(line.data(), line.size(), "%s %+.2f", label, static_cast<double>(value));
        text(x, y, line.data(), 1, kText);
    };
    const auto leftStick = input::applyRadialDeadZone(input::normalizeStick(
        controller.axes[SDL_CONTROLLER_AXIS_LEFTX], controller.axes[SDL_CONTROLLER_AXIS_LEFTY]), controller.deadZone);
    const auto rightStick = input::applyRadialDeadZone(input::normalizeStick(
        controller.axes[SDL_CONTROLLER_AXIS_RIGHTX], controller.axes[SDL_CONTROLLER_AXIS_RIGHTY]), controller.deadZone);
    valueLine(344, 43, "LX/1K", controller.axes[SDL_CONTROLLER_AXIS_LEFTX] / 1000.0F);
    valueLine(480, 43, "RX/1K", controller.axes[SDL_CONTROLLER_AXIS_RIGHTX] / 1000.0F);
    valueLine(344, 57, "LY/1K", controller.axes[SDL_CONTROLLER_AXIS_LEFTY] / 1000.0F);
    valueLine(480, 57, "RY/1K", controller.axes[SDL_CONTROLLER_AXIS_RIGHTY] / 1000.0F);
    std::array<char, 64> normalizedLine{};
    SDL_snprintf(normalizedLine.data(), normalizedLine.size(), "NORM LX%+.2f LY%+.2f",
                 static_cast<double>(leftStick.x), static_cast<double>(leftStick.y));
    text(344, 71, normalizedLine.data(), 1, kText);
    std::array<char, 64> normalizedRightLine{};
    SDL_snprintf(normalizedRightLine.data(), normalizedRightLine.size(), "NORM RX%+.2f RY%+.2f",
                 static_cast<double>(rightStick.x), static_cast<double>(rightStick.y));
    text(480, 71, normalizedRightLine.data(), 1, kText);
    std::array<char, 64> triggerLine{};
    SDL_snprintf(triggerLine.data(), triggerLine.size(), "L2 %d %0.f%%  R2 %d %0.f%%",
                 controller.axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT],
                 static_cast<double>(input::normalizeTrigger(controller.axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT]) * 100.0F),
                 controller.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT],
                 static_cast<double>(input::normalizeTrigger(controller.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT]) * 100.0F));
    text(344, 85, triggerLine.data(), 1, kText);

    std::array<char, 48> deadZoneLine{};
    SDL_snprintf(deadZoneLine.data(), deadZoneLine.size(), "RADIAL DEAD ZONE %d%%",
                 static_cast<int>(controller.deadZone * 100.0F));
    text(344, 99, deadZoneLine.data(), 1, kMuted);
    text(344, 115, "CAPABILITIES", 1, kMuted);
    text(344, 131, controller.touchpadApiAvailable ? "TOUCH CONTACT API AVAILABLE" : "TOUCH CONTACT UNSUPPORTED", 1,
         controller.touchpadApiAvailable ? kGreen : kOrange);
    if (controller.touchpadApiAvailable) {
        std::array<char, 64> touchLine{};
        SDL_snprintf(touchLine.data(), touchLine.size(), "PADS %d  FINGER SLOTS %d",
                     controller.touchpadCount, controller.touchpadFingerCount);
        text(344, 144, touchLine.data(), 1, kText);
        const SDL_Rect touchpadBounds{344, 158, 124, 26};
        outline(touchpadBounds, kMuted);
        for (std::size_t index = 0; index < controller.touchSampleCount; ++index) {
            const auto& touch = controller.touches[index];
            if (!touch.active || touch.touchpad != 0) {
                continue;
            }
            panel({344 + static_cast<int>(input::normalizeTouchCoordinate(touch.x) * 120.0F) - 2,
                   158 + static_cast<int>(input::normalizeTouchCoordinate(touch.y) * 22.0F) - 2, 5, 5}, kCyan);
        }
        float maxPressure = 0.0F;
        for (std::size_t index = 0; index < controller.touchSampleCount; ++index) {
            const auto& touch = controller.touches[index];
            if (touch.active) {
                maxPressure = std::max(maxPressure, touch.pressure);
            }
        }
        std::array<char, 48> pressureLine{};
        SDL_snprintf(pressureLine.data(), pressureLine.size(), "ACTIVE PRESSURE %.2f",
                     static_cast<double>(maxPressure));
        text(480, 165, pressureLine.data(), 1, kText);
    } else {
        text(344, 144, "CLICK BUTTON IS SEPARATE", 1, kMuted);
    }
    text(344, 190, controller.gyroAvailable ? "GYRO API AVAILABLE" : "GYRO UNSUPPORTED", 1,
         controller.gyroAvailable ? kGreen : kOrange);
    text(344, 203, controller.accelerometerAvailable ? "ACCEL API AVAILABLE" : "ACCEL UNSUPPORTED", 1,
         controller.accelerometerAvailable ? kGreen : kOrange);
    if (controller.gyroRead) {
        std::array<char, 80> line{};
        SDL_snprintf(line.data(), line.size(), "GYRO RAD/S %+.1f %+.1f %+.1f",
                     static_cast<double>(controller.gyro[0]), static_cast<double>(controller.gyro[1]),
                     static_cast<double>(controller.gyro[2]));
        text(344, 216, line.data(), 1, kText);
    }
    if (controller.accelerometerRead) {
        std::array<char, 80> line{};
        SDL_snprintf(line.data(), line.size(), "ACCEL M/S2 %+.1f %+.1f %+.1f",
                     static_cast<double>(controller.accelerometer[0]), static_cast<double>(controller.accelerometer[1]),
                     static_cast<double>(controller.accelerometer[2]));
        text(344, 229, line.data(), 1, kText);
    }
    const auto outputStatus = [&](bool available, bool confirmed, bool failed, bool pending,
                                  const char* label, int y) {
        std::array<char, 48> line{};
        SDL_Color color = kOrange;
        if (confirmed) {
            SDL_snprintf(line.data(), line.size(), "%s CONFIRMED", label);
            color = kGreen;
        } else if (failed) {
            SDL_snprintf(line.data(), line.size(), "%s FAILED", label);
        } else if (pending) {
            SDL_snprintf(line.data(), line.size(), "%s CHECK %s", label,
                         controls_ == ControlScheme::PlayStation ? "L1 OK / R1 FAIL" : "P OK / F FAIL");
            color = kCyan;
        } else if (available) {
            SDL_snprintf(line.data(), line.size(), "%s API AVAILABLE", label);
            color = kGreen;
        } else {
            SDL_snprintf(line.data(), line.size(), "%s UNSUPPORTED", label);
        }
        text(344, y, line.data(), 1, color);
    };
    outputStatus(controller.rumbleAvailable, controller.rumbleHardwareConfirmed, controller.rumbleHardwareFailed,
                 controller.pendingConfirmation == input::OutputTest::Rumble, "RUMBLE", 246);
    outputStatus(controller.triggerRumbleAvailable, controller.triggerRumbleHardwareConfirmed,
                 controller.triggerRumbleHardwareFailed,
                 controller.pendingConfirmation == input::OutputTest::TriggerRumble, "TRIGGER RUMBLE", 259);
    outputStatus(controller.ledAvailable, controller.ledHardwareConfirmed, controller.ledHardwareFailed,
                 controller.pendingConfirmation == input::OutputTest::Led, "RGB LED", 272);
    text(344, 285, "ADAPTIVE TRIGGERS UNSUPPORTED", 1, kOrange);
    std::array<char, 88> audioLine{};
    SDL_snprintf(audioLine.data(), audioLine.size(), "AUDIO OUT %d  CAPTURE %d",
                 controller.outputDeviceCount, controller.captureDeviceCount);
    text(344, 298, audioLine.data(), 1, kMuted);
    std::array<char, 80> rangeLine{};
    SDL_snprintf(rangeLine.data(), rangeLine.size(), "CAL LX %d..%d RX %d..%d",
                 controller.axisMinimum[SDL_CONTROLLER_AXIS_LEFTX],
                 controller.axisMaximum[SDL_CONTROLLER_AXIS_LEFTX],
                 controller.axisMinimum[SDL_CONTROLLER_AXIS_RIGHTX],
                 controller.axisMaximum[SDL_CONTROLLER_AXIS_RIGHTX]);
    text(344, 311, rangeLine.data(), 1, kMuted);

    text(24, 310, std::string_view(controller.lastAction.data()), 1,
         controller.lastError[0] == '\0' ? kCyan : kOrange);
    if (controller.hasResult) {
        std::array<char, 48> resultLine{};
        SDL_snprintf(resultLine.data(), resultLine.size(), "RESULT %d", controller.lastResult);
        text(200, 310, resultLine.data(), 1, controller.lastError[0] == '\0' ? kMuted : kOrange);
    }
    text(24, 326, controls_ == ControlScheme::PlayStation
             ? "DPAD LEFT/RIGHT DEVICE  UP/DOWN DEAD ZONE"
             : "ARROWS LEFT/RIGHT DEVICE  UP/DOWN DEAD ZONE",
         1, kMuted);
    text(24, 340, controls_ == ControlScheme::PlayStation
             ? "\x01 RUMBLE  \x02 LED  L3 TRIG  L1 OK  R1 FAIL  \x03 EXPORT  \x04 BACK"
             : "R RUMBLE  L LED  T TRIG  P OK  F FAIL  E EXPORT  ESC BACK",
         1, kMuted);
}

} // namespace echo::platform
