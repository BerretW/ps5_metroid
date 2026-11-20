# Assets

All sprites and sounds are original and created procedurally by [`scripts/generate-assets.ps1`](../scripts/generate-assets.ps1). The generated files are committed so a normal build does not need to run the generator.

```powershell
.\scripts\generate-assets.ps1
.\scripts\build-windows.ps1 -Configuration Debug -RunTests
```

## Sprites (`assets/sprites`)

Sheets are horizontal strips of equal frames, RGBA PNG. The renderer checks height and frame width when loading; a sheet with wrong dimensions is rejected and the built-in rectangle art is used.

| File | Frame | Frames |
| --- | --- | --- |
| `player.png` | 24x32 | idle 0–1, run 2–5, jump 6, fall 7 (drawn facing right, flipped for left) |
| `enemy.png` | 32x32 | 2-frame crawl |
| `tiles.png` | 32x32 | solid, hazard |
| `projectile.png` | 12x8 | 2-frame bolt |
| `pickup.png` | 32x32 | 2-frame pulse |
| `door.png` | 44x48 | 1 |

To replace art, keep the file name and frame layout. Files are limited to 4 MiB and 4096 px per side.

## Sounds (`assets/sounds`)

48 kHz, mono, 16-bit PCM WAV. Other WAV formats are converted at load time to the audio device format.

| File | Played when |
| --- | --- |
| `shot.wav` | a new projectile is fired |
| `jump.wav` | jump or Echo Step impulse |
| `hurt.wav` | player loses health or dies |
| `enemy_hit.wav` / `enemy_defeat.wav` | projectile damages / destroys an enemy |
| `pickup.wav` | Echo Step collected or facility completed |
| `door.wav` | room change |
| `gate_denied.wav` | the locked Power Station gate blocks the player |

Effects are mixed by an 8-voice callback mixer and scaled by the Options volume.

## Runtime location

- Windows: `<executable dir>/assets/` (CMake copies `assets/` after each build).
- PS5: `/app0/assets/` (copied into the package by `scripts/build-ps5.ps1`).

Missing or invalid files are logged and never stop the game.
