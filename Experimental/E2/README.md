# Link to the Past 3DS E2

Experimental 2 build for Old 3DS performance and bottom-screen latency testing.

This is **not** a GitHub Release. It is published only through the `E2` tag for testing.

It keeps the installed application metadata from v2.0/v2.2:

- SMDH title: Link to the Past 3DS v2.0
- CIA title: ALttP 3DS v2.0
- ProductCode: CTR-P-Z3DS
- UniqueId: 0x5A20D

Installing this CIA should update the same HOME Menu app instead of creating another icon.

## Experimental changes

- Old 3DS PPU split changed from the broken almost-all-main-thread layout to an actual 50/50 line split between the main thread and Core 1 worker.
- Bottom-screen touch now forces an immediate bottom redraw/present after a tap, instead of waiting behind the normal asynchronous redraw cadence.
- The L + R + B FPS readout now updates as a 2.5-second average, so it should be calmer and more useful than instant frame-to-frame values.

## What to test

- In Original screen mode on Old 3DS, make a dump after 1-2 minutes and check whether `PPU split line` is near 120 instead of 240.
- Check whether `Average PPU draw` drops from the previous 28-30 ms range.
- Check whether bottom-screen settings/map taps respond immediately, even if top-screen FPS remains below 60.

QR target:

https://github.com/EstebanPdN/zelda-alttp-3ds/raw/E2/Experimental/E2/zelda3-3ds-E2.cia
