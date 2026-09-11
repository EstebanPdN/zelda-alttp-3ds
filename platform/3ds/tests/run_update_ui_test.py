from pathlib import Path
import subprocess,shlex,argparse
args=argparse.ArgumentParser();args.add_argument('--sdl-root',type=Path,required=True);args.add_argument('--out',type=Path,required=True);args.add_argument('--touch-source',type=Path);args=args.parse_args()
r=Path(__file__).resolve().parents[3];out=args.out.resolve();out.mkdir(parents=True,exist_ok=True);src=r/'app/jni/src/src/platform/linux/second_screen_sdl.c'
s=src.read_text()
touch_source = args.touch_source or r/'app/jni/SDL2/src/video/n3ds/SDL_n3dstouch.c'
touch_source = touch_source.read_text()
native_poll = touch_source[touch_source.index('void N3DS_PollTouch(void)'):touch_source.index('#endif /* SDL_VIDEO_DRIVER_N3DS */')]

# Enable only the platform-independent updater panel in this host harness.
a=s.index('static void draw_update_panel(');b=s.index('static void draw_settings(',a)
s=s[:a]+s[a:b].replace('#ifdef __3DS__','#if 1')+s[b:]
a=s.index('static void apply_tap(');b=s.index('static void draw_second_screen(int logic_frames) {',a)
s=s[:a]+s[a:b].replace('#ifdef __3DS__','#if 1')+s[b:]
s=s.replace('#include "../../', '#include "'+str(r/'app/jni/src/src')+'/')
code='''#include <assert.h>
#include <SDL.h>
#include "updater.h"
#include "native_touch.h"
#include "hardware_profile.h"
static bool model;
static const Platform3DSHardwareProfile*Platform3DS_GetHardwareProfile(void){return Platform3DS_ProfileForModel(model);}
static uint64_t ss_touch_request_ticks;
static bool ss_touch_redraw_pending,ss_worker_interactive;
static int redraw_tab,priority_tab,redraws,restarts,checks,cancels,downloads;
static unsigned kBottomRedrawFull=2;
static uint64_t svcGetSystemTick(void){return 42;}
static void prioritize_bottom_touch(void);
static void request_bottom_redraw(unsigned bits);
static int Platform3DS_GetDisplayMode(void){return 0;}
static int Platform3DS_GetWideEdgeMode(void){return 0;}
static int Platform3DS_GetWideZoomIndex(void){return 0;}
static int Platform3DS_GetTurboMultiplier(void){return 5;}
static void Platform3DS_SetDisplayMode(int v){}
static void Platform3DS_SetWideEdgeMode(int v){}
static void Platform3DS_SetWideZoomIndex(int v){}
static void Platform3DS_SetTurboMultiplier(int v){}
static bool Platform3DS_GetShowFps(void){return false;}
static void Platform3DS_SetShowFps(bool v){}
static void Platform3DS_RequestRomSelection(void){restarts++;}
static unsigned Platform3DS_UpdateNotesPages(void){return 3;}
#define KEY_TOUCH (1u<<20)
typedef struct {unsigned short px,py;} touchPosition;
static touchPosition raw_touch;
static unsigned raw_held;
static void hidTouchRead(touchPosition*p){*p=raw_touch;}
static unsigned hidKeysHeld(void){return raw_held;}
extern int SDL_AddTouch(SDL_TouchID,SDL_TouchDeviceType,const char*);
extern int SDL_SendTouch(SDL_TouchID,SDL_FingerID,SDL_Window*,SDL_bool,float,float,float);
extern int SDL_SendTouchMotion(SDL_TouchID,SDL_FingerID,SDL_Window*,float,float,float);
#define N3DS_TOUCH_ID 0
#define TOUCHSCREEN_SCALE_X (1.0f/320)
#define TOUCHSCREEN_SCALE_Y (1.0f/240)
#define ZELDA3_TEST_3DS_UI 1
'''+native_poll+s+'''
static UpdateStatus fixture;
static void request_bottom_redraw(unsigned bits){redraw_tab=tab;redraws++;}
static void prioritize_bottom_touch(void){priority_tab=tab;}
bool Updater_Busy(void){return fixture.state==UPDATE_CHECKING||fixture.state==UPDATE_DOWNLOADING||fixture.state==UPDATE_INSTALLING;}
void Updater_Check(void){checks++;}
void Updater_Cancel(void){cancels++;}
void Updater_Download(void){downloads++;}
void Updater_SetChannel(bool pre){fixture.prerelease=pre;}
void SS_Set3DSDisplayMode(int v){}void SS_Set3DSWideEdgeMode(int v){}
void SS_SetHudHidden(bool b){}void SS_RequestMemoryDump(const char*d){}
void SS_RequestLoadLatestDumpState(void){}int SS_GetEquippedSlot(void){return 0;}
void SS_EquipSlot(int v){} void SS_SetWidescreen(bool b){}
static void touch_expect(RectFS r,bool changed){
 SDL_Event e={0};e.type=SDL_FINGERDOWN;e.tfinger.windowID=0;
 e.tfinger.x=(r.x+r.w/2)/320;e.tfinger.y=(r.y+r.h/2)/240;
 int before=redraws;
 raw_touch=(touchPosition){(unsigned short)lroundf(e.tfinger.x*320),(unsigned short)lroundf(e.tfinger.y*240)};
 raw_held=KEY_TOUCH;N3DS_PollTouch();
 assert(SDL_PeepEvents(&e,1,SDL_GETEVENT,native_touch_event,native_touch_event)==1);
 assert(SecondScreenSDL_HandleEvent(&e));assert(redraws==before+(changed?1:0));
 // A held contact/movement must not activate the menu twice.
 N3DS_PollTouch();SDL_Event duplicate;
 assert(SDL_PeepEvents(&duplicate,1,SDL_GETEVENT,native_touch_event,native_touch_event)==0);
 // Release with unchanged coordinates must re-arm the next contact.
 raw_held=0;N3DS_PollTouch();
 assert(SDL_PeepEvents(&e,1,SDL_GETEVENT,SDL_FINGERUP,SDL_FINGERUP)==1);
 if(changed)assert(redraw_tab==tab&&priority_tab==tab);
 e.type=SDL_FINGERUP;assert(SecondScreenSDL_HandleEvent(&e));assert(redraws==before+(changed?1:0));
 e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=ss_winid;e.button.which=SDL_TOUCH_MOUSEID;
 assert(SecondScreenSDL_HandleEvent(&e));assert(redraws==before+(changed?1:0));
}

static void touch(RectFS r){touch_expect(r,true);}
static int prior_calls;
static int prior_filter(void*data,SDL_Event*e){assert(data==&prior_calls);prior_calls++;return e->type!=SDL_KEYUP;}
void Updater_GetStatus(UpdateStatus*out){*out=fixture;}
void SS_ArmButtonCapture(bool b){} int SS_GetCapturedButton(void){return -1;}
void SS_SetGamepadControls(const int*p){} bool SS_IsWidescreen(void){return true;}
bool SS_IsHudHidden(void){return false;}int SS_GetModule(void){return 9;}
bool SS_IsIndoors(void){return false;} int SS_GetDungeon(void){return 0;}int SS_GetArea(void){return 0;}
const char*DumpState_ResultLabel(ZeldaDumpStateResult r){return "OK";}
static void font(void){
 unsigned pixels[256*256]={0};
 for(int c=0;c<26;c++){int cell=kSS_LetterCell[c];int x=(cell%SS_LETTER_COLS)*8,y=(cell/SS_LETTER_COLS)*8;const unsigned char*rows=tiny_letter('A'+c);for(int j=0;j<7;j++)for(int i=0;i<5;i++)if(rows[j]&(1<<(4-i)))pixels[(y+j)*256+x+i+1]=0xffffffff;}
 SDL_Surface*s=SDL_CreateRGBSurfaceWithFormatFrom(pixels,256,256,32,256*4,SDL_PIXELFORMAT_ARGB8888);tex_letters=SDL_CreateTextureFromSurface(ss_r,s);SDL_SetTextureBlendMode(tex_letters,SDL_BLENDMODE_BLEND);SDL_FreeSurface(s);
}
static void bounds(RectFS a){assert(a.x>=0&&a.y>=0&&a.x+a.w<=W&&a.y+a.h<=H);}
int main(int argc,char**argv){
 SDL_setenv("SDL_VIDEODRIVER","dummy",1);assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)==0);
 SDL_Window*projection_window=SDL_CreateWindow("top viewport",0,0,400,240,SDL_WINDOW_HIDDEN);
 assert(projection_window);SDL_Renderer*projection=SDL_CreateRenderer(projection_window,-1,SDL_RENDERER_SOFTWARE);assert(projection);
 SDL_Rect shifted={-80,0,400,240};assert(SDL_RenderSetViewport(projection,&shifted)==0);
 // Real SDL renderer watch changes an unprotected native event before delivery.
 SDL_Event control={0};control.type=SDL_FINGERDOWN;control.tfinger.x=48.0f/320;control.tfinger.y=220.0f/240;
 assert(SDL_PushEvent(&control)==1);assert(SDL_PeepEvents(&control,1,SDL_GETEVENT,SDL_FINGERDOWN,SDL_FINGERDOWN)==1);
 assert(fabsf(control.tfinger.x*320-48)>50); // E3 interpreted this as a different tab.
 SDL_SetEventFilter(prior_filter,&prior_calls);
 assert(NativeTouch_Init());
 assert(SDL_AddTouch(0,SDL_TOUCH_DEVICE_DIRECT,"Native test")>=0);
 // Boot/resume can expose a previous position with no active contact.
 // The production backend must not manufacture a press or latch its state.
 raw_touch=(touchPosition){140,217};raw_held=0;N3DS_PollTouch();
 SDL_Event phantom;
 assert(SDL_PeepEvents(&phantom,1,SDL_GETEVENT,native_touch_event,native_touch_event)==0);
 SDL_Event key={0};key.type=SDL_KEYDOWN;assert(SDL_PushEvent(&key)==1);
 assert(SDL_PeepEvents(&key,1,SDL_GETEVENT,SDL_KEYDOWN,SDL_KEYDOWN)==1);
 key.type=SDL_KEYUP;assert(SDL_PushEvent(&key)==0);
 SDL_Event invalid={0};invalid.type=SDL_FINGERDOWN;invalid.tfinger.x=NAN;assert(SDL_PushEvent(&invalid)==0);
 invalid.tfinger.x=-1;assert(SDL_PushEvent(&invalid)==0);
 // Every physical pixel survives the event queue/renderer without a shift.
 for(int y=0;y<240;y++)for(int x=0;x<320;x++){
  SDL_Event e={0};e.type=SDL_FINGERDOWN;e.tfinger.x=x/320.0f;e.tfinger.y=y/240.0f;
  raw_touch=(touchPosition){x,y};raw_held=KEY_TOUCH;N3DS_PollTouch();
  assert(SDL_PeepEvents(&e,1,SDL_GETEVENT,native_touch_event,native_touch_event)==1);
  float px,py;assert(NativeTouch_Decode(&e,&px,&py));assert(px==x&&py==y);
  raw_held=0;N3DS_PollTouch();
  assert(SDL_PeepEvents(&e,1,SDL_GETEVENT,SDL_FINGERUP,SDL_FINGERUP)==1);
 }
 W=320;H=240;u=.5f;
 for(int m=0;m<2;m++){model=m;SDL_Surface*screen=SDL_CreateRGBSurfaceWithFormat(0,320,240,m?32:16,m?SDL_PIXELFORMAT_ARGB8888:SDL_PIXELFORMAT_RGB565);ss_r=SDL_CreateSoftwareRenderer(screen);font();
 RectFS panel={5,5,310,190};update_mode=false;draw_settings(panel);
 for(int i=0;i<5;i++){bounds(settings_row_r[i]);if(i)assert(settings_row_r[i].y>=settings_row_r[i-1].y+settings_row_r[i-1].h);}
 assert(settings_row_r[0].y==32.5f);
 assert(settings_row_r[4].y+settings_row_r[4].h==panel.y+panel.h-10);
 for(int i=1;i<5;i++)assert(fabsf(settings_row_r[i].y-settings_row_r[i-1].y-settings_row_r[i-1].h-4)<.001f);
 char name[256];snprintf(name,sizeof(name),"%s/settings-%s.bmp",argv[1],m?"new":"old");SDL_SaveBMP(screen,name);
 update_mode=true;
 for(int state=UPDATE_IDLE;state<=UPDATE_ERROR;state++)for(int pre=0;pre<2;pre++){
 fixture=(UpdateStatus){.state=state,.prerelease=pre,.progress=45};strcpy(fixture.version,"v3.2-E1");strcpy(fixture.message,"YOU ARE UP TO DATE");
 update_show_notes=true;draw_settings(panel);bounds(update_channel_r);bounds(update_release_r);bounds(update_prev_r);bounds(update_next_r);bounds(update_action_r);
 assert(update_release_r.y>=update_channel_r.y+update_channel_r.h);assert(update_next_r.y+update_next_r.h<update_action_r.y);
 }
 snprintf(name,sizeof(name),"%s/update-%s.bmp",argv[1],m?"new":"old");SDL_SaveBMP(screen,name);
 // Actual SDL event -> production navigation -> redraw ordering, on both models.
 ss_win=(SDL_Window*)1;ss_winid=5;ss_is_new_3ds=m;art_ready=true;
 update_mode=false;tab=TAB_MAP;draw_tab_bar(42);draw_settings(panel);
 for(int i=0;i<100;i++) {touch(tab_gear_r);assert(tab==TAB_GEAR);touch(tab_map_r);assert(tab==TAB_MAP);}
 touch(tab_items_r);assert(tab==TAB_ITEMS);touch_expect(tab_items_r,false);assert(tab==TAB_ITEMS);
 touch(tab_gear_r);assert(tab==TAB_GEAR);touch_expect(tab_gear_r,false);assert(tab==TAB_GEAR);
 touch(tab_map_r);touch_expect(tab_map_r,false);assert(tab==TAB_MAP);
 // Visible borders/gaps and strip edges accept one touch, with no content-area leakage.
 const float samples[]={0,93,94,95,186,187,188,279,280,319};
 const int targets[]={TAB_GEAR,TAB_GEAR,TAB_GEAR,TAB_MAP,TAB_MAP,TAB_MAP,TAB_ITEMS,TAB_ITEMS,TAB_SETTINGS,TAB_SETTINGS};
 for(int i=0;i<10;i++)for(int y=198;y<=239;y+=41){
   tab=targets[i]==TAB_MAP?TAB_ITEMS:TAB_MAP;
   touch((RectFS){samples[i],y,0,0});assert(tab==targets[i]);
 }
 assert(tab_at_position(50,197)==-1&&tab_at_position(-1,220)==-1&&tab_at_position(320,220)==-1);
 tab=TAB_MAP;
 touch(tab_settings_r);assert(tab==TAB_SETTINGS);
 fixture.state=UPDATE_CURRENT;touch(settings_row_r[3]);assert(update_mode);
 draw_settings(panel);touch(update_release_r);assert(update_show_notes);
 touch(update_next_r);assert(update_page==1);touch(update_prev_r);assert(update_page==0);
 bool pre=fixture.prerelease;touch(update_channel_r);assert(fixture.prerelease!=pre);
 touch(update_back_r);assert(!update_mode);
 touch(settings_row_r[3]);assert(update_mode);fixture.state=UPDATE_CHECKING;
 int old_cancels=cancels;touch(update_back_r);assert(!update_mode&&cancels==old_cancels+1);
 fixture.state=UPDATE_AVAILABLE;touch(settings_row_r[3]);draw_settings(panel);
 touch(update_action_r);assert(update_confirm);touch(update_action_r);assert(downloads>0);
 touch(update_back_r);assert(!update_mode);int old_restarts=restarts;touch(settings_row_r[4]);assert(restarts==old_restarts+1);
 ss_win=NULL;
 SDL_DestroyTexture(tex_letters);tex_letters=NULL;SDL_DestroyRenderer(ss_r);SDL_FreeSurface(screen);
 }
 NativeTouch_Shutdown();assert(!native_touch_event);
 SDL_EventFilter restored;void*restored_data;SDL_GetEventFilter(&restored,&restored_data);
 assert(restored==prior_filter&&restored_data==&prior_calls&&prior_calls>76800);
 assert(NativeTouch_Init());NativeTouch_Shutdown(); // ROM restart can reinstall the filter.
 SDL_DestroyRenderer(projection);SDL_DestroyWindow(projection_window);
 SDL_Quit();puts("PASS: 76800 physical pixels through real SDL queue and renderer watch; E3 viewport control shifts; native KEY_TOUCH edges reject residual startup/release coordinates; actual settings/update drawing on Old RGB565 and New ARGB8888; five evenly spaced larger rows, no empty slot, both channels/all states, no control overlap. 200 tab switches/model, border/gap/edge targets, idempotent tabs without extra redraws, paused Update controls, channel/notes/pages/back/cancel/install/restart, NULL-window touch and no synthetic-mouse duplicate. Host font stand-in used for screenshots.");}
'''
(out/'ui-test.c').write_text(code)
sdk=args.sdl_root.resolve();flags=shlex.split(subprocess.check_output(['bash',str(sdk/'sdl2-config'),'--static-libs'],text=True));flags=[x for x in flags if x.startswith('-Wl,') or x in ['-lm','-liconv']]
cmd=['cc','-O1','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-Wl,-dead_strip','-I'+str(r/'app/jni/src'),'-I'+str(src.parent),'-I'+str(r/'platform/3ds/source'),'-I'+str(sdk/'include/SDL2'),'-I'+str(sdk/'include-config-release/SDL2'),str(out/'ui-test.c'),str(sdk/'libSDL2.a'),*flags,'-o',str(out/'ui-test')]
with (out/'ui-test.log').open('w') as log:
 subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True);subprocess.run([str(out/'ui-test'),str(out)],stdout=log,stderr=subprocess.STDOUT,check=True)
