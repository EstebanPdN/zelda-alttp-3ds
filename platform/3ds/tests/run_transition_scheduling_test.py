#!/usr/bin/env python3
"""Exercise actual bottom invalidation/priority policy through door modules."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[3];s=(r/'app/jni/src/src/platform/linux/second_screen_sdl.c').read_text()
def fn(sig):
 a=s.index(sig);i=s.index('{',a)+1;n=1
 while n:n+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]+'\n'
a=s.index('typedef struct BottomCriticalState');b=s.index('static void request_bottom_redraw_on_state_change',a)
code=r'''
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
static void *ss_old_display_pixels;
static bool ss_is_new_3ds,ss_scene_redraw_pending,ss_worker_interactive,ss_touch_redraw_pending;
static uint64_t ss_worker_touch_request_ticks;
static bool ss_worker_door_transition;
static bool ss_worker_busy,ss_worker_sidebar_patch,ss_worker_map_patch;
static int ss_worker_idle_priority=0x31;
#define ss_redraw_requests requests
typedef int s32;
static int ss_worker_thread=1,ss_worker_interactive_priority=0x2f,ss_worker_scene_priority=0x30;
static int priority=0x31,tab,module=9,area;
static unsigned requests;
enum {TAB_MAP,MODE_GAME,kBottomRedrawFull=1,kBottomRedrawHud=2,kBottomRedrawMap=4};
static int threadGetHandle(int t){return t;}
static void svcSetThreadPriority(int t,int p){priority=p;}
static void SS_ReadSram(uint8_t *p,unsigned n){memset(p,0,n);}
static int SS_GetModule(void){return module;}
static int SS_GetArea(void){return area;}
static int SS_GetDungeon(void){return 0;}
static bool SS_IsIndoors(void){return module==7;}
static int SS_GetEquippedSlot(void){return 0;}
static bool SS_GetMirrorPortal(int *p){return false;}
static int SS_GetLinkX(void){return 0;}
static int SS_GetLinkY(void){return 0;}
static uint32_t SDL_GetTicks(void){return 1000;}
static int mode_for_module(int m){return MODE_GAME;}
static void request_bottom_redraw(unsigned r){requests|=r;}
'''+s[a:b]+fn('static void prioritize_bottom_scene(')+fn('static void prioritize_bottom_touch(')+fn('static s32 bottom_worker_priority(')+fn('static void prioritize_bottom_hud(')+fn('static void request_bottom_redraw_on_state_change(')+r'''
int main(void) {
 request_bottom_redraw_on_state_change();assert(!requests&&priority==0x31);
 module=15;request_bottom_redraw_on_state_change();assert(!requests&&priority==0x31);
 module=16;request_bottom_redraw_on_state_change();assert(!requests&&priority==0x31);
 module=7;request_bottom_redraw_on_state_change();assert(requests==1&&priority==0x30&&ss_scene_redraw_pending);
 requests=0;prioritize_bottom_touch();assert(priority==0x2f);
 ss_touch_redraw_pending=true;area++;request_bottom_redraw_on_state_change();assert(priority==0x2f);
 ss_is_new_3ds=true;requests=0;module=15;request_bottom_redraw_on_state_change();assert(requests==1);
 puts("PASS real scene invalidation: Old door modules deferred, destination redraw shares main priority, touch preemption retained, New unchanged");
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.c').write_text(code)
 subprocess.run(['cc','-O1','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
