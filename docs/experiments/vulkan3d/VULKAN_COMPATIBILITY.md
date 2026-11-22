# Vulkan / RADV compatibility audit

Audit date: 2026-11-22. The Windows experiment uses the installed desktop Vulkan
loader and a real hardware GPU, not the PS5 driver. RADV is the selected future
PS5 backend; the older `ps5vk` driver is not linked or treated as interchangeable.

## Exact upstream revisions

| Repository | Audited HEAD | Build recipe pin |
| --- | --- | --- |
| [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan/tree/7f492b4c6f67a4e8c8c67a4272cfeef22abea9cc) | `7f492b4c6f67a4e8c8c67a4272cfeef22abea9cc` | same |
| [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa) | `75238ae4db3249803df3deeba47340f6de27962e` | `1ba9874f478657ccf6da72ce55f0260164cc0bd0` |
| [PS5_PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK) | `88d6e41ec7ee7e76d871ff14cd78b5cc4c8e5989` | `b83202be73e930050e00de0f1b4d9ec46c0391bf` |
| [PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate/tree/91179df760df1e6c6f2d4d172bc435319d79ff3c) | `91179df760df1e6c6f2d4d172bc435319d79ff3c` | template SDK `611893fc25ef3e138f5bc2c90d829d8b58174cd2` |

The Mesa README references older successful console revisions; they are not the
pin in `tools/build-radv.sh`. The template's SDK pin differs from PS5_Vulkan's
bootstrap pin. A future artifact must record both Mesa and SDK revisions in
`PROVENANCE.txt`, plus its independently measured SHA-256.

## Approach A: prebuilt artifacts

**BLOCKED:** the GitHub releases API returned no releases for all four repositories
at audit time. The template README's release link is not sufficient provenance.
The RADV "release archive" in the build recipe is a **locally built** output:
`.deps/native/radv-release/lib/libvulkan_radeon.ps5.a`. No independently
obtainable verified archive/checksum was found. No fabricated archive digest is
recorded, and no release executable was repurposed as a library.

## Approach B: native Windows adaptation

**NOT IMPLEMENTED / BLOCKED by dependencies**, not proved fundamentally impossible.
[build-radv.sh](https://github.com/mihawk-99/PS5_Vulkan/blob/7f492b4c6f67a4e8c8c67a4272cfeef22abea9cc/tools/build-radv.sh)
uses Bash, rsync, symlinks, git archive and Meson/Ninja. Bash orchestration, copying,
hashing and symlinks can be replaced by PowerShell/native equivalents. Meson,
Ninja, Python/Mako and Windows LLVM exist as native host tools; these script
assumptions alone do not prove a Linux-only compiler requirement.

The substantive remaining work is building native host `mesa_clc` and
`vtn_bindgen2` against compatible LLVM/Clang libraries, libclc and
SPIRV-LLVM-Translator; building the pinned platform layer and ELF
libc++/libc++abi/libunwind; replacing target compiler wrappers; and supplying
**ELF** Clang builtins with `__emutls_get_address`. The link recipe explicitly
selects `lib/linux/libclang_rt.builtins-x86_64.a`; the name is host-specific, but
the requirement is compatible ELF code, not executing that archive on Linux.
Windows COFF runtime archives cannot substitute. No host Mesa rewrite is justified.

[ps5-cross.ini](https://github.com/mihawk-99/PS5_Vulkan/blob/7f492b4c6f67a4e8c8c67a4272cfeef22abea9cc/tooling/radv/ps5-cross.ini)
sets FreeBSD/x86_64/little-endian as the **host** machine and
`needs_exe_wrapper=true`. Target tools must never run a PS5 executable on Windows.

## Runtime boundary

| Area | Verified upstream requirement / Echo mismatch |
| --- | --- |
| ABI | Public PS5 x86_64 ELF; Echo target is `x86_64-sie-ps5`. Object compatibility alone does not establish libc or loader compatibility. |
| Driver archive | Whole-archive linkage required: weak dispatch references otherwise leave entry points null. |
| CRT | Echo native `_start` and clean-room runtime differ from payload CRT assumptions; do not switch to `payload_main`. |
| C++ | RADV ACO needs libc++, libc++abi, libunwind; original Echo does not link them. |
| Linker | RADV recipe uses `ps5-pie-unwind.ld`, not Echo's `ps5-pie.ld`. Validate unwind layout and converter compatibility before reuse. |
| Allocator | Platform wraps the malloc family to a direct-memory heap; all alloc/free domains must agree. Never blindly redirect the working SDL application. |
| Threads | Wrapped create/join/detach allocate 2 MiB default stacks in direct memory. |
| libc | Numerous `--defsym` libc replacements and local symbol map required; locale, memstream, TLS destructors, directory APIs and random APIs must resolve correctly. |
| GPU | PS5 winsys initializes AGC, direct memory, submission ring and cache management in the title. GPU allocations must not reuse SDL surfaces. |
| Imports | AGC/AGCDriver stubs and extra kernel/platform symbols beyond the production runtime imports must be audited. |
| Presentation | `VK_KHR_display` through VideoOut, not an SDL PS5 Vulkan surface. Do not initialize the old SDL VideoOut driver concurrently. |
| Synchronization | Upstream exposes queue families but some share the graphics ring; reported families do not imply real asynchronous compute. |
| Heap/mappings | Direct-memory heap and GPU virtual ranges require collision/reservation checks against the native app layout. |
| Size | No candidate archive/application acquired, so PS5 package size is UNKNOWN. |

Sources:
[radv-link.sh](https://github.com/mihawk-99/PS5_Vulkan/blob/7f492b4c6f67a4e8c8c67a4272cfeef22abea9cc/tools/radv-link.sh),
[heap.h](https://github.com/mihawk-99/PS5_PayloadSDK/blob/b83202be73e930050e00de0f1b4d9ec46c0391bf/platform/include/ps5platform/heap.h),
[winsys](https://github.com/mihawk-99/PS5_Mesa/blob/1ba9874f478657ccf6da72ce55f0260164cc0bd0/src/amd/vulkan/winsys/ps5/radv_ps5_winsys.c).

## Licensing

No upstream platform, driver or sample implementation/artwork is copied.
Original experiment code retains the repository MIT license. Windows uses
Khronos Vulkan SDK headers/loader, existing MIT yyjson and the already vendored
MIT/public-domain stb decoder.

PS5_Vulkan is GPL-3.0-or-later. PS5_PayloadSDK's platform layer carries GPL
notices and would be statically linked by the RADV recipe. Mesa is a mixed-license
distribution; its core and inspected PS5 winsys are MIT, but
[file-level SPDX and Mesa license documentation](https://github.com/mihawk-99/PS5_Mesa/blob/1ba9874f478657ccf6da72ce55f0260164cc0bd0/docs/license.rst)
must govern the actual archive, not GitHub's aggregate license badge.
Template base/starter/shadow/bloom/compute examples inspected carry MIT notices;
its optional UI module is GPL and assets have separate licenses. They are
reference material, not imported dependencies.

A future distributed PS5 binary combining GPL platform/runtime code needs
appropriate notices and corresponding-source obligations evaluated. Do not
label the combined executable MIT merely because Echo source is MIT.

## Upstream evidence versus Echo

Reviewed README, GETTING_STARTED, RADV_PHASE, CTS_GAPS, NATIVE_TOOLING,
DEPLOYMENT, TESTING, build/link/cross scripts, template startup/pipeline,
swapchain, shadow, bloom and compute sources at the pins above. Upstream RADV
reports Vulkan 1.4; `conformanceVersion` remains 0.0.0.0 pending its stated CTS
acceptance. Upstream's console frames/CTS results are **not Echo hardware evidence**.
This experiment enables only its implemented desktop features; compute, shadows
and postprocessing remain false even on a GPU supporting them.

## Existing software framebuffer failure

The user's console log proves 1920x1080 window creation succeeds and the software
renderer returns `Out of memory`; 1280x720 succeeds. SDL video initialization
already allocated its display surface and registered 64 MiB direct VideoOut
memory successfully before that failure.

The patched SDL window framebuffer allocates another RGBA CPU surface. Pixel
storage alone is 8,294,400 bytes at 1080p versus 3,686,400 bytes at 720p; the
display CPU surface remains separate. The runtime emitter registers imported
malloc/free/posix_memalign as the application heap API, rather than an independently
measured unlimited allocator. Its maximum-valued heap metadata does **not**
prove allocations are unlimited.

The log does not isolate window surface pixels, renderer bookkeeping, heap
fragmentation or a runtime allocation restriction. It does not prove GPU-memory
exhaustion or VideoOut failure. Do not change production based on speculation.
Next hardware probe: instrument the failing window-surface and software-renderer
allocations independently, log requested sizes and results, compare clean 720p
and 1080p starts, then examine allocator limits. RADV has its own documented
private-libc-heap failure and direct-memory solution, which is analogous evidence,
not proof of Echo's exact cause.
