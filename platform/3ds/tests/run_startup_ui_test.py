#!/usr/bin/env python3
"""Render real startup/progress views and exercise their native input loops."""
from pathlib import Path
import argparse,subprocess,struct,zlib,re,json
r=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--logo',type=Path);p.add_argument('--face',type=Path,help='Private native 16x16 ARGB portrait');a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
s=(r/'platform/3ds/source/platform_3ds.c').read_text()
def function(name):
 match=re.search(re.escape(name)+r'[^;{]*\{',s);assert match,name
 start=match.start();end=match.end();depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]+'\n'
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "platform/linux/ui_font.h"
#include "platform/linux/native_menu_button.h"
#include "ui_strings.h"
#include "quality_of_life.h"
#include "file_store.h"
#include "native_title_credit.h"
#include "native_list_layout.h"
#include "ui_glyphs_intl.h"
#include "game_languages.h"
#include "setup_selector_assets.h"
#include "setup_pattern.h"
#include "generated_operation_plaque.h"
#include "platform/linux/ss_textures.h"
typedef int gfxScreen_t;
enum{GFX_TOP,GFX_BOTTOM,KEY_A=1,KEY_B=2,KEY_START=4,KEY_Y=8,KEY_DUP=16,KEY_DDOWN=32,KEY_TOUCH=64,KEY_L=128,KEY_R=256,KEY_DLEFT=512,KEY_DRIGHT=1024};
typedef unsigned u32;
typedef struct{unsigned short px,py;}touchPosition;
typedef struct{char filename[64],profile[64];}RomEntry;
static RomEntry g_rom_entries[2]={{"wrong.sfc","wrong"},{"usa.sfc","active"}};
static const char *g_active_profile="active";
#define countof(x) (sizeof(x)/sizeof(*(x)))
static uint16_t g_setup_top_pixels[400*240],g_setup_bottom_pixels[320*240];
static int g_setup_progress_language;
static uint64_t time_now;static uint64_t osGetTime(void){return time_now++;}
static void ZeldaAnimateStartupLogoWithAudio(uint32_t *p,bool(*draw)(const uint32_t*,unsigned,void*),void(*audio)(const int16_t*,unsigned,void*),void *c){}
static void SetupAudioPlayIntro(const int16_t *p,unsigned n,void *c){}
static void SetupAudioStopIntro(void){}
static void Platform3DS_RevealStartupCard(void){}
static void gspWaitForVBlank(void){}
static void SetupAudioInit(void){}
static void SetupAudioStartMusic(void){}
static float g_setup_music_step,g_setup_music_target;enum{kSetupMusicFadeInFrames=61};
static int fades;static bool g_setup_audio_preserve;
static void SetupAudioFadeOutAndStop(void){fades++;}
static int move_sounds;static void SetupAudioPlayMove(void){move_sounds++;}
static int mods,reset,lang=0;
static void ShowModsMenu(const RomEntry *rom){assert(!strcmp(rom->profile,"active"));mods++;}
static int ReadChosenLanguage(void){return lang;}
static bool WriteChosenLanguage(int selected){lang=selected;return true;}
static void Platform3DS_RequestReset(void){reset++;}
static void CreateSetupDump(const char*s){}
static void Platform3DS_FormatSavePath(const char *name,char *out,size_t n){snprintf(out,n,"%s",name);}
static void BeginSetupConsole(void){}
static void EndSetupConsole(void){}
static unsigned events[12],event_count,event_next,current;
static touchPosition point;
typedef struct{short dx,dy;}circlePosition;static circlePosition circle;
static void hidCircleRead(circlePosition *p){*p=circle;}
static uint32_t fixture_face[256];static bool face_ready;
static bool SS_RenderOriginalLinkFace(uint32_t *p){memcpy(p,fixture_face,sizeof(fixture_face));return face_ready;}
static void hidTouchRead(touchPosition *p){*p=point;}
static void hidScanInput(void){current=event_next<event_count?events[event_next++]:0;}
static u32 hidKeysDown(void){return current;}
static u32 hidKeysHeld(void){return current;}
static bool aptMainLoop(void){return event_next<event_count;}
static bool record_setup_fade;static void RecordSetupFade(void);
static bool forbid_black;static void PresentSetupConsole(void){if(record_setup_fade)RecordSetupFade();if(forbid_black){bool lit=false;for(int i=0;i<400*240;i++)lit|=g_setup_top_pixels[i]!=0;assert(lit);}}
static void PresentSetupStable(void(*draw)(void*),void *ctx){draw(ctx);}
'''
code+=s[s.index('static uint16_t SetupRgb565('):s.index('static void SetupClearScreens(')]
code+=function('static bool SetupCanDraw(')+function('static const char *LanguageText(')
code+=function('static bool IsRegularFile(')
code+='static bool g_startup_menu_seen,g_startup_menu_pending,g_startup_intro_seen,g_startup_reopen_once,g_startup_card_pending;\nstatic const uint32_t *g_setup_menu_logo;\n'
code+=function('void Platform3DS_RequestStartupMenu(')
for f in ['static void SetupPurpleBackground(','static const char *SetupUiText(', 'static void SetupMenuSpan(','static void SetupStyledButton(','static void SetupMenuText(','static void SetupMenuLabel(','static void SetupFooter(','static int SetupFooterTouch(','static u32 SetupNavigationKeys(','static void SetupPanel(','static void SetupTitle(','static int SetupWrap(','static void SetupDrawLogoPixels(','static void SetupDrawLogo(','static void SetupCreditPixel(', 'static void SetupCreditSpan(', 'static void SetupDrawStartupCreditsFaded(', 'static void SetupDrawStartupCredits(','static bool SetupHasStarted(','static void SetupMarkStarted(','static void SetupMenuButton(','static void DrawOperationProgress(','static int SetupMenuTouch(', 'static void DrawLanguageSelector(']:code+=function(f)
code+=s[s.index('typedef struct LanguageSelectorContext'):s.index('// Returns the chosen language,')]
code+=s[s.index("typedef struct SetupQolContext"):s.index("static void DrawLanguageSelector(")]
code+=s[s.index('enum { kMaxMods ='):s.index('static ModEntry g_mod_entries')]
code+='enum { kZsprHeaderSize = 29 };\nstatic bool Mods_ZsprLayout(const uint8_t *h,size_t n,size_t f,uint32_t *p,uint32_t *c){return false;}\n'
code+=s[s.index('typedef struct ModsMenuContext'):s.index('static void SetupPurpleBackground(bool);')]
code+=function('static void DrawModsMenu(')
code+=function('static int SelectLanguage(')
code+=s[s.index('typedef struct StartupMenuContext'):s.index('void Platform3DS_RevealStartupCard(void) {')]+s[s.index('bool Platform3DS_StartupMenuPending(void) {'):s.index('// The USA cartridge every language is built from:')]
code+=function('static void DrawMissingRomFrame(')
code+=r'''
#include <sys/stat.h>
static void sequence(unsigned first,unsigned second){events[0]=first;events[1]=second;event_count=second?2:1;event_next=0;}
static void shot(const char *root,const char *name,int screen){
 char path[1024];snprintf(path,sizeof(path),"%s/%s-%s.ppm",root,name,screen?"bottom":"top");
 FILE*f=fopen(path,"wb");assert(f);int width=screen?320:400;uint16_t *pixels=screen?g_setup_bottom_pixels:g_setup_top_pixels;
 fprintf(f,"P6\n%d 240\n255\n",width);
 for(int i=0;i<width*240;i++){uint16_t c=pixels[i];uint8_t rgb[3]={((c>>11)*255+15)/31,(((c>>5)&63)*255+31)/63,((c&31)*255+15)/31};fwrite(rgb,1,3,f);}fclose(f);
}
static const char *fade_root;static unsigned fade_frame;static uint64_t previous_sum;
static void RecordSetupFade(void){
 uint64_t sum=0;for(int i=0;i<400*240;i++)sum+=g_setup_top_pixels[i];for(int i=0;i<320*240;i++)sum+=g_setup_bottom_pixels[i];
 assert(sum<=previous_sum);previous_sum=sum;
 char name[64];snprintf(name,sizeof(name),"continue-fade-%02u",++fade_frame);shot(fade_root,name,0);shot(fade_root,name,1);
}
int main(int argc,char**argv){
 int resets_before=reset;Platform3DS_RequestStartupMenu();assert(g_startup_reopen_once&&reset==resets_before+1);g_startup_reopen_once=false;
 for(int page=0;page<3;page++)assert(Qol_PageFirst(page)==page*5&&Qol_PageSize(page)==5);
 assert(Qol_StepPage(0,1)==5&&Qol_StepPage(5,1)==10&&Qol_StepPage(10,-1)==5&&Qol_StepPage(5,-1)==0);
 current=0;circle=(circlePosition){0,-30};assert(!SetupNavigationKeys(0));
 circle.dy=-100;assert(SetupNavigationKeys(0)&KEY_DDOWN);assert(!SetupNavigationKeys(0));
 time_now+=310;assert(SetupNavigationKeys(0)&KEY_DDOWN);circle=(circlePosition){0,0};assert(!SetupNavigationKeys(0));
 circle.dx=100;assert(SetupNavigationKeys(0)&KEY_DRIGHT);circle=(circlePosition){0,0};assert(!SetupNavigationKeys(0));
 assert(argc>=2&&argc<=4);char prefs[1024];snprintf(prefs,sizeof(prefs),"%s/qol.ini",argv[1]);Qol_LoadPreferences(prefs);static uint32_t logo[256*224];
 if(argc>=3){FILE*f=fopen(argv[2],"rb");assert(f);assert(fread(logo,4,256*224,f)==256*224);fclose(f);}
 if(argc==4){FILE*f=fopen(argv[3],"rb");assert(f);assert(fread(fixture_face,4,256,f)==256);fclose(f);face_ready=true;}
 StartupMenuContext fade_menu={logo,1,0};DrawStartupMenu(&fade_menu);
 shot(argv[1],"continue-fade-00",0);shot(argv[1],"continue-fade-00",1);
 fade_root=argv[1];previous_sum=UINT64_MAX;record_setup_fade=true;sequence(0,0);
 FadeSetupToBlack();record_setup_fade=false;assert(fade_frame==19&&previous_sum==0&&g_setup_music_target==0);
 for(int phase=0;phase<=18;phase+=3){
  SetupPurpleBackground(false);SetupDrawLogo(logo);SetupDrawStartupCreditsFaded(logo,phase);
  char name[32];snprintf(name,sizeof(name),"credits-fade-%02d",phase);shot(argv[1],name,0);
 }
 for(int selected=0;selected<2;selected++){
  memset(g_setup_bottom_pixels,0,sizeof(g_setup_bottom_pixels));SetupMenuButton(0,5,"CONTINUE",selected);
  char name[32];snprintf(name,sizeof(name),"button-face%d",selected);shot(argv[1],name,1);
 }

 for(unsigned step=0;step<=18;step+=3){DrawMissingRomFrame(&step);char name[64];snprintf(name,sizeof(name),"missing-rom-%02u",step);shot(argv[1],name,0);shot(argv[1],name,1);}
 mkdir("saves",0700);remove("startup-started.flag");remove("saves/sram.dat");assert(!SetupHasStarted());
 uint8_t fixture[8192]={0};fixture[0x3e5]=0xaa;fixture[0x3e6]=0x55;
 unsigned sum=0;for(int i=0;i<0x500;i+=2)sum+=fixture[i]|fixture[i+1]<<8;
 unsigned correction=(0x5a5a-sum)&65535;fixture[0x4fe]=correction;fixture[0x4ff]=correction>>8;
 FILE *sf=fopen("saves/sram.dat","wb");assert(sf);fwrite(fixture,1,sizeof(fixture),sf);fclose(sf);assert(SetupHasStarted());
 fixture[3]^=1;sf=fopen("saves/sram.dat","wb");fwrite(fixture,1,sizeof(fixture),sf);fclose(sf);assert(!SetupHasStarted());remove("saves/sram.dat");
 for(int language=0;language<kGameLanguageCount;language++){
  char name[64];lang=language;
  remove("startup-started.flag");assert(!SetupHasStarted());
  StartupMenuContext context={logo,language,0};DrawStartupMenu(&context);
  snprintf(name,sizeof(name),"%s-home-start",kGameLanguages[language].code);shot(argv[1],name,0);shot(argv[1],name,1);
  SetupMarkStarted();assert(SetupHasStarted());DrawStartupMenu(&context);
  snprintf(name,sizeof(name),"%s-home",kGameLanguages[language].code);shot(argv[1],name,0);shot(argv[1],name,1);
  DrawLanguageSelector(language,-1);snprintf(name,sizeof(name),"%s-language",kGameLanguages[language].code);shot(argv[1],name,0);shot(argv[1],name,1);
  for(int option=0;option<kQolCount;option++) {
    DrawGameplayQolTop(language,option);
    unsigned c=kSSTexMenu[0];
    assert(g_setup_top_pixels[0]==SetupRgb565(c>>16,(c>>8)&255,c&255));
    snprintf(name,sizeof(name),"%s-qol-game-%02d",kGameLanguages[language].code,option);shot(argv[1],name,0);
  }
  SetupQolContext qm={language,0,0,false};DrawSetupQol(&qm);snprintf(name,sizeof(name),"%s-qol-live",kGameLanguages[language].code);shot(argv[1],name,0);shot(argv[1],name,1);
  for(int page=0;page<3;page++) {qm.selected=page*5+2;qm.first=page*5;DrawSetupQol(&qm);snprintf(name,sizeof(name),"%s-qol-page%d",kGameLanguages[language].code,page+1);shot(argv[1],name,0);shot(argv[1],name,1);}
  qm.selected=14;qm.first=10;DrawSetupQol(&qm);snprintf(name,sizeof(name),"%s-qol-restart",kGameLanguages[language].code);shot(argv[1],name,0);shot(argv[1],name,1);
  point=(touchPosition){200,215};current=KEY_TOUCH;bool touch_before=Qol_Selected(kQolFastSwitch);
  sequence(KEY_TOUCH,KEY_B);assert(!ShowSetupQol(language)&&Qol_Selected(kQolFastSwitch)==touch_before);
  current=0;sequence(KEY_TOUCH,KEY_B);assert(ShowSetupQol(language)&&Qol_Selected(kQolFastSwitch)!=touch_before);
  int old_moves=move_sounds;current=0;sequence(KEY_DDOWN,KEY_B);assert(!ShowSetupQol(language)&&move_sounds==old_moves+1);
  bool before_qol=Qol_Selected(kQolFastSwitch);sequence(KEY_START,KEY_B);assert(ShowSetupQol(language)&&Qol_Selected(kQolFastSwitch)!=before_qol);
  sequence(KEY_DDOWN,KEY_A);events[2]=KEY_B;event_count=3;assert(ShowSetupQol(language));
  g_setup_progress_language=language;
  for(int percent=0;percent<=100;percent+=25) {
   uint16_t top_before[400*240],bottom_before[320*240];memcpy(top_before,g_setup_top_pixels,sizeof(top_before));memcpy(bottom_before,g_setup_bottom_pixels,sizeof(bottom_before));
   DrawOperationProgress(false,percent);assert(!memcmp(top_before,g_setup_top_pixels,sizeof(top_before))&&!memcmp(bottom_before,g_setup_bottom_pixels,sizeof(bottom_before)));
   DrawOperationProgress(true,percent);assert(!memcmp(top_before,g_setup_top_pixels,sizeof(top_before)));
   assert(g_setup_bottom_pixels[0]==kSetupPattern[40]);
   snprintf(name,sizeof(name),"%s-dump-%03d",kGameLanguages[language].code,percent);shot(argv[1],name,0);shot(argv[1],name,1);
  }
  ModEntry skins[6]={0};
  const char *names[]={"CONKER","DARK LINK","MARIO (CLASSIC)","MINISH CAP LINK","SAMUS (SUPER METROID)","ZELDA"};
  const char *folders[]={"conker","dark-link","mario-classic","minish-cap-link","samus-super-metroid","zelda"};
  for(int i=0;i<6;i++) {skins[i].base=MODS_FIXTURE;snprintf(skins[i].folder,96,"%s",folders[i]);snprintf(skins[i].name,40,"%s",names[i]);strcpy(skins[i].category,"SKIN");strcpy(skins[i].info[0],"By Charmander106/SePH");strcpy(skins[i].info[1],"Character graphics, armor and glove colors.");strcpy(skins[i].info[2],"Requires v4.0 or later.");}
  ModsMenuContext mm={0};mm.mods=skins;mm.count=6;mm.starts[1]=6;mm.categories=1;
  for(int i=0;i<6;i++) {mm.selected=i;DrawModsMenu(&mm);snprintf(name,sizeof(name),"%s-mods-%d",kGameLanguages[language].code,i);shot(argv[1],name,0);shot(argv[1],name,1);}
  mm.selected=-1;DrawModsMenu(&mm);snprintf(name,sizeof(name),"%s-mods-original",kGameLanguages[language].code);shot(argv[1],name,0);shot(argv[1],name,1);
  mm.count=0;DrawModsMenu(&mm);snprintf(name,sizeof(name),"%s-mods-empty",kGameLanguages[language].code);shot(argv[1],name,0);shot(argv[1],name,1);
  forbid_black=true;int oldfades=fades;
  point=(touchPosition){200,215};current=KEY_TOUCH;int oldmods=mods;
  sequence(KEY_TOUCH,KEY_B);assert(SelectLanguage(language,-1,StartupActiveRom())==-2&&mods==oldmods);
  current=0;sequence(KEY_B,0);assert(SelectLanguage(language,-1,StartupActiveRom())==-2);
  current=0;sequence(KEY_A,0);assert(SelectLanguage(language,-1,StartupActiveRom())==language);
  current=0;sequence(KEY_START,0);assert(SelectLanguage(language,-1,StartupActiveRom())==language);
  current=0;sequence(KEY_R,KEY_START);assert(SelectLanguage(language,-1,StartupActiveRom())==NativeListStepPage(language,1,kGameLanguageCount));
  current=0;point=(touchPosition){20,215};sequence(KEY_TOUCH,KEY_START);assert(SelectLanguage(language,-1,StartupActiveRom())==NativeListStepPage(language,-1,kGameLanguageCount));
  current=0;sequence(KEY_Y,KEY_START);int before=mods;assert(SelectLanguage(language,-1,StartupActiveRom())==language&&mods==before);
  current=0;point=(touchPosition){80,215};sequence(KEY_TOUCH,0);assert(SelectLanguage(language,-1,StartupActiveRom())==-2);
  lang=-1;current=0;sequence(KEY_TOUCH,0);assert(SelectLanguage(language,-1,StartupActiveRom())==-1);lang=language;
  assert(fades==oldfades);forbid_black=false;
  current=0;sequence(KEY_START,0);assert(Platform3DS_ShowStartupMenu(logo)==1);
  current=0;sequence(KEY_DDOWN,KEY_START);events[2]=0;events[3]=language?KEY_DUP:KEY_DDOWN;events[4]=KEY_START;event_count=5;assert(Platform3DS_ShowStartupMenu(logo)==0&&reset>0);
  current=0;point=(touchPosition){160,195};sequence(KEY_TOUCH,0);assert(Platform3DS_ShowStartupMenu(logo)==-1);
 }
 puts("PASS real startup menus: seven languages; A/Start confirm; touch Language/Mods/Quit; cold home and reset; startup/progress/QoL/Mods captures; valid SRAM and first-run detection; loading requests leave both screens untouched; purple dump progress preserves upper screen; Circle Pad deadzone/repeat; no language black frame or music teardown");
}
'''
code=code.replace('MODS_FIXTURE',json.dumps(str(r.parent.parent/'TestBuilds/v4.0-E1/mods')))
code=code.replace('  int scale=2;\n  SetupMenuText', '  int scale=2;assert(UiFontWidth(label,scale)<=220);\n  SetupMenuText')
code=code.replace('  SetupMenuText(GFX_BOTTOM,label,x+', '  assert(UiFontWidth(label,scale)<=w-12);\n  SetupMenuText(GFX_BOTTOM,label,x+')
code=code.replace('  SetupPanel(target,x,y,w,h);', '  assert(UiFontWidth(title,2)<=w-12);SetupPanel(target,x,y,w,h);')
(out/'startup.c').write_text(code)
subprocess.run(['cc','-O1','-fsanitize=address,undefined','-I'+str(r/'engine/src'),'-I'+str(r/'platform/3ds/source'),str(out/'startup.c'),str(r/'engine/src/ui_strings.c'),str(r/'engine/src/quality_of_life.c'),'-o',str(out/'startup')],check=True)
subprocess.run([str(out/'startup'),str(out),*([str(a.logo.resolve())]if a.logo else[]),*([str(a.face.resolve())]if a.face else[])],cwd=out,check=True)
def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
for path in out.glob('*.ppm'):
 _,dims,_,raw=path.read_bytes().split(b'\n',3);w,h=map(int,dims.split());scan=b''.join(b'\0'+raw[y*w*3:(y+1)*w*3] for y in range(h))
 path.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b''))
