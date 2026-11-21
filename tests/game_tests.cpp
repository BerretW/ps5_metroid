#include "game/game.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void step(echo::Game& game, echo::Input input, int ticks = 1) {
    for (int index = 0; index < ticks; ++index) {
        game.update(input);
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
    echo::Game game;
    check(game.technicianNearby(), "technician is reachable from the Landing Bay start");
    talkToTechnician(game);
    check(game.inventory().count == 1, "key pickup occupies one inventory slot");
    check(game.checkpoint().inventory.contains(echo::ItemId::MaintenanceKey),
          "checkpoint captures the inventory");
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
    echo::Game game;
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
