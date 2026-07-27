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

- Top screen: 400x240 software-rendered gameplay at 5:3.
- Bottom screen: 320x240 live map, gear, touch inventory and settings.
- D-Pad or Circle Pad: movement.
- A/B/X/Y, L/R, Start and Select: corresponding game buttons.
- ZL on New 3DS: hold for turbo.

The CIA metadata uses the Legacy memory mode for Old 3DS compatibility and
requests the New 3DS 804 MHz/L2 configuration when that hardware is available.

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

The expected SHA-256 is:

```text
66871d66be19ad2c34c927d6b14cd8eb6fc3181965b6e517cb361f7316009cfb
```
