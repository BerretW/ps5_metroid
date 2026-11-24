#pragma once

#include "echo/framework/abilities.hpp"
#include "echo/framework/attributes.hpp"
#include "echo/framework/equipment.hpp"
#include "echo/framework/inventory.hpp"
#include "echo/framework/narrative.hpp"
#include "echo/framework/cutscenes.hpp"
#include "echo/framework/power.hpp"
#include "echo/framework/world.hpp"
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
inline constexpr std::uint8_t kCameraShakeDurationSteps = 18;

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
    CargoHold,
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
    // Vertical stick/D-pad direction (-1 up, 1 down); only isometric rooms use it.
    int moveY{};
    bool jumpPressed{};
    bool jumpHeld{};
    bool shootHeld{};
    bool interactPressed{};
    bool useItemPressed{};
    // Screen-space aim (x right, y down; any length) from the mouse or right stick.
    // Isometric rooms shoot along it; zero falls back to the movement direction.
    float aimX{};
    float aimY{};
};

struct Player {
    Vec2 position{};
    Vec2 velocity{};
    int health{5};
    int facing{1};
    bool onGround{};
    bool hasDoubleJump{};
    // Normalized room-space aim used by isometric rooms; side rooms aim along `facing`.
    Vec2 aim{1.0F, 0.0F};
};

// Stalkers roam isometric rooms; they are not saved and respawn on room entry.
enum class EnemyType : std::uint8_t { Patrol, Charger, Armored, Sentry, Stalker };

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
    std::array<char, 32> id{};
    float detectionRange{};
    float chargeSpeed{};
    float projectileSpeed{};
    float fireInterval{};
    float guardCycle{};
    float guardStart{};
    int guardReduction{};

    [[nodiscard]] bool guarding() const noexcept {
        return type == EnemyType::Armored && attackCooldown >= guardStart;
    }
};

struct Projectile {
    RoomId room{};
    Rect bounds{};
    float velocityX{};
    bool active{};
    float velocityY{};
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
inline constexpr std::size_t kRoomCount = static_cast<std::size_t>(RoomId::Count);
inline constexpr int kMaxLayoutColumns = framework::kMaxRoomColumns;
inline constexpr int kMaxLayoutRows = framework::kMaxRoomRows;
inline constexpr std::size_t kMaxWorldDoors = 8;
inline constexpr std::size_t kMaxWorldProps = 24;
inline constexpr std::size_t kMaxRoamers = 6;

// Save format v6 identifies enemies by slot, so each slot keeps its room.
inline constexpr std::array<RoomId, kEnemyCount> kEnemySlotRooms{
    RoomId::MaintenanceTunnel, RoomId::PowerStation, RoomId::MaintenanceTunnel,
    RoomId::PowerStation, RoomId::ResearchLab};
inline constexpr std::array<std::string_view, kRoomCount> kRoomResourceIds{
    "echo:room/landing_bay", "echo:room/maintenance_tunnel", "echo:room/power_station",
    "echo:room/research_lab", "echo:room/central_chamber", "echo:room/cargo_hold"};

struct RoomExitLayout {
    bool open{};
    bool completesGame{};
    bool requiresEchoStep{};
    RoomId room{};
};

struct RoomLayout {
    std::string_view name;
    int columns{kRoomColumns};
    int rows{kRoomRows};
    std::array<Tile, static_cast<std::size_t>(kMaxLayoutColumns) * kMaxLayoutRows> tiles{};
    std::array<std::uint8_t, 3> background{13, 30, 43};
    RoomExitLayout left{};
    RoomExitLayout right{};
    // Isometric rooms use top-down movement without gravity; x/y are grid-plane coordinates.
    bool isometric{};
    framework::RoomProjection projection{framework::RoomProjection::Side};
    // Visual tile layers; null for the built-in layout.
    const framework::RoomDefinition* definition{};

    [[nodiscard]] float width() const noexcept {
        return static_cast<float>(columns * kTileSize);
    }
    [[nodiscard]] float height() const noexcept {
        return static_cast<float>(rows * kTileSize);
    }
};

struct EnemySpawn {
    RoomId room{};
    Rect bounds{};
    float patrolLeft{};
    float patrolRight{};
    float speed{};
    int health{1};
    EnemyType type{EnemyType::Patrol};
    const framework::ObjectDefinition* archetype{};
    std::array<char, 32> id{};
    float detectionRange{};
    float chargeSpeed{};
    float projectileSpeed{};
    float fireInterval{};
    float guardCycle{};
    float guardStart{};
    int guardReduction{};
};

// A door is drawn; a trigger is an invisible interaction area.
struct DoorLayout {
    RoomId room{};
    Rect bounds{};
    RoomId destination{};
    bool arriveFromLeft{true};
    bool requiresKey{};
    bool visible{true};
    const framework::ObjectDefinition* archetype{};
    // Event-only triggers have no destination and fire on overlap.
    bool transitions{true};
    // Limits trigger actions to one firing; the fired state is saved per door index.
    bool once{};
    bool hasArrivePosition{};
    Vec2 arrivePosition{};
    std::array<char, 32> targetPlacement{};
    std::array<char, 96> requiresItem{};
    std::array<char, 96> requiresAbility{};
    std::array<char, 96> requiresCircuit{};
    std::array<char, 96> requiresFact{};
    std::array<char, 96> cutscene{};
    std::array<char, 96> setsFact{};
    std::array<char, 96> toggleCircuit{};

    [[nodiscard]] static std::string_view text(const std::array<char, 96>& buffer) noexcept {
        return buffer.data();
    }
    [[nodiscard]] bool hasActions() const noexcept {
        return cutscene[0] != '\0' || setsFact[0] != '\0' || toggleCircuit[0] != '\0';
    }
};

struct PropLayout {
    RoomId room{};
    std::array<char, 32> id{};
    Vec2 position{};
    bool facingLeft{};
    const framework::ObjectDefinition* archetype{};
    std::string_view interaction{};
    std::string_view circuit{};
    std::string_view dialogue{};
    std::string_view dialoguePowerCircuit{};
    std::string_view dialoguePowerState{};
};

struct WorldArchetypes {
    const framework::ObjectDefinition* player{};
    // Directional sprite set used while the player walks an isometric room.
    const framework::ObjectDefinition* playerIso{};
    const framework::ObjectDefinition* engineer{};
    const framework::ObjectDefinition* pickup{};
    const framework::ObjectDefinition* projectile{};
    const framework::ObjectDefinition* enemyProjectile{};
    const framework::ObjectDefinition* loot{};
};

struct WorldLayout {
    std::array<RoomLayout, kRoomCount> rooms{};
    framework::WorldLighting worldLighting{};
    std::array<EnemySpawn, kEnemyCount> enemies{};
    std::array<DoorLayout, kMaxWorldDoors> doors{};
    std::size_t doorCount{};
    std::array<PropLayout, kMaxWorldProps> props{};
    std::size_t propCount{};
    std::array<EnemySpawn, kMaxRoamers> roamers{};
    std::size_t roamerCount{};
    RoomId startRoom{RoomId::LandingBay};
    Vec2 playerStart{};
    Vec2 playerSize{20.0F, 28.0F};
    int playerFacing{1};
    // The engineer stands in the start room; position and size come from her object.
    Rect engineer{};
    RoomId abilityRoom{RoomId::ResearchLab};
    Rect abilityPickup{};
    WorldArchetypes archetypes{};

    [[nodiscard]] std::span<const DoorLayout> doorList() const noexcept {
        return {doors.data(), doorCount};
    }
    [[nodiscard]] std::span<const PropLayout> propList() const noexcept {
        return {props.data(), propCount};
    }
    [[nodiscard]] std::span<const EnemySpawn> roamerList() const noexcept {
        return {roamers.data(), roamerCount};
    }
};

// The compiled layout used when no world data is configured (and by tests).
[[nodiscard]] const WorldLayout& defaultWorldLayout() noexcept;
// Converts finalized world data into gameplay layout. The layout keeps
// non-owning pointers into `world`, which must outlive it.
[[nodiscard]] framework::Status buildWorldLayout(const framework::WorldModule& world,
                                                 WorldLayout& layout) noexcept;
[[nodiscard]] const PropLayout* nearbyPowerPanel(const WorldLayout& world, RoomId room,
                                                 Vec2 playerPosition) noexcept;
[[nodiscard]] framework::Status validateWorldLayout(const WorldLayout& layout) noexcept;

struct ItemStack {
    ItemId item{};
    std::uint16_t quantity{};
    friend constexpr bool operator==(const ItemStack&, const ItemStack&) noexcept = default;
};

struct EnemyLootDefinition {
    std::uint8_t enemySlot{};
    ItemStack stack{};
    friend constexpr bool operator==(const EnemyLootDefinition&,
                                     const EnemyLootDefinition&) noexcept = default;
};

class EnemyLootTable {
public:
    [[nodiscard]] framework::Status load(
        framework::JsonValue document,
        const framework::ItemCatalog& catalog) noexcept;
    [[nodiscard]] bool loaded() const noexcept { return loaded_; }
    [[nodiscard]] const EnemyLootDefinition* at(std::size_t saveSlot) const noexcept {
        return saveSlot < definitions_.size() ? &definitions_[saveSlot] : nullptr;
    }

private:
    std::array<EnemyLootDefinition, kLootDropCount> definitions_{};
    bool loaded_{};
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
    std::int32_t speedBoostAmount{};
    std::int32_t jumpBoostAmount{};
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
    // Bit N is set once door/trigger N (world door order) fired its once-only actions.
    std::uint8_t firedTriggers{};
};

class Game {
public:
    Game() noexcept;

    // Selects the room layout; restarts the game. The layout must outlive the game.
    [[nodiscard]] framework::Status configureWorld(const WorldLayout& layout) noexcept;
    [[nodiscard]] const WorldLayout& world() const noexcept { return *world_; }
    [[nodiscard]] const RoomLayout& currentRoom() const noexcept {
        return world_->rooms[static_cast<std::size_t>(room_)];
    }
    [[nodiscard]] int roomColumns() const noexcept { return currentRoom().columns; }
    [[nodiscard]] int roomRows() const noexcept { return currentRoom().rows; }
    [[nodiscard]] float roomWidth() const noexcept { return currentRoom().width(); }
    [[nodiscard]] float roomHeight() const noexcept { return currentRoom().height(); }
    [[nodiscard]] bool isometric() const noexcept { return currentRoom().isometric; }
    [[nodiscard]] framework::Status configureInventory(
        framework::InventoryModule& module) noexcept;
    [[nodiscard]] framework::Status configureLoot(
        const EnemyLootTable& table) noexcept;
    [[nodiscard]] framework::Status configureDialogue(
        const framework::DialogueModule& module, framework::GameState& state,
        float interactionReach = kDefaultInteractionReach,
        framework::InventoryModule* inventoryModule = nullptr) noexcept;
    [[nodiscard]] framework::Status configureAttributes(
        const framework::AttributesModule& module) noexcept;
    [[nodiscard]] framework::Status configureAbilities(
        const framework::AbilityModule& module) noexcept;
    [[nodiscard]] framework::Status configureEquipment(
        const framework::EquipmentModule& module) noexcept;
    [[nodiscard]] framework::Status configurePower(
        framework::PowerModule& module) noexcept;
    [[nodiscard]] framework::Status configureQuests(
        const framework::QuestModule& module) noexcept;
    [[nodiscard]] framework::Status configureCutscenes(
        const framework::CutsceneModule& module) noexcept;
    [[nodiscard]] framework::QuestProgress questProgress() const noexcept;
    [[nodiscard]] framework::Status cargoQuestProgress(
        framework::QuestProgress& progress) const noexcept;
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
    // Room-local isometric enemies of the current room.
    [[nodiscard]] std::span<const Enemy> roamers() const noexcept {
        return {roamers_.data(), roamerCount_};
    }
    [[nodiscard]] std::span<const LootDrop> lootDrops() const noexcept { return lootDrops_; }
    [[nodiscard]] std::span<const Projectile> projectiles() const noexcept { return projectiles_; }
    [[nodiscard]] std::span<const Projectile> enemyProjectiles() const noexcept { return enemyProjectiles_; }
    [[nodiscard]] bool propPosition(RoomId room, std::string_view id,
                                    Vec2& position) const noexcept;
    [[nodiscard]] framework::Status setPropPosition(RoomId room, std::string_view id,
                                                    Vec2 position) noexcept;
    [[nodiscard]] const PropLayout* nearbyPowerPanel(Vec2 playerPosition) const noexcept;
    [[nodiscard]] Tile tileAt(int column, int row) const noexcept;
    [[nodiscard]] float cameraX() const noexcept;
    [[nodiscard]] Vec2 cameraShakeOffset() const noexcept;
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
    [[nodiscard]] const PropLayout* nearbyDialogueProp() const noexcept;
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
    [[nodiscard]] std::string_view itemName(ItemId item) const noexcept;
    [[nodiscard]] std::string_view itemDescription(ItemId item) const noexcept;
    [[nodiscard]] std::string_view itemResource(ItemId item) const noexcept;
    [[nodiscard]] std::uint16_t itemStackLimit(ItemId item) const noexcept;
    [[nodiscard]] ItemStack expectedLoot(std::size_t index) const noexcept;
    [[nodiscard]] RoomId expectedLootRoom(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t lootEnemy(std::size_t index) const noexcept;
    [[nodiscard]] static std::string_view inventorySlotName(std::size_t slot) noexcept;
    [[nodiscard]] static bool validItemStackShape(
        std::size_t slot, ItemStack stack) noexcept;
    [[nodiscard]] bool acceptsItem(std::size_t slot, ItemStack stack) const noexcept;

private:
    void updatePlayer(const Input& input, float deltaSeconds) noexcept;
    void updateIsometricPlayer(const Input& input, float deltaSeconds) noexcept;
    void updateRoamers(float deltaSeconds) noexcept;
    void spawnRoamers() noexcept;
    void updateEnemies(float deltaSeconds) noexcept;
    void updateProjectiles(const Input& input, float deltaSeconds) noexcept;
    void damageEnemy(std::size_t index, int amount) noexcept;
    void resolveRoomEdges() noexcept;
    [[nodiscard]] framework::Status interact(int moveDirection) noexcept;
    [[nodiscard]] framework::Status syncDialogueState() noexcept;
    [[nodiscard]] framework::Status syncDialoguePropState(
        const PropLayout& prop) noexcept;
    [[nodiscard]] framework::Status refreshDialogue() noexcept;
    [[nodiscard]] framework::Status updateDialogue(const Input& input) noexcept;
    [[nodiscard]] framework::Status claimQuestRewardIfReady() noexcept;
    [[nodiscard]] bool hasAbility(std::string_view id) const noexcept;
    [[nodiscard]] static framework::ActionResult grantDialogueItem(
        void* context, const framework::ActionRequest& request) noexcept;
    [[nodiscard]] const framework::ItemDefinition* itemDefinition(
        ItemId item) const noexcept;
    [[nodiscard]] const framework::ItemDefinition* itemDefinition(
        std::string_view resource) const noexcept;
    [[nodiscard]] static std::size_t equipmentSlotIndex(
        std::string_view slot) noexcept;
    [[nodiscard]] framework::Status useActiveItem() noexcept;
    [[nodiscard]] framework::Status rebuildEquipment() noexcept;
    void setInventorySlot(std::size_t slot, ItemStack stack) noexcept;
    [[nodiscard]] int attributeValue(std::string_view id, int fallback) const noexcept;
    void damagePlayer() noexcept;
    void enterRoom(RoomId destination, bool fromLeft,
                   const Vec2* arrival = nullptr) noexcept;
    [[nodiscard]] bool factActive(std::string_view id) const noexcept;
    [[nodiscard]] framework::Status doorConditionsMet(const DoorLayout& door,
                                                      bool& met) const noexcept;
    [[nodiscard]] framework::Status runDoorActions(std::size_t doorIndex) noexcept;
    [[nodiscard]] bool collidesWithWorld(const Rect& bounds) const noexcept;
    [[nodiscard]] bool overlapsHazard(const Rect& bounds) const noexcept;
    [[nodiscard]] static bool overlaps(const Rect& first, const Rect& second) noexcept;

    RoomId room_{RoomId::LandingBay};
    const WorldLayout* world_{&defaultWorldLayout()};
    Player player_{};
    std::array<Enemy, kEnemyCount> enemies_{};
    std::array<Enemy, kMaxRoamers> roamers_{};
    std::size_t roamerCount_{};
    EnemyLootTable lootTable_{};
    std::array<LootDrop, kLootDropCount> lootDrops_{};
    std::array<Projectile, 12> projectiles_{};
    std::array<Projectile, 6> enemyProjectiles_{};
    struct RuntimePropTransform {
        Vec2 position{};
        bool overridden{};
    };
    std::array<RuntimePropTransform, kMaxWorldProps> runtimePropTransforms_{};
    Rect abilityPickup_{};
    float coyoteTimer_{};
    float jumpBufferTimer_{};
    float invulnerabilityTimer_{};
    float shotCooldown_{};
    float speedBoostSeconds_{};
    float jumpBoostSeconds_{};
    float shieldSeconds_{};
    std::int32_t speedBoostAmount_{};
    std::int32_t jumpBoostAmount_{};
    std::uint8_t cameraShakeTicks_{};
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
    Rect technician_{};
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
    framework::AttributeSet<16, 8> attributes_{};
    bool attributesConfigured_{};
    const framework::AbilityModule* abilityModule_{};
    framework::AbilitySet<8> abilities_{};
    const framework::EquipmentModule* equipmentModule_{};
    framework::EquipmentLoadout<4> equipment_{};
    framework::PowerModule* powerModule_{};
    const framework::QuestDefinition* mainQuest_{};
    const framework::QuestDefinition* cargoQuest_{};
    const framework::CutsceneDefinition* centralCutscene_{};
    const framework::CutsceneModule* cutsceneModule_{};
    std::uint8_t firedTriggers_{};
    framework::CutscenePlayer cutscenePlayer_{};
};

} // namespace echo
