# Controls

| Action | Windows keyboard | Windows controller (SDL names) | PS5 DualSense |
| --- | --- | --- | --- |
| Move | Left/Right arrows or A/D | Left stick or D-pad | Left stick or D-pad |
| Jump / double jump | Space (configurable) | A | Cross × |
| Fire | J (configurable) | X | Square □ |
| Interact / advance dialogue / use door | E (configurable) | Y | Triangle △ |
| Select dialogue choice | Left/Right arrows or A/D | Left stick or D-pad left/right | Left stick or D-pad left/right |
| Pause / resume | Escape / Enter | Start / A | Options / Cross × |
| Toggle map | M | Not assigned | Touchpad press |
| Menu navigation / confirm | Up/Down, Enter | D-pad, A | D-pad, Cross × |
| Return from options or paused game | Escape | Not assigned | Circle ○ |
| Open DualSense Lab | Main menu selection | Main menu selection | Main menu selection |

Inside DualSense Lab, Cross starts a short rumble test, Square cycles the lightbar, Triangle exports diagnostics, Circle returns to the menu, L3 tests trigger rumble only if exposed, and L1/R1 record an operator-confirmed output result. D-pad left/right selects a controller; up/down adjusts the lab-only stick dead zone. Windows also accepts R/L/E/T, 1/2, Escape and arrow keys.

Run `build.bat` and choose Windows or PS5. The actual target selects the prompt scheme even when built directly through PowerShell: Windows uses keyboard labels; PS5 uses bitmap button symbols in the menu, options, gameplay hints, door prompt and pause screen.

Options lets you select EN or CS for Mira's dialogue with left/right (D-pad left/right on PS5); the selection persists in the separate settings save. Windows Options can also rebind jump, fire and interact. Bindings persist, and gameplay, dialogue and door prompts follow the interact binding. On PS5 the face-button bindings are fixed and Options displays their symbols instead of offering keyboard rebinding; volume remains adjustable with D-pad left/right.

In the Landing Bay, approach Engineer Mira and press Use to begin or advance her dialogue. When choices appear, press left/right to select either the lab information or the Maintenance Key, then press Use to confirm. The inventory HUD shows the key, which opens the Research Laboratory door. After obtaining it, her conversation reflects that it is already owned. Return to her after finding Echo Step to ask how it opens the Central Chamber gate.

PS5 physical keyboard and IME support remain disabled. SDL's PS5 backend maps cross to A, circle to B, square to X, triangle to Y, touchpad to TOUCHPAD and Options to START. The lab queries actual SDL backend capabilities; its rumble/lightbar tests need hardware confirmation. See [DualSense Lab](DUALSENSE_LAB.md), [compatibility findings](DUALSENSE_COMPATIBILITY.md) and the [hardware checklist](DUALSENSE_HARDWARE_TESTS.md).
