#include "echo/framework/power.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
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

constexpr char kPowerJson[] = R"({
    "schemaVersion": 1,
    "circuits": [
        {"id":"echo:power/grid","defaultOn":true},
        {"id":"echo:power/backup","defaultOn":false},
        {"id":"echo:power/primary","defaultOn":true,"parent":"echo:power/grid"},
        {"id":"echo:power/door","defaultOn":true,"parent":"echo:power/primary",
         "backups":["echo:power/backup"]}
    ]
})";

class PowerListener final : public echo::framework::IEventListener<
                                echo::framework::PowerChangeEvent> {
public:
    explicit PowerListener(echo::framework::PowerModule* module = nullptr) noexcept
        : module_(module) {}

    void onEvent(const echo::framework::PowerChangeEvent& event) noexcept override {
        if (count < ids.size()) {
            std::memcpy(ids[count].data(), event.circuitId.data(), event.circuitId.size());
            requestedBefore[count] = event.requestedBefore;
            requestedAfter[count] = event.requestedAfter;
            effectiveBefore[count] = event.effectiveBefore;
            effectiveAfter[count] = event.effectiveAfter;
        }
        ++count;
        if (module_ != nullptr && !reentryChecked) {
            reentryChecked = true;
            reentryStatus = module_->toggle(event.circuitId.data());
        }
    }

    std::array<std::array<char, 96>, 16> ids{};
    std::array<bool, 16> requestedBefore{};
    std::array<bool, 16> requestedAfter{};
    std::array<bool, 16> effectiveBefore{};
    std::array<bool, 16> effectiveAfter{};
    std::size_t count{};
    bool reentryChecked{};
    echo::framework::Status reentryStatus{};

private:
    echo::framework::PowerModule* module_{};
};

void testPowerResolutionAndEvents() {
    using namespace echo::framework;
    PowerModule power;
    JsonError error{};
    check(power.load(kPowerJson, sizeof(kPowerJson) - 1, error),
          "versioned power document loads");
    check(power.info().id == "echo.power" && power.info().version == 1 &&
              power.info().provides.size() == 1 &&
              power.info().provides[0] == "echo.service.power",
          "power module exposes stable metadata and service");
    ModuleContext context{};
    check(power.initialize(context), "loaded power circuits initialize");

    PowerCircuitView door{};
    check(power.get("echo:power/door", door) && door.defaultOn &&
              door.requestedOn && door.supplied && door.effectiveOn &&
              door.parent == "echo:power/primary" && door.backupCount == 1 &&
              door.backups[0] == "echo:power/backup",
          "circuit view exposes configured and resolved state");

    PowerListener listener{&power};
    Subscription subscription{};
    check(power.subscribe(listener, subscription), "bounded power listener subscribes");
    check(power.setRequested("echo:power/grid", false),
          "primary supply can be disabled");
    check(listener.count == 3, "dependency changes publish all circuit events");
    check(std::string_view(listener.ids[0].data()) == "echo:power/grid" &&
              std::string_view(listener.ids[1].data()) == "echo:power/primary" &&
              std::string_view(listener.ids[2].data()) == "echo:power/door",
          "dependency change events preserve configuration order");
    check(listener.reentryChecked &&
              listener.reentryStatus.code == ErrorCode::InvalidState,
          "power mutations during event dispatch are rejected");
    bool supplied{};
    bool powered{true};
    check(power.isSupplied("echo:power/door", supplied) && !supplied &&
              power.isPowered("echo:power/door", powered) && !powered,
          "condition helpers report unavailable primary and backup supply");

    check(power.toggle("echo:power/backup"),
          "backup circuit can be toggled on");
    check(power.isSupplied("echo:power/door", supplied) && supplied &&
              power.isPowered("echo:power/door", powered) && powered,
          "backup dependency supplies a circuit when its parent is off");
    check(power.setRequested("echo:power/backup", false) &&
              power.isPowered("echo:power/door", powered) && !powered,
          "turning off the final supply disables dependent circuits");

    const std::array<PowerCircuitRequest, 2> restored{{
        {"echo:power/grid", false}, {"echo:power/backup", true},
    }};
    check(power.restore(restored) &&
              power.isPowered("echo:power/door", powered) && powered,
          "restore applies saved requested values and defaults omitted circuits");
    const std::array<PowerCircuitRequest, 2> duplicate{{
        {"echo:power/grid", true}, {"echo:power/grid", false},
    }};
    check(power.restore(duplicate).code == ErrorCode::DuplicateId &&
              power.isPowered("echo:power/door", powered) && powered,
          "invalid restore is rejected without partial state changes");
    const std::array<PowerCircuitRequest, 1> unknown{{
        {"echo:power/missing", true},
    }};
    check(power.restore(unknown).code == ErrorCode::NotFound,
          "restore rejects unknown circuit references");
    check(power.restore() && power.isPowered("echo:power/door", powered) && powered,
          "default restore resets requested state and resolves dependencies");
    check(power.unsubscribe(subscription) && !power.unsubscribe(subscription),
          "power event subscription can be released exactly once");
    power.shutdown();
    check(!power.initialized(), "power module shuts down");
}

void testPowerActions() {
    using namespace echo::framework;
    PowerModule power;
    JsonError error{};
    check(power.load(kPowerJson, sizeof(kPowerJson) - 1, error),
          "action fixture loads");
    ModuleContext context{};
    check(power.initialize(context), "action fixture initializes");
    ActionRegistry actions;
    check(power.registerActions(actions), "power actions register with a capability");
    StateValue circuit{};
    check(StateValue::fromString("echo:power/grid", circuit),
          "action circuit parameter is constructed");
    const std::array<ActionParameter, 2> setParameters{{
        {"circuit", circuit}, {"requested", StateValue::fromBoolean(false)},
    }};
    check(actions.execute({"echo.power.set", setParameters}).status.code ==
              ErrorCode::InvalidState,
          "power actions require an active game");
    actions.setGameActive(true);
    check(actions.execute({"echo.power.set", setParameters}).status,
          "namespaced power.set action changes requested state");
    const std::array<ActionParameter, 1> toggleParameters{{{"circuit", circuit}}};
    check(actions.execute({"echo.power.toggle", toggleParameters}).status,
          "namespaced power.toggle action changes requested state");
    check(actions.execute({"echo.power.restore", {}}).status,
          "namespaced power.restore action resets defaults");

    const std::array<ActionParameter, 1> extraParameters{{
        {"unexpected", StateValue::fromBoolean(true)},
    }};
    check(actions.execute({"echo.power.toggle", extraParameters}).status.code ==
              ErrorCode::InvalidValue,
          "power actions reject unexpected parameters");
    power.shutdown();
}

void testPowerPersistence() {
    using namespace echo::framework;
    PowerModule source;
    JsonError error{};
    check(source.load(kPowerJson, sizeof(kPowerJson) - 1, error),
          "persistent power fixture loads");
    ModuleContext context{};
    check(source.initialize(context), "persistent power fixture initializes");
    check(source.setRequested("echo:power/grid", false) &&
              source.setRequested("echo:power/backup", true),
          "runtime requested states change before saving");
    GameState saved{};
    check(source.saveState(saved), "requested circuit states encode into GameState");

    PowerModule restored;
    check(restored.load(kPowerJson, sizeof(kPowerJson) - 1, error) &&
              restored.initialize(context) && restored.restoreState(saved),
          "saved power state restores against stable circuit IDs");
    bool powered{};
    check(restored.isPowered("echo:power/door", powered) && powered,
          "persistent backup state powers dependent circuits after reload");
    GameState empty{};
    check(restored.restoreState(empty) &&
              restored.isPowered("echo:power/door", powered) && powered,
          "missing saved circuit keys use content defaults");

    GameState malformed{};
    const Status keyStatus = malformed.set(
        {StateScope::Global, {}, "power.0000000000000000.on"},
        StateValue::fromInteger(1));
    check(keyStatus, "malformed fixture state can be populated");
    check(restored.restoreState(malformed),
          "unrelated or unknown state keys are ignored");
    source.shutdown();
    restored.shutdown();
}

void testInvalidPowerDocuments() {
    using namespace echo::framework;
    const auto load = [](std::string_view json) {
        PowerModule power;
        JsonError error{};
        return power.load(json.data(), json.size(), error);
    };
    check(load(R"({"schemaVersion":2,"circuits":[{"id":"echo:power/a","defaultOn":true}]})")
              .code == ErrorCode::InvalidValue,
          "unsupported power schema versions are rejected");
    check(load(R"({"schemaVersion":1,"circuits":[{"id":"echo:power/a","defaultOn":true,"extra":1}]})")
              .code == ErrorCode::InvalidValue,
          "unknown circuit fields are rejected");
    check(load(R"({"schemaVersion":1,"circuits":[{"id":"echo:power/a","defaultOn":true},{"id":"echo:power/a","defaultOn":false}]})")
              .code == ErrorCode::DuplicateId,
          "duplicate stable circuit IDs are rejected");
    check(load(R"({"schemaVersion":1,"circuits":[{"id":"echo:power/a","defaultOn":true,"parent":"echo:power/missing"}]})")
              .code == ErrorCode::MissingDependency,
          "unknown parent dependencies are rejected");
    check(load(R"({"schemaVersion":1,"circuits":[{"id":"echo:power/a","defaultOn":true,"backups":["echo:power/missing"]}]})")
              .code == ErrorCode::MissingDependency,
          "unknown backup dependencies are rejected");
    check(load(R"({"schemaVersion":1,"circuits":[{"id":"echo:power/a","defaultOn":true,"parent":"echo:power/b"},{"id":"echo:power/b","defaultOn":true,"parent":"echo:power/a"}]})")
              .code == ErrorCode::DependencyCycle,
          "parent dependency cycles are rejected");
    check(load(R"({"schemaVersion":1,"circuits":[{"id":"echo:power/a","defaultOn":true},{"id":"echo:power/b","defaultOn":true,"backups":["echo:power/a"]},{"id":"echo:power/c","defaultOn":true,"backups":["echo:power/b"]}]})"),
          "acyclic backup dependencies load");
    check(load(R"({"schemaVersion":1,"circuits":[{"id":"echo:power/a","defaultOn":true,"backups":["echo:power/b"]},{"id":"echo:power/b","defaultOn":true,"backups":["echo:power/a"]}]})")
              .code == ErrorCode::DependencyCycle,
          "backup dependency cycles are rejected even when not selected");
    check(load(R"({"schemaVersion":1,"circuits":[{"id":"echo:power/a","defaultOn":true,"requestedOn":"yes"}]})")
              .code == ErrorCode::InvalidValue,
          "requested state must be boolean");
    check(load(R"({"schemaVersion":1,"circuits":[{"id":"echo:item/not_power","defaultOn":true}]})")
              .code == ErrorCode::InvalidValue,
          "circuit IDs must use the stable power content namespace");
}

} // namespace

int main() {
    testPowerResolutionAndEvents();
    testPowerActions();
    testPowerPersistence();
    testInvalidPowerDocuments();
    if (failures != 0) {
        std::cerr << failures << " framework power test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Echo Framework power tests passed\n";
    return EXIT_SUCCESS;
}
