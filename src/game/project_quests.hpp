#pragma once

#include "echo/framework/quests.hpp"
#include "echo/framework/inventory.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace echo {

inline constexpr std::string_view kMainQuestId = "echo:quest/main_route";
inline constexpr std::string_view kCargoQuestId = "echo:quest/cargo_hold_repair";
inline constexpr std::string_view kCargoQuestFact = "echo:fact/cargo_emergency_powered";
inline constexpr std::array<std::string_view, 3> kMainQuestFacts{
    "echo:fact/maintenance_key", "echo:fact/echo_step", "echo:fact/exit_reached"};
inline constexpr std::array<std::string_view, 2> kMainQuestBranchFacts{
    "echo:fact/kinetic_coil", "echo:fact/sentry_defeated"};
inline constexpr std::array<std::string_view, 2> kMainQuestBranchIds{"coil", "sentry"};
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
        if (branch == nullptr || branch->fact.data() != kMainQuestBranchFacts[index]) {
            return {framework::ErrorCode::MissingDependency, kMainQuestId,
                    "The main quest route facts must match the game contract."};
        }
    }
    const framework::QuestDefinition* cargoQuest = module.find(kCargoQuestId);
    if (cargoQuest == nullptr || cargoQuest->objectives().size() != 1 ||
        !cargoQuest->branches().empty() ||
        cargoQuest->objectives()[0].fact.data() != kCargoQuestFact) {
        return {framework::ErrorCode::MissingDependency, kCargoQuestId,
                "The Cargo Hold repair quest must have one emergency-power objective."};
    }
    return framework::Status::success();
}

[[nodiscard]] inline framework::Status validateProjectQuestRewards(
    const framework::QuestModule& quests, const framework::ItemCatalog& catalog,
    std::uint32_t itemKeyLimit) noexcept {
    for (const framework::QuestDefinition& quest : quests.definitions()) {
        for (const framework::QuestBranch& branch : quest.branches()) {
            const framework::ItemDefinition* reward = catalog.find(branch.rewardItem.data());
            if (reward == nullptr || reward->key == 0 || reward->key >= itemKeyLimit ||
                branch.rewardQuantity == 0 || branch.rewardQuantity > reward->maxStack) {
                return {framework::ErrorCode::MissingDependency, branch.rewardItem.data(),
                        "Quest rewards must match a supported project inventory item and stack."};
            }
        }
    }
    return framework::Status::success();
}

} // namespace echo
