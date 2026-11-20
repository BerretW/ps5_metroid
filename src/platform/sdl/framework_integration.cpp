#include "platform/sdl/framework_integration.hpp"

#include "echo/framework/data.hpp"

#include <SDL.h>

#include <array>
#include <string_view>

namespace echo::platform {
namespace {

constexpr std::array<std::string_view, 1> kStateServices{"echo.state"};
constexpr std::array<std::string_view, 1> kProjectDependencies{"echo.state"};
constexpr std::array<std::string_view, 1> kProjectServices{"echo.actions"};
constexpr std::size_t kMaxGameConfigBytes = 16 * 1024;

const framework::ModuleInfo kStateModuleInfo{
    "echo.state", "Echo state service", 1, {}, {}, kStateServices, {}};
const framework::ModuleInfo kProjectModuleInfo{
    "echo.project_echo", "PROJECT ECHO adapter", 1,
    kProjectDependencies, {}, kProjectServices, {}};

} // namespace

const framework::ModuleInfo& framework_detail::StateModule::info() const noexcept {
    return kStateModuleInfo;
}

const framework::ModuleInfo& framework_detail::ProjectEchoModule::info() const noexcept {
    return kProjectModuleInfo;
}

void framework_detail::UnlockListener::onEvent(const AbilityUnlocked& event) noexcept {
    if (std::string_view(event.ability.data()) == "echo_step") {
        ++count_;
    }
}

FrameworkIntegration::~FrameworkIntegration() {
    shutdown();
}

framework::Status FrameworkIntegration::initialize(const char* configPath) noexcept {
    if (initialized_) {
        return {framework::ErrorCode::InvalidState, "echo.project_echo",
                "Framework integration is already initialized."};
    }
    if (configPath == nullptr) {
        return {framework::ErrorCode::InvalidArgument, {}, "Framework game configuration path is required."};
    }
    SDL_RWops* file = SDL_RWFromFile(configPath, "rb");
    if (file == nullptr) {
        return {framework::ErrorCode::NotFound, configPath, "Framework game configuration could not be opened."};
    }
    const Sint64 fileSize = SDL_RWsize(file);
    if (fileSize <= 0 || static_cast<std::uint64_t>(fileSize) > kMaxGameConfigBytes) {
        SDL_RWclose(file);
        return {framework::ErrorCode::CapacityExceeded, configPath,
                "Framework game configuration must be between 1 byte and 16 KiB."};
    }
    const auto size = static_cast<std::size_t>(fileSize);
    auto* data = static_cast<char*>(SDL_malloc(size));
    if (data == nullptr) {
        SDL_RWclose(file);
        return {framework::ErrorCode::CapacityExceeded, configPath,
                "Framework configuration buffer allocation failed."};
    }
    std::size_t bytesRead = 0;
    while (bytesRead < size) {
        const std::size_t count = SDL_RWread(file, data + bytesRead, 1, size - bytesRead);
        if (count == 0) {
            break;
        }
        bytesRead += count;
    }
    const int closeResult = SDL_RWclose(file);
    if (bytesRead != size || closeResult != 0) {
        SDL_free(data);
        return {framework::ErrorCode::StorageFailure, configPath,
                "Framework game configuration read or close failed."};
    }
    framework::JsonDocument document{};
    framework::JsonError jsonError{};
    framework::Status status = framework::JsonDocument::parse(data, size, document, jsonError);
    SDL_free(data);
    if (!status) {
        return status;
    }
    const framework::JsonValue root = document.root();
    std::string_view gameId;
    const framework::JsonValue moduleList = root.find("modules");
    if (!root.isObject() || !root.find("game").string(gameId) ||
        gameId != "echo.project_echo" || !moduleList.isArray() || moduleList.size() > 8) {
        return {framework::ErrorCode::InvalidValue, configPath,
                "Configuration requires game echo.project_echo and a bounded modules array."};
    }
    bool stateSelected = false;
    bool projectSelected = false;
    for (std::size_t index = 0; index < moduleList.size(); ++index) {
        std::string_view moduleId;
        if (!moduleList.at(index).string(moduleId)) {
            return {framework::ErrorCode::InvalidValue, "modules",
                    "Configured module IDs must be strings."};
        }
        if (moduleId != "echo.state" && moduleId != "echo.project_echo") {
            return {framework::ErrorCode::MissingDependency, "modules",
                    "Configured module is not compiled into this application."};
        }
        stateSelected = stateSelected || moduleId == "echo.state";
        projectSelected = projectSelected || moduleId == "echo.project_echo";
        for (std::size_t previous = 0; previous < index; ++previous) {
            std::string_view previousId;
            (void)moduleList.at(previous).string(previousId);
            if (previousId == moduleId) {
                return {framework::ErrorCode::DuplicateId, "modules",
                        "A module is selected more than once in game configuration."};
            }
        }
    }
    if (!stateSelected || !projectSelected) {
        return {framework::ErrorCode::MissingDependency, "echo.project_echo",
                "The game configuration must select the state service and PROJECT ECHO adapter."};
    }
    framework::ContentRegistry content{};
    status = framework::loadResourceManifest(root, content);
    if (!status) {
        return {status.code, "resources", status.detail};
    }
    status = modules_.add(stateModule_, stateSelected);
    if (!status) {
        return status;
    }
    status = modules_.add(projectModule_, projectSelected);
    if (!status) {
        return status;
    }
    framework::ModuleContext context{this};
    status = modules_.initializeAll(context);
    if (!status) {
        modules_.shutdownAll();
        return status;
    }
    status = actions_.addCapability("echo.state.write");
    if (!status) {
        modules_.shutdownAll();
        return status;
    }
    status = actions_.registerAction(
        "echo.state.set_flag", "echo.state.write", &framework::ActionRegistry::setStateFlag,
        &stateModule_.state, true);
    if (!status) {
        modules_.shutdownAll();
        return status;
    }
    status = events_.subscribe(unlockListener_, unlockSubscription_);
    if (!status) {
        modules_.shutdownAll();
        return status;
    }
    initialized_ = true;
    return framework::Status::success();
}

framework::Status FrameworkIntegration::beginGame() noexcept {
    if (!initialized_) {
        return {framework::ErrorCode::InvalidState, "echo.project_echo",
                "Framework integration must initialize before starting a game."};
    }
    actions_.setGameActive(true);
    return stateModule_.state.set(
        {framework::StateScope::Global, {}, "player.echo_step_unlocked"},
        framework::StateValue::fromBoolean(false));
}

framework::Status FrameworkIntegration::notifyEchoStepUnlocked() noexcept {
    if (!initialized_) {
        return {framework::ErrorCode::InvalidState, "echo.project_echo",
                "Framework integration is not initialized."};
    }
    constexpr framework::StateAddress address{
        framework::StateScope::Global, {}, "player.echo_step_unlocked"};
    framework::StateValue current{};
    if (stateModule_.state.get(address, current) &&
        current.type == framework::ValueType::Boolean && current.boolean) {
        return framework::Status::success();
    }

    framework::StateValue key{};
    if (!framework::StateValue::fromString(address.key, key)) {
        return {framework::ErrorCode::InvalidValue, address.key,
                "Ability state key does not fit the core value limit."};
    }
    const std::array<framework::ActionParameter, 2> parameters{{
        {"key", key},
        {"value", framework::StateValue::fromBoolean(true)},
    }};
    const framework::ActionResult action =
        actions_.execute({"echo.state.set_flag", parameters});
    if (!action.status) {
        return action.status;
    }

    const framework::Condition condition = framework::Condition::compare(
        address, framework::CompareOperator::Equal,
        framework::StateValue::fromBoolean(true));
    const framework::ConditionResult evaluated =
        framework::evaluate(condition, stateModule_.state);
    if (!evaluated.status) {
        return evaluated.status;
    }
    if (!evaluated.value) {
        return {framework::ErrorCode::InvalidState, address.key,
                "Ability state condition did not match after the registered action."};
    }

    framework_detail::AbilityUnlocked event{};
    constexpr std::string_view ability = "echo_step";
    for (std::size_t index = 0; index < ability.size(); ++index) {
        event.ability[index] = ability[index];
    }
    return events_.publish(event);
}

void FrameworkIntegration::shutdown() noexcept {
    if (unlockSubscription_.valid()) {
        (void)events_.unsubscribe(unlockSubscription_);
        unlockSubscription_ = {};
    }
    actions_.setGameActive(false);
    modules_.shutdownAll();
    initialized_ = false;
}

std::size_t FrameworkIntegration::unlockEventCount() const noexcept {
    return unlockListener_.count();
}

} // namespace echo::platform
