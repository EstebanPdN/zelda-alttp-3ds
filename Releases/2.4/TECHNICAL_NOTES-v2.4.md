# Technical Notes v2.4

The rendering bug was caused by Old 3DS performance experiments being applied to the shared SNES PPU renderer. Because New 3DS used that same `ppu.c` path, those optimizations also changed the stable New 3DS renderer and caused black bars / incorrect layer composition in scenes that rendered correctly in v2.0.

v2.4 restores `app/jni/src/snes/ppu.c` and `app/jni/src/snes/ppu.h` to the stable v2.0 renderer behavior, removes the unsafe v2.4.0 scanline cache hooks, and keeps the later platform fixes outside the PPU renderer.

Future Old 3DS experiments must use a separate backend/wrapper so renderer changes cannot affect New 3DS.
The Settings changes are limited to the second-screen SDL UI and do not touch the shared SNES PPU renderer.
