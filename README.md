# Zelda A Link to the Past 3DS

This repository contains a Nintendo 3DS dual-screen port of Zelda3, based on
the open-source `zelda3-android` dual-screen branch.

Original engine: https://github.com/snesrev/zelda3  
Android port base: https://github.com/Waterdish/zelda3-android  
3DS source base: https://github.com/samyost1/zelda3-android

No ROM or extracted game asset package is distributed in this repository. Each
user must provide their own legally obtained USA, unheadered ROM on their own
3DS SD card.

## Nintendo 3DS Features

- Top screen: 400x240 gameplay.
- Bottom screen: 320x240 live map, dungeon map, gear view, item selection and
  touch settings.
- First launch extracts `zelda3_assets.dat` locally from the user's ROM.
- Display modes: wide mod, stretched original and original aspect.
- Turbo speed: off, x2, x3, x4 or x5.
- New 3DS: ZL or C-stick can hold turbo when turbo is enabled.
- Quick diagnostics: press `L + R + A` to create a dump with memory files plus
  top and bottom screenshots.
- HOME Menu metadata is versioned for each build. v1.5 appears as
  `Zelda ALttP 3DS` / `Zelda A Link to the Past 3DS v1.5`.

## 3DS Installation

Install the CIA, then create this directory on the SD card:

```text
sdmc:/3ds/Zelda 3DS/
```

Place a legally obtained USA, unheadered ROM there. The preferred filename is
`zelda3.sfc`, but the setup also accepts other `.sfc` or `.smc` filenames.
On first launch, press A to validate the ROM and extract the assets. Audio
requires `sdmc:/3ds/dspfirm.cdc`.

## 3DS Build

Requirements:

- devkitARM, libctru and 3ds-cmake under `DEVKITPRO`
- `makerom` and `bannertool` for CIA packaging
- the vendored SDL2 source in `app/jni/SDL2`

Build:

```sh
chmod +x platform/3ds/build.sh
platform/3ds/build.sh
```

The script builds the 3DSX and CIA under `build-3ds/game/`.

## Original Android/Dual-Screen Notes

The dual-screen mod was made with the help of Claude Code and opencode.

The bottom screen shows a live world map, a dungeon map with the rooms you
visited, and a touch inventory where you can tap an item to equip it. On the
title screen and during cutscenes it just shows a triforce.

The second screen graphics are made from your ROM while the game runs, so there is no extra setup and the app contains no game assets.

This branch also builds the second screen for desktop/handheld Linux (SDL2), so the same UI runs on dual-screen Linux handhelds.

![](showcase.png)

# Instructions

1. Install the APK from the releases page. Android 13 users: check the releases tab for the Android 13 version of the app.
2. Launch the app and tap "Select ROM" when asked, then pick your "Legend of Zelda, The - A Link to the Past (USA)" rom file.
3. That's it. The app extracts the game assets once and boots straight into the game. Every launch after that goes directly to the game.

The rom is only read to build the assets (zelda3_assets.dat) — it is never copied or kept, and the app ships no game assets of its own.

The game itself is controller only, but the bottom screen has full touch controls (map, inventory, equipping items).

## Settings

Common options can be changed from the settings screen on the bottom panel. Everything else is in Android/data/com.dishii.zelda3/files/zelda3.ini — edit it with a text editor.

Default settings:
- L3 Turbo button
- 16:9 aspect ratio
- Fullscreen (no android on-screen controls)

## Optional: providing zelda3_assets.dat yourself

If you'd rather extract the assets on a computer, drop your zelda3_assets.dat into Android/data/com.dishii.zelda3/files and the app will use it and skip the ROM prompt. You can create it with the manual instructions on the original repository, or on-device as follows if you don't have access to a computer:

1. Download PyDroid: https://play.google.com/store/apps/details?id=ru.iiec.pydroid3&hl=en_US. Choose to skip any options that ask for money, you can do all of the following steps without paying.
2. Open the hamburger menu at the top left of the app and select Pip.
3. Type in "Pillow" without the quotes and it will have you install the repository app from the app store.
4. Once the repository app is installed, you can install "Pillow" and "pyyaml"
5. Download the **source code** zip file for zelda3 at https://github.com/snesrev/zelda3/releases/tag/v0.3. The zip file with the exe file in it will not work.
6. Extract the zip file.
7. Place your rom file in the main zelda3 directory that you extracted, the same one as extract_assets.bat, and rename it to zelda3.sfc
8. Open PyDroid again, open the hamburger menu, and select Terminal.
9. Navigate to where you placed the rom file. (If you are unfamiliar with terminal commands, "ls" lists the folders and files and "cd Foldername" changes the directory. An example using the 0.3 release of zelda3 above would be "cd Download" "cd zelda3-0.3" "cd zelda3-0.3" or simply "cd Download/zelda3-0.3/zelda3-0.3")
10. Paste in this command `python3 assets/restool.py --extract-from-rom`
11. It should pause for a while and when it finishes you should be able to see zelda3_assets.dat in the same folder as your rom. You can go ahead and copy that to the Android/data/com.dishii.zelda3/files location.

# Building

The native code lives in `app/jni/src` and builds two ways; pick the one for your target. `second_screen.c` is the shared, platform-free core (game-state reads + art generation); each target compiles only its own frontend from `src/platform/`:

- `src/platform/android/` — JNI bridge (`second_screen_jni.c`) + no-op SDL stubs
- `src/platform/linux/` — the SDL UI (`second_screen_sdl.c`) + its generated tables

**Android:** open the project in Android Studio and build/run, or `./gradlew assembleDebug`. The NDK build (`jni/Android.mk`) compiles `src/*.c` + `src/platform/android/*.c`.

**Linux (desktop / handheld):**
```
cd app/jni/src
make zelda3          # needs SDL2 dev headers (libsdl2-dev / SDL2-devel / sdl2)
```
This compiles `src/*.c` + `src/platform/linux/*.c` into the `zelda3` binary. Enable the second screen with `ZELDA3_SECOND_SCREEN=1` (env knobs are documented at the top of `second_screen_sdl.c`).

The generated tables (`src/platform/linux/ss_sheets.h`, `ss_textures.h`) come from `app/src/main/assets/secondscreen/` and are committed, so a normal build doesn't regenerate them. If those assets change, run `python tools/secondscreen/gen_linux_tables.py` from the repo root (needs Pillow).

# MSU-1 music (custom soundtracks)

To use an MSU-1 audio pack instead of the built-in music:

1. Copy the pack files (`alttp_msu-1.pcm`, `alttp_msu-2.pcm`, ...) into `Android/data/com.dishii.zelda3/files/msu/`.
2. In `Android/data/com.dishii.zelda3/files/zelda3.ini`, set `EnableMSU = true`, `MSUPath = msu/alttp_msu-`, and `AudioFreq = 44100` (use `48000` for OPUZ packs).
3. Relaunch the game.
