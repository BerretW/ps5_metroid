# Native Windows PS5 feasibility

Status: **BLOCKED**. There is no PS5 Vulkan executable, FSELF or installable
experiment directory. Existing PROJECT ECHO remains untouched.

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

Without `-Probe`, the script fails explicitly and writes the blocker report.
It never invokes the production PS5 build, packages an incomplete title, changes
console files or connects to a console.

## Reproducible adaptation sequence

1. Acquire the exact source pins in `experiments/vulkan3d/dependencies.json`
   into an isolated cache, verify Git object IDs and retain license files.
2. Prefer a published RADV archive only when its source/SDK pins, full dependency
   set, license terms and independently measured SHA-256 can be verified.
   None was available at audit time.
3. Otherwise port the small build orchestration to PowerShell. Keep Meson's
   build description; use native Windows Ninja/Python/Mako and cross-file
   compiler paths. Build matching native `mesa_clc`/`vtn_bindgen2` dependencies
   before configuring target Mesa with `radv-winsys=ps5`, `mesa-clc=system`.
4. Build and inspect ELF builtins, libc++, libc++abi, libunwind and the pinned
   platform library. Inventory archive members and undefined symbols with the
   existing Windows `llvm-ar`/`llvm-nm`/`llvm-readelf`. Do not link COFF objects.
5. Resolve heap-domain, thread-stack, emulated TLS and libc rebinding with the
   native `_start` runtime. Preserve localized symbols and unwind data. No
   allocator fallback should be mistaken for proof that the combined runtime works.
6. Implement a separate native RADV `VK_KHR_display` surface adapter. Never
   initialize SDL video or keyboard. Enumerate actual formats, display modes,
   queues and capabilities; start at 1280x720.
7. Link a minimal Vulkan title before adding the reusable renderer; validate
   imports, 16 KiB load segments, converter records, FSELF integrity, shader
   assets and notices.
8. Package only in `dist/vulkan-experiment/PPSA91008`, using coordinated metadata
   from `experiments/vulkan3d/ps5/identity.json`, a distinct launcher icon and
   `/data/echo-vulkan-lab` logs/save path. Confirm this identity is unused on the
   actual console before manual installation.
9. Run the hardware checklist. Successful static validation alone is Stage C,
   never Stage D.

No mandatory WSL, Linux host, MSYS2, container, VM or proprietary SDK is introduced.
Missing dependencies are documented in [VULKAN_COMPATIBILITY.md](VULKAN_COMPATIBILITY.md).
