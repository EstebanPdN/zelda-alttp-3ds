# Zelda A Link to the Past 3DS

<img width="1672" height="941" alt="alttp" src="https://github.com/user-attachments/assets/6fc340f1-7d18-4e75-9a1a-bf8986d490dc" />

Nintendo 3DS dual-screen port of Zelda3, built with help from Codex.

This project is based on open-source work from:

- Original reverse-engineered Zelda3 engine: https://github.com/snesrev/zelda3
- Android port base: https://github.com/Waterdish/zelda3-android
- Dual-screen Android branch used as the 3DS source base:
  https://github.com/samyost1/zelda3-android

No ROM or extracted game asset package is distributed in this repository. Each
user must provide their own legally obtained USA, unheadered ROM on their own
3DS SD card.

## Discord
https://discord.gg/SMW49UMkw

## Features

- Top screen: 400x240 gameplay.
- Bottom screen: 320x240 live map, dungeon map, gear view, item selection and
  touch settings.
- First launch extracts `zelda3_assets.dat` locally from the user's ROM.
- Display modes: wide mod, stretched original and original aspect.
- Turbo speed: off, x2, x3, x4 or x5.
- New 3DS: ZL or C-stick can hold turbo when turbo is enabled.
- Quick diagnostics: press `L + R + A` to create a dump with memory files,
  physical 400x240/320x240 screen captures, raw display framebuffers and a
  validated `load-state.bin` checkpoint. Audio pauses during capture and a
  `DUMP SAVED` notice confirms complete success on the top screen.
- Developer settings can load the newest dump checkpoint for the active ROM
  profile and optionally show the current FPS on the top screen.
- PICA200/Citro2D presentation for both screens with nearest-neighbor sampling
  and RGB565 display output.
- PICA200 GPU rendering on Old 3DS, with automatic software fallback for
  unsupported effects. New 3DS uses its existing CPU renderer.
- Native 400x240 WIDE mode, fixed-camera edge corrections and persistent
  per-ROM display, zoom, turbo and control settings.
- Responsive map controls and independent heart updates on Old 3DS.
- Fixed-step 60 Hz gameplay timing with bounded catch-up instead of making
  game speed depend on when a VBlank wait returns.
- Parallel PPU scanline rendering on Core 0 and Core 1, plus Core 2 on New 3DS,
  with persistent tile-row caches and frame-time diagnostics in quick dumps.
- HOME Menu banner uses a lightweight custom CGFX 3D logo model with the
  supplied hover sound converted to a short PCM WAV.

## Installation

Install the CIA, then create this directory on the SD card:

```text
sdmc:/3ds/Zelda 3DS/
```

Place a legally obtained USA, unheadered ROM there. The preferred filename is
`zelda3.sfc`, but the setup also accepts other `.sfc` or `.smc` filenames.

On first launch, press A to validate the ROM and extract the assets. The ROM is
read locally and is never copied into the CIA.

If you’re using a translated/patched ROM, put both the clean USA ROM and the patched ROM in `sdmc:/3ds/Zelda 3DS/`.

The port should use the clean ROM for the original assets and the patched ROM for the translated text. Translation patches may work this way, but gameplay hacks are not guaranteed to be compatible.

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

GitHub supplies automatic source-code archives for each tag.
The release page shows the QR code, a short changelog and bug-report instructions.
Detailed development notes are preserved inside the source snapshot.

Latest release: [v3.0](https://github.com/EstebanPdN/zelda-alttp-3ds/releases/tag/v3.0)

See [CHANGELOG.md](CHANGELOG.md) for the changes since v2.8.

## Bug reports

Press `L + R + A` while the issue is visible and attach the resulting dump from:

`sdmc:/3ds/Zelda 3DS/dumps/`

## Building

Requirements:

- devkitARM, libctru and 3ds-cmake under `DEVKITPRO`
- `makerom` and `bannertool` for CIA packaging
- the vendored SDL2 source in `app/jni/SDL2`
- `banner.cgfx` is prebuilt in `platform/3ds/assets`; it was generated from
  the supplied 2.0 Blender logo model.

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

## Credits

Thanks to [@999sian](https://github.com/999sian) for her Old 3DS optimization
work, [arth78](https://github.com/arth78) for the WIDE display contribution,
and [Archaistic](https://github.com/Archaistic) for the WIDE height and
Triforce improvements.

Logo work by [Phibonacci](https://github.com/Phibonacci), based on the original
3D model by [TiraArt](https://sketchfab.com/TiraArt).

See [3DS controls and diagnostics](platform/3ds/README.md).

## Local v3.1-E2 updater

Settings contains Screen, Turbo Speed, Developer, Update and Restart.
Restart always opens the ROM selector and starts the selected ROM fresh;
SRAM and per-ROM saves remain intact. Automatic state restoration is skipped
for that restart.

Update checks the selected Stable or Pre-release channel from
EstebanPdN/zelda-alttp-3ds on GitHub. Checks also run in the background at
startup; available updates are indicated on the title/menu card. Choose the
channel, tap the release name to read its changelog above, and use Prev/Next
below to turn pages. Download Update opens an installation confirmation.
A completed installation closes the application; reopen it from HOME or HBL.
Save your game before installing.

Downloads use verified HTTPS, GitHub asset size and SHA-256, and the CIA title
ID must match this port. Channel selection is stored in update/channel.txt.
Only newer versions are offered: version tags use vMAJOR.MINOR[.PATCH] or
vMAJOR.MINOR[.PATCH]-E<number>; assets must be named
zelda3-3ds-vVERSION.cia / .3dsx and include GitHub's sha256 digest.
Pre-release checks scan the latest 100 release records. Changelogs display
up to 12 KiB as wrapped ASCII text with Markdown links/images simplified.

This is a local hardware-test build. The current public v3.0 and older
pre-releases do not supersede v3.1-E1. No release was published for this build.

The E2 touch repair dispatches SDL touch edges before game pause/timing exits,
including inside Update on both models. Navigation is committed before waking
the redraw worker. Old 3DS prepares its initial bottom image synchronously;
failed GPU submissions retain pending UI images for retry. Settings now has
five evenly sized rows. Changelogs use the same game letter/glyph sheets,
menu colors and full-screen border as the bottom UI; small hints use integer
pixel scaling. These changes still require the owner's console verification.
