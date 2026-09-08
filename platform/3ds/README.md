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

The HOME Menu metadata is versioned for every release. v3.0-E7 uses:

```text
Short name:  Zelda 3DS EXP 7
Long name:   A Link to the Past 3DS experimental 7
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

## E7 diagnostics and Old 3DS profile

E7 adds full-resolution color-math lookup tables and ARMv6 opaque tile spans
on Old 3DS, plus preconverted opaque UI textures. The 60 FPS hardware target
has not yet been verified. New 3DS retains its existing rendering profile.

New dumps use `dumps/001/`, `002/`, `003/`, and continue numerically after a
restart (including `999` to `1000`). Existing timestamped dumps are preserved.
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
