#include "game/game.hpp"
#include "platform/sdl/framework_integration.hpp"

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {

int failures{};

void check(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void check(echo::framework::Status status, std::string_view message) {
    check(static_cast<bool>(status), message);
}

} // namespace

int main() {
    constexpr const char* fixturePath = "echo-framework-invalid-config-test.json";
    constexpr const char* configPath = ECHO_SOURCE_ASSETS "framework/game.json";
    constexpr const char* dialoguePath = ECHO_SOURCE_ASSETS "framework/engineer.json";
    const auto writeFixture = [&](std::string_view content) {
        std::ofstream fixture(fixturePath, std::ios::binary);
        fixture.write(content.data(), static_cast<std::streamsize>(content.size()));
        fixture.close();
        return static_cast<bool>(fixture);
    };
    {
        std::ofstream fixture(fixturePath, std::ios::binary);
        fixture << R"({"game":"echo.project_echo","modules":["echo.state"],"resources":[]})";
        fixture.close();
        check(static_cast<bool>(fixture), "invalid configuration fixture is written");
    }
    {
        echo::platform::FrameworkIntegration missingModule;
        const auto status = missingModule.initialize(fixturePath, dialoguePath);
        check(status.code == echo::framework::ErrorCode::MissingDependency &&
                  status.subject == "echo.project_echo",
              "single-module configuration rejects the missing PROJECT ECHO adapter");
    }
    check(std::remove(fixturePath) == 0, "configuration fixture is removed");
    {
        std::ofstream fixture(fixturePath, std::ios::binary);
        fixture << "{ invalid dialogue json";
        fixture.close();
        check(static_cast<bool>(fixture), "invalid dialogue fixture is written");
        echo::platform::FrameworkIntegration malformedDialogue;
        check(malformedDialogue.initialize(configPath, fixturePath).code ==
                  echo::framework::ErrorCode::ParseError,
              "malformed dialogue content fails startup explicitly");
    }
    check(std::remove(fixturePath) == 0, "dialogue fixture is removed");
    {
        constexpr std::string_view shiftedConfig = R"({
            "game": "echo.project_echo",
            "modules": ["echo.state", "echo.narrative.dialogue", "echo.project_echo"],
            "technician": {"transform": {"position": [250, 280]}},
            "resources": [
                {"id": "echo:dialogue/engineer", "type": "dialogue"},
                {"id": "echo:item/maintenance_key", "type": "item"}
            ]
        })";
        check(writeFixture(shiftedConfig), "shifted engineer configuration is written");
        echo::platform::FrameworkIntegration shifted;
        echo::Game shiftedGame;
        check(shifted.initialize(fixturePath, dialoguePath) &&
                  shifted.configureGame(shiftedGame) &&
                  shiftedGame.technicianBounds().x == 250.0F &&
                  !shiftedGame.technicianNearby(),
              "changing the registered transform changes the engineer's gameplay position");
        const auto checkpoint = shiftedGame.checkpoint();
        shiftedGame.start();
        check(shiftedGame.restore(checkpoint) &&
                  shiftedGame.technicianBounds().x == 250.0F,
              "new games and checkpoint restoration retain the configured engineer position");
    }
    check(std::remove(fixturePath) == 0, "shifted configuration fixture is removed");
    {
        const auto configForInteraction = [](std::string_view interaction) {
            std::string json = R"({"game":"echo.project_echo",
                "modules":["echo.state","echo.narrative.dialogue","echo.project_echo"],
                "technician":{"transform":{"position":[108,280]},"interaction":)";
            json.append(interaction);
            json += R"(},"resources":[
                {"id":"echo:dialogue/engineer","type":"dialogue"},
                {"id":"echo:item/maintenance_key","type":"item"}]})";
            return json;
        };
        check(writeFixture(configForInteraction(R"({"horizontalReach":46})")),
              "narrow interaction config is written");
        echo::platform::FrameworkIntegration narrow;
        echo::Game narrowGame;
        check(narrow.initialize(fixturePath, dialoguePath) &&
                  narrow.configureGame(narrowGame) && !narrowGame.technicianNearby(),
              "configured narrow reach changes gameplay");
        check(writeFixture(configForInteraction(R"({"horizontalReach":47})")),
              "wide interaction config is written");
        echo::platform::FrameworkIntegration wide;
        echo::Game wideGame;
        check(wide.initialize(fixturePath, dialoguePath) &&
                  wide.configureGame(wideGame) && wideGame.technicianNearby(),
              "configured wider reach allows talking from the same position");
        const auto checkpoint = wideGame.checkpoint();
        wideGame.start();
        check(wideGame.restore(checkpoint) && wideGame.technicianNearby(),
              "new games and checkpoint restoration retain the configured reach");
        check(writeFixture(configForInteraction(R"({"horizontalReach":0})")),
              "minimum interaction reach config is written");
        auto minimum = std::make_unique<echo::platform::FrameworkIntegration>();
        echo::Game minimumGame;
        check(minimum->initialize(fixturePath, dialoguePath) &&
                  minimum->configureGame(minimumGame) && !minimumGame.technicianNearby(),
              "minimum interaction reach is accepted");
        check(writeFixture(configForInteraction(R"({"horizontalReach":128})")),
              "maximum interaction reach config is written");
        auto maximum = std::make_unique<echo::platform::FrameworkIntegration>();
        echo::Game maximumGame;
        check(maximum->initialize(fixturePath, dialoguePath) &&
                  maximum->configureGame(maximumGame) && maximumGame.technicianNearby(),
              "maximum interaction reach is accepted");
        for (const std::string_view invalid : {
                 R"({"horizontalReach":-1})", R"({"horizontalReach":129})",
                 R"({"horizontalReach":"far"})", R"({"horizontalReach":40,"extra":1})",
                 "null"}) {
            check(writeFixture(configForInteraction(invalid)),
                  "invalid interaction config is written");
            echo::platform::FrameworkIntegration invalidInteraction;
            check(invalidInteraction.initialize(fixturePath, dialoguePath).code ==
                      echo::framework::ErrorCode::InvalidValue,
                  "invalid interaction component is rejected before gameplay");
        }
        check(writeFixture(configForInteraction("{}")), "default interaction config is written");
        echo::platform::FrameworkIntegration defaults;
        echo::Game defaultGame;
        check(defaults.initialize(fixturePath, dialoguePath) &&
                  defaults.configureGame(defaultGame) && defaultGame.technicianNearby(),
              "omitted component property uses the legacy reach");
        constexpr std::string_view legacyConfig = R"({
            "game": "echo.project_echo",
            "modules": ["echo.state", "echo.narrative.dialogue", "echo.project_echo"],
            "technician": {"transform": {"position": [108, 280]}},
            "resources": [
                {"id": "echo:dialogue/engineer", "type": "dialogue"},
                {"id": "echo:item/maintenance_key", "type": "item"}
            ]
        })";
        check(writeFixture(legacyConfig), "legacy configuration is written");
        auto legacy = std::make_unique<echo::platform::FrameworkIntegration>();
        echo::Game legacyGame;
        check(legacy->initialize(fixturePath, dialoguePath) &&
                  legacy->configureGame(legacyGame) && legacyGame.technicianNearby(),
              "legacy configurations without the component retain the original reach");
    }
    check(std::remove(fixturePath) == 0, "interaction configuration fixture is removed");
    {
        constexpr std::string_view unsupportedTransform = R"({
            "game": "echo.project_echo",
            "modules": ["echo.state", "echo.narrative.dialogue", "echo.project_echo"],
            "technician": {"transform": {"position": [108, 280], "scale": [2, 1]}},
            "resources": [
                {"id": "echo:dialogue/engineer", "type": "dialogue"},
                {"id": "echo:item/maintenance_key", "type": "item"}
            ]
        })";
        check(writeFixture(unsupportedTransform), "unsupported transform fixture is written");
        echo::platform::FrameworkIntegration invalidTransform;
        check(invalidTransform.initialize(fixturePath, dialoguePath).code ==
                  echo::framework::ErrorCode::InvalidValue,
              "unsupported engineer transform fields fail rather than being ignored");
    }
    check(std::remove(fixturePath) == 0, "unsupported transform fixture is removed");
    {
        constexpr std::string_view unsupportedAction = R"({
            "schemaVersion": 1,
            "id": "echo:dialogue/engineer",
            "version": 1,
            "startNode": "start",
            "nodes": [{
                "id": "start",
                "speaker": "ENGINEER MIRA",
                "text": "CHOOSE.",
                "choices": [{
                    "id": "invalid",
                    "label": "INVALID",
                    "targetNode": "",
                    "onSelect": {"id": "echo.unsupported"}
                }]
            }]
        })";
        check(writeFixture(unsupportedAction), "unsupported dialogue action fixture is written");
        echo::platform::FrameworkIntegration invalidAction;
        echo::Game invalidGame;
        check(invalidAction.initialize(configPath, fixturePath) &&
                  invalidAction.configureGame(invalidGame).code ==
                      echo::framework::ErrorCode::InvalidValue &&
                  !invalidGame.inventory().contains(echo::ItemId::MaintenanceKey),
              "unsupported compiled actions are rejected before gameplay changes");
    }
    check(std::remove(fixturePath) == 0, "unsupported action fixture is removed");
    {
        const auto displayStatus = [&](std::string_view speaker, std::string_view line,
                                       std::string_view label) {
            std::string json =
                R"({"schemaVersion":1,"id":"echo:dialogue/engineer","version":1,"startNode":"start","nodes":[{"id":"start","speaker":")";
            json.append(speaker);
            json += R"(","text":")";
            json.append(line);
            json += R"(","choices":[{"id":"choice","label":")";
            json.append(label);
            json += R"(","targetNode":""}]}]})";
            if (!writeFixture(json)) {
                return echo::framework::ErrorCode::StorageFailure;
            }
            echo::platform::FrameworkIntegration candidate;
            return candidate.initialize(configPath, fixturePath).code;
        };
        check(displayStatus(std::string(39, 'A'), std::string(80, 'A'),
                            std::string(72, 'A')) == echo::framework::ErrorCode::None,
              "dialogue text at each bitmap panel limit is accepted");
        check(displayStatus(std::string(40, 'A'), "LINE", "CHOICE") ==
                  echo::framework::ErrorCode::InvalidValue &&
                  displayStatus("SPEAKER", std::string(81, 'A'), "CHOICE") ==
                  echo::framework::ErrorCode::InvalidValue &&
                  displayStatus("SPEAKER", "LINE", std::string(73, 'A')) ==
                  echo::framework::ErrorCode::InvalidValue,
              "dialogue text exceeding any bitmap panel limit is rejected");
        check(displayStatus("SPEAKER", "\xCE\xA9", "CHOICE") ==
                  echo::framework::ErrorCode::InvalidValue,
              "unsupported UTF-8 glyphs fail explicitly instead of drawing blank text");
        check(displayStatus("SPEAKER", "UNSUPPORTED!", "CHOICE") ==
                  echo::framework::ErrorCode::InvalidValue,
              "unsupported ASCII glyphs also fail explicitly");
    }
    check(std::remove(fixturePath) == 0, "dialogue display fixture is removed");
    echo::platform::FrameworkIntegration integration;
    check(integration.initialize("missing-framework-config.json", dialoguePath).code ==
              echo::framework::ErrorCode::NotFound,
          "missing game configuration is reported");
    check(integration.initialize(configPath, "missing-engineer-dialogue.json").code ==
              echo::framework::ErrorCode::NotFound,
          "missing dialogue content is reported");
    check(integration.initialize(configPath, dialoguePath),
          "PROJECT ECHO module configuration initializes");
    check(integration.beginGame(), "framework state resets at a new game");
    echo::Game game;
    check(integration.configureGame(game), "game binds the initialized dialogue module");
    check(game.technicianBounds().x == 108.0F && game.technicianBounds().y == 280.0F,
          "schema-instantiated transform positions the engineer in the game");
    check(game.update({.interactPressed = true}) &&
              game.dialogueText() == "LAB ACCESS IS LOCKED." &&
              game.update({.interactPressed = true}) &&
              game.dialogueChoiceCount() == 2 &&
              game.update({.interactPressed = true}) &&
              game.inventory().contains(echo::ItemId::MaintenanceKey),
          "packaged dialogue loads, renders a choice view, and grants the gameplay key");
    check(game.update({.interactPressed = true}) && !game.dialogueActive(),
          "the initial key-offer conversation can be closed");
    check(integration.notifyEchoStepUnlocked(), "ability unlock is mirrored through an action");
    check(integration.unlockEventCount() == 1,
          "ability transition publishes one typed framework event");
    check(integration.notifyEchoStepUnlocked() && integration.unlockEventCount() == 1,
          "repeated state synchronization does not duplicate the unlock event");
    check(game.update({.interactPressed = true}) &&
              game.update({.interactPressed = true}) &&
              game.update({.interactPressed = true}) &&
              game.dialogueChoiceCount() == 1 &&
              game.dialogueChoiceText(0) == "ASK ABOUT ECHO STEP" &&
              game.update({.interactPressed = true}) &&
              game.dialogueText() == "DOUBLE JUMP OPENS THE CENTRAL GATE.",
          "the real ability-unlock projection enables Mira's conditional advice branch");
    check(integration.beginGame(), "new game resets the temporary framework mirror");
    game.start();
    echo::framework::StateValue abilityState{};
    check(integration.gameState().get(
              {echo::framework::StateScope::Global, {}, "player.echo_step_unlocked"},
              abilityState) &&
              abilityState.type == echo::framework::ValueType::Boolean &&
              !abilityState.boolean,
          "new game clears the previously unlocked ability state");
    check(integration.notifyEchoStepUnlocked() && integration.unlockEventCount() == 2,
          "a later game emits a new ability transition");
    echo::framework::GameState savedState{};
    check(savedState.set(
              {echo::framework::StateScope::Global, {}, "player.echo_step_unlocked"},
              echo::framework::StateValue::fromBoolean(true)),
          "fixture save state records the unlocked ability");
    check(integration.resumeGame(savedState), "framework state restores from a save");
    check(integration.notifyEchoStepUnlocked() && integration.unlockEventCount() == 2,
          "resuming an unlocked ability does not publish a duplicate transition");
    integration.shutdown();

    if (failures != 0) {
        std::cerr << failures << " framework integration test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PROJECT ECHO framework integration tests passed\n";
    return EXIT_SUCCESS;
}
