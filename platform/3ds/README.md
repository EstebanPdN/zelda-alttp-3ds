# Zelda 3DS platform

This target builds the dual-screen native frontend for Nintendo 3DS.

## Console installation

Install the CIA, then create this directory on the SD card:

```text
sdmc:/3ds/Zelda 3DS/
```

Place a legally obtained USA, unheadered ROM there. The preferred filename is
`zelda3.sfc`, but the setup also accepts `.sfc` and `.smc` files with other
names. On first launch, press A to validate the ROM and extract
`zelda3_assets.dat`. The ROM is read locally and is never copied into the CIA.

Audio requires `sdmc:/3ds/dspfirm.cdc`. Luma3DS can create it from the
console's own firmware through Rosalina's `Dump DSP firmware` command.

## Display and controls

- Top screen: 400x240 gameplay at 5:3 through a native RGB565 presenter.
- Bottom screen: 320x240 live map, gear, touch inventory and settings.
- D-Pad or Circle Pad: movement.
- A/B/X/Y, L/R, Start and Select: corresponding game buttons.
- ZL or C-stick on New 3DS: hold for turbo when `TURBO SPEED` is not `OFF`.
- L + R + A: create a quick dump under `sdmc:/3ds/Zelda 3DS/dumps/`.
- Settings > Developer > Load State: load the validated checkpoint from the
  newest dump after confirmation. Checkpoints from another ROM profile are
  rejected.
- Settings > Developer > Show FPS: toggle the top-screen FPS counter.

The CIA metadata uses the Legacy memory mode for Old 3DS compatibility and
requests the New 3DS 804 MHz/L2 configuration when that hardware is available.
The 3DSX also requests New 3DS speedup at runtime. Normal gameplay advances
once per VBlank. The New 3DS bottom UI redraws at 30 FPS; the Old 3DS profile
redraws it when its visible state changes, with a low-frequency safety refresh.
Quick-dump `info.txt` files include average/max frame work time and the number
of frames that exceed the 16.67 ms budget. Each completed dump also includes
physical 400x240 and 320x240 BMP captures, the two raw display framebuffers,
`load-state.bin` and a short `DUMP SAVED` notice. NDSP playback pauses for the
complete synchronous capture and resumes at the same playback position.

The HOME Menu metadata is versioned for every release. v3.0-E11 uses:

```text
Short name:  Zelda 3DS EXP 11
Long name:   A Link to the Past 3DS experimental 11
ProductCode: CTR-P-Z3DE
UniqueId:    0x5A13E
```

The CIA banner uses `assets/banner.cgfx`, generated from the supplied 2.0
Blender logo model and kept below the HOME Menu CGFX size limit.

v3.0-E6 restores E4's verified BGRX top software buffer and RGBA8 texture
upload on both models. It fixes the E5 output-origin error, restores the full
address-indexed PPU tile caches and adds conservative full-resolution PPU fast
paths without reducing lines, pixels or color accuracy. The event-driven Old
3DS Developer overlay and the corrected Citro2D texture-state ordering remain.
New 3DS keeps its existing 4x Mode 7 eligibility.

## Requirements

- devkitARM, libctru and 3ds-cmake under `DEVKITPRO`
- `makerom` and `bannertool` for the optional CIA step
- the SDL 2.28.1 source already vendored at `app/jni/SDL2`

Run:

```sh
chmod +x platform/3ds/build.sh
platform/3ds/build.sh
```

The script first builds the vendored SDL port, then creates the 3DSX and CIA.
No ROM or extracted asset file is included in either package.

Release checksums are published in `SHA256SUMS.txt` beside each CIA and 3DSX.

## E8 hardware profiles and diagnostics

E8 selects an explicit device policy automatically. Old 3DS uses its own
full-resolution PPU optimizations, including packed half-add for rain, and
preconverted opaque UI textures. New 3DS restores E6 palette-upload and UI
behavior and excludes the Old PPU and audio probes. Its existing renderer,
scheduler and cache strategy remain in place. Both screens use corrected
texture-state submission.
Hardware validation, including the Old 3DS 60 FPS target, remains pending.

New dumps use `dumps/000-dump-YYYYMMDD-HHMMSS/`, `001-dump-...`, etc.
The sequence continues across restarts and clock changes. A counter plus
folder scanning prevents reuse; an empty dump collection starts again at zero.
The counter file is `dumps/dump-sequence.txt`. Existing E7 numeric-only folders
and legacy `dump-YYYYMMDD-HHMMSS` folders remain loadable and are preserved.
`LOAD STATE` prefers the highest numbered directory and fails visibly if its
checkpoint is missing, corrupt or belongs to another ROM profile.

Each dump retains the physical BMP/raw screenshots, RAM, SRAM, VRAM and
validated checkpoint, and adds:

- `frame-times.csv`: up to 120 recent Old 3DS samples, oldest first. Microsecond
  columns separate logic, PPU, presentation, bottom submission, total work,
  presentation interval, main/worker rendering, worker join, frame begin,
  top cache clean/transfer and frame end. Scheduled/executed logic and split
  line are counts. Timings overlap; do not sum every column. These are wall
  spans including preemption, not per-thread CPU cycle counts.
- `audio.txt`: buffer format, queue state, refill wall spans, empty-queue
  transitions, worker priority and cache maintenance mode. Queue observations
  are not a count of audible glitches; audio is paused during capture.
- `ppu.txt`, `cgram.bin`, `oam.bin`, `ppu-main-priority.bin`,
  `ppu-sub-priority.bin`: current scene, PPU/DMA registers and raw buffers.
  Registers are post-render snapshots, not a full HDMA trace; priority buffers
  contain the last main-thread scanline and may include unused/stale spans.
- `top-source.raw` and `top-source.txt`, when a frame has been submitted:
  the CPU top-frame source in BGRX8888 with width/height/pitch metadata. It may
  differ from the physical capture by one presentation; it is not a GPU screenshot.
- `bottom-ui.txt`: UI scale, format, worker state and stable geometry.
  `bottom-ui.raw` is included only when its source front buffer is stable;
  its dimensions/pitch/format are recorded in the text file. A busy worker
  is reported rather than read concurrently.
- `runtime.log` and `zelda3.ini`, when present; `info.txt` adds model/profile,
  heap space, cache modes, recent averages and Citro3D timing information.
- `manifest.txt`: file sizes, FNV-1a checksums and capture completion status.
  Checksums detect accidental file damage; they are not authentication.

Diagnostic SD writes remain outside normal frame metrics. Bottom redraw work
runs asynchronously: `bottom_submit_us` is not its full rendering duration;
use the full/patch/touch worker statistics in `info.txt` for that cost.
`gpu_end_us` covers `C3D_FrameEnd`, excluding the preceding C2D flush/clean.
The recent ring is Old 3DS only; it can be empty immediately after startup.

### E9 display and map repairs

WIDE keeps 400x224 source pixels centered vertically (8-pixel margins).
ORIGINAL stays 256x224 and STRETCH still fills the display. RGB565 margins are
cleared with native black. Old map movement requests coalesce at most ten
updates/second and use map-only patches where possible; actual refresh depends
on hardware load. Scene changes get priority. Mirror portals are displayed
from the engine's live return coordinates.

PR #30 (arth78) and #32 (Archaistic) are included. New retains its renderer,
clock and worker scheduling policy; shared map/display/ROM-switch bug fixes
also apply to New. Old-only sprite and color caches are excluded from New's
worker copy. No hardware FPS or graphical acceptance is claimed by host tests.

## Local E10 regression candidate

E10 restores integer rounded controls on both hardware profiles. New retains
its E6 renderer policy, texture format, frame cadence and clock settings. The
Old PPU implementation is unchanged from E9. A runtime exit handler stops
audio/render workers before libctru frees their heap stacks; fatal errors are
logged before cleanup and shown through the existing error screen.

The supplied E9 crash matches all 96 captured instruction bytes. It faults in
ndspiReadChnState while pushing to an unmapped stack. Heap teardown with a
live audio worker is a supported hypothesis, not a confirmed explanation of
why the application was exiting. E10 is a mitigation and diagnostic candidate;
Old startup recovery and 60 FPS still require hardware confirmation.

E9 mistakenly kept the E8 diagnostics version macro. E10 corrects that macro,
filenames and HOME Menu metadata. Existing dump numbering is preserved.

Builds E10 and later remain local until the owner explicitly publishes them.
Do not push commits/tags or upload release assets during this testing series.

### E11 Old 3DS phase samples

The `ppu.txt` diagnostic includes one sampled frame per 64 frames: preparation,
sprite evaluation, main-screen layer drawing, sub-screen selection/drawing and
color composition, separately for the main thread and its joined worker.
Mode 7 HQ is reported separately. The sample records frame age, scene, split,
retained row count and changed tile count. These are wall spans with thread
preemption, not CPU-only measurements; main and worker overlap and must not be
summed. Preparation occurs once on the main thread. The existing recent-frame
CSV remains available for steady-state cadence. Dump numbering stays
`000-dump-YYYYMMDD-HHMMSS`, `001-dump-YYYYMMDD-HHMMSS`, etc.
