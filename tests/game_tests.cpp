#include "game/game.hpp"
#include "platform/presentation/bitmap_text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void testBitmapTextPresentation() {
    using echo::rendering::Rect2D;
    std::vector<Rect2D> plain;
    std::vector<Rect2D> accented;
    echo::platform::drawBitmapText(10, 20, "C", 2,
                                   [&](Rect2D pixel) { plain.push_back(pixel); });
    echo::platform::drawBitmapText(10, 20, "\xC4\x8C", 2,
                                   [&](Rect2D pixel) { accented.push_back(pixel); });
    bool sameGlyph = accented.size() == plain.size() + 3;
    for (std::size_t i = 0; sameGlyph && i < plain.size(); ++i) {
        const Rect2D& expected = plain[i];
        const Rect2D& actual = accented[i + 3];
        sameGlyph = actual.x == expected.x && actual.y == expected.y + 4 &&
                    actual.w == expected.w && actual.h == expected.h;
    }
    check(sameGlyph && accented[0].x == 12 && accented[0].y == 20 &&
              accented[1].x == 16 && accented[1].y == 20 &&
              accented[2].x == 14 && accented[2].y == 22,
          "SDL-free bitmap text preserves Czech caron and glyph placement");

    std::vector<Rect2D> afterInvalid;
    echo::platform::drawBitmapText(10, 20, "\xFF" "C", 2,
                                   [&](Rect2D pixel) { afterInvalid.push_back(pixel); });
    check(afterInvalid.size() == plain.size() &&
              !afterInvalid.empty() && afterInvalid.front().x == plain.front().x &&
              afterInvalid.front().y == plain.front().y,
          "SDL-free bitmap text skips an invalid UTF-8 byte without shifting text");
}

class DialogueFixture {
public:
    DialogueFixture() {
        std::ifstream input(ECHO_SOURCE_ASSETS "framework/engineer.json", std::ios::binary);
        if (!input) {
            status_ = {echo::framework::ErrorCode::NotFound, "engineer.json",
                       "Dialogue test asset could not be opened."};
            return;
        }
        const std::string source{std::istreambuf_iterator<char>(input),
                                 std::istreambuf_iterator<char>()};
        document_ = std::make_unique<echo::framework::DialogueDocument>();
        echo::framework::JsonError error{};
        status_ = document_->parse(source.data(), source.size(), error);
        if (!status_) return;
        status_ = module_.add(*document_->definition());
        if (!status_) return;
        std::ifstream configInput(ECHO_SOURCE_ASSETS "framework/game.json",
                                  std::ios::binary);
        const std::string configSource{std::istreambuf_iterator<char>(configInput),
                                       std::istreambuf_iterator<char>()};
        echo::framework::JsonDocument configDocument{};
        echo::framework::JsonError configError{};
        status_ = echo::framework::JsonDocument::parse(
            configSource.data(), configSource.size(), configDocument, configError);
        if (!status_) return;
        status_ = attributesModule_.load(configDocument.root().find("attributes"));
        if (!status_) return;
        status_ = equipmentModule_.load(configDocument.root().find("equipment"));
        if (!status_) return;
        std::ifstream itemsInput(ECHO_SOURCE_ASSETS "framework/items.json",
                                 std::ios::binary);
        const std::string itemsSource{std::istreambuf_iterator<char>(itemsInput),
                                      std::istreambuf_iterator<char>()};
        echo::framework::JsonDocument itemsDocument{};
        status_ = echo::framework::JsonDocument::parse(
            itemsSource.data(), itemsSource.size(), itemsDocument, configError);
        if (!status_) return;
        status_ = inventoryModule_.load(itemsDocument.root());
        if (!status_) return;
        echo::framework::ModuleContext context{};
        if (!module_.initialize(context)) {
            status_ = {echo::framework::ErrorCode::InitializationFailed, "dialogue",
                       "Dialogue test module could not initialize."};
            return;
        }
        if (!inventoryModule_.initialize(context) ||
            !attributesModule_.initialize(context) ||
            !equipmentModule_.initialize(context)) {
            status_ = {echo::framework::ErrorCode::InitializationFailed, "equipment",
                       "Equipment test modules could not initialize."};
            return;
        }
        status_ = lootTable_.load(
            configDocument.root().find("loot"), inventoryModule_.catalog());
    }

    ~DialogueFixture() {
        inventoryModule_.shutdown();
        equipmentModule_.shutdown();
        attributesModule_.shutdown();
        module_.shutdown();
    }

    [[nodiscard]] echo::framework::Status bind(
        echo::Game& game, float reach = echo::kDefaultInteractionReach,
        bool withEquipment = false) noexcept {
        if (!status_) return status_;
        echo::framework::Status status =
            game.configureDialogue(module_, state_, reach, &inventoryModule_);
        if (!status) return status;
        status = game.configureLoot(lootTable_);
        if (!status || !withEquipment) return status;
        status = game.configureAttributes(attributesModule_);
        return status ? game.configureEquipment(equipmentModule_) : status;
    }

    [[nodiscard]] echo::framework::Status bindInventory(echo::Game& game) noexcept {
        if (!status_) return status_;
        echo::framework::Status status = game.configureInventory(inventoryModule_);
        return status ? game.configureLoot(lootTable_) : status;
    }

    [[nodiscard]] echo::framework::Status loadLoot(
        echo::framework::JsonValue document, echo::EnemyLootTable& table) const noexcept {
        return status_ ? table.load(document, inventoryModule_.catalog()) : status_;
    }

    [[nodiscard]] echo::framework::Status setEchoStepUnlocked(bool unlocked) noexcept {
        return state_.set(
            {echo::framework::StateScope::Global, {}, "player.echo_step_unlocked"},
            echo::framework::StateValue::fromBoolean(unlocked));
    }

private:
    std::unique_ptr<echo::framework::DialogueDocument> document_{};
    echo::framework::DialogueModule module_{};
    echo::framework::AttributesModule attributesModule_{};
    echo::framework::EquipmentModule equipmentModule_{};
    echo::framework::InventoryModule inventoryModule_{};
    echo::EnemyLootTable lootTable_{};
    echo::framework::GameState state_{};
    echo::framework::Status status_{};
};

void step(echo::Game& game, echo::Input input, int ticks = 1) {
    for (int index = 0; index < ticks; ++index) {
        const auto status = game.update(input);
        check(static_cast<bool>(status), status ? "game update succeeded" : status.detail);
        if (!status) {
            return;
        }
        input.jumpPressed = false;
        input.interactPressed = false;
    }
}

bool moveUntilRoom(echo::Game& game, echo::RoomId target, int direction, int maxTicks = 1800) {
    for (int index = 0; index < maxTicks && game.roomId() != target; ++index) {
        step(game, {.moveX = direction, .jumpPressed = true, .jumpHeld = true, .shootHeld = true});
    }
    return game.roomId() == target;
}

bool moveUntilX(echo::Game& game, float target, int direction, int maxTicks = 900) {
    for (int index = 0; index < maxTicks; ++index) {
        const bool reached = direction > 0 ? game.player().position.x >= target : game.player().position.x <= target;
        if (reached) {
            return true;
        }
        step(game, {.moveX = direction, .jumpPressed = true, .jumpHeld = true, .shootHeld = true});
    }
    return false;
}

void talkToTechnician(echo::Game& game) {
    step(game, {.interactPressed = true});
    check(game.dialogueActive(), "technician opens a dialogue when approached");
    check(!game.inventory().contains(echo::ItemId::MaintenanceKey),
          "key is not granted before the offer is advanced");
    step(game, {.interactPressed = true});
    step(game, {.interactPressed = true});
    check(game.inventory().contains(echo::ItemId::MaintenanceKey),
          "technician dialogue grants the maintenance key");
    step(game, {.interactPressed = true});
    check(!game.dialogueActive(), "technician dialogue can be dismissed");
}

void testMovementJumpAndFloorCollision() {
    echo::Game game;
    const float initialX = game.player().position.x;
    step(game, {.moveX = 1}, 30);
    check(game.player().position.x > initialX + 30.0F, "horizontal movement accelerates the player");

    step(game, {.jumpPressed = true, .jumpHeld = true});
    const float jumpStartY = game.player().position.y;
    step(game, {.jumpHeld = true}, 12);
    check(game.player().position.y < jumpStartY, "jump applies upward velocity");

    step(game, {}, 120);
    check(game.player().onGround, "player lands on a solid floor");
    check(game.player().position.y == 292.0F, "floor collision resolves to the tile surface");
}

void testAttributeModifiersAffectGameplay() {
    echo::framework::AttributesModule module;
    constexpr char attributesJson[] = R"({
        "schemaVersion": 1,
        "definitions": [
            {"id": "echo:attribute/player.max_health", "baseValue": 5, "minimum": 1, "maximum": 8},
            {"id": "echo:attribute/player.move_speed", "baseValue": 190, "minimum": 100, "maximum": 250},
            {"id": "echo:attribute/player.horizontal_acceleration", "baseValue": 1500, "minimum": 100, "maximum": 5000},
            {"id": "echo:attribute/player.projectile_speed", "baseValue": 650, "minimum": 100, "maximum": 1500},
            {"id": "echo:attribute/player.fire_interval_ms", "baseValue": 500, "minimum": 1, "maximum": 2000},
            {"id": "echo:attribute/player.invulnerability_ms", "baseValue": 1000, "minimum": 0, "maximum": 5000}
        ]
    })";
    echo::framework::JsonDocument document{};
    echo::framework::JsonError jsonError{};
    check(static_cast<bool>(echo::framework::JsonDocument::parse(
              attributesJson, sizeof(attributesJson) - 1, document, jsonError)) &&
              static_cast<bool>(module.load(document.root())),
          "gameplay attribute fixture is loaded from JSON");
    echo::framework::ModuleContext context{};
    check(module.initialize(context), "attributes module initializes for gameplay");
    echo::Game game;
    check(static_cast<bool>(game.configureAttributes(module)),
          "game configures its bounded player attributes");
    check(static_cast<bool>(game.addAttributeModifier(
              "echo:attribute/player.max_health", "echo:upgrade/vitality", 2)) &&
              game.maximumHealth() == 7,
          "a bounded max-health modifier changes the gameplay health limit");
    check(game.addAttributeModifier(
              "echo:attribute/player.max_health", "echo:upgrade/overload", 2).code ==
              echo::framework::ErrorCode::InvalidValue &&
              game.maximumHealth() == 7,
          "out-of-range gameplay modifiers fail without changing player attributes");
    check(static_cast<bool>(game.addAttributeModifier(
              "echo:attribute/player.max_health", "echo:upgrade/overload", -4)) &&
              game.maximumHealth() == 3 && game.player().health == 3 &&
              static_cast<bool>(game.removeAttributeModifier(
                  "echo:attribute/player.max_health", "echo:upgrade/overload")) &&
              game.maximumHealth() == 7 && game.player().health == 3 &&
              static_cast<bool>(game.removeAttributeModifier(
                  "echo:attribute/player.max_health", "echo:upgrade/vitality")) &&
              game.maximumHealth() == 5,
          "bounded max-health reductions clamp health without healing on removal");

    echo::Game defaultAcceleration;
    echo::Game fasterAcceleration;
    check(static_cast<bool>(defaultAcceleration.configureAttributes(module)) &&
              static_cast<bool>(fasterAcceleration.configureAttributes(module)) &&
              static_cast<bool>(fasterAcceleration.addAttributeModifier(
                  "echo:attribute/player.horizontal_acceleration",
                  "echo:upgrade/acceleration", 500)),
          "horizontal acceleration is loaded and modifier-bounded through JSON attributes");
    step(defaultAcceleration, {.moveX = 1});
    step(fasterAcceleration, {.moveX = 1});
    check(fasterAcceleration.player().velocity.x > defaultAcceleration.player().velocity.x,
          "configured horizontal acceleration changes deterministic movement");

    echo::Game tunedWeapon;
    check(static_cast<bool>(tunedWeapon.configureAttributes(module)) &&
              static_cast<bool>(tunedWeapon.update({.shootHeld = true})),
          "projectile settings load from JSON attributes");
    const auto firstShot = std::find_if(
        tunedWeapon.projectiles().begin(), tunedWeapon.projectiles().end(),
        [](const echo::Projectile& projectile) { return projectile.active; });
    step(tunedWeapon, {.shootHeld = true}, 20);
    std::size_t activeShots = 0;
    for (const echo::Projectile& projectile : tunedWeapon.projectiles()) {
        activeShots += projectile.active ? 1U : 0U;
    }
    check(firstShot != tunedWeapon.projectiles().end() &&
              firstShot->velocityX == 650.0F && activeShots == 1,
          "configured projectile speed and fire interval control shooting");

    echo::Game baseline;
    const float baselineX = baseline.player().position.x;
    const float upgradedX = game.player().position.x;
    check(static_cast<bool>(game.addAttributeModifier(
              "echo:attribute/player.move_speed", "echo:upgrade/mobility", 60)),
          "a bounded movement modifier can be applied");
    step(baseline, {.moveX = 1}, 30);
    step(game, {.moveX = 1}, 30);
    check(game.player().position.x - upgradedX >
              baseline.player().position.x - baselineX,
          "the movement attribute changes deterministic player movement");
    module.shutdown();
}

void testCheckpointRoundTrip() {
    DialogueFixture fixture;
    echo::Game source;
    check(static_cast<bool>(fixture.bindInventory(source)),
          "checkpoint source binds the item catalog");
    step(source, {.moveX = 1}, 24);
    echo::Game restored;
    check(static_cast<bool>(fixture.bindInventory(restored)),
          "checkpoint restore game binds the item catalog");
    const echo::GameCheckpoint checkpoint = source.checkpoint();
    check(restored.restore(checkpoint), "valid gameplay checkpoint restores");
    check(restored.roomId() == source.roomId() &&
              restored.player().position.x == source.player().position.x &&
              restored.player().health == source.player().health &&
              restored.deaths() == source.deaths(),
          "checkpoint round-trip preserves room and player progress");

    echo::GameCheckpoint progressed = checkpoint;
    progressed.room = echo::RoomId::CentralChamber;
    progressed.hasDoubleJump = true;
    progressed.abilityCollected = true;
    progressed.completed = true;
    progressed.enemyHealth = {0, 1};
    check(progressed.inventory.add(echo::ItemId::MaintenanceKey),
          "inventory accepts a valid item");
    check(restored.restore(progressed) &&
              restored.roomId() == echo::RoomId::CentralChamber &&
              restored.abilityCollected() && restored.completed() &&
              restored.inventory().contains(echo::ItemId::MaintenanceKey) &&
              restored.livingEnemies(echo::RoomId::PowerStation) == 1,
          "checkpoint restores inventory, ability, completion and defeated-enemy progress");

    echo::GameCheckpoint invalid = checkpoint;
    invalid.playerPosition.x = -1.0F;
    const float previousX = restored.player().position.x;
    check(!restored.restore(invalid), "checkpoint rejects an out-of-room position");
    check(restored.player().position.x == previousX,
          "invalid checkpoint does not modify the active game");

    echo::GameCheckpoint invalidInventory = checkpoint;
    invalidInventory.inventory.count = echo::Inventory::kCapacity + 1;
    check(!restored.restore(invalidInventory), "checkpoint rejects an oversized inventory");
    invalidInventory = checkpoint;
    invalidInventory.inventory.count = 1;
    invalidInventory.inventory.items[0] = static_cast<echo::ItemId>(99);
    check(!restored.restore(invalidInventory), "checkpoint rejects an unknown inventory item");

    echo::GameCheckpoint missingAbility = progressed;
    missingAbility.hasDoubleJump = false;
    missingAbility.abilityCollected = false;
    missingAbility.completed = false;
    check(!restored.restore(missingAbility),
          "checkpoint rejects access to the central chamber without its ability");
}

void testEnemyLootDrops() {
    DialogueFixture fixture;
    echo::Game game;
    check(static_cast<bool>(fixture.bindInventory(game)),
          "enemy loot game binds the item catalog");
    echo::GameCheckpoint checkpoint = game.checkpoint();
    checkpoint.room = echo::RoomId::MaintenanceTunnel;
    checkpoint.playerPosition = {650.0F, 292.0F};
    checkpoint.enemyHealth[2] = 0;
    check(checkpoint.inventory.add(echo::ItemId::MaintenanceKey) &&
              game.restore(checkpoint),
          "loot test enters the tunnel with valid room access");
    for (int tick = 0; tick < 120 &&
         game.livingEnemies(echo::RoomId::MaintenanceTunnel) != 0; ++tick) {
        check(static_cast<bool>(game.update({.shootHeld = true})),
              "combat update succeeds while defeating a loot-bearing enemy");
    }
    check(game.livingEnemies(echo::RoomId::MaintenanceTunnel) == 0 &&
              game.lootDrops()[0].active &&
              game.lootDrops()[0].stack == echo::ItemStack{echo::ItemId::Scrap, 2},
          "defeating the tunnel enemy spawns its deterministic scrap drop");

    checkpoint = game.checkpoint();
    checkpoint.inventory.clear();
    check(checkpoint.inventory.add(echo::ItemId::MaintenanceKey) &&
              checkpoint.inventory.add(echo::ItemId::HealthCell, 3, 57),
          "loot test fills all backpack slots while preserving a key");
    checkpoint.playerPosition = {game.lootDrops()[0].position.x, 292.0F};
    check(game.restore(checkpoint) &&
              static_cast<bool>(game.update({})) &&
              game.lootDrops()[0].active &&
              game.itemQuantity(echo::ItemId::Scrap) == 0,
          "a full backpack leaves the world drop available instead of losing loot");

    checkpoint = game.checkpoint();
    check(checkpoint.inventory.remove(echo::ItemId::HealthCell, 3) &&
              game.restore(checkpoint) &&
              static_cast<bool>(game.update({})) &&
              !game.lootDrops()[0].active &&
              game.itemQuantity(echo::ItemId::Scrap) == 2,
          "touching the drop collects scrap into a newly freed stable backpack slot");
    const echo::GameCheckpoint collected = game.checkpoint();
    echo::Game restored;
    check(static_cast<bool>(fixture.bindInventory(restored)),
          "collected loot restore game binds the item catalog");
    check(restored.restore(collected) &&
              !restored.lootDrops()[0].active &&
              restored.itemQuantity(echo::ItemId::Scrap) == 2,
          "checkpoint restore preserves collected loot without respawning it");

    auto invalidDrop = collected;
    invalidDrop.lootDrops[0].active = true;
    invalidDrop.lootDrops[0].stack = {echo::ItemId::HealthCell, 2};
    check(!game.restore(invalidDrop),
          "checkpoint validation rejects an unsupported enemy drop item");
    auto powerStationDrop = collected;
    powerStationDrop.room = echo::RoomId::PowerStation;
    powerStationDrop.enemyHealth = {2, 0};
    powerStationDrop.lootDrops[0] = {};
    powerStationDrop.lootDrops[1] = {
        echo::RoomId::PowerStation, {820.0F, 304.0F},
        {echo::ItemId::Scrap, 3}, true};
    check(game.restore(powerStationDrop) && game.lootDrops()[1].active &&
              game.lootDrops()[1].stack.quantity == 3,
          "the Power Station enemy checkpoint carries its three-scrap drop");
}

void testInvalidEnemyLootTable() {
    DialogueFixture fixture;
    constexpr char source[] = R"({"schemaVersion":2,"drops":[]})";
    echo::framework::JsonDocument document{};
    echo::framework::JsonError error{};
    echo::EnemyLootTable table{};
    check(echo::framework::JsonDocument::parse(
              source, sizeof(source) - 1, document, error) &&
              !fixture.loadLoot(document.root(), table) && !table.loaded(),
          "enemy loot rejects unsupported schema versions without partial output");
}

void testRoomTransition() {
    DialogueFixture fixture;
    echo::Game game;
    check(static_cast<bool>(fixture.bindInventory(game)),
          "room transition game binds the item catalog");
    check(moveUntilRoom(game, echo::RoomId::MaintenanceTunnel, 1), "landing bay connects to maintenance tunnel");
    check(game.roomName() == "Maintenance Tunnel", "room transition loads the destination name");
}

void testDataDrivenWorldLayout() {
    DialogueFixture fixture;
    static echo::WorldLayout wide = echo::defaultWorldLayout();
    echo::RoomLayout& landing = wide.rooms[static_cast<std::size_t>(echo::RoomId::LandingBay)];
    landing.columns = 48;
    for (int column = 40; column < 48; ++column) {
        landing.tiles[static_cast<std::size_t>(10 * echo::kMaxLayoutColumns + column)] = echo::Tile::Solid;
    }
    echo::Game game;
    check(static_cast<bool>(fixture.bindInventory(game)),
          "data-driven layout game binds the item catalog");
    check(static_cast<bool>(game.configureWorld(wide)) && game.roomColumns() == 48,
          "a data-driven layout can widen a room beyond the built-in 40 columns");
    check(moveUntilX(game, 1300.0F, 1) && game.roomId() == echo::RoomId::LandingBay,
          "the player keeps walking past the old room edge in the wider room");
    check(moveUntilRoom(game, echo::RoomId::MaintenanceTunnel, 1),
          "the wider room still exits through its configured right edge");

    static echo::WorldLayout invalid = echo::defaultWorldLayout();
    invalid.rooms[0].columns = echo::kMaxLayoutColumns + 1;
    echo::Game rejected;
    check(!rejected.configureWorld(invalid) && rejected.roomColumns() == echo::kRoomColumns,
          "an out-of-bounds layout is rejected and the previous world is kept");
    static echo::WorldLayout invalidHeight = echo::defaultWorldLayout();
    invalidHeight.rooms[0].rows = echo::kRoomRows + 1;
    check(!rejected.configureWorld(invalidHeight),
          "side-view rooms cannot exceed their fixed simulation height");
    invalidHeight = echo::defaultWorldLayout();
    invalidHeight.rooms[static_cast<std::size_t>(echo::RoomId::CargoHold)].rows =
        echo::kMaxLayoutRows + 1;
    check(!rejected.configureWorld(invalidHeight),
          "isometric rooms cannot exceed their fixed-capacity height");
    static echo::WorldLayout invalidPlayer = echo::defaultWorldLayout();
    invalidPlayer.playerSize.x = std::numeric_limits<float>::quiet_NaN();
    check(!rejected.configureWorld(invalidPlayer), "non-finite player bounds are rejected");
    invalidPlayer.playerSize.x = 33.0F;
    check(!rejected.configureWorld(invalidPlayer), "player bounds larger than one tile are rejected");
    invalidPlayer.playerSize.x = 20.0F;
    invalidPlayer.playerFacing = 0;
    check(!rejected.configureWorld(invalidPlayer), "invalid player facing is rejected");
    static echo::WorldLayout weakEnemy = echo::defaultWorldLayout();
    weakEnemy.enemies[0].health = 0;
    check(!rejected.configureWorld(weakEnemy), "enemy spawns outside the save contract are rejected");
    static echo::WorldLayout invalidBehavior = echo::defaultWorldLayout();
    invalidBehavior.enemies[2].chargeSpeed = std::numeric_limits<float>::infinity();
    check(!rejected.configureWorld(invalidBehavior), "non-finite charger speed is rejected");
    invalidBehavior = echo::defaultWorldLayout();
    invalidBehavior.roamers[0].detectionRange = 4097.0F;
    check(!rejected.configureWorld(invalidBehavior), "out-of-range stalker sight is rejected");
    invalidBehavior = echo::defaultWorldLayout();
    invalidBehavior.enemies[4].projectileSpeed = std::numeric_limits<float>::quiet_NaN();
    check(!rejected.configureWorld(invalidBehavior), "non-finite sentry projectile speed is rejected");
    invalidBehavior.enemies[4].projectileSpeed = 601.0F;
    check(!rejected.configureWorld(invalidBehavior), "out-of-range sentry projectile speed is rejected");
    invalidBehavior.enemies[4].projectileSpeed = 220.0F;
    invalidBehavior.enemies[4].fireInterval = 0.0F;
    check(!rejected.configureWorld(invalidBehavior), "sentry intervals shorter than one step are rejected");
    invalidBehavior.enemies[4].fireInterval = 60.1F;
    check(!rejected.configureWorld(invalidBehavior), "out-of-range sentry fire intervals are rejected");
    invalidBehavior.enemies[4].detectionRange = 4096.0F;
    invalidBehavior.enemies[4].projectileSpeed = 600.0F;
    invalidBehavior.enemies[4].fireInterval = echo::kFixedStep;
    check(static_cast<bool>(rejected.configureWorld(invalidBehavior)),
          "sentry tuning accepts its fixed-step and upper bounds");
    invalidBehavior.enemies[4].detectionRange = 0.0F;
    invalidBehavior.enemies[4].projectileSpeed = 0.0F;
    invalidBehavior.enemies[4].fireInterval = 60.0F;
    check(static_cast<bool>(rejected.configureWorld(invalidBehavior)),
          "sentry tuning accepts zero range and maximum interval");
    invalidBehavior = echo::defaultWorldLayout();
    invalidBehavior.enemies[3].guardCycle = std::numeric_limits<float>::quiet_NaN();
    check(!rejected.configureWorld(invalidBehavior), "non-finite armor cycle is rejected");
    invalidBehavior.enemies[3].guardCycle = 0.049F;
    check(!rejected.configureWorld(invalidBehavior), "armor cycle cannot be shorter than a gameplay update");
    invalidBehavior.enemies[3].guardCycle = 3.0F;
    invalidBehavior.enemies[3].guardStart = 3.0F;
    check(!rejected.configureWorld(invalidBehavior), "armor guard start must precede cycle end");
    invalidBehavior.enemies[3].guardStart = 0.0F;
    invalidBehavior.enemies[3].guardReduction = -1;
    check(!rejected.configureWorld(invalidBehavior), "negative armor reduction is rejected");
    invalidBehavior.enemies[3].guardReduction = 100;
    check(!rejected.configureWorld(invalidBehavior), "excessive armor reduction is rejected");
    invalidBehavior.enemies[3].guardCycle = 0.05F;
    invalidBehavior.enemies[3].guardReduction = 99;
    check(static_cast<bool>(rejected.configureWorld(invalidBehavior)),
          "armor tuning accepts its minimum cycle and maximum reduction");

    static echo::WorldLayout movedEngineer = echo::defaultWorldLayout();
    movedEngineer.engineer.x = 400.0F;
    echo::Game moved;
    check(static_cast<bool>(moved.configureWorld(movedEngineer)) &&
              moved.technicianBounds().x == 400.0F && moved.technicianBounds().y == 280.0F &&
              !moved.technicianNearby(),
          "the engineer object's spawn position moves her in the game");
    const echo::GameCheckpoint movedCheckpoint = moved.checkpoint();
    moved.start();
    check(moved.technicianBounds().x == 400.0F && moved.restore(movedCheckpoint) &&
              moved.technicianBounds().x == 400.0F,
          "new games and checkpoints keep the engineer's configured spawn");
    static echo::WorldLayout outsideEngineer = echo::defaultWorldLayout();
    outsideEngineer.engineer.x = 1270.0F;
    check(!rejected.configureWorld(outsideEngineer),
          "an engineer spawn outside the Landing Bay is rejected");
    static echo::WorldLayout buriedEngineer = echo::defaultWorldLayout();
    buriedEngineer.engineer.y = 300.0F;
    check(!rejected.configureWorld(buriedEngineer),
          "an engineer spawn inside solid tiles is rejected");
}

void testTechnicianDialogueAndInventory() {
    echo::Game unconfigured;
    check(unconfigured.update({.interactPressed = true}).code ==
              echo::framework::ErrorCode::InvalidState &&
              !unconfigured.dialogueActive(),
          "dialogue without its required service reports an error");

    DialogueFixture fixture;
    echo::Game reachTest;
    check(static_cast<bool>(fixture.bind(reachTest, 46.0F)) &&
              !reachTest.technicianNearby() &&
              static_cast<bool>(reachTest.update({.interactPressed = true})) &&
              !reachTest.dialogueActive(),
          "dialogue remains unavailable at the exact non-overlapping reach boundary");
    check(static_cast<bool>(fixture.bind(reachTest, 47.0F)) &&
              reachTest.technicianNearby() &&
              static_cast<bool>(reachTest.update({.interactPressed = true})) &&
              reachTest.dialogueActive(),
          "an increased reach enables dialogue at the same player position");
    echo::Game invalidReach;
    check(fixture.bind(invalidReach, -1.0F).code == echo::framework::ErrorCode::InvalidValue &&
              fixture.bind(invalidReach, echo::kMaxInteractionReach + 1.0F).code ==
                  echo::framework::ErrorCode::InvalidValue &&
              fixture.bind(invalidReach, std::numeric_limits<float>::quiet_NaN()).code ==
                  echo::framework::ErrorCode::InvalidValue,
          "game rejects negative, excessive and non-finite interaction reaches");
    echo::Game game;
    check(static_cast<bool>(fixture.bind(game)), "game binds the loaded engineer dialogue");
    check(game.technicianNearby(), "technician is reachable from the Landing Bay start");
    step(game, {.interactPressed = true});
    check(game.dialogueActive() && game.dialogueSpeaker() == "ENGINEER MIRA" &&
              game.dialogueText() == "LAB ACCESS IS LOCKED.",
          "engineer displays the first line from the JSON asset");
    const std::string_view englishGreeting = game.dialogueText();
    check(static_cast<bool>(game.setDialogueLanguage(
              echo::framework::DialogueLanguage::Czech)) &&
              game.dialogueText() != englishGreeting &&
              static_cast<bool>(game.setDialogueLanguage(
                  echo::framework::DialogueLanguage::English)) &&
              game.dialogueText() == englishGreeting,
          "active dialogue refreshes in the selected language and back");
    step(game, {.interactPressed = true});
    check(game.dialogueChoiceCount() == 2 && game.selectedDialogueChoice() == 0 &&
              game.dialogueChoiceText(0) == "TAKE MAINTENANCE KEY",
          "the offer exposes conditional JSON choices");
    const float position = game.player().position.x;
    step(game, {.moveX = 1}, 3);
    check(game.selectedDialogueChoice() == 1 && game.player().position.x == position,
          "a held direction changes the choice once without moving the player");
    step(game, {.moveX = 1, .interactPressed = true});
    check(game.dialogueText() == "THE KEY OPENS THE LAB DOOR." &&
              !game.inventory().contains(echo::ItemId::MaintenanceKey),
          "the information choice follows its branch without granting the key");
    step(game, {.interactPressed = true});
    check(game.dialogueChoiceCount() == 2 && game.selectedDialogueChoice() == 0,
          "the information branch loops back to the offer");
    step(game, {.interactPressed = true});
    check(game.inventory().contains(echo::ItemId::MaintenanceKey) &&
              game.dialogueText() == "TAKE THIS MAINTENANCE KEY.",
          "a registered action grants the key and enters the target line");
    step(game, {.interactPressed = true});
    check(!game.dialogueActive(), "the conversation can be dismissed");
    check(game.inventory().count == 1, "key pickup occupies one inventory slot");
    check(game.checkpoint().inventory.contains(echo::ItemId::MaintenanceKey),
          "checkpoint captures the inventory");

    const echo::GameCheckpoint checkpoint = game.checkpoint();
    check(game.restore(checkpoint), "a configured game restores its checkpoint");
    step(game, {.interactPressed = true});
    step(game, {.interactPressed = true});
    check(game.dialogueChoiceCount() == 0,
          "restored inventory hides choices that require not having the key");
    step(game, {.interactPressed = true});
    check(game.dialogueText() == "THE KEY OPENS THE LAB DOOR." &&
              game.dialogueChoiceCount() == 0,
          "hidden choices follow the JSON fallback for a returning player");
    step(game, {.interactPressed = true});
    check(game.dialogueText() == "GOOD LUCK OUT THERE.",
          "the owned-key conversation reaches its farewell without Echo Step");
    step(game, {.interactPressed = true});
    check(!game.dialogueActive() && game.inventory().count == 1,
          "the repeat conversation preserves the restored inventory");

    check(static_cast<bool>(fixture.setEchoStepUnlocked(true)),
          "test fixture mirrors the Echo Step unlock");
    step(game, {.interactPressed = true});
    step(game, {.interactPressed = true});
    step(game, {.interactPressed = true});
    check(game.dialogueChoiceCount() == 1 &&
              game.dialogueChoiceText(0) == "ASK ABOUT ECHO STEP",
          "Mira's optional advice appears only after Echo Step is unlocked");
    step(game, {.interactPressed = true});
    check(game.dialogueText() == "DOUBLE JUMP OPENS THE CENTRAL GATE.",
          "the unlocked ability choice opens its JSON dialogue branch");
    step(game, {.interactPressed = true});
    step(game, {.interactPressed = true});
    check(!game.dialogueActive(),
          "the ability advice returns through the dialogue graph to its farewell");
}

void testLabDoorRequiresKey() {
    DialogueFixture fixture;
    echo::Game game;
    check(static_cast<bool>(fixture.bindInventory(game)),
          "locked-door game binds the item catalog");
    check(moveUntilRoom(game, echo::RoomId::MaintenanceTunnel, 1),
          "locked-door test reaches the maintenance tunnel");
    check(moveUntilX(game, 955.0F, 1), "locked-door test reaches the lab door");
    step(game, {}, 60);
    step(game, {.interactPressed = true});
    check(game.roomId() == echo::RoomId::MaintenanceTunnel && game.accessBlocked(),
          "research lab door remains locked without the maintenance key");
}

void testHealthCellDialogueAndUse() {
    DialogueFixture fixture;
    echo::Game game;
    check(static_cast<bool>(fixture.bind(game)),
          "health-cell test binds the engineer dialogue");
    echo::GameCheckpoint damaged = game.checkpoint();
    damaged.playerHealth = 3;
    check(damaged.inventory.add(echo::ItemId::MaintenanceKey) &&
              game.restore(damaged),
          "damaged player checkpoint restores with the lab key");
    step(game, {.interactPressed = true});
    step(game, {.interactPressed = true});
    step(game, {.interactPressed = true});
    check(game.dialogueChoiceCount() == 1 &&
              game.dialogueChoiceText(0) == "TAKE HEALTH CELL",
          "Mira offers a health cell only when the player is hurt and has none");
    step(game, {.interactPressed = true});
    check(game.inventory().quantity(echo::ItemId::HealthCell) == 1 &&
              game.dialogueText() == "PUT THE CELL IN YOUR ACTIVE SLOT.",
          "the JSON dialogue action grants the catalogued health cell");
    step(game, {.interactPressed = true});
    check(!game.dialogueActive(), "health-cell grant dialogue can be dismissed");
    check(game.update({.useItemPressed = true}) && game.player().health == 3 &&
              game.inventory().quantity(echo::ItemId::HealthCell) == 1,
          "a backpack cell is not used until assigned to the active slot");
    check(static_cast<bool>(game.moveInventorySlot(1, echo::Game::kActiveItemSlot)),
          "a health-cell stack moves from the backpack to the active slot");
    check(static_cast<bool>(game.update({.useItemPressed = true})) &&
              game.player().health == 5 &&
              game.itemQuantity(echo::ItemId::HealthCell) == 0,
          "using a health cell restores up to two health and consumes one item");
    check(static_cast<bool>(game.update({.useItemPressed = true})) &&
              game.player().health == 5,
          "using the item action without a remaining cell is a safe no-op");

    echo::Game fullHealth;
    check(static_cast<bool>(fixture.bind(fullHealth)),
          "full-health item fixture binds the item catalog");
    echo::GameCheckpoint fullCheckpoint = fullHealth.checkpoint();
    check(fullCheckpoint.inventory.add(echo::ItemId::HealthCell, 3, 1) &&
              fullHealth.restore(fullCheckpoint) &&
              fullHealth.moveInventorySlot(0, echo::Game::kActiveItemSlot),
          "full-health fixture accepts a valid health cell");
    check(static_cast<bool>(fullHealth.update({.useItemPressed = true})) &&
              fullHealth.player().health == 5 &&
              fullHealth.itemQuantity(echo::ItemId::HealthCell) == 1,
          "a health cell is not consumed when health is already full");
}

void testKineticCoilDialogueAndCheckpoint() {
    DialogueFixture fixture;
    echo::Game game;
    check(static_cast<bool>(fixture.bind(game, echo::kDefaultInteractionReach, true)),
          "kinetic-coil test configures dialogue and equipment modules");
    talkToTechnician(game);
    step(game, {.interactPressed = true});
    step(game, {.interactPressed = true});
    step(game, {.interactPressed = true});
    check(game.dialogueChoiceCount() == 1 &&
              game.dialogueChoiceText(0) == "TAKE KINETIC COIL",
          "Mira offers the suit equipment once the player has the key");
    step(game, {.interactPressed = true});
    check(game.hasItem(echo::ItemId::KineticCoil) &&
              !game.inventory().contains(echo::ItemId::KineticCoil) &&
              game.equipmentEquipped("echo:equipment/kinetic_coil"),
          "accepting Mira's offer grants and equips the kinetic coil");
    check(game.removeAttributeModifier(
              "echo:attribute/player.move_speed", "echo:equipment/kinetic_coil").code ==
              echo::framework::ErrorCode::InvalidState,
          "equipment-owned modifiers cannot be removed through the generic attribute API");
    step(game, {.interactPressed = true});

    echo::Game baseline;
    const float baselineStartX = baseline.player().position.x;
    const float equippedStartX = game.player().position.x;
    step(baseline, {.moveX = 1}, 30);
    step(game, {.moveX = 1}, 30);
    check(game.player().position.x - equippedStartX >
              baseline.player().position.x - baselineStartX,
          "the equipped coil increases movement speed in the game");

    const echo::GameCheckpoint checkpoint = game.checkpoint();
    echo::Game restored;
    check(static_cast<bool>(fixture.bind(restored, echo::kDefaultInteractionReach, true)) &&
              restored.restore(checkpoint) &&
              restored.hasItem(echo::ItemId::KineticCoil) &&
              restored.equipmentEquipped("echo:equipment/kinetic_coil"),
          "checkpoint restore re-equips the coil and retains its player attribute");
}

void testSlotInventoryTransfersAndMigration() {
    DialogueFixture fixture;
    echo::Game game;
    check(static_cast<bool>(fixture.bind(game, echo::kDefaultInteractionReach, true)),
          "slot inventory fixture configures equipment");
    auto checkpoint = game.checkpoint();
    checkpoint.playerHealth = 3;
    checkpoint.inventory.items[19] = echo::ItemId::MaintenanceKey;
    checkpoint.inventory.quantities[19] = 1;
    checkpoint.inventory.count = 1;
    const std::size_t body = echo::Game::kFirstEquipmentSlot +
        static_cast<std::size_t>(echo::EquipmentSlot::Body);
    checkpoint.equipment[static_cast<std::size_t>(echo::EquipmentSlot::Body)] =
        {echo::ItemId::KineticCoil, 1};
    checkpoint.activeItem = {echo::ItemId::HealthCell, 2};
    check(game.restore(checkpoint) && game.inventory().items[19] == echo::ItemId::MaintenanceKey,
          "checkpoint preserves a sparse backpack and separate gear and active stacks");
    step(game, {.moveX = 1}, 30);
    check(game.moveInventorySlot(body, 4) &&
              !game.equipmentEquipped("echo:equipment/kinetic_coil") &&
              game.player().velocity.x <= 190.0F,
          "unequipping moves gear into a bag slot and removes its speed modifier safely");
    check(game.moveInventorySlot(4, echo::Game::kFirstEquipmentSlot).code ==
              echo::framework::ErrorCode::InvalidValue &&
              game.inventory().items[4] == echo::ItemId::KineticCoil,
          "incompatible equipment transfers fail without changing either slot");
    check(game.moveInventorySlot(4, body) &&
              game.equipmentEquipped("echo:equipment/kinetic_coil") &&
              game.inventory().items[4] == echo::ItemId::None,
          "moving gear back to the body slot reapplies the modifier once");
    check(game.moveInventorySlot(echo::Game::kActiveItemSlot, 19).code ==
              echo::framework::ErrorCode::InvalidValue &&
              game.inventorySlot(echo::Game::kActiveItemSlot).quantity == 2 &&
              game.inventory().items[19] == echo::ItemId::MaintenanceKey,
          "swaps validate the displaced item as well as the selected one");
    check(game.moveInventorySlot(echo::Game::kActiveItemSlot, 5) &&
              game.update({.useItemPressed = true}) && game.player().health == 3 &&
              game.moveInventorySlot(5, echo::Game::kActiveItemSlot) &&
              game.update({.useItemPressed = true}) && game.player().health == 5 &&
              game.inventorySlot(echo::Game::kActiveItemSlot).quantity == 1,
          "only the active stack is consumed and its remainder stays in place");
    auto duplicated = game.checkpoint();
    duplicated.inventory.items[0] = echo::ItemId::KineticCoil;
    duplicated.inventory.quantities[0] = 1;
    ++duplicated.inventory.count;
    check(!game.restore(duplicated) && game.equipmentEquipped("echo:equipment/kinetic_coil"),
          "checkpoint validation rejects duplicated unique gear atomically");
    auto stacks = game.checkpoint();
    stacks.inventory.items[0] = echo::ItemId::HealthCell;
    stacks.inventory.quantities[0] = 2;
    ++stacks.inventory.count;
    check(game.restore(stacks) &&
              game.moveInventorySlot(0, echo::Game::kActiveItemSlot) &&
              game.inventorySlot(echo::Game::kActiveItemSlot).quantity == 3 &&
              game.inventory().items[0] == echo::ItemId::None,
          "complete health-cell stacks merge up to the exact three-cell limit");
    stacks = game.checkpoint();
    stacks.inventory.items[0] = echo::ItemId::HealthCell;
    stacks.inventory.quantities[0] = 1;
    ++stacks.inventory.count;
    check(game.restore(stacks) &&
              game.moveInventorySlot(0, echo::Game::kActiveItemSlot).code ==
                  echo::framework::ErrorCode::CapacityExceeded &&
              game.inventory().quantities[0] == 1 &&
              game.inventorySlot(echo::Game::kActiveItemSlot).quantity == 3 &&
              game.moveInventorySlot(echo::Game::kInventorySlotCount, 0).code ==
                  echo::framework::ErrorCode::InvalidArgument,
          "stack overflow and out-of-range slots leave both compartments unchanged");
    auto fullBackpack = game.checkpoint();
    fullBackpack.inventory.clear();
    check(fullBackpack.inventory.add(echo::ItemId::MaintenanceKey) &&
              fullBackpack.inventory.add(echo::ItemId::HealthCell, 3, 57) &&
              !fullBackpack.inventory.add(echo::ItemId::HealthCell, 3, 1) &&
              game.restore(fullBackpack) && game.inventory().count == 20 &&
              game.inventory().items[19] == echo::ItemId::HealthCell &&
              game.inventorySlot(echo::Game::kActiveItemSlot).quantity == 3 &&
              game.inventorySlot(body).item == echo::ItemId::KineticCoil,
          "all twenty backpack slots are usable independently of gear and active storage");
    auto legacy = game.checkpoint();
    legacy.inventory = {};
    legacy.equipment.fill({});
    legacy.activeItem = {};
    legacy.inventoryLayoutVersion = 1;
    check(legacy.inventory.add(echo::ItemId::MaintenanceKey) &&
              legacy.inventory.add(echo::ItemId::KineticCoil) &&
              legacy.inventory.add(echo::ItemId::HealthCell, 3, 2) &&
              game.restore(legacy) && game.inventory().count == 1 &&
              game.inventorySlot(body).item == echo::ItemId::KineticCoil &&
              game.inventorySlot(echo::Game::kActiveItemSlot).quantity == 2,
          "legacy checkpoints retain equipped gear and usable health-cell stacks");
}

void testProjectileCombatAndDamage() {
    DialogueFixture fixture;
    echo::Game game;
    check(static_cast<bool>(fixture.bindInventory(game)),
          "projectile combat game binds the item catalog");
    check(moveUntilRoom(game, echo::RoomId::MaintenanceTunnel, 1), "combat test reaches maintenance tunnel");
    step(game, {.moveX = 1}, 140);
    step(game, {.shootHeld = true}, 120);
    check(game.livingEnemies(echo::RoomId::MaintenanceTunnel) == 0, "projectiles defeat a destructible enemy");

    echo::Game damageGame;
    check(static_cast<bool>(fixture.bindInventory(damageGame)),
          "damage game binds the item catalog");
    check(moveUntilRoom(damageGame, echo::RoomId::MaintenanceTunnel, 1),
          "damage test reaches the maintenance tunnel");
    auto damageState = damageGame.checkpoint();
    damageState.playerPosition = {760.0F, 292.0F};
    damageState.enemyHealth[2] = 0;
    check(damageGame.restore(damageState), "contact-damage fixture restores beside an enemy");
    const int initialHealth = damageGame.player().health;
    step(damageGame, {});
    check(damageGame.player().health < initialHealth, "enemy contact damages the player");
    const echo::Vec2 firstShake = damageGame.cameraShakeOffset();
    check(std::abs(firstShake.x) >= 6.0F && std::abs(firstShake.y) >= 3.0F,
          "accepted contact damage starts a visible camera shake");
    const int damagedHealth = damageGame.player().health;

    echo::GameCheckpoint damagedCheckpoint = damageGame.checkpoint();
    echo::Game restoredDamage = damageGame;
    check(static_cast<bool>(fixture.bindInventory(restoredDamage)),
          "damage checkpoint game binds the item catalog");
    check(restoredDamage.restore(damagedCheckpoint) &&
              restoredDamage.cameraShakeOffset().x == 0.0F &&
              restoredDamage.cameraShakeOffset().y == 0.0F,
          "restoring a checkpoint clears transient camera shake");

    echo::Game noInvulnerabilityGame;
    check(static_cast<bool>(fixture.bind(
              noInvulnerabilityGame, echo::kDefaultInteractionReach, true)) &&
              moveUntilRoom(noInvulnerabilityGame,
                            echo::RoomId::MaintenanceTunnel, 1),
          "configurable invulnerability fixture binds and reaches an enemy");
    auto noInvulnerabilityCheckpoint = noInvulnerabilityGame.checkpoint();
    noInvulnerabilityCheckpoint.playerPosition = {760.0F, 292.0F};
    noInvulnerabilityCheckpoint.enemyHealth[2] = 0;
    check(noInvulnerabilityGame.restore(noInvulnerabilityCheckpoint) &&
              static_cast<bool>(noInvulnerabilityGame.addAttributeModifier(
                  "echo:attribute/player.invulnerability_ms",
                  "echo:upgrade/no_invulnerability", -1000)),
          "invulnerability duration can be configured to zero");
    const int healthBeforeRapidHits = noInvulnerabilityGame.player().health;
    step(noInvulnerabilityGame, {}, 2);
    check(noInvulnerabilityGame.player().health <= healthBeforeRapidHits - 2,
          "configured invulnerability duration controls repeat contact damage");

    step(damageGame, {}, 1);
    const echo::Vec2 advancedShake = damageGame.cameraShakeOffset();
    check(damageGame.player().health == damagedHealth &&
              (advancedShake.x != firstShake.x || advancedShake.y != firstShake.y),
          "invulnerability prevents repeat damage while shake advances by a fixed step");
    step(damageGame, {}, echo::kCameraShakeDurationSteps - 1);
    check(damageGame.cameraShakeOffset().x == 0.0F &&
              damageGame.cameraShakeOffset().y == 0.0F,
          "camera shake ends after its fixed-step duration");

    echo::Game retriggerGame;
    check(static_cast<bool>(fixture.bindInventory(retriggerGame)),
          "damage retrigger game binds the item catalog");
    check(moveUntilRoom(retriggerGame, echo::RoomId::MaintenanceTunnel, 1),
          "retrigger test reaches the maintenance tunnel");
    int observedHealth = retriggerGame.player().health;
    int acceptedHits = 0;
    for (int tick = 0; tick < 250; ++tick) {
        step(retriggerGame, {.moveX = 1});
        if (retriggerGame.player().health < observedHealth) {
            ++acceptedHits;
            const echo::Vec2 retriggeredShake = retriggerGame.cameraShakeOffset();
            check(retriggeredShake.x == firstShake.x && retriggeredShake.y == firstShake.y,
                  "each later accepted hit restarts the camera shake");
            observedHealth = retriggerGame.player().health;
        }
    }
    check(acceptedHits > 1, "the retrigger fixture receives multiple accepted hits");
}

void testHazardDeathAndRespawn() {
    echo::Game game;
    step(game, {.moveX = 1}, 200);
    step(game, {.jumpPressed = true, .jumpHeld = true}, 14);
    step(game, {.moveX = 1, .jumpHeld = true}, 30);
    step(game, {}, 360);
    check(game.deaths() > 0, "fatal damage increments the death counter");
    check(game.roomId() == echo::RoomId::LandingBay, "death respawns the player at the landing bay");
    check(game.player().health == 5, "respawn restores player health");
}

void testIsometricCargoHold() {
    DialogueFixture fixture;
    echo::Game game;
    check(static_cast<bool>(fixture.bindInventory(game)),
          "isometric room game binds the item catalog");
    check(moveUntilRoom(game, echo::RoomId::CargoHold, -1),
          "Landing Bay west exit leads into the isometric Cargo Hold");
    check(game.isometric() && !echo::Game{}.isometric(),
          "only the Cargo Hold uses the isometric projection");
    check(game.player().position.x > 700.0F && game.player().position.y > 150.0F &&
              game.player().position.y < 210.0F,
          "isometric entry places the player in the east doorway");
    check(game.roamers().size() == 2, "stalkers spawn on Cargo Hold entry");

    echo::Game baseline;
    echo::WorldLayout calm = baseline.world();
    calm.roamerCount = 0;
    echo::Game walker;
    check(static_cast<bool>(walker.configureWorld(calm)) &&
              static_cast<bool>(fixture.bindInventory(walker)),
          "stalker-free isometric layout and item catalog load");
    echo::GameCheckpoint entry = game.checkpoint();
    check(walker.restore(entry) && walker.roomId() == echo::RoomId::CargoHold,
          "checkpoints restore into the isometric room");

    const echo::Vec2 start = walker.player().position;
    step(walker, {.jumpPressed = true, .jumpHeld = true}, 60);
    check(walker.player().position.x == start.x && walker.player().position.y == start.y,
          "isometric rooms have no gravity or jump");

    step(walker, {.moveY = -1}, 20);
    check(walker.player().position.x < start.x && walker.player().position.y < start.y,
          "screen-up walks diagonally into the grid (north-west)");
    const echo::Vec2 afterUp = walker.player().position;
    step(walker, {.moveX = -1, .moveY = 1}, 20);
    check(walker.player().position.x == afterUp.x &&
              walker.player().position.y > afterUp.y,
          "screen down-left walks along one grid axis (8-way input)");

    echo::GameCheckpoint corridor = entry;
    corridor.playerPosition = {600.0F, 170.0F};
    check(walker.restore(corridor), "wall fixture restores");
    step(walker, {.moveX = -1, .moveY = -1}, 600);
    check(walker.roomId() == echo::RoomId::CargoHold && walker.player().position.x == 32.0F &&
              walker.player().position.y == 170.0F,
          "solid wall blocks isometric movement");

    check(walker.restore(entry), "doorway fixture restores");
    check(moveUntilRoom(walker, echo::RoomId::LandingBay, 1, 120) && !walker.isometric(),
          "Cargo Hold east doorway returns to the side-view Landing Bay");

    echo::GameCheckpoint pool = entry;
    pool.playerPosition = {320.0F, 270.0F};
    check(walker.restore(pool), "hazard pool fixture restores");
    const int health = walker.player().health;
    step(walker, {}, 5);
    check(walker.roomId() == echo::RoomId::CargoHold &&
              walker.player().health < health,
          "isometric hazard pools are walkable but damage the player");

    echo::GameCheckpoint hunt = entry;
    hunt.playerPosition = {470.0F, 118.0F};
    echo::Game hunter;
    check(hunter.restore(hunt) && hunter.roamers().size() == 2,
          "stalker fixture restores with both stalkers");
    const auto distance = [](const echo::Game& current) {
        if (current.roamers().empty()) return 0.0F;
        const echo::Enemy& stalker = current.roamers()[0];
        return std::abs(stalker.bounds.x - current.player().position.x) +
               std::abs(stalker.bounds.y - current.player().position.y);
    };
    const float initialDistance = distance(hunter);
    step(hunter, {}, 30);
    check(distance(hunter) < initialDistance, "stalkers chase the player in the isometric room");
    static echo::WorldLayout shortSight = echo::defaultWorldLayout();
    shortSight.roamers[0].detectionRange = 10.0F;
    echo::Game shortSightHunter;
    check(shortSightHunter.configureWorld(shortSight) && shortSightHunter.restore(hunt),
          "custom stalker sight fixture restores");
    const float shortSightDistance = distance(shortSightHunter);
    step(shortSightHunter, {}, 30);
    check(distance(shortSightHunter) == shortSightDistance,
          "stalkers outside their configured sight range do not chase");
    step(hunter, {.moveX = -1, .moveY = -1});
    step(hunter, {.shootHeld = true}, 150);
    check(hunter.roomId() == echo::RoomId::CargoHold && !hunter.roamers().empty() &&
              !hunter.roamers()[0].alive,
          "isometric shots defeat a stalker");

    echo::Game shooter;
    check(static_cast<bool>(shooter.configureWorld(calm)) && shooter.restore(entry),
          "diagonal shot fixture restores");
    step(shooter, {.moveY = -1});
    step(shooter, {.shootHeld = true});
    const auto shot = std::find_if(shooter.projectiles().begin(), shooter.projectiles().end(),
                                   [](const echo::Projectile& projectile) { return projectile.active; });
    check(shot != shooter.projectiles().end() && shot->velocityX < 0.0F && shot->velocityY < 0.0F,
          "isometric shots travel along the diagonal aim");

    echo::Game twinStick;
    check(static_cast<bool>(twinStick.configureWorld(calm)) && twinStick.restore(entry),
          "twin-stick fixture restores");
    const float strafeStartX = twinStick.player().position.x;
    step(twinStick, {.moveX = -1, .aimX = 64.0F}, 10);
    step(twinStick, {.moveX = -1, .shootHeld = true, .aimX = 64.0F, .aimY = 0.0F});
    const auto aimedShot = std::find_if(twinStick.projectiles().begin(), twinStick.projectiles().end(),
                                        [](const echo::Projectile& projectile) { return projectile.active; });
    check(twinStick.player().position.x < strafeStartX && aimedShot != twinStick.projectiles().end() &&
              aimedShot->velocityX > 0.0F && aimedShot->velocityY < 0.0F &&
              std::abs(aimedShot->velocityX + aimedShot->velocityY) < 1.0F,
          "screen-east aim fires east while the player walks west");
    step(twinStick, {.moveY = 1}, 2);
    check(twinStick.player().aim.x > 0.0F && twinStick.player().aim.y > 0.0F,
          "without an aim device the aim follows movement again");
    step(twinStick, {.aimX = std::numeric_limits<float>::quiet_NaN(), .aimY = 1.0F});
    check(std::isfinite(twinStick.player().aim.x) && std::isfinite(twinStick.player().aim.y),
          "non-finite aim input is ignored");
}

void testNewEnemyBehaviorsAndItems() {
    using echo::ItemId;
    DialogueFixture fixture;
    echo::Game game;
    check(static_cast<bool>(fixture.bind(game)),
          "combat item fixture binds the item catalog");
    auto checkpoint = game.checkpoint();
    checkpoint.room = echo::RoomId::MaintenanceTunnel;
    checkpoint.playerPosition = {340.0F, 292.0F};
    checkpoint.enemyHealth[0] = 0;
    check(game.restore(checkpoint), "charger fixture restores");
    const float chargerX = game.enemies()[2].bounds.x;
    step(game, {}, 10);
    check(game.enemies()[2].bounds.x < chargerX,
          "charger pursues a nearby player rather than continuing its patrol");
    static echo::WorldLayout slowCharge = echo::defaultWorldLayout();
    slowCharge.enemies[2].chargeSpeed = 30.0F;
    echo::Game slowCharger;
    check(slowCharger.configureWorld(slowCharge) && fixture.bind(slowCharger) &&
              slowCharger.restore(checkpoint),
          "custom charger speed fixture restores");
    const float slowStart = slowCharger.enemies()[2].bounds.x;
    step(slowCharger, {}, 10);
    const float slowDistance = slowStart - slowCharger.enemies()[2].bounds.x;
    check(slowDistance > 4.9F && slowDistance < 5.1F,
          "charger uses the configured charge speed");
    static echo::WorldLayout shortChargeRange = echo::defaultWorldLayout();
    shortChargeRange.enemies[2].detectionRange = 10.0F;
    echo::Game shortRangeCharger;
    check(shortRangeCharger.configureWorld(shortChargeRange) && fixture.bind(shortRangeCharger) &&
              shortRangeCharger.restore(checkpoint),
          "custom charger range fixture restores");
    const float patrolStart = shortRangeCharger.enemies()[2].bounds.x;
    step(shortRangeCharger, {}, 10);
    check(shortRangeCharger.enemies()[2].bounds.x > patrolStart,
          "charger patrols when the player is outside its configured range");

    checkpoint = game.checkpoint();
    checkpoint.room = echo::RoomId::PowerStation;
    checkpoint.playerPosition = {400.0F, 292.0F};
    checkpoint.enemyHealth[2] = 0;
    check(game.restore(checkpoint), "armored enemy fixture restores");
    const float armorX = game.enemies()[3].bounds.x;
    step(game, {}, 10);
    check(game.enemies()[3].health == 6 &&
              game.enemies()[3].bounds.x > armorX &&
              game.enemies()[3].bounds.x - armorX < 6.0F,
          "armored enemy has higher health and a slower patrol");
    checkpoint = game.checkpoint();
    checkpoint.playerPosition = {520.0F, 292.0F};
    checkpoint.activeItem = {ItemId::BlastCharge, 1};
    check(game.restore(checkpoint), "armored guard fixture restores");
    step(game, {}, 125);
    check(game.enemies()[3].attackCooldown >= 2.0F, "armored enemy enters guard phase");
    step(game, {.useItemPressed = true});
    check(game.enemies()[3].health == 5, "armored guard reduces incoming blast damage");
    static echo::WorldLayout tunedArmor = echo::defaultWorldLayout();
    tunedArmor.enemies[3].guardCycle = 0.5F;
    tunedArmor.enemies[3].guardStart = 0.1F;
    tunedArmor.enemies[3].guardReduction = 2;
    auto armored = std::make_unique<echo::Game>();
    check(armored->configureWorld(tunedArmor) && fixture.bind(*armored) &&
              armored->restore(checkpoint),
          "custom armor fixture restores");
    step(*armored, {.useItemPressed = true});
    check(armored->enemies()[3].health == 4,
          "the configured armor does not reduce damage before guarding");
    check(armored->restore(checkpoint), "custom armor fixture resets");
    step(*armored, {}, 7);
    check(armored->enemies()[3].guarding(), "armor enters its configured guard phase");
    const float guardingX = armored->enemies()[3].bounds.x;
    step(*armored, {}, 3);
    check(armored->enemies()[3].bounds.x == guardingX,
          "armor stops moving while guarding");
    step(*armored, {.useItemPressed = true});
    check(armored->enemies()[3].health == 6,
          "the configured armor reduction applies while guarding");
    step(*armored, {}, 21);
    check(!armored->enemies()[3].guarding() && armored->enemies()[3].bounds.x > guardingX,
          "armor resumes patrol after the configured cycle");

    checkpoint = game.checkpoint();
    checkpoint.room = echo::RoomId::ResearchLab;
    checkpoint.playerPosition = {970.0F, 292.0F};
    checkpoint.enemyHealth[3] = 0;
    check(checkpoint.inventory.add(ItemId::MaintenanceKey) && game.restore(checkpoint),
          "sentry fixture restores in the laboratory");
    auto distantCheckpoint = checkpoint;
    distantCheckpoint.playerPosition.x = 800.0F;
    static echo::WorldLayout shortSentryRange = echo::defaultWorldLayout();
    shortSentryRange.enemies[4].detectionRange = 100.0F;
    echo::Game shortRangeSentry;
    check(shortRangeSentry.configureWorld(shortSentryRange) && fixture.bind(shortRangeSentry) &&
              shortRangeSentry.restore(distantCheckpoint),
          "custom sentry detection fixture restores");
    step(shortRangeSentry, {}, 1);
    check(std::none_of(shortRangeSentry.enemyProjectiles().begin(),
                       shortRangeSentry.enemyProjectiles().end(),
                       [](const echo::Projectile& shot) { return shot.active; }),
          "sentry does not fire outside its configured range");
    static echo::WorldLayout tunedSentry = echo::defaultWorldLayout();
    tunedSentry.enemies[4].projectileSpeed = 60.0F;
    tunedSentry.enemies[4].fireInterval = 0.25F;
    echo::Game customizedSentry;
    check(customizedSentry.configureWorld(tunedSentry) && fixture.bind(customizedSentry) &&
              customizedSentry.restore(distantCheckpoint),
          "custom sentry shot fixture restores");
    step(customizedSentry, {}, 1);
    check(customizedSentry.enemyProjectiles()[0].active &&
              customizedSentry.enemyProjectiles()[0].velocityX == -60.0F,
          "sentry fires at the configured projectile speed");
    step(customizedSentry, {}, 14);
    check(!customizedSentry.enemyProjectiles()[1].active,
          "sentry waits for the configured interval");
    step(customizedSentry, {}, 3);
    check(customizedSentry.enemyProjectiles()[1].active,
          "sentry fires again after the configured interval");
    step(game, {}, 1);
    bool fired = false;
    for (const auto& shot : game.enemyProjectiles()) fired |= shot.active;
    check(fired && game.enemies()[4].type == echo::EnemyType::Sentry,
          "stationary sentry fires a hostile projectile");
    const int healthBeforeShot = game.player().health;
    step(game, {}, 5);
    check(game.player().health < healthBeforeShot, "sentry projectiles damage the player");

    echo::Game equipped;
    check(static_cast<bool>(fixture.bind(equipped, echo::kDefaultInteractionReach, true)),
          "equipment fixture binds modules");
    auto gear = equipped.checkpoint();
    gear.equipment[0] = {ItemId::JumpVisor, 1};
    gear.equipment[1] = {ItemId::VitalVest, 1};
    gear.equipment[2] = {ItemId::PowerBarrel, 1};
    gear.equipment[3] = {ItemId::SprintModule, 1};
    check(equipped.restore(gear) && equipped.maximumHealth() == 6,
          "vital vest increases maximum health");
    step(equipped, {.moveX = 1}, 15);
    check(equipped.player().velocity.x > 190.0F,
          "sprint module increases movement speed");
    step(equipped, {.jumpPressed = true, .jumpHeld = true});
    check(equipped.player().velocity.y < -420.0F,
          "jump visor increases jump impulse");
    gear = equipped.checkpoint();
    gear.room = echo::RoomId::MaintenanceTunnel;
    gear.playerPosition = {700.0F, 292.0F};
    gear.playerVelocity = {};
    gear.playerOnGround = true;
    gear.enemyHealth[2] = 0;
    check(equipped.restore(gear), "weapon fixture restores");
    step(equipped, {.shootHeld = true}, 10);
    check(!equipped.enemies()[0].alive,
          "power barrel defeats the two-health patrol with one shot");

    for (const auto item : {ItemId::MedPatch, ItemId::MedKit, ItemId::ShieldCell,
                            ItemId::SpeedCharge, ItemId::JumpCharge, ItemId::BlastCharge}) {
        echo::Game consumer;
        check(static_cast<bool>(fixture.bind(consumer)),
              "consumable fixture binds the item catalog");
        auto state = consumer.checkpoint();
        state.playerHealth = 1;
        state.activeItem = {item, 1};
        check(consumer.restore(state), "consumable fixture restores");
        step(consumer, {.useItemPressed = true});
        const auto after = consumer.checkpoint();
        check(after.activeItem.item == ItemId::None, "using a consumable removes its stack");
        if (item == ItemId::MedPatch) check(consumer.player().health == 2, "med patch heals one");
        if (item == ItemId::MedKit) check(consumer.player().health == 5, "med kit heals four");
        if (item == ItemId::ShieldCell) check(after.shieldSeconds > 3.9F, "shield grants protection");
        if (item == ItemId::SpeedCharge) {
            check(after.speedBoostAmount == 25,
                  "speed boost magnitude comes from its item definition");
            step(consumer, {.moveX = 1}, 15);
            check(consumer.player().velocity.x > 190.0F, "speed charge increases speed");
        }
        if (item == ItemId::JumpCharge) {
            check(after.jumpBoostAmount == 60,
                  "jump boost magnitude comes from its item definition");
            step(consumer, {.jumpPressed = true, .jumpHeld = true});
            check(consumer.player().velocity.y < -420.0F, "jump charge increases jump");
        }
    }

    echo::Game blast;
    check(static_cast<bool>(fixture.bind(blast)),
          "blast fixture binds the item catalog");
    auto blastState = blast.checkpoint();
    blastState.room = echo::RoomId::MaintenanceTunnel;
    blastState.playerPosition = {700.0F, 292.0F};
    blastState.enemyHealth[2] = 0;
    blastState.activeItem = {ItemId::BlastCharge, 1};
    check(blast.restore(blastState), "blast fixture restores");
    step(blast, {.useItemPressed = true});
    check(!blast.enemies()[0].alive && blast.lootDrops()[0].active,
          "blast charge damages nearby enemies and produces normal loot");
    echo::Game shielded;
    check(static_cast<bool>(fixture.bind(shielded)),
          "shield fixture binds the item catalog");
    auto shieldState = shielded.checkpoint();
    shieldState.room = echo::RoomId::MaintenanceTunnel;
    shieldState.playerPosition = {760.0F, 292.0F};
    shieldState.enemyHealth[2] = 0;
    shieldState.activeItem = {ItemId::ShieldCell, 1};
    check(shielded.restore(shieldState), "shield fixture restores beside an enemy");
    step(shielded, {.useItemPressed = true});
    check(shielded.player().health == 5 &&
              shielded.cameraShakeOffset().x == 0.0F &&
              shielded.cameraShakeOffset().y == 0.0F,
          "shield prevents enemy damage and its camera shake");
    std::array<bool, static_cast<std::size_t>(ItemId::Count)> drops{};
    for (std::size_t index = 2; index < echo::kLootDropCount; ++index) {
        const auto id = static_cast<std::size_t>(game.expectedLoot(index).item);
        check(id >= static_cast<std::size_t>(ItemId::JumpVisor) &&
                  id < drops.size() && !drops[id], "each new loot item is unique");
        if (id < drops.size()) drops[id] = true;
    }

    for (std::size_t enemy = 2; enemy < echo::kEnemyCount; ++enemy) {
        echo::Game lootGame;
        check(static_cast<bool>(fixture.bind(lootGame)),
              "new enemy loot fixture binds the item catalog");
        auto state = lootGame.checkpoint();
        state.room = lootGame.expectedLootRoom(enemy == 2 ? 2 : enemy == 3 ? 5 : 8);
        state.playerPosition = {enemy == 2 ? 280.0F : enemy == 3 ? 420.0F : 930.0F, 292.0F};
        state.enemyHealth[0] = 0;
        state.enemyHealth[1] = 0;
        state.enemyHealth[enemy] = 1;
        state.activeItem = {ItemId::BlastCharge, 1};
        check(state.inventory.add(ItemId::MaintenanceKey) && lootGame.restore(state),
              "new enemy loot fixture restores");
        step(lootGame, {.useItemPressed = true});
        for (std::size_t index = 2; index < echo::kLootDropCount; ++index) {
            if (lootGame.lootEnemy(index) != enemy) continue;
            check(lootGame.lootDrops()[index].active &&
                      lootGame.lootDrops()[index].stack == lootGame.expectedLoot(index),
                  "new enemy defeat creates its corresponding drop");
        }
    }

    echo::Game fullBag;
    check(static_cast<bool>(fixture.bindInventory(fullBag)),
          "full-backpack loot game binds the item catalog");
    auto full = fullBag.checkpoint();
    full.room = echo::RoomId::MaintenanceTunnel;
    full.playerPosition = {400.0F, 292.0F};
    full.enemyHealth[2] = 0;
    full.lootDrops[2] = {full.room, {400.0F, 304.0F}, {ItemId::JumpVisor, 1}, true};
    check(full.inventory.add(ItemId::MaintenanceKey) &&
              full.inventory.add(ItemId::HealthCell, 3, 57) && fullBag.restore(full),
          "new loot full-backpack fixture restores");
    step(fullBag, {});
    check(fullBag.lootDrops()[2].active, "new loot remains when the backpack is full");
    full = fullBag.checkpoint();
    check(full.inventory.remove(ItemId::HealthCell, 3) && fullBag.restore(full),
          "new loot fixture frees a backpack slot");
    step(fullBag, {});
    check(!fullBag.lootDrops()[2].active && fullBag.hasItem(ItemId::JumpVisor),
          "new loot is collected after freeing space");
}

void testAbilityGateAndBacktracking() {
    DialogueFixture fixture;
    echo::framework::AbilityModule abilities{};
    echo::framework::ModuleContext moduleContext{};
    std::ifstream configInput(ECHO_SOURCE_ASSETS "framework/game.json", std::ios::binary);
    const std::string config{std::istreambuf_iterator<char>(configInput),
                             std::istreambuf_iterator<char>()};
    echo::framework::JsonDocument document{};
    echo::framework::JsonError error{};
    echo::framework::QuestModule quests{};
    echo::framework::CutsceneModule cutscenes{};
    check(echo::framework::JsonDocument::parse(config.data(), config.size(), document, error) &&
              abilities.load(document.root().find("abilities")) &&
              abilities.initialize(moduleContext) &&
              quests.load(document.root().find("quests")) && quests.initialize(moduleContext) &&
              cutscenes.load(document.root().find("cutscenes")) &&
              cutscenes.initialize(moduleContext),
          "project ability, quest and cutscene content loads and initializes");
    echo::Game game;
    check(static_cast<bool>(fixture.bind(game)) && game.configureAbilities(abilities) &&
              game.configureQuests(quests) && game.configureCutscenes(cutscenes),
          "progression game binds dialogue, abilities, quests and cutscenes");
    check(game.questProgress().completed == 0 &&
              game.questProgress().objective == "GET THE KEY FROM THE ENGINEER",
          "a new game displays the key objective");
    talkToTechnician(game);
    check(game.questProgress().completed == 1 &&
              game.questProgress().choiceRequired &&
              game.questProgress().choices.size() == 2,
          "receiving the key opens the bounded route choice");
    auto progression = game.checkpoint();
    progression.enemyHealth[2] = 0;
    progression.enemyHealth[3] = 0;
    progression.enemyHealth[4] = 0;
    check(game.restore(progression), "progression fixture isolates the original route");
    check(static_cast<bool>(game.update({.moveX = 1})) &&
              static_cast<bool>(game.update({.interactPressed = true})) &&
              game.questBranch() == 2 &&
              game.questProgress().completed == 2 &&
              game.questProgress().objective == "FIND ECHO STEP IN THE LAB",
          "the sentry route advances once its branch-specific fact is true");
    check(moveUntilRoom(game, echo::RoomId::MaintenanceTunnel, 1), "progression reaches maintenance");
    check(moveUntilRoom(game, echo::RoomId::PowerStation, 1), "power station is reachable before the upgrade");
    step(game, {.moveX = 1}, 500);
    check(game.roomId() == echo::RoomId::PowerStation, "central chamber passage remains locked without ability");
    check(game.gateBlocked(), "locked passage reports its blocked state");

    check(moveUntilRoom(game, echo::RoomId::MaintenanceTunnel, -1), "player backtracks out of the power station");
    check(moveUntilX(game, 970.0F, -1), "player can backtrack to the laboratory entrance");
    step(game, {}, 40);
    step(game, {.interactPressed = true});
    check(game.roomId() == echo::RoomId::ResearchLab, "laboratory door opens without the upgrade");
    step(game, {.moveX = 1}, 350);
    check(game.abilityCollected(), "laboratory collectible unlocks double jump");
    check(game.player().hasDoubleJump, "ability is available to the player");
    check(game.questProgress().completed == 3 &&
              game.questProgress().objective == "REACH THE CENTRAL CHAMBER EXIT",
          "unlocking Echo Step advances the quest");
    const echo::GameCheckpoint abilityCheckpoint = game.checkpoint();
    echo::Game restored;
    check(static_cast<bool>(fixture.bindInventory(restored)) &&
              restored.configureAbilities(abilities) && restored.configureQuests(quests) &&
              restored.restore(abilityCheckpoint) &&
              restored.abilityCollected() && restored.player().hasDoubleJump &&
              restored.questProgress().completed == 3 &&
              restored.questBranch() == 2,
          "checkpoint restore rebuilds ability ownership and quest progress");
    {
        std::string reordered = config;
        const std::string keyObjective =
            R"({"id": "key", "fact": "echo:fact/maintenance_key", "text": "GET THE KEY FROM THE ENGINEER"})";
        const std::string abilityObjective =
            R"({"id": "ability", "fact": "echo:fact/echo_step", "text": "FIND ECHO STEP IN THE LAB"})";
        const std::size_t keyPosition = reordered.find(keyObjective);
        const std::size_t abilityPosition = reordered.find(abilityObjective);
        const std::size_t branchPosition = reordered.find("\"after\": 1");
        check(keyPosition != std::string::npos && abilityPosition != std::string::npos &&
                  keyPosition < abilityPosition && branchPosition != std::string::npos,
              "reordered quest fixture finds its source fields");
        if (keyPosition != std::string::npos && abilityPosition != std::string::npos &&
            keyPosition < abilityPosition && branchPosition != std::string::npos) {
            reordered.replace(branchPosition, std::string("\"after\": 1").size(), "\"after\": 2");
            reordered.replace(abilityPosition, abilityObjective.size(), keyObjective);
            reordered.replace(keyPosition, keyObjective.size(), abilityObjective);
            echo::framework::JsonDocument reorderedDocument{};
            echo::framework::JsonError reorderedError{};
            auto reorderedQuests = std::make_unique<echo::framework::QuestModule>();
            auto reorderedGame = std::make_unique<echo::Game>();
            check(echo::framework::JsonDocument::parse(
                      reordered.data(), reordered.size(), reorderedDocument, reorderedError) &&
                      reorderedQuests->load(reorderedDocument.root().find("quests")) &&
                      reorderedQuests->initialize(moduleContext) &&
                      fixture.bindInventory(*reorderedGame) &&
                      reorderedGame->configureAbilities(abilities) &&
                      reorderedGame->configureQuests(*reorderedQuests) &&
                      reorderedGame->restore(progression),
                  "reordered objectives and a later branch point load");
            check(reorderedGame->questProgress().completed == 0 &&
                      !reorderedGame->questProgress().choiceRequired &&
                      reorderedGame->questProgress().objective == "FIND ECHO STEP IN THE LAB",
                  "objective order and choice timing follow the quest data");
            auto earlyBranch = progression;
            earlyBranch.questBranch = 2;
            check(reorderedGame->restore(earlyBranch) &&
                      reorderedGame->questBranch() == 2 &&
                      reorderedGame->questProgress().completed == 0 &&
                      !reorderedGame->questProgress().choiceRequired &&
                      reorderedGame->questProgress().objective == "FIND ECHO STEP IN THE LAB",
                  "an earlier saved route remains selected while the new objective is pending");
            auto unselected = abilityCheckpoint;
            unselected.questBranch = 0;
            check(reorderedGame->restore(unselected) &&
                      reorderedGame->questProgress().completed == 2 &&
                      reorderedGame->questProgress().choiceRequired &&
                      reorderedGame->update({.moveX = 1}) &&
                      reorderedGame->update({.interactPressed = true}) &&
                      reorderedGame->questBranch() == 2 &&
                      reorderedGame->questProgress().completed == 3,
                  "the later route choice opens after both configured objectives");
            check(reorderedGame->restore(abilityCheckpoint) &&
                      reorderedGame->questBranch() == 2 &&
                      reorderedGame->questProgress().completed == 3,
                  "previously saved route choices survive a changed quest order");
        }
    }

    step(game, {.jumpPressed = true, .jumpHeld = true});
    step(game, {.jumpHeld = true}, 10);
    const float heightBeforeDoubleJump = game.player().position.y;
    step(game, {.jumpPressed = true, .jumpHeld = true});
    const float doubleJumpVelocity = game.player().velocity.y;
    check(doubleJumpVelocity < 0.0F && game.player().position.y < heightBeforeDoubleJump,
          "unlocked ability provides a second upward jump");
    step(game, {.jumpHeld = true}, 8);
    const float velocityAfterSecondJump = game.player().velocity.y;
    step(game, {.jumpPressed = true, .jumpHeld = true});
    check(game.player().velocity.y > velocityAfterSecondJump,
          "double jump cannot be repeated before landing");

    step(game, {.moveX = -1}, 350);
    step(game, {.interactPressed = true});
    check(game.roomId() == echo::RoomId::MaintenanceTunnel, "laboratory exit requires backtracking");
    check(moveUntilRoom(game, echo::RoomId::PowerStation, 1), "player returns through maintenance");
    step(game, {.moveX = 1}, 500);
    check(game.roomId() == echo::RoomId::CentralChamber, "double jump unlocks the central passage");
    check(game.cutsceneActive() && game.cutsceneCardCount() == 3 &&
              game.cutsceneCardIndex() == 0 &&
              game.cutsceneSpeaker() == "UNKNOWN SIGNAL",
          "entering Central Chamber starts the localized signal cutscene");
    const float sceneStartX = game.player().position.x;
    step(game, {.moveX = 1, .jumpPressed = true, .jumpHeld = true, .shootHeld = true}, 8);
    check(game.player().position.x == sceneStartX && game.cutsceneCardIndex() == 0,
          "gameplay remains fixed while a cutscene card is displayed");
    check(static_cast<bool>(game.setDialogueLanguage(
              echo::framework::DialogueLanguage::Czech)) &&
              game.cutsceneSpeaker() == "NEZNAMY SIGNAL",
          "cutscene speaker follows the configured Czech language");
    check(static_cast<bool>(game.update({.interactPressed = true})) &&
              game.cutsceneCardIndex() == 1 &&
              game.cutsceneText() == "TO VYSILANI PRISLO Z HLOUBI STANICE",
          "the cutscene advances one card using the selected translation");
    check(static_cast<bool>(game.update({.interactPressed = true})) &&
              game.cutsceneCardIndex() == 2 &&
              static_cast<bool>(game.update({.interactPressed = true})) &&
              !game.cutsceneActive(),
          "the final interaction closes the cutscene and releases gameplay");
    check(static_cast<bool>(game.setDialogueLanguage(
              echo::framework::DialogueLanguage::English)),
          "cutscene test restores the default language");
    auto fullBag = game.checkpoint();
    fullBag.inventory.clear();
    check(fullBag.inventory.add(echo::ItemId::MaintenanceKey) &&
              fullBag.inventory.add(echo::ItemId::HealthCell, 3, 57) &&
              game.restore(fullBag),
          "quest reward fixture fills every backpack slot without losing the key");
    step(game, {.moveX = 1, .jumpPressed = true, .jumpHeld = true, .shootHeld = true}, 700);
    check(game.completed() && game.questProgress().completed == 4 &&
              game.questProgress().objective.empty() && !game.questRewardClaimed() &&
              game.itemQuantity(echo::ItemId::MedKit) == 0,
          "a completed route leaves its reward pending when the backpack is full");
    check(game.moveInventorySlot(1, echo::Game::kActiveItemSlot) &&
              static_cast<bool>(game.update({})) && game.questRewardClaimed() &&
              game.itemQuantity(echo::ItemId::MedKit) == 1 &&
              static_cast<bool>(game.update({})) &&
              game.itemQuantity(echo::ItemId::MedKit) == 1,
          "freeing one slot claims the reward once and retries are idempotent");
    check(restored.restore(game.checkpoint()) && restored.questProgress().completed == 4 &&
              restored.questRewardClaimed() && restored.itemQuantity(echo::ItemId::MedKit) == 1,
          "completed route and claimed reward persist through checkpoint restore");

    const auto loadRewardQuest = [&](std::string_view item, std::string_view quantity,
                                     echo::framework::QuestModule& output) {
        std::string modified = config;
        const std::string original = "\"rewardItem\": \"echo:item/med_kit\"";
        const std::size_t rewardPosition = modified.find(original);
        if (rewardPosition == std::string::npos) return false;
        modified.replace(rewardPosition, original.size(),
                         "\"rewardItem\": \"" + std::string(item) + "\"");
        const std::string originalQuantity = "\"rewardQuantity\": 1";
        const std::size_t quantityPosition = modified.find(originalQuantity, rewardPosition);
        if (quantityPosition == std::string::npos) return false;
        modified.replace(quantityPosition, originalQuantity.size(),
                         "\"rewardQuantity\": " + std::string(quantity));
        echo::framework::JsonDocument rewardDocument{};
        echo::framework::JsonError rewardError{};
        return echo::framework::JsonDocument::parse(
                   modified.data(), modified.size(), rewardDocument, rewardError) &&
               output.load(rewardDocument.root().find("quests")) &&
               output.initialize(moduleContext);
    };
    auto alternateQuests = std::make_unique<echo::framework::QuestModule>();
    auto alternateGame = std::make_unique<echo::Game>();
    auto rewardCheckpoint = game.checkpoint();
    rewardCheckpoint.inventory.clear();
    rewardCheckpoint.equipment = {};
    rewardCheckpoint.activeItem = {};
    rewardCheckpoint.questRewardClaimed = false;
    check(rewardCheckpoint.inventory.add(echo::ItemId::MaintenanceKey) &&
              loadRewardQuest("echo:item/scrap", "3", *alternateQuests) &&
              fixture.bindInventory(*alternateGame) &&
              alternateGame->configureAbilities(abilities) &&
              alternateGame->configureQuests(*alternateQuests) &&
              alternateGame->restore(rewardCheckpoint),
          "a completed checkpoint restores with a different data-driven reward");
    check(alternateGame->update({}) && alternateGame->questRewardClaimed() &&
              alternateGame->itemQuantity(echo::ItemId::Scrap) == 3 &&
              alternateGame->update({}) && alternateGame->itemQuantity(echo::ItemId::Scrap) == 3 &&
              alternateGame->restore(alternateGame->checkpoint()),
          "configured reward and quantity are granted once and survive checkpoint restore");

    auto uniqueQuests = std::make_unique<echo::framework::QuestModule>();
    auto uniqueGame = std::make_unique<echo::Game>();
    rewardCheckpoint.equipment[0] = {echo::ItemId::JumpVisor, 1};
    check(loadRewardQuest("echo:item/jump_visor", "1", *uniqueQuests) &&
              fixture.bind(*uniqueGame, echo::kDefaultInteractionReach, true) &&
              uniqueGame->configureAbilities(abilities) &&
              uniqueGame->configureQuests(*uniqueQuests) &&
              uniqueGame->restore(rewardCheckpoint) && uniqueGame->update({}) &&
              !uniqueGame->questRewardClaimed() &&
              uniqueGame->itemQuantity(echo::ItemId::JumpVisor) == 1 &&
              uniqueGame->restore(uniqueGame->checkpoint()),
          "an already equipped unique reward remains pending without producing an invalid save");
    rewardCheckpoint = uniqueGame->checkpoint();
    rewardCheckpoint.equipment[0] = {};
    check(uniqueGame->restore(rewardCheckpoint) && uniqueGame->update({}) &&
              uniqueGame->questRewardClaimed() &&
              uniqueGame->itemQuantity(echo::ItemId::JumpVisor) == 1,
          "the pending unique reward is granted after the existing item is removed");

    auto invalidQuests = std::make_unique<echo::framework::QuestModule>();
    auto invalidGame = std::make_unique<echo::Game>();
    check(loadRewardQuest("echo:item/med_kit", "4", *invalidQuests) &&
              fixture.bindInventory(*invalidGame) && !invalidGame->configureQuests(*invalidQuests),
          "quest rewards exceeding the catalog stack limit are rejected");
    restored.start();
    check(restored.questProgress().completed == 0, "starting over resets derived quest progress");

    echo::Game coilRoute;
    check(static_cast<bool>(fixture.bindInventory(coilRoute)) &&
              coilRoute.configureAbilities(abilities) && coilRoute.configureQuests(quests),
          "alternate route fixture configures gameplay modules");
    auto coilCheckpoint = coilRoute.checkpoint();
    check(coilCheckpoint.inventory.add(echo::ItemId::MaintenanceKey) &&
              coilCheckpoint.inventory.add(echo::ItemId::KineticCoil) &&
              coilRoute.restore(coilCheckpoint) &&
              coilRoute.questProgress().choiceRequired &&
              static_cast<bool>(coilRoute.update({.interactPressed = true})) &&
              coilRoute.questBranch() == 1 &&
              coilRoute.questProgress().completed == 2 &&
              coilRoute.questProgress().objective == "FIND ECHO STEP IN THE LAB",
          "the coil route is independently selectable and checks its own fact");
}

} // namespace

int main() {
    testBitmapTextPresentation();
    testMovementJumpAndFloorCollision();
    testAttributeModifiersAffectGameplay();
    testCheckpointRoundTrip();
    testEnemyLootDrops();
    testInvalidEnemyLootTable();
    testRoomTransition();
    testDataDrivenWorldLayout();
    testTechnicianDialogueAndInventory();
    testHealthCellDialogueAndUse();
    testKineticCoilDialogueAndCheckpoint();
    testSlotInventoryTransfersAndMigration();
    testLabDoorRequiresKey();
    testProjectileCombatAndDamage();
    testHazardDeathAndRespawn();
    testIsometricCargoHold();
    testNewEnemyBehaviorsAndItems();
    testAbilityGateAndBacktracking();

    if (failures != 0) {
        std::cerr << failures << " gameplay test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All gameplay tests passed\n";
    return EXIT_SUCCESS;
}
