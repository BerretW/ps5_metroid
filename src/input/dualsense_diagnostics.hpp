#pragma once

#include <SDL.h>

#include <array>
#include <string_view>

namespace echo::input {

inline constexpr Uint32 kMaxTestEffectDurationMs = 250;

struct AxisPair {
    float x{};
    float y{};
};

struct TouchSample {
    int touchpad{};
    int finger{};
    bool active{};
    float x{};
    float y{};
    float pressure{};
};

struct ControllerSnapshot {
    bool connected{};
    std::array<char, 128> name{};
    SDL_JoystickID instanceId{-1};
    SDL_JoystickPowerLevel battery{SDL_JOYSTICK_POWER_UNKNOWN};
    std::array<Sint16, SDL_CONTROLLER_AXIS_MAX> axes{};
    std::array<bool, SDL_CONTROLLER_BUTTON_MAX> buttons{};
    TouchSample* touches{};
    std::size_t touchSampleCount{};
    std::size_t touchSampleCapacity{};
    int touchpadCount{};
    int touchpadFingerCount{};
    bool touchpadApiAvailable{};
    bool gyroAvailable{};
    bool accelerometerAvailable{};
    bool gyroRead{};
    bool accelerometerRead{};
    std::array<float, 3> gyro{};
    std::array<float, 3> accelerometer{};
    std::array<Sint16, SDL_CONTROLLER_AXIS_MAX> axisMinimum{};
    std::array<Sint16, SDL_CONTROLLER_AXIS_MAX> axisMaximum{};
    bool calibrationInitialized{};
    float deadZone{0.15F};
    bool rumbleAvailable{};
    bool triggerRumbleAvailable{};
    bool ledAvailable{};
    bool buttonObserved{};
    bool sticksObserved{};
    bool triggersObserved{};
    bool touchObserved{};
    std::array<char, 64> lastAction{};
    std::array<char, 256> lastError{};
    int lastResult{};
    bool hasResult{};
    bool rumbleCallFailed{};
    bool ledCallFailed{};
    bool rumbleHardwareConfirmed{};
    bool rumbleHardwareFailed{};
    bool triggerRumbleCallFailed{};
    bool triggerRumbleHardwareConfirmed{};
    bool triggerRumbleHardwareFailed{};
    bool ledHardwareConfirmed{};
    bool ledHardwareFailed{};
    int outputDeviceCount{};
    int captureDeviceCount{};
};

using ReportWriteCallback = bool (*)(void* context, const char* data, std::size_t size);

template <std::size_t Size>
void setText(std::array<char, Size>& destination, std::string_view value) noexcept {
    const std::size_t count = value.size() < Size - 1 ? value.size() : Size - 1;
    SDL_memcpy(destination.data(), value.data(), count);
    destination[count] = '\0';
}

[[nodiscard]] AxisPair normalizeStick(Sint16 x, Sint16 y) noexcept;
[[nodiscard]] AxisPair applyRadialDeadZone(AxisPair value, float deadZone) noexcept;
[[nodiscard]] float normalizeTrigger(Sint16 value) noexcept;
[[nodiscard]] float normalizeTouchCoordinate(float value) noexcept;
[[nodiscard]] bool writeReport(const ControllerSnapshot& snapshot, std::string_view platform,
                               ReportWriteCallback callback, void* context) noexcept;
[[nodiscard]] std::string_view batteryName(SDL_JoystickPowerLevel level) noexcept;

} // namespace echo::input
