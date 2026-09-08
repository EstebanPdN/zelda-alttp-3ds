# Changelog

Concise cumulative history from v2.9 through local v3.0-E11.

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

## v3.0-E7

- Added Old 3DS fixed-color and subscreen component lookup tables, skipped
  subscreen drawing when no visible main pixel uses color math, and processed
  opaque tile rows with aligned ARMv6 paired priority operations.
- Fixed palette-cache invalidation for direct NMI palette uploads.
- Preconverted opaque bottom-screen maps and backgrounds to RGB565 on Old
  3DS, avoiding repeated conversion during software scaling. Alpha sprites
  retain ARGB8888 and nearest sampling.
- Fixed the stray horizontal line inside selected item borders at reduced UI
  scale. The shared rounded-rectangle fix also covers gear slots, menu boxes,
  settings, tabs and confirmation buttons.
- Numbered new dumps `001`, `002`, `003`, continuing after restart without
  overwriting existing folders; legacy timestamped dumps remain loadable.
- Added 120 recent frame samples, PPU worker/join timings, GPU submission
  timings, audio queue/refill diagnostics, scene/register information, CGRAM,
  OAM, priority buffers, bottom-UI diagnostics and a capture manifest.

Full-resolution rendering remains enabled. E7's 60 FPS target on Old 3DS
requires hardware validation; no emulator or host benchmark establishes it.

## v3.0-E8

- Corrected the texture-environment submission order on both screens. Apply
  the channel mapping after Citro2D's lazy image-mode update and immediately
  flush the image before another texture, overlay or screen can change it.
- Added explicit Old 3DS and New 3DS hardware policies. New 3DS restores E6
  palette-upload and UI behavior and excludes Old PPU/UI/audio experiments.
  New 3DS keeps its existing renderer, scheduler and cache strategy.
- Added an Old-only packed full-brightness half-add path for rain composition,
  preserving exact SNES rounding and output colors at full resolution.
- Changed dump names to `000-dump-YYYYMMDD-HHMMSS`, `001-dump-...`, matching
  the Mario Kart naming convention. Continue across restarts, recover the
  counter from existing folders, and reset to zero for an empty collection.
- Retained loading support for E7 numeric-only and older timestamped dumps.
- Added the last submitted CPU top-image source and its format metadata to
  dumps, so source colors can be compared with physical display captures.

E8's hardware FPS and the reported New 3DS flicker require on-device retesting;
local image/state tests do not establish console performance or stability.

## v3.0-E9

- Clear RGB565 targets with native black, removing the incorrect RGBA fill
  used for the ORIGINAL margins.
- Include arth78's PR #30: keep WIDE at native 400x224, centered without
  fractional vertical stretching. STRETCH remains available.
- Include Archaistic's PR #32: use a baked antialiased Triforce mask, static
  on Old and animated on New; avoid periodic Old cinema redraws.
- Keep gameplay camera offsets out of dungeon/world/flute map menus so
  projected markers remain aligned (issues #36/#37).
- Add movement-triggered Old overworld map patches and prioritize scene
  transitions instead of waiting for the idle fallback (issue #34).
- Show the live mirror-return portal on the bottom overworld map (issue #21).
- Join and reset UI/PPU workers and ROM-specific map caches when switching
  ROMs; discard old frame/worker state before restarting (issue #24).
- Precompute Old sprite scanline candidates and backdrop/subscreen colors,
  preserving sprite order/limits, clipping, palette invalidation and rounding.
  New uses the existing renderer path and does not copy the added Old caches.
- Add sprite-candidate and map-redraw context to diagnostics.

PR #31 is an alternative to #30 and is not included: its 240-line WIDE view
adds rendering work and requires additional gameplay/HDMA coverage. No console
FPS claim is made; E9 graphics, scene transitions and performance need hardware
confirmation. Rain-lightning issue #35 remains unconfirmed.

## v3.0-E10 (local)

- Added ordered SDL/NDSP and worker cleanup on normal/error exits as a mitigation
  for the observed E9 shutdown lifetime crash. The initiating failure was not proven.
- Restored integer bottom-UI geometry on New as well as Old 3DS.
- Corrected the diagnostic version definition; E9 had incorrectly identified as E8.

## v3.0-E11 (local)

- Retain Old 3DS Mode 1 background planes across frames; update changed tiles/map
  entries, keep palette changes independent and use aligned ARM pair-priority merges.
- Fall back on allocation failure, live VRAM writes, changed map configuration,
  unprepared modes and unsupported effects. New keeps its existing profile.
- Add sampled per-thread PPU phase timings and cache work to numbered dumps.
- Add E10 differential tests, live VRAM/map/mode transitions, allocation-fallback
  coverage and ARM machine-code replay of supplied private states.
- Document the PICA200 renderer design. The GPU backend is not implemented in E11;
  host/ARM replay improvements are not physical-console FPS measurements.
- Keep CIA/3DSX, frozen source, symbols and LAN installation QR local only.
