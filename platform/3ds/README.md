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

The HOME Menu metadata is versioned for every release. v3.0-E6 uses:

```text
Short name:  Zelda 3DS EXP 6
Long name:   A Link to the Past 3DS experimental 6
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
