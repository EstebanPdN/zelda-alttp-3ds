# Link to the Past 3DS E3

Experimental build for Old 3DS performance testing.

This is **not** a GitHub Release. It is published only through the `E3` tag for testing.

## What changed since E2

- Keeps the E2 fix that makes bottom-screen touch interactions respond immediately.
- Replaces the fixed 50/50 Old 3DS PPU split with an adaptive split.
- On Old 3DS, Core 1 now starts with a much smaller scanline chunk and adjusts according to real measured cost per line.
- The goal is to avoid the E2 failure mode where the slow system core received half the screen and blocked the whole top frame.

## What to test on physical Old 3DS

- Original mode FPS in overworld.
- Wide mode FPS in overworld.
- Bottom-screen settings/map responsiveness.
- Generate a dump after a minute of gameplay.

Useful dump fields:

- `PPU split line` should move below the E2 value of 120 if Core 1 is too slow.
- `Last main PPU segment` and `Last slowest PPU worker` should be much closer than E2's 15 ms vs 50 ms.
- `Average PPU draw` is the key bottleneck number.

## Direct CIA URL

https://github.com/EstebanPdN/zelda-alttp-3ds/raw/E3/Experimental/E3/zelda3-3ds-E3.cia
