#!/usr/bin/env python3
# Bakes the bottom-screen label font: Nunito ExtraBold (SIL OFL 1.1,
# Copyright 2014 The Nunito Project Authors, github.com/googlefonts/nunito)
# into two white-on-transparent glyph sheets, LARGE (8 px cap height: tabs,
# ribbons, counters, settings rows) and SMALL (6 px cap height: gear labels,
# key badges). The UI tints them with SDL_SetTextureColorMod.
# Usage, from the repo root (needs Pillow; downloads the TTF once unless --ttf):
#   python3 tools/secondscreen/bake_ui_font.py [--ttf Nunito[wght].ttf]
#   python3 tools/secondscreen/gen_linux_tables.py
# Writes app/src/main/assets/secondscreen/ui_font_{large,small}.png + ui_font.json.
import argparse, json, os, tempfile, urllib.request
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, '..', '..', 'app', 'src', 'main', 'assets', 'secondscreen')
URL = 'https://github.com/google/fonts/raw/main/ofl/nunito/Nunito%5Bwght%5D.ttf'
CHARS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-/:x%'
# Pixel sizes whose hinted cap height is exactly 8 and 6 px.
SIZES = [('large', 11, 8), ('small', 8, 6)]
COLS = 16
PAD = 1  # transparent column left of each glyph; the UI draws cells at pen_x - PAD


def load_font(path, size):
    f = ImageFont.truetype(path, size)
    f.set_variation_by_name('ExtraBold')
    return f


def bake(path, name, size, cap):
    f = load_font(path, size)
    # One glyph per probe canvas, origin at (PAD, top margin), to measure ink.
    probe = 2 * size
    boxes = {}
    for ch in CHARS + 'H':
        im = Image.new('L', (probe * 2, probe * 2), 0)
        ImageDraw.Draw(im).text((PAD, size // 2), ch, font=f, fill=255)
        boxes[ch] = im.getbbox()
    cap_top = boxes['H'][1]
    assert boxes['H'][3] - cap_top == cap, (name, boxes['H'], cap)
    top = min(b[1] for b in boxes.values())
    bottom = max(b[3] for b in boxes.values())
    cell_w = max(b[2] for b in boxes.values()) + 1
    cell_h = bottom - top
    rows = (len(CHARS) + COLS - 1) // COLS
    sheet = Image.new('L', (COLS * cell_w, rows * cell_h), 0)
    for i, ch in enumerate(CHARS):
        im = Image.new('L', (cell_w, cell_h), 0)
        ImageDraw.Draw(im).text((PAD, size // 2 - top), ch, font=f, fill=255)
        sheet.paste(im, ((i % COLS) * cell_w, (i // COLS) * cell_h))
    rgba = Image.merge('RGBA', (Image.new('L', sheet.size, 255),) * 3 + (sheet,))
    rgba.save(os.path.join(ASSETS, f'ui_font_{name}.png'))
    return {
        'size': size, 'cap': cap, 'cell_w': cell_w, 'cell_h': cell_h, 'cols': COLS,
        'cap_top': cap_top - top, 'pad': PAD, 'space': round(f.getlength(' ')),
        'advance': [round(f.getlength(ch)) for ch in CHARS],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ttf', help='Nunito variable TTF (downloaded when omitted)')
    args = ap.parse_args()
    path = args.ttf or os.path.join(tempfile.gettempdir(), 'Nunito[wght].ttf')
    if not os.path.exists(path):
        urllib.request.urlretrieve(URL, path)
    out = {'font': 'Nunito ExtraBold (SIL OFL 1.1)', 'chars': CHARS}
    for name, size, cap in SIZES:
        out[name] = bake(path, name, size, cap)
    with open(os.path.join(ASSETS, 'ui_font.json'), 'w') as fp:
        json.dump(out, fp, indent=1)
    print('wrote ui_font_large.png, ui_font_small.png, ui_font.json')


if __name__ == '__main__':
    main()
