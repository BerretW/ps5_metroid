---
name: ps5-vulkan-radv
description: Build, debug and validate PROJECT ECHO's isolated PS5 RADV Vulkan experiments on native Windows.
---

# PS5 Vulkan RADV workflow

## Activate when

Working on PS5 Vulkan, RADV, VideoOut presentation, the Vulkan experiment's
native Windows build, or PS5 graphics hardware diagnostics.

## Procedure

1. Read `docs/experiments/vulkan3d/PS5_BUILD.md`, `VULKAN_COMPATIBILITY.md`,
   `RESULTS.md` and `HARDWARE_TESTS.md`; check current pins and provenance in
   `experiments/vulkan3d/dependencies.json` and the scripts rather than assuming
   an old binary or local cache still matches.
2. Keep the experiment isolated from the working SDL game and `src/game/`.
   Preserve production identity `PPSA91007` and the existing confirmed
   diagnostic packages. Do not initialize the SDL PS5 VideoOut driver alongside
   RADV VideoOut or enable the known-problematic PS5 keyboard path.
3. Use native Windows 11 x64, PowerShell, LLVM/LLD, CMake/Ninja and the existing
   native application CRT and converter. Never run PS5 binaries on Windows or
   substitute a payload CRT, Windows COFF libraries, Linux host, WSL or a
   proprietary SDK. Restore/build standalone Hello as documented in
   `docs/PS5_COMPATIBILITY.md`.
4. With the documented, previously prepared host tools, cross file, target
   archive and runtime-hold template available, build the interactive probe:
   `scripts\probe-vulkan-radv-link.ps1 -Native -Interactive -Package`.
   The build replaces its recognized variant's local output; preserve a
   hardware-tested package separately before rebuilding it when needed.
   The host-tool and cross-file preparation is not yet a complete clean-machine
   pipeline. `scripts\build-vulkan-radv-archive.ps1` rebuilds the target archive
   when its documented prerequisites exist; it does not restore all of them.
5. Run `tests\vulkan_ps5_radv_tests.ps1 -Interactive` after building the required
   isolated variants. It includes controller/loop mocks, package boundary checks
   and Windows GPU pixel readbacks; it requires `VULKAN_SDK` and a Windows Vulkan
   GPU. Run `scripts\build-windows.ps1 -RunTests` after build or gameplay changes,
   and `tests\ps5_package_tests.ps1 -Controller` after package changes.
6. Validate the exact candidate package, hashes, system imports and ELF/FSELF
   separately from hardware results. See `build/vulkan-ps5/logs` and package
   provenance. Do not turn a static pass or successful present return into a
   visible-output or console-compatibility claim.
7. Transfers, launches, log retrieval and closing are manual. Copy the complete
   `PPSA91008` application directory directly into the loader scan root, after
   checking ownership; never leave duplicate variants of that title or overwrite
   `PPSA91007` or unrelated files. Back up or remove only the owned diagnostic
   log before launch, since failure before `main` can leave a stale log. Record
   the operator's complete log, visible result, close outcome and installed
   digest only when actually supplied.

## Verified boundaries and lessons

- The selected PS5 target is `x86_64-sie-ps5` using native boilerplate `_start`,
  the compatible runtime, and ELF target archives. `payload_main` and payload
  CRTs have different loader contracts. LLVM `lib/linux` in upstream naming is
  not evidence that the Windows host must run Linux.
- The pinned Mesa host tools require the build-local
  `mesa-host-binary-spirv.patch`: unpatched Windows `mesa_clc` writes binary
  SPIR-V in text mode and corrupts it. Never modify the upstream checkout as
  the source of truth. Host tools are Windows executables; target RADV archive
  members must be ELF64/x86-64, not COFF.
- Whole-archive RADV linkage, the native unwind layout, real runtime wrappers
  and audited libc bindings are required. Preserve explicit hidden weak symbols
  with `-fno-visibility-from-dllstorageclass`; the conflicting
  `-fvisibility-nodllstorageclass=default` caused thousands of false dynamic
  weak imports and converter failure. Do not suppress undefined strong symbols.
- `compat.c` binds `mkstemp` to the verified platform `ps5_mkstemps` and
  `readlink` to `ENOSYS` only after the link script verifies their specific
  optional Mesa diagnostic call sites. Do not generalize this adapter to real
  filesystem functionality without a new audit.
- AGC `.so` stubs are host-link-only: never copy them to the console. The
  instance/frame variants have five explicitly validated system imports; the
  interactive controller variant adds Pad and UserService, validated with
  `-ExperimentalRadv -ExperimentalRadvPad`. Keep production validation
  independent. Include license notices and evaluate GPL corresponding-source
  obligations before distribution.
- Use `VK_KHR_display`/VideoOut, not an assumed SDL Vulkan surface. Query
  instance/device extensions, graphics queue, display plane, supported display,
  mode, surface capabilities, format, usage and FIFO support before creating a
  swapchain. Reported modes and API version are driver values, not proof of
  the monitor's active resolution or Vulkan conformance.
- Compile shaders offline for Vulkan 1.0, validate SPIR-V, and preserve shader
  compiler, SPIR-V and source provenance. The diagnostic uses a render pass,
  image views/framebuffers, shaders and a graphics pipeline; see `cube_probe.c`.
  The interactive cube uses back-face culling, not a depth attachment.
- For repeated frames, wait for the GPU fence before resetting its command
  pool/fence and use presentation semaphores per swapchain image. Check all
  results, use finite acquire/fence timeouts and log failing stages. After
  uncertain in-flight work, the diagnostic retains resources for manual close;
  that is not a validated production shutdown policy.
- The interactive adapter uses the pinned SDL PS5 joystick header's pad ABI
  without initializing SDL video; it polls the first logged-in user's left
  stick. Do not log user IDs or continuous axis samples. A connected/read
  return is not proof of physical movement without an operator observation.
- On 2026-11-22 the operator reported a visible turquoise first frame and a
  visible moving interactive cube. The cube log shows graphics-pipeline
  creation, submit/fence/present, pad connection and motion, plus a redraw
  heartbeat. The installed binary digest and close outcome were not reported.
  Depth, textures, the full renderer, sustained stability, clean normal CRT
  exit and complete controller behavior remain unverified on PS5.
- The earlier runtime diagnostic reached exit callbacks but reported
  CE-108255-1 after normal termination; the precise cause is unknown. A hold
  diagnostic is not an exit fix. Never infer GPU memory exhaustion from the
  separate 1080p SDL software-renderer allocation failure.

## References

- `scripts/probe-vulkan-radv-link.ps1`, `scripts/build-vulkan-radv-archive.ps1`
- `experiments/vulkan3d/ps5/radv/`, `experiments/vulkan3d/dependencies.json`
- `tests/vulkan_ps5_radv_tests.ps1`, `tests/vulkan_ps5_host_tool_tests.ps1`
- `docs/experiments/vulkan3d/PS5_BUILD.md`, `RESULTS.md`, `HARDWARE_TESTS.md`,
  `VULKAN_COMPATIBILITY.md`, and `docs/STATUS.md`
