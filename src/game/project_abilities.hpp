#pragma once

#include "echo/framework/abilities.hpp"

#include <array>
#include <string_view>

namespace echo {

inline constexpr std::string_view kEchoStepAbilityId = "echo:ability/echo_step";
inline constexpr std::array<framework::AbilityDefinition, 1>
    kProjectAbilityDefinitions{{{kEchoStepAbilityId}}};

} // namespace echo
