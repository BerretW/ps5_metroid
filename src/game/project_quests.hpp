#pragma once

#include "echo/framework/quests.hpp"

#include <algorithm>
#include <array>

namespace echo {

inline constexpr std::string_view kMainQuestId = "echo:quest/main_route";
inline constexpr std::array<std::string_view, 3> kMainQuestFacts{
    "echo:fact/maintenance_key", "echo:fact/echo_step", "echo:fact/exit_reached"};

[[nodiscard]] inline framework::Status validateProjectQuest(
    const framework::QuestModule& module) noexcept {
    const framework::QuestDefinition* quest = module.find(kMainQuestId);
    if (quest == nullptr || quest->objectives().size() != kMainQuestFacts.size()) {
        return {framework::ErrorCode::MissingDependency, kMainQuestId,
                "The main quest must contain the three supported progression facts."};
    }
    for (const framework::QuestObjective& objective : quest->objectives()) {
        const std::string_view fact{objective.fact.data()};
        if (std::find(kMainQuestFacts.begin(), kMainQuestFacts.end(), fact) ==
            kMainQuestFacts.end()) {
            return {framework::ErrorCode::MissingDependency, kMainQuestId,
                    "The main quest contains an unsupported progression fact."};
        }
    }
    return framework::Status::success();
}

} // namespace echo
