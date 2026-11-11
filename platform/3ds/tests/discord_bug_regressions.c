#include <assert.h>
#include <stdio.h>
#define main ZeldaUnusedUiMain
#include "src/main.c"
#undef main
#include "src/sprite_main.h"
#include "src/rando/item_ids.h"
#include "src/rando/location_ids.h"
@RTL@
uint32_t Platform3DS_GetActiveProfileId(void) { return 0x36cbffd5; }
static uint32 pixels[400*240];
static void Scene(int n) {
  assert(ZeldaLoadTestScenario(n));
  for (int i=0;i<5;i++) ZeldaRunFrame(0);
  enhanced_features0 |= kFeatures0_ExtendScreen64 | kFeatures0_WidescreenVisualFixes | kFeatures0_MiscBugFixes;
}
static void Activation(void) {
  Scene(33);
  for(int shift=-72;shift<=72;shift+=72) {
    Sprite_DisableAll();
    memset(sprite_where_in_overworld,0,4096);memset(overworld_sprite_was_loaded,0,512);
    BG2HOFS_copy2=256;BG2VOFS_copy2=256;sprcoll_x_base=sprcoll_y_base=0;
    sprcoll_x_size=sprcoll_y_size=1024;g_wide_picture_shift=shift;
    // Barrier/vendor spawn points near opposite edges of the new camera.
    const int xs[]={192,544};const int types[]={0x40,0x75};
    for(int i=0;i<2;i++) {int x=xs[i],y=320;int block=((y>>8)*4+(x>>8))*256+(y&240)+((x>>4)&15);sprite_where_in_overworld[block]=types[i]+1;}
    Sprite_ActivateAllProxima();
    for(int i=0;i<2;i++) {bool found=false;for(int k=0;k<16;k++)found|=sprite_state[k]&&sprite_type[k]==types[i]&&Sprite_GetX(k)==xs[i];assert(found);}
    assert(BG2HOFS_copy2==256&&BG2VOFS_copy2==256);
  }
  puts("PASS initial actor activation with stale left, center and right cameras");
}
static void Eyegore(void) {
  Scene(3);Sprite_DisableAll();submodule_index=0;flag_unk1=0;
  for(int delta=16;delta<=272;delta+=256) {
    SpritePrep_ResetProperties(0);sprite_type[0]=0x83;sprite_state[0]=9;
    Sprite_SetX(0,link_x_coord+delta);Sprite_SetY(0,link_y_coord+8);
    sprite_ai_state[0]=sprite_delay_main[0]=sprite_pause[0]=sprite_F[0]=0;
    sprite_defl_bits[0]=0x80;link_disable_sprite_damage=1;
    Eyegore_Main(0);assert(sprite_ai_state[0]==(delta==16));
  }
  puts("PASS nearby Eyegore wakes; 256-pixel alias stays asleep");
}
static void Flood(void) {
  Scene(3);BG2HOFS_copy2=BG2VOFS_copy2=0;water_hdma_var0=128;
  water_hdma_var1=100;water_hdma_var2=60;water_hdma_var3=32;spotlight_y_upper=90;
  for(int i=0;i<240;i++)hdma_table_dynamic[i]=0x1357;
  FloodDam_PrepFloodHDMA();for(int i=0;i<240;i++)assert(hdma_table_dynamic[i]!=0x1357);
  for(int i=225;i<240;i++)assert(hdma_table_dynamic[i]==0xff);
  puts("PASS dam window initializes every extended scanline");
}
static void Mirror(void) {
  Scene(32);BG2HOFS_copy2=0x200;frame_counter=0;mirror_vars.ctr=1;darkening_or_lightening_screen=0xff;
  subsubmodule_index=3;HDMAEN_copy=0xc0;for(int i=0;i<240;i++)hdma_table_dynamic[i]=0x200;
  hdma_table_dynamic[200]=0;MirrorWarp_BuildDewavingHDMATable();assert(subsubmodule_index==3&&HDMAEN_copy==0xc0);
  for(int i=0;i<240;i++)hdma_table_dynamic[i]=0x200;
  mirror_vars.ctr=1;MirrorWarp_BuildDewavingHDMATable();assert(subsubmodule_index==4&&HDMAEN_copy==0);
  Scene(32);main_module_index=9;submodule_index=23;subsubmodule_index=0;
  sound_effect_ambient_last=11;sound_effect_ambient=0;Module09_MirrorWarp();assert(sound_effect_ambient==5);
  // Exercise the real HDMA decoder with an absolute scroll sample.
  uint8 table[]={0x81,0x00,0x02,0};SimpleHdma h={0};h.table=table;h.mode=2;h.ppu_addr=(uint8)BG2HOFS;
  g_zenv.ppu->renderObjXOffset=72;SimpleHdma_DoLine(&h,g_zenv.ppu);
  assert(g_zenv.ppu->bgLayer[1].hScroll==0x200-72);
  g_zenv.ppu->renderObjXOffset=0;
  puts("PASS mirror waits for all rows, stops flute ambience and retains HDMA camera");
}
static void Parallax(void) {
  Scene(32);main_module_index=9;HDMAEN_copy=0;dungeon_room_index=0;BG2HOFS_copy2=1000;BG2VOFS_copy2=1000;
  WideFrame f={0};f.context=9;f.fixed_camera=true;f.rows=true;f.view.offset_x=72;f.view.delta_y=16;
  const uint8 overlays[]={0x95,0xb5,0x97,0x9c};
  const int dx[]={18,36,0,72},dy[]={8,4,0,16};
  for(int i=0;i<4;i++) {overlay_index=overlays[i];g_zenv.ppu->bgLayer[0].hScroll=g_zenv.ppu->bgLayer[0].vScroll=500;
    WideRenderState s=BeginWideRender(&f);assert(g_zenv.ppu->bgLayer[0].hScroll==500-dx[i]);assert(g_zenv.ppu->bgLayer[0].vScroll==500-dy[i]);EndWideRender(&s);assert(g_zenv.ppu->bgLayer[0].hScroll==500&&g_zenv.ppu->bgLayer[0].vScroll==500);}
  puts("PASS half/quarter-speed, wind-only and full-speed overlay cameras");
}
int main(int argc,char **argv) {
  assert(argc==2);assert(!chdir(argv[1]));setvbuf(stdout,NULL,_IOLBF,0);
  ParseConfigFile(NULL);LoadAssets();LoadLinkGraphics();ZeldaInitialize();ZeldaSetLanguage(NULL);ZeldaReadSram();
  g_config.features0|=kFeatures0_ExtendScreen64|kFeatures0_WidescreenVisualFixes|kFeatures0_MiscBugFixes;
  g_wanted_zelda_features=g_config.features0;g_config.extended_aspect_ratio=72;g_config.extend_y=true;
  g_zenv.ppu->extraLeftRight=72;g_snes_width=400;g_snes_height=240;
  g_ppu_render_flags=kPpuRenderFlags_NewRenderer|kPpuRenderFlags_Height240;ZeldaSetWidescreenEdgeMode(1);
  Activation();Eyegore();Flood();Mirror();Parallax();ZeldaShutdownSession();return 0;
}
