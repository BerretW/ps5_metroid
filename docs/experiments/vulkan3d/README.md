# Echo Vulkan 3D experiment

Experimental branch only: `experiment/vulkan-3d`. Independent build project,
reusable SDL-free rendering interfaces and a real Vulkan backend. PROJECT ECHO's
working SDL software renderer is not replaced or migrated.

| Acceptance stage | Scope |
| --- | --- |
| A | Real Windows GPU scene; see RESULTS for measured evidence |
| B | Reusable bounded mesh/material/camera interface, metadata and basic sprite path; limitations in architecture/material/VFX guides |
| C | PS5-compatible Vulkan application link/package: BLOCKED |
| D | Actual Echo Vulkan console output/relaunch: NOT TESTED |

Read [BASELINE](BASELINE.md), [WINDOWS_BUILD](WINDOWS_BUILD.md),
[VULKAN_COMPATIBILITY](VULKAN_COMPATIBILITY.md), [PS5_BUILD](PS5_BUILD.md),
[RENDERER_ARCHITECTURE](RENDERER_ARCHITECTURE.md), [SHADERS](SHADERS.md),
[MATERIALS](MATERIALS.md), [VFX](VFX.md), [HARDWARE_TESTS](HARDWARE_TESTS.md)
and [RESULTS](RESULTS.md). Dependency research pins are machine-readable in
`experiments/vulkan3d/dependencies.json`.

Unsupported features are explicit, never advertised as working. Static
compilation is not hardware proof. No automatic uploads or console connections.
