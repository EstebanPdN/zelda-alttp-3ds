# Installing native mods on the 3DS

## Character skins: just copy the file

Download any Link `.zspr`, for example from the
[snesrev sprite catalog](https://snesrev.github.io/sprites-gfx/snes/zelda3/link/), and
copy it to `/3ds/Zelda 3DS/mods/`. Nothing else is needed: no conversion, no
`mod.ini`. Open Mods from the startup home and choose it. The name, author and
portrait come from the file itself, in every language. It may also sit beside the
ROM or one group folder down, such as `mods/skins/`. Up to 512 mods are listed.

Only a version 1 Link ZSPR is accepted (`0x7000` graphics bytes, 124 palette
bytes). A file name with a comma or `..`, or longer than 63 characters, is left out.
For your own portrait, put `My Skin.bmp` beside `My Skin.zspr`.

## Other mods

Use the application's existing SD directory:

```
/3ds/Zelda 3DS/
```

Mod packages keep their folders under `mods/`, for example:

```
Zelda 3DS/
  mods/
    my-skin/
      mod.ini
      skin.z3mod
      preview.bmp
```

Open Mods from the startup home. The application rescans the SD each time
that menu opens; there is no separate registry or manual INI edit. A skin's
manifest can use the same payload for each supported language:

```
Name = My Skin
Category = SKIN
Info = Author and description
us = skin.z3mod
en = skin.z3mod
es = skin.z3mod
pt = skin.z3mod
it = skin.z3mod
fr = skin.z3mod
de = skin.z3mod
ru = skin.z3mod
fr-c = skin.z3mod
```

`preview.bmp` is optional, uncompressed 24/32-bit, with magenta transparency.
A package may also sit one group folder down: `mods/skins/my-skin/`.

Native `.z3mod` skin files can be dropped directly in `mods/` or beside the
ROM in `Zelda 3DS/`. The file name becomes its menu name. For an optional
portrait put `My Skin.bmp` beside `My Skin.z3mod`. Automatic loose-file
classification accepts only valid native Link graphics/palette entries.

Story, GAME and QUEST packages keep their explicit manifests and language
files so category conflicts and separate quest saves remain correct. A raw
IPS/BPS patch, ROM or ZIP is not a mod. Extract compatible packages before
copying them; renaming a file does not convert its data.

Choose an entry with Select/A/Start. Its green name marks the active choice.
Original restores that category. Only one entry per category is active. SD
packages with the same folder name replace bundled packages. Missing payloads
and malformed loose files are omitted. Choices retain the existing per-profile
`mods.txt` format and are now written with recovery/rollback on SD failures.

## Credits

See [community-pack and original mod credits](../CREDITS.md#community-pack-and-original-mod-authors) and [bundled skin credits](../CREDITS.md#character-skins-included-in-version-40).
