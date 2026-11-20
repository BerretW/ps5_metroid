#include "echo/framework/data.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

constexpr std::array<std::string_view, 3> kCoreServices{
    "echo.state", "echo.entities", "echo.events"};
constexpr std::array<std::string_view, 1> kDemoRequires{"echo.core"};

class DemoModule final : public echo::framework::IModule {
public:
    DemoModule(const echo::framework::ModuleInfo& metadata,
               std::array<int, 4>& shutdownLog, int& shutdownSize,
               int identity) noexcept
        : metadata_(metadata), shutdownLog_(shutdownLog), shutdownSize_(shutdownSize),
          identity_(identity) {}

    [[nodiscard]] const echo::framework::ModuleInfo& info() const noexcept override {
        return metadata_;
    }
    bool initialize(echo::framework::ModuleContext&) noexcept override { return true; }
    void shutdown() noexcept override { shutdownLog_[shutdownSize_++] = identity_; }

private:
    const echo::framework::ModuleInfo& metadata_;
    std::array<int, 4>& shutdownLog_;
    int& shutdownSize_;
    int identity_{};
};

struct DemoPosition {
    float x{};
    float y{};
};

struct DemoEvent {
    echo::framework::EntityId entity{};
    std::int32_t code{};
};

class DemoListener final : public echo::framework::IEventListener<DemoEvent> {
public:
    void onEvent(const DemoEvent& event) noexcept override {
        received_ = event.code == 42 && event.entity.valid();
    }
    [[nodiscard]] bool received() const noexcept { return received_; }

private:
    bool received_{};
};

[[nodiscard]] bool require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "Framework demo failed: " << message << '\n';
    }
    return condition;
}

[[nodiscard]] bool require(echo::framework::Status status, std::string_view message) {
    return require(static_cast<bool>(status), message);
}

} // namespace

int main() {
    std::ifstream input(ECHO_FRAMEWORK_DEMO_CONFIG, std::ios::binary);
    if (!require(static_cast<bool>(input), "could not read game.json")) {
        return 1;
    }
    const std::string json{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    echo::framework::JsonDocument document{};
    echo::framework::JsonError jsonError{};
    if (!require(echo::framework::JsonDocument::parse(json.data(), json.size(), document, jsonError),
                 "game.json was not valid bounded JSON")) {
        return 1;
    }
    std::string_view gameId;
    if (!require(document.root().find("game").string(gameId) &&
                     gameId == "echo.framework_demo",
                 "game identifier did not match")) {
        return 1;
    }
    echo::framework::ContentRegistry content{};
    if (!require(echo::framework::loadResourceManifest(document.root(), content),
                 "content manifest failed validation")) {
        return 1;
    }

    const echo::framework::ModuleInfo coreInfo{
        "echo.core", "Core services", 1, {}, {}, kCoreServices, {}};
    const echo::framework::ModuleInfo demoInfo{
        "echo.demo", "Demo module", 1, kDemoRequires, {}, {}, {}};
    std::array<int, 4> shutdownLog{};
    int shutdownSize = 0;
    DemoModule core(coreInfo, shutdownLog, shutdownSize, 1);
    DemoModule demo(demoInfo, shutdownLog, shutdownSize, 2);
    echo::framework::ModuleRegistry modules{};
    echo::framework::ModuleContext moduleContext{};
    if (!require(modules.add(demo) && modules.add(core) && modules.initializeAll(moduleContext),
                 "static module dependency resolution failed")) {
        return 1;
    }

    echo::framework::EntityStore<8> entities{};
    echo::framework::ComponentPool<DemoPosition, 8> positions{};
    const echo::framework::EntityId player = entities.create();
    if (!require(player.valid() && positions.set(player, entities, {12.0F, 4.0F}),
                 "entity/component creation failed")) {
        return 1;
    }

    echo::framework::EventBus<> events{};
    DemoListener listener{};
    echo::framework::Subscription eventToken{};
    if (!require(events.subscribe(listener, eventToken) &&
                     events.publish(DemoEvent{player, 42}) && listener.received(),
                 "typed event dispatch failed")) {
        return 1;
    }
    echo::framework::ScopedSubscription subscription(events, eventToken);

    echo::framework::GameState state{};
    echo::framework::ActionRegistry actions{};
    if (!require(actions.addCapability("echo.state.write") &&
                     actions.registerAction("echo.state.set_flag", "echo.state.write",
                                            &echo::framework::ActionRegistry::setStateFlag,
                                            &state),
                 "registered state action setup failed")) {
        return 1;
    }
    actions.setGameActive(true);
    echo::framework::StateValue key{};
    echo::framework::StateValue flag = echo::framework::StateValue::fromBoolean(true);
    if (!require(echo::framework::StateValue::fromString("story.demo_complete", key),
                 "action key could not be created")) {
        return 1;
    }
    const std::array<echo::framework::ActionParameter, 2> parameters{{
        {"key", key}, {"value", flag},
    }};
    const echo::framework::ActionResult action =
        actions.execute({"echo.state.set_flag", parameters});
    if (!require(action.status, "explicit action handler failed")) {
        return 1;
    }

    const echo::framework::Condition completed = echo::framework::Condition::compare(
        {echo::framework::StateScope::Global, {}, "story.demo_complete"},
        echo::framework::CompareOperator::Equal, flag);
    const echo::framework::ConditionResult conditionResult =
        echo::framework::evaluate(completed, state);
    if (!require(conditionResult.status && conditionResult.value,
                 "declarative condition did not match")) {
        return 1;
    }

    constexpr std::array<echo::framework::ModuleVersion, 1> saveModules{{
        {"echo.core", 1, true},
    }};
    std::array<char, echo::framework::kMaxSaveBytes> save{};
    std::size_t saveSize{};
    echo::framework::GameState restored{};
    echo::framework::SaveHeader header{};
    if (!require(echo::framework::SaveCodec::encode(
                     gameId, 1, saveModules, state, save.data(), save.size(), saveSize) &&
                     echo::framework::SaveCodec::decode(
                         save.data(), saveSize, gameId, 1, saveModules, restored, header) &&
                     restored.contains({echo::framework::StateScope::Global, {},
                                        "story.demo_complete"}),
                 "versioned save/restore failed")) {
        return 1;
    }

    modules.shutdownAll();
    if (!require(shutdownLog[0] == 2 && shutdownLog[1] == 1,
                 "modules did not shut down in reverse dependency order")) {
        return 1;
    }
    std::cout << "Headless Echo Framework demonstration passed\n";
    return 0;
}
