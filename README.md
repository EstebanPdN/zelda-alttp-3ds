# Zelda A Link to the Past 3DS

![Zelda A Link to the Past 3DS running on Nintendo 3DS](Git3DS.png)

Nintendo 3DS dual-screen port of Zelda3, built with help from Codex.

This project is based on open-source work from:

- Original reverse-engineered Zelda3 engine: https://github.com/snesrev/zelda3
- Android port base: https://github.com/Waterdish/zelda3-android
- Dual-screen Android branch used as the 3DS source base:
  https://github.com/samyost1/zelda3-android

No ROM or extracted game asset package is distributed in this repository. Each
user must provide their own legally obtained USA, unheadered ROM on their own
3DS SD card.

## Features

- Top screen: 400x240 gameplay.
- Bottom screen: live map, dungeon map, gear view, item section and settings.
- First launch extracts `zelda3_assets.dat` locally from the user's ROM.
- ROM files can use any filename, though short names are recommended.
- Screen menu with `Original`, `Stretch` and `Wide` display modes.
- Turbo speed support on New 3DS.
- Quick diagnostics: press `L + R + A` to create a dump with memory files plus
  top and bottom screenshots.
- Use diagnostics for any bug or issue report so I can fix problems more
  easily.

## Installation

Install the CIA, then create this directory on the SD card:

```text
sdmc:/3ds/Zelda 3DS/
```

Place a legally obtained USA, unheadered ROM there. The ROM can use any `.sfc`
or `.smc` filename, though short names are recommended.

The port can also use certain translated ROMs, and some Spanish translations
should work correctly. Keep the original USA ROM and the translated ROM as
separate files in the same folder. If a translated ROM is missing some data or
assets, the port can use the original USA version to build the missing parts.

On first launch, press A to validate the ROM and extract the assets. The ROM is
read locally and is never copied into the CIA.

Audio requires:

```text
sdmc:/3ds/dspfirm.cdc
```

Luma3DS can create this file from the console's own firmware through Rosalina's
`Dump DSP firmware` command.

## Releases

Every GitHub release includes:

- installable CIA
- Homebrew Launcher 3DSX
- QR code for scanning the CIA URL from FBI on a 3DS
- clean source-code zip for that exact version

Release binaries and QR images are kept on the GitHub Releases page, not in the
repository source tree. The release page itself shows the QR code, legal notice
and a short changelog.

Latest release:

https://github.com/EstebanPdN/zelda-alttp-3ds/releases/latest

## Building

Requirements:

- devkitARM, libctru and 3ds-cmake under `DEVKITPRO`
- `makerom` and `bannertool` for CIA packaging
- the vendored SDL2 source in `app/jni/SDL2`
- `banner.cgfx` is prebuilt in `platform/3ds/assets`; it was generated from
  the supplied box glTF with only the base diffuse texture.

Build:

```sh
chmod +x platform/3ds/build.sh
platform/3ds/build.sh
```

The script builds the 3DSX and CIA under `build-3ds/game/`.

## Legal

This repository contains only source code, build scripts, redistributable port
assets and patch/extraction logic. It does not include a ROM, extracted game
assets, or `zelda3_assets.dat`.

Users are responsible for providing their own legally obtained compatible ROM.
