#pragma once

#include "echo/framework/cutscenes.hpp"

namespace echo {

inline constexpr std::string_view kCentralSignalCutsceneId =
    "echo:cutscene/central_signal";

[[nodiscard]] inline framework::Status validateProjectCutscenes(
    const framework::CutsceneModule& module) noexcept {
    const framework::CutsceneDefinition* definition =
        module.find(kCentralSignalCutsceneId);
    if (definition == nullptr || definition->cards().size() != 3) {
        return {framework::ErrorCode::MissingDependency,
                kCentralSignalCutsceneId,
                "The reference game requires a three-card Central Chamber signal scene."};
    }
    return framework::Status::success();
}

} // namespace echo
