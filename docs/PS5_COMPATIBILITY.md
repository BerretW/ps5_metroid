# Native Windows PS5 compatibility

**Cross-compilation, linking and application-directory packaging: PASS. Hello and the `playable` milestone controller build launch: PASS (user-reported). The newer controller-symbol UI/map/back changes need another hardware run. The original keyboard-enabled build still fails on launch.**

Verified on 2026-11-20 using Windows 11 x64, PowerShell 5.1.26100.9818, official Windows LLVM/LLD 18.1.8, CMake 4.4.4, Ninja 1.13.2 and native MSVC 19.51.36252 host utilities. No PS5 binary was executed on Windows. The working Windows executable and shared gameplay implementation were preserved.

## Reproduce

```powershell
.\scripts\build-ps5.ps1 -Target Hello -RestoreDependencies
.\scripts\build-ps5.ps1 -Target Game -RestoreDependencies
.\scripts\build-ps5.ps1 -Target Game -SkipPs5Keyboard
.\tests\ps5_package_tests.ps1
.\scripts\deploy-ps5.ps1
```

The game command completes and validates standalone Hello before configuring SDL or compiling PROJECT ECHO. Restore verifies pinned archive SHA256 values and clean Git commits. LLVM is extracted under `.deps`, not installed globally. [dependencies.ps5.json](../dependencies.ps5.json) records the exact SDK, LLVM, host zlib and source pins.

## Toolchain comparison: not interchangeable

| Source | Reviewed pin | Actual files and compatibility |
| --- | --- | --- |
| [Native app boilerplate](https://github.com/drakmor/ps5-native-app-boilerplate) | `470695e0c557f99ff2df0e36e4df713c5e636526` | `tools/build.sh`, `tooling/prospero-clang18`, `tooling/native/ps5-pie.ld`, `app_crt.cpp`, `app_cpp_runtime.cpp`, `native_app_builder.cpp`, `sce_module_writer.cpp`, `self_container.cpp` and runtime manifests. Selected application ABI/packager, with the verified RELRO fix below. |
| [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk) | source `4122b714c7e7a9449d403812b9d640bf7a55af3f`; binary **v0.42** | Inspected `Makefile.inc`, `host/bin/prospero-lld`, `host/win` wrappers, `host/toolchain/prospero.cmake` and payload CRT. The v0.42 binary's actual wrappers/headers/stubs were also inspected: the newer source revision is not assumed identical to this release. Only compatible target headers and import stubs are consumed. |
| [PS5Dev/PS5SDK](https://github.com/PS5Dev/PS5SDK) | `a2e03a2a0231a3a3397fa6cd087a01ca6d04f273` | `cmake/toolchain-ps5.cmake` targets `x86_64-freebsd-pc-elf`; CRT `__ps5sdk_crt_start` invokes `payload_main(struct payload_args*)` using loader-supplied dlsym/runtime arguments. Its README explicitly limits it to payloads rather than full applications and does not support STL. Not substituted. |
| [PS5 SDL fork](https://github.com/ps5-payload-dev/SDL) | `ee4c47dc0d617b3bc8f35108f9956baf228a1322` | SDL 2.30.12. Actual PS5 CMake branch, SDL2main, filesystem, audio, joystick, framebuffer, pthread and loadso implementations inspected. Built from source with the narrow native-app patch described below. |
| [PacBrew repository](https://github.com/ps5-payload-dev/pacbrew-repo) | recipe source `722187f0ee246c0d7d57401c8a97de1c7f4e2ff0`; binary snapshot **v0.40.2** | SDL recipe uses unpinned Git head plus a Mesa patch and enables OpenGL/GLES/Vulkan/loadso. Prebuilt SDL references `getargv`, dlopen/dlsym and OS-specific APIs; it is not used. Verified snapshot and libsamplerate 0.2.2 archive inspected, then unnecessary PacBrew dependencies removed from this software-only build. |

The selected native CRT `_start(void *process_parameters, Destructor loader_teardown)` initializes the environment, runs constructors, calls `main`, and registers teardown. This is not the payload SDK CRT, which initializes payload syscall/kernel/rtld state from a payload argument structure. Both produce x86-64 code, but matching architecture alone does not make their startup contracts or loaders compatible.

### Executable formats

- Clang/LLD intermediate: generic ELF64 `ET_DYN` PIE, target `x86_64-sie-ps5`, SysV x86-64 calling convention and Itanium C++ ABI.
- Converted application: SCE executable ELF type **0xFE10**, FreeBSD OSABI 9, ABI version 2, nonzero native `_start`, process-parameter segment and SCE module/import records.
- Application launcher: plaintext developer FSELF `eboot.bin`, magic **0x1D3D154F**, developer authority `0x3100000000000002`. This is homebrew wrapping, not a retail Sony signature.
- Packaged runtime: clean-room SCE module type **0xFE18**, wrapped as `sce_module/libc.prx`.

`build/ps5/game/project_echo_ps5.llvm.elf` is **not** a deployable payload. `build/ps5/game/eboot.elf` is the unwrapped native SCE application, not a replacement for either SDK's payload-loader executable. For native application-directory loading, manually transfer the complete title directory containing `eboot.bin`.

## Exact blocker inventory

The initial blanket assertion that Bash scripts made native Windows compilation impossible was incorrect.

| Dependency / operation | Finding and native replacement |
| --- | --- |
| Boilerplate `build.ps1`, setup/doctor/rebuild scripts | These explicitly launch WSL. Their canonical Bash scripts use `find`, `sort`, `cp`, `sha256sum`, `realpath`, Make and shell arrays. Replaced by PowerShell, native CMake/Ninja, filesystem APIs and `Get-FileHash`; WSL wrappers never run. |
| SDK `bin/prospero-nid` | Actual shipped ELF64 x86-64 host executable, not runnable on Windows. Not invoked: the native converter has its own NID hashing. |
| SDK `bin/clang`, `ld.lld`, `prospero-*` | Most are Bash wrappers/discovery scripts, not Linux compilers. SDK source builds assume POSIX Make/Bash, but the prebuilt target sysroot is host-independent. Official Windows `clang++.exe` and `ld.lld.exe` are invoked directly. |
| SDK Windows wrappers | `.cmd` compiler/CMake wrappers and `prospero-lld.exe` already exist. The source linker wrapper uses `unistd.h`/`libgen.h`, MinGW-oriented compilation and weak command quoting. These defaults also inject payload CRT/libraries, so they are not used for native applications. |
| Host C++ converters and runtime builder | Standard C++20 plus zlib, not inherently Linux-only. Compiled as Windows PE/COFF executables with native MSVC. Their host zlib is independently source-built for Windows. |
| Target headers and libraries | v0.42 `target/include`, `target/include/c++/v1` and compatible `target/lib/*.so` ELF import stubs. The `.so` files describe system symbols; they are not copied into the title or executed on Windows. |
| Target CRT / runtime | Boilerplate `app_crt.cpp`, its minimal allocation bridge, system runtime imports and reproduced clean-room libc module. SDK payload `crt1.o`, `libc.a`, `libc++.a`, `libc++abi.a` and `libunwind.a` are not linked into the final application. |
| Host-specific compiler runtimes | Linux `libclang_rt` archives and Windows COFF compiler-runtime `.lib` files are not PS5 target libraries. No such archive is linked. The final target has no unresolved emulated-TLS, stack-check, C++ exception or payload-loader helper imports. |
| SDL2 dependencies | Software SDL, PS5 VideoOut, Pad/UserService, AudioOut, Keyboard/ImeDialog/SystemService and kernel pthread/timer APIs. GL/GLES/Vulkan/HIDAPI/loadso are disabled. SDL's unused filesystem backend is disabled because its `getargv` contract is payload-specific. |
| PacBrew static archives | Inspected SDL and libsamplerate ELF64 x86-64 objects. Libsamplerate references ordinary allocation/memory functions and no loader ABI, but is unnecessary when the existing option is OFF. No prebuilt SDL, Mesa, PacBrew host executable or package manager is required. |
| ELF / FSELF packaging | Native LLD PIE -> native converter SCE ELF -> native FSELF signing/wrapping -> title directory. PowerShell builds the title tree and original PROJECT ECHO icon. |
| Optional `.ffpkg` / `.ffpfsc` | Upstream setup invokes .NET UFS2Tool or Python MkPFS through Linux-named shell launchers. The scripts assume POSIX; this alone does not establish Linux-only binaries. These image formats and tools are outside the requested application-directory output and are not installed or claimed validated. |

### Exact compiler/linker configuration

[cmake/ps5-windows.cmake](../cmake/ps5-windows.cmake) sets the PS5 target, SDK sysroot, native Windows LLVM paths and target archiver. C++ headers precede C headers; `-nostdinc++` prevents host C++ headers leaking into the cross-build.

Compile flags: `--target=x86_64-sie-ps5`, `-fvisibility-nodllstorageclass=default`, SDK `-isystem` directories, `-fno-stack-protector`, `-fno-plt`, `-femulated-tls`, C++20, `-O2`, `-Wall -Wextra`, `-ffunction-sections -fdata-sections`, `-fno-exceptions -fno-rtti`.

Direct LLD flags: `-m elf_x86_64 -pie --eh-frame-hdr -z max-page-size=0x4000 -mllvm -emulated-tls --hash-style=gnu`, upstream `ps5-pie.ld`, `app-symbols.map`, entry `_start`, and `--as-needed` for an explicit native system-stub allowlist.

Converter constants: module SDK `0x02000009`, companion SDK `0x08050001`, original file name `eboot.elf`. Native response files avoid Windows command-length limits. CMake internally normalizes paths so Clang's GNU-style response parser does not consume Windows backslashes as escapes. Rpaths are disabled, including cached try-compile settings.

## Identified fixes, without gameplay changes

1. **Native converter RELRO bug:** upstream used `got.file_offset` as the file origin of a segment starting at `relro_start`. Hello's empty `.data.rel.ro` hid this. SDL placed the GOT 0x2D50 bytes after RELRO began, producing mismatched file/virtual offsets and wrong process-parameter placement. The host CMake build generates a patched copy using `got.file_offset - (got.address - relro_start)`, with an underflow guard. Pristine upstream source is preserved. Static segment checks caught the real failure; they were not weakened.
2. **SDL dynapi include-order bug:** without payload dlopen, different include order selected `_REAL` exports in one translation unit and disabled their public wrappers in another. The build-local patch consistently disables dynapi on PS5 native static applications.
3. **SDL framebuffer contract/ownership bug:** upstream left format, pixels and pitch outputs unset and stored its allocation in SDL's public `window->surface`. The patch returns all three outputs, uses 32-bit pixels and keeps driver-owned storage in `window->driverdata`, avoiding the lost allocation/double-free contract.
4. **Unconditional samplerate dependency:** the PS5 CMake branch linked libsamplerate even with `SDL_LIBSAMPLERATE=OFF`. The patch respects that existing option; audio remains enabled with SDL's built-in resampling. No PacBrew build dependency remains.
5. **Real feature checks:** SDL's compile-only CMake probes incorrectly enabled declared-but-unexported BSD functions such as `wcslcpy`. Probes now link against selected target stubs, without execution. Excluding WebKit-only stubs also makes SDL use its own `strcasestr` fallback instead of importing `libScePosixForWebKit`.
6. **Application platform portability:** full libc++ iostream/filesystem/localization produced actual unresolved target symbols, including `__runes_for_locale`. PS5-only adapter branches use stdio logging under `/data/project_echo/logs`, bounded key-name copies and percentage formatting, plus software rendering. Windows behavior and all shared gameplay systems remain unchanged.
7. **Git revision in copied SDL:** the patched SDL source copy has no `.git`; after the project became a Git repository, SDL mistakenly embedded the parent project's commit as its own revision. The PS5 CMake configuration disables SDL's optional Git discovery so it uses `SDL-2.30.12-no-vcs`, as it did for the hardware-tested build. The pinned source revision remains in [dependencies.ps5.json](../dependencies.ps5.json); the Windows SDL checkout is unaffected.

[patches/sdl2-native-ps5.patch](../patches/sdl2-native-ps5.patch) is applied to a hash-keyed build-local SDL copy. No patch is applied to the Windows SDL checkout. No new gameplay feature was introduced.

## Package and runtime evidence

Stable homebrew identity: **PPSA91007**, content ID **UP9000-PPSA91007_00-PROJECTECHO00001**, title **PROJECT ECHO**, version **01.000.000**. This is a project-local homebrew identity, not a retail entitlement.

```text
dist\ps5\PPSA91007\
  eboot.bin
  sce_sys\param.json
  sce_sys\icon0.png
  sce_module\libc.prx
  assets\sprites\*.png
  assets\sounds\*.wav
  assets\framework\game.json
  licenses\PROJECT_ECHO.txt
  licenses\boilerplate.txt
  licenses\SDL2.txt
  licenses\stb.txt
  licenses\yyjson.txt
```

The game icon, sprites and sounds are original and generated by `scripts\generate-assets.ps1`. At runtime they are read from `/app0/assets/` through `SDL_RWFromFile`; PNGs are decoded by the pinned stb_image (PNG-only, no stdio, no thread-local storage, SDL allocator, 4096 px / 4 MiB limits) and WAVs by SDL. `test-ps5-package.ps1` checks each packaged asset's signature, PCM format and size. Missing graphics/audio retain their existing fallback; missing or invalid framework configuration is a logged startup failure. The standalone `dist/ps5-hello/PPSA99999` uses the untouched upstream graphical Hello source and its presentation assets solely for that milestone.

Echo Framework adds static core/data libraries and yyjson 0.13.0 without changing SDK, `_start`, CRT, executable format, SDL patch or title identity. The SDK libc++ instantiated `std::span<const Condition>` copy constraints before the recursive condition type was complete; declaring its constructors/assignment in the header and defaulting them after type completion resolves that verified compiler failure without changing gameplay. Both PS5 game variants now compile/link/package; the controller build retains the same seven system modules and libc hash. New JSON parsing on the console is **NOT TESTED**, and no PS5 save-storage provider is enabled. See [current evidence](STATUS.md#echo-framework-verification).

The required system modules are kernel, SceLibcInternal, SystemService, VideoOut, Pad, UserService, AudioOut, Keyboard and ImeDialog. Their exact `.prx` dependency names are recorded in the validation report. Only the clean-room libc module is bundled; console system modules are **not** redistributed.

Raw clean-room runtime SHA256: `8ee6e124993e1af26420cb455890fd002f5d6c7e78883c860ce45734e7d002bb`. Signed SHA256: `e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036`, **1,284,674 bytes**. Two independent native Windows builds match both exact upstream hashes.

Validation checks ELF class/endianness/machine/SCE type/entry, segment bounds, file/memory sizes, 16-KiB load congruence, executable entry mapping, process parameters, native module imports, FSELF integrity, runtime hashes and package identity/files. Upstream `self --inspect` can return zero while printing `INVALID`; the script explicitly requires `integrity: valid`. Runtime SELF omits the original debug section table: extraction strips section-header references for LLVM inspection, after validating the untouched container digest.

Focused tests reject a corrupt container digest and a newly wrapped, integrity-valid ELF with a misaligned load segment. The complete Windows Release regression build and headless gameplay suite also pass.

Evidence: `build/ps5/logs/commands.log`, `host-tool-format.stdout.log` (native PE/COFF), `clang-version.stdout.log`, `hello-build.stdout.log`, `game-build.stdout.log`, `*-readelf.stdout.log`, runtime/converter/signing logs, `PPSA91007-validation.json`, `PPSA99999-validation.json`, and `windows-regression.log`. Exact compiler/linker response files and `compile_commands.json` remain under the corresponding build directories. Earlier link/RELRO failures are retained for diagnosis.

## Controller build and manual upload

The operator explicitly requested a non-diagnostic PS5 controller build without physical keyboard or IME support. Build with `.\scripts\build-ps5.ps1 -Target Game -SkipPs5Keyboard`. This opt-in uses [patches/sdl2-ps5-no-keyboard.patch](../patches/sdl2-ps5-no-keyboard.patch), without changing Windows or shared gameplay. It keeps stable title/content identity and stores output separately in [dist/ps5-controller/PPSA91007](../dist/ps5-controller/PPSA91007). No `sceKeyboard*` or `sceImeDialog*` imports or startup diagnostic markers are present in its LLVM ELF. The original build and user-tested diagnostic are separate variants; generated outputs are not tracked in Git.

Hardware-tested `playable` milestone controller `eboot.bin`: **1,151,105 bytes**, SHA256 **`eff47dcc2a39b3e7b92e226d9851fafdb6078055763c6d50221e7af8072d70bd`**. The operator confirmed on 2026-11-20 that it launches and is playable on firmware 13.60 / Kraken Mount 0.3.1. The supplied screenshot shows Landing Bay gameplay with the player, platforms and HUD. This is operator-provided hardware evidence, not execution by the Windows build host. A local copy was retained in `dist/ps5-controller-playable/PPSA91007`; generated packages are not tracked in Git.

Current sources use PlayStation bitmap symbols in the main menu, options, gameplay hints, door prompt and pause screen. The fixed face-button bindings are unchanged; touchpad toggles the map and circle returns from options or a paused game. `build.bat` option **P** selects this controller build. These changes need another console test; the milestone's hardware PASS does not automatically validate the new executable.

The updated controller package built through the batch selector and passed native Windows static validation: `eboot.bin` **1,152,065 bytes**, SHA256 **`7ddf8c7afcac1395d59b1c61b41ce67a5a876750b86365487285971037ff6279`**. Required modules remain kernel, LibcInternal, SystemService, VideoOut, Pad, UserService and AudioOut, with no keyboard/IME imports. Windows gameplay and offscreen prompt tests pass; this updated binary has not been run on PS5.

The operator confirmed manual transfer and clarified the actual loader is **Kraken Mount 0.3.1**, using native application directories. [scripts/deploy-ps5.ps1](../scripts/deploy-ps5.ps1) defaults to the controller package, validates locally and prints instructions only: no FTP connection, staging copy, deletion or console overwrite occurs. Transfer the complete title tree into the location required by the installed loader; do not replace unrelated titles or console system files.

ShadowMountPlus documentation reviewed at `3530d8d88ea1fa0d907545eda7ac95d4240a0213` warns about storage/mounting risks. No console mount, transfer or launch was performed here. Compatibility with a particular firmware/loader version is not established by static packaging.

Remaining PROJECT ECHO hardware checks: complete controller mappings and reconnect, AudioOut playback, normal writable log path, five-room gameplay/progression and exit/relaunch. Launch and in-game rendering are confirmed for the approved controller build, not the original keyboard-enabled build. Removing keyboard/IME is a verified workaround on this reported setup; the exact underlying keyboard-path fault remains undiagnosed.

## Hardware crash investigation: Kraken Mount 0.3.1

The operator reports firmware **13.60** with **Kraken Mount 0.3.1**:

- Standalone Hello World launches and displays Hello World.
- PROJECT ECHO registers and shows its icon, but immediately crashes with **CE-108255-1** and produces no application log.
- The first diagnostic's startup log shows `01`, `02`, `03` twice and no later marker: SDL core returned, and the next call is video initialization. The checkpoint itself still performs `fsync` and close after writing `03`, so those operations cannot be excluded. The precise failing operation and a crash PC/backtrace are not yet known.
- The subsequent detailed-video diagnostic reached `03h: before PS5 keyboard init`; `03g` remote initialization returned, but its result was not recorded. No `03i` was observed. This narrows the next investigation to the keyboard initialization call or checkpoint completion; the exact API and cause are not established.
- The keyboard diagnostic logged `03h2: user service result 0x80960003` and `03h3: before sceKeyboardInit`, with no `03h4`. The SDL fork explicitly treats that user-service code as already initialized. Failure during `sceKeyboardInit` is a strong lead, but checkpoint completion, symbol binding and loader/runtime behavior are not excluded.
- The operator reports the separate no-keyboard diagnostic launches and is playable. Its log reaches `03h-skip` and `12: first frame rendered`, including video, controller initialization, renderer setup and an audio initialization attempt. It does not establish that audio playback or every controller, gameplay, filesystem or shutdown path works.
- The normal local package remains SHA256 `a62fcdd17e127686e8aaed19d6f40bf3da1c0c44ebba475a6e8c6cd228c746ac`.

CE-108255-1 is a generic application-crash report, not a diagnosis. The normal logger first runs **after combined video/controller `SDL_Init`**, so its absence does not distinguish pre-main failure from SDL initialization failure. The successful Hello narrows the investigation but does not prove all extra game imports, relocations or SDL/runtime functions work.

Reviewed the exact Kraken **v0.3.1** tag, commit `387e665f6372e1a11ae834c52e6f77ce99b2012b`: [README](https://github.com/quer3q/kraken-mount/blob/387e665f6372e1a11ae834c52e6f77ce99b2012b/README.md), [launch.rs](https://github.com/quer3q/kraken-mount/blob/387e665f6372e1a11ae834c52e6f77ce99b2012b/src/launch.rs), [cache.rs](https://github.com/quer3q/kraken-mount/blob/387e665f6372e1a11ae834c52e6f77ce99b2012b/src/cache.rs). Folder applications use their source directory as app0; cached fakelib comes from `fakelib2`/`fakelib` plus configured emulator modules. The launch overlay also exposes `/data` and attempts writable `/app0`; filesystem availability is therefore not guaranteed merely by icon registration. No loader, kernel, runtime format, SDK or fakelib changes were made on this evidence.

The supplied Kraken log records sandbox overlay and `/data` mounting followed by process exits for multiple launches, without identifying a crash PC or SDL function. One earlier attempt reported `/data` mount busy; other attempts mounted it successfully. Timestamps alone do not establish which recorded launch matches the diagnostic log, and neither finding identifies the crash cause.

### Manual diagnostic test

```powershell
.\scripts\build-ps5.ps1 -Target Game -StartupDiagnostics
.\scripts\test-ps5-package.ps1 -PackagePath .\dist\ps5-diagnostic\PPSA91007 -LogDirectory .\build\ps5\logs\startup-diagnostic
Get-FileHash .\dist\ps5-diagnostic\PPSA91007\eboot.bin -Algorithm SHA256
```

The diagnostic package is [dist/ps5-diagnostic/PPSA91007](../dist/ps5-diagnostic/PPSA91007). It retains the title/content identity and runtime, while the normal PS5 output and original Windows executable remain separate. Logs and native compiler/linker commands are isolated under [build/ps5/logs/startup-diagnostic](../build/ps5/logs/startup-diagnostic).

Verified keyboard diagnostic `eboot.bin`: **1,160,098 bytes**, SHA256 **`26395ea4ba3af5f09e9c7364ed07fcaa382067b3dfe7836ec8c6d8e6e9a5d531`**. This supersedes the detailed-video diagnostic (`f9dec7f8...`) and first diagnostic (`cb465f5f...`). Rebuilding without `-StartupDiagnostics` previously reproduced the original normal PS5 package byte-for-byte.

To isolate the keyboard call without changing the regular build:

```powershell
.\scripts\build-ps5.ps1 -Target Game -StartupDiagnostics -SkipPs5Keyboard
```

The experimental package is [dist/ps5-diagnostic-no-keyboard/PPSA91007](../dist/ps5-diagnostic-no-keyboard/PPSA91007); its `eboot.bin` is **1,155,889 bytes**, SHA256 **`053b3f1da79b6854b0b6cafd594a8a961198ae6b8767049c01d35b77ff5837f6`**. Its evidence is under [build/ps5/logs/startup-diagnostic-no-keyboard](../build/ps5/logs/startup-diagnostic-no-keyboard). The build intentionally disables PS5 physical keyboard and IME initialization, event pumping and screen keyboard callbacks; SDL controller and remote paths are not intentionally changed. Static inspection shows no `sceKeyboard*` or `sceImeDialog*` imports in this variant. Since removing those imports also changes loaded modules, a different result alone does not prove which call failed. This is a temporary diagnostic, not a feature-complete build or verified fix.

1. Close PROJECT ECHO. Back up only its existing title directory if needed, then manually replace that title with the complete diagnostic directory. Do not install two scan-root copies of `PPSA91007` simultaneously.
2. Ensure Kraken is registering that source directory rather than another copy. Do not purge unrelated caches or replace console libraries. If possible, verify the transferred `eboot.bin` hash against the diagnostic validation report.
3. Launch once and note the **last numbered ECHO DIAG notification**. Kernel notifications use the upstream Hello request layout; tracing avoids SDL logging, stdio, heap allocation and errno.
4. Retrieve `/data/project_echo-startup.log`. If `/data` cannot be opened, a notification explicitly reports a fallback to `/app0/project_echo-startup.log`, located in the installed title directory when app0 is writable. If neither is writable, report the numbered stage notifications; log failures are also notified. Logs append across launches; use the last `01: main reached` run.
5. Collect relevant `PPSA91007` entries from `/data/krakenmount/kraken.log` and any available crash PC/backtrace.

Stages: **01** main entry; **02** before SDL core; **03** core ready/before video; **03a–03e** events, keyboard, mouse, touch and driver selection; **03f–03j** PS5 device, remote and keyboard setup; **03h1–03h5** keyboard user-service and `sceKeyboardInit` calls, hexadecimal return codes and error reporting; **03k–03l** SDL video setup and driver initialization; **03m–03u** VideoOut, direct memory, event queue, buffer registration, surface, tilemap and display setup; **03v–03w** driver and SDL video completion; **04** video ready/before controller; **05** controller ready/before normal logger; **06** before renderer; **07** renderer ready/before logger; **07a** before audio; **08** audio attempt completed/before game state; **09** controller enumeration ready/before clock; **10** before event polling; **11** before first render; **12** first frame rendered. No `01` means main entry was not observed; it is not proof of a particular loader/CRT fault.

Diagnostic-only initialization uses separately traced SDL core, video and controller calls. A reported initialization error records `SDL_GetError` and stays in-process for collection; close it manually from the console. This avoids returning through the native teardown path, which upstream Hello itself deliberately does not exercise. Audio retains the existing optional-audio behavior. Neither a held error process nor successful static validation is a game-launch PASS.

The no-keyboard diagnostic and non-diagnostic controller executables have compiled, linked, converted and passed static package validation on native Windows. Windows adapter rebuild/gameplay regression passed. The operator reports both launch and are playable on hardware; the exact keyboard-path crash root cause and complete game acceptance remain unverified.

### Save-storage diagnostic (first console probe failed)

The PS5 save provider is included only in a dedicated opt-in build. Rebuild and validate it with:

```powershell
.\scripts\build-ps5.ps1 -Target Game -StartupDiagnostics -SkipPs5Keyboard -SaveStorageDiagnostics
.\scripts\test-ps5-package.ps1 -PackagePath .\dist\ps5-save-diagnostic\PPSA91007 -LogDirectory .\build\ps5\logs\save-storage-diagnostic
Get-FileHash .\dist\ps5-save-diagnostic\PPSA91007\eboot.bin -Algorithm SHA256
```

The resulting separate package is [dist/ps5-save-diagnostic/PPSA91007](../dist/ps5-save-diagnostic/PPSA91007), title ID `PPSA91007`, `eboot.bin` SHA256 `865de685076ce98ec1b1af5f3763733d50386de612ebcf92ed8c4f3546e9c044`. It has passed static package validation on Windows. The operator's 2026-11-21 log for the preceding diagnostic reported parent-directory initialization failure before slot writes; gameplay startup continued. Because the logger's identical `mkdir` on that path succeeds, the suspect is the `lstat` status check (the clean-room runtime itself only relies on `sceKernelStat`). The revised provider tries `lstat` first and falls back to `open(O_NOFOLLOW)`+`fstat`, which still rejects symlinks. Every run now logs a `PS5 save storage probe detail:` line naming the method used, `mkdir`/`lstat`/`open` errno values and directory mode; later write/replace/read/enumerate failures log the failing step and errno. The operator's second console run confirmed `lstat` returns EPERM (1) in the app sandbox while the `open+fstat` fallback works; write, replacement and read-back passed, but `opendir` also returned EPERM. A third run showed a 4 KiB `getdents` on that descriptor fails with EINVAL (22). Enumeration now tries ordered, bounds-checked 64 KiB variants (`getdents`, `getdirentries`, `sceKernelGetdents`, `sceKernelGetdirentries`, then plain-open fallbacks verified by `fstat`) and logs either `enumerate: method=...` or each attempt's errno. The operator's fourth console run (2026-11-21) of this package reported `enumerate: method=getdents64k` and `PS5 save storage probe: PASS`: directory initialization via `open+fstat`, write, `rename` replacement, read-back and enumeration all work in the app sandbox. This is a single operator-reported run of the diagnostic path; the provider is not yet wired to gameplay saves. It uses the controller-only diagnostic path. As with other same-title packages, replace the intended app directory rather than registering multiple `PPSA91007` copies.

On launch, the probe initializes `/data/project_echo/saves`, writes and replaces the reserved `framework_probe.sav`, loads the replacement and checks that enumeration includes the slot. The expected success line is `PS5 save storage probe: PASS; reserved slot framework_probe.sav remains for inspection` in `/data/project_echo/logs/project_echo.log`. Collect the log after the console run. The diagnostic deliberately leaves that one reserved save file; it does not clean up or overwrite unrelated save slots. A failure is reported with the exact stage in the same log. Do not treat package validation, app launch, or the pre-existing use of `/data` for logs as evidence that save persistence passed.

Original PROJECT ECHO code is licensed under the [MIT License](../LICENSE). The PS5 binary also combines the game with the upstream GPL-3.0-or-later CRT/runtime. Before redistributing binaries, satisfy its applicable license and source obligations and those of other dependencies. The package includes license notices, but those notices and local compilation alone are not a redistribution/license-compliance certification.
