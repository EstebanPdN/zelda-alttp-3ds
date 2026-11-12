# Language patch preparation

The local selector offers English, Spanish, Portuguese, Italian, French,
German, Russian, Japanese and Simplified Chinese. One choice selects both the cartridge translation
and the port UI. The Settings globe asks for
confirmation before opening this selector. Cancelling returns to the current
screen; confirming discards the transient session/autosave and preserves SRAM.

Work only on private copies outside the source checkout. The base cartridge is
headerless USA, SHA-1 `6d4f10a8b10e10dbe624cb23cf03b88bb8252973`.
Original fan patches and their readmes must be preserved separately.

## Supplemental text

`complete_text.py` accepts these exact headerless translated inputs:

| Code | Original source | Input CRC32 | Completed CRC32 |
|---|---|---|---|
| es | D_Skywalk Final 1.11 / F01h | `86de4b5d` | `8f07fc47` |
| it | SadNES cITy 1.1 | `6d879a98` | `977ad1c5` |
| pt | Hexagon 1.3 | `4a56f979` | `a164a974` |

SadNES and Hexagon patches expect a copier header while being applied; remove
that 512-byte header before supplying the resulting copy to this tool. It
rejects other inputs and an output path equal to the input path.

```sh
python3 tools/languages/complete_text.py pt /private/pt-original.sfc /private/pt-completed.sfc
python3 tools/languages/make_bps.py /private/usa.sfc /private/pt-completed.sfc platform/3ds/romfs/languages/pt.bps
```

The supplement translates remaining staff roles and adventure-history labels
in all three languages, the 16 Italian epilogue captions, Portuguese file
menus, the owner-requested Spanish NOMBRE heading, and the last card as FIN, FINE or FIM. Names, original translator credits,
death counters and unrelated sprite tiles are retained. The card lettering is
new pixel artwork authored in the tool, using the cartridge's gold palette.
The compressed sprite is round-trip checked and must fit its original space.
No cartridge graphics or original dialogue are stored in this tool.

`make_bps.py` checks the resulting patch by applying it before writing it. The
console's language cache stamp includes an extractor schema and bundled BPS
checksum, so a patch update invalidates cached assets even in the same language.

## Quality pass

`quality_text.py` works on the completed carts: `export` writes all dialogue as
readable text, `check` measures every line against the 168-pixel box and `apply`
writes corrections. It adds the accented letters Portuguese and Italian lacked,
converts Italian `e'` and `piu'`, moves lines that do not fit, and corrects item
names, file-screen words and epilogue captions. New letters only use font codes
nothing else draws: the file screens and keyboards share this sheet.
Keep the private edits files with the release record. Then run `make_bps.py` as above.

## Verification

Run `platform/3ds/tests/run_language_cache_test.py` for cache invalidation and
`run_ui_strings_test.py` for the seven UI alphabets. With private extracted
inputs, `run_language_tour.py` renders file menus, HUD, inventory, map, attract
scenes, all 16 epilogue captions, the entire credit scroll and the last card
through the production engine. Inspect its PNGs as well as its exit status.
`run_language_quality_test.py` covers the regressions found by this pass.
The native UI layout test separately covers both bottom-screen pixel formats,
confirmation, cancellation and selector requests.

These are source/host checks. Record ARM compilation, isolated Azahar behavior
and physical Old/New 3DS acceptance separately. Emulator FPS is not hardware
performance evidence.

## Japanese

`ja.bps` turns the USA cartridge into the official Japanese one (headerless v1.0,
SHA-1 `e7e852f0159ce612e3911164878a9b08b3cb9060`, title `ZELDANODENSETSU`), the way
`fr.bps` and `de.bps` do for theirs. It is not a translation of the USA text: the engine
reads that cartridge's own text format, font, tables and screens. Its message numbers
are the USA ones less two from 13 on. USA 11 and 12 do not exist there and are made by
the extractor; nothing in the game reaches them.

The menus' glyphs are in `engine/src/platform/linux/generated_ui_japanese.h`, from
`tools/ui/generate_japanese_ui_glyphs.py` and the M+ bitmap faces (PixelMplus 10 and 12).
Run it again after adding characters to `engine/src/ui_strings.c`.
