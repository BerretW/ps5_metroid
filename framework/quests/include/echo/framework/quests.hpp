#pragma once

#include "echo/framework/capability_manifest.hpp"
#include "echo/framework/data.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace echo::framework {

inline constexpr std::size_t kMaxQuests = 8;
inline constexpr std::size_t kMaxQuestObjectives = 8;

struct QuestObjective {
    std::array<char, 64> id{};
    std::array<char, 128> fact{};
    std::array<char, 64> text{};
};

class QuestDefinition {
public:
    [[nodiscard]] std::string_view id() const noexcept { return id_.data(); }
    [[nodiscard]] std::span<const QuestObjective> objectives() const noexcept {
        return {objectives_.data(), objectiveCount_};
    }

private:
    std::array<char, 128> id_{};
    std::array<QuestObjective, kMaxQuestObjectives> objectives_{};
    std::size_t objectiveCount_{};
    friend class QuestModule;
};

struct QuestProgress {
    std::size_t completed{};
    std::size_t total{};
    std::string_view objective{};
};

[[nodiscard]] QuestProgress evaluateQuest(
    const QuestDefinition& quest, std::span<const std::string_view> facts) noexcept;

class QuestModule final : public IModule {
public:
    [[nodiscard]] Status load(JsonValue document) noexcept;
    [[nodiscard]] const ModuleInfo& info() const noexcept override;
    bool initialize(ModuleContext&) noexcept override;
    void shutdown() noexcept override;
    [[nodiscard]] const QuestDefinition* find(std::string_view id) const noexcept;
    [[nodiscard]] std::span<const QuestDefinition> definitions() const noexcept {
        return {quests_.data(), count_};
    }
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

private:
    std::array<QuestDefinition, kMaxQuests> quests_{};
    std::size_t count_{};
    bool initialized_{};
};

[[nodiscard]] CapabilityManifestFragment questsCapabilityManifestFragment() noexcept;

} // namespace echo::framework
