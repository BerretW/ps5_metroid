#pragma once

#include "echo/framework/canvas2d.hpp"
#include "echo/framework/narrative.hpp"
#include "platform/presentation/presentation_types.hpp"

#include <cstddef>
#include <string_view>

namespace echo {
class Game;
}

namespace echo::platform {

struct OptionsPresentation {
    int selection{};
    int volume{};
    framework::DialogueLanguage language{};
    bool waitingForKey{};
    std::string_view jumpKey{};
    std::string_view shootKey{};
    std::string_view interactKey{};
    DisplayMode display{DisplayMode::Window720};
    bool renderDebug{};
    bool hasDisplayOption{};
};

[[nodiscard]] bool drawMainMenu(rendering::ICanvas2D& canvas, ControlScheme controls,
                                int selection, bool continueAvailable,
                                bool modelWindow) noexcept;
[[nodiscard]] bool drawOptionsMenu(rendering::ICanvas2D& canvas, ControlScheme controls,
                                   const OptionsPresentation& options) noexcept;
[[nodiscard]] bool drawInventoryMenu(rendering::ICanvas2D& canvas, ControlScheme controls,
                                     const Game& game, std::size_t selected,
                                     std::size_t source, std::string_view message) noexcept;

} // namespace echo::platform
