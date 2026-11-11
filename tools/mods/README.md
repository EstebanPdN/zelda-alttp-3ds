# ZSPR skin conversion

The game reads a `.zspr` copied to `mods/` directly since v4.1-E3, so most people
never need this tool. It remains for a package with a manifest, such as a mod
that should be bundled in the RomFS.

`convert_zspr.py` converts a locally supplied version 1 Link ZSPR into one
folder the 3DS Mods menu can read. It validates the checksum, section bounds,
metadata termination and exact graphics/palette lengths before writing files.
No ROM or extracted base-game assets are needed.

```sh
python3 tools/mods/convert_zspr.py /path/to/character.zspr /path/to/mods/character \
  --source-url https://example.org/original.zspr
```

The folder contains `skin.z3mod`, `mod.ini`, a 16x16 `preview.bmp` and provenance
with the original author, URL and SHA256. Language aliases cover the seven
current languages plus the legacy USA/French Canadian keys. One SKIN can be
active at once. Copy the folder to `sdmc:/3ds/Zelda 3DS/mods/`, open the language
selector, press Y for Mods, toggle with A and return with B.

## Runtime contract

Requires the v4.0 skin trial build with `kModTable_LinkPalette` support.
Earlier loaders reject these packages as a whole instead of applying half of
one. Existing version 1/2 asset mods remain supported by the new loader.

A version 2 package has two entries:

- Whole asset 57: the author's 0x7000 sprite bytes.
- Kind 3 table 3: exactly 124 bytes, four 15-color SNES palettes (armor and
  bunny, 120 bytes), then the two glove colors (4 bytes).

The game reads these colors for gameplay, file selection, bunny gear and the
bottom map marker. The original fifth palette stays in the base assets.
Unloading a mod releases its table and restores normal palette lookup.

The author's artwork stays external to the source repository. This
converter preserves credits; it does not grant redistribution rights.

## Verification with locally supplied inputs

```sh
python3 platform/3ds/tests/run_mods_test.py --out /outside/source/loader-tests
SDL2_CONFIG=/path/to/sdl2-config python3 platform/3ds/tests/run_skin_trials.py \
  --languages /private/language-assets --refs /private/reference-saves \
  --downloads /private/sprites-with-skins-json --mods /private/converted-mods \
  --work-dir /outside/source/skin-trials
```

The real-engine trial compares each converted mod against its source ZSPR in
fresh processes: nine armor/glove combinations, four walking directions,
sword pose and map marker. Every capture must be a stable gameplay frame;
every logic frame is drawn, including raster IRQ handling. Palette, glove,
file-selection colors and unload behavior are asserted under ASan.

Original artists and community-pack contributors are listed in [the project credits](../../CREDITS.md#community-pack-and-original-mod-authors).
