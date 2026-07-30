# Link to the Past 3DS E4

Experimental Old 3DS performance build.

This is **not** a GitHub Release. It is published only through the `E4` tag for testing.

## What changed since E3

- Public v2.3 release was removed before accepting this as official.
- HOME Menu metadata no longer contains a version:
  - Short name: `The Legend of Zelda`
  - Long name: `A Link to the Past 3DS port`
  - Author: `EstebanPdN`
- Old 3DS uses an experimental fast scanline PPU path outside HQ Mode7 scenes:
  - draws one real PPU scanline
  - duplicates the following scanline
  - keeps per-line HDMA/state stepping

## What to test

- Average FPS in Original mode.
- Average FPS in Wide mode.
- Whether the top screen looks acceptable or too vertically soft.
- Whether overworld scrolling/map transitions show artifacts.
- Generate a dump after 1-2 minutes of physical Old 3DS gameplay.

## Direct CIA URL

https://github.com/EstebanPdN/zelda-alttp-3ds/raw/E4/Experimental/E4/zelda3-3ds-E4.cia
