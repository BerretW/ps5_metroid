#include "platform/sdl/renderer.hpp"

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
            game.update({.moveX = 1, .jumpPressed = true, .jumpHeld = true, .shootHeld = true});
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
        keyboard.drawMenu(2);
        const auto labMenuEntry = capture(keyboardWindow, {120, 231, 210, 24});
        keyboard.drawMenu(1);
        check(labMenuEntry != capture(keyboardWindow, {120, 231, 210, 24}),
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
