# Controls

| Action | Windows keyboard | Windows controller (SDL names) | PS5 DualSense |
| --- | --- | --- | --- |
| Move | Left/Right arrows or A/D | Left stick or D-pad | Left stick or D-pad |
| Move up/down (isometric rooms only) | Up/Down arrows; W/S when not bound to an action | Left stick or D-pad up/down | Left stick or D-pad up/down |
| Aim (isometric rooms only) | Mouse | Right stick | Right stick |
| Fire while aiming (isometric rooms only) | Left mouse button | R2 | R2 |
| Jump / double jump | Space (configurable) | A | Cross × |
| Fire | J (configurable) | X | Square □ |
| Interact / advance dialogue / use door | E (configurable) | Y | Triangle △ |
| Use active item | H | Right shoulder | R1 |
| Open inventory | I (also from pause) | Left shoulder | L1 |
| Inventory navigate / select / move | Arrows/WASD, Enter/Space | D-pad, A | D-pad, Cross × |
| Inventory cancel selection / close to pause | Escape / I | B / Start | Circle ○ / Options |
| Select dialogue choice | Left/Right arrows or A/D | Left stick or D-pad left/right | Left stick or D-pad left/right |
| Pause / resume | Escape / Enter | Start / A | Options / Cross × |
| Toggle map | M | Not assigned | Touchpad press |
| Menu navigation / confirm | Up/Down, Enter | D-pad, A | D-pad, Cross × |
| Return from options or paused game | Escape | Not assigned | Circle ○ |
| Open DualSense Lab | Main menu selection | Main menu selection | Main menu selection |

Inside DualSense Lab, Cross starts a short rumble test, Square cycles the lightbar, Triangle exports diagnostics, Circle returns to the menu, L3 tests trigger rumble only if exposed, and L1/R1 record an operator-confirmed output result. D-pad left/right selects a controller; up/down adjusts the lab-only stick dead zone. Windows also accepts R/L/E/T, 1/2, Escape and arrow keys.

Run `build.bat` and choose Windows or PS5. The actual target selects the prompt scheme even when built directly through PowerShell: Windows uses keyboard labels; PS5 uses bitmap button symbols in the menu, options, gameplay hints, door prompt and pause screen.

Options lets you select EN or CS for Mira's dialogue with left/right (D-pad left/right on PS5); the selection persists in the separate settings save. Windows Options can also rebind jump, fire and interact. Bindings persist, and gameplay, dialogue and door prompts follow the interact binding. On PS5 the face-button bindings are fixed and Options displays their symbols instead of offering keyboard rebinding; volume remains adjustable with D-pad left/right.

Windows Options has a DISPLAY row: left/right cycles 1280X720 window, 1920X1080 window and desktop FULLSCREEN, applies it immediately and saves it. The game keeps its 640x360 logical view with integer scaling, so larger modes show the same picture larger. Settings saved before this option default to 1280x720. PS5 has no DISPLAY row and always creates a display-sized fullscreen window.

The Landing Bay's west exit leads to the isometric Cargo Hold, drawn Diablo 2-style with a 2:1 diamond grid. There is no gravity or jump: the stick, D-pad or arrows walk in eight screen directions, and Fire shoots along the aim. Aiming is twin-stick: the mouse (Windows) or right stick aims independently of movement, and an orange crosshair shows the direction. While mouse-aiming on Windows the cursor is locked inside the window and replaced by the crosshair; pausing (Esc), the map, menus or switching windows release it. Moving the mouse selects mouse aim until a controller button or the right stick takes over; with the right stick released and no mouse aim, shots follow the last movement direction. The usual Fire key/button still works, plus the left mouse button and R2. Crates in front of the player become translucent. Stalkers chase you on sight; the floor pools hurt but can be crossed. Leave through the east doorway to return to the side-view Landing Bay.

In the Landing Bay, approach Engineer Mira and press Use to begin or advance her dialogue. When choices appear, press left/right to select either the lab information or the Maintenance Key, then press Use to confirm. The inventory HUD shows the key, which opens the Research Laboratory door. After obtaining it, her conversation reflects that it is already owned. Return to her after finding Echo Step to ask how it opens the Central Chamber gate.

Inventory contains 20 fixed backpack slots, separate Head/Body/Weapon/Accessory
equipment slots, and one physical active-item stack. Select an occupied slot,
confirm, navigate to a destination, then confirm to move, swap or merge a
complete stack. Incompatible transfers and stack overflow do not change either
slot. Back first cancels the selected source; pressing it again closes to pause.
Inventory freezes gameplay and successful moves autosave. If I is already bound
to jump/fire/interact, that action keeps working during gameplay; open inventory
with I from pause instead.

Open inventory during play with I/L1 (the pause screen also lists the binding).
After collecting a Health Cell from Mira while injured, move its backpack stack
to Active, then press H/R1 to restore up to two health. Backpack cells cannot
be used directly. A cell is not consumed at full health; the active stack holds
up to three. Mira only offers another while injured and owning none across
compartments. Her horizontal interaction reach is configurable in
[game.json](../assets/framework/game.json); see
[component schemas](framework/COMPONENT_SCHEMAS.md) for limits.

Equipment auto-equips when its compatible slot is empty; otherwise it goes to
the backpack. Move it to the correct slot to use its bonus. Mira's Kinetic Coil
and the Vital Vest both use Body, so only one can be equipped at a time. Legacy
saves move their Coil to Body and first cell stack to Active automatically.

Defeated enemies leave visible Scrap piles where they fall: two in the
Maintenance Tunnel and three in the Power Station. Walk over a pile to collect
it; Scrap stacks to 99 and its total appears in the HUD. If the backpack is
full, the pile stays available until you make room.

Three additional enemies carry ten new items. The orange-marked Charger in the
Maintenance Tunnel pursues nearby players; the cyan Armored enemy in the Power
Station periodically stops and guards (white marker); the red Sentry in the
Research Laboratory fires projectiles. Their drops are collected by touch:

| Enemy | Item | Effect |
| --- | --- | --- |
| Charger | Jump Visor | Head: jump impulse +40 |
| Charger | Med Patch | Active: heal 1 |
| Charger | Speed Charge | Active: speed +25 for 8 seconds |
| Armored | Vital Vest | Body: maximum health +1 |
| Armored | Power Barrel | Weapon: shot damage +1 |
| Armored | Med Kit | Active: heal up to 4 |
| Sentry | Sprint Module | Accessory: speed +15 |
| Sentry | Shield Cell | Active: prevent damage for 4 seconds |
| Sentry | Jump Charge | Active: jump impulse +60 for 8 seconds |
| Sentry | Blast Charge | Active: deal 2 damage to nearby enemies |

Move consumables from the backpack to Active before using H/R1. Healing items
are not consumed at full health; other charges are consumed on use. Armored
guard reduces each hit by one damage. Item descriptions also appear in the
inventory screen.

PS5 physical keyboard and IME support remain disabled. SDL's PS5 backend maps cross to A, circle to B, square to X, triangle to Y, touchpad to TOUCHPAD and Options to START. The lab queries actual SDL backend capabilities; its rumble/lightbar tests need hardware confirmation. See [DualSense Lab](DUALSENSE_LAB.md), [compatibility findings](DUALSENSE_COMPATIBILITY.md) and the [hardware checklist](DUALSENSE_HARDWARE_TESTS.md).
