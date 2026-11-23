# Acceptance status

Statuses are evidence-based: **PASS**, **FAIL**, or **NOT TESTED**. Update after running validation; a source implementation alone does not count as a runtime result.

**PS5 operator report (2026-11-23):** The operator reported that the current
Vulkan package works on PS5. The binary digest, hardware setup and specific
features exercised were not stated; the report cannot be attributed to a
specific recorded digest or interpreted as verification of individual features.

**Data-driven main quest ordering:** The three supported objectives can be
reordered and the route choice moved, subject to key-before-choice and
choice-before-exit validation. Gameplay tests cover later choices and restoring
existing selected routes; Windows Debug build/tests pass 22/22. Both PS5
controller and Vulkan packages compile and pass static package tests, including
valid and invalid quest timings. Controller FSELF: 1,943,681 bytes, SHA-256
`29c310fac864496ab8b4738263b6f91184c898e871e8cbeb7bb54c69e60ef785`.
Vulkan FSELF: 28,217,997 bytes, SHA-256
`c8765e56a27e8269ce5bb8e94fed0836e953a5f1a216b45b2bc414d8dd64781b`.
These specific packages have not been tested on PS5 hardware.

**Data-driven quest rewards (2026-11-23):** Quest route reward IDs and
quantities are no longer duplicated in the project quest contract. Startup,
host content validation and PS5 package checks validate the configured rewards
against the item catalog and stack limits. Gameplay tests cover changed rewards,
pending unique equipment, and checkpoint restore. Windows Debug build/tests pass
22/22. Both native Windows PS5 variants compile, link, pass static validation
and package tests under `PPSA91007`, including alternate and invalid rewards.
Controller FSELF: 1,942,769 bytes, SHA-256
`0e5f5dd549fc981526ab274be26d3eed3470e0261a6a965636e2f8bf840d0b0f`.
Vulkan FSELF: 28,217,101 bytes, SHA-256
`3c45595d8a8aaa45caa44176525ebf436687999f3751bf0af1a5be5406688d5b`.
Both contain the 9,841-byte `game.json` with SHA-256
`2a6a3d349433dcb0e328e01756433f6819e95ec9e2c95561a7936bd9a42b6c86`.
Neither exact digest has been identified in the PS5 hardware report above.

**Data-driven armored guard (2026-11-23):** Guard cycle, start and damage
reduction load from inherited object properties with placement overrides.
Gameplay, damage and side-view rendering use the same guard state. Windows
Debug build/tests pass 22/22, including headless behavior and invalid-content
tests and an offscreen guard-marker check. Both native Windows PS5 variants
compile, link, pass static validation and their package tests under
`PPSA91007`; package tests do not check the gameplay-specific guard bounds,
which world loading validates. Controller FSELF: 1,942,337 bytes, SHA-256
`7b6c99e7e8b3bfc2e6046b0d337f0f7c1659dfbf85e7998d5b7eca63d4b00f46`.
Vulkan FSELF: 28,216,717 bytes, SHA-256
`f656d0a960cc8cb27314915a278daa9036976e373b70e22dfd88b73e5bd8f47d`.
Both contain the 416-byte armored object with SHA-256
`c1467d36089f8bc14422cf9c5627d3b1a040368ac78afd7e2fde2bcb589b2dbb`.
These packages have not been tested on PS5 hardware.

**Data-driven sentry firing (2026-11-23):** Sentry detection range, projectile
speed and fire interval now load from inherited world-object properties, with
placement overrides and bounded validation. Headless tests cover tuning,
boundaries, malformed data and gameplay from a placement override. Windows
Debug build/tests pass 22/22. Both native Windows PS5 variants compile, link,
pass static validation and their package tests under `PPSA91007`; package
tests do not check the new gameplay-specific bounds, which world loading
validates. Controller FSELF: 1,941,649 bytes, SHA-256
`45b5f8db02dc787d0b4d9f99dcdd293246c8ceec6bb196dd4b141b5f784387e8`.
Vulkan FSELF: 28,216,077 bytes, SHA-256
`ba013a947ee82897568ef2176e988f515ca13f465d05ff0149b4ca1f5c4f6737`.
Both contain the 351-byte sentry object with SHA-256
`2ec34f9744efc61ac2524238b57776ad9db715c435715885fc51b5a154caa5d7`.
These packages have not been tested on PS5 hardware.

**Data-driven enemy pursuit (2026-11-23):** Charger detection range and charge
speed, and stalker detection range, load from inherited world-object properties
with room-placement overrides. Headless tests exercise pursuit, overrides and
invalid inputs; Windows Debug build/tests pass 22/22. Both native Windows PS5
variants compile, link, pass static validation and their package tests under
`PPSA91007`. Those package tests do not validate the new gameplay-specific
property bounds; world loading does. Controller FSELF: 1,940,897 bytes,
SHA-256 `349ed1df460ed65477586630ca8dbf3ad95258a33f4376bafb63a4edaf5d9366`.
Vulkan FSELF: 28,215,309 bytes, SHA-256
`64ea1c1c2ef3139d363a9bf654ee356c51a6b35383b4d9fad4233ddee4218968`.
These packages have not been tested on PS5 hardware.

**Data-driven item effects (schema v2, 2026-11-23):** At that milestone, the
version-2 item catalog provides bounded descriptions and use-effect parameters
for healing, shielding, speed/jump boosts, and area damage. Gameplay dispatches
only compiled effect handlers; the JSON cannot load code. Autosave format 8
persists boost magnitudes and migrates formats 1-7 using legacy values. Windows
tests pass 22/22. Both native Windows PS5 variants compiled, linked, passed
static validation, and passed package rejection tests under `PPSA91007`. FSELF
SHA-256 values for that schema-v2 revision were
`a4e7a0d21beebfb85e3e64ccde41799feb3ad0f44fa817dfbe9f9f77af5421fb` (SDL
controller, 1,936,305 bytes) and
`496b6cd75b90b1a733d060146dea014487a1e841f156422f344d20e168e5309e` (Vulkan,
28,210,829 bytes). Neither package has been tested on PS5 hardware. The
operator's 2026-11-23 PS5 report did not identify the tested variant or digest
and does not verify this revision. The earlier `echo_control_prompts` stack
overflow did not recur in the full suite after the fixed-capacity equipment
module was moved out of the stack-resident integration object; a call-stack
diagnosis was not available.

**Catalog equipment bindings (2026-11-23):** Item catalog schema version 3
moves item-to-equipment and slot bindings into `assets/framework/items.json`.
Stable numeric item keys still preserve save compatibility, while the compiled
Equipment module remains the allowlist for gear modifiers. Windows Debug
build/tests pass 22/22. Both native Windows PS5 variants compile and link;
static validation and package rejection tests pass. The controller package
(`dist/ps5-controller/PPSA91007/eboot.bin`) is 1,933,025 bytes, SHA-256
`49a201a2fb722eec973cb2a4af3bf7651c99d825966ae94418d272cf93b4f766`; the
Vulkan package (`dist/ps5-vulkan/PPSA91007/eboot.bin`) is 28,207,581 bytes,
SHA-256 `6bf8fc92da231a2baff3f74508561ecf574f37a7e617ba006b157d8cd3479e64`.
Neither schema-v3 package has been tested on PS5 hardware.

**Data-driven enemy loot (2026-11-23):** Item IDs, quantities, and mappings
from the 12 stable loot save slots to enemy slots now load from the bounded
version-1 `loot` table in `assets/framework/game.json`. Existing save formats
are unchanged; active extra loot records are reconstructed using the configured
table and validated by gameplay restore. Windows Debug build/tests pass 22/22.
Both native Windows PS5 variants compile, link, pass static validation, and pass
package rejection tests, including malformed loot references and duplicate
save slots. The controller package is 1,936,513 bytes, SHA-256
`f1d01ae39e295a9602bd3fb1c12532f68051a0702199bd12231b16d2a0c8347c`; the
Vulkan package is 28,211,085 bytes, SHA-256
`dccfc354ced1cb64b0d7917a04072feb7cd0552c623dd112255958b8cdf18def`.
Neither package has been individually verified on PS5 hardware. On
2026-11-23, the operator reported that a PS5 build passed and the game works;
the tested variant, package digest, and setup were not specified, so this
report cannot be attributed to either recorded package.

**Data-driven player movement and combat tuning (2026-11-23):** The Attributes
block in `assets/framework/game.json` now defines seven side-view movement parameters:
horizontal acceleration, gravity, maximum fall speed, coyote and jump-buffer
times, jump-release gravity scaling, and double-jump impulse scaling. They are
bounded integer attributes; times use milliseconds and scales use percent.
It also defines the player projectile speed, fire interval, and invulnerability
duration. The fixed-step simulation reads these values while preserving its
existing algorithms. Windows Debug tests pass 22/22. Both native Windows PS5
variants compile, link, pass static validation, and pass their package tests under
`PPSA91007`. The controller FSELF is 1,938,881 bytes, SHA-256
`8b16fb242f786d516ce075399f0625c6ec2965a34c845a6f4ba9c61b8b81062e`; the
Vulkan FSELF is 28,213,453 bytes, SHA-256
`498a7d1a9dcb17a5da935663d4b83791533310c4365d16a066053ce51e54ff66`. Both
package the 9,841-byte `game.json` with SHA-256
`2a6a3d349433dcb0e328e01756433f6819e95ec9e2c95561a7936bd9a42b6c86`. These
specific versions have not been tested on PS5 hardware.

**Partial presentation extraction (2026-11-23):** main menu, options and
inventory now use SDL-free canvas presentation, directly in the Vulkan path
and through an SDL adapter in the legacy path. The default Windows build no
longer creates the legacy application target. PNG reads close the file on a
short read, and canvas initialization and sprite-upload failures roll back
completed resources. Windows Debug and Release pass 22/22 tests including the
production Vulkan smoke test; the Vulkan experiment GPU/readback suite passes.
PS5 Vulkan cross-build and package rejection tests pass statically; this new
package has NOT been tested on hardware. The Vulkan game still compiles the
SDL renderer for the world and remaining UI, so the migration is incomplete.

**Cargo Hold emergency-power objective:** the Cargo Control terminal and north
panel now support a dedicated repair quest with state-aware dialogue and HUD
progress. Windows build/tests passed 22/22. The current PS5 Vulkan package
(`dist/ps5-vulkan/PPSA91007`) passes static validation with 73 validated assets,
and `tests/ps5_package_tests.ps1 -Vulkan` passes, including missing/malformed
quest and dialogue cases. PS5 hardware execution is NOT TESTED.

### Manual PS5 verification for a new Vulkan candidate

Back up the existing title package and saves before manual transfer; retain
PPSA91007 and install only one variant. Record the installed digest, firmware,
loader, observations and any errors.

- Boot and menu, options and prompts; start a new game.
- Move, jump, aim and shoot in the platformer; verify audio.
- Enter Cargo Hold, move and fight, and check its lighting and effects.
- Open inventory, dialogue, map, pause and cutscene screens.
- Save, return to menu, Continue and verify restored state.
- Shut down and relaunch; check for errors and controller reconnection.

**Vulkan-first migration checkpoint (2026-11-22):** the native Windows Vulkan
production target and PS5 Vulkan game path build successfully. The Vulkan GPU
experiment passes 35/35 tests, Windows Debug/Release pass 20/20 CTest cases, and
the PS5 Vulkan game package passes native static validation. On 2026-11-22,
the operator reported the PS5 Vulkan game fully functional. The installed binary
digest and individual checks were not provided, so this is not a detailed
hardware acceptance report. SDL remains available explicitly
as a legacy/diagnostic renderer; SDL controller, audio and event services remain
active.

**Windows Vulkan startup fix (2026-11-22):** the operator reported missing
`scene.vert.spv` at startup. The Windows build now compiles and validates both
game shaders, and the shader directory is resolved from the executable's asset
root. The production-game smoke test starts from a different working directory,
presents one menu frame on the NVIDIA GPU and shuts down without accessing saves.
The operator subsequently confirmed that the Windows build works; individual
gameplay checks were not specified.

**Window and build-selection follow-up:** Windows Vulkan Debug and Release pass 21/21
tests including a production menu-frame, resize and mouse-mapping GPU smoke test;
the separate Vulkan GPU suite passes 35/35 including readbacks. Windows legacy
Debug passes 20/20. PS5 Hello, the default Vulkan game and explicit legacy
controller game build and pass static package validation; package rejection tests
pass for both game variants. Earlier packages were backed up before rebuilding.
These automated results do not exercise every gameplay screen interactively.
The game presentation implementation still depends on SDL; the requested
complete SDL-independent split remains outstanding.

**Bitmap text extraction:** the glyph rasterizer and sprite-sheet definitions
are now SDL-independent and shared by the existing renderer. Window-free tests
cover Czech diacritics and invalid UTF-8; Windows Vulkan Debug passes 21/21,
and the Vulkan GPU suite passes 35/35. The rebuilt PS5 Vulkan package passes
static and package-rejection checks. After it was presented, the operator
reported that Windows and PS5 both run cleanly. The installed digest and
individual checks were not supplied.

**Milestone: PLAYABLE prototype.** The native Windows application builds and launches, the shared gameplay simulation passes its headless tests, and the PS5 controller build is reported playable by the operator. This is a development baseline, not a claim that every feature or platform behavior is complete.

**Menu glyph update:** Windows Vulkan tests pass with a GPU-rendered supplied
WebP-only GLB, zero reported validation errors, generated tangents, and normal
mapping. Windows SDL/game regression tests pass. Both PS5 variants compile,
link and pass static package tests. On 2026-11-22, after these packages were
presented, the operator reported full functionality; the report did not
identify the tested variant(s), installed digest, setup, or individual checks.
Current local FSELF SHA-256 (not verified as installed):
SDL `8f276bd749620dd8396d081e3bf4ab79fda89feb7e395ed88f309c47ccc87a2c`;
Vulkan `ee8b379f0a590ba383301d4af98d54f23c0c6e1d92f9ebde3006f0dcb8225b76`.
The same `PPSA91007` identity is used by both: upload one complete variant
manually, not both at once.

| # | Acceptance criterion | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Native Windows compile and launch | PASS | Debug and Release CMake/Ninja builds succeeded; the Release SDL app opened a `PROJECT ECHO` window, initialized video/controller subsystems, and opened an audio device. |
| 2 | Move, jump, and shoot | PASS (simulation) | Headless tests pass for horizontal movement, jumping, and projectile combat. Interactive keyboard play was not verified. |
| 3 | Five traversable rooms | PASS (simulation) | Headless progression test talks to the Landing Bay engineer, uses the Maintenance Key, collects Echo Step, and reaches the Central Chamber through the five-room route. |
| 4 | Ability unlock changes access | PASS (simulation) | Test confirms the locked gate rejects entry, the Lab requires backtracking, Echo Step is acquired, and the gate then opens. |
| 5 | Enemies damage player and can be defeated | PASS (simulation) | Tests cover enemy damage, projectile defeat, hazard damage, death, and respawn. |
| 6 | Main menu and pause | NOT TESTED | Main menu was visually confirmed; pause/options interaction was not exercised because foreground input was detected. |
| 7 | Controller input on supported targets | NOT TESTED (complete acceptance) | User reports the PS5 controller build is playable, but the input device, complete mappings and reconnect behavior have not been separately verified. |
| 8 | Structurally valid PS5 application package | PASS (static) | Native Windows Hello World and PROJECT ECHO compile, link, convert and package. ELF64/SCE entry, segment bounds/alignment, FSELF digest, exact runtime hashes and title-directory structure validate. |
| 9 | PROJECT ECHO launch on actual PS5 hardware | PASS (user-reported) | The operator confirmed the earlier playable milestone and, on 2026-11-22, reported the build from `dist/ps5-controller` tested and working. The latest report does not specify individual controls or features tested. The original keyboard-enabled build still crashes with CE-108255-1. |
| 10 | No WSL, Docker, Linux VM, or proprietary Sony SDK required | PASS (both builds) | Native Windows LLVM 18.1.8/LLD, MSVC host utilities, CMake/Ninja, PowerShell and public SDK target files build both PS5 artifacts. |
| 11 | Platform-specific control prompts | PASS (offscreen Windows tests); NOT TESTED (new PS5 hardware UI) | SDL dummy/software-renderer tests verify controller symbols, no keyboard-binding leakage on PS5, and updated Windows gameplay/door prompts after rebinding. |
| 12 | Interactive `build.bat` platform selector | PASS | Windows selects Release plus tests; PS5 now offers SDL or Vulkan and routes each selection to its supported PowerShell build command. Cancellation returns zero. |
| 13 | DualSense Lab baseline | PASS (Windows tests, PS5 static package, operator-reported hardware rumble/LED and console report); NOT TESTED (complete controller hardware acceptance) | Offscreen tests verify the menu entry, live button/stick visualization and platform prompts. Pure tests cover normalization, dead zone, touch-coordinate bounds, bounded output duration and report escaping. The operator's 2026-11-20 screenshot shows the lab open; supplied PS5 JSON records button/stick/trigger observations, full recorded stick and trigger travel, and the expected unsupported backend features. The operator separately confirms physical vibration and LED output. Complete mappings, LED color sequence/reset, effect duration/stop behavior, reconnect and gameplay regression on this console build remain unverified. |
| 14 | PNG sprites and WAV sound effects | PASS (Windows tests and launch, PS5 static package); NOT TESTED (PS5 hardware) | Offscreen test decodes all six sprite sheets, verifies the player is drawn from PNG, the missing-directory fallback, WAV loading/resampling and rejection of non-PNG data. The Windows app logs `Assets loaded: 6/6 sprites, 8/8 sounds`. The PS5 package contains 14 assets validated by signature (PNG, PCM WAV) and size; `eboot.bin` SHA256 `e2fa2820ad80c122ab371070b6f1a2a58cfb1ab50082f9a9268bf9bbe67a997b` links with no new system modules. Loading from `/app0/assets/` and AudioOut playback have not been run on the console. |
| 15 | Echo Framework foundation and reference integration | PASS (Windows Debug/Release tests, PS5 static package); operator-reported PS5 gameplay save confirmation | Both Windows configurations pass 10/10 tests, including bounded JSON condition decoding, Local AppData storage, and the dialogue module. Native Windows PS5 Hello and game builds compile/link/package; package tests validate the artifacts. The operator confirms an earlier controller package works on PS5 and reports gameplay saving works on that integrated build. Other full-framework and gameplay acceptance remains partial. See remaining boundaries below. |
| 16 | PS5 save-storage adapter and diagnostic | PASS (Windows and one operator-reported PS5 diagnostic run, 2026-11-21) | Initial console probes identified sandbox EPERM for `lstat`/`opendir` and EINVAL for 4 KiB `getdents`; the revised provider uses an `open+fstat` status fallback and bounded 64 KiB directory reads. Probe 4 reported write, replacement, read-back, `enumerate: method=getdents64k` and `PS5 save storage probe: PASS`. The gameplay autosave was subsequently confirmed by the operator; see criterion 17. |
| 17 | Gameplay autosave and Continue integration | PASS (Windows Release 8/8 tests; PS5 static package validation; operator-reported PS5 save test, 2026-11-21) | Adds validated, versioned `autosave` checkpoints, a Continue menu entry, major-progress and 30-second autosaves, and saves on return to menu/orderly shutdown. Checkpoints include room, player state, ability/completion/death progress and enemy health; invalid checkpoints are rejected before changing the active game. The controller package is `dist/ps5-controller/PPSA91007`, `eboot.bin` SHA256 `818e4d772f145da29516b26b00390c1866a18e6d18d0d37cfd6a597f36ddcfd4` (1,527,457 bytes). The operator reports that gameplay saving works on PS5; this is a single hardware confirmation. |
| 18 | Persistent volume and keyboard settings | PASS (Windows Release tests; PS5 static package validation; operator-reported settings persistence on PS5, 2026-11-21); NOT TESTED (PS5 keyboard rebinding) | A separate versioned `settings` slot stores bounded volume and keyboard bindings, loads them at startup, and saves on changes/orderly shutdown without replacing the gameplay autosave. Round-trip and malformed-value tests pass. The operator confirms settings persistence works on the PS5 build; keyboard rebinding itself is unavailable there because PS5 keyboard input is disabled. Package `dist/ps5-controller/PPSA91007` has `eboot.bin` SHA256 `f71714c13b653ff2a6fcd3aba5c1ac48fbc7bcfe5fc91754e2e51542c719032d` (1,531,745 bytes). |
| 19 | NPC dialogue, inventory, and key-gated lab access | PASS (Windows Debug tests, PS5 static package); NOT TESTED (dialogue revision 6 on PS5 hardware) | Mira's version-6 JSON dialogue presents English/Czech text and choices, grants the Maintenance Key, offers a Health Cell only to injured players without one, and conditionally offers the Kinetic Coil. A conditional advice branch appears after Echo Step is unlocked. Headless tests cover dialogue grants, equipment, active-only healing, language switching, access denial and checkpoint validation; offscreen tests cover glyph rendering. Earlier saves migrate to current slot-layout autosave format 6. |
| 20 | Bounded component schemas and host-side content/editor tools | PASS (Windows tests and CLI checks; PS5 static package); NOT TESTED (latest schema integration on PS5) | The fixed-capacity registry validates nested property metadata/configuration, resource references, component dependencies, and deterministic JSON export. PROJECT ECHO instantiates the project-specific dialogue interaction component; Mira's position now comes from her `echo:object/engineer` world object instead of a `Transform2D` entry. The opt-in project editor and schema exporter register both descriptors. |
| 21 | Bounded dialogue module integrated into PROJECT ECHO | PASS (Windows Debug/Release tests and example; PS5 static package); NOT TESTED (latest content on PS5) | The module loads bounded JSON, validates its fields and graph, filters choices with `Condition`, and dispatches registered actions. Tests cover ownership, lifecycle, malformed content, conditional choices, actions, transitions, text bounds and the game's inventory/ability-dependent branches. |
| 22 | Playable English/Czech dialogue selection and persistence | PASS (Windows Debug/Release tests and PS5 static package); NOT TESTED (latest interface/content on PS5) | Options selects EN/CS; the choice updates an active conversation and persists in the existing settings slot. Old settings without the optional field default to English; unknown values are rejected atomically. Offscreen tests verify Czech UTF-8 glyphs and Options language display. |
| 23 | Project-specific schema and configurable dialogue reach | PASS (Windows Debug/Release tests, native editor/export checks and PS5 static package); NOT TESTED (PS5 hardware) | A bounded `dialogue_interaction` component controls Mira's horizontal interaction reach. Headless/integration tests cover the exact distance boundary, invalid values, defaults and checkpoint continuity. The native project editor accepts valid and rejects out-of-range configurations; its schema exporter includes the component. |
| 24 | Unified framework capability manifest (A2) | PASS (Windows Debug tests, generic/project exporter checks, PS5 static package); NOT TESTED (PS5 hardware) | Bounded, versioned JSON combines static framework, Dialogue, PROJECT ECHO, action/capability/event/content-schema/runtime-feature and registered component metadata. Descriptor IDs are checked, output is deterministic across fragment order, and unresolved action capabilities are rejected. This does not dynamically load modules or implement the manifest-driven editor/content validator. |
| 25 | Manifest-backed component editor (A3) | PASS (Windows tests and CLI checks) | Generic and project editors list/select only manifest-declared components, display manifest-backed property metadata, reject unsupported manifest/API and component versions, and validate configurations through statically compiled registry descriptors. Out-of-range configuration and a compiled component omitted from the selected manifest are rejected. |
| 26 | PROJECT ECHO content validator (A4) | PASS (Windows Debug build and 16/16 CTest) | `echo_project_content_validate` checks the real game configuration, project capability manifest, resource dependencies, component instances, dialogue ID/graph/action parameter contracts, item catalog, and bitmap-font text bounds. Its integration test accepts checked-in content and rejects fifteen invalid cases, including unknown/missing modules and resources, unknown actions, bad component values, stale manifest API, missing component schema/item, invalid action types, and input over 16 KiB. No PS5 hardware result is implied. |
| 27 | Reusable inventory and item catalog | PASS (Windows Debug tests; PS5 static build and package tests) | `EchoFramework::Inventory` loads a bounded, versioned catalog and provides compact or stable fixed-capacity stacks with atomic grants and deterministic removal. PROJECT ECHO selects it for Mira's key/cell grants and Scrap drops; cells are usable from Active, Scrap stacks to 99, and inventory plus loot persists in checkpoints. Earlier saves remain compatible. The latest package is statically validated but has NOT been run on PS5 hardware. |
| 28 | Bounded attributes integrated into gameplay | PASS (Windows Debug 15/15; PS5 static package); NOT TESTED (PS5 hardware) | `EchoFramework::Attributes` provides bounded fixed-capacity attributes and modifiers. PROJECT ECHO applies them to max health, movement speed, jump impulse, Health Cell healing, healing-offer conditions, checkpoint validation, and HUD health display. Headless tests verify modifier bounds, health clamping, and deterministic movement changes. |
| 29 | Bounded equipment and persistent Kinetic Coil | PASS (Windows Debug tests; PS5 static package and package tests); NOT TESTED (PS5 hardware) | `EchoFramework::Equipment` requires Attributes, uses fixed-capacity slot loadouts, and applies bounded modifiers. Mira offers the Kinetic Coil after the Maintenance Key; it auto-equips into empty Body, increases movement speed and appears in the HUD. Moving it to the backpack removes its modifier; restore rebuilds gear from equipped slots, with explicit legacy inventory migration. |
| 30 | Reusable abilities and Echo Step ownership | PASS (Windows Debug 16/16; PS5 static build/package tests); NOT TESTED (PS5 hardware) | `EchoFramework::Abilities` validates compiled stable ability IDs and tracks unlocked IDs in fixed-capacity sets. PROJECT ECHO registers Echo Step, unlocks it at the Lab pickup, and uses module-backed ownership for double jump and central-gate access. Restore reconstructs the set from existing checkpoint fields without changing the save format. |
| 31 | Slot-based backpack, equipment and active item | PASS (Windows Debug 16/16, native PS5 static build/package tests); NOT TESTED (interactive application input or PS5 hardware) | 20 stable backpack slots, Head/Body/Weapon/Accessory and one separate active stack are playable through the inventory screen. Tests cover sparse slots, atomic compatible moves/swaps, three-cell merge/overflow, active-only healing, unequip speed clamping, menu navigation/rendering/prompts, strict layout validation and actual SaveCodec format 1/2/3-to-4 migrations. Successful moves autosave; simulation stays paused in inventory. |
| 32 | Enemy loot drops and collection | PASS (Windows Debug 16/16; native PS5 static build and package tests); NOT TESTED (PS5 hardware) | Defeating the Maintenance Tunnel enemy drops two Scrap and the Power Station enemy three at the defeat location. Touch pickup fills the 20-slot backpack with stacks up to 99; a full bag leaves loot in the world. Checkpoints retain active drop location/state and collected inventory, preventing loss/duplication across Continue. Offscreen tests verify loot rendering; game tests cover defeat, collection, full-bag handling and checkpoint restore. |
| 33 | Three enemies and ten functional loot items | PASS (Windows Debug 16/16; native PS5 controller build and static package tests); NOT TESTED (interactive use or PS5 hardware) | Charger pursues, Armored periodically guards, and Sentry fires. Their deterministic drops include four equipment items and six consumables. Tests cover effects, enemy behavior, loot, full bags and format 1–4 save migration to format 5; the package validator rejects a missing new item. |
| 34 | Branched quest choices and idempotent rewards | PASS (Windows Debug 18/18; native PS5 controller build and package tests); NOT TESTED (interactive application or PS5 hardware) | Version-2 quest JSON adds Coil and Sentry routes with branch-specific facts/rewards. Autosave format 6 persists route selection and claim state, explicitly migrating formats 1–5. Headless and offscreen tests cover both routes, visible selection, full-inventory pending rewards, one-time claim and checkpoint migration. PS5 evidence is static only. |
| 35 | Localized linear cutscene module | PASS (Windows Debug 18/18; native PS5 controller build and static package tests); NOT TESTED (interactive application or PS5 hardware) | A bounded reusable module loads localized scene cards and advances deterministically. Entering Central Chamber starts a three-card scene that pauses simulation until interaction advances it. Headless, offscreen renderer, content-validator and package tests pass. No cutscene reward, branching scene, or hardware run is claimed. |
| 36 | Data-driven world: objects, animations and multi-tileset rooms | PASS (Windows Debug 19/19; native PS5 controller build and static package tests); NOT TESTED (interactive application or PS5 hardware) | `echo.world` loads 20 object and 6 room JSON files with `parent` inheritance (per-field animation merge, tile legend by symbol, tilesets by ID). Each object names its sprite sheet, frame size, offset, bounds, spawn, facing and per-animation frames/fps; this includes the engineer, whose Landing Bay position and facing come from `engineer.json` (`game.json` keeps only her interaction reach and rejects a duplicate transform). Rooms use multiple tilesets with tile position, rotation and flips. All five rooms, enemies, door, pickup and props are built from `assets/world`; tests prove the shipped data equals the previous built-in layout and that invalid layouts are rejected. New original sheets cover five enemies, the engineer, projectiles, loot, props and tiles. Offscreen renders were checked; PS5 load/render behavior is untested. The PS5 link exposed a throwing `string_view::substr` path, replaced with non-throwing slices. |
| 37 | Diablo 2-style isometric room and display modes | PASS (Windows Debug 19/19 including offscreen render tests; native PS5 controller build and static package tests; user-reported PS5 diagnostic fallback and isometric room rendering); OPEN (PS5 fullscreen); NOT TESTED (remaining interactive acceptance) | Room JSON `projection: isometric` selects 8-way top-down movement without gravity, aim-directed shots, walkable damaging pools and stalkers that chase and respawn on entry. The new Cargo Hold (24x12) connects to the Landing Bay's west side and uses new original isometric floor/block, player (5 directions, mirrored) and stalker sheets. The renderer draws 64x32 diamonds with depth sorting, a two-axis camera and translucent occluders. Windows Options cycles 1280x720, 1920x1080 and desktop fullscreen, applying immediately and persisting an optional `video.display` setting (missing defaults to 1280x720; invalid values are rejected atomically). PS5 has no display option. The display-sized (1920x1080) software renderer reports `Out of memory` at diagnostic 06e; fallback to the centred 1280x720 window succeeds on hardware (user report, 2026-11-22). The log confirms rendering room 5 (`iso=1`) and returning to room 0. Fullscreen remains unresolved; the exact allocation failure has not been traced. |
| 38 | Deterministic in-game damage camera shake | PASS (Windows Debug 19/19, including headless and offscreen SDL/canvas tests); NOT TESTED (PS5 package or hardware) | Accepted damage triggers a visible 18-step two-axis shake (up to 8 by 5 logical pixels); blocked hits do not. Headless tests cover expiry, retriggering and checkpoint reset. Shared world rendering moves while the HUD stays fixed, and SDL output matches the recorded canvas. |
| 38 | Isometric twin-stick aiming | PASS (Windows Debug 19/19 including deterministic aim tests; native PS5 controller build and static package tests; Windows app smoke launch); NOT TESTED (interactive mouse aim or PS5 right stick/R2 on hardware) | `echo::Input` carries a screen-space aim vector; isometric rooms inverse-project it onto the grid so shots ignore the movement direction (walking west while aiming east fires east). Zero or non-finite aim falls back to movement. Windows converts the mouse position to logical coordinates relative to the centred player; the right stick (deadzone 9000) overrides it, and left mouse/R2 fire. While mouse-aiming on Windows the cursor is confined to the window and hidden, and the crosshair follows it; pause, the map, menus or losing focus release it. Controller aim shows the crosshair beside the player. |

| 39 | Experimental PS5 RADV device | PASS (operator-reported instance and device diagnostics, 2026-11-22) | Separate `PPSA91008` logs report successful instance and logical-device creation, one physical device, two queue families, a graphics queue and orderly device/instance destruction. The driver reports Vulkan 1.4, one display and a 1920x1080 59.94-Hz mode; those are reported capabilities, not displayed frames or conformance evidence. The logs do not identify installed FSELF digests or report manual close. Normal CRT exit remains unverified. See [experiment results](experiments/vulkan3d/RESULTS.md). |
| 40 | Experimental PS5 RADV first frame | PASS (operator-reported visible cyan clear and submit/fence/present returns, 2026-11-22) | The `PPSA91008` first-frame log reports successful swapchain creation, acquire, GPU submit, fence wait and present, followed by four heartbeats. The operator confirms a turquoise screen. The local FSELF SHA-256 is `ee8f57dc72e0759adbc3e0ad04dc4858a78708358ef6ecae0a25c5591a198fa7`; the installed digest and manual-close outcome were not reported. Normal shutdown and the full Vulkan renderer remain unverified. See [manual test](experiments/vulkan3d/HARDWARE_TESTS.md). |
| 41 | Experimental PS5 RADV cube | PASS (static package and Windows GPU image tests); NOT TESTED (this static variant on PS5) | A separate `PPSA91008` probe draws a static perspective cube with vertex/fragment shaders. Windows GPU readback verifies three colored faces and their positions. FSELF SHA-256 `e97f2d735159927c979d1aa5d05861b7528861bd178f3d487bdda766cf47a6d1`. A different interactive variant was later tested on PS5; see [manual test](experiments/vulkan3d/HARDWARE_TESTS.md). |
| 42 | Experimental PS5 joystick-controlled cube | PASS (operator-reported moving cube and PS5 log, 2026-11-22) | The log reports graphics-pipeline creation, submit/fence/present, connected pad, motion and a redraw heartbeat; the operator confirms the cube is visible and moves. The local FSELF SHA-256 is `69589268aca0352da3c9299facee87e9f56db61062202029335e587ffabb1ca7`, but the installed digest and close outcome were not supplied. Full controller behavior, normal shutdown and the game renderer remain unverified. See [manual test](experiments/vulkan3d/HARDWARE_TESTS.md). |
| 43 | Experimental Vulkan game menu | PASS (Windows GPU readback and SDL image equivalence); NOT TESTED (PS5) | A separate Windows Vulkan diagnostic draws the actual game's main menu and bitmap glyphs in a single dynamic-mesh draw. SDL tests compare recorded keyboard and PlayStation menus to the existing renderer and check failure propagation. The diagnostic remains separate; the newer PS5 game candidate is tracked below. |
| 44 | Experimental Vulkan gameplay with JSON world sprites | PASS (Windows Vulkan GPU readback and content load); NOT TESTED (interactive acceptance or PS5) | The diagnostic loads the real game configuration and JSON world, uploads its referenced sheets and records the starting room through Vulkan. GPU capture differs from fallback rendering; SDL tests verify data-driven tile/object atlas submissions and reject unsafe mappings. It remains a diagnostic rather than a complete interactive game. |
| 45 | Portable 2D recording boundary and dynamic meshes | PASS (Windows Debug 19/19; Vulkan GPU readback); NOT TESTED (this revision on PS5 hardware) | SDL-free `ICanvas2D` defines ordered clears, fills and textured quads; Vulkan batches adjacent materials with atlas UVs, tint and logical-coordinate rotation. `CanvasWorldSprites` maps validated asset-relative world sheets to backend handles. Dynamic mesh creation and updates are available through `IRenderBackend`. SDL stays the default; the separate PS5 candidate selects Vulkan at build time, not runtime. Production particle integration was added later (criterion 47); shadows, bloom and postprocessing remain unsupported. |
| 46 | PS5 Vulkan game candidate | PASS (native compile/link and package static validation; Windows regression/GPU tests; operator-reported functionality on PS5) | The production game loop records gameplay, menus, options, inventory, DualSense Lab and VFX to the Vulkan canvas. The candidate uses RADV `VK_KHR_display`, SDL controller/audio without SDL video and the existing saves. On 2026-11-22 the operator reported earlier SDL and Vulkan packages fully functional. After the glyph revision was presented, the operator again reported full functionality without specifying the variant(s), installed digests, setup, or individual checks. Current local package hashes are recorded above. Do not install both title-ID variants simultaneously. See [manual test](experiments/vulkan3d/HARDWARE_TESTS.md#project-echo-vulkan-game). |
| 47 | Fixed-step production shot particles | PASS (Windows Debug 19/19, static PS5 controller and Vulkan package checks, and operator-reported functionality of both earlier variants on PS5) | The shared bounded CPU particle system emits deterministic muzzle sparks from observed projectile births and advances only with `echo::kFixedStep`. Both SDL and Vulkan game renderers draw the same particles through the canvas-compatible fill path. The earlier local SDL and Vulkan FSELF digests were `694394deb9e28c75608e16f757024e25d499f1fe13932f3c92f6f188667de987` and `2177bff9e8f87d44a8ff72fb97be260ff26d72ec8548404cd367ca06e97e0dff`; installed digests and console setup were not reported. Later revisions are tracked separately. |
| 48 | Render debug overlay | PASS (Windows Debug 19/19; Windows Vulkan 17/17; both PS5 variants compiled and passed static package checks; operator-reported FPS observations on PS5) | Options persists the debug toggle with backward-compatible default OFF. The gameplay overlay shows renderer, sampled FPS/frame interval and Vulkan's previous-frame draw and triangle counts. The operator reported 30 FPS in the SDL isometric scene and 60 FPS throughout the Vulkan game. Installed digests and exact setup were not reported; these observations are not an uncapped benchmark. |
| 49 | Fixed-step damage flash | PASS (Windows Debug 19/19; Windows Vulkan 17/17; both PS5 builds and static package tests; operator-reported full functionality of both packages); NOT TESTED (specific flash appearance on hardware) | Health loss or death triggers a brief red canvas overlay that fades over 12 fixed steps without changing gameplay. Tests cover fading, retriggering, reset, and SDL/canvas pixel equivalence. The operator did not separately describe the flash or provide installed digests. |
| 50 | Combat impact sparks and particle lifecycle | PASS (Windows Debug 19/19, Windows Vulkan 17/17, both PS5 builds and static package tests; operator-reported functionality of both packages on PS5); NOT TESTED (individual effect appearance and lifecycle on PS5) | Regular and isometric enemies produce bounded deterministic impact bursts on damage or defeat. Muzzle sparks originate at newly observed projectile centers. Effects clear on room transition, respawn, New Game and Continue. Tests cover real combat, transitions, reset, fixed-step behavior and SDL/canvas pixel equivalence in both projections. The operator did not separately describe individual effects or provide installed digests. |
| 51 | Vulkan offscreen color/depth render targets | PASS (Windows Debug/Release GPU readback and PS5 renderer cross-compile); NOT TESTED (PS5 package and hardware) | `IRenderBackend` supports target creation, independent resize, offscreen frame submission and destruction. The Windows GPU test verifies color output, depth ordering, memory accounting and clean resource teardown with zero validation errors. No updated PS5 game package or console run was produced. See [renderer results](experiments/vulkan3d/RESULTS.md). |
| 52 | Vulkan render-target sampling and scheduled composition probe | PASS (Windows Debug/Release GPU readback, production regression and PS5 renderer cross-compile); NOT TESTED (PS5 package and hardware) | `Material::targetTexture` samples completed targets with retained lifetimes and descriptor refresh after resize. The diagnostic schedules source, feedback-guard and sampling passes and checks both source and composed pixels. No package or console run was produced. See [renderer results](experiments/vulkan3d/RESULTS.md). |
| 53 | Defeat explosions and Cargo Hold fog | PASS (Windows Debug 19/19, including deterministic VFX and SDL/canvas render tests; operator-reported PS5 package build, upload and test; exact variant/digest not supplied) | Defeating an enemy adds a larger bounded explosion burst alongside impact sparks. The Cargo Hold starts with an evenly distributed translucent fog field and continues fixed-step emission. Room transitions and respawn reset the effects; SDL and canvas output match in the isometric render test. |
| 54 | Data-driven side-view parallax layers | PASS (Windows Debug 19/19, including world schema bounds and canvas draw-position tests; operator-reported PS5 package build, upload and test; exact variant/digest not supplied) | Tile layers accept validated `scrollFactorX` metadata from 0 to 1; omitted values keep prior camera tracking. The Landing Bay's original background decorations now scroll at 0.35 while terrain and object props retain foreground movement. Isometric rooms reject parallax factors to preserve depth sorting. |
| 55 | Reusable 2D lighting and power circuits | PARTIAL (Windows Debug and Release Vulkan GPU/readback 35/35 each; production Windows 20/20; PS5 Vulkan candidate static/package tests PASS; hardware NOT TESTED) | `game.json` defines world ambient/directional lighting and a smooth day/night cycle; rooms can override those values and define bounded static point/spot lights. Object/tileset JSON binds linear normal maps, emissive masks and power-circuit-driven emission; the Cargo Hold console uses the emergency circuit, and its player-attached spotlight follows aim. Persistent/room light-enable overrides use framework state and save integration; room lights can smoothly fade when enabled state or power circuits change. Stable prop IDs now expose bounded runtime transforms used by prop draws, panel interactions and prop-attached lights; transforms reset on new game and are not saved in `GameCheckpoint`. Point attenuation and spotlight cones use projected world-space distances/directions, including isometric projection; GPU readbacks verify projected light geometry, side/top-down/isometric transforms, camera invariance, blackout, UI isolation, directional normals and emissive masks. The Cargo Hold GPU integration scenario round-trips the game checkpoint, main/emergency circuits and persistent light override through versioned SaveCodec bytes, then verifies overhead dimming and the emergency emissive display by pixel readback. A Landing Bay GPU scenario verifies the circuit-controlled lamp outage, independent emissive backup indicator, and projectile-light movement and expiry. A bounded GPU shadow path uses the 32 nearest visible solid-tile, prop, player, and living-enemy edges; Windows readback verifies light blocking and UI isolation. Generic entity-component attachment, static-light accumulation caching, projectile/particle occlusion and HDR bloom remain incomplete; no PS5 hardware PASS is claimed. See [world lighting notes](framework/WORLD.md#lighting-data). |
| 56 | Versioned JSON attribute and equipment definitions | PASS (Windows 22/22; PS5 SDL controller and Vulkan static/package tests); NOT TESTED (PS5 hardware) | `game.json` provides bounded attributes and the equipment module's versioned definitions; the generic Attributes module owns validated JSON definitions, and the duplicate attribute table was removed from `game.cpp`. The content validator rejects invalid attribute bounds. Controller and Vulkan package negative tests pass; both packages keep `PPSA91007`. This is a partial content migration, not a fully data-driven engine. |
| 57 | Versioned JSON ability definitions | PASS (Windows 22/22; PS5 SDL controller and Vulkan static/package tests); NOT TESTED (PS5 hardware) | The Echo Step ability ID is now declared in `game.json`; the bounded Abilities module validates the version, ID shape, uniqueness, and capacity, then owns its data. Runtime startup and the host validator load the same definitions; malformed content is covered by focused tests. Both variants retain `PPSA91007`. |
| 58 | Versioned JSON item-use effects and descriptions | PASS (Windows 22/22; PS5 SDL controller and Vulkan static/package rejection tests); NOT TESTED (this revision on PS5 hardware) | The version-2 item catalog supplies bounded descriptions and parameters for healing, shielding, speed/jump boosts and area damage. Compiled effect handlers remain allowlisted; catalog data cannot load code. Autosave format 8 persists boost magnitudes and migrates formats 1-7 with legacy values. Package tests reject unknown effect IDs. |
| 59 | Data-driven movement physics and player combat tuning | PASS (Windows 22/22; PS5 SDL controller and Vulkan static/package tests); NOT TESTED (this revision on PS5 hardware) | Ten tunables now load through the versioned Attributes block, with bounded values and preserved defaults. Focused headless tests verify configurable acceleration, projectile speed, and fire interval. |

## Echo Framework verification

Latest statically validated controller package: `dist/ps5-controller/PPSA91007`,
`eboot.bin` **1,938,881 bytes**, SHA256
`8b16fb242f786d516ce075399f0625c6ec2965a34c845a6f4ba9c61b8b81062e`; static
report: PASS, hardware NOT TESTED. The matching Vulkan candidate is
`dist/ps5-vulkan/PPSA91007`, `eboot.bin` **28,213,453 bytes**, SHA256
`498a7d1a9dcb17a5da935663d4b83791533310c4365d16a066053ce51e54ff66`; static
report: PASS, hardware NOT TESTED. Both retain title ID `PPSA91007`; controller
imports remain free of keyboard/IME modules. Packaged `assets/framework/game.json`
is 9,841 bytes, SHA256
`2a6a3d349433dcb0e328e01756433f6819e95ec9e2c95561a7936bd9a42b6c86`;
`items.json` is 2,376 bytes, SHA256
`ed42cdffc9be8a052df882386848364906ef29ae36e51c5761576927caa4d4a7`. The
controller package contains 71 assets and the Vulkan package 73. The seven
controller system imports, Vulkan system imports, and bundled clean-room libc
hash are recorded in the corresponding
`build/ps5/logs/package-tests/*/PPSA91007-validation.json` reports. yyjson
0.13.0 is linked statically, with its MIT notice packaged.

Evidence: `build/windows/.build-logs/` (latest Debug build/test and tool build), exported manifests under `build/windows/`, `build/ps5/logs/controller-release/commands.log`, `game-build.stdout.log`, `game-readelf.stdout.log`, and `PPSA91007-validation.json`; compiler commands and retained response files are under `build/ps5/game-controller/`. `tests/ps5_package_tests.ps1 -Controller` exercises the controller package, including negative quest-route validation and the absence of keyboard/IME imports.

Hardware follow-up (user report, 2026-11-22): after the shared PNG decoder build, the operator reports the PS5 game is running. The local controller package at the time had an `eboot.bin` of **1,808,193 bytes**, SHA256 `117a3ba5ffe3daeb5fa345bdd66b691ab9426f1b7fe6e4ff8ff62c1fe186a962`; the installed digest and individual features tested were not reported. This is the SDL game, not a PS5 Vulkan test.

Hardware follow-up (user report, 2026-11-22): the no-keyboard diagnostic `PPSA91007` package, `eboot.bin` **1,805,441 bytes**, SHA256 `851fdeb9a6bb84c8cb62e853148383b8d5f696ea10da57ae5a22a19976c5dd8c`, is reported working. Its log confirms the 1920x1080 renderer fails with `Out of memory`, the 1280x720 retry succeeds, 20 world sheets and 8 sounds are loaded, New Game renders, the isometric room is entered/rendered/exited, pause renders, Continue returns success, and rooms 1, 2, 3 and 4 render. This is hardware evidence for diagnostic startup, world loading/rendering and Continue, not proof of audible audio, right-stick aiming, every gameplay feature or fullscreen. The later game launch confirmation above does not include an installed digest.

The existing fixed-step game loop and gameplay objects remain authoritative. Framework integration handles configuration/module startup, game-state reset/resume, Mira's JSON dialogue, catalog-backed key and Health Cell actions, bounded attributes, the persistent Kinetic Coil equipment grant, and the Echo Step action/condition/event projection. The abilities module now owns the compiled Echo Step unlock state consumed by the fixed-step simulation. Dialogue conditions read projections of authoritative health, inventory, equipment and ability state. The SDL app wires both platform storage providers into the versioned gameplay checkpoint flow. The provider diagnostic passed on PS5, and the operator reports the gameplay autosave works on an earlier console build (2026-11-21).

The component schema API, unified deterministic capability-manifest exporter, resource-manifest validator, and manifest-backed CLI editor are verified on Windows. The generic editor retains only the built-in Transform2D; opt-in PROJECT ECHO editor/export executables additionally register the project's dialogue interaction descriptor. The current capability-manifest/editor milestones are host-side metadata tooling; no PS5 hardware result is claimed for this revision.

`EchoFramework::Narrative` is an optional reusable static library with a headless example, focused tests, and a bounded loader for one dialogue JSON document at a time. PROJECT ECHO requires and links it for Mira's interaction. The game supports optional Czech speaker, line and choice fields with English fallback, a bounded UTF-8 bitmap glyph set, and persisted EN/CS selection.

The foundation is usable but broader framework acceptance remains incomplete: a reusable platform-neutral application coordinator, finer compiled module selection, hardware verification of keyboard rebinding, and future migration of additional gameplay objects remain outstanding. Save codec tests use in-memory storage, and the Windows provider test uses disposable Local AppData storage. See [framework roadmap](framework/ROADMAP.md).

The opt-in PS5 storage diagnostic is built with `scripts\build-ps5.ps1 -Target Game -StartupDiagnostics -SkipPs5Keyboard -SaveStorageDiagnostics`. Its distinct package is `dist/ps5-save-diagnostic/PPSA91007`. The first diagnostic (`bdb48597...`) failed parent-directory initialization in the operator's 2026-11-21 log. The second (`1f96b6b5...`) reported `lstat_errno=1` with a working `open+fstat` fallback, passed write, replacement and read-back, and failed enumeration with `opendir failed errno=1`. The third (`596eb13c...`) replaced `opendir` with a 4 KiB `getdents`, which failed with `errno=22`. The fourth diagnostic tries ordered 64 KiB raw-directory variants (no-follow `getdents`, `getdirentries`, `sceKernelGetdents`, `sceKernelGetdirentries`, then plain-open `getdents`/`sceKernelGetdents` with an `fstat` directory check), and logs the chosen method or every attempt's errno. Its `eboot.bin` SHA256 is `865de685076ce98ec1b1af5f3763733d50386de612ebcf92ed8c4f3546e9c044` (1,527,841 bytes). Static package validation, PS5 package tests and Windows Release 8/8 tests passed; on 2026-11-21 the operator reported the fourth package's console run as `PS5 save storage probe: PASS` with `enumerate: method=getdents64k`. Build logs, compiler/linker commands, ELF inspection, and package validation are under `build/ps5/logs/save-storage-diagnostic/`. After success the probe leaves the reserved `framework_probe.sav` under `/data/project_echo/saves`; its expected result is `PS5 save storage probe: PASS` in `/data/project_echo/logs/project_echo.log`.

Native cross-build verification: 2026-11-20. The original portability investigation used an isolated Release Windows regression build. The newer platform selector builds the normal `build/windows/project_echo.exe`; gameplay and offscreen control-prompt tests both pass. PS5 upload and launch remain manual. Hello and milestone controller-build hardware success are operator reports, not execution performed by the build host.

DualSense Lab verification: Windows Debug and Release tests pass for gameplay, renderer/input prompts and diagnostics serialization; a test-generated report also parses with PowerShell `ConvertFrom-Json`. The controller-only PS5 app compiled and passed ELF/FSELF/package validation using the existing native Windows toolchain.

PS5 hardware run, 2026-11-20 (operator report with console screenshot and exported JSON): the lab opened; buttons, sticks and analog triggers were observed (`SUPPORTED` in the report); touch contacts, gyroscope, accelerometer, trigger rumble and adaptive triggers report `UNSUPPORTED`, matching the backend audit. The operator confirmed that **vibration and lightbar color changes physically worked**. That report still shows rumble/LED as `NOT TESTED` because the L1 confirmations were rejected: the build accepted confirmation only when the output test was the immediately preceding action and did not show the result. The lab now remembers the last successful output test until confirmed, shows `CHECK L1 OK / R1 FAIL`, then `CONFIRMED`/`FAILED` on screen. This fixed build (`eboot.bin` SHA256 `7db1e86ebd23be39577aeea685e4cf797329a386ea580f5f0d4f149c2b67b944`) passed static validation only; it has not yet been run on the console. On PS5 the controller name shown is the signed-in user's account name returned by the SDL backend, not a device name; the exported report therefore contains that name. Build with `scripts\build-ps5.ps1 -Target Game -SkipPs5Keyboard`, then follow [the DualSense hardware checklist](DUALSENSE_HARDWARE_TESTS.md).

- Historical keyboard-enabled PROJECT ECHO package (do not deploy): `dist/ps5/PPSA91007`, `eboot.bin` SHA256 `a62fcdd17e127686e8aaed19d6f40bf3da1c0c44ebba475a6e8c6cd228c746ac`.
- Hello World package: `dist/ps5-hello/PPSA99999`, `eboot.bin` SHA256 `205372ec8df455f3397709001ee50285750f41ebd8dbed7d50942908bd6885d6`.
- Clean-room `libc.prx`: SHA256 `e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036`, 1,284,674 bytes; independently reproduced twice.
- Evidence: `build/ps5/logs/*-validation.json`, compiler/linker logs, `game/compile_commands.json` and retained Ninja response files.

The startup diagnostic reached the call to `sceKeyboardInit` on hardware. A separate experimental build (`-Target Game -StartupDiagnostics -SkipPs5Keyboard`, output `dist/ps5-diagnostic-no-keyboard/PPSA91007`) omits PS5 hardware keyboard and IME calls, retaining the controller path. The operator reports this build launches and is playable, but it is not a permanent fix or full acceptance test. Initialization errors are reported explicitly and the diagnostic process stays open for collection. See [PS5 compatibility](PS5_COMPATIBILITY.md#hardware-crash-investigation-kraken-mount-031).

At the operator's request, `-Target Game -SkipPs5Keyboard` produces the non-diagnostic controller build in `dist/ps5-controller/PPSA91007`, without physical PS5 keyboard or IME support. The hardware-tested `playable` milestone's `eboot.bin` SHA256 is `eff47dcc2a39b3e7b92e226d9851fafdb6078055763c6d50221e7af8072d70bd`. The operator confirmed on 2026-11-20 that this milestone is playable and supplied a gameplay screenshot; its local package was retained in `dist/ps5-controller-playable/PPSA91007` before rebuilding the newer UI. The new controller-symbol prompts, touchpad map and circle/back actions are not yet hardware-tested. The original PS5 package and user-tested diagnostic are built separately; generated packages are not tracked in Git. Full controller mappings/reconnect, audio playback, five-room progression and exit/relaunch remain unverified; the exact underlying keyboard-path fault remains undiagnosed.

The controller-symbol update built through `build.bat` option P and passed ELF/FSELF/package validation on Windows. Its `eboot.bin` is 1,152,065 bytes, SHA256 `7ddf8c7afcac1395d59b1c61b41ce67a5a876750b86365487285971037ff6279`; keyboard/IME imports remain absent. This new executable still requires a console test.
