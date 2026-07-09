import sys, os, json
sys.path.insert(0, '.')
import util
from util import get_words
import sprite_sheets, tables
from PIL import Image
import numpy as np

util.load_rom(sys.argv[1])
OUT = sys.argv[2]

# --- decode HUD 2bpp tile sheets: sets 106,107,105 -> tile ids 0x000-0x17F, pixel values 0..3
sheets = []
for s in (106, 107, 105):
    data = sprite_sheets.decode_2bit_tileset(s, height=64, base=0)
    sheets.append(np.frombuffer(bytes(data), np.uint8).reshape(64,128))
tiles_img = np.concatenate(sheets, axis=0)   # 192x128 = 24 rows x 16 cols of 8x8
def tile_px(idx):
    r, c = idx // 16, idx % 16
    return tiles_img[r*8:(r+1)*8, c*8:(c+1)*8]

hud_pal = get_words(0x9BD660, 64)  # 16 groups of 4 (entry 0 unused/transparent)
def col(group, pix):
    w = hud_pal[group*4 + pix]
    r, g, b = w & 31, (w>>5)&31, (w>>10)&31
    return ((r<<3)|(r>>2), (g<<3)|(g>>2), (b<<3)|(b>>2), 255)

def draw_tile(canvas, x, y, v):
    t = tile_px(v & 0x3ff).copy()
    if v & 0x4000: t = t[:, ::-1]
    if v & 0x8000: t = t[::-1, :]
    p = (v >> 10) & 7
    for yy in range(8):
        for xx in range(8):
            pix = t[yy,xx]
            if pix: canvas[y+yy, x+xx] = col(p, pix)

def strip_bg(c):
    # remove near-black background connected to the icon border
    dark = (c[...,3] > 0) & (c[...,:3].astype(int).sum(axis=2) < 60)
    seen = np.zeros((16,16), bool)
    stack = [(y,x) for y in range(16) for x in (0,15)] + [(y,x) for y in (0,15) for x in range(16)]
    while stack:
        y,x = stack.pop()
        if y<0 or y>15 or x<0 or x>15 or seen[y,x] or not dark[y,x]: continue
        seen[y,x] = True
        stack += [(y+1,x),(y-1,x),(y,x+1),(y,x-1)]
    c[seen] = 0
    return c

def icon16(vs):   # 4 tilemap words: TL,TR,BL,BR
    c = np.zeros((16,16,4), np.uint8)
    draw_tile(c, 0,0, vs[0]); draw_tile(c, 8,0, vs[1])
    draw_tile(c, 0,8, vs[2]); draw_tile(c, 8,8, vs[3])
    return strip_bg(c)

E = (0x20f5,)*4
ITEMS = {
 'bow': [E,(0x28ba,0x28e9,0x28e8,0x28cb),(0x28ba,0x284a,0x2849,0x28cb),(0x28ba,0x28e9,0x28e8,0x28cb),(0x28ba,0x28bb,0x24ca,0x28cb)],
 'boomerang': [E,(0x2cb8,0x2cb9,0x2cf5,0x2cc9),(0x24b8,0x24b9,0x24f5,0x24c9)],
 'hookshot': [E,(0x24f5,0x24f6,0x24c0,0x24f5)],
 'bombs': [E,(0x2cb2,0x2cb3,0x2cc2,0x6cc2)],
 'mushroom': [E,(0x2444,0x2445,0x2446,0x2447),(0x203b,0x203c,0x203d,0x203e)],
 'firerod': [E,(0x24b0,0x24b1,0x24c0,0x24c1)],
 'icerod': [E,(0x2cb0,0x2cbe,0x2cc0,0x2cc1)],
 'bombos': [E,(0x287d,0x287e,0xe87e,0xe87d)],
 'ether': [E,(0x2876,0x2877,0xe877,0xe876)],
 'quake': [E,(0x2866,0x2867,0xe867,0xe866)],
 'torch': [E,(0x24bc,0x24bd,0x24cc,0x24cd)],
 'hammer': [E,(0x20b6,0x20b7,0x20c6,0x20c7)],
 'flute': [E,(0x20d0,0x20d1,0x20e0,0x20e1),(0x2cd4,0x2cd5,0x2ce4,0x2ce5),(0x2cd4,0x2cd5,0x2ce4,0x2ce5)],
 'bugnet': [E,(0x3c40,0x3c41,0x2842,0x3c43)],
 'book': [E,(0x3ca5,0x3ca6,0x3cd8,0x3cd9)],
 'bottle': [E,(0x2044,0x2045,0x2046,0x2047),(0x2837,0x2838,0x2cc3,0x2cd3),(0x24d2,0x64d2,0x24e2,0x24e3),(0x3cd2,0x7cd2,0x3ce2,0x3ce3),(0x2cd2,0x6cd2,0x2ce2,0x2ce3),(0x2855,0x6855,0x2c57,0x2c5a),(0x2837,0x2838,0x2839,0x283a),(0x2837,0x2838,0x2839,0x283a)],
 'somaria': [E,(0x24dc,0x24dd,0x24ec,0x24ed)],
 'byrna': [E,(0x2cdc,0x2cdd,0x2cec,0x2ced)],
 'cape': [E,(0x24b4,0x24b5,0x24c4,0x24c5)],
 'mirror': [E,(0x28de,0x28df,0x28ee,0x28ef),(0x2c62,0x2c63,0x2c72,0x2c73),(0x2886,0x2887,0x2888,0x2889)],
 'gloves': [E,(0x2130,0x2131,0x2140,0x2141),(0x28da,0x28db,0x28ea,0x28eb)],
 'boots': [E,(0x3429,0x342a,0x342b,0x342c)],
 'flippers': [E,(0x2c9a,0x2c9b,0x2c9d,0x2c9e)],
 'moonpearl': [E,(0x2433,0x2434,0x2435,0x2436)],
 'sword': [E,(0x2c64,0x2cce,0x2c75,0x3d25),(0x2c8a,0x2c65,0x2474,0x3d26),(0x248a,0x2465,0x3c74,0x2d48),(0x288a,0x2865,0x2c74,0x2d39)],
 'shield': [E,(0x2cfd,0x6cfd,0x2cfe,0x6cfe),(0x34ff,0x74ff,0x349f,0x749f),(0x2880,0x2881,0x288d,0x288e)],
 'armor': [(0x3c68,0x7c68,0x3c78,0x7c78),(0x2c68,0x6c68,0x2c78,0x6c78),(0x2468,0x6468,0x2478,0x6478)],
}

# pack icons16 sheet
keys = []
for name, lv in ITEMS.items():
    for i, vs in enumerate(lv):
        keys.append((f'{name}_{i}', vs))
COLS = 10
rows = (len(keys)+COLS-1)//COLS
sheet = np.zeros((rows*16, COLS*16, 4), np.uint8)
manifest = {}
for i,(k,vs) in enumerate(keys):
    r,c = i//COLS, i%COLS
    sheet[r*16:(r+1)*16, c*16:(c+1)*16] = icon16(vs)
    manifest[k] = [c, r]
Image.fromarray(sheet).save(os.path.join(OUT,'icons.png'))
json.dump({'cols':COLS,'cell':16,'map':manifest}, open(os.path.join(OUT,'icons.json'),'w'))

# glyphs 8x8: digits (white pal1 + yellow pal5), hearts, counter icons, magic bar
G = {}
for d in range(10):
    G[f'digit{d}'] = 0x2400 | (0x90+d)
    G[f'digit{d}y'] = 0x3400 | (0x90+d)
G.update({'heart_full':0x24A0,'heart_half':0x24A1,'heart_empty':0x24A2,
 'rupee':0x3ca8,'bomb0':0x2c88,'bomb1':0x2c89,'arrow0':0x20a7,'arrow1':0x20a9,'key':0x2871,
 'magic_top':0x28F7,'magic_mid':0x2851,'magic_bot':0x28FA,
 'mbar_full':0x3c5e,'mbar_1':0x3c5f,'mbar_2':0x3c4c,'mbar_3':0x3c4d,'mbar_4':0x3c4e,'mbar_empty':0x3cf5})
gk = list(G.keys())
COLS2 = 12
rows2 = (len(gk)+COLS2-1)//COLS2
gs = np.zeros((rows2*8, COLS2*8, 4), np.uint8)
gman = {}
for i,k in enumerate(gk):
    r,c = i//COLS2, i%COLS2
    draw_tile(gs, c*8, r*8, G[k])
    gman[k] = [c, r]
Image.fromarray(gs).save(os.path.join(OUT,'glyphs.png'))
json.dump({'cols':COLS2,'cell':8,'map':gman}, open(os.path.join(OUT,'glyphs.json'),'w'))
print('icons:', len(keys), 'glyphs:', len(gk))
