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
        quest->branches().size() != kMainQuestBranchIds.size() ||
        quest->branchAfter() == 0 || quest->branchAfter() >= quest->objectives().size()) {
        return {framework::ErrorCode::MissingDependency, kMainQuestId,
                "The main quest must contain its three objectives and two route choices."};
    }
    std::size_t keyIndex = quest->objectives().size();
    std::size_t exitIndex = quest->objectives().size();
    for (const std::string_view fact : kMainQuestFacts) {
        const auto found = std::find_if(
            quest->objectives().begin(), quest->objectives().end(),
            [fact](const framework::QuestObjective& objective) {
                return std::string_view(objective.fact.data()) == fact;
            });
        if (found == quest->objectives().end()) {
            return {framework::ErrorCode::MissingDependency, kMainQuestId,
                    "The main quest must contain each supported progression fact."};
        }
        const std::size_t index = static_cast<std::size_t>(found - quest->objectives().begin());
        if (fact == kMainQuestFacts[0]) keyIndex = index;
        if (fact == kMainQuestFacts[2]) exitIndex = index;
    }
    if (keyIndex >= quest->branchAfter() || exitIndex < quest->branchAfter()) {
        return {framework::ErrorCode::InvalidValue, kMainQuestId,
                "The route choice must follow the key and precede the exit objective."};
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
