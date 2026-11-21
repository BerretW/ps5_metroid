#pragma once

#include "echo/framework/data.hpp"

#include <SDL.h>

#include <cstdint>
#include <limits>
#include <string_view>

namespace echo::platform {

struct Bindings {
    SDL_Keycode jump{SDLK_SPACE};
    SDL_Keycode shoot{SDLK_j};
    SDL_Keycode interact{SDLK_e};
};

namespace settings_detail {

inline bool validBindingKey(SDL_Keycode key) noexcept {
    const SDL_Scancode scancode = SDL_GetScancodeFromKey(key);
    return key != SDLK_UNKNOWN && scancode > SDL_SCANCODE_UNKNOWN &&
           scancode < SDL_NUM_SCANCODES;
}

} // namespace settings_detail

[[nodiscard]] inline framework::Status encodeSettings(
    int volume, const Bindings& bindings, framework::GameState& output) noexcept {
    using namespace framework;
    if (volume < 0 || volume > 100 ||
        !settings_detail::validBindingKey(bindings.jump) ||
        !settings_detail::validBindingKey(bindings.shoot) ||
        !settings_detail::validBindingKey(bindings.interact)) {
        return {ErrorCode::InvalidValue, "settings",
                "Volume or keyboard binding is invalid."};
    }
    GameState candidate{};
    const auto setInteger = [&candidate](std::string_view key, std::int64_t value) {
        return candidate.set({StateScope::Global, {}, key}, StateValue::fromInteger(value));
    };
    if (const Status status = setInteger("audio.volume", volume); !status) return status;
    if (const Status status = setInteger("input.jump", bindings.jump); !status) return status;
    if (const Status status = setInteger("input.shoot", bindings.shoot); !status) return status;
    if (const Status status = setInteger("input.interact", bindings.interact); !status) return status;
    return output.replaceValues(candidate);
}

[[nodiscard]] inline framework::Status decodeSettings(
    const framework::GameState& state, int& volume, Bindings& bindings) noexcept {
    using namespace framework;
    const auto readInteger = [&state](std::string_view key, std::int64_t& value) {
        StateValue stateValue{};
        if (!state.get({StateScope::Global, {}, key}, stateValue) ||
            stateValue.type != ValueType::Integer) {
            return false;
        }
        value = stateValue.integer;
        return true;
    };
    std::int64_t savedVolume{};
    std::int64_t savedJump{};
    std::int64_t savedShoot{};
    std::int64_t savedInteract{};
    if (!readInteger("audio.volume", savedVolume) || savedVolume < 0 || savedVolume > 100 ||
        !readInteger("input.jump", savedJump) ||
        savedJump < std::numeric_limits<SDL_Keycode>::min() ||
        savedJump > std::numeric_limits<SDL_Keycode>::max() ||
        !readInteger("input.shoot", savedShoot) ||
        savedShoot < std::numeric_limits<SDL_Keycode>::min() ||
        savedShoot > std::numeric_limits<SDL_Keycode>::max() ||
        !readInteger("input.interact", savedInteract) ||
        savedInteract < std::numeric_limits<SDL_Keycode>::min() ||
        savedInteract > std::numeric_limits<SDL_Keycode>::max()) {
        return {ErrorCode::CorruptData, "settings",
                "Settings save is missing required values or contains invalid types."};
    }
    const Bindings candidate{
        static_cast<SDL_Keycode>(savedJump),
        static_cast<SDL_Keycode>(savedShoot),
        static_cast<SDL_Keycode>(savedInteract),
    };
    if (!settings_detail::validBindingKey(candidate.jump) ||
        !settings_detail::validBindingKey(candidate.shoot) ||
        !settings_detail::validBindingKey(candidate.interact)) {
        return {ErrorCode::CorruptData, "settings",
                "Settings save contains an unsupported keyboard binding."};
    }
    volume = static_cast<int>(savedVolume);
    bindings = candidate;
    return Status::success();
}

} // namespace echo::platform
