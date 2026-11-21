#include "platform/sdl/renderer.hpp"

#include "platform/sdl/assets.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

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
    window_ = SDL_CreateWindow("PROJECT ECHO", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               1280, 720, SDL_WINDOW_RESIZABLE);
    if (window_ == nullptr) {
        return false;
    }

#ifdef __PROSPERO__
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
#else
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
    return true;
}

void Renderer::destroy() noexcept {
    releaseSprites();
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
    setColor(renderer_, color);
    SDL_RenderFillRect(renderer_, &bounds);
}

void Renderer::text(int x, int y, std::string_view value, int scale, SDL_Color color) noexcept {
    setColor(renderer_, color);
    for (char character : value) {
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        if (upper == ' ') {
            x += 6 * scale;
            continue;
        }

        const auto glyph = std::find_if(std::begin(kGlyphs), std::end(kGlyphs), [upper](const Glyph& item) {
            return item.character == upper;
        });
        if (glyph != std::end(kGlyphs)) {
            for (int row = 0; row < 7; ++row) {
                for (int column = 0; column < 5; ++column) {
                    if ((glyph->rows[static_cast<std::size_t>(row)] & (1U << (4 - column))) == 0) {
                        continue;
                    }
                    SDL_Rect pixel{x + column * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer_, &pixel);
                }
            }
        }
        x += 6 * scale;
    }
}

void Renderer::drawMenu(int selection, bool continueAvailable) noexcept {
    SDL_SetRenderDrawColor(renderer_, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer_);
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
    SDL_RenderPresent(renderer_);
}

void Renderer::drawOptions(int selection, int volume, bool waitingForKey,
                           std::string_view jumpKey, std::string_view shootKey,
                           std::string_view interactKey) noexcept {
    SDL_SetRenderDrawColor(renderer_, kBackground.r, kBackground.g, kBackground.b, 255);
    SDL_RenderClear(renderer_);
    panel({74, 38, 492, 284}, kPanel);
    panel({74, 38, 492, 3}, kCyan);
    text(110, 62, "OPTIONS", 3, kText);
    if (controls_ == ControlScheme::PlayStation) {
        text(110, 94, "FIXED CONTROLLER BUTTONS", 1, kMuted);
    }

    const std::array<std::string_view, 5> rows{
        "VOLUME", "JUMP", "FIRE", "INTERACT", "BACK"
    };
    for (int index = 0; index < static_cast<int>(rows.size()); ++index) {
        const int y = 120 + index * 34;
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
        }
    }
    if (controls_ == ControlScheme::PlayStation) {
        text(104, 294, "\x05 SELECT/VOLUME  \x04 BACK", 1, kMuted);
    } else {
        text(104, 294, waitingForKey ? "PRESS A NEW KEY  ESC CANCEL" : "ENTER REBIND  LEFT RIGHT VOLUME", 1, kMuted);
    }
    SDL_RenderPresent(renderer_);
}

void Renderer::drawGame(const Game& game, bool showMap, bool paused,
                        std::string_view jumpKey, std::string_view shootKey,
                        std::string_view interactKey) noexcept {
    SDL_SetRenderDrawColor(renderer_, 8, 15, 26, 255);
    SDL_RenderClear(renderer_);

    const float cameraX = game.cameraX();
    const int roomTone = static_cast<int>(game.roomId());
    const SDL_Color roomTint = roomTone == 3 ? SDL_Color{20, 38, 42, 255}
                                : roomTone == 2 ? SDL_Color{28, 27, 47, 255}
                                                : SDL_Color{13, 30, 43, 255};
    panel({0, 0, 640, 360}, roomTint);

    for (int column = 0; column < kRoomColumns; ++column) {
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
    if (game.roomId() == RoomId::MaintenanceTunnel) {
        const int doorX = static_cast<int>(970.0F - cameraX);
        if (!sprite(Sprite::Door, 0, {doorX, 272, 44, 48})) {
            panel({doorX, 272, 44, 48}, SDL_Color{32, 63, 76, 255});
            panel({doorX + 5, 278, 34, 36}, SDL_Color{19, 39, 49, 255});
            panel({doorX + 32, 294, 3, 3}, kCyan);
        }
    } else if (game.roomId() == RoomId::ResearchLab && !game.abilityCollected()) {
        const Rect pickup = game.abilityPickup();
        const int pickupX = static_cast<int>(pickup.x - cameraX);
        if (!sprite(Sprite::Pickup, static_cast<int>((ticks / 300U) % 2U),
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
        panel({x + 4, y + 12, 20, 28}, SDL_Color{45, 122, 129, 255});
        panel({x + 6, y + 1, 16, 15}, SDL_Color{190, 166, 132, 255});
        panel({x + 7, y, 14, 5}, SDL_Color{55, 53, 65, 255});
        panel({x + 8, y + 7, 3, 3}, kText);
        panel({x + 17, y + 7, 3, 3}, kText);
        panel({x + 7, y + 38, 6, 2}, SDL_Color{35, 52, 65, 255});
        panel({x + 16, y + 38, 6, 2}, SDL_Color{35, 52, 65, 255});
        if (game.technicianNearby() && !game.dialogueActive()) {
            text(x - 1, y - 12, "ENGINEER", 1, kCyan);
        }
    }

    for (const Enemy& enemy : game.enemies()) {
        if (!enemy.alive || enemy.room != game.roomId()) {
            continue;
        }
        const int x = static_cast<int>(enemy.bounds.x - cameraX);
        const int y = static_cast<int>(enemy.bounds.y);
        const int width = static_cast<int>(enemy.bounds.width);
        if (sprite(Sprite::Enemy, static_cast<int>((ticks / 180U) % 2U),
                   {x + (width - 32) / 2, y + static_cast<int>(enemy.bounds.height) - 32, 32, 32})) {
            continue;
        }
        panel({x, y + 8, width, 20}, SDL_Color{135, 80, 99, 255});
        panel({x + 4, y + 2, 20, 14}, SDL_Color{202, 111, 121, 255});
        panel({x + 8, y + 7, 4, 4}, kOrange);
        panel({x + 18, y + 7, 4, 4}, kOrange);
    }

    for (const Projectile& projectile : game.projectiles()) {
        if (projectile.active && projectile.room == game.roomId()) {
            const SDL_Rect bounds{static_cast<int>(projectile.bounds.x - cameraX), static_cast<int>(projectile.bounds.y),
                                  static_cast<int>(projectile.bounds.width), static_cast<int>(projectile.bounds.height)};
            if (!sprite(Sprite::Projectile, static_cast<int>((ticks / 60U) % 2U),
                        {bounds.x + bounds.w / 2 - 6, bounds.y + bounds.h / 2 - 4, 12, 8},
                        projectile.velocityX < 0.0F)) {
                panel(bounds, kOrange);
            }
        }
    }

    const Player& player = game.player();
    const int playerX = static_cast<int>(player.position.x - cameraX);
    const int playerY = static_cast<int>(player.position.y);
    const bool flashing = (ticks / 90U) % 2U == 0U;
    if (player.health > 0 && (flashing || !paused) &&
        !sprite(Sprite::Player, playerFrame(player, ticks), {playerX - 2, playerY - 2, 24, 32}, player.facing < 0)) {
        panel({playerX, playerY + 5, 20, 23}, SDL_Color{105, 163, 172, 255});
        panel({playerX + 3, playerY, 14, 10}, SDL_Color{174, 211, 207, 255});
        panel({playerX + (player.facing > 0 ? 13 : 3), playerY + 3, 5, 3}, kCyan);
        panel({playerX + 5, playerY + 27, 4, 3}, SDL_Color{35, 52, 65, 255});
        panel({playerX + 12, playerY + 27, 4, 3}, SDL_Color{35, 52, 65, 255});
    }

    panel({12, 12, 184, 38}, SDL_Color{4, 10, 17, 218});
    text(22, 19, "VITALS", 1, kMuted);
    for (int index = 0; index < 5; ++index) {
        panel({22 + index * 22, 32, 16, 8}, index < player.health ? kRed : SDL_Color{55, 60, 67, 255});
    }
    panel({470, 12, 158, 38}, SDL_Color{4, 10, 17, 218});
    text(481, 18, game.roomName(), 1, kText);
    text(481, 32, player.hasDoubleJump ? "ECHO STEP ONLINE" : "ECHO STEP LOCKED", 1,
         player.hasDoubleJump ? kCyan : kMuted);
    panel({206, 12, 152, 38}, SDL_Color{4, 10, 17, 218});
    text(218, 18, "INVENTORY", 1, kMuted);
    const bool hasMaintenanceKey = game.inventory().contains(ItemId::MaintenanceKey);
    text(218, 32, hasMaintenanceKey ? "MAINTENANCE KEY" : "EMPTY",
         1, hasMaintenanceKey ? kCyan : kMuted);

    if (game.gateBlocked()) {
        panel({188, 274, 264, 42}, SDL_Color{4, 10, 17, 230});
        text(211, 290, "POWER GATE NEEDS ECHO STEP", 1, kOrange);
    }
    if (game.accessBlocked()) {
        panel({188, 274, 264, 42}, SDL_Color{4, 10, 17, 230});
        text(211, 290, "LAB DOOR NEEDS ACCESS KEY", 1, kOrange);
    }
    if (game.technicianNearby() && !game.dialogueActive()) {
        text(520, 285, controls_ == ControlScheme::PlayStation ? "\x03" : interactKey, 2, kCyan);
    }
    if (game.roomId() == RoomId::MaintenanceTunnel && player.position.x > 900.0F &&
        player.position.x < 1040.0F) {
        text(520, 285, controls_ == ControlScheme::PlayStation ? "\x03" : interactKey, 2, kCyan);
    }
    if (controls_ == ControlScheme::PlayStation) {
        text(12, 340, "\x05 MOVE  \x01 JUMP  \x02 FIRE  \x03 USE  \x07 MAP  \x06 PAUSE", 2, kMuted);
    } else {
        int hintX = 12;
        const auto hint = [&](std::string_view value) {
            text(hintX, 340, value, 1, kMuted);
            hintX += static_cast<int>(value.size()) * 6;
        };
        hint("ARROWS MOVE  ");
        hint(jumpKey);
        hint(" JUMP  ");
        hint(shootKey);
        hint(" FIRE  ");
        hint(interactKey);
        hint(" USE  M MAP  ESC PAUSE");
    }

    if (showMap) {
        panel({151, 92, 338, 172}, SDL_Color{4, 10, 17, 238});
        text(174, 109, "FACILITY MAP", 2, kText);
        constexpr std::array<SDL_Rect, 5> rooms{{
            {170, 170, 48, 28}, {236, 170, 65, 28}, {320, 170, 58, 28},
            {236, 124, 65, 28}, {397, 170, 74, 28},
        }};
        panel({218, 182, 18, 3}, kMuted);
        panel({301, 182, 19, 3}, kMuted);
        panel({269, 152, 3, 18}, kMuted);
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
        panel({172, 125, 296, 112}, SDL_Color{4, 10, 17, 238});
        text(245, 146, "PAUSED", 3, kText);
        text(195, 199, controls_ == ControlScheme::PlayStation ? "\x01 RESUME  \x04 MENU" : "ENTER RESUME  ESC MENU", 1, kMuted);
    }
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
