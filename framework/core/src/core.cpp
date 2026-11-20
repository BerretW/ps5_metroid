#include "echo/framework/core.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace echo::framework {
namespace {

[[nodiscard]] bool equals(std::string_view left, std::string_view right) noexcept {
    return left == right;
}

void copyText(std::array<char, 64>& destination, std::string_view source) noexcept {
    destination.fill('\0');
    if (!source.empty()) {
        std::memcpy(destination.data(), source.data(), source.size());
    }
}
[[nodiscard]] bool validParameterName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 63) {
        return false;
    }
    for (const char character : name) {
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '_')) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool isNumeric(ValueType type) noexcept {
    return type == ValueType::Integer || type == ValueType::Number;
}

[[nodiscard]] double numericValue(const StateValue& value) noexcept {
    return value.type == ValueType::Integer ? static_cast<double>(value.integer) : value.number;
}

[[nodiscard]] ConditionResult evaluateAt(const Condition& condition, const GameState& state,
                                         std::size_t depth) noexcept {
    if (depth >= 32) {
        return {{ErrorCode::InvalidValue, condition.address.key, "Condition nesting exceeds 32 levels."}, false};
    }

    if (condition.kind == ConditionKind::Exists || condition.kind == ConditionKind::Compare) {
        const bool validEntityScope = condition.address.scope == StateScope::Entity
                                          ? condition.address.entity.valid()
                                          : !condition.address.entity.valid();
        if (!validNamespaceKey(condition.address.key) || !validEntityScope ||
            (condition.kind == ConditionKind::Compare && !condition.value.valid())) {
            return {{ErrorCode::InvalidValue, condition.address.key,
                     "Condition state address or comparison value is invalid."}, false};
        }
    }

    switch (condition.kind) {
    case ConditionKind::Exists:
        return {Status::success(), state.contains(condition.address)};
    case ConditionKind::Compare: {
        StateValue actual{};
        if (!state.get(condition.address, actual)) {
            return {Status::success(), false};
        }
        bool result = false;
        if (condition.comparison == CompareOperator::Equal ||
            condition.comparison == CompareOperator::NotEqual) {
            const bool equal = actual.equals(condition.value);
            result = condition.comparison == CompareOperator::Equal ? equal : !equal;
            return {Status::success(), result};
        }
        if (!isNumeric(actual.type) || !isNumeric(condition.value.type)) {
            return {{ErrorCode::InvalidValue, condition.address.key,
                     "Ordering comparisons require integer or numeric values."}, false};
        }
        const double left = numericValue(actual);
        const double right = numericValue(condition.value);
        switch (condition.comparison) {
        case CompareOperator::Less: result = left < right; break;
        case CompareOperator::LessOrEqual: result = left <= right; break;
        case CompareOperator::Greater: result = left > right; break;
        case CompareOperator::GreaterOrEqual: result = left >= right; break;
        case CompareOperator::Equal:
        case CompareOperator::NotEqual:
            break;
        }
        return {Status::success(), result};
    }
    case ConditionKind::And:
    case ConditionKind::Or: {
        if (condition.children.empty() || condition.children.size() > 32) {
            return {{ErrorCode::InvalidValue, condition.address.key,
                     "Logical groups require 1 to 32 child conditions."}, false};
        }
        bool value = condition.kind == ConditionKind::And;
        for (const Condition& child : condition.children) {
            const ConditionResult childResult = evaluateAt(child, state, depth + 1);
            if (!childResult.status) {
                return childResult;
            }
            if (condition.kind == ConditionKind::And) {
                value = value && childResult.value;
                if (!value) {
                    break;
                }
            } else {
                value = value || childResult.value;
                if (value) {
                    break;
                }
            }
        }
        return {Status::success(), value};
    }
    case ConditionKind::Not:
        if (condition.children.size() != 1) {
            return {{ErrorCode::InvalidValue, condition.address.key,
                     "NOT requires exactly one child condition."}, false};
        } else {
            const ConditionResult child = evaluateAt(condition.children.front(), state, depth + 1);
            return child.status ? ConditionResult{Status::success(), !child.value} : child;
        }
    }
    return {{ErrorCode::InvalidValue, condition.address.key, "Unknown condition kind."}, false};
}

} // namespace

Status ModuleRegistry::add(IModule& module, bool selected) noexcept {
    const ModuleInfo& info = module.info();
    if (info.id.empty() || info.version == 0) {
        return {ErrorCode::InvalidArgument, info.id, "Module ID and non-zero version are required."};
    }
    if (info.id.size() > 63 || !validNamespaceKey(info.id)) {
        return {ErrorCode::InvalidValue, info.id, "Module IDs must be namespaced and at most 63 bytes."};
    }
    if (find(info.id) >= 0) {
        return {ErrorCode::DuplicateId, info.id, "A module with this ID is already registered."};
    }
    if (count_ == slots_.size()) {
        return {ErrorCode::CapacityExceeded, info.id, "Module registry capacity is full."};
    }
    slots_[count_++] = {&module, selected, false, 0};
    return Status::success();
}

Status ModuleRegistry::initializeAll(ModuleContext& context) noexcept {
    if (initializedCount_ != 0) {
        return {ErrorCode::InvalidState, {}, "Modules are already initialized."};
    }
    orderCount_ = 0;
    for (Slot& slot : slots_) {
        slot.visit = 0;
        slot.initialized = false;
    }
    for (std::size_t moduleIndex = 0; moduleIndex < count_; ++moduleIndex) {
        if (!slots_[moduleIndex].selected) {
            continue;
        }
        const auto services = slots_[moduleIndex].module->info().provides;
        for (std::size_t serviceIndex = 0; serviceIndex < services.size(); ++serviceIndex) {
            if (!validNamespaceKey(services[serviceIndex])) {
                return {ErrorCode::InvalidValue, services[serviceIndex],
                        "Provided service IDs must be namespaced."};
            }
            for (std::size_t previousModule = 0; previousModule <= moduleIndex; ++previousModule) {
                if (!slots_[previousModule].selected) {
                    continue;
                }
                const auto previousServices = slots_[previousModule].module->info().provides;
                const std::size_t limit = previousModule == moduleIndex ? serviceIndex
                                                                         : previousServices.size();
                for (std::size_t previousService = 0; previousService < limit; ++previousService) {
                    if (services[serviceIndex] == previousServices[previousService]) {
                        return {ErrorCode::DuplicateId, services[serviceIndex],
                                "A selected service has more than one provider."};
                    }
                }
            }
        }
    }
    for (std::size_t index = 0; index < count_; ++index) {
        if (!slots_[index].selected) {
            continue;
        }
        const Status status = visit(index);
        if (!status) {
            orderCount_ = 0;
            return status;
        }
    }

    for (std::size_t orderIndex = 0; orderIndex < orderCount_; ++orderIndex) {
        Slot& slot = slots_[order_[orderIndex]];
        if (!slot.module->initialize(context)) {
            const std::string_view moduleId = slot.module->info().id;
            shutdownAll();
            return {ErrorCode::InitializationFailed, moduleId, "Module initialization returned false."};
        }
        slot.initialized = true;
        ++initializedCount_;
    }
    return Status::success();
}

void ModuleRegistry::shutdownAll() noexcept {
    for (std::size_t index = orderCount_; index > 0 && initializedCount_ != 0; --index) {
        const std::size_t moduleIndex = order_[index - 1];
        Slot& slot = slots_[moduleIndex];
        if (slot.initialized) {
            slot.module->shutdown();
            slot.initialized = false;
            --initializedCount_;
        }
    }
    initializedCount_ = 0;
}

int ModuleRegistry::find(std::string_view id) const noexcept {
    for (std::size_t index = 0; index < count_; ++index) {
        if (slots_[index].module->info().id == id) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

Status ModuleRegistry::visit(std::size_t index) noexcept {
    Slot& slot = slots_[index];
    if (slot.visit == 2) {
        return Status::success();
    }
    if (slot.visit == 1) {
        return {ErrorCode::DependencyCycle, slot.module->info().id, "Module dependency cycle detected."};
    }
    slot.visit = 1;
    const ModuleInfo& info = slot.module->info();
    Status status = visitDependencies(index, info.required, false);
    if (!status) {
        return status;
    }
    status = visitDependencies(index, info.initializeAfter, false);
    if (!status) {
        return status;
    }
    status = visitDependencies(index, info.optional, true);
    if (!status) {
        return status;
    }
    slot.visit = 2;
    order_[orderCount_++] = index;
    return Status::success();
}

Status ModuleRegistry::visitDependencies(std::size_t index,
                                         std::span<const std::string_view> dependencies,
                                         bool optional) noexcept {
    for (const std::string_view dependency : dependencies) {
        const int found = find(dependency);
        if (found < 0 || !slots_[static_cast<std::size_t>(found)].selected) {
            if (optional) {
                continue;
            }
            return {ErrorCode::MissingDependency, dependency,
                    "A selected module requires a missing or disabled module."};
        }
        const Status status = visit(static_cast<std::size_t>(found));
        if (!status) {
            return status;
        }
        if (slots_[static_cast<std::size_t>(found)].visit == 1) {
            return {ErrorCode::DependencyCycle, slots_[index].module->info().id,
                    "Module dependency cycle detected."};
        }
    }
    return Status::success();
}

StateValue StateValue::fromBoolean(bool value) noexcept {
    StateValue output{};
    output.type = ValueType::Boolean;
    output.boolean = value;
    return output;
}

StateValue StateValue::fromInteger(std::int64_t value) noexcept {
    StateValue output{};
    output.type = ValueType::Integer;
    output.integer = value;
    return output;
}

StateValue StateValue::fromNumber(double value) noexcept {
    StateValue output{};
    output.type = ValueType::Number;
    output.number = value;
    return output;
}

bool StateValue::fromString(std::string_view value, StateValue& output) noexcept {
    if (value.size() >= output.string.size() || !validUtf8(value)) {
        return false;
    }
    output = {};
    output.type = ValueType::String;
    std::memcpy(output.string.data(), value.data(), value.size());
    output.string[value.size()] = '\0';
    return true;
}

bool StateValue::valid() const noexcept {
    switch (type) {
    case ValueType::Boolean:
    case ValueType::Integer:
        return true;
    case ValueType::Number:
        return std::isfinite(number);
    case ValueType::String:
        return stringValue().size() < string.size() && validUtf8(stringValue());
    }
    return false;
}

bool StateValue::equals(const StateValue& other) const noexcept {
    if (type != other.type) {
        return false;
    }
    switch (type) {
    case ValueType::Boolean: return boolean == other.boolean;
    case ValueType::Integer: return integer == other.integer;
    case ValueType::Number: return number == other.number;
    case ValueType::String: return stringValue() == other.stringValue();
    }
    return false;
}

std::string_view StateValue::stringValue() const noexcept {
    const void* terminator = std::memchr(string.data(), '\0', string.size());
    const auto length = terminator == nullptr
                            ? string.size()
                            : static_cast<std::size_t>(static_cast<const char*>(terminator) - string.data());
    return {string.data(), length};
}

bool validNamespaceKey(std::string_view key) noexcept {
    if (key.empty() || key.size() > 63 || key.front() == '.' || key.back() == '.') {
        return false;
    }
    bool hasSeparator = false;
    bool previousDot = false;
    for (const char character : key) {
        const bool validCharacter = (character >= 'a' && character <= 'z') ||
                                    (character >= 'A' && character <= 'Z') ||
                                    (character >= '0' && character <= '9') ||
                                    character == '_' || character == '-' || character == '.';
        if (!validCharacter || (character == '.' && previousDot)) {
            return false;
        }
        hasSeparator = hasSeparator || character == '.';
        previousDot = character == '.';
    }
    return hasSeparator;
}

bool validUtf8(std::string_view text) noexcept {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<std::uint8_t>(text[index]);
        if (first <= 0x7f) {
            ++index;
            continue;
        }
        std::uint32_t codePoint{};
        std::size_t continuationCount{};
        if ((first & 0xe0U) == 0xc0U) {
            codePoint = first & 0x1fU;
            continuationCount = 1;
            if (codePoint < 2) {
                return false;
            }
        } else if ((first & 0xf0U) == 0xe0U) {
            codePoint = first & 0x0fU;
            continuationCount = 2;
        } else if ((first & 0xf8U) == 0xf0U) {
            codePoint = first & 0x07U;
            continuationCount = 3;
            if (codePoint > 4) {
                return false;
            }
        } else {
            return false;
        }
        if (index + continuationCount >= text.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset <= continuationCount; ++offset) {
            const auto next = static_cast<std::uint8_t>(text[index + offset]);
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
            codePoint = (codePoint << 6U) | (next & 0x3fU);
        }
        if ((continuationCount == 2 && codePoint < 0x800U) ||
            (continuationCount == 3 && codePoint < 0x10000U) ||
            (codePoint >= 0xd800U && codePoint <= 0xdfffU) || codePoint > 0x10ffffU) {
            return false;
        }
        index += continuationCount + 1;
    }
    return true;
}

bool GameState::validAddress(StateAddress address) noexcept {
    if (!validNamespaceKey(address.key)) {
        return false;
    }
    switch (address.scope) {
    case StateScope::Global:
    case StateScope::World:
        return !address.entity.valid();
    case StateScope::Entity:
        return address.entity.valid();
    }
    return false;
}

std::size_t GameState::find(StateAddress address) const noexcept {
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        const Entry& entry = entries_[index];
        if (entry.used && entry.scope == address.scope && entry.entity == address.entity &&
            std::string_view(entry.key.data()) == address.key) {
            return index;
        }
    }
    return entries_.size();
}

Status GameState::set(StateAddress address, StateValue value) noexcept {
    if (!validAddress(address) || !value.valid()) {
        return {ErrorCode::InvalidValue, address.key, "State key, scope, or value is invalid."};
    }
    std::size_t index = find(address);
    if (index == entries_.size()) {
        for (std::size_t candidate = 0; candidate < entries_.size(); ++candidate) {
            if (!entries_[candidate].used) {
                index = candidate;
                break;
            }
        }
        if (index == entries_.size()) {
            return {ErrorCode::CapacityExceeded, address.key, "Game-state entry capacity is full."};
        }
    } else if (entries_[index].value.equals(value)) {
        return Status::success();
    }

    const Entry* before = entries_[index].used ? &entries_[index] : nullptr;
    Entry after{};
    after.used = true;
    after.scope = address.scope;
    after.entity = address.entity;
    std::memcpy(after.key.data(), address.key.data(), address.key.size());
    after.value = value;
    const Status queued = queueChange(before, &after, address);
    if (!queued) {
        return queued;
    }
    if (!before) {
        ++entryCount_;
    }
    entries_[index] = after;
    if (!dispatchingChanges_) {
        dispatchChanges();
    }
    return Status::success();
}

bool GameState::get(StateAddress address, StateValue& value) const noexcept {
    if (!validAddress(address)) {
        return false;
    }
    const std::size_t index = find(address);
    if (index == entries_.size()) {
        return false;
    }
    value = entries_[index].value;
    return true;
}

Status GameState::getOr(StateAddress address, StateValue defaultValue,
                        StateValue& value) const noexcept {
    if (!validAddress(address) || !defaultValue.valid()) {
        return {ErrorCode::InvalidValue, address.key, "Default state lookup arguments are invalid."};
    }
    if (!get(address, value)) {
        value = defaultValue;
    }
    return Status::success();
}

bool GameState::contains(StateAddress address) const noexcept {
    StateValue ignored{};
    return get(address, ignored);
}

bool GameState::entryAt(std::size_t ordinal, StateEntryView& entry) const noexcept {
    for (const Entry& stored : entries_) {
        if (!stored.used) {
            continue;
        }
        if (ordinal == 0) {
            entry.address = {stored.scope, stored.entity, std::string_view(stored.key.data())};
            entry.value = stored.value;
            return true;
        }
        --ordinal;
    }
    return false;
}

bool GameState::erase(StateAddress address) noexcept {
    if (!validAddress(address)) {
        return false;
    }
    const std::size_t index = find(address);
    if (index == entries_.size()) {
        return false;
    }
    const Status queued = queueChange(&entries_[index], nullptr, address);
    if (!queued) {
        return false;
    }
    entries_[index].used = false;
    --entryCount_;
    if (!dispatchingChanges_) {
        dispatchChanges();
    }
    return true;
}

Status GameState::addObserver(StateChangeCallback callback, void* context,
                              Subscription& subscription) noexcept {
    if (callback == nullptr) {
        return {ErrorCode::InvalidArgument, {}, "State observer callback is required."};
    }
    for (std::size_t index = 0; index < observers_.size(); ++index) {
        if (!observers_[index].active) {
            Observer& observer = observers_[index];
            ++observer.generation;
            if (observer.generation == 0) {
                observer.generation = 1;
            }
            observer.callback = callback;
            observer.context = context;
            observer.sequence = nextObserverSequence_++;
            observer.active = true;
            subscription = {static_cast<std::uint16_t>(index), observer.generation};
            return Status::success();
        }
    }
    return {ErrorCode::CapacityExceeded, "state observer", "Observer capacity is full."};
}

bool GameState::removeObserver(Subscription subscription) noexcept {
    if (!subscription.valid() || subscription.slot >= observers_.size()) {
        return false;
    }
    Observer& observer = observers_[subscription.slot];
    if (!observer.active || observer.generation != subscription.generation) {
        return false;
    }
    observer.active = false;
    observer.callback = nullptr;
    observer.context = nullptr;
    return true;
}

Status GameState::replaceValues(const GameState& other) noexcept {
    if (other.entryCount_ > entries_.size()) {
        return {ErrorCode::CapacityExceeded, {}, "Restored game state exceeds entry capacity."};
    }
    entries_ = other.entries_;
    entryCount_ = other.entryCount_;
    changeHead_ = 0;
    changeCount_ = 0;
    return Status::success();
}

Status GameState::queueChange(const Entry* before, const Entry* after,
                              StateAddress address) noexcept {
    bool hasObservers = false;
    for (const Observer& observer : observers_) {
        hasObservers = hasObservers || observer.active;
    }
    if (!hasObservers) {
        return Status::success();
    }
    if (changeCount_ == changes_.size()) {
        return {ErrorCode::CapacityExceeded, address.key, "State-change queue capacity is full."};
    }
    const std::size_t tail = (changeHead_ + changeCount_) % changes_.size();
    ChangeRecord& record = changes_[tail];
    record = {};
    record.scope = address.scope;
    record.entity = address.entity;
    std::memcpy(record.key.data(), address.key.data(), address.key.size());
    if (before != nullptr) {
        record.before = before->value;
        record.hadBefore = true;
    }
    if (after != nullptr) {
        record.after = after->value;
        record.hasAfter = true;
    }
    ++changeCount_;
    return Status::success();
}

void GameState::dispatchChanges() noexcept {
    dispatchingChanges_ = true;
    while (changeCount_ != 0) {
        ChangeRecord record = changes_[changeHead_];
        changeHead_ = (changeHead_ + 1) % changes_.size();
        --changeCount_;
        const StateChange change{
            {record.scope, record.entity, std::string_view(record.key.data())},
            record.before, record.after, record.hadBefore, record.hasAfter};
        const std::uint32_t sequenceLimit = nextObserverSequence_;
        std::uint32_t afterSequence = 0;
        for (;;) {
            std::size_t next = observers_.size();
            std::uint32_t nextSequence = sequenceLimit;
            for (std::size_t index = 0; index < observers_.size(); ++index) {
                const Observer& candidate = observers_[index];
                if (candidate.active && candidate.sequence >= afterSequence &&
                    candidate.sequence < nextSequence) {
                    next = index;
                    nextSequence = candidate.sequence;
                }
            }
            if (next == observers_.size()) {
                break;
            }
            afterSequence = nextSequence + 1;
            Observer& observer = observers_[next];
            if (observer.active && observer.callback != nullptr) {
                observer.callback(observer.context, change);
            }
        }
    }
    dispatchingChanges_ = false;
}

// Defer span copy constraints until Condition is complete for the PS5 libc++.
Condition::Condition() noexcept = default;
Condition::Condition(const Condition&) noexcept = default;
Condition& Condition::operator=(const Condition&) noexcept = default;

Condition Condition::exists(StateAddress address) noexcept {
    Condition condition{};
    condition.kind = ConditionKind::Exists;
    condition.address = address;
    return condition;
}

Condition Condition::compare(StateAddress address, CompareOperator op,
                             StateValue value) noexcept {
    Condition condition{};
    condition.kind = ConditionKind::Compare;
    condition.address = address;
    condition.comparison = op;
    condition.value = value;
    return condition;
}

Condition Condition::all(std::span<const Condition> childConditions) noexcept {
    Condition condition{};
    condition.kind = ConditionKind::And;
    condition.children = childConditions;
    return condition;
}

Condition Condition::any(std::span<const Condition> childConditions) noexcept {
    Condition condition{};
    condition.kind = ConditionKind::Or;
    condition.children = childConditions;
    return condition;
}

Condition Condition::negate(const Condition& child) noexcept {
    Condition condition{};
    condition.kind = ConditionKind::Not;
    condition.children = {&child, 1};
    return condition;
}

ConditionResult evaluate(const Condition& condition, const GameState& state) noexcept {
    return evaluateAt(condition, state, 0);
}

Status ActionRegistry::addCapability(std::string_view id) noexcept {
    if (!validNamespaceKey(id)) {
        return {ErrorCode::InvalidValue, id, "Capability IDs must be namespaced keys."};
    }
    if (hasCapability(id)) {
        return {ErrorCode::DuplicateId, id, "Capability is already registered."};
    }
    for (Capability& capability : capabilities_) {
        if (!capability.active) {
            copyText(capability.id, id);
            capability.active = true;
            return Status::success();
        }
    }
    return {ErrorCode::CapacityExceeded, id, "Capability registry capacity is full."};
}

Status ActionRegistry::registerAction(std::string_view id, std::string_view capability,
                                      ActionHandler handler, void* context,
                                      bool requiresActiveGame) noexcept {
    if (!validNamespaceKey(id) || id.size() > 63 || capability.size() > 63 ||
        (!capability.empty() && !validNamespaceKey(capability)) || handler == nullptr) {
        return {ErrorCode::InvalidValue, id, "Action ID, capability, or handler is invalid."};
    }
    for (const Action& action : actions_) {
        if (action.active && std::string_view(action.id.data()) == id) {
            return {ErrorCode::DuplicateId, id, "Action is already registered."};
        }
    }
    for (Action& action : actions_) {
        if (!action.active) {
            copyText(action.id, id);
            copyText(action.capability, capability);
            action.handler = handler;
            action.context = context;
            action.requiresActiveGame = requiresActiveGame;
            action.active = true;
            return Status::success();
        }
    }
    return {ErrorCode::CapacityExceeded, id, "Action registry capacity is full."};
}

ActionResult ActionRegistry::execute(const ActionRequest& request) const noexcept {
    if (request.id.empty() || request.parameters.size() > 16) {
        return {{ErrorCode::InvalidArgument, request.id, "Action request is malformed."}, {}, false};
    }
    for (const Action& action : actions_) {
        if (!action.active || std::string_view(action.id.data()) != request.id) {
            continue;
        }
        const std::string_view capability(action.capability.data());
        if (!capability.empty() && !hasCapability(capability)) {
            return {{ErrorCode::MissingCapability, capability,
                     "The action's required capability is not enabled."}, {}, false};
        }
        if (action.requiresActiveGame && !gameActive_) {
            return {{ErrorCode::InvalidState, request.id,
                     "This action cannot execute outside an active game state."}, {}, false};
        }
        for (const ActionParameter& parameter : request.parameters) {
            if (!validParameterName(parameter.name) || !parameter.value.valid()) {
                return {{ErrorCode::InvalidValue, parameter.name,
                         "Action parameters require valid names and values."}, {}, false};
            }
        }
        for (std::size_t index = 0; index < request.parameters.size(); ++index) {
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (request.parameters[index].name == request.parameters[previous].name) {
                    return {{ErrorCode::InvalidValue, request.parameters[index].name,
                             "Action parameter names must be unique."}, {}, false};
                }
            }
        }
        return action.handler(action.context, request);
    }
    return {{ErrorCode::UnknownAction, request.id, "No handler is registered for this action."}, {}, false};
}

ActionResult ActionRegistry::setStateValue(void* context,
                                           const ActionRequest& request) noexcept {
    auto* state = static_cast<GameState*>(context);
    if (state == nullptr) {
        return {{ErrorCode::InvalidArgument, request.id, "State service is unavailable."}, {}, false};
    }
    const ActionParameter* key = nullptr;
    const ActionParameter* value = nullptr;
    for (const ActionParameter& parameter : request.parameters) {
        if (parameter.name == "key") {
            key = &parameter;
        } else if (parameter.name == "value") {
            value = &parameter;
        } else {
            return {{ErrorCode::InvalidValue, parameter.name, "Unexpected action parameter."}, {}, false};
        }
    }
    if (key == nullptr || value == nullptr || key->value.type != ValueType::String) {
        return {{ErrorCode::InvalidValue, request.id, "Action requires string key and typed value parameters."}, {}, false};
    }
    const Status status = state->set({StateScope::Global, {}, key->value.stringValue()}, value->value);
    return {status, {}, false};
}

ActionResult ActionRegistry::setStateFlag(void* context,
                                          const ActionRequest& request) noexcept {
    auto* state = static_cast<GameState*>(context);
    if (state == nullptr) {
        return {{ErrorCode::InvalidArgument, request.id, "State service is unavailable."}, {}, false};
    }
    const ActionParameter* key = nullptr;
    const ActionParameter* value = nullptr;
    for (const ActionParameter& parameter : request.parameters) {
        if (parameter.name == "key") {
            key = &parameter;
        } else if (parameter.name == "value") {
            value = &parameter;
        } else {
            return {{ErrorCode::InvalidValue, parameter.name, "Unexpected action parameter."}, {}, false};
        }
    }
    if (key == nullptr || value == nullptr || key->value.type != ValueType::String ||
        value->value.type != ValueType::Boolean) {
        return {{ErrorCode::InvalidValue, request.id, "Action requires a string key and boolean value."}, {}, false};
    }
    const Status status = state->set({StateScope::Global, {}, key->value.stringValue()}, value->value);
    return {status, {}, false};
}

bool ActionRegistry::hasCapability(std::string_view id) const noexcept {
    for (const Capability& capability : capabilities_) {
        if (capability.active && std::string_view(capability.id.data()) == id) {
            return true;
        }
    }
    return false;
}

} // namespace echo::framework
