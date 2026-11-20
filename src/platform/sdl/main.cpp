#include "game/game.hpp"
#include "platform/sdl/renderer.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#ifdef __PROSPERO__
#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
#else
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#endif
#if defined(__PROSPERO__) && defined(ECHO_PS5_STARTUP_DIAGNOSTICS)
#include <fcntl.h>

extern "C" {
    int sceKernelOpen(const char* path, int flags, unsigned int mode);
    std::int64_t sceKernelWrite(int descriptor, const void* buffer, std::size_t size);
    int sceKernelFsync(int descriptor);
    int sceKernelClose(int descriptor);
    int sceKernelUsleep(std::uint32_t microseconds);
    int sceKernelSendNotificationRequest(std::uint32_t device, void* request, std::size_t size, int blocking);
}
#endif

namespace {

#ifdef __PROSPERO__
constexpr auto kControlScheme = echo::platform::ControlScheme::PlayStation;
#else
constexpr auto kControlScheme = echo::platform::ControlScheme::Keyboard;
#endif

#if defined(__PROSPERO__) && defined(ECHO_PS5_STARTUP_DIAGNOSTICS)
void diagnosticNotify(std::string_view message) noexcept {
    struct Notification {
        std::uint8_t reserved[45];
        char message[3075];
    };
    static Notification notification{};
    const std::size_t count = std::min(message.size(), sizeof(notification.message) - 1);
    for (std::size_t index = 0; index < count; ++index) {
        notification.message[index] = message[index];
    }
    notification.message[count] = '\0';
    if (sceKernelSendNotificationRequest(0, &notification, sizeof(notification), 0) < 0) {
        constexpr char error[] = "ECHO DIAG notification failed\n";
        (void)sceKernelWrite(2, error, sizeof(error) - 1);
    }
}

// Kernel I/O deliberately avoids SDL, FILE objects, allocation and errno.
void diagnosticCheckpoint(std::string_view message) noexcept {
    diagnosticNotify(message);
    int descriptor = sceKernelOpen("/data/project_echo-startup.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (descriptor < 0) {
        diagnosticNotify("ECHO DIAG: /data log unavailable; trying /app0");
        descriptor = sceKernelOpen("/app0/project_echo-startup.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    }
    if (descriptor < 0) {
        diagnosticNotify("ECHO DIAG: startup log unavailable; report stage notifications");
        return;
    }
    std::size_t written = 0;
    while (written < message.size()) {
        const auto count = sceKernelWrite(descriptor, message.data() + written, message.size() - written);
        if (count <= 0 || static_cast<std::uint64_t>(count) > message.size() - written) {
            diagnosticNotify("ECHO DIAG: startup log write failed");
            break;
        }
        written += static_cast<std::size_t>(count);
    }
    if (sceKernelWrite(descriptor, "\n", 1) != 1 || sceKernelFsync(descriptor) != 0) {
        diagnosticNotify("ECHO DIAG: startup log flush failed");
    }
    if (sceKernelClose(descriptor) != 0) {
        diagnosticNotify("ECHO DIAG: startup log close failed");
    }
}

[[noreturn]] void diagnosticFailure(std::string_view stage) noexcept {
    diagnosticCheckpoint(stage);
    const char* error = SDL_GetError();
    diagnosticCheckpoint(error != nullptr && error[0] != '\0' ? error : "SDL supplied no error message");
    // Upstream Hello also remains in-process rather than returning through exit.
    for (;;) {
        (void)sceKernelUsleep(1000000);
    }
}
#endif

enum class Screen {
    MainMenu,
    Options,
    Playing,
    DualSenseLab
};

enum class RebindAction {
    None,
    Jump,
    Shoot,
    Interact
};

struct Bindings {
    SDL_Keycode jump{SDLK_SPACE};
    SDL_Keycode shoot{SDLK_j};
    SDL_Keycode interact{SDLK_e};
};

void logMessage(std::string_view message) {
#ifdef __PROSPERO__
    SDL_Log("%.*s", static_cast<int>(message.size()), message.data());
    for (const char* directory : {"/data/project_echo", "/data/project_echo/logs"}) {
        if (mkdir(directory, 0777) != 0 && errno != EEXIST) {
            SDL_Log("Could not create %s: errno %d", directory, errno);
            return;
        }
    }
    FILE* log = std::fopen("/data/project_echo/logs/project_echo.log", "a");
    if (log == nullptr) {
        SDL_Log("Could not open PROJECT ECHO log: errno %d", errno);
        return;
    }
    const bool writeFailed = std::fprintf(log, "%.*s\n", static_cast<int>(message.size()), message.data()) < 0;
    if (std::fclose(log) != 0 || writeFailed) {
        SDL_Log("Could not write PROJECT ECHO log: errno %d", errno);
    }
#else
    std::cout << message << '\n';
    std::error_code error;
    std::filesystem::create_directories("logs", error);
    if (error) {
        std::cerr << "Could not create logs directory: " << error.message() << '\n';
        return;
    }
    std::ofstream log("logs/project_echo.log", std::ios::app);
    if (!log) {
        std::cerr << "Could not open logs/project_echo.log\n";
        return;
    }
    log << message << '\n';
#endif
}

auto keyName(SDL_Keycode key) {
    const char* name = SDL_GetKeyName(key);
#ifdef __PROSPERO__
    std::array<char, 64> copy{};
    SDL_strlcpy(copy.data(), name == nullptr ? "UNKNOWN" : name, copy.size());
    return copy;
#else
    return name == nullptr ? std::string{"UNKNOWN"} : std::string{name};
#endif
}

class Audio {
public:
    bool open() noexcept {
        SDL_AudioSpec desired{};
        desired.freq = 48000;
        desired.format = AUDIO_S16SYS;
        desired.channels = 1;
        desired.samples = 512;
        device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained_, 0);
        if (device_ == 0) {
            SDL_Log("Audio unavailable; continuing without sound: %s", SDL_GetError());
            return false;
        }
        SDL_PauseAudioDevice(device_, 0);
        return true;
    }

    void playShot(int volume) noexcept {
        if (device_ == 0 || SDL_GetQueuedAudioSize(device_) > 8192) {
            return;
        }
        std::array<std::int16_t, 640> samples{};
        constexpr double twoPi = 6.283185307179586;
        for (std::size_t index = 0; index < samples.size(); ++index) {
            const double envelope = 1.0 - static_cast<double>(index) / samples.size();
            const double wave = std::sin(twoPi * 560.0 * static_cast<double>(index) / obtained_.freq);
            samples[index] = static_cast<std::int16_t>(wave * envelope * volume * 85.0);
        }
        if (SDL_QueueAudio(device_, samples.data(), static_cast<Uint32>(sizeof(samples))) != 0) {
            SDL_Log("Could not queue shot audio: %s", SDL_GetError());
        }
    }

    void close() noexcept {
        if (device_ != 0) {
            SDL_ClearQueuedAudio(device_);
            SDL_CloseAudioDevice(device_);
            device_ = 0;
        }
    }

private:
    SDL_AudioDeviceID device_{};
    SDL_AudioSpec obtained_{};
};

bool writeDualSenseReport(const echo::input::ControllerSnapshot& controller,
                          std::array<char, 256>& error) {
#ifdef __PROSPERO__
    constexpr const char* path = "/data/project_echo/logs/dualsense_report.json";
    if (mkdir("/data/project_echo", 0777) != 0 && errno != EEXIST) {
        echo::input::setText(error, "Cannot create /data/project_echo");
        return false;
    }
    if (mkdir("/data/project_echo/logs", 0777) != 0 && errno != EEXIST) {
        echo::input::setText(error, "Cannot create /data/project_echo/logs");
        return false;
    }
    FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        echo::input::setText(error, "Cannot open /data/project_echo/logs/dualsense_report.json");
        return false;
    }
    const auto writeChunk = [](void* context, const char* data, std::size_t size) {
        return std::fwrite(data, 1, size, static_cast<FILE*>(context)) == size;
    };
    const bool writeSucceeded = echo::input::writeReport(controller, "PS5", writeChunk, file);
    const bool closeFailed = std::fclose(file) != 0;
    if (!writeSucceeded || closeFailed) {
        echo::input::setText(error, "Failed writing DualSense report");
        return false;
    }
#else
    constexpr const char* path = "logs/dualsense_report.json";
    std::error_code filesystemError;
    std::filesystem::create_directories("logs", filesystemError);
    if (filesystemError) {
        echo::input::setText(error, "Cannot create logs directory");
        return false;
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        echo::input::setText(error, "Cannot open logs/dualsense_report.json");
        return false;
    }
    const auto writeChunk = [](void* context, const char* data, std::size_t size) {
        auto* output = static_cast<std::ofstream*>(context);
        output->write(data, static_cast<std::streamsize>(size));
        return static_cast<bool>(*output);
    };
    const bool writeSucceeded = echo::input::writeReport(controller, "Windows", writeChunk, &file);
    file.flush();
    if (!file) {
        echo::input::setText(error, "Failed writing logs/dualsense_report.json");
        return false;
    }
    if (!writeSucceeded) {
        echo::input::setText(error, "Failed writing logs/dualsense_report.json");
        return false;
    }
#endif
    error.fill('\0');
    return true;
}

void setLabResult(echo::input::ControllerSnapshot& snapshot, std::string_view action,
                  int result, std::string_view error = {}) {
    echo::input::setText(snapshot.lastAction, action);
    snapshot.lastResult = result;
    snapshot.hasResult = true;
    echo::input::setText(snapshot.lastError, error);
}

template <std::size_t Size>
bool textEquals(const std::array<char, Size>& value, std::string_view expected) noexcept {
    return std::string_view(value.data()) == expected;
}

} // namespace

#if defined(__PROSPERO__) && defined(ECHO_PS5_STARTUP_DIAGNOSTICS)
extern "C" void echo_ps5_video_checkpoint(const char* message) {
    diagnosticCheckpoint(message);
}

extern "C" void echo_ps5_video_status(const char* label, int result) {
    std::array<char, 128> message{};
    std::size_t length = 0;
    while (length < message.size() - 12 && label[length] != '\0') {
        message[length] = label[length];
        ++length;
    }
    message[length++] = ' ';
    message[length++] = '0';
    message[length++] = 'x';
    constexpr char digits[] = "0123456789ABCDEF";
    const auto value = static_cast<std::uint32_t>(result);
    for (int shift = 28; shift >= 0; shift -= 4) {
        message[length++] = digits[(value >> shift) & 0xF];
    }
    diagnosticCheckpoint(std::string_view(message.data(), length));
}
#endif

int main(int, char**) {
#if defined(__PROSPERO__) && defined(ECHO_PS5_STARTUP_DIAGNOSTICS)
    diagnosticCheckpoint("ECHO DIAG 01: main reached");
#endif
    SDL_SetMainReady();
#if defined(__PROSPERO__) && defined(ECHO_PS5_STARTUP_DIAGNOSTICS)
    diagnosticCheckpoint("ECHO DIAG 02: before SDL core");
    if (SDL_Init(0) != 0) {
        diagnosticFailure("ECHO DIAG ERROR: SDL core initialization failed");
    }
    diagnosticCheckpoint("ECHO DIAG 03: core ready; before SDL video");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        diagnosticFailure("ECHO DIAG ERROR: SDL video initialization failed");
    }
    diagnosticCheckpoint("ECHO DIAG 04: video ready; before game controller");
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        diagnosticFailure("ECHO DIAG ERROR: SDL controller initialization failed");
    }
    diagnosticCheckpoint("ECHO DIAG 05: controller ready; before normal logger");
#else
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
#ifdef __PROSPERO__
        std::fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
#else
        std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
#endif
        return 1;
    }
#endif
    logMessage("SDL video and game-controller subsystems initialized");

#if defined(__PROSPERO__) && defined(ECHO_PS5_STARTUP_DIAGNOSTICS)
    diagnosticCheckpoint("ECHO DIAG 06: before renderer creation");
#endif
    echo::platform::Renderer renderer{kControlScheme};
    if (!renderer.create()) {
#if defined(__PROSPERO__) && defined(ECHO_PS5_STARTUP_DIAGNOSTICS)
        diagnosticFailure("ECHO DIAG ERROR: renderer creation failed");
#endif
#ifdef __PROSPERO__
        std::fprintf(stderr, "SDL renderer creation failed: %s\n", SDL_GetError());
#else
        std::cerr << "SDL renderer creation failed: " << SDL_GetError() << '\n';
#endif
        SDL_Quit();
        return 1;
    }
#if defined(__PROSPERO__) && defined(ECHO_PS5_STARTUP_DIAGNOSTICS)
    diagnosticCheckpoint("ECHO DIAG 07: renderer ready; before normal logger");
#endif
    logMessage("SDL window and renderer created");

#if defined(__PROSPERO__) && defined(ECHO_PS5_STARTUP_DIAGNOSTICS)
    diagnosticCheckpoint("ECHO DIAG 07a: before audio subsystem/device");
#endif
    Audio audio;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0 && audio.open()) {
        logMessage("SDL audio device opened at the platform-selected format");
    } else {
        SDL_Log("SDL audio initialization failed; continuing without audio: %s", SDL_GetError());
    }
#if defined(__PROSPERO__) && defined(ECHO_PS5_STARTUP_DIAGNOSTICS)
    diagnosticCheckpoint("ECHO DIAG 08: audio attempt completed; before game state");
#endif

    echo::Game game;
    Bindings bindings;
    Screen screen = Screen::MainMenu;
    RebindAction rebindAction = RebindAction::None;
    bool running = true;
    bool paused = false;
    bool showMap = false;
    bool waitingForKey = false;
    int menuSelection = 0;
    int optionSelection = 0;
    int volume = 60;
    bool pendingJump = false;
    bool pendingInteract = false;
    bool pendingPause = false;
    std::array<bool, SDL_NUM_SCANCODES> keys{};
    SDL_GameController* controller = nullptr;
    echo::input::ControllerSnapshot controllerSnapshot;
    echo::input::setText(controllerSnapshot.lastAction, "NO TEST RUN");
    bool labRumbleActive = false;
    bool labTriggerRumbleActive = false;
    bool labLedChanged = false;
    bool labSensorsEnabled = false;
    std::uint64_t labRumbleStopAt = 0;
    std::uint64_t labTriggerRumbleStopAt = 0;
    int labLedColor = -1;

    for (int index = 0; index < SDL_NumJoysticks(); ++index) {
        if (SDL_IsGameController(index) == SDL_TRUE) {
            controller = SDL_GameControllerOpen(index);
            if (controller != nullptr) {
                logMessage("SDL game controller detected");
                break;
            }
        }
    }
#if defined(__PROSPERO__) && defined(ECHO_PS5_STARTUP_DIAGNOSTICS)
    diagnosticCheckpoint("ECHO DIAG 09: game/controller enumeration ready; before clock");
    bool diagnosticFirstFrame = true;
#endif

    const auto stopLabOutput = [&]() {
        if (controller != nullptr && labRumbleActive) {
            SDL_ClearError();
            if (SDL_GameControllerRumble(controller, 0, 0, 0) != 0) {
                SDL_Log("Could not stop DualSense rumble: %s", SDL_GetError());
            }
        }
        labRumbleActive = false;
        labRumbleStopAt = 0;
        if (controller != nullptr && labTriggerRumbleActive) {
            SDL_ClearError();
            if (SDL_GameControllerRumbleTriggers(controller, 0, 0, 0) != 0) {
                SDL_Log("Could not stop DualSense trigger rumble: %s", SDL_GetError());
            }
        }
        labTriggerRumbleActive = false;
        labTriggerRumbleStopAt = 0;
        if (controller != nullptr && labLedChanged) {
            SDL_ClearError();
            if (SDL_GameControllerSetLED(controller, 0, 0, 0) != 0) {
                SDL_Log("Could not reset DualSense lightbar: %s", SDL_GetError());
            }
        }
        labLedChanged = false;
        labLedColor = -1;
        if (controller != nullptr && labSensorsEnabled) {
            if (SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO) == SDL_TRUE) {
                (void)SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_GYRO, SDL_FALSE);
            }
            if (SDL_GameControllerHasSensor(controller, SDL_SENSOR_ACCEL) == SDL_TRUE) {
                (void)SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_ACCEL, SDL_FALSE);
            }
        }
        labSensorsEnabled = false;
    };

    const auto enableLabSensors = [&]() {
        if (controller == nullptr) {
            return;
        }
        if (SDL_InitSubSystem(SDL_INIT_SENSOR) != 0) {
            setLabResult(controllerSnapshot, "SENSOR INITIALIZATION", -1, SDL_GetError());
            return;
        }
        for (const SDL_SensorType type : {SDL_SENSOR_GYRO, SDL_SENSOR_ACCEL}) {
            if (SDL_GameControllerHasSensor(controller, type) == SDL_TRUE &&
                SDL_GameControllerSetSensorEnabled(controller, type, SDL_TRUE) != 0) {
                setLabResult(controllerSnapshot, "SENSOR ENABLE", -1, SDL_GetError());
                logMessage(controllerSnapshot.lastError.data());
            }
        }
        labSensorsEnabled = true;
    };

    const auto resetControllerObservations = [&]() {
        controllerSnapshot.buttonObserved = false;
        controllerSnapshot.sticksObserved = false;
        controllerSnapshot.triggersObserved = false;
        controllerSnapshot.touchObserved = false;
        controllerSnapshot.gyroRead = false;
        controllerSnapshot.accelerometerRead = false;
        controllerSnapshot.calibrationInitialized = false;
        controllerSnapshot.axisMinimum.fill(0);
        controllerSnapshot.axisMaximum.fill(0);
        controllerSnapshot.touchSampleCount = 0;
        controllerSnapshot.rumbleCallFailed = false;
        controllerSnapshot.ledCallFailed = false;
        controllerSnapshot.rumbleHardwareConfirmed = false;
        controllerSnapshot.rumbleHardwareFailed = false;
        controllerSnapshot.triggerRumbleCallFailed = false;
        controllerSnapshot.triggerRumbleHardwareConfirmed = false;
        controllerSnapshot.triggerRumbleHardwareFailed = false;
        controllerSnapshot.ledHardwareConfirmed = false;
        controllerSnapshot.ledHardwareFailed = false;
    };

    const auto refreshControllerSnapshot = [&]() {
        controllerSnapshot.connected = false;
        controllerSnapshot.name.fill('\0');
        controllerSnapshot.instanceId = -1;
        controllerSnapshot.battery = SDL_JOYSTICK_POWER_UNKNOWN;
        controllerSnapshot.axes.fill(0);
        controllerSnapshot.buttons.fill(false);
        controllerSnapshot.touchSampleCount = 0;
        controllerSnapshot.touchpadCount = 0;
        controllerSnapshot.touchpadFingerCount = 0;
        controllerSnapshot.touchpadApiAvailable = false;
        controllerSnapshot.gyroAvailable = false;
        controllerSnapshot.accelerometerAvailable = false;
        controllerSnapshot.gyro.fill(0.0F);
        controllerSnapshot.accelerometer.fill(0.0F);
        controllerSnapshot.rumbleAvailable = false;
        controllerSnapshot.triggerRumbleAvailable = false;
        controllerSnapshot.ledAvailable = false;
        const int outputCount = SDL_GetNumAudioDevices(0);
        const int captureCount = SDL_GetNumAudioDevices(1);
        controllerSnapshot.outputDeviceCount = std::max(0, outputCount);
        controllerSnapshot.captureDeviceCount = std::max(0, captureCount);
        if (controller == nullptr) {
            return;
        }

        controllerSnapshot.connected = true;
        const char* name = SDL_GameControllerName(controller);
        echo::input::setText(controllerSnapshot.name, name == nullptr ? "UNKNOWN" : name);
        SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
        controllerSnapshot.instanceId = SDL_JoystickInstanceID(joystick);
        controllerSnapshot.battery = SDL_JoystickCurrentPowerLevel(joystick);
        for (int axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; ++axis) {
            controllerSnapshot.axes[static_cast<std::size_t>(axis)] =
                SDL_GameControllerGetAxis(controller, static_cast<SDL_GameControllerAxis>(axis));
        }
        for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; ++button) {
            const bool pressed = SDL_GameControllerGetButton(
                controller, static_cast<SDL_GameControllerButton>(button)) != 0;
            controllerSnapshot.buttons[static_cast<std::size_t>(button)] = pressed;
            controllerSnapshot.buttonObserved = controllerSnapshot.buttonObserved || pressed;
        }
        constexpr Sint16 stickObservationThreshold = 8000;
        for (int axis = SDL_CONTROLLER_AXIS_LEFTX; axis <= SDL_CONTROLLER_AXIS_RIGHTY; ++axis) {
            const Sint16 value = controllerSnapshot.axes[static_cast<std::size_t>(axis)];
            controllerSnapshot.sticksObserved = controllerSnapshot.sticksObserved ||
                value > stickObservationThreshold || value < -stickObservationThreshold;
        }
        controllerSnapshot.triggersObserved = controllerSnapshot.triggersObserved ||
            controllerSnapshot.axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT] > 0 ||
            controllerSnapshot.axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] > 0;
        if (!controllerSnapshot.calibrationInitialized) {
            controllerSnapshot.axisMinimum = controllerSnapshot.axes;
            controllerSnapshot.axisMaximum = controllerSnapshot.axes;
            controllerSnapshot.calibrationInitialized = true;
        } else {
            for (std::size_t axis = 0; axis < controllerSnapshot.axes.size(); ++axis) {
                controllerSnapshot.axisMinimum[axis] =
                    std::min(controllerSnapshot.axisMinimum[axis], controllerSnapshot.axes[axis]);
                controllerSnapshot.axisMaximum[axis] =
                    std::max(controllerSnapshot.axisMaximum[axis], controllerSnapshot.axes[axis]);
            }
        }
        controllerSnapshot.touchpadCount = SDL_GameControllerGetNumTouchpads(controller);
        controllerSnapshot.touchpadApiAvailable = controllerSnapshot.touchpadCount > 0;
        int totalFingerSlots = 0;
        for (int touchpad = 0; touchpad < controllerSnapshot.touchpadCount; ++touchpad) {
            const int fingerCount = SDL_GameControllerGetNumTouchpadFingers(controller, touchpad);
            if (fingerCount <= 0) {
                continue;
            }
            controllerSnapshot.touchpadFingerCount += fingerCount;
            totalFingerSlots += fingerCount;
        }
        if (totalFingerSlots > 0 &&
            static_cast<std::size_t>(totalFingerSlots) > controllerSnapshot.touchSampleCapacity) {
            const std::size_t newCapacity = static_cast<std::size_t>(totalFingerSlots);
            void* allocation = SDL_realloc(controllerSnapshot.touches, newCapacity * sizeof(echo::input::TouchSample));
            if (allocation == nullptr) {
                setLabResult(controllerSnapshot, "TOUCH DATA", -1, "Unable to allocate touch sample buffer");
                totalFingerSlots = 0;
            } else {
                controllerSnapshot.touches = static_cast<echo::input::TouchSample*>(allocation);
                controllerSnapshot.touchSampleCapacity = newCapacity;
            }
        }
        for (int touchpad = 0; touchpad < controllerSnapshot.touchpadCount && totalFingerSlots > 0; ++touchpad) {
            const int fingerCount = SDL_GameControllerGetNumTouchpadFingers(controller, touchpad);
            if (fingerCount <= 0) {
                continue;
            }
            for (int finger = 0; finger < fingerCount; ++finger) {
                echo::input::TouchSample touch;
                touch.touchpad = touchpad;
                touch.finger = finger;
                Uint8 state = 0;
                if (SDL_GameControllerGetTouchpadFinger(controller, touchpad, finger, &state,
                                                        &touch.x, &touch.y, &touch.pressure) == 0) {
                    touch.active = state != 0;
                    controllerSnapshot.touchObserved = controllerSnapshot.touchObserved || touch.active;
                    if (controllerSnapshot.touchSampleCount < controllerSnapshot.touchSampleCapacity) {
                        controllerSnapshot.touches[controllerSnapshot.touchSampleCount++] = touch;
                    } else {
                        setLabResult(controllerSnapshot, "TOUCH DATA", -1, "Backend returned more touch slots than reported");
                    }
                }
            }
        }
        controllerSnapshot.gyroAvailable =
            SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO) == SDL_TRUE;
        controllerSnapshot.accelerometerAvailable =
            SDL_GameControllerHasSensor(controller, SDL_SENSOR_ACCEL) == SDL_TRUE;
        if (labSensorsEnabled && controllerSnapshot.gyroAvailable) {
            controllerSnapshot.gyroRead = SDL_GameControllerGetSensorData(
                controller, SDL_SENSOR_GYRO, controllerSnapshot.gyro.data(), 3) == 0 ||
                controllerSnapshot.gyroRead;
        }
        if (labSensorsEnabled && controllerSnapshot.accelerometerAvailable) {
            controllerSnapshot.accelerometerRead = SDL_GameControllerGetSensorData(
                controller, SDL_SENSOR_ACCEL, controllerSnapshot.accelerometer.data(), 3) == 0 ||
                controllerSnapshot.accelerometerRead;
        }
        controllerSnapshot.rumbleAvailable = SDL_GameControllerHasRumble(controller) == SDL_TRUE;
        controllerSnapshot.triggerRumbleAvailable =
            SDL_GameControllerHasRumbleTriggers(controller) == SDL_TRUE;
        controllerSnapshot.ledAvailable = SDL_GameControllerHasLED(controller) == SDL_TRUE;
    };

    const auto selectNextController = [&](int direction) {
        int candidateCount = 0;
        int currentOrdinal = -1;
        int candidateIndex = -1;
        const SDL_JoystickID currentId = controller == nullptr ? -1 :
            SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
        for (int index = 0; index < SDL_NumJoysticks(); ++index) {
            if (SDL_IsGameController(index) == SDL_TRUE) {
                if (SDL_JoystickGetDeviceInstanceID(index) == currentId) {
                    currentOrdinal = candidateCount;
                }
                ++candidateCount;
            }
        }
        if (candidateCount == 0) {
            setLabResult(controllerSnapshot, "DEVICE SELECT", -1, "No SDL game controllers are connected");
            return;
        }
        if (candidateCount == 1) {
            return;
        }
        if (currentOrdinal < 0) {
            currentOrdinal = 0;
        }
        const int nextOrdinal = (currentOrdinal + direction + candidateCount) % candidateCount;
        int ordinal = 0;
        for (int index = 0; index < SDL_NumJoysticks(); ++index) {
            if (SDL_IsGameController(index) == SDL_TRUE && ordinal++ == nextOrdinal) {
                candidateIndex = index;
                break;
            }
        }
        SDL_GameController* selected = candidateIndex < 0 ? nullptr : SDL_GameControllerOpen(candidateIndex);
        if (selected == nullptr) {
            setLabResult(controllerSnapshot, "DEVICE SELECT", -1, SDL_GetError());
            return;
        }
        stopLabOutput();
        SDL_free(controllerSnapshot.touches);
        controllerSnapshot.touches = nullptr;
        controllerSnapshot.touchSampleCapacity = 0;
        if (controller != nullptr) {
            SDL_GameControllerClose(controller);
        }
        controller = selected;
        resetControllerObservations();
        setLabResult(controllerSnapshot, "DEVICE SELECT", 0);
        logMessage("DualSense Lab selected another SDL controller");
        enableLabSensors();
    };

    const auto selectMenu = [&]() {
        if (menuSelection == 0) {
            game.start();
            screen = Screen::Playing;
            paused = false;
            logMessage("New game started");
        } else if (menuSelection == 1) {
            screen = Screen::Options;
        } else if (menuSelection == 2) {
            screen = Screen::DualSenseLab;
            enableLabSensors();
            logMessage("DualSense Lab opened");
        } else {
            running = false;
        }
    };

    const auto runRumbleTest = [&]() {
        if (controller == nullptr) {
            setLabResult(controllerSnapshot, "RUMBLE TEST", -1, "No controller connected");
            controllerSnapshot.rumbleCallFailed = true;
            return;
        }
        if (SDL_GameControllerHasRumble(controller) != SDL_TRUE) {
            setLabResult(controllerSnapshot, "RUMBLE TEST", -1, "Controller reports no rumble support");
            return;
        }
        SDL_ClearError();
        const int result = SDL_GameControllerRumble(
            controller, 8192, 4096, echo::input::kMaxTestEffectDurationMs);
        setLabResult(controllerSnapshot, "RUMBLE TEST", result, result == 0 ? "" : SDL_GetError());
        controllerSnapshot.rumbleCallFailed = result != 0;
        controllerSnapshot.rumbleHardwareConfirmed = false;
        controllerSnapshot.rumbleHardwareFailed = false;
        labRumbleActive = result == 0;
        labRumbleStopAt = result == 0 ? SDL_GetTicks64() + echo::input::kMaxTestEffectDurationMs : 0;
        if (result != 0) {
            logMessage(controllerSnapshot.lastError.data());
        }
    };

    const auto runTriggerRumbleTest = [&]() {
        if (controller == nullptr) {
            setLabResult(controllerSnapshot, "TRIGGER RUMBLE TEST", -1, "No controller connected");
            controllerSnapshot.triggerRumbleCallFailed = true;
            return;
        }
        if (SDL_GameControllerHasRumbleTriggers(controller) != SDL_TRUE) {
            setLabResult(controllerSnapshot, "TRIGGER RUMBLE TEST", -1,
                         "Controller reports no trigger-rumble support");
            return;
        }
        SDL_ClearError();
        const int result = SDL_GameControllerRumbleTriggers(
            controller, 2048, 2048, echo::input::kMaxTestEffectDurationMs);
        setLabResult(controllerSnapshot, "TRIGGER RUMBLE TEST", result,
                     result == 0 ? "" : SDL_GetError());
        controllerSnapshot.triggerRumbleCallFailed = result != 0;
        controllerSnapshot.triggerRumbleHardwareConfirmed = false;
        controllerSnapshot.triggerRumbleHardwareFailed = false;
        labTriggerRumbleActive = result == 0;
        labTriggerRumbleStopAt = result == 0 ? SDL_GetTicks64() + echo::input::kMaxTestEffectDurationMs : 0;
        if (result != 0) {
            logMessage(controllerSnapshot.lastError.data());
        }
    };

    const auto runLedTest = [&]() {
        if (controller == nullptr) {
            setLabResult(controllerSnapshot, "LED TEST", -1, "No controller connected");
            controllerSnapshot.ledCallFailed = true;
            return;
        }
        if (SDL_GameControllerHasLED(controller) != SDL_TRUE) {
            setLabResult(controllerSnapshot, "LED TEST", -1, "Controller reports no modifiable LED");
            return;
        }
        constexpr std::array<SDL_Color, 5> colors{{
            {255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255},
            {255, 255, 255, 255}, {0, 0, 0, 255},
        }};
        labLedColor = (labLedColor + 1) % static_cast<int>(colors.size());
        const SDL_Color color = colors[static_cast<std::size_t>(labLedColor)];
        SDL_ClearError();
        const int result = SDL_GameControllerSetLED(controller, color.r, color.g, color.b);
        setLabResult(controllerSnapshot, "LED TEST", result, result == 0 ? "" : SDL_GetError());
        controllerSnapshot.ledCallFailed = result != 0;
        controllerSnapshot.ledHardwareConfirmed = false;
        controllerSnapshot.ledHardwareFailed = false;
        labLedChanged = result == 0 && labLedColor != static_cast<int>(colors.size()) - 1;
        if (result != 0) {
            logMessage(controllerSnapshot.lastError.data());
        }
    };

    const auto confirmLastOutputTest = [&](bool passed) {
        if (!controllerSnapshot.hasResult || controllerSnapshot.lastResult != 0) {
            setLabResult(controllerSnapshot, "VERIFY OUTPUT", -1,
                         "Run a supported output test successfully before confirming it");
            return;
        }
        if (textEquals(controllerSnapshot.lastAction, "RUMBLE TEST")) {
            controllerSnapshot.rumbleHardwareConfirmed = passed;
            controllerSnapshot.rumbleHardwareFailed = !passed;
            setLabResult(controllerSnapshot, passed ? "RUMBLE CONFIRMED" : "RUMBLE FAILED", 0);
        } else if (textEquals(controllerSnapshot.lastAction, "TRIGGER RUMBLE TEST")) {
            controllerSnapshot.triggerRumbleHardwareConfirmed = passed;
            controllerSnapshot.triggerRumbleHardwareFailed = !passed;
            setLabResult(controllerSnapshot, passed ? "TRIGGER RUMBLE CONFIRMED" : "TRIGGER RUMBLE FAILED", 0);
        } else if (textEquals(controllerSnapshot.lastAction, "LED TEST")) {
            controllerSnapshot.ledHardwareConfirmed = passed;
            controllerSnapshot.ledHardwareFailed = !passed;
            setLabResult(controllerSnapshot, passed ? "LED CONFIRMED" : "LED FAILED", 0);
        } else {
            setLabResult(controllerSnapshot, "VERIFY OUTPUT", -1,
                         "The last action was not an output test");
        }
    };

    const auto adjustDeadZone = [&](int direction) {
        controllerSnapshot.deadZone = std::clamp(controllerSnapshot.deadZone +
            static_cast<float>(direction) * 0.05F, 0.05F, 0.40F);
    };

    const auto exportReport = [&]() {
        std::array<char, 256> error{};
        const bool succeeded = writeDualSenseReport(controllerSnapshot, error);
        setLabResult(controllerSnapshot, "EXPORT REPORT", succeeded ? 0 : -1, error.data());
        if (succeeded) {
#ifdef __PROSPERO__
            logMessage("DualSense report exported to /data/project_echo/logs/dualsense_report.json");
#else
            logMessage("DualSense report exported to logs/dualsense_report.json");
#endif
        } else {
            logMessage(error.data());
        }
    };

    const auto leaveLab = [&]() {
        stopLabOutput();
        screen = Screen::MainMenu;
        logMessage("DualSense Lab closed");
    };

    std::uint64_t previousCounter = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    double accumulator = 0.0;

    while (running) {
        if (screen == Screen::DualSenseLab) {
            refreshControllerSnapshot();
        }
#if defined(__PROSPERO__) && defined(ECHO_PS5_STARTUP_DIAGNOSTICS)
        if (diagnosticFirstFrame) {
            diagnosticCheckpoint("ECHO DIAG 10: before first event poll");
        }
#endif
        SDL_Event event{};
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                running = false;
                continue;
            }

            if (event.type == SDL_CONTROLLERDEVICEADDED && controller == nullptr &&
                SDL_IsGameController(event.cdevice.which) == SDL_TRUE) {
                controller = SDL_GameControllerOpen(event.cdevice.which);
                if (controller != nullptr) {
                    resetControllerObservations();
                    logMessage("SDL game controller connected");
                    if (screen == Screen::DualSenseLab) {
                        enableLabSensors();
                    }
                }
            } else if (event.type == SDL_CONTROLLERDEVICEREMOVED && controller != nullptr &&
                       SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) == event.cdevice.which) {
                stopLabOutput();
                SDL_GameControllerClose(controller);
                controller = nullptr;
                resetControllerObservations();
                logMessage("SDL game controller disconnected");
                for (int index = 0; index < SDL_NumJoysticks(); ++index) {
                    if (SDL_IsGameController(index) == SDL_TRUE) {
                        controller = SDL_GameControllerOpen(index);
                        if (controller != nullptr) {
                            resetControllerObservations();
                            logMessage("SDL game controller reconnected");
                            if (screen == Screen::DualSenseLab) {
                                enableLabSensors();
                            }
                            break;
                        }
                    }
                }
            }

            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                const SDL_GameControllerButton button =
                    static_cast<SDL_GameControllerButton>(event.cbutton.button);
                if (screen == Screen::MainMenu) {
                    if (button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                        menuSelection = (menuSelection + 3) % 4;
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        menuSelection = (menuSelection + 1) % 4;
                    } else if (button == SDL_CONTROLLER_BUTTON_A) {
                        selectMenu();
                    }
                } else if (screen == Screen::Options) {
                    if (kControlScheme == echo::platform::ControlScheme::PlayStation &&
                        button == SDL_CONTROLLER_BUTTON_B) {
                        screen = Screen::MainMenu;
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                        optionSelection = (optionSelection + 4) % 5;
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        optionSelection = (optionSelection + 1) % 5;
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT && optionSelection == 0) {
                        volume = std::max(0, volume - 10);
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT && optionSelection == 0) {
                        volume = std::min(100, volume + 10);
                    } else if (button == SDL_CONTROLLER_BUTTON_A && optionSelection == 4) {
                        screen = Screen::MainMenu;
                    }
                } else if (screen == Screen::DualSenseLab) {
                    if (button == SDL_CONTROLLER_BUTTON_A) {
                        runRumbleTest();
                    } else if (button == SDL_CONTROLLER_BUTTON_X) {
                        runLedTest();
                    } else if (button == SDL_CONTROLLER_BUTTON_Y) {
                        exportReport();
                    } else if (button == SDL_CONTROLLER_BUTTON_B) {
                        leaveLab();
                    } else if (button == SDL_CONTROLLER_BUTTON_LEFTSTICK) {
                        runTriggerRumbleTest();
                    } else if (button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
                        confirmLastOutputTest(true);
                    } else if (button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                        confirmLastOutputTest(false);
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
                        selectNextController(-1);
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
                        selectNextController(1);
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                        adjustDeadZone(1);
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        adjustDeadZone(-1);
                    }
                } else if (screen == Screen::Playing) {
                    if (kControlScheme == echo::platform::ControlScheme::PlayStation &&
                        button == SDL_CONTROLLER_BUTTON_B && paused) {
                        screen = Screen::MainMenu;
                        paused = false;
                        logMessage("Returned to main menu");
                    } else if (kControlScheme == echo::platform::ControlScheme::PlayStation &&
                               button == SDL_CONTROLLER_BUTTON_TOUCHPAD && !paused) {
                        showMap = !showMap;
                    } else if (button == SDL_CONTROLLER_BUTTON_START) {
                        if (paused) {
                            paused = false;
                        } else {
                            pendingPause = true;
                        }
                    } else if (!paused && button == SDL_CONTROLLER_BUTTON_A) {
                        pendingJump = true;
                    } else if (!paused && button == SDL_CONTROLLER_BUTTON_Y) {
                        pendingInteract = true;
                    } else if (paused && button == SDL_CONTROLLER_BUTTON_A) {
                        paused = false;
                    }
                }
                continue;
            }

            if (event.type == SDL_KEYUP) {
                const SDL_Scancode code = event.key.keysym.scancode;
                if (code >= 0 && code < SDL_NUM_SCANCODES) {
                    keys[static_cast<std::size_t>(code)] = false;
                }
                continue;
            }
            if (event.type != SDL_KEYDOWN || event.key.repeat != 0) {
                continue;
            }

            const SDL_Keycode key = event.key.keysym.sym;
            const SDL_Scancode code = event.key.keysym.scancode;
            if (code >= 0 && code < SDL_NUM_SCANCODES) {
                keys[static_cast<std::size_t>(code)] = true;
            }

            if (waitingForKey) {
                if (key != SDLK_ESCAPE) {
                    if (rebindAction == RebindAction::Jump) {
                        bindings.jump = key;
                    } else if (rebindAction == RebindAction::Shoot) {
                        bindings.shoot = key;
                    } else if (rebindAction == RebindAction::Interact) {
                        bindings.interact = key;
                    }
                    logMessage("Input binding changed");
                }
                waitingForKey = false;
                rebindAction = RebindAction::None;
                continue;
            }

            if (screen == Screen::MainMenu) {
                if (key == SDLK_UP || key == SDLK_w) {
                    menuSelection = (menuSelection + 3) % 4;
                } else if (key == SDLK_DOWN || key == SDLK_s) {
                    menuSelection = (menuSelection + 1) % 4;
                } else if (key == SDLK_RETURN || key == SDLK_SPACE) {
                    selectMenu();
                }
            } else if (screen == Screen::Options) {
                if (key == SDLK_UP) {
                    optionSelection = (optionSelection + 4) % 5;
                } else if (key == SDLK_DOWN) {
                    optionSelection = (optionSelection + 1) % 5;
                } else if (key == SDLK_LEFT && optionSelection == 0) {
                    volume = std::max(0, volume - 10);
                } else if (key == SDLK_RIGHT && optionSelection == 0) {
                    volume = std::min(100, volume + 10);
                } else if ((key == SDLK_RETURN || key == SDLK_SPACE) && optionSelection == 1) {
                    waitingForKey = true;
                    rebindAction = RebindAction::Jump;
                } else if ((key == SDLK_RETURN || key == SDLK_SPACE) && optionSelection == 2) {
                    waitingForKey = true;
                    rebindAction = RebindAction::Shoot;
                } else if ((key == SDLK_RETURN || key == SDLK_SPACE) && optionSelection == 3) {
                    waitingForKey = true;
                    rebindAction = RebindAction::Interact;
                } else if ((key == SDLK_RETURN || key == SDLK_SPACE) && optionSelection == 4) {
                    screen = Screen::MainMenu;
                } else if (key == SDLK_ESCAPE) {
                    screen = Screen::MainMenu;
                }
            } else if (screen == Screen::DualSenseLab &&
                       kControlScheme == echo::platform::ControlScheme::Keyboard) {
                if (key == SDLK_ESCAPE) {
                    leaveLab();
                } else if (key == SDLK_r) {
                    runRumbleTest();
                } else if (key == SDLK_l) {
                    runLedTest();
                } else if (key == SDLK_e) {
                    exportReport();
                } else if (key == SDLK_t) {
                    runTriggerRumbleTest();
                } else if (key == SDLK_p) {
                    confirmLastOutputTest(true);
                } else if (key == SDLK_f) {
                    confirmLastOutputTest(false);
                } else if (key == SDLK_LEFT) {
                    selectNextController(-1);
                } else if (key == SDLK_RIGHT) {
                    selectNextController(1);
                } else if (key == SDLK_UP) {
                    adjustDeadZone(1);
                } else if (key == SDLK_DOWN) {
                    adjustDeadZone(-1);
                }
            } else if (screen == Screen::Playing) {
                if (key == SDLK_ESCAPE) {
                    if (paused) {
                        screen = Screen::MainMenu;
                        paused = false;
                        logMessage("Returned to main menu");
                    } else {
                        paused = true;
                    }
                } else if (key == SDLK_m && !paused) {
                    showMap = !showMap;
                } else if (paused && (key == SDLK_RETURN || key == SDLK_SPACE)) {
                    paused = false;
                } else if (!paused && key == bindings.jump) {
                    pendingJump = true;
                } else if (!paused && key == bindings.interact) {
                    pendingInteract = true;
                }
            }
        }

        const std::uint64_t now = SDL_GetPerformanceCounter();
        const double elapsed = std::min(static_cast<double>(now - previousCounter) / frequency, 0.25);
        previousCounter = now;
        accumulator += elapsed;

        if (labRumbleActive && SDL_GetTicks64() >= labRumbleStopAt) {
            SDL_ClearError();
            if (controller != nullptr && SDL_GameControllerRumble(controller, 0, 0, 0) != 0) {
                SDL_Log("Could not stop timed DualSense rumble: %s", SDL_GetError());
            }
            labRumbleActive = false;
        }
        if (labTriggerRumbleActive && SDL_GetTicks64() >= labTriggerRumbleStopAt) {
            SDL_ClearError();
            if (controller != nullptr && SDL_GameControllerRumbleTriggers(controller, 0, 0, 0) != 0) {
                SDL_Log("Could not stop timed DualSense trigger rumble: %s", SDL_GetError());
            }
            labTriggerRumbleActive = false;
            labTriggerRumbleStopAt = 0;
        }

        if (pendingPause) {
            paused = !paused;
            pendingPause = false;
        }
        bool shotThisFrame = false;
        while (screen == Screen::Playing && !paused && accumulator >= echo::kFixedStep) {
            const auto isDown = [&keys](SDL_Keycode key) {
                const SDL_Scancode scan = SDL_GetScancodeFromKey(key);
                return scan >= 0 && scan < SDL_NUM_SCANCODES && keys[static_cast<std::size_t>(scan)];
            };
            int moveX = (isDown(SDLK_RIGHT) || isDown(SDLK_d) ? 1 : 0) -
                        (isDown(SDLK_LEFT) || isDown(SDLK_a) ? 1 : 0);
            bool jumpHeld = isDown(bindings.jump);
            bool shootHeld = isDown(bindings.shoot);
            if (controller != nullptr) {
                const Sint16 axis = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
                moveX += axis > 8000 ? 1 : axis < -8000 ? -1 : 0;
                moveX += (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) != 0 ? 1 : 0) -
                         (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT) != 0 ? 1 : 0);
                jumpHeld = jumpHeld || SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A) != 0;
                shootHeld = shootHeld || SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X) != 0;
            }

            const echo::Input input{
                std::clamp(moveX, -1, 1),
                pendingJump,
                jumpHeld,
                shootHeld,
                pendingInteract,
            };
            const int previousProjectiles = static_cast<int>(std::count_if(
                game.projectiles().begin(), game.projectiles().end(), [](const echo::Projectile& projectile) {
                    return projectile.active;
                }));
            game.update(input);
            const int currentProjectiles = static_cast<int>(std::count_if(
                game.projectiles().begin(), game.projectiles().end(), [](const echo::Projectile& projectile) {
                    return projectile.active;
                }));
            shotThisFrame = shotThisFrame || currentProjectiles > previousProjectiles;
            pendingJump = false;
            pendingInteract = false;
            accumulator -= echo::kFixedStep;
        }
        if (shotThisFrame) {
            audio.playShot(volume);
        }

#if defined(__PROSPERO__) && defined(ECHO_PS5_STARTUP_DIAGNOSTICS)
        if (diagnosticFirstFrame) {
            diagnosticCheckpoint("ECHO DIAG 11: before first frame render");
        }
#endif
        if (screen == Screen::MainMenu) {
            renderer.drawMenu(menuSelection);
        } else if (screen == Screen::DualSenseLab) {
            refreshControllerSnapshot();
            renderer.drawDualSenseLab(controllerSnapshot, SDL_NumJoysticks());
        } else {
            const auto jump = keyName(bindings.jump);
            const auto shoot = keyName(bindings.shoot);
            const auto interact = keyName(bindings.interact);
            if (screen == Screen::Options) {
                renderer.drawOptions(optionSelection, volume, waitingForKey, jump.data(), shoot.data(), interact.data());
            } else {
                renderer.drawGame(game, showMap, paused, jump.data(), shoot.data(), interact.data());
            }
        }
#if defined(__PROSPERO__) && defined(ECHO_PS5_STARTUP_DIAGNOSTICS)
        if (diagnosticFirstFrame) {
            diagnosticCheckpoint("ECHO DIAG 12: first frame rendered");
            diagnosticFirstFrame = false;
        }
#endif
        SDL_Delay(1);
    }

    stopLabOutput();
    if (controller != nullptr) {
        SDL_GameControllerClose(controller);
    }
    SDL_free(controllerSnapshot.touches);
    audio.close();
    renderer.destroy();
    SDL_Quit();
    logMessage("Application shutdown");
    return 0;
}
