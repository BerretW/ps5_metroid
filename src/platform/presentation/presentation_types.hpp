#pragma once

#include <cstdint>

namespace echo::platform {

enum class ControlScheme { Keyboard, PlayStation };

// PS5 always presents fullscreen and ignores the selected Windows mode.
enum class DisplayMode : std::uint8_t { Window720, Window1080, Fullscreen, Count };

struct RenderDebugInfo {
    bool vulkan{};
    bool timingAvailable{};
    bool backendStatsAvailable{};
    unsigned fps{};
    unsigned frameTenthsMs{};
    std::uint32_t drawCalls{};
    std::uint32_t triangles{};
};

class DamageFlash {
public:
    static constexpr unsigned kDurationSteps = 12;
    static constexpr std::uint8_t kMaximumOpacity = 64;

    void trigger() noexcept { remaining_ = kDurationSteps; }
    void step() noexcept { if (remaining_ != 0) --remaining_; }
    void clear() noexcept { remaining_ = 0; }
    [[nodiscard]] std::uint8_t opacity() const noexcept {
        return static_cast<std::uint8_t>(kMaximumOpacity * remaining_ / kDurationSteps);
    }

private:
    unsigned remaining_{};
};

} // namespace echo::platform
