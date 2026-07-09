import sys, os, json
sys.path.insert(0, '.')
import util, sprite_sheets
from util import get_words
from PIL import Image
import numpy as np

util.load_rom(sys.argv[1])
OUT = sys.argv[2]

# ---- letter glyphs A-Z from HUD sheet 105 (tiles 0x150-0x169) ----
sheets = []
for s in (106, 107, 105):
    d = sprite_sheets.decode_2bit_tileset(s, height=64, base=0)
    sheets.append(np.frombuffer(bytes(d), np.uint8).reshape(64,128))
tiles = np.concatenate(sheets, axis=0)
PAL = {0:(0,0,0,0), 1:(40,40,48,255), 2:(248,248,248,255), 3:(0,0,0,0)}
def tile_rgba(idx):
    t = tiles[(idx//16)*8:(idx//16)*8+8, (idx%16)*8:(idx%16)*8+8]
    out = np.zeros((8,8,4), np.uint8)
    for k,v in PAL.items(): out[t==k] = v
    return out

letters = {}
for i in range(16):  # A-P
    letters[chr(ord('A')+i)] = 0x150+i
for i in range(10):  # Q-Z
    letters[chr(ord('Q')+i)] = 0x160+i
sheet = np.zeros((2*8, 16*8, 4), np.uint8)
man = {}
for i,(ch,tid) in enumerate(letters.items()):
    r,c = i//16, i%16
    sheet[r*8:(r+1)*8, c*8:(c+1)*8] = tile_rgba(tid)
    man[ch] = [c, r]
Image.fromarray(sheet).save(os.path.join(OUT,'letters.png'))
json.dump({'cols':16,'cell':8,'map':man}, open(os.path.join(OUT,'letters.json'),'w'))

# ---- link face marker (from screenshot slice) ----
face = Image.open('/home/sam/.claude/jobs/4c2630b6/tmp/marker_sprite.png')
face = face.resize((32,32), Image.NEAREST)
face.save(os.path.join(OUT,'linkface.png'))

# ---- theme textures (tileable 64x64, pixel-quantized) ----
rng = np.random.RandomState(7)
def save(name, img): Image.fromarray(img).save(os.path.join(OUT,name))

# wood: vertical planks with grain
wood = np.zeros((64,64,3), np.uint8)
base = np.array([146,92,42]); dark = np.array([112,66,28]); darker = np.array([88,50,20]); light = np.array([170,112,56])
for x in range(64):
    for y in range(64):
        v = base.copy()
        if x % 16 in (0,15): v = darker           # plank seams
        elif rng.rand() < 0.06: v = dark          # grain flecks
        elif (x*7+y*3) % 23 == 0: v = dark        # grain streaks
        elif rng.rand() < 0.03: v = light
        wood[y,x] = v
save('tex_wood.png', wood)

# parchment: cream with blotches
parch = np.zeros((64,64,3), np.uint8)
pb = np.array([236,222,178]); pd = np.array([222,204,152]); pdd = np.array([204,184,128])
for y in range(64):
    for x in range(64):
        r = rng.rand()
        parch[y,x] = pdd if r < 0.02 else (pd if r < 0.14 else pb)
save('tex_parchment.png', parch)

# stone: slate blocks with bevels
stone = np.zeros((64,64,3), np.uint8)
sb = np.array([92,100,116]); sd = np.array([70,78,92]); sl = np.array([116,124,140]); sdd = np.array([54,60,72])
for y in range(64):
    for x in range(64):
        bx, by = x % 32, (y + (16 if (x//32)%2 else 0)) % 16
        v = sb.copy()
        if by == 0 or bx == 0: v = sdd            # mortar
        elif by == 1 or bx == 1: v = sl           # top/left bevel
        elif by == 15 or bx == 31: v = sd         # bottom/right shade
        elif rng.rand() < 0.05: v = sd
        stone[y,x] = v
save('tex_stone.png', stone)
print('ui assets done')
