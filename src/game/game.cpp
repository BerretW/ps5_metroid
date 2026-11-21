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
constexpr std::string_view kMaxHealthAttribute = "echo:attribute/player.max_health";
constexpr std::string_view kMoveSpeedAttribute = "echo:attribute/player.move_speed";
constexpr std::string_view kJumpImpulseAttribute = "echo:attribute/player.jump_impulse";
constexpr std::string_view kHealthCellHealAttribute = "echo:attribute/health_cell.heal";
constexpr std::string_view kProjectileDamageAttribute = "echo:attribute/player.projectile_damage";
constexpr std::array<framework::AttributeDefinition, 5> kPlayerAttributeDefinitions{{
    {kMaxHealthAttribute, 5, 1, 8},
    {kMoveSpeedAttribute, 190, 100, 250},
    {kJumpImpulseAttribute, 420, 300, 520},
    {kHealthCellHealAttribute, 2, 1, 4},
    {kProjectileDamageAttribute, 1, 1, 3},
}};
constexpr std::string_view kEngineerDialogue = "echo:dialogue/engineer";
constexpr std::string_view kGrantItemAction = "echo.inventory.grant";
constexpr std::string_view kMaintenanceKeyResource = "echo:item/maintenance_key";
constexpr std::string_view kHealthCellResource = "echo:item/health_cell";
constexpr framework::StateAddress kHasMaintenanceKey{
    framework::StateScope::Global, {}, "inventory.maintenance_key"};
constexpr framework::StateAddress kHasHealthCell{
    framework::StateScope::Global, {}, "inventory.health_cell"};
constexpr framework::StateAddress kHasKineticCoil{
    framework::StateScope::Global, {}, "equipment.kinetic_coil"};
constexpr framework::StateAddress kNeedsHealing{
    framework::StateScope::Global, {}, "player.needs_healing"};

struct ProjectItemMapping {
    std::string_view resource;
    ItemId id;
    std::uint32_t key;
    std::uint16_t maxStack;
    std::string_view equipmentId{};
    std::string_view name{};
    EquipmentSlot slot{EquipmentSlot::Count};
};

constexpr std::array<ProjectItemMapping, 14> kProjectItems{{
    {kMaintenanceKeyResource, ItemId::MaintenanceKey, 1, 1, {}, "MAINTENANCE KEY"},
    {kHealthCellResource, ItemId::HealthCell, 2, 3, {}, "HEALTH CELL"},
    {"echo:item/kinetic_coil", ItemId::KineticCoil, 3, 1,
     "echo:equipment/kinetic_coil", "KINETIC COIL", EquipmentSlot::Body},
    {"echo:item/scrap", ItemId::Scrap, 4, 99, {}, "SCRAP"},
    {"echo:item/jump_visor", ItemId::JumpVisor, 5, 1,
     "echo:equipment/jump_visor", "JUMP VISOR", EquipmentSlot::Head},
    {"echo:item/vital_vest", ItemId::VitalVest, 6, 1,
     "echo:equipment/vital_vest", "VITAL VEST", EquipmentSlot::Body},
    {"echo:item/power_barrel", ItemId::PowerBarrel, 7, 1,
     "echo:equipment/power_barrel", "POWER BARREL", EquipmentSlot::Weapon},
    {"echo:item/sprint_module", ItemId::SprintModule, 8, 1,
     "echo:equipment/sprint_module", "SPRINT MODULE", EquipmentSlot::Accessory},
    {"echo:item/med_patch", ItemId::MedPatch, 9, 3, {}, "MED PATCH"},
    {"echo:item/med_kit", ItemId::MedKit, 10, 3, {}, "MED KIT"},
    {"echo:item/shield_cell", ItemId::ShieldCell, 11, 3, {}, "SHIELD CELL"},
    {"echo:item/speed_charge", ItemId::SpeedCharge, 12, 3, {}, "SPEED CHARGE"},
    {"echo:item/jump_charge", ItemId::JumpCharge, 13, 3, {}, "JUMP CHARGE"},
    {"echo:item/blast_charge", ItemId::BlastCharge, 14, 3, {}, "BLAST CHARGE"},
}};

[[nodiscard]] const ProjectItemMapping* findProjectItem(ItemId id) noexcept {
    const auto found = std::find_if(kProjectItems.begin(), kProjectItems.end(),
        [id](const ProjectItemMapping& item) { return item.id == id; });
    return found == kProjectItems.end() ? nullptr : &*found;
}

[[nodiscard]] const ProjectItemMapping* findProjectItem(
    std::string_view resource) noexcept {
    const auto found = std::find_if(kProjectItems.begin(), kProjectItems.end(),
        [resource](const ProjectItemMapping& item) { return item.resource == resource; });
    return found == kProjectItems.end() ? nullptr : &*found;
}

[[nodiscard]] std::size_t roomIndex(RoomId room) noexcept {
    return static_cast<std::size_t>(room);
}

} // namespace

Game::Game() noexcept {
    start();
}

framework::Status Game::configureAttributes(
    const framework::AttributesModule& module) noexcept {
    if (!module.initialized()) {
        return {framework::ErrorCode::InvalidState, "echo.rpg.attributes",
                "The attributes module must be initialized before configuring gameplay."};
    }
    if (attributesConfigured_) {
        return {framework::ErrorCode::InvalidState, "echo.rpg.attributes",
                "Gameplay attributes are already configured."};
    }
    const framework::Status status = attributes_.configure(kPlayerAttributeDefinitions);
    if (status) {
        baseAttributes_ = attributes_;
        attributesConfigured_ = true;
        player_.health = std::min(player_.health, maximumHealth());
    }
    return status;
}

framework::Status Game::configureAbilities(
    const framework::AbilityModule& module) noexcept {
    if (!module.initialized()) {
        return {framework::ErrorCode::InvalidState, "echo.rpg.abilities",
                "The abilities module must be initialized before configuring gameplay."};
    }
    if (abilityModule_ != nullptr) {
        return {framework::ErrorCode::InvalidState, "echo.rpg.abilities",
                "Gameplay abilities are already configured."};
    }
    if (module.find(kEchoStepAbilityId) == nullptr) {
        return {framework::ErrorCode::MissingDependency, kEchoStepAbilityId,
                "The configured abilities module must provide Echo Step."};
    }
    abilityModule_ = &module;
    if (abilityCollected_) {
        const framework::Status status = abilities_.unlock(module, kEchoStepAbilityId);
        if (!status) {
            abilityModule_ = nullptr;
            return status;
        }
    }
    player_.hasDoubleJump = hasAbility(kEchoStepAbilityId);
    return framework::Status::success();
}

framework::Status Game::configureEquipment(
    const framework::EquipmentModule& module) noexcept {
    if (!module.initialized() || !attributesConfigured_) {
        return {framework::ErrorCode::InvalidState, "echo.rpg.equipment",
                "The equipment and attributes modules must be initialized before configuration."};
    }
    if (equipmentModule_ != nullptr) {
        return {framework::ErrorCode::InvalidState, "echo.rpg.equipment",
                "Gameplay equipment is already configured."};
    }
    equipmentModule_ = &module;
    const framework::Status status = rebuildEquipment();
    if (!status) {
        equipmentModule_ = nullptr;
        return status;
    }
    return framework::Status::success();
}

framework::Status Game::addAttributeModifier(
    std::string_view id, std::string_view source, std::int32_t amount) noexcept {
    if (!attributesConfigured_) {
        return {framework::ErrorCode::InvalidState, "echo.rpg.attributes",
                "The attributes module must be configured before adding modifiers."};
    }
    if (equipmentModule_ != nullptr && equipmentModule_->find(source) != nullptr) {
        return {framework::ErrorCode::InvalidState, source,
                "Equipment-owned modifiers must be changed through the equipment loadout."};
    }
    const framework::Status status = attributes_.addModifier(id, source, amount);
    if (status && id == kMaxHealthAttribute) {
        player_.health = std::min(player_.health, maximumHealth());
    }
    return status;
}

framework::Status Game::removeAttributeModifier(
    std::string_view id, std::string_view source) noexcept {
    if (!attributesConfigured_) {
        return {framework::ErrorCode::InvalidState, "echo.rpg.attributes",
                "The attributes module must be configured before removing modifiers."};
    }
    if (equipmentModule_ != nullptr && equipmentModule_->find(source) != nullptr) {
        return {framework::ErrorCode::InvalidState, source,
                "Equipment-owned modifiers must be changed through the equipment loadout."};
    }
    const framework::Status status = attributes_.removeModifier(id, source);
    if (status && id == kMaxHealthAttribute) {
        player_.health = std::min(player_.health, maximumHealth());
    }
    return status;
}

framework::Status Game::configureDialogue(
    const framework::DialogueModule& module, framework::GameState& state,
    Vec2 technicianPosition, float interactionReach,
    framework::InventoryModule* inventoryModule) noexcept {
    using namespace framework;
    if (dialogueSession_.active()) {
        return {ErrorCode::InvalidState, kEngineerDialogue,
                "Dialogue services cannot change during a conversation."};
    }
    if (!module.initialized()) {
        return {ErrorCode::InvalidState, kEngineerDialogue,
                "The dialogue module must be initialized before configuring the game."};
    }
    if (inventoryModule != nullptr && !inventoryModule->initialized()) {
        return {ErrorCode::InvalidState, "echo.rpg.inventory",
                "The inventory module must be initialized before configuring the game."};
    }
    if (!std::isfinite(technicianPosition.x) || !std::isfinite(technicianPosition.y) ||
        technicianPosition.x < 0.0F || technicianPosition.y < 0.0F ||
        technicianPosition.x > static_cast<float>(kRoomWidth) - technician_.width ||
        technicianPosition.y > static_cast<float>(kRoomHeight) - technician_.height) {
        return {ErrorCode::InvalidValue, "technician.transform",
                "The engineer position must fit inside the Landing Bay."};
    }
    if (!std::isfinite(interactionReach) || interactionReach < 0.0F ||
        interactionReach > kMaxInteractionReach) {
        return {ErrorCode::InvalidValue, "technician.interaction",
                "The engineer interaction reach is outside its supported range."};
    }
    const DialogueDefinition* definition = nullptr;
    const Status found = module.find(kEngineerDialogue, definition);
    if (!found) {
        return found;
    }
    for (const DialogueNode& node : definition->nodes) {
        for (const DialogueChoice& choice : node.choices) {
            const ActionRequest& action = choice.onSelect;
            if (action.id.empty()) {
                continue;
            }
            if (action.id != kGrantItemAction || action.parameters.size() != 1 ||
                action.parameters[0].name != "item" ||
                action.parameters[0].value.type != ValueType::String) {
                return {ErrorCode::InvalidValue, "dialogue.action",
                        "The engineer dialogue contains an unsupported action or parameter."};
            }
            const std::string_view resource =
                action.parameters[0].value.stringValue();
            const ProjectItemMapping* item = findProjectItem(resource);
            if (item == nullptr) {
                return {ErrorCode::InvalidValue, "dialogue.action",
                        "The engineer dialogue contains an unsupported item."};
            }
            if (inventoryModule != nullptr) {
                const ItemDefinition* catalogItem =
                    inventoryModule->catalog().find(resource);
                if (catalogItem == nullptr || catalogItem->key != item->key ||
                    catalogItem->maxStack != item->maxStack) {
                    return {ErrorCode::MissingDependency, resource,
                            "The dialogue item does not match its compiled inventory contract."};
                }
            }
        }
    }
    dialogueModule_ = &module;
    dialogueState_ = &state;
    inventoryModule_ = inventoryModule;
    technician_.x = technicianPosition.x;
    technician_.y = technicianPosition.y;
    interactionReach_ = interactionReach;
    return Status::success();
}

void Game::start() noexcept {
    if (attributesConfigured_) attributes_ = baseAttributes_;
    equipment_.resetAfterAttributesReset();
    room_ = RoomId::LandingBay;
    player_ = {{42.0F, kFloorY}, {}, maximumHealth(), 1, true, false};
    enemies_ = {{
        {RoomId::MaintenanceTunnel, {760.0F, kFloorY, 28.0F, 28.0F}, 700.0F, 910.0F, 45.0F, 2, true},
        {RoomId::PowerStation, {790.0F, kFloorY, 30.0F, 28.0F}, 720.0F, 940.0F, 52.0F, 3, true},
        {RoomId::MaintenanceTunnel, {390.0F, kFloorY, 28.0F, 28.0F}, 340.0F, 520.0F, 70.0F, 3, true, EnemyType::Charger},
        {RoomId::PowerStation, {530.0F, kFloorY, 30.0F, 28.0F}, 480.0F, 650.0F, 26.0F, 6, true, EnemyType::Armored},
        {RoomId::ResearchLab, {1000.0F, kFloorY, 28.0F, 28.0F}, 960.0F, 1100.0F, 0.0F, 4, true, EnemyType::Sentry},
    }};
    lootDrops_.fill({});
    projectiles_.fill({});
    enemyProjectiles_.fill({});
    abilityPickup_ = {690.0F, kFloorY + 2.0F, 20.0F, 24.0F};
    coyoteTimer_ = 0.0F;
    jumpBufferTimer_ = 0.0F;
    invulnerabilityTimer_ = 0.0F;
    shotCooldown_ = 0.0F;
    speedBoostSeconds_ = 0.0F;
    jumpBoostSeconds_ = 0.0F;
    shieldSeconds_ = 0.0F;
    doubleJumpUsed_ = false;
    abilityCollected_ = false;
    abilities_.reset();
    gateBlocked_ = false;
    accessBlocked_ = false;
    completed_ = false;
    dialogueSession_.reset();
    dialogueLine_ = {};
    dialogueChoices_.fill({});
    dialogueChoiceCount_ = 0;
    dialogueSelection_ = 0;
    dialogueDirection_ = 0;
    deaths_ = 0;
    inventory_ = {};
    equipmentItems_.fill({});
    activeItem_ = {};
}

GameCheckpoint Game::checkpoint() const noexcept {
    GameCheckpoint result{};
    result.room = room_;
    result.playerPosition = player_.position;
    result.playerVelocity = player_.velocity;
    result.playerHealth = player_.health;
    result.playerFacing = player_.facing;
    result.playerOnGround = player_.onGround;
    result.hasDoubleJump = player_.hasDoubleJump;
    result.abilityCollected = abilityCollected_;
    result.completed = completed_;
    result.deaths = deaths_;
    result.inventory = inventory_;
    result.equipment = equipmentItems_;
    result.activeItem = activeItem_;
    result.inventoryLayoutVersion = 2;
    for (std::size_t index = 0; index < enemies_.size(); ++index) {
        result.enemyHealth[index] = enemies_[index].alive ? enemies_[index].health : 0;
    }
    result.lootDrops = lootDrops_;
    result.speedBoostSeconds = speedBoostSeconds_;
    result.jumpBoostSeconds = jumpBoostSeconds_;
    result.shieldSeconds = shieldSeconds_;
    return result;
}

bool Game::restore(const GameCheckpoint& checkpointValue) noexcept {
    const std::size_t index = roomIndex(checkpointValue.room);
    if (index >= kRooms.size() ||
        !std::isfinite(checkpointValue.playerPosition.x) ||
        !std::isfinite(checkpointValue.playerPosition.y) ||
        !std::isfinite(checkpointValue.playerVelocity.x) ||
        !std::isfinite(checkpointValue.playerVelocity.y) ||
        checkpointValue.playerPosition.x < 0.0F ||
        checkpointValue.playerPosition.x > static_cast<float>(kRoomWidth) - kPlayerWidth ||
        checkpointValue.playerPosition.y < 0.0F ||
        checkpointValue.playerPosition.y > static_cast<float>(kRoomHeight) - kPlayerHeight ||
        checkpointValue.playerHealth < 1 ||
        (checkpointValue.playerFacing != -1 && checkpointValue.playerFacing != 1) ||
        checkpointValue.deaths < 0 ||
        checkpointValue.hasDoubleJump != checkpointValue.abilityCollected ||
        (checkpointValue.room == RoomId::CentralChamber &&
         !checkpointValue.hasDoubleJump) ||
        ((checkpointValue.room == RoomId::ResearchLab ||
          checkpointValue.room == RoomId::CentralChamber) &&
         !checkpointValue.inventory.contains(ItemId::MaintenanceKey)) ||
        (checkpointValue.completed &&
         (checkpointValue.room != RoomId::CentralChamber || !checkpointValue.hasDoubleJump))) {
        return false;
    }
    if (!checkpointValue.inventory.valid() ||
        (checkpointValue.inventoryLayoutVersion != 1 &&
         checkpointValue.inventoryLayoutVersion != 2)) {
        return false;
    }
    for (std::size_t itemIndex = 0; itemIndex < Inventory::kCapacity; ++itemIndex) {
        const ItemId item = checkpointValue.inventory.items[itemIndex];
        if (item == ItemId::None) continue;
        if (item >= ItemId::Count) return false;
        std::uint16_t maxStack = itemStackLimit(item);
        if (inventoryModule_ != nullptr) {
            const framework::ItemDefinition* definition =
                inventoryModule_->catalog().find(static_cast<std::uint32_t>(item));
            if (definition == nullptr) return false;
            maxStack = definition->maxStack;
        }
        if (checkpointValue.inventory.quantities[itemIndex] > maxStack) return false;
    }

    Game restored = *this;
    restored.start();
    restored.inventory_ = checkpointValue.inventory;
    restored.lootDrops_ = checkpointValue.lootDrops;
    restored.equipmentItems_ = checkpointValue.equipment;
    restored.activeItem_ = checkpointValue.activeItem;
    for (std::size_t slot = kFirstEquipmentSlot; slot < kInventorySlotCount; ++slot) {
        if (!acceptsItem(slot, restored.inventorySlot(slot))) return false;
    }
    for (const ProjectItemMapping& item : kProjectItems) {
        if (item.maxStack == 1 && restored.itemQuantity(item.id) > 1) return false;
    }
    if (checkpointValue.inventoryLayoutVersion == 1) {
        for (const ItemStack stack : checkpointValue.equipment) {
            if (stack != ItemStack{}) return false;
        }
        if (checkpointValue.activeItem != ItemStack{}) return false;
        if (restored.inventory_.contains(ItemId::KineticCoil)) {
            if (!restored.inventory_.remove(ItemId::KineticCoil)) return false;
            restored.equipmentItems_[static_cast<std::size_t>(EquipmentSlot::Body)] =
                {ItemId::KineticCoil, 1};
        }
        for (std::size_t slot = 0; slot < Inventory::kCapacity; ++slot) {
            if (restored.inventory_.items[slot] == ItemId::HealthCell) {
                restored.activeItem_ = {ItemId::HealthCell,
                                       restored.inventory_.quantities[slot]};
                restored.setInventorySlot(slot, {});
                break;
            }
        }
    }
    if (checkpointValue.abilityCollected && restored.abilityModule_ != nullptr) {
        const framework::Status unlocked =
            restored.abilities_.unlock(*restored.abilityModule_, kEchoStepAbilityId);
        if (!unlocked) return false;
    }
    if (!restored.rebuildEquipment()) return false;
    if (!std::isfinite(checkpointValue.speedBoostSeconds) ||
        !std::isfinite(checkpointValue.jumpBoostSeconds) ||
        !std::isfinite(checkpointValue.shieldSeconds) ||
        checkpointValue.speedBoostSeconds < 0 || checkpointValue.speedBoostSeconds > 8 ||
        checkpointValue.jumpBoostSeconds < 0 || checkpointValue.jumpBoostSeconds > 8 ||
        checkpointValue.shieldSeconds < 0 || checkpointValue.shieldSeconds > 4) return false;
    restored.speedBoostSeconds_ = checkpointValue.speedBoostSeconds;
    restored.jumpBoostSeconds_ = checkpointValue.jumpBoostSeconds;
    restored.shieldSeconds_ = checkpointValue.shieldSeconds;
    const float maximumPlayerSpeed = static_cast<float>(
        restored.attributeValue(kMoveSpeedAttribute, static_cast<int>(kPlayerSpeed))) +
        (restored.speedBoostSeconds_ > 0 ? 25.0F : 0.0F);
    const float playerJumpImpulse = static_cast<float>(
        restored.attributeValue(kJumpImpulseAttribute, static_cast<int>(kJumpImpulse))) + 60.0F;
    if (std::abs(checkpointValue.playerVelocity.x) > maximumPlayerSpeed ||
        checkpointValue.playerVelocity.y < -playerJumpImpulse ||
        checkpointValue.playerVelocity.y > kMaximumFallSpeed ||
        checkpointValue.playerHealth > restored.maximumHealth()) {
        return false;
    }

    for (std::size_t enemy = 0; enemy < checkpointValue.enemyHealth.size(); ++enemy) {
        constexpr std::array<int, kEnemyCount> maximumHealth{2, 3, 3, 6, 4};
        if (checkpointValue.enemyHealth[enemy] < 0 ||
            checkpointValue.enemyHealth[enemy] > maximumHealth[enemy]) {
            return false;
        }
    }
    for (std::size_t lootIndex = 0; lootIndex < checkpointValue.lootDrops.size(); ++lootIndex) {
        const LootDrop& drop = checkpointValue.lootDrops[lootIndex];
        if (!std::isfinite(drop.position.x) || !std::isfinite(drop.position.y) ||
            drop.position.x < 0.0F ||
            drop.position.x > static_cast<float>(kRoomWidth) - 16.0F ||
            drop.position.y < 0.0F ||
            drop.position.y > static_cast<float>(kRoomHeight) - 16.0F) return false;
        if (drop.active) {
            if (checkpointValue.enemyHealth[lootEnemy(lootIndex)] != 0 ||
                drop.stack != expectedLoot(lootIndex) ||
                drop.room != expectedLootRoom(lootIndex) ||
                (lootIndex >= 2 && drop.position.y != kFloorY + 12.0F)) {
                return false;
            }
        } else if (drop.stack != ItemStack{}) {
            return false;
        }
    }

    restored.room_ = checkpointValue.room;
    restored.player_.position = checkpointValue.playerPosition;
    restored.player_.velocity = checkpointValue.playerVelocity;
    restored.player_.health = checkpointValue.playerHealth;
    restored.player_.facing = checkpointValue.playerFacing;
    restored.player_.onGround = checkpointValue.playerOnGround;
    restored.abilityCollected_ = checkpointValue.abilityCollected;
    restored.player_.hasDoubleJump = restored.hasAbility(kEchoStepAbilityId);
    restored.completed_ = checkpointValue.completed;
    restored.deaths_ = checkpointValue.deaths;
    for (std::size_t enemy = 0; enemy < restored.enemies_.size(); ++enemy) {
        restored.enemies_[enemy].health = checkpointValue.enemyHealth[enemy];
        restored.enemies_[enemy].alive = checkpointValue.enemyHealth[enemy] > 0;
    }
    if (restored.collidesWithWorld({restored.player_.position.x,
                                    restored.player_.position.y,
                                    kPlayerWidth, kPlayerHeight})) {
        return false;
    }
    *this = restored;
    return true;
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

Rect Game::technicianBounds() const noexcept {
    return technician_;
}

bool Game::technicianNearby() const noexcept {
    if (room_ != RoomId::LandingBay) {
        return false;
    }
    const Rect reach{technician_.x - interactionReach_, technician_.y - 20.0F,
                     technician_.width + 2.0F * interactionReach_, technician_.height + 40.0F};
    return overlaps({player_.position.x, player_.position.y, kPlayerWidth, kPlayerHeight}, reach);
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

framework::Status Game::update(const Input& input, float deltaSeconds) noexcept {
    if (completed_) {
        return framework::Status::success();
    }

    const float dt = std::clamp(deltaSeconds, 0.0F, 0.05F);
    gateBlocked_ = false;
    accessBlocked_ = false;
    if (dialogueSession_.active()) {
        return updateDialogue(input);
    }
    if (input.useItemPressed) {
        const framework::Status status = useActiveItem();
        if (!status) return status;
    }
    invulnerabilityTimer_ = std::max(0.0F, invulnerabilityTimer_ - dt);
    shotCooldown_ = std::max(0.0F, shotCooldown_ - dt);
    speedBoostSeconds_ = std::max(0.0F, speedBoostSeconds_ - dt);
    jumpBoostSeconds_ = std::max(0.0F, jumpBoostSeconds_ - dt);
    shieldSeconds_ = std::max(0.0F, shieldSeconds_ - dt);

    updatePlayer(input, dt);
    updateEnemies(dt);
    updateProjectiles(input, dt);
    for (LootDrop& drop : lootDrops_) {
        if (!drop.active || drop.room != room_ ||
            !overlaps({player_.position.x, player_.position.y, kPlayerWidth, kPlayerHeight},
                      {drop.position.x, drop.position.y, 16.0F, 16.0F})) {
            continue;
        }
        const ProjectItemMapping* mapping = findProjectItem(drop.stack.item);
        if (mapping == nullptr) {
            return {framework::ErrorCode::InvalidState, "loot.item", "Unknown loot item."};
        }
        const std::uint16_t maxStack = mapping->maxStack;
        if (inventoryModule_ != nullptr) {
            const framework::ItemDefinition* definition =
                inventoryModule_->catalog().find(mapping->resource);
            if (definition == nullptr || definition->key != mapping->key ||
                definition->maxStack != maxStack) {
                return {framework::ErrorCode::MissingDependency, mapping->resource,
                        "Enemy loot requires its matching catalog definition."};
            }
        }
        bool collected = false;
        if (mapping->slot != EquipmentSlot::Count && equipmentModule_ != nullptr &&
            attributesConfigured_ && !hasItem(mapping->id)) {
            const std::size_t slot = kFirstEquipmentSlot + static_cast<std::size_t>(mapping->slot);
            if (inventorySlot(slot).item == ItemId::None) {
                Game candidate = *this;
                candidate.setInventorySlot(slot, drop.stack);
                const framework::Status status = candidate.rebuildEquipment();
                if (!status) return status;
                *this = candidate;
                collected = true;
            }
        }
        if (!collected && (maxStack != 1 || !hasItem(mapping->id))) {
            collected = inventory_.add(mapping->id, maxStack, drop.stack.quantity);
        }
        if (collected) {
            drop.active = false;
            drop.stack = {};
        }
    }

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

    if (!hasAbility(kEchoStepAbilityId) && room_ == RoomId::ResearchLab &&
        overlaps({player_.position.x, player_.position.y, kPlayerWidth, kPlayerHeight}, abilityPickup_)) {
        if (abilityModule_ != nullptr) {
            const framework::Status unlocked =
                abilities_.unlock(*abilityModule_, kEchoStepAbilityId);
            if (!unlocked) return unlocked;
        }
        abilityCollected_ = true;
        player_.hasDoubleJump = true;
    }

    if (input.interactPressed) {
        const framework::Status status = interact(input.moveX);
        if (!status) {
            return status;
        }
    }
    resolveRoomEdges();
    return framework::Status::success();
}

void Game::updatePlayer(const Input& input, float deltaSeconds) noexcept {
    const float playerSpeed =
        static_cast<float>(attributeValue(kMoveSpeedAttribute, static_cast<int>(kPlayerSpeed))) +
        (speedBoostSeconds_ > 0 ? 25.0F : 0.0F);
    const float jumpImpulse =
        static_cast<float>(attributeValue(kJumpImpulseAttribute, static_cast<int>(kJumpImpulse))) +
        (jumpBoostSeconds_ > 0 ? 60.0F : 0.0F);
    const int direction = std::clamp(input.moveX, -1, 1);
    if (direction != 0) {
        player_.facing = direction;
        player_.velocity.x += static_cast<float>(direction) * kHorizontalAcceleration * deltaSeconds;
        player_.velocity.x = std::clamp(player_.velocity.x, -playerSpeed, playerSpeed);
    } else {
        const float slowdown = kHorizontalAcceleration * deltaSeconds;
        if (std::abs(player_.velocity.x) <= slowdown) {
            player_.velocity.x = 0.0F;
        } else {
            player_.velocity.x -= std::copysign(slowdown, player_.velocity.x);
        }
    }
    player_.velocity.x = std::clamp(player_.velocity.x, -playerSpeed, playerSpeed);

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
        player_.velocity.y = -jumpImpulse;
        player_.onGround = false;
        doubleJumpUsed_ = false;
        jumpBufferTimer_ = 0.0F;
        coyoteTimer_ = 0.0F;
    } else if (jumpBufferTimer_ > 0.0F && hasAbility(kEchoStepAbilityId) &&
               !player_.onGround && !doubleJumpUsed_) {
        player_.velocity.y = -jumpImpulse * 0.92F;
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
        if (enemy.type == EnemyType::Sentry) {
            enemy.attackCooldown = std::max(0.0F, enemy.attackCooldown - deltaSeconds);
            if (enemy.attackCooldown == 0.0F &&
                std::abs(player_.position.x - enemy.bounds.x) < 420.0F) {
                const auto shot = std::find_if(enemyProjectiles_.begin(), enemyProjectiles_.end(),
                    [](const Projectile& projectile) { return !projectile.active; });
                if (shot != enemyProjectiles_.end()) {
                    const float direction = player_.position.x < enemy.bounds.x ? -1.0F : 1.0F;
                    *shot = {enemy.room, {enemy.bounds.x, enemy.bounds.y + 12.0F, 10.0F, 6.0F},
                             direction * 220.0F, true};
                    enemy.attackCooldown = 1.5F;
                }
            }
            continue;
        }
        if (enemy.type == EnemyType::Armored) {
            enemy.attackCooldown += deltaSeconds;
            if (enemy.attackCooldown >= 3.0F) enemy.attackCooldown -= 3.0F;
            if (enemy.attackCooldown >= 2.0F) continue;
        }
        float speed = enemy.speed;
        if (enemy.type == EnemyType::Charger &&
            std::abs(player_.position.x - enemy.bounds.x) < 220.0F) {
            speed = player_.position.x < enemy.bounds.x ? -130.0F : 130.0F;
        }
        enemy.bounds.x += speed * deltaSeconds;
        if (enemy.bounds.x < enemy.patrolLeft) {
            enemy.bounds.x = enemy.patrolLeft;
            enemy.speed = std::abs(enemy.speed);
        } else if (enemy.bounds.x + enemy.bounds.width > enemy.patrolRight) {
            enemy.bounds.x = enemy.patrolRight - enemy.bounds.width;
            enemy.speed = -std::abs(enemy.speed);
        }
    }
}

void Game::damageEnemy(std::size_t index, int amount) noexcept {
    Enemy& enemy = enemies_[index];
    if (!enemy.alive) return;
    if (enemy.type == EnemyType::Armored && enemy.attackCooldown >= 2.0F) {
        amount = std::max(0, amount - 1);
    }
    enemy.health = std::max(0, enemy.health - amount);
    if (enemy.health != 0) return;
    enemy.alive = false;
    for (std::size_t loot = 0; loot < lootDrops_.size(); ++loot) {
        if (lootEnemy(loot) != index) continue;
        const float offset = index < 2 ? 0.0F :
            static_cast<float>(static_cast<int>(loot) -
                (index == 2 ? 3 : index == 3 ? 6 : 9)) * 18.0F;
        lootDrops_[loot] = {
            enemy.room,
            {std::clamp(enemy.bounds.x + (enemy.bounds.width - 16.0F) * 0.5F + offset,
                        0.0F, static_cast<float>(kRoomWidth) - 16.0F),
             enemy.bounds.y + 12.0F},
            expectedLoot(loot), true};
    }
}

void Game::updateProjectiles(const Input& input, float deltaSeconds) noexcept {
    for (Projectile& projectile : enemyProjectiles_) {
        if (!projectile.active) continue;
        projectile.bounds.x += projectile.velocityX * deltaSeconds;
        if (projectile.room != room_ || projectile.bounds.x < 0.0F ||
            projectile.bounds.x > static_cast<float>(kRoomWidth) ||
            collidesWithWorld(projectile.bounds)) {
            projectile.active = false;
        } else if (overlaps(projectile.bounds,
                            {player_.position.x, player_.position.y, kPlayerWidth, kPlayerHeight})) {
            projectile.active = false;
            damagePlayer();
        }
    }
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
        for (std::size_t index = 0; index < enemies_.size(); ++index) {
            Enemy& enemy = enemies_[index];
            if (enemy.alive && enemy.room == projectile.room && overlaps(projectile.bounds, enemy.bounds)) {
                damageEnemy(index, attributeValue(kProjectileDamageAttribute, 1));
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

framework::Status Game::syncDialogueState() noexcept {
    if (dialogueState_ == nullptr) {
        return {framework::ErrorCode::InvalidState, kEngineerDialogue,
                "Dialogue state has not been configured."};
    }
    framework::Status status = dialogueState_->set(
        kHasMaintenanceKey,
        framework::StateValue::fromBoolean(hasItem(ItemId::MaintenanceKey)));
    if (!status) return status;
    status = dialogueState_->set(
        kHasHealthCell,
        framework::StateValue::fromBoolean(hasItem(ItemId::HealthCell)));
    if (!status) return status;
    status = dialogueState_->set(
        kHasKineticCoil,
        framework::StateValue::fromBoolean(
            equipmentModule_ == nullptr ||
            hasItem(ItemId::KineticCoil)));
    if (!status) return status;
    return dialogueState_->set(
        kNeedsHealing,
        framework::StateValue::fromBoolean(player_.health < maximumHealth()));
}

framework::Status Game::setDialogueLanguage(framework::DialogueLanguage language) noexcept {
    using namespace framework;
    if (language != DialogueLanguage::English && language != DialogueLanguage::Czech) {
        return {ErrorCode::InvalidValue, "dialogue.language",
                "Dialogue language is not supported."};
    }
    const DialogueLanguage previous = dialogueLanguage_;
    dialogueLanguage_ = language;
    const Status status = refreshDialogue();
    if (!status) {
        dialogueLanguage_ = previous;
    }
    return status;
}

framework::Status Game::refreshDialogue() noexcept {
    if (!dialogueSession_.active()) {
        dialogueLine_ = {};
        dialogueChoices_.fill({});
        dialogueChoiceCount_ = 0;
        dialogueSelection_ = 0;
        return framework::Status::success();
    }
    framework::DialogueLineView line{};
    const framework::Status inspected =
        dialogueSession_.inspect(*dialogueState_, line, dialogueLanguage_);
    if (!inspected) {
        return inspected;
    }
    std::array<framework::DialogueChoiceView, framework::kMaxDialogueChoices> choices{};
    for (std::size_t index = 0; index < line.availableChoices; ++index) {
        const framework::Status status =
            dialogueSession_.choiceAt(*dialogueState_, index, choices[index], dialogueLanguage_);
        if (!status) {
            return status;
        }
    }
    dialogueLine_ = line;
    dialogueChoices_ = choices;
    dialogueChoiceCount_ = line.availableChoices;
    if (dialogueSelection_ >= dialogueChoiceCount_) {
        dialogueSelection_ = 0;
    }
    return framework::Status::success();
}

ItemStack Game::inventorySlot(std::size_t slot) const noexcept {
    if (slot < Inventory::kCapacity) {
        return {inventory_.items[slot], inventory_.quantities[slot]};
    }
    if (slot < kActiveItemSlot) return equipmentItems_[slot - kFirstEquipmentSlot];
    return slot == kActiveItemSlot ? activeItem_ : ItemStack{};
}

void Game::setInventorySlot(std::size_t slot, ItemStack stack) noexcept {
    if (slot < Inventory::kCapacity) {
        const bool wasOccupied = inventory_.items[slot] != ItemId::None;
        const bool occupied = stack.item != ItemId::None;
        if (wasOccupied && !occupied) --inventory_.count;
        if (!wasOccupied && occupied) ++inventory_.count;
        inventory_.items[slot] = stack.item;
        inventory_.quantities[slot] = stack.quantity;
    } else if (slot < kActiveItemSlot) {
        equipmentItems_[slot - kFirstEquipmentSlot] = stack;
    } else if (slot == kActiveItemSlot) {
        activeItem_ = stack;
    }
}

bool Game::acceptsItem(std::size_t slot, ItemStack stack) noexcept {
    if (slot >= kInventorySlotCount) return false;
    if (stack.item == ItemId::None) return stack.quantity == 0;
    if (stack.item >= ItemId::Count || stack.quantity == 0) return false;
    const std::uint16_t limit = itemStackLimit(stack.item);
    if (stack.quantity > limit) return false;
    if (slot < Inventory::kCapacity) return true;
    if (slot == kActiveItemSlot) {
        return stack.item == ItemId::HealthCell ||
               (stack.item >= ItemId::MedPatch && stack.item <= ItemId::BlastCharge);
    }
    const ProjectItemMapping* item = findProjectItem(stack.item);
    return item != nullptr && item->slot != EquipmentSlot::Count &&
           slot == kFirstEquipmentSlot + static_cast<std::size_t>(item->slot);
}

std::uint32_t Game::itemQuantity(ItemId item) const noexcept {
    if (item == ItemId::None || item >= ItemId::Count) return 0;
    std::uint32_t total = inventory_.quantity(item);
    for (const ItemStack stack : equipmentItems_) {
        if (stack.item == item) total += stack.quantity;
    }
    if (activeItem_.item == item) total += activeItem_.quantity;
    return total;
}

std::string_view Game::itemName(ItemId item) noexcept {
    const ProjectItemMapping* mapping = findProjectItem(item);
    return mapping == nullptr ? "EMPTY" : mapping->name;
}

std::string_view Game::itemResource(ItemId item) noexcept {
    const ProjectItemMapping* mapping = findProjectItem(item);
    return mapping == nullptr ? std::string_view{} : mapping->resource;
}

std::uint16_t Game::itemStackLimit(ItemId item) noexcept {
    const ProjectItemMapping* mapping = findProjectItem(item);
    return mapping == nullptr ? 0 : mapping->maxStack;
}

ItemStack Game::expectedLoot(std::size_t index) noexcept {
    constexpr std::array<ItemStack, kLootDropCount> loot{{
        {ItemId::Scrap, 2}, {ItemId::Scrap, 3},
        {ItemId::JumpVisor, 1}, {ItemId::MedPatch, 1}, {ItemId::SpeedCharge, 1},
        {ItemId::VitalVest, 1}, {ItemId::PowerBarrel, 1}, {ItemId::MedKit, 1},
        {ItemId::SprintModule, 1}, {ItemId::ShieldCell, 1},
        {ItemId::JumpCharge, 1}, {ItemId::BlastCharge, 1},
    }};
    return index < loot.size() ? loot[index] : ItemStack{};
}

std::size_t Game::lootEnemy(std::size_t index) noexcept {
    if (index < 2) return index;
    if (index < 5) return 2;
    if (index < 8) return 3;
    return index < kLootDropCount ? 4 : kEnemyCount;
}

RoomId Game::expectedLootRoom(std::size_t index) noexcept {
    const std::size_t enemy = lootEnemy(index);
    return enemy == 0 || enemy == 2 ? RoomId::MaintenanceTunnel :
           enemy == 1 || enemy == 3 ? RoomId::PowerStation : RoomId::ResearchLab;
}

std::string_view Game::inventorySlotName(std::size_t slot) noexcept {
    if (slot < Inventory::kCapacity) return "BACKPACK";
    constexpr std::array<std::string_view, kEquipmentSlotCount> names{
        "HEAD", "BODY", "WEAPON", "ACCESSORY"};
    if (slot < kActiveItemSlot) return names[slot - kFirstEquipmentSlot];
    return slot == kActiveItemSlot ? "ACTIVE ITEM" : "INVALID SLOT";
}

framework::Status Game::rebuildEquipment() noexcept {
    bool hasGear = false;
    for (const ItemStack stack : equipmentItems_) hasGear |= stack.item != ItemId::None;
    if (equipmentModule_ == nullptr || !attributesConfigured_) {
        return hasGear
            ? framework::Status{framework::ErrorCode::MissingDependency,
                                "echo.rpg.equipment",
                                "Equipped gear requires the equipment and attributes modules."}
            : framework::Status::success();
    }
    constexpr std::array<std::string_view, kEquipmentSlotCount> slots{
        "echo:slot/head", "echo:slot/suit", "echo:slot/weapon", "echo:slot/accessory"};
    for (const std::string_view slot : slots) {
        if (equipment_.occupies(slot)) {
            const framework::Status status = equipment_.unequip(*equipmentModule_, slot, attributes_);
            if (!status) return status;
        }
    }
    for (const ItemStack stack : equipmentItems_) {
        if (stack.item == ItemId::None) continue;
        const ProjectItemMapping* mapping = findProjectItem(stack.item);
        if (mapping == nullptr || mapping->equipmentId.empty()) {
            return {framework::ErrorCode::InvalidValue, "equipment", "Unknown equipped item."};
        }
        const framework::Status status = equipment_.equip(*equipmentModule_, mapping->equipmentId, attributes_);
        if (!status) return status;
    }
    const float speed = static_cast<float>(
        attributeValue(kMoveSpeedAttribute, static_cast<int>(kPlayerSpeed)));
    player_.velocity.x = std::clamp(player_.velocity.x, -speed, speed);
    player_.health = std::min(player_.health, maximumHealth());
    return framework::Status::success();
}

framework::Status Game::moveInventorySlot(std::size_t source,
                                         std::size_t destination) noexcept {
    using namespace framework;
    if (source >= kInventorySlotCount || destination >= kInventorySlotCount) {
        return {ErrorCode::InvalidArgument, "inventory.slot", "Slot index is out of range."};
    }
    if (source == destination) return Status::success();
    const ItemStack from = inventorySlot(source);
    const ItemStack to = inventorySlot(destination);
    if (from.item == ItemId::None) {
        return {ErrorCode::InvalidValue, "inventory.slot", "The source slot is empty."};
    }
    Game candidate = *this;
    if (from.item == to.item && itemStackLimit(from.item) > 1) {
        const unsigned quantity = static_cast<unsigned>(from.quantity) + to.quantity;
        if (quantity > itemStackLimit(from.item)) {
            return {ErrorCode::CapacityExceeded, "inventory.slot",
                    "The complete stack does not fit in the destination."};
        }
        const ItemStack merged{from.item, static_cast<std::uint16_t>(quantity)};
        if (!acceptsItem(destination, merged)) {
            return {ErrorCode::InvalidValue, "inventory.slot", "Incompatible destination slot."};
        }
        candidate.setInventorySlot(source, {});
        candidate.setInventorySlot(destination, merged);
    } else {
        if (!acceptsItem(destination, from) || !acceptsItem(source, to)) {
            return {ErrorCode::InvalidValue, "inventory.slot",
                    "The item is incompatible with the equipment or active slot."};
        }
        candidate.setInventorySlot(source, to);
        candidate.setInventorySlot(destination, from);
    }
    const Status status = candidate.rebuildEquipment();
    if (!status) return status;
    *this = candidate;
    return Status::success();
}

framework::ActionResult Game::grantDialogueItem(
    void* context, const framework::ActionRequest& request) noexcept {
    using namespace framework;
    auto* game = static_cast<Game*>(context);
    if (game == nullptr || request.parameters.size() != 1 ||
        request.parameters[0].name != "item" ||
        request.parameters[0].value.type != ValueType::String) {
        return {{ErrorCode::InvalidValue, kGrantItemAction,
                 "The inventory action requires one supported project item."}, {}, false};
    }
    const ProjectItemMapping* item =
        findProjectItem(request.parameters[0].value.stringValue());
    if (item == nullptr) {
        return {{ErrorCode::InvalidValue, kGrantItemAction,
                 "The inventory action references an unsupported project item."}, {}, false};
    }
    std::uint16_t maxStack = item->maxStack;
    if (game->inventoryModule_ != nullptr) {
        const framework::ItemDefinition* definition =
            game->inventoryModule_->catalog().find(item->resource);
        if (definition == nullptr || definition->key != item->key ||
            definition->maxStack != item->maxStack) {
            return {{ErrorCode::MissingDependency, item->resource,
                     "The item has no matching compiled inventory definition."},
                    {}, false};
        }
        maxStack = definition->maxStack;
    }
    if (!item->equipmentId.empty() &&
        (game->equipmentModule_ == nullptr || !game->attributesConfigured_)) {
        return {{ErrorCode::MissingDependency, item->resource,
                 "Equipping this item requires the selected equipment and attributes modules."},
                {}, false};
    }
    if (item->maxStack == 1 && game->hasItem(item->id)) {
        return {{ErrorCode::DuplicateId, item->resource,
                 "This unique item is already owned."}, {}, false};
    }
    Game candidate = *game;
    const std::size_t equipmentSlot = kFirstEquipmentSlot +
        static_cast<std::size_t>(item->slot);
    if (!item->equipmentId.empty() &&
        candidate.inventorySlot(equipmentSlot).item == ItemId::None) {
        candidate.setInventorySlot(equipmentSlot, {item->id, 1});
        const Status equipped = candidate.rebuildEquipment();
        if (!equipped) return {equipped, {}, false};
    } else if (!candidate.inventory_.add(item->id, maxStack, 1)) {
        return {{ErrorCode::CapacityExceeded, item->resource,
                 "The item could not be added to the inventory."}, {}, false};
    }
    *game = candidate;
    return {Status::success(), {}, false};
}

framework::Status Game::useActiveItem() noexcept {
    const ItemId item = activeItem_.item;
    if (item == ItemId::None) return framework::Status::success();
    if (item == ItemId::HealthCell || item == ItemId::MedPatch || item == ItemId::MedKit) {
        if (player_.health >= maximumHealth()) return framework::Status::success();
        const int healing = item == ItemId::HealthCell ? attributeValue(kHealthCellHealAttribute, 2) :
                            item == ItemId::MedPatch ? 1 : 4;
        player_.health = std::min(maximumHealth(), player_.health + healing);
    } else if (item == ItemId::ShieldCell) {
        shieldSeconds_ = 4.0F;
    } else if (item == ItemId::SpeedCharge) {
        speedBoostSeconds_ = 8.0F;
    } else if (item == ItemId::JumpCharge) {
        jumpBoostSeconds_ = 8.0F;
    } else if (item == ItemId::BlastCharge) {
        for (std::size_t index = 0; index < enemies_.size(); ++index) {
            if (enemies_[index].alive && enemies_[index].room == room_ &&
                std::abs(enemies_[index].bounds.x - player_.position.x) < 140.0F) {
                damageEnemy(index, 2);
            }
        }
    } else {
        return {framework::ErrorCode::InvalidValue, "inventory.active", "Unsupported active item."};
    }
    if (--activeItem_.quantity == 0) activeItem_ = {};
    return framework::Status::success();
}

int Game::maximumHealth() const noexcept {
    return attributeValue(kMaxHealthAttribute, 5);
}

int Game::attributeValue(std::string_view id, int fallback) const noexcept {
    if (!attributesConfigured_) return fallback;
    std::int32_t value{};
    const framework::Status status = attributes_.value(id, value);
    return status ? value : fallback;
}

framework::Status Game::updateDialogue(const Input& input) noexcept {
    using namespace framework;
    const int direction = std::clamp(input.moveX, -1, 1);
    if (direction != 0 && direction != dialogueDirection_ && dialogueChoiceCount_ > 1) {
        if (direction > 0) {
            dialogueSelection_ = (dialogueSelection_ + 1) % dialogueChoiceCount_;
        } else {
            dialogueSelection_ = (dialogueSelection_ + dialogueChoiceCount_ - 1) %
                                 dialogueChoiceCount_;
        }
    }
    dialogueDirection_ = direction;
    if (!input.interactPressed) {
        return Status::success();
    }
    Status status = syncDialogueState();
    if (!status) {
        return status;
    }
    status = refreshDialogue();
    if (!status) {
        return status;
    }
    if (dialogueChoiceCount_ == 0) {
        status = dialogueSession_.advance(*dialogueState_);
    } else {
        ActionRegistry actions{};
        status = actions.addCapability("echo.inventory.write");
        if (!status) {
            return status;
        }
        status = actions.registerAction(
            kGrantItemAction, "echo.inventory.write", &Game::grantDialogueItem, this);
        if (!status) {
            return status;
        }
        actions.setGameActive(true);
        status = dialogueSession_.select(
            dialogueChoices_[dialogueSelection_].id, *dialogueState_, &actions);
    }
    if (!status) {
        return status;
    }
    dialogueSelection_ = 0;
    status = syncDialogueState();
    return status ? refreshDialogue() : status;
}

framework::Status Game::interact(int moveDirection) noexcept {
    const Rect playerBounds{player_.position.x, player_.position.y, kPlayerWidth, kPlayerHeight};
    if (technicianNearby()) {
        if (dialogueModule_ == nullptr || dialogueState_ == nullptr) {
            return {framework::ErrorCode::InvalidState, kEngineerDialogue,
                    "The engineer dialogue has not been configured."};
        }
        framework::Status status = syncDialogueState();
        if (!status) {
            return status;
        }
        status = dialogueModule_->start(kEngineerDialogue, dialogueSession_);
        if (!status) {
            return status;
        }
        dialogueDirection_ = std::clamp(moveDirection, -1, 1);
        status = refreshDialogue();
        if (!status) {
            dialogueSession_.reset();
        }
        return status;
    }
    if (room_ == RoomId::MaintenanceTunnel &&
        overlaps(playerBounds, {970.0F, 272.0F, 44.0F, 48.0F})) {
        if (inventory_.contains(ItemId::MaintenanceKey)) {
            enterRoom(RoomId::ResearchLab, true);
        } else {
            accessBlocked_ = true;
        }
    } else if (room_ == RoomId::ResearchLab && player_.position.x < 120.0F) {
        enterRoom(RoomId::MaintenanceTunnel, false);
    }
    return framework::Status::success();
}

bool Game::hasAbility(std::string_view id) const noexcept {
    return abilityModule_ != nullptr ? abilities_.contains(id)
                                     : id == kEchoStepAbilityId && abilityCollected_;
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
        } else if (room_ == RoomId::PowerStation && hasAbility(kEchoStepAbilityId)) {
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
    if (invulnerabilityTimer_ > 0.0F || shieldSeconds_ > 0.0F) {
        return;
    }

    --player_.health;
    invulnerabilityTimer_ = 1.0F;
    if (player_.health <= 0) {
        ++deaths_;
        player_.health = maximumHealth();
        enterRoom(RoomId::LandingBay, true);
    }
}

} // namespace echo
