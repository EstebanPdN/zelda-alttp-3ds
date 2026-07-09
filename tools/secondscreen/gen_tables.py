#!/usr/bin/env python3
"""Emit C tables for second-screen icon/glyph/letter rendering from the python generators."""
import re, sys

src = open('/home/sam/sync/code/zelda3-android/tools/secondscreen/render_icons.py').read()

# exec just the table definitions (E, ITEMS, G) in a bare namespace
ns = {}
m = re.search(r"^E = .*?^}", src, re.S | re.M)
exec(m.group(0), ns)
mg = re.search(r"^G = \{\}.*?'mbar_empty':0x3cf5\}\)", src, re.S | re.M)
exec(mg.group(0), ns)

ITEMS, G = ns['ITEMS'], ns['G']

icon_entries = []
for name, lv in ITEMS.items():
    for i, vs in enumerate(lv):
        icon_entries.append(vs)
glyph_entries = list(G.values())

out = []
out.append("// GENERATED from tools/secondscreen/render_icons.py - do not edit by hand")
out.append(f"#define kIconCount {len(icon_entries)}")
out.append("#define kIconCols 10")
out.append("static const uint16 kIconTilemap[kIconCount][4] = {")
for vs in icon_entries:
    out.append("  {%s}," % ", ".join("0x%04x" % v for v in vs))
out.append("};")
out.append(f"#define kGlyphCount {len(glyph_entries)}")
out.append("#define kGlyphCols 12")
out.append("static const uint16 kGlyphTiles[kGlyphCount] = {")
out.append("  " + ", ".join("0x%04x" % v for v in glyph_entries))
out.append("};")

open('/home/sam/sync/code/zelda3-android/app/jni/src/src/second_screen_tables.h', 'w').write("\n".join(out) + "\n")
print("icons", len(icon_entries), "glyphs", len(glyph_entries))
