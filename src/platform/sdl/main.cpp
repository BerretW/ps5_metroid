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
    Playing
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

    const auto selectMenu = [&]() {
        if (menuSelection == 0) {
            game.start();
            screen = Screen::Playing;
            paused = false;
            logMessage("New game started");
        } else if (menuSelection == 1) {
            screen = Screen::Options;
        } else {
            running = false;
        }
    };

    std::uint64_t previousCounter = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    double accumulator = 0.0;

    while (running) {
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
                    logMessage("SDL game controller connected");
                }
            } else if (event.type == SDL_CONTROLLERDEVICEREMOVED && controller != nullptr &&
                       SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) == event.cdevice.which) {
                SDL_GameControllerClose(controller);
                controller = nullptr;
                logMessage("SDL game controller disconnected");
            }

            if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                const SDL_GameControllerButton button =
                    static_cast<SDL_GameControllerButton>(event.cbutton.button);
                if (screen == Screen::MainMenu) {
                    if (button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                        menuSelection = (menuSelection + 2) % 3;
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        menuSelection = (menuSelection + 1) % 3;
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
                    menuSelection = (menuSelection + 2) % 3;
                } else if (key == SDLK_DOWN || key == SDLK_s) {
                    menuSelection = (menuSelection + 1) % 3;
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

    if (controller != nullptr) {
        SDL_GameControllerClose(controller);
    }
    audio.close();
    renderer.destroy();
    SDL_Quit();
    logMessage("Application shutdown");
    return 0;
}
