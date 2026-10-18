#!/usr/bin/env python3
# Draws the original UI chrome icons for the ALttP 3DS bottom screen in the
# A Link Between Worlds look: heart (full/half/empty), magic flask, bomb, arrow,
# small key, rupee, cog, sword. Each is drawn at 96 px and downsampled to 24 px
# and 12 px with Lanczos, which gives the soft gloss at device size.
# Original art (no Nintendo assets). Usage, from the repo root:
#   python3 tools/secondscreen/gen_ui_icons.py app/src/main/assets/secondscreen
#   python3 tools/secondscreen/gen_linux_tables.py   # bake into ss_textures.h
# -> OUTDIR/ui_icons.png (sheet) + ui_icons.json (cells)
import math, sys, os, json
from PIL import Image, ImageDraw, ImageFilter

N = 96

def canvas():
    return Image.new('RGBA', (N, N), (0, 0, 0, 0))

def vgrad(w, h, stops):
    im = Image.new('RGBA', (w, h)); px = im.load()
    for y in range(h):
        t = y / (h - 1)
        for i in range(len(stops) - 1):
            t0, c0 = stops[i]; t1, c1 = stops[i + 1]
            if t0 <= t <= t1:
                u = (t - t0) / (t1 - t0) if t1 > t0 else 0
                c = tuple(int(c0[k] + (c1[k] - c0[k]) * u) for k in range(3)) + (255,)
                break
        for x in range(w):
            px[x, y] = c
    return im

def fill_shape(base, mask, stops, outline=None, ow=6):
    if outline:
        big = mask.filter(ImageFilter.MaxFilter(ow * 2 + 1))
        base.paste(Image.new('RGBA', (N, N), outline), (0, 0), big)
    base.paste(vgrad(N, N, stops), (0, 0), mask)

def highlight(base, box, alpha=170):
    hl = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    ImageDraw.Draw(hl).ellipse(box, fill=(255, 255, 255, alpha))
    base.alpha_composite(hl.filter(ImageFilter.GaussianBlur(2)))

def heart_mask():
    m = Image.new('L', (N, N), 0); d = ImageDraw.Draw(m)
    cx, cy, r = 48, 46, 19
    d.ellipse((cx - 2 * r, cy - r * 1.1, cx, cy + r * 0.9), fill=255)
    d.ellipse((cx, cy - r * 1.1, cx + 2 * r, cy + r * 0.9), fill=255)
    d.polygon([(cx - 2 * r + 1, cy + r * 0.15), (cx + 2 * r - 1, cy + r * 0.15), (cx, cy + 2.3 * r)], fill=255)
    return m

def heart(kind):
    im = canvas(); m = heart_mask()
    if kind == 'off':
        fill_shape(im, m, [(0, (70, 28, 36)), (1, (38, 12, 20))], outline=(26, 8, 12, 255)); return im
    fill_shape(im, m, [(0, (255, 150, 140)), (0.45, (228, 34, 46)), (1, (128, 10, 22))], outline=(46, 8, 14, 255))
    if kind == 'half':
        right = Image.new('L', (N, N), 0); ImageDraw.Draw(right).rectangle((48, 0, N, N), fill=255)
        rm = Image.composite(m, Image.new('L', (N, N), 0), right)
        im.paste(vgrad(N, N, [(0, (70, 28, 36)), (1, (38, 12, 20))]), (0, 0), rm)
        ImageDraw.Draw(im).line((48, 26, 48, 82), fill=(46, 8, 14, 255), width=4)
    highlight(im, (24, 24, 40, 38), 150 if kind != 'half' else 110)
    return im

def flask():
    im = canvas(); m = Image.new('L', (N, N), 0); d = ImageDraw.Draw(m)
    d.rounded_rectangle((26, 30, 70, 90), radius=20, fill=255); d.rectangle((36, 14, 60, 34), fill=255)
    fill_shape(im, m, [(0, (200, 232, 255)), (0.35, (70, 150, 230)), (1, (20, 70, 150))], outline=(10, 30, 60, 255))
    ImageDraw.Draw(im).rounded_rectangle((32, 8, 64, 20), radius=5, fill=(236, 220, 180, 255), outline=(90, 60, 20, 255), width=3)
    highlight(im, (32, 40, 44, 70), 120); return im

def bomb():
    im = canvas(); m = Image.new('L', (N, N), 0); ImageDraw.Draw(m).ellipse((16, 28, 80, 92), fill=255)
    fill_shape(im, m, [(0, (120, 140, 180)), (0.5, (40, 56, 96)), (1, (14, 20, 40))], outline=(8, 10, 24, 255))
    d = ImageDraw.Draw(im); d.line((62, 32, 74, 16), fill=(80, 60, 30, 255), width=7)
    d.ellipse((68, 6, 86, 24), fill=(255, 200, 60, 255)); d.ellipse((72, 10, 82, 20), fill=(255, 250, 200, 255))
    highlight(im, (28, 40, 44, 56), 150); return im

def arrow():
    im = canvas(); d = ImageDraw.Draw(im)
    d.line((14, 82, 70, 26), fill=(60, 40, 24, 255), width=12); d.line((14, 82, 70, 26), fill=(190, 150, 100, 255), width=6)
    d.polygon([(88, 8), (60, 20), (76, 36)], fill=(60, 70, 90, 255)); d.polygon([(86, 12), (64, 22), (74, 32)], fill=(220, 230, 240, 255))
    d.polygon([(12, 84), (30, 70), (22, 60), (8, 74)], fill=(200, 60, 50, 255)); d.polygon([(12, 84), (26, 72), (36, 80), (22, 92)], fill=(230, 90, 70, 255))
    return im

def key():
    im = canvas(); m = Image.new('L', (N, N), 0); d = ImageDraw.Draw(m)
    d.ellipse((22, 8, 74, 60), fill=255); d.ellipse((36, 22, 60, 46), fill=0)
    d.rectangle((42, 52, 54, 90), fill=255); d.rectangle((54, 66, 70, 74), fill=255); d.rectangle((54, 80, 66, 88), fill=255)
    fill_shape(im, m, [(0, (255, 236, 160)), (0.5, (230, 180, 60)), (1, (140, 90, 20))], outline=(70, 45, 10, 255))
    highlight(im, (30, 14, 44, 26), 140); return im

def rupee():
    im = canvas(); m = Image.new('L', (N, N), 0)
    ImageDraw.Draw(m).polygon([(48, 6), (80, 30), (80, 66), (48, 90), (16, 66), (16, 30)], fill=255)
    fill_shape(im, m, [(0, (150, 255, 170)), (0.5, (50, 200, 90)), (1, (10, 110, 40))], outline=(10, 60, 25, 255))
    d = ImageDraw.Draw(im)
    d.polygon([(48, 22), (66, 36), (66, 60), (48, 74), (30, 60), (30, 36)], fill=(90, 230, 120, 255))
    d.polygon([(48, 22), (66, 36), (48, 48), (30, 36)], fill=(190, 255, 200, 255))
    return im

def cog():
    im = canvas(); m = Image.new('L', (N, N), 0); d = ImageDraw.Draw(m)
    for i in range(8):
        a = i * math.pi / 4
        d.polygon([(48 + math.cos(a - 0.22) * 30, 48 + math.sin(a - 0.22) * 30), (48 + math.cos(a - 0.16) * 44, 48 + math.sin(a - 0.16) * 44),
                   (48 + math.cos(a + 0.16) * 44, 48 + math.sin(a + 0.16) * 44), (48 + math.cos(a + 0.22) * 30, 48 + math.sin(a + 0.22) * 30)], fill=255)
    d.ellipse((16, 16, 80, 80), fill=255); d.ellipse((36, 36, 60, 60), fill=0)
    fill_shape(im, m, [(0, (240, 210, 130)), (0.5, (200, 150, 60)), (1, (110, 70, 20))], outline=(60, 38, 10, 255)); return im

def sword():
    im = canvas(); d = ImageDraw.Draw(im)
    d.polygon([(48, 4), (58, 16), (58, 56), (38, 56), (38, 16)], fill=(40, 50, 70, 255))
    d.polygon([(48, 8), (54, 18), (54, 54), (42, 54), (42, 18)], fill=(215, 225, 240, 255)); d.line((48, 10, 48, 52), fill=(255, 255, 255, 255), width=2)
    d.rounded_rectangle((26, 54, 70, 64), radius=4, fill=(50, 80, 160, 255), outline=(20, 30, 70, 255), width=2)
    d.rectangle((43, 64, 53, 86), fill=(50, 80, 160, 255)); d.ellipse((40, 82, 56, 94), fill=(240, 200, 80, 255))
    return im

ICONS = [('heart', heart('full')), ('heart_half', heart('half')), ('heart_off', heart('off')), ('flask', flask()),
         ('bomb', bomb()), ('arrow', arrow()), ('key', key()), ('rupee', rupee()), ('cog', cog()), ('sword', sword())]

def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    # Sheet: row 0 = 24 px cells, row 1 = 12 px cells (in 24 px slots, top-left aligned).
    cols = len(ICONS); sheet = Image.new('RGBA', (cols * 24, 48), (0, 0, 0, 0)); cells = {}
    for i, (name, im) in enumerate(ICONS):
        big = im.resize((24, 24), Image.LANCZOS); small = im.resize((12, 12), Image.LANCZOS)
        sheet.paste(big, (i * 24, 0), big); sheet.paste(small, (i * 24, 24), small)
        cells[name + '_24'] = [i, 0]; cells[name + '_12'] = [i, 1]
    sheet.save(os.path.join(outdir, 'ui_icons.png'))
    json.dump({'cell': 24, 'cols': cols, 'map': cells}, open(os.path.join(outdir, 'ui_icons.json'), 'w'), indent=1)
    print('wrote', outdir, cols, 'icons')

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '.')
