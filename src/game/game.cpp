#include "game/game.hpp"

#include <algorithm>
#include <cmath>

namespace echo {
namespace {

constexpr TileSpan kLandingTiles[] = {
    {0, 10, kRoomColumns, Tile::Solid},
    {5, 8, 5, Tile::Solid},
    {14, 7, 5, Tile::Solid},
    {25, 8, 4, Tile::Solid},
    {22, 8, 3, Tile::Hazard},
};

constexpr TileSpan kMaintenanceTiles[] = {
    {0, 10, kRoomColumns, Tile::Solid},
    {6, 8, 4, Tile::Solid},
    {14, 7, 5, Tile::Solid},
    {18, 8, 3, Tile::Hazard},
};

constexpr TileSpan kPowerTiles[] = {
    {0, 10, kRoomColumns, Tile::Solid},
    {4, 8, 5, Tile::Solid},
    {13, 7, 5, Tile::Solid},
    {23, 8, 5, Tile::Solid},
    {31, 8, 3, Tile::Hazard},
};

constexpr TileSpan kLabTiles[] = {
    {0, 10, kRoomColumns, Tile::Solid},
    {8, 8, 6, Tile::Solid},
    {23, 8, 6, Tile::Solid},
};

constexpr TileSpan kCentralTiles[] = {
    {0, 10, kRoomColumns, Tile::Solid},
    {7, 8, 5, Tile::Solid},
    {18, 7, 5, Tile::Solid},
    {29, 8, 5, Tile::Solid},
};

struct RoomDefinition {
    std::string_view name;
    std::span<const TileSpan> tiles;
};

const std::array<RoomDefinition, static_cast<std::size_t>(RoomId::Count)> kRooms{{
    {"Landing Bay", kLandingTiles},
    {"Maintenance Tunnel", kMaintenanceTiles},
    {"Power Station", kPowerTiles},
    {"Research Laboratory", kLabTiles},
    {"Central Chamber", kCentralTiles},
}};

constexpr float kPlayerWidth = 20.0F;
constexpr float kPlayerHeight = 28.0F;
constexpr float kFloorY = static_cast<float>(10 * kTileSize) - kPlayerHeight;
constexpr float kPlayerSpeed = 190.0F;
constexpr float kHorizontalAcceleration = 1500.0F;
constexpr float kGravity = 1100.0F;
constexpr float kJumpImpulse = 420.0F;
constexpr float kMaximumFallSpeed = 520.0F;
constexpr float kCoyoteSeconds = 0.10F;
constexpr float kJumpBufferSeconds = 0.12F;

[[nodiscard]] std::size_t roomIndex(RoomId room) noexcept {
    return static_cast<std::size_t>(room);
}

} // namespace

Game::Game() noexcept {
    start();
}

void Game::start() noexcept {
    room_ = RoomId::LandingBay;
    player_ = {{42.0F, kFloorY}, {}, 5, 1, true, false};
    enemies_ = {{
        {RoomId::MaintenanceTunnel, {760.0F, kFloorY, 28.0F, 28.0F}, 700.0F, 910.0F, 45.0F, 2, true},
        {RoomId::PowerStation, {790.0F, kFloorY, 30.0F, 28.0F}, 720.0F, 940.0F, 52.0F, 3, true},
    }};
    projectiles_.fill({});
    abilityPickup_ = {690.0F, kFloorY + 2.0F, 20.0F, 24.0F};
    coyoteTimer_ = 0.0F;
    jumpBufferTimer_ = 0.0F;
    invulnerabilityTimer_ = 0.0F;
    shotCooldown_ = 0.0F;
    doubleJumpUsed_ = false;
    abilityCollected_ = false;
    gateBlocked_ = false;
    completed_ = false;
    deaths_ = 0;
}

std::string_view Game::roomName() const noexcept {
    return kRooms[roomIndex(room_)].name;
}

Tile Game::tileAt(int column, int row) const noexcept {
    if (row >= kRoomRows) {
        return Tile::Solid;
    }
    if (column < 0 || column >= kRoomColumns || row < 0) {
        return Tile::Empty;
    }

    Tile result = Tile::Empty;
    for (const TileSpan& span : kRooms[roomIndex(room_)].tiles) {
        if (row == span.row && column >= span.column && column < span.column + span.length) {
            if (span.tile == Tile::Hazard) {
                return Tile::Hazard;
            }
            result = span.tile;
        }
    }
    return result;
}

float Game::cameraX() const noexcept {
    const float centered = player_.position.x + kPlayerWidth * 0.5F - 320.0F;
    return std::clamp(centered, 0.0F, static_cast<float>(kRoomWidth) - 640.0F);
}

int Game::livingEnemies(RoomId room) const noexcept {
    return static_cast<int>(std::count_if(enemies_.begin(), enemies_.end(), [room](const Enemy& enemy) {
        return enemy.room == room && enemy.alive;
    }));
}

bool Game::overlaps(const Rect& first, const Rect& second) noexcept {
    return first.x < second.x + second.width && first.x + first.width > second.x &&
           first.y < second.y + second.height && first.y + first.height > second.y;
}

bool Game::collidesWithWorld(const Rect& bounds) const noexcept {
    const int left = static_cast<int>(std::floor(bounds.x / kTileSize));
    const int right = static_cast<int>(std::floor((bounds.x + bounds.width - 0.01F) / kTileSize));
    const int top = static_cast<int>(std::floor(bounds.y / kTileSize));
    const int bottom = static_cast<int>(std::floor((bounds.y + bounds.height - 0.01F) / kTileSize));

    for (int row = top; row <= bottom; ++row) {
        for (int column = left; column <= right; ++column) {
            if (tileAt(column, row) != Tile::Empty) {
                return true;
            }
        }
    }
    return false;
}

bool Game::overlapsHazard(const Rect& bounds) const noexcept {
    const int left = static_cast<int>(std::floor(bounds.x / kTileSize));
    const int right = static_cast<int>(std::floor((bounds.x + bounds.width - 0.01F) / kTileSize));
    const int top = static_cast<int>(std::floor(bounds.y / kTileSize));
    const int bottom = static_cast<int>(std::floor((bounds.y + bounds.height - 0.01F) / kTileSize));

    for (int row = top; row <= bottom; ++row) {
        for (int column = left; column <= right; ++column) {
            if (tileAt(column, row) == Tile::Hazard) {
                return true;
            }
        }
    }

    const int supportingRow = static_cast<int>(std::floor((bounds.y + bounds.height + 0.01F) / kTileSize));
    for (int column = left; column <= right; ++column) {
        if (tileAt(column, supportingRow) == Tile::Hazard) {
            return true;
        }
    }
    return false;
}

void Game::update(const Input& input, float deltaSeconds) noexcept {
    if (completed_) {
        return;
    }

    const float dt = std::clamp(deltaSeconds, 0.0F, 0.05F);
    gateBlocked_ = false;
    invulnerabilityTimer_ = std::max(0.0F, invulnerabilityTimer_ - dt);
    shotCooldown_ = std::max(0.0F, shotCooldown_ - dt);

    updatePlayer(input, dt);
    updateEnemies(dt);
    updateProjectiles(input, dt);

    if (overlapsHazard({player_.position.x, player_.position.y, kPlayerWidth, kPlayerHeight})) {
        damagePlayer();
    }

    for (const Enemy& enemy : enemies_) {
        if (enemy.alive && enemy.room == room_ &&
            overlaps({player_.position.x, player_.position.y, kPlayerWidth, kPlayerHeight}, enemy.bounds)) {
            damagePlayer();
            break;
        }
    }

    if (!abilityCollected_ && room_ == RoomId::ResearchLab &&
        overlaps({player_.position.x, player_.position.y, kPlayerWidth, kPlayerHeight}, abilityPickup_)) {
        abilityCollected_ = true;
        player_.hasDoubleJump = true;
    }

    if (input.interactPressed) {
        interact();
    }
    resolveRoomEdges();
}

void Game::updatePlayer(const Input& input, float deltaSeconds) noexcept {
    const int direction = std::clamp(input.moveX, -1, 1);
    if (direction != 0) {
        player_.facing = direction;
        player_.velocity.x += static_cast<float>(direction) * kHorizontalAcceleration * deltaSeconds;
        player_.velocity.x = std::clamp(player_.velocity.x, -kPlayerSpeed, kPlayerSpeed);
    } else {
        const float slowdown = kHorizontalAcceleration * deltaSeconds;
        if (std::abs(player_.velocity.x) <= slowdown) {
            player_.velocity.x = 0.0F;
        } else {
            player_.velocity.x -= std::copysign(slowdown, player_.velocity.x);
        }
    }

    if (input.jumpPressed) {
        jumpBufferTimer_ = kJumpBufferSeconds;
    } else {
        jumpBufferTimer_ = std::max(0.0F, jumpBufferTimer_ - deltaSeconds);
    }

    if (player_.onGround) {
        coyoteTimer_ = kCoyoteSeconds;
    } else {
        coyoteTimer_ = std::max(0.0F, coyoteTimer_ - deltaSeconds);
    }

    if (jumpBufferTimer_ > 0.0F && (player_.onGround || coyoteTimer_ > 0.0F)) {
        player_.velocity.y = -kJumpImpulse;
        player_.onGround = false;
        doubleJumpUsed_ = false;
        jumpBufferTimer_ = 0.0F;
        coyoteTimer_ = 0.0F;
    } else if (jumpBufferTimer_ > 0.0F && player_.hasDoubleJump && !player_.onGround && !doubleJumpUsed_) {
        player_.velocity.y = -kJumpImpulse * 0.92F;
        doubleJumpUsed_ = true;
        jumpBufferTimer_ = 0.0F;
    }

    const float gravityScale = !input.jumpHeld && player_.velocity.y < 0.0F ? 1.7F : 1.0F;
    player_.velocity.y = std::min(player_.velocity.y + kGravity * gravityScale * deltaSeconds, kMaximumFallSpeed);

    player_.position.x += player_.velocity.x * deltaSeconds;
    Rect playerBounds{player_.position.x, player_.position.y, kPlayerWidth, kPlayerHeight};
    if (collidesWithWorld(playerBounds)) {
        const int left = static_cast<int>(std::floor(playerBounds.x / kTileSize));
        const int right = static_cast<int>(std::floor((playerBounds.x + playerBounds.width - 0.01F) / kTileSize));
        const int top = static_cast<int>(std::floor(playerBounds.y / kTileSize));
        const int bottom = static_cast<int>(std::floor((playerBounds.y + playerBounds.height - 0.01F) / kTileSize));
        for (int row = top; row <= bottom; ++row) {
            for (int column = left; column <= right; ++column) {
                if (tileAt(column, row) == Tile::Empty) {
                    continue;
                }
                if (player_.velocity.x > 0.0F) {
                    player_.position.x = static_cast<float>(column * kTileSize) - kPlayerWidth;
                } else if (player_.velocity.x < 0.0F) {
                    player_.position.x = static_cast<float>((column + 1) * kTileSize);
                }
                player_.velocity.x = 0.0F;
            }
        }
    }

    player_.onGround = false;
    player_.position.y += player_.velocity.y * deltaSeconds;
    playerBounds = {player_.position.x, player_.position.y, kPlayerWidth, kPlayerHeight};
    if (collidesWithWorld(playerBounds)) {
        const int left = static_cast<int>(std::floor(playerBounds.x / kTileSize));
        const int right = static_cast<int>(std::floor((playerBounds.x + playerBounds.width - 0.01F) / kTileSize));
        const int top = static_cast<int>(std::floor(playerBounds.y / kTileSize));
        const int bottom = static_cast<int>(std::floor((playerBounds.y + playerBounds.height - 0.01F) / kTileSize));
        for (int row = top; row <= bottom; ++row) {
            for (int column = left; column <= right; ++column) {
                if (tileAt(column, row) == Tile::Empty) {
                    continue;
                }
                if (player_.velocity.y > 0.0F) {
                    player_.position.y = static_cast<float>(row * kTileSize) - kPlayerHeight;
                    player_.onGround = true;
                    coyoteTimer_ = kCoyoteSeconds;
                    doubleJumpUsed_ = false;
                } else if (player_.velocity.y < 0.0F) {
                    player_.position.y = static_cast<float>((row + 1) * kTileSize);
                }
                player_.velocity.y = 0.0F;
            }
        }
    }
}

void Game::updateEnemies(float deltaSeconds) noexcept {
    for (Enemy& enemy : enemies_) {
        if (!enemy.alive || enemy.room != room_) {
            continue;
        }
        enemy.bounds.x += enemy.speed * deltaSeconds;
        if (enemy.bounds.x < enemy.patrolLeft) {
            enemy.bounds.x = enemy.patrolLeft;
            enemy.speed = std::abs(enemy.speed);
        } else if (enemy.bounds.x + enemy.bounds.width > enemy.patrolRight) {
            enemy.bounds.x = enemy.patrolRight - enemy.bounds.width;
            enemy.speed = -std::abs(enemy.speed);
        }
    }
}

void Game::updateProjectiles(const Input& input, float deltaSeconds) noexcept {
    for (Projectile& projectile : projectiles_) {
        if (!projectile.active) {
            continue;
        }
        projectile.bounds.x += projectile.velocityX * deltaSeconds;
        if (projectile.room != room_ || projectile.bounds.x < 0.0F ||
            projectile.bounds.x > static_cast<float>(kRoomWidth)) {
            projectile.active = false;
            continue;
        }
        if (collidesWithWorld(projectile.bounds)) {
            projectile.active = false;
            continue;
        }
        for (Enemy& enemy : enemies_) {
            if (enemy.alive && enemy.room == projectile.room && overlaps(projectile.bounds, enemy.bounds)) {
                --enemy.health;
                if (enemy.health <= 0) {
                    enemy.alive = false;
                }
                projectile.active = false;
                break;
            }
        }
    }

    if (input.shootHeld && shotCooldown_ <= 0.0F) {
        const auto freeProjectile = std::find_if(projectiles_.begin(), projectiles_.end(), [](const Projectile& item) {
            return !item.active;
        });
        if (freeProjectile != projectiles_.end()) {
            freeProjectile->room = room_;
            freeProjectile->bounds = {
                player_.position.x + (player_.facing > 0 ? kPlayerWidth : -10.0F),
                player_.position.y + 11.0F,
                10.0F,
                6.0F,
            };
            freeProjectile->velocityX = static_cast<float>(player_.facing) * 500.0F;
            freeProjectile->active = true;
            shotCooldown_ = 0.22F;
        }
    }
}

void Game::interact() noexcept {
    const Rect playerBounds{player_.position.x, player_.position.y, kPlayerWidth, kPlayerHeight};
    if (room_ == RoomId::MaintenanceTunnel &&
        overlaps(playerBounds, {970.0F, 272.0F, 44.0F, 48.0F})) {
        enterRoom(RoomId::ResearchLab, true);
    } else if (room_ == RoomId::ResearchLab && player_.position.x < 120.0F) {
        enterRoom(RoomId::MaintenanceTunnel, false);
    }
}

void Game::resolveRoomEdges() noexcept {
    if (player_.position.x < 0.0F) {
        if (room_ == RoomId::MaintenanceTunnel) {
            enterRoom(RoomId::LandingBay, false);
        } else if (room_ == RoomId::PowerStation) {
            enterRoom(RoomId::MaintenanceTunnel, false);
        } else if (room_ == RoomId::CentralChamber) {
            enterRoom(RoomId::PowerStation, false);
        } else {
            player_.position.x = 0.0F;
        }
    } else if (player_.position.x + kPlayerWidth > static_cast<float>(kRoomWidth)) {
        if (room_ == RoomId::LandingBay) {
            enterRoom(RoomId::MaintenanceTunnel, true);
        } else if (room_ == RoomId::MaintenanceTunnel) {
            enterRoom(RoomId::PowerStation, true);
        } else if (room_ == RoomId::PowerStation && player_.hasDoubleJump) {
            enterRoom(RoomId::CentralChamber, true);
        } else if (room_ == RoomId::CentralChamber) {
            completed_ = true;
            player_.position.x = static_cast<float>(kRoomWidth) - kPlayerWidth;
        } else {
            player_.position.x = static_cast<float>(kRoomWidth) - kPlayerWidth;
            if (room_ == RoomId::PowerStation) {
                gateBlocked_ = true;
            }
        }
    }
}

void Game::enterRoom(RoomId destination, bool fromLeft) noexcept {
    room_ = destination;
    player_.position.x = fromLeft ? 16.0F : static_cast<float>(kRoomWidth) - kPlayerWidth - 16.0F;
    player_.position.y = kFloorY;
    player_.velocity = {};
    player_.onGround = true;
    coyoteTimer_ = kCoyoteSeconds;
    doubleJumpUsed_ = false;
}

void Game::damagePlayer() noexcept {
    if (invulnerabilityTimer_ > 0.0F) {
        return;
    }

    --player_.health;
    invulnerabilityTimer_ = 1.0F;
    if (player_.health <= 0) {
        ++deaths_;
        player_.health = 5;
        enterRoom(RoomId::LandingBay, true);
    }
}

} // namespace echo
