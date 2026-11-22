# Isolated baseline

- Branch: `experiment/vulkan-3d`. Never merge automatically into `main`.
- `git fetch origin` completed before branching on 2026-11-22.
- Actual starting HEAD: `1e90adc6519d8f278f856beaaac7f668287909bd`
  (`main`, `origin/main`), after the PS5 diagnostic hardware follow-up.
- Working tree was clean; there were no uncommitted changes or stashes to move.
- No existing experiment branch was present. Created with
  `git switch -c experiment/vulkan-3d`.
- Existing SDL renderer, gameplay, root CMake, framework default selection,
  production dependency manifest and PS5 pipeline are not changed.
- Builds: `build/vulkan-windows`, `build/vulkan-ps5`. Future PS5 packages:
  `dist/vulkan-experiment/PPSA91008` only.
- Production title `PPSA91007`, Hello `PPSA99999`; reserved experiment identity
  `PPSA91008`, content ID `UP9000-PPSA91008_00-ECHOVULKAN000001`.
  This is repository-local collision checking, not proof that this ID is unused
  on an operator's console. No experiment package exists yet.

## Recoverability

The original implementation is preserved at the baseline commit and on `main`.
Commit experiment work before switching back; never discard uncommitted work.
No force push, merge, deploy, console connection or installation is performed.
The following binary digests were recorded before experiment builds:

| Existing artifact | SHA-256 |
| --- | --- |
| `build/windows/project_echo.exe` | `339f758c22473383bede407f1b77004ca18dc4e128ebd841bbc911eab9901853` |
| `dist/ps5-controller/PPSA91007/eboot.bin` | `b4076b970cde91ca3f3ee96a65bf96a1dbcae22b5d08ed64397cbe532cebbef9` |

All experimental additions are opt-in through their independent CMake project.
Do not infer PS5 behavior from the Windows Vulkan driver.
