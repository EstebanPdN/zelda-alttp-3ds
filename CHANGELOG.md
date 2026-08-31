# Changelog

Concise cumulative history from v2.9 through v3.0-E6.

## v2.9

- Fixed map rendering when WIDE mode and the FIXED camera are used together.
- Added a custom 3D HOME Menu logo banner.
- Improved Old 3DS bottom-screen responsiveness with RGB565 buffers,
  touch-first input handling and dirty-region HUD redraws.

## v2.9.2

- Updated the HOME Menu banner to the supplied 2.0 logo model and reduced its
  generated CGFX size.

## v3.0-E3

- Added bottom-screen display and turbo settings, opened from the Triforce on
  the main menu.
- Saved screen settings between sessions.
- Improved spacing for hearts, magic and the equipped item in the bottom HUD.

## v3.0-E4

- Reduced Old 3DS GPU and PCM-audio cache-clean overhead by using direct cache
  maintenance with compatibility fallbacks.
- Replaced Citro3D's full linear-heap frame flush with an explicit bounded
  Citro2D dirty range.
- Switched both presentation targets to color-only RGB565, reducing render
  bandwidth and VRAM use without changing the source image or display modes.
- Enabled function/data section garbage collection for smaller release builds.
- Added Minish Cap-style `DUMP SAVED` confirmation and an optional top-screen
  FPS counter.
- Added Developer > Load State with an in-menu confirmation, newest-dump
  selection, checksum validation and active-ROM profile protection.
- Added a validated `load-state.bin` checkpoint to every completed quick dump.

The E4 cache strategy was adapted to this engine after studying
[@999sian's Old 3DS performance work in PR #26](https://github.com/EstebanPdN/zelda-tmc-3ds/pull/26).

## v3.0-E5

- Added direct Old 3DS RGB565 PPU output to reduce wide-mode framebuffer,
  cache-clean and texture-upload traffic.
- Reduced the parallel Old 3DS PPU tile-cache working set and replaced the
  idle Core 1 polling loop with event-based wakeups.
- Made the Old 3DS Developer overlay event-driven instead of redrawing the
  full bottom screen every frame.
- Fixed the blue/glitched Old 3DS bottom screen by configuring Citro2D texture
  state before image submission.
- Changed the top counter to `FPS <value>` with no CPU/GPU suffix.
- Fixed `DUMP SAVED` truncation and removed its deliberate post-dump pause.

## v3.0-E6

- Reverted E5's direct RGB565 top-screen experiment to E4's verified BGRX
  software buffer and RGBA8 texture-upload path.
- Fixed E5's PPU destination-origin regression, which wrote the visible image
  at the internal 96-pixel priority-buffer origin instead of the configured
  output origin and caused displaced, stale or corrupted top-screen pixels.
- Restored the full address-indexed tile-row caches; reconstructed dump
  benchmarks showed E5's compact collision cache was slower despite its
  smaller footprint.
- Restored conservative full-resolution PPU optimizations: palette rebuilds
  only when CGRAM/brightness changes, hidden-OBJ scan elimination, disabled
  subscreen work elimination, color-window fast paths and unrolled final
  palette mapping.
- Replaced software-canvas dump screenshots with physical GSP display
  captures: 400x240 top, 320x240 bottom, plus both raw framebuffers.
- Paused the active NDSP channel for the entire synchronous dump transaction,
  so already-queued music stops and resumes at the same playback position.
- Excluded the deliberate dump I/O frame from subsequent performance metrics.
