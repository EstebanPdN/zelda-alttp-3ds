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

The Spanish, Italian, Portuguese and Russian patches also carry the corrections of
[`quality_text.py`](../../../../tools/languages/README.md).

The `.bps` files remain ignored binary inputs for the local build. Recreate
these from the owner-provided cartridges and original fan patches; never put
cartridges or extracted assets in Git. This source records the supplementary
text transformations and their provenance, rather than private game data.
