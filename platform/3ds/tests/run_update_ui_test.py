from pathlib import Path
import subprocess,shlex,argparse
args=argparse.ArgumentParser();args.add_argument('--sdl-root',type=Path,required=True);args.add_argument('--out',type=Path,required=True);args=args.parse_args()
r=Path(__file__).resolve().parents[3];out=args.out.resolve();out.mkdir(parents=True,exist_ok=True);src=r/'app/jni/src/src/platform/linux/second_screen_sdl.c'
s=src.read_text()
# Enable only the platform-independent updater panel in this host harness.
a=s.index('static void draw_update_panel(');b=s.index('static void draw_settings(',a)
s=s[:a]+s[a:b].replace('#ifdef __3DS__','#if 1')+s[b:]
a=s.index('static void apply_tap(');b=s.index('static void draw_second_screen(int logic_frames) {',a)
s=s[:a]+s[a:b].replace('#ifdef __3DS__','#if 1')+s[b:]
s=s.replace('#include "../../', '#include "'+str(r/'app/jni/src/src')+'/')
code='''#include <assert.h>
#include <SDL.h>
#include "updater.h"
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
#define ZELDA3_TEST_3DS_UI 1
'''+s+'''
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
static void touch(RectFS r){
 SDL_Event e={0};e.type=SDL_FINGERDOWN;e.tfinger.windowID=0;
 e.tfinger.x=(r.x+r.w/2)/320;e.tfinger.y=(r.y+r.h/2)/240;
 int before=redraws;assert(SecondScreenSDL_HandleEvent(&e));assert(redraws==before+1);
 assert(redraw_tab==tab&&priority_tab==tab);
 e.type=SDL_FINGERUP;assert(SecondScreenSDL_HandleEvent(&e));assert(redraws==before+1);
 e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=ss_winid;e.button.which=SDL_TOUCH_MOUSEID;
 assert(SecondScreenSDL_HandleEvent(&e));assert(redraws==before+1);
}

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
int main(int argc,char**argv){SDL_Init(0);W=320;H=240;u=.5f;
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
 SDL_Quit();puts("PASS: actual settings/update drawing on Old RGB565 and New ARGB8888; five evenly spaced larger rows, no empty slot, both channels/all states, no control overlap. 200 tab switches/model, paused Update controls, channel/notes/pages/back/cancel/install/restart, NULL-window touch and no synthetic-mouse duplicate. Host font stand-in used for screenshots.");}
'''
(out/'ui-test.c').write_text(code)
sdk=args.sdl_root.resolve();flags=shlex.split(subprocess.check_output(['bash',str(sdk/'sdl2-config'),'--static-libs'],text=True));flags=[x for x in flags if x.startswith('-Wl,') or x in ['-lm','-liconv']]
cmd=['cc','-O1','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections','-Wl,-dead_strip','-I'+str(r/'app/jni/src'),'-I'+str(src.parent),'-I'+str(r/'platform/3ds/source'),'-I'+str(sdk/'include/SDL2'),'-I'+str(sdk/'include-config-release/SDL2'),str(out/'ui-test.c'),str(sdk/'libSDL2.a'),*flags,'-o',str(out/'ui-test')]
with (out/'ui-test.log').open('w') as log:
 subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True);subprocess.run([str(out/'ui-test'),str(out)],stdout=log,stderr=subprocess.STDOUT,check=True)
