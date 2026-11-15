#!/usr/bin/env python3
"""Generate the native 3DS UI font and icons, exactly one pixel per bit.
No ROM, extracted game asset, external font or image library is required.
Run from any directory; --check verifies deterministic checked-in outputs.
"""
from pathlib import Path
import argparse, json, math, struct, zlib
from generate_intl_glyphs import CYRILLIC
ROOT=Path(__file__).resolve().parents[2]
LETTERS = [[14, 17, 17, 31, 17, 17, 17], [30, 17, 17, 30, 17, 17, 30], [14, 17, 16, 16, 16, 17, 14], [30, 17, 17, 17, 17, 17, 30], [31, 16, 16, 30, 16, 16, 31], [31, 16, 16, 30, 16, 16, 16], [14, 17, 16, 23, 17, 17, 14], [17, 17, 17, 31, 17, 17, 17], [14, 4, 4, 4, 4, 4, 14], [7, 2, 2, 2, 18, 18, 12], [17, 18, 20, 24, 20, 18, 17], [16, 16, 16, 16, 16, 16, 31], [17, 27, 21, 21, 17, 17, 17], [17, 25, 21, 19, 17, 17, 17], [14, 17, 17, 17, 17, 17, 14], [30, 17, 17, 30, 16, 16, 16], [14, 17, 17, 17, 21, 18, 13], [30, 17, 17, 30, 20, 18, 17], [15, 16, 16, 14, 1, 1, 30], [31, 4, 4, 4, 4, 4, 4], [17, 17, 17, 17, 17, 17, 14], [17, 17, 17, 17, 17, 10, 4], [17, 17, 17, 21, 21, 21, 10], [17, 17, 10, 4, 10, 17, 17], [17, 17, 10, 4, 4, 4, 4], [31, 1, 2, 4, 8, 16, 31]]
DIGITS = [[14, 17, 19, 21, 25, 17, 14], [4, 12, 4, 4, 4, 4, 14], [14, 17, 1, 2, 4, 8, 31], [30, 1, 1, 14, 1, 1, 30], [2, 6, 10, 18, 31, 2, 2], [31, 16, 30, 1, 1, 17, 14], [6, 8, 16, 30, 17, 17, 14], [31, 1, 2, 4, 8, 8, 8], [14, 17, 17, 14, 17, 17, 14], [14, 17, 17, 15, 1, 2, 12]]
ACCENTS={"À":("A",1),"Á":("A",0),"Â":("A",3),"Ã":("A",4),"Ä":("A",5),"Ç":("C",2),"È":("E",1),"É":("E",0),"Ê":("E",3),"Ë":("E",5),"Ì":("I",1),"Í":("I",0),"Î":("I",3),"Ï":("I",5),"Ñ":("N",4),"Ò":("O",1),"Ó":("O",0),"Ô":("O",3),"Õ":("O",4),"Ö":("O",5),"Ù":("U",1),"Ú":("U",0),"Û":("U",3),"Ü":("U",5),"Å":("A",6)}
PUNCT={".":[0,0,0,0,0,12,12],"-":[0,0,0,31,0,0,0],"'": [12,12,8,0,0,0,0],":":[0,4,4,0,4,4,0],"/":[1,2,2,4,8,8,16],"?":[14,17,1,2,4,0,4],"+":[0,4,4,31,4,4,0],"(":[2,4,8,8,8,4,2],")":[8,4,2,2,2,4,8],"%": [17,2,4,4,8,16,17],"!":[4,4,4,4,4,0,4]}
PUNCT["^"]=[0,4,10,17,0,0,0]
PUNCT.update({"_": [0,0,0,0,0,0,31], ",": [0,0,0,0,0,4,8], "[": [14,8,8,8,8,8,14], "]": [14,2,2,2,2,2,14], "<": [1,2,4,8,4,2,1], ">": [16,8,4,2,4,8,16], "=": [0,0,31,0,31,0,0]})
# Original port artwork, authored on a 16x16 grid. Zero is transparent.
PENDANT = [
 "0000000660000000", "0000006666000000", "0000066006600000", "0000060000600000",
 "0000006666000000", "0000011111100000", "0000124444210000", "0001245554421000",
 "0012455444442100", "0012444444432100", "0012344444332100", "0012334443332100",
 "0001233333321000", "0000123333210000", "0000012222100000", "0000001111000000"]
CRYSTAL = [
 "0000000110000000", "0000001441000000", "0000014554100000", "0000145555410000",
 "0001455544441000", "0014555444444100", "0014554444443100", "0014544444433100",
 "0014444444333100", "0014444443333100", "0001444433331000", "0000144333310000",
 "0000014333100000", "0000001331000000", "0000000110000000", "0000000000000000"]
PIECE = [
 "0000011111100000", "0001122222211000", "0012226666222100", "0122665555662210",
 "0126555555556210", "1265555555555621", "1265555555555621", "1265555544555621",
 "1265555444555621", "1265554444555621", "0126544444566210", "0122644444662210",
 "0012264446622100", "0001126662211000", "0000011111100000", "0000000000000000"]
def color_sprite(pattern, base):
 palette=[0,0xff241b14,0xff66502c,0xff000000|sum((c//2)<<sh for c,sh in zip(base,(16,8,0))),
          0xff000000|sum(c<<sh for c,sh in zip(base,(16,8,0))),0xfffff5ce,0xffc69942]
 return [[palette[int(v)] for v in row] for row in pattern]
SPRITES=[color_sprite(PENDANT,c) for c in [(64,202,96),(88,128,240),(232,80,88)]]
SPRITES += [color_sprite(CRYSTAL,(116,180,248)), color_sprite(PIECE,(232,72,72))]
# Eight distinct teeth and a clear central bore; no filled disc at 1x.
GEAR=[0x0180,0x03c0,0x23c4,0x7ffe,0x3ffc,0x1c38,0x7c3e,0xf81f,0xf81f,0x7c3e,0x1c38,0x3ffc,0x7ffe,0x23c4,0x03c0,0x0180]
ARROW=[0,0,0,0x0800,0x0c00,0x0e00,0x0f00,0x0f80,0x0f00,0x0e00,0x0c00,0x0800,0,0,0,0]
def chunk(kind,data):
 return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
def png(width,height,pixels):
 raw=b''.join(b'\0'+pixels[y*width*4:(y+1)*width*4] for y in range(height))
 return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw,9))+chunk(b'IEND',b'')
def generate():
 glyphs=[[0]*11 for _ in range(256)]
 for i,rows in enumerate(LETTERS):glyphs[65+i][2:9]=rows
 for i,rows in enumerate(DIGITS):glyphs[48+i][2:9]=rows
 for c,rows in PUNCT.items():glyphs[ord(c)][2:9]=rows
 for c,(base,accent) in ACCENTS.items():
  rows=glyphs[ord(base)].copy()
  if accent==0:rows[0:2]=[2,4]
  elif accent==1:rows[0:2]=[8,4]
  elif accent==2:rows[9:11]=[4,8]
  elif accent==3:rows[0:2]=[4,10]
  elif accent==4:rows[0:2]=[10,5]
  elif accent==5:rows[0:2]=[10,0]
  elif accent==6:rows[0:2]=[14,10]  # ring: closes on the A's rounded top
  glyphs[ord(c)]=rows
 # Cyrillic uses otherwise unused 0x80..0xa0 cells of the 256-glyph atlas.
 for c, rows in CYRILLIC.items():
  glyphs[128+ord(c)-0x410][2:9]=rows
 glyphs[128+ord('Й')-0x410][0:2]=[10,4]
 glyphs[160]=glyphs[128+ord('Е')-0x410].copy()
 glyphs[160][0:2]=[10,0]
 header='// Generated by tools/ui/generate_native_ui.py. Edit its pixel patterns.\n#pragma once\n#include <stdint.h>\nstatic const uint8_t kNativeUiGlyphs[256][11] = {\n'
 header+=''.join('  {'+','.join(map(str,row))+'},\n' for row in glyphs)+'};\n'
 large=[]
 for rows in glyphs:
  expanded=[]
  for yy,row in enumerate(rows):
   bits=sum(3<<(8-2*x) for x in range(5) if row&(1<<(4-x)))
   expanded.extend([bits]*(1 if yy==5 else 2))
  assert len(expanded)==21
  large.append(expanded)
 header+='static const uint16_t kNativeUiLargeGlyphs[256][21] = {\n'
 header+=''.join('  {'+','.join(map(str,row))+'},\n' for row in large)+'};\n'
 # A distinct 7x9 native face for compact lists. Every label uses the same
 # face, with explicit 3px accent/descender bands and symmetric stroke mapping.
 medium=[]
 for rows in glyphs:
  mapped=[]
  for sy in [0,0,1,2,3,3,4,5,6,7,7,8,9,10,10]:
   mapped.append(sum(1<<(6-x) for x,sx in enumerate([0,1,1,2,3,3,4]) if rows[sy]&(1<<(4-sx))))
  medium.append(mapped)
 header+='static const uint8_t kNativeUiMediumGlyphs[256][15] = {\n'
 header+=''.join('  {'+','.join(map(str,row))+'},\n' for row in medium)+'};\n'
 header+='static const struct { char base; int accent; unsigned code; } kNativeUiAccents[] = {\n'
 header+=''.join("  {'%s',%d,%d},\n"%(b,a,ord(c)) for c,(b,a) in ACCENTS.items())+'};\n'
 # A larger wheel for the 32px Settings button: eight teeth, 7px bore.
 gear20=[]
 for y in range(20):
  row=0
  for x in range(20):
   dx=x-9.5;dy=y-9.5;radius=math.hypot(dx,dy)
   phase=(math.atan2(dy,dx)+math.pi/8)%(math.pi/4)-math.pi/8
   if radius>=3.5 and (radius<=7.5 or (radius<=9.5 and abs(phase)<.16)):row|=1<<(19-x)
  gear20.append(row)
 header+='static const uint32_t kNativeUiGear20[20] = {'+','.join(hex(x) for x in gear20)+'};\n'
 triforce=[]
 for y in range(28):
  row=0
  for x in range(32):
   if y<14:
    lit=abs(x-15.5)<=((y+1)*8/14)
   else:lit=min(abs(x-7.5),abs(x-23.5))<=((y-13)*8/14)
   if lit:row|=1<<(31-x)
  triforce.append(row)
 header+='static const uint32_t kNativeUiTriforce32[28] = {'+','.join(hex(x) for x in triforce)+'};\n'
 triangle48=[]
 for y in range(42):
  bits=0
  centers=[24] if y<21 else [12,36]
  extent=12*((y%21)+1)//21
  for center in centers:
   for x in range(center-extent,center+extent):bits|=1<<(47-x)
  triangle48.append(bits)
 header+='static const uint64_t kNativeUiTriforce48[42] = {'+','.join('UINT64_C('+hex(v)+')' for v in triangle48)+'};\n'
 # Native 48x42 sprite based on Wikimedia Commons Triforce.svg (simple
 # geometry, public domain; vector by Ian Moody and Sonarpulse). No filtering.
 # https://commons.wikimedia.org/wiki/File:Triforce.svg
 def inside(px,py,points):
  cross=[(b[0]-a[0])*(py-a[1])-(b[1]-a[1])*(px-a[0]) for a,b in zip(points,points[1:]+points[:1])]
  return all(c>=0 for c in cross) or all(c<=0 for c in cross)
 sprite=[]
 for y in range(42):
  for x in range(48):
   px=(x+.5)*200/48;py=(y+.5)*173.2/42;color=0
   for ox,oy in [(50,86.6),(0,173.2),(100,173.2)]:
    if not inside(px,py,[(ox,oy),(ox+100,oy),(ox+50,oy-86.6)]):continue
    color=0xff7a581e  # Same flat gold as the cinema frame.
    break
   sprite.append(color)
 header+='static const uint32_t kNativeUiTriforceSprite[48*42] = {\n'
 for y in range(42):header+=' '+','.join(hex(v) for v in sprite[y*48:y*48+48])+',\n'
 header+='};\n'
 for name,plus in [('Plus',True),('Minus',False)]:
  rows=[]
  for y in range(16):
   bits=0
   for x in range(16):
    if (y in (7,8) and 2<=x<14) or (plus and x in (7,8) and 2<=y<14):bits|=1<<(15-x)
   rows.append(bits)
  header+='static const uint16_t kNativeUiZoom'+name+'16[16] = {'+','.join(hex(v) for v in rows)+'};\n'

 for name,rows in [('Gear',GEAR),('Arrow',ARROW)]:header+='static const uint16_t kNativeUi%s[16] = {'%name+','.join(hex(x) for x in rows)+'};\n'
 width,height=128,176;pixels=bytearray(width*height*4)
 for code,rows in enumerate(glyphs):
  for y,row in enumerate(rows):
   for x in range(5):
    if row&(1<<(4-x)):
     o=(((code//16)*11+y)*width+(code%16)*8+x)*4;pixels[o:o+4]=b'\xff'*4
 sprite_width=16*len(SPRITES)
 sprite_pixels=[sprite[y][x] for y in range(16) for sprite in SPRITES for x in range(16)]
 header+='static const uint32_t kNativeUiSpritePixels[%d] = {\n'%len(sprite_pixels)
 for y in range(16):header+='  '+','.join(hex(v) for v in sprite_pixels[y*sprite_width:(y+1)*sprite_width])+',\n'
 header+='};\n'
 rgba=b''.join(bytes([(v>>16)&255,(v>>8)&255,v&255,v>>24]) for v in sprite_pixels)
 return {ROOT/'engine/src/platform/linux/generated_ui_sprites.h':header.encode(),
         ROOT/'docs/images/native-ui-font.png':png(width,height,pixels),
         ROOT/'docs/images/native-ui-sprites.png':png(sprite_width,16,rgba)}

def main():
 p=argparse.ArgumentParser();p.add_argument('--check',action='store_true');a=p.parse_args()
 for path,data in generate().items():
  if a.check:
   if not path.exists() or path.read_bytes()!=data:raise SystemExit('Generated output differs: '+str(path))
  else:path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
 print('PASS native UI atlas: 256 glyphs in 7px/9px/13px faces; pixel masks and five 16x16 colored sprites')
if __name__=='__main__':main()
