# Native Windows PS5 feasibility

The experimental Vulkan game compiles, links, signs and passes static
package validation on native Windows. The operator reported earlier
Vulkan and SDL impact-sparks packages fully functional on PS5 (2026-11-22).
After the subsequent menu glyph and WebP packages were presented, the operator
reported full functionality on 2026-11-22 without identifying the tested
variant(s), installed digest, setup, or individual checks.
Individual effect appearance and lifecycle were not separately described.
A separate RADV first-frame diagnostic also passes static validation.
Runtime hold checks and RADV instance/device/queue creation were reported
successful on hardware. A first-frame log reports successful submit, fence
wait and present calls, and the operator confirms a visible turquoise screen.
The operator also reports a visible moving interactive cube, with successful
pipeline, pad and loop stages in the log. Those diagnostics do not establish
game rendering, textures, input, audio, saves or normal shutdown. The operator
subsequently confirmed that the previous full PROJECT ECHO Vulkan game build
was fully playable on PS5, including controls, audio, saving and clean shutdown
(2026-11-22), and later reported both SDL and Vulkan particle-effect builds
fully functional. The installed digests and console setup were not provided.

## PS5 Vulkan game candidate

With the previously prepared pinned RADV archive, runtime, SDK and Windows
Vulkan SDK available (the manual host-tool prerequisites below still apply):

```powershell
.\scripts\build-ps5.ps1 -Target Game -SkipPs5Keyboard -Vulkan -RestoreDependencies
.\tests\ps5_package_tests.ps1 -Vulkan
```

The game build validates standalone Hello first, checks the RADV source pins
and link inputs, compiles/validates Vulkan 1.0 SPIR-V, and stages the complete
native application at `dist\ps5-vulkan\PPSA91007`. This is a separate build
from the default SDL controller package at `dist\ps5-controller\PPSA91007`.
Both intentionally use the same stable identity: **never leave both copies in
the loader scan root**. Keep the existing package and saves backed up before
manually replacing the project's own installed title. No upload is automated.
See [HARDWARE_TESTS.md](HARDWARE_TESTS.md#project-echo-vulkan-game) for the
manual checklist and log location.

The candidate uses RADV through `VK_KHR_display`, with SDL controller and
audio but no SDL video or PS5 keyboard/IME initialization. It records the
existing game and screens as a 640x360 ordered 2D canvas; the existing SDL
renderer remains the default in the other build. The separate Windows 3D/VFX
experiment is separate from the production game. Production shot particles
use the bounded CPU system and are recorded as canvas fills in both SDL and
Vulkan builds. The current revision also adds enemy impact sparks and clears
particles on room transition, respawn, New Game and Continue.
The menu additionally loads the bounded GLB/glTF glyph and WebP textures
described in [MATERIALS.md](MATERIALS.md); the SDL package uses the PNG fallback.

The current candidate FSELF is 28,114,973 bytes, SHA-256
`16e0b58d262f51bae1286ba8fae077dabaf57fa76383d6fe077f5bac879c5a4a`.
The updated artifact passes native Windows static validation and
`tests\ps5_package_tests.ps1 -Vulkan`. The subsequent user report of full
functionality followed this build. The installed digest and console setup were
not supplied, so the local hash is not proof of which binary ran. The prior
60 FPS observation predates the damage-flash change.
The validation report is `build\ps5\logs\vulkan-game\PPSA91007-validation.json`;
compiler/linker output and commands are in that log directory and
`build\ps5\game-vulkan`. The package records source pins, archive and shader
hashes in `licenses\build-provenance.json`. Its nine system imports include
AGC/AGCDriver, VideoOut, Pad, UserService and AudioOut; the AGC link stubs are
not packaged. GPL and mixed Mesa license obligations still apply to redistribution.

## Build the runtime preflight

Restore the production native toolchain first if it is not already available:

```powershell
.\scripts\build-ps5.ps1 -Target Hello -RestoreDependencies
.\scripts\doctor.ps1 -Target PS5
.\scripts\build-vulkan-ps5.ps1 -Target Dependencies -RestoreDependencies
.\scripts\build-vulkan-ps5.ps1 -Target Runtime
```

`Dependencies` builds the pinned platform and LLVM 18.1.8 compiler-rt builtins
using Windows Clang/LLD/CMake/Ninja. It retains the SDK v0.42 libc++/libc++abi/
libunwind binaries, just as the pinned upstream platform bootstrap does. All
members of all five runtime archives are inspected for ELF64, little-endian,
x86-64 relocatable format. The weak ELF `__emutls_get_address` definition is
required; Windows COFF builtins are never substituted.

The fork's tracked FreeBSD header symlinks are materialized from their pinned
Git targets without administrator privileges. Their resolution rejects cycles
and escapes from the source include tree. Neither original SDK nor upstream
source checkouts are modified. Target assembly also explicitly selects PS5.

`Runtime` validates standalone Hello first, links the full C++ runtime with
unwind boundaries, then uses the existing native converter and FSELF signer.
Static C++ ABI definitions are localized rather than published as unsupported
application exports. `__assert` binds to the real platform `ps5___assert`, never
a dummy assertion. Linker scripts/maps are tracked as relink dependencies.

Outputs:

```text
build/vulkan-ps5/runtime-probe/echo_vulkan_runtime.llvm.elf
build/vulkan-ps5/runtime-probe/eboot.elf
dist/vulkan-experiment/PPSA91008/
  eboot.bin
  sce_sys/param.json
  sce_sys/icon0.png
  sce_module/libc.prx
  licenses/
```

The generic static validator verifies title/content identity, imports, 16 KiB
segments, runtime companion and FSELF integrity. The only system modules are
`libkernel.prx` and `libSceLibcInternal.prx`; no SDL/keyboard/IME/GPU module is
imported. Native commands and compiler/linker output are retained under
`build/vulkan-ps5/logs`; `PPSA91008-validation.json` is the static report.
`native-build-history.log` retains actual compiler/linker output across rebuilds,
while individual stage stdout/stderr files describe their most recent invocation.
`licenses/build-provenance.json` retains runtime archive hashes and source pins.
`build/vulkan-ps5/compatibility.json` describes the most recent script mode and
can be replaced by a subsequent probe; it is not the persistent package report.

Copy only the **`PPSA91008` application directory itself** into Kraken Mount's
configured scan root (normally `/data/homebrew`), so the console path is
`/data/homebrew/PPSA91008`. Kraken Mount also scans one directory below a scan
root, but does not recursively search arbitrary nesting. Do not copy the
enclosing `dist/vulkan-experiment` directory: that makes the title too deep to
discover. Confirm `PPSA91008` is unused before manual copying. This is a native application, not a `payload_main` binary
or proof of compatibility with an arbitrary ELF sender. The probe has no
visible frame; a short black screen/exit is not success evidence. Retrieve
`/data/echo-vulkan-lab/startup.log` and follow
[HARDWARE_TESTS.md](HARDWARE_TESTS.md). Each launch replaces this log.
The title metadata is emitted as UTF-8 **without a BOM**: Kraken Mount parses
`sce_sys/param.json` directly as JSON bytes, and PowerShell 5.1's
`Set-Content -Encoding UTF8` adds a BOM that causes its scanner to reject the
folder silently from the library view.

The probe touches/frees 8 MiB of direct heap, tests C++ allocation/library locale,
two default direct-memory pthread stacks, independent emulated TLS and stack
release accounting. It rejects any recorded private-libc allocator fallback.
Cached stacks may remain intentionally; a virtual heap reservation is not a
physical-memory measurement. Even a successful runtime log will not establish
Vulkan support or explain the original 1080p software-renderer failure.

For comparison with the observed post-callback exit crash, build the separately
staged `-Target Runtime -RuntimeHold` variant. It remains in-process after
successful checks, records a heartbeat every five seconds and must be closed
manually. This follows the pinned boilerplate demo's approach but does not fix
or validate normal CRT exit. See [HARDWARE_TESTS.md](HARDWARE_TESTS.md).

## Missing-driver diagnostic

```powershell
.\scripts\build-vulkan-ps5.ps1 -Probe
```

This reproducible minimal diagnostic uses the existing Windows LLVM 18.1.8,
the production public SDK headers and installed Vulkan headers. It compiles
`link_probe.c` for `x86_64-sie-ps5`, inspects its ELF64/x86_64 relocatable object,
then deliberately attempts a PIE link without any Vulkan implementation.
The expected failure is `undefined symbol: vkCreateInstance`. This establishes
native compilation of the public API and the absent driver link boundary, not
compatibility of a RADV archive. `build/vulkan-ps5/compatibility.json` records
compiler/linker arguments, missing dependencies and separate static/hardware
status. An unexpected successful link is an error.

The older `build-vulkan-ps5.ps1` default `Vulkan` feasibility mode still fails
explicitly and does not produce the game; use the `build-ps5.ps1 -Vulkan` game
command above instead.
`Runtime` is the separately named preflight mode described above.
No mode connects to a console, uploads or executes a PS5 binary on Windows.

## RADV instance diagnostic

With the previously built pinned Mesa host tools, target cross file, runtime
archives, Hello converter and runtime-hold package already present:

```powershell
.\scripts\build-vulkan-radv-archive.ps1
.\scripts\probe-vulkan-radv-link.ps1 -Native -Package
.\scripts\probe-vulkan-radv-link.ps1 -Native -Display -Package
.\scripts\probe-vulkan-radv-link.ps1 -Native -Frame -Package
.\scripts\probe-vulkan-radv-link.ps1 -Native -Cube -Package
.\scripts\probe-vulkan-radv-link.ps1 -Native -Interactive -Package
.\tests\vulkan_ps5_radv_tests.ps1
.\tests\vulkan_ps5_radv_tests.ps1 -Cube
.\tests\vulkan_ps5_radv_tests.ps1 -Interactive
```

These commands rebuild the target archive in a separate directory and create
`dist/vulkan-experiment/ps5-radv-instance/PPSA91008`. They require the clean
pinned PS5_Vulkan source at `.deps/vulkan-ps5/vulkan-source`; host-tool
restoration and the cross file are still manual prerequisites, so this is not
yet a clean-machine build. Compiler/linker commands and archive inspections are
in `build/vulkan-ps5/logs/radv-visibility-build.log` and
`build/vulkan-ps5/logs/radv-instance-link.log`. The static package report is
`build/vulkan-ps5/logs/radv-instance/PPSA91008-validation.json`.

The diagnostic obtains `vkCreateInstance` through the RADV ICD, creates an
instance, enumerates the physical-device count, destroys the instance and holds
with five-second heartbeats. It opens no SDL window and renders no frame.
It imports `libSceAgc`, `libSceAgcDriver`, `libkernel`,
`libSceLibcInternal` and `libSceVideoOut` as system modules; AGC `.so` files
are host link-only stubs and must never be copied to the console. No runtime
availability is inferred from those imports. See
[HARDWARE_TESTS.md](HARDWARE_TESTS.md) for the manual test and log location.

The separately staged `dist/vulkan-experiment/ps5-radv-display/PPSA91008`
variant checks display/swapchain extensions, retrieves the physical-device
properties and graphics queue, enumerates displays and modes, creates a logical
device and gets its queue, then destroys the device and instance. It reports the
smallest available mode at least 1280x720, preferring a refresh nearest 60 Hz
for equal dimensions. It creates no surface or swapchain and renders no frame.
Its log is `/data/echo-vulkan-lab/radv-display.log`; its static report is
`build/vulkan-ps5/logs/radv-display/PPSA91008-validation.json`. Do not install
two variants of the shared `PPSA91008` identity at once.

The independently staged first-frame variant is
`dist/vulkan-experiment/ps5-radv-frame/PPSA91008`. It checks plane, surface,
swapchain and FIFO support and attempts one cyan clear and presentation without
SDL or shaders. Its log is `/data/echo-vulkan-lab/radv-frame.log`; native
compiler/linker output is `build/vulkan-ps5/logs/radv-frame-link.log` and the
static report is `build/vulkan-ps5/logs/radv-frame/PPSA91008-validation.json`.
On successful present, or an uncertain acquire/submit/present outcome, it keeps
resources alive and waits for manual close. This is diagnostic behavior, not
validated normal shutdown. See [HARDWARE_TESTS.md](HARDWARE_TESTS.md).

The `-Cube` variant independently stages
`dist/vulkan-experiment/ps5-radv-cube/PPSA91008`, retaining the confirmed
first-frame package. It requires the Windows Vulkan SDK identified by
`VULKAN_SDK`; the compiler is checked against the manifest SHA-256. GLSL is
compiled to Vulkan 1.0 SPIR-V, validated using `spirv-val`, embedded in the
binary and recorded in provenance. Its command log and static report are under
`build/vulkan-ps5/logs/radv-cube*`; the console log is
`/data/echo-vulkan-lab/radv-cube.log`. The `-Cube` test option additionally
requires a Windows Vulkan GPU and the shader header generated by this build.
The static cube uses fixed face order without a depth buffer or animation.
As with the first-frame test, resources are retained for manual close.

The `-Interactive` variant is separately staged at
`dist/vulkan-experiment/ps5-radv-cube-input/PPSA91008`. It checks the pinned
SDL source revision and uses its PS5 pad declarations, without initializing
SDL video or keyboard. It imports the system `libScePad.prx` and
`libSceUserService.prx` in addition to the five RADV modules; explicit
`-ExperimentalRadv -ExperimentalRadvPad` validation checks this exact set
without changing production or non-pad RADV acceptance. The SDL license and
header/compiler/SPIR-V hashes are included in provenance. The log is
`/data/echo-vulkan-lab/radv-cube-input.log`. The `-Interactive` test option
includes the cube tests, controller/loop mocks and two Windows GPU rotation
readbacks; build the variant before running it. These host tests alone do not
prove PS5 behavior; the separate operator-reported cube result is in
[HARDWARE_TESTS.md](HARDWARE_TESTS.md).

## Reproducible adaptation sequence

1. Acquire the exact source pins in `experiments/vulkan3d/dependencies.json`
   into an isolated cache, verify Git object IDs and retain license files.
2. Prefer a published RADV archive only when its source/SDK pins, full dependency
   set, license terms and independently measured SHA-256 can be verified.
   None was available at audit time.
3. Native host tools are still built manually; the separate target archive,
   diagnostic link and package now have PowerShell orchestration. Complete
   clean-machine host-tool restoration before calling the full build reproducible.
4. Runtime archives and native preflight linkage are now statically verified.
   Continue inventorying driver archive members and undefined symbols with
   Windows `llvm-ar`/`llvm-nm`/`llvm-readelf`; do not link COFF target objects.
5. Manually test the native preflight's heap/thread/TLS checks, then audit the
   additional libc rebinding required by actual RADV objects. Locale/C++ linkage
   alone does not establish every driver runtime requirement.
6. Test the separate native RADV `VK_KHR_display` surface diagnostic. Never
   initialize SDL video or keyboard. Enumerate actual formats, display modes,
   queues and capabilities; start at 1280x720.
7. Test the statically validated Vulkan game candidate, including its shaders,
   resource requirements and input/audio behavior, separately from the already
   reported first-frame and cube diagnostics.
8. Use a separate variant directory for diagnostics with coordinated metadata
   from `experiments/vulkan3d/ps5/identity.json`, a distinct launcher icon and
   `/data/echo-vulkan-lab` logs/save path. Confirm this identity is unused on the
   actual console before manual installation. The game candidate instead retains
   the production `PPSA91007` identity and project save/log paths.
9. Run the hardware checklist. Successful static validation alone is Stage C,
   never Stage D.

No mandatory WSL, Linux host, MSYS2, container, VM or proprietary SDK is introduced.
Missing dependencies are documented in [VULKAN_COMPATIBILITY.md](VULKAN_COMPATIBILITY.md).
