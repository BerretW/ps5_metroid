#pragma once

#include "echo/framework/inventory.hpp"
#include "echo/framework/narrative.hpp"
#include "game/project_components.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace echo {
class Game;
}

namespace echo::platform {

namespace framework_detail {

struct AbilityUnlocked {
    std::uint32_t actorIndex{};
    std::uint32_t actorGeneration{};
    std::array<char, 32> ability{};
};

class StateModule final : public framework::IModule {
public:
    [[nodiscard]] const framework::ModuleInfo& info() const noexcept override;
    bool initialize(framework::ModuleContext&) noexcept override { return true; }
    void shutdown() noexcept override {}

    framework::GameState state{};
};

class ProjectEchoModule final : public framework::IModule {
public:
    [[nodiscard]] const framework::ModuleInfo& info() const noexcept override;
    bool initialize(framework::ModuleContext&) noexcept override { return true; }
    void shutdown() noexcept override {}
};

class UnlockListener final : public framework::IEventListener<AbilityUnlocked> {
public:
    void onEvent(const AbilityUnlocked& event) noexcept override;
    [[nodiscard]] std::size_t count() const noexcept { return count_; }

private:
    std::size_t count_{};
};

} // namespace framework_detail

class FrameworkIntegration {
public:
    FrameworkIntegration() noexcept = default;
    ~FrameworkIntegration();

    FrameworkIntegration(const FrameworkIntegration&) = delete;
    FrameworkIntegration& operator=(const FrameworkIntegration&) = delete;

    [[nodiscard]] framework::Status initialize(const char* configPath,
                                                const char* dialoguePath,
                                                const char* itemCatalogPath = nullptr) noexcept;
    [[nodiscard]] framework::Status configureGame(Game& game) noexcept;
    [[nodiscard]] framework::Status beginGame() noexcept;
    [[nodiscard]] framework::Status resumeGame(const framework::GameState& state) noexcept;
    [[nodiscard]] framework::Status notifyEchoStepUnlocked() noexcept;
    [[nodiscard]] framework::GameState& gameState() noexcept { return stateModule_.state; }
    void shutdown() noexcept;
    [[nodiscard]] std::size_t unlockEventCount() const noexcept;

private:
    framework_detail::StateModule stateModule_{};
    framework_detail::ProjectEchoModule projectModule_{};
    framework::InventoryModule inventoryModule_{};
    std::unique_ptr<framework::DialogueDocument> dialogueDocument_{};
    framework::DialogueModule dialogueModule_{};
    framework::ModuleRegistry modules_{};
    framework::ActionRegistry actions_{};
    framework::EventBus<8, 8, 64> events_{};
    framework_detail::UnlockListener unlockListener_{};
    framework::Subscription unlockSubscription_{};
    std::array<float, 2> technicianPosition_{};
    float technicianInteractionReach_{kDefaultInteractionReach};
    bool initialized_{};
};

} // namespace echo::platform
