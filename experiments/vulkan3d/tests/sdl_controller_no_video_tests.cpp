#include <SDL.h>

#include <cstdio>

int main() {
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL controller initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "Controller-only SDL initialization unexpectedly enabled video\n");
        SDL_Quit();
        return 1;
    }
    const int device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
                                                 SDL_CONTROLLER_AXIS_MAX,
                                                 SDL_CONTROLLER_BUTTON_MAX, 0);
    if (device < 0) {
        std::fprintf(stderr, "Could not attach a virtual controller: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GameController* controller = SDL_GameControllerOpen(device);
    if (!controller) {
        std::fprintf(stderr, "Could not open the virtual controller: %s\n", SDL_GetError());
        SDL_JoystickDetachVirtual(device);
        SDL_Quit();
        return 1;
    }
    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
    const SDL_JoystickID id = SDL_JoystickInstanceID(joystick);
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    const bool injected = SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTX, 12345) == 0 &&
                          SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_A, SDL_PRESSED) == 0;
    bool axisEvent = false;
    bool buttonEvent = false;
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_CONTROLLERAXISMOTION && event.caxis.which == id &&
            event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX && event.caxis.value == 12345) {
            axisEvent = true;
        }
        if (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.which == id &&
            event.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
            buttonEvent = true;
        }
    }
    const bool input = injected && axisEvent && buttonEvent &&
                       SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX) == 12345 &&
                       SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A) == SDL_PRESSED;
    if (!input) std::fprintf(stderr, "SDL controller input did not update without video: %s\n", SDL_GetError());
    SDL_GameControllerClose(controller);
    const bool detached = SDL_JoystickDetachVirtual(device) == 0;
    if (!detached) std::fprintf(stderr, "Could not detach the virtual controller: %s\n", SDL_GetError());
    SDL_Quit();
    return input && detached ? 0 : 1;
}
