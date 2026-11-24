#include "game/game.hpp"
#include "platform/sdl/framework_integration.hpp"
#include "platform/sdl/game_persistence.hpp"

#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {

int failures{};

void check(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void check(echo::framework::Status status, std::string_view message) {
    if (!status) {
        std::cerr << "DETAIL: " << status.subject << ": " << status.detail << '\n';
    }
    check(static_cast<bool>(status), message);
}

bool loadWorldFixture(std::string_view changedId, std::string_view original,
                      std::string_view replacement, echo::framework::WorldModule& world,
                      echo::WorldLayout& layout) {
    using namespace echo::framework;
    auto load = [&](const char* path, auto&& apply) {
        std::ifstream input(std::string(ECHO_SOURCE_ASSETS) + path, std::ios::binary);
        if (!input) return false;
        std::string source{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
        JsonDocument document{};
        JsonError error{};
        return JsonDocument::parse(source.data(), source.size(), document, error) &&
               apply(document.root());
    };
    if (!load("world/world.json", [&](JsonValue root) { return world.loadManifest(root); })) {
        return false;
    }
    bool changed = false;
    for (bool objects : {true, false}) {
        const std::size_t count = objects ? world.manifestObjectCount()
                                          : world.manifestRoomCount();
        for (std::size_t index = 0; index < count; ++index) {
            const std::string_view id = objects ? world.manifestObject(index)
                                                : world.manifestRoom(index);
            std::array<char, 160> path{};
            if (!WorldModule::contentPath(id, path.data(), path.size())) return false;
            std::ifstream input(std::string(ECHO_SOURCE_ASSETS) + path.data(),
                                std::ios::binary);
            if (!input) return false;
            std::string source{std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()};
            if (id == changedId) {
                const std::size_t position = source.find(original);
                if (position == std::string::npos) return false;
                source.replace(position, original.size(), replacement);
                changed = true;
            }
            JsonDocument document{};
            JsonError error{};
            if (!JsonDocument::parse(source.data(), source.size(), document, error) ||
                !(objects ? world.addObject(id, document.root())
                          : world.addRoom(id, document.root()))) return false;
        }
    }
    if (!changed || !world.finalize()) return false;
    return static_cast<bool>(echo::buildWorldLayout(world, layout));
}

// Copies the world and framework assets to a scratch root, applies one text edit and
// starts a fresh integration against it.
echo::framework::Status initializeWithEditedAsset(
    std::string_view relativeFile, std::string_view original, std::string_view replacement,
    echo::platform::FrameworkIntegration& integration) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "echo-door-fixture";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root, error);
    for (const char* directory : {"world", "framework"}) {
        error.clear();
        fs::copy(fs::path(ECHO_SOURCE_ASSETS) / directory, root / directory,
                 fs::copy_options::recursive, error);
        if (error) return {echo::framework::ErrorCode::NotFound, directory, "Fixture copy failed."};
    }
    const fs::path file = root / relativeFile;
    std::string source;
    {
        std::ifstream input(file, std::ios::binary);
        source.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    const std::size_t position = source.find(original);
    if (position == std::string::npos) {
        return {echo::framework::ErrorCode::NotFound, "fixture", "Edited text not found."};
    }
    source.replace(position, original.size(), replacement);
    {
        std::ofstream output(file, std::ios::binary);
        output.write(source.data(), static_cast<std::streamsize>(source.size()));
    }
    const std::string content = root.generic_string() + "/";
    const std::string framework = content + "framework/";
    return integration.initialize(
        (framework + "game.json").c_str(), (framework + "engineer.json").c_str(),
        (framework + "items.json").c_str(), content.c_str());
}

} // namespace

int main() {
    constexpr const char* fixturePath = "echo-framework-invalid-config-test.json";
    constexpr const char* malformedDialoguePath =
        "echo-framework-invalid-dialogue-test.json";
    constexpr const char* dialogueFixtureAssetPath =
        ECHO_SOURCE_ASSETS "framework/echo_framework_test_dialogue.json";
    constexpr const char* configPath = ECHO_SOURCE_ASSETS "framework/game.json";
    constexpr const char* dialoguePath = ECHO_SOURCE_ASSETS "framework/engineer.json";
    constexpr const char* itemCatalogPath = ECHO_SOURCE_ASSETS "framework/items.json";
    const auto writeFixture = [&](std::string_view content) {
        std::ofstream fixture(fixturePath, std::ios::binary);
        fixture.write(content.data(), static_cast<std::streamsize>(content.size()));
        fixture.close();
        return static_cast<bool>(fixture);
    };
    const auto writeConfigWithDialogueSource = [&](std::string_view source) {
        std::ifstream input(configPath, std::ios::binary);
        std::string config{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
        constexpr std::string_view original = "framework/engineer.json";
        const std::size_t position = config.find(original);
        if (position == std::string::npos) return false;
        config.replace(position, original.size(), source);
        return writeFixture(config);
    };
    const auto writeDialogueFixture = [&](std::string_view content) {
        std::ofstream fixture(dialogueFixtureAssetPath, std::ios::binary);
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
        auto missingModule = std::make_unique<echo::platform::FrameworkIntegration>();
        const auto status = missingModule->initialize(fixturePath, dialoguePath, nullptr, ECHO_SOURCE_ASSETS);
        check(status.code == echo::framework::ErrorCode::MissingDependency &&
                  status.subject == "echo.project_echo",
              "single-module configuration rejects the missing PROJECT ECHO adapter");
    }
    check(std::remove(fixturePath) == 0, "configuration fixture is removed");
    {
        constexpr std::string_view legacyConfig = R"({
            "game": "echo.project_echo",
            "modules": ["echo.state", "echo.narrative.dialogue", "echo.project_echo"],
            "technician": {},
            "resources": [
                {"id": "echo:dialogue/engineer", "type": "dialogue"},
                {"id": "echo:item/maintenance_key", "type": "item"}
            ]
        })";
        check(writeFixture(legacyConfig), "single-dialogue legacy config is written");
        std::ofstream fixture(malformedDialoguePath, std::ios::binary);
        fixture << "{ invalid dialogue json";
        fixture.close();
        check(static_cast<bool>(fixture), "invalid dialogue fixture is written");
        auto malformedDialogue = std::make_unique<echo::platform::FrameworkIntegration>();
        check(malformedDialogue->initialize(fixturePath, malformedDialoguePath).code ==
                  echo::framework::ErrorCode::ParseError,
              "malformed dialogue content fails startup explicitly");
    }
    check(std::remove(malformedDialoguePath) == 0 &&
              std::remove(fixturePath) == 0,
          "dialogue fixtures are removed");
    {
        std::ifstream input(configPath, std::ios::binary);
        const std::string source{std::istreambuf_iterator<char>(input),
                                 std::istreambuf_iterator<char>()};
        for (const auto& [original, replacement] : {
                 std::pair<std::string_view, std::string_view>{
                     "echo:fact/echo_step", "echo:fact/unknown"},
                 {"echo:item/shield_cell", "echo:item/unknown"},
                 {"GET THE KEY FROM THE ENGINEER", "INVALID?"},
                 {"FIND THE SOURCE BEFORE THE SIGNAL FADES", "INVALID?"}}) {
            std::string invalid = source;
            const std::size_t position = invalid.find(original);
            check(position != std::string::npos, "quest fixture field exists");
            if (position == std::string::npos) continue;
            invalid.replace(position, original.size(), replacement);
            check(writeFixture(invalid), "invalid quest configuration is written");
            auto integration = std::make_unique<echo::platform::FrameworkIntegration>();
            check(!integration->initialize(fixturePath, dialoguePath, itemCatalogPath, ECHO_SOURCE_ASSETS),
                  "runtime rejects unsupported quest or cutscene content");
        }
        std::string alternate = source;
        const std::string originalReward = "\"rewardItem\": \"echo:item/med_kit\"";
        const std::size_t rewardPosition = alternate.find(originalReward);
        check(rewardPosition != std::string::npos, "alternate reward fixture field exists");
        if (rewardPosition != std::string::npos) {
            alternate.replace(rewardPosition, originalReward.size(),
                              "\"rewardItem\": \"echo:item/scrap\"");
            const std::string originalQuantity = "\"rewardQuantity\": 1";
            const std::size_t quantityPosition = alternate.find(originalQuantity, rewardPosition);
            check(quantityPosition != std::string::npos, "alternate reward quantity exists");
            if (quantityPosition != std::string::npos) {
                alternate.replace(quantityPosition, originalQuantity.size(), "\"rewardQuantity\": 3");
                check(writeFixture(alternate), "alternate reward config is written");
                auto integration = std::make_unique<echo::platform::FrameworkIntegration>();
                echo::Game rewardGame;
                check(integration->initialize(fixturePath, dialoguePath, itemCatalogPath,
                                              ECHO_SOURCE_ASSETS) &&
                          integration->configureGame(rewardGame),
                      "startup accepts a supported data-driven quest reward and quantity");
                alternate.replace(quantityPosition, std::string("\"rewardQuantity\": 3").size(),
                                  "\"rewardQuantity\": 100");
                check(writeFixture(alternate), "oversized reward config is written");
                auto invalidReward = std::make_unique<echo::platform::FrameworkIntegration>();
                check(!invalidReward->initialize(fixturePath, dialoguePath, itemCatalogPath,
                                                 ECHO_SOURCE_ASSETS),
                      "startup rejects a quest reward exceeding its catalog stack limit");
            }
        }
    }
    check(std::remove(fixturePath) == 0, "quest configuration fixture is removed");
    {
        constexpr std::string_view legacyTransformConfig = R"({
            "game": "echo.project_echo",
            "modules": ["echo.state", "echo.narrative.dialogue", "echo.project_echo"],
            "technician": {"transform": {"position": [250, 280]}},
            "resources": [
                {"id": "echo:dialogue/engineer", "type": "dialogue"},
                {"id": "echo:item/maintenance_key", "type": "item"}
            ]
        })";
        check(writeFixture(legacyTransformConfig), "legacy engineer transform configuration is written");
        auto legacyTransform = std::make_unique<echo::platform::FrameworkIntegration>();
        check(legacyTransform->initialize(fixturePath, dialoguePath, nullptr, ECHO_SOURCE_ASSETS).code ==
                  echo::framework::ErrorCode::InvalidValue,
              "a second engineer position in game.json is rejected; echo:object/engineer owns it");
    }
    check(std::remove(fixturePath) == 0, "legacy transform fixture is removed");
    {
        const auto configForInteraction = [](std::string_view interaction) {
            std::string json = R"({"game":"echo.project_echo",
                "modules":["echo.state","echo.narrative.dialogue","echo.project_echo"],
                "technician":{"interaction":)";
            json.append(interaction);
            json += R"(},"resources":[
                {"id":"echo:dialogue/engineer","type":"dialogue"},
                {"id":"echo:item/maintenance_key","type":"item"}]})";
            return json;
        };
        check(writeFixture(configForInteraction(R"({"horizontalReach":46})")),
              "narrow interaction config is written");
        auto narrow = std::make_unique<echo::platform::FrameworkIntegration>();
        echo::Game narrowGame;
        check(narrow->initialize(fixturePath, dialoguePath, nullptr, ECHO_SOURCE_ASSETS) &&
                  narrow->configureGame(narrowGame) && !narrowGame.technicianNearby(),
              "configured narrow reach changes gameplay");
        check(writeFixture(configForInteraction(R"({"horizontalReach":47})")),
              "wide interaction config is written");
        auto wide = std::make_unique<echo::platform::FrameworkIntegration>();
        echo::Game wideGame;
        check(wide->initialize(fixturePath, dialoguePath, nullptr, ECHO_SOURCE_ASSETS) &&
                  wide->configureGame(wideGame) && wideGame.technicianNearby(),
              "configured wider reach allows talking from the same position");
        const auto checkpoint = wideGame.checkpoint();
        wideGame.start();
        check(wideGame.restore(checkpoint) && wideGame.technicianNearby(),
              "new games and checkpoint restoration retain the configured reach");
        check(writeFixture(configForInteraction(R"({"horizontalReach":0})")),
              "minimum interaction reach config is written");
        auto minimum = std::make_unique<echo::platform::FrameworkIntegration>();
        echo::Game minimumGame;
        check(minimum->initialize(fixturePath, dialoguePath, nullptr, ECHO_SOURCE_ASSETS) &&
                  minimum->configureGame(minimumGame) && !minimumGame.technicianNearby(),
              "minimum interaction reach is accepted");
        check(writeFixture(configForInteraction(R"({"horizontalReach":128})")),
              "maximum interaction reach config is written");
        auto maximum = std::make_unique<echo::platform::FrameworkIntegration>();
        echo::Game maximumGame;
        check(maximum->initialize(fixturePath, dialoguePath, nullptr, ECHO_SOURCE_ASSETS) &&
                  maximum->configureGame(maximumGame) && maximumGame.technicianNearby(),
              "maximum interaction reach is accepted");
        for (const std::string_view invalid : {
                 R"({"horizontalReach":-1})", R"({"horizontalReach":129})",
                 R"({"horizontalReach":"far"})", R"({"horizontalReach":40,"extra":1})",
                 "null"}) {
            check(writeFixture(configForInteraction(invalid)),
                  "invalid interaction config is written");
            auto invalidInteraction = std::make_unique<echo::platform::FrameworkIntegration>();
            check(invalidInteraction->initialize(fixturePath, dialoguePath, nullptr, ECHO_SOURCE_ASSETS).code ==
                      echo::framework::ErrorCode::InvalidValue,
                  "invalid interaction component is rejected before gameplay");
        }
        check(writeFixture(configForInteraction("{}")), "default interaction config is written");
        auto defaults = std::make_unique<echo::platform::FrameworkIntegration>();
        echo::Game defaultGame;
        check(defaults->initialize(fixturePath, dialoguePath, nullptr, ECHO_SOURCE_ASSETS) &&
                  defaults->configureGame(defaultGame) && defaultGame.technicianNearby(),
              "omitted component property uses the legacy reach");
        constexpr std::string_view legacyConfig = R"({
            "game": "echo.project_echo",
            "modules": ["echo.state", "echo.narrative.dialogue", "echo.project_echo"],
            "technician": {},
            "resources": [
                {"id": "echo:dialogue/engineer", "type": "dialogue"},
                {"id": "echo:item/maintenance_key", "type": "item"}
            ]
        })";
        check(writeFixture(legacyConfig), "legacy configuration is written");
        auto legacy = std::make_unique<echo::platform::FrameworkIntegration>();
        echo::Game legacyGame;
        check(legacy->initialize(fixturePath, dialoguePath, nullptr, ECHO_SOURCE_ASSETS) &&
                  legacy->configureGame(legacyGame) && legacyGame.technicianNearby(),
              "legacy configurations without the component retain the original reach");
    }
    check(std::remove(fixturePath) == 0, "interaction configuration fixture is removed");
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
        check(writeDialogueFixture(unsupportedAction) &&
                  writeConfigWithDialogueSource(
                      "framework/echo_framework_test_dialogue.json"),
              "unsupported dialogue action fixture is written and selected by content data");
        auto invalidAction = std::make_unique<echo::platform::FrameworkIntegration>();
        echo::Game invalidGame;
        check(invalidAction->initialize(fixturePath, dialoguePath, itemCatalogPath, ECHO_SOURCE_ASSETS) &&
                  invalidAction->configureGame(invalidGame).code ==
                      echo::framework::ErrorCode::InvalidValue &&
                  !invalidGame.inventory().contains(echo::ItemId::MaintenanceKey),
              "unsupported compiled actions are rejected before gameplay changes");
    }
    check(std::remove(fixturePath) == 0,
          "unsupported action config fixture is removed");
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
            if (!writeDialogueFixture(json) ||
                !writeConfigWithDialogueSource(
                    "framework/echo_framework_test_dialogue.json")) {
                return echo::framework::ErrorCode::StorageFailure;
            }
            auto candidate = std::make_unique<echo::platform::FrameworkIntegration>();
            return candidate->initialize(fixturePath, dialoguePath, itemCatalogPath,
                                         ECHO_SOURCE_ASSETS).code;
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
    check(std::remove(fixturePath) == 0 &&
              std::remove(dialogueFixtureAssetPath) == 0,
          "dialogue display fixtures are removed");
    auto missingItemCatalog = std::make_unique<echo::platform::FrameworkIntegration>();
    check(missingItemCatalog->initialize(configPath, dialoguePath, nullptr, ECHO_SOURCE_ASSETS).code ==
              echo::framework::ErrorCode::InvalidArgument,
          "selected inventory module requires its external item catalog");
    auto integration = std::make_unique<echo::platform::FrameworkIntegration>();
    check(integration->initialize("missing-framework-config.json", dialoguePath, nullptr, ECHO_SOURCE_ASSETS).code ==
              echo::framework::ErrorCode::NotFound,
          "missing game configuration is reported");
    check(writeConfigWithDialogueSource(
              "framework/missing_engineer_dialogue_test.json") &&
              integration->initialize(fixturePath, dialoguePath, itemCatalogPath,
                                      ECHO_SOURCE_ASSETS).code ==
              echo::framework::ErrorCode::NotFound,
          "missing manifest-declared dialogue source is reported");
    check(std::remove(fixturePath) == 0,
          "missing dialogue source config fixture is removed");
    check(integration->initialize(configPath, dialoguePath, itemCatalogPath, ECHO_SOURCE_ASSETS),
          "PROJECT ECHO module configuration initializes");
    check(integration->beginGame(), "framework state resets at a new game");
    echo::framework::PowerModule* power = integration->powerModule();
    bool mainPower{};
    bool emergencyPower{};
    check(power != nullptr &&
              power->isPowered("echo:power/cargo_main", mainPower) && mainPower &&
              power->isPowered("echo:power/cargo_emergency", emergencyPower) &&
              !emergencyPower,
          "Cargo Hold emergency power starts offline while main power remains on");
    check(power != nullptr && power->setRequested("echo:power/cargo_main", false) &&
              power->isPowered("echo:power/cargo_main", mainPower) && !mainPower &&
              power->isPowered("echo:power/cargo_emergency", emergencyPower) &&
              !emergencyPower,
          "Cargo Hold circuits can be controlled independently");
    check(integration->beginGame() &&
              power->isPowered("echo:power/cargo_main", mainPower) && mainPower &&
              power->isPowered("echo:power/cargo_emergency", emergencyPower) &&
              !emergencyPower,
          "starting a new game restores configured power defaults");
    check(integration->togglePowerCircuit("echo:power/cargo_main") &&
              power->isPowered("echo:power/cargo_main", mainPower) && !mainPower &&
              power->isPowered("echo:power/cargo_emergency", emergencyPower) &&
              !emergencyPower,
          "the registered power action switches off the main circuit independently");
    check(integration->togglePowerCircuit("echo:power/cargo_main") &&
              power->isPowered("echo:power/cargo_main", mainPower) && mainPower,
          "the registered power action restores the main circuit");
    echo::Game game;
    check(integration->configureGame(game), "game binds the initialized dialogue module");
    {
        static echo::WorldLayout gated{};
        gated = *integration->worldLayout();
        echo::DoorLayout door{};
        door.room = echo::RoomId::MaintenanceTunnel;
        door.bounds = {600.0F, 272.0F, 44.0F, 48.0F};
        door.destination = echo::RoomId::PowerStation;
        door.arriveFromLeft = true;
        const std::string_view circuit = "echo:power/cargo_emergency";
        std::copy(circuit.begin(), circuit.end(), door.requiresCircuit.begin());
        gated.doors[gated.doorCount++] = door;
        echo::Game gatedGame;
        check(integration->configureGame(gatedGame), "gated game binds services");
        check(gatedGame.configureWorld(gated), "circuit-gated door layout configures");
        auto place = [&gatedGame] {
            echo::GameCheckpoint cp = gatedGame.checkpoint();
            cp.room = echo::RoomId::MaintenanceTunnel;
            cp.playerPosition = {610.0F, 292.0F};
            return gatedGame.restore(cp);
        };
        bool powered{};
        check(place() && power->isPowered("echo:power/cargo_emergency", powered) && !powered &&
                  gatedGame.update({.interactPressed = true}) && gatedGame.accessBlocked() &&
                  gatedGame.roomId() == echo::RoomId::MaintenanceTunnel,
              "a requiresCircuit door denies entry while the circuit is off");
        check(integration->togglePowerCircuit("echo:power/cargo_emergency") &&
                  place() && gatedGame.update({.interactPressed = true}) &&
                  gatedGame.roomId() == echo::RoomId::PowerStation,
              "a requiresCircuit door opens once the circuit is powered");
        check(integration->togglePowerCircuit("echo:power/cargo_emergency"),
              "emergency circuit is restored to its default");

        door.requiresCircuit = {};
        const std::string_view cutscene = "echo:cutscene/central_signal";
        std::copy(cutscene.begin(), cutscene.end(), door.cutscene.begin());
        door.transitions = false;
        door.once = true;
        door.destination = echo::RoomId::MaintenanceTunnel;
        gated.doors[gated.doorCount - 1] = door;
        check(gatedGame.configureWorld(gated) && place() && gatedGame.update({}) && gatedGame.cutsceneActive(),
              "an event-only trigger starts its cutscene on overlap");
        for (int i = 0; i < 40 && gatedGame.cutsceneActive(); ++i) {
            check(gatedGame.update({.interactPressed = true}), "cutscene advances");
        }
        for (int i = 0; i < 5; ++i) check(gatedGame.update({}), "trigger idles");
        check(!gatedGame.cutsceneActive(), "a once trigger does not restart its cutscene");
    }
    {
        echo::Game demo;
        check(integration->configureGame(demo), "demo game binds services");
        auto place = [&demo](echo::RoomId room, float x) {
            echo::GameCheckpoint cp = demo.checkpoint();
            cp.room = room;
            cp.playerPosition = {x, 292.0F};
            return demo.restore(cp);
        };
        check(place(echo::RoomId::PowerStation, 335.0F) &&
                  demo.update({.interactPressed = true}) && demo.accessBlocked() &&
                  demo.roomId() == echo::RoomId::PowerStation,
              "the shipped bypass door is locked before the survey and emergency power");
        check(place(echo::RoomId::PowerStation, 205.0F) && demo.update({}) &&
                  demo.checkpoint().firedTriggers != 0,
              "the shipped survey trigger records its fact on overlap");
        check(place(echo::RoomId::PowerStation, 335.0F) &&
                  demo.update({.interactPressed = true}) && demo.accessBlocked(),
              "the shipped bypass door still needs emergency power");
        check(integration->togglePowerCircuit("echo:power/cargo_emergency") &&
                  place(echo::RoomId::PowerStation, 335.0F) &&
                  demo.update({.interactPressed = true}) &&
                  demo.roomId() == echo::RoomId::MaintenanceTunnel &&
                  demo.player().position.x == 640.0F,
              "the shipped bypass door arrives at its custom coordinates");
        check(integration->togglePowerCircuit("echo:power/cargo_emergency") &&
                  place(echo::RoomId::MaintenanceTunnel, 225.0F) &&
                  demo.update({.interactPressed = true}) && demo.accessBlocked(),
              "the shipped return door requires Echo Step");
    }
    {
        auto rejected = std::make_unique<echo::platform::FrameworkIntegration>();
        check(initializeWithEditedAsset(
                  "world/rooms/maintenance_tunnel.json", "\"object\": \"echo:object/door_lab\"",
                  "\"object\": \"echo:object/door_lab\", \"properties\": "
                  "{\"requiresCircuit\": \"echo:power/nonexistent\"}",
                  *rejected).code == echo::framework::ErrorCode::MissingDependency,
              "startup rejects a door referencing an unknown circuit");
        auto missingCutscene = std::make_unique<echo::platform::FrameworkIntegration>();
        check(initializeWithEditedAsset(
                  "world/rooms/research_lab.json", "\"object\": \"echo:object/trigger_lab_exit\"",
                  "\"object\": \"echo:object/trigger_lab_exit\", \"properties\": "
                  "{\"cutscene\": \"echo:cutscene/missing\"}",
                  *missingCutscene).code == echo::framework::ErrorCode::MissingDependency,
              "startup rejects a trigger referencing an unknown cutscene");
        std::error_code ignored;
        std::filesystem::remove_all(std::filesystem::temp_directory_path() / "echo-door-fixture",
                                    ignored);
    }
    {
        auto fixtureWorld = std::make_unique<echo::framework::WorldModule>();
        auto fixtureLayout = std::make_unique<echo::WorldLayout>();
        check(loadWorldFixture("echo:object/player_base", "\"bounds\": [20, 28]",
                               "\"bounds\": [16, 24]", *fixtureWorld, *fixtureLayout),
              "player bounds load from the player archetype");
        check(fixtureLayout->playerSize.x == 16.0F && fixtureLayout->playerSize.y == 24.0F &&
                  fixtureLayout->archetypes.playerIso->width() == 16.0F &&
                  fixtureLayout->archetypes.playerIso->height() == 24.0F,
              "isometric appearance inherits the player's collision dimensions");
        echo::Game resized;
        check(resized.configureWorld(*fixtureLayout), "resized player world configures");
        for (int step = 0; step < 60; ++step) check(resized.update({}), "resized player updates");
        check(resized.player().onGround && resized.player().position.y == 296.0F,
              "resized player lands using the configured collision height");
        check(resized.update({.shootHeld = true}) && resized.projectiles()[0].active &&
                  resized.projectiles()[0].bounds.x == resized.player().position.x + 16.0F &&
                  resized.projectiles()[0].bounds.y == resized.player().position.y + 9.0F,
              "resized player fires from the configured collision bounds");
        auto invalidCheckpoint = resized.checkpoint();
        invalidCheckpoint.playerPosition.x = resized.roomWidth() - 15.0F;
        check(!resized.restore(invalidCheckpoint),
              "checkpoint bounds use the configured collision width");
    }
    {
        auto fixtureWorld = std::make_unique<echo::framework::WorldModule>();
        auto fixtureLayout = std::make_unique<echo::WorldLayout>();
        check(loadWorldFixture("echo:object/player_base", "\"facing\": \"right\"",
                               "\"facing\": \"left\"",
                               *fixtureWorld, *fixtureLayout),
              "player facing loads from the archetype");
        echo::Game facing;
        check(facing.configureWorld(*fixtureLayout) && facing.player().facing == -1 &&
                  facing.player().aim.x == -1.0F,
              "new games use the configured player facing and aim");
    }
    {
        auto fixtureWorld = std::make_unique<echo::framework::WorldModule>();
        auto fixtureLayout = std::make_unique<echo::WorldLayout>();
        check(!loadWorldFixture("echo:object/player_iso", "\"sprite\": {",
                                "\"bounds\": [19, 28], \"sprite\": {",
                                *fixtureWorld, *fixtureLayout),
              "inconsistent player bounds across projections are rejected");
    }
    {
        auto fixtureWorld = std::make_unique<echo::framework::WorldModule>();
        auto fixtureLayout = std::make_unique<echo::WorldLayout>();
        check(loadWorldFixture("echo:room/maintenance_tunnel", "\"patrolMax\": 520",
                               "\"patrolMax\": 520, \"chargeSpeed\": 50",
                               *fixtureWorld, *fixtureLayout) &&
                  fixtureLayout->enemies[2].chargeSpeed == 50.0F &&
                  fixtureLayout->enemies[2].detectionRange == 220.0F &&
                  fixtureLayout->roamers[0].detectionRange == 300.0F,
              "enemy behavior properties inherit from archetypes and placement overrides win");
        {
            echo::Game overridden;
            auto checkpoint = overridden.checkpoint();
            checkpoint.room = echo::RoomId::MaintenanceTunnel;
            checkpoint.playerPosition = {340.0F, 292.0F};
            checkpoint.enemyHealth[0] = 0;
            check(overridden.configureWorld(*fixtureLayout) && overridden.restore(checkpoint),
                  "placement override game restores");
            const float start = overridden.enemies()[2].bounds.x;
            bool updated = true;
            for (int step = 0; step < 6; ++step) {
                if (!overridden.update({})) updated = false;
            }
            const float traveled = start - overridden.enemies()[2].bounds.x;
            check(updated && traveled > 4.9F && traveled < 5.1F,
                  "the placement override changes charger movement in the game");
        }
        fixtureWorld->reset();
        check(!loadWorldFixture("echo:object/enemy_charger", "\"chargeSpeed\": 130",
                                "\"chargeSpeed\": \"fast\"", *fixtureWorld, *fixtureLayout),
              "non-numeric charger speed is rejected");
        fixtureWorld->reset();
        check(!loadWorldFixture("echo:object/enemy_stalker", "\"detectionRange\": 300",
                                "\"detectionRange\": 1e100", *fixtureWorld, *fixtureLayout),
              "stalker detection range outside float bounds is rejected");
        fixtureWorld->reset();
        check(!loadWorldFixture("echo:object/enemy_charger", ",\n    \"chargeSpeed\": 130",
                                "", *fixtureWorld, *fixtureLayout),
              "missing charger speed is rejected");
        fixtureWorld->reset();
        check(loadWorldFixture("echo:room/research_lab", "\"patrolMax\": 1100",
                               "\"patrolMax\": 1100, \"detectionRange\": 300, "
                               "\"projectileSpeed\": 60, \"fireInterval\": 0.25",
                               *fixtureWorld, *fixtureLayout) &&
                  fixtureLayout->enemies[4].detectionRange == 300.0F &&
                  fixtureLayout->enemies[4].projectileSpeed == 60.0F &&
                  fixtureLayout->enemies[4].fireInterval == 0.25F,
              "sentry tuning accepts placement overrides");
        {
            echo::Game overridden = game;
            auto checkpoint = overridden.checkpoint();
            checkpoint.room = echo::RoomId::ResearchLab;
            checkpoint.playerPosition = {800.0F, 292.0F};
            checkpoint.questBranch = 1;
            check(checkpoint.inventory.add(echo::ItemId::MaintenanceKey),
                  "sentry override fixture adds the laboratory key");
            check(overridden.configureWorld(*fixtureLayout), "sentry override world configures");
            check(overridden.restore(checkpoint), "sentry override checkpoint restores");
            check(overridden.update({}), "sentry override game updates");
            check(overridden.enemyProjectiles()[0].active &&
                      overridden.enemyProjectiles()[0].velocityX == -60.0F &&
                      overridden.enemies()[4].attackCooldown == 0.25F,
                  "sentry placement overrides affect gameplay");
        }
        fixtureWorld->reset();
        check(!loadWorldFixture("echo:object/enemy_sentry", "\"projectileSpeed\": 220",
                                "\"projectileSpeed\": \"fast\"", *fixtureWorld, *fixtureLayout),
              "non-numeric sentry projectile speed is rejected");
        fixtureWorld->reset();
        check(!loadWorldFixture("echo:object/enemy_sentry", "\"fireInterval\": 1.5",
                                "\"fireInterval\": 0", *fixtureWorld, *fixtureLayout),
              "sentry fire interval below one fixed step is rejected");
        fixtureWorld->reset();
        check(!loadWorldFixture("echo:object/enemy_sentry", ",\n    \"detectionRange\": 420",
                                "", *fixtureWorld, *fixtureLayout),
              "missing sentry detection range is rejected");
        fixtureWorld->reset();
        check(!loadWorldFixture("echo:room/research_lab", "\"patrolMax\": 1100",
                                "\"patrolMax\": 1100, \"projectileSpeed\": -1",
                                *fixtureWorld, *fixtureLayout),
              "invalid sentry placement override is rejected");
        fixtureWorld->reset();
        check(loadWorldFixture("echo:room/power_station", "\"patrolMax\": 650",
                               "\"patrolMax\": 650, \"guardCycle\": 0.5, "
                               "\"guardStart\": 0.1, \"guardReduction\": 2",
                               *fixtureWorld, *fixtureLayout) &&
                  fixtureLayout->enemies[3].guardCycle == 0.5F &&
                  fixtureLayout->enemies[3].guardStart == 0.1F &&
                  fixtureLayout->enemies[3].guardReduction == 2,
              "armored tuning accepts placement overrides");
        {
            echo::Game overridden;
            auto checkpoint = overridden.checkpoint();
            checkpoint.room = echo::RoomId::PowerStation;
            checkpoint.playerPosition = {400.0F, 292.0F};
            check(overridden.configureWorld(*fixtureLayout) && overridden.restore(checkpoint),
                  "armored override fixture restores");
            bool updated = true;
            for (int step = 0; step < 7; ++step) {
                if (!overridden.update({})) updated = false;
            }
            check(updated && overridden.enemies()[3].guarding() &&
                      overridden.enemies()[3].guardReduction == 2,
                  "armored placement overrides affect the runtime guard phase");
        }
        fixtureWorld->reset();
        check(!loadWorldFixture("echo:object/enemy_armored", "\"guardReduction\": 1",
                                "\"guardReduction\": 1.5", *fixtureWorld, *fixtureLayout),
              "fractional armor reduction is rejected");
        fixtureWorld->reset();
        check(!loadWorldFixture("echo:object/enemy_armored", ",\n    \"guardStart\": 2",
                                "", *fixtureWorld, *fixtureLayout),
              "missing armor guard start is rejected");
        fixtureWorld->reset();
        check(!loadWorldFixture("echo:room/power_station", "\"patrolMax\": 650",
                                "\"patrolMax\": 650, \"guardStart\": 3",
                                *fixtureWorld, *fixtureLayout),
              "armor placement override cannot start guarding at cycle end");
    }
    {
        const echo::WorldLayout* loaded = integration->worldLayout();
        const echo::WorldLayout& expected = echo::defaultWorldLayout();
        check(loaded != nullptr && &game.world() == loaded,
              "the selected world module configures the game layout from JSON");
        if (loaded != nullptr) {
            check(loaded->enemies[3].guardCycle == 3.0F &&
                      loaded->enemies[3].guardStart == 2.0F &&
                      loaded->enemies[3].guardReduction == 1,
                  "shipped armor data preserves the original guard behavior");
            echo::Vec2 consoleStart{};
            echo::Vec2 consoleMoved{};
            const auto initialConsolePosition = game.propPosition(
                echo::RoomId::CargoHold, "power_panel", consoleStart);
            const auto moveConsole = game.setPropPosition(
                echo::RoomId::CargoHold, "power_panel",
                {consoleStart.x + 12.0F, consoleStart.y + 8.0F});
            const bool movedConsole = game.propPosition(
                echo::RoomId::CargoHold, "power_panel", consoleMoved);
            const auto resetConsole = game.setPropPosition(
                echo::RoomId::CargoHold, "power_panel", consoleStart);
            check(initialConsolePosition && moveConsole && movedConsole && resetConsole &&
                      consoleMoved.x == consoleStart.x + 12.0F &&
                      consoleMoved.y == consoleStart.y + 8.0F,
                  "runtime prop transforms resolve by stable room-local placement ID");
            check(game.setPropPosition(echo::RoomId::CargoHold, "missing_prop",
                                       consoleStart).code ==
                      echo::framework::ErrorCode::MissingDependency &&
                      game.setPropPosition(echo::RoomId::CargoHold, "power_panel",
                                           {-1.0F, consoleStart.y}).code ==
                          echo::framework::ErrorCode::InvalidValue,
                  "runtime prop transforms reject unknown IDs and out-of-room positions");
            const echo::PropLayout* powerPanel = echo::nearbyPowerPanel(
                *loaded, echo::RoomId::CargoHold, {384.0F, 224.0F});
            check(powerPanel != nullptr &&
                      powerPanel->interaction == "toggle_power" &&
                      powerPanel->circuit == "echo:power/cargo_main",
                  "the Cargo Hold console is an interactable world-space power panel");
            const echo::PropLayout* emergencyPanel = nullptr;
            const echo::PropLayout* cargoTerminal = nullptr;
            for (const echo::PropLayout& prop : loaded->propList()) {
                if (prop.room != echo::RoomId::CargoHold) continue;
                if (std::string_view(prop.id.data()) == "north_panel") {
                    emergencyPanel = &prop;
                } else if (std::string_view(prop.id.data()) == "cargo_terminal") {
                    cargoTerminal = &prop;
                }
            }
            check(emergencyPanel != nullptr &&
                      emergencyPanel->interaction == "toggle_power" &&
                      emergencyPanel->circuit == "echo:power/cargo_emergency" &&
                      cargoTerminal != nullptr &&
                      cargoTerminal->dialogue == "echo:dialogue/cargo_operator" &&
                      cargoTerminal->dialoguePowerCircuit ==
                          "echo:power/cargo_emergency" &&
                      cargoTerminal->dialoguePowerState ==
                          "quest.cargo_emergency_powered",
                  "world data binds the terminal dialogue and its power-state input");
            check(echo::nearbyPowerPanel(*loaded, echo::RoomId::LandingBay,
                                         {384.0F, 224.0F}) == nullptr &&
                      echo::nearbyPowerPanel(*loaded, echo::RoomId::CargoHold,
                                             {0.0F, 0.0F}) == nullptr,
                  "power-panel interaction is limited by room and world-space reach");
            bool roomsMatch = true;
            for (std::size_t index = 0; index < echo::kRoomCount; ++index) {
                const echo::RoomLayout& room = loaded->rooms[index];
                const echo::RoomLayout& reference = expected.rooms[index];
                const auto sameExit = [](const echo::RoomExitLayout& a,
                                         const echo::RoomExitLayout& b) {
                    return a.open == b.open && a.completesGame == b.completesGame &&
                           a.requiresEchoStep == b.requiresEchoStep &&
                           (!a.open || a.completesGame || a.room == b.room);
                };
                const bool expandedCargo = index == static_cast<std::size_t>(echo::RoomId::CargoHold);
                roomsMatch = roomsMatch && room.name == reference.name &&
                             (expandedCargo || (room.columns == reference.columns &&
                                                room.rows == reference.rows &&
                                                room.tiles == reference.tiles)) &&
                             room.background == reference.background &&
                             sameExit(room.left, reference.left) &&
                             sameExit(room.right, reference.right) && room.definition != nullptr;
            }
            check(roomsMatch, "JSON rooms preserve side-view collision, exits and backgrounds");
            const auto& cargoRoom = loaded->rooms[static_cast<std::size_t>(echo::RoomId::CargoHold)];
            check(cargoRoom.isometric && cargoRoom.columns == 96 && cargoRoom.rows == 48 &&
                      loaded->roamerCount == 6,
                  "Cargo Hold loads the requested 96 by 48 isometric level and six stalkers");
            {
                const auto& oldCargo = expected.rooms[static_cast<std::size_t>(echo::RoomId::CargoHold)];
                bool oldFloorPreserved = true;
                for (int row = 0; row < oldCargo.rows; ++row) {
                    for (int column = 0; column < oldCargo.columns; ++column) {
                        const auto index = static_cast<std::size_t>(
                            row * echo::kMaxLayoutColumns + column);
                        if (oldCargo.tiles[index] != echo::Tile::Solid &&
                            cargoRoom.tiles[index] == echo::Tile::Solid) {
                            oldFloorPreserved = false;
                        }
                    }
                }
                check(oldFloorPreserved, "expanded Cargo Hold keeps the original walkable floor open");
            }
            {
                constexpr int width = 96;
                constexpr int height = 48;
                std::array<bool, width * height> reached{};
                std::array<int, width * height> queue{};
                std::size_t front = 0;
                std::size_t back = 0;
                queue[back++] = 5 * width + 94;
                reached[queue[0]] = true;
                while (front < back) {
                    const int cell = queue[front++];
                    const int x = cell % width;
                    const int y = cell / width;
                    for (const auto& [dx, dy] : std::array<std::pair<int, int>, 4>{
                             {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                        const int nx = x + dx;
                        const int ny = y + dy;
                        if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                        const int next = ny * width + nx;
                        if (reached[next] ||
                            cargoRoom.tiles[ny * echo::kMaxLayoutColumns + nx] == echo::Tile::Solid) {
                            continue;
                        }
                        reached[next] = true;
                        queue[back++] = next;
                    }
                }
                bool placementsReachable = true;
                for (const auto& prop : loaded->propList()) {
                    if (prop.room != echo::RoomId::CargoHold) continue;
                    const int x = static_cast<int>(prop.position.x) / echo::kTileSize;
                    const int y = static_cast<int>(prop.position.y) / echo::kTileSize;
                    placementsReachable = placementsReachable && reached[y * width + x];
                }
                for (const auto& roamer : loaded->roamerList()) {
                    const int x = static_cast<int>(roamer.bounds.x) / echo::kTileSize;
                    const int y = static_cast<int>(roamer.bounds.y) / echo::kTileSize;
                    placementsReachable = placementsReachable && reached[y * width + x];
                }
                check(placementsReachable && back > 3900,
                      "Cargo Hold panels, props and stalkers connect to the eastern entrance");
            }
            {
                echo::Game cargoGame = game;
                auto checkpoint = cargoGame.checkpoint();
                checkpoint.room = echo::RoomId::CargoHold;
                checkpoint.playerVelocity = {};
                echo::Vec2 terminalPosition{};
                check(cargoGame.propPosition(echo::RoomId::CargoHold, "cargo_terminal",
                                             terminalPosition),
                      "Cargo Control terminal has a stable world position");
                checkpoint.playerPosition = terminalPosition;
                check(cargoGame.restore(checkpoint),
                      "a checkpoint from the original Cargo Hold remains restorable");
                echo::framework::QuestProgress cargoProgress{};
                check(cargoGame.nearbyDialogueProp() != nullptr &&
                          cargoGame.nearbyDialogueProp()->dialogue ==
                              "echo:dialogue/cargo_operator" &&
                          cargoGame.cargoQuestProgress(cargoProgress) &&
                          cargoProgress.total == 1 && cargoProgress.completed == 0 &&
                          cargoProgress.objective == "RESTORE CARGO EMERGENCY POWER",
                      "Cargo Hold shows its repair task and terminal interaction");
                check(cargoGame.update({.interactPressed = true}) &&
                          cargoGame.dialogueText() == "CARGO HOLD MAINTENANCE ARCHIVE." &&
                          cargoGame.dialogueChoiceCount() == 1 &&
                          cargoGame.dialogueChoiceText(0) == "OPEN REPAIR TASK" &&
                          cargoGame.update({.interactPressed = true}) &&
                          cargoGame.dialogueText() ==
                              "RESTORE EMERGENCY POWER AT THE NORTH PANEL." &&
                          cargoGame.update({.interactPressed = true}) &&
                          !cargoGame.dialogueActive(),
                      "Cargo Control briefs the player through its localized dialogue module");
                check(power->toggle("echo:power/cargo_emergency") &&
                          cargoGame.cargoQuestProgress(cargoProgress) &&
                          cargoProgress.completed == cargoProgress.total,
                      "restoring the emergency circuit completes the Cargo Hold quest");
                check(cargoGame.update({.interactPressed = true}) &&
                          cargoGame.dialogueChoiceCount() == 1 &&
                          cargoGame.dialogueChoiceText(0) == "CHECK POWER STATUS" &&
                          cargoGame.update({.interactPressed = true}) &&
                          cargoGame.dialogueText() ==
                              "EMERGENCY POWER IS STABLE. CARGO HOLD SECURED.",
                      "the terminal reports completion after the saved circuit is restored");
                check(integration->beginGame() &&
                          power->isPowered("echo:power/cargo_emergency", emergencyPower) &&
                          !emergencyPower,
                      "a new game resets the repair task's circuit progress");
                checkpoint.playerPosition = {2500.0F, 1200.0F};
                check(cargoGame.restore(checkpoint),
                      "a checkpoint beyond the old room bounds is restorable");
                echo::framework::GameState saved{};
                echo::GameCheckpoint decoded{};
                check(echo::platform::encodeGameCheckpoint(cargoGame, saved) &&
                          echo::platform::decodeGameCheckpoint(saved, decoded) &&
                          cargoGame.restore(decoded) &&
                          cargoGame.player().position.x == 2500.0F &&
                          cargoGame.player().position.y == 1200.0F,
                      "expanded Cargo Hold positions survive checkpoint encoding and restoration");
                checkpoint.playerPosition = {300.0F, 120.0F};
                checkpoint.activeItem = {echo::ItemId::BlastCharge, 1};
                check(cargoGame.restore(checkpoint), "Cargo Hold blast-charge fixture restores");
                const int nearHealth = cargoGame.roamers()[0].health;
                const int farHealth = cargoGame.roamers()[1].health;
                check(cargoGame.update({.useItemPressed = true}) &&
                          cargoGame.roamers()[0].health == nearHealth - 2 &&
                          cargoGame.roamers()[1].health == farHealth,
                      "blast charge damages nearby stalkers without hitting distant ones");
            }
            {
                echo::Game traveler = game;
                bool updatesOk = true;
                for (int tick = 0; tick < 300 && traveler.roomId() != echo::RoomId::CargoHold;
                     ++tick) {
                    if (!traveler.update({.moveX = -1})) updatesOk = false;
                }
                const bool entered = traveler.roomId() == echo::RoomId::CargoHold &&
                                     traveler.player().position.x > 3000.0F;
                for (int tick = 0; tick < 60 && traveler.roomId() == echo::RoomId::CargoHold;
                     ++tick) {
                    if (!traveler.update({.moveX = 1, .moveY = 1})) updatesOk = false;
                }
                check(updatesOk && entered && traveler.roomId() == echo::RoomId::LandingBay,
                      "the expanded eastern doorway allows entry and return to Landing Bay");
            }
            bool enemiesMatch = true;
            for (std::size_t index = 0; index < echo::kEnemyCount; ++index) {
                const echo::EnemySpawn& enemy = loaded->enemies[index];
                const echo::EnemySpawn& reference = expected.enemies[index];
                enemiesMatch = enemiesMatch && enemy.room == reference.room &&
                               enemy.bounds.x == reference.bounds.x &&
                               enemy.bounds.y == reference.bounds.y &&
                               enemy.bounds.width == reference.bounds.width &&
                               enemy.bounds.height == reference.bounds.height &&
                               enemy.patrolLeft == reference.patrolLeft &&
                               enemy.patrolRight == reference.patrolRight &&
                               enemy.speed == reference.speed && enemy.health == reference.health &&
                               enemy.type == reference.type && enemy.archetype != nullptr;
            }
            check(enemiesMatch, "JSON enemy placements reproduce every save slot");
            // The shipped world adds three demonstration doors/triggers beyond the compiled pair.
            bool doorsMatch = loaded->doorCount == expected.doorCount + 3;
            for (std::size_t index = 0; doorsMatch && index < expected.doorCount; ++index) {
                const echo::DoorLayout& reference = expected.doors[index];
                bool found = false;
                for (std::size_t other = 0; other < loaded->doorCount && !found; ++other) {
                    const echo::DoorLayout& door = loaded->doors[other];
                    found = door.room == reference.room && door.destination == reference.destination &&
                            door.bounds.x == reference.bounds.x && door.bounds.y == reference.bounds.y &&
                            door.bounds.width == reference.bounds.width &&
                            door.bounds.height == reference.bounds.height &&
                            door.arriveFromLeft == reference.arriveFromLeft &&
                            door.requiresKey == reference.requiresKey &&
                            door.visible == reference.visible && door.transitions &&
                            !door.hasActions() && !door.hasArrivePosition;
                }
                doorsMatch = found;
            }
            check(doorsMatch, "JSON doors and triggers reproduce the compiled transitions");
            check(loaded->playerStart.x == expected.playerStart.x &&
                      loaded->playerStart.y == expected.playerStart.y &&
                      loaded->abilityRoom == expected.abilityRoom &&
                      loaded->abilityPickup.x == expected.abilityPickup.x &&
                      loaded->abilityPickup.y == expected.abilityPickup.y &&
                      loaded->abilityPickup.width == expected.abilityPickup.width &&
                      loaded->abilityPickup.height == expected.abilityPickup.height &&
                      loaded->propCount > 0,
                  "JSON player start, pickup and decorative props are loaded");
            check(loaded->engineer.x == 108.0F && loaded->engineer.y == 280.0F &&
                      loaded->engineer.width == 28.0F && loaded->engineer.height == 40.0F &&
                      loaded->engineer.x == expected.engineer.x &&
                      loaded->engineer.y == expected.engineer.y,
                  "the engineer's spawn position and bounds come from echo:object/engineer");
            const echo::framework::ObjectDefinition* heavy =
                loaded->enemies[1].archetype;
            check(heavy != nullptr && heavy->id() == "echo:object/enemy_patrol_heavy" &&
                      std::string_view{heavy->sprite().sheet.data()} == "sprites/enemies/heavy.png" &&
                      heavy->findAnimation("walk") != nullptr &&
                      heavy->findAnimation("walk")->frames == 4 &&
                      heavy->findAnimation("walk")->fps == 6,
                  "the heavy patrol inherits animations from the patrol enemy and overrides its sheet");
        }
    }
    check(game.technicianBounds().x == 108.0F && game.technicianBounds().y == 280.0F,
          "the engineer object's spawn positions her in the game");
    const auto greetingStatus = game.update({.interactPressed = true});
    const auto offerStatus = game.update({.interactPressed = true});
    const auto grantStatus = game.update({.interactPressed = true});
    check(greetingStatus && offerStatus && grantStatus &&
              game.inventory().contains(echo::ItemId::MaintenanceKey),
          "packaged dialogue loads, renders a choice view, and grants the gameplay key");
    if (!greetingStatus) std::cerr << greetingStatus.detail << '\n';
    if (!offerStatus) std::cerr << offerStatus.detail << '\n';
    if (!grantStatus) std::cerr << grantStatus.detail << '\n';
    check(game.update({.interactPressed = true}) && !game.dialogueActive(),
          "the initial key-offer conversation can be closed");
    check(static_cast<bool>(game.update({.moveX = 1})) &&
              static_cast<bool>(game.update({.interactPressed = true})) &&
              game.questBranch() == 2,
          "the integrated quest route is selected before further dialogue");
    check(integration->notifyEchoStepUnlocked(), "ability unlock is mirrored through an action");
    check(integration->unlockEventCount() == 1,
          "ability transition publishes one typed framework event");
    check(integration->notifyEchoStepUnlocked() && integration->unlockEventCount() == 1,
          "repeated state synchronization does not duplicate the unlock event");
    check(game.update({.interactPressed = true}) &&
              game.update({.interactPressed = true}) &&
              game.update({.interactPressed = true}) &&
              game.dialogueChoiceCount() == 2 &&
              game.dialogueChoiceText(0) == "ASK ABOUT ECHO STEP" &&
              game.update({.interactPressed = true}) &&
              game.dialogueText() == "DOUBLE JUMP OPENS THE CENTRAL GATE.",
          "the real ability-unlock projection enables Mira's conditional advice branch");
    check(game.update({.interactPressed = true}) &&
              game.update({.interactPressed = true}) &&
              game.update({.interactPressed = true}) &&
              game.update({.interactPressed = true}) &&
              game.update({.interactPressed = true}) &&
              game.update({.moveX = 1}) &&
              game.update({.interactPressed = true}) &&
              game.update({.interactPressed = true}) &&
              !game.dialogueActive() &&
              game.hasItem(echo::ItemId::KineticCoil) &&
              game.equipmentEquipped("echo:equipment/kinetic_coil"),
          "the integrated item catalog grants Mira's coil and equips it");
    echo::Game baseline;
    const float baselineStartX = baseline.player().position.x;
    const float coilStartX = game.player().position.x;
    for (int tick = 0; tick < 30; ++tick) {
        check(static_cast<bool>(baseline.update({.moveX = 1})) &&
                  static_cast<bool>(game.update({.moveX = 1})),
              "coil movement regression steps succeed");
    }
    check(game.player().position.x - coilStartX >
              baseline.player().position.x - baselineStartX,
          "integrated equipment affects the shared gameplay movement");
    echo::Game restored;
    const echo::GameCheckpoint equipmentCheckpoint = game.checkpoint();
    check(integration->configureGame(restored) && restored.restore(equipmentCheckpoint) &&
              restored.equipmentEquipped("echo:equipment/kinetic_coil"),
          "integrated checkpoints preserve and re-equip the coil");
    echo::GameCheckpoint lootCheckpoint = game.checkpoint();
    lootCheckpoint.room = echo::RoomId::MaintenanceTunnel;
    lootCheckpoint.playerPosition = {100.0F, 292.0F};
    lootCheckpoint.enemyHealth[0] = 0;
    lootCheckpoint.lootDrops[0] = {
        echo::RoomId::MaintenanceTunnel, {100.0F, 304.0F},
        {echo::ItemId::Scrap, 2}, true};
    check(game.restore(lootCheckpoint) && game.update({}) &&
              game.itemQuantity(echo::ItemId::Scrap) == 2 &&
              !game.lootDrops()[0].active,
          "the integrated item catalog accepts collectible enemy Scrap");
    check(integration->beginGame(), "new game resets the temporary framework mirror");
    game.start();
    echo::framework::StateValue abilityState{};
    check(integration->gameState().get(
              {echo::framework::StateScope::Global, {}, "player.echo_step_unlocked"},
              abilityState) &&
              abilityState.type == echo::framework::ValueType::Boolean &&
              !abilityState.boolean,
          "new game clears the previously unlocked ability state");
    check(integration->notifyEchoStepUnlocked() && integration->unlockEventCount() == 2,
          "a later game emits a new ability transition");
    echo::framework::GameState savedState{};
    check(savedState.set(
              {echo::framework::StateScope::Global, {}, "player.echo_step_unlocked"},
              echo::framework::StateValue::fromBoolean(true)),
          "fixture save state records the unlocked ability");
    check(power != nullptr && power->setRequested("echo:power/cargo_main", false) &&
              power->saveState(savedState),
          "fixture save state records persistent circuit requests");
    check(integration->resumeGame(savedState), "framework state restores from a save");
    check(power != nullptr &&
              power->isPowered("echo:power/cargo_main", mainPower) && !mainPower,
          "resuming a save restores the persistent Cargo Hold blackout state");
    check(integration->notifyEchoStepUnlocked() && integration->unlockEventCount() == 2,
          "resuming an unlocked ability does not publish a duplicate transition");
    integration->shutdown();

    if (failures != 0) {
        std::cerr << failures << " framework integration test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PROJECT ECHO framework integration tests passed\n";
    return EXIT_SUCCESS;
}
