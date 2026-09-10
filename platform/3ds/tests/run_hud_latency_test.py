#!/usr/bin/env python3
"""Actual damage invalidation, dispatch and worker priorities under simulated load."""
from pathlib import Path
import tempfile,subprocess,argparse
r=Path(__file__).resolve().parents[3]
a=argparse.ArgumentParser();a.add_argument('--source',type=Path,default=r/'app/jni/src/src/platform/linux/second_screen_sdl.c');args=a.parse_args();s=args.source.read_text()
def fn(sig):
 a=s.index(sig);i=s.index('{',a)+1;n=1
 while n:n+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]+'\n'
a=s.index('typedef struct BottomCriticalState');b=s.index('static void request_bottom_redraw_on_state_change',a)
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef int s32;
static bool ss_is_new_3ds,ss_scene_redraw_pending,ss_worker_interactive,ss_touch_redraw_pending;
static bool ss_worker_busy,ss_worker_sidebar_patch,ss_worker_map_patch,ss_worker_running;
static bool ss_enabled=true,ss_frame_ready,done_ready;
static int ss_worker_thread=1,ss_worker_interactive_priority=0x2f,ss_worker_scene_priority=0x30,ss_worker_idle_priority=0x31;
static int ss_front_buffer=0,ss_worker_buffer,ss_win=1,ss_worker_start,ss_worker_done,ss_worker_logic_frames;
static int priority=0x31,tab,module=9,area,drawn_health,signals,waits,load_result,load_flash_until;
static uint32_t ss_redraw_requests;
static uint64_t ss_touch_request_ticks,ss_worker_touch_request_ticks;
static uint64_t ss_patch_redraw_count,ss_patch_redraw_total_ticks,ss_patch_redraw_max_ticks;
static uint64_t ss_full_redraw_count,ss_full_redraw_total_ticks,ss_full_redraw_max_ticks;
static uint64_t ss_touch_redraw_count,ss_touch_redraw_total_ticks,ss_touch_redraw_max_ticks;
static uint8_t live[128];
enum {TAB_MAP,MODE_GAME,kBottomRedrawFull=1,kBottomRedrawHud=2,kBottomRedrawMap=4};
#define CUR_THREAD_HANDLE 0
static int threadGetHandle(int t){return t;}
static void svcSetThreadPriority(int t,int p){priority=p;}
static uint64_t svcGetSystemTick(void){return 100;}
static void SS_ReadSram(uint8_t *p,unsigned n){memcpy(p,live,n);}
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
static void request_bottom_redraw(unsigned r){ss_redraw_requests|=r;}
static bool Platform3DS_IsSystemClosing(void){return false;}
static int SS_TakeLoadDumpStateResult(void){return -1;}
static bool ensure_window(void){return true;}
static bool ensure_second_screen_worker(void){return true;}
static bool bottom_needs_periodic_redraw(void){return true;}
static bool can_patch_bottom_map(void){return !ss_is_new_3ds;}
static bool can_patch_bottom_sidebar(void){return !ss_is_new_3ds;}
static void LightEvent_Wait(int *e){if(waits++)ss_worker_running=false;}
static bool LightEvent_TryWait(int *e){bool d=done_ready;done_ready=false;return d;}
static void LightEvent_Signal(int *e){if(e==&ss_worker_start)signals++;else done_ready=true;}
static void draw_bottom_map_patch(void){assert(priority>=0x30);}
static void draw_bottom_sidebar_patch(void){assert(priority==0x2f);drawn_health=live[0x6d];}
static void draw_second_screen(int n){if(!ss_worker_touch_request_ticks)assert(priority>=0x30);drawn_health=live[0x6d];}
'''+s[a:b]+fn('static void prioritize_bottom_scene(')+fn('static void prioritize_bottom_touch(')
if 'static s32 bottom_worker_priority(' in s:code+=fn('static s32 bottom_worker_priority(')+fn('static void prioritize_bottom_hud(')
code+=fn('static void request_bottom_redraw_on_state_change(')+fn('void SecondScreenSDL_BeginFrame(')+fn('static void second_screen_worker_main(')
code+=r'''
static void RunWorker(void){waits=0;ss_worker_running=true;second_screen_worker_main(NULL);assert(done_ready);}
static void Present(void){SecondScreenSDL_BeginFrame(1);assert(ss_frame_ready);ss_frame_ready=false;}
int main(void) {
 live[0x6c]=24;live[0x6d]=24;request_bottom_redraw_on_state_change();
 // Damage while a full map redraw is busy: it must not remain starved at0x31.
 ss_worker_busy=true;live[0x6d]=16;SecondScreenSDL_BeginFrame(1);
 assert(ss_redraw_requests&kBottomRedrawHud);assert(priority==0x30);assert(!signals);
 // After map completion, present it, then dispatch the latest HUD alone.
 done_ready=true;Present();ss_redraw_requests|=kBottomRedrawMap|kBottomRedrawFull;
 live[0x6d]=8;SecondScreenSDL_BeginFrame(1);
 assert(ss_worker_sidebar_patch&&!ss_worker_map_patch&&signals==1);
 assert(ss_redraw_requests==(kBottomRedrawMap|kBottomRedrawFull));
 RunWorker();assert(drawn_health==8);assert(priority==0x31);Present();
 SecondScreenSDL_BeginFrame(1);assert(!ss_worker_sidebar_patch&&!ss_worker_map_patch);
 RunWorker();Present();
 // Healing plus a scene change must preserve both HUD and full-map requests.
 live[0x6d]=24;area++;SecondScreenSDL_BeginFrame(1);
 assert(ss_worker_sidebar_patch && (ss_redraw_requests&kBottomRedrawFull));
 RunWorker();assert(drawn_health==24);Present();
 SecondScreenSDL_BeginFrame(1);assert(!ss_worker_sidebar_patch);RunWorker();Present();
 // Damage may not demote active touch navigation.
 ss_worker_busy=true;ss_worker_touch_request_ticks=25;priority=0x2f;live[0x6d]=16;
 SecondScreenSDL_BeginFrame(1);assert(priority==0x2f);
 ss_worker_busy=false;ss_worker_touch_request_ticks=0;ss_touch_redraw_pending=true;ss_touch_request_ticks=25;
 ss_redraw_requests|=kBottomRedrawFull;SecondScreenSDL_BeginFrame(1);
 assert(!ss_worker_sidebar_patch && ss_worker_touch_request_ticks==25);RunWorker();Present();
 // New keeps its existing full redraw route and ignores Old priority changes.
 ss_is_new_3ds=true;ss_redraw_requests=0;live[0x6d]=8;priority=0x31;
 SecondScreenSDL_BeginFrame(1);assert(!ss_worker_sidebar_patch&&!ss_worker_map_patch&&priority==0x31);
 puts("PASS actual damage/healing invalidation, queued latest health, HUD-before-map dispatch, busy-map fairness, scene/touch priority, New unchanged and idle restoration");
}
'''
with tempfile.TemporaryDirectory(prefix='alttp-hud-latency-') as t:
 p=Path(t);(p/'test.c').write_text(code)
 subprocess.run(['cc','-O1','-fsanitize=address,undefined',p/'test.c','-o',p/'test'],check=True)
 subprocess.run([p/'test'],check=True)
