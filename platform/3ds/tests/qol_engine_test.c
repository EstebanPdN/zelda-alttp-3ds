
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#define main ZeldaUnusedUiMain
#include "src/main.c"
#undef main
#include "src/test_scenarios.h"
#include "snes/ppu_ui.h"
#include "src/hud.h"
#include "src/misc.h"
#include "src/dump_state.h"
uint32_t Platform3DS_GetActiveProfileId(void){return 1234;}
#include "src/load_gfx.h"


#include "src/quality_of_life.h"
#include "src/features.h"
#include "src/overworld.h"
#include "src/dungeon.h"
#include "src/player.h"
#include "src/sprite.h"
#include "src/messaging.h"
#include "src/tile_detect.h"
#include "src/rando/rando.h"
#include "src/ancilla.h"
int SS_GetInventorySize(void);
int SS_GetInventorySlot(int slot);
static void apply(void){
 uint32 next=Qol_Features1(enhanced_features1);
 if((next^enhanced_features1)&kFeatures1_QolInventory)Hud_QolTranslateInventory((next&kFeatures1_QolInventory)!=0);
 enhanced_features0=Qol_Features0(enhanced_features0);enhanced_features1=next;
 g_wanted_zelda_features=enhanced_features0;g_wanted_zelda_features1=enhanced_features1;
}
static void set(int option,bool on){assert(Qol_SetOption(option,on));if(Qol_NeedsRestart(option))Qol_LatchForBoot();apply();}
static void shot(const char*root,const char*name){
 static uint8 image[400*240*4];char path[512];g_zenv.ppu->extraLeftRight=72;
 ZeldaDrawPpuFrame(image,400*4,kPpuRenderFlags_NewRenderer|kPpuRenderFlags_Height240);
 snprintf(path,sizeof(path),"%s/shots/%s.raw",root,name);FILE*f=fopen(path,"wb");assert(f);assert(fwrite(image,4,400*240,f)==400*240);fclose(f);
}
int main(int argc,char**argv){
 assert(argc==5&&chdir(argv[1])==0);ParseConfigFile(NULL);LoadAssets();LoadLinkGraphics();ZeldaInitialize();ZeldaSetLanguage(NULL);
 remove("qol.ini");Qol_LoadPreferences("qol.ini");assert(Qol_Selected(kQolFastSwitch)&&Qol_Selected(kQolMuteBeep));
 assert(!Qol_Selected(kQolBombs99));assert(Qol_Features0(0)&kFeatures0_SkipIntroOnKeypress);
 assert(Qol_SetOption(kQolBombs99,true));assert(!(Qol_Features1(0)&kFeatures1_QolBombs99));
 Qol_LoadPreferences("qol.ini");assert(Qol_Selected(kQolBombs99)&&(Qol_Features1(0)&kFeatures1_QolBombs99));
 assert(rename("qol.ini","qol.ini.replace.bak")==0);Qol_LoadPreferences("qol.ini");assert(Qol_Selected(kQolBombs99));
 FILE*f=fopen("qol.ini","wb");fputs("Z3QOL 1 4294967295\n",f);fclose(f);Qol_LoadPreferences("qol.ini");assert(!Qol_Selected(kQolBombs99));
 assert(!Qol_SetOption(-1,true)&&!Qol_SetOption(kQolCount,true));
 Qol_LoadPreferences("/missing-qol-parent/qol.ini");bool old=Qol_Selected(kQolPegasus);assert(!Qol_SetOption(kQolPegasus,!old)&&Qol_Selected(kQolPegasus)==old);
 Qol_LoadPreferences("qol.ini");apply();assert(ZeldaLoadTestScenario(32));apply();
 uint8 bottles[4];memcpy(bottles,link_bottle_info,4);
 for(int level=0;level<8;level++) {
  link_bomb_upgrades=link_arrow_upgrades=level;
  set(kQolBombs99,true);set(kQolArrows99,true);set(kQolWallet9999,true);
  assert(Hud_MaxBombs()==99&&Hud_MaxArrows()==99&&Hud_Capacity(0x8ddc31,0xdb40,level)==0x99);
  link_item_bombs=link_num_arrows=99;link_rupees_actual=link_rupees_goal=9999;Hud_QolNormalizeInventory();
  assert(link_item_bombs==99&&link_num_arrows==99&&link_rupees_actual==9999);
  set(kQolBombs99,false);set(kQolArrows99,false);set(kQolWallet9999,false);Hud_QolNormalizeInventory();
  assert(link_item_bombs==Hud_MaxBombs()&&link_num_arrows==Hud_MaxArrows()&&link_rupees_actual==999&&link_rupees_goal==999);
  assert(!memcmp(bottles,link_bottle_info,4));
 }
 puts("OK capacity: 99/99/9999, independent toggles, all original upgrade limits, no bottle changes");
 set(kQolShovel,true);link_item_flute=3;assert(Hud_QolHasShovel());
 hud_cur_item=13;current_item_y=8;main_module_index=14;saved_module_for_menu=7;overworld_map_state=4;filtered_joypad_H=0;filtered_joypad_L=kJoypadL_A;joypad1H_last=0;
 Hud_NormalMenu();assert(current_item_y==21&&link_item_flute==3);Hud_UpdateEquippedItem();assert(current_item_y==21);
 Hud_NormalMenu();assert(current_item_y==8&&link_item_flute==3);
 hud_cur_item=16;link_item_bottle_index=4;set(kQolInventory,true);assert(hud_cur_item==24&&SS_GetInventorySize()==24);
 for(int i=21;i<=24;i++){int packed=SS_GetInventorySlot(i);assert(packed>=0&&(packed>>8)==15&&(packed&255)==bottles[i-21]);}
 assert((SS_GetInventorySlot(16)&255)==1);hud_cur_item=16;Hud_UpdateEquippedItem();assert(current_item_y==21);
 set(kQolInventory,false);assert(hud_cur_item==13&&link_item_flute==3&&SS_GetInventorySize()==20);
 set(kQolShovel,false);assert(!Hud_QolHasShovel());
 g_rando_slot_active=true;g_rando_flute_shovel_owned=kRandoFluteShovel_Flute|kRandoFluteShovel_FluteActive;set(kQolShovel,true);assert(!Hud_QolHasShovel());
 g_rando_flute_shovel_owned|=kRandoFluteShovel_Shovel;assert(Hud_QolHasShovel());
 set(kQolInventory,true);enhanced_features1|=kFeatures1_RandomizerActive;
 link_item_flute=1;Hud_DrawYButtonItems();uint16 flute_tile=uvram_screen.row[0].col[2+13*32];
 link_item_flute=3;Hud_DrawYButtonItems();assert(uvram_screen.row[0].col[2+13*32]==flute_tile);
 g_rando_slot_active=false;enhanced_features1&=~kFeatures1_RandomizerActive;set(kQolInventory,false);
 puts("OK inventory: 24 slots, four bottle contents, permanent shovel/flute, old-grid restoration, no Randomizer grants");
 main_module_index=7;submodule_index=0;player_is_indoors=1;tilemap_location_calc_mask=0x1ff;dung_hdr_tag[0]=0;dung_index_of_torches_start=2;
 memset(dung_bg2_attr_table,0,0x2000);dung_object_tilemap_pos[0]=0x1080;dung_replacement_tile_state[0]=3;
 set(kQolRepeatBlocks,true);vram_upload_offset=0;PushBlock_CheckForPit(0);dung_replacement_tile_state[0]++;
 assert(dung_replacement_tile_state[0]==0xfffe&&dung_bg2_attr_table[0x840]==0x70);
 link_last_direction_moved_towards=3;push_block_direction=6;index_of_changable_dungeon_objs[0]=1;index_of_changable_dungeon_objs[1]=0;
 assert(!InitializePushBlock(0,0)&&dung_replacement_tile_state[0]==1);
 for(int j=0;j<2;j++) {
  for(int k=0;k<40&&dung_replacement_tile_state[0]!=0xfffe;k++){dung_misc_objs_index=0;vram_upload_offset=0;Dungeon_PushBlock_Handler();}
  assert(dung_replacement_tile_state[0]==0xfffe);
  index_of_changable_dungeon_objs[0]=1;assert(!InitializePushBlock(0,0));
 }
 dung_replacement_tile_state[0]=0xfffe;set(kQolRepeatBlocks,false);assert(InitializePushBlock(0,0));
 puts("OK blocks: repeated native movement, collision and disabling");
 assert(ZeldaLoadTestScenario(38));apply();link_hearts_filler=0;sprite_type[0]=0xe3;
 set(kQolFairyHearts,true);Sprite_HandleAbsorptionByPlayer(0);assert(link_hearts_filler==80);
 GameOver_InitializeRevivalFairy();assert(link_hearts_filler==80);
 set(kQolFairyHearts,false);GameOver_InitializeRevivalFairy();assert(link_hearts_filler==56);
 puts("OK fairies: ten hearts in pickups and automatic revival, original seven when off");
 uint16 original[1024],patched[1024],again[1024];unsigned changed=0;
 for(int s=0;s<128;s++) {
  set(kQolKakariko,false);assert(Overworld_DecodeScreenMap16(s,original));set(kQolKakariko,true);assert(Overworld_DecodeScreenMap16(s,patched));
  for(int i=0;i<1024;i++)if(original[i]!=patched[i]){assert(s==0x22||s==0x23||s==0x62||s==0x63);changed++;}
  set(kQolKakariko,false);assert(Overworld_DecodeScreenMap16(s,again)&&!memcmp(original,again,sizeof(original)));
 }
 assert(changed>200&&changed<400);printf("OK shortcut: %u native tile changes in four screens only, reversible\n",changed);
 assert(ZeldaLoadTestScenario(39));apply();hdr_dungeon_dark_with_lantern=1;dung_want_lights_out=1;
 set(kQolBrightRooms,false);shot(argv[2],"dark-off");uint8 before[64];memcpy(before,g_ram+0x400,64);
 set(kQolBrightRooms,true);shot(argv[2],"dark-on");assert(!memcmp(before,g_ram+0x400,64));
 for(int option=0;option<kQolCount;option++)set(option,true);
 for(int scenario=0;scenario<kTestScenarioCount;scenario++) {
  assert(ZeldaLoadTestScenario(scenario));apply();
  for(int frame=0;frame<60;frame++)ZeldaRunFrame(frame<24?1<<7:0);
 }
 puts("OK all 40 code-created scenarios with every QoL feature enabled and playable inputs");
 // Record a grid transition, then replay it with different current preferences.
 assert(ZeldaLoadTestScenario(32));apply();
 set(kQolInventory,false);hud_cur_item=16;link_item_bottle_index=2;PatchCommand('k');
 assert(Qol_SetOption(kQolInventory,true));Qol_LatchForBoot();
 for(int frame=0;frame<40;frame++)ZeldaRunFrame(frame<12?1<<7:0);
 static uint8 recorded[0x20000];memcpy(recorded,g_ram,sizeof(recorded));
 SaveLoadSlot(kSaveLoad_Save,17);
 assert(Qol_SetOption(kQolInventory,false));Qol_LatchForBoot();
 SaveLoadSlot(kSaveLoad_Replay,17);
 for(int frame=0;frame<40;frame++)assert(ZeldaRunFrame(0));
 assert(!memcmp(recorded,g_ram,sizeof(recorded)));
 puts("OK deterministic replay: expanded inventory transition ignores later preferences");
 ZeldaShutdownSession();return 0;
}
