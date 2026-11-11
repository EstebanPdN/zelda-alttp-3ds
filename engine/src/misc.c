#include "misc.h"
#include "variables.h"
#include "mods.h"
#include "features.h"
#include "rando/rando.h"  // Phase B Slice 1 §38 — Rando_BumpReachabilityCounter
#include "rando/location_ids.h"  // LOC_Agahnim (mark Aga-1 defeat as the DefeatAgahnim event)

// Forward-declared here to avoid pulling rando_placement.h into misc.c.
// NULL `settings` is supported (used by the Module05_LoadFile call below).
struct RandoSettings;
bool Rando_TryGrantStartingInventory(const struct RandoSettings *settings);
#include "hud.h"
#include "test_scenarios.h"
#include "dungeon.h"
#include "overworld.h"
#include "load_gfx.h"
#include "sprite.h"
#include "poly.h"
#include "ancilla.h"
#include "select_file.h"
#include "tile_detect.h"
#include "player.h"
#include "player_oam.h"
#include "messaging.h"
#include "ending.h"
#include "attract.h"
#include "zelda_rtl.h"
#include "snes/snes_regs.h"
#include "assets.h"

static void KillAgahnim_LoadMusic();
static void KillAghanim_Init();
static void KillAghanim_Func2();
static void KillAghanim_Func3();
static void KillAghanim_Func4();
static void KillAghanim_Func5();
static void KillAghanim_Func6();
static void KillAghanim_Func7();
static void KillAghanim_Func8();
static void KillAghanim_Func12();
static uint8 PlaySfx_SetPan(uint8 a);

static bool CutsceneFastForwardEnabled(void) {
  return (enhanced_features0 & kFeatures0_CutsceneFastForward) &&
         !ZeldaIsEmulatorAttached();
}

static uint8 CutsceneClampTimer8(uint8 value, uint8 cap) {
  return CutsceneFastForwardEnabled() && value > cap ? cap : value;
}

const uint8 kReceiveItem_Tab1[76] = {
  0, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 2, 2, 2,
  2, 2, 2, 0, 2, 0, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 0, 2, 2, 2, 2, 2, 0, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  2, 2, 0, 0, 2, 0, 2, 2, 2, 0, 2, 2,
};
static const int8 kReceiveItem_Tab2[76] = {
  -5, -5, -5, -5, -5, -4, -4, -5, -5, -4, -4, -4, -2, -4, -4, -4,
  -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4,
  -4, -4, -4, -5, -4, -4, -4, -4, -4, -4, -2, -4, -4, -4, -4, -4,
  -4, -4, -4, -4, -2, -2, -2, -4, -4, -4, -4, -4, -4, -4, -4, -4,
  -4, -4, -2, -2, -4, -2, -4, -4, -4, -5, -4, -4,
};
static const uint8 kReceiveItem_Tab3[76] = {
  4, 4, 4, 4, 4, 0, 0, 4, 4, 4, 4, 4, 5, 0, 0, 0,
  0, 0, 0, 4, 0, 4, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 4, 4, 0, 4, 0, 0, 0, 4, 0, 0,
};
const uint8 kReceiveItemGfx[76] = {
  6, 0x18, 0x18, 0x18, 0x2d, 0x20, 0x2e,    9,    9,  0xa,    8,    5, 0x10,  0xb, 0x2c, 0x1b,
  0x1a, 0x1c, 0x14, 0x19,  0xc,    7, 0x1d, 0x2f,    7, 0x15, 0x12,  0xd,  0xd,  0xe, 0x11, 0x17,
  0x28, 0x27,    4,    4,  0xf, 0x16,    3, 0x13,    1, 0x1e, 0x10,    0,    0,    0,    0,    0,
  0, 0x30, 0x22, 0x21, 0x24, 0x24, 0x24, 0x23, 0x23, 0x23, 0x29, 0x2a, 0x2c, 0x2b,    3,    3,
  0x34, 0x35, 0x31, 0x33,    2, 0x32, 0x36, 0x37, 0x2c,    6,  0xc, 0x38,
};
const uint16 kMemoryLocationToGiveItemTo[76] = {
  0xf359, 0xf359, 0xf359, 0xf359,
  0xf35a, 0xf35a, 0xf35a, 0xf345,
  0xf346, 0xf34b, 0xf342, 0xf340,
  0xf341, 0xf344, 0xf35c, 0xf347,
  0xf348, 0xf349, 0xf34a, 0xf34c,
  0xf34c, 0xf350, 0xf35c, 0xf36b,
  0xf351, 0xf352, 0xf353, 0xf354,
  0xf354, 0xf34e, 0xf356, 0xf357,
  0xf37a, 0xf34d, 0xf35b, 0xf35b,
  0xf36f, 0xf364, 0xf36c, 0xf375,
  0xf375, 0xf344, 0xf341, 0xf35c,
  0xf35c, 0xf35c, 0xf36d, 0xf36e,
  0xf36e, 0xf375, 0xf366, 0xf368,
  0xf360, 0xf360, 0xf360, 0xf374,
  0xf374, 0xf374, 0xf340, 0xf340,
  0xf35c, 0xf35c, 0xf36c, 0xf36c,
  0xf360, 0xf360, 0xf372, 0xf376,
  0xf376, 0xf373, 0xf360, 0xf360,
  0xf35c, 0xf359, 0xf34c, 0xf355,
};
static const int8 kValueToGiveItemTo[76] = {
     1,   2,   3,  4,
     1,   2,   3,  1,
     1,   1,   1,  1,
     1,   2,  -1,  1,
     1,   1,   1,  1,
     2,   1,  -1, -1,
     1,   1,   2,  1,
     2,   1,   1,  1,
    -1,   1,  -1,  2,
    -1,  -1,  -1, -1,
    -1,  -1,   2, -1,
    -1,  -1,  -1, -1,
    -1,  -1,  -1, -1,
    -1,  -5, -20, -1,
    -1,  -1,   1,  3,
    -1,  -1,  -1, -1,
  -100, -50,  -1,  1,
    10,  -1,  -1, -1,
    -1,   1,   3,  1,
};
static const uint8 kDungeon_DefaultAttr[384] = {
  1, 1, 1, 0, 2, 1, 2, 0, 1, 1, 2, 2, 2, 2, 2, 2,
  2, 2, 2, 0, 0, 1, 0, 0, 2, 0, 0, 2, 2, 2, 2, 2,
  2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 1, 1, 0, 0,
  2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 1, 1, 0, 0,
  0, 0, 0, 0x2a, 1, 0x20, 1, 1, 4, 1, 1, 0x18, 1, 2, 0x1c, 1,
  0x28, 0x28, 0x2a, 0x2a, 1, 2, 1, 1, 4, 0, 0, 0, 0x28, 1, 0xa, 0,
  1, 1, 0xc, 0xc, 2, 2, 2, 2, 0x28, 0x2a, 0x20, 0x20, 0x20, 2, 8, 0,
  4, 4, 1, 1, 1, 2, 2, 2, 0, 0, 0x20, 0x20, 0, 2, 0, 0,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0x18, 0x10, 0x10, 1, 1, 1,
  1, 1, 4, 4, 4, 4, 4, 4, 1, 2, 2, 0, 0, 0, 0, 0,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x62, 0x62,
  0, 0, 0x24, 0x24, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x62, 0x62,
  0x27, 2, 2, 2, 0x27, 0x27, 1, 0, 0, 0, 0, 0x24, 0, 0, 0, 0,
  0x27, 0x27, 0x27, 0x27, 0x27, 0x10, 2, 1, 0, 0, 0, 0x24, 0, 0, 0, 0,
  0x27, 2, 2, 2, 0x27, 0x27, 0x27, 0x27, 2, 2, 2, 0x24, 0, 0, 0, 0,
  0x27, 0x27, 0x27, 0x27, 0x27, 0x20, 2, 2, 1, 2, 2, 0x23, 2, 0, 0, 0,
  0x27, 0x27, 0x27, 0x27, 0x27, 0x20, 2, 0x27, 2, 0x54, 0, 0, 0x27, 2, 2, 2,
  0x27, 0x27, 0x27, 0x27, 0x27, 0x27, 2, 0x27, 2, 0x54, 0, 0, 0x27, 2, 2, 2,
  0x27, 0x27, 0, 0x27, 0x60, 0x60, 1, 1, 1, 1, 2, 2, 0xd, 0, 0, 0x4b,
  0x67, 0x67, 0x67, 0x67, 0x66, 0x66, 0x66, 0x66, 0, 0, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x27, 0x63, 0x27, 0x55, 0x55, 1, 0x44, 0, 1, 0x20, 2, 2, 0x1c, 0x3a, 0x3b, 0,
  0x27, 0x63, 0x27, 0x53, 0x53, 1, 0x44, 1, 0xd, 0, 0, 0, 9, 9, 9, 9,
};
static PlayerHandlerFunc *const kModule_BossVictory[6] = {
  &BossVictory_Heal,
  &Dungeon_StartVictorySpin,
  &Dungeon_RunVictorySpin,
  &Dungeon_CloseVictorySpin,
  &Dungeon_PrepExitWithSpotlight,
  &Spotlight_ConfigureTableAndControl,
};
static PlayerHandlerFunc *const kModule_KillAgahnim[13] = {
  &KillAgahnim_LoadMusic,
  &KillAghanim_Init,
  &KillAghanim_Func2,
  &KillAghanim_Func3,
  &KillAghanim_Func4,
  &KillAghanim_Func5,
  &KillAghanim_Func6,
  &KillAghanim_Func7,
  &KillAghanim_Func8,
  &BossVictory_Heal,
  &Dungeon_StartVictorySpin,
  &Dungeon_RunVictorySpin,
  &KillAghanim_Func12,
};
static PlayerHandlerFunc *const kMainRouting[28] = {
  &Module00_Intro,
  &Module01_FileSelect,
  &Module02_CopyFile,
  &Module03_KILLFile,
  &Module04_NameFile,
  &Module05_LoadFile,
  &Module_PreDungeon,
  &Module07_Dungeon,
  &Module08_OverworldLoad,
  &Module09_Overworld,
  &Module08_OverworldLoad,
  &Module09_Overworld,
  &Module_Unknown0,
  &Module_Unknown1,
  &Module0E_Interface,
  &Module0F_SpotlightClose,
  &Module10_SpotlightOpen,
  &Module11_DungeonFallingEntrance,
  &Module12_GameOver,
  &Module13_BossVictory_Pendant,
  &Module14_Attract,
  &Module15_MirrorWarpFromAga,
  &Module16_BossVictory_Crystal,
  &Module17_SaveAndQuit,
  &Module18_GanonEmerges,
  &Module19_TriforceRoom,
  &Module1A_Credits,
  &Module1B_SpawnSelect,
};

const uint16 *SrcPtr(uint16 src) {
  return &kPredefinedTileData[src >> 1];
}

uint8 Ancilla_Sfx2_Near(uint8 a) {
  return sound_effect_1 = PlaySfx_SetPan(a);
}

void Ancilla_Sfx3_Near(uint8 a) {
  sound_effect_2 = PlaySfx_SetPan(a);
}

void LoadDungeonRoomRebuildHUD() {
  mosaic_level = 0;
  MOSAIC_copy = 7;
  Hud_SearchForEquippedItem();
  Hud_Rebuild();
  Hud_UpdateEquippedItem();
  Module_PreDungeon();
}

void Module_Unknown0() {
  assert(0);
}

void Module_Unknown1() {
  assert(0);
}

static void KillAgahnim_LoadMusic() {
  nmi_disable_core_updates = 0;
  overworld_map_state++;
  submodule_index++;
  LoadOWMusicIfNeeded();
}

static void KillAghanim_Init() {
  music_control = 8;
  BYTE(overworld_screen_trans_dir_bits) = 8;
  InitializeMirrorHDMA();
  overworld_map_state = 0;
  PaletteFilter_InitializeWhiteFilter();
  Overworld_LoadGFXAndScreenSize();
  submodule_index++;
  link_player_handler_state = kPlayerState_Mirror;
  bg1_x_offset = 0;
  bg1_y_offset = 0;
  dung_savegame_state_bits = 0;
  WORD(link_y_vel) = 0;
  main_palette_buffer[0] = 0x7fff;
  main_palette_buffer[32] = 0x7fff;
  Ancilla_TerminateSelectInteractives(0);
  Link_ResetProperties_A();
}

static void KillAghanim_Func2() {
  HDMAEN_copy = 192;
  MirrorWarp_BuildWavingHDMATable();
  submodule_index++;
  subsubmodule_index = 0;
}

static void KillAghanim_Func3() {
  MirrorWarp_BuildWavingHDMATable();
  if (subsubmodule_index) {
    subsubmodule_index = 0;
    submodule_index++;
  }
}

static void KillAghanim_Func4() {
  MirrorWarp_BuildDewavingHDMATable();
  if (subsubmodule_index) {
    subsubmodule_index = 0;
    submodule_index++;
  }
}

static void KillAghanim_Func5() {
  HdmaSetup(0, 0xf2fb, 0x41, 0, (uint8)WH0, 0);
  for (int i = 0; i < 240; i++)
    hdma_table_dynamic[i] = 0xff00;
  palette_filter_countdown = 0;
  darkening_or_lightening_screen = 0;
  dialogue_message_index = 0x35;
  Main_ShowTextMessage();
  ReloadPreviouslyLoadedSheets();
  Hud_RebuildIndoor();
  HDMAEN_copy = 0x80;
  main_module_index = 21;
  submodule_index = 6;
  subsubmodule_index = CutsceneFastForwardEnabled() ? 4 : 24;
}

static void KillAghanim_Func6() {
  subsubmodule_index = CutsceneClampTimer8(subsubmodule_index, 4);
  if (!--subsubmodule_index) {
    submodule_index++;
    sound_effect_ambient = 9;
  }
}

static void KillAghanim_Func7() {
  RenderText();
  if (!submodule_index) {
    overworld_map_state = 0;
    sound_effect_ambient = 5;
    if (!link_item_moon_pearl) {
      dialogue_message_index = 0x36;
      Main_ShowTextMessage();
      sound_effect_ambient = 0;
      main_module_index = 21;
      submodule_index = 8;
    } else {
      submodule_index = 9;
    }
  }
}

static void KillAghanim_Func8() {
  RenderText();
  if (!submodule_index) {
    subsubmodule_index = CutsceneFastForwardEnabled() ? 4 : 32;
    submodule_index = 12;
  }
}

static void KillAghanim_Func12() {
  subsubmodule_index = CutsceneClampTimer8(subsubmodule_index, 4);
  if (--subsubmodule_index)
    return;
  ResetAncillaAndCutscene();
  Overworld_SetSongList();
  save_ow_event_info[0x1b] |= 32;
  // Phase B Slice 1 §38 — Aga 1 defeat is a reachability-changing event
  // (per audit.md §0.4a). Mark the Agahnim event location checked: it is a pure
  // event location (vanilla_item = DefeatAgahnim, no real item to grant), and the
  // logic's HAS_ITEM(DefeatAgahnim) gates (e.g. Lumberjack Tree, pyramid access)
  // derive from it via Rando_BuildRuntimeCounts. Nothing else marks loc 34 at
  // runtime, so without this the live reachability tracker would treat post-Aga1
  // checks as permanently unreachable. Mark (not just bump) — Rando_MarkLocation-
  // Checked bumps the reachability counter itself.
  if (enhanced_features1 & kFeatures1_RandomizerActive)
    Rando_MarkLocationChecked(LOC_Agahnim);
  BYTE(cur_palace_index_x2) = 255;
  submodule_index = 0;
  overworld_map_state = 0;
  nmi_disable_core_updates = 0;
  main_module_index = 9;
  BYTE(BG1VOFS_copy2) = 0;
  music_control = link_item_moon_pearl ? 9 : 4;
  savegame_map_icons_indicator = 6;
}

void Module_MainRouting() {  // 8080b5
  kMainRouting[main_module_index]();
}

void NMI_PrepareSprites() {  // 8085fc
  static const uint16 kLinkDmaSources1[303] = {
    0x8080, 0x8080, 0x8080, 0x8080, 0x8080, 0x8040, 0x8040, 0x8040, 0x8040, 0x8040, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,
    0x9440, 0x8080, 0x8080, 0x8080, 0x9400, 0x8040, 0x80c0, 0x80c0, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,
    0x8080, 0x8080, 0x8080, 0x8080, 0x8080, 0x8040, 0x8040, 0x8040, 0x8040, 0x8040, 0x8000, 0xa8c0, 0xa900, 0x8000, 0xa8c0, 0xa900,
    0x9100, 0x8080, 0x8080, 0x90c0, 0x8040, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x9a00, 0x9140, 0x9180, 0x8000, 0x9500,
    0x9480, 0x94c0, 0x94c0, 0x9ae0, 0x8080, 0x8080, 0x9a60, 0x80c0, 0x80c0, 0x9aa0, 0x8000, 0x8000, 0x9aa0, 0x8000, 0x8000, 0x8080,
    0x8080, 0x8100, 0x8100, 0x85c0, 0x8000, 0x8000, 0x85c0, 0x8000, 0x8000, 0xadc0, 0xadc0, 0xadc0, 0xadc0, 0xadc0, 0xad40, 0xad40,
    0xad40, 0xad40, 0xad40, 0xad80, 0xad80, 0xad80, 0xad80, 0xad80, 0xad80, 0x8040, 0x9400, 0x8040, 0x8000, 0x8080, 0x8080, 0x9440,
    0x8000, 0x8000, 0x8000, 0x8000, 0x8080, 0x8040, 0x8040, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0xc440, 0x8140, 0x8140,
    0xca40, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8040, 0x85c0, 0x8040, 0x85c0, 0x8100, 0x80c0, 0x91c0, 0x8080, 0x8080,
    0x8040, 0x8040, 0x8000, 0x8000, 0x8000, 0x8000, 0x8080, 0x8080, 0x9100, 0xa0c0, 0xa100, 0xa100, 0xa1c0, 0xa400, 0xa440, 0xa1c0,
    0xa400, 0xa440, 0x8080, 0xc480, 0x8080, 0x8040, 0x8040, 0xca80, 0xca80, 0xca00, 0xc400, 0xca00, 0xc400, 0x81c0, 0x8080, 0x8080,
    0x8080, 0x8080, 0x8080, 0x8080, 0x8080, 0x8080, 0x8040, 0x8040, 0x8040, 0x8040, 0x8040, 0x8040, 0x8040, 0x8000, 0xa8c0, 0xa900,
    0x8000, 0x8000, 0xa8c0, 0xa900, 0x8000, 0xa8c0, 0xa900, 0x8000, 0x8000, 0xa8c0, 0xa900, 0x8040, 0x8040, 0x8040, 0x8080, 0x8080,
    0x8040, 0x8040, 0x8040, 0x8040, 0x8000, 0x8000, 0x8000, 0x8000, 0xd080, 0x8080, 0x90c0, 0xd000, 0x9080, 0xd040, 0x9080, 0xd040,
    0xd080, 0xd080, 0xd080, 0xd080, 0xd080, 0xd000, 0xd000, 0xd000, 0xd000, 0xd000, 0xd040, 0xd040, 0xd040, 0xd040, 0xd040, 0xd040,
    0x8040, 0xd000, 0x85c0, 0x85c0, 0x85c0, 0xdc40, 0xdc40, 0xdc40, 0x85c0, 0x85c0, 0x85c0, 0xdc40, 0xdc40, 0xdc40, 0xe1c0, 0xd000,
    0x8000, 0xe400, 0xe400, 0xe440, 0x90c0, 0x90c0, 0xd000, 0x8000, 0x8000, 0xd040, 0x8000, 0x8000, 0xd040, 0xe400, 0xe400, 0xe400,
    0x9080, 0xa5c0, 0xac40, 0xe480, 0x8180, 0x90c0, 0x80c0, 0xe180, 0xd000, 0xe4c0, 0xe4c0, 0xe840, 0xe840, 0xe840, 0xe540, 0xe540,
    0xe540, 0xe900, 0xe900, 0xe900, 0xe900, 0x8080, 0x8080, 0x8000, 0xa9c0, 0x8080, 0x8140, 0x91c0, 0x8040, 0xa800, 0xa840,
  };
  static const uint16 kLinkDmaSources2[303] = {
    0x8840, 0x8800, 0x8580, 0x8800, 0x8580, 0x84c0, 0x8500, 0x8540, 0x8500, 0x8540, 0x8400, 0x8440, 0x8480, 0x8400, 0x8440, 0x8480,
    0x9640, 0x8c40, 0x8c80, 0xad00, 0x9600, 0x8980, 0x8c00, 0xacc0, 0x8880, 0x88c0, 0x8900, 0x8940, 0x8880, 0x88c0, 0x8900, 0x8940,
    0xb0c0, 0xb100, 0xb140, 0xb100, 0xb140, 0xb000, 0xb040, 0xb080, 0xec80, 0xecc0, 0xb180, 0xd440, 0xb1c0, 0xb180, 0xd440, 0xb1c0,
    0x8c80, 0xad00, 0x95c0, 0x99c0, 0xb440, 0x9580, 0xb480, 0xb4c0, 0x9580, 0xb480, 0xb4c0, 0x9c20, 0x8000, 0x8000, 0x8000, 0x9700,
    0x9680, 0x96c0, 0x96c0, 0x9ce0, 0x8c80, 0xb540, 0x9c60, 0xb580, 0x8c00, 0x9ca0, 0x8900, 0xb500, 0x9ca0, 0x8900, 0xb500, 0x8c40,
    0xec40, 0x8c00, 0xec00, 0x8dc0, 0x9540, 0x89c0, 0x8dc0, 0x9540, 0x89c0, 0xb940, 0xb980, 0xb9c0, 0xb980, 0xb9c0, 0xb5c0, 0xb800,
    0xb840, 0xb800, 0xb840, 0xb880, 0xb8c0, 0xb900, 0xb880, 0xb8c0, 0xb900, 0x8980, 0x9600, 0xbcc0, 0x8400, 0xbc80, 0x8c40, 0x9640,
    0xa040, 0xa080, 0xa000, 0xbc40, 0xbd40, 0x8500, 0xbd00, 0xbd80, 0xbd80, 0x88c0, 0x8900, 0xe9c0, 0x8900, 0xc640, 0xc040, 0xc000,
    0xcc40, 0x8940, 0x88c0, 0x8900, 0xe9c0, 0x8900, 0x8940, 0x8d40, 0x8d80, 0x8d40, 0x8d80, 0xbd00, 0xb000, 0xb000, 0xa480, 0xa480,
    0xa480, 0xa480, 0xac00, 0xac00, 0xac00, 0xac00, 0xa140, 0xa180, 0xa180, 0xa4c0, 0xa4c0, 0xa500, 0x9d40, 0x9d80, 0x9dc0, 0x9d40,
    0x9d80, 0x9dc0, 0x8d00, 0xc680, 0xc180, 0xc140, 0x8c00, 0xcc80, 0xcc80, 0xcc00, 0xc600, 0xcc00, 0xc600, 0xbd00, 0x8580, 0x8800,
    0xc9c0, 0xccc0, 0xcdc0, 0xcd00, 0xcd40, 0xcd80, 0x8500, 0x8540, 0xc940, 0xc980, 0x8540, 0xc940, 0xc980, 0x8440, 0x8480, 0xc1c0,
    0xc900, 0xc580, 0xc5c0, 0xc8c0, 0x8440, 0x8480, 0xc1c0, 0xc900, 0xc580, 0xc5c0, 0xc8c0, 0xbd00, 0xacc0, 0xc040, 0xd540, 0xd580,
    0xd4c0, 0xd500, 0xd4c0, 0xd500, 0xd440, 0xd480, 0xd440, 0xd480, 0xd1c0, 0xd400, 0xd100, 0xd100, 0xd140, 0xd180, 0xd140, 0xd180,
    0xb0c0, 0xb100, 0xb140, 0xb100, 0xb140, 0xdd40, 0xdd80, 0xddc0, 0xdd80, 0xddc0, 0xdc80, 0xdcc0, 0xdd00, 0xdc80, 0xdcc0, 0xdd00,
    0xd100, 0xd100, 0xe000, 0xe040, 0xe080, 0xe0c0, 0xe100, 0xe140, 0xe000, 0xe040, 0xe080, 0xe0c0, 0xe100, 0xe140, 0x8000, 0xd0c0,
    0x8000, 0xb940, 0xb980, 0xb940, 0xdd40, 0xdd80, 0xdd40, 0xdc80, 0xdcc0, 0xc0c0, 0xdc80, 0xdcc0, 0xc0c0, 0xb9c0, 0xb980, 0xb9c0,
    0xa560, 0xa5a0, 0xac80, 0xed00, 0x8000, 0x8cc0, 0xbd00, 0xe380, 0xbdc0, 0xe500, 0xe500, 0xe880, 0xe8c0, 0xe8c0, 0xe800, 0xe5c0,
    0xe5c0, 0xe940, 0xe980, 0xe940, 0xe980, 0xbd40, 0x8c80, 0xa080, 0x8000, 0xa980, 0xbd00, 0xbdc0, 0xb400, 0xa880, 0xedc0,
  };
  static const uint16 kLinkDmaSources3[27] = {
    0x9a40, 0x9e00, 0x9d20, 0x9f20, 0x9b20, 0xbc20, 0xbc20, 0xbe20, 0xbe20, 0xbe00, 0xbe00, 0xbe00, 0xbe00, 0xa540, 0xa540, 0xa540,
    0xa540, 0xbc00, 0xbc00, 0xbc00, 0xbc00, 0xa740, 0xa740, 0xa740, 0xa740, 0xe780, 0xe780,
  };
  static const uint16 kLinkDmaSources4[8] = { 0x9000, 0x9020, 0x9060, 0x91e0, 0x90a0, 0x90c0, 0x9100, 0x9140 };
  static const uint16 kLinkDmaSources5[3] = { 0x9300, 0x9340, 0x9380 };
  static const uint16 kLinkDmaSources6[128] = {
    0x9480, 0x94c0, 0x94e0, 0x95c0, 0x9500, 0x9520, 0x9540, 0x9480, 0x9640, 0x9680, 0x96a0, 0x9780, 0x96c0, 0x96e0, 0x9700, 0x9480,
    0x9800, 0x9840, 0x98a0, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9ac0, 0x9b00, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480,
    0x9bc0, 0x9c00, 0x9c40, 0x9c80, 0x9cc0, 0x9d00, 0x9d40, 0x9480, 0x9f40, 0x9f80, 0x9fc0, 0x9fe0, 0xa000, 0x9480, 0x9480, 0x9480,
    0xa100, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480,
    0x98c0, 0x9900, 0x99c0, 0x99e0, 0x9a00, 0x9a20, 0x9a40, 0x9a60, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480,
    0x9a80, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480,
    0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480,
    0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480, 0x9480,
  };
  static const uint16 kLinkDmaSources7[16] = { 0xe0, 0xe0, 0x60, 0x80, 0x1c0, 0xe0, 0x40, 0, 0x80, 0, 0x40, 0, 0, 0, 0, 0 };
  static const uint16 kLinkDmaCtrs0[6] = { 14, 4, 6, 16, 6, 8 };
  static const uint16 kLinkDmaSources9[15] = { 0, 0x20, 0x40, 0, 0x20, 0x40, 0, 0x40, 0x80, 0, 0x40, 0x80, 0xb340, 0xb400, 0xb4c0 };
  static const uint16 kLinkDmaSources8[4] = { 0xa480, 0xa4c0, 0xa500, 0xa540 };

  for (int i = 0; i < 32; i++) {
    extended_oam[i] = bytewise_extended_oam[3 + 4 * i] << 6 |
      bytewise_extended_oam[2 + 4 * i] << 4 |
      bytewise_extended_oam[1 + 4 * i] << 2 |
      bytewise_extended_oam[0 + 4 * i] << 0;
  }

  dma_source_addr_3 = kLinkDmaSources1[link_dma_graphics_index >> 1];
  dma_source_addr_0 = dma_source_addr_3 + 0x200;
  dma_source_addr_4 = kLinkDmaSources2[link_dma_graphics_index >> 1];
  dma_source_addr_1 = dma_source_addr_4 + 0x200;
  dma_source_addr_5 = kLinkDmaSources3[link_dma_var1 >> 1];
  dma_source_addr_2 = kLinkDmaSources3[link_dma_var2 >> 1];


  dma_source_addr_6 = kLinkDmaSources4[link_dma_var3 >> 1];
  dma_source_addr_11 = dma_source_addr_6 + 0x180;

  if (link_dma_var4 == 0x8b) {
    dma_source_addr_7 = 0xe099;
  } else {
    dma_source_addr_7 = kLinkDmaSources5[link_dma_var4 >> 1];
  }
  dma_source_addr_12 = dma_source_addr_7 + 0xc0;


  int j = (link_dma_var5 & 0xf8) >> 3;
  dma_source_addr_8 = kLinkDmaSources6[link_dma_var5];
  dma_source_addr_13 = dma_source_addr_8 + kLinkDmaSources7[j];
  dma_source_addr_10 = kLinkDmaSources8[pushedblocks_some_index & 3];
  dma_source_addr_15 = dma_source_addr_10 + 0x100;

  if (--bg_tile_animation_countdown == 0) {
    bg_tile_animation_countdown = (BYTE(overlay_index) == 0xb5 || BYTE(overlay_index) == 0xbc) ? 0x17 : 9;

    uint16 t = word_7EC00F + 0x400;
    if (t == 0xc00)
      t = 0;
    word_7EC00F = t;
    animated_tile_data_src = 0xa680 + word_7EC00F;
  }

  if (--word_7EC013 == 0) {
    int t = word_7EC015 + 2;
    if (t == 12)
      t = 0;
    word_7EC015 = t;
    word_7EC013 = kLinkDmaCtrs0[t >> 1];
    dma_source_addr_9 = kLinkDmaSources9[t >> 1] + 0xb280;
    dma_source_addr_14 = dma_source_addr_9 + 0x60;
  }

  dma_source_addr_16 = 0xB940 + dma_var6 * 2;
  dma_source_addr_18 = dma_source_addr_16 + 0x200;

  dma_source_addr_17 = 0xB940 + dma_var7 * 2;
  dma_source_addr_19 = dma_source_addr_17 + 0x200;

  dma_source_addr_20 = 0xB540 + flag_travel_bird * 2;
  dma_source_addr_21 = dma_source_addr_20 + 0x200;
}

void Sound_LoadIntroSongBank() {  // 808901
  LoadSongBank(kSoundBank_intro);
}

void LoadOverworldSongs() {  // 808913
  LoadSongBank(kSoundBank_intro);
}

void LoadDungeonSongs() {  // 808925
  LoadSongBank(kSoundBank_indoor);
}

void LoadCreditsSongs() {  // 808931
  LoadSongBank(kSoundBank_ending);
}

void Dungeon_LightTorch() {  // 81f3ec
  if ((byte_7E0333 & 0xf0) != 0xc0) {
    byte_7E0333 = 0;
    return;
  }
  uint8 r8 = (uint8)dungeon_room_index == 0 ? 0x80 : 0xc0;

  int i = (byte_7E0333 & 0xf) + (dung_index_of_torches_start >> 1);
  int opos = dung_object_pos_in_objdata[i];
  if (dung_object_tilemap_pos[i] & 0x8000)
    return;
  dung_object_tilemap_pos[i] |= 0x8000;
  if (r8 == 0)
    dung_torch_data[opos] = dung_object_tilemap_pos[i];

  uint16 x = dung_object_tilemap_pos[i] & 0x3fff;
  RoomDraw_AdjustTorchLightingChange(x, 0xeca, x);

  sound_effect_1 = 42 | CalculateSfxPan_Arbitrary((x & 0x7f) * 2);

  nmi_copy_packets_flag = 1;
  if (dung_want_lights_out) {
    if (dung_num_lit_torches++ < 3) {
      TS_copy = 0;
      overworld_fixed_color_plusminus = kLitTorchesColorPlus[dung_num_lit_torches];
      submodule_index = 10;
      subsubmodule_index = 0;
    }
  }

  dung_torch_timers[byte_7E0333 & 0xf] = r8;
  byte_7E0333 = 0;
}

void RoomDraw_AdjustTorchLightingChange(uint16 x, uint16 y, uint16 r8) {  // 81f746
  const uint16 *ptr = SrcPtr(y);
  x >>= 1;
  overworld_tileattr[x + 0] = ptr[0];
  overworld_tileattr[x + 64] = ptr[1];
  overworld_tileattr[x + 1] = ptr[2];
  overworld_tileattr[x + 65] = ptr[3];
  Dungeon_PrepOverlayDma_nextPrep(0, r8);
}

int Dungeon_PrepOverlayDma_nextPrep(int dst, uint16 r8) {  // 81f764
  uint16 r6 = 0x880 + ((r8 & 0x3f) >= 0x3a);
  return Dungeon_PrepOverlayDma_watergate(dst, r8, r6, 4);
}

int Dungeon_PrepOverlayDma_watergate(int dst, uint16 r8, uint16 r6, int loops) {  // 81f77c
  for (int k = 0; k < loops; k++) {
    int x = r8 >> 1;
    vram_upload_tile_buf[dst + 0] = ((r8 & 0x40) << 4) | ((r8 & 0x303f) >> 1) | ((r8 & 0xf80) >> 2);
    vram_upload_tile_buf[dst + 1] = r6;
    vram_upload_tile_buf[dst + 2] = overworld_tileattr[x + 0];
    if (!(r6 & 1)) {
      vram_upload_tile_buf[dst + 3] = overworld_tileattr[x + 1];
      vram_upload_tile_buf[dst + 4] = overworld_tileattr[x + 2];
      vram_upload_tile_buf[dst + 5] = overworld_tileattr[x + 3];
      r8 += 128;
    } else {
      vram_upload_tile_buf[dst + 3] = overworld_tileattr[x + 64];
      vram_upload_tile_buf[dst + 4] = overworld_tileattr[x + 128];
      vram_upload_tile_buf[dst + 5] = overworld_tileattr[x + 192];
      r8 += 2;
    }
    dst += 6;
  }
  vram_upload_tile_buf[dst] = 0xffff;
  return dst;
}

void Module05_LoadFile() {  // 828136
  if (ZeldaRestartTestScenario()) return;  // Continue within the isolated developer scenario
  ZeldaWideForgetPlaces();
  EnableForceBlank();
  overworld_map_state = 0;
  dung_unk6 = 0;
  byte_7E02D4 = 0;
  byte_7E02D7 = 0;
  tagalong_var5 = 0;
  byte_7E0379 = 0;
  byte_7E03FD = 0;
  EraseTileMaps_normal();
  LoadDefaultGraphics();
  Sprite_LoadGraphicsProperties();
  Init_LoadDefaultTileAttr();
  DecompressSwordGraphics();
  DecompressShieldGraphics();
  Link_Initialize();
  LoadFollowerGraphics();
  sprite_gfx_subset_0 = 70;
  sprite_gfx_subset_1 = 70;
  sprite_gfx_subset_2 = 70;
  sprite_gfx_subset_3 = 70;
  word_7E02CD = 0x200;
  virq_trigger = 48;

  // Rando starting-inventory grant. Self-gates on `g_rando_slot_active` and
  // `g_rando_starting_inventory_granted`, so vanilla mode is unaffected and
  // load-existing-slot doesn't re-grant. NULL settings is intentional —
  // sidecar slots don't carry the full settings blob, so this call only
  // triggers the placement-based escape-ammo grant. The Inverted
  // Moon-Pearl/Mirror branch is inert here until world_state is persisted
  // through slot reload.
  (void)Rando_TryGrantStartingInventory(NULL);
  (void)Rando_NormalizeMirrorlessAwayWorld();

  // Phase B post-escape S&Q respawn fix. A non-Standard rando slot starts
  // post-escape (sram_progress_indicator == 2): the Hyrule Castle escape never
  // happens, but the HC interior / sewers are still physically reachable. If
  // the player saves-and-quits while standing in an escape-only spawn region,
  // the vanilla escape story-beat sprites may have written an escape-only value
  // into the persisted respawn pointer `which_starting_point` (cell = 2, post-
  // uncle sewers = 3, post-throne sewers = 4, Old-Man escort = 5) and/or left
  // Zelda as a persisted follower. On reload Module05 would then drop the player
  // back into the sealed escape with no Zelda to push the throne — a hard trap,
  // since there is no longer any exit (matching the playtest symptom). The
  // only safe spawn points are the spawn-select menu locations (Link's House 0,
  // Sanctuary 1, Mountain Cave 6). Under an active rando slot that is already
  // post-escape, sanitize any escape-only respawn pointer back to Sanctuary and
  // clear a stale escape follower so reload routes through the normal
  // post-escape spawn-select instead of the sealed escape area. Standard is
  // untouched (its escape is real and progress climbs through it normally).
  // Gate on the slot's world_state, NOT sram_progress_indicator: entering
  // Hyrule Castle in a non-Standard seed can re-run the escape story beats,
  // which both sets which_starting_point to an escape-only value AND drops
  // progress below 2 — so a progress>=2 gate (the earlier attempt) misses
  // exactly the trap it was meant to catch (confirmed by an F12 dump:
  // progress=1, which_starting_point=3). Standard is excluded (its escape is
  // real and progress climbs through it legitimately).
  if ((enhanced_features1 & kFeatures1_RandomizerActive) &&
      Rando_GetActiveWorldState() != 1 /* kWorldState_Standard */) {
    uint8 sp = which_starting_point;
    if (sp != 0 && sp != 1 && sp != 6) {
      // rando-exempt: post-escape respawn sanitize — redirect an escape-only
      // spawn pointer to Sanctuary so S&Q can't trap the player in the sealed
      // HC escape. Not an item grant.
      which_starting_point = 1;  // Sanctuary
      if (follower_indicator == 1)  // stale Zelda mid-escape follower
        follower_indicator = 0;
      // The re-engaged escape dropped progress; restore the post-escape
      // free-roam state so the overworld isn't gated behind the sealed escape.
      // rando-exempt: post-escape progress restore (non-Standard rando)
      if (sram_progress_indicator < 2)
        sram_progress_indicator = 2;
    }
  }

  // Phase B Inverted runtime: an Inverted rando slot bakes
  // savegame_is_darkworld=0x40 into a fresh save (select_file.c's world-state
  // start-SRAM block), so it takes the DW-outdoors load path below (the
  // mirror-exit overworld screen, dungeon_room_index 32) — the SAME path
  // vanilla uses for a Dark-World save — and spawns in the Dark World, where
  // the #82 tile overlays render the inverted topology.
  //
  // (An earlier revision excluded Inverted here and routed it through the LW
  // spawn-select, believing the DW-outdoors path produced an uncontrollable
  // spawn. That was actually the kPlayerState_HoldUpItem freeze from granting
  // Moon Pearl/Mirror via Link_ReceiveItem — since fixed with a direct
  // byte-write in Rando_TryGrantStartingInventory. Playtest-confirmed the DW
  // spawn works, so the exclusion is removed: Inverted spawns in the DW like
  // the original approach, not the Light World.)
  if (savegame_is_darkworld) {
    if (player_is_indoors) {
      LoadDungeonRoomRebuildHUD();
      return;
    }
    // #82 Inverted spawn fix. The vanilla DW-outdoors respawn
    // (dungeon_room_index=32 -> the room-0x20 exit) lands on the Pyramid-of-Power
    // ledge — a sealed pocket with no items to escape, which traps an Inverted
    // player from the first frame (playtest-confirmed: spawn screen 0x5B, no
    // movement items). ALTTPR relocates the Inverted start to the navigable
    // "Dark Chapel". The fork never ported that; below we route post-Agahnim
    // Inverted loads through the spawn-select menu (Dark Chapel / Dark Mountain)
    // and use Link's House as the navigable home for everything else.
    // Gated on the active Inverted slot; vanilla / Open / Standard / Retro DW
    // saves keep the room-32 path.
    if ((enhanced_features1 & kFeatures1_RandomizerActive) &&
        Rando_GetActiveWorldState() == 2 /* kWorldState_Inverted */) {
      // add-rando-inverted-dark-chapel-spawn: expose the post-Agahnim spawn-select
      // menu for Inverted too. ALTTPR renames its three options "@'s House" /
      // "Dark Chapel" / "Dark Mountain" (Link's House / Sanctuary / Mountain Cave)
      // and places all three in the Dark World. Vanilla only reaches the menu via
      // the Light-World-save `else` branch below; an Inverted slot bakes
      // savegame_is_darkworld=0x40 and lands here, so before this it never saw the
      // menu (it hard-routed to Link's House). Mirror the LW branch's menu gate
      // verbatim: a death-revival / pre-Agahnim respawn keeps the §B6 direct
      // Link's-House spawn; only a post-Agahnim file load opens the menu. The
      // chosen anchor is forced into the DW by g_rando_inverted_spawn_redirect
      // (set in Module1B_SpawnSelect, consumed at the overworld exit) instead of
      // a kExitData asset override — that way the Sanctuary / Mountain-Cave
      // *checks* (same rooms, entered from the overworld) still exit to the LW.
      bool inverted_show_spawn_menu = !(mosaic_level ||
          (death_var5 != 0 && !death_var4) ||
          sram_progress_indicator < 2 ||
          which_starting_point == 5);
      if (inverted_show_spawn_menu) {
        dialogue_message_index = (link_item_mirror == 2) ? 0x185 : 0x184;
        Main_ShowTextMessage();
        Dungeon_LoadPalettes();
        INIDISP_copy = 15;
        TM_copy = 4;
        TS_copy = 0;
        main_module_index = 27;
        return;
      }
      // Death-revival / pre-Agahnim respawn: §B6 direct Link's-House spawn. The
      // empty post-escape house exits to the DW Bomb-Shop position (per the
      // inverted Link's-House<->Bomb-Shop exit swap), connecting to DW-South.
      player_is_indoors = 1;
      dungeon_room_index = 0x104;  // Link's House (Inverted home)
      LoadDungeonRoomRebuildHUD();
      return;
    }
    Hud_SearchForEquippedItem();
    Hud_Rebuild();
    Hud_UpdateEquippedItem();
    death_var5 = 0;
    dungeon_room_index = 32;
    main_module_index = 8;
    submodule_index = 0;
    subsubmodule_index = 0;
    death_var4 = 0;
  } else {
    if (mosaic_level || death_var5 != 0 && !death_var4 || sram_progress_indicator < 2 || which_starting_point == 5) {
      LoadDungeonRoomRebuildHUD();
      return;
    }
    dialogue_message_index = (link_item_mirror == 2) ? 0x185 : 0x184;
    Main_ShowTextMessage();
    Dungeon_LoadPalettes();
    INIDISP_copy = 15;
    TM_copy = 4;
    TS_copy = 0;
    main_module_index = 27;
  }
}

void Module13_BossVictory_Pendant() {  // 829c4a
  kModule_BossVictory[submodule_index]();
  Sprite_Main();
  LinkOam_Main();
}

void BossVictory_Heal() {  // 829c59
  if (!Hud_RefillMagicPower())
    overworld_map_state++;
  if (!Hud_RefillHealth())
    overworld_map_state++;
  if (!overworld_map_state) {
    button_mask_b_y &= ~0x40;
    Dungeon_ResetTorchBackgroundAndPlayerInner();
    link_direction_facing = 2;
    link_direction_last = 2 << 1;
    flag_update_hud_in_nmi++;
    submodule_index++;
    subsubmodule_index = CutsceneFastForwardEnabled() ? 4 : 16;
    flag_is_link_immobilized++;
  }
  overworld_map_state = 0;
  Hud_RefillLogic();
}

void Dungeon_StartVictorySpin() {  // 829c93
  if (--subsubmodule_index)
    return;
  flag_is_link_immobilized = 0;
  link_direction_facing = 2;
  Link_AnimateVictorySpin();
  Ancilla_TerminateSelectInteractives(0);
  AncillaAdd_VictorySpin();
  submodule_index++;
}

void Dungeon_RunVictorySpin() {  // 829cad
  Link_Main();
  if (link_player_handler_state != 0)
    return;
  if (link_sword_type + 1 & 0xfe)
    sound_effect_1 = 0x2C;
  link_force_hold_sword_up = 1;
  subsubmodule_index = CutsceneFastForwardEnabled() ? 4 : 32;
  submodule_index++;
}

void Dungeon_CloseVictorySpin() {  // 829cd1
  subsubmodule_index = CutsceneClampTimer8(subsubmodule_index, 4);
  if (--subsubmodule_index)
    return;
  submodule_index++;
  link_y_vel = 0;
  link_x_vel = 0;
  overworld_fixed_color_plusminus = 0;
}

void Module15_MirrorWarpFromAga() {  // 829cfc
  kModule_KillAgahnim[submodule_index]();
  if (submodule_index < 2 || submodule_index >= 5) {
    Sprite_Main();
    LinkOam_Main();
  }
}

void Module16_BossVictory_Crystal() {  // 829e8a
  switch (submodule_index) {
  case 0: BossVictory_Heal(); break;
  case 1: Dungeon_StartVictorySpin(); break;
  case 2: Dungeon_RunVictorySpin(); break;
  case 3: Dungeon_CloseVictorySpin(); break;
  case 4: Module16_04_FadeAndEnd(); break;
  }
  Sprite_Main();
  LinkOam_Main();
}

void Module16_04_FadeAndEnd() {  // 829e9a
  INIDISP_copy = CutsceneClampTimer8(INIDISP_copy, 1);
  if (--INIDISP_copy)
    return;
  bg1_x_offset = 0;
  bg1_y_offset = 0;
  link_y_vel = 0;
  flag_is_link_immobilized = 0;
  Palette_RevertTranslucencySwap();
  link_player_handler_state = kPlayerState_Ground;
  link_receiveitem_index = 0;
  link_pose_for_item = 0;
  link_disable_sprite_damage = 0;
  main_module_index = saved_module_for_menu;
  submodule_index = 0;
  subsubmodule_index = 0;
  OpenSpotlight_Next2();
}

static uint8 PlaySfx_SetPan(uint8 a) {  // 878036
  byte_7E0CF8 = a;
  return a | Link_CalculateSfxPan();
}

void TriforceRoom_LinkApproachTriforce() {  // 87f49c
  uint8 y = link_y_coord;
  if (y < 152) {
    link_animation_steps = 0;
    link_direction = 0;
    link_direction_last = 0;
    if (!--link_delay_timer_spin_attack) {
      link_pose_for_item = 2;
      subsubmodule_index++;
    }
  } else {
    if (y < 169)
      link_speed_setting = 0x14;
    link_direction = 8;
    link_direction_last = 8;
    link_direction_facing = 0;
    link_delay_timer_spin_attack = 64;
  }
}

static const uint8 kItemReceiptBottleList[7] = {
  0x16, 0x2b, 0x2c, 0x2d, 0x3d, 0x3c, 0x48,
};
static const uint8 kItemReceiptPotionList[5] = {
  0x2e, 0x2f, 0x30, 0xff, 0x0e,
};

// Return whether a bottle acquisition/content receipt can actually write a
// slot. Non-bottle receive codes are unconstrained by this predicate. Vanilla
// historically animates even a bottle no-op, so callers only enforce the
// result while a randomizer slot is active.
static bool ItemReceipt_BottleGrantAvailable(uint8 item) {
  if (FindInByteArray(kItemReceiptBottleList, item,
                      countof(kItemReceiptBottleList)) >= 0) {
    for (int i = 0; i != 4; i++) {
      if (link_bottle_info[i] < 2)
        return true;
    }
    return false;
  }
  if (FindInByteArray(kItemReceiptPotionList, item,
                      countof(kItemReceiptPotionList)) >= 0) {
    for (int i = 0; i != 4; i++) {
      if (link_bottle_info[i] == 2)
        return true;
    }
    return false;
  }
  return true;
}

bool ItemReceipt_CanAccept(uint8 item) {
  return item < 76 && ItemReceipt_BottleGrantAvailable(item);
}

bool ItemReceipt_GrantInventory(uint8 j) {
  uint8 t;
  // Defensive bound guard: every array below (kValueToGiveItemTo,
  // kMemoryLocationToGiveItemTo, …) is sized 76 (0x4C) and indexed by j. The
  // currently-defined receive codes are all < 0x4C, so this is a no-op today; it
  // exists so a future receive code >= 0x4C can't read past these tables.
  if (j >= 76)
    return false;
  // A shuffled bottle must remain retryable when inventory has no compatible
  // slot. Reject before any table write or receipt side effect. Keep vanilla's
  // original animated-no-op behavior when the randomizer is inactive.
  if ((enhanced_features1 & kFeatures1_RandomizerActive) &&
      !ItemReceipt_CanAccept(j))
    return false;
  if (j == 0) {
    // Vanilla "GiveSwordAndShield": receiving the L1 (Fighter) sword also grants
    // the Fighter shield. In vanilla this fires once (from Uncle), but under rando
    // a tier-0 ProgressiveSword translates to code 0 (progressive_to_lttp), so any
    // first-sword pickup runs it — and the player may already hold a higher shield
    // from a separately-shuffled drop. Clamp this side-effect to never-downgrade,
    // exactly like the absolute-write clamp below; otherwise a sword pickup resets
    // a Red/Mirror shield back to Fighter. Vanilla path keeps the unconditional set.
    uint8 *shield = &g_ram[kMemoryLocationToGiveItemTo[4]];  // link_shield_type
    uint8 sv = (uint8)kValueToGiveItemTo[0];                 // 1 = Fighter shield
    if (enhanced_features1 & kFeatures1_RandomizerActive) {
      // rando-exempt: never-downgrade the shield (mirrors the v > *p clamp below)
      if (sv > *shield) *shield = sv;
    } else {
      *shield = sv;
    }
  }

  uint8 v = kValueToGiveItemTo[j];
  uint8 *p = &g_ram[kMemoryLocationToGiveItemTo[j]];
  // Rando: the shovel (0x13) and flute (0x14 inactive / 0x4a active) are
  // independent shuffled items that vanilla packs into one byte
  // (link_item_flute: 1=shovel, 2=flute, 3=active flute). The vanilla
  // unconditional `*p = v` would let acquiring the 2nd of the pair overwrite —
  // and permanently lose — the 1st, softlocking seeds that need both.
  // Rando_GrantFluteShovel records true ownership separately and raises the
  // shared byte without ever downgrading, so both survive; the player swaps
  // which one the slot performs via the item-menu toggle (Hud_NormalMenu).
  if (enhanced_features1 & kFeatures1_RandomizerActive) {
    // Rando: a shared-byte upgrade item must never DOWNGRADE when its lower
    // tier is acquired after the higher one (the byte holds what rando shuffles
    // as independent items). Flute/shovel and boomerang/bow additionally track
    // true ownership so the player can swap the slot's tier in the item menu
    // (see Rando_Grant* / Hud_NormalMenu); every other absolute byte-write
    // (sword 0x00-0x03, shield 0x04-0x06, gloves 0x1b/0x1c, …) just clamps to
    // never-downgrade via `v > *p`. Decreases use negative `v` (sign8) or are
    // special-cased in the chain below, so they never reach this clamp.
    if (j == 0x13 || j == 0x14 || j == 0x4a) {
      Rando_GrantFluteShovel((uint8)j);
    } else if (j == 0x0c || j == 0x2a) {           // any boomerang (blue/red item)
      // Progressive: item color is ignored — 1st collected = blue, 2nd = red.
      Rando_GrantBoomerang();
    } else if (j == 0x0b || j == 0x3a || j == 0x3b) {  // wood bow / silver arrows
      Rando_GrantBow((uint8)j);
    } else if (j == 0x43 || j == 0x44) {
      // link_arrow_filler (0xF376) is a per-frame DRAIN countdown that the HUD
      // pays into link_num_arrows — NOT a monotonic tier. The never-downgrade
      // clamp below would wrongly DROP an arrow grant while a prior fill is
      // still mid-drain (e.g. a 2nd 10-arrow shop purchase — rupees spent, no
      // arrows). Absolute-write it, exactly like vanilla. (bomb/magic filler
      // only ever take negative `v`, so they never reach the clamp.)
      if (!sign8(v))
        *p = v;
    } else if (!sign8(v) && v > *p) {
      *p = v;
    }
  } else if (!sign8(v)) {
    *p = v;
  }

  if (j == 0x1f)
    link_is_bunny = 0;
  else if (j == 0x4b || j == 0x1e)
    link_ability_flags |= (j == 0x4b) ? 4 : 2;

  if (j == 0x1b || j == 0x1c) {
    Palette_UpdateGlovesColor();
  } else if ((t = 4, j == 0x37) || (t = 1, j == 0x38) || (t = 2, j == 0x39)) {
    // Pendant double-grant fix (symmetric to the `AncillaAdd_FallingPrize`
    // crystal-grant fix). This site is the vanilla pendant grant — reached
    // via the boss-kill → RoomTag_GetHeartForPrize → AncillaAdd_FallingPrize
    // chain, which sets link_receiveitem_index to the VANILLA dungeon's
    // pendant code (0x37/0x38/0x39 per kFallingItem_Type /
    // kBossFinishedFallingItem). Under rando with non-identity prize_shuffle,
    // the falling-prize collision path dispatches the placed prize before this
    // receipt grant. Letting `*p |= t` run here would double-OR the vanilla
    // pendant on top — silently corrupting state when a pendant dungeon
    // (EP/DP/TH) was shuffled to hold a crystal. Suppress the OR under
    // rando-active; the dispatch path owns the bit. Map-indicator and
    // overworld-map state are kept regardless — the (*p & 7) == 7 check still
    // works because the rando dispatch has set the correct bits.
    if (!(enhanced_features1 & kFeatures1_RandomizerActive))
      // rando-exempt: vanilla-only fall-through; dispatch path above owns the bit when rando is active.
      *p |= t;
    if ((*p & 7) == 7)
      savegame_map_icons_indicator = 4;
    overworld_map_state++;
  } else if (j == 0x22) {
    if (*p == 0)
      *p = 1;
  } else if (j == 0x25 || j == 0x32 || j == 0x33) {
    WORD(*p) |= 0x8000 >> (BYTE(cur_palace_index_x2) >> 1);
  } else if (j == 0x3e) {
    if (link_state_bits & 0x80)
      link_picking_throw_state = 2;
  } else if (j == 0x20) {
    overworld_map_state++;
    for (int i = 4; i >= 0; i--) {
      if (ancilla_type[i] == 7 || ancilla_type[i] == 0x2c) {
        ancilla_type[i] = 0;
        link_state_bits = 0;
        link_picking_throw_state = 0;
      }
    }
    if (link_cape_mode) {
      link_bunny_transform_timer = 32;
      link_disable_sprite_damage = 0;
      link_cape_mode = 0;
      AncillaAdd_CapePoof(0x23, 4);
      sound_effect_1 = 0x15 | Link_CalculateSfxPan();
    }
  } else if (j == 0x29) {
    if (link_item_mushroom != 2) {
      *p = 1;
      Hud_RefreshIcon();
    }
  } else if ((t = 1, j == 0x24) || item_receipt_method != 2 && (j == 0x27 || (t = 3, j == 0x28) || (t = 10, j == 0x31))) {
    *p += t;
    if (*p > 99)
      *p = 99;
    Hud_RefreshIcon();
  } else if (j == 0x17) {
    *p = (*p + 1) & 3;
    sound_effect_2 = 0x2d | Link_CalculateSfxPan();
  } else if (j == 1) {
    Overworld_SetSongList();
  } else {
    ItemReceipt_GiveBottledItem(j);
  }
  return true;
}

// Return whether Ancilla_AddAncilla(0x22, 4) can create a receive-item
// ancilla without changing allocator state. Keep this byte-for-byte equivalent
// to Ancilla_AllocInit's five-slot free/evictable scan: shops use it as a
// preflight, while Link_ReceiveItem uses it as the acceptance decision before
// touching Link's hold-up state.
bool ItemReceipt_CanAllocate(void) {
  int receipt_count = 0;
  for (int i = 0; i < 5; i++) {
    if (ancilla_type[i] == 0x22)
      receipt_count++;
  }
  if (receipt_count == 5)
    return false;

  for (int i = 0; i < 5; i++) {
    if (ancilla_type[i] == 0)
      return true;
  }

  int k = ancilla_alloc_rotate;
  do {
    if (--k < 0)
      k = 4;
    uint8 type = ancilla_type[k];
    if (type == 0x3c || type == 0x13 || type == 0x0a)
      return true;
  } while (k != 0);
  return false;
}

typedef struct ItemReceiptActionSnapshot {
  uint8 flag_immobilized;
  // NOTE: link_cape_mode is deliberately NOT captured here. Pairing it with
  // link_disable_sprite_damage looks right (the 0x20 crystal branch clears
  // both) but this snapshot is restored on the SUCCESS path too, so restoring
  // cape_mode un-does that branch's intentional cape cancel -- leaving the cape
  // active after its poof played and the bunny timer armed. The pre-existing
  // disable_sprite_damage restore has the same shape and is the real defect;
  // fixing it means excluding both when the 0x20 branch ran, not adding more.
  uint8 flag_sprite_pickup;
  uint8 flag_ancilla_pickup;
  uint8 flag_sprite_pickup_cached;
  uint8 pickup_handshake;
  uint8 player_handler;
  uint8 item_in_hand;
  uint8 state_bits;
  uint8 picking_throw_state;
  uint8 anim_timer_steps;
  uint8 anim_timer;
  uint8 button_mask;
  uint8 a_button;
  uint8 button_frames;
  uint8 speed;
  uint8 cant_change_dir;
  uint8 player_handler_state;
  uint8 pose_for_item;
  uint8 position_mode;
  uint8 disable_sprite_damage;
  uint8 auxiliary_state;
  uint8 incapacitated_timer;
  uint8 blink_countdown;
  uint8 receive_var1;
  uint8 is_running;
  uint8 dash_countdown;
} ItemReceiptActionSnapshot;

static ItemReceiptActionSnapshot ItemReceipt_CaptureActionState(void) {
  ItemReceiptActionSnapshot s;
  s.flag_immobilized = flag_is_link_immobilized;
  s.flag_sprite_pickup = flag_is_sprite_to_pick_up;
  s.flag_ancilla_pickup = flag_is_ancilla_to_pick_up;
  s.flag_sprite_pickup_cached = flag_is_sprite_to_pick_up_cached;
  s.pickup_handshake = byte_7E0FB2;
  s.player_handler = player_handler_timer;
  s.item_in_hand = link_item_in_hand;
  s.state_bits = link_state_bits;
  s.picking_throw_state = link_picking_throw_state;
  s.anim_timer_steps = some_animation_timer_steps;
  s.anim_timer = some_animation_timer;
  s.button_mask = button_mask_b_y;
  s.a_button = bitfield_for_a_button;
  s.button_frames = button_b_frames;
  s.speed = link_speed_setting;
  s.cant_change_dir = link_cant_change_direction;
  s.player_handler_state = link_player_handler_state;
  s.pose_for_item = link_pose_for_item;
  s.position_mode = link_position_mode;
  s.disable_sprite_damage = link_disable_sprite_damage;
  s.auxiliary_state = link_auxiliary_state;
  s.incapacitated_timer = link_incapacitated_timer;
  s.blink_countdown = countdown_for_blink;
  s.receive_var1 = link_receiveitem_var1;
  s.is_running = link_is_running;
  s.dash_countdown = link_countdown_for_dash;
  return s;
}

static void ItemReceipt_RestoreActionState(const ItemReceiptActionSnapshot *s) {
  flag_is_link_immobilized = s->flag_immobilized;
  flag_is_sprite_to_pick_up = s->flag_sprite_pickup;
  flag_is_ancilla_to_pick_up = s->flag_ancilla_pickup;
  flag_is_sprite_to_pick_up_cached = s->flag_sprite_pickup_cached;
  byte_7E0FB2 = s->pickup_handshake;
  player_handler_timer = s->player_handler;
  link_item_in_hand = s->item_in_hand;
  link_state_bits = s->state_bits;
  link_picking_throw_state = s->picking_throw_state;
  some_animation_timer_steps = s->anim_timer_steps;
  some_animation_timer = s->anim_timer;
  button_mask_b_y = s->button_mask;
  bitfield_for_a_button = s->a_button;
  button_b_frames = s->button_frames;
  link_speed_setting = s->speed;
  link_cant_change_direction = s->cant_change_dir;
  link_player_handler_state = s->player_handler_state;
  link_pose_for_item = s->pose_for_item;
  link_position_mode = s->position_mode;
  link_disable_sprite_damage = s->disable_sprite_damage;
  link_auxiliary_state = s->auxiliary_state;
  link_incapacitated_timer = s->incapacitated_timer;
  countdown_for_blink = s->blink_countdown;
  link_receiveitem_var1 = s->receive_var1;
  link_is_running = s->is_running;
  link_countdown_for_dash = s->dash_countdown;
}

static void ItemReceipt_ApplyDeferredEffects(uint8 item) {
  switch (item) {
  case 0x26: case 0x3f:  // BossHeartContainer
    if (link_health_capacity != 0xa0) {
      link_health_capacity += 8;  // rando-exempt: central no-animation receipt dispatcher
      link_hearts_filler += link_health_capacity - link_health_current;
    }
    break;
  case 0x3e:             // BossHeartContainer (boss-drop code)
    if (link_health_capacity != 0xa0) {
      link_health_capacity += 8;  // rando-exempt: central no-animation receipt dispatcher
      link_hearts_filler += 8;
    }
    break;
  case 0x17:             // PieceOfHeart: the fourth piece rolls over to a heart
    if (link_heart_pieces == 0 && link_health_capacity != 0xa0) {
      link_health_capacity += 8;  // rando-exempt: central no-animation receipt dispatcher
      link_hearts_filler += link_health_capacity - link_health_current;
    }
    break;
  case 0x42:
    link_hearts_filler += 8;
    break;
  case 0x45:
    link_magic_filler += 16;
    break;
  case 0x34:
    link_rupees_goal += 1;
    break;
  case 0x35:
    link_rupees_goal += 5;
    break;
  case 0x36: case 0x47:
    link_rupees_goal += 20;
    break;
  case 0x40:
    link_rupees_goal += 100;
    break;
  case 0x41:
    link_rupees_goal += 50;
    break;
  case 0x46:
    link_rupees_goal += 300;
    break;
  case 0x22: case 0x23:
    Palette_Load_LinkArmorAndGloves();
    break;
  default:
    break;
  }
}

// Apply the completed result of a normal item receipt without allocating an
// ancilla or entering Link's hold-up state. This is the authoritative quiet
// path for both naturally quiet pickups and saturated animated receipts.
// Presentation callers remain responsible for their sound/HUD/icon policy.
bool ItemReceipt_GrantWithoutAnimation(uint8 item) {
  if (item >= 76)
    return false;

  ItemReceiptActionSnapshot action = ItemReceipt_CaptureActionState();
  uint8 saved_receipt_method = item_receipt_method;
  uint8 saved_receive_index = link_receiveitem_index;
  // Preserve method 2. ItemReceipt_GrantInventory reads item_receipt_method:
  // a method-2 (Wish Pond) receipt of 0x27/0x28/0x31 deliberately does NOT bump
  // the bomb/arrow counter. Forcing 0 here made the quiet path grant capacity
  // vanilla withholds. Every other method behaves identically at 0.
  if (item_receipt_method != 2)
    item_receipt_method = 0;
  if (!ItemReceipt_GrantInventory(item)) {
    ItemReceipt_RestoreActionState(&action);
    item_receipt_method = saved_receipt_method;
    link_receiveitem_index = saved_receive_index;
    return false;
  }

  ItemReceipt_ApplyDeferredEffects(item);

  // A full animated receipt refreshes equipped gear after the inventory write.
  // Quiet delivery must do the same or a sword/shield pickup renders the old
  // tier until the next room/load transition. Receive code 0 grants both the
  // Fighter Sword and Fighter Shield, so it needs both refreshes.
  uint8 gfx = kReceiveItemGfx[item];
  if (gfx == 0x20 || gfx == 0x2d || gfx == 0x2e || item == 0) {
    DecompressShieldGraphics();
    Palette_Load_Shield();
  }
  if (gfx == 6 || gfx == 0x18) {
    DecompressSwordGraphics();
    Palette_Load_Sword();
  }

  ItemReceipt_RestoreActionState(&action);
  item_receipt_method = 0;
  link_receiveitem_index = 0;
  return true;
}

// Complete the action-state portion of a method-3 falling-prize receipt after
// quiet delivery. This mirrors Ancilla22_ItemReceipt's label_a/final teardown:
// return Link to Ground (or Swimming), release receipt-owned pose/damage/
// immobilization, and perform the same indoor boss-exit handoff. The inventory
// and deferred item effects have already been applied by the caller.
void ItemReceipt_CompleteMethod3WithoutAnimation(uint8 item) {
  link_player_handler_state =
      link_is_in_deep_water ? kPlayerState_Swimming : kPlayerState_Ground;
  link_receiveitem_index = 0;
  link_pose_for_item = 0;
  link_disable_sprite_damage = 0;
  item_receipt_method = 0;
  if (player_is_indoors && item != 0x10 && item != 0x26 &&
      item != 0x0f && item != 0x20)
    PrepareDungeonExitFromBossFight();
  flag_is_link_immobilized = 0;
}

// Fast fanfare shortens a chest receipt from 0x38 to 0x18 frames. Vanilla
// chest rupees are deferred until Ancilla22_ItemReceipt sees the timer at
// 0x28, so a shortened receipt can never reach the grant point. Apply that
// one deferred side effect when the timer is shortened; the normal receipt
// path still owns every other method and the non-fast chest path.
static void ItemReceipt_ApplyFastFanfare(int ancilla, uint8 item,
                                        bool emulator_attached) {
  bool fast_fanfare =
      (enhanced_features0 & kFeatures0_FastFanfare) && !emulator_attached;
  bool is_prize = item == 0x20 || item == 0x37 || item == 0x38 || item == 0x39;
  bool fast_boss_prize = ItemReceipt_IsFastBossPrize(
      item, item_receipt_method, player_is_indoors != 0, fast_fanfare);
  if (fast_fanfare && (!is_prize || fast_boss_prize) &&
      ancilla_aux_timer[ancilla] > 0x18) {
    if (item_receipt_method == 1)
      Ancilla_AddRupees(ancilla);
    ancilla_aux_timer[ancilla] = 0x18;
  }
}

bool ItemReceipt_IsFastBossPrize(uint8 item, uint8 receipt_method,
                                 bool indoors, bool fast_fanfare) {
  bool is_prize = item == 0x20 || item == 0x37 || item == 0x38 || item == 0x39;
  return fast_fanfare && indoors && receipt_method == 3 && is_prize;
}

bool ItemReceipt_ShouldWaitForPendantFanfare(uint8 item, uint8 apu_busy,
                                             bool fast_boss_prize) {
  bool is_pendant = item == 0x37 || item == 0x38 || item == 0x39;
  return is_pendant && apu_busy != 0 && !fast_boss_prize;
}

uint8 ItemReceipt_TickCrystalFanfare(uint8 timer, bool fast_boss_prize) {
  // Crystal receipts set flag_is_link_immobilized=2, which deliberately skips
  // Ancilla22_ItemReceipt's normal aux-timer decrement while vanilla waits for
  // the APU fanfare to finish. Give the fast boss-prize path its own countdown
  // so its shortened 0x18-frame hold can actually reach the handoff threshold.
  return fast_boss_prize && timer > 1 ? timer - 1 : timer;
}

bool ItemReceipt_ShouldAdvanceCrystalFanfare(uint8 apu_busy, uint8 timer,
                                             bool fast_boss_prize) {
  return apu_busy == 0 || (fast_boss_prize && timer <= 1);
}

void ItemReceipt_FastFanfareSelfCheck(void) {
  uint32 saved_features0 = enhanced_features0;
  uint8 saved_method = item_receipt_method;
  uint8 saved_timer = ancilla_aux_timer[0];
  uint8 saved_item = ancilla_item_to_link[0];
  uint16 saved_rupees = link_rupees_goal;
  uint8 saved_indoors = player_is_indoors;

  enhanced_features0 |= kFeatures0_FastFanfare;
  item_receipt_method = 1;
  static const struct { uint8 item; uint16 amount; } kRupees[] = {
    {0x34, 1}, {0x35, 5}, {0x36, 20}, {0x40, 100},
    {0x41, 50}, {0x46, 300}, {0x47, 20},
  };
  for (size_t i = 0; i < sizeof(kRupees) / sizeof(kRupees[0]); i++) {
    ancilla_item_to_link[0] = kRupees[i].item;
    ancilla_aux_timer[0] = 0x38;
    link_rupees_goal = 20;
    ItemReceipt_ApplyFastFanfare(0, kRupees[i].item, false);
    if (ancilla_aux_timer[0] != 0x18 ||
        link_rupees_goal != 20 + kRupees[i].amount) {
      fprintf(stderr,
              "ItemReceipt_FastFanfareSelfCheck: fast chest rupees 0x%02x were lost\n",
              kRupees[i].item);
      exit(2);
    }
  }

  // Non-chest receipts grant rupees at their normal completion point, so the
  // fast setup must not pre-grant those too.
  item_receipt_method = 0;
  ancilla_item_to_link[0] = 0x40;
  ancilla_aux_timer[0] = 0x60;
  link_rupees_goal = 20;
  ItemReceipt_ApplyFastFanfare(0, 0x40, false);
  if (ancilla_aux_timer[0] != 0x18 || link_rupees_goal != 20) {
    fprintf(stderr, "ItemReceipt_FastFanfareSelfCheck: pickup rupees double-granted\n");
    exit(2);
  }

  // Side-by-side comparison suppresses FastFanfare and must retain the
  // vanilla timer/grant schedule.
  item_receipt_method = 1;
  ancilla_aux_timer[0] = 0x38;
  link_rupees_goal = 20;
  ItemReceipt_ApplyFastFanfare(0, 0x40, true);
  if (ancilla_aux_timer[0] != 0x38 || link_rupees_goal != 20) {
    fprintf(stderr, "ItemReceipt_FastFanfareSelfCheck: emulator path diverged\n");
    exit(2);
  }

  // Pendant receipts use the same short timer, then bypass the vanilla APU
  // completion hold. Their prize bits and dungeon exit remain in the normal
  // receipt-completion path; only the audio wait is optional.
  item_receipt_method = 3;
  player_is_indoors = 1;
  ancilla_aux_timer[0] = 0x68;
  ItemReceipt_ApplyFastFanfare(0, 0x37, false);
  bool fast_pendant = ItemReceipt_IsFastBossPrize(0x37, 3, true, true);
  if (ancilla_aux_timer[0] != 0x18 || !fast_pendant ||
      ItemReceipt_ShouldWaitForPendantFanfare(0x37, 1, fast_pendant) ||
      !ItemReceipt_ShouldWaitForPendantFanfare(0x37, 1, false) ||
      ItemReceipt_ShouldWaitForPendantFanfare(0x37, 0, false) ||
      ItemReceipt_ShouldWaitForPendantFanfare(0x20, 1, false) ||
      ItemReceipt_IsFastBossPrize(0x37, 3, false, true) ||
      ItemReceipt_IsFastBossPrize(0x37, 1, true, true) ||
      ItemReceipt_IsFastBossPrize(0x37, 3, true, false)) {
    fprintf(stderr, "ItemReceipt_FastFanfareSelfCheck: pendant wait policy failed\n");
    exit(2);
  }
  ancilla_aux_timer[0] = 0x68;
  ItemReceipt_ApplyFastFanfare(0, 0x20, false);
  if (ancilla_aux_timer[0] != 0x18 ||
      ItemReceipt_TickCrystalFanfare(0x18, true) != 0x17 ||
      ItemReceipt_TickCrystalFanfare(1, true) != 1 ||
      ItemReceipt_TickCrystalFanfare(0x18, false) != 0x18 ||
      ItemReceipt_ShouldAdvanceCrystalFanfare(1, 2, true) ||
      !ItemReceipt_ShouldAdvanceCrystalFanfare(1, 1, true) ||
      ItemReceipt_ShouldAdvanceCrystalFanfare(1, 1, false) ||
      !ItemReceipt_ShouldAdvanceCrystalFanfare(0, 2, false)) {
    fprintf(stderr, "ItemReceipt_FastFanfareSelfCheck: crystal policy failed\n");
    exit(2);
  }
  player_is_indoors = 0;
  ancilla_aux_timer[0] = 0x68;
  ItemReceipt_ApplyFastFanfare(0, 0x37, false);
  if (ancilla_aux_timer[0] != 0x68) {
    fprintf(stderr, "ItemReceipt_FastFanfareSelfCheck: outdoor prize was shortened\n");
    exit(2);
  }

  enhanced_features0 = saved_features0;
  item_receipt_method = saved_method;
  ancilla_aux_timer[0] = saved_timer;
  ancilla_item_to_link[0] = saved_item;
  link_rupees_goal = saved_rupees;
  player_is_indoors = saved_indoors;
  fprintf(stderr, "[ItemReceipt_FastFanfareSelfCheck] OK\n");
}

static void ItemReceipt_LosslessSelfCheckFail(const uint8 *saved_ram,
                                               const char *message) {
  memcpy(g_ram, saved_ram, sizeof(g_ram));
  fprintf(stderr, "ItemReceipt_LosslessSelfCheck: %s\n", message);
  exit(2);
}

void ItemReceipt_LosslessSelfCheck(void) {
  static uint8 saved_ram[sizeof(g_ram)];
  static uint8 before_ram[sizeof(g_ram)];
  memcpy(saved_ram, g_ram, sizeof(g_ram));

  // The read-only capacity predicate must match Ancilla_AllocInit for every
  // allocator rotation and every possible position/type of evictable entry.
  static const uint8 kEvictableTypes[] = {0x3c, 0x13, 0x0a};
  for (int rotate = 0; rotate < 5; rotate++) {
    for (int slot = 0; slot < 5; slot++) {
      for (size_t type = 0;
           type < sizeof(kEvictableTypes) / sizeof(kEvictableTypes[0]);
           type++) {
        for (int i = 0; i < 5; i++)
          ancilla_type[i] = 1;
        ancilla_type[slot] = kEvictableTypes[type];
        ancilla_alloc_rotate = (uint8)rotate;
        bool capacity = ItemReceipt_CanAllocate();
        ancilla_alloc_rotate = (uint8)rotate;
        bool allocator = Ancilla_AllocInit(0x22, 4) >= 0;
        if (capacity != allocator)
          ItemReceipt_LosslessSelfCheckFail(
              saved_ram, "capacity predicate diverged from allocator");
      }
    }
  }

  for (int i = 0; i < 5; i++)
    ancilla_type[i] = 1;
  ancilla_alloc_rotate = 0;
  if (ItemReceipt_CanAllocate())
    ItemReceipt_LosslessSelfCheckFail(
        saved_ram, "full non-evictable pool reported capacity");
  ancilla_type[3] = 0;
  if (!ItemReceipt_CanAllocate())
    ItemReceipt_LosslessSelfCheckFail(saved_ram, "free slot was rejected");
  for (int i = 0; i < 5; i++)
    ancilla_type[i] = 0x22;
  if (ItemReceipt_CanAllocate())
    ItemReceipt_LosslessSelfCheckFail(
        saved_ram, "five same-type receipts bypassed the allocator limit");

  // Rupee amounts are normally deferred to Ancilla_AddRupees. Exercise every
  // receive code through the shared no-animation path.
  static const struct {
    uint8 item;
    uint16 amount;
  } kRupees[] = {
      {0x34, 1}, {0x35, 5}, {0x36, 20}, {0x40, 100},
      {0x41, 50}, {0x46, 300}, {0x47, 20},
  };
  for (size_t i = 0; i < sizeof(kRupees) / sizeof(kRupees[0]); i++) {
    memcpy(g_ram, saved_ram, sizeof(g_ram));
    enhanced_features1 |= kFeatures1_RandomizerActive;
    link_rupees_goal = 20;
    if (!ItemReceipt_GrantWithoutAnimation(kRupees[i].item) ||
        link_rupees_goal != 20 + kRupees[i].amount)
      ItemReceipt_LosslessSelfCheckFail(
          saved_ram, "quiet rupee deferred effect mismatch");
  }

  // Heart containers, the fourth heart piece, and refill items all finish in
  // Ancilla22_ItemReceipt rather than the immediate inventory table.
  static const uint8 kCapacityItems[] = {0x26, 0x3f};
  for (size_t i = 0;
       i < sizeof(kCapacityItems) / sizeof(kCapacityItems[0]); i++) {
    memcpy(g_ram, saved_ram, sizeof(g_ram));
    enhanced_features1 |= kFeatures1_RandomizerActive;
    link_health_capacity = 0x40;  // rando-exempt: lossless-receipt self-check fixture
    link_health_current = 0x30;
    link_hearts_filler = 2;
    if (!ItemReceipt_GrantWithoutAnimation(kCapacityItems[i]) ||
        link_health_capacity != 0x48 || link_hearts_filler != 0x1a)
      ItemReceipt_LosslessSelfCheckFail(
          saved_ram, "quiet heart-container deferred effect mismatch");
  }

  memcpy(g_ram, saved_ram, sizeof(g_ram));
  enhanced_features1 |= kFeatures1_RandomizerActive;
  link_health_capacity = 0x40;  // rando-exempt: lossless-receipt self-check fixture
  link_hearts_filler = 2;
  link_state_bits = 0x80;
  link_picking_throw_state = 0x5a;
  ItemReceiptActionSnapshot before_action = ItemReceipt_CaptureActionState();
  if (!ItemReceipt_GrantWithoutAnimation(0x3e) ||
      link_health_capacity != 0x48 || link_hearts_filler != 0x0a)
    ItemReceipt_LosslessSelfCheckFail(
        saved_ram, "quiet boss-heart deferred effect mismatch");
  ItemReceiptActionSnapshot after_action = ItemReceipt_CaptureActionState();
  if (memcmp(&before_action, &after_action, sizeof(before_action)) != 0)
    ItemReceipt_LosslessSelfCheckFail(
        saved_ram, "quiet delivery changed Link action state");

  memcpy(g_ram, saved_ram, sizeof(g_ram));
  enhanced_features1 |= kFeatures1_RandomizerActive;
  link_health_capacity = 0x40;  // rando-exempt: lossless-receipt self-check fixture
  link_health_current = 0x30;
  link_hearts_filler = 2;
  link_heart_pieces = 3;  // rando-exempt: lossless-receipt self-check fixture
  if (!ItemReceipt_GrantWithoutAnimation(0x17) ||
      link_heart_pieces != 0 || link_health_capacity != 0x48 ||
      link_hearts_filler != 0x1a)
    ItemReceipt_LosslessSelfCheckFail(
        saved_ram, "quiet fourth-heart-piece rollover mismatch");

  memcpy(g_ram, saved_ram, sizeof(g_ram));
  enhanced_features1 |= kFeatures1_RandomizerActive;
  link_hearts_filler = 1;
  link_magic_filler = 2;
  if (!ItemReceipt_GrantWithoutAnimation(0x42) || link_hearts_filler != 9 ||
      !ItemReceipt_GrantWithoutAnimation(0x45) || link_magic_filler != 18)
    ItemReceipt_LosslessSelfCheckFail(
        saved_ram, "quiet heart/magic refill mismatch");

  // Drive the production Link_ReceiveItem saturation branch. Its only allowed
  // state changes are the delivered inventory/resource, receipt feedback, and
  // receipt-global cleanup; occupied ancillae and Link action state stay put.
  memcpy(g_ram, saved_ram, sizeof(g_ram));
  enhanced_features1 |= kFeatures1_RandomizerActive;
  for (int i = 0; i < 5; i++)
    ancilla_type[i] = 1;
  item_receipt_method = 0;
  uint8 occupied[5];
  memcpy(occupied, ancilla_type, sizeof(occupied));
  flag_is_link_immobilized = 0x41;
  flag_is_sprite_to_pick_up = 0x42;
  flag_is_ancilla_to_pick_up = 0x43;
  flag_is_sprite_to_pick_up_cached = 0x44;
  byte_7E0FB2 = 0x45;
  player_handler_timer = 0x46;
  link_item_in_hand = 0x47;
  link_state_bits = 0x48;
  link_picking_throw_state = 0x49;
  some_animation_timer_steps = 0x4a;
  some_animation_timer = 0x4b;
  button_mask_b_y = 0x4c;
  bitfield_for_a_button = 0x4d;
  button_b_frames = 0x4e;
  link_speed_setting = 0x4f;
  link_cant_change_direction = 0x50;
  link_player_handler_state = 0x51;
  link_pose_for_item = 0x52;
  link_position_mode = 0x53;
  link_disable_sprite_damage = 0x54;
  link_auxiliary_state = 0x55;
  link_incapacitated_timer = 0x56;
  countdown_for_blink = 0x57;
  link_receiveitem_var1 = 0x58;
  link_is_running = 1;
  link_countdown_for_dash = 0x59;
  before_action = ItemReceipt_CaptureActionState();
  link_rupees_goal = 20;
  overworld_map_state = 9;
  sound_effect_2 = 0;
  Link_ReceiveItem(0x34, 0);
  after_action = ItemReceipt_CaptureActionState();
  if (link_rupees_goal != 21 ||
      memcmp(occupied, ancilla_type, sizeof(occupied)) != 0 ||
      memcmp(&before_action, &after_action, sizeof(before_action)) != 0 ||
      item_receipt_method != 0 || link_receiveitem_index != 0 ||
      (sound_effect_2 & 0x3f) != 0x0f || overworld_map_state != 0)
    ItemReceipt_LosslessSelfCheckFail(
        saved_ram, "active saturated receipt was not lossless");

  // Saturated heart-container delivery completes immediately, so its audible
  // result must be the normal completion cue rather than the generic pickup
  // chime. Boss-heart code 0x3e uses the same completion cue after its onset.
  static const uint8 kHeartContainerItems[] = {0x26, 0x3e};
  for (size_t i = 0; i < countof(kHeartContainerItems); i++) {
    memcpy(g_ram, saved_ram, sizeof(g_ram));
    enhanced_features1 |= kFeatures1_RandomizerActive;
    for (int j = 0; j < 5; j++)
      ancilla_type[j] = 1;
    link_health_capacity = 0x40;  // rando-exempt: lossless-receipt self-check fixture
    link_health_current = 0x30;
    link_hearts_filler = 0;
    sound_effect_2 = 0;
    Link_ReceiveItem(kHeartContainerItems[i], 0);
    if (link_health_capacity != 0x48 ||
        (sound_effect_2 & 0x3f) != 0x0d)
      ItemReceipt_LosslessSelfCheckFail(
          saved_ram, "saturated heart-container feedback mismatch");
  }

  // At capacity, 0x3e never reaches the completion cue; preserve its normal
  // boss-heart onset sound instead of inventing the generic receipt chime.
  memcpy(g_ram, saved_ram, sizeof(g_ram));
  enhanced_features1 |= kFeatures1_RandomizerActive;
  for (int i = 0; i < 5; i++)
    ancilla_type[i] = 1;
  link_health_capacity = 0xa0;  // rando-exempt: lossless-receipt self-check fixture
  sound_effect_2 = 0;
  Link_ReceiveItem(0x3e, 0);
  if ((sound_effect_2 & 0x3f) != 0x2e)
    ItemReceipt_LosslessSelfCheckFail(
        saved_ram, "at-cap saturated boss-heart feedback mismatch");

  // A saturated falling-prize handoff (method 3) must perform the same action
  // teardown as a completed animated receipt.
  memcpy(g_ram, saved_ram, sizeof(g_ram));
  enhanced_features1 |= kFeatures1_RandomizerActive;
  for (int i = 0; i < 5; i++)
    ancilla_type[i] = 1;
  item_receipt_method = 3;
  player_is_indoors = 0;
  flag_is_link_immobilized = 1;
  link_pose_for_item = 2;
  link_disable_sprite_damage = 1;
  link_player_handler_state = 0x66;
  link_is_in_deep_water = 0;
  link_rupees_goal = 20;
  Link_ReceiveItem(0x34, 0);
  if (link_rupees_goal != 21 || flag_is_link_immobilized != 0 ||
      link_pose_for_item != 0 || link_disable_sprite_damage != 0 ||
      link_player_handler_state != kPlayerState_Ground ||
      item_receipt_method != 0 || link_receiveitem_index != 0)
    ItemReceipt_LosslessSelfCheckFail(
        saved_ram, "saturated falling receipt did not release Link");

  memcpy(g_ram, saved_ram, sizeof(g_ram));
  enhanced_features1 |= kFeatures1_RandomizerActive;
  for (int i = 0; i < 5; i++)
    ancilla_type[i] = 1;
  item_receipt_method = 3;
  player_is_indoors = 0;
  link_is_in_deep_water = 1;
  link_player_handler_state = 0x66;
  link_rupees_goal = 20;
  Link_ReceiveItem(0x34, 0);
  if (link_rupees_goal != 21 ||
      link_player_handler_state != kPlayerState_Swimming ||
      item_receipt_method != 0 || link_receiveitem_index != 0)
    ItemReceipt_LosslessSelfCheckFail(
        saved_ram, "saturated falling receipt did not restore swimming");

  // Bottle acquisition needs a free bottle slot; loose contents need an
  // existing empty bottle. Both failures must report retry without mutation.
  memcpy(g_ram, saved_ram, sizeof(g_ram));
  enhanced_features1 |= kFeatures1_RandomizerActive;
  memset(link_bottle_info, 3, 4);
  item_receipt_method = 3;
  link_receiveitem_index = 0x16;
  uint8 bottles_before[4];
  memcpy(bottles_before, link_bottle_info, sizeof(bottles_before));
  if (ItemReceipt_GrantWithoutAnimation(0x16) ||
      memcmp(bottles_before, link_bottle_info, sizeof(bottles_before)) != 0 ||
      item_receipt_method != 3 || link_receiveitem_index != 0x16)
    ItemReceipt_LosslessSelfCheckFail(
        saved_ram, "full bottle acquisition was not retryable");
  if (ItemReceipt_GrantWithoutAnimation(0x2e) ||
      memcmp(bottles_before, link_bottle_info, sizeof(bottles_before)) != 0)
    ItemReceipt_LosslessSelfCheckFail(
        saved_ram, "bottle contents without an empty slot were not retryable");
  // rando-exempt: lossless-receipt self-check fixture
  link_bottle_info[2] = 2;
  if (!ItemReceipt_GrantWithoutAnimation(0x2e) || link_bottle_info[2] != 3)
    ItemReceipt_LosslessSelfCheckFail(
        saved_ram, "bottle contents with an empty slot were rejected");

  // Link_ReceiveItem must use the same acceptance predicate before entering
  // its hold-up state or allocating an otherwise-free receipt ancilla. A full
  // bottle inventory is therefore a byte-for-byte no-op in an active slot.
  memcpy(g_ram, saved_ram, sizeof(g_ram));
  enhanced_features1 |= kFeatures1_RandomizerActive;
  memset(link_bottle_info, 3, 4);
  memset(ancilla_type, 0, 5);
  item_receipt_method = 0;
  link_receiveitem_index = 0x44;
  memcpy(before_ram, g_ram, sizeof(g_ram));
  Link_ReceiveItem(0x16, 0);
  if (memcmp(before_ram, g_ram, sizeof(g_ram)) != 0)
    ItemReceipt_LosslessSelfCheckFail(
        saved_ram, "full-bottle Link_ReceiveItem mutated state");

  // Preserve the legacy non-randomizer behavior: a full bottle receipt is an
  // accepted animated no-op rather than a new failure outcome.
  memcpy(g_ram, saved_ram, sizeof(g_ram));
  enhanced_features1 &= ~kFeatures1_RandomizerActive;
  memset(link_bottle_info, 3, 4);
  if (!ItemReceipt_GrantInventory(0x16))
    ItemReceipt_LosslessSelfCheckFail(
        saved_ram, "randomizer-inactive full bottle behavior changed");

  // With the randomizer bit clear, the same saturated call retains the legacy
  // behavior: Link enters the hold-up state and the unallocated item is not
  // granted. This guards the RAM-compare path from the new fallback.
  memcpy(g_ram, saved_ram, sizeof(g_ram));
  enhanced_features1 &= ~kFeatures1_RandomizerActive;
  for (int i = 0; i < 5; i++)
    ancilla_type[i] = 1;
  item_receipt_method = 0;
  memcpy(occupied, ancilla_type, sizeof(occupied));
  link_rupees_goal = 20;
  link_is_running = 1;
  link_countdown_for_dash = 0x59;
  Link_ReceiveItem(0x34, 0);
  if (link_rupees_goal != 20 ||
      memcmp(occupied, ancilla_type, sizeof(occupied)) != 0 ||
      link_player_handler_state != kPlayerState_HoldUpItem ||
      link_disable_sprite_damage != 1 || link_receiveitem_index != 0x34 ||
      link_receiveitem_var1 != 0x60 || link_is_running != 0 ||
      link_countdown_for_dash != 0)
    ItemReceipt_LosslessSelfCheckFail(
        saved_ram, "randomizer-inactive saturated receipt changed behavior");

  memcpy(g_ram, saved_ram, sizeof(g_ram));
  fprintf(stderr, "[ItemReceipt_LosslessSelfCheck] OK\n");
}

bool AncillaAdd_ItemReceipt(uint8 ain, uint8 yin, int chest_pos) {  // 8985e8
  int j = link_receiveitem_index;
  if (j >= 76)
    return false;

  int ancilla = Ancilla_AddAncilla(ain, yin);
  if (ancilla < 0)
    return false;

  // A vanilla pickup repaints the shared receive-item VRAM slot (chars
  // 0x24/0x34) via DecodeAnimatedSpriteTile_variable further down. Under rando,
  // a direct-grant confirmation icon (kAncillaType_RandoIconReceipt) may still
  // be floating from a recent silent grant; it draws from that same slot and
  // would abruptly render this pickup's tiles. Retire it so it never shows the
  // wrong item. Type 0x44 never exists outside rando, so this is a no-op in the
  // vanilla side-by-side path.
  if (enhanced_features1 & kFeatures1_RandomizerActive) {
    for (int i = 0; i < 5; i++) {
      if (ancilla_type[i] == kAncillaType_RandoIconReceipt)
        ancilla_type[i] = 0;
    }
  }

  // Reject BEFORE taking the lock. This return was unreachable until the
  // rando acceptance term (ItemReceipt_CanAccept, a bottle with no compatible
  // slot) made ItemReceipt_GrantInventory able to fail for real. Running it
  // after Ancilla_AddAncilla + the immobilize set leaks a half-built 0x22
  // receipt -- ancilla_item_to_link, ancilla_step and the timers are all set
  // below -- so Ancilla22_ItemReceipt would then run on the previous
  // occupant's fields with Link locked. The sole caller happens to pre-check
  // the same predicate today, so this is latent; the predicate is DUPLICATED,
  // not shared, and a second caller would make it a hard freeze.
  if (!ItemReceipt_GrantInventory((uint8)j)) {
    ancilla_type[ancilla] = 0;
    return false;
  }

  flag_is_link_immobilized = (link_receiveitem_index == 0x20) ? 2 : 1;

  uint8 gfx = kReceiveItemGfx[j];
  if (gfx == 0xff) {
    gfx = 0;
  } else if (gfx == 0x20 || gfx == 0x2d || gfx == 0x2e) {
    DecompressShieldGraphics();
    Palette_Load_Shield();
  }
  DecodeAnimatedSpriteTile_variable(gfx);

  if ((gfx == 6 || gfx == 0x18) && j != 0) {
    DecompressSwordGraphics();
    Palette_Load_Sword();
  }

  ancilla_item_to_link[ancilla] = j;
  ancilla_arr1[ancilla] = 0;

  if (j == 1 && item_receipt_method != 2 && Mods_RomByte(0x8987ca, 0xd0) == 0xd0 &&
      !(enhanced_features1 & kFeatures1_RandomizerActive)) {
    ancilla_timer[ancilla] = 160;
    submodule_index = 43;
    BYTE(palette_filter_countdown) = 0;
    AncillaAdd_MSCutscene(0x35, 4);
    ancilla_arr3[ancilla] = 2;
  } else {
    ancilla_arr3[ancilla] = 9;
  }
  ancilla_arr4[ancilla] = 5;
  ancilla_step[ancilla] = item_receipt_method;

  ancilla_aux_timer[ancilla] =
    (j == 0x20 || j == 0x37 || j == 0x38 || j == 0x39) ? 0x68 :
    (j == 0x26) ? 0x2 : (item_receipt_method ? 0x38 : 0x60);
  ItemReceipt_ApplyFastFanfare(ancilla, (uint8)j,
                              ZeldaIsEmulatorAttached());

  int x, y;

  if (item_receipt_method == 1) {
    y = (chest_pos & 0x1f80) >> 4;
    x = (chest_pos & 0x7e) << 2;
    y += dung_loade_bgoffs_v_copy & ~0xff;
    x += dung_loade_bgoffs_h_copy & ~0xff;
    y += kReceiveItem_Tab2[j];
    x += kReceiveItem_Tab3[j];
  } else {
    if (ancilla_step[ancilla] == 0 && j == 1 && Mods_RomByte(0x898886, 0xd0) == 0xd0) {
      sound_effect_1 = Link_CalculateSfxPan() | 0x2c;
    } else if (j == 0x20 || j == 0x37 || j == 0x38 || j == 0x39) {
      music_control = Link_CalculateSfxPan() | 0x13;
    } else if (j != 0x3e && j != 0x17) {
      sound_effect_2 = Link_CalculateSfxPan() | 0xf;
    }
    int method = item_receipt_method == 3 ? 0 : item_receipt_method;
    x = (method != 0) ? kReceiveItem_Tab3[j] :
      (kReceiveItem_Tab1[j] == 0) ? 10 : (j == 0x20) ? 0 : 6;
    x += link_x_coord;
    y = method ? kReceiveItem_Tab2[j] : -14;
    y += link_y_coord + ((method == 2) ? -8 : 0);
  }
  Ancilla_SetXY(ancilla, x, y);
  return true;
}

void ItemReceipt_GiveBottledItem(uint8 item) {  // 89893e
  int j;
  if ((j = FindInByteArray(kItemReceiptBottleList, item,
                           countof(kItemReceiptBottleList))) >= 0) {
    for (int i = 0; i != 4; i++) {
      if (link_bottle_info[i] < 2) {
        // rando-exempt: dispatcher-core — ItemReceipt_GiveBottledItem is
        // called from AncillaAdd_ItemReceipt, the central dispatch helper.
        // §6 dispatch wraps Link_ReceiveItem call sites, not this internal
        // helper. (audit.md §0.1.1 dispatcher funnel)
        link_bottle_info[i] = j + 2;
        return;
      }
    }
  }
  if ((j = FindInByteArray(kItemReceiptPotionList, item,
                           countof(kItemReceiptPotionList))) >= 0) {
    for (int i = 0; i != 4; i++) {
      if (link_bottle_info[i] == 2) {
        // rando-exempt: dispatcher-core — see above. (audit.md §0.1.1)
        link_bottle_info[i] = j + 3;
        return;
      }
    }
  }
}

void Module17_SaveAndQuit() {  // 89f79f
  switch (submodule_index) {
  case 0:
    submodule_index++;
  case 1:
    if (!--INIDISP_copy) {
      MOSAIC_copy = 15;
      subsubmodule_index = 1;
      Death_Func15(false);
    }
    break;
  }
  Sprite_Main();
  LinkOam_Main();
}

void WallMaster_SendPlayerToLastEntrance() {  // 8bffa8
  SaveDungeonKeys();
  Dungeon_FlagRoomData_Quadrants();
  Sprite_ResetAll();
  death_var4 = 0;
  main_module_index = 17;
  submodule_index = 0;
  nmi_load_bg_from_vram = 0;
  ResetSomeThingsAfterDeath(17);  // wtf: argument?
}

uint8 GetRandomNumber() {  // 8dba71
  uint8 t = byte_7E0FA1 + frame_counter;
  t = (t & 1) ? (t >> 1) : (t >> 1) ^ 0xb8;
  byte_7E0FA1 = t;
  return t;
}

uint8 Link_CalculateSfxPan() {  // 8dbb67
  return CalculateSfxPan(link_x_coord);
}

void SpriteSfx_QueueSfx1WithPan(int k, uint8 a) {  // 8dbb6e
  if (sound_effect_ambient == 0)
    sound_effect_ambient = a | Sprite_CalculateSfxPan(k);
}

void SpriteSfx_QueueSfx2WithPan(int k, uint8 a) {  // 8dbb7c
  if (sound_effect_1 == 0)
    sound_effect_1 = a | Sprite_CalculateSfxPan(k);
}

void SpriteSfx_QueueSfx3WithPan(int k, uint8 a) {  // 8dbb8a
  if (sound_effect_2 == 0)
    sound_effect_2 = a | Sprite_CalculateSfxPan(k);
}

uint8 Sprite_CalculateSfxPan(int k) {  // 8dbba1
  return CalculateSfxPan(Sprite_GetX(k));
}

uint8 CalculateSfxPan(uint16 x) {  // 8dbba8
  static const uint8 kPanTable[] = { 0, 0x80, 0x40 };
  int o = 0;
  x -= BG2HOFS_copy2 + 80;
  if (x >= 80)
    o = 1 + ((int16)x >= 0);
  return kPanTable[o];
}

uint8 CalculateSfxPan_Arbitrary(uint8 a) {  // 8dbbd0
  static const uint8 kTorchPans[] = { 0x80, 0x80, 0x80, 0, 0, 0x40, 0x40, 0x40 };
  return kTorchPans[((a - BG2HOFS_copy2) >> 5) & 7];
}

void Init_LoadDefaultTileAttr() {  // 8e97d9
  memcpy(attributes_for_tile, kDungeon_DefaultAttr, 0x140);
  memcpy(attributes_for_tile + 0x1c0, kDungeon_DefaultAttr + 0x140, 64);
}

void Main_ShowTextMessage() {  // 8ffdaa
  if (main_module_index != 14) {
    byte_7E0223 = 0;
    messaging_module = 0;
    submodule_index = 2;
    saved_module_for_menu = main_module_index;
    main_module_index = 14;
  }
}

uint8 HandleItemTileAction_Overworld(uint16 x, uint16 y) {  // 9bbd7a
  if (player_is_indoors)
    return HandleItemTileAction_Dungeon(x, y);
  else
    return Overworld_ToolAndTileInteraction(x, y);
}
