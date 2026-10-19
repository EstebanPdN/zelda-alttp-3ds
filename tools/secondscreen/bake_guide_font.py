#!/usr/bin/env python3
# Bakes the guide reading font: printable ASCII from the public-domain X11
# misc-fixed 6x9 BDF ("Public domain font. Share and enjoy.") into a 16x6 grid
# of 6x9 cells, white on transparent, tinted at draw time.
#   python3 tools/secondscreen/bake_guide_font.py path/to/6x9.bdf
# Writes app/src/main/assets/secondscreen/guide_font.png; then re-run
# gen_linux_tables.py to embed it.
import os, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "app", "src", "main", "assets", "secondscreen", "guide_font.png")
CW, CH, COLS, ASCENT = 6, 9, 16, 7

def glyphs(path):
    g, enc, bbx, rows, inbmp = {}, None, None, [], False
    for line in open(path, encoding="latin-1"):
        t = line.split()
        if not t: continue
        if t[0] == "ENCODING": enc = int(t[1])
        elif t[0] == "BBX": bbx = tuple(int(v) for v in t[1:5])
        elif t[0] == "BITMAP": inbmp, rows = True, []
        elif t[0] == "ENDCHAR":
            if enc is not None and 32 <= enc < 127: g[enc] = (bbx, rows)
            inbmp = False
        elif inbmp: rows.append(int(t[0], 16))
    return g

def main(bdf):
    g = glyphs(bdf)
    sheet = Image.new("RGBA", (COLS * CW, 6 * CH), (0, 0, 0, 0))
    px = sheet.load()
    for code in range(32, 127):
        if code not in g: continue
        (w, h, xo, yo), rows = g[code]
        cx, cy = (code - 32) % COLS * CW, (code - 32) // COLS * CH
        top = ASCENT - (yo + h)
        bits = max(8, ((w + 7) // 8) * 8)
        for r, v in enumerate(rows):
            for c in range(w):
                if v >> (bits - 1 - c) & 1:
                    x, y = xo + c, top + r
                    if 0 <= x < CW and 0 <= y < CH: px[cx + x, cy + y] = (255, 255, 255, 255)
    sheet.save(OUT)
    print("wrote", OUT, sheet.size)

if __name__ == "__main__":
    main(sys.argv[1])
