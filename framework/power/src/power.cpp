#include "echo/framework/power.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace echo::framework {
namespace {

constexpr std::array<std::string_view, 2> kRequiredDocumentFields{
    "schemaVersion", "circuits"};
constexpr std::array<std::string_view, 2> kDocumentFields{
    "schemaVersion", "circuits"};
constexpr std::array<std::string_view, 2> kRequiredCircuitFields{"id", "defaultOn"};
constexpr std::array<std::string_view, 5> kCircuitFields{
    "id", "defaultOn", "requestedOn", "parent", "backups"};
constexpr std::array<std::string_view, 1> kPowerServices{"echo.service.power"};
constexpr std::array<ManifestModuleDescriptor, 1> kManifestModules{{
    {kPowerModuleId, "Power", 1, {}, {}, kPowerServices, {}},
}};
constexpr std::array<ManifestSchemaDescriptor, 1> kManifestSchemas{{
    {"echo.power_json", 1},
}};
constexpr std::array<ManifestActionParameter, 2> kCircuitSetParameters{{
    {"circuit", "string", true}, {"requested", "boolean", true},
}};
constexpr std::array<ManifestActionParameter, 1> kCircuitParameters{{
    {"circuit", "string", true},
}};
constexpr std::array<ManifestActionDescriptor, 3> kManifestActions{{
    {"echo.power.set", kPowerControlCapability, true, kCircuitSetParameters},
    {"echo.power.toggle", kPowerControlCapability, true, kCircuitParameters},
    {"echo.power.restore", kPowerControlCapability, true, {}},
}};
constexpr std::array<ManifestCapabilityDescriptor, 1> kManifestCapabilities{{
    {kPowerControlCapability},
}};
constexpr std::array<ManifestEventDescriptor, 1> kManifestEvents{{
    {"echo.power.circuit_changed", 1, {}},
}};

[[nodiscard]] bool hasOnlyFields(
    JsonValue object, std::span<const std::string_view> allowed,
    std::span<const std::string_view> required) noexcept {
    if (!object.isObject() || object.size() < required.size() ||
        object.size() > allowed.size()) {
        return false;
    }
    for (std::size_t index = 0; index < object.size(); ++index) {
        std::string_view name;
        JsonValue ignored{};
        if (!object.memberAt(index, name, ignored) ||
            std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
            return false;
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            std::string_view earlier;
            if (!object.memberAt(previous, earlier, ignored) || earlier == name) {
                return false;
            }
        }
    }
    for (const std::string_view name : required) {
        if (!object.find(name).valid()) {
            return false;
        }
    }
    return true;
}

template <std::size_t Size>
[[nodiscard]] bool copyText(std::string_view input,
                            std::array<char, Size>& output) noexcept {
    if (input.empty() || input.size() >= Size ||
        input.find('\0') != std::string_view::npos) {
        return false;
    }
    std::memcpy(output.data(), input.data(), input.size());
    output[input.size()] = '\0';
    return true;
}

[[nodiscard]] bool powerCircuitId(std::string_view id) noexcept {
    return isValidContentId(id) && id.find(":power/") != std::string_view::npos;
}

[[nodiscard]] const ActionParameter* findParameter(
    const ActionRequest& request, std::string_view name) noexcept {
    for (const ActionParameter& parameter : request.parameters) {
        if (parameter.name == name) {
            return &parameter;
        }
    }
    return nullptr;
}

[[nodiscard]] Status validateParameters(
    const ActionRequest& request,
    std::span<const std::string_view> accepted) noexcept {
    for (const ActionParameter& parameter : request.parameters) {
        if (std::find(accepted.begin(), accepted.end(), parameter.name) == accepted.end()) {
            return {ErrorCode::InvalidValue, parameter.name,
                    "Power action contains an unexpected parameter."};
        }
    }
    return Status::success();
}

[[nodiscard]] ActionResult actionFailure(Status status) noexcept {
    return {status, {}, false};
}

} // namespace

Status PowerModule::load(JsonValue document) noexcept {
    if (initialized_) {
        return {ErrorCode::InvalidState, kPowerModuleId,
                "Power circuits cannot be reloaded while the module is initialized."};
    }
    if (!hasOnlyFields(document, kDocumentFields, kRequiredDocumentFields)) {
        return {ErrorCode::InvalidValue, "power",
                "Power content requires exactly schemaVersion and circuits."};
    }
    std::int64_t version{};
    const JsonValue entries = document.find("circuits");
    if (!document.find("schemaVersion").integer(version) || version != 1 ||
        !entries.isArray() || entries.size() == 0 ||
        entries.size() > kMaxPowerCircuits) {
        return {ErrorCode::InvalidValue, "power",
                "Power content requires schema version 1 and 1 to 32 circuits."};
    }

    std::array<Circuit, kMaxPowerCircuits> candidate{};
    const std::size_t count = entries.size();
    for (std::size_t index = 0; index < count; ++index) {
        const JsonValue entry = entries.at(index);
        std::string_view id;
        bool defaultOn{};
        if (!hasOnlyFields(entry, kCircuitFields, kRequiredCircuitFields) ||
            !entry.find("id").string(id) || !powerCircuitId(id) ||
            !entry.find("defaultOn").boolean(defaultOn) ||
            id.size() >= candidate[index].id.size()) {
            return {ErrorCode::InvalidValue, "power.circuit",
                    "Each circuit requires a stable power ID and boolean defaultOn."};
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (std::string_view(candidate[previous].id.data()) == id) {
                return {ErrorCode::DuplicateId, id,
                        "Power circuit IDs must be unique."};
            }
        }
        Circuit& circuit = candidate[index];
        if (!copyText(id, circuit.id)) {
            return {ErrorCode::CapacityExceeded, id,
                    "Power circuit ID exceeds its fixed storage."};
        }
        circuit.defaultOn = defaultOn;
        circuit.requestedOn = defaultOn;

        const JsonValue requested = entry.find("requestedOn");
        if (requested.valid() && !requested.boolean(circuit.requestedOn)) {
            return {ErrorCode::InvalidValue, id,
                    "requestedOn must be a boolean when present."};
        }
        std::string_view parent;
        const JsonValue parentValue = entry.find("parent");
        if (parentValue.valid()) {
            if (!parentValue.string(parent) || !powerCircuitId(parent) ||
                !copyText(parent, circuit.parent)) {
                return {ErrorCode::InvalidValue, id,
                        "parent must be a stable power circuit ID."};
            }
        }
        const JsonValue backups = entry.find("backups");
        if (backups.valid()) {
            if (!backups.isArray() || backups.size() > kMaxPowerBackups) {
                return {ErrorCode::CapacityExceeded, id,
                        "A circuit may list at most four backup dependencies."};
            }
            for (std::size_t backupIndex = 0; backupIndex < backups.size(); ++backupIndex) {
                std::string_view backupId;
                if (!backups.at(backupIndex).string(backupId) ||
                    !powerCircuitId(backupId) ||
                    !copyText(backupId, circuit.backups[backupIndex])) {
                    return {ErrorCode::InvalidValue, id,
                            "Backup dependencies must be stable power circuit IDs."};
                }
                if (backupId == parent && !parent.empty()) {
                    return {ErrorCode::DuplicateId, backupId,
                            "A circuit cannot depend on itself or duplicate its parent."};
                }
                for (std::size_t previous = 0; previous < backupIndex; ++previous) {
                    if (std::string_view(circuit.backups[previous].data()) == backupId) {
                        return {ErrorCode::DuplicateId, backupId,
                                "Backup dependencies must be unique."};
                    }
                }
                ++circuit.backupCount;
            }
        }
    }

    for (std::size_t index = 0; index < count; ++index) {
        const Circuit& circuit = candidate[index];
        if (circuit.parent[0] != '\0' &&
            find(candidate, count, circuit.parent.data()) == count) {
            return {ErrorCode::MissingDependency, circuit.parent.data(),
                    "Power circuit parent does not exist."};
        }
        for (std::size_t backup = 0; backup < circuit.backupCount; ++backup) {
            if (find(candidate, count, circuit.backups[backup].data()) == count) {
                return {ErrorCode::MissingDependency, circuit.backups[backup].data(),
                        "Power circuit backup dependency does not exist."};
            }
        }
    }

    std::array<bool, kMaxPowerCircuits> requested{};
    std::array<bool, kMaxPowerCircuits> supplied{};
    std::array<bool, kMaxPowerCircuits> effective{};
    for (std::size_t index = 0; index < count; ++index) {
        requested[index] = candidate[index].requestedOn;
    }
    const Status resolved = resolve(candidate, count, requested, supplied, effective);
    if (!resolved) {
        return resolved;
    }
    for (std::size_t index = 0; index < count; ++index) {
        candidate[index].supplied = supplied[index];
        candidate[index].effectiveOn = effective[index];
    }
    circuits_ = candidate;
    circuitCount_ = count;
    return Status::success();
}

Status PowerModule::load(const char* data, std::size_t size,
                         JsonError& error) noexcept {
    if (data == nullptr || size == 0) {
        return {ErrorCode::InvalidArgument, "power",
                "Power JSON input is empty."};
    }
    if (size > kMaxPowerJsonBytes) {
        return {ErrorCode::CapacityExceeded, "power",
                "Power JSON is empty or exceeds the 32 KiB limit."};
    }
    JsonDocument document{};
    const Status parsed = JsonDocument::parse(data, size, document, error);
    return parsed ? load(document.root()) : parsed;
}

const ModuleInfo& PowerModule::info() const noexcept {
    static constexpr ModuleInfo moduleInfo{
        kPowerModuleId, "Power", 1, {}, {}, kPowerServices, {}};
    return moduleInfo;
}

bool PowerModule::initialize(ModuleContext&) noexcept {
    initialized_ = circuitCount_ != 0;
    return initialized_;
}

void PowerModule::shutdown() noexcept {
    for (Subscription& subscription : subscriptions_) {
        if (subscription.valid()) {
            (void)events_.unsubscribe(subscription);
            subscription = {};
        }
    }
    initialized_ = false;
    dispatchingChanges_ = false;
}

Status PowerModule::registerActions(ActionRegistry& actions) noexcept {
    if (!initialized_) {
        return {ErrorCode::InvalidState, kPowerModuleId,
                "Power actions require an initialized module."};
    }
    Status status = actions.addCapability(kPowerControlCapability);
    if (!status) return status;
    status = actions.registerAction(
        "echo.power.set", kPowerControlCapability, &PowerModule::setAction, this, true);
    if (!status) return status;
    status = actions.registerAction(
        "echo.power.toggle", kPowerControlCapability, &PowerModule::toggleAction, this, true);
    if (!status) return status;
    return actions.registerAction(
        "echo.power.restore", kPowerControlCapability, &PowerModule::restoreAction,
        this, true);
}

Status PowerModule::setRequested(std::string_view circuitId,
                                bool requestedOn) noexcept {
    if (!initialized_) {
        return {ErrorCode::InvalidState, kPowerModuleId,
                "Power state changes require an initialized module."};
    }
    if (dispatchingChanges_) {
        return {ErrorCode::InvalidState, circuitId,
                "Power state cannot change while circuit events are being dispatched."};
    }
    const std::size_t index = find(circuits_, circuitCount_, circuitId);
    if (index == circuitCount_) {
        return {ErrorCode::NotFound, circuitId, "Power circuit does not exist."};
    }
    std::array<bool, kMaxPowerCircuits> requested{};
    std::array<bool, kMaxPowerCircuits> supplied{};
    std::array<bool, kMaxPowerCircuits> effective{};
    for (std::size_t ordinal = 0; ordinal < circuitCount_; ++ordinal) {
        requested[ordinal] = circuits_[ordinal].requestedOn;
    }
    requested[index] = requestedOn;
    const Status status = resolve(circuits_, circuitCount_, requested, supplied, effective);
    return status ? commit(requested, supplied, effective) : status;
}

Status PowerModule::toggle(std::string_view circuitId) noexcept {
    if (!initialized_) {
        return {ErrorCode::InvalidState, kPowerModuleId,
                "Power state changes require an initialized module."};
    }
    const std::size_t index = find(circuits_, circuitCount_, circuitId);
    if (index == circuitCount_) {
        return {ErrorCode::NotFound, circuitId, "Power circuit does not exist."};
    }
    return setRequested(circuitId, !circuits_[index].requestedOn);
}

Status PowerModule::restore() noexcept {
    std::array<PowerCircuitRequest, kMaxPowerCircuits> defaults{};
    for (std::size_t index = 0; index < circuitCount_; ++index) {
        defaults[index] = {circuits_[index].id.data(), circuits_[index].defaultOn};
    }
    return restore({defaults.data(), circuitCount_});
}

Status PowerModule::restore(
    std::span<const PowerCircuitRequest> requests) noexcept {
    if (!initialized_) {
        return {ErrorCode::InvalidState, kPowerModuleId,
                "Power state restoration requires an initialized module."};
    }
    if (dispatchingChanges_) {
        return {ErrorCode::InvalidState, kPowerModuleId,
                "Power state cannot be restored while circuit events are being dispatched."};
    }
    if (requests.size() > circuitCount_) {
        return {ErrorCode::CapacityExceeded, kPowerModuleId,
                "Restored power state exceeds the configured circuit count."};
    }
    std::array<bool, kMaxPowerCircuits> requested{};
    std::array<bool, kMaxPowerCircuits> supplied{};
    std::array<bool, kMaxPowerCircuits> effective{};
    for (std::size_t index = 0; index < circuitCount_; ++index) {
        requested[index] = circuits_[index].defaultOn;
    }
    for (std::size_t index = 0; index < requests.size(); ++index) {
        const std::size_t circuitIndex =
            find(circuits_, circuitCount_, requests[index].circuitId);
        if (circuitIndex == circuitCount_) {
            return {ErrorCode::NotFound, requests[index].circuitId,
                    "Restored power state references an unknown circuit."};
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (requests[previous].circuitId == requests[index].circuitId) {
                return {ErrorCode::DuplicateId, requests[index].circuitId,
                        "Restored power state contains a duplicate circuit."};
            }
        }
        requested[circuitIndex] = requests[index].requestedOn;
    }
    const Status status = resolve(circuits_, circuitCount_, requested, supplied, effective);
    return status ? commit(requested, supplied, effective) : status;
}

Status PowerModule::saveState(GameState& state, bool useDefaults) const noexcept {
    if (!initialized_) {
        return {ErrorCode::InvalidState, kPowerModuleId,
                "Power state saving requires an initialized module."};
    }
    std::array<std::array<char, 32>, kMaxPowerCircuits> keys{};
    GameState candidate = state;
    for (std::size_t index = 0; index < circuitCount_; ++index) {
        Status status = stateKey(circuits_[index].id.data(), keys[index]);
        if (!status) return status;
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (std::string_view(keys[previous].data()) == keys[index].data()) {
                return {ErrorCode::DuplicateId, circuits_[index].id.data(),
                        "Power circuit IDs collide in their persistent state keys."};
            }
        }
    }
    for (std::size_t index = 0; index < circuitCount_; ++index) {
        const Status status = candidate.set(
            {StateScope::Global, {}, keys[index].data()},
            StateValue::fromBoolean(useDefaults ? circuits_[index].defaultOn
                                                : circuits_[index].requestedOn));
        if (!status) return status;
    }
    return state.replaceValues(candidate);
}

Status PowerModule::restoreState(const GameState& state) noexcept {
    if (!initialized_) {
        return {ErrorCode::InvalidState, kPowerModuleId,
                "Power state restoration requires an initialized module."};
    }
    std::array<PowerCircuitRequest, kMaxPowerCircuits> requests{};
    std::array<std::array<char, 32>, kMaxPowerCircuits> keys{};
    for (std::size_t index = 0; index < circuitCount_; ++index) {
        Status status = stateKey(circuits_[index].id.data(), keys[index]);
        if (!status) return status;
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (std::string_view(keys[previous].data()) == keys[index].data()) {
                return {ErrorCode::DuplicateId, circuits_[index].id.data(),
                        "Power circuit IDs collide in their persistent state keys."};
            }
        }
        requests[index].circuitId = circuits_[index].id.data();
        requests[index].requestedOn = circuits_[index].defaultOn;
        StateValue value{};
        if (state.get({StateScope::Global, {}, keys[index].data()}, value)) {
            if (value.type != ValueType::Boolean) {
                return {ErrorCode::CorruptData, keys[index].data(),
                        "Saved power circuit state must be boolean."};
            }
            requests[index].requestedOn = value.boolean;
        }
    }
    return restore({requests.data(), circuitCount_});
}

Status PowerModule::get(std::string_view circuitId,
                        PowerCircuitView& view) const noexcept {
    if (!initialized_) {
        return {ErrorCode::InvalidState, kPowerModuleId,
                "Power circuit queries require an initialized module."};
    }
    const std::size_t index = find(circuits_, circuitCount_, circuitId);
    if (index == circuitCount_) {
        return {ErrorCode::NotFound, circuitId, "Power circuit does not exist."};
    }
    (void)circuitAt(index, view);
    return Status::success();
}

bool PowerModule::circuitAt(std::size_t index, PowerCircuitView& view) const noexcept {
    if (!initialized_ || index >= circuitCount_) {
        return false;
    }
    const Circuit& circuit = circuits_[index];
    PowerCircuitView candidate{};
    candidate.id = circuit.id.data();
    candidate.parent = circuit.parent.data();
    for (std::size_t backup = 0; backup < circuit.backupCount; ++backup) {
        candidate.backups[backup] = circuit.backups[backup].data();
    }
    candidate.backupCount = circuit.backupCount;
    candidate.defaultOn = circuit.defaultOn;
    candidate.requestedOn = circuit.requestedOn;
    candidate.supplied = circuit.supplied;
    candidate.effectiveOn = circuit.effectiveOn;
    view = candidate;
    return true;
}

Status PowerModule::isSupplied(std::string_view circuitId,
                               bool& supplied) const noexcept {
    PowerCircuitView view{};
    const Status status = get(circuitId, view);
    if (status) supplied = view.supplied;
    return status;
}

Status PowerModule::isPowered(std::string_view circuitId,
                              bool& powered) const noexcept {
    PowerCircuitView view{};
    const Status status = get(circuitId, view);
    if (status) powered = view.effectiveOn;
    return status;
}

Status PowerModule::subscribe(
    IEventListener<PowerChangeEvent>& listener,
    Subscription& subscription) noexcept {
    if (!initialized_) {
        return {ErrorCode::InvalidState, kPowerModuleId,
                "Power event subscriptions require an initialized module."};
    }
    for (std::size_t index = 0; index < subscriptions_.size(); ++index) {
        if (!subscriptions_[index].valid()) {
            const Status status = events_.subscribe(listener, subscription);
            if (!status) return status;
            subscriptions_[index] = subscription;
            return Status::success();
        }
    }
    return {ErrorCode::CapacityExceeded, "power event listener",
            "Power event listener capacity is full."};
}

bool PowerModule::unsubscribe(Subscription subscription) noexcept {
    if (!events_.unsubscribe(subscription)) {
        return false;
    }
    for (Subscription& token : subscriptions_) {
        if (token.slot == subscription.slot &&
            token.generation == subscription.generation) {
            token = {};
            break;
        }
    }
    return true;
}

std::size_t PowerModule::find(
    const std::array<Circuit, kMaxPowerCircuits>& circuits, std::size_t count,
    std::string_view id) noexcept {
    for (std::size_t index = 0; index < count; ++index) {
        if (std::string_view(circuits[index].id.data()) == id) {
            return index;
        }
    }
    return count;
}

Status PowerModule::stateKey(std::string_view id,
                             std::array<char, 32>& key) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char character : id) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    const int written = std::snprintf(key.data(), key.size(), "power.%016llx.on",
                                      static_cast<unsigned long long>(hash));
    if (written <= 0 || static_cast<std::size_t>(written) >= key.size()) {
        return {ErrorCode::CapacityExceeded, id,
                "Power circuit persistent state key could not be encoded."};
    }
    return Status::success();
}

Status PowerModule::resolve(
    const std::array<Circuit, kMaxPowerCircuits>& circuits, std::size_t count,
    const std::array<bool, kMaxPowerCircuits>& requested,
    std::array<bool, kMaxPowerCircuits>& supplied,
    std::array<bool, kMaxPowerCircuits>& effective) noexcept {
    std::array<std::uint8_t, kMaxPowerCircuits> visit{};
    const auto evaluate = [&](auto&& self, std::size_t index) -> Status {
        if (visit[index] == 2) return Status::success();
        if (visit[index] == 1) {
            return {ErrorCode::DependencyCycle, circuits[index].id.data(),
                    "Power circuit dependencies contain a cycle."};
        }
        visit[index] = 1;
        const Circuit& circuit = circuits[index];
        bool hasDependencies = circuit.parent[0] != '\0' || circuit.backupCount != 0;
        bool available = false;
        if (circuit.parent[0] != '\0') {
            const std::size_t parent = find(circuits, count, circuit.parent.data());
            if (parent == count) {
                return {ErrorCode::MissingDependency, circuit.parent.data(),
                        "Power circuit parent does not exist."};
            }
            const Status status = self(self, parent);
            if (!status) return status;
            available = effective[parent];
        }
        for (std::size_t backupIndex = 0;
             backupIndex < circuit.backupCount; ++backupIndex) {
            const std::size_t backup =
                find(circuits, count, circuit.backups[backupIndex].data());
            if (backup == count) {
                return {ErrorCode::MissingDependency,
                        circuit.backups[backupIndex].data(),
                        "Power circuit backup dependency does not exist."};
            }
            const Status status = self(self, backup);
            if (!status) return status;
            if (effective[backup]) available = true;
        }
        supplied[index] = !hasDependencies || available;
        effective[index] = requested[index] && supplied[index];
        visit[index] = 2;
        return Status::success();
    };
    for (std::size_t index = 0; index < count; ++index) {
        const Status status = evaluate(evaluate, index);
        if (!status) return status;
    }
    return Status::success();
}

Status PowerModule::commit(
    const std::array<bool, kMaxPowerCircuits>& requested,
    const std::array<bool, kMaxPowerCircuits>& supplied,
    const std::array<bool, kMaxPowerCircuits>& effective) noexcept {
    std::array<PowerChangeEvent, kMaxPowerCircuits> changes{};
    std::size_t changeCount{};
    for (std::size_t index = 0; index < circuitCount_; ++index) {
        const Circuit& circuit = circuits_[index];
        if (circuit.requestedOn == requested[index] &&
            circuit.supplied == supplied[index] &&
            circuit.effectiveOn == effective[index]) {
            continue;
        }
        PowerChangeEvent& event = changes[changeCount++];
        std::memcpy(event.circuitId.data(), circuit.id.data(), circuit.id.size());
        event.requestedBefore = circuit.requestedOn;
        event.requestedAfter = requested[index];
        event.suppliedBefore = circuit.supplied;
        event.suppliedAfter = supplied[index];
        event.effectiveBefore = circuit.effectiveOn;
        event.effectiveAfter = effective[index];
    }
    for (std::size_t index = 0; index < circuitCount_; ++index) {
        circuits_[index].requestedOn = requested[index];
        circuits_[index].supplied = supplied[index];
        circuits_[index].effectiveOn = effective[index];
    }
    dispatchingChanges_ = true;
    for (std::size_t index = 0; index < changeCount; ++index) {
        const Status status = events_.publish(changes[index]);
        if (!status) {
            dispatchingChanges_ = false;
            return status;
        }
    }
    dispatchingChanges_ = false;
    return Status::success();
}

ActionResult PowerModule::setAction(
    void* context, const ActionRequest& request) noexcept {
    auto* module = static_cast<PowerModule*>(context);
    if (module == nullptr) {
        return actionFailure({ErrorCode::InvalidArgument, request.id,
                              "Power module is unavailable."});
    }
    constexpr std::array<std::string_view, 2> accepted{"circuit", "requested"};
    const Status parameters = validateParameters(request, accepted);
    const ActionParameter* circuit = findParameter(request, "circuit");
    const ActionParameter* requested = findParameter(request, "requested");
    if (!parameters) return actionFailure(parameters);
    if (request.parameters.size() != 2 || circuit == nullptr || requested == nullptr ||
        circuit->value.type != ValueType::String ||
        requested->value.type != ValueType::Boolean) {
        return actionFailure({ErrorCode::InvalidValue, request.id,
                              "echo.power.set requires string circuit and boolean requested."});
    }
    return actionFailure(module->setRequested(
        circuit->value.stringValue(), requested->value.boolean));
}

ActionResult PowerModule::toggleAction(
    void* context, const ActionRequest& request) noexcept {
    auto* module = static_cast<PowerModule*>(context);
    if (module == nullptr) {
        return actionFailure({ErrorCode::InvalidArgument, request.id,
                              "Power module is unavailable."});
    }
    constexpr std::array<std::string_view, 1> accepted{"circuit"};
    const Status parameters = validateParameters(request, accepted);
    const ActionParameter* circuit = findParameter(request, "circuit");
    if (!parameters) return actionFailure(parameters);
    if (request.parameters.size() != 1 || circuit == nullptr ||
        circuit->value.type != ValueType::String) {
        return actionFailure({ErrorCode::InvalidValue, request.id,
                              "echo.power.toggle requires a string circuit."});
    }
    return actionFailure(module->toggle(circuit->value.stringValue()));
}

ActionResult PowerModule::restoreAction(
    void* context, const ActionRequest& request) noexcept {
    auto* module = static_cast<PowerModule*>(context);
    if (module == nullptr) {
        return actionFailure({ErrorCode::InvalidArgument, request.id,
                              "Power module is unavailable."});
    }
    if (!request.parameters.empty()) {
        return actionFailure({ErrorCode::InvalidValue, request.id,
                              "echo.power.restore does not accept parameters."});
    }
    return actionFailure(module->restore());
}

CapabilityManifestFragment powerCapabilityManifestFragment() noexcept {
    return {.modules = kManifestModules,
            .capabilities = kManifestCapabilities,
            .actions = kManifestActions,
            .events = kManifestEvents,
            .contentSchemas = kManifestSchemas};
}

} // namespace echo::framework
