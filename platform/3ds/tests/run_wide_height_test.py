#!/usr/bin/env python3
"""Check actual config, boot sizing and live mode switches against PR #31."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[3]
s=(r/'platform/3ds/source/platform_3ds.c').read_text();m=(r/'app/jni/src/src/main.c').read_text()
def function(text,signature):
 a=text.index(signature);b=text.index('{',a);n=1;e=b+1
 while n:n+=(text[e]=='{')-(text[e]=='}');e+=1
 return text[a:e]+'\n'
code=r'''
#include <assert.h>
#include <stdio.h>
#include "src/config.h"
#include "src/features.h"
#include "snes/ppu.h"
#include "platform_3ds.h"
struct Config g_config;
static Ppu ppu;
static struct {Ppu *ppu;} g_zenv={&ppu};
static int g_display_mode,g_wide_edge_mode,g_turbo_multiplier=5;
static int g_snes_width,g_snes_height,g_ppu_render_flags;
uint32 g_wanted_zelda_features;
static unsigned resize_count;
#define Platform3DS_LogRuntime(...) ((void)0)
static void ZeldaApplyRendererSize(void){resize_count++;}
void Platform3DS_SetDisplayMode(enum Platform3DSDisplayMode mode){g_display_mode=mode;}
void PpuSetExtraSideSpace(Ppu *p,int l,int r,int b){p->extraLeftCur=l;p->extraRightCur=r;p->extraBottomCur=b;}
'''
code+=function(s,'void Platform3DS_ApplyConfig(')
code+=function(m,'void ZeldaSet3DSDisplayMode(')
start=m.index('  g_zenv.ppu->extraLeftRight = UintMin(g_config.extended_aspect_ratio')
end=m.index('#ifdef __3DS__',start)
code+='static void BootSize(void) {\n'+m[start:end]+'}\n'
code+=r'''
static void check(int mode,int flags) {
 bool wide=mode==kPlatform3DSDisplayUltraWideMod;
 assert(g_snes_width==(wide?400:256));assert(g_snes_height==(wide?240:224));
 assert(g_config.extend_y==wide);assert(!!(g_ppu_render_flags&kPpuRenderFlags_Height240)==wide);
 assert((g_ppu_render_flags&~kPpuRenderFlags_Height240)==flags);
 assert(ppu.extraLeftRight==(wide?72:0));
 // Existing presenter's native geometry fills 400x240 only for WIDE.
 int draw_height=g_snes_height<240?g_snes_height:240;
 assert((240-draw_height)/2==(wide?0:8));
 assert(g_config.ignore_aspect_ratio==(mode==kPlatform3DSDisplayStretch));
 assert(g_config.features0&kFeatures0_MiscBugFixes);
}
int main(void) {
 for(int old=0;old<2;old++)for(int boot=0;boot<3;boot++) {
  g_display_mode=boot;g_config.features0=kFeatures0_MiscBugFixes;
  Platform3DS_ApplyConfig(&g_config);BootSize();
  if(old)g_ppu_render_flags|=kPpuRenderFlags_Old3DS;
  int flags=kPpuRenderFlags_NewRenderer|(old?kPpuRenderFlags_Old3DS:0);
  check(boot,flags);
  for(int n=0;n<100;n++)for(int mode=0;mode<3;mode++) {
   ZeldaSet3DSDisplayMode(mode);check(mode,flags);
   assert(g_wanted_zelda_features==g_config.features0);
   if(mode!=1)assert(!ppu.extraLeftCur&&!ppu.extraRightCur&&!ppu.extraBottomCur);
  }
 }
 assert(resize_count==1800);
 puts("PASS actual config/boot and 1800 live mode switches: Old/New 400x240 WIDE, 256x224 Original/Stretch, flags and unrelated features retained");
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.c').write_text(code)
 subprocess.run(['cc','-O1','-fsanitize=address,undefined','-I'+str(r/'app/jni/src'),'-I'+str(r/'app/jni/SDL2/include'),'-I'+str(r/'platform/3ds/source'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
