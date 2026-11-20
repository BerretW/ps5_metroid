#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace echo {

inline constexpr int kTileSize = 32;
inline constexpr int kRoomColumns = 40;
inline constexpr int kRoomRows = 12;
inline constexpr int kRoomWidth = kTileSize * kRoomColumns;
inline constexpr int kRoomHeight = kTileSize * kRoomRows;
inline constexpr float kFixedStep = 1.0F / 60.0F;

struct Vec2 {
    float x{};
    float y{};
};

struct Rect {
    float x{};
    float y{};
    float width{};
    float height{};
};

enum class RoomId : std::uint8_t {
    LandingBay,
    MaintenanceTunnel,
    PowerStation,
    ResearchLab,
    CentralChamber,
    Count
};

enum class Tile : std::uint8_t {
    Empty,
    Solid,
    Hazard
};

struct TileSpan {
    int column{};
    int row{};
    int length{};
    Tile tile{Tile::Solid};
};

struct Input {
    int moveX{};
    bool jumpPressed{};
    bool jumpHeld{};
    bool shootHeld{};
    bool interactPressed{};
};

struct Player {
    Vec2 position{};
    Vec2 velocity{};
    int health{5};
    int facing{1};
    bool onGround{};
    bool hasDoubleJump{};
};

struct Enemy {
    RoomId room{};
    Rect bounds{};
    float patrolLeft{};
    float patrolRight{};
    float speed{45.0F};
    int health{2};
    bool alive{true};
};

struct Projectile {
    RoomId room{};
    Rect bounds{};
    float velocityX{};
    bool active{};
};

class Game {
public:
    Game() noexcept;

    void start() noexcept;
    void update(const Input& input, float deltaSeconds = kFixedStep) noexcept;

    [[nodiscard]] RoomId roomId() const noexcept { return room_; }
    [[nodiscard]] std::string_view roomName() const noexcept;
    [[nodiscard]] const Player& player() const noexcept { return player_; }
    [[nodiscard]] std::span<const Enemy> enemies() const noexcept { return enemies_; }
    [[nodiscard]] std::span<const Projectile> projectiles() const noexcept { return projectiles_; }
    [[nodiscard]] Tile tileAt(int column, int row) const noexcept;
    [[nodiscard]] float cameraX() const noexcept;
    [[nodiscard]] bool abilityCollected() const noexcept { return abilityCollected_; }
    [[nodiscard]] bool gateBlocked() const noexcept { return gateBlocked_; }
    [[nodiscard]] bool completed() const noexcept { return completed_; }
    [[nodiscard]] int deaths() const noexcept { return deaths_; }
    [[nodiscard]] int livingEnemies(RoomId room) const noexcept;
    [[nodiscard]] Rect abilityPickup() const noexcept { return abilityPickup_; }

private:
    void updatePlayer(const Input& input, float deltaSeconds) noexcept;
    void updateEnemies(float deltaSeconds) noexcept;
    void updateProjectiles(const Input& input, float deltaSeconds) noexcept;
    void resolveRoomEdges() noexcept;
    void interact() noexcept;
    void damagePlayer() noexcept;
    void enterRoom(RoomId destination, bool fromLeft) noexcept;
    [[nodiscard]] bool collidesWithWorld(const Rect& bounds) const noexcept;
    [[nodiscard]] bool overlapsHazard(const Rect& bounds) const noexcept;
    [[nodiscard]] static bool overlaps(const Rect& first, const Rect& second) noexcept;

    RoomId room_{RoomId::LandingBay};
    Player player_{};
    std::array<Enemy, 2> enemies_{};
    std::array<Projectile, 12> projectiles_{};
    Rect abilityPickup_{};
    float coyoteTimer_{};
    float jumpBufferTimer_{};
    float invulnerabilityTimer_{};
    float shotCooldown_{};
    bool doubleJumpUsed_{};
    bool abilityCollected_{};
    bool gateBlocked_{};
    bool completed_{};
    int deaths_{};
};

} // namespace echo
