#include "platform/sdl/framework_integration.hpp"

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
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
    {
        std::ofstream fixture(fixturePath, std::ios::binary);
        fixture << R"({"game":"echo.project_echo","modules":["echo.state"],"resources":[]})";
        fixture.close();
        check(static_cast<bool>(fixture), "invalid configuration fixture is written");
    }
    {
        echo::platform::FrameworkIntegration missingModule;
        const auto status = missingModule.initialize(fixturePath);
        check(status.code == echo::framework::ErrorCode::MissingDependency &&
                  status.subject == "echo.project_echo",
              "single-module configuration rejects the missing PROJECT ECHO adapter");
    }
    check(std::remove(fixturePath) == 0, "configuration fixture is removed");
    echo::platform::FrameworkIntegration integration;
    check(integration.initialize("missing-framework-config.json").code ==
              echo::framework::ErrorCode::NotFound,
          "missing game configuration is reported");
    check(integration.initialize(ECHO_SOURCE_ASSETS "framework/game.json"),
          "PROJECT ECHO module configuration initializes");
    check(integration.beginGame(), "framework state resets at a new game");
    check(integration.notifyEchoStepUnlocked(), "ability unlock is mirrored through an action");
    check(integration.unlockEventCount() == 1,
          "ability transition publishes one typed framework event");
    check(integration.notifyEchoStepUnlocked() && integration.unlockEventCount() == 1,
          "repeated state synchronization does not duplicate the unlock event");
    check(integration.beginGame(), "new game resets the temporary framework mirror");
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
