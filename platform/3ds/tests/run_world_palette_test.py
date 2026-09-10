#!/usr/bin/env python3
"""Exercise the production NMI upload with both profiles and unchanged brightness."""
from pathlib import Path
import subprocess,tempfile,argparse
r=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--profile',type=Path);p.add_argument('--dump',type=Path,required=True);args=p.parse_args()
s=(r/'app/jni/src/src/nmi.c').read_text();a=s.index('  if (flag_update_cgram_in_nmi)');b=s.index('  flag_update_hud_in_nmi = 0;',a)
code='''#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes/ppu.h"
#include "hardware_profile.h"
uint64_t svcGetSystemTick(void){return 0;}
static bool model;
static const Platform3DSHardwareProfile *Platform3DS_GetHardwareProfile(void){return Platform3DS_ProfileForModel(model);}
static struct { Ppu *ppu; } g_zenv;
static unsigned flag_update_cgram_in_nmi=1;
static uint16_t main_palette_buffer[256];
static void upload(void){\n'''+s[a:b]+'''\n}
int main(int argc,char**argv){
 FILE*f=fopen(argv[1],"rb");assert(f);uint16_t captured[256];assert(fread(captured,2,256,f)==256);fclose(f);
 unsigned *pixels=calloc(512*240,4);assert(pixels);
 for(model=false;;model=true){
  Ppu*p=g_zenv.ppu=ppu_init();ppu_reset(p);p->mode=1;p->forcedBlank=false;p->brightness=15;
  unsigned flags=1|(model?0:kPpuRenderFlags_Old3DS);
  for(int direction=0;direction<2;direction++){
   for(int i=0;i<256;i++)main_palette_buffer[i]=captured[i]^(direction?0:0x7fff);
   upload();PpuBeginDrawing(p,(void*)pixels,2048,flags);
   for(int i=0;i<256;i++){
    unsigned c=main_palette_buffer[i];unsigned rr=c&31,gg=(c>>5)&31,bb=(c>>10)&31;
    unsigned expected=((rr<<3|rr>>2)<<16)|((gg<<3|gg>>2)<<8)|(bb<<3|bb>>2);
    if(p->colorMapRgb[i]!=expected){fprintf(stderr,"FAIL %s direction=%d palette=%d cached=%06x expected=%06x\\n",model?"New":"Old",direction,i,p->colorMapRgb[i],expected);return 1;}
   }
   assert(!p->colorMapDirty);upload();assert(!p->colorMapDirty);
  }
  ppu_free(p);if(model)break;
 }
 free(pixels);puts("PASS: 1024 palette colors across both directions, Old/New, unchanged brightness; unchanged palette retains cache");
}
'''
with tempfile.TemporaryDirectory() as td:
 t=Path(td);(t/'test.c').write_text(code)
 profile=args.profile or r/'platform/3ds/source/hardware_profile.h';(t/'hardware_profile.h').write_bytes(profile.read_bytes())
 subprocess.run(['cc','-O1','-D__3DS__','-fsanitize=address,undefined','-fno-sanitize=shift-base','-I'+str(t),'-I'+str(r/'app/jni/src'),str(t/'test.c'),str(r/'app/jni/src/snes/ppu.c'),'-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test'),str(args.dump/'cgram.bin')],check=True)
