# Echo Vulkan 3D experiment

Independent Windows 3D experiment, reusable SDL-free rendering interfaces, and
an opt-in PS5 Vulkan game candidate. PROJECT ECHO's working SDL software build
remains available as the default.

| Acceptance stage | Scope |
| --- | --- |
| A | Real Windows GPU scene; see RESULTS for measured evidence |
| B | Reusable bounded mesh/material/camera interface, metadata and basic sprite path; limitations in architecture/material/VFX guides |
| C | PS5 Vulkan game link/package: STATIC PASS |
| D | Actual Echo Vulkan game output/relaunch: NOT TESTED |

Read [BASELINE](BASELINE.md), [WINDOWS_BUILD](WINDOWS_BUILD.md),
[VULKAN_COMPATIBILITY](VULKAN_COMPATIBILITY.md), [PS5_BUILD](PS5_BUILD.md),
[RENDERER_ARCHITECTURE](RENDERER_ARCHITECTURE.md), [SHADERS](SHADERS.md),
[MATERIALS](MATERIALS.md), [VFX](VFX.md), [HARDWARE_TESTS](HARDWARE_TESTS.md)
and [RESULTS](RESULTS.md). Dependency research pins are machine-readable in
`experiments/vulkan3d/dependencies.json`.

Unsupported features are explicit, never advertised as working. Static
compilation is not hardware proof. No automatic uploads or console connections.
