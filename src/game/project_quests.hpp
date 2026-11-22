#pragma once

#include "echo/framework/quests.hpp"

#include <algorithm>
#include <array>

namespace echo {

inline constexpr std::string_view kMainQuestId = "echo:quest/main_route";
inline constexpr std::array<std::string_view, 3> kMainQuestFacts{
    "echo:fact/maintenance_key", "echo:fact/echo_step", "echo:fact/exit_reached"};
inline constexpr std::array<std::string_view, 2> kMainQuestBranchFacts{
    "echo:fact/kinetic_coil", "echo:fact/sentry_defeated"};
inline constexpr std::array<std::string_view, 2> kMainQuestBranchIds{"coil", "sentry"};
inline constexpr std::array<std::string_view, 2> kMainQuestRewards{
    "echo:item/shield_cell", "echo:item/med_kit"};

[[nodiscard]] inline framework::Status validateProjectQuest(
    const framework::QuestModule& module) noexcept {
    const framework::QuestDefinition* quest = module.find(kMainQuestId);
    if (quest == nullptr || quest->objectives().size() != kMainQuestFacts.size() ||
        quest->branches().size() != kMainQuestBranchIds.size() || quest->branchAfter() != 1) {
        return {framework::ErrorCode::MissingDependency, kMainQuestId,
                "The main quest must contain its three objectives and two route choices."};
    }
    for (std::size_t index = 0; index < kMainQuestFacts.size(); ++index) {
        const framework::QuestObjective& objective = quest->objectives()[index];
        if (objective.fact.data() != kMainQuestFacts[index]) {
            return {framework::ErrorCode::MissingDependency, kMainQuestId,
                    "The main quest objective order must match the game progression."};
        }
    }
    for (std::size_t index = 0; index < kMainQuestBranchIds.size(); ++index) {
        const framework::QuestBranch* branch = quest->branch(kMainQuestBranchIds[index]);
        if (branch == nullptr || branch->fact.data() != kMainQuestBranchFacts[index] ||
            branch->rewardItem.data() != kMainQuestRewards[index] ||
            branch->rewardQuantity != 1) {
            return {framework::ErrorCode::MissingDependency, kMainQuestId,
                    "The main quest route facts and rewards must match the game contract."};
        }
    }
    return framework::Status::success();
}

} // namespace echo
