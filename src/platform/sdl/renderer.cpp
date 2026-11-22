#include "platform/sdl/renderer.hpp"

#include "platform/sdl/assets.hpp"
#include "platform/sdl/bitmap_font.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
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

struct Glyph {
    char character;
    std::array<std::uint8_t, 7> rows;
};

constexpr Glyph kGlyphs[] = {
    // Internal bitmap symbols: cross, square, triangle, circle, D-pad, Options, touchpad.
    {'\x01', {17, 17, 10, 4, 10, 17, 17}}, {'\x02', {31, 17, 17, 17, 17, 17, 31}},
    {'\x03', {4, 4, 10, 10, 17, 17, 31}}, {'\x04', {14, 17, 17, 17, 17, 17, 14}},
    {'\x05', {4, 4, 31, 31, 31, 4, 4}}, {'\x06', {31, 0, 31, 0, 31, 0, 0}},
    {'\x07', {31, 17, 21, 21, 17, 31, 0}},
    {'+', {0, 4, 4, 31, 4, 4, 0}}, {'%', {17, 2, 4, 8, 17, 0, 0}},
    {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {31, 4, 4, 4, 4, 4, 31}}, {'J', {7, 2, 2, 2, 18, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}}, {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}}, {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}}, {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}}, {'5', {31, 16, 16, 30, 1, 1, 30}},
    {'6', {14, 16, 16, 30, 17, 17, 14}}, {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 1, 14}},
    {':', {0, 4, 4, 0, 4, 4, 0}}, {'-', {0, 0, 0, 31, 0, 0, 0}},
    {'.', {0, 0, 0, 0, 0, 12, 12}}, {'/', {1, 2, 2, 4, 8, 8, 16}},
};

void setColor(SDL_Renderer* renderer, SDL_Color color) noexcept {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

struct SpriteSheet {
    const char* file;
    int frameWidth;
    int frameHeight;
};

constexpr std::array<SpriteSheet, static_cast<int>(Sprite::Count)> kSpriteSheets{{
    {"sprites/player.png", 24, 32},
    {"sprites/enemy.png", 32, 32},
    {"sprites/tiles.png", 32, 32},
    {"sprites/projectile.png", 12, 8},
    {"sprites/pickup.png", 32, 32},
    {"sprites/door.png", 44, 48},
}};

// Player sheet layout: idle 0-1, run 2-5, jump 6, fall 7.
int playerFrame(const Player& player, Uint64 ticks) noexcept {
    if (!player.onGround) {
        return player.velocity.y < 0.0F ? 6 : 7;
    }
    if (std::abs(player.velocity.x) > 20.0F) {
        return 2 + static_cast<int>((ticks / 90U) % 4U);
    }
    return static_cast<int>((ticks / 500U) % 2U);
}

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
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
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
    const framework::AnimationSpec* animation = object->animationOrDefault(animationName);
    if (sheet == nullptr || animation == nullptr || animation->frames <= 0 ||
        spec.frameWidth <= 0 || spec.frameHeight <= 0) {
        return false;
    }
    const auto frames = static_cast<Uint64>(animation->frames);
    const Uint64 frame = animation->loop
        ? (ticks * static_cast<Uint64>(std::max(animation->fps, 0)) / 1000U) % frames
        : frames - 1U;
    const SDL_Rect source{(animation->column + static_cast<int>(frame)) * spec.frameWidth,
                          animation->row * spec.frameHeight, spec.frameWidth, spec.frameHeight};
    if (source.x < 0 || source.y < 0 || source.x + source.w > sheet->width ||
        source.y + source.h > sheet->height) {
        return false;
    }
    // Mirroring keeps the frame aligned with the collision box.
    const int offsetX = flip
        ? static_cast<int>(bounds.width) - spec.frameWidth - spec.offsetX
        : spec.offsetX;
    const SDL_Rect destination{static_cast<int>(bounds.x - cameraX) + offsetX,
                               static_cast<int>(bounds.y) + spec.offsetY,
                               spec.frameWidth, spec.frameHeight};
    return SDL_RenderCopyEx(renderer_, sheet->texture, &source, &destination, 0.0, nullptr,
                            flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE) == 0;
}

bool Renderer::drawRoomLayers(const RoomLayout& room, float cameraX) noexcept {
    const framework::RoomDefinition* definition = room.definition;
    if (definition == nullptr) return false;
    const int firstColumn = std::max(0, static_cast<int>(cameraX) / kTileSize);
    const int lastColumn = std::min(room.columns - 1, firstColumn + 640 / kTileSize + 1);
    for (std::size_t layer = 0; layer < definition->layers().size(); ++layer) {
        for (int row = 0; row < definition->rows(); ++row) {
            for (int column = firstColumn; column <= lastColumn; ++column) {
                const framework::TileType* tile = definition->tile(layer, column, row);
                if (tile == nullptr) continue;
                const framework::TilesetSpec& tileset = definition->tilesets()[tile->tilesetIndex];
                const WorldSheet* sheet = worldSheet(tileset.sheet.data());
                const SDL_Rect source{
                    tileset.margin + tile->column * (tileset.tileWidth + tileset.spacing),
                    tileset.margin + tile->row * (tileset.tileHeight + tileset.spacing),
                    tileset.tileWidth, tileset.tileHeight};
                const SDL_Rect destination{static_cast<int>(column * kTileSize - cameraX),
                                           row * kTileSize, kTileSize, kTileSize};
                if (sheet == nullptr || source.x + source.w > sheet->width ||
                    source.y + source.h > sheet->height) {
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
                SDL_RenderCopyEx(renderer_, sheet->texture, &source, &destination,
                                 static_cast<double>(tile->rotation), nullptr,
                                 static_cast<SDL_RendererFlip>(flags));
            }
        }
    }
    return true;
}

bool Renderer::sprite(Sprite sprite, int frame, SDL_Rect destination, bool flip) noexcept {
    const auto index = static_cast<std::size_t>(sprite);
    SDL_Texture* texture = sprites_[index];
    if (texture == nullptr) {
        return false;
    }
    int width = 0;
    SDL_QueryTexture(texture, nullptr, nullptr, &width, nullptr);
    const SpriteSheet& sheet = kSpriteSheets[index];
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
    if (drawSink_ != nullptr) {
        if (!drawSinkFailed_ && !drawSink_->fill(bounds, color)) drawSinkFailed_ = true;
    } else {
        SDL_RenderFillRect(renderer_, &bounds);
    }
}

void Renderer::text(int x, int y, std::string_view value, int scale, SDL_Color color) noexcept {
    if (drawSink_ == nullptr) setColor(renderer_, color);
    std::size_t offset{};
    while (offset < value.size()) {
        std::uint32_t codePoint{};
        if (!nextUtf8CodePoint(value, offset, codePoint)) {
            ++offset;
            continue;
        }
        if (codePoint == ' ') {
            x += 6 * scale;
            continue;
        }

        char character{};
        enum class Accent { None, Acute, Caron, Ring } accent{Accent::None};
        if (codePoint < 0x80U) {
            character = static_cast<char>(
                std::toupper(static_cast<unsigned char>(codePoint)));
        } else {
            switch (codePoint) {
            case 0x00C1: case 0x00E1: character = 'A'; accent = Accent::Acute; break;
            case 0x010C: case 0x010D: character = 'C'; accent = Accent::Caron; break;
            case 0x010E: case 0x010F: character = 'D'; accent = Accent::Caron; break;
            case 0x00C9: case 0x00E9: character = 'E'; accent = Accent::Acute; break;
            case 0x011A: case 0x011B: character = 'E'; accent = Accent::Caron; break;
            case 0x00CD: case 0x00ED: character = 'I'; accent = Accent::Acute; break;
            case 0x0147: case 0x0148: character = 'N'; accent = Accent::Caron; break;
            case 0x00D3: case 0x00F3: character = 'O'; accent = Accent::Acute; break;
            case 0x0158: case 0x0159: character = 'R'; accent = Accent::Caron; break;
            case 0x0160: case 0x0161: character = 'S'; accent = Accent::Caron; break;
            case 0x0164: case 0x0165: character = 'T'; accent = Accent::Caron; break;
            case 0x00DA: case 0x00FA: character = 'U'; accent = Accent::Acute; break;
            case 0x016E: case 0x016F: character = 'U'; accent = Accent::Ring; break;
            case 0x00DD: case 0x00FD: character = 'Y'; accent = Accent::Acute; break;
            case 0x017D: case 0x017E: character = 'Z'; accent = Accent::Caron; break;
            default: break;
            }
        }
        const auto glyph = std::find_if(std::begin(kGlyphs), std::end(kGlyphs),
                                        [character](const Glyph& item) {
                                            return item.character == character;
                                        });
        if (glyph != std::end(kGlyphs)) {
            if (accent == Accent::Acute) {
                SDL_Rect first{x + 3 * scale, y, scale, scale};
                SDL_Rect second{x + 2 * scale, y + scale, scale, scale};
                fillRecordedOrSdl(first, color);
                fillRecordedOrSdl(second, color);
            } else if (accent == Accent::Caron) {
                SDL_Rect left{x + scale, y, scale, scale};
                SDL_Rect right{x + 3 * scale, y, scale, scale};
                SDL_Rect middle{x + 2 * scale, y + scale, scale, scale};
                fillRecordedOrSdl(left, color);
                fillRecordedOrSdl(right, color);
                fillRecordedOrSdl(middle, color);
            } else if (accent == Accent::Ring) {
                SDL_Rect top{x + 2 * scale, y, scale, scale};
                SDL_Rect left{x + scale, y + scale, scale, scale};
                SDL_Rect right{x + 3 * scale, y + scale, scale, scale};
                fillRecordedOrSdl(top, color);
                fillRecordedOrSdl(left, color);
                fillRecordedOrSdl(right, color);
            }
            const int glyphOffset = accent == Accent::None ? 0 : 2;
            for (int row = 0; row < 7; ++row) {
                for (int column = 0; column < 5; ++column) {
                    if ((glyph->rows[static_cast<std::size_t>(row)] & (1U << (4 - column))) == 0) {
                        continue;
                    }
                    SDL_Rect pixel{x + column * scale, y + (row + glyphOffset) * scale,
                                   scale, scale};
                    fillRecordedOrSdl(pixel, color);
                }
            }
        }
        x += 6 * scale;
    }
}

void Renderer::drawMenu(int selection, bool continueAvailable) noexcept {
    SDL_SetRenderDrawColor(renderer_, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer_);
    drawMenuContent(selection, continueAvailable);
    SDL_RenderPresent(renderer_);
}

bool Renderer::beginRecording(DrawSink& sink, SDL_Color background) noexcept {
    if (drawSink_ != nullptr) return false;
    drawSink_ = &sink;
    drawSinkFailed_ = !sink.clear(background);
    return true;
}

bool Renderer::endRecording() noexcept {
    drawSink_ = nullptr;
    return !drawSinkFailed_;
}

bool Renderer::recordMenu(DrawSink& sink, int selection, bool continueAvailable) noexcept {
    if (!beginRecording(sink, kBackground)) return false;
    if (!drawSinkFailed_) drawMenuContent(selection, continueAvailable);
    return endRecording();
}

void Renderer::drawMenuContent(int selection, bool continueAvailable) noexcept {
    for (int y = 34; y < 360; y += 32) {
        panel({0, y, 640, 1}, SDL_Color{18, 35, 48, 255});
    }
    panel({74, 62, 492, 244}, kPanel);
    panel({74, 62, 492, 3}, kCyan);
    text(120, 94, "PROJECT ECHO", 4, kText);
    text(122, 135, "FACILITY 07  /  SIGNAL LOST", 1, kMuted);

    constexpr std::array<std::string_view, 5> labels{
        "NEW GAME", "CONTINUE", "OPTIONS", "DUALSENSE LAB", "EXIT"};
    for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
        const int y = 169 + index * 27;
        if (index == selection) {
            panel({112, y - 4, 230, 22}, kPanelLight);
            panel({112, y - 5, 3, 24}, kCyan);
        }
        const SDL_Color color = index == 1 && !continueAvailable
                                    ? kMuted
                                    : index == selection ? kCyan : kText;
        text(128, y, labels[static_cast<std::size_t>(index)], index == 3 ? 1 : 2, color);
    }

    if (controls_ == ControlScheme::PlayStation) {
        text(86, 326, "\x05 SELECT  \x01 CONFIRM", 2, kMuted);
    } else {
        text(86, 326, "ARROWS SELECT  ENTER CONFIRM", 1, kMuted);
    }
}

void Renderer::drawOptions(int selection, int volume,
                           framework::DialogueLanguage language, bool waitingForKey,
                           std::string_view jumpKey, std::string_view shootKey,
                           std::string_view interactKey, DisplayMode display) noexcept {
    SDL_SetRenderDrawColor(renderer_, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer_);
    panel({74, 38, 492, 284}, kPanel);
    panel({74, 38, 492, 3}, kCyan);
    text(110, 62, "OPTIONS", 3, kText);
    if (controls_ == ControlScheme::PlayStation) {
        text(110, 94, "FIXED CONTROLLER BUTTONS", 1, kMuted);
    }

    std::array<std::string_view, 7> rows{
        "VOLUME", "JUMP", "FIRE", "INTERACT", "DIALOGUE", "DISPLAY", "BACK"
    };
    rows[static_cast<std::size_t>(optionCount() - 1)] = "BACK";
    const int spacing = optionCount() > 6 ? 25 : 28;
    for (int index = 0; index < optionCount(); ++index) {
        const int y = 110 + index * spacing;
        if (selection == index) {
            panel({102, y - 5, 436, 25}, kPanelLight);
            panel({102, y - 5, 3, 25}, kCyan);
        }
        text(118, y, rows[static_cast<std::size_t>(index)], 2,
             selection == index ? kCyan : kText);
        if (index == 0) {
#ifdef __PROSPERO__
            std::array<char, 16> percentage{};
            SDL_snprintf(percentage.data(), percentage.size(), "%d%%", volume);
            text(390, y, percentage.data(), 2, kText);
#else
            text(390, y, std::to_string(volume) + "%", 2, kText);
#endif
        } else if (index < 4) {
            const std::string_view keyName = controls_ == ControlScheme::PlayStation
                ? (index == 1 ? "\x01" : index == 2 ? "\x02" : "\x03")
                : (index == 1 ? jumpKey : index == 2 ? shootKey : interactKey);
            text(390, y, keyName, 2, kText);
        } else if (index == 4) {
            text(390, y, language == framework::DialogueLanguage::Czech ? "CS" : "EN",
                 2, kText);
        } else if (index == kDisplayOption && optionCount() > 6) {
            text(390, y, display == DisplayMode::Fullscreen ? "FULLSCREEN"
                         : display == DisplayMode::Window1080 ? "1920X1080" : "1280X720",
                 2, kText);
        }
    }
    if (controls_ == ControlScheme::PlayStation) {
        text(104, 294, "\x05 SELECT/CHANGE  \x04 BACK", 1, kMuted);
    } else {
        text(104, 294, waitingForKey ? "PRESS A NEW KEY  ESC CANCEL"
                                    : "ENTER REBIND  LEFT RIGHT CHANGE", 1, kMuted);
    }
    SDL_RenderPresent(renderer_);
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
        (void)drawObject(prop.archetype, "idle", ticks,
                         {prop.position.x, prop.position.y, prop.archetype->width(),
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
        const bool guarding = enemy.type == EnemyType::Armored && enemy.attackCooldown >= 2.0F;
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
                                         ? (enemy.attackCooldown >= 2.0F ? kText : kCyan)
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
                    {player.position.x, player.position.y, 20.0F, 28.0F},
                    cameraX, player.facing < 0) &&
        !sprite(Sprite::Player, playerFrame(player, ticks), {playerX - 2, playerY - 2, 24, 32}, player.facing < 0)) {
        panel({playerX, playerY + 5, 20, 23}, SDL_Color{105, 163, 172, 255});
        panel({playerX + 3, playerY, 14, 10}, SDL_Color{174, 211, 207, 255});
        panel({playerX + (player.facing > 0 ? 13 : 3), playerY + 3, 5, 3}, kCyan);
        panel({playerX + 5, playerY + 27, 4, 3}, SDL_Color{35, 52, 65, 255});
        panel({playerX + 12, playerY + 27, 4, 3}, SDL_Color{35, 52, 65, 255});
    }
}

void Renderer::drawIsometricWorld(const Game& game, bool paused) noexcept {
    constexpr float kActorWidth = 20.0F;
    constexpr float kActorHeight = 28.0F;
    const RoomLayout& room = game.currentRoom();
    const WorldLayout& world = game.world();
    const framework::RoomDefinition* definition = room.definition;
    const Player& player = game.player();
    const Uint64 ticks = SDL_GetTicks64();

    // Grid (x, y) maps to screen (x - y, (x + y) / 2): one 32 px cell is a 64x32 diamond.
    const auto project = [](float x, float y) noexcept {
        return SDL_FPoint{x - y, (x + y) * 0.5F};
    };
    const SDL_FPoint focus = project(player.position.x + kActorWidth * 0.5F,
                                     player.position.y + kActorHeight * 0.5F);
    const float cameraX = std::floor(focus.x - 320.0F);
    const float cameraY = std::floor(focus.y - 196.0F);
    const auto screenOf = [&](float x, float y) noexcept {
        const SDL_FPoint point = project(x, y);
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
        const SDL_Rect destination{top.x - tileset.tileWidth / 2,
                                   top.y + kTileSize - tileset.tileHeight,
                                   tileset.tileWidth, tileset.tileHeight};
        if (!visible(destination)) return;
        const SDL_Rect source{
            tileset.margin + tile->column * (tileset.tileWidth + tileset.spacing),
            tileset.margin + tile->row * (tileset.tileHeight + tileset.spacing),
            tileset.tileWidth, tileset.tileHeight};
        if (sheet == nullptr || source.x + source.w > sheet->width ||
            source.y + source.h > sheet->height) {
            const Tile cell = tile->collision == framework::TileCollision::Hazard ? Tile::Hazard
                            : tile->collision == framework::TileCollision::Solid  ? Tile::Solid
                                                                                   : Tile::Empty;
            panel(layer == 0 ? SDL_Rect{top.x - 16, top.y + 8, 32, 16}
                             : SDL_Rect{top.x - 24, top.y - 24, 48, 48},
                  cellColor(cell));
            return;
        }
        SDL_SetTextureAlphaMod(sheet->texture, alpha);
        SDL_RenderCopy(renderer_, sheet->texture, &source, &destination);
        if (alpha != 255) SDL_SetTextureAlphaMod(sheet->texture, 255);
    };

    const int rows = kRoomRows;
    const int columns = game.roomColumns();
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) drawTile(0, column, row, 255);
    }

    // Everything standing on the floor is sorted by the front corner of its footprint.
    enum class Kind : std::uint8_t { Block, Roamer, Projectile, Player };
    struct Drawable {
        float depth;
        Kind kind;
        std::uint16_t index;
        std::uint8_t layer;
    };
    std::array<Drawable, static_cast<std::size_t>(kMaxLayoutColumns) * kRoomRows * 2 + 32> drawables{};
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
    const Rect playerBounds{player.position.x, player.position.y, kActorWidth, kActorHeight};
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
        panel({reticleX - 7, reticleY - 1, 5, 2}, kOrange);
        panel({reticleX + 3, reticleY - 1, 5, 2}, kOrange);
        panel({reticleX - 1, reticleY - 7, 2, 5}, kOrange);
        panel({reticleX - 1, reticleY + 3, 2, 5}, kOrange);
        panel({reticleX - 1, reticleY - 1, 2, 2}, kText);
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

void Renderer::drawGame(const Game& game, bool showMap, bool paused,
                        std::string_view jumpKey, std::string_view shootKey,
                        std::string_view interactKey) noexcept {
    SDL_SetRenderDrawColor(renderer_, 8, 15, 26, 255);
    SDL_RenderClear(renderer_);
    drawGameContent(game, showMap, paused, jumpKey, shootKey, interactKey);
    SDL_RenderPresent(renderer_);
}

bool Renderer::recordGame(DrawSink& sink, const Game& game, bool showMap, bool paused,
                          std::string_view jumpKey, std::string_view shootKey,
                          std::string_view interactKey) noexcept {
    if (worldSheetCount_ != 0 ||
        std::any_of(sprites_.begin(), sprites_.end(), [](SDL_Texture* texture) { return texture != nullptr; })) {
        SDL_Log("Recording game with loaded SDL textures is not supported");
        return false;
    }
    if (!beginRecording(sink, SDL_Color{8, 15, 26, 255})) return false;
    if (!drawSinkFailed_) drawGameContent(game, showMap, paused, jumpKey, shootKey, interactKey);
    return endRecording();
}

void Renderer::drawGameContent(const Game& game, bool showMap, bool paused,
                               std::string_view jumpKey, std::string_view shootKey,
                               std::string_view interactKey) noexcept {
    const RoomLayout& room = game.currentRoom();
    const WorldLayout& world = game.world();
    panel({0, 0, 640, 360},
          SDL_Color{room.background[0], room.background[1], room.background[2], 255});

    if (game.isometric()) {
        drawIsometricWorld(game, paused);
    } else {
        drawSideWorld(game, paused);
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
    const std::string_view activeName = Game::itemName(activeItem.item);
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
    if (!paused && !game.dialogueActive() && activeItem.item != ItemId::None) {
        text(481, 54, controls_ == ControlScheme::PlayStation ? "R1 USE" : "H USE",
             1, kCyan);
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
}

void Renderer::drawInventory(const Game& game, const InventoryMenu& menu,
                             std::string_view message) noexcept {
    SDL_SetRenderDrawColor(renderer_, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer_);
    panel({16, 16, 608, 328}, kPanel);
    panel({16, 16, 608, 3}, kCyan);
    text(28, 28, "INVENTORY", 2, kText);
    text(230, 30, Game::itemName(game.inventorySlot(menu.selected).item), 1, kCyan);
    text(28, 54, "BACKPACK 20 SLOTS", 1, kMuted);
    for (std::size_t slot = 0; slot < Game::kInventorySlotCount; ++slot) {
        const int column = static_cast<int>(slot % 5);
        const int row = static_cast<int>(slot / 5);
        const int x = 28 + column * 116;
        const int y = row < 4 ? 72 + row * 42 : 264;
        if (row == 4) text(x, 247, Game::inventorySlotName(slot), 1, kMuted);
        const SDL_Color border = slot == menu.selected ? kCyan :
                                 slot == menu.source ? kOrange : kPanelLight;
        panel({x, y, 108, 36}, border);
        panel({x + 2, y + 2, 104, 32}, kBackground);
        const ItemStack stack = game.inventorySlot(slot);
        text(x + 5, y + 6, Game::itemName(stack.item), 1,
             stack.item == ItemId::None ? kMuted : kText);
        std::array<char, 32> quantity{};
        std::snprintf(quantity.data(), quantity.size(), "%02u QTY %u",
                      static_cast<unsigned>(slot + 1), static_cast<unsigned>(stack.quantity));
        text(x + 5, y + 22, quantity.data(), 1, slot == menu.source ? kOrange : kMuted);
    }
    const std::size_t length = std::min<std::size_t>(message.size(), 94);
    text(28, 311, {message.data(), length}, 1, kOrange);
    const ItemId selected = game.inventorySlot(menu.selected).item;
    std::string_view effect{};
    switch (selected) {
    case ItemId::JumpVisor: effect = "HEAD: JUMP +40"; break;
    case ItemId::VitalVest: effect = "BODY: MAX HEALTH +1"; break;
    case ItemId::PowerBarrel: effect = "WEAPON: SHOT DAMAGE +1"; break;
    case ItemId::SprintModule: effect = "ACCESSORY: SPEED +15"; break;
    case ItemId::MedPatch: effect = "ACTIVE: HEAL 1"; break;
    case ItemId::MedKit: effect = "ACTIVE: HEAL 4"; break;
    case ItemId::ShieldCell: effect = "ACTIVE: SHIELD 4 SECONDS"; break;
    case ItemId::SpeedCharge: effect = "ACTIVE: SPEED +25 FOR 8 SECONDS"; break;
    case ItemId::JumpCharge: effect = "ACTIVE: JUMP +60 FOR 8 SECONDS"; break;
    case ItemId::BlastCharge: effect = "ACTIVE: NEARBY ENEMIES TAKE 2 DAMAGE"; break;
    default: break;
    }
    text(28, 321, effect, 1, kCyan);
    text(28, 333, controls_ == ControlScheme::PlayStation
                     ? "\x05 SELECT  \x01 PICK/MOVE  \x04 CANCEL/BACK"
                     : "ARROWS SELECT  ENTER PICK/MOVE  ESC CANCEL/BACK", 1, kMuted);
    SDL_RenderPresent(renderer_);
}

void Renderer::drawDualSenseLab(const input::ControllerSnapshot& controller,
                                int connectedControllers) noexcept {
    SDL_SetRenderDrawColor(renderer_, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer_);
    panel({12, 12, 310, 310}, kPanel);
    panel({330, 12, 298, 310}, kPanel);
    panel({12, 12, 616, 3}, kCyan);
    text(24, 24, "DUALSENSE LAB", 2, kText);

    if (controller.connected) {
        text(24, 48, std::string_view(controller.name.data()).substr(0, 34), 1, kCyan);
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
        setColor(renderer_, kMuted);
        SDL_RenderDrawRect(renderer_, &bounds);
        SDL_RenderDrawLine(renderer_, x - 19, y, x + 19, y);
        SDL_RenderDrawLine(renderer_, x, y - 19, x, y + 19);
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
        setColor(renderer_, kMuted);
        SDL_RenderDrawRect(renderer_, &touchpadBounds);
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
    SDL_RenderPresent(renderer_);
}

} // namespace echo::platform
