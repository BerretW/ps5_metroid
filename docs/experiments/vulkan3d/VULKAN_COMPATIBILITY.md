# Vulkan / RADV compatibility audit

Audit date: 2026-11-22. The Windows experiment uses the installed desktop Vulkan
loader and a real hardware GPU, not the PS5 driver. RADV is now linked into an
opt-in, statically validated PS5 game candidate; the older `ps5vk` driver is not
linked or treated as interchangeable. The game candidate itself is not yet
hardware-tested.

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

**GAME CANDIDATE BUILDS / PS5 HARDWARE NOT TESTED.** A clean-machine restoration
of the RADV host tools remains incomplete.
[build-radv.sh](https://github.com/mihawk-99/PS5_Vulkan/blob/7f492b4c6f67a4e8c8c67a4272cfeef22abea9cc/tools/build-radv.sh)
uses Bash, rsync, symlinks, git archive and Meson/Ninja. Bash orchestration, copying,
hashing and symlinks can be replaced by PowerShell/native equivalents. Meson,
Ninja, Python/Mako and Windows LLVM exist as native host tools; these script
assumptions alone do not prove a Linux-only compiler requirement.

The pinned platform layer and **ELF** Clang builtins with
`__emutls_get_address` now build natively. The existing SDK v0.42 supplies the
exact libc++/libc++abi/libunwind archives retained by upstream
`platform/tools/setup-sdk.sh`: its release ZIP SHA-256 matches the repository
manifest (`8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da`).
It is incorrect to call these three archives unavailable.

All archive members are ELF64/little-endian/x86-64 relocatables:

| Archive | Verified members | Origin |
| --- | --- | --- |
| libc++.a | 54 | SDK v0.42 |
| libc++abi.a | 18 | SDK v0.42 |
| libunwind.a | 9 | SDK v0.42 |
| libps5platform.a | 25 | Native Windows cross-build, pinned platform source |
| libclang_rt.builtins-x86_64.a | 167 | Native Windows cross-build, LLVM 18.1.8 |

Their hashes are measured in the package build provenance. A separate native
runtime preflight links/converts/signs with `_start`, real allocator/thread/
locale/assertion providers and unwind data; its package passes static checks.
It is not a driver. The user reported the heap/C++/thread/TLS checks and manual
close succeeding in hold mode; normal CRT termination remains unresolved.

The pinned Mesa source was restored and inspected:
`meson.build` requires Meson >=1.4.0, LLVM >=15.0 for CLC,
SPIRV-LLVM-Translator matching the chosen LLVM major/minor (for LLVM 18.1.8:
>=18.1 and <18.2), and SPIRV-Tools >=2024.1 plus native Clang libraries.
libclc is conditional on the selected frontend, not an unconditional dependency
of an install-only `mesa_clc` build. Windows shader cache must be disabled.

Native Windows MSVC built SPIRV-Tools 2024.4, SPIRV-LLVM-Translator 18.1
and both Mesa host tools using the isolated LLVM 18.1.8 development archive
and pinned Python dependencies. These host libraries are Windows COFF and are
never linked into PS5 binaries. LLVM's exported CMake metadata hardcodes its
original Visual Studio 2019 DIA path; the isolated extracted copy was adjusted
to the installed Visual Studio DIA library, without changing the source archive.
The pinned Windows pkgconf wheel contains an
incorrect internal Linux platform tag; only its hash-verified Windows executable
was extracted, not its Python metadata.

The unmodified Windows `mesa_clc` produced corrupted SPIR-V: it opens binary
output in text mode, producing a 721-byte unaligned test module and crashing
`vtn_bindgen2`. The isolated host source copy applies
`experiments/vulkan3d/ps5/patches/mesa-host-binary-spirv.patch`. The fixed tools
successfully compile and generate bindings for both a minimal kernel and the
actual pinned RADV geometry kernel; an independent LLVM translator decodes the
minimal output. Run `tests/vulkan_ps5_host_tool_tests.ps1` against the built
host tools. The pinned upstream source checkout remains unmodified.

Native Windows cross-compilation then produced a RADV archive with upstream
exception, RTTI, IEEE denormal and PS5 winsys options. All **689** members were
checked as ELF64 little-endian x86-64 relocatables; the ICD entry point and
PS5 winsys symbol exist. The local archive has 283,879,660 bytes and SHA-256
`1bb799b839df9cfe6d209ea86264e5e4301440fc512c19068e50d6ff5befede2`.
It is an experimental
local build, not a released driver or proof of runtime ABI compatibility.
The host tools and target were built manually; a clean, automated restoration
and build pipeline remains unfinished. Native setup and verbose build logs are
under `build/vulkan-ps5/logs`.

The initial target configuration exposed a real feature-probe issue:
SDK libc exports `qsort_s`, but the PS5 headers do not declare it. A link check
for BSD `qsort_r` succeeds only when bound to the pinned platform's
`ps5_qsort_r` and ELF compiler-rt. The resulting build selects the BSD path;
the same binding is required when linking an application. PS5 Clang also
requests a program named `prospero-lld`; the experimental native adapter invokes
the actual Windows `ld.lld.exe` and propagates its exit code.

The first whole-archive ICD link failed without undefined-symbol suppression.
The native unwind layout and the two GPL-licensed upstream AGC host-link stubs
resolve its unwind/AGC link boundary. The stubs identify system-module imports;
they are neither packaged nor executable substitutes. PS5 Clang's default
DLL-storage-class visibility hides the stubs' exports, so their isolated build
explicitly uses default visibility.

The pinned SDK exports `mkstemp` and `readlink` only through modules upstream
reports as null for titles; the pinned platform has neither alias. In the
audited archive, `mkstemp` is referenced only by optional external disassembly,
and `readlink` only by optional `/proc/self/fd` GPU-info printing. The link
script checks those exact archive members before binding `mkstemp` to the real
`ps5_mkstemps` with suffix zero, and `readlink` to an explicit `ENOSYS` failure.
No general filesystem or successful symlink behavior is claimed. The external
disassembler is not supported on PS5. Host tests check error propagation and
the failure result; neither known-incompatible module is imported.

An additional converter failure exposed a separate compiler issue. With
`-fvisibility-nodllstorageclass=default`, Windows LLVM's PS5 target emitted
Mesa's explicitly hidden weak dispatch declarations as **default** visibility:
6,743 unresolved weak symbols appeared as dynamic imports and the converter
rejected the first. A minimal compiler test reproduced the behavior. Building
the target with `-fno-visibility-from-dllstorageclass` respects Mesa's explicit
hidden attributes, so absent optional dispatch entries resolve to null rather
than becoming imports. It does not suppress undefined strong symbols. The new
archive contains 689 verified ELF64 little-endian x86-64 relocatables and has
local SHA-256 `207141d4ce63835e2ddfd855eaf6a32cc31becbe3c4047e9591d4fda1ea3662c`.
The diagnostic then links, converts, signs and passes static package validation
with five explicitly checked system modules. The operator reported successful
instance creation, physical-device enumeration, logical-device creation and
graphics-queue retrieval. A separate first-frame log reports successful submit,
fence wait and queue-present returns, and the operator confirms a visible
turquoise screen. A later interactive cube log reports graphics-pipeline,
pad and repeated presentation stages, and the operator confirms visible motion.
This does not establish textured rendering or clean shutdown. The separate
`dist/ps5-vulkan/PPSA91007` game now links the same RADV archive and the existing
game loop, with SDL controller/audio but without SDL video. Its package passes
static validation, and on 2026-11-22 the operator confirmed that it is fully
playable on PS5, including controls, audio, saving and clean shutdown. The
installed digest and setup were not provided.

The link recipe explicitly
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
| Driver archive | Whole-archive linkage required; optional weak dispatch declarations must retain hidden visibility. |
| CRT | Echo native `_start` and clean-room runtime differ from payload CRT assumptions; do not switch to `payload_main`. |
| C++ | RADV ACO needs libc++, libc++abi, libunwind; the Vulkan variant links the verified ELF archives, while the original SDL variant remains unchanged. |
| Linker | The experimental unwind layout and native converter passed static checks for the diagnostics; instance and device creation were reported on hardware. |
| Allocator | Platform wraps the malloc family to a direct-memory heap; all alloc/free domains must agree. Never blindly redirect the working SDL application. |
| Threads | Wrapped create/join/detach allocate 2 MiB default stacks in direct memory. |
| libc | Numerous `--defsym` libc replacements and local symbol map required; locale, memstream, TLS destructors, directory APIs and random APIs must resolve correctly. |
| GPU | PS5 winsys initializes AGC, direct memory, submission ring and cache management in the title. GPU allocations must not reuse SDL surfaces. |
| Imports | AGC/AGCDriver are system imports named by host-only stubs. Instance and device creation were reported on hardware; other imports and runtime paths remain untested. |
| Presentation | `VK_KHR_display` through VideoOut, not an SDL PS5 Vulkan surface. Do not initialize the old SDL VideoOut driver concurrently. |
| Synchronization | Upstream exposes queue families but some share the graphics ring; reported families do not imply real asynchronous compute. |
| Heap/mappings | Direct-memory heap and GPU virtual ranges require collision/reservation checks against the native app layout. |
| Size | The first-frame diagnostic FSELF is 26,063,562 bytes; submit/fence/present and a visible turquoise screen were reported on PS5. |

Sources:
[radv-link.sh](https://github.com/mihawk-99/PS5_Vulkan/blob/7f492b4c6f67a4e8c8c67a4272cfeef22abea9cc/tools/radv-link.sh),
[heap.h](https://github.com/mihawk-99/PS5_PayloadSDK/blob/b83202be73e930050e00de0f1b4d9ec46c0391bf/platform/include/ps5platform/heap.h),
[winsys](https://github.com/mihawk-99/PS5_Mesa/blob/1ba9874f478657ccf6da72ce55f0260164cc0bd0/src/amd/vulkan/winsys/ps5/radv_ps5_winsys.c).

## Licensing

No upstream driver/sample implementation or artwork is copied into Echo source.
The pinned GPL platform source is restored and compiled in an ignored isolated
dependency cache, and its code is statically included in the runtime preflight.
Original experiment code retains the repository MIT license. Windows uses
Khronos Vulkan SDK headers/loader, existing MIT yyjson and the already vendored
MIT/public-domain stb decoder.

PS5_Vulkan is GPL-3.0-or-later. PS5_PayloadSDK's platform layer carries GPL
notices and is statically linked by the preflight as well as the RADV recipe. Mesa is a mixed-license
distribution; its core and inspected PS5 winsys are MIT, but
[file-level SPDX and Mesa license documentation](https://github.com/mihawk-99/PS5_Mesa/blob/1ba9874f478657ccf6da72ce55f0260164cc0bd0/docs/license.rst)
must govern the actual archive, not GitHub's aggregate license badge.
Template base/starter/shadow/bloom/compute examples inspected carry MIT notices;
its optional UI module is GPL and assets have separate licenses. They are
reference material, not imported dependencies.

The preflight package includes project/boilerplate/SDK/LLVM and musl-regex
notices, the actual dlmalloc MIT-0 notice, and pinned build provenance.
LLVM compiler-rt/C++ runtimes use LLVM licensing; imported dlmalloc's wrapper
is GPL even though its allocator file is MIT-0. A distributed combined PS5
binary still requires evaluation and fulfillment of GPL corresponding-source
obligations; notices/provenance alone are not a complete source offer. Do not
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
