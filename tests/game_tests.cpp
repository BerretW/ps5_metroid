#include "game/game.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
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
        }
    }

    ~DialogueFixture() { module_.shutdown(); }

    [[nodiscard]] echo::framework::Status bind(echo::Game& game) noexcept {
        return status_ ? game.configureDialogue(module_, state_, {108.0F, 280.0F})
                       : status_;
    }

private:
    std::unique_ptr<echo::framework::DialogueDocument> document_{};
    echo::framework::DialogueModule module_{};
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

void testRoomTransition() {
    echo::Game game;
    check(moveUntilRoom(game, echo::RoomId::MaintenanceTunnel, 1), "landing bay connects to maintenance tunnel");
    check(game.roomName() == "Maintenance Tunnel", "room transition loads the destination name");
}

void testTechnicianDialogueAndInventory() {
    echo::Game unconfigured;
    check(unconfigured.update({.interactPressed = true}).code ==
              echo::framework::ErrorCode::InvalidState &&
              !unconfigured.dialogueActive(),
          "dialogue without its required service reports an error");

    DialogueFixture fixture;
    echo::Game game;
    check(static_cast<bool>(fixture.bind(game)), "game binds the loaded engineer dialogue");
    check(game.technicianNearby(), "technician is reachable from the Landing Bay start");
    step(game, {.interactPressed = true});
    check(game.dialogueActive() && game.dialogueSpeaker() == "ENGINEER MIRA" &&
              game.dialogueText() == "LAB ACCESS IS LOCKED.",
          "engineer displays the first line from the JSON asset");
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
    check(game.dialogueText() == "THE KEY OPENS THE LAB DOOR.",
          "hidden choices follow the JSON fallback for a returning player");
    step(game, {.interactPressed = true});
    check(!game.dialogueActive() && game.inventory().count == 1,
          "the repeat conversation preserves the restored inventory");
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

void testAbilityGateAndBacktracking() {
    DialogueFixture fixture;
    echo::Game game;
    check(static_cast<bool>(fixture.bind(game)), "progression game binds engineer dialogue");
    talkToTechnician(game);
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
}

} // namespace

int main() {
    testMovementJumpAndFloorCollision();
    testCheckpointRoundTrip();
    testRoomTransition();
    testTechnicianDialogueAndInventory();
    testLabDoorRequiresKey();
    testProjectileCombatAndDamage();
    testHazardDeathAndRespawn();
    testAbilityGateAndBacktracking();

    if (failures != 0) {
        std::cerr << failures << " gameplay test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All gameplay tests passed\n";
    return EXIT_SUCCESS;
}
