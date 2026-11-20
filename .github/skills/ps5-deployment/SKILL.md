---
name: ps5-deployment
description: Prepare safe, title-scoped PS5 staging and deployment only after a complete PS5 application artifact and loader layout are verified.
---

# PS5 deployment workflow

## Activate when

Changing FTP staging, ShadowMountPlus compatibility, title identity, or console log collection.

## Procedure

1. Read `docs/PS5_COMPATIBILITY.md` and `docs/STATUS.md`.
2. Require a complete validated title directory and a stable, project-specific title ID.
3. Keep transfer manual, as requested by the operator. The deployment script validates locally only.
4. Document the complete ShadowMount title directory; never overwrite system paths or unrelated titles.
5. Require manual launch/close and separately record hardware results.

## Project-specific lessons

- The stable game identity is `PPSA91007` (`UP9000-PPSA91007_00-PROJECTECHO00001`). Keep each build variant in its own output directory; do not place multiple copies of the same title ID in the loader scan root.
- The default deploy helper validates and prints manual-upload instructions; it does not connect to the console. Do not add automatic FTP, payload loading, or device configuration.
- The controller-only package is the variant reported playable on firmware 13.60 with Kraken Mount 0.3.1. Upload the complete application directory, including `eboot.bin`, `sce_sys`, and `sce_module/libc.prx`; do not upload only an ELF or binary.
- User-reported launch/playability is not full platform acceptance. Track remaining controller mappings/reconnect, audio, room progression, writable logging and exit/relaunch separately.

## Constraints

- Do not deploy or delete anything when the build artifact or target path is unverified.
- Do not store console addresses, credentials, or device-specific configuration in Git.
- Do not assume FTP is encrypted; use it only on a trusted local network after explicit user invocation.
- Do not automate payload loading, firmware changes, or system modifications.

## References and validation

- `scripts/build-ps5.ps1`, `scripts/deploy-ps5.ps1`, `scripts/collect-logs.ps1`
- `docs/PS5_COMPATIBILITY.md`, `docs/STATUS.md`
- Validate local title-directory structure before any future network operation.
