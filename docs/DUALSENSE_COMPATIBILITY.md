# DualSense compatibility

The application uses the repository's pinned SDL2 fork, commit `ee4c47dc0d617b3bc8f35108f9956baf228a1322` (SDL 2.30.12). The PS5 app uses the existing native SDL PS5 joystick driver and PS5 SDK; the lab does not add a new native controller API or change the SDL patches.

## PS5 backend source audit

Evidence comes from the pinned SDL PS5 backend under `.deps/sources/sdl/src/joystick/ps5/`:

| Feature | Backend evidence | Lab interpretation |
| --- | --- | --- |
| Device name | `PS5_JoystickGetDeviceName` returns the context name populated by `sceUserServiceGetUserName`, with a user-ID fallback. | On PS5 the SDL-reported name is the console profile label, not the controller model. It does not identify a mouse or another device type; selection uses the joystick instance ID. |
| Buttons, sticks, analog L2/R2 | `SDL_ps5joystick.c` maps face buttons, shoulders, stick clicks, d-pad, touchpad click and six axes from `scePadReadState`. | Available through SDL polling; supported status requires observed input. |
| Touchpad click | The game-controller mapping maps touchpad to button index 4. | Button state is shown separately from touch contacts. |
| Touch contacts | The PS5 driver declares touch fields in its local pad-state structure but never translates contacts to SDL touchpad slots. `SDL_GameControllerGetNumTouchpads` therefore reports none for this driver. | UNSUPPORTED by the pinned PS5 SDL backend; no touch coordinates or pressure are fabricated. |
| Gyroscope / accelerometer | The local pad-state structure has sensor fields, but the driver does not expose sensor capabilities or data; `PS5_JoystickSetSensorsEnabled` returns `SDL_Unsupported()`. | UNSUPPORTED via SDL on PS5. The mere presence of fields in a struct is not an implemented sensor path. |
| Ordinary rumble | `PS5_JoystickGetCapabilities` advertises rumble and `PS5_JoystickRumble` calls `scePadSetVibration`. | API can be tested for at most 250 ms. Operator confirmed physical vibration on PS5 hardware (2026-11-20). |
| Trigger rumble | `PS5_JoystickRumbleTriggers` returns `SDL_Unsupported()`. | UNSUPPORTED. This is separate from adaptive trigger resistance. |
| RGB lightbar | The backend advertises LED support and `PS5_JoystickSetLED` calls `scePadSetLightBar`. | API can be tested with explicit colors. Operator confirmed visible color changes on PS5 hardware (2026-11-20). |
| Adaptive triggers | No adaptive-trigger function or effect packet is implemented in this backend. | UNSUPPORTED; generic trigger rumble is not treated as adaptive resistance. |
| Create, PS, mute | The PS5 button map omits Create/back, Guide/PS and Misc1/mute. | These are shown as unavailable on PS5, not as confirmed unpressed buttons. |
| Battery | The PS5 driver does not provide an SDL power-level implementation. | Usually UNKNOWN; no battery percentage is inferred. |
| Device name | `PS5_JoystickDetect` fills the name with `sceUserServiceGetUserName` for the signed-in user. | The lab shows and exports the account name, not a controller model name. |
| Speaker / microphone | The backend does not route SDL audio to a controller-specific speaker or expose controller microphone capture. | UNSUPPORTED on PS5. No audio is played to or captured from the controller. |

SDL2 itself declares touchpad, sensor, rumble, trigger-rumble and LED APIs, but declarations alone do not mean a given driver implements them. The diagnostic queries per-device capabilities and reports API availability separately from physical verification.

## Windows

The SDL Windows driver and connected device determine capabilities at runtime. The lab queries SDL game-controller capabilities, lists audio output and capture device names in the exported report, and does not assume that a generic system audio endpoint is the DualSense speaker or microphone. Windows physical hardware testing has not been performed by the automated build.

The lab's stick dead zone is a configurable display/calibration aid; it does not change PROJECT ECHO gameplay input. Changing dead zone, controller selection, rumble or LED is confined to the lab screen. No keyboard initialization or additional PS5 SDK dependency is introduced.
