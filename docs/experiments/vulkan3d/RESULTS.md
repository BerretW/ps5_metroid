# Experiment results

Baseline: `1e90adc6519d8f278f856beaaac7f668287909bd`.
Work is isolated on `experiment/vulkan-3d`; no merge/push/deployment performed.

| Acceptance stage | Result |
| --- | --- |
| A: genuine Windows GPU rendering | PASS: NVIDIA GeForce RTX 4070, textured cube/floor, depth, lights and presentation |
| B: reusable initial backend | PASS for SDL-free interfaces, bounded resources, meshes/cameras, materials and alpha sprites; advanced API work remains |
| C: PS5-compatible linked Vulkan rendering application/package | PARTIAL: isolated RADV first-frame diagnostic STATIC PASS; reusable renderer remains blocked |
| D: actual PS5 Vulkan rendering | PARTIAL: operator reports a visible cyan clear and moving interactive cube; full scene remains untested |

## Verified Windows evidence

Native Visual Studio C++/CMake/Ninja build, official Vulkan SDK 1.4.363.1,
offline glslang 16.6.0 SPIR-V compilation and `spirv-val` validation passed.
The installed hardware exposes Vulkan 1.4.341; the renderer requests Vulkan 1.1,
not unimplemented Vulkan 1.4 features. This is hardware rasterization, never a
CPU renderer or Vulkan software-device fallback.

`scripts/build-vulkan-windows.ps1 -Configuration Debug -RunTests` and the same
command with `-Configuration Release` each passed **4/4**:

- Rendering math, camera, hierarchy, validation, schema/content and VFX tests.
- glTF mesh/material import, strided normal/UV data and invalid-data cases.
- Real device/swapchain/pipeline/buffer/texture/depth creation, 60 presented
  frames, resize to 960x540, lifetime probes, GPU readback and clean shutdown.
- GPU image-region comparison with explicit pixel thresholds.

The integration log reports zero validation errors. The independently staged
application additionally presented 120 isometric frames at a 1920x1080 client
target and relaunched for 60 frames at 1280x720, also with zero validation
errors. Both camera modes show the same scene, original alpha sprite and UI.
The captured frames are GPU transfer readbacks before presentation.
The final Release package also passed the 120-frame 1080p isometric run.
Executable-relative assets were verified from a different working directory.
A missing vertex shader returned exit 1 with structured `shader.open`,
`scene.vert.spv` and errno 2 evidence; cleanup completed and restoring the
shader allowed a successful zero-validation-error relaunch.

Logs, native compiler commands, captures and the unified rendering/component
manifest reside in `build/vulkan-windows`; the runnable whole-directory output
is `dist/vulkan-experiment/windows`. SDK tool checksums/pins are in the
experiment dependency manifest; the local compiler digest is not independently
verified installer provenance. Final Release executable SHA-256:
`ad0d60f24bdc78c64dbb2cb9e1b42469eff675d05476b5d5500466220549c8c7`.
It requires the Windows Vulkan driver/loader and release Visual C++ x64 runtime,
not copied development-only debug CRT libraries.

Diagnostics include CPU recording time, FPS, draw/triangle counts, CPU particle
count and accounted Vulkan allocation bytes. Accounted bytes exclude driver
internals/swapchain ownership; they are not complete VRAM/heap measurements.
Capture and resize stalls are included in FPS, so short captures are not a
performance benchmark. GPU timestamps and sustained stability remain untested.

## Production regression/preservation

`scripts/build-windows.ps1 -Configuration Debug -RunTests`: **19/19 PASS**.
Root CMake/default module selection, gameplay, SDL renderer and production PS5
scripts/metadata remain unchanged. After rebuilding, both baseline digests
still match:

| Artifact | SHA-256 |
| --- | --- |
| Windows PROJECT ECHO | `339f758c22473383bede407f1b77004ca18dc4e128ebd841bbc911eab9901853` |
| Existing PPSA91007 eboot | `b4076b970cde91ca3f3ee96a65bf96a1dbcae22b5d08ed64397cbe532cebbef9` |

No experiment PS5 title was installed by these scripts. `PPSA91008` is reserved separately with
distinct content/save/log identity; operator-console collision checks are still
required before any future manual deployment.

## PS5 RADV diagnostic

The pinned PS5 RADV archive was rebuilt on native Windows with 689 verified
ELF64/x86-64 relocatables. Native link, conversion, FSELF signing and package
validation now pass for a separate `_start` diagnostic which creates a Vulkan
instance and enumerates physical devices. It renders no frame. Its five
system-module imports and runtime companion are recorded in the static report;
the AGC link stubs are not packaged. No unresolved strong symbols were
suppressed, and unsupported `readlink` returns `ENOSYS`. See
[VULKAN_COMPATIBILITY.md](VULKAN_COMPATIBILITY.md) for the visibility fix and
adapter boundaries.

The local FSELF is 26,052,314 bytes, SHA-256
`b3b35de64a7af556c69f61b3d45494c9b6e0c1a347a22259cc97b3adc5912671`.
On 2026-11-22 the operator supplied a console log reporting successful instance
creation, enumeration of one physical device, instance destruction and four
five-second hold heartbeats. The reported log contains no package hash or
manual-close result; it is associated with the requested instance diagnostic,
not independent proof of the installed binary's digest. This instance-only
probe did not test submission or presentation; normal CRT exit remains
unresolved. Host adapter tests, experimental package positive/
negative checks, production controller package tests and Windows regressions
passed.

The next separate device/display capability diagnostic is statically packaged
as `dist/vulkan-experiment/ps5-radv-display/PPSA91008`, FSELF 26,056,522 bytes,
SHA-256 `7cd58352e86600fe95cf27fc287927df1322765037f7d74936f1fa61a811bd90`.
Its mock-driven Windows tests cover the supported path, missing instance
extension, missing graphics queue, unsupported mode and a log failure after
device creation. The device is destroyed on that logging-failure path.
On 2026-11-22 the operator supplied its console log: instance and device
creation, graphics queue retrieval, device and instance destruction succeeded,
followed by four hold heartbeats. It reports Vulkan 1.4, vendor ID 4098, device
ID 0, two queue families, one display and a 1920x1080 59.94-Hz mode. These are
driver-reported values; they do not prove active monitor resolution, Vulkan
conformance or presentation. The log did not identify the installed FSELF digest
or report manual close. This diagnostic produces no frame.

The separate first-frame diagnostic is
`dist/vulkan-experiment/ps5-radv-frame/PPSA91008`, FSELF 26,063,562 bytes,
SHA-256 `ee8f57dc72e0759adbc3e0ad04dc4858a78708358ef6ecae0a25c5591a198fa7`.
It validates plane and surface capabilities, clears one swapchain image cyan,
submits and presents. Native link/conversion, FSELF validation and exact
five-module import checks pass. On 2026-11-22 the operator supplied a log for
the requested first-frame diagnostic: swapchain creation, acquire, command
recording, queue submit, fence wait and queue present all returned code 0,
followed by four hold heartbeats. The operator subsequently confirmed a visible
turquoise screen. The log does not identify the installed FSELF digest, and the
manual-close outcome was not reported. This confirms a visible solid-color
diagnostic, not the textured scene or normal shutdown.
Windows mocks cover capabilities, synchronization, failure
and resource retention. The held GPU resources are deliberate; this is not
a normal shutdown test or the reusable game renderer.

The new separate cube diagnostic is
`dist/vulkan-experiment/ps5-radv-cube/PPSA91008`, FSELF 26,071,674 bytes,
SHA-256 `e97f2d735159927c979d1aa5d05861b7528861bd178f3d487bdda766cf47a6d1`.
It creates a graphics pipeline from embedded, Vulkan 1.0-validated SPIR-V and
draws a static perspective cube with orange, yellow and blue faces on a dark
background. The six faces use a fixed back-to-front order for this fixed view;
there is no depth attachment, animation or texture. Native Windows
cross-compilation, conversion, FSELF and experimental package checks pass.
The host test renders the same shaders and pipeline to a Windows NVIDIA GeForce
RTX 4070 offscreen image (using a transfer-source final layout for readback):
4,393 orange, 4,398 yellow, 4,393 blue and 52,352 background pixels, including
face-position assertions. Failure and lifetime tests cover framebuffer/pipeline
creation failure and post-acquire resource retention. This does not test the
PS5 driver; PS5 cube execution and visible output remain **NOT TESTED**.

The interactive variant is
`dist/vulkan-experiment/ps5-radv-cube-input/PPSA91008`, FSELF 26,078,120
bytes, SHA-256
`69589268aca0352da3c9299facee87e9f56db61062202029335e587ffabb1ca7`.
It polls the first logged-in user's left stick through the same public pad
declarations used by the pinned SDL PS5 joystick backend, and rotates the cube
around two axes. It redraws with per-image presentation semaphores and a fence,
retaining resources on error for manual close. Back-face culling replaces the
fixed face order; there is still no depth attachment, texture or game logic.
Native conversion and the exact seven-system-module package check pass.
Host mocks cover stick dead zones, movement, disconnect/read errors, frame
sequencing and failure retention. On the Windows NVIDIA GPU, two rotated
offscreen renders changed 8,576 and 10,315 pixels from the initial view;
they contained 13,379 and 12,739 colorful pixels respectively. The offscreen
test substitutes a transfer-source final layout for readback.

On 2026-11-22 the operator reported the cube was visible and moved on PS5.
The supplied log reports successful shader modules and graphics pipeline,
frame submit/fence/present, pad initialization/open/read/connection, motion and
one redraw heartbeat after 300 loop iterations. The log does not independently
identify the installed FSELF digest, and the operator did not report both axes,
centering, manual-close outcome or a sustained frame rate. This is a hardware
result for the interactive diagnostic only, not the static package, depth,
textures, full game renderer or normal CRT shutdown.

### New native runtime evidence

`scripts/build-vulkan-ps5.ps1 -Target Dependencies -RestoreDependencies` and
`-Target Runtime` succeeded using native Windows Clang/LLD/CMake/Ninja and the
existing native converter/FSELF signer. Standalone Hello also passed static
package validation before the preflight.

The SDK v0.42 libc++/libc++abi/libunwind archives are available and match the
release used by the pinned upstream platform bootstrap. New platform and
LLVM 18.1.8 ELF builtins archives were built from pinned sources. All five
archives' members were inspected: **54/18/9/25/167** ELF64/x86-64 relocatables.
The linker uses real emulated TLS, locale/assertion providers, local static
C++ ABI symbols and explicit unwind boundaries; no undefined-symbol bypass,
dummy runtime or payload CRT was introduced.

The native preflight package is
`dist/vulkan-experiment/PPSA91008`; its only system modules are
`libkernel.prx` and `libSceLibcInternal.prx`. Identity, ELF/FSELF segments,
imports and companion runtime passed the existing static validator.
Package provenance records the exact current ELF runtime inputs and FSELF
digest. Licenses include GPL platform/runtime, LLVM, musl and dlmalloc notices.

Runtime ELF size: **545,320 bytes**; signed FSELF: **438,758 bytes**.
FSELF SHA-256:
`a11dedd943e05a1cb2e8a5c4c0c02a31a5b2b31c2481aac3658e179d85626b10`.
`_start` is present; emulated TLS/personality/assertion providers are local,
unwind bounds exist and no application exports reach the native converter.
Windows regression remains **19/19 PASS**, production PS5 controller package
rejection tests PASS, and both original executable hashes still match baseline.

The operator then ran the preflight on PS5 and reported `CE-108255-1` after
`runtime.complete`; every heap, C++, locale, thread, direct-stack and TLS probe
passed with zero libc allocator fallbacks. A follow-up diagnostic FSELF adds
durable kernel-I/O markers before/after `fclose`, after `main` returns, and at
the application CRT's call into `exit`. Its current SHA-256 is
`d0dde9f54cf3ecf1c0858a1ed8599d23fd13f49ec2a412ac0ede5e9292fea4b5`.
The operator tested the preceding build
`56c6254581060752997563af30a14e09fe7a7be4503bf32f6734f9b4cccc3a2c`
and reported `crt.fini.begin/end` (slot 1) and
`crt.exit_callback.begin/end` (slot 0). In the pinned `_start`, slot 0 is
the loader-supplied teardown and slot 1 is `_fini`; both returned.
This does not show whether other libc-internal or C++ exit handlers execute
later, or identify the crash instruction.

The current diagnostic adds a sentinel registered before these callbacks and
instruments `__cxa_atexit` calls made by linked application code. It does not
interpose registrations made internally in separately loaded system libraries.
On that build, the operator reported zero application-registered C++ destructors
and `crt.exit_sentinel` after both wrapped callbacks. Thus these callbacks
returned, but separately loaded runtime internals, the termination path and the
crash instruction remain unobserved.

The pinned native boilerplate's `src/demo_renderer.cpp` explicitly avoids
returning from `main` or calling `exit` in its launch context. The separate
`-Target Runtime -RuntimeHold` preflight repeats the checks, flushes/closes its
log, then deliberately remains active with five-second heartbeats. It has
FSELF SHA-256 `b7525260b6e1e15c68c561d85c947c7c9a9c5eec481291baa01a50c05568470b`
and static validation PASS. The operator supplied a PS5 log reaching
`diagnostic.hold.start` and heartbeats 1 through 5, after all heap/C++/thread/TLS
checks passed. This observes five completed five-second wait cycles; the result
of manually closing this hold build was reported as error-free. Normal CRT exit
remains **FAILING** on the prior diagnostic build.
This is a diagnostic comparison, not a fix for `exit` and not a production
shutdown policy. See
[HARDWARE_TESTS](HARDWARE_TESTS.md) for interpreting the last marker.

**This is not Vulkan rendering.** The preflight performs heap/C++/pthread/TLS
checks when manually run on PS5; the user reported success in hold mode.
No video/GPU code is included. Its log and manual procedure are in
[HARDWARE_TESTS](HARDWARE_TESTS.md).

### Remaining driver boundary

`scripts/build-vulkan-ps5.ps1 -Probe` uses native Windows LLVM 18.1.8,
`--target=x86_64-sie-ps5`, public headers and LLD. It emits an ELF64 x86-64
relocatable object, then reproducibly fails linking with:

```text
ld.lld: error: undefined symbol: vkCreateInstance
```

This is deliberately a link-only missing-driver probe, **not** a complete
native application, runtime ABI proof, FSELF, package or hardware test.
`build/vulkan-ps5/compatibility.json` preserves arguments and missing artifacts.
The separate RADV ICD whole-archive probe resolves those boundaries and produced
the statically validated instance diagnostic described above. The ordinary
renderer build still exits explicitly BLOCKED rather than producing a
success-shaped rendering placeholder.

The audited primary driver is RADV, not legacy ps5vk. No independently
obtainable release archive/provenance/checksum was verified. Native Windows
adaptation has produced host tools, target archive and an instance package,
but still needs clean-machine host-tool automation, hardware validation of the
driver libc/import bindings and the new VK_KHR_display/VideoOut diagnostic. The exact pinned
sources, license boundaries, runtime differences and staged integration plan
are in [VULKAN_COMPATIBILITY](VULKAN_COMPATIBILITY.md) and [PS5_BUILD](PS5_BUILD.md).
No template implementation or protected sample assets were copied. GPL platform
code is explicitly imported as a pinned external dependency for the preflight,
not silently relicensed as original MIT Echo code.

The existing 1080p SDL failure remains separate: its successful VideoOut setup
followed by software-renderer `Out of memory` and successful 720p retry implicate
CPU-side allocations, but the exact failing allocation/heap cause is not proven.
It does not establish insufficient GPU memory; no production allocator changed.

## Remaining work before broader acceptance

No full PROJECT ECHO migration, SDL adapter, full scene authoring schema, GPU
multi-material submesh submission, normal
maps/IBL, offscreen target/viewport API, shadows, bloom/blur, instancing, GPU
timing/compute particles, richer glTF/importer registry, full animation/layers,
screen flash, camera shake or light events is claimed. VFX is bounded CPU
simulation rasterized by the GPU. Device loss fails explicitly; automatic
recovery is not implemented.

PS5 stages require verified driver/runtime artifacts first, a separate native
presentation adapter and compatible initialization/cleanup, then real ELF/FSELF
and package/import/license validation. Only afterward can the manual
[hardware checklist](HARDWARE_TESTS.md) establish stage D. Neither Windows
success nor upstream RADV tests prove Echo PS5 hardware support.
