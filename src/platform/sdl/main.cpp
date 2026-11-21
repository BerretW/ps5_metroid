#include "game/game.hpp"
#include "platform/sdl/assets.hpp"
#include "platform/sdl/framework_integration.hpp"
#include "platform/sdl/game_persistence.hpp"
#include "platform/sdl/renderer.hpp"
#include "platform/sdl/settings_persistence.hpp"
#include "echo/framework/data.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>
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
    Inventory,
    DualSenseLab
};

enum class RebindAction {
    None,
    Jump,
    Shoot,
    Interact
};

using Bindings = echo::platform::Bindings;

constexpr std::string_view kGameId = "echo.project_echo";
constexpr std::string_view kAutosaveSlot = "autosave";
constexpr std::uint32_t kSaveFormatVersion = 5;
constexpr std::string_view kSettingsGameId = "echo.project_echo.settings";
constexpr std::string_view kSettingsSlot = "settings";
constexpr std::uint32_t kSettingsFormatVersion = 1;
constexpr std::array<echo::framework::ModuleVersion, 2> kSaveModules{{
    {"echo.state", 1, true},
    {"echo.project_echo", 1, true},
}};
constexpr std::array<echo::framework::ModuleVersion, 1> kSettingsModules{{
    {"echo.settings", 1, true},
}};

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

#if defined(__PROSPERO__) && defined(ECHO_PS5_SAVE_STORAGE_DIAGNOSTICS)
void logProviderDiagnostics(const echo::framework::Ps5SaveStorage& provider) {
    std::array<char, 600> line{};
    const std::string_view detail = provider.diagnostics();
    (void)std::snprintf(line.data(), line.size(), "PS5 save storage probe detail: %.*s",
                        static_cast<int>(detail.size()), detail.data());
    logMessage(line.data());
}

void runPs5SaveStorageDiagnostic() {
    using namespace echo::framework;
    logMessage("PS5 save storage probe: starting in /data/project_echo/saves");
    Ps5SaveStorage provider{};
    const Status initialized = provider.initialize();
    if (!initialized) {
        logMessage("PS5 save storage probe: directory initialization failed");
        logMessage(initialized.detail);
        logProviderDiagnostics(provider);
        return;
    }

    logProviderDiagnostics(provider);
    const SaveStorage storage = provider.callbacks();
    GameState state{};
    constexpr std::array<ModuleVersion, 1> modules{{{"echo.storage_probe", 1, true}}};
    std::array<char, kMaxSaveBytes> scratch{};
    constexpr std::string_view slot = "framework_probe";
    if (!state.set({StateScope::Global, {}, "probe.value"}, StateValue::fromInteger(1))) {
        logMessage("PS5 save storage probe: could not initialize fixture state");
        return;
    }
    Status status = SaveService::save(storage, slot, "echo.project_echo", 1,
                                      modules, state, scratch.data(), scratch.size());
    if (!status) {
        logMessage("PS5 save storage probe: initial write failed");
        logMessage(status.detail);
        logProviderDiagnostics(provider);
        return;
    }
    logMessage("PS5 save storage probe: initial write passed");

    if (!state.set({StateScope::Global, {}, "probe.value"}, StateValue::fromInteger(2))) {
        logMessage("PS5 save storage probe: could not update fixture state");
        return;
    }
    status = SaveService::save(storage, slot, "echo.project_echo", 1,
                               modules, state, scratch.data(), scratch.size());
    if (!status) {
        logMessage("PS5 save storage probe: replacement failed");
        logMessage(status.detail);
        logProviderDiagnostics(provider);
        return;
    }
    logMessage("PS5 save storage probe: replacement passed");

    GameState restored{};
    SaveHeader header{};
    status = SaveService::load(storage, slot, "echo.project_echo", 1, modules,
                               restored, header, scratch.data(), scratch.size());
    StateValue restoredValue{};
    if (!status || !restored.get({StateScope::Global, {}, "probe.value"}, restoredValue) ||
        restoredValue.type != ValueType::Integer || restoredValue.integer != 2) {
        logMessage("PS5 save storage probe: read-back verification failed");
        if (!status) {
            logMessage(status.detail);
            logProviderDiagnostics(provider);
        }
        return;
    }
    logMessage("PS5 save storage probe: read-back verification passed");

    std::array<char, 128> slots{};
    std::size_t slotsSize{};
    status = SaveService::enumerate(storage, slots.data(), slots.size(), slotsSize);
    if (!status) {
        logMessage("PS5 save storage probe: slot enumeration failed");
        logMessage(status.detail);
        logProviderDiagnostics(provider);
        return;
    }
    bool foundProbe = false;
    for (std::size_t offset = 0; offset < slotsSize;) {
        const char* const terminator = static_cast<const char*>(
            std::memchr(slots.data() + offset, '\0', slotsSize - offset));
        if (terminator == nullptr) {
            logMessage("PS5 save storage probe: slot list is malformed");
            return;
        }
        const std::string_view item(
            slots.data() + offset, static_cast<std::size_t>(terminator - (slots.data() + offset)));
        foundProbe = foundProbe || item == slot;
        offset += item.size() + 1;
    }
    if (!foundProbe) {
        logMessage("PS5 save storage probe: enumeration omitted the probe slot");
        logProviderDiagnostics(provider);
        return;
    }
    logProviderDiagnostics(provider);
    logMessage("PS5 save storage probe: PASS; reserved slot framework_probe.sav remains for inspection");
}
#endif

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

enum class SoundId { Shot, Jump, Hurt, EnemyHit, EnemyDefeat, Pickup, Door, GateDenied, Count };

class Audio {
public:
    bool open() noexcept {
        SDL_AudioSpec desired{};
        desired.freq = 48000;
        desired.format = AUDIO_S16SYS;
        desired.channels = 1;
        desired.samples = 512;
        desired.callback = &Audio::mix;
        desired.userdata = this;
        device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained_, 0);
        if (device_ == 0) {
            SDL_Log("Audio unavailable; continuing without sound: %s", SDL_GetError());
            return false;
        }
        createFallbackShot();
        SDL_PauseAudioDevice(device_, 0);
        return true;
    }

    // Loads packaged WAV effects; any missing file leaves that effect silent
    // (the shot keeps its synthesized fallback). Returns the number loaded.
    int loadSounds(const char* assetDirectory) noexcept {
        static constexpr std::array<const char*, static_cast<int>(SoundId::Count)> files{
            "sounds/shot.wav", "sounds/jump.wav", "sounds/hurt.wav", "sounds/enemy_hit.wav",
            "sounds/enemy_defeat.wav", "sounds/pickup.wav", "sounds/door.wav", "sounds/gate_denied.wav"};
        if (device_ == 0 || assetDirectory == nullptr) {
            return 0;
        }
        int loaded = 0;
        for (std::size_t index = 0; index < files.size(); ++index) {
            char path[512];
            const int written = std::snprintf(path, sizeof(path), "%s%s", assetDirectory, files[index]);
            echo::platform::SoundBuffer sound;
            if (written > 0 && written < static_cast<int>(sizeof(path)) &&
                echo::platform::loadWavMono16(path, obtained_.freq, sound)) {
                SDL_LockAudioDevice(device_);
                std::swap(sounds_[index], sound);
                SDL_UnlockAudioDevice(device_);
                echo::platform::freeSound(sound);
                ++loaded;
            }
        }
        return loaded;
    }

    void play(SoundId id, int volume) noexcept {
        const auto index = static_cast<std::size_t>(id);
        if (device_ == 0 || volume <= 0 || sounds_[index].samples == nullptr) {
            return;
        }
        SDL_LockAudioDevice(device_);
        Voice* target = &voices_[0];
        for (Voice& voice : voices_) {
            if (voice.sound == nullptr) {
                target = &voice;
                break;
            }
            if (voice.position > target->position) {
                target = &voice;
            }
        }
        *target = {&sounds_[index], 0, std::clamp(volume, 0, 100) * 256 / 100};
        SDL_UnlockAudioDevice(device_);
    }

    void close() noexcept {
        if (device_ != 0) {
            SDL_CloseAudioDevice(device_);
            device_ = 0;
        }
        voices_ = {};
        for (echo::platform::SoundBuffer& sound : sounds_) {
            echo::platform::freeSound(sound);
        }
    }

private:
    struct Voice {
        const echo::platform::SoundBuffer* sound{};
        Uint32 position{};
        int gain{};
    };

    // Runs on SDL's audio thread: no allocation, no logging.
    static void SDLCALL mix(void* userdata, Uint8* stream, int length) {
        auto* audio = static_cast<Audio*>(userdata);
        auto* output = reinterpret_cast<Sint16*>(stream);
        const int count = length / static_cast<int>(sizeof(Sint16));
        for (int index = 0; index < count; ++index) {
            int sum = 0;
            for (Voice& voice : audio->voices_) {
                if (voice.sound == nullptr) {
                    continue;
                }
                sum += voice.sound->samples[voice.position] * voice.gain / 256;
                if (++voice.position >= voice.sound->count) {
                    voice = {};
                }
            }
            output[index] = static_cast<Sint16>(std::clamp(sum, -32768, 32767));
        }
    }

    void createFallbackShot() noexcept {
        constexpr Uint32 count = 640;
        auto& shot = sounds_[static_cast<std::size_t>(SoundId::Shot)];
        shot.samples = static_cast<Sint16*>(SDL_malloc(count * sizeof(Sint16)));
        if (shot.samples == nullptr) {
            return;
        }
        shot.count = count;
        constexpr double twoPi = 6.283185307179586;
        for (Uint32 index = 0; index < count; ++index) {
            const double envelope = 1.0 - static_cast<double>(index) / count;
            const double wave = std::sin(twoPi * 560.0 * static_cast<double>(index) / obtained_.freq);
            shot.samples[index] = static_cast<Sint16>(wave * envelope * 8500.0);
        }
    }

    SDL_AudioDeviceID device_{};
    SDL_AudioSpec obtained_{};
    std::array<echo::platform::SoundBuffer, static_cast<int>(SoundId::Count)> sounds_{};
    std::array<Voice, 8> voices_{};
};

// Gameplay stays sound-agnostic; the platform layer derives effects from state changes.
struct SoundState {
    unsigned activeProjectiles{};
    float velocityY{};
    int health{};
    int deaths{};
    int enemyHealth{};
    int livingEnemies{};
    std::uint32_t healthCellCount{};
    std::uint32_t scrapCount{};
    std::array<std::uint32_t, static_cast<std::size_t>(echo::ItemId::Count)> itemCounts{};
    std::array<echo::ItemStack, echo::Game::kInventorySlotCount> inventorySlots{};
    unsigned activeLoot{};
    echo::RoomId room{};
    bool abilityCollected{};
    bool hasMaintenanceKey{};
    bool gateBlocked{};
    bool completed{};
};

SoundState captureSoundState(const echo::Game& game) noexcept {
    SoundState state;
    for (std::size_t index = 0; index < game.projectiles().size(); ++index) {
        if (game.projectiles()[index].active) {
            state.activeProjectiles |= 1U << index;
        }
    }
    for (const echo::Enemy& enemy : game.enemies()) {
        if (enemy.alive) {
            state.enemyHealth += enemy.health;
            ++state.livingEnemies;
        }
    }
    state.velocityY = game.player().velocity.y;
    state.health = game.player().health;
    state.deaths = game.deaths();
    state.room = game.roomId();
    state.abilityCollected = game.abilityCollected();
    state.hasMaintenanceKey = game.hasItem(echo::ItemId::MaintenanceKey);
    state.healthCellCount = game.itemQuantity(echo::ItemId::HealthCell);
    state.scrapCount = game.itemQuantity(echo::ItemId::Scrap);
    for (std::size_t index = 1; index < state.itemCounts.size(); ++index) {
        state.itemCounts[index] = game.itemQuantity(static_cast<echo::ItemId>(index));
    }
    for (std::size_t index = 0; index < state.inventorySlots.size(); ++index) {
        state.inventorySlots[index] = game.inventorySlot(index);
    }
    for (std::size_t index = 0; index < game.lootDrops().size(); ++index) {
        if (game.lootDrops()[index].active) state.activeLoot |= 1U << index;
    }
    state.gateBlocked = game.gateBlocked();
    state.completed = game.completed();
    return state;
}

unsigned soundEventsBetween(const SoundState& before, const SoundState& after) noexcept {
    const auto bit = [](SoundId id) { return 1U << static_cast<unsigned>(id); };
    unsigned events = 0;
    if ((after.activeProjectiles & ~before.activeProjectiles) != 0U) {
        events |= bit(SoundId::Shot);
    }
    // Gravity only increases downward speed, so a sharp upward change is a jump impulse.
    if (after.velocityY < before.velocityY - 200.0F) {
        events |= bit(SoundId::Jump);
    }
    const bool died = after.deaths != before.deaths;
    if (after.health < before.health || died) {
        events |= bit(SoundId::Hurt);
    }
    if (after.livingEnemies < before.livingEnemies) {
        events |= bit(SoundId::EnemyDefeat);
    } else if (after.enemyHealth < before.enemyHealth) {
        events |= bit(SoundId::EnemyHit);
    }
    if ((after.abilityCollected && !before.abilityCollected) ||
        (after.hasMaintenanceKey && !before.hasMaintenanceKey) ||
        after.healthCellCount > before.healthCellCount ||
        after.scrapCount > before.scrapCount ||
        after.health > before.health ||
        (after.completed && !before.completed)) {
        events |= bit(SoundId::Pickup);
    }
    for (std::size_t index = 1; index < before.itemCounts.size(); ++index) {
        if (after.itemCounts[index] > before.itemCounts[index]) events |= bit(SoundId::Pickup);
    }
    if ((before.activeLoot & ~after.activeLoot) != 0U) events |= bit(SoundId::Pickup);
    if (after.room != before.room && !died) {
        events |= bit(SoundId::Door);
    }
    if (after.gateBlocked && !before.gateBlocked) {
        events |= bit(SoundId::GateDenied);
    }
    return events;
}

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

    std::array<char, 512> assetsRoot{};
    std::array<char, 512> gameConfigPath{};
    std::array<char, 512> dialoguePath{};
    std::array<char, 512> itemCatalogPath{};
    if (!echo::platform::assetRoot(assetsRoot.data(), assetsRoot.size())) {
        logMessage("Framework initialization failed: asset directory is unavailable");
        SDL_Quit();
        return 1;
    }
    const int configPathLength = std::snprintf(
        gameConfigPath.data(), gameConfigPath.size(), "%sframework/game.json", assetsRoot.data());
    if (configPathLength <= 0 ||
        static_cast<std::size_t>(configPathLength) >= gameConfigPath.size()) {
        logMessage("Framework initialization failed: game configuration path is too long");
        SDL_Quit();
        return 1;
    }
    const int dialoguePathLength = std::snprintf(
        dialoguePath.data(), dialoguePath.size(), "%sframework/engineer.json", assetsRoot.data());
    if (dialoguePathLength <= 0 ||
        static_cast<std::size_t>(dialoguePathLength) >= dialoguePath.size()) {
        logMessage("Framework initialization failed: dialogue path is too long");
        SDL_Quit();
        return 1;
    }
    const int itemCatalogPathLength = std::snprintf(
        itemCatalogPath.data(), itemCatalogPath.size(), "%sframework/items.json", assetsRoot.data());
    if (itemCatalogPathLength <= 0 ||
        static_cast<std::size_t>(itemCatalogPathLength) >= itemCatalogPath.size()) {
        logMessage("Framework initialization failed: item catalog path is too long");
        SDL_Quit();
        return 1;
    }
    echo::platform::FrameworkIntegration framework;
    const echo::framework::Status frameworkStatus =
        framework.initialize(gameConfigPath.data(), dialoguePath.data(),
                             itemCatalogPath.data());
    if (!frameworkStatus) {
        logMessage("Framework initialization failed");
        logMessage(frameworkStatus.detail);
        SDL_Quit();
        return 1;
    }
    echo::Game game;
    const echo::framework::Status gameSetup = framework.configureGame(game);
    if (!gameSetup) {
        logMessage("Game content initialization failed");
        logMessage(gameSetup.detail);
        framework.shutdown();
        SDL_Quit();
        return 1;
    }
    logMessage("Echo Framework and engineer dialogue initialized from assets/framework");
#if defined(__PROSPERO__)
    echo::framework::Ps5SaveStorage saveProvider{};
#else
    echo::framework::WindowsSaveStorage saveProvider{};
#endif
    echo::framework::SaveStorage saveStorage{};
    bool saveStorageReady = false;
    const echo::framework::Status saveStorageStatus = saveProvider.initialize();
    if (!saveStorageStatus) {
        logMessage("Save storage initialization failed; continuing without persistence");
        logMessage(saveStorageStatus.detail);
    } else {
        saveStorage = saveProvider.callbacks();
        saveStorageReady = true;
        logMessage("Save storage initialized");
    }
#if defined(__PROSPERO__) && defined(ECHO_PS5_SAVE_STORAGE_DIAGNOSTICS)
    runPs5SaveStorageDiagnostic();
#endif

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
    {
        std::array<char, 512> assets{};
        if (echo::platform::assetRoot(assets.data(), assets.size())) {
            const int sprites = renderer.loadSprites(assets.data());
            const int sounds = audio.loadSounds(assets.data());
            std::array<char, 160> summary{};
            std::snprintf(summary.data(), summary.size(), "Assets loaded: %d/6 sprites, %d/8 sounds", sprites, sounds);
            logMessage(summary.data());
        } else {
            logMessage("Asset directory unavailable; using built-in art");
        }
    }
#if defined(__PROSPERO__) && defined(ECHO_PS5_STARTUP_DIAGNOSTICS)
    diagnosticCheckpoint("ECHO DIAG 08a: assets attempted; before game state");
#endif

    std::array<char, echo::framework::kMaxSaveBytes> saveScratch{};
    bool continueAvailable = false;
    bool settingsAvailable = false;
    if (saveStorageReady) {
        std::array<char, echo::framework::kMaxSaveBytes> slots{};
        std::size_t written{};
        const echo::framework::Status status =
            echo::framework::SaveService::enumerate(saveStorage, slots.data(), slots.size(), written);
        if (!status) {
            logMessage("Save-slot enumeration failed");
            logMessage(status.detail);
        } else {
            std::size_t offset = 0;
            while (offset < written) {
                const char* const terminator = static_cast<const char*>(
                    std::memchr(slots.data() + offset, '\0', written - offset));
                if (terminator == nullptr) {
                    logMessage("Save-slot list is malformed");
                    break;
                }
                const std::string_view slot(
                    slots.data() + offset,
                    static_cast<std::size_t>(terminator - (slots.data() + offset)));
                continueAvailable = continueAvailable || slot == kAutosaveSlot;
                settingsAvailable = settingsAvailable || slot == kSettingsSlot;
                offset += slot.size() + 1;
            }
        }
    }
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
    echo::framework::DialogueLanguage dialogueLanguage =
        echo::framework::DialogueLanguage::English;
    bool pendingJump = false;
    bool pendingInteract = false;
    bool pendingUseItem = false;
    bool pendingPause = false;
    echo::platform::InventoryMenu inventoryMenu{};
    std::array<char, 128> inventoryMessage{};
    bool gameSessionActive = false;
    double secondsSinceAutosave = 0.0;
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
        controllerSnapshot.pendingConfirmation = echo::input::OutputTest::None;
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
        controllerSnapshot.pendingConfirmation =
            result == 0 ? echo::input::OutputTest::Rumble : echo::input::OutputTest::None;
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
        controllerSnapshot.pendingConfirmation =
            result == 0 ? echo::input::OutputTest::TriggerRumble : echo::input::OutputTest::None;
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
        controllerSnapshot.pendingConfirmation =
            result == 0 ? echo::input::OutputTest::Led : echo::input::OutputTest::None;
        labLedChanged = result == 0 && labLedColor != static_cast<int>(colors.size()) - 1;
        if (result != 0) {
            logMessage(controllerSnapshot.lastError.data());
        }
    };

    const auto confirmLastOutputTest = [&](bool passed) {
        switch (controllerSnapshot.pendingConfirmation) {
        case echo::input::OutputTest::Rumble:
            controllerSnapshot.rumbleHardwareConfirmed = passed;
            controllerSnapshot.rumbleHardwareFailed = !passed;
            setLabResult(controllerSnapshot, passed ? "RUMBLE CONFIRMED" : "RUMBLE FAILED", 0);
            break;
        case echo::input::OutputTest::TriggerRumble:
            controllerSnapshot.triggerRumbleHardwareConfirmed = passed;
            controllerSnapshot.triggerRumbleHardwareFailed = !passed;
            setLabResult(controllerSnapshot, passed ? "TRIGGER RUMBLE CONFIRMED" : "TRIGGER RUMBLE FAILED", 0);
            break;
        case echo::input::OutputTest::Led:
            controllerSnapshot.ledHardwareConfirmed = passed;
            controllerSnapshot.ledHardwareFailed = !passed;
            setLabResult(controllerSnapshot, passed ? "LED CONFIRMED" : "LED FAILED", 0);
            break;
        case echo::input::OutputTest::None:
        default:
            setLabResult(controllerSnapshot, "RUN A TEST FIRST", -1,
                         "Run a supported output test successfully before confirming it");
            return;
        }
        controllerSnapshot.pendingConfirmation = echo::input::OutputTest::None;
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

    const auto saveCurrentGame = [&]() {
        secondsSinceAutosave = 0.0;
        if (!saveStorageReady) {
            return false;
        }
        echo::framework::GameState snapshot{};
        echo::framework::Status status = echo::platform::encodeGameCheckpoint(game, snapshot);
        if (!status) {
            logMessage("Gameplay checkpoint encoding failed");
            logMessage(status.detail);
            return false;
        }
        status = framework.gameState().replaceValues(snapshot);
        if (!status) {
            logMessage("Framework save state synchronization failed");
            logMessage(status.detail);
            return false;
        }
        status = echo::framework::SaveService::save(
            saveStorage, kAutosaveSlot, kGameId, kSaveFormatVersion, kSaveModules,
            snapshot, saveScratch.data(), saveScratch.size());
        if (!status) {
            logMessage("Gameplay autosave failed");
            logMessage(status.detail);
            return false;
        }
        continueAvailable = true;
        return true;
    };

    const auto loadSavedGame = [&]() {
        if (!saveStorageReady || !continueAvailable) {
            logMessage("No autosave is available to continue");
            return false;
        }
        echo::framework::GameState loadedState{};
        echo::framework::SaveHeader header{};
        echo::framework::Status status = echo::framework::SaveService::load(
            saveStorage, kAutosaveSlot, kGameId, kSaveFormatVersion, kSaveModules,
            loadedState, header, saveScratch.data(), saveScratch.size(),
            &echo::platform::migrateGameCheckpointSave);
        if (!status) {
            logMessage("Gameplay autosave load failed");
            logMessage(status.detail);
            return false;
        }
        echo::GameCheckpoint checkpoint{};
        status = echo::platform::decodeGameCheckpoint(loadedState, checkpoint);
        if (!status) {
            logMessage("Gameplay autosave validation failed");
            logMessage(status.detail);
            return false;
        }
        echo::Game resumed = game;
        if (!resumed.restore(checkpoint)) {
            logMessage("Gameplay autosave checkpoint is invalid");
            return false;
        }
        status = framework.resumeGame(loadedState);
        if (!status) {
            logMessage("Framework save-state restore failed");
            logMessage(status.detail);
            return false;
        }
        game = resumed;
        gameSessionActive = true;
        secondsSinceAutosave = 0.0;
        screen = Screen::Playing;
        paused = false;
        logMessage("Autosave loaded");
        return true;
    };

    const auto saveSettings = [&]() {
        if (!saveStorageReady) {
            return false;
        }
        echo::framework::GameState settings{};
        echo::framework::Status status =
            echo::platform::encodeSettings(volume, bindings, dialogueLanguage, settings);
        if (!status) {
            logMessage("Settings encoding failed");
            logMessage(status.detail);
            return false;
        }
        status = echo::framework::SaveService::save(
            saveStorage, kSettingsSlot, kSettingsGameId, kSettingsFormatVersion,
            kSettingsModules, settings, saveScratch.data(), saveScratch.size());
        if (!status) {
            logMessage("Settings save failed");
            logMessage(status.detail);
            return false;
        }
        settingsAvailable = true;
        return true;
    };

    const auto loadSavedSettings = [&]() {
        if (!saveStorageReady || !settingsAvailable) {
            return false;
        }
        echo::framework::GameState settings{};
        echo::framework::SaveHeader header{};
        const echo::framework::Status status = echo::framework::SaveService::load(
            saveStorage, kSettingsSlot, kSettingsGameId, kSettingsFormatVersion,
            kSettingsModules, settings, header, saveScratch.data(), saveScratch.size());
        if (!status) {
            logMessage("Settings load failed; defaults retained");
            logMessage(status.detail);
            return false;
        }
        int restoredVolume = volume;
        Bindings restoredBindings = bindings;
        auto restoredLanguage = dialogueLanguage;
        const echo::framework::Status decoded =
            echo::platform::decodeSettings(settings, restoredVolume, restoredBindings,
                                           restoredLanguage);
        if (!decoded) {
            logMessage("Settings validation failed; defaults retained");
            logMessage(decoded.detail);
            return false;
        }
        const echo::framework::Status languageStatus =
            game.setDialogueLanguage(restoredLanguage);
        if (!languageStatus) {
            logMessage("Dialogue language restore failed; defaults retained");
            logMessage(languageStatus.detail);
            return false;
        }
        volume = restoredVolume;
        bindings = restoredBindings;
        dialogueLanguage = restoredLanguage;
        logMessage("Settings loaded");
        return true;
    };

    (void)loadSavedSettings();

    const auto changeDialogueLanguage = [&](int direction) {
        const auto candidate = dialogueLanguage == echo::framework::DialogueLanguage::English
            ? echo::framework::DialogueLanguage::Czech
            : echo::framework::DialogueLanguage::English;
        if (direction == 0 || candidate == dialogueLanguage) {
            return;
        }
        const echo::framework::Status status = game.setDialogueLanguage(candidate);
        if (!status) {
            logMessage("Dialogue language change failed");
            logMessage(status.detail);
            return;
        }
        dialogueLanguage = candidate;
        (void)saveSettings();
    };

    const auto selectMenu = [&]() {
        if (menuSelection == 0) {
            const echo::framework::Status status = framework.beginGame();
            if (!status) {
                logMessage("Framework game-state reset failed");
                logMessage(status.detail);
                running = false;
                return;
            }
            game.start();
            gameSessionActive = true;
            screen = Screen::Playing;
            paused = false;
            (void)saveCurrentGame();
            logMessage("New game started");
        } else if (menuSelection == 1) {
            (void)loadSavedGame();
        } else if (menuSelection == 2) {
            screen = Screen::Options;
        } else if (menuSelection == 3) {
            screen = Screen::DualSenseLab;
            enableLabSensors();
            logMessage("DualSense Lab opened");
        } else {
            running = false;
        }
    };

    std::uint64_t previousCounter = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    double accumulator = 0.0;

    const auto openInventory = [&]() {
        inventoryMenu.reset();
        inventoryMessage.fill(0);
        pendingJump = false;
        pendingInteract = false;
        pendingUseItem = false;
        pendingPause = false;
        keys.fill(false);
        accumulator = 0.0;
        screen = Screen::Inventory;
    };
    const auto handleInventory = [&](echo::platform::InventoryCommand command) {
        using echo::platform::InventoryCommand;
        if (command == InventoryCommand::Back) {
            if (inventoryMenu.back()) {
                screen = Screen::Playing;
                paused = true;
            }
            inventoryMessage.fill(0);
        } else if (command == InventoryCommand::Confirm) {
            const bool moving =
                inventoryMenu.source != echo::platform::InventoryMenu::kNoSource &&
                inventoryMenu.source != inventoryMenu.selected;
            const echo::framework::Status status = inventoryMenu.confirm(game);
            if (!status) {
                logMessage("Inventory move failed");
                logMessage(status.detail);
                SDL_snprintf(inventoryMessage.data(), inventoryMessage.size(), "%.*s",
                             static_cast<int>(status.detail.size()), status.detail.data());
            } else {
                SDL_snprintf(inventoryMessage.data(), inventoryMessage.size(), "%s",
                             moving ? "ITEM MOVED" :
                             inventoryMenu.source == echo::platform::InventoryMenu::kNoSource
                                 ? "SELECTION CLEARED" : "SELECT DESTINATION");
                if (moving && !saveCurrentGame()) {
                    SDL_snprintf(inventoryMessage.data(), inventoryMessage.size(),
                                 "%s", "ITEM MOVED - SAVE FAILED");
                }
            }
        } else {
            inventoryMenu.navigate(command == InventoryCommand::Left ? -1 :
                                   command == InventoryCommand::Right ? 1 : 0,
                                   command == InventoryCommand::Up ? -1 :
                                   command == InventoryCommand::Down ? 1 : 0);
        }
    };

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
                        menuSelection = (menuSelection + 4) % 5;
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        menuSelection = (menuSelection + 1) % 5;
                    } else if (button == SDL_CONTROLLER_BUTTON_A) {
                        selectMenu();
                    }
                } else if (screen == Screen::Options) {
                    if (kControlScheme == echo::platform::ControlScheme::PlayStation &&
                        button == SDL_CONTROLLER_BUTTON_B) {
                        (void)saveSettings();
                        screen = Screen::MainMenu;
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                        optionSelection = (optionSelection + 5) % 6;
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        optionSelection = (optionSelection + 1) % 6;
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT && optionSelection == 0) {
                        const int adjusted = std::max(0, volume - 10);
                        if (adjusted != volume) {
                            volume = adjusted;
                            (void)saveSettings();
                        }
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT && optionSelection == 0) {
                        const int adjusted = std::min(100, volume + 10);
                        if (adjusted != volume) {
                            volume = adjusted;
                            (void)saveSettings();
                        }
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT && optionSelection == 4) {
                        changeDialogueLanguage(-1);
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT && optionSelection == 4) {
                        changeDialogueLanguage(1);
                    } else if (button == SDL_CONTROLLER_BUTTON_A && optionSelection == 5) {
                        (void)saveSettings();
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
                } else if (screen == Screen::Inventory) {
                    using echo::platform::InventoryCommand;
                    if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
                        handleInventory(InventoryCommand::Left);
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
                        handleInventory(InventoryCommand::Right);
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
                        handleInventory(InventoryCommand::Up);
                    } else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
                        handleInventory(InventoryCommand::Down);
                    } else if (button == SDL_CONTROLLER_BUTTON_A) {
                        handleInventory(InventoryCommand::Confirm);
                    } else if (button == SDL_CONTROLLER_BUTTON_B ||
                               button == SDL_CONTROLLER_BUTTON_START) {
                        handleInventory(InventoryCommand::Back);
                    }
                } else if (screen == Screen::Playing) {
                    if (kControlScheme == echo::platform::ControlScheme::PlayStation &&
                        button == SDL_CONTROLLER_BUTTON_B && paused) {
                        if (saveCurrentGame()) {
                            gameSessionActive = false;
                        }
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
                    } else if (!paused &&
                               button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                        pendingUseItem = true;
                    } else if (paused && button == SDL_CONTROLLER_BUTTON_Y) {
                        openInventory();
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
                    (void)saveSettings();
                }
                waitingForKey = false;
                rebindAction = RebindAction::None;
                continue;
            }

            if (screen == Screen::MainMenu) {
                if (key == SDLK_UP || key == SDLK_w) {
                    menuSelection = (menuSelection + 4) % 5;
                } else if (key == SDLK_DOWN || key == SDLK_s) {
                    menuSelection = (menuSelection + 1) % 5;
                } else if (key == SDLK_RETURN || key == SDLK_SPACE) {
                    selectMenu();
                }
            } else if (screen == Screen::Options) {
                if (key == SDLK_UP) {
                    optionSelection = (optionSelection + 5) % 6;
                } else if (key == SDLK_DOWN) {
                    optionSelection = (optionSelection + 1) % 6;
                } else if (key == SDLK_LEFT && optionSelection == 0) {
                    const int adjusted = std::max(0, volume - 10);
                    if (adjusted != volume) {
                        volume = adjusted;
                        (void)saveSettings();
                    }
                } else if (key == SDLK_RIGHT && optionSelection == 0) {
                    const int adjusted = std::min(100, volume + 10);
                    if (adjusted != volume) {
                        volume = adjusted;
                        (void)saveSettings();
                    }
                } else if (key == SDLK_LEFT && optionSelection == 4) {
                    changeDialogueLanguage(-1);
                } else if (key == SDLK_RIGHT && optionSelection == 4) {
                    changeDialogueLanguage(1);
                } else if ((key == SDLK_RETURN || key == SDLK_SPACE) && optionSelection == 1) {
                    waitingForKey = true;
                    rebindAction = RebindAction::Jump;
                } else if ((key == SDLK_RETURN || key == SDLK_SPACE) && optionSelection == 2) {
                    waitingForKey = true;
                    rebindAction = RebindAction::Shoot;
                } else if ((key == SDLK_RETURN || key == SDLK_SPACE) && optionSelection == 3) {
                    waitingForKey = true;
                    rebindAction = RebindAction::Interact;
                } else if ((key == SDLK_RETURN || key == SDLK_SPACE) && optionSelection == 5) {
                    (void)saveSettings();
                    screen = Screen::MainMenu;
                } else if (key == SDLK_ESCAPE) {
                    (void)saveSettings();
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
            } else if (screen == Screen::Inventory) {
                using echo::platform::InventoryCommand;
                if (key == SDLK_LEFT || key == SDLK_a) {
                    handleInventory(InventoryCommand::Left);
                } else if (key == SDLK_RIGHT || key == SDLK_d) {
                    handleInventory(InventoryCommand::Right);
                } else if (key == SDLK_UP || key == SDLK_w) {
                    handleInventory(InventoryCommand::Up);
                } else if (key == SDLK_DOWN || key == SDLK_s) {
                    handleInventory(InventoryCommand::Down);
                } else if (key == SDLK_RETURN || key == SDLK_SPACE) {
                    handleInventory(InventoryCommand::Confirm);
                } else if (key == SDLK_ESCAPE || key == SDLK_i) {
                    handleInventory(InventoryCommand::Back);
                }
            } else if (screen == Screen::Playing) {
                if (key == SDLK_i && (paused ||
                    (bindings.jump != key && bindings.shoot != key && bindings.interact != key))) {
                    openInventory();
                } else if (key == SDLK_ESCAPE) {
                    if (paused) {
                        if (saveCurrentGame()) {
                            gameSessionActive = false;
                        }
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
                } else if (!paused && key == SDLK_h) {
                    pendingUseItem = true;
                }
            }
        }

        const std::uint64_t now = SDL_GetPerformanceCounter();
        const double elapsed = std::min(static_cast<double>(now - previousCounter) / frequency, 0.25);
        previousCounter = now;
        if (screen == Screen::Playing && !paused) accumulator += elapsed;
        else accumulator = 0.0;
        if (gameSessionActive && screen == Screen::Playing) {
            secondsSinceAutosave += elapsed;
        }

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
        unsigned soundEvents = 0;
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
                pendingUseItem,
            };
            const SoundState before = captureSoundState(game);
            const echo::framework::Status gameplayStatus = game.update(input);
            if (!gameplayStatus) {
                logMessage("Gameplay interaction failed");
                logMessage(gameplayStatus.detail);
            }
            const SoundState after = captureSoundState(game);
            soundEvents |= soundEventsBetween(before, after);
            if (!before.abilityCollected && after.abilityCollected) {
                const echo::framework::Status status = framework.notifyEchoStepUnlocked();
                if (!status) {
                    logMessage("Framework ability synchronization failed");
                    logMessage(status.detail);
                }
            }
            if (after.room != before.room || after.deaths != before.deaths ||
                after.livingEnemies < before.livingEnemies ||
                after.abilityCollected != before.abilityCollected ||
                after.hasMaintenanceKey != before.hasMaintenanceKey ||
                after.healthCellCount != before.healthCellCount ||
                after.scrapCount != before.scrapCount ||
                after.itemCounts != before.itemCounts ||
                after.inventorySlots != before.inventorySlots ||
                after.activeLoot != before.activeLoot ||
                after.completed != before.completed) {
                (void)saveCurrentGame();
            }
            pendingJump = false;
            pendingInteract = false;
            pendingUseItem = false;
            accumulator -= echo::kFixedStep;
        }
        if (gameSessionActive && screen == Screen::Playing && secondsSinceAutosave >= 30.0) {
            (void)saveCurrentGame();
        }
        for (int index = 0; index < static_cast<int>(SoundId::Count); ++index) {
            if ((soundEvents & (1U << index)) != 0U) {
                audio.play(static_cast<SoundId>(index), volume);
            }
        }

#if defined(__PROSPERO__) && defined(ECHO_PS5_STARTUP_DIAGNOSTICS)
        if (diagnosticFirstFrame) {
            diagnosticCheckpoint("ECHO DIAG 11: before first frame render");
        }
#endif
        if (screen == Screen::MainMenu) {
            renderer.drawMenu(menuSelection, continueAvailable);
        } else if (screen == Screen::DualSenseLab) {
            refreshControllerSnapshot();
            renderer.drawDualSenseLab(controllerSnapshot, SDL_NumJoysticks());
        } else if (screen == Screen::Inventory) {
            renderer.drawInventory(game, inventoryMenu, inventoryMessage.data());
        } else {
            const auto jump = keyName(bindings.jump);
            const auto shoot = keyName(bindings.shoot);
            const auto interact = keyName(bindings.interact);
            if (screen == Screen::Options) {
                renderer.drawOptions(optionSelection, volume, dialogueLanguage, waitingForKey,
                                     jump.data(), shoot.data(), interact.data());
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

    (void)saveSettings();
    if (gameSessionActive) {
        (void)saveCurrentGame();
    }
    stopLabOutput();
    if (controller != nullptr) {
        SDL_GameControllerClose(controller);
    }
    SDL_free(controllerSnapshot.touches);
    audio.close();
    renderer.destroy();
    framework.shutdown();
    SDL_Quit();
    logMessage("Application shutdown");
    return 0;
}
