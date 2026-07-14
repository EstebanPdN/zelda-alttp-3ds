# zelda3-android
A port of Zelda3 to Android with a second screen mod for dual screen devices like the AYN Thor. <br>

Original Repository: https://github.com/snesrev/zelda3 <br>
Based on: https://github.com/Waterdish/zelda3-android <br>

The bottom screen shows a live world map, a dungeon map with the rooms you visited, and a touch inventory where you can tap an item to equip it. On the title screen and during cutscenes it just shows a triforce. <br>

The second screen graphics are made from your zelda3_assets.dat while the game runs, so there is no extra setup and the app contains no game assets. <br>

This branch also builds the second screen for desktop/handheld Linux (SDL2), so the same UI runs on dual-screen Linux handhelds. <br>

![](showcase.png)

## Building

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

First launch: the app asks for your Zelda 3 ROM. Tap "Select ROM", pick your
"Legend of Zelda, The - A Link to the Past (USA)" file, and it extracts the
assets (zelda3_assets.dat) once, then boots straight into the game. Every launch
after that skips the prompt. The ROM is only read to build the assets — it is
never copied or kept, and the app still ships no game assets of its own. <br>

If you'd rather supply the extracted zelda3_assets.dat yourself, drop it in
Android/data/com.dishii.zelda3/files and the app will use it and skip the
prompt. You can create it with the manual instructions on the original
repository (or below if you don't have access to a computer). <br>

Android 13 users: check the releases tab for the Android 13 version of the app. 

NOTE: The game itself is controller only, but the bottom screen has full touch controls (map, inventory, equipping items). <br>

How to Change Settings: <br>
Android/data/com.dishii.zelda3/files contains zelda3.ini. Use a text editor to change options. <br>

Default Settings:
L3 Turbo button <br>
18:9 Aspect Ratio <br>
Fullscreen(no android on-screen controls) <br>

<h3>Instructions for creating zelda3_assets.dat on android:</h3>
1. Download PyDroid: https://play.google.com/store/apps/details?id=ru.iiec.pydroid3&hl=en_US. Choose to skip any options that ask for money, you can do all of the following steps without paying.<br>
2. Open the hamburger menu at the top left of the app and select Pip.<br>
3. Type in "Pillow" without the quotes and it will have you install the repository app from the app store.<br>
4. Once the repository app is installed, you can install "Pillow" and "pyyaml" <br>
5. Download the <b>source code</b> zip file for zelda3 at https://github.com/snesrev/zelda3/releases/tag/v0.3. The zip file with the exe file in it will not work. <br>
6. Extract the zip file. <br>
7. Place your rom file in the main zelda3 directory that you extracted, the same one as extract_assets.bat, and rename it to zelda3.sfc <br>
8. Open PyDroid again, open the hamburger menu, and select Terminal.<br>
9. Navigate to where you placed the rom file. (If you are unfamiliar with terminal commands, "ls" lists the folders and files and "cd Foldername" changes the directory. An example using the 0.3 release of zelda3 above would be "cd Download" "cd zelda3-0.3" "cd zelda3-0.3" or simply "cd Download/zelda3-0.3/zelda3-0.3") <br> 
10. Paste in this command <code>python3 assets/restool.py --extract-from-rom</code> <br>
11. It should pause for a while and when it finishes you should be able to see zelda3_assets.dat in the same folder as your rom. You can go ahead and copy that to the Android/data/com.dishii.zelda3/files location. <br>

