#pragma once

#include "echo/framework/abilities.hpp"
#include "echo/framework/inventory.hpp"
#include "echo/framework/narrative.hpp"
#include "echo/framework/attributes.hpp"
#include "echo/framework/equipment.hpp"
#include "echo/framework/quests.hpp"
#include "echo/framework/cutscenes.hpp"
#include "echo/framework/power.hpp"
#include "echo/framework/world.hpp"
#include "game/project_components.hpp"
#include "game/project_abilities.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace echo {
class Game;
class EnemyLootTable;
struct WorldLayout;
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

    // `contentRoot` is the asset directory (with trailing separator). It is
    // required when game.json selects echo.world.
    [[nodiscard]] framework::Status initialize(const char* configPath,
                                                const char* dialoguePath,
                                                const char* itemCatalogPath = nullptr,
                                                const char* contentRoot = nullptr) noexcept;
    [[nodiscard]] framework::Status configureGame(Game& game) noexcept;
    [[nodiscard]] framework::Status beginGame() noexcept;
    [[nodiscard]] framework::Status resumeGame(const framework::GameState& state) noexcept;
    [[nodiscard]] framework::Status notifyEchoStepUnlocked() noexcept;
    [[nodiscard]] framework::Status togglePowerCircuit(std::string_view circuitId) noexcept;
    [[nodiscard]] framework::Status setWorldLightEnabled(
        std::string_view roomId, std::string_view lightId, bool enabled) noexcept;
    [[nodiscard]] framework::Status saveWorldLightOverrides(
        framework::GameState& state) const noexcept;
    [[nodiscard]] framework::GameState& gameState() noexcept { return stateModule_.state; }
    // Null when the world module is not selected.
    [[nodiscard]] const WorldLayout* worldLayout() const noexcept { return worldLayout_.get(); }
    [[nodiscard]] const framework::WorldModule* world() const noexcept { return worldModule_.get(); }
    [[nodiscard]] framework::PowerModule* powerModule() noexcept { return powerModule_.get(); }
    [[nodiscard]] const framework::PowerModule* powerModule() const noexcept {
        return powerModule_.get();
    }
    void shutdown() noexcept;
    [[nodiscard]] std::size_t unlockEventCount() const noexcept;

private:
    [[nodiscard]] framework::Status loadWorld(const char* contentRoot) noexcept;
    framework_detail::StateModule stateModule_{};
    framework_detail::ProjectEchoModule projectModule_{};
    std::unique_ptr<framework::InventoryModule> inventoryModule_{};
    std::unique_ptr<framework::AttributesModule> attributesModule_{};
    std::unique_ptr<framework::EquipmentModule> equipmentModule_{};
    framework::QuestModule questModule_{};
    framework::CutsceneModule cutsceneModule_{};
    std::unique_ptr<framework::PowerModule> powerModule_{};
    std::unique_ptr<framework::WorldModule> worldModule_{};
    std::unique_ptr<WorldLayout> worldLayout_{};
    std::unique_ptr<EnemyLootTable> lootTable_{};
    std::array<char, 160> worldErrorSubject_{};
    framework::WorldLighting worldLighting_{};
    std::array<std::unique_ptr<framework::DialogueDocument>,
               framework::kMaxDialogues> dialogueDocuments_{};
    std::size_t dialogueDocumentCount_{};
    framework::DialogueModule dialogueModule_{};
    framework::ModuleRegistry modules_{};
    framework::ActionRegistry actions_{};
    framework::EventBus<8, 8, 64> events_{};
    framework_detail::UnlockListener unlockListener_{};
    framework::Subscription unlockSubscription_{};
    std::unique_ptr<framework::AbilityModule> abilitiesModule_{};
    float technicianInteractionReach_{kDefaultInteractionReach};
    bool initialized_{};
};

} // namespace echo::platform
