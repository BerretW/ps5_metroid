#include "platform/sdl/assets.hpp"
#include "platform/sdl/framework_integration.hpp"
#include "platform/sdl/game_persistence.hpp"
#include "platform/sdl/renderer.hpp"
#include "platform/sdl/settings_persistence.hpp"

#include <SDL.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void testSettingsPersistence() {
    using echo::framework::GameState;
    using echo::framework::StateScope;
    using echo::framework::StateValue;

    GameState saved{};
    const echo::platform::Bindings original{SDLK_f, SDLK_g, SDLK_h};
    check(static_cast<bool>(echo::platform::encodeSettings(70, original, saved)),
          "settings encode into framework state");
    int volume = 20;
    echo::platform::Bindings bindings{};
    check(static_cast<bool>(echo::platform::decodeSettings(saved, volume, bindings)) &&
              volume == 70 && bindings.jump == SDLK_f &&
              bindings.shoot == SDLK_g && bindings.interact == SDLK_h,
          "settings round-trip volume and keyboard bindings");

    GameState invalid = saved;
    check(static_cast<bool>(invalid.set(
              {StateScope::Global, {}, "audio.volume"}, StateValue::fromInteger(101))),
          "invalid volume fixture is stored");
    const int previousVolume = volume;
    const echo::platform::Bindings previousBindings = bindings;
    check(!echo::platform::decodeSettings(invalid, volume, bindings) &&
              volume == previousVolume && bindings.jump == previousBindings.jump &&
              bindings.shoot == previousBindings.shoot &&
              bindings.interact == previousBindings.interact,
          "invalid settings are rejected without partial application");

    GameState wrongType = saved;
    check(static_cast<bool>(wrongType.set(
              {StateScope::Global, {}, "input.jump"}, StateValue::fromBoolean(true))),
          "invalid key type fixture is stored");
    check(!echo::platform::decodeSettings(wrongType, volume, bindings),
          "settings decoder rejects values with incorrect types");
}

void testGameCheckpointPersistence(echo::platform::FrameworkIntegration& integration) {
    using echo::framework::GameState;
    using echo::framework::StateScope;
    using echo::framework::StateValue;

    echo::Game game;
    check(static_cast<bool>(integration.configureGame(game)), "checkpoint game binds dialogue");
    check(static_cast<bool>(game.update({.interactPressed = true})) &&
              static_cast<bool>(game.update({.interactPressed = true})) &&
              static_cast<bool>(game.update({.interactPressed = true})),
          "checkpoint fixture acquires the key through the loaded dialogue");
    GameState saved{};
    check(static_cast<bool>(echo::platform::encodeGameCheckpoint(game, saved)),
          "game checkpoint encodes to framework state");
    echo::GameCheckpoint restored{};
    check(static_cast<bool>(echo::platform::decodeGameCheckpoint(saved, restored)) &&
              restored.inventory.contains(echo::ItemId::MaintenanceKey),
          "game checkpoint round-trip preserves inventory");

    GameState invalid = saved;
    check(static_cast<bool>(invalid.set(
              {StateScope::Global, {}, "inventory.item.0"}, StateValue::fromInteger(99))),
          "invalid inventory fixture is stored");
    echo::GameCheckpoint unchanged{};
    unchanged.playerHealth = 3;
    check(!echo::platform::decodeGameCheckpoint(invalid, unchanged) &&
              unchanged.playerHealth == 3 && unchanged.inventory.count == 0,
          "checkpoint decoder rejects invalid inventory without partial output");

    GameState migrated{};
    check(echo::platform::migrateGameCheckpointSave(1, migrated, nullptr),
          "version-one gameplay save migration succeeds");
    StateValue migratedCount{};
    StateValue migratedItem{};
    check(migrated.get({StateScope::Global, {}, "inventory.count"}, migratedCount) &&
              migratedCount.integer == 1 &&
              migrated.get({StateScope::Global, {}, "inventory.item.0"}, migratedItem) &&
              migratedItem.integer == static_cast<std::int64_t>(echo::ItemId::MaintenanceKey),
          "legacy saves receive the key to preserve their previously unlocked route");
    check(!echo::platform::migrateGameCheckpointSave(3, migrated, nullptr),
          "unknown future save versions are not migrated");
}

SDL_Window* createdWindow(SDL_Window* previous = nullptr) {
    // The isolated dummy-video session creates only these two windows, in order.
    const Uint32 id = previous == nullptr ? 1 : SDL_GetWindowID(previous) + 1;
    SDL_Window* window = SDL_GetWindowFromID(id);
    if (window == nullptr || SDL_GetRenderer(window) == nullptr) {
        throw std::runtime_error("Created test renderer window is unavailable.");
    }
    return window;
}

std::vector<Uint32> capture(SDL_Window* window, SDL_Rect logicalRegion) {
    SDL_Renderer* renderer = SDL_GetRenderer(window);
    int width = 0;
    int height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0) {
        throw std::runtime_error(SDL_GetError());
    }
    const SDL_Rect region{logicalRegion.x * width / 640, logicalRegion.y * height / 360,
                          logicalRegion.w * width / 640, logicalRegion.h * height / 360};
    std::vector<Uint32> pixels(static_cast<std::size_t>(region.w) * region.h);
    if (SDL_RenderReadPixels(renderer, &region, SDL_PIXELFORMAT_ARGB8888, pixels.data(),
                             region.w * static_cast<int>(sizeof(Uint32))) != 0) {
        throw std::runtime_error(SDL_GetError());
    }
    return pixels;
}

} // namespace

int main() {
    SDL_SetMainReady();
    if (SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) != 0 ||
        SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "software", SDL_HINT_OVERRIDE) != SDL_TRUE ||
        SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL test initialization failed: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }

    using echo::platform::ControlScheme;
    echo::platform::Renderer keyboard{ControlScheme::Keyboard};
    echo::platform::Renderer pad{ControlScheme::PlayStation};
    try {
        echo::platform::FrameworkIntegration integration;
        const auto status = integration.initialize(
            ECHO_SOURCE_ASSETS "framework/game.json",
            ECHO_SOURCE_ASSETS "framework/engineer.json");
        if (!status || !integration.beginGame()) {
            throw std::runtime_error("Framework test content failed to initialize.");
        }
        testSettingsPersistence();
        testGameCheckpointPersistence(integration);
        if (!keyboard.create()) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_Window* keyboardWindow = createdWindow();
        if (!pad.create()) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_Window* padWindow = createdWindow(keyboardWindow);
        echo::Game game;

        keyboard.drawGame(game, false, false, "SPACE", "J", "E");
        const auto keyboardHint = capture(keyboardWindow, {12, 340, 616, 16});
        keyboard.drawGame(game, false, false, "K", "L", "F");
        check(keyboardHint != capture(keyboardWindow, {12, 340, 616, 16}),
              "keyboard gameplay hints follow rebound keys");

        pad.drawGame(game, false, false, "SPACE", "J", "E");
        const auto padHint = capture(padWindow, {12, 340, 616, 16});
        pad.drawGame(game, false, false, "K", "L", "F");
        check(padHint == capture(padWindow, {12, 340, 616, 16}),
              "PlayStation gameplay hints do not expose keyboard bindings");
        check(padHint != keyboardHint, "platform selection changes the gameplay prompts");

        echo::Game dialogueGame;
        check(static_cast<bool>(integration.configureGame(dialogueGame)),
              "renderer game binds loaded dialogue content");
        keyboard.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        const auto emptyInventory = capture(keyboardWindow, {206, 12, 152, 38});
        const auto closedDialogue = capture(keyboardWindow, {64, 234, 512, 82});
        check(static_cast<bool>(dialogueGame.update({.interactPressed = true})),
              "engineer conversation starts");
        check(dialogueGame.dialogueActive(), "nearby engineer can be talked to");
        keyboard.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        check(closedDialogue != capture(keyboardWindow, {64, 234, 512, 82}),
              "engineer dialogue renders in the game viewport");
        check(static_cast<bool>(dialogueGame.update({.interactPressed = true})) &&
                  dialogueGame.dialogueChoiceCount() == 2,
              "engineer dialogue shows choices");
        keyboard.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        const auto firstChoice = capture(keyboardWindow, {82, 264, 340, 14});
        check(static_cast<bool>(dialogueGame.update({.moveX = 1})),
              "dialogue choice navigation succeeds");
        keyboard.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        check(firstChoice != capture(keyboardWindow, {82, 264, 340, 14}),
              "the selected dialogue choice changes on screen");
        const auto keyboardChoiceHint = capture(keyboardWindow, {82, 296, 200, 12});
        pad.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        check(keyboardChoiceHint != capture(padWindow, {82, 296, 200, 12}),
              "dialogue choice navigation uses platform-specific prompts");
        check(static_cast<bool>(dialogueGame.update({.moveX = -1})) &&
                  static_cast<bool>(dialogueGame.update({.interactPressed = true})),
              "selecting the key choice succeeds");
        keyboard.drawGame(dialogueGame, false, false, "SPACE", "J", "E");
        check(emptyInventory != capture(keyboardWindow, {206, 12, 152, 38}),
              "inventory HUD updates when the maintenance key is acquired");

        pad.drawOptions(0, 60, false, "SPACE", "J", "E");
        const auto padOptions = capture(padWindow, {74, 38, 492, 284});
        const auto cross = capture(padWindow, {390, 154, 10, 14});
        const auto square = capture(padWindow, {390, 188, 10, 14});
        const auto triangle = capture(padWindow, {390, 222, 10, 14});
        const auto visibleSymbol = [](const std::vector<Uint32>& pixels) {
            return !pixels.empty() && std::any_of(pixels.begin(), pixels.end(),
                [first = pixels.front()](Uint32 pixel) { return pixel != first; });
        };
        check(visibleSymbol(cross) && visibleSymbol(square) && visibleSymbol(triangle),
              "each PlayStation button symbol has a visible bitmap");
        check(cross != square && square != triangle && cross != triangle,
              "PlayStation actions render distinct button symbols");
        pad.drawOptions(0, 60, true, "K", "L", "F");
        check(padOptions == capture(padWindow, {74, 38, 492, 284}),
              "PlayStation options do not show keyboard bindings or rebinding instructions");

        for (int tick = 0; tick < 2400; ++tick) {
            if (game.roomId() == echo::RoomId::MaintenanceTunnel &&
                game.player().position.x > 930.0F && game.player().position.x < 1040.0F) {
                break;
            }
            const auto update = game.update(
                {.moveX = 1, .jumpPressed = true, .jumpHeld = true, .shootHeld = true});
            if (!update) {
                check(false, "door prompt gameplay fixture updates");
                break;
            }
        }
        check(game.roomId() == echo::RoomId::MaintenanceTunnel &&
                  game.player().position.x > 930.0F && game.player().position.x < 1040.0F,
              "door prompt fixture reaches the laboratory entrance");
        keyboard.drawGame(game, false, false, "SPACE", "J", "E");
        const auto keyboardDoor = capture(keyboardWindow, {520, 285, 12, 14});
        keyboard.drawGame(game, false, false, "SPACE", "J", "F");
        check(keyboardDoor != capture(keyboardWindow, {520, 285, 12, 14}),
              "keyboard door prompt follows the interact binding");
        pad.drawGame(game, false, false, "SPACE", "J", "E");
        const auto padDoor = capture(padWindow, {520, 285, 12, 14});
        pad.drawGame(game, false, false, "SPACE", "J", "F");
        check(padDoor == capture(padWindow, {520, 285, 12, 14}) && padDoor != keyboardDoor,
              "PlayStation door prompt uses the controller symbol");

        keyboard.drawMenu(0);
        pad.drawMenu(0);
        check(capture(keyboardWindow, {86, 326, 492, 16}) != capture(padWindow, {86, 326, 492, 16}),
              "main menu uses platform-specific navigation prompts");
        keyboard.drawMenu(3);
        const auto labMenuEntry = capture(keyboardWindow, {120, 246, 230, 24});
        keyboard.drawMenu(2);
        check(labMenuEntry != capture(keyboardWindow, {120, 246, 230, 24}),
              "main menu exposes a distinct DualSense Lab entry");

        echo::input::ControllerSnapshot simulatedController;
        simulatedController.connected = true;
        echo::input::setText(simulatedController.name, "Simulated DualSense");
        simulatedController.battery = SDL_JOYSTICK_POWER_FULL;
        keyboard.drawDualSenseLab(simulatedController, 1);
        const auto idleCross = capture(keyboardWindow, {229, 181, 44, 16});
        const auto centeredStick = capture(keyboardWindow, {63, 267, 38, 38});
        simulatedController.buttons[SDL_CONTROLLER_BUTTON_A] = true;
        simulatedController.axes[SDL_CONTROLLER_AXIS_LEFTX] = 16000;
        keyboard.drawDualSenseLab(simulatedController, 1);
        check(idleCross != capture(keyboardWindow, {229, 181, 44, 16}),
              "DualSense Lab highlights an observed button press");
        check(centeredStick != capture(keyboardWindow, {63, 267, 38, 38}),
              "DualSense Lab moves the live stick indicator");
        simulatedController.rumbleAvailable = true;
        keyboard.drawDualSenseLab(simulatedController, 1);
        const auto rumbleAvailable = capture(keyboardWindow, {344, 246, 270, 10});
        simulatedController.pendingConfirmation = echo::input::OutputTest::Rumble;
        keyboard.drawDualSenseLab(simulatedController, 1);
        const auto rumblePending = capture(keyboardWindow, {344, 246, 270, 10});
        simulatedController.pendingConfirmation = echo::input::OutputTest::None;
        simulatedController.rumbleHardwareConfirmed = true;
        keyboard.drawDualSenseLab(simulatedController, 1);
        const auto rumbleConfirmed = capture(keyboardWindow, {344, 246, 270, 10});
        check(rumblePending != rumbleAvailable && rumbleConfirmed != rumbleAvailable &&
                 rumbleConfirmed != rumblePending,
              "DualSense Lab shows pending and recorded output confirmations");
        keyboard.drawGame(game, false, true, "SPACE", "J", "E");
        pad.drawGame(game, false, true, "SPACE", "J", "E");
        check(capture(keyboardWindow, {195, 199, 260, 14}) != capture(padWindow, {195, 199, 260, 14}),
              "pause menu uses platform-specific resume and back prompts");

        const SDL_Rect playerRegion{static_cast<int>(game.player().position.x - game.cameraX()) - 2,
                                    static_cast<int>(game.player().position.y) - 2, 24, 32};
        pad.drawGame(game, false, false, "SPACE", "J", "E");
        const auto fallbackPlayer = capture(padWindow, playerRegion);
        check(pad.loadSprites("Z:/project-echo-missing-assets/") == 0 &&
                  !pad.hasSprite(echo::platform::Sprite::Player),
              "missing asset directory keeps the built-in art");
        pad.drawGame(game, false, false, "SPACE", "J", "E");
        check(fallbackPlayer == capture(padWindow, playerRegion), "fallback art renders without sprites");
        check(pad.loadSprites(ECHO_SOURCE_ASSETS) == static_cast<int>(echo::platform::Sprite::Count),
              "all packaged PNG sprite sheets decode with expected dimensions");
        pad.drawGame(game, false, false, "SPACE", "J", "E");
        check(fallbackPlayer != capture(padWindow, playerRegion), "player is drawn from the PNG sprite sheet");

        echo::platform::SoundBuffer sound;
        check(echo::platform::loadWavMono16(ECHO_SOURCE_ASSETS "sounds/shot.wav", 48000, sound) &&
                  sound.count == 4320,
              "48 kHz WAV loads without resampling");
        check(echo::platform::loadWavMono16(ECHO_SOURCE_ASSETS "sounds/shot.wav", 44100, sound) &&
                  sound.count > 3900 && sound.count < 4100,
              "WAV is converted to the audio device rate");
        echo::platform::freeSound(sound);
        check(echo::platform::loadPngTexture(SDL_GetRenderer(padWindow), ECHO_SOURCE_ASSETS "sounds/shot.wav") ==
                  nullptr,
              "non-PNG data is rejected by the sprite decoder");
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "SDL prompt test failed: " << error.what() << '\n';
    }
    pad.destroy();
    keyboard.destroy();
    SDL_Quit();
    if (failures != 0) {
        return EXIT_FAILURE;
    }
    std::cout << "Platform-specific SDL prompts passed\n";
    return EXIT_SUCCESS;
}
