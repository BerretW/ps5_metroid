# Controls

| Action | Windows keyboard | Windows controller (SDL names) | PS5 DualSense |
| --- | --- | --- | --- |
| Move | Left/Right arrows or A/D | Left stick or D-pad | Left stick or D-pad |
| Jump / double jump | Space (configurable) | A | Cross × |
| Fire | J (configurable) | X | Square □ |
| Use door | E (configurable) | Y | Triangle △ |
| Pause / resume | Escape / Enter | Start / A | Options / Cross × |
| Toggle map | M | Not assigned | Touchpad press |
| Menu navigation / confirm | Up/Down, Enter | D-pad, A | D-pad, Cross × |
| Return from options or paused game | Escape | Not assigned | Circle ○ |
| Open DualSense Lab | Main menu selection | Main menu selection | Main menu selection |

Inside DualSense Lab, Cross starts a short rumble test, Square cycles the lightbar, Triangle exports diagnostics, Circle returns to the menu, L3 tests trigger rumble only if exposed, and L1/R1 record an operator-confirmed output result. D-pad left/right selects a controller; up/down adjusts the lab-only stick dead zone. Windows also accepts R/L/E/T, 1/2, Escape and arrow keys.

Run `build.bat` and choose Windows or PS5. The actual target selects the prompt scheme even when built directly through PowerShell: Windows uses keyboard labels; PS5 uses bitmap button symbols in the menu, options, gameplay hints, door prompt and pause screen.

Windows Options can rebind jump, fire and interact for the current run. The gameplay and door hints follow these bindings. On PS5 the face-button bindings are fixed and Options displays their symbols instead of offering keyboard rebinding; volume is still adjustable with D-pad left/right. Bindings are not persisted between launches.

PS5 physical keyboard and IME support remain disabled. SDL's PS5 backend maps cross to A, circle to B, square to X, triangle to Y, touchpad to TOUCHPAD and Options to START. The lab queries actual SDL backend capabilities; its rumble/lightbar tests need hardware confirmation. See [DualSense Lab](DUALSENSE_LAB.md), [compatibility findings](DUALSENSE_COMPATIBILITY.md) and the [hardware checklist](DUALSENSE_HARDWARE_TESTS.md).
