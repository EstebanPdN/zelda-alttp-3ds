#!/usr/bin/env python3
# Bakes the Linux-only second-screen tables (ss_sheets.h, ss_textures.h) in
# app/jni/src/src/platform/linux from the committed assets in
# app/src/main/assets/secondscreen. (render_*.py generate those assets from a
# ROM; gen_tables.py emits the shared second_screen_tables.h.) Run from repo root:
#   python tools/secondscreen/gen_linux_tables.py
# Needs Pillow (for the texture PNGs). The generated headers are committed, so
# a normal `make` build does not need to run this -- only re-run it if the
# secondscreen assets change.
import json, os

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "..", "..", "app", "src", "main", "assets", "secondscreen")
SRC = os.path.join(HERE, "..", "..", "app", "jni", "src", "src", "platform", "linux")

ITEM_NAMES = ["bow", "boomerang", "hookshot", "bombs", "mushroom", "firerod",
              "icerod", "bombos", "ether", "quake", "torch", "hammer", "flute",
              "bugnet", "book", "bottle", "somaria", "byrna", "cape", "mirror"]
ITEM_MAX = [4, 2, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 3, 1, 1, 7, 1, 1, 1, 3]
GEAR_KEYS = ["sword_1", "sword_2", "sword_3", "sword_4", "shield_1", "shield_2",
             "shield_3", "armor_0", "armor_1", "armor_2", "gloves_1", "gloves_2",
             "boots_1", "flippers_1", "moonpearl_1", "bottle_1", "bottle_2",
             "bottle_3", "bottle_4", "bottle_5", "bottle_6", "bottle_7"]


def cells(manifest):
    return {k: v[1] * manifest["cols"] + v[0] for k, v in manifest["map"].items()}


def gen_sheets():
    ic = cells(json.load(open(os.path.join(ASSETS, "icons.json"))))
    gl = cells(json.load(open(os.path.join(ASSETS, "glyphs.json"))))
    lt = cells(json.load(open(os.path.join(ASSETS, "letters.json"))))

    out = ["// Generated from secondscreen/{icons,glyphs,letters}.json by",
           "// gen_secondscreen.py -- do not hand-edit.",
           "// Cell index = row*cols+col into the corresponding generated sheet.",
           "#pragma once", "",
           "#define SS_ICON_COLS 10", "#define SS_GLYPH_COLS 12",
           "#define SS_LETTER_COLS 16", ""]

    rows = []
    for i, name in enumerate(ITEM_NAMES):
        r = [-1] * 8
        for v in range(1, ITEM_MAX[i] + 1):
            r[v] = ic.get(f"{name}_{v}", -1)
        rows.append("  {" + ",".join(f"{x:3d}" for x in r) + "},  // " + name)
    out.append("static const short kSS_ItemCell[20][8] = {\n" + "\n".join(rows) + "\n};\n")
    out.append("static const unsigned char kSS_ItemMaxLevel[20] = {" +
               ",".join(map(str, ITEM_MAX)) + "};\n")
    for k in GEAR_KEYS:
        out.append(f"#define SS_ICON_{k.upper()} {ic.get(k, -1)}")
    out.append("")
    for k, v in gl.items():
        out.append(f"#define SS_GLYPH_{k.upper()} {v}")
    out.append("")
    letter_cells = [lt[chr(ord('A') + i)] for i in range(26)]
    out.append("static const unsigned char kSS_LetterCell[26] = {" +
               ",".join(map(str, letter_cells)) + "};")
    open(os.path.join(SRC, "ss_sheets.h"), "w", newline="\n").write("\n".join(out) + "\n")
    print("wrote src/ss_sheets.h")


def gen_textures():
    from PIL import Image
    out = ["// Theme background textures from secondscreen/*.png, baked to ARGB",
           "// arrays by gen_secondscreen.py so no image loader is needed at runtime.",
           "// (tex_wood.png is unused by the UI and not embedded.)",
           "#pragma once", ""]
    for name, sym in [("tex_menu", "kSSTexMenu"), ("tex_parchment", "kSSTexParch"),
                      ("tex_stone", "kSSTexStone")]:
        im = Image.open(os.path.join(ASSETS, f"{name}.png")).convert("RGB")
        w, h = im.size
        px = list(im.getdata())
        out.append(f"#define {sym}_W {w}")
        out.append(f"#define {sym}_H {h}")
        vals = [f"0xff{r:02x}{g:02x}{b:02x}" for (r, g, b) in px]
        rows = [",".join(vals[i:i + 12]) for i in range(0, len(vals), 12)]
        out.append(f"static const unsigned int {sym}[{w * h}] = {{")
        out.append(",\n".join(rows))
        out.append("};\n")
    open(os.path.join(SRC, "ss_textures.h"), "w", newline="\n").write("\n".join(out))
    print("wrote src/ss_textures.h")


if __name__ == "__main__":
    gen_sheets()
    gen_textures()
