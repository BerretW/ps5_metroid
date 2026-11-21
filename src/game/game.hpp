#pragma once

#include "echo/framework/narrative.hpp"

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

enum class ItemId : std::uint8_t {
    None,
    MaintenanceKey,
    Count
};

struct Inventory {
    static constexpr std::size_t kCapacity = 8;

    std::array<ItemId, kCapacity> items{};
    std::size_t count{};

    [[nodiscard]] bool contains(ItemId item) const noexcept;
    [[nodiscard]] bool add(ItemId item) noexcept;
};

struct GameCheckpoint {
    RoomId room{RoomId::LandingBay};
    Vec2 playerPosition{};
    Vec2 playerVelocity{};
    std::array<int, 2> enemyHealth{};
    Inventory inventory{};
    int playerHealth{5};
    int playerFacing{1};
    int deaths{};
    bool playerOnGround{true};
    bool hasDoubleJump{};
    bool abilityCollected{};
    bool completed{};
};

class Game {
public:
    Game() noexcept;

    [[nodiscard]] framework::Status configureDialogue(
        const framework::DialogueModule& module, framework::GameState& state,
        Vec2 technicianPosition) noexcept;
    [[nodiscard]] framework::Status setDialogueLanguage(
        framework::DialogueLanguage language) noexcept;
    void start() noexcept;
    [[nodiscard]] framework::Status update(
        const Input& input, float deltaSeconds = kFixedStep) noexcept;
    [[nodiscard]] GameCheckpoint checkpoint() const noexcept;
    [[nodiscard]] bool restore(const GameCheckpoint& checkpoint) noexcept;

    [[nodiscard]] RoomId roomId() const noexcept { return room_; }
    [[nodiscard]] std::string_view roomName() const noexcept;
    [[nodiscard]] const Player& player() const noexcept { return player_; }
    [[nodiscard]] std::span<const Enemy> enemies() const noexcept { return enemies_; }
    [[nodiscard]] std::span<const Projectile> projectiles() const noexcept { return projectiles_; }
    [[nodiscard]] Tile tileAt(int column, int row) const noexcept;
    [[nodiscard]] float cameraX() const noexcept;
    [[nodiscard]] bool abilityCollected() const noexcept { return abilityCollected_; }
    [[nodiscard]] bool gateBlocked() const noexcept { return gateBlocked_; }
    [[nodiscard]] bool accessBlocked() const noexcept { return accessBlocked_; }
    [[nodiscard]] bool completed() const noexcept { return completed_; }
    [[nodiscard]] int deaths() const noexcept { return deaths_; }
    [[nodiscard]] int livingEnemies(RoomId room) const noexcept;
    [[nodiscard]] Rect abilityPickup() const noexcept { return abilityPickup_; }
    [[nodiscard]] Rect technicianBounds() const noexcept;
    [[nodiscard]] bool technicianNearby() const noexcept;
    [[nodiscard]] bool dialogueActive() const noexcept { return dialogueSession_.active(); }
    [[nodiscard]] std::string_view dialogueSpeaker() const noexcept { return dialogueLine_.speaker; }
    [[nodiscard]] std::string_view dialogueText() const noexcept { return dialogueLine_.text; }
    [[nodiscard]] std::size_t dialogueChoiceCount() const noexcept { return dialogueChoiceCount_; }
    [[nodiscard]] std::size_t selectedDialogueChoice() const noexcept { return dialogueSelection_; }
    [[nodiscard]] std::string_view dialogueChoiceText(std::size_t index) const noexcept {
        return index < dialogueChoiceCount_ ? dialogueChoices_[index].label : std::string_view{};
    }
    [[nodiscard]] const Inventory& inventory() const noexcept { return inventory_; }

private:
    void updatePlayer(const Input& input, float deltaSeconds) noexcept;
    void updateEnemies(float deltaSeconds) noexcept;
    void updateProjectiles(const Input& input, float deltaSeconds) noexcept;
    void resolveRoomEdges() noexcept;
    [[nodiscard]] framework::Status interact(int moveDirection) noexcept;
    [[nodiscard]] framework::Status syncDialogueState() noexcept;
    [[nodiscard]] framework::Status refreshDialogue() noexcept;
    [[nodiscard]] framework::Status updateDialogue(const Input& input) noexcept;
    [[nodiscard]] static framework::ActionResult grantDialogueItem(
        void* context, const framework::ActionRequest& request) noexcept;
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
    bool accessBlocked_{};
    bool completed_{};
    int deaths_{};
    Inventory inventory_{};
    Rect technician_{108.0F, 280.0F, 28.0F, 40.0F};
    const framework::DialogueModule* dialogueModule_{};
    framework::GameState* dialogueState_{};
    framework::DialogueSession dialogueSession_{};
    framework::DialogueLineView dialogueLine_{};
    std::array<framework::DialogueChoiceView, framework::kMaxDialogueChoices> dialogueChoices_{};
    std::size_t dialogueChoiceCount_{};
    std::size_t dialogueSelection_{};
    int dialogueDirection_{};
    framework::DialogueLanguage dialogueLanguage_{framework::DialogueLanguage::English};
};

} // namespace echo
