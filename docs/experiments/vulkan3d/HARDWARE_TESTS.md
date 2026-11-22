# Manual PS5 acceptance

All Vulkan console checks below are **NOT TESTED**; no candidate PS5 package
exists. Upstream tests and PROJECT ECHO's software isometric room are not
substitutes. Deployment is manual only.

Before copying any files, verify the candidate digest/static report and confirm
`PPSA91008` is unused on the console. Do not overwrite `PPSA91007`, system
libraries, other titles, or running files. Keep the original game independently
installed. Record console model, firmware, loader version, resolution and hashes.

| Check | Evidence required | Status |
| --- | --- | --- |
| Original game still launches | Independent `PPSA91007` launch before/after | NOT TESTED |
| Experiment launch | Distinct title and structured startup log | NOT TESTED |
| RADV initialization | Driver/API version, queues/extensions, no SDL VideoOut startup | NOT TESTED |
| Native runtime | Heap statistics, thread creation, AGC/direct memory success | NOT TESTED |
| Pipeline and resources | Shader/pipeline/buffer/depth allocation log | NOT TESTED |
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
