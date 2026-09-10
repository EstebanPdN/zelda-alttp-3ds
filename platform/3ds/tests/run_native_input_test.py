#!/usr/bin/env python3
"""Exercise actual native input after SDL consumes HID release edges."""
from pathlib import Path
import subprocess,tempfile,os
r=Path(__file__).resolve().parents[3]
s=Path(os.environ.get('INPUT_SOURCE',r/'platform/3ds/source/platform_3ds.c')).read_text()
a=s.index('uint16_t Platform3DS_ReadInput(');b=s.index('\nstatic char *Trim',a)
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t u32;
typedef struct {int dx,dy;} circlePosition;
enum {KEY_X=1,KEY_Y=2,KEY_A=4,KEY_B=8,KEY_L=16,KEY_R=32,
KEY_ZL=64,KEY_ZR=128,KEY_DUP=256,KEY_DDOWN=512,KEY_DLEFT=1024,
KEY_DRIGHT=2048,KEY_SELECT=4096,KEY_START=8192};
static u32 physical,scanned,up;
static uint64_t now;
static bool g_is_new_3ds,g_quick_dump_requested;
static int g_turbo_multiplier=5,overlays;
static void hidScanInput(void){up=scanned&~physical;scanned=physical;}
static u32 hidKeysHeld(void){return scanned;}
static u32 hidKeysUp(void){return up;}
static uint64_t osGetTime(void){return now;}
static void hidCircleRead(circlePosition *p){p->dx=p->dy=0;}
static bool CStickIsHeld(u32 keys){return false;}
static void SecondScreenSDL_OpenDeveloperOverlay(void){overlays++;}
FUNCTION
static void poll(u32 keys,uint64_t time,uint16_t expected,bool turbo) {
 physical=keys;now=time;hidScanInput(); // SDL N3DS_PumpEvents before ReadInput
 bool t;int m;uint16_t got=Platform3DS_ReadInput(&t,&m);
 assert(got==expected);assert(t==turbo);assert(m==(g_turbo_multiplier?g_turbo_multiplier:1));
}
int main(void) {
 // Tap on release, including time zero and repeated SDL pumps while waiting.
 poll(KEY_X,0,0,false);poll(KEY_X,33,0,false);
 physical=0;hidScanInput();hidScanInput();poll(0,66,512,false);poll(0,100,0,false);
 for(int i=0;i<100;i++) {uint64_t t=200+i*1100;
  poll(KEY_X,t,0,false);poll(0,t+999,512,false);poll(0,t+1000,0,false);
 }
 // Exact threshold; holding and release never leak X into game logic.
 poll(KEY_X,120000,0,false);poll(KEY_X,120999,0,false);
 poll(KEY_X,121000,0,true);poll(KEY_X,123000,0,true);poll(0,123033,0,false);
 // No intermediate poll at the threshold: still not a short tap.
 poll(KEY_X,124000,0,false);poll(0,125000,0,false);
 // Other buttons work during the hold; quick dump stays a single edge.
 poll(KEY_X|KEY_A,126000,256,false);poll(KEY_A,126050,768,false);poll(0,126100,0,false);
 poll(KEY_L|KEY_R|KEY_A,127000,3072,false);assert(g_quick_dump_requested);
 g_quick_dump_requested=false;poll(KEY_L|KEY_R|KEY_A,127033,3072,false);assert(!g_quick_dump_requested);poll(0,127100,0,false);
 // Disabling turbo gives normal immediate X, including long presses.
 g_turbo_multiplier=0;poll(KEY_X,128000,512,false);poll(KEY_X,130000,512,false);poll(0,130033,0,false);
 // New retains immediate X plus independent shoulder turbo.
 g_turbo_multiplier=5;g_is_new_3ds=true;
 poll(KEY_X,131000,512,false);poll(KEY_X|KEY_ZL,133000,512,true);poll(0,133033,0,false);
 puts("PASS actual input: SDL double scans, 101 taps, threshold/hold/release, other buttons, dump combo, disabled turbo and New controls");
}
'''.replace('FUNCTION',s[a:b])
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.c').write_text(code)
 subprocess.run(['cc','-O1','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
