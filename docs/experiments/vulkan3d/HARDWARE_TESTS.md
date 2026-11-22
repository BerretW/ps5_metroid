# Manual PS5 acceptance

The operator reported a visible turquoise first frame and a moving interactive
cube on PS5, with successful RADV graphics and pad stages in the supplied logs.
The Vulkan game remains **NOT TESTED** on PS5.
Upstream tests and PROJECT ECHO's software isometric room are not substitutes.
Deployment is manual only.

For the `PPSA91008` diagnostics below, verify the candidate digest/static report
and confirm that identity is unused on the console. Do not overwrite
`PPSA91007`, system libraries, other titles, or running files. Keep the original
game independently installed. The separate `PPSA91007` Vulkan game procedure
below requires a backup and a deliberate replacement of only the project's own
title. Record console model, firmware, loader version, resolution and hashes.

## Runtime preflight first

Build with `scripts/build-vulkan-ps5.ps1 -Target Runtime`; manually copy only the
whole `dist/vulkan-experiment/PPSA91008` title directory, not its parent folder
and not just `eboot.bin`. Place it directly at `/data/homebrew/PPSA91008` (or
one level below a Kraken Mount scan root); Kraken Mount does not recurse through
arbitrarily deep directory trees. Inspect `licenses/build-provenance.json` and
the static validation report before copying. The diagnostic variants share
`PPSA91008`; do not install them concurrently or replace another app without
confirming ownership. The Vulkan game candidate instead uses `PPSA91007`.

The test has no video/controller interface and should exit after its checks.
Neither a black screen nor a clean exit proves success. Retrieve
`/data/echo-vulkan-lab/startup.log` manually before the next launch overwrites it.
If no file exists, record the loader/kernel error; directory/open/write failures
also report through `sceKernelDebugOutText`.

Require `result: "ok"` for `heap.direct_8mib`, `cpp.allocator`,
`cpp.library_locale`, `tls.main`, `threads.create`, `threads.join`,
`threads.direct_stacks`, `tls.isolated`, `threads.released`,
`heap.no_libc_fallback` and finally `runtime.complete`. `heap.stats` must show
zero `libcFallbacks`; retain mapped/peak bytes, arenas and cached stack count.
The lifecycle tail should then show `main.before_fclose`,
`main.after_fclose` (code 0), `crt.main_return` (code 0), and
`crt.exit_called` (code 0), followed by reverse-order `crt.exit_callback.begin`
and `.end` markers. `_fini` is labeled separately as `crt.fini.begin/end`;
callback `code` is its registration slot. If the app reports CE-108255-1,
retain the exact last lifecycle marker: absence of `main.after_fclose` points
to stdio close; absence of `crt.main_return` points between returning from
`main` and the CRT hook; no callback marker after `crt.exit_called` narrows the
failure to libc exit before these callbacks; an unmatched callback `.begin` identifies
the callback active at failure. `crt.cxa_callback.begin/end` marks C++ destructors
registered by the application's linked code, not registrations internal to
dynamic libraries. The diagnostic registers `crt.exit_sentinel` before the CRT
callbacks; `crt.exit_sentinel.registration` must report code 0. If the sentinel
appears after the callbacks, the tracked CRT callbacks and sentinel returned;
subsequent libc/system termination is still unproven. A missing sentinel is not
proof that a callback crashed: other uninstrumented handlers or log failure
remain possible. These markers do not identify a crash PC.
`vulkan.not_initialized` is deliberate and is **not** Vulkan success.
Repeat clean close/relaunch and verify the original `PPSA91007` still launches.
The operator reported successful heap/C++/thread/TLS checks, the sentinel and
zero linked C++ destructor registrations; normal termination still crashed.
The first-failure stage stops later checks; retain the entire log.

### Hold comparison after exit crash

Build `scripts/build-vulkan-ps5.ps1 -Target Runtime -RuntimeHold`. Copy the
title directory `dist/vulkan-experiment/ps5-runtime-hold/PPSA91008` itself
directly into the scan root, manually replacing only the operator's previous
test copy. Both variants use the same `PPSA91008` identity: never leave two
copies in scan roots, and do not touch the production `PPSA91007` title.

This variant intentionally has no visible frame and, on successful checks,
stays open rather than executing the normal CRT exit. After 15 seconds, close
it manually and retrieve `/data/echo-vulkan-lab/startup.log`. Require
`diagnostic.hold.start` and at least two `diagnostic.hold.heartbeat` records;
`diagnostic.hold.sleep_failed` indicates a failed wait and is not success.
Compare the error and resulting log with the normal-exit run. This does not
prove that normal exit was fixed; forced manual close is a separate outcome.
The operator supplied a log with heartbeats 1 through 5 and reported closing the
hold build without an error. This does not establish clean normal CRT exit.

## RADV instance diagnostic

This is an experimental instance/device-enumeration test, not a graphics frame.
After reviewing the static report, manually copy the complete
`dist/vulkan-experiment/ps5-radv-instance/PPSA91008` directory directly into
the scan root. It shares the runtime probe's `PPSA91008`: ensure only one copy
of that identity is scanned, and never replace `PPSA91007` or another title.
The script does not connect to the console. If an earlier
`/data/echo-vulkan-lab/radv-instance.log` exists, back it up or remove only
that owned diagnostic log before launch so an early loader failure cannot be
mistaken for a new run.

Launch manually, wait at least 15 seconds, close manually and retrieve
`/data/echo-vulkan-lab/radv-instance.log`. The file is truncated at `main`
entry; failures before `main` may leave no new file. Kernel debug output can
also report log I/O failures. Expected successful stages, in order:

```text
radv.start
radv.icd.get_create.begin
radv.instance.create.begin
radv.instance.create.end            code 0
radv.physical.enumerate.begin
radv.physical.enumerate.end         code 0
radv.physical.count                 code > 0
radv.instance.destroy.begin
radv.instance.destroy.end
radv.hold.start                     code 0
radv.hold.heartbeat                 at least two records
```

If a call fails, the last stage and numerical code are important; hold start
with code 1 is failure, not success. Record the full log, loader/console error
and manual-close outcome. A heartbeat or black screen does not prove a rendered
frame, correct VideoOut, or normal CRT termination.

On 2026-11-22 the operator reported successful instance creation, a count of
one physical device, successful instance destruction, and heartbeats 1-4.
The log did not include the installed FSELF digest or a manual-close result.

## RADV device and display capability diagnostic

The separate `dist/vulkan-experiment/ps5-radv-display/PPSA91008` package
checks extension availability, graphics queue families, driver-reported
display modes, logical-device creation, queue retrieval and device destruction.
It creates no display surface or swapchain and renders no frame. Review the
static report and manually copy the **whole**
title directory, replacing only the operator's previous `PPSA91008` test
copy; do not leave multiple copies in the scan root.

Before launch, back up or remove only the owned
`/data/echo-vulkan-lab/radv-display.log` if it exists. Launch manually, wait
15 seconds, close manually and retrieve that log. The file is truncated at
`main`; failure before `main` can leave no new file. The successful path
includes `radv.instance.extensions.supported`, `radv.physical.count`,
`radv.queue.graphics`, `radv.device.swapchain.supported`,
`radv.displays.count`, `radv.mode.width`, `radv.mode.height`,
`radv.mode.refresh_millihz`, `radv.device.create.end` (code 0),
`radv.device.queue.end` (code 0), `radv.device.destroy.end`,
`radv.instance.destroy.end`, `radv.hold.start` (code 0) and heartbeats.
Mode values are driver-reported, not confirmation of the monitor's active
resolution. Keep the entire log and report any console error and manual-close
outcome; a black screen is expected and does not prove a rendered frame.

On 2026-11-22 the operator reported success through device/queue retrieval,
device and instance destruction and four hold heartbeats. The driver reported
one display, 1920x1080 at 59.94 Hz, Vulkan 1.4 and two queue families.
The log did not include a binary digest or a manual-close result.

## RADV first-frame diagnostic

The separately built title is
`dist/vulkan-experiment/ps5-radv-frame/PPSA91008`. Its `eboot.bin` SHA-256 is
`ee8f57dc72e0759adbc3e0ad04dc4858a78708358ef6ecae0a25c5591a198fa7`;
review `build/vulkan-ps5/logs/radv-frame/PPSA91008-validation.json` before
copying. The five system modules are imports; the AGC link stubs are not
console files. This is a separate diagnostic.

Manually copy the complete `PPSA91008` directory directly into the Kraken
Mount scan root, replacing only your previous experimental `PPSA91008` copy.
Never leave multiple variants of this identity in scan roots or touch
`PPSA91007` or other titles. Before launching, back up or remove only the owned
`/data/echo-vulkan-lab/radv-frame.log`; an early loader failure otherwise
could leave an old log. The program truncates the file at `main`.

Launch manually, wait at least 15 seconds, note whether a solid cyan/blue screen
is visible, close manually, then retrieve the complete log. The successful path
includes `radv.frame.surface.create.end`, `radv.frame.swapchain.create.end`,
`radv.frame.acquire.end`, `radv.frame.record.end`, `radv.frame.submit.end`,
`radv.frame.fence.wait.end`, `radv.frame.present.end`,
`radv.frame.resources.hold`, `radv.hold.start` and heartbeats. Most successful
Vulkan return codes are 0; `1000001003` is `VK_SUBOPTIMAL_KHR` for acquire or
present. `radv.hold.start` code 0 means the calls completed, 1 means a
pre-acquire failure, and 2 means a failure after acquire began, with resources
retained because work may still be in flight. Report the exact last stage,
console error, visible result and manual-close outcome. Successful present
alone does not prove VideoOut actually displayed the frame.

On 2026-11-22 the operator supplied a first-frame log with code 0 through
acquire, submission, fence wait and queue present, followed by four heartbeats,
and confirmed a visible turquoise screen. The installed digest and manual-close
outcome were not reported.

## RADV 3D cube diagnostic

The separately built package is
`dist/vulkan-experiment/ps5-radv-cube/PPSA91008`, FSELF SHA-256
`e97f2d735159927c979d1aa5d05861b7528861bd178f3d487bdda766cf47a6d1`.
This static variant has not been tested on PS5. Manually copy the
entire `PPSA91008` directory directly into the scan root, replacing only your
previous experiment; never leave duplicate copies or change `PPSA91007`.
Before launch, back up or remove only the owned
`/data/echo-vulkan-lab/radv-cube.log` so an early loader failure cannot be
mistaken for this run.

Launch manually and allow 30-60 seconds for the graphics pipeline to compile.
The intended image is a **stationary** orange/yellow/blue perspective cube on
a dark background. There is no texture, animation, depth buffer or interaction.
Close manually and retrieve the full log. Useful successful markers include
`radv.cube.renderpass.end`, `radv.cube.vertex_shader.end`,
`radv.cube.fragment_shader.end`, `radv.cube.pipeline.end`,
`radv.cube.draw.end`, `radv.frame.submit.end`, `radv.frame.fence.wait.end`,
`radv.frame.present.end`, `radv.hold.start` code 0 and heartbeats. If it
fails, preserve the last `.begin` and any error code. If submission may be
in flight the probe holds its resources until manually closed; hold code 1 or
2 is not success. Report the visible result, any console error and manual-close
outcome; a successful present return alone does not establish visible output.

## RADV joystick-controlled cube diagnostic

The separately built package is
`dist/vulkan-experiment/ps5-radv-cube-input/PPSA91008`, FSELF SHA-256
`69589268aca0352da3c9299facee87e9f56db61062202029335e587ffabb1ca7`.
This interactive variant was tested on PS5 as reported below. Manually copy the
complete title directory directly into the scan root, replacing only your
previous experiment; do not leave duplicate `PPSA91008` variants or change
`PPSA91007`. Before launch, back up or remove only the owned
`/data/echo-vulkan-lab/radv-cube-input.log` to avoid mistaking a stale log
for this run.

Launch manually while logged in with a connected controller. Allow 30-60
seconds for pipeline compilation. The first image should be a colored cube on
a dark background. Move the **left stick** horizontally and vertically: it
should rotate the cube around two axes, and stop rotating when centered.
Rotation speed depends on frame rate. This is a standalone diagnostic, not the
game; it has no depth buffer, textures or controller outputs.

The log should reach `radv.frame.present.end`, `radv.pad.user_service.end`,
`radv.pad.init.end`, `radv.pad.users.end`, `radv.pad.open.end`,
`radv.pad.read.end`, `radv.pad.connected`, `radv.cube.loop.present.end` and,
after moving the stick, `radv.pad.motion`. Periodic
`radv.cube.loop.heartbeat` records require 300 completed redraws each; they
are not a fixed-time or frame-rate measurement. No axis values or user IDs are
logged. If input or rendering fails, the diagnostic logs the stage and holds
resources for manual close; `radv.hold.start` code 2 is failure, not success.
After testing, close manually and retrieve the full log. Report whether the
cube appeared, moved in both directions, stopped at center and closed without
an error. A successful present or input call alone does not prove those
visible results.

On 2026-11-22 the operator confirmed a visible moving cube. The supplied log
reaches `radv.cube.pipeline.end`, `radv.pad.connected`, `radv.pad.motion`, a
successful loop submit/fence/present and one `radv.cube.loop.heartbeat`.
The installed digest, behavior of both axes and centering, and manual-close
outcome were not reported.

## PROJECT ECHO Vulkan game

The build used for the 2026-11-22 hardware confirmation was staged at
`dist\ps5-vulkan\PPSA91007`, with `eboot.bin` SHA-256
`5994688994746aeae6a265f039cef29715142fefac6f0dfe52c52b6c030aec41`.
On 2026-11-22 the operator confirmed that this PS5 Vulkan game was fully
playable, including controller input, audio, saving and clean shutdown. The
installed executable digest and firmware/loader setup were not supplied. A later
production shot-particle build had local Vulkan FSELF hash
`2177bff9e8f87d44a8ff72fb97be260ff26d72ec8548404cd367ca06e97e0dff`;
the operator reported both Vulkan and SDL particle builds fully functional.
Installed hashes and console setup were not supplied. A later render-debug
Vulkan candidate had local FSELF hash
`3e4c0edc7ec645f1399c8a64f2d48e2f12a0606f5ae41051cccd29e868c90dd0`
and the operator reported Vulkan held 60 FPS while SDL dropped to 30 FPS in
the isometric scene. The installed hashes and exact setup were not provided.
The newer damage-flash candidate has local Vulkan FSELF hash
`9eb2bcca66ebd4048049fa1cee9cdec3f37a843d4ff74ccb165b448fc347c13d`
and local SDL FSELF hash
`bf697af4b244b3f67148743d8d43546cd631ce6902ae7be366d89aa885bbdb12`.
On 2026-11-22 the operator reported both latest packages fully functional on
PS5. The installed digests, firmware, loader, and individual damage-flash
observation were not supplied.
The title uses the existing 2D Vulkan canvas, not the separate 3D/VFX demonstration.

1. Back up the existing project title outside all loader scan roots and back up
   `/data/project_echo/saves` before trying the candidate. Both builds share
   saves and title ID `PPSA91007`; never scan two copies of that identity.
2. Manually copy the **complete** `PPSA91007` directory from the Vulkan staging
   path into the loader's configured scan root, replacing only your own
   installed PROJECT ECHO title. Do not copy AGC host stubs or system modules.
3. Back up `/data/project_echo/logs/project_echo.log` before launch. The game
   appends to this log; older lines must not be mistaken for this run.
4. The operator confirms full playability, including controls, audio, saving and
   clean shutdown.
5. To identify the exact tested binary and setup, retrieve the new log lines and
   report the last stage, visible result, any console error, installed executable
   hash, firmware and loader version. Useful markers include
   `SDL game-controller initialized without SDL video`, `instance.ready`,
   `device.ready`, `PS5 Vulkan canvas initialized`, `frame.first.submit`,
   `frame.first.present` and `shutdown.complete`. A return from present is not
   proof of a visible frame. If no new log is written, record the loader error;
   this may be an early failure or logging failure.

No automatic connection, upload or console deletion is performed.

## Separate 3D Vulkan application

| Check | Evidence required | Status |
| --- | --- | --- |
| Original game still launches | Independent `PPSA91007` launch before/after | NOT TESTED |
| Experiment launch | Distinct title and structured startup log | PASS (operator-reported RADV instance diagnostic) |
| RADV initialization | Driver/API version, queues/extensions, no SDL VideoOut startup | PARTIAL (operator-reported device/queue, extensions and first-frame submission) |
| Native runtime | Heap statistics, thread creation, AGC/direct memory success | NOT TESTED |
| Pipeline and resources | Shader/pipeline/buffer/depth allocation log | PARTIAL (interactive cube shader/pipeline and framebuffers reported; buffer/depth untested) |
| First frame | GPU submit, present and visible textured rotating cube plus floor | NOT TESTED |
| Depth | Cube occludes floor correctly from both camera modes | NOT TESTED |
| Textures/alpha/UI | Checker texture, original Echo sprite and overlay visible together | NOT TESTED |
| Lighting | Directional and point lights visibly affect surfaces | NOT TESTED |
| VFX | CPU particles visibly rendered; no compute claim | NOT TESTED |
| Stability | FPS/frame time, allocation plateau, ten-minute run | NOT TESTED |
| Resolution | Separate 720p and 1080p runs with allocation statistics | NOT TESTED |
| Shutdown | GPU idle, resources freed, no crash or lingering VideoOut ownership | NOT TESTED |
| Relaunch | Ten clean close/relaunch cycles without corruption | NOT TESTED |
| Failure | Missing shader/allocation failures name exact stage and exit safely | NOT TESTED |

Do not enable desktop validation layers on PS5 unless availability is verified.
Retain first-frame logs and a manual frame capture. Record every result separately
from static checks in RESULTS; do not claim 4K readiness.
