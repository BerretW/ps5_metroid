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
inline constexpr std::size_t kMaxQuestBranches = 4;

struct QuestObjective {
    std::array<char, 64> id{};
    std::array<char, 128> fact{};
    std::array<char, 64> text{};
};

struct QuestBranch {
    std::array<char, 32> id{};
    std::array<char, 64> label{};
    std::array<char, 128> fact{};
    std::array<char, 64> objective{};
    std::array<char, 128> rewardItem{};
    std::uint16_t rewardQuantity{};
};

class QuestDefinition {
public:
    [[nodiscard]] std::string_view id() const noexcept { return id_.data(); }
    [[nodiscard]] std::span<const QuestObjective> objectives() const noexcept {
        return {objectives_.data(), objectiveCount_};
    }
    [[nodiscard]] std::span<const QuestBranch> branches() const noexcept {
        return {branches_.data(), branchCount_};
    }
    [[nodiscard]] const QuestBranch* branch(std::string_view id) const noexcept;
    [[nodiscard]] std::size_t branchAfter() const noexcept { return branchAfter_; }
    [[nodiscard]] std::string_view branchPrompt() const noexcept {
        return branchPrompt_.data();
    }

private:
    std::array<char, 128> id_{};
    std::array<QuestObjective, kMaxQuestObjectives> objectives_{};
    std::array<QuestBranch, kMaxQuestBranches> branches_{};
    std::array<char, 64> branchPrompt_{};
    std::size_t objectiveCount_{};
    std::size_t branchCount_{};
    std::size_t branchAfter_{};
    friend class QuestModule;
};

struct QuestProgress {
    std::size_t completed{};
    std::size_t total{};
    std::string_view objective{};
    std::span<const QuestBranch> choices{};
    bool choiceRequired{};
};

[[nodiscard]] QuestProgress evaluateQuest(
    const QuestDefinition& quest, std::span<const std::string_view> facts,
    std::string_view selectedBranch = {}) noexcept;

[[nodiscard]] bool isValidQuestBranch(const QuestDefinition& quest,
                                      std::string_view id) noexcept;

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
