#include "game/game.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <limits>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
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
        echo::framework::ModuleContext context{};
        if (!module_.initialize(context)) {
            status_ = {echo::framework::ErrorCode::InitializationFailed, "dialogue",
                       "Dialogue test module could not initialize."};
            return;
        }
        if (!attributesModule_.initialize(context) ||
            !equipmentModule_.initialize(context)) {
            status_ = {echo::framework::ErrorCode::InitializationFailed, "equipment",
                       "Equipment test modules could not initialize."};
        }
    }

    ~DialogueFixture() {
        equipmentModule_.shutdown();
        attributesModule_.shutdown();
        module_.shutdown();
    }

    [[nodiscard]] echo::framework::Status bind(
        echo::Game& game, float reach = echo::kDefaultInteractionReach,
        bool withEquipment = false) noexcept {
        if (!status_) return status_;
        echo::framework::Status status =
            game.configureDialogue(module_, state_, {108.0F, 280.0F}, reach);
        if (!status || !withEquipment) return status;
        status = game.configureAttributes(attributesModule_);
        return status ? game.configureEquipment(equipmentModule_) : status;
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
    echo::Game source;
    step(source, {.moveX = 1}, 24);
    echo::Game restored;
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
    echo::Game game;
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

void testRoomTransition() {
    echo::Game game;
    check(moveUntilRoom(game, echo::RoomId::MaintenanceTunnel, 1), "landing bay connects to maintenance tunnel");
    check(game.roomName() == "Maintenance Tunnel", "room transition loads the destination name");
}

void testDataDrivenWorldLayout() {
    static echo::WorldLayout wide = echo::defaultWorldLayout();
    echo::RoomLayout& landing = wide.rooms[static_cast<std::size_t>(echo::RoomId::LandingBay)];
    landing.columns = 48;
    for (int column = 40; column < 48; ++column) {
        landing.tiles[static_cast<std::size_t>(10 * echo::kMaxLayoutColumns + column)] = echo::Tile::Solid;
    }
    echo::Game game;
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
    static echo::WorldLayout weakEnemy = echo::defaultWorldLayout();
    weakEnemy.enemies[0].health = 0;
    check(!rejected.configureWorld(weakEnemy), "enemy spawns outside the save contract are rejected");
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
    echo::Game game;
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
    echo::Game game;
    check(moveUntilRoom(game, echo::RoomId::MaintenanceTunnel, 1), "combat test reaches maintenance tunnel");
    step(game, {.moveX = 1}, 140);
    step(game, {.shootHeld = true}, 120);
    check(game.livingEnemies(echo::RoomId::MaintenanceTunnel) == 0, "projectiles defeat a destructible enemy");

    echo::Game damageGame;
    moveUntilRoom(damageGame, echo::RoomId::MaintenanceTunnel, 1);
    const int initialHealth = damageGame.player().health;
    step(damageGame, {.moveX = 1}, 250);
    check(damageGame.player().health < initialHealth, "enemy contact damages the player");
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

void testNewEnemyBehaviorsAndItems() {
    using echo::ItemId;
    echo::Game game;
    auto checkpoint = game.checkpoint();
    checkpoint.room = echo::RoomId::MaintenanceTunnel;
    checkpoint.playerPosition = {340.0F, 292.0F};
    checkpoint.enemyHealth[0] = 0;
    check(game.restore(checkpoint), "charger fixture restores");
    const float chargerX = game.enemies()[2].bounds.x;
    step(game, {}, 10);
    check(game.enemies()[2].bounds.x < chargerX,
          "charger pursues a nearby player rather than continuing its patrol");

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

    checkpoint = game.checkpoint();
    checkpoint.room = echo::RoomId::ResearchLab;
    checkpoint.playerPosition = {970.0F, 292.0F};
    checkpoint.enemyHealth[3] = 0;
    check(checkpoint.inventory.add(ItemId::MaintenanceKey) && game.restore(checkpoint),
          "sentry fixture restores in the laboratory");
    step(game, {}, 1);
    bool fired = false;
    for (const auto& shot : game.enemyProjectiles()) fired |= shot.active;
    check(fired && game.enemies()[4].type == echo::EnemyType::Sentry,
          "stationary sentry fires a hostile projectile");
    const int healthBeforeShot = game.player().health;
    step(game, {}, 5);
    check(game.player().health < healthBeforeShot, "sentry projectiles damage the player");

    DialogueFixture fixture;
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
            step(consumer, {.moveX = 1}, 15);
            check(consumer.player().velocity.x > 190.0F, "speed charge increases speed");
        }
        if (item == ItemId::JumpCharge) {
            step(consumer, {.jumpPressed = true, .jumpHeld = true});
            check(consumer.player().velocity.y < -420.0F, "jump charge increases jump");
        }
    }

    echo::Game blast;
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
    auto shieldState = shielded.checkpoint();
    shieldState.room = echo::RoomId::MaintenanceTunnel;
    shieldState.playerPosition = {760.0F, 292.0F};
    shieldState.enemyHealth[2] = 0;
    shieldState.activeItem = {ItemId::ShieldCell, 1};
    check(shielded.restore(shieldState), "shield fixture restores beside an enemy");
    step(shielded, {.useItemPressed = true});
    check(shielded.player().health == 5, "shield prevents enemy contact damage");
    std::array<bool, static_cast<std::size_t>(ItemId::Count)> drops{};
    for (std::size_t index = 2; index < echo::kLootDropCount; ++index) {
        const auto id = static_cast<std::size_t>(echo::Game::expectedLoot(index).item);
        check(id >= static_cast<std::size_t>(ItemId::JumpVisor) &&
                  id < drops.size() && !drops[id], "each new loot item is unique");
        if (id < drops.size()) drops[id] = true;
    }

    for (std::size_t enemy = 2; enemy < echo::kEnemyCount; ++enemy) {
        echo::Game lootGame;
        auto state = lootGame.checkpoint();
        state.room = echo::Game::expectedLootRoom(enemy == 2 ? 2 : enemy == 3 ? 5 : 8);
        state.playerPosition = {enemy == 2 ? 280.0F : enemy == 3 ? 420.0F : 930.0F, 292.0F};
        state.enemyHealth[0] = 0;
        state.enemyHealth[1] = 0;
        state.enemyHealth[enemy] = 1;
        state.activeItem = {ItemId::BlastCharge, 1};
        check(state.inventory.add(ItemId::MaintenanceKey) && lootGame.restore(state),
              "new enemy loot fixture restores");
        step(lootGame, {.useItemPressed = true});
        for (std::size_t index = 2; index < echo::kLootDropCount; ++index) {
            if (echo::Game::lootEnemy(index) != enemy) continue;
            check(lootGame.lootDrops()[index].active &&
                      lootGame.lootDrops()[index].stack == echo::Game::expectedLoot(index),
                  "new enemy defeat creates its corresponding drop");
        }
    }

    echo::Game fullBag;
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
    echo::framework::AbilityModule abilities{echo::kProjectAbilityDefinitions};
    echo::framework::ModuleContext moduleContext{};
    check(abilities.initialize(moduleContext), "game ability definitions initialize");
    std::ifstream configInput(ECHO_SOURCE_ASSETS "framework/game.json", std::ios::binary);
    const std::string config{std::istreambuf_iterator<char>(configInput),
                             std::istreambuf_iterator<char>()};
    echo::framework::JsonDocument document{};
    echo::framework::JsonError error{};
    echo::framework::QuestModule quests{};
    echo::framework::CutsceneModule cutscenes{};
    check(echo::framework::JsonDocument::parse(config.data(), config.size(), document, error) &&
              quests.load(document.root().find("quests")) && quests.initialize(moduleContext) &&
              cutscenes.load(document.root().find("cutscenes")) &&
              cutscenes.initialize(moduleContext),
          "project quest and cutscene content loads and initializes");
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
    check(restored.configureAbilities(abilities) && restored.configureQuests(quests) &&
              restored.restore(abilityCheckpoint) &&
              restored.abilityCollected() && restored.player().hasDoubleJump &&
              restored.questProgress().completed == 3 &&
              restored.questBranch() == 2,
          "checkpoint restore rebuilds ability ownership and quest progress");

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
    restored.start();
    check(restored.questProgress().completed == 0, "starting over resets derived quest progress");

    echo::Game coilRoute;
    check(coilRoute.configureAbilities(abilities) && coilRoute.configureQuests(quests),
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
    testMovementJumpAndFloorCollision();
    testAttributeModifiersAffectGameplay();
    testCheckpointRoundTrip();
    testEnemyLootDrops();
    testRoomTransition();
    testDataDrivenWorldLayout();
    testTechnicianDialogueAndInventory();
    testHealthCellDialogueAndUse();
    testKineticCoilDialogueAndCheckpoint();
    testSlotInventoryTransfersAndMigration();
    testLabDoorRequiresKey();
    testProjectileCombatAndDamage();
    testHazardDeathAndRespawn();
    testNewEnemyBehaviorsAndItems();
    testAbilityGateAndBacktracking();

    if (failures != 0) {
        std::cerr << failures << " gameplay test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All gameplay tests passed\n";
    return EXIT_SUCCESS;
}
