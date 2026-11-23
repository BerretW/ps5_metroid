#pragma once

#include "echo/framework/data.hpp"
#include "platform/presentation/presentation_types.hpp"

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
    if (key == SDLK_UNKNOWN) return false;
    const Uint32 code = static_cast<Uint32>(key);
    const Uint32 scancodeMask = static_cast<Uint32>(SDLK_SCANCODE_MASK);
    if ((code & scancodeMask) != 0) {
        const Uint32 scancode = code & ~scancodeMask;
        return scancode > SDL_SCANCODE_UNKNOWN && scancode < SDL_NUM_SCANCODES;
    }
    return key >= SDLK_SPACE && key <= SDLK_DELETE;
}

} // namespace settings_detail

[[nodiscard]] inline framework::Status encodeSettings(
    int volume, const Bindings& bindings, framework::DialogueLanguage language,
    framework::GameState& output, DisplayMode display = DisplayMode::Window720,
    bool renderDebug = false) noexcept {
    using namespace framework;
    if (volume < 0 || volume > 100 ||
        (language != DialogueLanguage::English && language != DialogueLanguage::Czech) ||
        display >= DisplayMode::Count ||
        !settings_detail::validBindingKey(bindings.jump) ||
        !settings_detail::validBindingKey(bindings.shoot) ||
        !settings_detail::validBindingKey(bindings.interact)) {
        return {ErrorCode::InvalidValue, "settings",
                "Volume, display mode or keyboard binding is invalid."};
    }
    GameState candidate{};
    const auto setInteger = [&candidate](std::string_view key, std::int64_t value) {
        return candidate.set({StateScope::Global, {}, key}, StateValue::fromInteger(value));
    };
    if (const Status status = setInteger("audio.volume", volume); !status) return status;
    if (const Status status = setInteger("input.jump", bindings.jump); !status) return status;
    if (const Status status = setInteger("input.shoot", bindings.shoot); !status) return status;
    if (const Status status = setInteger("input.interact", bindings.interact); !status) return status;
    if (const Status status = setInteger("dialogue.language", static_cast<std::int64_t>(language));
        !status) return status;
    if (const Status status = setInteger("video.display", static_cast<std::int64_t>(display));
        !status) return status;
    if (const Status status = setInteger("video.render_debug", renderDebug ? 1 : 0);
        !status) return status;
    return output.replaceValues(candidate);
}

[[nodiscard]] inline framework::Status decodeSettings(
    const framework::GameState& state, int& volume, Bindings& bindings,
    framework::DialogueLanguage& language, DisplayMode& display, bool& renderDebug) noexcept {
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
    std::int64_t savedLanguage{};
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
    const bool hasSavedLanguage = readInteger("dialogue.language", savedLanguage);
    if (hasSavedLanguage &&
        savedLanguage != static_cast<std::int64_t>(DialogueLanguage::English) &&
        savedLanguage != static_cast<std::int64_t>(DialogueLanguage::Czech)) {
        return {ErrorCode::CorruptData, "settings",
                "Settings save contains an unsupported dialogue language."};
    }
    if (!hasSavedLanguage && state.contains({StateScope::Global, {}, "dialogue.language"})) {
        return {ErrorCode::CorruptData, "settings",
                "Settings save contains an invalid dialogue language value."};
    }
    // Settings written before display modes existed fall back to the 1280x720 window.
    std::int64_t savedDisplay{};
    const bool hasSavedDisplay = readInteger("video.display", savedDisplay);
    if ((hasSavedDisplay &&
         (savedDisplay < 0 || savedDisplay >= static_cast<std::int64_t>(DisplayMode::Count))) ||
        (!hasSavedDisplay && state.contains({StateScope::Global, {}, "video.display"}))) {
        return {ErrorCode::CorruptData, "settings",
                "Settings save contains an unsupported display mode."};
    }
    std::int64_t savedRenderDebug{};
    const bool hasSavedRenderDebug = readInteger("video.render_debug", savedRenderDebug);
    if ((hasSavedRenderDebug && savedRenderDebug != 0 && savedRenderDebug != 1) ||
        (!hasSavedRenderDebug && state.contains({StateScope::Global, {}, "video.render_debug"}))) {
        return {ErrorCode::CorruptData, "settings",
                "Settings save contains an invalid render debug value."};
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
    language = hasSavedLanguage ? static_cast<DialogueLanguage>(savedLanguage)
                                : DialogueLanguage::English;
    display = hasSavedDisplay ? static_cast<DisplayMode>(savedDisplay) : DisplayMode::Window720;
    renderDebug = hasSavedRenderDebug && savedRenderDebug == 1;
    return Status::success();
}

[[nodiscard]] inline framework::Status decodeSettings(
    const framework::GameState& state, int& volume, Bindings& bindings,
    framework::DialogueLanguage& language, DisplayMode& display) noexcept {
    bool renderDebug{};
    return decodeSettings(state, volume, bindings, language, display, renderDebug);
}

[[nodiscard]] inline framework::Status decodeSettings(
    const framework::GameState& state, int& volume, Bindings& bindings,
    framework::DialogueLanguage& language) noexcept {
    DisplayMode display{};
    return decodeSettings(state, volume, bindings, language, display);
}

} // namespace echo::platform
