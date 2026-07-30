# Link to the Past 3DS E5

Experimental Old 3DS performance build.

This is **not** a GitHub Release. It is published only through the `E5` tag for testing.

## What changed since E4

- Reverted the E4 fast scanline experiment. The top screen is back to normal full-resolution PPU rendering.
- Removed the artificial Old 3DS visual cap that forced Original/Stretch toward ~30 FPS.
- Old 3DS now renders as fast as the CPU can finish frames while game logic remains scheduled at 60 Hz.
- Old 3DS uses native 224-line SNES top rendering instead of forcing 240 software-rendered lines.
- New 3DS keeps the normal 240-line profile.

## What to test

- Top screen image quality should look normal again.
- Average FPS in Original and Stretch should no longer be capped around 30 by policy.
- Wide mode remains more expensive because it renders a larger 400px-wide PPU scene.
- Generate a dump after 1-2 minutes of physical Old 3DS gameplay.

Key dump fields:

- `Average PPU draw`
- `Average presentation interval`
- `Measured presentation rate`
- `Measured normal logic rate`
- `PPU split line`
- `Last main PPU segment`
- `Last slowest PPU worker`

## Direct CIA URL

https://github.com/EstebanPdN/zelda-alttp-3ds/raw/E5/Experimental/E5/zelda3-3ds-E5.cia
