#include "overworld.h"
#include "qol_shortcut.h"
#include "localized.h"
#include "mods.h"
#include "hud.h"
#include "load_gfx.h"
#include "dungeon.h"
#include "tagalong.h"
#include "sprite.h"
#include "ancilla.h"
#include "player.h"
#include "misc.h"
#include "messaging.h"
#include "player_oam.h"
#include "snes/snes_regs.h"
#include "assets.h"
#include "features.h"   // enhanced_features1 / kFeatures1_RandomizerActive
#include "zelda_rtl.h"
#include "rando/rando.h"  // Phase B Slice 1 §38 — Rando_BumpReachabilityCounter
#include "rando/location_ids.h"
#include "rando/shuffle_ow_warp.h"  // add-rando-ow-warp-shuffle runtime hooks
#include "rando/chains_runtime.h"
#include "rando/location_ids.h"  // LOC_Hammer_Pegs (Phase B Slice 8 §67/#79)
#include "rando/item_ids.h"      // ITEM_PieceOfHeart
#include "rando/inverted_maps.h" // Overworld_ApplyInvertedTiles (#82 Inverted topology)
#include "rando/medallion_icons.h"  // Rando_PatchMedallionEntranceMap8
#include <stdio.h>  // Overworld_DumpTerrainTable (--dump-terrain-table)

static bool CutsceneFastForwardEnabled(void) {
  return (enhanced_features0 & kFeatures0_CutsceneFastForward) &&
         !ZeldaIsEmulatorAttached();
}

const uint16 kOverworld_OffsetBaseX[64] = {
  0,     0, 0x400, 0x600, 0x600, 0xa00, 0xa00, 0xe00,
  0,     0, 0x400, 0x600, 0x600, 0xa00, 0xa00, 0xe00,
  0, 0x200, 0x400, 0x600, 0x800, 0xa00, 0xc00, 0xe00,
  0,     0, 0x400, 0x600, 0x600, 0xa00, 0xc00, 0xc00,
  0,     0, 0x400, 0x600, 0x600, 0xa00, 0xc00, 0xc00,
  0, 0x200, 0x400, 0x600, 0x800, 0xa00, 0xc00, 0xe00,
  0,     0, 0x400, 0x600, 0x800, 0xa00, 0xa00, 0xe00,
  0,     0, 0x400, 0x600, 0x800, 0xa00, 0xa00, 0xe00,
};
const uint16 kOverworld_OffsetBaseY[64] = {
      0,     0,     0,     0,     0,     0,     0,     0,
      0,     0, 0x200,     0,     0,     0,     0, 0x200,
  0x400, 0x400, 0x400, 0x400, 0x400, 0x400, 0x400, 0x400,
  0x600, 0x600, 0x600, 0x600, 0x600, 0x600, 0x600, 0x600,
  0x600, 0x600, 0x800, 0x600, 0x600, 0x800, 0x600, 0x600,
  0xa00, 0xa00, 0xa00, 0xa00, 0xa00, 0xa00, 0xa00, 0xa00,
  0xc00, 0xc00, 0xc00, 0xc00, 0xc00, 0xc00, 0xc00, 0xc00,
  0xc00, 0xc00, 0xe00, 0xe00, 0xe00, 0xc00, 0xc00, 0xe00,
};
// The special areas (screens 0x80 and up) index the two tables above with
// 0x80 to 0xbf. On the SNES that reads past both, into the code that follows
// kOverworld_OffsetBaseX ($02:A9C4 and $02:AA44). These are those words, so
// that the result is the SNES one instead of whatever follows the arrays;
// entering a special area then sets both bases from kSpExit_Top and
// kSpExit_LeftEdgeOfMap.
static const uint16 kOverworld_OffsetBaseY_Special[64] = {
  0x16ad, 0xf004, 0x2003, 0xf273, 0x20c2, 0x30a5, 0xff29, 0xf000,
  0xa523, 0x2967, 0x000c, 0x0085, 0x00ae, 0xa507, 0x3820, 0xc4ff,
  0x02a8, 0x06a0, 0x08a2, 0x04c9, 0x9000, 0xa03b, 0xa204, 0xcd04,
  0x0716, 0x32b0, 0x31a5, 0xff29, 0xf000, 0xad2f, 0x0716, 0x6918,
  0x0004, 0x0285, 0x67a5, 0x0329, 0x8500, 0xae00, 0x0700, 0x22a5,
  0xff38, 0xa944, 0xa002, 0xa202, 0xc902, 0x0006, 0x0890, 0x00a0,
  0x01a2, 0x02c5, 0x0490, 0x00e4, 0x05f0, 0x4922, 0x0ede, 0xe260,
  0x2220, 0xf439, 0xb007, 0x84f3, 0x6402, 0x2003, 0x8b0c, 0x31c2,
};
static const uint16 kOverworld_OffsetBaseX_Special[64] = {
  0x02a6, 0x84a5, 0x2c3f, 0x02a6, 0x8485, 0x00ad, 0x1807, 0x347f,
  0x02a8, 0x8548, 0x8a04, 0x0a0a, 0x0a0a, 0x0a0a, 0x0405, 0xa5aa,
  0x1884, 0x347f, 0x02a6, 0x8485, 0x4a68, 0xe2aa, 0xa530, 0x488a,
  0x2ac9, 0x05d0, 0x80a9, 0x2d8d, 0xbf01, 0xa5ec, 0x0f02, 0xf3ca,
  0x857e, 0x8d8a, 0x040a, 0xafaa, 0xf3ca, 0xf07e, 0xaf06, 0xf357,
  0xf07e, 0xbf1f, 0x5b00, 0x4a7f, 0x4a4a, 0xd04a, 0xa905, 0x8d05,
  0x012d, 0x00bf, 0x7f5b, 0x0f29, 0x30cd, 0xf001, 0xa905, 0x8df1,
  0x012c, 0x0820, 0xa9ab, 0x8501, 0xa511, 0x8d00, 0x0410, 0x168d,
};
static const uint16 kOverworld_UpDownScrollTarget[64] = {
  0xff20, 0xff20, 0xff20, 0xff20, 0xff20, 0xff20, 0xff20, 0xff20,
  0xff20, 0xff20,  0x120, 0xff20, 0xff20, 0xff20, 0xff20,  0x120,
   0x320,  0x320,  0x320,  0x320,  0x320,  0x320,  0x320,  0x320,
   0x520,  0x520,  0x520,  0x520,  0x520,  0x520,  0x520,  0x520,
   0x520,  0x520,  0x720,  0x520,  0x520,  0x720,  0x520,  0x520,
   0x920,  0x920,  0x920,  0x920,  0x920,  0x920,  0x920,  0x920,
   0xb20,  0xb20,  0xb20,  0xb20,  0xb20,  0xb20,  0xb20,  0xb20,
   0xb20,  0xb20,  0xd20,  0xd20,  0xd20,  0xb20,  0xb20,  0xd20,
};
static const uint16 kOverworld_LeftRightScrollTarget[64] = {
  0xff00, 0xff00, 0x300, 0x500, 0x500, 0x900, 0x900, 0xd00,
  0xff00, 0xff00, 0x300, 0x500, 0x500, 0x900, 0x900, 0xd00,
  0xff00,  0x100, 0x300, 0x500, 0x700, 0x900, 0xb00, 0xd00,
  0xff00, 0xff00, 0x300, 0x500, 0x500, 0x900, 0xb00, 0xb00,
  0xff00, 0xff00, 0x300, 0x500, 0x500, 0x900, 0xb00, 0xb00,
  0xff00,  0x100, 0x300, 0x500, 0x700, 0x900, 0xb00, 0xd00,
  0xff00, 0xff00, 0x300, 0x500, 0x700, 0x900, 0x900, 0xd00,
  0xff00, 0xff00, 0x300, 0x500, 0x700, 0x900, 0x900, 0xd00,
};
#if 0
static const uint16 kSpExit_Top[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0x200, 0x200, 0, 0, 0, 0, 0, 0 };
static const uint16 kSpExit_Bottom[16] = { 0x120, 0x20, 0x320, 0x20, 0, 0, 0x320, 0x320, 0x320, 0x220, 0, 0, 0, 0, 0x320, 0x320 };
static const uint16 kSpExit_Left[16] = { 0, 0x100, 0x200, 0x600, 0x600, 0xa00, 0xc00, 0xc00, 0, 0x100, 0x200, 0x600, 0x600, 0xa00, 0xc00, 0xc00 };
static const uint16 kSpExit_Right[16] = { 0, 0x100, 0x500, 0x600, 0x600, 0xa00, 0xc00, 0xc00, 0, 0x100, 0x400, 0x600, 0x600, 0xa00, 0xc00, 0xc00 };
static const uint16 kSpExit_Tab4[16] = { 0xff20, 0xff20, 0xff20, 0xff20, 0xff20, 0xff20, 0xff20, 0xff20, 0xff20, 0xff20, 0x120, 0xff20, 0xff20, 0xff20, 0xff20, 0x120 };
static const int16 kSpExit_Tab6[16] = { -4, 0x100, 0x300, 0x100, 0x500, 0x900, 0xb00, 0xb00, -4, 0x100, 0x300, 0x500, 0x500, 0x900, 0xb00, 0xb00 };
static const int16 kSpExit_Tab5[16] = { -0xe0, -0xe0, -0xe0, -0xe0, -0xe0, -0xe0, 0x400, 0x400, -0xe0, -0xe0, 0x120, -0xe0, -0xe0, -0xe0, 0x400, 0x400 };
static const uint16 kSpExit_Tab7[16] = { 4, 0x104, 0x300, 0x100, 0x500, 0x900, 0xb00, 0xb00, 4, 0x104, 0x300, 0x100, 0x500, 0x900, 0xb00, 0xb00 };
static const uint16 kSpExit_LeftEdgeOfMap[16] = { 0, 0, 0x200, 0x600, 0x600, 0xa00, 0xc00, 0xc00, 0, 0, 0x200, 0x600, 0x600, 0xa00, 0xc00, 0xc00 };
static const uint8 kSpExit_Dir[16] = { 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const uint8 kSpExit_SprGfx[16] = { 0xc, 0xc, 0xe, 0xe, 0xe, 0x10, 0x10, 0x10, 0xe, 0xe, 0xe, 0xe, 0x10, 0x10, 0x10, 0x10 };
static const uint8 kSpExit_AuxGfx[16] = { 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f };
static const uint8 kSpExit_PalBg[16] = { 0xa, 0xa, 0xa, 0xa, 2, 2, 2, 0xa, 2, 2, 0xa, 2, 2, 2, 2, 0xa };
static const uint8 kSpExit_PalSpr[16] = { 1, 8, 8, 8, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 2 };
#endif
#define turtlerock_ctr (some_menu_ctr)
#define ganonentrance_ctr (some_menu_ctr)
static PlayerHandlerFunc *const kOverworld_EntranceSequence[5] = {
  &Overworld_AnimateEntrance_PoD,
  &Overworld_AnimateEntrance_Skull,
  &Overworld_AnimateEntrance_Mire,
  &Overworld_AnimateEntrance_TurtleRock,
  &Overworld_AnimateEntrance_GanonsTower,
};
#ifndef map16_decode_0
#define map16_decode_0 ((uint8*)(g_ram+0x14400))
#define map16_decode_1 ((uint8*)(g_ram+0x14410))
#define map16_decode_2 ((uint8*)(g_ram+0x14420))
#define map16_decode_3 ((uint8*)(g_ram+0x14430))
#define map16_decode_last (*(uint16*)(g_ram+0x14440))
#define map16_decode_tmp (*(uint16*)(g_ram+0x14442))
#endif
static const uint16 kSecondaryOverlayPerOw[128] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0x1c0c, 0x1c0c, 0, 0, 0, 0, 0, 0, 0x1c0c, 0x1c0c, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0x3b0, 0x180c, 0x180c, 0x288, 0, 0, 0, 0, 0, 0x180c, 0x180c, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x1ab6, 0x1ab6, 0, 0xe2e, 0xe2e, 0, 0, 0,
  0x1ab6, 0x1ab6, 0, 0xe2e, 0xe2e, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x3b0, 0, 0, 0x288,
  0, 0, 0, 0, 0, 0, 0, 0,
};
#define XY(x, y) ((y)*64+(x))
// this alternate entry point is for scrolling OW area loads
// b/c drawing a door only applies to when you transition from a dungeon to the OW
// the exceptioon is OW areas 0x80 and above which are handled similar to entrances
static const uint16 kOverworld_DrawStrip_Tab[3] = { 0x3d0, 0x410, 0xf410 };
static const uint16 kOverworld_Func2_Tab[4] = { 8, 4, 2, 1 };
static const uint16 kOverworld_Entrance_Tab0[44] = {
  0xfe, 0xc5, 0xfe, 0x114, 0x115, 0x175, 0x156, 0xf5, 0xe2, 0x1ef, 0x119, 0xfe, 0x172, 0x177, 0x13f, 0x172, 0x112, 0x161, 0x172, 0x14c, 0x156, 0x1ef, 0xfe, 0xfe, 0xfe, 0x10b, 0x173, 0x143, 0x149, 0x175, 0x103, 0x100,
  0x1cc, 0x15e, 0x167, 0x128, 0x131, 0x112, 0x16d, 0x163, 0x173, 0xfe, 0x113, 0x177,
};
static const uint16 kOverworld_Entrance_Tab1[44] = {
  0x14a, 0xc4, 0x14f, 0x115, 0x114, 0x174, 0x155, 0xf5, 0xee, 0x1eb, 0x118, 0x146, 0x171, 0x155, 0x137, 0x174, 0x173, 0x121, 0x164, 0x155, 0x157, 0x128, 0x114, 0x123, 0x113, 0x109, 0x118, 0x161, 0x149, 0x117, 0x174, 0x101,
  0x1cc, 0x131, 0x51, 0x14e, 0x131, 0x112, 0x17a, 0x163, 0x172, 0x1bd, 0x152, 0x167,
};
static const uint16 kDwPaletteAnim[35] = {
  0x884, 0xcc7, 0x150a, 0x154d, 0x7ff6, 0x5944, 0x7ad1,
  0x884, 0xcc7, 0x150a, 0x154d, 0x5bff, 0x7ad1, 0x21af,
  0x1084, 0x48c0, 0x6186, 0x7e6d, 0x7fe0, 0x5944, 0x7e20,
  0x1084, 0x000e, 0x1059, 0x291f, 0x7fe0, 0x5944, 0x7e20,
  0x1084, 0x1508, 0x196c, 0x21af, 0x7ff6, 0x1d4c, 0x7ad1,
};
static const uint16 kDwPaletteAnim2[40] = {
  0x7fff, 0x884, 0x1cc8, 0x1dce, 0x3694, 0x4718, 0x1d4a, 0x18ac,
  0x7fff, 0x1908, 0x2d2f, 0x3614, 0x4eda, 0x471f, 0x1d4a, 0x390f,
  0x7fff, 0x34cd, 0x5971, 0x5635, 0x7f1b, 0x7fff, 0x1d4a, 0x3d54,
  0x7fff, 0x1908, 0x2d2f, 0x3614, 0x4eda, 0x471f, 0x1d4a, 0x390f,
  0x7fff, 0x884, 0x52a, 0x21ef, 0x3ab5, 0x4b39, 0x1d4c, 0x18ac,
};
static const uint16 kSpecialSwitchArea_Map8[4] = { 0x105, 0x1e4, 0xad, 0xb9 };
static const uint16 kSpecialSwitchArea_Screen[4] = { 0, 45, 15, 129 };
static const uint8 kSpecialSwitchArea_Direction[4] = { 8, 2, 8, 8 };
static const uint16 kSpecialSwitchArea_Exit[4] = { 0x180, 0x181, 0x182, 0x189 };
const uint8 kVariousPacks[16] = {
  0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x5b, 0x01, 0x5a,
  0x42, 0x43, 0x44, 0x45, 0x3f, 0x59, 0x0b, 0x5a
};
static const uint16 kSpecialSwitchAreaB_Map8[3] = { 0x17c, 0x1e4, 0xad };
static const uint16 kSpecialSwitchAreaB_Screen[3] = { 0x80, 0x80, 0x81 };
static const uint16 kSpecialSwitchAreaB_Direction[3] = { 4, 1, 4 };
static const int16 kSwitchAreaTab0[4] = { 0xf80, 0xf80, 0x3f, 0x3f };
static const int16 kSwitchAreaTab1[256] = {
  0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x1060, 0x1060, 0x1060, 0x1060, 0x60,
  0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
  0x60, 0x60, 0x60, 0x1060, 0x1060, 0x60, 0x1060, 0x1060, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
  0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x1060, 0x1060, 0x60,
  0x80, 0x80, 0x40, 0x80, 0x80, 0x80, 0x80, 0x40, 0x1080, 0x1080, 0x40, 0x1080, 0x1080, 0x1080, 0x1080, 0x40,
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x80, 0x80, 0x40, 0x80, 0x80, 0x40, 0x80, 0x80,
  0x1080, 0x1080, 0x40, 0x1080, 0x1080, 0x40, 0x1080, 0x1080, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
  0x80, 0x80, 0x40, 0x40, 0x40, 0x80, 0x80, 0x40, 0x1080, 0x1080, 0x40, 0x40, 0x40, 0x1080, 0x1080, 0x40,
  0x1800, 0x1840, 0x1800, 0x1800, 0x1840, 0x1800, 0x1840, 0x1800, 0x1800, 0x1840, 0x1800, 0x1800, 0x1840, 0x1800, 0x1840, 0x1800,
  0x1800, 0x1800, 0x1800, 0x1800, 0x1800, 0x1800, 0x1800, 0x1800, 0x1800, 0x1840, 0x1800, 0x1800, 0x1840, 0x1800, 0x1800, 0x1840,
  0x1800, 0x1840, 0x1800, 0x1800, 0x1840, 0x1800, 0x1800, 0x1840, 0x1800, 0x1800, 0x1800, 0x1800, 0x1800, 0x1800, 0x1800, 0x1800,
  0x1800, 0x1840, 0x1800, 0x1800, 0x1800, 0x1800, 0x1840, 0x1800, 0x1800, 0x1840, 0x1800, 0x1800, 0x1800, 0x1800, 0x1840, 0x1800,
  0x2000, 0x2040, 0x1000, 0x2000, 0x2040, 0x2000, 0x2040, 0x1000, 0x2000, 0x2040, 0x1000, 0x2000, 0x2040, 0x2000, 0x2040, 0x1000,
  0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x2000, 0x2040, 0x1000, 0x2000, 0x2040, 0x1000, 0x2000, 0x2040,
  0x2000, 0x2040, 0x1000, 0x2000, 0x2040, 0x1000, 0x2000, 0x2040, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000,
  0x2000, 0x2040, 0x1000, 0x1000, 0x1000, 0x2000, 0x2040, 0x1000, 0x2000, 0x2040, 0x1000, 0x1000, 0x1000, 0x2000, 0x2040, 0x1000,
};
static const int16 kSwitchAreaTab3[4] = { 2, -2, 16, -16 };
// kOverworldAreaHeads[i] != i for subregions of a big area
static const uint8 kOverworldAreaHeads[64] = {
  0,  0,  2,  3,  3,  5,  5,  7,
  0,  0, 10,  3,  3,  5,  5, 15,
  16, 17, 18, 19, 20, 21, 22, 23,
  24, 24, 26, 27, 27, 29, 30, 30,
  24, 24, 34, 27, 27, 37, 30, 30,
  40, 41, 42, 43, 44, 45, 46, 47,
  48, 48, 50, 51, 52, 53, 53, 55,
  48, 48, 58, 59, 60, 53, 53, 63,
};
static const uint16 kOverworld_Size1[2] = { 0x11e, 0x31e };
static const uint16 kOverworld_Size2[2] = { 0x100, 0x300 };
static const uint16 kOverworld_UpDownScrollSize[2] = { 0x2e0, 0x4e0 };
static const uint16 kOverworld_LeftRightScrollSize[2] = { 0x300, 0x500 };
static const int16 kOverworld_Func6B_Tab1[4] = { -8, 8, -8, 8 };
static const int16 kOverworld_Func6B_Tab2[4] = { 27, 27, 30, 30 };
static const int16 kOverworld_Func6B_Tab3[4] = { -0x70, 0x70, -0x70, 0x70 };
static const int16 kOverworld_Func6B_AreaDelta[4] = { -8, 8, -1, 1 };
static const uint8 kOverworld_Func8_tab[4] = { 0xe0, 8, 0xe0, 0x10 };
static const uint16 kDoorAnimTiles[56] = {
  0xda8, 0xda9, 0xdaa, 0xdab,
  0xdac, 0xdad, 0xdae, 0xdaf,
  0xdb0, 0xdb1, 0xdb2, 0xdb3,
  0xdb6, 0xdb7, 0xdb8, 0xdb9,
  0xdba, 0xdbb, 0xdbc, 0xdbd,
  0xdcd, 0xdce, 0xdcf, 0xdd0,
  0xdd3, 0xdd4, 0xdd5, 0xdd6,
  0xdd7, 0xdd8, 0xdd9, 0xdda,
  0xdd1, 0xdd2, 0xdd3, 0xdd4,
  0xdd1, 0xdd2, 0xdd7, 0xdd8,
  0x918, 0x919, 0x91a, 0x91b,
  0xddb, 0xddc, 0xddd, 0xdde,
  0xdd1, 0xdd2, 0xddb, 0xddc,
  0xe21, 0xe22, 0xe23, 0xe24,
};
static PlayerHandlerFunc *const kOverworldSubmodules[48] = {
  &Module09_00_PlayerControl,
  &Module09_LoadAuxGFX,
  &Overworld_FinishTransGfx,
  &Module09_LoadNewMapAndGFX,
  &Module09_LoadNewSprites,
  &Overworld_StartScrollTransition,
  &Overworld_RunScrollTransition,
  &Overworld_EaseOffScrollTransition,
  &Overworld_FinalizeEntryOntoScreen,
  &Module09_09_OpenBigDoorFromExiting,
  &Module09_0A_WalkFromExiting_FacingDown,
  &Module09_0B_WalkFromExiting_FacingUp,
  &Module09_0C_OpenBigDoor,
  &Overworld_StartMosaicTransition,
  &PreOverworld_LoadOverlays,
  &Module09_LoadAuxGFX,
  &Overworld_FinishTransGfx,
  &Module09_LoadNewMapAndGFX,
  &Module09_LoadNewSprites,
  &Overworld_StartScrollTransition,
  &Overworld_RunScrollTransition,
  &Overworld_EaseOffScrollTransition,
  &Module09_FadeBackInFromMosaic,
  &Overworld_StartMosaicTransition,
  &Overworld_Func18,
  &Overworld_Func19,
  &Module09_LoadAuxGFX,
  &Overworld_FinishTransGfx,
  &Overworld_Func1C,
  &Overworld_Func1D,
  &Overworld_Func1E,
  &Overworld_Func1F,
  &Overworld_LoadOverlays2,
  &Overworld_LoadAmbientOverlayFalse,
  &Overworld_Func22,
  &Module09_MirrorWarp,
  &Overworld_StartMosaicTransition,
  &Overworld_LoadOverlays,
  &Module09_LoadAuxGFX,
  &Overworld_FinishTransGfx,
  &Overworld_LoadAndBuildScreen,
  &Module09_FadeBackInFromMosaic,
  &Module09_2A_RecoverFromDrowning,
  &Overworld_Func2B,
  &Module09_MirrorWarp,
  &Overworld_WeathervaneExplosion,
  &Module09_2E_Whirlpool,
  &Overworld_Func2F,
};
static PlayerHandlerFunc *const kModule_PreOverworld[3] = {
  &PreOverworld_LoadProperties,
  &PreOverworld_LoadOverlays,
  &Module08_02_LoadAndAdvance,
};
const uint8 *GetMap8toTileAttr() {
  return kMap8DataToTileAttr;
}

const uint16 *GetMap16toMap8Table() {
  const uint16 *loc = Loc_Map16ToMap8(kMap16ToMap8, kMap16ToMap8_SIZE);
  return loc ? loc : kMap16ToMap8;
}

// The 4 map8 tiles of a map16, the way the game reads them: LDA $0F8000,X
// with X the map16 times 8 in 16 bits. A map16 past the table, as the
// overlays of Secrets of the Past write, wraps on 8192 and lands in the
// table or past it: in the code that follows it in the cartridge
// ($8F:F540), then in the work RAM's first 8 KB that bank $10 mirrors, then
// in its registers. The code and the registers read as 0 here.
const uint16 *Map16ToMap8Entry(uint16 map16) {
  static const uint16 kNone[4];
  uint32 i = map16 & 0x1fff;
  if (i < kMap16ToMap8_SIZE / 8)
    return GetMap16toMap8Table() + i * 4;
  if (i >= 0x1000 && i < 0x1400)
    return (const uint16 *)(g_ram + (i - 0x1000) * 8);
  return kNone;
}

bool LookupInOwEntranceTab(uint16 r0, uint16 r2) {
  for (int i = countof(kOverworld_Entrance_Tab0) - 1; i >= 0; i--) {
    if (r0 == kOverworld_Entrance_Tab0[i] && r2 == kOverworld_Entrance_Tab1[i])
      return true;
  }
  return false;
}

int LookupInOwEntranceTab2(uint16 pos) {
  for (int i = 128; i >= 0; i--) {
    if (pos == kOverworld_Entrance_Pos[i] && overworld_area_index == kOverworld_Entrance_Area[i])
      return i;
  }
  return -1;
}

bool CanEnterWithTagalong(int e) {
  uint8 t = follower_indicator;
  return t == 0 || t == 5 || t == 14 || t == 1 || (t == 7 || t == 8) && e >= 59;
}

int DirToEnum(int dir) {
  int xx = 3;
  while (!(dir & 1))
    xx--, dir >>= 1;
  return xx;
}

void Overworld_ResetMosaicDown() {
  if (palette_filter_countdown & 1)
    mosaic_level -= 0x10;
  BGMODE_copy = 9;
  MOSAIC_copy = mosaic_level | 7;
}

void Overworld_Func1D() {
  assert(0);
}

void Overworld_Func1E() {
  assert(0);
}

uint16 Overworld_GetSignText(int area) {
  return kOverworld_SignText[area];
}

const uint8 *GetOverworldSpritePtr(int area) {
  int base = (sram_progress_indicator == 3) ? 2 :
    (sram_progress_indicator == 2) ? 1 : 0;
  return kOverworldSprites + kOverworldSpriteOffs[area + base * 144];
}

uint8 GetOverworldBgPalette(int idx) {
  return kOverworldBgPalettes[idx];
}

void Sprite_LoadGraphicsProperties() {  // 80fc41
  memcpy(overworld_sprite_gfx + 64, kOverworldSpriteGfx + 0xc0, 64);
  memcpy(overworld_sprite_palettes + 64, kOverworldSpritePalettes + 0xc0, 64);
  Sprite_LoadGraphicsProperties_light_world_only();
}

void Sprite_LoadGraphicsProperties_light_world_only() {  // 80fc62
  int i = sram_progress_indicator < 2 ? 0 :
    sram_progress_indicator != 3 ? 1 : 2;
  memcpy(overworld_sprite_gfx, kOverworldSpriteGfx + i * 64, 64);
  memcpy(overworld_sprite_palettes, kOverworldSpritePalettes + i * 64, 64);
}

void InitializeMirrorHDMA() {  // 80fdee
  HDMAEN_copy = 0;

  mirror_vars.var0 = 0;
  mirror_vars.var6 = 0;
  mirror_vars.var5 = 0;
  mirror_vars.var7 = 0;
  mirror_vars.var8 = 0;

  mirror_vars.var10 = mirror_vars.var11 = 8;
  mirror_vars.var9 = 21;
  mirror_vars.var1[0] = -0x200;
  mirror_vars.var1[1] = 0x200;
  mirror_vars.var3[0] = -0x40;
  mirror_vars.var3[1] = 0x40;

  HdmaSetup(0xF2FB, 0xF2FB, 0x42, (uint8)BG1HOFS, (uint8)BG2HOFS, 0);

  uint16 v = BG2HOFS_copy2;
  for (int i = 0; i < 240; i++)
    hdma_table_dynamic[i] = v;
  HDMAEN_copy = 0xc0;
}

// Fast-forward may advance CPU-only mirror animation work more than once per
// rendered frame, but AnimateMirrorWarp also uses a single NMI command slot and
// shared staging buffers for its graphics stages. Once a step queues NMI work,
// another step in the same frame would replace that command/buffer before the
// upload runs, leaving the destination world rendered with stale graphics.
static void MirrorWarp_RunFastForwardSteps() {
  int steps = CutsceneFastForwardEnabled() ? 3 : 1;
  do {
    MirrorWarp_RunAnimationSubmodules();
    if (nmi_subroutine_index != 0 || nmi_disable_core_updates != 0)
      break;
  } while (--steps);
}

void MirrorWarp_BuildWavingHDMATable() {  // 80fe64
  MirrorWarp_RunFastForwardSteps();
  if (!CutsceneFastForwardEnabled() && (frame_counter & 1)) {
    return;
  }

  int y = 240 - 8;
  do {
    hdma_table_dynamic[y] = hdma_table_dynamic[y + 2] = hdma_table_dynamic[y + 4] = hdma_table_dynamic[y + 6] = hdma_table_dynamic[y - 8];
    y -= 8;
  } while (y != 0);

  int i = mirror_vars.var0 >> 1;
  int t = mirror_vars.var6 + mirror_vars.var3[i];
  if (!sign16(t - mirror_vars.var1[i] ^ mirror_vars.var1[i])) {
    t = mirror_vars.var1[i];
    mirror_vars.var5 = 0;
    mirror_vars.var7 = 0;
    mirror_vars.var0 ^= 2;
  }
  mirror_vars.var6 = t;
  t += mirror_vars.var7;
  mirror_vars.var7 = t & 0xff;
  if (sign16(t))
    t |= 0xff;
  else
    t &= ~0xff;
  t = mirror_vars.var5 + swap16(t);
  mirror_vars.var5 = t;
  if (palette_filter_countdown >= 0x30 && !(t & ~7)) {
    mirror_vars.var1[0] = -0x100;
    mirror_vars.var1[1] = 0x100;
    subsubmodule_index++;
    t = 0;
  }
  hdma_table_dynamic[0] = hdma_table_dynamic[2] = hdma_table_dynamic[4] = hdma_table_dynamic[6] = t + BG2HOFS_copy2;
}

void MirrorWarp_BuildDewavingHDMATable() {  // 80ff2f
  MirrorWarp_RunFastForwardSteps();
  if (!CutsceneFastForwardEnabled() && (frame_counter & 1)) {
    return;
  }
  int y = 240 - 8;
  do {
    hdma_table_dynamic[y] = hdma_table_dynamic[y + 2] = hdma_table_dynamic[y + 4] = hdma_table_dynamic[y + 6] = hdma_table_dynamic[y - 8];
    y -= 8;
  } while (y != 0);

  uint16 t = hdma_table_dynamic[0xc0] | hdma_table_dynamic[0xc8] | hdma_table_dynamic[0xd0] | hdma_table_dynamic[0xd8];
  bool settled = t == BG2HOFS_copy2;
  if (enhanced_features0 & kFeatures0_WidescreenVisualFixes) {
    // OR-ing four samples can equal the base while a wave is still visible.
    // Include the extended bottom rows before ending the effect.
    settled = true;
    for (int row = 192; row < 240; row += 2)
      settled &= hdma_table_dynamic[row] == BG2HOFS_copy2;
  }
  if (settled) {
    HDMAEN_copy = 0;
    subsubmodule_index++;
    Overworld_SetFixedColAndScroll();
    if ((overworld_screen_index & 0x3f) != 0x1b) {
      BG1HOFS_copy2 = BG1HOFS_copy = BG2HOFS_copy = BG2HOFS_copy2;
      BG1VOFS_copy2 = BG1VOFS_copy = BG2VOFS_copy = BG2VOFS_copy2;
    }
  }
}

void TakeDamageFromPit() {  // 81ffd9
  link_visibility_status = 12;
  submodule_index = player_is_indoors ? 20 : 42;
  link_health_current -= 8;
  if (link_health_current >= 0xa8)
    link_health_current = 0;
}

void Module08_OverworldLoad() {  // 8283bf
  kModule_PreOverworld[submodule_index]();
}

void PreOverworld_LoadProperties() {  // 8283c7
  CGWSEL_copy = 0x82;
  dung_unk6 = 0;
  AdjustLinkBunnyStatus();
  if (main_module_index == 8)
    LoadOverworldFromDungeon();
  else
    LoadOverworldFromSpecialOverworld();
  Overworld_SetSongList();
  // rando-exempt: state-shuffle — 0xff sentinel marks "outside dungeon" key
  // count when transitioning to the overworld. (audit.md §0.2.2)
  link_num_keys = 0xff;
  Hud_RefillLogic();

  uint8 sc = overworld_screen_index, dr = dungeon_room_index;
  uint8 ow_anim_tiles = 0x58;
  uint8 xt = 2;

  if (sc == 3 || sc == 5 || sc == 7) {
    xt = 2;
  } else if (sc == 0x43 || sc == 0x45 || sc == 0x47) {
    xt = 9;
  } else if (ow_anim_tiles = 0x5a, sc >= 0x40) {
    goto dark;
  } else if (dr == 0xe3 || dr == 0x18 || dr == 0x2f || dr == 0x1f && sc == 0x18) {
    xt = sram_progress_indicator < 3 ? 7 : 2;
  } else {
    xt = savegame_has_master_sword_flags & 0x40 ? 2 : 5;
    if (dr != 0 && dr != 0xe1) {
dark:
      xt = 0xf3;
      if (queued_music_control == 0xf2)
        goto setsong;
      xt = sram_progress_indicator < 2 ? 3 : 2;
    }
  }
  if (savegame_is_darkworld) {
    xt = sc == 0x40 || sc == 0x43 || sc == 0x45 || sc == 0x47 ? 13 : 9;
    if (!link_item_moon_pearl)
      xt = 4;
  }
setsong:
  queued_music_control = xt;
  DecompressAnimatedOverworldTiles(ow_anim_tiles);
  InitializeTilesets();
  OverworldLoadScreensPaletteSet();
  Overworld_LoadPalettes(kOverworldBgPalettes[sc], overworld_sprite_palettes[sc]);
  Palette_SetOwBgColor();
  if (main_module_index == 8) {
    Overworld_LoadPalettesInner();
  } else {
    SpecialOverworld_CopyPalettesToCache();
  }
  Overworld_SetFixedColAndScroll();
  overworld_fixed_color_plusminus = 0;
  Follower_Initialize();

  if (!(BYTE(overworld_screen_index) & 0x3f))
    DecodeAnimatedSpriteTile_variable(0x1e);
  saved_module_for_menu = 9;
  Sprite_ReloadAll_Overworld();
  // #82 Inverted: residual portal spawn-side (z3randomizer $82B34D). Vanilla
  // spawns the leftover mirror portal only in the LIGHT world (!(ow&0x40));
  // Inverted flips the home world, so spawn it in the DARK world instead.
  bool portal_away_world =
      ((enhanced_features1 & kFeatures1_RandomizerActive) &&
       Rando_GetActiveWorldState() == 2 /* kWorldState_Inverted */)
        ? (overworld_screen_index & 0x40) != 0
        : !(overworld_screen_index & 0x40);
  if (portal_away_world)
    Sprite_InitializeMirrorPortal();
  sound_effect_ambient = sram_progress_indicator < 2 ? 1 : 5;
  if (follower_indicator == 6)
    follower_indicator = 0;

  is_standing_in_doorway = 0;
  button_mask_b_y = 0;
  button_b_frames = 0;
  link_cant_change_direction = 0;
  link_speed_setting = 0;
  draw_water_ripples_or_grass = 0;
  Dungeon_ResetTorchBackgroundAndPlayerInner();
  // #82 Inverted: DecideIfBunnyByScreenIndex (z3randomizer bugfixes.asm). On
  // overworld load, bunny when in the away-world without the Moon Pearl. Vanilla
  // away-world = DW (savegame_is_darkworld set); Inverted away-world = LW (clear).
  bool ow_load_away_world =
      ((enhanced_features1 & kFeatures1_RandomizerActive) &&
       Rando_GetActiveWorldState() == 2 /* kWorldState_Inverted */)
        ? !savegame_is_darkworld
        :  (savegame_is_darkworld != 0);
  if (!link_item_moon_pearl && ow_load_away_world) {
    link_is_bunny = link_is_bunny_mirror = 1;
    link_player_handler_state = kPlayerState_PermaBunny;
    LoadGearPalettes_bunny();
  }
  BGMODE_copy = 9;
  dung_want_lights_out = 0;
  dung_hdr_collision = 0;
  link_is_on_lower_level = 0;
  link_is_on_lower_level_mirror = 0;
  submodule_index++;
  flag_update_hud_in_nmi++;
  dung_savegame_state_bits = 0;
  LoadOWMusicIfNeeded();
}

void AdjustLinkBunnyStatus() {  // 82856a
  if (link_item_moon_pearl)
    ForceNonbunnyStatus();
}

void ForceNonbunnyStatus() {  // 828570
  link_player_handler_state = kPlayerState_Ground;
  link_timer_tempbunny = 0;
  link_need_for_poof_for_transform = 0;
  link_is_bunny = 0;
  link_is_bunny_mirror = 0;

  if (enhanced_features0 & kFeatures0_TurnWhileDashing)
    link_is_running = 0;
}

void RecoverPositionAfterDrowning() {  // 829583
  link_x_coord = link_x_coord_cached;
  link_y_coord = link_y_coord_cached;
  ow_scroll_vars0.ystart = room_scroll_vars_y_vofs1_cached;
  ow_scroll_vars0.xstart = room_scroll_vars_y_vofs2_cached;
  ow_scroll_vars1.ystart = room_scroll_vars_x_vofs1_cached;
  ow_scroll_vars1.xstart = room_scroll_vars_x_vofs2_cached;

  up_down_scroll_target = up_down_scroll_target_cached;
  up_down_scroll_target_end = up_down_scroll_target_end_cached;
  left_right_scroll_target = left_right_scroll_target_cached;
  left_right_scroll_target_end = left_right_scroll_target_end_cached;

  if (player_is_indoors) {
    camera_y_coord_scroll_low = camera_y_coord_scroll_low_cached;
    camera_y_coord_scroll_hi = camera_y_coord_scroll_low + 2;
    camera_x_coord_scroll_low = camera_x_coord_scroll_low_cached;
    camera_x_coord_scroll_hi = camera_x_coord_scroll_low + 2;
  }
  WORD(quadrant_fullsize_x) = WORD(quadrant_fullsize_x_cached);
  WORD(link_quadrant_x) = WORD(link_quadrant_x_cached);
  if (!player_is_indoors) {
    camera_y_coord_scroll_hi = camera_y_coord_scroll_low - 2;
    camera_x_coord_scroll_hi = camera_x_coord_scroll_low - 2;
  }

  link_direction_facing = link_direction_facing_cached;
  link_is_on_lower_level = link_is_on_lower_level_cached;
  link_is_on_lower_level_mirror = link_is_on_lower_level_mirror_cached;
  is_standing_in_doorway = is_standing_in_doorway_cahed;
  dung_cur_floor = dung_cur_floor_cached;
  link_visibility_status = 0;
  countdown_for_blink = 0x90;
  Dungeon_PlayBlipAndCacheQuadrantVisits();
  link_disable_sprite_damage = 0;
  Link_ResetStateAfterDamagingPit();
  tagalong_var5 = 0;
  Follower_Initialize();
  dung_flag_statechange_waterpuzzle = 0;
  overworld_map_state = 0;
  subsubmodule_index = 0;
  overworld_screen_transition = 0;
  submodule_index = 0;
  if (!link_health_current) {
    mapbak_TM = TM_copy;
    mapbak_TS = TS_copy;
    saved_module_for_menu = main_module_index;
    main_module_index = 18;
    submodule_index = 1;
    countdown_for_blink = 0;
  }
}

void Module0F_SpotlightClose() {  // 829982
  static const uint8 kTab[4] = { 8, 4, 2, 1 };
  Sprite_Main();
  if (submodule_index == 0)
    Dungeon_PrepExitWithSpotlight();
  else
    Spotlight_ConfigureTableAndControl();

  if (!player_is_indoors) {
    if (BYTE(overworld_screen_index) == 0xf)
      draw_water_ripples_or_grass = 1;
    link_speed_setting = 6;
    Link_HandleVelocity();
    link_x_vel = link_y_vel = 0;
  }

  int i = link_direction_facing >> 1;
  if (!player_is_indoors)
    i = (which_entrance == 0x43) ? 1 : 0;

  link_direction = link_direction_last = kTab[i];
  Link_HandleMovingAnimation_FullLongEntry();
  LinkOam_Main();
}

void Dungeon_PrepExitWithSpotlight() {  // 8299ca
  is_nmi_thread_active = 0;
  nmi_flag_update_polyhedral = 0;
  if (!player_is_indoors) {
    Ancilla_TerminateWaterfallSplashes();
    link_y_coord_exit = link_y_coord;
  }
  uint8 m = ZeldaGetEntranceMusicTrack(which_entrance);
  if (m != 3 || (m = sram_progress_indicator) >= 2) {
    if (m != 0xf2)  // fade to 0x40
      m = 0xf1;  // fade to zero
    else if (music_unk1 == 12)
      m = 7;
    music_control = m;
  }
  hud_floor_changed_timer = 0;
  Hud_FloorIndicator();
  flag_update_hud_in_nmi++;
  IrisSpotlight_close();
  submodule_index++;
}

void Spotlight_ConfigureTableAndControl() {  // 829a19
  IrisSpotlight_ConfigureTable();
  is_nmi_thread_active = 0;
  nmi_flag_update_polyhedral = 0;
  if (submodule_index)
    return;
  if (main_module_index == 6)
    link_y_coord = link_y_coord_exit;
  OpenSpotlight_Next2();
}

void OpenSpotlight_Next2() {  // 829a37
  if (main_module_index != 9) {
    EnableForceBlank();
    Link_ItemReset_FromOverworldThings();
  }

  if (main_module_index == 9) {
    if (dungeon_room_index != 0x20)
      submodule_index = link_direction_facing ? 0xa : 0xb;

    ow_countdown_transition = 16;
    if ((BYTE(ow_entrance_value) | BYTE(big_rock_starting_address)) && HIBYTE(big_rock_starting_address)) { // wtf
      BYTE(door_open_closed_counter) = (big_rock_starting_address & 0x8000) ? 0x18 : 0;
      big_rock_starting_address &= 0x7fff;
      BYTE(door_animation_step_indicator) = 0;
      submodule_index = 9;
      subsubmodule_index = 0;
      sound_effect_2 = 21;
    }
  }
  W12SEL_copy = 0;
  W34SEL_copy = 0;
  WOBJSEL_copy = 0;
  TMW_copy = 0;
  TSW_copy = 0;
  link_force_hold_sword_up = 0;

  uint8 sc = overworld_screen_index;
  if (sc == 3 || sc == 5 || sc == 7) {
    COLDATA_copy0 = 0x26;
    COLDATA_copy1 = 0x4c;
    COLDATA_copy2 = 0x8c;
  } else if (sc == 0x43 || sc == 0x45 || sc == 0x47) {
    COLDATA_copy0 = 0x26;
    COLDATA_copy1 = 0x4a;
    COLDATA_copy2 = 0x87;
  }
}

void Module10_SpotlightOpen() {  // 829ad7
  Sprite_Main();
  if (submodule_index == 0)
    Module10_00_OpenIris();
  else
    Spotlight_ConfigureTableAndControl();
  LinkOam_Main();
}

void Module10_00_OpenIris() {  // 829ae6
  Spotlight_open();
  submodule_index++;
}

void SetTargetOverworldWarpToPyramid() {  // 829e5f
  if (main_module_index != 21)
    return;
  LoadOverworldFromDungeon();
  DecompressAnimatedOverworldTiles(0x5a);
  ResetAncillaAndCutscene();
}

void ResetAncillaAndCutscene() {  // 829e6e
  Ancilla_TerminateSelectInteractives(0);
  link_disable_sprite_damage = 0;
  button_b_frames = 0;
  button_mask_b_y = 0;
  link_force_hold_sword_up = 0;
  flag_is_link_immobilized = 0;
}

void Module09_Overworld() {  // 82a475
  kOverworldSubmodules[submodule_index]();
  ZeldaWideBeforeSprites();

  int bg2x = BG2HOFS_copy2;
  int bg2y = BG2VOFS_copy2;
  int bg1x = BG1HOFS_copy2;
  int bg1y = BG1VOFS_copy2;

  BG2HOFS_copy2 = BG2HOFS_copy = bg2x + bg1_x_offset;
  BG2VOFS_copy2 = BG2VOFS_copy = bg2y + bg1_y_offset;
  BG1HOFS_copy2 = BG1HOFS_copy = bg1x + bg1_x_offset;
  BG1VOFS_copy2 = BG1VOFS_copy = bg1y + bg1_y_offset;

  Sprite_Main();
  Rando_DrawOverworldEnemyMarkerGlints();
  Rando_DrawTerrainGlints();  // add-rando-grass-rock-shuffle — capped "check" glints

  BG2HOFS_copy2 = bg2x;
  BG2VOFS_copy2 = bg2y;
  BG1HOFS_copy2 = bg1x;
  BG1VOFS_copy2 = bg1y;

  LinkOam_Main();
  Hud_RefillLogic();
  OverworldOverlay_HandleRain();
}

void OverworldOverlay_HandleRain() {  // 82a4cd
  static const uint8 kOverworld_DrawBadWeather_X[4] = { 1, 0, 1, 0 };
  static const uint8 kOverworld_DrawBadWeather_Y[4] = { 0, 17, 0, 17 };
  if (BYTE(overworld_screen_index) != 0x70 && sram_progress_indicator >= 2 || (save_ow_event_info[0x70] & 0x20))
    return;
  if (frame_counter == 3 || frame_counter == 88) {
    CGADSUB_copy = 0x32;
  } else if (frame_counter == 5 || frame_counter == 44 || frame_counter == 90) {
    CGADSUB_copy = 0x72;
  } else if (frame_counter == 36) {
    sound_effect_1 = 54;
    CGADSUB_copy = 0x32;
  }
  if (frame_counter & 3)
    return;
  int i = (move_overlay_ctr + 1) & 3;
  move_overlay_ctr = i;
  BG1HOFS_copy2 += kOverworld_DrawBadWeather_X[i] << 8;
  BG1VOFS_copy2 += kOverworld_DrawBadWeather_Y[i] << 8;
}

// #82 Inverted: the DW->LW "under-rock" world-warps sit beneath rocks that the
// inverted overlay (inverted_maps.c / Overworld_ApplyInvertedTiles) only draws
// on a FULL screen rebuild — mirror / flute / cave-exit / save-and-quit. A
// walking-scroll entry skips that overlay, so the rock (and thus the warp record
// under it) is missing when you simply walk onto the screen. Re-assert just the
// single liftable rock tile for the current warp screen here; this runs every
// frame in the walking state, so the tile is restored the frame the screen
// settles and on any partial redraw. The {pos,tile} pairs mirror the overlay
// rows in inverted_maps.c exactly (pos is the dung_bg2 byte offset; overlay's
// field is 0x2000 + pos). We skip when the tile already shows the rock OR the
// revealed warp swirl (0x0212), so lifting the rock is never re-covered before
// the player can step onto the warp. Reusable by design (the warp flips worlds
// and may be used repeatedly), so it is NOT gated on a location-checked flag.
static const struct {
  uint8  screen;  // overworld_screen_index (Dark World)
  uint16 pos;     // dung_bg2 byte offset of the warp rock
  uint16 tile;    // liftable-rock map16 value
} kInvertedWarpRocks[] = {
  { 0x73, 0x02A8, 0x020F },
  { 0x50, 0x0B2E, 0x020F },
  { 0x6F, 0x0BB2, 0x020F },
  { 0x4E, 0x1D4A, 0x0239 },
  { 0x70, 0x1D94, 0x0239 },
  { 0x78, 0x1D94, 0x0239 },
  { 0x75, 0x0F50, 0x0239 },
  { 0x47, 0x069E, 0x0239 },
  { 0x47, 0x06A4, 0x0239 },
};

// Never replace event-modified tiles (lifted rocks, open doors, etc.).
static void Overworld_QolShortcutTiles(uint16 *dst,int screen,int stride) {
  if (!(enhanced_features1 & kFeatures1_QolKakariko)) return;
  for (unsigned i=0;i<countof(kQolShortcutCells);i++) {
    const struct QolShortcutCell *c=&kQolShortcutCells[i];
    if(c->screen==screen && dst[c->y*stride+c->x]==c->original)
      dst[c->y*stride+c->x]=c->tile;
  }
}

// Live changes use bounded VRAM stripes on ordinary overworld control frames.
// Defer a tile under Link until he has stepped away when closing the passage.
static void Overworld_QolShortcutLive(void) {
  if (!(enhanced_features1 & kFeatures1_QolManaged)) return;
  bool enabled=(enhanced_features1 & kFeatures1_QolKakariko)!=0;
  int base=overworld_screen_index, writes=0;
  for (unsigned i=0;i<countof(kQolShortcutCells);i++) {
    const struct QolShortcutCell *c=&kQolShortcutCells[i];
    int delta=c->screen-base, ox=delta&1, oy=delta>>3;
    if(delta!=0 && delta!=1 && delta!=8 && delta!=9)continue;
    int x=c->x+ox*32,y=c->y+oy*32, pos=(y*64+x)*2;
    uint16 from=enabled?c->original:c->tile,to=enabled?c->tile:c->original;
    if(dung_bg2[pos>>1]!=from)continue;
    int wx=((base&7)*512)+x*16,wy=(((base&63)>>3)*512)+y*16;
    if(!enabled && abs((int)link_x_coord-wx)<24 && abs((int)link_y_coord-wy)<24)continue;
    if(vram_upload_offset>0x300 || writes==8)break;
    Overworld_DrawMap16_Persist(pos,to);writes++;
  }
  if(writes)nmi_load_bg_from_vram=1;
}

static void Overworld_EnsureInvertedWarpRock(void) {
  if (!((enhanced_features1 & kFeatures1_RandomizerActive) &&
        Rando_GetActiveWorldState() == 2 /* kWorldState_Inverted */))
    return;
  uint8 scr = (uint8)overworld_screen_index;
  for (uint32 i = 0; i < sizeof(kInvertedWarpRocks) / sizeof(kInvertedWarpRocks[0]); i++) {
    if (kInvertedWarpRocks[i].screen != scr)
      continue;
    uint16 pos = kInvertedWarpRocks[i].pos;
    uint16 cur = dung_bg2[pos >> 1];
    if (cur == kInvertedWarpRocks[i].tile || cur == 0x0212)
      continue;  // rock already placed, or warp already revealed — leave it
    Overworld_DrawMap16_Persist(pos, kInvertedWarpRocks[i].tile);
    nmi_load_bg_from_vram = 1;
  }
}

void Module09_00_PlayerControl() {  // 82a53c
  Overworld_EnsureInvertedWarpRock();
  Overworld_QolShortcutLive();
  if (!(flag_custom_spell_anim_active | flag_is_link_immobilized | flag_block_link_menu | trigger_special_entrance)) {
    if (filtered_joypad_H & 0x10) {
      overworld_map_state = 0;
      submodule_index = 1;
      saved_module_for_menu = main_module_index;
      main_module_index = 14;
      return;
    }

    if (DidPressButtonForMap()) {
      overworld_map_state = 0;
      submodule_index = 7;
      saved_module_for_menu = main_module_index;
      main_module_index = 14;
      return;
    }
    if (joypad1H_last & 0x20) {
      DisplaySelectMenu();
      return;
    }
    Hud_HandleItemSwitchInputs();
  }
  if (trigger_special_entrance)
    Overworld_AnimateEntrance();
  Link_Main();
  if (super_bomb_indicator_unk2 != 0xff)
    Hud_SuperBombIndicator();
  current_area_of_player = (link_y_coord & 0x1e00) >> 5 | (link_x_coord & 0x1e00) >> 8;
  Graphics_LoadChrHalfSlot();
  Overworld_OperateCameraScroll();
  if (main_module_index != 11) {
    Overworld_UseEntrance();
    Overworld_DwDeathMountainPaletteAnimation();
    OverworldHandleTransitions();
  } else {
    ScrollAndCheckForSOWExit();
  }
}

// The ROM's words from $82:A8C4: kOverworld_OffsetBaseY, kOverworld_OffsetBaseX,
// then the code that follows, which the game reads as more of the table when
// Link walks off the map's edge (a quest that lets him gives those bytes).
static uint16 OwOffsetBaseWord(int i) {
  if (i >= 0 && i < 64)
    return kOverworld_OffsetBaseY[i];
  if (i >= 64 && i < 128)
    return kOverworld_OffsetBaseX[i - 64];
  return Mods_RomWord(0x82a8c4 + i * 2, 0);
}

// The same from $82:BEE2: the scroll targets, then the sizes.
static uint16 OwScrollTargetWord(int i) {
  if (i >= 0 && i < 64)
    return kOverworld_UpDownScrollTarget[i];
  if (i >= 64 && i < 128)
    return kOverworld_LeftRightScrollTarget[i - 64];
  if (i >= 128 && i < 136) {
    static const uint16 *const kSizes[4] = { kOverworld_Size1, kOverworld_Size2, kOverworld_UpDownScrollSize, kOverworld_LeftRightScrollSize };
    return kSizes[(i - 128) >> 1][i & 1];
  }
  return Mods_RomWord(0x82bee2 + i * 2, 0);
}

// The ROM's bytes from $82:A5EC: kOverworldAreaHeads, then kSwitchAreaTab0
// ($82:A62C) and kSwitchAreaTab1 ($82:A634). Off the map's edge the game
// reads past the heads and past a row of kSwitchAreaTab1, into these.
static uint8 SwitchAreaRomByte(int i) {
  if (i < 64)
    return MODS_U8(kOverworldAreaHeads, 0x82a5ec)[i];
  if (i < 72)
    return ((const uint8 *)kSwitchAreaTab0)[i - 64];
  if (i < 72 + 512)
    return ((const uint8 *)kSwitchAreaTab1)[i - 72];
  return 0;
}

void OverworldHandleTransitions() {  // 82a9c4
  if (BYTE(overworld_screen_trans_dir_bits2))
    OverworldHandleMapScroll();
  int dir;
  uint16 x, y, t;
  if (link_y_vel != 0) {
    dir = link_direction & 12;
    t = link_y_coord - OwOffsetBaseWord(BYTE(current_area_of_player) >> 1);
    if ((y = 6, x = 8, t < 4) || (y = 4, x = 4, t >= overworld_right_bottom_bound_for_scroll))
      goto compare;
  }
  if (link_x_vel != 0) {
    dir = link_direction & 3;
    t = link_x_coord - OwOffsetBaseWord((BYTE(current_area_of_player) >> 1) + 64);
    if ((y = 2, x = 2, t < 6) || (y = 0, x = 1, t >= (uint16)(overworld_right_bottom_bound_for_scroll + 4))) {
compare:
      if (x == dir && !Link_CheckForEdgeScreenTransition())
        goto after;
    }
  }
  Overworld_CheckSpecialSwitchArea();
  return;
after:
  y >>= 1;
  Dungeon_ResetTorchBackgroundAndPlayerInner();
  map16_load_src_off &= kSwitchAreaTab0[y];
  // As the game: the area in 16 bits, then 8 ($82:AA6D), and the row of
  // kSwitchAreaTab1 ORed in, not added ($82:AA60). Inside the map they are
  // the same; a quest can let Link walk off it.
  uint16 v = current_area_of_player + kSwitchAreaTab3[y];
  int pushed = (v >> 1) & 0xff;
  uint16 off = (y * 2) << 6 | v;
  map16_load_src_off += SwitchAreaRomByte(72 + off) | SwitchAreaRomByte(73 + off) << 8;

  uint8 old_screen = overworld_screen_index;
  if (old_screen == 0x2a)
    sound_effect_ambient = 0x80;

  uint8 new_area = SwitchAreaRomByte(pushed) | savegame_is_darkworld;
  BYTE(overworld_screen_index) = new_area;
  BYTE(overworld_area_index) = new_area;
  if (!savegame_is_darkworld || link_item_moon_pearl) {
    uint8 music = overworld_music[new_area];
    if ((music & 0xf0) == 0)
      sound_effect_ambient = 5;
    if (!ZeldaIsPlayingMusicTrack(music & 0xf))
      music_control = 0xf1;
  }
  Overworld_LoadGFXAndScreenSize();
  submodule_index = 1;
  BYTE(overworld_screen_trans_dir_bits) = dir;
  BYTE(overworld_screen_trans_dir_bits2) = dir;
  byte_7E069C = overworld_screen_transition = DirToEnum(dir);
  BYTE(ow_entrance_value) = 0;
  BYTE(big_rock_starting_address) = 0;
  transition_counter = 0;

  if (!(old_screen & 0x3f) || !(overworld_screen_index & 0xbf)) {
    subsubmodule_index = 0;
    submodule_index = 13;
    MOSAIC_copy = 0;
    mosaic_level = 0;
  } else {
    uint8 sc = overworld_screen_index;
    Overworld_LoadPalettes(kOverworldBgPalettes[sc], overworld_sprite_palettes[sc]);
    Overworld_CopyPalettesToCache();
  }
}

void Overworld_LoadGFXAndScreenSize() {  // 82ab08
  int i = BYTE(overworld_screen_index);
  incremental_counter_for_vram = 0;
  sprite_graphics_index = overworld_sprite_gfx[i];
  // Past its 128 areas the game reads the next table ($80:FD1C), as when a
  // quest lets Link walk off the map's edge into a special area.
  aux_tile_theme_index = i < 128 ? kOverworldAuxTileThemeIndexes[i] : kOverworldBgPalettes[i - 128];

  overworld_area_is_big_backup = overworld_area_is_big;
  BYTE(overworld_area_is_big) = kOverworldMapIsSmall[i & 0x3f] ? 0 : 0x20;
  ((uint8 *)&overworld_right_bottom_bound_for_scroll)[1] = kOverworldMapIsSmall[i & 0x3f] ? 1 : 3;
  main_tile_theme_index = overworld_screen_index & 0x40 ? 0x21 : 0x20;
  misc_sprites_graphics_index = kVariousPacks[6 + (overworld_screen_index & 0x40 ? 8 : 0)];

  int j = overworld_screen_index & 0xbf;
  overworld_offset_base_y = j < 64 ? kOverworld_OffsetBaseY[j] : kOverworld_OffsetBaseY_Special[j & 63];
  overworld_offset_base_x = (j < 64 ? kOverworld_OffsetBaseX[j] : kOverworld_OffsetBaseX_Special[j & 63]) >> 3;

  int m = overworld_area_is_big ? 0x3f0 : 0x1f0;
  overworld_offset_mask_y = m;
  overworld_offset_mask_x = m >> 3;
}

void ScrollAndCheckForSOWExit() {  // 82ab7b
  if (BYTE(overworld_screen_trans_dir_bits2))
    OverworldHandleMapScroll();

  const uint16 *map8 = Overworld_GetMap16OfLink_Mult8();
  int a = map8[0] & 0x1ff;
  for (int i = 2; i >= 0; i--) {
    if (kSpecialSwitchAreaB_Map8[i] == a && kSpecialSwitchAreaB_Screen[i] == overworld_screen_index) {
      link_direction = kSpecialSwitchAreaB_Direction[i];
      byte_7E069C = overworld_screen_transition = DirToEnum(link_direction);
      submodule_index = 36;
      subsubmodule_index = 0;
      BYTE(dungeon_room_index) = 0;
      break;
    }
  }
}

void Module09_LoadAuxGFX() {  // 82ab88
  ZeldaWideBeforeAuxGfx();
  save_ow_event_info[0x3b] &= ~0x20;
  save_ow_event_info[0x7b] &= ~0x20;
  save_dung_info[267] &= ~0x80;
  save_dung_info[40] &= ~0x100;
  LoadTransAuxGFX();
  PrepTransAuxGfx();
  nmi_disable_core_updates = nmi_subroutine_index = 9;
  submodule_index++;
}

void Overworld_FinishTransGfx() {  // 82abbc
  nmi_disable_core_updates = nmi_subroutine_index = 10;
  submodule_index++;
}

void Module09_LoadNewMapAndGFX() {  // 82abc6
  word_7E04C8 = 0;
  SomeTileMapChange();
  nmi_disable_core_updates++;
  CreateInitialNewScreenMapToScroll();
  LoadNewSpriteGFXSet();
}

void Overworld_RunScrollTransition() {  // 82abda
  Link_HandleMovingAnimation_FullLongEntry();
  Graphics_IncrementalVRAMUpload();
  uint8 rv = OverworldScrollTransition();
  if (!(rv & 0xf)) {
    BYTE(overworld_screen_trans_dir_bits2) = BYTE(overworld_screen_trans_dir_bits);
    OverworldTransitionScrollAndLoadMap();
    BYTE(overworld_screen_trans_dir_bits2) = 0;
  }
}

void Module09_LoadNewSprites() {  // 82abed
  if (overworld_screen_transition == 1) {
    BG2VOFS_copy2 += 2;
    link_y_coord += 2;
  }
  Sprite_OverworldReloadAll_justLoad();
  num_memorized_tiles = 0;
  if (sram_progress_indicator >= 2 && submodule_index != 18)
    Overworld_SetFixedColAndScroll();
  Overworld_StartScrollTransition();
}

void Overworld_StartScrollTransition() {  // 82ac27
  submodule_index++;
  if (BYTE(overworld_screen_trans_dir_bits) >= 4) {
    BYTE(overworld_screen_trans_dir_bits2) = BYTE(overworld_screen_trans_dir_bits);
    OverworldTransitionScrollAndLoadMap();
    BYTE(overworld_screen_trans_dir_bits2) = 0;
  }
}

void Overworld_EaseOffScrollTransition() {  // 82ac3a
  if (kOverworldMapIsSmall[BYTE(overworld_screen_index)]) {
    BYTE(overworld_screen_trans_dir_bits2) = BYTE(overworld_screen_trans_dir_bits);
    OverworldTransitionScrollAndLoadMap();
    BYTE(overworld_screen_trans_dir_bits2) = 0;
  }
  if (++subsubmodule_index < 8)
    return;
  if ((BYTE(overworld_screen_trans_dir_bits) == 8 || BYTE(overworld_screen_trans_dir_bits) == 2) && subsubmodule_index < 9)
    return;

  subsubmodule_index = 0;
  BYTE(overworld_screen_trans_dir_bits) = 0;

  if (kOverworldMapIsSmall[BYTE(overworld_screen_index)]) {
    map16_load_src_off = orange_blue_barrier_state;
    map16_load_dst_off = word_7EC174;
    map16_load_var2 = word_7EC176;
  }
  submodule_index++;
  Follower_Disable();
}

void Module09_0A_WalkFromExiting_FacingDown() {  // 82ac8f
  link_direction_last = 4;
  Link_HandleMovingAnimation_FullLongEntry();
  link_y_coord += 1;
  if (--ow_countdown_transition)
    return;
  submodule_index = 0;
  link_y_coord += 3;
  link_y_vel = 3;
  Overworld_OperateCameraScroll();
  if (BYTE(overworld_screen_trans_dir_bits2))
    OverworldHandleMapScroll();
}

void Module09_0B_WalkFromExiting_FacingUp() {  // 82acc2
  Link_HandleMovingAnimation_FullLongEntry();
  link_y_coord -= 1;
  if (--ow_countdown_transition)
    return;
  submodule_index = 0;
}

void Module09_09_OpenBigDoorFromExiting() {  // 82ad4a
  if (BYTE(door_animation_step_indicator) != 3) {
    Overworld_DoMapUpdate32x32_conditional();
    return;
  }
  ow_countdown_transition = 36;
  BYTE(overworld_screen_trans_dir_bits2) = 0;
  submodule_index++;
}

void Overworld_DoMapUpdate32x32_B() {  // 82ad5c
  Overworld_DoMapUpdate32x32();
  BYTE(door_open_closed_counter) = 0;
}

void Module09_0C_OpenBigDoor() {  // 82ad6c
  if (BYTE(door_animation_step_indicator) != 3) {
    Overworld_DoMapUpdate32x32_conditional();
    return;
  }
  submodule_index = 0;
  subsubmodule_index = 0;
  BYTE(overworld_screen_trans_dir_bits2) = 0;
}

void Overworld_DoMapUpdate32x32_conditional() {  // 82ad7b
  if (door_open_closed_counter & 7)
    BYTE(door_open_closed_counter)++;
  else
    Overworld_DoMapUpdate32x32();
}

void Overworld_DoMapUpdate32x32() {  // 82ad85
  int i = num_memorized_tiles >> 1;
  int j = door_open_closed_counter >> 1;
  uint16 pos, tile;

  memorized_tile_addr[i] = pos = big_rock_starting_address;
  memorized_tile_value[i] = tile = kDoorAnimTiles[j + 0];
  Overworld_DrawMap16_Persist(pos, tile);

  memorized_tile_addr[i + 1] = pos = big_rock_starting_address + 2;
  memorized_tile_value[i + 1] = tile = kDoorAnimTiles[j + 1];
  Overworld_DrawMap16_Persist(pos, tile);

  memorized_tile_addr[i + 2] = pos = big_rock_starting_address + 0x80;
  memorized_tile_value[i + 2] = tile = kDoorAnimTiles[j + 2];
  Overworld_DrawMap16_Persist(pos, tile);

  memorized_tile_addr[i + 3] = pos = big_rock_starting_address + 0x82;
  memorized_tile_value[i + 3] = tile = kDoorAnimTiles[j + 3];
  Overworld_DrawMap16_Persist(pos, tile);
  vram_upload_data[vram_upload_offset >> 1] = 0xffff;
  num_memorized_tiles += 8;
  door_animation_step_indicator += (door_open_closed_counter == 32) ? 2 : 1;
  nmi_load_bg_from_vram = 1;
  BYTE(door_open_closed_counter)++;
}

void Overworld_StartMosaicTransition() {  // 82ae5e
  ConditionalMosaicControl();
  switch (subsubmodule_index) {
  case 0:
    if (BYTE(overworld_screen_index) != 0x80) {
      if (!ZeldaIsPlayingMusicTrack(overworld_music[BYTE(overworld_screen_index)] & 0xf))
        music_control = 0xf1;
    }
    ResetTransitionPropsAndAdvance_ResetInterface();
    break;
  case 1:
    ApplyPaletteFilter_bounce();
    break;
  default:
    INIDISP_copy = 0x80;
    subsubmodule_index = 0;
    if (!(overworld_screen_index & 0x3f))
      DecodeAnimatedSpriteTile_variable(0x1e);
    if (BYTE(overworld_area_index) != 0 && main_module_index != 11) {
      TM_copy = 0x16;
      TS_copy = 1;
      CGWSEL_copy = 0x82;
      CGADSUB_copy = 0x20;
      submodule_index++;
      return;
    }
    if (submodule_index == 36) {
      LoadOverworldFromSpecialOverworld();
      if (!(overworld_screen_index & 0x3f))
        DecodeAnimatedSpriteTile_variable(0x1e);
    }
    submodule_index++;
    break;
  }
}

void Overworld_LoadOverlays() {  // 82af0b
  Sprite_InitializeSlots();
  Sprite_ReloadAll_Overworld();
  link_state_bits = 0;
  link_picking_throw_state = 0;
  sound_effect_ambient = 5;
  Overworld_LoadOverlays2();

}

void PreOverworld_LoadOverlays() {  // 82af19
  sound_effect_ambient = 5;
  Overworld_LoadOverlays2();
}

void Overworld_LoadOverlays2() {  // 82af1e
  uint16 xv;

  overworld_screen_index_prev = overworld_screen_index;
  map16_load_src_off_prev = map16_load_src_off;
  map16_load_var2_prev = map16_load_var2;
  map16_load_dst_off_prev = map16_load_dst_off;
  overworld_screen_transition_prev = overworld_screen_transition;
  overworld_screen_trans_dir_bits_prev = overworld_screen_trans_dir_bits;
  overworld_screen_trans_dir_bits2_prev = overworld_screen_trans_dir_bits2;

  overlay_index = 0;
  BG1VOFS_subpixel = 0;
  BG1HOFS_subpixel = 0;

  int si = overworld_screen_index;
  if (si >= 0x80) {
    xv = 0x97;
    if (dungeon_room_index == 0x180) {
      if (save_ow_event_info[0x80] & 0x40)
        goto getout; // master sword retrieved?
      goto load_overlay;
    }
    if ((xv = 0x94, dungeon_room_index == 0x181) ||
        (xv = 0x93, dungeon_room_index == 0x189))
      goto load_overlay;

    if (dungeon_room_index == 0x182 || dungeon_room_index == 0x183)
      sound_effect_ambient = 1;  // zora falls
getout:
    TS_copy = 0;
    submodule_index++;
    return;
  }

  if ((si & 0x3f) == 0) {
    xv = (!(si & 0x40) && save_ow_event_info[0x80] & 0x40) ? 0x9e : 0x9d;  // forest
    goto load_overlay;
  }

  if ((xv = 0x95, si == 0x3 || si == 0x5 || si == 0x7) ||
      (xv = 0x9c, si == 0x43 || si == 0x45 || si == 0x47))
    goto load_overlay;
  if (si == 0x70) {
    if (!(save_ow_event_info[0x70] & 0x20))
      xv = 0x9f; // rain
  } else {
    xv = (sram_progress_indicator < 2) ? 0x9f : 0x96;
  }
load_overlay:
  map16_load_src_off = 0x390;
  overlay_index = overworld_screen_index = xv;
  map16_load_var2 = (map16_load_src_off - 0x400 & 0xf80) >> 7;
  map16_load_dst_off = (map16_load_src_off - 0x10 & 0x3e) >> 1;
  overworld_screen_transition = 0;
  overworld_screen_trans_dir_bits = 0;
  overworld_screen_trans_dir_bits2 = 0;
  CGWSEL_copy = 0x82;
  TM_copy = 0x16;
  TS_copy = 1;
  sound_effect_ambient = overworld_music[BYTE(overworld_screen_index)] >> 4;

  if (xv == 0x97 || xv == 0x94 || xv == 0x93 || xv == 0x9d || xv == 0x9e || xv == 0x9f)
    CGADSUB_copy = 0x72;
  else if (xv == 0x95 || xv == 0x9c || BYTE(overworld_screen_index_prev) == 0x5b ||
           BYTE(overworld_screen_index_prev) == 0x1b && (submodule_index == 35 || submodule_index == 44))
    CGADSUB_copy = 0x20;
  else
    TS_copy = 0, CGADSUB_copy = 0x20;

  LoadOverworldOverlay();
  if (BYTE(overlay_index) == 0x94)
    BG1VOFS_copy2 |= 0x100;

  overworld_screen_index = overworld_screen_index_prev;
  map16_load_src_off = map16_load_src_off_prev;
  map16_load_var2 = map16_load_var2_prev;
  map16_load_dst_off = map16_load_dst_off_prev;
  overworld_screen_transition = overworld_screen_transition_prev;
  overworld_screen_trans_dir_bits = overworld_screen_trans_dir_bits_prev;
  overworld_screen_trans_dir_bits2 = overworld_screen_trans_dir_bits2_prev;
}

void Module09_FadeBackInFromMosaic() {  // 82b0d2
  Overworld_ResetMosaicDown();
  switch (subsubmodule_index) {
  case 0: {
    uint8 sc = overworld_screen_index;
    Overworld_LoadPalettes(kOverworldBgPalettes[sc], overworld_sprite_palettes[sc]);
    OverworldMosaicTransition_LoadSpriteGraphicsAndSetMosaic();
    break;
  }
  case 1:
    Graphics_IncrementalVRAMUpload();
    ApplyPaletteFilter_bounce();
    break;
  default:
    last_music_control = music_unk1;
    if (BYTE(overworld_screen_index) != 0x80 && BYTE(overworld_screen_index) != 0x2a) {
      uint8 m = overworld_music[BYTE(overworld_screen_index)];
      sound_effect_ambient = (m >> 4) ? (m >> 4) : 5;
      if (!ZeldaIsPlayingMusicTrack(m & 0xf))
        music_control = (m & 0xf);
    }
    submodule_index = 8;
    subsubmodule_index = 0;
    if (main_module_index == 11) {
      main_module_index = 9;
      submodule_index = 31;
      ow_countdown_transition = 12;
    }
  }
}

void Overworld_Func1C() {  // 82b150
  Overworld_ResetMosaicDown();
  switch (subsubmodule_index) {
  case 0:
    OverworldMosaicTransition_LoadSpriteGraphicsAndSetMosaic();
    break;
  case 1:
    Graphics_IncrementalVRAMUpload();
    ApplyPaletteFilter_bounce();
    break;
  default:
    if (BYTE(overworld_screen_index) < 0x80)
      music_control = (overworld_screen_index & 0x3f) ? 2 : 5;
    submodule_index = 8;
    subsubmodule_index = 0;
    break;
  }
}

void OverworldMosaicTransition_LoadSpriteGraphicsAndSetMosaic() {  // 82b171
  LoadNewSpriteGFXSet();
  INIDISP_copy = 0xf;
  HDMAEN_copy = 0x80;
  BYTE(palette_filter_countdown) = mosaic_target_level - 1;
  mosaic_target_level = 0;
  BYTE(darkening_or_lightening_screen) = 2;
  subsubmodule_index++;
}

void Overworld_Func22() {  // 82b1bb
  if (++INIDISP_copy == 15) {
    submodule_index = 0;
    subsubmodule_index = 0;
  }
}

void Overworld_Func18() {  // 82b1c8
  link_maybe_swim_faster = 0;
  uint8 m = main_module_index;
  uint8 sub = submodule_index;
  Overworld_EnterSpecialArea();
  Overworld_LoadOverlays();
  submodule_index = sub + 1;
  main_module_index = m;
}

void Overworld_Func19() {  // 82b1df
  uint8 m = main_module_index;
  uint8 sub = submodule_index;
  Module08_02_LoadAndAdvance();
  submodule_index = sub + 1;
  main_module_index = m;
}

void Module09_MirrorWarp() {  // 82b1fa
  nmi_disable_core_updates++;
  switch (subsubmodule_index) {
  case 0:
    if (BYTE(overworld_screen_index) >= 0x80) {
      submodule_index = 0;
      subsubmodule_index = 0;
      overworld_map_state = 0;
      return;
    }
    if ((enhanced_features0 & kFeatures0_MiscBugFixes) &&
        (sound_effect_ambient_last & 0x3f) == 11)
      sound_effect_ambient = 5;
    music_control = 8;
    flag_overworld_area_did_change = 8;
    countdown_for_blink = 0x90;
    InitializeMirrorHDMA();
    savegame_is_darkworld ^= 0x40;
    word_7E04C8 = 0;
    BYTE(overworld_screen_index) = BYTE(overworld_area_index) = (overworld_screen_index & 0x3f) | savegame_is_darkworld;
    overworld_map_state = 0;
    PaletteFilter_InitializeWhiteFilter();
    Overworld_LoadGFXAndScreenSize();
    subsubmodule_index++;
    break;
  case 1:
    subsubmodule_index++;
    HDMAEN_copy = 0xc0;
  case 2:
    MirrorWarp_BuildWavingHDMATable();
    break;
  case 3:
    MirrorWarp_BuildDewavingHDMATable();
    break;
  default:
    MirrorWarp_FinalizeAndLoadDestination();
    break;
  }
}

// JP-1.0 overworld music/ambient selection (kFeatures0_JpOverworldMusic).
// Default-off and suppressed under side-by-side so the JP music — which
// diverges from the US-1.0 reference RAM every frame — never trips the
// comparator. Mirrors JpGlitchEnabled() in player.c.
static bool JpOwMusicEnabled(void) {
  return (enhanced_features0 & kFeatures0_JpOverworldMusic) &&
         !ZeldaIsEmulatorAttached();
}

void MirrorWarp_FinalizeAndLoadDestination() {  // 82b260
  HdmaSetup(0, 0xf2fb, 0x41, 0, (uint8)WH0, 0);
  IrisSpotlight_ResetTable();
  palette_filter_countdown = 0;
  darkening_or_lightening_screen = 0;
  ReloadPreviouslyLoadedSheets();
  Overworld_SetSongList();
  HDMAEN_copy = 0x80;
  if (JpOwMusicEnabled()) {
    // JP 1.0 ($82B260, off 131BD-1320D): pick the post-mirror-warp track from
    // world state + screen index + progress instead of re-reading the just-built
    // overworld_music[] scratch table. Ambient ($12D) is left untouched except
    // on the three special pyramid/HC screens 0x43/0x45/0x47.
    uint8 s = BYTE(overworld_screen_index);
    uint8 x = 4;
    if (!(savegame_is_darkworld && !link_item_moon_pearl)) {
      x = 9;
      if (s < 0x40) {
        x = 2;
        if (s == 0x18 && sram_progress_indicator < 3)
          x = 7;
      }
    }
    music_control = x;
    if (s == 0x40 || s == 0x43 || s == 0x45 || s == 0x47) {
      if (s != 0x40)
        sound_effect_ambient = 9;
      music_control = link_item_moon_pearl ? 0xd : 4;
    }
  } else {
    uint8 m = overworld_music[BYTE(overworld_screen_index)];
    music_control = m & 0xf;
    sound_effect_ambient = m >> 4;
    if (BYTE(overworld_screen_index) >= 0x40 && !link_item_moon_pearl)
      music_control = 4;
  }

  saved_module_for_menu = submodule_index;
  submodule_index = 0;
  subsubmodule_index = 0;
  overworld_map_state = 0;
  nmi_disable_core_updates = 0;
}

void Overworld_DrawScreenAtCurrentMirrorPosition() {  // 82b2e6
  uint16 bak1 = map16_load_src_off;
  uint16 bak2 = map16_load_dst_off;
  uint16 bak3 = map16_load_var2;
  if (kOverworldMapIsSmall[BYTE(overworld_screen_index)]) {
    map16_load_src_off = 0x390;
    map16_load_var2 = (0x390 - 0x400 & 0xf80) >> 7;
    map16_load_dst_off = (0x390 - 0x10 & 0x3e) >> 1;
  }
  Overworld_DrawQuadrantsAndOverlays();
  if (submodule_index == 44)
    MirrorBonk_RecoverChangedTiles();
  map16_load_var2 = bak3;
  map16_load_dst_off = bak2;
  map16_load_src_off = bak1;
}

void MirrorWarp_LoadSpritesAndColors() {  // 82b334
  countdown_for_blink = 0x90;
  uint16 bak1 = map16_load_src_off;
  uint16 bak2 = map16_load_dst_off;
  uint16 bak3 = map16_load_var2;
  if (kOverworldMapIsSmall[BYTE(overworld_screen_index)]) {
    map16_load_src_off = 0x390;
    map16_load_var2 = (0x390 - 0x400 & 0xf80) >> 7;
    map16_load_dst_off = (0x390 - 0x10 & 0x3e) >> 1;
  }
  Map16ToMap8(&g_ram[0x2000], 0);
  map16_load_var2 = bak3;
  map16_load_dst_off = bak2;
  map16_load_src_off = bak1;
  OverworldLoadScreensPaletteSet();
  uint8 sc = overworld_screen_index;
  Overworld_LoadPalettes(kOverworldBgPalettes[sc], overworld_sprite_palettes[sc]);
  Palette_SpecialOw();
  Overworld_SetFixedColAndScroll();
  if (BYTE(overworld_screen_index) == 0x1b || BYTE(overworld_screen_index) == 0x5b)
    TS_copy = 1;
  for (int i = 0; i < 16 * 6; i++)
    main_palette_buffer[32 + i] = 0x7fff;
  main_palette_buffer[0] = 0x7fff;
  if (overworld_screen_index == 0x5b) {
    main_palette_buffer[0] = 0;
    main_palette_buffer[32] = 0;
  }
  Sprite_ResetAll();
  Sprite_ReloadAll_Overworld();
  Link_ItemReset_FromOverworldThings();
  Dungeon_ResetTorchBackgroundAndPlayerInner();
  link_player_handler_state = kPlayerState_Mirror;
  // #82 Inverted: residual portal spawn-side (z3randomizer $82B34D). Vanilla
  // spawns the leftover mirror portal only in the LIGHT world (!(ow&0x40));
  // Inverted flips the home world, so spawn it in the DARK world instead.
  bool portal_away_world =
      ((enhanced_features1 & kFeatures1_RandomizerActive) &&
       Rando_GetActiveWorldState() == 2 /* kWorldState_Inverted */)
        ? (overworld_screen_index & 0x40) != 0
        : !(overworld_screen_index & 0x40);
  if (portal_away_world)
    Sprite_InitializeMirrorPortal();
}

void Overworld_Func2B() {  // 82b40a
  Palette_AnimGetMasterSword();
}

void Overworld_WeathervaneExplosion() {  // 82b40e
  // empty
}

void Module09_2E_Whirlpool() {  // 82b40f
  // this is called when entering the whirlpool
  nmi_disable_core_updates++;
  switch (subsubmodule_index) {
  case 0:
    sound_effect_1 = 0x34;
    sound_effect_ambient = 5;
    overworld_map_state = 0;
    palette_filter_countdown = 0;
    subsubmodule_index++;
    break;
  case 1:
    PaletteFilter_WhirlpoolBlue();
    break;
  case 2:
    PaletteFilter_IsolateWhirlpoolBlue();
    break;
  case 3:
    COLDATA_copy2 = 0x9f;
    overworld_palette_aux_or_main = 0;
    hud_palette = 0;
    FindPartnerWhirlpoolExit();
    BYTE(dung_draw_width_indicator) = 0;
    Overworld_LoadOverlays2();
    submodule_index--;
    nmi_subroutine_index = 12;
    flag_update_cgram_in_nmi = 0;
    COLDATA_copy2 = 0x80;
    INIDISP_copy = 0xf;
    nmi_disable_core_updates++;
    subsubmodule_index++;
    break;
  case 4: case 6:
    nmi_subroutine_index = 13;
    nmi_disable_core_updates++;
    subsubmodule_index++;
    break;
  case 5:
    Overworld_LoadOverlayAndMap();
    nmi_subroutine_index = 12;
    INIDISP_copy = 0xf;
    nmi_disable_core_updates++;
    subsubmodule_index++;
    break;
  case 7:
    Module09_LoadAuxGFX();
    submodule_index--;
    subsubmodule_index++;
    break;
  case 8:
    Overworld_FinishTransGfx();
    INIDISP_copy = 0xf;
    nmi_disable_core_updates++;
    submodule_index--;
    subsubmodule_index++;
    break;
  case 9: {
    overworld_palette_aux_or_main = 0;
    Palette_Load_SpriteMain();
    Palette_Load_SpriteEnvironment();
    Palette_Load_Sp0L();
    Palette_Load_HUD();
    Palette_Load_OWBGMain();
    uint8 sc = overworld_screen_index;
    Overworld_LoadPalettes(kOverworldBgPalettes[sc], overworld_sprite_palettes[sc]);
    Palette_SetOwBgColor();
    Overworld_SetFixedColAndScroll();
    LoadNewSpriteGFXSet();
    COLDATA_copy2 = 0x80;
    INIDISP_copy = 0xf;
    nmi_disable_core_updates++;
    subsubmodule_index++;
    break;
  }
  case 10:
    PaletteFilter_WhirlpoolRestoreRedGreen();
    if (BYTE(palette_filter_countdown))
      PaletteFilter_WhirlpoolRestoreRedGreen();
    break;
  case 11:
    Graphics_IncrementalVRAMUpload();
    PaletteFilter_WhirlpoolRestoreBlue();
    break;
  case 12:
    countdown_for_blink = 144;
    ReloadPreviouslyLoadedSheets();
    HDMAEN_copy = 0x80;
    sound_effect_ambient = overworld_music[BYTE(overworld_screen_index)] >> 4;
    music_control = savegame_is_darkworld ? 9 : 2;
    submodule_index = 0;
    subsubmodule_index = 0;
    overworld_map_state = 0;
    nmi_disable_core_updates = 0;
    break;
  }
}

void Overworld_Func2F() {  // 82b521
  dung_bg2[0x720 / 2] = 0x212;
  Overworld_Memorize_Map16_Change(0x720, 0x212);
  Overworld_DrawMap16(0x720, 0x212);
  nmi_load_bg_from_vram = 1;
  submodule_index = 0;
}

void Module09_2A_RecoverFromDrowning() {  // 82b528
  // this is called for example when entering water without swim capability
  switch (subsubmodule_index) {
  case 0:  Module09_2A_00_ScrollToLand(); break;
  default: RecoverPositionAfterDrowning(); break;
  }
}

void Module09_2A_00_ScrollToLand() {  // 82b532
  uint16 x = link_x_coord, xd = 0;
  if (x != link_x_coord_cached) {
    int16 d = (x > link_x_coord_cached) ? -1 : 1;
    if ((x += d) != link_x_coord_cached)
      x += d;
    xd = x - link_x_coord;
    link_x_coord = x;
  }
  uint16 y = link_y_coord, yd = 0;
  if (y != link_y_coord_cached) {
    int16 d = (y > link_y_coord_cached) ? -1 : 1;
    if ((y += d) != link_y_coord_cached)
      y += d;
    yd = y - link_y_coord;
    link_y_coord = y;
  }
  link_y_vel = yd;
  link_x_vel = xd;
  if (y == link_y_coord_cached && x == link_x_coord_cached) {
    subsubmodule_index++;
    link_incapacitated_timer = 0;
    set_when_damaging_enemies = 0;
  }
  Overworld_OperateCameraScroll();
  if (BYTE(overworld_screen_trans_dir_bits2))
    OverworldHandleMapScroll();
}

void Overworld_OperateCameraScroll() {  // 82bb90
  int z = (allow_scroll_z && link_z_coord != 0xffff) ? link_z_coord : 0;
  uint16 y = link_y_coord - z + 12;

  if (link_y_vel != 0) {
    int vy = sign8(link_y_vel) ? -1 : 1;
    int av = sign8(link_y_vel) ? (link_y_vel ^ 0xff) + 1 : link_y_vel;
    uint16 r4 = 0, subp;
    do {
      if (sign8(link_y_vel)) {
        if (y <= camera_y_coord_scroll_low)
          r4 += OverworldCameraBoundaryCheck(6, 0, vy, 0);
      } else {
        if (y >= camera_y_coord_scroll_hi)
          r4 += OverworldCameraBoundaryCheck(6, 2, vy, 0);
      }
    } while (--av);
    WORD(byte_7E069E[0]) = r4;
    uint8 oi = BYTE(overlay_index);
    if (oi != 0x97 && oi != 0x9d && r4 != 0) {
      if (oi == 0xb5 || oi == 0xbe) {
        subp = (r4 & 3) << 14;
        r4 >>= 2;
        if (r4 >= 0x3000)
          r4 |= 0xf000;
      } else {
        subp = (r4 & 1) << 15;
        r4 >>= 1;
        if (r4 >= 0x7000)
          r4 |= 0xf000;
      }
      uint32 tmp = BG1VOFS_subpixel | BG1VOFS_copy2 << 16;
      tmp += subp | r4 << 16;
      BG1VOFS_subpixel = (uint16)(tmp);
      BG1VOFS_copy2 = (uint16)(tmp >> 16);
      if ((overworld_screen_index & 0x3f) == 0x1b) {
        if (BG1VOFS_copy2 <= 0x600)
          BG1VOFS_copy2 = 0x600;
        else if (BG1VOFS_copy2 >= 0x6c0)
          BG1VOFS_copy2 = 0x6c0;
      }
    }
  }

  uint16 x = link_x_coord + 8;
  if (link_x_vel != 0) {
    int vx = sign8(link_x_vel) ? -1 : 1;
    int ax = sign8(link_x_vel) ? (link_x_vel ^ 0xff) + 1 : link_x_vel;
    uint16 r4 = 0, subp;
    do {
      if (sign8(link_x_vel)) {
        if (x <= camera_x_coord_scroll_low)
          r4 += OverworldCameraBoundaryCheck(0, 4, vx, 4);
      } else {
        if (x >= camera_x_coord_scroll_hi)
          r4 += OverworldCameraBoundaryCheck(0, 6, vx, 4);
      }
    } while (--ax);
    WORD(byte_7E069E[1]) = r4;
    uint8 oi = BYTE(overlay_index);
    if (oi != 0x97 && oi != 0x9d && r4 != 0) {
      if (oi == 0x95 || oi == 0x9e) {
        subp = (r4 & 3) << 14;
        r4 >>= 2;
        if (r4 >= 0x3000)
          r4 |= 0xf000;
      } else {
        subp = (r4 & 1) << 15;
        r4 >>= 1;
        if (r4 >= 0x7000)
          r4 |= 0xf000;
      }
      uint32 tmp = BG1HOFS_subpixel | BG1HOFS_copy2 << 16;
      tmp += subp | r4 << 16;
      BG1HOFS_subpixel = (uint16)(tmp), BG1HOFS_copy2 = (uint16)(tmp >> 16);
    }
  }
  if (BYTE(overworld_screen_index) != 0x47) {
    if (BYTE(overlay_index) == 0x9c) {
      uint32 tmp = BG1VOFS_subpixel | BG1VOFS_copy2 << 16;
      tmp -= 0x2000;
      BG1VOFS_subpixel = (uint16)(tmp), BG1VOFS_copy2 = (uint16)(tmp >> 16) + WORD(byte_7E069E[0]);
      BG1HOFS_copy2 = BG2HOFS_copy2;
    } else if (BYTE(overlay_index) == 0x97 || BYTE(overlay_index) == 0x9d) {
      uint32 tmp = BG1VOFS_subpixel | BG1VOFS_copy2 << 16;
      tmp += 0x2000;
      BG1VOFS_subpixel = (uint16)(tmp), BG1VOFS_copy2 = (uint16)(tmp >> 16);
      tmp = BG1HOFS_subpixel | BG1HOFS_copy2 << 16;
      tmp += 0x2000;
      BG1HOFS_subpixel = (uint16)(tmp), BG1HOFS_copy2 = (uint16)(tmp >> 16);
    }
  }

  if (dungeon_room_index == 0x181) {
    BG1VOFS_copy2 = BG2VOFS_copy2 | 0x100;
    BG1HOFS_copy2 = BG2HOFS_copy2;
  }
}

int OverworldCameraBoundaryCheck(int xa, int ya, int vd, int r8) {  // 82bd62
  ya >>= 1, r8 >>= 1;

  uint16 *xp = (xa ? &BG2VOFS_copy2 : &BG2HOFS_copy2);
  uint16 *yp = &(&ow_scroll_vars0.ystart)[ya];
  if (*xp == *yp) {
    (&overworld_unk1)[ya] = 0;
    (&overworld_unk1)[ya ^ 1] = 0;
    return 0;
  }
  *xp += vd;

  int tt = vd + (&camera_y_coord_scroll_hi)[r8];
  (&camera_y_coord_scroll_hi)[r8] = tt;
  (&camera_y_coord_scroll_low)[r8] = tt + 2;
  uint16 *op = (&overworld_unk1) + ya;
  if (!sign16(++(*op) - 0x10)) {
    (*op) -= 0x10;
    overworld_screen_trans_dir_bits2 |= kOverworld_Func2_Tab[ya];
  }
  (&overworld_unk1)[ya ^ 1] = -(&overworld_unk1)[ya];
  return vd;
}

int OverworldScrollTransition() {  // 82c001
  transition_counter++;
  int y = overworld_screen_transition;
  int d = kOverworld_Func6B_Tab1[y], rv;
  if (y < 2) {
    byte_7E069E[0] = d;
    rv = (BG2VOFS_copy2 += d);
    if (BYTE(overworld_screen_index) != 0x1b && BYTE(overworld_screen_index) != 0x5b)
      BG1VOFS_copy2 = BG2VOFS_copy2;
    if (transition_counter >= kOverworld_Func6B_Tab2[y])
      link_y_coord += d;
    if (rv != (&up_down_scroll_target)[y])
      return rv;
    if (y == 0)
      BG2VOFS_copy2 -= 2;
    link_y_coord &= ~7;
    camera_y_coord_scroll_hi = link_y_coord + kOverworld_Func6B_Tab3[y] + 11;
    camera_y_coord_scroll_low = camera_y_coord_scroll_hi + 2;
    overworld_unk1 = overworld_unk1_neg = 0;
  } else {
    byte_7E069E[1] = d;
    rv = (BG2HOFS_copy2 += d);
    if (BYTE(overworld_screen_index) != 0x1b && BYTE(overworld_screen_index) != 0x5b)
      BG1HOFS_copy2 = BG2HOFS_copy2;
    if (transition_counter >= kOverworld_Func6B_Tab2[y])
      link_x_coord += d;
    if (rv != (&up_down_scroll_target)[y])
      return rv;
    link_x_coord &= ~7;
    camera_x_coord_scroll_hi = link_x_coord + kOverworld_Func6B_Tab3[y] + 11;
    camera_x_coord_scroll_low = camera_x_coord_scroll_hi + 2;
    overworld_unk3 = overworld_unk3_neg = 0;
  }
  Overworld_SetCameraBoundaries(overworld_area_is_big != 0, (current_area_of_player >> 1) + kOverworld_Func6B_AreaDelta[y]);

  flag_overworld_area_did_change = 1;
  submodule_index += 1;
  subsubmodule_index = 0;
  transition_counter = 0;
  Sprite_InitializeSlots();
  return rv;
}

void Overworld_SetCameraBoundaries(int big, int area) {  // 82c0c3
  ow_scroll_vars0.ystart = OwOffsetBaseWord(area);
  ow_scroll_vars0.yend = ow_scroll_vars0.ystart + kOverworld_Size1[big];
  ow_scroll_vars0.xstart = OwOffsetBaseWord(area + 64);
  ow_scroll_vars0.xend = ow_scroll_vars0.xstart + kOverworld_Size2[big];
  up_down_scroll_target = OwScrollTargetWord(area);
  up_down_scroll_target_end = up_down_scroll_target + kOverworld_UpDownScrollSize[big];
  left_right_scroll_target = OwScrollTargetWord(area + 64);
  left_right_scroll_target_end = left_right_scroll_target + kOverworld_LeftRightScrollSize[big];
}

void Overworld_FinalizeEntryOntoScreen() {  // 82c242
  Link_HandleMovingAnimation_FullLongEntry();
  int d = (byte_7E069C & 1) ? 2 : -2;
  if (byte_7E069C & 2)
    link_x_coord = (d += link_x_coord);
  else
    link_y_coord = (d += link_y_coord);
  if ((d & 0xfe) == kOverworld_Func8_tab[byte_7E069C]) {
    submodule_index = 0;
    subsubmodule_index = 0;
    if (JpOwMusicEnabled()) {
      // JP 1.0 ($82C242, off 141B3-141CE): when the transition track is armed
      // (music_unk1 == 0xf1) set music_control from screen+progress rather than
      // from overworld_music[]. JP does NOT write sound_effect_ambient here.
      if (music_unk1 == 0xf1) {
        uint8 x = 2;
        if (BYTE(overworld_screen_index) == 0x18 && sram_progress_indicator < 3)
          x = 7;
        music_control = x;
      }
    } else {
      uint8 m = overworld_music[BYTE(overworld_screen_index)];
      sound_effect_ambient = m >> 4;
      if (music_unk1 == 0xf1)
        music_control = m & 0xf;
    }
  }
  Overworld_OperateCameraScroll();
  if (BYTE(overworld_screen_trans_dir_bits2))
    OverworldHandleMapScroll();
}

void Overworld_Func1F() {  // 82c2a4
  Link_HandleMovingAnimation_FullLongEntry();
  int8 vel = byte_7E069C & 1 ? 1 : -1;
  if (byte_7E069C & 2) {
    link_x_coord += vel;
    link_x_vel = vel;
  } else {
    link_y_coord += vel;
    link_y_vel = vel;
  }
  if (!--ow_countdown_transition) {
    main_module_index = 9;
    subsubmodule_index = submodule_index = 0;
  }
  Overworld_OperateCameraScroll();
}

void ConditionalMosaicControl() {  // 82c2e4
  if (palette_filter_countdown & 1)
    mosaic_level += 0x10;
  BGMODE_copy = 9;
  MOSAIC_copy = mosaic_level | 7;
}

void Overworld_ResetMosaic_alwaysIncrease() {  // 82c2eb
  mosaic_level += 0x10;
  BGMODE_copy = 9;
  MOSAIC_copy = mosaic_level | 7;
}

void Overworld_SetSongList() {  // 82c463
  uint8 r0 = 2, y = 0xc0;
  if (sram_progress_indicator < 3) {
    y = 0x80;
    if (link_sword_type < 2) {
      r0 = 5;
      y = 0x40;
      if (sram_progress_indicator < 2)
        y = 0;
    }
  }
  memcpy(overworld_music, &kOwMusicSets[y], 64);
  memcpy(overworld_music + 64, kOwMusicSets2, 96);
  overworld_music[128] = r0;
}

void LoadOverworldFromDungeon() {  // 82e4a3
  player_is_indoors = 0;
  hdr_dungeon_dark_with_lantern = 0;
  WORD(overworld_fixed_color_plusminus) = 0;
  cur_palace_index_x2 = 0xff;
  num_memorized_tiles = 0;

  // Phase C Stage 2 — dungeon entrance-shuffle coupling. CONSUME the source-room
  // override UNCONDITIONALLY here (audit HIGH-1): this function is reached not just
  // by the room-keyed exit search below but also by Magic-Mirror / special-area /
  // ending warps (overworld.c:697,1914, ending.c) and the cached-exit `if` branch;
  // clearing only in the search branch let a mirror-out of a shuffled dungeon leak
  // a stale source room into a LATER unrelated exit → wrong-door warp. Capture once
  // into a local, zero the global, then use the local only in the search branch.
  uint16 coupled_exit_room = g_rando_entrance_exit_room;
  g_rando_entrance_exit_room = 0;
  // Phase C Stage 3 — a cave→dungeon cross redirect loads a dungeon room (which
  // would take the search branch) but must return to the SOURCE cave door; the
  // cached *_exit vars hold that cave position, so force the cached branch.
  bool force_cached = (g_rando_entrance_force_cached != 0);
  g_rando_entrance_force_cached = 0;
  // The source cave's room, captured beside the flag. Consume-at-top (same as the
  // flag) so a stray non-cached exit can't leave it armed for a later one.
  uint16 force_cached_room = g_rando_force_cached_room;
  g_rando_force_cached_room = 0;
  // Decoupled (D.4) — consume the entered-interior the SAME way (unconditionally
  // at the top), so a mirror/special/ending warp can't leave it set for a later
  // cave-class exit to misuse. Only the cave-class branch below acts on it.
  uint16 decoupled_entered = Rando_DecoupledConsumeEntered();
  // Cross + decoupled (Insanity + Crossed): consume the resolved one-way exit kind
  // (0 none / 1 cave / 2 dungeon). A CAVE target forces the cached branch (replay
  // that cave's arrival); a DUNGEON target forces the search branch at its room
  // (coupled_exit_room was set to it at the entry hook). kind 0 leaves the normal
  // branch selection. Consumed-at-top like the sibling exit-coupling globals.
  uint8 cross_cave = 0xFF;
  uint8 cross_kind = Rando_CrossDecoupledConsumeExit(&cross_cave);

  // add-rando-inverted-dark-chapel-spawn: consume the Inverted spawn-select
  // redirect the SAME way (clear the global into a local at the top) so a stray
  // mirror / special / ending warp can't leave it armed for a later unrelated
  // exit. The redirect drives the room-keyed SEARCH branch to OR 0x40 into the
  // exit screen. As built this drives only Mountain Cave (0x03 -> 0x43); the Dark
  // Chapel exits via its own cached *_exit cache (screen 0x53 baked in, see below)
  // and Link's House is already 0x6C, so the redirect is a no-op for both.
  bool inverted_spawn_redirect = (g_rando_inverted_spawn_redirect != 0);
  g_rando_inverted_spawn_redirect = 0;

  bool take_cached;
  if (cross_kind != 0) {
    take_cached = (cross_kind == 1);  // cave → cached replay; dungeon → search
  } else {
    take_cached = force_cached ||
        (dungeon_room_index != 0x104 && dungeon_room_index < 0x180 && dungeon_room_index >= 0x100);
  }
  // add-rando-inverted-dark-chapel-spawn: the Dark Chapel interior is room 0x112
  // (>= 0x100, != 0x104), so it takes the cached-exit branch below. The chapel spawn
  // (Dungeon_LoadEntrance) pre-loads the genuine DW screen-0x53 arrival into the
  // *_exit cache, so the cached branch replays the real chapel screen — same as the
  // normal "walk into the dark sanctuary" entry. No screen-borrow / redirect needed.
  if (take_cached) {
    // Decoupled (D.4): on a cave-class exit, replay the target cave's captured
    // arrival so Link emerges at a DIFFERENT door (it overwrites the live *_exit
    // block + world flags + target room/door-settings). For cross-decoupled the
    // target cave was resolved at entry (cross_cave); otherwise it's net[entered].
    // No-op / coupled return when inactive or the target hasn't been captured.
    bool decoupled_crossed = (cross_kind == 1)
        ? Rando_CrossDecoupledReplayCave(cross_cave)
        : Rando_DecoupledReplaceArrival(decoupled_entered);
    // Cross-category coupled cave→dungeon (force_cached): the cached *_exit block
    // returns Link to the source cave door, but dungeon_room_index still holds the
    // loaded DUNGEON room (< 0x124), so LoadCachedEntranceProperties' vanilla
    // room-keyed Y-adjust (`if (dungeon_room_index < 0x124) link_y -= 0x10`) would
    // shift Link up 0x10px for caves whose OWN room is >= 0x124 (thief_hideout
    // 0x124, thief_hideout_3 0x125, fairy_cave_5 0x126, heart_piece_cave_3 0x127).
    // Restore the source cave room so the adjust keys off the cave. The decoupled
    // paths already reset dungeon_room_index via their replay helpers, so guard on
    // !decoupled_crossed. PLAYTEST-PENDING: confirm Link's emergence Y with an F12
    // dump on a coupled cross seed whose cave→dungeon source room is >= 0x124.
    if (force_cached && force_cached_room != 0 && !decoupled_crossed)
      dungeon_room_index = force_cached_room;
    LoadCachedEntranceProperties();
    if (decoupled_crossed) {
      // A one-way decoupled exit can land Link in the OTHER world, but this cached
      // path (unlike the mirror warp, ancilla.c) never re-evaluates Link's bunny
      // form for the destination — leaving a DW→LW exit stuck as a Light-World
      // bunny (and a LW→DW exit wrongly human). Re-derive it from the destination
      // world + Moon Pearl, matching PreOverworld_LoadProperties / the mirror warp.
      if (link_item_moon_pearl || !savegame_is_darkworld) {
        ForceNonbunnyStatus();
      } else {
        link_is_bunny = link_is_bunny_mirror = 1;
        link_player_handler_state = kPlayerState_PermaBunny;
        LoadGearPalettes_bunny();
      }
    } else if (enhanced_features1 & kFeatures1_RandomizerActive) {
      // add-rando-inverted-dark-chapel-spawn: a cached exit can cross worlds — the
      // Inverted Dark Chapel is a Dark-World interior whose cached arrival is DW
      // screen 0x53. LoadCachedEntranceProperties restores the screen index (world
      // bit) but not savegame_is_darkworld / is_in_dark_world / Link's bunny form;
      // sync them from the restored screen (mirroring the search branch). GATED on
      // RandomizerActive (defense-in-depth): in vanilla a cached exit never changes
      // the world — the entry screen's world bit always equals the unchanged
      // savegame_is_darkworld, so the inner `if` is already a no-op — but gating
      // guarantees the RAM-compare can't diverge even if a future vanilla path breaks
      // that load-bearing invariant.
      uint8 dest_dark = (overworld_screen_index & 0x40) ? 1 : 0;
      if (dest_dark != (savegame_is_darkworld != 0)) {
        savegame_is_darkworld = dest_dark;
        is_in_dark_world = dest_dark;
        if (link_item_moon_pearl || !dest_dark) {
          ForceNonbunnyStatus();
        } else {
          link_is_bunny = link_is_bunny_mirror = 1;
          link_player_handler_state = kPlayerState_PermaBunny;
          LoadGearPalettes_bunny();
        }
      }
    }
    // add-rando-inverted-dark-chapel-spawn: position Link to re-enter the Dark Chapel.
    // Screen 0x53's STATIC map already contains the enterable door — the entrance
    // (ent[89], tilemap pos 0x2AA = col 21, row 5) fires when Link is FACING UP there,
    // because the up-facing door check reads the cell one to the RIGHT (col 22 = map16
    // 0xE0, whose subtiles are in kOverworld_Entrance_Tab0/1; asset-decode-verified).
    // So NO entrance marker is needed (and must NOT be painted — 0xDA4/0xDA6 would
    // overwrite that 0xE0 and kill the door). Runtime logs proved the trigger is a
    // precise spot: Link must walk straight UP column 21 (x=0x758) into row 5. The
    // generic cached `link_y -= 0x10` left Link NORTH of it; a south-facing exit made
    // him turn before walking up and drift off the column to col 22, missing the door.
    // Fix: clear any marker, pin Link to column 21 just SOUTH of the door, and face
    // him UP so simply holding "up" walks him straight into it — exactly the genuine
    // Link's-House->dark-sanctuary approach. (map16_load_src_off_exit=0x9C, set at
    // spawn, keeps the tilemap aligned so 0xE0 lands at col 22.)
    if (dungeon_room_index == 0x112 &&
        (enhanced_features1 & kFeatures1_RandomizerActive) &&
        Rando_GetActiveWorldState() == 2) {
      ow_entrance_value = 0;          // no marker; the static 0xE0 door must survive
      link_x_coord = 0x0758;          // column 21 — the chapel door's column (exact)
      link_y_coord = 0x0466;          // row 6, just below the door (closer to the real
                                      // exit spot; still south of the row-5 trigger so
                                      // walking up re-enters)
      WORD(link_direction_facing) = 2; // face DOWN: avoids auto-walking back into the door
    }
  } else {

    // If the player entered a shuffled dungeon door, key the exit search on the
    // SOURCE dungeon's room (so they return to the door they entered) instead of
    // the loaded dungeon's room. 0 = normal exit.
    uint16 exit_room = (coupled_exit_room != 0) ? coupled_exit_room : dungeon_room_index;
    int k = 79;
    do k--; while (k > 0 && kExitDataRooms[k] != exit_room);
    // The decreasing search lands on slot 0 (a WRONG but in-bounds door) when
    // exit_room isn't in kExitDataRooms. A dungeon-decoupled / cross-decoupled
    // redirect can key the search on a SOURCE-dungeon room (coupled_exit_room)
    // that has no overworld-exit entry; fall back to the coupled return-to-source
    // (dungeon_room_index, which a real exit always carries) rather than slot 0.
    // RandomizerActive-gated + only when the table miss happened on a coupled
    // override — vanilla (coupled_exit_room == 0 ⇒ exit_room == dungeon_room_index)
    // never re-searches, so the vanilla door choice stays byte-identical.
    if ((enhanced_features1 & kFeatures1_RandomizerActive) &&
        kExitDataRooms[k] != exit_room && exit_room != dungeon_room_index) {
      k = 79;
      do k--; while (k > 0 && kExitDataRooms[k] != dungeon_room_index);
    }
    uint16 chain_origin_room = 0;
    if (enhanced_features1 & kFeatures1_RandomizerActive)
      chain_origin_room = Chains_RuntimeConsumeMainExitOrigin(kExitDataRooms[k]);
    if (chain_origin_room != 0 && chain_origin_room != exit_room) {
      exit_room = chain_origin_room;
      k = 79;
      do k--; while (k > 0 && kExitDataRooms[k] != exit_room);
    }
    BG1VOFS_copy2 = BG2VOFS_copy2 = BG1VOFS_copy = BG2VOFS_copy = kExitData_ScrollY[k];
    BG1HOFS_copy2 = BG2HOFS_copy2 = BG1HOFS_copy = BG2HOFS_copy = kExitData_ScrollX[k];
    link_y_coord = kExitData_YCoord[k];
    link_x_coord = kExitData_XCoord[k];
    map16_load_src_off = kExitData_Map16LoadSrcOff[k];
    map16_load_var2 = (map16_load_src_off - 0x400 & 0xf80) >> 7;
    map16_load_dst_off = (map16_load_src_off - 0x10 & 0x3e) >> 1;
    camera_y_coord_scroll_low = kExitData_CameraYScroll[k];
    camera_y_coord_scroll_hi = camera_y_coord_scroll_low - 2;
    camera_x_coord_scroll_low = kExitData_CameraXScroll[k];
    camera_x_coord_scroll_hi = camera_x_coord_scroll_low - 2;
    WORD(link_direction_facing) = 2;
    ow_entrance_value = kExitData_NormalDoor[k];
    big_rock_starting_address = kExitData_FancyDoor[k];
    overworld_area_index = overworld_screen_index = kExitData_ScreenIndex[k];
    overworld_unk1 = kExitData_Unk1[k];
    overworld_unk3 = kExitData_Unk3[k];
    overworld_unk1_neg = -overworld_unk1;
    overworld_unk3_neg = -overworld_unk3;
    // add-rando-inverted-dark-chapel-spawn: when this overworld load is an Inverted
    // spawn-select respawn (flag armed in Module1B_SpawnSelect), force the anchor
    // into the Dark World. Vanilla exit screens are LW (Sanctuary 0x13, Mountain
    // Cave 0x03, Link's House 0x2C); ORing the world bit lands the DW mirror
    // (0x53 "Dark Chapel" / 0x43 DW Death Mountain / 0x6C — the last already DW via
    // the Link's-House asset override, so this is a no-op there). Setting the world
    // bit HERE (before the sync block below) lets that block flip
    // savegame_is_darkworld / is_in_dark_world / bunny for us — same path a
    // cross-world dungeon exit uses. Done only in the room-keyed search branch, so
    // a normal Sanctuary/Mountain-Cave *check* visit (flag not armed) is unaffected.
    if (inverted_spawn_redirect) {
      overworld_screen_index |= 0x40;
      overworld_area_index |= 0x40;
    }
    // (The Dark Chapel, room 0x112, no longer reaches this search branch — it takes
    // the cached-exit branch above with a pre-loaded genuine screen-0x53 arrival.
    // Mountain Cave / Link's House still use this branch + the 0x40 redirect.)
    // Dungeon decoupled (Insanity): a one-way dungeon exit can emerge in the OTHER
    // world (the destination is a DIFFERENT dungeon's door). This search branch sets
    // the screen index (world bit 0x40) but NOT savegame_is_darkworld / is_in_dark_
    // world / Link's bunny form — the mirror warp normally does that. Sync them when
    // the destination world differs from the entry world, else a cross-world dungeon
    // exit leaves stale world flags (wrong area-head at the next screen edge) and a
    // wrong/stuck bunny. No-op for coupled/vanilla exits (same world ⇒ no change).
    uint8 dest_dark = (overworld_screen_index & 0x40) ? 1 : 0;
    if (dest_dark != (savegame_is_darkworld != 0)) {
      savegame_is_darkworld = dest_dark;
      is_in_dark_world = dest_dark;
      if (link_item_moon_pearl || !dest_dark) {
        ForceNonbunnyStatus();
      } else {
        link_is_bunny = link_is_bunny_mirror = 1;
        link_player_handler_state = kPlayerState_PermaBunny;
        LoadGearPalettes_bunny();
      }
    }
  }
  Overworld_LoadNewScreenProperties();
}

void Overworld_LoadNewScreenProperties() {  // 82e58b
  tilemap_location_calc_mask = ~7;
  Overworld_LoadGFXAndScreenSize();
  BYTE(overworld_right_bottom_bound_for_scroll) = 0xe4;
  overworld_area_is_big &= 0xff;
  Overworld_SetCameraBoundaries(overworld_area_is_big != 0, overworld_screen_index & 0x3f);
  link_quadrant_x = 0;
  link_quadrant_y = 2;
  quadrant_fullsize_x = 2;
  quadrant_fullsize_y = 2;
  player_oam_x_offset = player_oam_y_offset = 0x80;
  link_direction_mask_a = link_direction_mask_b = 0xf;
  BYTE(link_z_coord) = 0xff;
  link_actual_vel_z = 0xff;
}

void LoadCachedEntranceProperties() {  // 82e5d4
  overworld_area_index = overworld_area_index_exit;
  WORD(TM_copy) = WORD(TM_copy_exit);
  BG2VOFS_copy2 = BG2VOFS_copy = BG1VOFS_copy2 = BG1VOFS_copy = BG2VOFS_copy2_exit;
  BG2HOFS_copy2 = BG2HOFS_copy = BG1HOFS_copy2 = BG1HOFS_copy = BG2HOFS_copy2_exit;
  link_x_coord = link_x_coord_exit;
  link_y_coord = link_y_coord_exit;
  if (dungeon_room_index < 0x124)
    link_y_coord -= 0x10;
  WORD(link_direction_facing) = 2;
  if (ow_entrance_value == 0xffff) {
    link_y_coord += 0x20;
    WORD(link_direction_facing) = 0;
  }
  overworld_screen_index = overworld_screen_index_exit;
  map16_load_src_off = map16_load_src_off_exit;
  map16_load_var2 = (map16_load_src_off - 0x400 & 0xf80) >> 7;
  map16_load_dst_off = (map16_load_src_off - 0x10 & 0x3e) >> 1;
  camera_y_coord_scroll_low = camera_y_coord_scroll_low_exit;
  camera_y_coord_scroll_hi = camera_y_coord_scroll_low - 2;
  camera_x_coord_scroll_low = camera_x_coord_scroll_low_exit;
  camera_x_coord_scroll_hi = camera_x_coord_scroll_low - 2;
  ow_scroll_vars0 = ow_scroll_vars0_exit;
  up_down_scroll_target = up_down_scroll_target_exit;
  up_down_scroll_target_end = up_down_scroll_target_end_exit;
  left_right_scroll_target = left_right_scroll_target_exit;
  left_right_scroll_target_end = left_right_scroll_target_end_exit;
  overworld_unk1 = overworld_unk1_exit;
  overworld_unk1_neg = overworld_unk1_neg_exit;
  overworld_unk3 = overworld_unk3_exit;
  overworld_unk3_neg = overworld_unk3_neg_exit;
  byte_7E0AA0 = byte_7EC164;
  main_tile_theme_index = main_tile_theme_index_exit;
  aux_tile_theme_index = aux_tile_theme_index_exit;
  sprite_graphics_index = sprite_graphics_index_exit;

}

void Overworld_EnterSpecialArea() {  // 82e851
  num_memorized_tiles = 0;
  overworld_area_index_spexit = overworld_area_index;
  WORD(TM_copy_spexit) = WORD(TM_copy);
  BG2VOFS_copy2_spexit = BG2VOFS_copy2;
  BG2HOFS_copy2_spexit = BG2HOFS_copy2;

  link_x_coord_spexit = link_x_coord;
  link_y_coord_spexit = link_y_coord;

  camera_y_coord_scroll_low_spexit = camera_y_coord_scroll_low;
  camera_x_coord_scroll_low_spexit = camera_x_coord_scroll_low;
  overworld_screen_index_spexit = overworld_screen_index;
  map16_load_src_off_spexit = map16_load_src_off;
  room_scroll_vars0_ystart_spexit = ow_scroll_vars0.ystart;
  room_scroll_vars0_yend_spexit = ow_scroll_vars0.yend;
  room_scroll_vars0_xstart_spexit = ow_scroll_vars0.xstart;
  room_scroll_vars0_xend_spexit = ow_scroll_vars0.xend;

  up_down_scroll_target_spexit = up_down_scroll_target;
  up_down_scroll_target_end_spexit = up_down_scroll_target_end;
  left_right_scroll_target_spexit = left_right_scroll_target;
  left_right_scroll_target_end_spexit = left_right_scroll_target_end;
  overworld_unk1_spexit = overworld_unk1;
  overworld_unk1_neg_spexit = overworld_unk1_neg;
  overworld_unk3_spexit = overworld_unk3;
  overworld_unk3_neg_spexit = overworld_unk3_neg;
  byte_7EC124 = byte_7E0AA0;
  main_tile_theme_index_spexit = main_tile_theme_index;
  aux_tile_theme_index_spexit = aux_tile_theme_index;
  sprite_graphics_index_spexit = sprite_graphics_index;
  LoadOverworldFromDungeon();
  if (dungeon_room_index == 0x1010)
    dungeon_room_index = 0x182;

  uint8 roombak = dungeon_room_index;
  int i = (BYTE(dungeon_room_index) -= 0x80);
  link_direction_facing = kSpExit_Dir[i];
  incremental_counter_for_vram = 0;
  sprite_graphics_index = kSpExit_SprGfx[i];
  aux_tile_theme_index = kSpExit_AuxGfx[i];
  Overworld_LoadPalettes(kSpExit_PalBg[i], kSpExit_PalSpr[i]);

  int j = dungeon_room_index & 0x3f;
  overworld_offset_base_y = kSpExit_Top[j];
  overworld_offset_base_x = kSpExit_LeftEdgeOfMap[j] >> 3;
  overworld_offset_mask_y = 0x3f0;
  overworld_offset_mask_x = 0x3f0 >> 3;

  int k = dungeon_room_index & 0x7f;
  ow_scroll_vars0.ystart = kSpExit_Top[k];
  ow_scroll_vars0.yend = kSpExit_Bottom[k];
  ow_scroll_vars0.xstart = kSpExit_Left[k];
  ow_scroll_vars0.xend = kSpExit_Right[k];
  up_down_scroll_target = kSpExit_Tab4[k];
  up_down_scroll_target_end = kSpExit_Tab5[k];
  left_right_scroll_target = kSpExit_Tab6[k];
  left_right_scroll_target_end = kSpExit_Tab7[k];

  BYTE(dungeon_room_index) = roombak;
  Palette_SpecialOw();
}

void LoadOverworldFromSpecialOverworld() {  // 82e9bc
  num_memorized_tiles = 0;
  overworld_area_index = overworld_area_index_spexit;
   WORD(TM_copy) = WORD(TM_copy_spexit);
  BG2VOFS_copy2 = BG2VOFS_copy = BG1VOFS_copy2 = BG1VOFS_copy = BG2VOFS_copy2_spexit;
  BG2HOFS_copy2 = BG2HOFS_copy = BG1HOFS_copy2 = BG1HOFS_copy = BG2HOFS_copy2_spexit;
  link_x_coord = link_x_coord_spexit;
  link_y_coord = link_y_coord_spexit;
  overworld_screen_index = overworld_screen_index_spexit;
  map16_load_src_off = map16_load_src_off_spexit;
  map16_load_var2 = (map16_load_src_off - 0x400 & 0xf80) >> 7;
  map16_load_dst_off = (map16_load_src_off - 0x10 & 0x3e) >> 1;
  camera_y_coord_scroll_low = camera_y_coord_scroll_low_spexit;
  camera_y_coord_scroll_hi = camera_y_coord_scroll_low - 2;
  camera_x_coord_scroll_low = camera_x_coord_scroll_low_spexit;
  camera_x_coord_scroll_hi = camera_x_coord_scroll_low - 2;
  ow_scroll_vars0.ystart = room_scroll_vars0_ystart_spexit;
  ow_scroll_vars0.yend = room_scroll_vars0_yend_spexit;
  ow_scroll_vars0.xstart = room_scroll_vars0_xstart_spexit;
  ow_scroll_vars0.xend = room_scroll_vars0_xend_spexit;
  up_down_scroll_target = up_down_scroll_target_spexit;
  up_down_scroll_target_end = up_down_scroll_target_end_spexit;
  left_right_scroll_target = left_right_scroll_target_spexit;
  left_right_scroll_target_end = left_right_scroll_target_end_spexit;
  overworld_unk1 = overworld_unk1_spexit;
  overworld_unk1_neg = overworld_unk1_neg_spexit;
  overworld_unk3 = overworld_unk3_spexit;
  overworld_unk3_neg = overworld_unk3_neg_spexit;
  byte_7E0AA0 = byte_7EC124;
  main_tile_theme_index = main_tile_theme_index_spexit;
  aux_tile_theme_index = aux_tile_theme_index_spexit;
  sprite_graphics_index = sprite_graphics_index_spexit;
  uint8 sc = overworld_screen_index;
  Overworld_LoadPalettes(kOverworldBgPalettes[sc], overworld_sprite_palettes[sc]);
  Palette_SpecialOw();
  link_quadrant_x = 0;
  link_quadrant_y = 2;
  quadrant_fullsize_x = 2;
  quadrant_fullsize_y = 2;
  player_oam_x_offset = player_oam_y_offset = 0x80;
  link_direction_mask_a = link_direction_mask_b = 0xf;
  BYTE(link_z_coord) = 0xff;
  link_actual_vel_z = 0xff;
  Link_ResetSwimmingState();
  Overworld_LoadGFXAndScreenSize();
  BYTE(overworld_right_bottom_bound_for_scroll) = 228;
  overworld_area_is_big &= 0xff;
}

void FluteMenu_LoadTransport() {  // 82ec39
  num_memorized_tiles = 0;
  int k = birdtravel_var1[0];
  WORD(birdtravel_var1[0]) <<= 1;
  Overworld_LoadBirdTravelPos(k);
}

void Overworld_LoadBirdTravelPos(int k) {  // 82ec47
  // add-rando-ow-warp-shuffle — under an active flute shuffle, slots 0-7
  // install the per-seed candidate's arrival tableau (same field writes as
  // the vanilla body below; the OwWarp selfcheck oracle pins the candidate
  // columns byte-equal to the kBirdTravel_* rows for the vanilla screens).
  // Whirlpool rows (k >= 9) never take this branch.
  const RandoOwFluteCandidate *rc = Rando_OwWarp_FluteSpotForBirdIndex(k);
  if (rc != NULL) {
    num_memorized_tiles = 0;
    BG1VOFS_copy2 = BG2VOFS_copy2 = BG1VOFS_copy = BG2VOFS_copy = rc->bg_y;
    BG1HOFS_copy2 = BG2HOFS_copy2 = BG1HOFS_copy = BG2HOFS_copy = rc->bg_x;
    link_y_coord = rc->link_y;
    link_x_coord = rc->link_x;
    overworld_unk1 = (uint16)rc->unk1;
    overworld_unk3 = (uint16)rc->unk2;
    overworld_unk1_neg = -overworld_unk1;
    overworld_unk3_neg = -overworld_unk3;
    overworld_area_index = overworld_screen_index = (uint8)rc->screen;
    map16_load_src_off = rc->vram;
    map16_load_var2 = (map16_load_src_off - 0x400 & 0xf80) >> 7;
    map16_load_dst_off = (map16_load_src_off - 0x10 & 0x3e) >> 1;
    camera_y_coord_scroll_low = rc->cam_y;
    camera_y_coord_scroll_hi = camera_y_coord_scroll_low - 2;
    camera_x_coord_scroll_low = rc->cam_x;
    camera_x_coord_scroll_hi = camera_x_coord_scroll_low - 2;
    ow_entrance_value = 0;
    big_rock_starting_address = 0;
    Overworld_LoadNewScreenProperties();
    Sprite_ResetAll();
    Sprite_ReloadAll_Overworld();
    is_standing_in_doorway = 0;
    // audit M2: the vanilla body below ends with this unconditional reset
    // (select-interactive ancilla termination + run/aux/handler state);
    // omitting it let live state carry across a shuffled-flute warp.
    Dungeon_ResetTorchBackgroundAndPlayerInner();
    return;
  }
  BG1VOFS_copy2 = BG2VOFS_copy2 = BG1VOFS_copy = BG2VOFS_copy = kBirdTravel_ScrollY[k];
  BG1HOFS_copy2 = BG2HOFS_copy2 = BG1HOFS_copy = BG2HOFS_copy = kBirdTravel_ScrollX[k];
  link_y_coord = kBirdTravel_LinkYCoord[k];
  link_x_coord = kBirdTravel_LinkXCoord[k];
  overworld_unk1 = kBirdTravel_Unk1[k];
  overworld_unk3 = kBirdTravel_Unk3[k];
  overworld_unk1_neg = -overworld_unk1;
  overworld_unk3_neg = -overworld_unk3;
  overworld_area_index = overworld_screen_index = kBirdTravel_ScreenIndex[k];

  map16_load_src_off = kBirdTravel_Map16LoadSrcOff[k];
  map16_load_var2 = (map16_load_src_off - 0x400 & 0xf80) >> 7;
  map16_load_dst_off = (map16_load_src_off - 0x10 & 0x3e) >> 1;
  camera_y_coord_scroll_low = kBirdTravel_CameraYScroll[k];
  camera_y_coord_scroll_hi = camera_y_coord_scroll_low - 2;
  camera_x_coord_scroll_low = kBirdTravel_CameraXScroll[k];
  camera_x_coord_scroll_hi = camera_x_coord_scroll_low - 2;
  ow_entrance_value = 0;
  big_rock_starting_address = 0;
  Overworld_LoadNewScreenProperties();
  Sprite_ResetAll();
  Sprite_ReloadAll_Overworld();
  is_standing_in_doorway = 0;
  Dungeon_ResetTorchBackgroundAndPlayerInner();
}

void FluteMenu_LoadSelectedScreenPalettes() {  // 82ecdd
  OverworldLoadScreensPaletteSet();
  uint8 sc = overworld_screen_index;
  Overworld_LoadPalettes(kOverworldBgPalettes[sc], overworld_sprite_palettes[sc]);
  Palette_SetOwBgColor();
  Overworld_LoadPalettesInner();
}

void FindPartnerWhirlpoolExit() {  // 82ed08
  int j = FindInWordArray(kWhirlpoolAreas, overworld_screen_index, kWhirlpoolAreas_SIZE / 2);
  // add-rando-ow-warp-shuffle — re-pair: swap the ENTERED row for the row
  // whose (+9) tableau lands at the per-seed partner (identity when the
  // axis is off; screen-keyed so table order cannot desync).
  if (j >= 0) j = Rando_OwWarp_WhirlpoolBirdRow((uint8)j);
  if (j >= 0) {
    num_memorized_tiles = 0;
    Overworld_LoadBirdTravelPos(j + 9);
  }
}

void Overworld_LoadAmbientOverlay(bool load_map_data) {  // 82ed25
  uint16 bak1 = map16_load_src_off;
  uint16 bak2 = map16_load_dst_off;
  uint16 bak3 = map16_load_var2;

  if (kOverworldMapIsSmall[BYTE(overworld_screen_index)]) {
    map16_load_src_off = 0x390;
    map16_load_var2 = (0x390 - 0x400 & 0xf80) >> 7;
    map16_load_dst_off = (0x390 - 0x10 & 0x3e) >> 1;
  }

  if (load_map_data)
    Overworld_DrawQuadrantsAndOverlays();

  Map16ToMap8(&g_ram[0x2000], 0);
  map16_load_var2 = bak3;
  map16_load_dst_off = bak2;
  map16_load_src_off = bak1;

  nmi_subroutine_index = 4;
  nmi_disable_core_updates = 4;
  submodule_index++;
  INIDISP_copy = 0;
}

void Overworld_LoadAmbientOverlayFalse() {  // 82ed24
  Overworld_LoadAmbientOverlay(false);
}

void Overworld_LoadAndBuildScreen() {  // 82ed59
  Overworld_LoadAmbientOverlay(true);
}

void Module08_02_LoadAndAdvance() {  // 82edb9
  Overworld_LoadAndBuildScreen();
  main_module_index = 16;
  submodule_index = 0;
  subsubmodule_index = 0;
}

void Overworld_DrawQuadrantsAndOverlays() {  // 82eec5
  Overworld_DecompressAndDrawAllQuadrants();
  for (int i = 0; i < 16 * 4; i++)
    dung_bg1[i] = 0xdc4;
  uint16 pos = ow_entrance_value;
  if (pos != 0 && pos != 0xffff) {
    if (pos < 0x8000) {
      dung_bg2[pos >> 1] = 0xDA4;
      Overworld_Memorize_Map16_Change(pos, 0xda4);
      dung_bg2[(pos + 2) >> 1] = 0xda6;
      Overworld_Memorize_Map16_Change(pos + 2, 0xda6);
    } else {
      pos &= 0x1fff;
      dung_bg2[pos >> 1] = 0xdb4;
      Overworld_Memorize_Map16_Change(pos, 0xdb4);
      dung_bg2[(pos + 2) >> 1] = 0xdb5;
      Overworld_Memorize_Map16_Change(pos + 2, 0xdb5);
    }
    ow_entrance_value = 0;
  }
  Overworld_HandleOverlaysAndBombDoors();

  // #82 Inverted overworld topology: after the screen's map16 buffer is built
  // (quadrants + entrance markers + vanilla event overlays), apply the inverted
  // per-screen tile-overlay set, mirroring z3randomizer's Overworld_LoadNewTiles
  // hook ("executed right after the original tile load"). Gated on the active
  // Inverted rando world-state so vanilla / Open / Standard / Retro screens are
  // byte-identical. This is purely a render-buffer rewrite (consumed by the
  // following Map16ToMap8); no game-logic / RAM-compare-relevant state changes.
  if ((enhanced_features1 & kFeatures1_RandomizerActive) &&
      Rando_GetActiveWorldState() == 2 /* kWorldState_Inverted */) {
    Overworld_ApplyInvertedTiles();
  }
}

void Overworld_HandleOverlaysAndBombDoors() {  // 82ef29
  // #82 Inverted: the two hardcoded Light-World rocks (screens 0x33 / 0x2F) are
  // removed in Inverted mode (z3randomizer inverted.asm HardcodedRocks: skip the
  // 0x020F rock write when InvertedMode). Otherwise the LW (reached via mirror)
  // keeps a rock that the inverted topology expects gone.
  bool rando_inverted_ow =
      (enhanced_features1 & kFeatures1_RandomizerActive) &&
      Rando_GetActiveWorldState() == 2 /* kWorldState_Inverted */;
  if (!rando_inverted_ow) {
    // A quest can leave a tile out: its branch then always skips ($82:EF31,
    // $82:EF3A).
    uint16 tile = Mods_RomWord(0x82ef2a, 0x20f);
    if (overworld_screen_index == 0x33 && Mods_RomByte(0x82ef31, 0xd0) == 0xd0)
      dung_bg2[340] = tile;
    else if (overworld_screen_index == 0x2f && Mods_RomByte(0x82ef3a, 0xd0) == 0xd0)
      dung_bg2[1497] = tile;
  }
  if (rando_inverted_ow && BYTE(overworld_screen_index) == 0x47)
    save_ow_event_info[0x47] |= 0x20;
  if (BYTE(overworld_screen_index) < 0x80 && save_ow_event_info[BYTE(overworld_screen_index)] & 0x20)
    Overworld_LoadEventOverlay();
  if (save_ow_event_info[BYTE(overworld_screen_index)] & 2) {
    int pos = kSecondaryOverlayPerOw[overworld_screen_index] >> 1;
    dung_bg2[pos + 0] = 0xdb4;
    dung_bg2[pos + 1] = 0xdb5;
  }
}

void TriggerAndFinishMapLoadStripe_Y(int n) {  // 82ef7a
  BYTE(overworld_screen_trans_dir_bits2) = 8;
  nmi_subroutine_index = 3;
  uint16 *dst = uvram.data;
  *dst++ = 0x80;
  do {
    dst = BufferAndBuildMap16Stripes_Y(dst);
    map16_load_src_off -= 0x80;
    map16_load_var2 = (map16_load_var2 - 1) & 0x1f;
  } while (--n);
  *dst = 0xffff;
}

void TriggerAndFinishMapLoadStripe_X(int n) {  // 82efb3
  BYTE(overworld_screen_trans_dir_bits2) = 2;
  nmi_subroutine_index = 3;
  uint16 *dst = uvram.data;
  *dst++ = 0x8040;
  do {
    dst = BufferAndBuildMap16Stripes_X(dst);
    map16_load_src_off -= 2;
    map16_load_dst_off = (map16_load_dst_off - 1) & 0x1f;
  } while (--n);
  *dst = 0xffff;
}

void SomeTileMapChange() {  // 82efe8
  Overworld_DecompressAndDrawAllQuadrants();
  for (int i = 0; i < 64; i++)
    dung_bg1[i] = 0xdc4;
  Overworld_HandleOverlaysAndBombDoors();
  submodule_index++;
}

void CreateInitialNewScreenMapToScroll() {  // 82f031
  if (!kOverworldMapIsSmall[BYTE(overworld_screen_index)]) {
    switch (BYTE(overworld_screen_trans_dir_bits2)) {
    case 1: CreateInitialOWScreenView_Big_East(); break;
    case 2: CreateInitialOWScreenView_Big_West(); break;
    case 4: CreateInitialOWScreenView_Big_South(); break;
    case 8: CreateInitialOWScreenView_Big_North(); break;
    default:
      assert(0);
      submodule_index = 0;
    }
  } else {
    switch (BYTE(overworld_screen_trans_dir_bits2)) {
    case 1: CreateInitialOWScreenView_Small_East(); break;
    case 2: CreateInitialOWScreenView_Small_West(); break;
    case 4: CreateInitialOWScreenView_Small_South(); break;
    case 8: CreateInitialOWScreenView_Small_North(); break;
    default:
      assert(0);
      submodule_index = 0;
    }
  }
}

void CreateInitialOWScreenView_Big_North() {  // 82f06b
  map16_load_src_off += 0x380;
  map16_load_var2 = 31;
  TriggerAndFinishMapLoadStripe_Y(7);
}

void CreateInitialOWScreenView_Big_South() {  // 82f087
  uint16 pos = map16_load_src_off;
  while (pos >= 0x80)
    pos -= 0x80;
  map16_load_src_off = pos + 0x780;
  map16_load_var2 = 7;
  TriggerAndFinishMapLoadStripe_Y(8);
  map16_load_var2 = (map16_load_var2 + 9) & 0x1f;
  map16_load_src_off -= 0xB80;
}

void CreateInitialOWScreenView_Big_West() {  // 82f0c0
  map16_load_src_off += 14;
  map16_load_dst_off = 31;
  TriggerAndFinishMapLoadStripe_X(7);
}

void CreateInitialOWScreenView_Big_East() {  // 82f0dc
  map16_load_src_off = map16_load_src_off - 0x60 + 0x1e;
  map16_load_dst_off = 7;
  TriggerAndFinishMapLoadStripe_X(8);
  map16_load_dst_off = (map16_load_dst_off + 9) & 0x1f;
  map16_load_src_off -= 0x2e;
}

void CreateInitialOWScreenView_Small_North() {  // 82f10f
  orange_blue_barrier_state = map16_load_src_off - 0x700;
  word_7EC174 = map16_load_dst_off;
  word_7EC176 = 10;
  map16_load_src_off = 0x1390;
  map16_load_dst_off = 0;
  map16_load_var2 = 31;
  TriggerAndFinishMapLoadStripe_Y(7);
}

void CreateInitialOWScreenView_Small_South() {  // 82f141
  orange_blue_barrier_state = BYTE(map16_load_src_off);
  word_7EC174 = map16_load_dst_off;
  word_7EC176 = 24;
  map16_load_src_off = 0x790;
  map16_load_dst_off = 0;
  map16_load_var2 = 7;
  TriggerAndFinishMapLoadStripe_Y(8);
  map16_load_var2 = (map16_load_var2 + 9) & 0x1f;
  map16_load_src_off -= 0xB80;
}

void CreateInitialOWScreenView_Small_West() {  // 82f185
  orange_blue_barrier_state = map16_load_src_off - 0x20;
  word_7EC174 = 8;
  word_7EC176 = map16_load_var2;
  map16_load_src_off = 0x44e;
  map16_load_var2 = 0;
  map16_load_dst_off = 31;
  TriggerAndFinishMapLoadStripe_X(7);
}

void CreateInitialOWScreenView_Small_East() {  // 82f1b7
  orange_blue_barrier_state = map16_load_src_off - 0x60;
  word_7EC174 = 0x18;
  word_7EC176 = map16_load_var2;
  map16_load_src_off = 0x41e;
  map16_load_var2 = 0;
  map16_load_dst_off = 7;
  TriggerAndFinishMapLoadStripe_X(8);
  map16_load_dst_off = (map16_load_dst_off + 9) & 0x1f;
  map16_load_src_off -= 0x2e;
}

void OverworldTransitionScrollAndLoadMap() {  // 82f20e
  uint16 *dst = uvram.data;
  switch (BYTE(overworld_screen_trans_dir_bits2)) {
  case 1: dst = BuildFullStripeDuringTransition_East(dst); break;
  case 2: dst = BuildFullStripeDuringTransition_West(dst); break;
  case 4: dst = BuildFullStripeDuringTransition_South(dst); break;
  case 8: dst = BuildFullStripeDuringTransition_North(dst); break;
  default:
    assert(0);
    submodule_index = 0;
  }
  dst[0] = dst[1] = 0xffff;
  if (dst != uvram.data)
    nmi_subroutine_index = 3;
}

uint16 *BuildFullStripeDuringTransition_North(uint16 *dst) {  // 82f218
  *dst++ = 0x80;
  dst = BufferAndBuildMap16Stripes_Y(dst);
  map16_load_src_off -= 0x80;
  map16_load_var2 = (map16_load_var2 - 1) & 0x1f;
  return dst;
}

uint16 *BuildFullStripeDuringTransition_South(uint16 *dst) {  // 82f238
  *dst++ = 0x80;
  dst = BufferAndBuildMap16Stripes_Y(dst);
  map16_load_src_off += 0x80;
  map16_load_var2 = (map16_load_var2 + 1) & 0x1f;
  return dst;
}

uint16 *BuildFullStripeDuringTransition_West(uint16 *dst) {  // 82f241
  *dst++ = 0x8040;
  dst = BufferAndBuildMap16Stripes_X(dst);
  map16_load_src_off -= 2;
  map16_load_dst_off = (map16_load_dst_off - 1) & 0x1f;
  return dst;
}

uint16 *BuildFullStripeDuringTransition_East(uint16 *dst) {  // 82f24a
  *dst++ = 0x8040;
  dst = BufferAndBuildMap16Stripes_X(dst);
  map16_load_src_off += 2;
  map16_load_dst_off = (map16_load_dst_off + 1) & 0x1f;
  return dst;
}

void OverworldHandleMapScroll() {  // 82f273
  uint16 *dst = uvram.data;
  switch (BYTE(overworld_screen_trans_dir_bits2)) {
  case 1:
    dst = CheckForNewlyLoadedMapAreas_East(dst);
    BYTE(overworld_screen_trans_dir_bits2) = 0;
    break;
  case 2:
    dst = CheckForNewlyLoadedMapAreas_West(dst);
    BYTE(overworld_screen_trans_dir_bits2) = 0;
    break;
  case 4:
    dst = CheckForNewlyLoadedMapAreas_South(dst);
    BYTE(overworld_screen_trans_dir_bits2) = 0;
    break;
  case 5:
  case 6:
    dst = CheckForNewlyLoadedMapAreas_South(dst);
    BYTE(overworld_screen_trans_dir_bits2) &= 3;
    break;
  case 8:
    dst = CheckForNewlyLoadedMapAreas_North(dst);
    BYTE(overworld_screen_trans_dir_bits2) = 0;
    break;
  case 9:
  case 10:
    dst = CheckForNewlyLoadedMapAreas_North(dst);
    BYTE(overworld_screen_trans_dir_bits2) &= 3;
    break;
  default:
    assert(0);
    submodule_index = 0;
  }
  dst[0] = dst[1] = 0xffff;
  if (dst != uvram.data)
    nmi_subroutine_index = 3;
  overworld_screen_transition = overworld_screen_trans_dir_bits2;
}

uint16 *CheckForNewlyLoadedMapAreas_North(uint16 *dst) {  // 82f2dd
  if (sign16(map16_load_src_off - 0x80))
    return dst;
  if (!kOverworldMapIsSmall[overworld_screen_index]) {
    *dst++ = 0x80;
    dst = BufferAndBuildMap16Stripes_Y(dst);
  }
  map16_load_src_off -= 0x80;
  map16_load_var2 = (map16_load_var2 - 1) & 0x1f;
  return dst;
}

uint16 *CheckForNewlyLoadedMapAreas_South(uint16 *dst) {  // 82f311
  if (map16_load_src_off >= 0x1800)
    return dst;
  if (!kOverworldMapIsSmall[overworld_screen_index]) {
    *dst++ = 0x80;
    dst = BufferAndBuildMap16Stripes_Y(dst);
  }
  map16_load_src_off += 0x80;
  map16_load_var2 = (map16_load_var2 + 1) & 0x1f;
  return dst;
}

uint16 *CheckForNewlyLoadedMapAreas_West(uint16 *dst) {  // 82f345
  uint16 pos = map16_load_src_off;
  while (pos >= 0x80)
    pos -= 0x80;
  if (pos == 0)
    return dst;
  if (!kOverworldMapIsSmall[overworld_screen_index]) {
    *dst++ = 0x8040;
    dst = BufferAndBuildMap16Stripes_X(dst);
  }
  map16_load_src_off -= 2;
  map16_load_dst_off = (map16_load_dst_off - 1) & 0x1f;
  return dst;
}

uint16 *CheckForNewlyLoadedMapAreas_East(uint16 *dst) {  // 82f37f
  uint16 pos = map16_load_src_off;
  while (pos >= 0x80)
    pos -= 0x80;
  if (pos >= 0x60)
    return dst;
  if (!kOverworldMapIsSmall[overworld_screen_index]) {
    *dst++ = 0x8040;
    dst = BufferAndBuildMap16Stripes_X(dst);
  }
  map16_load_src_off += 2;
  map16_load_dst_off = (map16_load_dst_off + 1) & 0x1f;
  return dst;
}

uint16 *BufferAndBuildMap16Stripes_X(uint16 *dst) {  // 82f3b9
  uint16 pos = map16_load_src_off - kOverworld_DrawStrip_Tab[overworld_screen_trans_dir_bits2 >> 1 & 1];
  int d = map16_load_var2;
  uint16 *tmp = dung_replacement_tile_state;
  for (int i = 0; i < 32; i++) {
    tmp[d] = (pos >= 0x2000) ? 0 : dung_bg2[pos >> 1];
    d = (d + 1) & 0x1f, pos += 128;
  }
  uint16 r0 = 0, of = map16_load_dst_off;
  if (of >= 0x10)
    of &= 0xf, r0 = 0x400;
  r0 += of * 2;
  for (int i = 0; i < 2; i++, r0 += 0x800) {
    dst[0] = r0;
    dst[33] = r0 + 1;
    dst++;
    for (int j = 0; j < 16; j++) {
      uint16 k = *tmp++;
      assert(k < 0xea8);
      const uint16 *s = Map16ToMap8Entry(k);
      uint16 map8_words[4] = {s[0], s[1], s[2], s[3]};
      Rando_PatchMedallionEntranceMap8(k, map8_words);
      dst[0] = map8_words[0];
      dst[33] = map8_words[1];
      dst[1] = map8_words[2];
      dst[34] = map8_words[3];
      dst += 2;
    }
    dst += 33;
  }
  return dst;
}

uint16 *BufferAndBuildMap16Stripes_Y(uint16 *dst) {  // 82f482
  uint16 pos = map16_load_src_off - kOverworld_DrawStrip_Tab[1 + (overworld_screen_trans_dir_bits2 >> 2 & 1)];
  int d = map16_load_dst_off;
  uint16 *tmp = dung_replacement_tile_state;
  for (int i = 0; i < 32; i++) {
    tmp[d] = (pos >= 0x2000) ? 0 : dung_bg2[pos >> 1]; // fixed bug warning. can go negative
    pos += 2;
    d = (d + 1) & 0x1f;
  }
  uint16 r0 = 0, of = map16_load_var2;
  if (of >= 0x10)
    of &= 0xf, r0 = 0x800;
  r0 += of * 64;
  for (int i = 0; i < 2; i++, r0 += 0x400) {
    *dst++ = r0;
    for (int j = 0; j < 16; j++) {
      uint16 k = *tmp++;
      assert(k < 0xea8);
      const uint16 *s = Map16ToMap8Entry(k);
      uint16 map8_words[4] = {s[0], s[1], s[2], s[3]};
      Rando_PatchMedallionEntranceMap8(k, map8_words);
      dst[0] = map8_words[0];
      dst[32] = map8_words[2];
      dst[1] = map8_words[1];
      dst[33] = map8_words[3];
      dst += 2;
    }
    dst += 32;
  }
  return dst;
}

void Overworld_DecompressAndDrawAllQuadrants() {  // 82f54a
  int si = overworld_screen_index;
  ZeldaWideBeforeMapLoad(9);
  Overworld_DecompressAndDrawOneQuadrant((uint16 *)&g_ram[0x2000], si + 0);
  Overworld_DecompressAndDrawOneQuadrant((uint16 *)&g_ram[0x2040], si + 1);
  Overworld_DecompressAndDrawOneQuadrant((uint16 *)&g_ram[0x3000], si + 8);
  Overworld_DecompressAndDrawOneQuadrant((uint16 *)&g_ram[0x3040], si + 9);
  ZeldaWideAfterMapLoad(9);
}

static const uint8 *GetOverworldHibytes(int i) {
  return kOverworld_Hibytes_Comp(i).ptr;
}

static const uint8 *GetOverworldLobytes(int i) {
  return kOverworld_Lobytes_Comp(i).ptr;
}


void Overworld_DecompressAndDrawOneQuadrant(uint16 *dst, int screen) {  // 82f595
  Decompress_bank02(&g_ram[0x14400], GetOverworldHibytes(screen));
  for (int i = 0; i < 256; i++)
    g_ram[0x14001 + i * 2] = g_ram[0x14400 + i];

  Decompress_bank02(&g_ram[0x14400], GetOverworldLobytes(screen));
  for (int i = 0; i < 256; i++)
    g_ram[0x14000 + i * 2] = g_ram[0x14400 + i];

  map16_decode_last = 0xffff;

  uint16 *qol_start = dst;
  uint16 *src = (uint16 *)&g_ram[0x14000];
  for (int j = 0; j < 16; j++) {
    for (int i = 0; i < 16; i++) {
      Overworld_ParseMap32Definition(dst, *src++ * 2);
      dst += 2;
    }
    dst += 96;
  }
  Overworld_QolShortcutTiles(qol_start,screen,64);
}

void Overworld_ParseMap32Definition(uint16 *dst, uint16 input) {  // 82f691
  uint16 a = input & ~7;
  if (a != map16_decode_last) {
    map16_decode_last = a;
    map16_decode_tmp = a >> 1;
    int x = (a >> 1) + (a >> 2);
    const uint8 *ov;
    ov = kMap32ToMap16_0 + x;

    map16_decode_0[0] = ov[0];
    map16_decode_0[2] = ov[1];
    map16_decode_0[4] = ov[2];
    map16_decode_0[6] = ov[3];
    map16_decode_0[1] = ov[4] >> 4;
    map16_decode_0[3] = ov[4] & 0xf;
    map16_decode_0[5] = ov[5] >> 4;
    map16_decode_0[7] = ov[5] & 0xf;
    ov = kMap32ToMap16_1 + x;
    map16_decode_1[0] = ov[0];
    map16_decode_1[2] = ov[1];
    map16_decode_1[4] = ov[2];
    map16_decode_1[6] = ov[3];
    map16_decode_1[1] = ov[4] >> 4;
    map16_decode_1[3] = ov[4] & 0xf;
    map16_decode_1[5] = ov[5] >> 4;
    map16_decode_1[7] = ov[5] & 0xf;
    ov = kMap32ToMap16_2 + x;
    map16_decode_2[0] = ov[0];
    map16_decode_2[2] = ov[1];
    map16_decode_2[4] = ov[2];
    map16_decode_2[6] = ov[3];
    map16_decode_2[1] = ov[4] >> 4;
    map16_decode_2[3] = ov[4] & 0xf;
    map16_decode_2[5] = ov[5] >> 4;
    map16_decode_2[7] = ov[5] & 0xf;
    ov = kMap32ToMap16_3 + x;
    map16_decode_3[0] = ov[0];
    map16_decode_3[2] = ov[1];
    map16_decode_3[4] = ov[2];
    map16_decode_3[6] = ov[3];
    map16_decode_3[1] = ov[4] >> 4;
    map16_decode_3[3] = ov[4] & 0xf;
    map16_decode_3[5] = ov[5] >> 4;
    map16_decode_3[7] = ov[5] & 0xf;
  }
  dst[0] = WORD(map16_decode_0[input & 7]);
  dst[64] = WORD(map16_decode_2[input & 7]);
  dst[1] = WORD(map16_decode_1[input & 7]);
  dst[65] = WORD(map16_decode_3[input & 7]);
}

// Decompress_bank02 bounded by the destination's and the source's sizes:
// -1 rather than writing or reading past either.
static int DecompressBank02Bounded(uint8 *dst, int cap, const uint8 *src, size_t size) {
  const uint8 *end = src + size;
  int n = 0;
  for (;;) {
    if (src >= end)
      return -1;
    uint8 cmd = *src++;
    if (cmd == 0xff)
      return n;
    int len;
    if ((cmd & 0xe0) != 0xe0) {
      len = (cmd & 0x1f) + 1;
      cmd &= 0xe0;
    } else {
      if (src >= end)
        return -1;
      len = *src++ + ((cmd & 3) << 8) + 1;
      cmd = (cmd << 3) & 0xe0;
    }
    if (len > cap - n)
      return -1;
    if (cmd == 0) {
      if (end - src < len)
        return -1;
      memcpy(dst + n, src, len);
      src += len, n += len;
    } else if (cmd & 0x80) {
      if (end - src < 2)
        return -1;
      int offs = src[0] << 8 | src[1];
      src += 2;
      if (offs > cap - len)
        return -1;
      for (int i = 0; i < len; i++)
        dst[n++] = dst[offs + i];
    } else if (!(cmd & 0x40)) {
      if (src >= end)
        return -1;
      memset(dst + n, *src++, len);
      n += len;
    } else if (!(cmd & 0x20)) {
      if (end - src < 2)
        return -1;
      for (int i = 0; i < len; i++)
        dst[n++] = src[i & 1];
      src += 2;
    } else {
      if (src >= end)
        return -1;
      uint8 v = *src++;
      for (int i = 0; i < len; i++)
        dst[n++] = v++;
    }
  }
}

// The map16 of one overworld screen, 32x32 entries row by row, as
// Overworld_DecompressAndDrawOneQuadrant builds it but into dst: no WRAM is
// touched. Without the overlays the game adds on entering an area
// (Overworld_HandleOverlaysAndBombDoors). For the WIDE side bands, which
// show screens the game has not loaded during a scroll.
bool Overworld_DecodeScreenMap16(int screen, uint16 *dst) {
  MemBlk hi_comp = kOverworld_Hibytes_Comp(screen);
  MemBlk lo_comp = kOverworld_Lobytes_Comp(screen);
  uint8 hi[0x300], lo[0x300];
  if (!hi_comp.ptr || !lo_comp.ptr ||
      DecompressBank02Bounded(hi, sizeof(hi), hi_comp.ptr, hi_comp.size) < 256 ||
      DecompressBank02Bounded(lo, sizeof(lo), lo_comp.ptr, lo_comp.size) < 256)
    return false;
  const uint8 *tables[4] = { kMap32ToMap16_0, kMap32ToMap16_1, kMap32ToMap16_2, kMap32ToMap16_3 };
  size_t sizes[4] = { kMap32ToMap16_0_SIZE, kMap32ToMap16_1_SIZE, kMap32ToMap16_2_SIZE, kMap32ToMap16_3_SIZE };
  for (int i = 0; i < 256; i++) {
    // Overworld_ParseMap32Definition, from the input it is given.
    uint16 input = (uint16)((hi[i] << 8 | lo[i]) * 2);
    uint16 a = input & ~7;
    size_t x = (a >> 1) + (a >> 2);
    int k = (input & 7) >> 1;
    uint16 *out = dst + (i >> 4) * 64 + (i & 15) * 2;
    for (int t = 0; t < 4; t++) {
      if (x + 6 > sizes[t])
        return false;
      const uint8 *ov = tables[t] + x;
      uint8 nibbles = ov[4 + (k >> 1)];
      out[(t >> 1) * 32 + (t & 1)] = ov[k] | ((k & 1 ? nibbles & 0xf : nibbles >> 4) << 8);
    }
  }
  Overworld_QolShortcutTiles(dst,screen,32);
  return true;
}

void OverworldLoad_LoadSubOverlayMap32() {  // 82f7cb
  int si = overworld_screen_index;
  Overworld_DecompressAndDrawOneQuadrant((uint16 *)&g_ram[0x4000], si);
}

void LoadOverworldOverlay() {  // 82fd0d
  OverworldLoad_LoadSubOverlayMap32();
  Map16ToMap8(&g_ram[0x4000], 0x1000);
  nmi_subroutine_index = nmi_disable_core_updates = 4;
  submodule_index++;
}

void Map16ToMap8(const uint8 *src, int r20) {  // 82fd46
  map16_load_src_off += 0x1000;
  int n = 32;
  int r14 = 0;
  uint16 *r10 = &word_7F4000;
  do {
    OverworldCopyMap16ToBuffer(src, r20, r14, r10);
    r14 += 0x100, r10 += 2;
    map16_load_src_off -= 0x80;
    map16_load_var2 = (map16_load_var2 - 1) & 0x1f;
  } while (--n);
}

void OverworldCopyMap16ToBuffer(const uint8 *src, uint16 r20, int r14, uint16 *r10) {  // 82fd87
  int yr = map16_load_src_off - 0x410 & 0x1fff;
  int xr = map16_load_dst_off;
  uint16 *tmp = (uint16 *)(g_ram + 0x500);
  int n = 32;
  do {
    WORD(tmp[xr]) = WORD(src[yr]);
    xr = (xr + 1) & 0x1f;
    yr = (yr + 2) & 0x1fff;
  } while (--n);

  uint16 r0 = 0, of = map16_load_var2;
  if (of >= 0x10)
    of &= 0xf, r0 = 0x800;
  r0 += of * 64;

  for (int i = 0; i < 2; i++, r0 += 0x400, r14 += 0x40) {
    *r10++ = r0 | r20;
    for (int j = 0; j < 16; j++, r14 += 4) {
      uint16 k = *tmp++;
      const uint16 *m = Map16ToMap8Entry(k);
      uint16 map8_words[4] = {m[0], m[1], m[2], m[3]};
      Rando_PatchMedallionEntranceMap8(k, map8_words);
      WORD(dung_bg2_attr_table[r14]) = WORD(map8_words[0]);
      WORD(dung_bg2_attr_table[r14 + 64]) = WORD(map8_words[2]);
      WORD(dung_bg2_attr_table[r14 + 2]) = WORD(map8_words[1]);
      WORD(dung_bg2_attr_table[r14 + 66]) = WORD(map8_words[3]);
    }
  }

}

void MirrorBonk_RecoverChangedTiles() {  // 82fe47
  for (int i = 0, i_end = num_memorized_tiles >> 1; i != i_end; i++) {
    uint16 pos = memorized_tile_addr[i];
    dung_bg2[pos >> 1] = memorized_tile_value[i];
  }
}

void DecompressEnemyDamageSubclasses() {  // 82fe71
  uint8 *tmp = &g_ram[0x14000];
  memcpy(tmp, kEnemyDamageData, kEnemyDamageData_SIZE);
  for (int i = 0; i < 0x1000; i += 2) {
    uint8 t = *tmp++;
    enemy_damage_data[i + 0] = t >> 4;
    enemy_damage_data[i + 1] = t & 0xf;
  }
}

int Decompress_bank02(uint8 *dst, const uint8 *src) {  // 82febb
  uint8 *dst_org = dst;
  int len;
  for (;;) {
    uint8 cmd = *src++;
    if (cmd == 0xff)
      return dst - dst_org;
    if ((cmd & 0xe0) != 0xe0) {
      len = (cmd & 0x1f) + 1;
      cmd &= 0xe0;
    } else {
      len = *src++;
      len += ((cmd & 3) << 8) + 1;
      cmd = (cmd << 3) & 0xe0;
    }
    //printf("%d: %d,%d\n", (int)(dst - dst_org), cmd, len);
    if (cmd == 0) {
      do {
        *dst++ = *src++;
      } while (--len);
    } else if (cmd & 0x80) {
      uint32 offs = *src++ << 8;
      offs |= *src++;
      do {
        *dst++ = dst_org[offs++];
      } while (--len);
    } else if (!(cmd & 0x40)) {
      uint8 v = *src++;
      do {
        *dst++ = v;
      } while (--len);
    } else if (!(cmd & 0x20)) {
      uint8 lo = *src++;
      uint8 hi = *src++;
      do {
        *dst++ = lo;
        if (--len == 0)
          break;
        *dst++ = hi;
      } while (--len);
    } else {
      // copy bytes with the byte incrementing by 1 in between
      uint8 v = *src++;
      do {
        *dst++ = v;
      } while (v++, --len);
    }
  }
}

uint8 Overworld_ReadTileAttribute(uint16 x, uint16 y) {  // 85faa2
  int t = ((x - overworld_offset_base_x) & overworld_offset_mask_x);
  t |= ((y - overworld_offset_base_y) & overworld_offset_mask_y) << 3;
  return kSomeTileAttr[dung_bg2[t >> 1]];
}

void Overworld_SetFixedColAndScroll() {  // 8bfe70
  TS_copy = 0;
  uint16 p = 0x19C6;
  uint16 si = overworld_screen_index;
  if (si == 0x80) {
    if (dungeon_room_index == 0x181) {
      TS_copy = 1;
      p = si & 0x40 ? 0x2A32 : 0x2669;
    }
  } else if (si != 0x81) {
    p = 0;
    if (si != 0x5b && (si & 0xbf) != 3 && (si & 0xbf) != 5 && (si & 0xbf) != 7)
      p = si & 0x40 ? 0x2A32 : 0x2669;
  }
  main_palette_buffer[0] = p;
  aux_palette_buffer[0] = p;
  main_palette_buffer[32] = p;
  aux_palette_buffer[32] = p;

  COLDATA_copy0 = 0x20;
  COLDATA_copy1 = 0x40;
  COLDATA_copy2 = 0x80;

  uint32 cv;

  if (si != 0 && si != 0x40 && si != 0x5b) {
    if (si == 0x70)
      goto getout;
    cv = 0x8c4c26;
    if (si != 3 && si != 5 && si != 7) {
      cv = 0x874a26;
      if (si != 0x43 && si != 0x45) {
        flag_update_cgram_in_nmi += 1;
        return;
      }
    }
    COLDATA_copy0 = (uint8)(cv);
    COLDATA_copy1 = (uint8)(cv >> 8);
    COLDATA_copy2 = (uint8)(cv >> 16);
  }

  if (submodule_index != 4) {
    BG1VOFS_copy2 = BG2VOFS_copy2;
    BG1HOFS_copy2 = BG2HOFS_copy2;
    if ((si & 0x3f) == 0x1b) {
      int16 y = (int16)(BG2HOFS_copy2 - 0x778) >> 1;
      BG1HOFS_copy2 = BG2HOFS_copy2 - y;
      uint16 a = BG1VOFS_copy2;
      if (a >= 0x6C0) {
        a = (a - 0x600) & 0x3ff;
        BG1VOFS_copy2 = (a < 0x180) ? (a >> 1) | 0x600 : 0x6c0;
      } else {
        BG1VOFS_copy2 = (a & 0xff) >> 1 | 0x600;
      }
    }
  } else {
    if ((si & 0x3f) == 0x1b) {
      BG1HOFS_copy2 = (BYTE(overworld_screen_trans_dir_bits) != 8) ? 0x838 : BG2HOFS_copy2;
      BG1VOFS_copy2 = 0x6c0;
    }
  }
getout:
  TS_copy = 1;
  flag_update_cgram_in_nmi++;
}

void Overworld_Memorize_Map16_Change(uint16 pos, uint16 value) {  // 8edd40
  if (value == 0xdc5 || value == 0xdc9)
    return;

  int x = num_memorized_tiles;
  memorized_tile_value[x >> 1] = value;
  memorized_tile_addr[x >> 1] = pos;
  num_memorized_tiles = x + 2;
}

static bool s_rando_peg_grant_blocked;

void HandlePegPuzzles(uint16 pos) {  // 8edd67
  static const uint16 kLwTurtleRockPegPositions[3] = { 0x826, 0x5a0, 0x81a };
  s_rando_peg_grant_blocked = false;

  if (overworld_screen_index == 7) {
    // #82 Inverted: TurtleRockPegSolved. In Inverted the Turtle Rock entrance
    // pegs are treated as already solved (the puzzle is pre-flat), per
    // z3randomizer inverted.asm TurtleRockPegSolved which forces the
    // peg-solved event test (`LDA $7EF287 : AND #$0020`) to always read 0x20.
    // Mirror that here so the troll-peg hammer interaction is skipped under the
    // active Inverted rando world-state (the peg puzzle behaves as complete).
    // Vanilla / Open / Standard / Retro use the persisted solved-bit unchanged.
    // NOTE: this guard covers only the LIGHT-world screen-7 peg approach. The
    // actual Inverted "reach TR from the tail" access opens the entrance via the
    // screen-0x47 (DW) auto-open in Overworld_HandleOverlaysAndBombDoors + the
    // screen-0x47 collision table (tile_detect.c); this screen-7 pre-solve is
    // belt-and-suspenders, not the load-bearing TR-open for the tail route.
    bool inverted_rando =
        (enhanced_features1 & kFeatures1_RandomizerActive) &&
        Rando_GetActiveWorldState() == 2 /* kWorldState_Inverted */;
    if (inverted_rando || (save_ow_event_info[7] & 0x20))
      return;
    if (word_7E04C8 != 0xffff && kLwTurtleRockPegPositions[word_7E04C8 >> 1] == pos) {
      WORD(sound_effect_1) = 0x2d00;
      word_7E04C8 += 2;
      if (word_7E04C8 == 6) {
        WORD(sound_effect_1) = 0x1b00;
        save_ow_event_info[7] |= 0x20;
        submodule_index = 47;
      }
    } else {
      WORD(sound_effect_1) = 0x3c;
      word_7E04C8 = 0xffff;
    }
  } else if (overworld_screen_index == 98) {
    if (++word_7E04C8 == 22) {
      RandoGrantResult grant = Rando_GrantLocation(
          LOC_Hammer_Pegs, ITEM_PieceOfHeart, 0x17,
          kRandoGrantPresentation_Animated, 0, 0);
      if (grant == kRandoGrantResult_Retryable ||
          grant == kRandoGrantResult_Invalid) {
        // The final peg remains standing and the persisted puzzle bits remain
        // untouched, so a later hammer hit can retry the same transaction.
        word_7E04C8 = 21;
        s_rando_peg_grant_blocked = true;
        return;
      }
      save_ow_event_info[0x62] |= 0x20;
      sound_effect_2 = 27;
      if (grant == kRandoGrantResult_Accepted ||
          grant == kRandoGrantResult_AlreadyChecked)
        save_ow_event_info[0x62] |= 0x40;
      door_open_closed_counter = 0x50;
      big_rock_starting_address = 0xd20;
      Overworld_DoMapUpdate32x32_B();
    }
  }
  //assert(0);
}

void GanonTowerEntrance_Func1() {  // 8eddfc
  if (!subsubmodule_index) {
    sound_effect_1 = 0x2e;
    Palette_AnimGetMasterSword2();
  } else {
    PaletteFilter_BlindingWhite();
    if (darkening_or_lightening_screen == 255) {
      palette_filter_countdown = 255;
      subsubmodule_index++;
    } else {
      Palette_AnimGetMasterSword3();
    }
  }
}

void Overworld_CheckSpecialSwitchArea() {  // 8ede49
  const uint16 *map8 = Overworld_GetMap16OfLink_Mult8();
  int a = map8[0] & 0x1ff;
  for (int i = 3; i >= 0; i--) {
    if (kSpecialSwitchArea_Map8[i] == a && kSpecialSwitchArea_Screen[i] == overworld_screen_index) {
      dungeon_room_index = kSpecialSwitchArea_Exit[i];
      BYTE(overworld_screen_trans_dir_bits) = BYTE(overworld_screen_trans_dir_bits2) = link_direction = kSpecialSwitchArea_Direction[i];
      WORD(byte_7E069C) = WORD(overworld_screen_transition) = DirToEnum(link_direction);
      submodule_index = 23;
      main_module_index = 11;
      break;
    }
  }
}

const uint16 *Overworld_GetMap16OfLink_Mult8() {  // 8ede9a
  uint16 xc = (link_x_coord + 8) >> 3, yc = link_y_coord + 12;
  uint16 pos = ((yc - overworld_offset_base_y) & overworld_offset_mask_y) * 8 +
               ((xc - overworld_offset_base_x) & overworld_offset_mask_x);
  return Map16ToMap8Entry(dung_bg2[pos >> 1]);
}

void Palette_AnimGetMasterSword() {  // 8ef400
  if (subsubmodule_index == 0) {
    Palette_AnimGetMasterSword2();
  } else {
    PaletteFilter_BlindingWhite();
    if (darkening_or_lightening_screen == 0xff) {
      for (int i = 0; i < 8; i++)
        main_palette_buffer[0x58 + i] = aux_palette_buffer[0x58 + i] = 0;
      palette_filter_countdown = 0;
      darkening_or_lightening_screen = 0;
      submodule_index = 0;
    } else {
      Palette_AnimGetMasterSword3();
    }
  }
}

void Palette_AnimGetMasterSword2() {  // 8ef404
  memcpy(mapbak_palette, aux_palette_buffer, 512);
  for (int i = 0; i < 256; i++)
    aux_palette_buffer[i] = 0x7fff;
  main_palette_buffer[32] = main_palette_buffer[0];
  palette_filter_countdown = 0;
  darkening_or_lightening_screen = 2;
  subsubmodule_index++;
}

void Palette_AnimGetMasterSword3() {  // 8ef48c
  if (darkening_or_lightening_screen != 0 || palette_filter_countdown != 31)
    return;
  memcpy(aux_palette_buffer, mapbak_palette, 512);
  TS_copy = 0;
}

void Overworld_DwDeathMountainPaletteAnimation() {  // 8ef582
  if (trigger_special_entrance)
    return;
  uint8 sc = overworld_screen_index;
  if (sc != 0x43 && sc != 0x45 && sc != 0x47)
    return;
  uint8 fc = frame_counter;
  if (fc == 5 || fc == 44 || fc == 90) {
    for (int i = 1; i < 8; i++) {
      main_palette_buffer[0x30 + i] = aux_palette_buffer[0x30 + i];
      main_palette_buffer[0x38 + i] = aux_palette_buffer[0x38 + i];
      main_palette_buffer[0x48 + i] = aux_palette_buffer[0x48 + i];
      main_palette_buffer[0x70 + i] = aux_palette_buffer[0x70 + i];
      main_palette_buffer[0x78 + i] = aux_palette_buffer[0x78 + i];
    }
  } else if (fc == 3 || fc == 36 || fc == 88) {
    if (fc == 36)
      sound_effect_1 = 54;
    for (int i = 1; i < 8; i++) {
      main_palette_buffer[0x30 + i] = kDwPaletteAnim[i - 1 + 0];
      main_palette_buffer[0x38 + i] = kDwPaletteAnim[i - 1 + 7];
      main_palette_buffer[0x48 + i] = kDwPaletteAnim[i - 1 + 14];
      main_palette_buffer[0x70 + i] = kDwPaletteAnim[i - 1 + 21];
      main_palette_buffer[0x78 + i] = kDwPaletteAnim[i - 1 + 28];
    }
  }
  flag_update_cgram_in_nmi++;
  int yy = 32;
  if (sc == 0x43 || sc == 0x45) {
    if (save_ow_event_info[0x43] & 0x20)
      return;
    yy = (frame_counter & 0xc) * 2;
  }
  for (int i = 0; i < 8; i++)
    main_palette_buffer[0x68 + i] = kDwPaletteAnim2[yy + i];
}

// A quest's overlays are its cartridge's own routines ($8E:F652 on), given
// with its bytes (kModTable_RomBytes): they are run here, in the state the
// game calls them in, A and X the screen times two, 16 bits, the data bank
// $7E. They write tiles with a few instructions; the ones below are all the
// routines of the quests seen use, even where a jump table entry lands in
// the middle of an instruction. Y is what the caller left in it, unused by
// them: 0 here.
static void QuestOverlay_Write(uint32 addr, uint16 v) {
  g_ram[addr & 0x1ffff] = v;
  g_ram[(addr + 1) & 0x1ffff] = v >> 8;
}

static uint16 QuestOverlay_Read(uint32 addr) {
  return g_ram[addr & 0x1ffff] | g_ram[(addr + 1) & 0x1ffff] << 8;
}

static bool Overworld_RunQuestOverlay(void) {
  if (Mods_RomByte(0x8ef652, 0) != 0x8b)  // PHB: the quest gives them
    return false;
  uint16 a = overworld_screen_index * 2, x = a, y = 0;
  uint32 c = 0, pc = 0x8e0000 | Mods_RomWord(0x8ef664 + a, 0), stack[8];
  int sp = 0;
  for (int n = 0; n < 20000; n++) {
    uint8 op = Mods_RomByte(pc, 0), b = Mods_RomByte(pc + 1, 0);
    uint16 w = Mods_RomWord(pc + 1, 0);
    uint32 l = w | Mods_RomByte(pc + 3, 0) << 16, t;
    switch (op) {
    case 0xa9: a = w; pc += 3; break;  // LDA #
    case 0xad: a = QuestOverlay_Read(0x7e0000 | w); pc += 3; break;  // LDA abs
    case 0xbd: a = QuestOverlay_Read(0x7e0000 + w + x); pc += 3; break;  // LDA abs,X
    case 0x8d: QuestOverlay_Write(0x7e0000 | w, a); pc += 3; break;  // STA abs
    case 0x9d: QuestOverlay_Write(0x7e0000 + w + x, a); pc += 3; break;  // STA abs,X
    case 0x99: QuestOverlay_Write(0x7e0000 + w + y, a); pc += 3; break;  // STA abs,Y
    case 0x8f:  // STA long
      if ((l >> 17) != 0x3f)
        goto unknown;
      QuestOverlay_Write(l, a), pc += 4;
      break;
    case 0x9c: QuestOverlay_Write(0x7e0000 | w, 0); pc += 3; break;  // STZ abs
    case 0x9e: QuestOverlay_Write(0x7e0000 + w + x, 0); pc += 3; break;  // STZ abs,X
    case 0x8e: QuestOverlay_Write(0x7e0000 | w, x); pc += 3; break;  // STX abs
    case 0x8c: QuestOverlay_Write(0x7e0000 | w, y); pc += 3; break;  // STY abs
    case 0x1a: a++; pc += 1; break;  // INC A
    case 0x3a: a--; pc += 1; break;  // DEC A
    case 0xa2: x = w; pc += 3; break;  // LDX #
    case 0xa0: y = w; pc += 3; break;  // LDY #
    case 0xb6: x = QuestOverlay_Read((uint16)(b + y)); pc += 2; break;  // LDX dp,Y
    case 0xaa: x = a; pc += 1; break;  // TAX
    case 0x8a: a = x; pc += 1; break;  // TXA
    case 0xa8: y = a; pc += 1; break;  // TAY
    case 0x98: a = y; pc += 1; break;  // TYA
    case 0xe8: x++; pc += 1; break;  // INX
    case 0xc8: y++; pc += 1; break;  // INY
    case 0xca: x--; pc += 1; break;  // DEX
    case 0x88: y--; pc += 1; break;  // DEY
    case 0x18: c = 0; pc += 1; break;  // CLC
    case 0x38: c = 1; pc += 1; break;  // SEC
    case 0xb8: pc += 1; break;  // CLV
    case 0x69: t = a + w + c; c = t >> 16; a = t; pc += 3; break;  // ADC #
    case 0x29: a &= w; pc += 3; break;  // AND #
    case 0x25: a &= QuestOverlay_Read(b); pc += 2; break;  // AND dp
    case 0x01: a |= QuestOverlay_Read(0x7e0000 | QuestOverlay_Read((uint16)(b + x))); pc += 2; break;  // ORA (dp,X)
    case 0x26:  // ROL dp
      t = QuestOverlay_Read(b);
      QuestOverlay_Write(b, t << 1 | c), c = t >> 15, pc += 2;
      break;
    case 0x06:  // ASL dp
      t = QuestOverlay_Read(b);
      QuestOverlay_Write(b, t << 1), c = t >> 15, pc += 2;
      break;
    case 0x4c: pc = 0x8e0000 | w; break;  // JMP abs
    case 0x20:  // JSR abs
      if (sp == 8)
        goto unknown;
      stack[sp++] = pc + 3, pc = 0x8e0000 | w;
      break;
    case 0x60:  // RTS
      if (!sp)
        return true;
      pc = stack[--sp];
      break;
    default:
    unknown:
      fprintf(stderr, "Quest overlay of screen %d: $%02x at $%06x not run\n", overworld_screen_index, op,
              (unsigned)pc);
      return true;
    }
  }
  return true;
}

void Overworld_LoadEventOverlay() {  // 8ef652
  int x;
  uint16 *dst = dung_bg2;
  if (Overworld_RunQuestOverlay())
    return;
  switch (overworld_screen_index) {
  case 0: case 1: case 2:
    dst[XY(11, 16)] = 0xe32;
    dst[XY(12, 16)] = 0xe32;
    dst[XY(13, 16)] = 0xe32;
    dst[XY(14, 16)] = 0xe32;
    dst[XY(11, 17)] = 0xe32;
    dst[XY(14, 17)] = 0xe32;
    dst[XY(12, 17)] = 0xe33;
    dst[XY(13, 17)] = 0xe34;
    dst[XY(11, 18)] = 0xe35;
    dst[XY(12, 18)] = 0xe36;
    dst[XY(13, 18)] = 0xe37;
    dst[XY(14, 18)] = 0xe38;
    dst[XY(11, 19)] = 0xe39;
    dst[XY(12, 19)] = 0xe3a;
    dst[XY(13, 19)] = 0xe3b;
    dst[XY(14, 19)] = 0xe3c;
    dst[XY(12, 20)] = 0xe3d;
    dst[XY(13, 20)] = 0xe3e;
    break;
  case 3: case 4: case 5: case 6: case 7:
    dst[XY(16, 14)] = 0x212;
    break;
  case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15: case 16: case 17: case 18: case 19:
    x = XY(3, 10);
loc_8EF7B4:
    dst[x + XY(0, 0)] = 0x918;
    dst[x + XY(1, 0)] = 0x919;
    dst[x + XY(0, 1)] = 0x91a;
    dst[x + XY(1, 1)] = 0x91b;
    break;
  case 20:
    dst[XY(25, 10)] = 0xdd1;
    dst[XY(26, 10)] = 0xdd2;
    dst[XY(25, 11)] = 0xdd7;
    dst[XY(26, 11)] = 0xdd8;
    dst[XY(25, 12)] = 0xdd9;
    dst[XY(26, 12)] = 0xdda;
    break;
  case 21: case 22: case 23: case 24: case 25: case 32: case 33:
    dst[XY(31, 24)] = 0xe21;
    dst[XY(33, 24)] = 0xe21;
    dst[XY(32, 24)] = 0xe22;
    dst[XY(31, 25)] = 0xe23;
    dst[XY(32, 25)] = 0xe24;
    dst[XY(33, 25)] = 0xe25;
    break;
  case 26: case 27: case 28: case 35: case 36:
    dst[XY(30, 39)] = 0xdc1;
    dst[XY(31, 39)] = 0xdc2;
    dst[XY(30, 40)] = 0xdbe;
    dst[XY(31, 40)] = 0xdbf;
    dst[XY(32, 39)] = 0xdc2;
    dst[XY(33, 39)] = 0xdc3;
    dst[XY(32, 40)] = 0xdbf;
    dst[XY(33, 40)] = 0xdc0;
    break;
  case 29: case 30: case 31: case 34: case 37: case 38: case 39: case 40: case 41: case 42: case 43: case 107:
    x = XY(24, 6);
    goto loc_8EF7B4;
  case 44: case 45: case 46: case 47: case 48: case 49: case 56: case 57:
    x = XY(44, 6);
    goto loc_8EF7B4;
  case 50: case 51: case 52: case 53: case 54: case 55: case 119:
    x = XY(6, 8);
    goto loc_8EF7B4;
  case 58:
    x = XY(15, 20);
    goto loc_8EF7B4;
  case 59: case 123:
    dst[XY(22, 7)] = 0xddf;
    dst[XY(18, 8)] = 0xddf;
    dst[XY(16, 9)] = 0xddf;
    dst[XY(15, 10)] = 0xddf;
    dst[XY(14, 12)] = 0xddf;
    dst[XY(26, 14)] = 0xddf;
    dst[XY(23, 7)] = 0xde0;
    dst[XY(17, 9)] = 0xde0;
    dst[XY(24, 7)] = 0xde1;
    dst[XY(28, 8)] = 0xde1;
    dst[XY(29, 9)] = 0xde1;
    dst[XY(21, 11)] = 0xde1;
    dst[XY(29, 14)] = 0xde1;
    dst[XY(19, 8)] = 0xde2;
    dst[XY(20, 8)] = 0xde2;
    dst[XY(21, 8)] = 0xde2;
    dst[XY(25, 8)] = 0xde2;
    dst[XY(26, 8)] = 0xde2;
    dst[XY(27, 8)] = 0xde2;
    dst[XY(22, 8)] = 0xde3;
    dst[XY(18, 9)] = 0xde3;
    dst[XY(16, 10)] = 0xde3;
    dst[XY(15, 12)] = 0xde3;
    dst[XY(23, 8)] = 0xde4;
    dst[XY(19, 9)] = 0xde4;
    dst[XY(20, 9)] = 0xde4;
    dst[XY(24, 9)] = 0xde4;
    dst[XY(27, 9)] = 0xde4;
    dst[XY(17, 10)] = 0xde4;
    dst[XY(18, 10)] = 0xde4;
    dst[XY(19, 10)] = 0xde4;
    dst[XY(28, 10)] = 0xde4;
    dst[XY(16, 11)] = 0xde4;
    dst[XY(17, 11)] = 0xde4;
    dst[XY(18, 11)] = 0xde4;
    dst[XY(19, 11)] = 0xde4;
    dst[XY(16, 12)] = 0xde4;
    dst[XY(17, 12)] = 0xde4;
    dst[XY(15, 13)] = 0xde4;
    dst[XY(16, 13)] = 0xde4;
    dst[XY(15, 14)] = 0xde4;
    dst[XY(16, 14)] = 0xde4;
    dst[XY(19, 16)] = 0xde4;
    dst[XY(19, 17)] = 0xde4;
    dst[XY(20, 17)] = 0xde4;
    dst[XY(19, 18)] = 0xde4;
    dst[XY(24, 8)] = 0xde5;
    dst[XY(28, 9)] = 0xde5;
    dst[XY(20, 11)] = 0xde5;
    dst[XY(21, 12)] = 0xde5;
    dst[XY(21, 9)] = 0xde6;
    dst[XY(25, 9)] = 0xde6;
    dst[XY(20, 10)] = 0xde6;
    dst[XY(28, 11)] = 0xde6;
    dst[XY(21, 17)] = 0xde6;
    dst[XY(20, 18)] = 0xde6;
    dst[XY(22, 9)] = 0xde7;
    dst[XY(24, 10)] = 0xde7;
    dst[XY(15, 15)] = 0xde7;
    dst[XY(16, 15)] = 0xde7;
    dst[XY(19, 19)] = 0xde7;
    dst[XY(28, 19)] = 0xde7;
    dst[XY(23, 9)] = 0xde8;
    dst[XY(26, 9)] = 0xde8;
    dst[XY(27, 10)] = 0xde8;
    dst[XY(17, 15)] = 0xde8;
    dst[XY(18, 16)] = 0xde8;
    dst[XY(23, 10)] = 0xde9;
    dst[XY(26, 10)] = 0xde9;
    dst[XY(14, 15)] = 0xde9;
    dst[XY(17, 16)] = 0xde9;
    dst[XY(26, 18)] = 0xde9;
    dst[XY(27, 19)] = 0xde9;
    dst[XY(29, 10)] = 0xdea;
    dst[XY(28, 12)] = 0xdea;
    dst[XY(28, 13)] = 0xdea;
    dst[XY(29, 18)] = 0xdea;
    dst[XY(15, 11)] = 0xdeb;
    dst[XY(27, 11)] = 0xdeb;
    dst[XY(27, 12)] = 0xdeb;
    dst[XY(14, 13)] = 0xdeb;
    dst[XY(27, 13)] = 0xdeb;
    dst[XY(14, 14)] = 0xdeb;
    dst[XY(18, 17)] = 0xdeb;
    dst[XY(18, 18)] = 0xdeb;
    dst[XY(18, 12)] = 0xdec;
    dst[XY(17, 13)] = 0xdec;
    dst[XY(19, 12)] = 0xded;
    dst[XY(20, 12)] = 0xdee;
    dst[XY(18, 13)] = 0xdef;
    dst[XY(27, 15)] = 0xdef;
    dst[XY(19, 13)] = 0xdf0;
    dst[XY(19, 14)] = 0xdf0;
    dst[XY(20, 14)] = 0xdf0;
    dst[XY(21, 14)] = 0xdf0;
    dst[XY(21, 15)] = 0xdf0;
    dst[XY(27, 16)] = 0xdf0;
    dst[XY(28, 16)] = 0xdf0;
    dst[XY(20, 13)] = 0xdf1;
    dst[XY(28, 15)] = 0xdf1;
    dst[XY(21, 13)] = 0xdf2;
    dst[XY(17, 14)] = 0xdf3;
    dst[XY(18, 15)] = 0xdf3;
    dst[XY(20, 16)] = 0xdf3;
    dst[XY(18, 14)] = 0xdf4;
    dst[XY(19, 15)] = 0xdf5;
    dst[XY(20, 15)] = 0xdf6;
    dst[XY(27, 17)] = 0xdf6;
    dst[XY(26, 15)] = 0xdf7;
    dst[XY(29, 15)] = 0xdf8;
    dst[XY(21, 16)] = 0xdf9;
    dst[XY(26, 16)] = 0xdfa;
    dst[XY(29, 16)] = 0xdfb;
    dst[XY(26, 17)] = 0xdfc;
    dst[XY(28, 17)] = 0xdfd;
    dst[XY(29, 17)] = 0xdfe;
    dst[XY(27, 18)] = 0xdff;
    dst[XY(28, 18)] = 0xe00;
    dst[XY(21, 10)] = 0xe01;
    dst[XY(25, 10)] = 0xe01;
    dst[XY(21, 18)] = 0xe01;
    dst[XY(29, 11)] = 0xe02;
    dst[XY(20, 19)] = 0xe02;
    dst[XY(29, 19)] = 0xe02;
    dst[XY(18, 19)] = 0xe03;
    dst[XY(27, 14)] = 0xe04;
    dst[XY(28, 14)] = 0xe05;
    break;
  case 60: case 61: case 62: case 63: case 64: case 65: case 72: case 73:
    dst[XY(8, 11)] = 0xe13;
    dst[XY(11, 11)] = 0xe14;
    dst[XY(8, 12)] = 0xe15;
    dst[XY(9, 12)] = 0xe16;
    dst[XY(10, 12)] = 0xe17;
    dst[XY(11, 12)] = 0xe18;
    dst[XY(9, 13)] = 0xe19;
    dst[XY(10, 13)] = 0xe1a;
    dst[XY(9, 16)] = 0xe06;
    dst[XY(10, 16)] = 0xe06;
    dst[XY(8, 14)] = 0xe07;
    dst[XY(8, 15)] = 0xe07;
    dst[XY(9, 14)] = 0xe08;
    dst[XY(9, 15)] = 0xe08;
    dst[XY(10, 14)] = 0xe09;
    dst[XY(10, 15)] = 0xe09;
    dst[XY(11, 14)] = 0xe0a;
    dst[XY(11, 15)] = 0xe0a;
    break;
  case 66: case 67: case 68: case 75: case 76:
    dst[XY(47, 8)] = 0xe96;
    dst[XY(48, 8)] = 0xe97;
    dst[XY(47, 9)] = 0xe9c;
    dst[XY(47, 10)] = 0xe9c;
    dst[XY(48, 9)] = 0xe9d;
    dst[XY(48, 10)] = 0xe9d;
    dst[XY(47, 11)] = 0xe9a;
    dst[XY(48, 11)] = 0xe9b;
    break;
  case 69: case 70: case 77: case 78:
    x = XY(52, 16);
    goto loc_8EF7B4;
  case 71:
    dst[XY(15, 19)] = 0xe78;
    dst[XY(16, 19)] = 0xe79;
    dst[XY(17, 19)] = 0xe7a;
    dst[XY(18, 19)] = 0xe7b;
    dst[XY(15, 20)] = 0xe7c;
    dst[XY(16, 20)] = 0xe7d;
    dst[XY(17, 20)] = 0xe7e;
    dst[XY(18, 20)] = 0xe7f;
    dst[XY(15, 21)] = 0xe80;
    dst[XY(16, 21)] = 0xe81;
    dst[XY(17, 21)] = 0xe82;
    dst[XY(18, 21)] = 0xe83;
    dst[XY(15, 22)] = 0xe84;
    dst[XY(16, 22)] = 0xe85;
    dst[XY(17, 22)] = 0xe86;
    dst[XY(18, 22)] = 0xe87;
    break;
  case 74: case 79: case 80: case 81: case 82: case 83: case 84: case 85: case 86: case 87: case 88: case 89: case 96: case 97:
    dst[XY(31, 26)] = 0xe1b;
    dst[XY(32, 26)] = 0xe1c;
    dst[XY(31, 27)] = 0xe1d;
    dst[XY(32, 27)] = 0xe1e;
    dst[XY(31, 28)] = 0xe1f;
    dst[XY(32, 28)] = 0xe20;
    break;
  case 90: case 91: case 92: case 99: case 100:
    dst[XY(30, 7)] = 0xe3f;
    dst[XY(31, 7)] = 0xe40;
    dst[XY(32, 7)] = 0xe41;
    dst[XY(30, 8)] = 0xe42;
    dst[XY(31, 8)] = 0xe43;
    dst[XY(32, 8)] = 0xe44;
    dst[XY(30, 9)] = 0xe45;
    dst[XY(31, 9)] = 0xe46;
    dst[XY(32, 9)] = 0xe47;
    break;
  case 93: case 94: case 95: case 102: case 103:
    dst[XY(51, 3)] = 0xe31;
    dst[XY(53, 4)] = 0xe2d;
    dst[XY(53, 5)] = 0xe2e;
    dst[XY(53, 6)] = 0xe2f;
    break;
  case 98:
    x = XY(16, 26);
    goto loc_8EF7B4;
  case 101: case 104: case 105: case 106: case 108: case 109: case 110: case 111: case 112: case 113: case 120: case 121:
    dst[XY(17, 10)] = 0xe64;
    dst[XY(18, 10)] = 0xe65;
    dst[XY(19, 10)] = 0xe66;
    dst[XY(20, 10)] = 0xe67;
    dst[XY(17, 11)] = 0xe68;
    dst[XY(18, 11)] = 0xe69;
    dst[XY(19, 11)] = 0xe6a;
    dst[XY(20, 11)] = 0xe6b;
    dst[XY(17, 12)] = 0xe6c;
    dst[XY(18, 12)] = 0xe6d;
    dst[XY(19, 12)] = 0xe6e;
    dst[XY(20, 12)] = 0xe6f;
    dst[XY(17, 13)] = 0xe70;
    dst[XY(18, 13)] = 0xe71;
    dst[XY(19, 13)] = 0xe72;
    dst[XY(20, 13)] = 0xe73;
    dst[XY(17, 14)] = 0xe74;
    dst[XY(18, 14)] = 0xe75;
    dst[XY(19, 14)] = 0xe76;
    dst[XY(20, 14)] = 0xe77;
    break;
  case 114: case 115: case 116: case 117: case 118: case 122: case 124: case 125: case 126: case 127:
    assert(0);
  }
}

void Ancilla_TerminateWaterfallSplashes() {  // 8ffd3c
  uint8 t = BYTE(overworld_screen_index);
  if (t == 0xf) {
    for (int i = 4; i >= 0; i--) {
      if (ancilla_type[i] == 0x41)
        ancilla_type[i] = 0;
    }
  }
}

void Overworld_GetPitDestination() {  // 9bb860
  // Hole-only Inverted Ganon relocation: any fall on area 0x1B (LW Hyrule Castle)
  // post-Agahnim routes to the Ganon room via entrance 0x7B — the same drop the
  // DW pyramid uses (kFallHole entries 8-10, area 0x5B -> 0x7B). The only pit on
  // 0x1B is the relocated Ganon pit (the 0x037 attr override in tile_detect.c), so
  // a position match isn't needed. Gated on Inverted + the pyramid-hole bit, so
  // non-Inverted / pre-Agahnim 0x1B is byte-identical (the dormant vanilla 0x1B
  // fall-hole entry, pos 0x170 -> 0x7D, is unaffected outside this gate).
  bool rando_inverted = (enhanced_features1 & kFeatures1_RandomizerActive) &&
                        Rando_GetActiveWorldState() == 2 /* kWorldState_Inverted */;
  if (rando_inverted && overworld_area_index == 0x1B &&
      (save_ow_event_info[0x5B] & 0x20)) {
    which_entrance = 0x7B;
    byte_7E010F = 0;
    g_rando_takeany_door_id = 0;
    if (enhanced_features1 & kFeatures1_RandomizerActive)
      g_ram[kRam_RandoOverworldDoor] = 0;  // a pit is not an overworld door
    g_rando_entrance_exit_room = 0;
    g_rando_entrance_force_cached = 0;
    Rando_RecordEnteredFallhole();
    return;
  }
  uint16 x = (link_x_coord & ~7);
  uint16 y = (link_y_coord & ~7);
  uint16 pos = ((y - overworld_offset_base_y) & overworld_offset_mask_y) << 3;
  pos += (((x >> 3) - overworld_offset_base_x) & overworld_offset_mask_x);


  int i = 36 / 2;
  for (;;) {
    // Inverted hole-only relocation: the DW pyramid (area 0x5B) is no longer a
    // Ganon access — Ganon moved to the LW castle (0x1B, handled above). Its
    // fall-hole entries (-> entrance 0x7B) must NOT match, so a fall at the
    // pyramid drops to the Chris-Houlihan fallback below (kept in the DW), not to
    // Ganon. Faithful to ALTTPR repointing the pyramid hole entrances off 0x5B
    // (Rom.php:1842-1844). Non-Inverted is byte-identical.
    if (kFallHole_Pos[i] == pos && kFallHole_Area[i] == overworld_area_index &&
        !(rando_inverted && overworld_area_index == 0x5B))
      break;
    if (--i < 0) {
      // #82 Inverted: vanilla hardcodes the LIGHT world for the unmatched
      // (Chris-Houlihan) fall-hole fallback because vanilla's home world is the
      // LW. In Inverted the home world is the DARK world, so an unmatched pit
      // must keep Link in the DW (0x40) rather than flipping him to the LW.
      // Gated so vanilla / Open / Standard / Retro stay byte-identical.
      savegame_is_darkworld = rando_inverted ? 0x40 : 0;
      // Chris Houlihan's room
      which_entrance = 130;
      byte_7E010F = 0;
      g_rando_takeany_door_id = 0;  // fall-hole is never a take-any (Slice 3b)
      if (enhanced_features1 & kFeatures1_RandomizerActive)
        g_ram[kRam_RandoOverworldDoor] = 0;  // nor an overworld door (shop key)
      g_rando_entrance_exit_room = 0;  // Phase C — clear stale dungeon-coupling room
      g_rando_entrance_force_cached = 0;
      return;
    }
  }
  which_entrance = kFallHole_Entrances[i];
  byte_7E010F = 0;
  g_rando_takeany_door_id = 0;  // fall-hole is never a take-any (Slice 3b)
  if (enhanced_features1 & kFeatures1_RandomizerActive)
    g_ram[kRam_RandoOverworldDoor] = 0;  // nor an overworld door (shop key)
  // Phase C Stage 2 — a fall-hole is not a shuffled dungeon DOOR, so any pending
  // dungeon-coupling source room from a prior door entry must not leak into this
  // interior's exit. (Stage 3: also clear the cross cave→dungeon force-cached flag,
  // or it would corrupt this interior's exit branch — matches the Houlihan path.)
  g_rando_entrance_exit_room = 0;
  g_rando_entrance_force_cached = 0;
  // Decoupled / capture (D.3/D.4): a drop-in cave is entered by falling, so record
  // it here (keyed by which_entrance) — lets it be captured and decouple its exit.
  Rando_RecordEnteredFallhole();
}

void Overworld_UseEntrance() {  // 9bbbf4
  uint16 xc = link_x_coord >> 3, yc = link_y_coord + 7;
  uint16 pos = ((yc - overworld_offset_base_y) & overworld_offset_mask_y) * 8 +
    ((xc - overworld_offset_base_x) & overworld_offset_mask_x);

  const uint16 *m = Map16ToMap8Entry(dung_bg2[pos >> 1]);

  if (!link_direction_facing) {
    uint16 a = m[1] & 0x41ff;
    if (a == 0xe9)
      goto do_draw;

    if (a == 0x149 || a == 0x169)
      goto is_149_or_169;

    m = Map16ToMap8Entry(dung_bg2[(pos >> 1) + 1]);
    a = m[0] & 0x41ff;
    if (a == 0x40e9) {
      pos -= 2;
do_draw:
      Overworld_DrawMap16_Persist(pos + 0, 0xDA4);
      Overworld_DrawMap16_Persist(pos + 2, 0xDA6);
      sound_effect_2 = 21;
      nmi_load_bg_from_vram = 1;
      return;
    }
    if (a == 0x4149 || a == 0x4169) {
      pos -= 2;
is_149_or_169:
      door_open_closed_counter = 0;
      if (a & 0x20) {
        // 0x169
        if ((sram_progress_indicator & 0xf) >= 3)
          goto after;
        door_open_closed_counter = 24;
      }
      big_rock_starting_address = pos - 0x80;
      sound_effect_2 = 21;
      subsubmodule_index = 0;
      BYTE(door_animation_step_indicator) = 0;
      submodule_index = 12;
      return;
    }
  }
after:
  if (!LookupInOwEntranceTab(m[2] & 0x1ff, m[3] & 0x1ff)) {
    big_key_door_message_triggered = 0;
    return;
  }

  int lx = LookupInOwEntranceTab2(pos);
  if (lx < 0)
    return;

  // #82 Inverted: PreventEnterOnBonk (z3randomizer entrances.asm:194). In
  // Inverted, while in the mirror-warp state (kPlayerState_Mirror == 0x14) a bonk
  // while CROSSING worlds (overworld_screen_index & 0x40 !=
  // last_light_vs_dark_world == WorldCache) must NOT open/enter the overworld
  // door under Link. Upstream takes .done (enter normally) on the BEQ when the
  // world bit MATCHES the cache, and falls through to block-entry when it
  // differs — so the guard fires on `!=`. Gated so vanilla / Open / Standard /
  // Retro enter byte-identically.
  if ((enhanced_features1 & kFeatures1_RandomizerActive) &&
      Rando_GetActiveWorldState() == 2 /* kWorldState_Inverted */ &&
      link_player_handler_state == kPlayerState_Mirror &&
      (overworld_screen_index & 0x40) != last_light_vs_dark_world)
    return;

  if (!follower_dropped && (link_pose_for_item == 1 || !CanEnterWithTagalong(kOverworld_Entrance_Id[lx] - 1))) {
    if (!big_key_door_message_triggered) {
      big_key_door_message_triggered = 1;
      dialogue_message_index = 5;
      Main_ShowTextMessage();
    }
  } else {
    which_entrance = kOverworld_Entrance_Id[lx];
    // D.3 capture-for-bake: record the entered door's vanilla cave interior in
    // EVERY game (vanilla or rando) so the arrival capture works in any mode.
    Rando_RecordEnteredDoorForCapture((uint16)lx);
    // Phase B Slice 3b — Retro TakeAny redirect. `lx` is the overworld door
    // row-index (== ALTTPR's X); door_id = lx+1. If this cave is an active
    // take-any this seed, capture the door (the disambiguation key — the
    // redirect overwrites which_entrance with the shared host-room entrance,
    // destroying the source identity) and send the player into the take-any
    // host room. See src/rando/rando.c / add-rando-retro-takeany/design.md §D1.
    g_rando_takeany_door_id = 0;
    if (enhanced_features1 & kFeatures1_RandomizerActive) {
      // add-rando-shopsanity — record the entered door as ALTTPR's
      // PreviousOverworldDoor (row index + 1; z3randomizer doorframefixes.asm
      // StoreLastOverworldDoorID). This is the shop identity key:
      // `which_entrance` cannot serve, because four Dark World shops share
      // entrance id 0x60 (room 0x10F) and two share 0x58 (room 0x112). Stored
      // in g_ram so a snapshot replay-restore — which restores g_ram but not C
      // statics — keeps the shop the player is standing in resolvable. Gated on
      // the rando bit because this byte is inside the rando-owned WRAM range
      // (kRam_RandoOwned*), which must stay untouched in vanilla snapshots.
      g_ram[kRam_RandoOverworldDoor] = (uint8)(lx + 1);
      uint8 host = Rando_TakeAnyHostByDoorIndex((uint8)lx);
      if (host) {
        g_rando_takeany_door_id = (uint8)(lx + 1);
        which_entrance = host;
      }
    }
    // Phase C Stage 2 — dungeon entrance-shuffle coupling. If this door is a
    // SHUFFLED dungeon door (which_entrance above now loads a DIFFERENT dungeon
    // via the overlay), capture the SOURCE dungeon's room so the dungeon-exit
    // search returns Link to THIS door (overworld.c "exit" path below), not the
    // loaded dungeon's vanilla door. 0 for caves (auto-coupled) / unshuffled doors.
    g_rando_entrance_exit_room = 0;
    g_rando_entrance_force_cached = 0;
    if (enhanced_features1 & kFeatures1_RandomizerActive) {
      if (Chains_RuntimeRecordDoorEntry((uint16)lx))
        Chains_ClearFollowerForChainStart();
      g_rando_entrance_exit_room = Rando_EntranceCoupledExitRoom((uint16)lx);
      // Dungeon decoupled (Insanity): a one-way dungeon exit overrides the coupled
      // (return-to-source) room with the π_out target dungeon's room, so Link emerges
      // at a DIFFERENT dungeon's overworld door. Non-zero only for a pooled dungeon
      // door under dungeon-decoupled (self-maps return 0 → the coupled room stands).
      uint16 ddx = Rando_EntranceDungeonDecoupledExitRoom((uint16)lx);
      if (ddx != 0) g_rando_entrance_exit_room = ddx;
      // Cross-category: a cave door redirected to a dungeon loads a dungeon room
      // (search-exit class) but must return to the cave — force the cached exit.
      if (g_rando_entrance_exit_room == 0)
        g_rando_entrance_force_cached = Rando_EntranceForceCachedExit((uint16)lx) ? 1 : 0;
      // Decoupled (D.4): remember which cave interior this door belongs to so the
      // exit can emerge at net[interior]'s door. No-op unless decoupled is active.
      Rando_DecoupledSetEnteredDoor((uint16)lx);
      // Cross + decoupled: resolve this door's one-way exit target (cave or dungeon)
      // and stash it; AUTHORITATIVE over the coupled exit above when active. No-op
      // otherwise.
      Rando_CrossDecoupledSetExit((uint16)lx);
    }
    link_auxiliary_state = 0;
    link_incapacitated_timer = 0;
    main_module_index = 15;
    saved_module_for_menu = 6;
    submodule_index = 0;
    subsubmodule_index = 0;
  }
}

uint16 Overworld_ToolAndTileInteraction(uint16 x, uint16 y) {  // 9bbd82
  word_7E04B2 = 0;
  index_of_interacting_tile = 0;
  uint16 pos = ((y - overworld_offset_base_y) & overworld_offset_mask_y) * 8 +
    ((x - overworld_offset_base_x) & overworld_offset_mask_x);
  uint16 attr = overworld_tileattr[pos >> 1], yv;

  if (!(link_item_in_hand & 2)) {
    if (!(link_item_in_hand & 0x40)) {
      if (attr == 0x34 || attr == 0x71 || attr == 0x35 || attr == 0x10d ||
          attr == 0x10f || attr == 0xe1 || attr == 0xe2 || attr == 0xda ||
          attr == 0xf8 || attr == 0x10e) {  // shovelable
        if (link_position_mode != 1)
          return attr;
        if (overworld_screen_index == 0x2a && pos == 0x492)
          word_7E04B2 = pos;
        yv = 0xdc9;
        goto check_secret;
      } else if (attr == 0x37e) {  // isThickGrass
        if (link_position_mode == 1)
          return attr;
        scratch_0 = x * 8 - 8;
        scratch_1 = (y - 8) & ~7;
        index_of_interacting_tile = 3;
        yv = 0xdc5;
        goto check_secret;
      }
    }
    if ((yv = 2, attr == 0x36) || (yv = 4, attr == 0x72a)) {
      // is_bush
      if (link_position_mode != 1) {
        scratch_0 = (x & ~1) * 8;
        scratch_1 = y & ~0xf;
        index_of_interacting_tile = yv;
        yv = (attr == 0x72a) ? 0xdc8 : 0xdc7;
        uint32 result;
        uint8 terrain_result;
check_secret:
        // add-rando-grass-rock-shuffle: hook the bush-cut / thick-grass-cut
        // consume (yv 0xdc5/0xdc7/0xdc8). The shovel/dig path (yv==0xdc9) is a
        // dig spot, NOT a terrain object — leave it to vanilla. On Suppress
        // the placed item was granted; skip RevealSecret and set the 0xFF
        // no-spawn sentinel (a ZERO would trigger the outdoor random-secret
        // substitution) so nothing spawns and yv keeps its cut-tile value.
        terrain_result = yv == 0xdc9 ? kRandoTerrain_Vanilla :
            Rando_TerrainRevealHook(overworld_screen_index, pos);
        if (terrain_result == kRandoTerrain_Retry)
          return attr;
        if (terrain_result == kRandoTerrain_Suppress) {
          BYTE(dung_secrets_unk1) = 0xff;
        } else {
          result = Overworld_RevealSecret(pos);
          if (result != 0)
            yv = result;
        }
memoize_getout:
        overworld_tileattr[pos >> 1] = yv;
        Overworld_Memorize_Map16_Change(pos, yv);
        Overworld_DrawMap16(pos, yv);
        nmi_load_bg_from_vram = 1;
      }
      uint16 t;
      t = Map16ToMap8Entry(attr)[((y & 8) >> 2) + (x & 1)];
      attr = kMap8DataToTileAttr[t & 0x1ff];
      if (index_of_interacting_tile) {
        Sprite_SpawnImmediatelySmashedTerrain(index_of_interacting_tile, scratch_0, scratch_1);
        AncillaAdd_BushPoof(scratch_0, scratch_1);
      }
      return attr;
    }
    return attr;
  } else {  // else_1
    if (attr == 0x21b) {
      sound_effect_1 = 17;
      HandlePegPuzzles(pos);
      if (s_rando_peg_grant_blocked)
        return attr;
      yv = 0xdcb;
      goto memoize_getout;
    } else { // else_3
      Overworld_PickHammerSfx(attr);
      return attr;
    }
  }

  return 0;
}

void Overworld_PickHammerSfx(uint16 a) {  // 9bbf1e
  uint16 attr = kMap8DataToTileAttr[Map16ToMap8Entry(a)[0] & 0x1ff];
  uint8 y;
  if (attr < 0x50) {
    return;
  } else if (attr < 0x52) {
    y = 26;
  } else if (attr < 0x54) {
    y = 17;
  } else if (attr < 0x58) {
    y = 5;
  } else {
    return;
  }
  sound_effect_1 = y;
}

uint16 Overworld_GetLinkMap16Coords(Point16U *xy) {  // 9bbf64
  uint16 x = (link_x_coord + kGetBestActionToPerformOnTile_x[link_direction_facing >> 1]) & ~0xf;
  uint16 y = (link_y_coord + kGetBestActionToPerformOnTile_y[link_direction_facing >> 1]) & ~0xf;
  xy->x = x;
  xy->y = y;
  uint16 rv = ((y - overworld_offset_base_y) & overworld_offset_mask_y) << 3;
  return rv + (((x >> 3) - overworld_offset_base_x) & overworld_offset_mask_x);
}

uint8 Overworld_HandleLiftableTiles(Point16U *pt_arg) {  // 9bbf9d
  uint16 pos = Overworld_GetLinkMap16Coords(pt_arg);
  Point16U pt = *pt_arg;
  uint16 a = overworld_tileattr[pos >> 1], y;
  if ((y = 0, a == 0x36d) || (y = 1, a == 0x36e) || (y = 2, a == 0x374) || (y = 3, a == 0x375) ||
      (y = 0, a == 0x23b) || (y = 1, a == 0x23c) || (y = 2, a == 0x23d) || (y = 3, a == 0x23e)) {
    return SmashRockPile_fromLift(a, pos, y, pt);
  } else if ((y = 0xdc7, a == 0x36) || (y = 0xdc8, a == 0x72a) || (y = 0xdca, a == 0x20f) || (y = 0xdca, a == 0x239) || (y = 0xdc6, a == 0x101)) {
    return Overworld_LiftingSmallObj(a, pos, y, pt);
  } else {
    return kMap8DataToTileAttr[Map16ToMap8Entry(a)[(pt.x & 8 ? 2 : 0) + (pt.y & 8 ? 1 : 0)] & 0x1ff];
  }
}

uint8 Overworld_LiftingSmallObj(uint16 a, uint16 pos, uint16 y, Point16U pt) {  // 9bc008
  // add-rando-grass-rock-shuffle: lift-consume of a bush / small rock. A sign
  // (map16 0x101) also routes here but is NOT registered, so the hook returns
  // Vanilla for it. On Suppress the placed item was granted; keep the lifted
  // replacement tile (y) and set the 0xFF no-spawn sentinel.
  uint8 terrain_result = Rando_TerrainRevealHook(overworld_screen_index, pos);
  if (terrain_result == kRandoTerrain_Retry) {
    // Return the "nothing was lifted" sentinel (0), as the dungeon counterpart
    // does. Returning the success expression made a refused grant
    // indistinguishable from a real lift, so the caller carried the object away
    // while the tile stayed on screen -- a duplicated, re-liftable bush/rock.
    return 0;
  }
  if (terrain_result == kRandoTerrain_Suppress) {
    BYTE(dung_secrets_unk1) = 0xff;
  } else {
    uint16 secret = Overworld_RevealSecret(pos);
    if (secret != 0)
      y = secret;
  }
  overworld_tileattr[pos >> 1] = y;
  Overworld_Memorize_Map16_Change(pos, y);
  Overworld_DrawMap16(pos, y);
  nmi_load_bg_from_vram = 1;
  return kMap8DataToTileAttr[Map16ToMap8Entry(a)[(pt.x & 8 ? 2 : 0) + (pt.y & 8 ? 1 : 0)] & 0x1ff];
}

int Overworld_SmashRockPile(bool down_one_tile, Point16U *pt) {  // 9bc055
  uint16 bak = link_y_coord;
  link_y_coord += down_one_tile ? 8 : 0;
  uint16 pos = Overworld_GetLinkMap16Coords(pt);
  link_y_coord = bak;
  uint16 a = dung_bg2[pos >> 1];
  uint8 y = 0;
  if ((y = 0, a == 0x226) || (y = 1, a == 0x227) || (y = 2, a == 0x228) || (y = 3, a == 0x229)) {
    return SmashRockPile_fromLift(a, pos, y, *pt);
  } else if (a == 0x36) {
    return Overworld_LiftingSmallObj(a, pos, 0xDC7, *pt);
  } else {
    return -1;
  }
}

uint8 SmashRockPile_fromLift(uint16 a, uint16 pos, uint16 y, Point16U pt) {  // 9bc09f
  static const int8 kBigRockTab1[] = { 0, -1, -64, -65 };
  static const int8 kBigRockTabY[] = { 0, 0, -64, -64 };
  static const int8 kBigRockTabX[] = { 0, -1, 0, -1 };
  pos = 2 * ((pos >> 1) + kBigRockTab1[y]);

  // add-rando-grass-rock-shuffle: big-pile lift-consume, keyed on the pile
  // ORIGIN pos (reassigned above — the registry key). A pile whose vanilla
  // secret is the structural stairs reveal (data 0xffff) is excluded from the
  // registry, so the hook returns Vanilla and the stairs branch below still
  // runs. On Suppress the placed item was granted; no structural reveal.
  uint16 secret;
  uint8 terrain_result = Rando_TerrainRevealHook(overworld_screen_index, pos);
  if (terrain_result == kRandoTerrain_Retry) {
    // Return the "nothing was lifted" sentinel (0), as the dungeon counterpart
    // does. Returning the success expression made a refused grant
    // indistinguishable from a real lift, so the caller carried the object away
    // while the tile stayed on screen -- a duplicated, re-liftable bush/rock.
    return 0;
  }

  big_rock_starting_address = pos;
  door_open_closed_counter = 40;
  *(uint16 *)&g_ram[0] = pt.y;
  *(uint16 *)&g_ram[2] = pt.x;

  if (terrain_result == kRandoTerrain_Suppress) {
    BYTE(dung_secrets_unk1) = 0xff;
    secret = 0;
  } else {
    secret = Overworld_RevealSecret(pos);
  }
  pt.y = *(uint16 *)&g_ram[0];
  pt.x = *(uint16 *)&g_ram[2];

  if (secret == 0xffff) {
    save_ow_event_info[overworld_screen_index] |= 0x20;
    sound_effect_2 = 27;
    door_open_closed_counter = 80;
  }
  pt.x += kBigRockTabX[y] * 2;
  pt.y += kBigRockTabY[y] * 2;

  Overworld_DoMapUpdate32x32_B(); // WARNING: The original destroys ram[0] and ram[2]

  return kMap8DataToTileAttr[Map16ToMap8Entry(a)[(pt.x & 8 ? 2 : 0) + (pt.y & 8 ? 1 : 0)] & 0x1ff];
}

void Overworld_BombTiles32x32(uint16 x, uint16 y) {  // 9bc0f8
  x = (x - 23) & ~7;
  y = (y - 20) & ~7;

  for (int yy = 3; yy != 0; yy--, y += 16) {
    for (int xx = 3, xt = x; xx != 0; xx--, xt += 16) {
      Overworld_BombTile(xt, y);
    }
  }
  word_7E0486 = x, word_7E0488 = y;
}

void Overworld_BombTile(int x, int y) {  // 9bc155
  int a, j, k;

  int pos = ((y - overworld_offset_base_y & overworld_offset_mask_y) << 3) +
    ((x >> 3) - overworld_offset_base_x & overworld_offset_mask_x);

  if (follower_indicator == 13)
    goto label_a;

  a = dung_bg2[pos >> 1];

  if (a == 0x36) {
    k = 2, j = 0xdc7;
  } else if (a == 0x72a) {
    k = 4, j = 0xdc8;
  } else if (a == 0x37e) {
    k = 3, j = 0xdc5;
  } else {
    goto label_a;
  }
  // add-rando-grass-rock-shuffle: bomb-consume of a bush / thick grass (this
  // is the CONSUMING branch — the label_a probe below runs for non-consumed
  // blast tiles and the super-bomb follower and must NOT be hooked). On
  // Suppress the placed item was granted; draw the cut tile (j), no secret.
  uint8 terrain_result = Rando_TerrainRevealHook(overworld_screen_index, pos);
  if (terrain_result == kRandoTerrain_Retry)
    return;
  if (terrain_result == kRandoTerrain_Suppress) {
    BYTE(dung_secrets_unk1) = 0xff;
    a = j;
  } else {
    a = Overworld_RevealSecret(pos);
    if (a == 0)
      a = j;
  }
  dung_bg2[pos >> 1] = a;
  Overworld_Memorize_Map16_Change(pos, a);
  Overworld_DrawMap16(pos, a);

  Sprite_SpawnImmediatelySmashedTerrain(k, x & ~7, y & ~7);
  nmi_load_bg_from_vram = 1;
  return;

label_a:
  a = Overworld_RevealSecret(pos);
  if (a == 0xdb4) {
    dung_bg2[pos >> 1] = a;
    Overworld_Memorize_Map16_Change(pos, a);
    Overworld_DrawMap16(pos, a);

    dung_bg2[(pos >> 1) + 1] = 0xDB5;
    Overworld_Memorize_Map16_Change(pos, 0xDB5);  // wtf
    Overworld_DrawMap16(pos + 2, 0xDB5);
    nmi_load_bg_from_vram = 1;
    save_ow_event_info[overworld_screen_index] |= 2;
  }
}

void Overworld_AlterWeathervane() {  // 9bc21d
  door_open_closed_counter = 0x68;
  big_rock_starting_address = 0xc3e;
  Overworld_DoMapUpdate32x32_B();
  Overworld_DrawMap16_Persist(0xc42, 0xe21);
  Overworld_DrawMap16_Persist(0xcc2, 0xe25);

  save_ow_event_info[0x18] |= 0x20;
  nmi_load_bg_from_vram = 1;
}

void OpenGargoylesDomain() {  // 9bc264
  Overworld_DrawMap16_Persist(0xd3e, 0xe1b);
  Overworld_DrawMap16_Persist(0xd40, 0xe1c);
  Overworld_DrawMap16_Persist(0xdbe, 0xe1d);
  Overworld_DrawMap16_Persist(0xdc0, 0xe1e);
  Overworld_DrawMap16_Persist(0xe3e, 0xe1f);
  Overworld_DrawMap16_Persist(0xe40, 0xe20);
  save_ow_event_info[0x58] |= 0x20;
  sound_effect_2 = 0x1b;
  nmi_load_bg_from_vram = 1;
}

void CreatePyramidHole() {  // 9bc2a7
  // #82 Inverted: the animated post-bat-slam pyramid hole. In Inverted the
  // pyramid sits at a different screen position than vanilla DW screen 0x5B,
  // so the carved hole occupies a different map16 footprint (z3randomizer
  // inverted.asm Overworld_CreatePyramidHoleModified .invertedBehavior). The
  // *static* (reload-the-screen-with-hole-already-present) inverted overlay is
  // already handled by the OWW_CUSTOM_MAP1B_AGA branch in
  // Overworld_ApplyInvertedTiles (inverted_maps_apply.c) on inverted screen
  // 0x1B. To keep the animated reveal and the static reload byte-identical, the
  // positions/tiles below are exactly the (pos, tile) pairs the MAP1B_AGA
  // stripe stream expands to (see kInvertedMapData screen 0x1B): a 0x046D base
  // tile, then 0xE39 and two 4-wide RLEINC runs 0xE3A.. / 0xE3E.. and the
  // 0x490/0x491 debris pair. Gated on the Inverted rando world-state so vanilla
  // / Open / Standard / Retro carve the original footprint unchanged. This only
  // rewrites the live map16 buffer + queues the matching VRAM upload (via
  // Overworld_DrawMap16_Persist); the event bit / SFX / reachability bump below
  // are shared.
  if ((enhanced_features1 & kFeatures1_RandomizerActive) &&
      Rando_GetActiveWorldState() == 2 /* kWorldState_Inverted */) {
    // Hole-only relocation: carve the no-art Ganon pit on screen 0x1B (a 2-tone
    // dark diamond). The pyramid-facade/hole blocks (0x046d/0x0e39../0x0490/
    // 0x0491) are deliberately NOT placed here — they render as garbage with the
    // castle gfx/palette; the pit blocks live in the InvertedHoleBlocks_Install
    // map16 shadow. Footprint + block layout are shared with the static reload
    // paint in Overworld_ApplyInvertedTiles (inverted_maps_apply.c).
    //
    // Only animate the carve when actually on the castle screen (0x1B). The
    // retreat-bat slam can fire while the player is at the DW pyramid (0x5B —
    // the bat isn't repositioned to the castle in this fork), and carving the pit
    // blocks there would draw a spurious dark hole on the pyramid. When the carve
    // is skipped, the static reload paint in Overworld_ApplyInvertedTiles draws
    // the pit the next time the player loads screen 0x1B (gated on the same
    // post-Agahnim bit set below), so the castle hole still appears.
    if ((uint8)overworld_screen_index == 0x1B)
      Inverted_CarveCastleHole();
  } else {
    Overworld_DrawMap16_Persist(0x3bc, 0xe3f);
    Overworld_DrawMap16_Persist(0x3be, 0xe40);
    Overworld_DrawMap16_Persist(0x3c0, 0xe41);
    Overworld_DrawMap16_Persist(0x43c, 0xe42);
    Overworld_DrawMap16_Persist(0x43e, 0xe43);
    Overworld_DrawMap16_Persist(0x440, 0xe44);
    Overworld_DrawMap16_Persist(0x4bc, 0xe45);
    Overworld_DrawMap16_Persist(0x4be, 0xe46);
    Overworld_DrawMap16_Persist(0x4c0, 0xe47);
  }
  WORD(sound_effect_ambient) = 0x3515;
  save_ow_event_info[0x5b] |= 0x20;
  // Phase B Slice 1 §38 — Pyramid-hole creation opens a new path to GT
  // (post-Aga2). Reachability state changes; bump the tracker counter so
  // its memoized reachability cache invalidates.
  if (enhanced_features1 & kFeatures1_RandomizerActive)
    Rando_MarkLocationChecked(LOC_Agahnim_2);
  sound_effect_2 = 3;
  nmi_load_bg_from_vram = 1;
}

// Strange return value in Carry/R14
uint16 Overworld_RevealSecret(uint16 pos) {  // 9bc8a4
  BYTE(dung_secrets_unk1) = 0;

  if (overworld_screen_index >= 0x80) {
fail:
    AdjustSecretForPowder();
    return 0;
  }

  const uint8 *ptr = kOverworldSecrets + kOverworldSecrets_Offs[overworld_screen_index];
  for (;;) {
    uint16 x = *(uint16 *)ptr;
    if (x == 0xffff)
      goto fail;
    if ((x & 0x7fff) == pos)
      break;
    ptr += 3;
  }
  uint8 data = ptr[2];
  if (data && data < 0x80)
    BYTE(dung_secrets_unk1) |= data;
  if (data < 0x80) {
    AdjustSecretForPowder();
    return 0; // carry set
  }

  BYTE(dung_secrets_unk1) = 0xff;
  if (data != 0x84 && !(save_ow_event_info[overworld_screen_index] & 2)) {
    if (overworld_screen_index == 0x5b && follower_indicator != 13)
      goto fail;
    sound_effect_2 = 0x1b;
  // The discovery chime is missing when lifting the rock covering the magic portal leading to the Ice Temple
  } else if (data == 0x82 && (enhanced_features0 & kFeatures0_MiscBugFixes)) {
    sound_effect_2 = 0x1b;
  }
  static const uint16 kTileBelow[4] = { 0xDCC, 0x212, 0xFFFF, 0xDB4 };
  AdjustSecretForPowder();
  return kTileBelow[(data & 0xf) >> 1];

}

void AdjustSecretForPowder() {  // 9bc943
  if (link_item_in_hand & 0x40)
    dung_secrets_unk1 = 4;
}

void Overworld_DrawMap16_Persist(uint16 pos, uint16 value) {  // 9bc97c
  dung_bg2[pos >> 1] = value;
  Overworld_DrawMap16(pos, value);
}

void Overworld_DrawMap16(uint16 pos, uint16 value) {  // 9bc980
  pos = Overworld_FindMap16VRAMAddress(pos);
  uint16 *dst = &vram_upload_data[vram_upload_offset >> 1];
  const uint16 *src = Map16ToMap8Entry(value);
  dst[0] = swap16(pos);
  dst[1] = 0x300;
  dst[2] = src[0];
  dst[3] = src[1];
  dst[4] = swap16(pos + 0x20);
  dst[5] = 0x300;
  dst[6] = src[2];
  dst[7] = src[3];
  dst[8] = 0xffff;
  vram_upload_offset += 16;
}

void Overworld_AlterTileHardcore(uint16 pos, uint16 value) {  // 9bc9de
  dung_bg2[pos >> 1] = value;
  pos = Overworld_FindMap16VRAMAddress(pos);
  uint16 *dst = &vram_upload_data[vram_upload_offset >> 1];
  const uint16 *src = Map16ToMap8Entry(value);
  dst[0] = swap16(pos);
  dst[1] = 0x300;
  dst[2] = src[0];
  dst[3] = src[1];
  dst[4] = swap16(pos + 0x20);
  dst[5] = 0x300;
  dst[6] = src[2];
  dst[7] = src[3];
  dst[8] = 0xffff;
  vram_upload_offset += 16;
}

uint16 Overworld_FindMap16VRAMAddress(uint16 addr) {  // 9bca69
  return (((addr & 0x3f) >= 0x20) ? 0x400 : 0) + (((addr & 0xfff) >= 0x800) ? 0x800 : 0) + (addr & 0x1f) + ((addr & 0x780) >> 1);
}

void Overworld_AnimateEntrance() {  // 9bcac4
  // #82 Inverted: AnimatedEntranceFix (z3randomizer entrances.asm:215). In
  // Inverted, when in the LIGHT world (overworld_screen_index & 0x40 == 0) the
  // overworld entrance-open animation is suppressed and the cutscene trigger is
  // cleared (the inverted LW topology has no animated entrances to play). Gated
  // so vanilla / Open / Standard / Retro run the animation byte-identically.
  if ((enhanced_features1 & kFeatures1_RandomizerActive) &&
      Rando_GetActiveWorldState() == 2 /* kWorldState_Inverted */ &&
      !(overworld_screen_index & 0x40)) {
    trigger_special_entrance = 0;
    return;
  }
  uint8 j = trigger_special_entrance;
  flag_is_link_immobilized = j;
  flag_unk1 = j;
  nmi_disable_core_updates = j;
  kOverworld_EntranceSequence[j - 1]();
}

void Overworld_AnimateEntrance_PoD() {  // 9bcade
  switch (subsubmodule_index) {
  case 0:
    if (++overworld_entrance_sequence_counter != 0x40)
      return;
    OverworldEntrance_AdvanceAndBoom();
    save_ow_event_info[0x5e] |= 0x20;
    Overworld_DrawMap16_Persist(0x1e6, 0xe31);
    Overworld_DrawMap16_Persist(0x2ea, 0xe30);
    Overworld_DrawMap16_Persist(0x26a, 0xe26);
    Overworld_DrawMap16_Persist(0x2ea, 0xe27);
    nmi_load_bg_from_vram = 1;
    break;
  case 1:
    if (++overworld_entrance_sequence_counter != 0x20)
      return;
    OverworldEntrance_AdvanceAndBoom();
    Overworld_DrawMap16_Persist(0x26a, 0xe28);
    Overworld_DrawMap16_Persist(0x2ea, 0xe29);
    nmi_load_bg_from_vram = 1;
    break;
  case 2:
    if (++overworld_entrance_sequence_counter != 0x20)
      return;
    OverworldEntrance_AdvanceAndBoom();
    Overworld_DrawMap16_Persist(0x26a, 0xe2a);
    Overworld_DrawMap16_Persist(0x2ea, 0xe2b);
    Overworld_DrawMap16_Persist(0x36a, 0xe2c);
    nmi_load_bg_from_vram = 1;
    break;
  case 3:
    if (++overworld_entrance_sequence_counter != 0x20)
      return;
    OverworldEntrance_AdvanceAndBoom();
    Overworld_DrawMap16_Persist(0x26a, 0xe2d);
    Overworld_DrawMap16_Persist(0x2ea, 0xe2e);
    Overworld_DrawMap16_Persist(0x36a, 0xe2f);
    nmi_load_bg_from_vram = 1;
    break;
  case 4:
    if (++overworld_entrance_sequence_counter != 0x20)
      return;
    OverworldEntrance_PlayJingle();
    break;
  }
}

// Dark Forest Palace
void Overworld_AnimateEntrance_Skull() {  // 9bcba6
  switch (subsubmodule_index) {
  case 0:
    if (++overworld_entrance_sequence_counter != 4)
      return;
    overworld_entrance_sequence_counter = 0;
    subsubmodule_index++;
    Overworld_DrawMap16_Persist(0x409 * 2, 0xe06);
    Overworld_DrawMap16_Persist(0x40a * 2, 0xe06);
    save_ow_event_info[BYTE(overworld_screen_index)] |= 0x20;
    nmi_load_bg_from_vram = 1;
    sound_effect_2 = 0x16;
    break;
  case 1:
    if (++overworld_entrance_sequence_counter != 12)
      return;
    overworld_entrance_sequence_counter = 0;
    subsubmodule_index++;
    Overworld_DrawMap16_Persist(0x3c8 * 2, 0xe07);
    Overworld_DrawMap16_Persist(0x3c9 * 2, 0xe08);
    Overworld_DrawMap16_Persist(0x3ca * 2, 0xe09);
    Overworld_DrawMap16_Persist(0x3cb * 2, 0xe0a);
    nmi_load_bg_from_vram = 1;
    sound_effect_2 = 0x16;
    break;
  case 2:
    if (++overworld_entrance_sequence_counter != 12)
      return;
    overworld_entrance_sequence_counter = 0;
    subsubmodule_index++;
    Overworld_DrawMap16_Persist(0x388 * 2, 0xe07);
    Overworld_DrawMap16_Persist(0x389 * 2, 0xe08);
    Overworld_DrawMap16_Persist(0x38a * 2, 0xe09);
    Overworld_DrawMap16_Persist(0x38b * 2, 0xe0a);
    nmi_load_bg_from_vram = 1;
    sound_effect_2 = 0x16;
    break;
  case 3:
    if (++overworld_entrance_sequence_counter != 12)
      return;
    overworld_entrance_sequence_counter = 0;
    subsubmodule_index++;
    Overworld_DrawMap16_Persist(0x2c8 * 2, 0xe11);
    Overworld_DrawMap16_Persist(0x2cb * 2, 0xe12);
    Overworld_DrawMap16_Persist(0x308 * 2, 0xe0d);
    Overworld_DrawMap16_Persist(0x309 * 2, 0xe0e);
    Overworld_DrawMap16_Persist(0x30a * 2, 0xe0f);
    Overworld_DrawMap16_Persist(0x30b * 2, 0xe10);
    Overworld_DrawMap16_Persist(0x349 * 2, 0xe0b);
    Overworld_DrawMap16_Persist(0x34a * 2, 0xe0c);
    nmi_load_bg_from_vram = 1;
    sound_effect_2 = 0x16;
    break;
  case 4:
    if (++overworld_entrance_sequence_counter != 12)
      return;
    overworld_entrance_sequence_counter = 0;
    subsubmodule_index++;
    Overworld_DrawMap16_Persist(0x2c8 * 2, 0xe13);
    Overworld_DrawMap16_Persist(0x2cb * 2, 0xe14);
    Overworld_DrawMap16_Persist(0x308 * 2, 0xe15);
    Overworld_DrawMap16_Persist(0x309 * 2, 0xe16);
    Overworld_DrawMap16_Persist(0x30a * 2, 0xe17);
    Overworld_DrawMap16_Persist(0x30b * 2, 0xe18);
    Overworld_DrawMap16_Persist(0x349 * 2, 0xe19);
    Overworld_DrawMap16_Persist(0x34a * 2, 0xe1a);
    nmi_load_bg_from_vram = 1;
    sound_effect_2 = 0x16;
    OverworldEntrance_PlayJingle();
    break;
  }
}

void Overworld_AnimateEntrance_Mire() {  // 9bccd4
  static const uint8 kMiseryMireEntranceBits[26] = {
    0xff, 0xf7, 0xf7, 0xfb, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0x88, 0x88, 0x88, 0x88, 0x80, 0x80, 0x80, 0x80, 0x80,
  };

  int j;
  if (subsubmodule_index >= 2) {
    bg1_x_offset = frame_counter & 1 ? -1 : 1;
    bg1_y_offset = -bg1_x_offset;
  }
  switch (subsubmodule_index) {
  case 0:
    if ((j = ++overworld_entrance_sequence_counter) < 32)
      break;
    j -= 32;
    if (j == 207)
      subsubmodule_index = 1, overworld_entrance_sequence_counter = 0;
    TS_copy = (kMiseryMireEntranceBits[j >> 3] & (0x80 >> (j & 7))) != 0;
    break;
  case 1: case 2:
    if ((j = ++overworld_entrance_sequence_counter) == 16) {
      subsubmodule_index++;
      sound_effect_ambient = 7;
    }
    if (j != 72)
      break;
    OverworldEntrance_AdvanceAndBoom();
    save_ow_event_info[BYTE(overworld_screen_index)] |= 0x20;
    j = 0xe48;
draw_misery_2:
    Overworld_DrawMap16_Persist(0x622, j++);
    Overworld_DrawMap16_Persist(0x624, j++);
    Overworld_DrawMap16_Persist(0x626, j++);
    Overworld_DrawMap16_Persist(0x628, j++);
    Overworld_DrawMap16_Persist(0x6a2, j++);
    Overworld_DrawMap16_Persist(0x6a4, j++);
    Overworld_DrawMap16_Persist(0x6a6, j++);
    Overworld_DrawMap16_Persist(0x6a8, j++);
    Overworld_DrawMap16_Persist(0x722, j++);
    Overworld_DrawMap16_Persist(0x724, j++);
    Overworld_DrawMap16_Persist(0x726, j++);
    Overworld_DrawMap16_Persist(0x728, j++);
    nmi_load_bg_from_vram = 1;
    break;
  case 3:
    if ((j = ++overworld_entrance_sequence_counter) != 72)
      break;
    OverworldEntrance_AdvanceAndBoom();
    j = 0xe54;
draw_misery_3:
    Overworld_DrawMap16_Persist(0x5a2, j++);
    Overworld_DrawMap16_Persist(0x5a4, j++);
    Overworld_DrawMap16_Persist(0x5a6, j++);
    Overworld_DrawMap16_Persist(0x5a8, j++);
    goto draw_misery_2;
  case 4:
    if ((j = ++overworld_entrance_sequence_counter) != 80)
      break;
    OverworldEntrance_AdvanceAndBoom();
    j = 0xe64;
    Overworld_DrawMap16_Persist(0x522, j++);
    Overworld_DrawMap16_Persist(0x524, j++);
    Overworld_DrawMap16_Persist(0x526, j++);
    Overworld_DrawMap16_Persist(0x528, j++);
    goto draw_misery_3;
  case 5:
    if ((j = ++overworld_entrance_sequence_counter) != 128)
      break;
    OverworldEntrance_PlayJingle();
    sound_effect_ambient = 5;
    break;
  }
}

void Overworld_AnimateEntrance_TurtleRock() {  // 9bce28
  bg1_x_offset = frame_counter & 1 ? -1 : 1;
  bg1_y_offset = -bg1_x_offset;

  switch (subsubmodule_index) {
  case 0:
    save_ow_event_info[overworld_screen_index] |= 0x20;
    Dungeon_ApproachFixedColor_variable(0);
    vram_upload_data[0] = 0x10;
common:
    vram_upload_data[1] = 0xfe47;
    vram_upload_data[2] = 0x1e3;
    BYTE(vram_upload_data[3]) = 0xff;
    subsubmodule_index++;
    nmi_load_bg_from_vram = 1;
    break;
  case 1:
    vram_upload_data[0] = 0x14;
    goto common;
  case 2:
    vram_upload_data[0] = 0x18;
    goto common;
  case 3:
    vram_upload_data[0] = 0x1c;
    goto common;
  case 4:
    for (int i = 0; i < 8; i++)
      main_palette_buffer[0x58 + i] = aux_palette_buffer[0x68 + i] = 0;
    BG1VOFS_copy2 = BG2VOFS_copy2;
    BG1HOFS_copy2 = BG2HOFS_copy2;
    subsubmodule_index++;
    flag_update_cgram_in_nmi++;
    break;
  case 5: {
    OverworldEntrance_DrawManyTR();
    TS_copy = 1;
    CGWSEL_copy = 2;
    CGADSUB_copy = 0x22;
    uint16 *vram = vram_upload_data, *vram_end = vram + (vram_upload_offset >> 1);
    do {
      vram[0] |= 0x10;
      if (vram[2] == 0x8aa)
        vram[2] = 0x1e3;
      if (vram[3] == 0x8aa)
        vram[3] = 0x1e3;
    } while ((vram += 4) != vram_end);
    turtlerock_ctr = 0;
    subsubmodule_index++;
    break;
  }
  case 6:
    if (!(frame_counter & 1)) {
      if (!(turtlerock_ctr & 7)) {
        PaletteFilter_RestoreAdditive(0xb0, 0xc0);
        PaletteFilter_RestoreSubtractive(0xd0, 0xe0);
        flag_update_cgram_in_nmi++;
        sound_effect_2 = 2;
      }
      if (!--turtlerock_ctr) {
        turtlerock_ctr = 0x30;
        subsubmodule_index++;
      }
    }
    break;
  case 7:
    if (!(frame_counter & 1) && !(turtlerock_ctr & 7))
      sound_effect_2 = 2;
    if (!--turtlerock_ctr) {
      OverworldEntrance_DrawManyTR();
      TS_copy = 0;
      CGWSEL_copy = 0x82;
      CGADSUB_copy = 0x20;
      subsubmodule_index++;
      sound_effect_ambient = 5;
    }
    break;
  case 8:
    OverworldEntrance_PlayJingle();
    break;
  }
}

void OverworldEntrance_PlayJingle() {  // 9bcf40
  sound_effect_2 = 27;
  trigger_special_entrance = 0;
  subsubmodule_index = 0;
  nmi_disable_core_updates = 0;
  flag_is_link_immobilized = 0;
  flag_unk1 = 0;
  bg1_x_offset = 0;
  bg1_y_offset = 0;
}

void OverworldEntrance_DrawManyTR() {  // 9bcf60
  int j = 0xe78;
  Overworld_DrawMap16_Persist(0x99e, j++);
  Overworld_DrawMap16_Persist(0x9a0, j++);
  Overworld_DrawMap16_Persist(0x9a2, j++);
  Overworld_DrawMap16_Persist(0x9a4, j++);

  Overworld_DrawMap16_Persist(0xa1e, j++);
  Overworld_DrawMap16_Persist(0xa20, j++);
  Overworld_DrawMap16_Persist(0xa22, j++);
  Overworld_DrawMap16_Persist(0xa24, j++);

  Overworld_DrawMap16_Persist(0xa9e, j++);
  Overworld_DrawMap16_Persist(0xaa0, j++);
  Overworld_DrawMap16_Persist(0xaa2, j++);
  Overworld_DrawMap16_Persist(0xaa4, j++);

  Overworld_DrawMap16_Persist(0xb1e, j++);
  Overworld_DrawMap16_Persist(0xb20, j++);
  Overworld_DrawMap16_Persist(0xb22, j++);
  Overworld_DrawMap16_Persist(0xb24, j++);
  nmi_load_bg_from_vram = 1;
  nmi_disable_core_updates = 1;
}

void Overworld_AnimateEntrance_GanonsTower() {  // 9bcfd9
  switch (subsubmodule_index) {
  case 0:
  case 1:
    save_ow_event_info[BYTE(overworld_screen_index)] |= 0x20;
    GanonTowerEntrance_Func1();
    break;
  case 2:
    GanonTowerEntrance_Func1();
    if (!TS_copy) {
      TS_copy = 1;
      if (++ganonentrance_ctr == 3) {
        ganonentrance_ctr = 0;
        sound_effect_ambient = 7;
      } else {
        subsubmodule_index = 0;
      }
    }
    break;
  case 3:
    if (++ganonentrance_ctr != 48)
      return;
    OverworldEntrance_AdvanceAndBoom();
    Overworld_DrawMap16_Persist(0x45e, 0xe88);
    Overworld_DrawMap16_Persist(0x460, 0xe89);
    Overworld_DrawMap16_Persist(0x4de, 0xea2);
    Overworld_DrawMap16_Persist(0x4e0, 0xea3);
    Overworld_DrawMap16_Persist(0x55e, 0xe8a);
    Overworld_DrawMap16_Persist(0x560, 0xe8b);
    nmi_load_bg_from_vram = 1;
    break;
  case 4:
    if (++ganonentrance_ctr != 48)
      return;
    OverworldEntrance_AdvanceAndBoom();
    Overworld_DrawMap16_Persist(0x45e, 0xe8c);
    Overworld_DrawMap16_Persist(0x460, 0xe8d);
    Overworld_DrawMap16_Persist(0x4de, 0xe8e);
    Overworld_DrawMap16_Persist(0x4e0, 0xe8f);
    Overworld_DrawMap16_Persist(0x55e, 0xe90);
    Overworld_DrawMap16_Persist(0x560, 0xe91);
    nmi_load_bg_from_vram = 1;
    break;
  case 5:
    if (++ganonentrance_ctr != 52)
      return;
    OverworldEntrance_AdvanceAndBoom();
    Overworld_DrawMap16_Persist(0x45e, 0xe92);
    Overworld_DrawMap16_Persist(0x460, 0xe93);
    Overworld_DrawMap16_Persist(0x4de, 0xe94);
    Overworld_DrawMap16_Persist(0x4e0, 0xe94);
    Overworld_DrawMap16_Persist(0x55e, 0xe95);
    Overworld_DrawMap16_Persist(0x560, 0xe95);
    nmi_load_bg_from_vram = 1;
    break;
  case 6:
    if (++ganonentrance_ctr != 32)
      return;
    OverworldEntrance_AdvanceAndBoom();
    Overworld_DrawMap16_Persist(0x45e, 0xe96);
    Overworld_DrawMap16_Persist(0x460, 0xe97);
    Overworld_DrawMap16_Persist(0x4de, 0xe98);
    Overworld_DrawMap16_Persist(0x4e0, 0xe99);
    nmi_load_bg_from_vram = 1;
    break;
  case 7:
    if (++ganonentrance_ctr != 32)
      return;
    OverworldEntrance_AdvanceAndBoom();
    Overworld_DrawMap16_Persist(0x4de, 0xe9a);
    Overworld_DrawMap16_Persist(0x4e0, 0xe9b);
    nmi_load_bg_from_vram = 1;
    break;
  case 8:
    if (++ganonentrance_ctr != 32)
      return;
    OverworldEntrance_AdvanceAndBoom();
    Overworld_DrawMap16_Persist(0x4de, 0xe9c);
    Overworld_DrawMap16_Persist(0x4e0, 0xe9d);
    Overworld_DrawMap16_Persist(0x55e, 0xe9e);
    Overworld_DrawMap16_Persist(0x560, 0xe9f);
    nmi_load_bg_from_vram = 1;
    break;
  case 9:
    if (++ganonentrance_ctr != 32)
      return;
    OverworldEntrance_AdvanceAndBoom();
    Overworld_DrawMap16_Persist(0x55e, 0xe9a);
    Overworld_DrawMap16_Persist(0x560, 0xe9b);
    nmi_load_bg_from_vram = 1;
    break;
  case 10:
    if (++ganonentrance_ctr != 32)
      return;
    OverworldEntrance_AdvanceAndBoom();
    Overworld_DrawMap16_Persist(0x55e, 0xe9c);
    Overworld_DrawMap16_Persist(0x560, 0xe9d);
    Overworld_DrawMap16_Persist(0x5de, 0xea0);
    Overworld_DrawMap16_Persist(0x5e0, 0xea1);
    nmi_load_bg_from_vram = 1;
    break;
  case 11:
    if (++ganonentrance_ctr != 32)
      return;
    sound_effect_ambient = 5;
    OverworldEntrance_AdvanceAndBoom();
    Overworld_DrawMap16_Persist(0x5de, 0xe9a);
    Overworld_DrawMap16_Persist(0x5e0, 0xe9b);
    nmi_load_bg_from_vram = 1;
    break;
  case 12:
    if (++ganonentrance_ctr != 72)
      return;
    OverworldEntrance_PlayJingle();
    ganonentrance_ctr = 0;
    music_control = 13;
    sound_effect_ambient = 9;
    break;
  }
}

void OverworldEntrance_AdvanceAndBoom() {  // 9bd00e
  subsubmodule_index++;
  overworld_entrance_sequence_counter = 0;
  sound_effect_1 = 12;
  sound_effect_2 = 7;
}

// ---------------------------------------------------------------------------
// --dump-terrain-table (dev/codegen): enumerate every consumable overworld
// terrain object (bushes, thick grass, small rocks, big rock piles; signs and
// dash-only bonk piles reported-but-excluded) as GROUND TRUTH from the engine
// itself, for assets/scripts/gen_terrain_tables.py (grass/rock drop shuffle,
// openspec change add-rando-grass-rock-shuffle Phase 1). Mirrors the
// --dump-pot-table philosophy: extract from the authoritative engine build
// rather than re-deriving map32->map16 decode + overlays in Python.
//
// Per parent area (screens 0x00-0x7F; specials >= 0x80 can't reveal secrets)
// this replicates ONLY the map16-buffer build of a full screen load:
// Overworld_LoadGFXAndScreenSize + Overworld_DecompressAndDrawAllQuadrants,
// then the overlay set of Overworld_DrawQuadrantsAndOverlays /
// Overworld_HandleOverlaysAndBombDoors -- replicated below with an explicit
// `inverted` flag instead of the live rando world-state gate (KEEP IN SYNC
// with those functions). No gfx/palette/sprite/VRAM work runs (headless-safe:
// every write path here is a direct dung_bg2/g_ram buffer write).
//
// State profiles cover the persistent tile states the registry invariant
// cares about (present-with-identical-class in EVERY state, else excluded):
// vanilla / vanilla+event-overlays (save_ow_event_info bits 0x20|0x02 set for
// ALL screens, so cross-screen reads like the inverted 0x1B hole gate on
// 0x5B see them) / inverted / inverted+events / vanilla pre-rescue
// (sram_progress_indicator < 3 -- the inverted map stream has one
// phase-dependent write).
static void TerrainDump_BuildScreen(uint8 scr, bool inverted, bool events) {
  overworld_screen_index = scr;
  ow_entrance_value = 0;
  Overworld_LoadGFXAndScreenSize();
  Overworld_DecompressAndDrawAllQuadrants();
  // Mirror of Overworld_HandleOverlaysAndBombDoors (see KEEP IN SYNC above):
  // hardcoded LW rocks (skipped in inverted), event overlay (bit 0x20; forced
  // on 0x47 in inverted -- TurtleRockEntranceFix), secondary overlay (bit 2),
  // then the inverted per-screen tile overrides.
  if (!inverted) {
    if (scr == 0x33)
      dung_bg2[340] = 0x20f;
    else if (scr == 0x2f)
      dung_bg2[1497] = 0x20f;
  }
  // Overworld_LoadEventOverlay's switch asserts on the screens vanilla never
  // sets bit 0x20 for (its final case group) — skip those here since the
  // profile sets the bit blanket-wide. KEEP IN SYNC with that switch.
  bool has_event_overlay =
      !(scr == 0x72 || scr == 0x73 || scr == 0x74 || scr == 0x75 ||
        scr == 0x76 || scr == 0x7a || scr == 0x7c || scr == 0x7d ||
        scr == 0x7e || scr == 0x7f);
  if ((events || (inverted && scr == 0x47)) && has_event_overlay)
    Overworld_LoadEventOverlay();
  if (events) {
    uint16 so = kSecondaryOverlayPerOw[scr];
    if (so) {
      int pos = so >> 1;
      dung_bg2[pos + 0] = 0xdb4;
      dung_bg2[pos + 1] = 0xdb5;
    }
  }
  if (inverted)
    Overworld_ApplyInvertedTiles();
}

int Overworld_DumpTerrainTable(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "--dump-terrain-table: cannot open %s\n", path);
    return 1;
  }
  // Emitted lines:
  //   A scr size            -- parent area header (small|big)
  //   S scr pos data        -- every kOverworldSecrets entry (static per screen)
  //   T prof scr pos m16 kind a0 a1 a2 a3 content
  //                         -- one consumable object under state profile prof
  //                            (kind: B/b bush LW/DW-id, G thick grass, R/r
  //                            small rock ids 0x20f/0x239, P/Q lift-pile
  //                            origins, D dash-pile origin (bonk; excluded),
  //                            S sign (excluded); content = secret byte or -1)
  //   U prof scr pos m16 a0 a1 a2 a3
  //                         -- map16 with a liftable attr (0x50..0x57) NOT in
  //                            the engine's reveal-helper id sets (coverage
  //                            assert input; tasks.md 1.3)
  fprintf(f, "# zelda3 terrain dump v1 (add-rando-grass-rock-shuffle Phase 1)\n"
             "# profiles: 0=vanilla 1=vanilla+events 2=inverted 3=inverted+events 4=vanilla-prerescue\n");
  static const struct { uint8 inv, ev, prog; } kProf[5] = {
    {0, 0, 3}, {0, 1, 3}, {1, 0, 3}, {1, 1, 3}, {0, 0, 1},
  };
  // stderr summary: per-kind counts, profile 0, split LW/DW.
  int hist[9][2];
  memset(hist, 0, sizeof hist);
  static const char kKinds[9] = { 'B', 'b', 'G', 'R', 'r', 'P', 'Q', 'D', 'S' };
  int areas = 0, unknown = 0;
  for (int scr = 0; scr < 0x80; scr++) {
    overworld_screen_index = (uint16)scr;
    Overworld_LoadGFXAndScreenSize();
    int w6 = scr & 0x3f;
    if (overworld_offset_base_y != ((w6 >> 3) * 512) ||
        overworld_offset_base_x != ((w6 & 7) * 64))
      continue;  // child screen of a big area -- the parent covers it
    bool small = kOverworldMapIsSmall[w6] != 0;
    areas++;
    fprintf(f, "A %02x %s\n", scr, small ? "small" : "big");
    const uint8 *sp = kOverworldSecrets + kOverworldSecrets_Offs[scr];
    for (;;) {
      uint16 x = *(const uint16 *)sp;
      if (x == 0xffff)
        break;
      fprintf(f, "S %02x %04x %d\n", scr, x & 0x7fff, sp[2]);
      sp += 3;
    }
    for (int pi = 0; pi < 5; pi++) {
      memset(save_ow_event_info, kProf[pi].ev ? 0x22 : 0, 0x80);
      sram_progress_indicator = kProf[pi].prog;
      TerrainDump_BuildScreen((uint8)scr, kProf[pi].inv != 0, kProf[pi].ev != 0);
      int w = small ? 32 : 64;
      for (int row = 0; row < w; row++) {
        for (int col = 0; col < w; col++) {
          uint16 m16 = dung_bg2[row * 64 + col];
          uint16 pos = (uint16)(row * 128 + col * 2);
          const uint16 *m8 = GetMap16toMap8Table() + m16 * 4;
          uint8 a0 = kMap8DataToTileAttr[m8[0] & 0x1ff];
          uint8 a1 = kMap8DataToTileAttr[m8[1] & 0x1ff];
          uint8 a2 = kMap8DataToTileAttr[m8[2] & 0x1ff];
          uint8 a3 = kMap8DataToTileAttr[m8[3] & 0x1ff];
          int kind_idx = -1;
          bool skip_quadrant = false;
          switch (m16) {
          case 0x036: kind_idx = 0; break;  // bush (LW-id; cut/lift/bomb/dash)
          case 0x72a: kind_idx = 1; break;  // bush (DW-id)
          case 0x37e: kind_idx = 2; break;  // thick grass (cut/bomb only)
          case 0x20f: kind_idx = 3; break;  // small rock (Overworld_LiftingSmallObj)
          case 0x239: kind_idx = 4; break;  // small rock (2nd id)
          case 0x36d: kind_idx = 5; break;  // lift-pile A origin (top-left quad)
          case 0x23b: kind_idx = 6; break;  // lift-pile B origin
          case 0x226: kind_idx = 7; break;  // dash-pile origin (bonk class; excluded)
          case 0x101: kind_idx = 8; break;  // sign (excluded from the feature)
          // non-origin pile quadrants -- recognized so they don't hit the U report
          case 0x36e: case 0x374: case 0x375:
          case 0x23c: case 0x23d: case 0x23e:
          case 0x227: case 0x228: case 0x229:
            skip_quadrant = true;
            break;
          default: break;
          }
          if (skip_quadrant)
            continue;
          if (kind_idx < 0) {
            if ((a0 >= 0x50 && a0 < 0x58) || (a1 >= 0x50 && a1 < 0x58) ||
                (a2 >= 0x50 && a2 < 0x58) || (a3 >= 0x50 && a3 < 0x58)) {
              fprintf(f, "U %d %02x %04x %03x %02x %02x %02x %02x\n",
                      pi, scr, pos, m16, a0, a1, a2, a3);
              unknown++;
            }
            continue;
          }
          int content = -1;
          const uint8 *p2 = kOverworldSecrets + kOverworldSecrets_Offs[scr];
          for (;;) {
            uint16 x = *(const uint16 *)p2;
            if (x == 0xffff)
              break;
            if ((x & 0x7fff) == pos) {
              content = p2[2];
              break;
            }
            p2 += 3;
          }
          fprintf(f, "T %d %02x %04x %03x %c %02x %02x %02x %02x %d\n",
                  pi, scr, pos, m16, kKinds[kind_idx], a0, a1, a2, a3, content);
          if (pi == 0)
            hist[kind_idx][scr >= 0x40]++;
        }
      }
    }
  }
  fclose(f);
  fprintf(stderr, "--dump-terrain-table: %d parent areas -> %s\n", areas, path);
  fprintf(stderr, "  profile-0 counts (kind: LW+DW=total):\n");
  int total = 0;
  for (int k = 0; k < 9; k++) {
    fprintf(stderr, "    %c: %d+%d=%d\n", kKinds[k], hist[k][0], hist[k][1],
            hist[k][0] + hist[k][1]);
    if (k <= 6)  // B b G R r P Q = in-scope candidates (D/S excluded)
      total += hist[k][0] + hist[k][1];
  }
  fprintf(stderr, "  in-scope candidate total (profile 0, B+b+G+R+r+P+Q): %d\n", total);
  if (unknown)
    fprintf(stderr, "  WARNING: %d unknown-liftable (U) lines -- extend the id sets\n", unknown);
  return 0;
}
