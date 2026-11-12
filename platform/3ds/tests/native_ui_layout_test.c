// Exercise production drawing, translated text bounds and touch navigation on
// both bottom-screen formats. Optional private art comes from the user's USA
// assets; without it all settings and their interactions still run.
#include <SDL.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "hardware_profile.h"
#include "startup_card.h"
#include "updater.h"
#include "update_view.h"
static bool new_model;
static int fixture_module=9,fixture_dungeon;
static bool fixture_indoors;
static const char *art_directory;
static UpdateStatus update_fixture;
static const Platform3DSHardwareProfile *Platform3DS_GetHardwareProfile(void) {
  return Platform3DS_ProfileForModel(new_model);
}
enum { kTurboControlAuto, kTurboControlX, kTurboControlZL, kTurboControlCStick };
static int fixture_turbo, selector_requests, startup_requests, menu_move_sounds;
static void Platform3DS_PlayMenuMove(void){menu_move_sounds++;}
static void Platform3DS_RequestRomSelection(void){selector_requests++;}
static void Platform3DS_RequestStartupMenu(void){startup_requests++;}
#include "src/rando/seed_summary.h"
static int Platform3DS_ListRandomizerSeeds(RandoSeedSummary *out,int cap){return 0;}
static int Platform3DS_ListRandomizerImports(RandoImportFile *out,int cap){return 0;}
static const char *Platform3DS_RandomizerSeedKey(void){return "";}
static RandoErrorCode Platform3DS_RandomizerErrorCode(void){return kRandoError_None;}
static bool Platform3DS_RequestImportedSeed(const char *name){return false;}
static bool Platform3DS_RequestSavedSeed(const char *key){return false;}
static bool Platform3DS_RequestGeneratedSeed(int preset,uint64_t seed){return false;}
static void Platform3DS_RequestOrdinaryGame(void){}
static void Platform3DS_RequestRandomizerMenu(void){}
static int Platform3DS_GetDisplayMode(void){return 1;}
static int Platform3DS_GetWideEdgeMode(void){return 1;}
static int Platform3DS_GetWideZoomIndex(void){return 0;}
static int Platform3DS_GetTurboMultiplier(void){return 5;}
static int Platform3DS_GetTurboControl(void){return fixture_turbo;}
static bool Platform3DS_GetAutosave(void){return true;}
static bool Platform3DS_GetShowFps(void){return false;}
static bool Platform3DS_IsNew3DS(void){return new_model;}
static bool Platform3DS_CanUseCore1PpuWorker(void){return !new_model;}
static void observe_text(const char *,float,float,int);
#define ZELDA3_TEST_3DS_UI 1
#define ZELDA3_TEST_NATIVE_MENUS 1
#define ZELDA3_TEST_UI_TEXT observe_text
#include "src/platform/linux/second_screen_sdl.c"

typedef struct TextBox { int x,y,w,h; char text[256]; } TextBox;
static TextBox text_boxes[256];
static unsigned text_count,captures,text_checks;
static const char *scene;
static int scene_lang;
static void observe_text(const char *str,float x,float y,int scale) {
  if(!*str)return;
  int min_x=10000,min_y=10000,max_x=-1,max_y=-1,cursor=0;const char *p=str;unsigned code;
  while((code=UiFontNextCode(&p))) {
    int rows=UiFontGlyphRows(code,scale),width=UiFontGlyphColumns(code,scale);
    for(int yy=0;yy<rows;yy++)for(int xx=0;xx<width;xx++){
      unsigned bits=UiFontGlyphBits(code,scale,yy);
      if(bits&(1u<<(width-1-xx))){int px=cursor+xx,py=yy+UiFontGlyphTop(code,scale);
        if(px<min_x)min_x=px;if(py<min_y)min_y=py;if(px>max_x)max_x=px;if(py>max_y)max_y=py;}
    }
    cursor+=UiFontAdvance(code,scale);
  }
  if(max_x<0)return;
  TextBox box={(int)lroundf(x)+min_x,(int)lroundf(y)+min_y,max_x-min_x+1,max_y-min_y+1,{0}};
  snprintf(box.text,sizeof(box.text),"%s",str);
  assert(scale>=1&&scale<=3);
  if(box.x<0||box.y<0||box.x+box.w>320||box.y+box.h>240) {
    fprintf(stderr,"OUT OF SCREEN %s lang %d: %s [%d,%d,%d,%d]\n",scene,scene_lang,str,box.x,box.y,box.w,box.h);abort();
  }
  for(unsigned i=0;i<text_count;i++) {
    TextBox *a=&text_boxes[i];
    if(box.x<a->x+a->w && box.x+box.w>a->x && box.y<a->y+a->h && box.y+box.h>a->y) {
      fprintf(stderr,"TEXT COLLISION %s lang %d: '%s' / '%s'\n",scene,scene_lang,str,a->text);abort();
    }
  }
  assert(text_count<256);text_boxes[text_count++]=box;text_checks++;
}

bool SS_IsIndoors(void){return fixture_indoors;}
bool SS_IsWidescreen(void){return true;}
bool SS_IsHudHidden(void){return false;}
int SS_GetCapturedButton(void){return -1;}
void SS_SetGamepadControls(const int *p){(void)p;}
void SS_GetGamepadControls(int *p){for(int i=0;i<12;i++)p[i]=i;}
void SS_ArmButtonCapture(bool on){(void)on;}
int SS_GetDungeon(void){return fixture_dungeon;}
int SS_GetModule(void){return fixture_module;}
int SS_GetArea(void){return fixture_indoors?0x51:0x2c;}
int SS_GetInventorySize(void){return (Qol_Features1(0)&(1u<<10))?24:20;}
int SS_GetInventorySlot(int slot){
 if(slot<1||slot>SS_GetInventorySize())return -1;
 int i=slot-1,kind=i,level;
 if(i>=20){kind=15;level=sram8(0x5c+i-20);}
 else if(i==15&&SS_GetInventorySize()==24){kind=12;level=sram8(0x4c)>0?1:0;}
 else level=i==15?bottle_level():sram8(0x40+i);
 return level>0?(kind<<8)|(level>7?7:level):-1;
}
int SS_GetEquippedSlot(void){return 1;}
void SS_Set3DSDisplayMode(int v){(void)v;}
void SS_Set3DSWideEdgeMode(int v){(void)v;}
void SS_SetHudHidden(bool v){(void)v;}
void SS_RequestMemoryDump(const char *p){(void)p;}
void SS_RequestLoadLatestDumpState(void){}
void SS_RequestRestart(void){}
void SS_SaveLoadState(bool save){}
bool SS_HasSaveState(void){return false;}
void SS_RequestTestScenario(int index){}
bool SS_TestScenarioAvailable(int index){return false;}
void SS_EquipSlot(int s){(void)s;}
void SS_ReadSram(uint8_t *p,int n){memcpy(p,sram,n);}
bool SS_GetMirrorPortal(int *p){p[0]=1600;p[1]=1600;return true;}
void Updater_GetStatus(UpdateStatus *p){*p=update_fixture;}
static bool load_art(const char *name,uint32_t *pixels,int width,int height) {
  if(!art_directory)return false;
  char path[1024];snprintf(path,sizeof(path),"%s/%s.bmp",art_directory,name);
  SDL_Surface *image=SDL_LoadBMP(path);if(!image){fprintf(stderr,"%s: %s\n",path,SDL_GetError());abort();}
  SDL_Surface *argb=SDL_ConvertSurfaceFormat(image,SDL_PIXELFORMAT_ARGB8888,0);assert(argb);
  assert(argb->w==width&&argb->h==height);
  for(int y=0;y<height;y++)memcpy(pixels+y*width,(uint8_t*)argb->pixels+y*argb->pitch,width*4);
  SDL_FreeSurface(argb);SDL_FreeSurface(image);return true;
}
bool SS_RenderWorldMap(uint32_t *p,bool dark){return load_art(dark?"world-dark":"world-light",p,512,512);}
bool SS_RenderIconSheet(uint32_t *p){return load_art("icons",p,160,128);}
bool SS_RenderGlyphSheet(uint32_t *p){return load_art("glyphs",p,96,32);}
bool SS_RenderLetterSheet(uint32_t *p){memset(p,0,128*16*4);return true;}
bool SS_RenderLinkFace(uint32_t *p,int chunk){assert(chunk==2);return load_art("face",p,16,16);}
int SS_GetDungeonLayout(int palace,uint8_t *p,int n){
 if(!art_directory){memset(p,15,n);return 8|(1<<8);}
 char path[1024];snprintf(path,sizeof(path),"%s/layout-%d.bin",art_directory,palace);
 FILE *f=fopen(path,"rb");assert(f);int result;assert(fread(&result,4,1,f)==1);assert(fread(p,1,n,f)==(size_t)n);fclose(f);return result;
}
bool SS_RenderDungeonFloor(int palace,int floor,uint32_t *p){char name[64];snprintf(name,sizeof(name),"floor-%d-%d",palace,floor);return load_art(name,p,80,80);}
bool SS_RenderMapIcons(int palace,uint32_t *p){char name[64];snprintf(name,sizeof(name),"map-icons-%d",palace);return load_art(name,p,32,8);}

static void bounds(RectFS r,RectFS container){
 assert(r.w>0&&r.h>0);assert(r.x>=container.x&&r.y>=container.y);
 assert(r.x+r.w<=container.x+container.w&&r.y+r.h<=container.y+container.h);
}
static void begin(const char *name){scene=name;text_count=0;SDL_RenderSetClipRect(ss_r,NULL);set_color(COL(8,8,8));SDL_RenderClear(ss_r);draw_tiled(tex_bg_menu,kSSTexMenu_W,kSSTexMenu_H,(RectFS){0,0,W,H},COL_BG_MENU);}
static void save(SDL_Surface *surface,const char *out,const char *name,bool footer){
 if(footer)draw_tab_bar(42);
 SDL_RenderFlush(ss_r);
 char path[1024];snprintf(path,sizeof(path),"%s/%s-%s-%s.bmp",out,UI_LanguageCode(scene_lang),name,new_model?"new":"old");
 assert(SDL_SaveBMP(surface,path)==0);captures++;
}
static void verify_tabs(void){
 RectFS strip={0,198,320,42};RectFS *tabs[]={&tab_gear_r,&tab_map_r,&tab_items_r,&tab_settings_r};
 int ids[]={kUi_Gear,kUi_Map,kUi_Items};
 for(int i=0;i<4;i++){bounds(*tabs[i],strip);if(i)assert(tabs[i]->x>=tabs[i-1]->x+tabs[i-1]->w);if(i<3)assert(text_width(UI_Text(ids[i]),2)+12<=tabs[i]->w);}
 // Every border, gap and strip edge routes to its closest visible button.
 for(int y=198;y<240;y++)for(int x=0;x<320;x++)assert(tab_at_position(x,y)>=0);
}
static void fill_inventory(void){
 memset(sram,0,sizeof(sram));for(int i=0;i<20;i++)sram[0x40+i]=kSS_ItemMaxLevel[i];
 sram[0x54]=2;sram[0x55]=sram[0x56]=sram[0x57]=1;sram[0x59]=4;sram[0x5a]=3;sram[0x5b]=2;for(int i=0;i<4;i++)sram[0x5c+i]=i+4;
 sram[0x62]=0xe7;sram[0x63]=3;sram[0x64]=255;sram[0x65]=255;sram[0x6b]=3;
 sram[0x6c]=160;sram[0x6d]=140;sram[0x6e]=96;sram[0x6f]=5;sram[0x74]=5;sram[0x77]=30;sram[0x7a]=0x5f;sram[0x7b]=1;
}
static void verify_dungeon_backgrounds(void) {
 uint16_t *expected=malloc(320*240*2),*actual=malloc(320*240*2);assert(expected&&actual);
 for(int step=0;step<28;step++) {
  int palace=step<14?step:27-step;unsigned pixel=0;
  for(unsigned i=kDungeonBgOffsets[palace];i<kDungeonBgOffsets[palace+1];i+=2) {
   uint32_t c=kDungeonBgPalette[palace][kDungeonBgRuns[i+1]];
   uint16_t v=((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31);
   for(int n=0;n<kDungeonBgRuns[i];n++){assert(pixel<320*240);expected[pixel++]=v;}
  }
  assert(pixel==320*240);assert(draw_dungeon_background(palace));
  assert(SDL_RenderReadPixels(ss_r,NULL,SDL_PIXELFORMAT_RGB565,actual,640)==0);
  assert(memcmp(expected,actual,320*240*2)==0);
  SDL_Rect clip={222,5,93,191};SDL_RenderSetClipRect(ss_r,&clip);
  fill_rect(222,5,93,191,COL(255,0,255));assert(draw_dungeon_background(palace));
  SDL_RenderSetClipRect(ss_r,NULL);
  assert(SDL_RenderReadPixels(ss_r,NULL,SDL_PIXELFORMAT_RGB565,actual,640)==0);
  assert(memcmp(expected,actual,320*240*2)==0);
 }
 free(expected);free(actual);
}


// SDL_RenderCopy refuses a texture built by another renderer, and every renderer
// teardown here goes through destroy_textures: the wide atlases are freed with
// the rest, so the next draw rebuilds them. This reproduces one rebuild and
// checks that Chinese still reaches the frame.
static void verify_wide_atlas_follows_renderer(SDL_Surface *surface) {
  // draw_wide_glyph is the atlas path and does not reach observe_text, so this
  // check adds no text boxes to the collision budget.
  scene = "text-atlas"; scene_lang = kUiLanguage_Chinese;
  int a = UiCjk_Index(0x8FD4), b = UiCjk_Index(0x56DE);
  assert(a >= 0 && b >= 0);
  SDL_SetRenderDrawColor(ss_r, 0, 0, 0, 255);
  SDL_RenderClear(ss_r);
  draw_wide_glyph(UI_FONT_CHINESE_CODE + a, 3, 20, 40, 0xffffffff);
  draw_wide_glyph(UI_FONT_CHINESE_CODE + b, 3, 40, 40, 0xffffffff);
  SDL_RenderFlush(ss_r);

  // Exactly what rebuild_renderer does, in the same order.
  destroy_textures();
  SDL_DestroyRenderer(ss_r);
  ss_r = SDL_CreateSoftwareRenderer(surface);
  assert(ss_r);

  SDL_SetRenderDrawColor(ss_r, 0, 0, 0, 255);
  SDL_RenderClear(ss_r);
  draw_wide_glyph(UI_FONT_CHINESE_CODE + a, 3, 20, 40, 0xffffffff);
  draw_wide_glyph(UI_FONT_CHINESE_CODE + b, 3, 40, 40, 0xffffffff);
  SDL_RenderFlush(ss_r);

  static uint32_t pixels[320 * 240];
  assert(SDL_RenderReadPixels(ss_r, NULL, SDL_PIXELFORMAT_ARGB8888, pixels, 320 * 4) == 0);
  unsigned ink = 0;
  for (int i = 0; i < 320 * 240; i++) if (pixels[i] & 0xffffff) ink++;
  assert(ink > 0);
}

int main(int argc,char **argv){
 assert(argc==2||argc==3);Qol_LoadPreferences("qol.ini");if(argc==3)art_directory=argv[2];assert(SDL_Init(0)==0);
 W=320;H=240;u=.5f;RectFS panel={5,5,310,191};
 for(int model=0;model<2;model++){
  new_model=model;ss_is_new_3ds=model;fixture_turbo=model?kTurboControlCStick:kTurboControlX;
  SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,320,240,model?32:16,model?SDL_PIXELFORMAT_ARGB8888:SDL_PIXELFORMAT_RGB565);assert(surface);
  ss_r=SDL_CreateSoftwareRenderer(surface);assert(ss_r);
  qol_selected=0;int before_sounds=menu_move_sounds;
  qol_select(1);assert(menu_move_sounds==before_sounds+1);
  qol_select(1);assert(menu_move_sounds==before_sounds+1);
  qol_select(0);assert(menu_move_sounds==before_sounds+2);
  for(int selected=0;selected<2;selected++) {
   begin("button-style");SDL_SetRenderDrawColor(ss_r,0,0,0,255);SDL_RenderClear(ss_r);
   RectFS row={30,28,260,32};draw_settings_row(&row,selected);
   draw_text("CONTINUE",38,UiFontCenteredY("CONTINUE",2,28,32),2);
   draw_text(">",276,UiFontCenteredY(">",1,28,32),1);
   char name[32];snprintf(name,sizeof(name),"button-face%d",selected);save(surface,argv[1],name,false);
  }
  if(!ZELDA3_ENABLE_DEVELOPER_TESTS) {
   begin("release-developer");leave_settings_submenu();developer_mode=true;draw_developer_panel(panel);
   assert(!developer_tests_r.w&&!developer_tests_r.h);
   scenario_open();assert(!scenario_mode);
   save(surface,argv[1],"release-developer",false);
  }
  verify_dungeon_backgrounds();
  if(art_directory){assert(try_load_art());art_ready=true;}
  for(int lang=0;lang<kUiLanguageCount;lang++){
   UI_SetLanguage(lang);scene_lang=lang;fill_inventory();fixture_module=9;fixture_indoors=false;tab=TAB_SETTINGS;
   for(int screen=0;screen<7;screen++){
    const char *name=(const char*[]){"settings","game","display","developer","information","load-confirm","language-confirm"}[screen];
    begin(name);leave_settings_submenu();
    gameplay_mode=screen==1;screen_mode=screen==2;developer_mode=screen==3||screen==4;developer_overlay_mode=screen==4;load_confirm_mode=screen==5;language_confirm_mode=screen==6;
    draw_settings(panel);
    // Every settings list shares the header inset, first row, height and gap.
    RectFS expected=settings_option_row(panel,0,6);
    RectFS *rows=screen==0?settings_row_r:screen==1?gameplay_row_r:screen==2?screen_row_r:developer_row_r;
    int n=screen==0||screen==1?5:screen==2?4:screen==3?6:0;
    for(int i=0;i<n;i++) {
      bounds(rows[i],panel);assert(rows[i].x==panel.x+8&&rows[i].w==panel.w-16);
      assert(rows[i].h==expected.h&&rows[i].y==expected.y+i*(expected.h+3));
    }
    if(screen==0)bounds(language_chip_r,panel);
    if(screen==1)bounds(gameplay_back_r,panel);
    if(screen==2)bounds(screen_back_r,panel);
    if(screen==3){bounds(developer_back_r,panel);assert(abs((int)((expected.y-(panel.y+34))-(panel.y+panel.h-(rows[5].y+rows[5].h))))<=1);}
    if(screen==5){bounds(load_cancel_r,panel);bounds(load_confirm_r,panel);}
    if(screen==6){bounds(language_no_r,panel);bounds(language_yes_r,panel);}
    save(surface,argv[1],name,true);verify_tabs();
   }
   for(int option=0;option<kQolCount;option++) {
    char name[64];snprintf(name,sizeof(name),"qol-%02d",option);
    begin(name);leave_settings_submenu();qol_mode=true;qol_select(option);draw_settings(panel);
    RectFS full={5,5,310,230};RectFS *controls[]={&qol_back,&qol_up,&qol_down,&qol_toggle};for(int j=0;j<4;j++)bounds(*controls[j],full);
    save(surface,argv[1],name,false);
    if(Qol_NeedsRestart(option))for(int enabled=0;enabled<2;enabled++) {
      assert(Qol_SetOption(option,enabled));begin("qol-confirm");qol_confirm=true;draw_settings(panel);
      bounds(qol_no,full);bounds(qol_yes,full);
      snprintf(name,sizeof(name),"qol-confirm-%02d-%d",option,enabled);save(surface,argv[1],name,false);
    }
   }
   for (int index=0;ZELDA3_ENABLE_DEVELOPER_TESTS&&index<kTestScenarioCount;index++) {
    char name[64];snprintf(name,sizeof(name),"test-%02d",index+1);
    begin(name);leave_settings_submenu();developer_mode=true;scenario_open();scenario_select(index);
    draw_settings(panel);save(surface,argv[1],name,true);
    begin("test-confirm");scenario_load();draw_settings(panel);
    save(surface,argv[1],"test-confirm",true);
   }
   for(int control=kTurboControlX;control<=kTurboControlCStick;control++){
    begin("turbo-options");leave_settings_submenu();gameplay_mode=true;fixture_turbo=control;draw_settings(panel);
   }
   fixture_turbo=model?kTurboControlCStick:kTurboControlX;
   for(int result=-2;result<=kZeldaDumpStateIoError;result++) {
    begin("developer-feedback");leave_settings_submenu();developer_mode=true;
    load_result=result;load_flash_until=SDL_GetTicks()+10000;dump_flash_until=SDL_GetTicks()+10000;
    draw_settings(panel);
   }
   load_result=-1;load_flash_until=dump_flash_until=0;
   for(int state=UPDATE_IDLE;state<=UPDATE_ERROR;state++)for(int variant=0;variant<3;variant++){
    begin("update");leave_settings_submenu();update_mode=true;
    update_show_notes=variant==1;update_confirm=variant==2;
    update_fixture=(UpdateStatus){.state=state,.prerelease=variant==1,.progress=63};
    snprintf(update_fixture.version,sizeof(update_fixture.version),"v3.3-local");
    if(state==UPDATE_ERROR)strcpy(update_fixture.message,"CONNECTION FAILED - RETRY");
    draw_settings(panel);bounds(update_channel_r,panel);bounds(update_release_r,panel);bounds(update_action_r,panel);
    if(update_prev_r.w){bounds(update_prev_r,panel);bounds(update_next_r,panel);assert(update_next_r.y+update_next_r.h<update_action_r.y);}
    char name[64];snprintf(name,sizeof(name),"update-%d-%d",state,variant);save(surface,argv[1],name,true);
   }
   if(art_directory){
    if(lang==0) {
     const int capacities[]={3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
     for(unsigned c=0;c<18;c++)for(int keys=0;keys<2;keys++)for(int half=0;half<2;half++)for(int health=0;health<4;health++) {
      fill_inventory();sram[0x6c]=capacities[c]*8;sram[0x6d]=health==0?capacities[c]*8:health==1?capacities[c]*8-4:health==2?4:0;sram[0x7b]=half;
      begin("sidebar");draw_tiled(tex_bg_menu,kSSTexMenu_W,kSSTexMenu_H,(RectFS){0,0,W,H},COL_BG_MENU);
      draw_sidebar(222,5,93,191,keys);
      bounds(y_ring_r,(RectFS){222,5,93,191});
      assert(y_ring_r.w==56&&y_ring_r.h==56);
      assert(y_ring_r.y+y_ring_r.h<166);
      char name[80];snprintf(name,sizeof(name),"sidebar-%d-hearts-keys%d-half%d-health%d",capacities[c],keys,half,health);
      save(surface,argv[1],name,true);
     }
     fill_inventory();
     assert(Qol_SetOption(kQolWallet9999,true));Qol_LatchForBoot();
     sram[0x62]=0x0f;sram[0x63]=0x27;
     begin("wallet-9999");draw_sidebar(222,5,93,191,false);save(surface,argv[1],"wallet-9999",true);
     assert(Qol_SetOption(kQolWallet9999,false));Qol_LatchForBoot();fill_inventory();
    }
    begin("gear");tab=TAB_GEAR;draw_gear(panel);save(surface,argv[1],"gear",true);
    tab=TAB_ITEMS;RectFS narrow={5,5,208,191};
    for(int expanded=0;expanded<2;expanded++) {
      assert(Qol_SetOption(kQolInventory,expanded));Qol_LatchForBoot();
      begin("items");draw_items(narrow);draw_sidebar(222,5,93,191,false);
      assert(grid_cell-(expanded?29:34)==3);
      save(surface,argv[1],expanded?"items-24":"items-20",true);
    }
    for(int dungeon=0;dungeon<14;dungeon++){
     fixture_indoors=true;fixture_dungeon=dungeon;
     begin("dungeon");tab=TAB_MAP;draw_dungeon_background(dungeon);draw_dungeon(narrow,200,200,0x51,dungeon);draw_sidebar(222,5,93,191,true);
     for(int i=0;i<plaque_count;i++)bounds(plaque_r[i],narrow);
     char name[64];snprintf(name,sizeof(name),"dungeon-%d",dungeon);save(surface,argv[1],name,true);
    }
    fixture_indoors=false;
    for(int overview=0;overview<2;overview++)for(int dark=0;dark<2;dark++){
     begin("map");tab=TAB_MAP;whole_map=overview;draw_overworld(narrow,2048,2048,dark?0x40:0);draw_sidebar(222,5,93,191,false);
     char name[64];snprintf(name,sizeof(name),"map-%d-%d",dark,overview);save(surface,argv[1],name,true);
    }
   }
   for(unsigned page=0;page<2;page++) {
    uint32_t *guarded=calloc(512*256+2,4);assert(guarded);guarded[0]=guarded[512*256+1]=0x12345678;
    char lines[28][43]={"Local interface preview.","All changes are kept on this computer.","", "- Crisp native pixel fonts.","- Consistent menu spacing.","- Seven interface languages."};
    for(int i=6;i<28;i++)memset(lines[i],'X',42);
    UpdateView_Draw(guarded+1,NULL,NULL,"LOCAL UI",lines,28,page);
    assert(guarded[0]==0x12345678&&guarded[512*256+1]==0x12345678);
    SDL_Surface *top=SDL_CreateRGBSurfaceWithFormatFrom(guarded+1,400,240,32,512*4,SDL_PIXELFORMAT_ARGB8888);assert(top);
    char path[1024];snprintf(path,sizeof(path),"%s/%s-update-notes-%u-%s.bmp",argv[1],UI_LanguageCode(lang),page,model?"new":"old");assert(SDL_SaveBMP(top,path)==0);captures++;
    SDL_FreeSurface(top);free(guarded);
   }
   ensure_cinema_art();begin("cinema");draw_cinema();save(surface,argv[1],"cinema",false);
   // The explicit pre-intro card must be pixel-identical to the normal
   // bottom worker card, with the same 512-wide pitch on both models.
   static uint8_t explicit_card[512*256*4],worker_card[320*240*4];
   StartupCardPixels(explicit_card,model!=0);int bpp=model?4:2;
   assert(SDL_RenderReadPixels(ss_r,NULL,model?SDL_PIXELFORMAT_ARGB8888:SDL_PIXELFORMAT_RGB565,worker_card,320*bpp)==0);
   for(int y=0;y<240;y++)assert(!memcmp(worker_card+y*320*bpp,explicit_card+y*512*bpp,320*bpp));
   begin("title-settings");leave_settings_submenu();tab=TAB_SETTINGS;draw_cinema_settings_overlay();save(surface,argv[1],"title-settings",false);
   // The Settings house returns to the full home; the Game language row
   // retains its separate language selector. Modal input cannot fall through.
   art_ready=true;fixture_module=9;tab=TAB_SETTINGS;
   begin("startup-open");leave_settings_submenu();draw_settings(panel);
   int before=selector_requests,home_before=startup_requests;
   apply_tap(language_chip_r.x+5,language_chip_r.y+5);
   assert(startup_confirm_mode&&!language_confirm_mode&&UI_GetLanguage()==lang);
   begin("startup-modal");draw_settings(panel);save(surface,argv[1],"startup-confirm",false);
   apply_tap(10,220);assert(startup_confirm_mode&&tab==TAB_SETTINGS);
   apply_tap(language_no_r.x+5,language_no_r.y+5);
   assert(!startup_confirm_mode&&tab==TAB_SETTINGS&&startup_requests==home_before&&selector_requests==before);
   begin("startup-again");draw_settings(panel);apply_tap(language_chip_r.x+5,language_chip_r.y+5);
   begin("startup-yes");draw_settings(panel);apply_tap(language_yes_r.x+5,language_yes_r.y+5);
   assert(!startup_confirm_mode&&startup_requests==home_before+1&&selector_requests==before&&UI_GetLanguage()==lang);
   begin("game-randomizer");gameplay_mode=true;draw_settings(panel);
   apply_tap(gameplay_row_r[3].x+5,gameplay_row_r[3].y+5);
   assert(randomizer_mode&&!language_confirm_mode&&selector_requests==before);
   leave_settings_submenu();
   // Root Settings has the same back arrow during gameplay and cinema.
   begin("settings-root-back");tab=TAB_SETTINGS;draw_settings(panel);bounds(cinema_back_r,panel);
   apply_tap(cinema_back_r.x+5,cinema_back_r.y+5);assert(tab==TAB_MAP);
   tab=TAB_SETTINGS;
   // Every submenu's visible Back button returns to the settings home screen.
   begin("back");gameplay_mode=true;draw_settings(panel);apply_tap(gameplay_back_r.x+5,gameplay_back_r.y+5);assert(!gameplay_mode);
   begin("back");screen_mode=true;draw_settings(panel);apply_tap(screen_back_r.x+5,screen_back_r.y+5);assert(!screen_mode);
   begin("back");developer_mode=true;draw_settings(panel);apply_tap(developer_back_r.x+5,developer_back_r.y+5);assert(!developer_mode);
  }
  verify_wide_atlas_follows_renderer(surface);
  destroy_textures();SDL_DestroyRenderer(ss_r);ss_r=NULL;SDL_FreeSurface(surface);
 }
 SDL_Quit();printf("PASS %u production UI captures, %u text checks, 7 languages, Old RGB565/New ARGB8888, no text collisions, bounded controls, language confirmation/cancel/selector and Back navigation%s\n",captures,text_checks,art_directory?", private USA art":"");return 0;
}
