#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace echo::framework {

enum class ErrorCode : std::uint8_t {
    None,
    InvalidArgument,
    InvalidValue,
    DuplicateId,
    MissingDependency,
    DependencyCycle,
    InitializationFailed,
    CapacityExceeded,
    NotFound,
    UnknownAction,
    MissingCapability,
    InvalidState,
    ParseError,
    UnsupportedVersion,
    CorruptData,
    BufferTooSmall,
    StorageFailure
};

struct Status {
    ErrorCode code{ErrorCode::None};
    std::string_view subject{};
    std::string_view detail{};

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return code == ErrorCode::None;
    }

    [[nodiscard]] static constexpr Status success() noexcept { return {}; }
};

struct ModuleInfo {
    std::string_view id;
    std::string_view name;
    std::uint32_t version{};
    std::span<const std::string_view> required;
    std::span<const std::string_view> optional;
    std::span<const std::string_view> provides;
    std::span<const std::string_view> initializeAfter;
};

struct ModuleContext {
    void* userData{};
};

class IModule {
public:
    virtual ~IModule() = default;
    [[nodiscard]] virtual const ModuleInfo& info() const noexcept = 0;
    virtual bool initialize(ModuleContext& context) noexcept = 0;
    virtual void shutdown() noexcept = 0;
};

class ModuleRegistry {
public:
    static constexpr std::size_t kMaxModules = 32;

    [[nodiscard]] Status add(IModule& module, bool selected = true) noexcept;
    [[nodiscard]] Status initializeAll(ModuleContext& context) noexcept;
    void shutdownAll() noexcept;
    [[nodiscard]] bool initialized() const noexcept { return initializedCount_ != 0; }
    [[nodiscard]] std::size_t registeredCount() const noexcept { return count_; }
    [[nodiscard]] std::size_t initializedCount() const noexcept { return initializedCount_; }

private:
    struct Slot {
        IModule* module{};
        bool selected{};
        bool initialized{};
        std::uint8_t visit{};
    };

    [[nodiscard]] int find(std::string_view id) const noexcept;
    [[nodiscard]] Status visit(std::size_t index) noexcept;
    [[nodiscard]] Status visitDependencies(std::size_t index,
                                           std::span<const std::string_view> dependencies,
                                           bool optional) noexcept;

    std::array<Slot, kMaxModules> slots_{};
    std::array<std::size_t, kMaxModules> order_{};
    std::size_t count_{};
    std::size_t orderCount_{};
    std::size_t initializedCount_{};
};

struct EntityId {
    static constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t index{kInvalidIndex};
    std::uint32_t generation{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return index != kInvalidIndex && generation != 0;
    }
    friend constexpr bool operator==(EntityId, EntityId) noexcept = default;
};

template <std::size_t Capacity>
class EntityStore {
    static_assert(Capacity > 0 && Capacity < EntityId::kInvalidIndex);

public:
    [[nodiscard]] EntityId create() noexcept {
        for (std::size_t index = 0; index < Capacity; ++index) {
            if (!alive_[index]) {
                if (generations_[index] == 0) {
                    generations_[index] = 1;
                }
                alive_[index] = true;
                ++aliveCount_;
                return {static_cast<std::uint32_t>(index), generations_[index]};
            }
        }
        return {};
    }

    [[nodiscard]] bool destroy(EntityId entity) noexcept {
        if (!isAlive(entity)) {
            return false;
        }
        alive_[entity.index] = false;
        ++generations_[entity.index];
        if (generations_[entity.index] == 0) {
            generations_[entity.index] = 1;
        }
        --aliveCount_;
        return true;
    }

    [[nodiscard]] bool isAlive(EntityId entity) const noexcept {
        return entity.index < Capacity && entity.generation != 0 &&
               alive_[entity.index] && generations_[entity.index] == entity.generation;
    }

    [[nodiscard]] std::size_t size() const noexcept { return aliveCount_; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    std::array<std::uint32_t, Capacity> generations_{};
    std::array<bool, Capacity> alive_{};
    std::size_t aliveCount_{};
};

template <typename Component, std::size_t Capacity>
class ComponentPool {
    static_assert(std::is_trivially_copyable_v<Component>,
                  "Components must be plain, trivially copyable data.");

public:
    [[nodiscard]] bool set(EntityId entity, const EntityStore<Capacity>& entities,
                           const Component& component) noexcept(std::is_nothrow_copy_assignable_v<Component>) {
        if (!entities.isAlive(entity)) {
            return false;
        }
        values_[entity.index] = component;
        generations_[entity.index] = entity.generation;
        present_[entity.index] = true;
        return true;
    }

    [[nodiscard]] Component* get(EntityId entity,
                                 const EntityStore<Capacity>& entities) noexcept {
        if (!entities.isAlive(entity) || !present_[entity.index] ||
            generations_[entity.index] != entity.generation) {
            return nullptr;
        }
        return &values_[entity.index];
    }

    [[nodiscard]] const Component* get(EntityId entity,
                                       const EntityStore<Capacity>& entities) const noexcept {
        if (!entities.isAlive(entity) || !present_[entity.index] ||
            generations_[entity.index] != entity.generation) {
            return nullptr;
        }
        return &values_[entity.index];
    }

    [[nodiscard]] bool remove(EntityId entity,
                              const EntityStore<Capacity>& entities) noexcept {
        if (get(entity, entities) == nullptr) {
            return false;
        }
        present_[entity.index] = false;
        return true;
    }

private:
    std::array<Component, Capacity> values_{};
    std::array<std::uint32_t, Capacity> generations_{};
    std::array<bool, Capacity> present_{};
};

struct Subscription {
    std::uint16_t slot{std::numeric_limits<std::uint16_t>::max()};
    std::uint16_t generation{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return slot != std::numeric_limits<std::uint16_t>::max() && generation != 0;
    }
};

template <typename Event>
class IEventListener {
public:
    virtual ~IEventListener() = default;
    virtual void onEvent(const Event& event) noexcept = 0;
};

template <std::size_t MaxListeners = 32, std::size_t MaxQueuedEvents = 64,
          std::size_t MaxEventBytes = 128>
class EventBus {
    static_assert(MaxListeners > 0 && MaxListeners <= std::numeric_limits<std::uint16_t>::max());
    static_assert(MaxQueuedEvents > 0 && MaxEventBytes > 0);

public:
    template <typename Event>
    [[nodiscard]] Status subscribe(IEventListener<Event>& listener,
                                   Subscription& subscription) noexcept {
        static_assert(std::is_trivially_copyable_v<Event>,
                      "Queued event payloads must be trivially copyable.");
        static_assert(sizeof(Event) <= MaxEventBytes,
                      "Event payload exceeds the configured queue item limit.");
        for (std::size_t index = 0; index < listeners_.size(); ++index) {
            if (!listeners_[index].active) {
                Listener& item = listeners_[index];
                ++item.generation;
                if (item.generation == 0) {
                    item.generation = 1;
                }
                item.type = typeToken<Event>();
                item.object = &listener;
                item.invoke = &invoke<Event>;
                item.sequence = nextSequence_++;
                item.active = true;
                subscription = {static_cast<std::uint16_t>(index), item.generation};
                return Status::success();
            }
        }
        return {ErrorCode::CapacityExceeded, "event listener", "Listener capacity is full."};
    }

    [[nodiscard]] bool unsubscribe(Subscription subscription) noexcept {
        if (!subscription.valid() || subscription.slot >= listeners_.size()) {
            return false;
        }
        Listener& listener = listeners_[subscription.slot];
        if (!listener.active || listener.generation != subscription.generation) {
            return false;
        }
        listener.active = false;
        listener.object = nullptr;
        listener.invoke = nullptr;
        return true;
    }

    template <typename Event>
    [[nodiscard]] Status publish(const Event& event) noexcept {
        static_assert(std::is_trivially_copyable_v<Event>,
                      "Queued event payloads must be trivially copyable.");
        static_assert(sizeof(Event) <= MaxEventBytes,
                      "Event payload exceeds the configured queue item limit.");
        if (queuedCount_ == queue_.size()) {
            return {ErrorCode::CapacityExceeded, "event queue", "Queued event capacity is full."};
        }
        const std::size_t tail = (queueHead_ + queuedCount_) % queue_.size();
        QueuedEvent& item = queue_[tail];
        item.type = typeToken<Event>();
        item.size = sizeof(Event);
        std::memcpy(item.payload.data(), &event, sizeof(Event));
        ++queuedCount_;
        if (!dispatching_) {
            dispatchPending();
        }
        return Status::success();
    }

    [[nodiscard]] std::size_t queuedCount() const noexcept { return queuedCount_; }

private:
    struct Listener {
        const void* type{};
        void* object{};
        void (*invoke)(void*, const void*) noexcept{};
        std::uint32_t sequence{};
        std::uint16_t generation{};
        bool active{};
    };

    struct QueuedEvent {
        const void* type{};
        std::size_t size{};
        alignas(std::max_align_t) std::array<std::byte, MaxEventBytes> payload{};
    };

    template <typename Event>
    [[nodiscard]] static const void* typeToken() noexcept {
        static const std::uint8_t token{};
        return &token;
    }

    template <typename Event>
    static void invoke(void* object, const void* payload) noexcept {
        static_cast<IEventListener<Event>*>(object)->onEvent(*static_cast<const Event*>(payload));
    }

    void dispatchPending() noexcept {
        dispatching_ = true;
        while (queuedCount_ != 0) {
            QueuedEvent current = queue_[queueHead_];
            queueHead_ = (queueHead_ + 1) % queue_.size();
            --queuedCount_;
            const std::uint32_t sequenceLimit = nextSequence_;
            std::uint32_t afterSequence = 0;
            for (;;) {
                std::size_t next = listeners_.size();
                std::uint32_t nextSequence = sequenceLimit;
                for (std::size_t index = 0; index < listeners_.size(); ++index) {
                    const Listener& candidate = listeners_[index];
                    if (candidate.active && candidate.type == current.type &&
                        candidate.sequence >= afterSequence &&
                        candidate.sequence < nextSequence) {
                        next = index;
                        nextSequence = candidate.sequence;
                    }
                }
                if (next == listeners_.size()) {
                    break;
                }
                afterSequence = nextSequence + 1;
                Listener& listener = listeners_[next];
                if (listener.active && listener.invoke != nullptr) {
                    listener.invoke(listener.object, current.payload.data());
                }
            }
        }
        dispatching_ = false;
    }

    std::array<Listener, MaxListeners> listeners_{};
    std::array<QueuedEvent, MaxQueuedEvents> queue_{};
    std::size_t queueHead_{};
    std::size_t queuedCount_{};
    std::uint32_t nextSequence_{1};
    bool dispatching_{};
};

template <typename Bus>
class ScopedSubscription {
public:
    ScopedSubscription() noexcept = default;
    ScopedSubscription(Bus& bus, Subscription subscription) noexcept
        : bus_(&bus), subscription_(subscription) {}
    ~ScopedSubscription() { reset(); }
    ScopedSubscription(const ScopedSubscription&) = delete;
    ScopedSubscription& operator=(const ScopedSubscription&) = delete;
    ScopedSubscription(ScopedSubscription&& other) noexcept
        : bus_(other.bus_), subscription_(other.subscription_) {
        other.bus_ = nullptr;
        other.subscription_ = {};
    }
    ScopedSubscription& operator=(ScopedSubscription&& other) noexcept {
        if (this != &other) {
            reset();
            bus_ = other.bus_;
            subscription_ = other.subscription_;
            other.bus_ = nullptr;
            other.subscription_ = {};
        }
        return *this;
    }

    void reset() noexcept {
        if (bus_ != nullptr) {
            (void)bus_->unsubscribe(subscription_);
            bus_ = nullptr;
            subscription_ = {};
        }
    }

private:
    Bus* bus_{};
    Subscription subscription_{};
};

enum class ValueType : std::uint8_t { Boolean, Integer, Number, String };

struct StateValue {
    ValueType type{ValueType::Boolean};
    bool boolean{};
    std::int64_t integer{};
    double number{};
    std::array<char, 128> string{};

    [[nodiscard]] static StateValue fromBoolean(bool value) noexcept;
    [[nodiscard]] static StateValue fromInteger(std::int64_t value) noexcept;
    [[nodiscard]] static StateValue fromNumber(double value) noexcept;
    [[nodiscard]] static bool fromString(std::string_view value, StateValue& output) noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool equals(const StateValue& other) const noexcept;
    [[nodiscard]] std::string_view stringValue() const noexcept;
};

enum class StateScope : std::uint8_t { Global, World, Entity };

struct StateAddress {
    StateScope scope{StateScope::Global};
    EntityId entity{};
    std::string_view key{};
};

struct StateChange {
    StateAddress address{};
    StateValue before{};
    StateValue after{};
    bool hadBefore{};
    bool hasAfter{};
};

struct StateEntryView {
    StateAddress address{};
    StateValue value{};
};

using StateChangeCallback = void (*)(void*, const StateChange&) noexcept;

class GameState {
public:
    static constexpr std::size_t kMaxEntries = 128;
    static constexpr std::size_t kMaxObservers = 16;
    static constexpr std::size_t kMaxPendingChanges = 64;

    [[nodiscard]] Status set(StateAddress address, StateValue value) noexcept;
    [[nodiscard]] bool get(StateAddress address, StateValue& value) const noexcept;
    [[nodiscard]] Status getOr(StateAddress address, StateValue defaultValue,
                               StateValue& value) const noexcept;
    [[nodiscard]] bool contains(StateAddress address) const noexcept;
    [[nodiscard]] bool erase(StateAddress address) noexcept;
    [[nodiscard]] Status addObserver(StateChangeCallback callback, void* context,
                                     Subscription& subscription) noexcept;
    [[nodiscard]] bool removeObserver(Subscription subscription) noexcept;
    [[nodiscard]] bool unsubscribe(Subscription subscription) noexcept {
        return removeObserver(subscription);
    }
    [[nodiscard]] std::size_t size() const noexcept { return entryCount_; }
    [[nodiscard]] bool entryAt(std::size_t ordinal, StateEntryView& entry) const noexcept;
    [[nodiscard]] Status replaceValues(const GameState& other) noexcept;

private:
    struct Entry {
        std::array<char, 64> key{};
        StateScope scope{StateScope::Global};
        EntityId entity{};
        StateValue value{};
        bool used{};
    };

    struct Observer {
        StateChangeCallback callback{};
        void* context{};
        std::uint32_t sequence{};
        std::uint16_t generation{};
        bool active{};
    };

    struct ChangeRecord {
        std::array<char, 64> key{};
        StateScope scope{StateScope::Global};
        EntityId entity{};
        StateValue before{};
        StateValue after{};
        bool hadBefore{};
        bool hasAfter{};
    };

    [[nodiscard]] static bool validAddress(StateAddress address) noexcept;
    [[nodiscard]] std::size_t find(StateAddress address) const noexcept;
    [[nodiscard]] Status queueChange(const Entry* before, const Entry* after,
                                     StateAddress address) noexcept;
    void dispatchChanges() noexcept;

    std::array<Entry, kMaxEntries> entries_{};
    std::array<Observer, kMaxObservers> observers_{};
    std::array<ChangeRecord, kMaxPendingChanges> changes_{};
    std::size_t entryCount_{};
    std::size_t changeHead_{};
    std::size_t changeCount_{};
    std::uint32_t nextObserverSequence_{1};
    bool dispatchingChanges_{};
};

enum class CompareOperator : std::uint8_t { Equal, NotEqual, Less, LessOrEqual, Greater, GreaterOrEqual };
enum class ConditionKind : std::uint8_t { Exists, Compare, And, Or, Not };

struct Condition {
    Condition() noexcept;
    Condition(const Condition&) noexcept;
    Condition& operator=(const Condition&) noexcept;

    ConditionKind kind{ConditionKind::Exists};
    StateAddress address{};
    CompareOperator comparison{CompareOperator::Equal};
    StateValue value{};
    std::span<const Condition> children{};

    [[nodiscard]] static Condition exists(StateAddress address) noexcept;
    [[nodiscard]] static Condition compare(StateAddress address, CompareOperator op,
                                          StateValue value) noexcept;
    [[nodiscard]] static Condition all(std::span<const Condition> children) noexcept;
    [[nodiscard]] static Condition any(std::span<const Condition> children) noexcept;
    [[nodiscard]] static Condition negate(const Condition& child) noexcept;
};

struct ConditionResult {
    Status status{};
    bool value{};
};

[[nodiscard]] ConditionResult evaluate(const Condition& condition,
                                       const GameState& state) noexcept;

struct ActionParameter {
    std::string_view name;
    StateValue value;
};

struct ActionRequest {
    std::string_view id;
    std::span<const ActionParameter> parameters;
};

struct ActionResult {
    Status status{};
    StateValue value{};
    bool hasValue{};
};

using ActionHandler = ActionResult (*)(void*, const ActionRequest&) noexcept;

class ActionRegistry {
public:
    static constexpr std::size_t kMaxActions = 32;
    static constexpr std::size_t kMaxCapabilities = 16;

    [[nodiscard]] Status addCapability(std::string_view id) noexcept;
    [[nodiscard]] Status registerAction(std::string_view id, std::string_view capability,
                                        ActionHandler handler, void* context,
                                        bool requiresActiveGame = true) noexcept;
    [[nodiscard]] ActionResult execute(const ActionRequest& request) const noexcept;
    void setGameActive(bool active) noexcept { gameActive_ = active; }
    [[nodiscard]] static ActionResult setStateValue(void* context,
                                                    const ActionRequest& request) noexcept;
    [[nodiscard]] static ActionResult setStateFlag(void* context,
                                                   const ActionRequest& request) noexcept;

private:
    struct Capability {
        std::array<char, 64> id{};
        bool active{};
    };

    struct Action {
        std::array<char, 64> id{};
        std::array<char, 64> capability{};
        ActionHandler handler{};
        void* context{};
        bool requiresActiveGame{};
        bool active{};
    };

    [[nodiscard]] bool hasCapability(std::string_view id) const noexcept;

    std::array<Capability, kMaxCapabilities> capabilities_{};
    std::array<Action, kMaxActions> actions_{};
    bool gameActive_{};
};

[[nodiscard]] bool validNamespaceKey(std::string_view key) noexcept;
[[nodiscard]] bool validUtf8(std::string_view text) noexcept;

} // namespace echo::framework
