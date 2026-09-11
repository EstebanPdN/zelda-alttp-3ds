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
static void *ss_old_display_pixels;
static bool ss_is_new_3ds,ss_scene_redraw_pending,ss_worker_interactive,ss_touch_redraw_pending;
static bool ss_worker_door_transition;
static bool ss_worker_busy,ss_worker_sidebar_patch,ss_worker_map_patch,ss_worker_running;
static bool ss_enabled=true,ss_frame_ready,ss_present_retry,done_ready,art_ready=true;
static bool update_mode;
typedef struct {unsigned revision;} UpdateStatus;
static void Updater_GetStatus(UpdateStatus *s){s->revision=0;}
static int ss_worker_thread=1,ss_worker_interactive_priority=0x2f,ss_worker_scene_priority=0x30,ss_worker_idle_priority=0x31;
static int ss_front_buffer=0,ss_worker_buffer,ss_win=1,ss_worker_start,ss_worker_done,ss_worker_logic_frames;
static int priority=0x31,tab,module=9,area,drawn_health,signals,waits,load_result,load_flash_until;
static uint32_t ss_redraw_requests;
static uint64_t ss_touch_request_ticks,ss_worker_touch_request_ticks;
static uint64_t ss_patch_redraw_count,ss_patch_redraw_total_ticks,ss_patch_redraw_max_ticks;
static uint64_t ss_full_redraw_count,ss_full_redraw_total_ticks,ss_full_redraw_max_ticks;
static uint64_t ss_touch_redraw_count,ss_touch_redraw_total_ticks,ss_touch_redraw_max_ticks;
static uint8_t live[128];
enum {TAB_MAP,TAB_ITEMS,TAB_GEAR,TAB_SETTINGS,MODE_GAME,kBottomRedrawFull=1,kBottomRedrawHud=2,kBottomRedrawMap=4};
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
static void draw_bottom_sidebar_patch(void){assert(priority==0x30);drawn_health=live[0x6d];}
static void draw_second_screen(int n){if(!ss_worker_touch_request_ticks)assert(priority>=0x30);drawn_health=live[0x6d];}
'''+s[a:b]+fn('static void prioritize_bottom_scene(')+fn('static void prioritize_bottom_touch(')
if 'static s32 bottom_worker_priority(' in s:code+=fn('static s32 bottom_worker_priority(')+fn('static void prioritize_bottom_hud(')
code+=fn('static void request_bottom_redraw_on_state_change(')+fn('void SecondScreenSDL_BeginFrame(')+fn('static void second_screen_worker_main(')
code+=r'''
typedef struct {int x,y,w,h;} SDL_Rect;
static int SDL_RenderReadPixels(void *r, const SDL_Rect *rect, int fmt, void *dst, int pitch) {assert(0);return -1;}
#define SDL_PIXELFORMAT_RGB565 0
typedef void SDL_Renderer;
'''
code+='#include "'+str(r/'app/jni/src/src/platform/linux/bottom_hearts.h')+'"\n'
code+=r'''
static BottomHearts ss_worker_hearts[2],ss_display_hearts;
static uint8_t display[512*256*2],worker0[512*256*2],worker1[512*256*2];
static uint8_t *ss_present_pixels[2]={worker0,worker1};
static bool ss_old_display_valid;
enum {k3DSBottomTextureWidth=512,k3DSBottomTextureHeight=256,W=320,H=240};
static int presents;
static int bottom_buffer_pitch(void){return ss_is_new_3ds?2048:1024;}
static bool gpu_active=true;
static bool Platform3DS_PresentBottomFrame(const uint8_t *p,int pitch,int w,int h){if(!gpu_active)return false;presents++;return true;}
'''+fn('void SecondScreenSDL_Update(')
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
 // A queued automatic map may not start during either door module. A job
 // already running is demoted, while real touch and completed frames survive.
 ss_redraw_requests=kBottomRedrawMap;ss_worker_busy=false;ss_frame_ready=false;
 ss_touch_redraw_pending=false;ss_worker_touch_request_ticks=0;int before=signals;
 module=15;SecondScreenSDL_BeginFrame(1);assert(signals==before&&ss_redraw_requests==kBottomRedrawMap);
 module=16;SecondScreenSDL_BeginFrame(1);assert(signals==before);
 module=9;ss_worker_busy=true;ss_worker_interactive=true;SecondScreenSDL_BeginFrame(1);assert(priority==0x30);
 module=15;SecondScreenSDL_BeginFrame(1);assert(priority==0x31);
 assert(bottom_worker_priority()==0x31); // Worker entry respects the same gate.
 ss_worker_touch_request_ticks=25;assert(bottom_worker_priority()==0x2f);
 ss_worker_touch_request_ticks=0;done_ready=true;SecondScreenSDL_BeginFrame(1);
 assert(ss_frame_ready&&!ss_worker_busy&&signals==before);ss_frame_ready=false;
 ss_touch_redraw_pending=true;ss_touch_request_ticks=25;ss_redraw_requests=kBottomRedrawFull;
 SecondScreenSDL_BeginFrame(1);assert(signals==before+1&&ss_worker_touch_request_ticks==25);RunWorker();Present();
 module=9;SecondScreenSDL_BeginFrame(1);if(ss_worker_busy){RunWorker();Present();}
 // New keeps its existing full redraw route and ignores Old priority changes.
 ss_is_new_3ds=true;ss_redraw_requests=0;live[0x6d]=8;priority=0x31;
 SecondScreenSDL_BeginFrame(1);assert(!ss_worker_sidebar_patch&&!ss_worker_map_patch&&priority==0x31);
 // Retained Old health must bypass even a blocked/busy map worker.
 ss_is_new_3ds=false;ss_old_display_pixels=display;ss_worker_busy=true;
 ss_worker_touch_request_ticks=0;ss_worker_interactive=false;ss_worker_sidebar_patch=false;
 request_bottom_redraw_on_state_change(); // settle the synthetic New-to-Old switch
 ss_redraw_requests=0;ss_frame_ready=false;done_ready=false;priority=0x31;
 ss_display_hearts=(BottomHearts){.valid=true,.count=3,.size=16,.capacity=24,.health=8};
 for(int i=0;i<3;i++)ss_display_hearts.rect[i]=(SDL_Rect){224+i*20,140,16,16};
 ss_old_display_valid=true;
 for(int i=0;i<64;i++){bottom_heart_glyphs[0][i]=0;bottom_heart_glyphs[1][i]=0xffff0000;bottom_heart_glyphs[2][i]=0xffff0000;}
 int old_signals=signals;
 for(int health=24;health>=0;health-=4){
   int old_presents=presents;live[0x6d]=health;SecondScreenSDL_BeginFrame(1);
   assert(!ss_redraw_requests && priority==0x31 && signals==old_signals);
   SecondScreenSDL_Update(1);assert(presents==old_presents+1);
   assert(ss_display_hearts.health==health);
 }
 // No changed glyph means no transfer. Busy worker storage must stay untouched.
 int old_presents=presents;SecondScreenSDL_Update(1);assert(presents==old_presents);
 for(unsigned i=0;i<sizeof(worker0);i++)assert(worker0[i]==0 && worker1[i]==0);
 // A completed old map must not make current hearts regress to captured health.
 ss_worker_hearts[0]=ss_display_hearts;ss_worker_hearts[0].health=24;
 ss_front_buffer=0;ss_frame_ready=true;SecondScreenSDL_Update(1);
 assert(ss_display_hearts.health==0 && presents==old_presents+1);
 // A capacity/half-magic mismatch and hidden tabs must not draw a stale layout.
 live[0x6d]=24;live[0x6c]=32;old_presents=presents;
 SecondScreenSDL_Update(1);assert(presents==old_presents);
 live[0x6c]=24;live[0x7b]=1;SecondScreenSDL_Update(1);assert(presents==old_presents);
 live[0x7b]=0;tab=TAB_SETTINGS;SecondScreenSDL_Update(1);assert(presents==old_presents);
 tab=TAB_MAP;SecondScreenSDL_Update(1);assert(presents==old_presents+1);
 // A dropped GPU frame retains the image even when no further UI changes.
 gpu_active=false;ss_frame_ready=true;old_presents=presents;
 SecondScreenSDL_Update(1);assert(ss_present_retry && presents==old_presents);
 gpu_active=true;SecondScreenSDL_Update(1);assert(!ss_present_retry && presents==old_presents+1);
 ss_is_new_3ds=true;gpu_active=false;ss_frame_ready=true;
 SecondScreenSDL_Update(1);assert(ss_frame_ready);
 gpu_active=true;SecondScreenSDL_Update(1);assert(!ss_frame_ready);
 // The first Old bottom frame is ready before a worker can starve at idle.
 ss_is_new_3ds=false;ss_worker_thread=0;ss_worker_busy=false;ss_front_buffer=-1;
 ss_frame_ready=false;art_ready=true;priority=0x30;SecondScreenSDL_BeginFrame(1);
 assert(ss_front_buffer==0 && ss_frame_ready && ss_worker_thread==0);
 // Completion during top rendering is consumed by Update in the SAME frame.
 for(int model=0;model<2;model++) {
  ss_is_new_3ds=model;ss_front_buffer=0;ss_worker_buffer=1;
  ss_worker_busy=true;ss_frame_ready=false;done_ready=true;
  int old_signals=signals,old_waits=waits;old_presents=presents;
  SecondScreenSDL_Update(1);
  assert(!ss_worker_busy&&!done_ready&&ss_front_buffer==1&&presents==old_presents+1);
  assert(signals==old_signals&&waits==old_waits); // No job or blocking wait added.
  ss_worker_busy=true;done_ready=false;old_presents=presents;
  SecondScreenSDL_Update(1);assert(ss_worker_busy&&presents==old_presents);
  assert(signals==old_signals&&waits==old_waits);
 }
 puts("PASS same-frame completion on both models with zero extra jobs/waits; startup and dropped-frame retry on both models; retained hearts while worker busy: no job/priority changes, immediate latest health, no unchanged-glyph upload, worker buffers untouched, completed-map reconciliation, capacity/half-magic/hidden-tab guards; fallback damage/healing invalidation, queued latest health, HUD-before-map dispatch, busy-map fairness, scene/touch priority, New unchanged and idle restoration");
}
'''
with tempfile.TemporaryDirectory(prefix='alttp-hud-latency-') as t:
 p=Path(t);(p/'test.c').write_text(code)
 subprocess.run(['cc','-O1','-fsanitize=address,undefined',p/'test.c','-o',p/'test'],check=True)
 subprocess.run([p/'test'],check=True)
