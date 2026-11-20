#include "input/dualsense_diagnostics.hpp"

#include <SDL_version.h>

#include <algorithm>
#include <cmath>

namespace echo::input {
namespace {

float normalizeAxis(Sint16 value) noexcept {
    return value < 0 ? static_cast<float>(value) / 32768.0F
                     : static_cast<float>(value) / 32767.0F;
}

class JsonWriter {
public:
    JsonWriter(ReportWriteCallback callback, void* context) noexcept
        : callback_(callback), context_(context) {}

    void raw(std::string_view value) noexcept {
        if (good_ && !callback_(context_, value.data(), value.size())) {
            good_ = false;
        }
    }

    void quoted(std::string_view value) noexcept {
        raw("\"");
        for (const char character : value) {
            switch (character) {
            case '"': raw("\\\""); break;
            case '\\': raw("\\\\"); break;
            case '\b': raw("\\b"); break;
            case '\f': raw("\\f"); break;
            case '\n': raw("\\n"); break;
            case '\r': raw("\\r"); break;
            case '\t': raw("\\t"); break;
            default:
                if (static_cast<unsigned char>(character) < 0x20U) {
                    constexpr char digits[] = "0123456789abcdef";
                    char escaped[7] = {'\\', 'u', '0', '0',
                        digits[(static_cast<unsigned char>(character) >> 4) & 0x0fU],
                        digits[static_cast<unsigned char>(character) & 0x0fU], '\0'};
                    raw(escaped);
                } else {
                    raw(std::string_view(&character, 1));
                }
            }
        }
        raw("\"");
    }

    void integer(int value) noexcept {
        char buffer[24]{};
        SDL_snprintf(buffer, sizeof(buffer), "%d", value);
        raw(buffer);
    }

    void decimal(float value, int precision = 4) noexcept {
        if (!std::isfinite(value)) {
            raw("0");
            return;
        }
        char buffer[32]{};
        SDL_snprintf(buffer, sizeof(buffer), precision == 2 ? "%.2f" : "%.4f",
                     static_cast<double>(value));
        raw(buffer);
    }

    void boolean(bool value) noexcept {
        raw(value ? "true" : "false");
    }

    void capability(std::string_view status, bool apiAvailable, bool hardwareVerified,
                    std::string_view detail) noexcept {
        raw("{\"status\":");
        quoted(status);
        raw(",\"api_available\":");
        boolean(apiAvailable);
        raw(",\"hardware_verified\":");
        boolean(hardwareVerified);
        raw(",\"detail\":");
        quoted(detail);
        raw("}");
    }

    [[nodiscard]] bool good() const noexcept { return good_; }

private:
    ReportWriteCallback callback_;
    void* context_;
    bool good_{true};
};

std::string_view inputStatus(bool observed) noexcept {
    return observed ? "SUPPORTED" : "NOT TESTED";
}

std::string_view availabilityStatus(bool available, bool failed = false) noexcept {
    if (failed) {
        return "FAILED";
    }
    return available ? "NOT TESTED" : "UNSUPPORTED";
}

std::string_view batteryNameLocal(SDL_JoystickPowerLevel level) noexcept {
    switch (level) {
    case SDL_JOYSTICK_POWER_EMPTY: return "EMPTY";
    case SDL_JOYSTICK_POWER_LOW: return "LOW";
    case SDL_JOYSTICK_POWER_MEDIUM: return "MEDIUM";
    case SDL_JOYSTICK_POWER_FULL: return "FULL";
    case SDL_JOYSTICK_POWER_WIRED: return "WIRED";
    case SDL_JOYSTICK_POWER_UNKNOWN:
    default: return "UNKNOWN";
    }
}

} // namespace

std::string_view batteryName(SDL_JoystickPowerLevel level) noexcept {
    return batteryNameLocal(level);
}

AxisPair normalizeStick(Sint16 x, Sint16 y) noexcept {
    return {normalizeAxis(x), normalizeAxis(y)};
}

AxisPair applyRadialDeadZone(AxisPair value, float deadZone) noexcept {
    const float zone = std::clamp(deadZone, 0.0F, 0.99F);
    const float magnitude = std::sqrt(value.x * value.x + value.y * value.y);
    if (magnitude <= zone || magnitude == 0.0F) {
        return {};
    }
    const float scaled = std::min((magnitude - zone) / (1.0F - zone), 1.0F) / magnitude;
    return {value.x * scaled, value.y * scaled};
}

float normalizeTrigger(Sint16 value) noexcept {
    return std::clamp(static_cast<float>(value) / 32767.0F, 0.0F, 1.0F);
}

float normalizeTouchCoordinate(float value) noexcept {
    if (!std::isfinite(value)) {
        return 0.0F;
    }
    return std::clamp(value, 0.0F, 1.0F);
}

bool writeReport(const ControllerSnapshot& snapshot, std::string_view platform,
                 ReportWriteCallback callback, void* context) noexcept {
    if (callback == nullptr) {
        return false;
    }
    JsonWriter json(callback, context);
    SDL_version version{};
    SDL_GetVersion(&version);
    json.raw("{\n  \"platform\":");
    json.quoted(platform);
    json.raw(",\n  \"sdl_version\":");
    char versionText[32]{};
    SDL_snprintf(versionText, sizeof(versionText), "%u.%u.%u",
                 version.major, version.minor, version.patch);
    json.quoted(versionText);
    json.raw(",\n  \"controller\":{\"connected\":");
    json.boolean(snapshot.connected);
    json.raw(",\"name\":");
    json.quoted(snapshot.name.data());
    json.raw(",\"instance_id\":");
    json.integer(snapshot.instanceId);
    json.raw(",\"battery\":");
    json.quoted(snapshot.connected ? batteryName(snapshot.battery) : "UNKNOWN");
    json.raw("},\n  \"observations\":{\"buttons\":");
    json.boolean(snapshot.buttonObserved);
    json.raw(",\"sticks\":");
    json.boolean(snapshot.sticksObserved);
    json.raw(",\"triggers\":");
    json.boolean(snapshot.triggersObserved);
    json.raw(",\"touch_contacts\":");
    json.boolean(snapshot.touchObserved);
    json.raw("},\n  \"axes\":{\"dead_zone\":");
    json.decimal(snapshot.deadZone, 2);
    json.raw(",\"raw\":[");
    for (std::size_t index = 0; index < snapshot.axes.size(); ++index) {
        if (index != 0) json.raw(",");
        json.integer(snapshot.axes[index]);
    }
    json.raw("],\"normalized_sticks\":[");
    const AxisPair left = applyRadialDeadZone(
        normalizeStick(snapshot.axes[SDL_CONTROLLER_AXIS_LEFTX], snapshot.axes[SDL_CONTROLLER_AXIS_LEFTY]),
        snapshot.deadZone);
    const AxisPair right = applyRadialDeadZone(
        normalizeStick(snapshot.axes[SDL_CONTROLLER_AXIS_RIGHTX], snapshot.axes[SDL_CONTROLLER_AXIS_RIGHTY]),
        snapshot.deadZone);
    json.raw("[");
    json.decimal(left.x);
    json.raw(",");
    json.decimal(left.y);
    json.raw("],[");
    json.decimal(right.x);
    json.raw(",");
    json.decimal(right.y);
    json.raw("]],\"trigger_normalized\":[");
    json.decimal(normalizeTrigger(snapshot.axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT]));
    json.raw(",");
    json.decimal(normalizeTrigger(snapshot.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT]));
    json.raw("],\"minimum\":[");
    for (std::size_t index = 0; index < snapshot.axisMinimum.size(); ++index) {
        if (index != 0) json.raw(",");
        json.integer(snapshot.axisMinimum[index]);
    }
    json.raw("],\"maximum\":[");
    for (std::size_t index = 0; index < snapshot.axisMaximum.size(); ++index) {
        if (index != 0) json.raw(",");
        json.integer(snapshot.axisMaximum[index]);
    }
    json.raw("]},\n  \"button_states\":[");
    for (std::size_t index = 0; index < snapshot.buttons.size(); ++index) {
        if (index != 0) json.raw(",");
        json.raw("{\"name\":");
        const char* name = SDL_GameControllerGetStringForButton(
            static_cast<SDL_GameControllerButton>(index));
        json.quoted(name == nullptr ? "unknown" : name);
        json.raw(",\"pressed\":");
        json.boolean(snapshot.buttons[index]);
        json.raw("}");
    }
    const bool ps5Backend = platform == "PS5";
    json.raw("],\n  \"features\":{\n    \"buttons\":");
    json.capability(snapshot.connected ? inputStatus(snapshot.buttonObserved) : "NOT TESTED",
                    snapshot.connected, snapshot.buttonObserved, "SDL game-controller button polling");
    json.raw(",\n    \"sticks\":");
    json.capability(snapshot.connected ? inputStatus(snapshot.sticksObserved) : "NOT TESTED",
                    snapshot.connected, snapshot.sticksObserved, "SDL normalized axes with configurable radial dead zone");
    json.raw(",\n    \"analog_triggers\":");
    json.capability(snapshot.connected ? inputStatus(snapshot.triggersObserved) : "NOT TESTED",
                    snapshot.connected, snapshot.triggersObserved, "SDL trigger axes normalized to 0..1");
    json.raw(",\n    \"touch_contacts\":");
    json.capability(snapshot.touchpadApiAvailable
                        ? (snapshot.touchObserved ? "SUPPORTED" : "NOT TESTED")
                        : (snapshot.connected ? "UNSUPPORTED" : "NOT TESTED"),
                    snapshot.touchpadApiAvailable, snapshot.touchpadApiAvailable && snapshot.touchObserved,
                    snapshot.touchpadApiAvailable
                        ? "SDL reported touchpad slots; active contact is observed live"
                        : "Connected backend reports no touch-contact slots; click may still be a button");
    json.raw(",\n    \"gyroscope\":");
    json.capability(snapshot.gyroAvailable
                        ? (snapshot.gyroRead ? "SUPPORTED" : "NOT TESTED")
                        : (snapshot.connected ? "UNSUPPORTED" : "NOT TESTED"),
                    snapshot.gyroAvailable, snapshot.gyroRead,
                    "SDL sensor API; angular velocity in radians per second");
    json.raw(",\n    \"accelerometer\":");
    json.capability(snapshot.accelerometerAvailable
                        ? (snapshot.accelerometerRead ? "SUPPORTED" : "NOT TESTED")
                        : (snapshot.connected ? "UNSUPPORTED" : "NOT TESTED"),
                    snapshot.accelerometerAvailable, snapshot.accelerometerRead,
                    "SDL sensor API; acceleration in meters per second squared");
    json.raw(",\n    \"rumble\":");
    const std::string_view rumbleStatus = snapshot.rumbleHardwareConfirmed ? "SUPPORTED"
        : snapshot.rumbleHardwareFailed || snapshot.rumbleCallFailed ? "FAILED"
        : availabilityStatus(snapshot.rumbleAvailable);
    json.capability(rumbleStatus, snapshot.rumbleAvailable,
                    snapshot.rumbleHardwareConfirmed || snapshot.rumbleHardwareFailed,
                    snapshot.rumbleHardwareConfirmed
                        ? "Operator confirmed physical vibration after the 250 ms test"
                        : snapshot.rumbleHardwareFailed
                            ? "Operator reported no expected vibration after the SDL test"
                            : "SDL_GameControllerRumble; API call alone does not confirm vibration");
    json.raw(",\n    \"trigger_rumble\":");
    const std::string_view triggerStatus = snapshot.triggerRumbleHardwareConfirmed ? "SUPPORTED"
        : snapshot.triggerRumbleHardwareFailed || snapshot.triggerRumbleCallFailed ? "FAILED"
        : snapshot.connected ? availabilityStatus(snapshot.triggerRumbleAvailable) : "NOT TESTED";
    json.capability(triggerStatus, snapshot.triggerRumbleAvailable,
                    snapshot.triggerRumbleHardwareConfirmed || snapshot.triggerRumbleHardwareFailed,
                    "Ordinary trigger vibration, separate from adaptive trigger resistance");
    json.raw(",\n    \"adaptive_triggers\":");
    json.capability(snapshot.connected && ps5Backend ? "UNSUPPORTED" : "NOT TESTED", false, false,
                    "No adaptive-trigger API or effect implementation in the pinned PS5 backend");
    json.raw(",\n    \"rgb_led\":");
    const std::string_view ledStatus = snapshot.ledHardwareConfirmed ? "SUPPORTED"
        : snapshot.ledHardwareFailed || snapshot.ledCallFailed ? "FAILED"
        : availabilityStatus(snapshot.ledAvailable);
    json.capability(ledStatus, snapshot.ledAvailable,
                    snapshot.ledHardwareConfirmed || snapshot.ledHardwareFailed,
                    snapshot.ledHardwareConfirmed
                        ? "Operator confirmed lightbar output"
                        : snapshot.ledHardwareFailed
                            ? "Operator reported the requested color was not visible"
                            : "SDL_GameControllerSetLED; API return does not confirm physical color");
    json.raw(",\n    \"speaker\":");
    json.capability(snapshot.connected && ps5Backend ? "UNSUPPORTED" : "NOT TESTED",
                    false, false, "Generic SDL output is not a verified controller-speaker route");
    json.raw(",\n    \"microphone\":");
    json.capability(snapshot.connected && ps5Backend ? "UNSUPPORTED" : "NOT TESTED",
                    false, false, "No controller-specific microphone capture path in the pinned PS5 backend");
    json.raw("\n  },\n  \"touchpads\":{\"count\":");
    json.integer(snapshot.touchpadCount);
    json.raw(",\"reported_finger_slots\":");
    json.integer(snapshot.touchpadFingerCount);
    json.raw("},\n  \"touches\":[");
    for (std::size_t index = 0; index < snapshot.touchSampleCount; ++index) {
        if (index != 0) json.raw(",");
        const TouchSample& touch = snapshot.touches[index];
        json.raw("{\"touchpad\":");
        json.integer(touch.touchpad);
        json.raw(",\"finger\":");
        json.integer(touch.finger);
        json.raw(",\"active\":");
        json.boolean(touch.active);
        json.raw(",\"x\":");
        json.decimal(normalizeTouchCoordinate(touch.x));
        json.raw(",\"y\":");
        json.decimal(normalizeTouchCoordinate(touch.y));
        json.raw(",\"pressure\":");
        json.decimal(normalizeTouchCoordinate(touch.pressure));
        json.raw("}");
    }
    json.raw("],\"sensors\":{\"gyro_available\":");
    json.boolean(snapshot.gyroAvailable);
    json.raw(",\"gyro_rad_s\":[");
    for (std::size_t index = 0; index < snapshot.gyro.size(); ++index) {
        if (index != 0) json.raw(",");
        json.decimal(snapshot.gyro[index], 4);
    }
    json.raw("],\"accelerometer_available\":");
    json.boolean(snapshot.accelerometerAvailable);
    json.raw(",\"accelerometer_m_s2\":[");
    for (std::size_t index = 0; index < snapshot.accelerometer.size(); ++index) {
        if (index != 0) json.raw(",");
        json.decimal(snapshot.accelerometer[index], 4);
    }
    json.raw("]},\"audio_devices\":{\"output_count\":");
    json.integer(snapshot.outputDeviceCount);
    json.raw(",\"capture_count\":");
    json.integer(snapshot.captureDeviceCount);
    json.raw(",\"output_names\":[");
    for (int index = 0; index < snapshot.outputDeviceCount; ++index) {
        if (index != 0) json.raw(",");
        const char* name = SDL_GetAudioDeviceName(index, 0);
        json.quoted(name == nullptr ? "UNKNOWN" : name);
    }
    json.raw("],\"capture_names\":[");
    for (int index = 0; index < snapshot.captureDeviceCount; ++index) {
        if (index != 0) json.raw(",");
        const char* name = SDL_GetAudioDeviceName(index, 1);
        json.quoted(name == nullptr ? "UNKNOWN" : name);
    }
    json.raw("]},\"last_test\":{\"action\":");
    json.quoted(snapshot.lastAction.data());
    json.raw(",\"result\":");
    if (snapshot.hasResult) {
        json.integer(snapshot.lastResult);
    } else {
        json.raw("null");
    }
    json.raw(",\"error\":");
    json.quoted(snapshot.lastError.data());
    json.raw("}\n}\n");
    return json.good();
}

} // namespace echo::input
