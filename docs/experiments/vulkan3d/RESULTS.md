# Experiment results

Baseline: `1e90adc6519d8f278f856beaaac7f668287909bd`.
Work is isolated on `experiment/vulkan-3d`; no merge/push/deployment performed.

| Acceptance stage | Result |
| --- | --- |
| A: genuine Windows GPU rendering | PASS: NVIDIA GeForce RTX 4070, textured cube/floor, depth, lights and presentation |
| B: reusable initial backend | PASS for SDL-free interfaces, bounded resources, meshes/cameras, materials and alpha sprites; advanced API work remains |
| C: PS5-compatible linked application/package | BLOCKED: no verified compatible RADV/platform/runtime archives |
| D: actual PS5 Vulkan rendering | NOT TESTED: no candidate application exists |

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

No experiment PS5 title was installed. `PPSA91008` is reserved separately with
distinct content/save/log identity; operator-console collision checks are still
required before any future manual deployment.

## Precise PS5 blocker

`scripts/build-vulkan-ps5.ps1 -Probe` uses native Windows LLVM 18.1.8,
`--target=x86_64-sie-ps5`, public headers and LLD. It emits an ELF64 x86-64
relocatable object, then reproducibly fails linking with:

```text
ld.lld: error: undefined symbol: vkCreateInstance
```

This is deliberately a link-only missing-driver probe, **not** a complete
native application, runtime ABI proof, FSELF, package or hardware test.
`build/vulkan-ps5/compatibility.json` preserves arguments and missing artifacts.
No actual driver was linked, so further unresolved-symbol/ABI checks have not
been performed. The ordinary build exits explicitly BLOCKED rather than
producing a success-shaped placeholder.

The audited primary driver is RADV, not legacy ps5vk. No independently
obtainable release archive/provenance/checksum was verified. Native Windows
adaptation still needs matching Mesa host compiler tools and LLVM/SPIR-V
libraries, target RADV/platform archives, ELF libc++/libc++abi/libunwind and
emulated-TLS builtins, plus audited allocator/thread/unwind and native
VK_KHR_display/VideoOut integration. Bash-only orchestration is portable; this
does **not** establish that a Windows port is impossible. The exact pinned
sources, license boundaries, runtime differences and staged integration plan
are in [VULKAN_COMPATIBILITY](VULKAN_COMPATIBILITY.md) and [PS5_BUILD](PS5_BUILD.md).
No GPL platform/template implementation or protected sample assets were copied.

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
