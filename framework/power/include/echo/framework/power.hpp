#pragma once

#include "echo/framework/capability_manifest.hpp"
#include "echo/framework/data.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace echo::framework {

inline constexpr std::size_t kMaxPowerCircuits = 32;
inline constexpr std::size_t kMaxPowerBackups = 4;
inline constexpr std::size_t kMaxPowerJsonBytes = 32 * 1024;
inline constexpr std::string_view kPowerModuleId = "echo.power";
inline constexpr std::string_view kPowerControlCapability = "echo.power.control";

struct PowerCircuitView {
    std::string_view id{};
    // Parent is preferred; backup dependencies are tested in declaration order.
    std::string_view parent{};
    std::array<std::string_view, kMaxPowerBackups> backups{};
    std::size_t backupCount{};
    bool defaultOn{};
    bool requestedOn{};
    bool supplied{};
    bool effectiveOn{};
};

struct PowerCircuitRequest {
    std::string_view circuitId{};
    bool requestedOn{};
};

struct PowerChangeEvent {
    std::array<char, 96> circuitId{};
    bool requestedBefore{};
    bool requestedAfter{};
    bool suppliedBefore{};
    bool suppliedAfter{};
    bool effectiveBefore{};
    bool effectiveAfter{};
};

class PowerModule final : public IModule {
public:
    [[nodiscard]] Status load(JsonValue document) noexcept;
    [[nodiscard]] Status load(const char* data, std::size_t size,
                              JsonError& error) noexcept;
    [[nodiscard]] const ModuleInfo& info() const noexcept override;
    bool initialize(ModuleContext& context) noexcept override;
    void shutdown() noexcept override;

    [[nodiscard]] Status registerActions(ActionRegistry& actions) noexcept;
    [[nodiscard]] Status setRequested(std::string_view circuitId, bool requestedOn) noexcept;
    [[nodiscard]] Status toggle(std::string_view circuitId) noexcept;
    [[nodiscard]] Status restore() noexcept;
    [[nodiscard]] Status restore(std::span<const PowerCircuitRequest> requests) noexcept;
    [[nodiscard]] Status saveState(GameState& state,
                                   bool useDefaults = false) const noexcept;
    [[nodiscard]] Status restoreState(const GameState& state) noexcept;

    [[nodiscard]] Status get(std::string_view circuitId,
                             PowerCircuitView& view) const noexcept;
    [[nodiscard]] bool circuitAt(std::size_t index, PowerCircuitView& view) const noexcept;
    [[nodiscard]] Status isSupplied(std::string_view circuitId,
                                    bool& supplied) const noexcept;
    [[nodiscard]] Status isPowered(std::string_view circuitId,
                                   bool& powered) const noexcept;

    [[nodiscard]] Status subscribe(IEventListener<PowerChangeEvent>& listener,
                                   Subscription& subscription) noexcept;
    [[nodiscard]] bool unsubscribe(Subscription subscription) noexcept;
    [[nodiscard]] std::size_t circuitCount() const noexcept { return circuitCount_; }
    [[nodiscard]] bool hasCircuit(std::string_view circuitId) const noexcept {
        return find(circuits_, circuitCount_, circuitId) != circuitCount_;
    }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

private:
    struct Circuit {
        std::array<char, 96> id{};
        std::array<char, 96> parent{};
        std::array<std::array<char, 96>, kMaxPowerBackups> backups{};
        std::size_t backupCount{};
        bool defaultOn{};
        bool requestedOn{};
        bool supplied{};
        bool effectiveOn{};
    };

    [[nodiscard]] static std::size_t find(
        const std::array<Circuit, kMaxPowerCircuits>& circuits, std::size_t count,
        std::string_view id) noexcept;
    [[nodiscard]] static Status stateKey(
        std::string_view id, std::array<char, 32>& key) noexcept;
    [[nodiscard]] static Status resolve(
        const std::array<Circuit, kMaxPowerCircuits>& circuits, std::size_t count,
        const std::array<bool, kMaxPowerCircuits>& requested,
        std::array<bool, kMaxPowerCircuits>& supplied,
        std::array<bool, kMaxPowerCircuits>& effective) noexcept;
    [[nodiscard]] Status commit(
        const std::array<bool, kMaxPowerCircuits>& requested,
        const std::array<bool, kMaxPowerCircuits>& supplied,
        const std::array<bool, kMaxPowerCircuits>& effective) noexcept;
    [[nodiscard]] static ActionResult setAction(
        void* context, const ActionRequest& request) noexcept;
    [[nodiscard]] static ActionResult toggleAction(
        void* context, const ActionRequest& request) noexcept;
    [[nodiscard]] static ActionResult restoreAction(
        void* context, const ActionRequest& request) noexcept;

    std::array<Circuit, kMaxPowerCircuits> circuits_{};
    EventBus<16, 32, 128> events_{};
    std::array<Subscription, 16> subscriptions_{};
    std::size_t circuitCount_{};
    bool initialized_{};
    bool dispatchingChanges_{};
};

[[nodiscard]] CapabilityManifestFragment powerCapabilityManifestFragment() noexcept;

} // namespace echo::framework
