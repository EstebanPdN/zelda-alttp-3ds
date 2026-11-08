from pathlib import Path
import subprocess,shlex,argparse,os,sys
args=argparse.ArgumentParser();args.add_argument('--sdl-root',type=Path);args.add_argument('--sdl-config',default=os.environ.get('SDL2_CONFIG','sdl2-config'));args.add_argument('--out',type=Path,required=True);args.add_argument('--touch-source',type=Path);args=args.parse_args()
r=Path(__file__).resolve().parents[3];out=args.out.resolve();out.mkdir(parents=True,exist_ok=True);src=r/'engine/src/platform/linux/second_screen_sdl.c'
s=src.read_text()
touch_source = args.touch_source or r/'third_party/SDL2/src/video/n3ds/SDL_n3dstouch.c'
touch_source = touch_source.read_text()
native_poll = touch_source[touch_source.index('void N3DS_PollTouch(void)'):touch_source.index('#endif /* SDL_VIDEO_DRIVER_N3DS */')]

# Enable only the platform-independent updater panel in this host harness.
a=s.index('static void draw_update_panel(');b=s.index('static void draw_settings(',a)
s=s[:a]+s[a:b].replace('#ifdef __3DS__','#if 1')+s[b:]
a=s.index('static void apply_tap(');b=s.index('static void draw_second_screen(int logic_frames) {',a)
s=s[:a]+s[a:b].replace('#ifdef __3DS__','#if 1')+s[b:]
s=s.replace('#include "../../', '#include "'+str(r/'engine/src')+'/')
code='''#include <assert.h>
#include <SDL.h>
#include "updater.h"
#include "native_touch.h"
#include "hardware_profile.h"
static bool model;
static const Platform3DSHardwareProfile*Platform3DS_GetHardwareProfile(void){return Platform3DS_ProfileForModel(model);}
static uint64_t ss_touch_request_ticks;
static bool ss_touch_redraw_pending,ss_touch_sync_redraw,ss_worker_interactive;
static int redraw_tab,priority_tab,redraws,restarts,checks,cancels,downloads;
static unsigned kBottomRedrawFull=2;
static uint64_t svcGetSystemTick(void){return 42;}
static void prioritize_bottom_touch(void);
static void request_bottom_redraw(unsigned bits);
static int Platform3DS_GetDisplayMode(void){return 0;}
static int Platform3DS_GetWideEdgeMode(void){return 0;}
static int Platform3DS_GetWideZoomIndex(void){return 0;}
static int Platform3DS_GetTurboMultiplier(void){return 5;}
enum {kTurboControlAuto,kTurboControlX,kTurboControlZL,kTurboControlCStick};
static int Platform3DS_GetTurboControl(void){return kTurboControlX;}
static void Platform3DS_SetTurboControl(int c){}
static bool Platform3DS_GetAutosave(void){return true;}
static void Platform3DS_SetAutosave(bool a){}
static void Platform3DS_RequestReset(void){restarts++;}
static void Platform3DS_SetDisplayMode(int v){}
static void Platform3DS_SetWideEdgeMode(int v){}
static void Platform3DS_SetWideZoomIndex(int v){}
static void Platform3DS_SetTurboMultiplier(int v){}
static bool Platform3DS_GetShowFps(void){return false;}
static bool Platform3DS_IsNew3DS(void){return model;}
static bool Platform3DS_CanUseCore1PpuWorker(void){return !model;}
static void Platform3DS_SetShowFps(bool v){}
static void Platform3DS_RequestRomSelection(void){restarts++;}
static void Platform3DS_RequestStartupMenu(void){restarts++;}
static void Platform3DS_PlayMenuMove(void){}
#include "src/rando/seed_summary.h"
static int randomizer_requests,randomizer_generations,randomizer_resumes,ordinary_requests,import_requests;
static bool import_ok=true;
static uint64_t requested_seed;
static void Platform3DS_RequestRandomizerMenu(void){randomizer_requests++;}
static const char *randomizer_error_text="";
static const char *Platform3DS_RandomizerError(void){return randomizer_error_text;}
static RandoErrorCode error_code=kRandoError_MissingPlacements;
static RandoErrorCode Platform3DS_RandomizerErrorCode(void){return error_code;}
static bool Platform3DS_RequestGeneratedSeed(int preset,uint64_t seed){randomizer_generations++;requested_seed=seed;return true;}
static bool Platform3DS_RequestSavedSeed(const char *key){randomizer_resumes++;return true;}
static const char *seed_key="";
static const char *Platform3DS_RandomizerSeedKey(void){return seed_key;}
static int state_saves,state_loads,scenario_requested=-1;static bool state_exists=true,scenario_exists=true;
void SS_SaveLoadState(bool save){if(save)state_saves++;else state_loads++;}
bool SS_HasSaveState(void){return state_exists;}
void SS_RequestTestScenario(int i){scenario_requested=i;}
bool SS_TestScenarioAvailable(int i){return scenario_exists;}
static void Platform3DS_RequestOrdinaryGame(void){ordinary_requests++;}
static int Platform3DS_ListRandomizerSeeds(RandoSeedSummary*out,int cap){
 int count=cap<6?cap:6;
 for(int i=0;i<count;i++){memset(&out[i],0,sizeof(out[i]));memset(out[i].key,'a'+i,64);out[i].key[64]=0;out[i].seed=i==4?UINT64_MAX:42+i;out[i].preset=i%5;}
 if(count>5){out[5].origin=2;strcpy(out[5].label,"mykaDWN6MQ");}
 return count;
}
static int Platform3DS_ListRandomizerImports(RandoImportFile *out,int cap){
 if(cap<2)return 0;
 strcpy(out[0].name,"mykaDWN6MQ.json");
 memset(out[1].name,'A',80);strcpy(out[1].name+80,".json");return 2;
}
static bool Platform3DS_RequestImportedSeed(const char *name){import_requests++;return import_ok;}

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
#define ZELDA3_TEST_NATIVE_MENUS 1
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
void SS_RequestLoadLatestDumpState(void){}int SS_GetInventorySize(void){return (Qol_Features1(0)&(1u<<10))?24:20;}
int SS_GetInventorySlot(int slot){
 if(slot<1||slot>SS_GetInventorySize())return -1;
 int i=slot-1,kind=i,level;
 if(i>=20){kind=15;level=sram8(0x5c+i-20);}
 else if(i==15&&SS_GetInventorySize()==24){kind=12;level=sram8(0x4c)>0?1:0;}
 else level=i==15?bottle_level():sram8(0x40+i);
 return level>0?(kind<<8)|(level>7?7:level):-1;
}
int SS_GetEquippedSlot(void){return 0;}
void SS_RequestRestart(void){restarts++;}
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
 for(int m=0;m<2;m++){model=m;SDL_Surface*screen=SDL_CreateRGBSurfaceWithFormat(0,320,240,m?32:16,m?SDL_PIXELFORMAT_ARGB8888:SDL_PIXELFORMAT_RGB565);ss_r=SDL_CreateSoftwareRenderer(screen);
 RectFS panel={5,5,310,191};update_mode=false;draw_settings(panel);
 for(int i=0;i<5;i++){bounds(settings_row_r[i]);if(i)assert(settings_row_r[i].y>=settings_row_r[i-1].y+settings_row_r[i-1].h);}
 assert(settings_row_r[0].y>=panel.y+38);
 assert(settings_row_r[4].y+settings_row_r[4].h<=panel.y+panel.h-6);
 for(int i=0;i<5;i++){assert(settings_row_r[i].h>=20);if(i)assert(settings_row_r[i].y-settings_row_r[i-1].y-settings_row_r[i-1].h>=2);}
 char name[256];snprintf(name,sizeof(name),"%s/settings-%s.bmp",argv[1],m?"new":"old");SDL_SaveBMP(screen,name);
 for(int language=0;language<kUiLanguageCount;language++){
  UI_SetLanguage(language);gameplay_mode=true;draw_settings(panel);
  for(int i=0;i<5;i++){bounds(gameplay_row_r[i]);assert(gameplay_row_r[i].h>=20);if(i)assert(gameplay_row_r[i].y>=gameplay_row_r[i-1].y+gameplay_row_r[i-1].h+2);}
  snprintf(name,sizeof(name),"%s/gameplay-%d-%s.bmp",argv[1],language,m?"new":"old");SDL_SaveBMP(screen,name);
  gameplay_mode=false;randomizer_confirm_mode=true;draw_settings(panel);
  snprintf(name,sizeof(name),"%s/randomizer-confirm-%d-%s.bmp",argv[1],language,m?"new":"old");SDL_SaveBMP(screen,name);
  randomizer_confirm_mode=false;randomizer_mode=true;randomizer_page=0;randomizer_seed=UINT64_MAX;randomizer_preset=4;
  draw_settings(panel);for(int i=0;i<8;i++)bounds(randomizer_rows[i]);
  assert(draw_wrapped_center(UI_Text(kUi_SkinsOnly),randomizer_rows[4],1,COL_GOLD)<=2);
  snprintf(name,sizeof(name),"%s/randomizer-%d-%s.bmp",argv[1],language,m?"new":"old");SDL_SaveBMP(screen,name);
  randomizer_page=2;strcpy(randomizer_seed_text,"18446744073709551615");draw_settings(panel);
  for(int i=0;i<12;i++)bounds(randomizer_keys[i]);
  snprintf(name,sizeof(name),"%s/randomizer-keypad-%d-%s.bmp",argv[1],language,m?"new":"old");SDL_SaveBMP(screen,name);
  randomizer_refresh_saved();randomizer_page=1;draw_settings(panel);
  snprintf(name,sizeof(name),"%s/randomizer-saved-%d-%s.bmp",argv[1],language,m?"new":"old");SDL_SaveBMP(screen,name);
  randomizer_saved_page=1;draw_settings(panel);
  snprintf(name,sizeof(name),"%s/randomizer-saved-last-%d-%s.bmp",argv[1],language,m?"new":"old");SDL_SaveBMP(screen,name);
  randomizer_import_count=Platform3DS_ListRandomizerImports(randomizer_imports,64);
  randomizer_saved_page=0;randomizer_page=4;draw_settings(panel);
  snprintf(name,sizeof(name),"%s/randomizer-import-%d-%s.bmp",argv[1],language,m?"new":"old");SDL_SaveBMP(screen,name);
  randomizer_import_count=0;draw_settings(panel);
  snprintf(name,sizeof(name),"%s/randomizer-import-empty-%d-%s.bmp",argv[1],language,m?"new":"old");SDL_SaveBMP(screen,name);
  randomizer_error_text="Existing seed save is incomplete or inconsistent. The previous selection is unchanged. Return and choose a different seed or normal game.";
  randomizer_page=3;draw_settings(panel);
  snprintf(name,sizeof(name),"%s/randomizer-error-%d-%s.bmp",argv[1],language,m?"new":"old");SDL_SaveBMP(screen,name);
  for(int error=1;error<=6;error++) {
    error_code=(RandoErrorCode)error;randomizer_page=3;draw_settings(panel);
    snprintf(name,sizeof(name),"%s/randomizer-error-%d-%d-%s.bmp",argv[1],language,error,m?"new":"old");SDL_SaveBMP(screen,name);
  }
  error_code=kRandoError_MissingPlacements;
  randomizer_error_text="";leave_settings_submenu();
  // Developer menu, its test list and the seed in play.
  tab=TAB_SETTINGS;developer_mode=true;draw_settings(panel);
  for(int i=0;i<6;i++){bounds(developer_row_r[i]);assert(developer_row_r[i].h>=20);if(i)assert(developer_row_r[i].y>=developer_row_r[i-1].y+developer_row_r[i-1].h+2);}
  bounds(developer_tests_r);assert(developer_tests_r.x>=developer_back_r.x+developer_back_r.w+4);
  assert(developer_row_r[5].y+developer_row_r[5].h<=panel.y+panel.h-3);
  snprintf(name,sizeof(name),"%s/developer-%d-%s.bmp",argv[1],language,m?"new":"old");SDL_SaveBMP(screen,name);
  scenario_selected=0;scenario_first=0;scenario_open();draw_settings(panel);
  for(int i=0;i<kScenarioRows;i++){bounds(scenario_row_r[i]);assert(scenario_row_r[i].h>=20);}
  snprintf(name,sizeof(name),"%s/tests-%d-%s.bmp",argv[1],language,m?"new":"old");SDL_SaveBMP(screen,name);
  scenario_select(kTestScenarioCount-1);draw_settings(panel);
  snprintf(name,sizeof(name),"%s/tests-last-%d-%s.bmp",argv[1],language,m?"new":"old");SDL_SaveBMP(screen,name);
  for(int i=0;i<kTestScenarioCount;i++){
   assert(UiFontWidth(TestScenario_Name(i,language),1)+UiFontWidth("00 ",1)<=scenario_row_r[0].w-16);
  }
  scenario_mode=false;load_confirm_state=true;load_confirm_mode=true;draw_settings(panel);
  snprintf(name,sizeof(name),"%s/load-state-%d-%s.bmp",argv[1],language,m?"new":"old");SDL_SaveBMP(screen,name);
  load_confirm_state=false;draw_settings(panel);
  snprintf(name,sizeof(name),"%s/load-diagnostic-%d-%s.bmp",argv[1],language,m?"new":"old");SDL_SaveBMP(screen,name);
  leave_settings_submenu();
  seed_key="aaaa";randomizer_mode=true;randomizer_page=randomizer_home=5;draw_settings(panel);
  bounds(randomizer_rows[2]);bounds(randomizer_rows[6]);bounds(randomizer_rows[7]);
  snprintf(name,sizeof(name),"%s/randomizer-current-%d-%s.bmp",argv[1],language,m?"new":"old");SDL_SaveBMP(screen,name);
  ordinary_confirm_mode=true;draw_settings(panel);
  snprintf(name,sizeof(name),"%s/randomizer-leave-%d-%s.bmp",argv[1],language,m?"new":"old");SDL_SaveBMP(screen,name);
  seed_key="";leave_settings_submenu();
 }
 UI_SetLanguage(kUiLanguage_English);
 update_mode=true;
 for(int state=UPDATE_IDLE;state<=UPDATE_ERROR;state++)for(int pre=0;pre<2;pre++){
 fixture=(UpdateStatus){.state=state,.prerelease=pre,.progress=45};strcpy(fixture.version,"v3.2-E1");strcpy(fixture.message,"YOU ARE UP TO DATE");
 update_show_notes=true;draw_settings(panel);bounds(update_channel_r);bounds(update_release_r);if(update_prev_r.w){bounds(update_prev_r);bounds(update_next_r);}bounds(update_action_r);
 assert(update_release_r.y>=update_channel_r.y+update_channel_r.h);if(update_next_r.w)assert(update_next_r.y+update_next_r.h<update_action_r.y);
 }
 snprintf(name,sizeof(name),"%s/update-%s.bmp",argv[1],m?"new":"old");SDL_SaveBMP(screen,name);
 // Actual SDL event -> production navigation -> redraw ordering, on both models.
 Qol_LoadPreferences("qol.ini");
 ss_win=(SDL_Window*)1;ss_winid=5;ss_is_new_3ds=m;art_ready=true;
 leave_settings_submenu();tab=TAB_SETTINGS;gameplay_mode=true;draw_settings(panel);
 touch(gameplay_row_r[4]);assert(qol_mode);draw_settings(panel);
 bool fast=Qol_Selected(kQolFastSwitch);int restart_before=restarts;
 touch(qol_toggle);assert(Qol_Selected(kQolFastSwitch)!=fast&&!qol_confirm&&restarts==restart_before);
 qol_select(kQolBombs99);draw_settings(panel);bool bombs=Qol_Selected(kQolBombs99);
 touch(qol_toggle);assert(qol_confirm);draw_settings(panel);
 touch(qol_no);assert(!qol_confirm&&Qol_Selected(kQolBombs99)==bombs&&restarts==restart_before);
 draw_settings(panel);touch(qol_toggle);draw_settings(panel);apply_tap(qol_yes.x+1,qol_yes.y+1);
 assert(Qol_Selected(kQolBombs99)!=bombs&&restarts==restart_before+1);
 qol_select(0);qol_mode=true;qol_confirm=false;ss_enabled=true;
 assert(!SecondScreenSDL_FilterButtons(1<<5));SecondScreenSDL_FilterButtons(0);assert(qol_selected==1);
 bool boots=Qol_Selected(kQolPegasus);SecondScreenSDL_FilterButtons(1<<8);SecondScreenSDL_FilterButtons(0);assert(Qol_Selected(kQolPegasus)!=boots);
 qol_select(kQolMirror);SecondScreenSDL_FilterButtons(1<<3);SecondScreenSDL_FilterButtons(0);assert(qol_confirm);
 SecondScreenSDL_FilterButtons(1<<0);SecondScreenSDL_FilterButtons(0);assert(!qol_confirm);
 SecondScreenSDL_FilterButtons(1<<0);SecondScreenSDL_FilterButtons(0);assert(!qol_mode&&gameplay_mode);
 leave_settings_submenu();
 update_mode=false;tab=TAB_MAP;draw_tab_bar(42);draw_settings(panel);
 tab=TAB_SETTINGS;gameplay_mode=true;draw_settings(panel);
 int requests_before=randomizer_requests;
 touch(gameplay_row_r[3]);assert(randomizer_mode&&!randomizer_confirm_mode&&randomizer_page==0);
 assert(randomizer_requests==requests_before);draw_settings(panel);
 touch(randomizer_rows[0]);assert(randomizer_page==2);draw_settings(panel);
 randomizer_seed_text[0]=0;touch(randomizer_keys[3]);touch(randomizer_keys[1]);touch(randomizer_keys[11]);
 assert(randomizer_seed==42&&randomizer_page==0);draw_settings(panel);
 int generations_before=randomizer_generations;touch(randomizer_rows[2]);
 assert(randomizer_confirm_mode&&randomizer_generations==generations_before);
 draw_settings(panel);touch(language_no_r);
 assert(randomizer_mode&&!randomizer_confirm_mode&&randomizer_page==0&&randomizer_generations==generations_before);
 draw_settings(panel);touch(randomizer_rows[2]);draw_settings(panel);touch(language_yes_r);
 assert(randomizer_generations==generations_before+1&&requested_seed==42);
 randomizer_mode=true;randomizer_page=randomizer_home=0;draw_settings(panel);
 touch(randomizer_rows[6]);assert(randomizer_page==1);draw_settings(panel);
 touch(randomizer_rows[5]);assert(randomizer_saved_page==1);draw_settings(panel);
 int resumes_before=randomizer_resumes;touch(randomizer_rows[0]);assert(randomizer_confirm_mode&&randomizer_resumes==resumes_before);
 draw_settings(panel);touch(language_yes_r);assert(randomizer_resumes==resumes_before+1);
 randomizer_mode=true;randomizer_page=randomizer_home=0;draw_settings(panel);
 int ordinary_before=ordinary_requests;touch(randomizer_rows[7]);assert(ordinary_confirm_mode&&ordinary_requests==ordinary_before);
 draw_settings(panel);touch(language_no_r);assert(!ordinary_confirm_mode&&ordinary_requests==ordinary_before);
 draw_settings(panel);touch(randomizer_rows[7]);draw_settings(panel);touch(language_yes_r);assert(ordinary_requests==ordinary_before+1);
 randomizer_mode=true;randomizer_page=randomizer_home=0;draw_settings(panel);
 touch(randomizer_rows[3]);assert(randomizer_page==4);draw_settings(panel);
 int imports_before=import_requests;touch(randomizer_rows[0]);assert(randomizer_confirm_mode&&import_requests==imports_before);
 draw_settings(panel);touch(language_yes_r);assert(import_requests==imports_before+1);
 randomizer_mode=true;randomizer_page=4;draw_settings(panel);
 import_ok=false;touch(randomizer_rows[1]);draw_settings(panel);touch(language_yes_r);assert(randomizer_page==3);import_ok=true;
 leave_settings_submenu();tab=TAB_MAP;
 // Inside a seed the Randomizer row opens the seed's page; Normal game asks.
 seed_key="aaaa";tab=TAB_SETTINGS;gameplay_mode=true;draw_settings(panel);
 touch(gameplay_row_r[3]);assert(randomizer_mode&&randomizer_page==5&&!randomizer_confirm_mode);draw_settings(panel);
 ordinary_before=ordinary_requests;touch(randomizer_rows[7]);assert(ordinary_confirm_mode&&ordinary_requests==ordinary_before);
 draw_settings(panel);touch(language_no_r);assert(!ordinary_confirm_mode&&randomizer_page==5);
 draw_settings(panel);touch(randomizer_rows[6]);assert(randomizer_page==1);touch(randomizer_back_r);assert(randomizer_page==5);
 draw_settings(panel);touch(randomizer_rows[7]);draw_settings(panel);touch(language_yes_r);assert(ordinary_requests==ordinary_before+1);
 seed_key="";leave_settings_submenu();
 // Developer rows: save, load (only with a state), diagnostic; Start opens the tests.
 ss_enabled=true;tab=TAB_SETTINGS;developer_mode=true;draw_settings(panel);
 int saves_before=state_saves,loads_before=state_loads;touch(developer_row_r[1]);assert(state_saves==saves_before+1);
 state_exists=false;touch(developer_row_r[2]);assert(!load_confirm_mode);state_exists=true;
 touch(developer_row_r[2]);assert(!load_confirm_mode&&state_loads==loads_before+1);
 draw_settings(panel);touch(developer_row_r[3]);assert(load_confirm_mode&&!load_confirm_state);draw_settings(panel);touch(load_cancel_r);
 #define F SecondScreenSDL_FilterButtons
 assert(F(1<<8)==(1<<8));F(0);
 assert(F(1<<3)==0&&scenario_mode);assert(F(1<<3)==0);
 // Down pressed before Start is released still moves the list.
 scenario_select(0);assert(F((1<<3)|(1<<5))==0&&scenario_selected==1);F(0);
 scenario_requested=-1;scenario_select(0);for(int i=0;i<3;i++){assert(F(1<<5)==0);F(0);}
 assert(scenario_selected==3);F(1<<11);F(0);assert(scenario_selected==8&&scenario_first==4);
 F(1<<4);F(0);assert(scenario_selected==7);
 // Held Down repeats after 20 frames, every 4.
 for(int i=0;i<28;i++)F(1<<5);F(0);assert(scenario_selected==7+1+3);
 scenario_first=4;scenario_select(7);
 draw_settings(panel);touch(scenario_row_r[0]);assert(scenario_selected==4);
 F(1<<8);assert(load_confirm_mode&&load_confirm_scenario&&scenario_requested<0);F(0);
 F(1<<0);assert(!load_confirm_mode&&scenario_mode&&scenario_requested<0);F(0);
 F(1<<8);F(0);assert(load_confirm_mode&&scenario_requested<0);
 F(1<<8);assert(scenario_requested==4&&!scenario_mode&&tab==TAB_MAP&&!load_confirm_mode);
 // The A that loaded stays the menu's until released; other buttons pass.
 assert(F(1<<8)==0);assert(F((1<<8)|(1<<5))==(1<<5));assert(F(0)==0);assert(F(1<<3)==(1<<3));F(0);
 tab=TAB_SETTINGS;developer_mode=true;F(1<<3);assert(scenario_mode&&scenario_selected==4);
 F(0);F(1<<0);assert(!scenario_mode&&developer_mode);assert(F(1<<0)==0);F(0);
 #undef F
 leave_settings_submenu();tab=TAB_MAP;

 for(int i=0;i<100;i++) {touch(tab_gear_r);assert(tab==TAB_GEAR);touch(tab_map_r);assert(tab==TAB_MAP);}
 touch(tab_items_r);assert(tab==TAB_ITEMS);touch_expect(tab_items_r,true);assert(tab==TAB_ITEMS);
 touch(tab_gear_r);assert(tab==TAB_GEAR);touch_expect(tab_gear_r,true);assert(tab==TAB_GEAR);
 touch(tab_map_r);touch_expect(tab_map_r,true);assert(tab==TAB_MAP);
 // Visible borders/gaps and strip edges accept one touch, with no content-area leakage.
 RectFS*tabs[]={&tab_gear_r,&tab_map_r,&tab_items_r,&tab_settings_r};
 const int targets[]={TAB_GEAR,TAB_MAP,TAB_ITEMS,TAB_SETTINGS};
 for(int i=0;i<4;i++)for(int y=198;y<=239;y+=41){
   float samples[]={tabs[i]->x,tabs[i]->x+tabs[i]->w/2,tabs[i]->x+tabs[i]->w-1};
   for(int j=0;j<3;j++){tab=targets[i]==TAB_MAP?TAB_ITEMS:TAB_MAP;touch((RectFS){samples[j],y,0,0});assert(tab==targets[i]);}
 }
 tab=TAB_MAP;touch((RectFS){0,239,0,0});assert(tab==TAB_GEAR);
 tab=TAB_MAP;touch((RectFS){319,239,0,0});assert(tab==TAB_SETTINGS);
 assert(tab_at_position(50,197)==-1&&tab_at_position(-1,220)==-1&&tab_at_position(320,220)==-1);
 tab=TAB_MAP;
 touch(tab_settings_r);assert(tab==TAB_SETTINGS);
 fixture.state=UPDATE_CURRENT;touch(settings_row_r[3]);assert(update_mode);
 draw_settings(panel);touch(update_release_r);assert(update_show_notes);draw_settings(panel);
 touch(update_next_r);assert(update_page==1);touch(update_prev_r);assert(update_page==0);
 bool pre=fixture.prerelease;touch(update_channel_r);assert(fixture.prerelease!=pre);
 touch(update_back_r);assert(!update_mode);
 touch(settings_row_r[3]);assert(update_mode);fixture.state=UPDATE_CHECKING;
 int old_cancels=cancels;touch(update_back_r);assert(!update_mode&&cancels==old_cancels+1);
 fixture.state=UPDATE_AVAILABLE;touch(settings_row_r[3]);draw_settings(panel);
 touch(update_action_r);assert(update_confirm);touch(update_action_r);assert(downloads>0);
 touch(update_back_r);assert(!update_mode);int old_restarts=restarts;touch(settings_row_r[4]);assert(restarts==old_restarts+1);
 ss_win=NULL;
 destroy_textures();SDL_DestroyRenderer(ss_r);SDL_FreeSurface(screen);
 }
 NativeTouch_Shutdown();assert(!native_touch_event);
 SDL_EventFilter restored;void*restored_data;SDL_GetEventFilter(&restored,&restored_data);
 assert(restored==prior_filter&&restored_data==&prior_calls&&prior_calls>76800);
 assert(NativeTouch_Init());NativeTouch_Shutdown(); // ROM restart can reinstall the filter.
 SDL_DestroyRenderer(projection);SDL_DestroyWindow(projection_window);
 SDL_Quit();puts("PASS: 76800 physical pixels through real SDL queue and renderer watch; E3 viewport control shifts; native KEY_TOUCH edges reject residual startup/release coordinates; actual settings/update drawing on Old RGB565 and New ARGB8888; five compact rows with equal touch heights and no empty slot, both channels/all states, no control overlap. 200 tab switches/model, border/gap/edge targets, selected tabs refresh without changing views, paused Update controls, channel/notes/pages/back/cancel/install/restart, NULL-window touch and no synthetic-mouse duplicate. Production Python-generated native font used for screenshots.");}
'''
code=code.replace('typedef struct {unsigned short px,py;} touchPosition;','')
(out/'3ds.h').write_text('#pragma once\n#define KEY_TOUCH (1u<<20)\ntypedef struct {unsigned short px,py;} touchPosition;\nstatic void hidScanInput(void){}\nstatic unsigned hidKeysHeld(void);\nstatic void hidTouchRead(touchPosition*);\n')
(out/'ui-test.c').write_text(code)
config=str(args.sdl_root.resolve()/'sdl2-config') if args.sdl_root else args.sdl_config
prefix=subprocess.check_output([config,'--prefix'],text=True).strip()
flags=shlex.split(subprocess.check_output([config,'--cflags','--static-libs'],text=True).replace(prefix,shlex.quote(prefix)))
# The touch harness calls SDL's internal N3DS event senders. They are hidden
# from the shared library; the pinned static SDL build retains those symbols.
flags=[str(Path(prefix)/'lib/libSDL2.a') if x=='-lSDL2' else x for x in flags]
cmd=['cc','-DSDL_MAIN_HANDLED','-O1','-fsanitize=address,undefined','-ffunction-sections','-fdata-sections',
 '-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections',
 '-I'+str(out),'-I'+str(r/'engine'),'-I'+str(src.parent),'-I'+str(r/'platform/3ds/source'),
 str(out/'ui-test.c'),str(r/'engine/src/ui_strings.c'),str(r/'engine/src/quality_of_life.c'),str(r/'engine/src/test_scenarios.c'),*flags,'-o',str(out/'ui-test')]
with (out/'ui-test.log').open('w') as log:
 subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
 subprocess.run([str(out/'ui-test'),str(out)],cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
print((out/'ui-test.log').read_text().splitlines()[-1])
