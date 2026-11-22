#pragma once

#include "echo/framework/abilities.hpp"
#include "echo/framework/attributes.hpp"
#include "echo/framework/equipment.hpp"
#include "echo/framework/inventory.hpp"
#include "echo/framework/narrative.hpp"
#include "echo/framework/cutscenes.hpp"
#include "game/project_components.hpp"
#include "game/project_abilities.hpp"
#include "game/project_quests.hpp"
#include "game/project_cutscenes.hpp"

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
    bool useItemPressed{};
};

struct Player {
    Vec2 position{};
    Vec2 velocity{};
    int health{5};
    int facing{1};
    bool onGround{};
    bool hasDoubleJump{};
};

enum class EnemyType : std::uint8_t { Patrol, Charger, Armored, Sentry };

struct Enemy {
    RoomId room{};
    Rect bounds{};
    float patrolLeft{};
    float patrolRight{};
    float speed{45.0F};
    int health{2};
    bool alive{true};
    EnemyType type{EnemyType::Patrol};
    float attackCooldown{};
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
    HealthCell,
    KineticCoil,
    Scrap,
    JumpVisor,
    VitalVest,
    PowerBarrel,
    SprintModule,
    MedPatch,
    MedKit,
    ShieldCell,
    SpeedCharge,
    JumpCharge,
    BlastCharge,
    Count
};

inline constexpr std::size_t kEnemyCount = 5;
inline constexpr std::size_t kLootDropCount = 12;

struct ItemStack {
    ItemId item{};
    std::uint16_t quantity{};
    friend constexpr bool operator==(const ItemStack&, const ItemStack&) noexcept = default;
};

struct LootDrop {
    RoomId room{};
    Vec2 position{};
    ItemStack stack{};
    bool active{};
};

enum class EquipmentSlot : std::uint8_t { Head, Body, Weapon, Accessory, Count };
inline constexpr std::size_t kEquipmentSlotCount =
    static_cast<std::size_t>(EquipmentSlot::Count);

struct Inventory : framework::SlotInventory<ItemId, 20> {
    using Storage = framework::SlotInventory<ItemId, 20>;
    static constexpr std::size_t kCapacity = Storage::kCapacity;

    [[nodiscard]] bool contains(ItemId item) const noexcept {
        return Storage::contains(item);
    }
    [[nodiscard]] bool add(ItemId item) noexcept {
        return add(item, 1, 1);
    }
    [[nodiscard]] bool add(ItemId item, std::uint16_t maxStack,
                           std::uint16_t amount) noexcept {
        if (item == ItemId::None || item >= ItemId::Count) return false;
        if (maxStack == 1 && contains(item)) return true;
        return static_cast<bool>(Storage::add(item, maxStack, amount));
    }
};

struct GameCheckpoint {
    RoomId room{RoomId::LandingBay};
    Vec2 playerPosition{};
    Vec2 playerVelocity{};
    std::array<int, kEnemyCount> enemyHealth{};
    std::array<LootDrop, kLootDropCount> lootDrops{};
    float speedBoostSeconds{};
    float jumpBoostSeconds{};
    float shieldSeconds{};
    Inventory inventory{};
    std::array<ItemStack, kEquipmentSlotCount> equipment{};
    ItemStack activeItem{};
    std::uint32_t inventoryLayoutVersion{1};
    int playerHealth{5};
    int playerFacing{1};
    int deaths{};
    bool playerOnGround{true};
    bool hasDoubleJump{};
    bool abilityCollected{};
    bool completed{};
    std::uint8_t questBranch{};
    bool questRewardClaimed{};
};

class Game {
public:
    Game() noexcept;

    [[nodiscard]] framework::Status configureDialogue(
        const framework::DialogueModule& module, framework::GameState& state,
        Vec2 technicianPosition,
        float interactionReach = kDefaultInteractionReach,
        framework::InventoryModule* inventoryModule = nullptr) noexcept;
    [[nodiscard]] framework::Status configureAttributes(
        const framework::AttributesModule& module) noexcept;
    [[nodiscard]] framework::Status configureAbilities(
        const framework::AbilityModule& module) noexcept;
    [[nodiscard]] framework::Status configureEquipment(
        const framework::EquipmentModule& module) noexcept;
    [[nodiscard]] framework::Status configureQuests(
        const framework::QuestModule& module) noexcept;
    [[nodiscard]] framework::Status configureCutscenes(
        const framework::CutsceneModule& module) noexcept;
    [[nodiscard]] framework::QuestProgress questProgress() const noexcept;
    [[nodiscard]] std::uint8_t questBranch() const noexcept { return questBranch_; }
    [[nodiscard]] bool questRewardClaimed() const noexcept { return questRewardClaimed_; }
    [[nodiscard]] std::size_t questChoiceSelection() const noexcept {
        return questChoiceSelection_;
    }
    [[nodiscard]] framework::Status addAttributeModifier(
        std::string_view id, std::string_view source, std::int32_t amount) noexcept;
    [[nodiscard]] framework::Status removeAttributeModifier(
        std::string_view id, std::string_view source) noexcept;
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
    [[nodiscard]] std::span<const LootDrop> lootDrops() const noexcept { return lootDrops_; }
    [[nodiscard]] std::span<const Projectile> projectiles() const noexcept { return projectiles_; }
    [[nodiscard]] std::span<const Projectile> enemyProjectiles() const noexcept { return enemyProjectiles_; }
    [[nodiscard]] Tile tileAt(int column, int row) const noexcept;
    [[nodiscard]] float cameraX() const noexcept;
    [[nodiscard]] bool abilityCollected() const noexcept {
        return hasAbility(kEchoStepAbilityId);
    }
    [[nodiscard]] bool gateBlocked() const noexcept { return gateBlocked_; }
    [[nodiscard]] bool accessBlocked() const noexcept { return accessBlocked_; }
    [[nodiscard]] bool completed() const noexcept { return completed_; }
    [[nodiscard]] int deaths() const noexcept { return deaths_; }
    [[nodiscard]] int maximumHealth() const noexcept;
    [[nodiscard]] bool equipmentEquipped(std::string_view id) const noexcept {
        return equipment_.contains(id);
    }
    [[nodiscard]] int livingEnemies(RoomId room) const noexcept;
    [[nodiscard]] Rect abilityPickup() const noexcept { return abilityPickup_; }
    [[nodiscard]] Rect technicianBounds() const noexcept;
    [[nodiscard]] bool technicianNearby() const noexcept;
    [[nodiscard]] bool dialogueActive() const noexcept { return dialogueSession_.active(); }
    [[nodiscard]] bool cutsceneActive() const noexcept { return cutscenePlayer_.active(); }
    [[nodiscard]] std::string_view cutsceneSpeaker() const noexcept;
    [[nodiscard]] std::string_view cutsceneText() const noexcept;
    [[nodiscard]] std::size_t cutsceneCardIndex() const noexcept {
        return cutscenePlayer_.currentIndex();
    }
    [[nodiscard]] std::size_t cutsceneCardCount() const noexcept {
        return cutscenePlayer_.cardCount();
    }
    [[nodiscard]] std::string_view dialogueSpeaker() const noexcept { return dialogueLine_.speaker; }
    [[nodiscard]] std::string_view dialogueText() const noexcept { return dialogueLine_.text; }
    [[nodiscard]] std::size_t dialogueChoiceCount() const noexcept { return dialogueChoiceCount_; }
    [[nodiscard]] std::size_t selectedDialogueChoice() const noexcept { return dialogueSelection_; }
    [[nodiscard]] std::string_view dialogueChoiceText(std::size_t index) const noexcept {
        return index < dialogueChoiceCount_ ? dialogueChoices_[index].label : std::string_view{};
    }
    [[nodiscard]] const Inventory& inventory() const noexcept { return inventory_; }
    static constexpr std::size_t kFirstEquipmentSlot = Inventory::kCapacity;
    static constexpr std::size_t kActiveItemSlot = kFirstEquipmentSlot + kEquipmentSlotCount;
    static constexpr std::size_t kInventorySlotCount = kActiveItemSlot + 1;
    [[nodiscard]] ItemStack inventorySlot(std::size_t slot) const noexcept;
    [[nodiscard]] framework::Status moveInventorySlot(std::size_t source,
                                                     std::size_t destination) noexcept;
    [[nodiscard]] bool hasItem(ItemId item) const noexcept { return itemQuantity(item) != 0; }
    [[nodiscard]] std::uint32_t itemQuantity(ItemId item) const noexcept;
    [[nodiscard]] static std::string_view itemName(ItemId item) noexcept;
    [[nodiscard]] static std::string_view itemResource(ItemId item) noexcept;
    [[nodiscard]] static std::uint16_t itemStackLimit(ItemId item) noexcept;
    [[nodiscard]] static ItemStack expectedLoot(std::size_t index) noexcept;
    [[nodiscard]] static RoomId expectedLootRoom(std::size_t index) noexcept;
    [[nodiscard]] static std::size_t lootEnemy(std::size_t index) noexcept;
    [[nodiscard]] static std::string_view inventorySlotName(std::size_t slot) noexcept;
    [[nodiscard]] static bool acceptsItem(std::size_t slot, ItemStack stack) noexcept;

private:
    void updatePlayer(const Input& input, float deltaSeconds) noexcept;
    void updateEnemies(float deltaSeconds) noexcept;
    void updateProjectiles(const Input& input, float deltaSeconds) noexcept;
    void damageEnemy(std::size_t index, int amount) noexcept;
    void resolveRoomEdges() noexcept;
    [[nodiscard]] framework::Status interact(int moveDirection) noexcept;
    [[nodiscard]] framework::Status syncDialogueState() noexcept;
    [[nodiscard]] framework::Status refreshDialogue() noexcept;
    [[nodiscard]] framework::Status updateDialogue(const Input& input) noexcept;
    [[nodiscard]] framework::Status claimQuestRewardIfReady() noexcept;
    [[nodiscard]] bool hasAbility(std::string_view id) const noexcept;
    [[nodiscard]] static framework::ActionResult grantDialogueItem(
        void* context, const framework::ActionRequest& request) noexcept;
    [[nodiscard]] framework::Status useActiveItem() noexcept;
    [[nodiscard]] framework::Status rebuildEquipment() noexcept;
    void setInventorySlot(std::size_t slot, ItemStack stack) noexcept;
    [[nodiscard]] int attributeValue(std::string_view id, int fallback) const noexcept;
    void damagePlayer() noexcept;
    void enterRoom(RoomId destination, bool fromLeft) noexcept;
    [[nodiscard]] bool collidesWithWorld(const Rect& bounds) const noexcept;
    [[nodiscard]] bool overlapsHazard(const Rect& bounds) const noexcept;
    [[nodiscard]] static bool overlaps(const Rect& first, const Rect& second) noexcept;

    RoomId room_{RoomId::LandingBay};
    Player player_{};
    std::array<Enemy, kEnemyCount> enemies_{};
    std::array<LootDrop, kLootDropCount> lootDrops_{};
    std::array<Projectile, 12> projectiles_{};
    std::array<Projectile, 6> enemyProjectiles_{};
    Rect abilityPickup_{};
    float coyoteTimer_{};
    float jumpBufferTimer_{};
    float invulnerabilityTimer_{};
    float shotCooldown_{};
    float speedBoostSeconds_{};
    float jumpBoostSeconds_{};
    float shieldSeconds_{};
    bool doubleJumpUsed_{};
    bool abilityCollected_{};
    bool gateBlocked_{};
    bool accessBlocked_{};
    bool completed_{};
    std::uint8_t questBranch_{};
    bool questRewardClaimed_{};
    std::size_t questChoiceSelection_{};
    int questChoiceDirection_{};
    int deaths_{};
    Inventory inventory_{};
    std::array<ItemStack, kEquipmentSlotCount> equipmentItems_{};
    ItemStack activeItem_{};
    Rect technician_{108.0F, 280.0F, 28.0F, 40.0F};
    float interactionReach_{kDefaultInteractionReach};
    const framework::DialogueModule* dialogueModule_{};
    framework::GameState* dialogueState_{};
    framework::DialogueSession dialogueSession_{};
    framework::DialogueLineView dialogueLine_{};
    std::array<framework::DialogueChoiceView, framework::kMaxDialogueChoices> dialogueChoices_{};
    std::size_t dialogueChoiceCount_{};
    std::size_t dialogueSelection_{};
    int dialogueDirection_{};
    framework::DialogueLanguage dialogueLanguage_{framework::DialogueLanguage::English};
    framework::InventoryModule* inventoryModule_{};
    framework::AttributeSet<8, 8> attributes_{};
    framework::AttributeSet<8, 8> baseAttributes_{};
    bool attributesConfigured_{};
    const framework::AbilityModule* abilityModule_{};
    framework::AbilitySet<8> abilities_{};
    const framework::EquipmentModule* equipmentModule_{};
    framework::EquipmentLoadout<4> equipment_{};
    const framework::QuestDefinition* mainQuest_{};
    const framework::CutsceneDefinition* centralCutscene_{};
    framework::CutscenePlayer cutscenePlayer_{};
};

} // namespace echo
