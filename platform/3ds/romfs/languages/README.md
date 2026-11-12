# Language patches

Each `<code>.bps` turns the USA cartridge (headerless, SHA-1
6D4F10A8B10E10DBE624CB23CF03B88BB8252973) into a translated one. On first
boot the language selector applies the chosen patch on the console and reads
the translated text from the result; the player's ROM is never changed.

Make them with `tools/languages/make_bps.py USA TRANSLATED OUT.bps`.
For the supplemental Spanish, Italian and Portuguese text, first run
[`complete_text.py`](../../../../tools/languages/README.md) on a separate,
private translated copy. Preserve the original patch and its readme.

| Code | Source | Credits |
|---|---|---|
| es | D_Skywalk "Re-traducción" Final 1.11 (F01h), 2021 | D_Skywalk; Pablito / Sayans (base) |
| it | SadNES cITy 1.1, 2001 (applied with a 512-byte header) | mickey, _Ombra_, Duke |
| pt | Hexagon 1.3, 2001 (applied with a 512-byte header) | GreenGoblin / Hexagon |
| ru | Chief-Net 1.01, 2022 | GD, Guyver, Tigran, alex231, Mefistotel |
| fr | Official French (PAL) release | Nintendo |
| de | Official German (PAL) release | Nintendo |
| ja | Official Super Famicom release, v1.0 | Nintendo |
| zh | Simplified Chinese translation carried by this fork, text and a 16x16 font | see [CREDITS.md](../../../../CREDITS.md) |

The Spanish, Italian, Portuguese and Russian patches also carry the corrections of
[`quality_text.py`](../../../../tools/languages/README.md).

`zh` is the one language with no cartridge behind it: its `<code>.bin` holds the
translation's text and its 16x16 font, and `zh.bps` dresses that pack as the
cartridge the extractor reads (USA cartridge + pack + a `ZCNP` footer). Like the
other patches, the pack is a build input and never enters this repository: put
`zh.bin` next to this file (or a ready `zh.bps`) and the port picks either up.
The engine draws it with the page-coded glyphs it shares with Japanese; see
`engine/src/dialogue_cjk.h`.

The pack comes from the translation's own project, and `zh.bps` is built from it
with [`make_zh_bps.py`](../../../../tools/languages/make_zh_bps.py). A pack from
the older 3.2-era layout first goes through
[`reencode_pages.py`](../../../../tools/languages/reencode_pages.py), which moves
its glyph escapes to the page bytes the engine reads.

The `.bps` and `.bin` files remain ignored binary inputs for the local build. Recreate
these from the owner-provided cartridges and original fan patches; never put
cartridges or extracted assets in Git. This source records the supplementary
text transformations and their provenance, rather than private game data.
