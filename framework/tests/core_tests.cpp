#include "echo/framework/core.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
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

struct TestModule final : echo::framework::IModule {
    echo::framework::ModuleInfo metadata{};
    std::array<int, 16>* log{};
    int identity{};
    bool failInitialize{};
    int* logSize{};
    int shutdownCount{};

    explicit TestModule(echo::framework::ModuleInfo moduleInfo,
                        std::array<int, 16>* moduleLog = nullptr,
                        int moduleIdentity = 0, bool fail = false,
                        int* size = nullptr) noexcept
        : metadata(moduleInfo), log(moduleLog), identity(moduleIdentity),
          failInitialize(fail), logSize(size) {}

    [[nodiscard]] const echo::framework::ModuleInfo& info() const noexcept override {
        return metadata;
    }
    bool initialize(echo::framework::ModuleContext&) noexcept override {
        if (log != nullptr && logSize != nullptr) {
            (*log)[(*logSize)++] = identity;
        }
        return !failInitialize;
    }
    void shutdown() noexcept override {
        ++shutdownCount;
        if (log != nullptr && logSize != nullptr) {
            (*log)[(*logSize)++] = -identity;
        }
    }
};

constexpr std::array<std::string_view, 1> kRequiresCore{"echo.core"};
constexpr std::array<std::string_view, 1> kRequiresFirst{"echo.first"};

void testModules() {
    using namespace echo::framework;
    std::array<int, 16> log{};
    int logSize = 0;
    TestModule core{{"echo.core", "Core", 1}, &log, 1, false, &logSize};
    TestModule game{{"echo.game", "Game", 1, kRequiresCore}, &log, 2, false, &logSize};
    ModuleRegistry registry;
    ModuleContext context{};
    check(registry.add(game), "register dependent module");
    check(registry.add(core), "register dependency module");
    check(registry.initializeAll(context), "resolve and initialize dependency order");
    check(logSize == 2 && log[0] == 1 && log[1] == 2,
          "dependencies initialize before dependents");
    registry.shutdownAll();
    check(logSize == 4 && log[2] == -2 && log[3] == -1,
          "modules shut down in reverse dependency order");

    ModuleRegistry missing;
    TestModule needsMissing{{"echo.needs_missing", "Needs missing", 1, kRequiresCore}};
    check(missing.add(needsMissing), "register module with an unresolved dependency");
    check(missing.initializeAll(context).code == ErrorCode::MissingDependency,
          "missing required dependency is rejected");

    constexpr std::array<std::string_view, 1> requiresGame{"echo.cycle_b"};
    constexpr std::array<std::string_view, 1> requiresA{"echo.cycle_a"};
    TestModule cycleA{{"echo.cycle_a", "Cycle A", 1, requiresGame}};
    TestModule cycleB{{"echo.cycle_b", "Cycle B", 1, requiresA}};
    ModuleRegistry cyclic;
    check(cyclic.add(cycleA) && cyclic.add(cycleB), "register cycle fixture");
    check(cyclic.initializeAll(context).code == ErrorCode::DependencyCycle,
          "dependency cycles are rejected");

    ModuleRegistry duplicate;
    TestModule duplicateA{{"echo.same", "First", 1}};
    TestModule duplicateB{{"echo.same", "Second", 1}};
    check(duplicate.add(duplicateA), "register first module ID");
    check(duplicate.add(duplicateB).code == ErrorCode::DuplicateId,
          "duplicate module IDs are rejected");

    ModuleRegistry failedInitialization;
    logSize = 0;
    TestModule first{{"echo.first", "First", 1}, &log, 1, false, &logSize};
    TestModule fails{{"echo.fails", "Fails", 1, kRequiresFirst}, &log, 2, true, &logSize};
    check(failedInitialization.add(first) && failedInitialization.add(fails),
          "register initialization failure fixture");
    check(failedInitialization.initializeAll(context).code == ErrorCode::InitializationFailed,
          "module initialization failure is explicit");
    check(first.shutdownCount == 1 && fails.shutdownCount == 0,
          "failed initialization rolls back prior modules only");
}

struct Position {
    float x{};
    float y{};
};

void testEntities() {
    using namespace echo::framework;
    EntityStore<2> entities;
    ComponentPool<Position, 2> positions;
    const EntityId first = entities.create();
    const EntityId second = entities.create();
    check(entities.isAlive(first) && entities.isAlive(second), "entities are created");
    check(!entities.create().valid(), "entity capacity is bounded");
    check(positions.set(first, entities, {4.0F, 7.0F}), "component attaches to live entity");
    check(positions.get(first, entities) != nullptr &&
              positions.get(first, entities)->x == 4.0F,
          "component query returns attached data");
    check(!positions.set({99, 1}, entities, {}), "invalid entity cannot receive a component");
    check(entities.destroy(first) && !entities.isAlive(first),
          "destroy invalidates entity handle");
    check(positions.get(first, entities) == nullptr, "stale handle cannot query a component");
    const EntityId reused = entities.create();
    check(reused.index == first.index && reused.generation != first.generation,
          "reused entity slot advances its generation");
    check(positions.get(reused, entities) == nullptr, "old component cannot leak to a reused entity");
    check(positions.set(reused, entities, {9.0F, 2.0F}) &&
              positions.remove(reused, entities),
          "component can be attached and removed");
}

struct NumberEvent {
    int value{};
};

struct RecordingListener final : echo::framework::IEventListener<NumberEvent> {
    std::array<int, 16>* log{};
    int* count{};
    echo::framework::EventBus<8, 4, 16>* bus{};
    bool publishNested{};

    RecordingListener(std::array<int, 16>& eventLog, int& eventCount,
                      echo::framework::EventBus<8, 4, 16>& eventBus,
                      bool nested = false) noexcept
        : log(&eventLog), count(&eventCount), bus(&eventBus), publishNested(nested) {}

    void onEvent(const NumberEvent& event) noexcept override {
        (*log)[(*count)++] = event.value;
        if (publishNested && event.value == 1) {
            (void)bus->publish(NumberEvent{2});
        }
    }
};

struct RemovingListener final : echo::framework::IEventListener<NumberEvent> {
    echo::framework::EventBus<8, 4, 16>* bus{};
    echo::framework::Subscription other{};
    explicit RemovingListener(echo::framework::EventBus<8, 4, 16>& eventBus) noexcept
        : bus(&eventBus) {}
    void onEvent(const NumberEvent&) noexcept override {
        (void)bus->unsubscribe(other);
    }
};

struct SaturatingListener final : echo::framework::IEventListener<NumberEvent> {
    echo::framework::EventBus<4, 1, 16>* bus{};
    echo::framework::Status firstNested{};
    echo::framework::Status overflow{};
    int calls{};

    explicit SaturatingListener(echo::framework::EventBus<4, 1, 16>& eventBus) noexcept
        : bus(&eventBus) {}

    void onEvent(const NumberEvent& event) noexcept override {
        ++calls;
        if (event.value == 1) {
            firstNested = bus->publish(NumberEvent{2});
            overflow = bus->publish(NumberEvent{3});
        }
    }
};

void testEvents() {
    using namespace echo::framework;
    EventBus<8, 4, 16> bus;
    std::array<int, 16> log{};
    int count = 0;
    RecordingListener first{log, count, bus, true};
    RecordingListener second{log, count, bus, false};
    Subscription firstToken{};
    Subscription secondToken{};
    check(bus.subscribe(first, firstToken) && bus.subscribe(second, secondToken),
          "typed event listeners subscribe");
    {
        ScopedSubscription guard(bus, firstToken);
        check(bus.publish(NumberEvent{1}), "event is published");
        check(count == 4 && log[0] == 1 && log[1] == 1 && log[2] == 2 && log[3] == 2,
              "nested events are queued and listeners run in subscription order");
    }
    check(!bus.unsubscribe(firstToken), "stale subscription token cannot remove another listener");
    check(bus.unsubscribe(secondToken), "listener explicitly unsubscribes");
    check(bus.publish(NumberEvent{3}), "event with no listeners still dispatches successfully");

    EventBus<8, 4, 16> removingBus;
    RemovingListener remover{removingBus};
    RecordingListener removed{log, count, removingBus, false};
    Subscription removerToken{};
    Subscription removedToken{};
    check(removingBus.subscribe(remover, removerToken) &&
              removingBus.subscribe(removed, removedToken),
          "listeners register for removal-during-dispatch test");
    remover.other = removedToken;
    const int before = count;
    check(removingBus.publish(NumberEvent{9}) && count == before,
          "removing the next listener during dispatch is safe");

    EventBus<8, 4, 16> lifetimeBus;
    int lifetimeCount = 0;
    {
        RecordingListener shortLived{log, lifetimeCount, lifetimeBus};
        Subscription token{};
        check(lifetimeBus.subscribe(shortLived, token), "short-lived listener subscribes");
        ScopedSubscription lifetime(lifetimeBus, token);
        check(lifetimeBus.publish(NumberEvent{4}), "short-lived listener receives its event");
    }
    const int afterDestruction = lifetimeCount;
    check(lifetimeBus.publish(NumberEvent{5}) && lifetimeCount == afterDestruction,
          "scoped subscription removes callbacks before listener destruction");

    EventBus<4, 1, 16> boundedBus;
    SaturatingListener saturating{boundedBus};
    Subscription boundedToken{};
    check(boundedBus.subscribe(saturating, boundedToken), "bounded queue listener subscribes");
    check(boundedBus.publish(NumberEvent{1}) && saturating.firstNested &&
              saturating.overflow.code == ErrorCode::CapacityExceeded && saturating.calls == 2,
          "nested queue overflow is reported without recursion or silent event loss");
}

struct StateObserverContext {
    echo::framework::GameState* state{};
    std::array<int, 8> order{};
    int count{};
};

void observeState(void* raw, const echo::framework::StateChange& change) noexcept {
    auto& context = *static_cast<StateObserverContext*>(raw);
    context.order[context.count++] = change.address.key == "story.first" ? 1 : 2;
    if (change.address.key == "story.first") {
        (void)context.state->set({echo::framework::StateScope::Global, {}, "story.second"},
                                 echo::framework::StateValue::fromBoolean(true));
    }
}

void testStateAndConditions() {
    using namespace echo::framework;
    GameState state;
    const StateAddress unlocked{StateScope::Global, {}, "player.echo_step_unlocked"};
    StateObserverContext observer{&state};
    Subscription token{};
    check(state.addObserver(&observeState, &observer, token), "state observer subscribes");
    check(state.set({StateScope::Global, {}, "story.first"}, StateValue::fromBoolean(true)),
          "namespaced boolean state is stored");
    check(observer.count == 2 && observer.order[0] == 1 && observer.order[1] == 2,
          "nested state changes dispatch deterministically without recursion");
    check(state.set(unlocked, StateValue::fromBoolean(true)), "ability state is stored");

    const EntityId actor{3, 8};
    check(state.set({StateScope::Entity, actor, "actor.level"}, StateValue::fromInteger(4)),
          "entity-scoped state is stored");
    StateValue value{};
    check(state.get({StateScope::Entity, actor, "actor.level"}, value) &&
              value.type == ValueType::Integer && value.integer == 4,
          "typed entity state is retrieved");
    check(!state.contains({StateScope::World, {}, "actor.level"}),
          "state scopes do not collide");
    check(state.set({StateScope::Global, {}, "world.speed"}, StateValue::fromNumber(1.25)),
          "floating point state is stored");
    StateValue defaultValue{};
    check(state.getOr({StateScope::Global, {}, "settings.volume"},
                      StateValue::fromInteger(60), defaultValue) &&
              defaultValue.type == ValueType::Integer && defaultValue.integer == 60,
          "missing state returns an explicit typed default");
    StateValue text{};
    check(StateValue::fromString("saved", text) &&
              state.set({StateScope::World, {}, "world.name"}, text),
          "UTF-8 string state is stored");
    check(!state.set({StateScope::Global, {}, "bad"}, StateValue::fromBoolean(true)),
          "unnamespaced state keys are rejected");
    check(!state.set({StateScope::Global, {}, "world.nan"},
                     StateValue::fromNumber(std::nan(""))),
          "non-finite state values are rejected");
    check(state.removeObserver(token), "state observer unsubscribes");

    const Condition hasAbility = Condition::compare(unlocked, CompareOperator::Equal,
                                                    StateValue::fromBoolean(true));
    const Condition hasFirst = Condition::exists({StateScope::Global, {}, "story.first"});
    const Condition notFound = Condition::exists({StateScope::Global, {}, "story.missing"});
    const std::array<Condition, 2> both{hasAbility, hasFirst};
    const Condition all = Condition::all(both);
    const ConditionResult result = evaluate(all, state);
    check(result.status && result.value, "conditions combine existence and typed comparisons");
    const std::array<Condition, 2> either{notFound, hasFirst};
    const Condition any = Condition::any(either);
    check(evaluate(any, state).status && evaluate(any, state).value,
          "logical OR short-circuits on a matching child");
    const Condition negated = Condition::negate(notFound);
    check(evaluate(negated, state).value, "logical NOT evaluates");
    const Condition invalid = Condition::all({});
    check(evaluate(invalid, state).status.code == ErrorCode::InvalidValue,
          "empty logical groups fail with a diagnostic");
    check(state.erase(unlocked) && !state.contains(unlocked), "state values can be deleted");
}

echo::framework::ActionResult failAction(void*, const echo::framework::ActionRequest&) noexcept {
    return {{echo::framework::ErrorCode::InvalidState, "action.fail", "Handler rejected the action."}, {}, false};
}

void testActions() {
    using namespace echo::framework;
    GameState state;
    ActionRegistry actions;
    check(actions.addCapability("echo.state.write"), "action capability registers");
    check(actions.registerAction("echo.state.set_flag", "echo.state.write",
                                 &ActionRegistry::setStateFlag, &state),
          "core flag action registers");
    check(actions.registerAction("echo.test.fail", "", &failAction, nullptr, false),
          "failure fixture registers");
    StateValue key{};
    StateValue enabled = StateValue::fromBoolean(true);
    check(StateValue::fromString("quest.generator_repaired", key), "action key is built");
    const std::array<ActionParameter, 2> parameters{{
        {"key", key}, {"value", enabled},
    }};
    const ActionRequest request{"echo.state.set_flag", parameters};
    check(actions.execute(request).status.code == ErrorCode::InvalidState,
          "unsafe action is rejected while game is inactive");
    actions.setGameActive(true);
    check(actions.execute(request).status &&
              state.contains({StateScope::Global, {}, "quest.generator_repaired"}),
          "registered action executes through its handler");
    check(actions.execute({"echo.unknown", {}}).status.code == ErrorCode::UnknownAction,
          "unknown actions are reported");
    check(actions.execute({"echo.test.fail", {}}).status.code == ErrorCode::InvalidState,
          "handler failures are returned");
    const std::array<ActionParameter, 1> invalid{{{"key", enabled}}};
    check(actions.execute({"echo.state.set_flag", invalid}).status.code == ErrorCode::InvalidValue,
          "invalid action parameters are rejected");
    const std::array<ActionParameter, 3> duplicated{{
        {"key", key}, {"key", key}, {"value", enabled},
    }};
    check(actions.execute({"echo.state.set_flag", duplicated}).status.code ==
              ErrorCode::InvalidValue,
          "duplicate action parameter names are rejected");

    ActionRegistry noCapability;
    check(noCapability.registerAction("echo.test.capability", "echo.audio.play",
                                      &failAction, nullptr, false),
          "capability-gated action registers without enabling its service");
    check(noCapability.execute({"echo.test.capability", {}}).status.code ==
              ErrorCode::MissingCapability,
          "missing action capability is explicit");
}

} // namespace

int main() {
    testModules();
    testEntities();
    testEvents();
    testStateAndConditions();
    testActions();
    if (failures != 0) {
        std::cerr << failures << " framework core test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Echo Framework core tests passed\n";
    return EXIT_SUCCESS;
}
