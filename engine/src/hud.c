#include "hud.h"
#include "zelda_rtl.h"

#include <string.h>

#include "variables.h"
#include "messaging.h"
#include "localized.h"
#include "language_cart.h"
#include "mods.h"
#include "sprite.h"
#include "features.h"
#include "rando/rando.h"
#include "rando/alttpr_import.h"
#include "rando/rando_logic.h"
#include "rando/rando_placement.h"
#include "load_gfx.h"  // DecompAndUpload2bpp (rando icon-atlas decode)
#include "assets.h"    // kHudPalData (rando icon-atlas palette)

#define kNewStyleInventory ((enhanced_features1 & kFeatures1_QolInventory) != 0)
#define kHudItemCount (kNewStyleInventory ? 24 : 20)

typedef struct ItemBoxGfx {
  uint16 v[4];
} ItemBoxGfx;

// Text and box tiles of the menus: `count` entries of the engine's table from
// `index`, or the same entries of the table at `loc` in the French Canadian
// cartridge, or at `us_addr` (where the USA cartridge keeps it) in a
// translation built from the USA one, each with its own lettering.
// Keep the cartridge and host table addresses across a call boundary. GCC
// LTO otherwise folds table - cartridge address into a negative ABS32 literal,
// which cannot be represented by the 3DSX relocation format.
#if defined(__GNUC__) && defined(__3DS__)
__attribute__((noinline))
#endif
static const uint16 *Hud_Text(uint16 *buf, const uint16 *table, uint32 loc,
                              uint32 us_addr, int index, int count) {
  if (g_loc_rom)
    return Loc_Words(buf, loc + index * 2, count);
  if (LanguageCart_Active())  // a translation in the USA layout (language_cart.h)
    return LanguageCart_Words(buf, us_addr + index * 2, count);
  return table + index;
}

static void Hud_DrawItem(uint16 *dst, const ItemBoxGfx *src);
static bool Hud_DoWeHaveThisItem(uint8 item);
static void Hud_EquipPrevItem(uint8 *item);
static void Hud_EquipNextItem(uint8 *item);
static int Hud_GetItemPosition(int item);
static void Hud_ReorderItem(int direction);
static void Hud_Update_Magic();
static void Hud_Update_Inventory();
static void Hud_UpdateItemBox(void);
static void Hud_Update_Hearts();

// The capacity tables of the cartridge at $0D:DB40, 8 levels each: bombs in
// BCD, then in binary, then arrows the same. Each place of the game reads one
// at the address in its own code, so a mod that moves or widens them (Max
// Consumables Fix) gives those bytes (Mods_RomByte) and the C code follows.
static const uint8 kCapacityTables[32] = {
  0x10, 0x15, 0x20, 0x25, 0x30, 0x35, 0x40, 0x50, 10, 15, 20, 25, 30, 35, 40, 50,
  0x30, 0x35, 0x40, 0x45, 0x50, 0x55, 0x60, 0x70, 30, 35, 40, 45, 50, 55, 60, 70,
};

uint8 Hud_Capacity(uint32 operand, uint16 table, int level) {
  if ((table==0xdb40 || table==0xdb48) && (enhanced_features1 & kFeatures1_QolBombs99))
    return table==0xdb40 ? 0x99 : 99;
  if ((table==0xdb50 || table==0xdb58) && (enhanced_features1 & kFeatures1_QolArrows99))
    return table==0xdb50 ? 0x99 : 99;
  uint32 a = 0x8d0000 | (uint16)(Mods_RomWord(operand, table) + level);
  return Mods_RomByte(a, a - 0x8ddb40 < 32 ? kCapacityTables[a - 0x8ddb40] : 0);
}

uint8 Hud_MaxBombs(void) {
  return Hud_Capacity(0x8ddc31, 0xdb48, link_bomb_upgrades);
}

uint8 Hud_MaxArrows(void) {
  return Hud_Capacity(0x8ddc53, 0xdb58, link_arrow_upgrades);
}
bool Hud_QolHasShovel(void) {
  if (g_rando_slot_active)
    return (g_rando_flute_shovel_owned & kRandoFluteShovel_Shovel)!=0;
  return link_item_flute==1 || ((enhanced_features1 & kFeatures1_QolShovel) && link_item_flute>=2);
}

void Hud_QolTranslateInventory(bool expanded) {
  uint8 *items[]={&hud_cur_item,&hud_cur_item_x,&hud_cur_item_l,&hud_cur_item_r};
  for(unsigned i=0;i<4;i++) {
    uint8 old=*items[i];
    if(expanded && old==16) *items[i]=link_item_bottle_index>=1&&link_item_bottle_index<=4?20+link_item_bottle_index:0;
    else if(expanded && old==13 && (link_item_flute==1 || (i==0 && current_item_y==21 && (enhanced_features1&kFeatures1_QolShovel))))*items[i]=16;
    else if(!expanded && old>=21&&old<=24){if(i==0)link_item_bottle_index=old-20;*items[i]=16;}
    else if(!expanded && old==16)*items[i]=link_item_flute?13:0;
  }
  memset(hud_inventory_order,0,24);
}

void Hud_QolNormalizeInventory(void) {
  uint8 bombs=Hud_MaxBombs(),arrows=Hud_MaxArrows();
  if(link_item_bombs>bombs){link_item_bombs=bombs;link_bomb_filler=0;}
  if(link_num_arrows>arrows){link_num_arrows=arrows;link_arrow_filler=0;}
  unsigned rupees=(enhanced_features0 & kFeatures0_CarryMoreRupees)?9999:999;
  if(link_rupees_actual>rupees)link_rupees_actual=rupees;
  if(link_rupees_goal>rupees)link_rupees_goal=rupees;
  // A state made with the expanded grid may retain bottle ids 21..24.
  if(!kNewStyleInventory && hud_cur_item>=21){link_item_bottle_index=hud_cur_item-20;hud_cur_item=16;}
  uint32 seen=0;bool invalid=false;
  if(hud_inventory_order[0])for(int i=0;i<kHudItemCount;i++){
    unsigned id=hud_inventory_order[i];
    if(!id||id>(unsigned)kHudItemCount||(seen&(1u<<id))){invalid=true;break;}
    seen|=1u<<id;
  }
  if(invalid)memset(hud_inventory_order,0,24);
}

static const uint8 kMaxHealthForLevel[] = { 9, 9, 9, 9, 9, 9, 9, 9, 17, 17, 17, 17, 17, 17, 17, 25, 25, 25, 25, 25, 25 };

#define HUDXY(x, y) ((x) + (y) * 32)

static const uint16 kHudItemInVramPtr_New[24] = {
  HUDXY(2,  7), HUDXY(5,  7), HUDXY(8,  7), HUDXY(11,  7), HUDXY(14,  7), HUDXY(17, 7),
  HUDXY(2, 10), HUDXY(5, 10), HUDXY(8, 10), HUDXY(11, 10), HUDXY(14, 10), HUDXY(17, 10),
  HUDXY(2, 13), HUDXY(5, 13), HUDXY(8, 13), HUDXY(11, 13), HUDXY(14, 13), HUDXY(17, 13),
  HUDXY(2, 16), HUDXY(5, 16), HUDXY(8, 16), HUDXY(11, 16), HUDXY(14, 16), HUDXY(17, 16),
};

static const uint16 kHudItemInVramPtr_Old[20] = {
  HUDXY(4,  7), HUDXY(7,  7), HUDXY(10,  7), HUDXY(13,  7), HUDXY(16,  7),
  HUDXY(4, 10), HUDXY(7, 10), HUDXY(10, 10), HUDXY(13, 10), HUDXY(16, 10),
  HUDXY(4, 13), HUDXY(7, 13), HUDXY(10, 13), HUDXY(13, 13), HUDXY(16, 13),
  HUDXY(4, 16), HUDXY(7, 16), HUDXY(10, 16), HUDXY(13, 16), HUDXY(16, 16),
};


#define kHudItemInVramPtr (kNewStyleInventory ? kHudItemInVramPtr_New : kHudItemInVramPtr_Old)

static const ItemBoxGfx kHudItemBottles[9] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x2044, 0x2045, 0x2046, 0x2047}},
  {{0x2837, 0x2838, 0x2cc3, 0x2cd3}},
  {{0x24d2, 0x64d2, 0x24e2, 0x24e3}},
  {{0x3cd2, 0x7cd2, 0x3ce2, 0x3ce3}},
  {{0x2cd2, 0x6cd2, 0x2ce2, 0x2ce3}},
  {{0x2855, 0x6855, 0x2c57, 0x2c5a}},
  {{0x2837, 0x2838, 0x2839, 0x283a}},
  {{0x2837, 0x2838, 0x2839, 0x283a}},
};
static const ItemBoxGfx kHudItemBow[5] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x28ba, 0x28e9, 0x28e8, 0x28cb}},
  {{0x28ba, 0x284a, 0x2849, 0x28cb}},
  {{0x28ba, 0x28e9, 0x28e8, 0x28cb}},
  {{0x28ba, 0x28bb, 0x24ca, 0x28cb}},
};
// Rando visual fix: keep the regular arrow shaft alignment while using the
// silver-arrow palette for the bottom-left quadrant.
static const ItemBoxGfx kHudItemBowRandoSilverArrows =
  {{0x28ba, 0x28bb, 0x2449, 0x28cb}};
static const ItemBoxGfx kHudItemBoomerang[3] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x2cb8, 0x2cb9, 0x2cf5, 0x2cc9}},
  {{0x24b8, 0x24b9, 0x24f5, 0x24c9}},
};
static const ItemBoxGfx kHudItemHookshot[2] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x24f5, 0x24f6, 0x24c0, 0x24f5}},
};
static const ItemBoxGfx kHudItemBombs[2] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x2cb2, 0x2cb3, 0x2cc2, 0x6cc2}},
};
static const ItemBoxGfx kHudItemMushroom[3] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x2444, 0x2445, 0x2446, 0x2447}},
  {{0x203b, 0x203c, 0x203d, 0x203e}},
};
static const ItemBoxGfx kHudItemFireRod[2] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x24b0, 0x24b1, 0x24c0, 0x24c1}},
};
static const ItemBoxGfx kHudItemIceRod[2] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x2cb0, 0x2cbe, 0x2cc0, 0x2cc1}},
};
static const ItemBoxGfx kHudItemBombos[2] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x287d, 0x287e, 0xe87e, 0xe87d}},
};
static const ItemBoxGfx kHudItemEther[2] = {
  {{0x20f5, 0x20f5,  0x20f5,  0x20f5}},
  {{0x2876, 0x2877, 0xE877, 0xE876}},
};
static const ItemBoxGfx kHudItemQuake[2] = {
  {{0x20f5, 0x20f5,  0x20f5,  0x20f5}},
  {{0x2866, 0x2867, 0xE867, 0xE866}},
};
static const ItemBoxGfx kHudItemTorch[2] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x24bc, 0x24bd, 0x24cc, 0x24cd}},
};
static const ItemBoxGfx kHudItemHammer[2] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x20b6, 0x20b7, 0x20c6, 0x20c7}},
};
static const ItemBoxGfx kHudItemFlute[4] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x20d0, 0x20d1, 0x20e0, 0x20e1}},
  {{0x2cd4, 0x2cd5, 0x2ce4, 0x2ce5}},
  {{0x2cd4, 0x2cd5, 0x2ce4, 0x2ce5}},
};
static const ItemBoxGfx kHudItemBugNet[2] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x3c40, 0x3c41, 0x2842, 0x3c43}},
};
static const ItemBoxGfx kHudItemBookMudora[2] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x3ca5, 0x3ca6, 0x3cd8, 0x3cd9}},
};
static const ItemBoxGfx kHudItemCaneSomaria[2] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x24dc, 0x24dd, 0x24ec, 0x24ed}},
};
static const ItemBoxGfx kHudItemCaneByrna[2] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x2cdc, 0x2cdd, 0x2cec, 0x2ced}},
};
static const ItemBoxGfx kHudItemCape[2] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x24b4, 0x24b5, 0x24c4, 0x24c5}},
};
static const ItemBoxGfx kHudItemMirror[4] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x28de, 0x28df, 0x28ee, 0x28ef}},
  {{0x2c62, 0x2c63, 0x2c72, 0x2c73}},
  {{0x2886, 0x2887, 0x2888, 0x2889}},
};
static const ItemBoxGfx kHudItemGloves[3] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x2130, 0x2131, 0x2140, 0x2141}},
  {{0x28da, 0x28db, 0x28ea, 0x28eb}},
};
static const ItemBoxGfx kHudItemBoots[2] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x3429, 0x342a, 0x342b, 0x342c}},
};
static const ItemBoxGfx kHudItemFlippers[2] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x2c9a, 0x2c9b, 0x2c9d, 0x2c9e}},
};
static const ItemBoxGfx kHudItemMoonPearl[2] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x2433, 0x2434, 0x2435, 0x2436}},
};
static const ItemBoxGfx kHudItemEmpty[1] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
};
static const ItemBoxGfx kHudItemSword[5] = {
  {{0x20f5, 0x20f5, 0x20f5, 0x20f5}},
  {{0x2c64, 0x2cce, 0x2c75, 0x3d25}},
  {{0x2c8a, 0x2c65, 0x2474, 0x3d26}},
  {{0x248a, 0x2465, 0x3c74, 0x2d48}},
  {{0x288a, 0x2865, 0x2c74, 0x2d39}},
};
static const ItemBoxGfx kHudItemShield[4] = {
  {{0x24f5, 0x24f5, 0x24f5, 0x24f5}},
  {{0x2cfd, 0x6cfd, 0x2cfe, 0x6cfe}},
  {{0x34ff, 0x74ff, 0x349f, 0x749f}},
  {{0x2880, 0x2881, 0x288d, 0x288e}},
};
static const ItemBoxGfx kHudItemArmor[5] = {
  {{0x3c68, 0x7c68, 0x3c78, 0x7c78}},
  {{0x2c68, 0x6c68, 0x2c78, 0x6c78}},
  {{0x2468, 0x6468, 0x2478, 0x6478}},
};


static const ItemBoxGfx * const kHudItemBoxGfxPtrs[] = {
  kHudItemBow,
  kHudItemBoomerang,
  kHudItemHookshot,
  kHudItemBombs,
  kHudItemMushroom,
  kHudItemFireRod,
  kHudItemIceRod,
  kHudItemBombos,
  kHudItemEther,
  kHudItemQuake,
  kHudItemTorch,
  kHudItemHammer,
  kHudItemFlute,
  kHudItemBugNet,
  kHudItemBookMudora,
  kHudItemBottles,
  kHudItemCaneSomaria,
  kHudItemCaneByrna,
  kHudItemCape,
  kHudItemMirror,
  kHudItemGloves,
  kHudItemBoots,
  kHudItemFlippers,
  kHudItemMoonPearl,
  kHudItemEmpty,
  kHudItemSword,
  kHudItemShield,
  kHudItemArmor,
  kHudItemBottles,
  kHudItemBottles,
  kHudItemBottles,
  kHudItemBottles,

};
static const uint16 kUpdateMagicPowerTilemap[17][4] = {
  {0x3cf5, 0x3cf5, 0x3cf5, 0x3cf5},
  {0x3cf5, 0x3cf5, 0x3cf5, 0x3c5f},
  {0x3cf5, 0x3cf5, 0x3cf5, 0x3c4c},
  {0x3cf5, 0x3cf5, 0x3cf5, 0x3c4d},
  {0x3cf5, 0x3cf5, 0x3cf5, 0x3c4e},
  {0x3cf5, 0x3cf5, 0x3c5f, 0x3c5e},
  {0x3cf5, 0x3cf5, 0x3c4c, 0x3c5e},
  {0x3cf5, 0x3cf5, 0x3c4d, 0x3c5e},
  {0x3cf5, 0x3cf5, 0x3c4e, 0x3c5e},
  {0x3cf5, 0x3c5f, 0x3c5e, 0x3c5e},
  {0x3cf5, 0x3c4c, 0x3c5e, 0x3c5e},
  {0x3cf5, 0x3c4d, 0x3c5e, 0x3c5e},
  {0x3cf5, 0x3c4e, 0x3c5e, 0x3c5e},
  {0x3c5f, 0x3c5e, 0x3c5e, 0x3c5e},
  {0x3c4c, 0x3c5e, 0x3c5e, 0x3c5e},
  {0x3c4d, 0x3c5e, 0x3c5e, 0x3c5e},
  {0x3c4e, 0x3c5e, 0x3c5e, 0x3c5e},
};
static const uint16 kDungFloorIndicator_Gfx0[11] = { 0x2508, 0x2509, 0x2509, 0x250a, 0x250b, 0x250c, 0x250d, 0x251d, 0xe51c, 0x250e, 0x7f };
static const uint16 kDungFloorIndicator_Gfx1[11] = { 0x2518, 0x2519, 0xa509, 0x251a, 0x251b, 0x251c, 0x2518, 0xa51d, 0xe50c, 0xa50e, 0x7f };


void Hud_RefreshIcon() {
  Hud_SearchForEquippedItem();
  Hud_UpdateHud();
  Hud_Rebuild();
  overworld_map_state = 0;
}

uint8 CheckPalaceItemPosession() {
  switch (cur_palace_index_x2 >> 1) {
  case 2: return link_item_bow != 0;
  case 3: return link_item_gloves != 0;
  case 5: return link_item_hookshot != 0;
  case 6: return link_item_hammer != 0;
  case 7: return link_item_cane_somaria != 0;
  case 8: return link_item_fire_rod != 0;
  case 9: return link_armor != 0;
  case 10: return link_item_moon_pearl != 0;
  case 11: return link_item_gloves != 1;
  case 12: return link_shield_type == 3;
  case 13: return link_armor == 2;
  default:
    return 0;
  }
}

// Returns the zero based index of the currently selected hud item,
// or -1 if the item is item 0.
static int Hud_GetItemPosition(int item) {
  if (item <= 0)
    return -1;
  if (hud_inventory_order[0] != 0) {
    int i = 0;
    for (; i < kHudItemCount - 1 && hud_inventory_order[i] != item; i++) {}
    return i;
  } else {
    return item ? item - 1 : item;
  }
}

static void Hud_GotoPrevItem(uint8 *item, uint8 first_item_index) {
  if (hud_inventory_order[0] != 0) {
    int pos = Hud_GetItemPosition(*item);
    *item = (pos == 0 && !first_item_index) ? 0 :
        hud_inventory_order[(pos <= 0 ? kHudItemCount : pos) - 1];
  } else {
    *item = (*item > first_item_index) ? *item - 1 : kHudItemCount;
  }
}

static void Hud_GotoNextItem(uint8 *item, uint8 first_item_index) {
  if (hud_inventory_order[0] != 0) {
    int i = Hud_GetItemPosition(*item);
    *item = hud_inventory_order[((uint)i >= kHudItemCount - 1) ? 0 : i + 1];
  } else {
    *item = (*item < kHudItemCount) ? *item + 1 : first_item_index;
  }
}

// The French Canadian cartridge names floors the French way ($8E:FEAC): RC for
// the ground floor, then 1, 2... with a raised e, and the basements with SS
// before the number. The digits are the USA ones, one floor further down.
static void Hud_FloorIndicator_Loc() {
  hud_tile_indices_buffer[0xf2 / 2] = 0x24aa;
  hud_tile_indices_buffer[0x134 / 2] = 0x207f;
  hud_tile_indices_buffer[0x132 / 2] = 0xa4aa;
  hud_tile_indices_buffer[0xf4 / 2] = 0x2520;

  int k = 0, j;
  if (!sign8(dung_cur_floor)) {
    if (!WORD(dung_cur_floor) && dungeon_room_index != 2 && sram_progress_indicator < 2)
      sound_effect_ambient = 3;
    if (dung_cur_floor == 0) {
      hud_tile_indices_buffer[0xf2 / 2] = 0x250f;
      hud_tile_indices_buffer[0xf4 / 2] = 0x251e;
      hud_tile_indices_buffer[0x132 / 2] = 0x251f;
      hud_tile_indices_buffer[0x134 / 2] = 0xa51e;
      flag_update_hud_in_nmi++;
      return;
    }
    j = dung_cur_floor - 1;
  } else {
    sound_effect_ambient = 5;
    k++;
    j = dung_cur_floor ^ 0xff;
  }
  hud_tile_indices_buffer[k + 0xf2 / 2] = kDungFloorIndicator_Gfx0[j];
  hud_tile_indices_buffer[k + 0x132 / 2] = kDungFloorIndicator_Gfx1[j];
  flag_update_hud_in_nmi++;
}

void Hud_FloorIndicator() {  // 8afd0c
  uint16 a = hud_floor_changed_timer;
  if (a == 0) {
    Hud_RemoveSuperBombIndicator();
    return;
  }
  a += 1;
  if (a == 0xc0)
    a = 0;
  WORD(hud_floor_changed_timer) = a;

  if (g_loc_rom) {
    Hud_FloorIndicator_Loc();
    return;
  }

  hud_tile_indices_buffer[0xf2 / 2] = 0x251e;
  hud_tile_indices_buffer[0x134 / 2] = 0x251f;
  hud_tile_indices_buffer[0x132 / 2] = 0x2520;
  hud_tile_indices_buffer[0xf4 / 2] = 0x250f;

  int k = 0, j;

  if (!sign8(dung_cur_floor)) {
    if (!WORD(dung_cur_floor) && dungeon_room_index != 2 && sram_progress_indicator < 2)
      sound_effect_ambient = 3;
    j = dung_cur_floor;
  } else {
    sound_effect_ambient = 5;
    k++;
    j = dung_cur_floor ^ 0xff;
  }
  hud_tile_indices_buffer[k + 0xf2 / 2] = kDungFloorIndicator_Gfx0[j];
  hud_tile_indices_buffer[k + 0x132 / 2] = kDungFloorIndicator_Gfx1[j];
  flag_update_hud_in_nmi++;
}

void Hud_RemoveSuperBombIndicator() {  // 8afd90
  hud_tile_indices_buffer[0xf2 / 2] = 0x7f;
  hud_tile_indices_buffer[0x132 / 2] = 0x7f;
  hud_tile_indices_buffer[0xf4 / 2] = 0x7f;
  hud_tile_indices_buffer[0x134 / 2] = 0x7f;
}

void Hud_SuperBombIndicator() {  // 8afda8
  if (!super_bomb_indicator_unk1) {
    if (sign8(super_bomb_indicator_unk2))
      goto remove;
    super_bomb_indicator_unk2--;
    super_bomb_indicator_unk1 = 62;
  }
  super_bomb_indicator_unk1--;
  if (sign8(super_bomb_indicator_unk2)) {
remove:
    super_bomb_indicator_unk2 = 0xff;
    Hud_RemoveSuperBombIndicator();
    return;
  }

  int r = super_bomb_indicator_unk2 % 10;
  int q = super_bomb_indicator_unk2 / 10;

  int j = sign8(r - 1) ? 9 : r - 1;
  hud_tile_indices_buffer[0xf4 / 2] = kDungFloorIndicator_Gfx0[j];
  hud_tile_indices_buffer[0x134 / 2] = kDungFloorIndicator_Gfx1[j];

  j = sign8(q - 1) ? 10 : q - 1;
  hud_tile_indices_buffer[0xf2 / 2] = kDungFloorIndicator_Gfx0[j];
  hud_tile_indices_buffer[0x132 / 2] = kDungFloorIndicator_Gfx1[j];
}

static int MaxRupees() {
  return enhanced_features0 & kFeatures0_CarryMoreRupees ? 9999 : 999;
}

void Hud_RefillLogic() {  // 8ddb92
  if (overworld_map_state)
    return;
  if (link_magic_filler) {
    if (link_magic_power >= 128) {
      link_magic_power = 128;
      link_magic_filler = 0;
    } else {
      link_magic_filler--;
      link_magic_power++;
      if ((frame_counter & 3) == 0 && sound_effect_1 == 0)
        sound_effect_1 = 45;
    }
  }

  uint16 a = link_rupees_actual;
  if (a != link_rupees_goal) {
    if (a >= link_rupees_goal) {
      if ((int16)--a < 0)
        link_rupees_goal = a = 0;
    } else {
      int m = MaxRupees();
      if (++a > m)
        link_rupees_goal = a = m;
    }
    link_rupees_actual = a;
    if (sound_effect_1 == 0) {
      if ((rupee_sfx_sound_delay++ & 7) == 0)
        sound_effect_1 = 41;
    } else {
      rupee_sfx_sound_delay = 0;
    }
  } else {
    rupee_sfx_sound_delay = 0;
  }

  if (link_bomb_filler) {
    link_bomb_filler--;
    if (link_item_bombs != Hud_MaxBombs())
      link_item_bombs++;
  }

  if (link_arrow_filler) {
    link_arrow_filler--;
    if (link_num_arrows != Hud_MaxArrows())
      link_num_arrows++;
    if (link_item_bow && (link_item_bow & 1) == 1) {
      // rando-exempt: state-shuffle — silver-arrows-bit promotion of
      // link_item_bow when arrow counter ticks. Cosmetic tier swap; the bow
      // item itself was granted via the dispatcher. (audit.md §0.2.2)
      link_item_bow++;
      Hud_RefreshIcon();
    }
  }

  if (!flag_is_link_immobilized && !link_hearts_filler &&
      link_health_current < kMaxHealthForLevel[link_health_capacity >> 3]) {
    if (link_lowlife_countdown_timer_beep) {
      link_lowlife_countdown_timer_beep--;
    } else if (!sound_effect_1) {
      if (!(enhanced_features0 & kFeatures0_DisableLowHealthBeep))
        sound_effect_1 = 43;
      link_lowlife_countdown_timer_beep = 32 - 1;
    }
  }

  if (is_doing_heart_animation)
    goto doing_animation;
  if (link_hearts_filler) {
    if (link_health_current < link_health_capacity) {
      link_health_current += 8;
      if (link_health_current >= link_health_capacity)
        link_health_current = link_health_capacity;

      if (sound_effect_2 == 0)
        sound_effect_2 = 13;

      link_hearts_filler -= 8;
      is_doing_heart_animation++;
      animate_heart_refill_countdown = 7;

doing_animation:
      Hud_Update_Magic();
      Hud_Update_Inventory();
      Hud_AnimateHeartRefill();
      flag_update_hud_in_nmi++;
      return;
    }
    link_health_current = link_health_capacity;
    link_hearts_filler = 0;
  }
  Hud_Update_Hearts();
  Hud_Update_Magic();
  Hud_Update_Inventory();
  flag_update_hud_in_nmi++;
}

void Hud_Module_Run() {  // 8ddd36
  byte_7E0206++;
  switch (overworld_map_state) {
  case 0: Hud_ClearTileMap(); break;
  case 1: Hud_Init(); break;
  case 2: Hud_BringMenuDown(); break;
  case 3: Hud_ChooseNextMode(); break;
  case 4: Hud_NormalMenu(); break;
  case 5: Hud_UpdateHud(); break;
  case 6: Hud_CloseMenu(); break;
  case 7: Hud_GotoBottleMenu(); break;
  case 8: Hud_InitBottleMenu(); break;
  case 9: Hud_ExpandBottleMenu(); break;
  case 10: Hud_BottleMenu(); break;
  case 11: Hud_EraseBottleMenu(); break;
  case 12: Hud_RestoreNormalMenu(); break;
  default:
    assert(0);
  }
}

void Hud_ClearTileMap() {  // 8ddd5a
  uint16 *target = (uint16 *)&g_ram[0x1000];
  for (int i = 0; i < 1024; i++)
    target[i] = 0x207f;
  sound_effect_2 = 17;
  nmi_subroutine_index = 1;
  BYTE(nmi_load_target_addr) = 0x22;
  overworld_map_state++;
}

bool Hud_HaveAnyItems() { // new
  for (int i = 0; i < 20; i++)
    if ((&link_item_bow)[i])
      return true;
  return false;
}

void Hud_Init() {  // 8dddab
  LanguageCart_UploadMenuChars(&g_zenv.vram[0x7000]);
  Hud_SearchForEquippedItem();
  Hud_DrawYButtonItems();
  Hud_DrawAbilityBox();
  Hud_DrawProgressIcons();
  Hud_DrawEquipmentBox();
  Hud_DrawSelectedYButtonItem();

  if (Hud_HaveAnyItems()) {   
    // This causes bottle flicker because it's not early enough
    int first_bottle = 0;
    while (first_bottle < 4 && link_bottle_info[first_bottle] == 0)
      first_bottle++;
    if (first_bottle == 4)
      link_item_bottle_index = 0;
    else if (link_item_bottle_index == 0)
      link_item_bottle_index = first_bottle + 1;

    if (hud_cur_item == kHudItem_BottleOld && !kNewStyleInventory) {
      timer_for_flashing_circle = 16;
      Hud_DrawBottleMenu();
    }
  }

  timer_for_flashing_circle = 16;
  nmi_subroutine_index = 1;
  BYTE(nmi_load_target_addr) = 0x22;
  overworld_map_state++;
}

void Hud_BringMenuDown() {  // 8dde59
  // The French Canadian cartridge scrolls 10 lines a frame and stops two
  // lines higher ($8D:DE5E).
  BG3VOFS_copy2 -= g_loc_rom ? 10 : 8;
  if (BG3VOFS_copy2 == (g_loc_rom ? 0xff1a : 0xff18))
    overworld_map_state++;
}

void Hud_ChooseNextMode() {  // 8dde6e
  if (Hud_HaveAnyItems()) {
    nmi_subroutine_index = 1;
    BYTE(nmi_load_target_addr) = 0x22;

    Hud_DrawSelectedYButtonItem();

    // Pick either the bottle state or normal one
    overworld_map_state = (hud_cur_item == kHudItem_BottleOld && !kNewStyleInventory) ? 10 : 4;
  } else {
    if (filtered_joypad_H)
      overworld_map_state = 5;
  }
}

static bool Hud_DoWeHaveThisItem(uint8 item) {  // 8ddeb0
  if (item == 0)
    return true;  // for the x item, 0 is valid

  if (item == kHudItem_Flute && kNewStyleInventory)
    return (enhanced_features1 & kFeatures1_RandomizerActive) ?
      (g_rando_flute_shovel_owned & kRandoFluteShovel_Flute) != 0 : link_item_flute >= 2;

  if (item == kHudItem_Shovel && kNewStyleInventory)
    return Hud_QolHasShovel();

  if (item >= kHudItem_Bottle1)
    return link_bottle_info[item - kHudItem_Bottle1] != 0;

  return (&link_item_bow)[item - 1] != 0;
}

static void Hud_EquipPrevItem(uint8 *item) {  // 8dded9
  do {
    Hud_GotoPrevItem(item, item == &hud_cur_item);
  } while (!Hud_DoWeHaveThisItem(*item));
}

static void Hud_EquipNextItem(uint8 *item) {  // 8ddee2
  do {
    Hud_GotoNextItem(item, item == &hud_cur_item);
  } while (!Hud_DoWeHaveThisItem(*item));
}

// A column only comes back to its start when the start is owned, and an empty
// slot or a lost item is not: after one round it looks through all the items,
// and with none it stays where it was.
static void Hud_EquipItemInColumn(uint8 *item, bool down) {
  uint8 start = *item;
  int column = kNewStyleInventory ? 6 : 5;
  int rounds = kHudItemCount / column + kHudItemCount;
  for (int round = 0; round < rounds; round++) {
    int num = *item == 0 || round >= kHudItemCount / column ? 1 : column;
    for (int i = 0; i < num; i++) {
      if (down)
        Hud_GotoNextItem(item, 1);
      else
        Hud_GotoPrevItem(item, 1);
    }
    if (Hud_DoWeHaveThisItem(*item))
      return;
  }
  *item = start;
}

static void Hud_EquipItemAbove(uint8 *item) {  // 8ddeeb
  Hud_EquipItemInColumn(item, false);
}

static void Hud_EquipItemBelow(uint8 *item) {  // 8ddf00
  Hud_EquipItemInColumn(item, true);
}

int GetCurrentItemButtonIndex() {
  if (enhanced_features0 & kFeatures0_SwitchLR) {
    return (joypad1L_last & kJoypadL_X) ? 1 :
           (joypad1L_last & kJoypadL_L) ? 2 :
           (joypad1L_last & kJoypadL_R) ? 3 : 0;
  }
  return 0;
}

uint8 *GetCurrentItemButtonPtr(int i) {
  return (i == 0) ? &hud_cur_item : 
         (i == 1) ? &hud_cur_item_x : 
         (i == 2) ? &hud_cur_item_l : &hud_cur_item_r;
}

void Hud_NormalMenu() {  // 8ddf15
  timer_for_flashing_circle++;
  if (!BYTE(joypad1H_last))
    BYTE(hud_tmp1) = 0;

  if (filtered_joypad_H & kJoypadH_Start) {
    overworld_map_state = 5;
    sound_effect_2 = 18;
    return;
  }

  // Allow select to open the save/exit thing
  if (joypad1H_last & kJoypadH_Select && sram_progress_indicator) {
    // One step of Hud_CloseMenu from here finishes closing, whichever its size.
    BG3VOFS_copy2 = g_loc_rom ? -10 : -8;
    Hud_CloseMenu();
    DisplaySelectMenu();
    return;
  }

  if (joypad1H_last & kJoypadH_Y && !(joypad1L_last & kJoypadL_X) && (enhanced_features0 & kFeatures0_SwitchLR)) {
    if (filtered_joypad_H & kJoypadH_Up) {
      Hud_ReorderItem(kNewStyleInventory ? -6 : -5);
    } else if (filtered_joypad_H & kJoypadH_Down) {
      Hud_ReorderItem(kNewStyleInventory ? 6 : 5);
    } else if (filtered_joypad_H & kJoypadH_Left) {
      Hud_ReorderItem(-1);
    } else if (filtered_joypad_H & kJoypadH_Right) {
      Hud_ReorderItem(1);
    }
  } else if (!BYTE(hud_tmp1)) {
    // If Special Key button is down, then move their circle
    int btn_index = GetCurrentItemButtonIndex();
    uint8 *item_p = GetCurrentItemButtonPtr(btn_index);
    uint16 old_item = *item_p;
    if (filtered_joypad_H & kJoypadH_Up) {
      Hud_EquipItemAbove(item_p);
    } else if (filtered_joypad_H & kJoypadH_Down) {
      Hud_EquipItemBelow(item_p);
    } else if (filtered_joypad_H & kJoypadH_Left) {
      Hud_EquipPrevItem(item_p);
    } else if (filtered_joypad_H & kJoypadH_Right) {
      Hud_EquipNextItem(item_p);
    }
    // The game keeps the press only when it was a move ($8D:DF73).
    if (filtered_joypad_H & (kJoypadH_Up | kJoypadH_Down | kJoypadH_Left | kJoypadH_Right))
      BYTE(hud_tmp1) = filtered_joypad_H;
    if (*item_p != old_item) {
      timer_for_flashing_circle = 16;
      sound_effect_2 = 32;
    }
  }

  // The permanent shovel can be selected with A on the old combined slot.
  // Leave the flute byte intact: ownership, bird travel and saved progression
  // must not be replaced by a transient selection.
  if (!kNewStyleInventory && !g_rando_slot_active &&
      (enhanced_features1 & kFeatures1_QolShovel) && link_item_flute>=2 &&
      hud_cur_item==kHudItem_Flute && (filtered_joypad_L & kJoypadL_A)) {
    current_item_y=current_item_y==21?8:21;
    Hud_UpdateItemBox();
    sound_effect_2=32;
  }

  // Rando item-menu swap. Several Y-slots pack what rando shuffles as
  // independent items into one byte: flute/shovel (link_item_flute: 1=shovel,
  // 2=flute, 3=active flute), boomerang (link_item_boomerang: 1=blue, 2=red)
  // and bow (link_item_bow: 1/2=wood, 3/4=silver arrows). When the player owns
  // both tiers, pressing A on the highlighted slot swaps which tier the slot
  // performs — mirroring ALTTPR's item-menu swap. Without it the lower tier
  // would be unreachable after a never-downgrade grant. A is otherwise unused
  // in this menu; ownership is tracked in g_rando_*_owned (one byte can't
  // represent it). See the Rando_Grant* helpers (rando.c) for the grants.
  if ((enhanced_features1 & kFeatures1_RandomizerActive) &&
      (filtered_joypad_L & kJoypadL_A)) {
    uint8 cur_slot = *GetCurrentItemButtonPtr(GetCurrentItemButtonIndex());
    bool swapped = true;
    if (cur_slot == kHudItem_Flute && Rando_FluteShovelCanToggle()) {
      // rando-exempt: player toggle of the flute/shovel SELECTED function, not
      // an item grant — ownership in g_rando_flute_shovel_owned is unchanged.
      // Gated on already owning both. See Rando_GrantFluteShovel (rando.c).
      link_item_flute = (link_item_flute == 1)
          ? ((g_rando_flute_shovel_owned & kRandoFluteShovel_FluteActive) ? 3 : 2)
          : 1;
    } else if (cur_slot == kHudItem_Boomerang && Rando_BoomerangCanToggle()) {
      // rando-exempt: player toggle of the boomerang SELECTED color, not an item
      // grant — ownership in g_rando_boomerang_owned is unchanged. Gated on
      // already owning both. See Rando_GrantBoomerang (rando.c).
      link_item_boomerang = (link_item_boomerang == 2) ? 1 : 2;  // red <-> blue
    } else if (cur_slot == kHudItem_Bow && Rando_BowCanToggle()) {
      // rando-exempt: player toggle of the bow arrow tier (SELECTED), not an item
      // grant — ownership in g_rando_bow_owned is unchanged. Gated on owning
      // silver (which implies wood). Preserves the arrow bit (parity): silver
      // 3/4 <-> wood 1/2. See Rando_GrantBow (rando.c).
      link_item_bow = (link_item_bow >= 3) ? (link_item_bow - 2)
                                           : (link_item_bow + 2);
    } else if (cur_slot == kHudItem_Mushroom && Rando_MushroomPowderCanToggle()) {
      // Toggling AWAY from Powder (byte 2) stops the byte showing Powder, so
      // record ownership first — logic/tracking read kRandoMushroom_PowderOwned,
      // not the byte, so Powder stays "owned" while the Mushroom icon is up.
      if (link_item_mushroom == 2)
        g_rando_mushroom_held |= kRandoMushroom_PowderOwned;
      // rando-exempt: player toggle of the shared Mushroom/Powder icon
      // (SELECTED), not an item grant — possession stays in g_rando_mushroom_held
      // and is unchanged. Gated on owning both. See Rando_MushroomPowderCanToggle.
      link_item_mushroom = (link_item_mushroom == 2) ? 1 : 2;  // mushroom <-> powder
    } else {
      swapped = false;
    }
    if (swapped) {
      timer_for_flashing_circle = 16;
      sound_effect_2 = 32;  // menu select sound
    }
  }

  Hud_DrawYButtonItems();
  Hud_DrawSelectedYButtonItem();
  if (hud_cur_item == kHudItem_BottleOld && !kNewStyleInventory)
    overworld_map_state = 7;

  nmi_subroutine_index = 1;
  BYTE(nmi_load_target_addr) = 0x22;
  //g_ram[0x15d0] = 0;
}

void Hud_UpdateHud() {  // 8ddfa9
  overworld_map_state++;
  Hud_Rebuild();
  Hud_UpdateEquippedItem();
}

uint8 Hud_LookupInventoryItem(uint8 item) {
  static const uint8 kHudItemToItemOrg[21] = {
    0,
    3,  2, 14, 1,  10,  5,
    6, 15, 16, 17,  9,  4,
    8,  7, 12, 11, 18, 13,
    19, 20,
  };

  static const uint8 kHudItemToItemNew[25] = {
    0,
    3,  2, 14, 1,  10,  5,
    6, 15, 16, 17,  9,  4,
    8,  7, 12, 21, 18, 13, // 8 is ocarina / shovel combined. moved shovel to 21.
    19, 20,11, 11, 11, 11, // 11 means bottle
  };
  if (!kNewStyleInventory && item==kHudItem_Flute && current_item_y==21 &&
      (enhanced_features1 & kFeatures1_QolShovel) && !g_rando_slot_active) return 21;
  return kNewStyleInventory ? kHudItemToItemNew[item] : kHudItemToItemOrg[item];
}

void Hud_UpdateEquippedItem() {  // 8ddfaf
  if (hud_cur_item >= kHudItem_Bottle1)
    link_item_bottle_index = hud_cur_item - kHudItem_Bottle1 + 1;

  assert(hud_cur_item < 25);
  if (kNewStyleInventory && hud_cur_item==kHudItem_Flute && g_rando_slot_active)
    link_item_flute=Rando_FluteShovelEffectiveLevel();
  current_item_y = Hud_LookupInventoryItem(hud_cur_item);
}

void Hud_CloseMenu() {  // 8ddfba
  BG3VOFS_copy2 += g_loc_rom ? 10 : 8;  // $8D:DFBF
  if (BG3VOFS_copy2)
    return;
  Hud_Rebuild();
  overworld_map_state = 0;
  submodule_index = 0;
  main_module_index = saved_module_for_menu;
  if (submodule_index)
    Hud_RestoreTorchBackground();
  if (current_item_y != 5 && current_item_y != 6) {
    eq_debug_variable = 2;
    link_debug_value_1 = 0;
  } else {
    assert(!link_debug_value_1);
    eq_debug_variable = 0;
  }
}

void Hud_GotoBottleMenu() {  // 8ddffb
  bottle_menu_expand_row = 0;
  overworld_map_state++;
}

void Hud_InitBottleMenu() {  // 8de002
  int r = bottle_menu_expand_row;
  uint16 *dst = uvram_screen.row[0].col + (kNewStyleInventory ? 1 : 0);
  for (int i = 21; i <= 30; i++)
    dst[HUDXY(i, 11 + r)] = 0x207f;

  if (++bottle_menu_expand_row == 19) {
    overworld_map_state++;
    bottle_menu_expand_row = 17;
  }
  nmi_subroutine_index = 1;
  BYTE(nmi_load_target_addr) = 0x22;
}

void Hud_ExpandBottleMenu() {  // 8de08c
  static const uint16 kBottleMenuTop[] = { 0x28FB, 0x28F9, 0x28F9, 0x28F9, 0x28F9, 0x28F9, 0x28F9, 0x28F9, 0x28F9, 0x68FB };
  static const uint16 kBottleMenuTop2[] = { 0x28FC, 0x24F5, 0x24F5, 0x24F5, 0x24F5, 0x24F5, 0x24F5, 0x24F5, 0x24F5, 0x68FC };
  static const uint16 kBottleMenuBottom[] = { 0xA8FB, 0xA8F9, 0xA8F9, 0xA8F9, 0xA8F9, 0xA8F9, 0xA8F9, 0xA8F9, 0xA8F9, 0xE8FB };

  int r = bottle_menu_expand_row;
  uint16 *dst = uvram_screen.row[0].col + (kNewStyleInventory ? 1 : 0);
  memcpy(&dst[HUDXY(21, 11 + r)], kBottleMenuTop, sizeof(uint16) * 10);
  memcpy(&dst[HUDXY(21, 12 + r)], kBottleMenuTop2, sizeof(uint16) * 10);
  memcpy(&dst[HUDXY(21, 29)], kBottleMenuBottom, sizeof(uint16) * 10);

  if (sign8(--bottle_menu_expand_row))
    overworld_map_state++;
  nmi_subroutine_index = 1;
  BYTE(nmi_load_target_addr) = 0x22;
}

void Hud_BottleMenu() {  // 8de0df
  timer_for_flashing_circle++;
  if (filtered_joypad_H & kJoypadH_Start) {
    sound_effect_2 = 18;
    overworld_map_state = 5;
  } else if (filtered_joypad_H & (kJoypadH_Left | kJoypadH_Right)) {
    if (filtered_joypad_H & kJoypadH_Left) {
      Hud_EquipPrevItem(&hud_cur_item);
    } else {
      Hud_EquipNextItem(&hud_cur_item);
    }
    timer_for_flashing_circle = 16;
    sound_effect_2 = 32;
    Hud_DrawYButtonItems();
    Hud_DrawSelectedYButtonItem();
    overworld_map_state++;
    bottle_menu_expand_row = 0;
    return;
  }
  Hud_DrawBottleMenu_Update();
  if (filtered_joypad_H & (kJoypadH_Down | kJoypadH_Up)) {
    uint8 old_val = link_item_bottle_index - 1, val = old_val;

    if (filtered_joypad_H & kJoypadH_Up) {
      do {
        val = (val - 1) & 3;
      } while (!link_bottle_info[val]);
    } else {
      do {
        val = (val + 1) & 3;
      } while (!link_bottle_info[val]);
    }
    if (old_val != val) {
      link_item_bottle_index = val + 1;
      timer_for_flashing_circle = 16;
      sound_effect_2 = 32;
    }
  }
}

static void Hud_DrawItem(uint16 *dst, const ItemBoxGfx *src) {  // new
  dst[0] = src->v[0];
  dst[1] = src->v[1];
  dst[32] = src->v[2];
  dst[33] = src->v[3];
}

static void Hud_DrawNxN(uint16 *dst, const uint16 *src, int w, int h) {  // new
  for (int y = 0; y < h; y++) {
    memcpy(dst, src, sizeof(uint16) * w);
    dst += 32, src += w;
  }
}

static void Hud_Copy2x2(uint16 *dst, uint16 *src) {  // new
  dst[0] = src[0];
  dst[1] = src[1];
  dst[32] = src[32];
  dst[33] = src[33];
}

static void Hud_DrawBox(uint16 *dst, int x1, int y1, int x2, int y2, uint8 palette) {  // new
  uint16 t;

  t = 0x20fb | palette << 10;
  dst[HUDXY(x1, y1)] = t;
  dst[HUDXY(x2, y1)] = t + 0x4000;
  dst[HUDXY(x1, y2)] = t + 0x8000;
  dst[HUDXY(x2, y2)] = t + 0xc000;

  t = 0x20fc | palette << 10;
  for (int y = y1 + 1; y < y2; y++) {
    dst[HUDXY(x1, y)] = t;
    dst[HUDXY(x2, y)] = t + 0x4000;
  }

  t = 0x20f9 | palette << 10;
  for (int x = x1 + 1; x < x2; x++) {
    dst[HUDXY(x, y1)] = t;
    dst[HUDXY(x, y2)] = t + 0x8000;
  }

  for (int y = y1 + 1; y < y2; y++) {
    for (int x = x1 + 1; x < x2; x++)
      dst[HUDXY(x, y)] = 0x24F5;
  }
}

static void Hud_DrawFlashingCircle(uint16 *p, uint8 palette) {  // new
  int pp = palette << 10;
  p[HUDXY(0, -1)] = pp | 0x2061;
  p[HUDXY(1, -1)] = pp | 0x2061 | 0x4000;
  p[HUDXY(-1, 0)] = pp | 0x2070;
  p[HUDXY(2, 0)] = pp | 0x2070 | 0x4000;
  p[HUDXY(-1, 1)] = pp | 0xa070;
  p[HUDXY(2, 1)] = pp | 0xa070 | 0x4000;
  p[HUDXY(0, 2)] = pp | 0xa061;
  p[HUDXY(1, 2)] = pp | 0xa061 | 0x4000;
  p[HUDXY(-1, -1)] = pp | 0x2060;
  p[HUDXY(2, -1)] = pp | 0x2060 | 0x4000;
  p[HUDXY(2, 2)] = pp | 0x2060 | 0xC000;
  p[HUDXY(-1, 2)] = pp | 0x2060 | 0x8000;
}

void Hud_DrawBottleMenu() {  // 8def67
  uint16 *dst = uvram_screen.row[0].col + (kNewStyleInventory ? 1 : 0);
  Hud_DrawBox(dst, 21, 11, 30, 29, 2);
  for (int i = 0; i < 4; i++)
    Hud_DrawItem(dst + HUDXY(25, 13 + i * 4), &kHudItemBottles[link_bottle_info[i]]);
  const ItemBoxGfx *p = &kHudItemBottles[link_bottle_info[link_item_bottle_index - 1]];
  Hud_DrawItem(uvram_screen.row[0].col + kHudItemInVramPtr[15], p);
  if (timer_for_flashing_circle & 0x10)
    Hud_DrawFlashingCircle(dst + HUDXY(25, 13 + (link_item_bottle_index - 1) * 4), 7);
}


void Hud_DrawBottleMenu_Update() {  // 8de17f
  Hud_DrawBottleMenu();
  Hud_DrawSelectedYButtonItem();

  nmi_subroutine_index = 1;
  BYTE(nmi_load_target_addr) = 0x22;
}

void Hud_EraseBottleMenu() {  // 8de2fd
  uint16 *dst = uvram_screen.row[0].col + (kNewStyleInventory ? 1 : 0);
  int r = bottle_menu_expand_row;
  for (int i = 0; i < 10; i++)
    dst[HUDXY(21 + i, 11 + r)] = 0x207f;
  if (++bottle_menu_expand_row == 19)
    overworld_map_state++;
  nmi_subroutine_index = 1;
  BYTE(nmi_load_target_addr) = 0x22;
}

void Hud_RestoreNormalMenu() {  // 8de346
  Hud_DrawProgressIcons();
  Hud_DrawEquipmentBox();
  overworld_map_state = 4;
  nmi_subroutine_index = 1;
  BYTE(nmi_load_target_addr) = 0x22;
}

void Hud_SearchForEquippedItem() {  // 8de399
  if (!Hud_HaveAnyItems()) {
    hud_cur_item = 0;
    hud_var1 = 0;
  } else {
    if (!hud_cur_item)
      hud_cur_item = 1;
    if (!Hud_DoWeHaveThisItem(hud_cur_item))
      Hud_EquipNextItem(&hud_cur_item);
  }
}

static const ItemBoxGfx *Hud_GetIconForItem(int i) {
  if (i <= 0)
    return kHudItemEmpty;

  if (i >= kHudItem_Bottle1)
    return &kHudItemBottles[link_bottle_info[i - kHudItem_Bottle1]];
  if (!kNewStyleInventory && i==kHudItem_Flute && current_item_y==21 &&
      (enhanced_features1 & kFeatures1_QolShovel) && !g_rando_slot_active)
    return &kHudItemFlute[1];
  if (i == kHudItem_Shovel && kNewStyleInventory)
    return &kHudItemFlute[Hud_QolHasShovel()];
  if (i == kHudItem_Flute && kNewStyleInventory) {
    uint8 level=Hud_DoWeHaveThisItem(i) ? (g_rando_slot_active?Rando_FluteShovelEffectiveLevel():link_item_flute) : 0;
    return &kHudItemFlute[level];
  }

  uint8 item_val = (&link_item_bow)[i - 1];
  if (i == kHudItem_Bombs) // bombs
    item_val = (item_val != 0);
  else if (i == kHudItem_BottleOld && !kNewStyleInventory)
    item_val = link_item_bottle_index ? link_bottle_info[link_item_bottle_index - 1] : 0;
  else if (i == kHudItem_Bow && item_val == 4 && g_rando_slot_active)
    return &kHudItemBowRandoSilverArrows;
  return &kHudItemBoxGfxPtrs[i - 1][item_val];
}

static void CopyTilesForSwitchLR(int switch_lr) {
  // BG3 characters start at $6000 in the French Canadian cartridge, whose
  // tiles 0xc-0xf are the same solid fill the USA ones are.
  const int bg3_chr = g_loc_rom ? 0x6000 : 0x7000;
#define PV(a0,a1,a2,a3,a4,a5,a6,a7)  ((a0 & 1) << 7 | (a0 >> 1 & 1) << 15 | (a1 & 1) << 6 | (a1 >> 1 & 1) << 14 | (a2 & 1) <<5 | (a2 >> 1&1) <<13 | (a3 & 1) << 4 | (a3>> 1 & 1) << 12 | (a4 & 1) << 3 | (a4 >> 1 & 1) << 11 | (a5 & 1) << 2 | (a5 >> 1 & 1) << 10 | (a6 & 1) << 1 | (a6 >> 1 & 1) << 9 | (a7 & 1) << 0 | (a7 >> 1 & 1) << 8) 
    
  if (switch_lr == 3) {
    static const uint16 kBytesForNewTile0xC_TopOfR[8] = {
      PV(1,1,1,1,1,1,3,3),
      PV(1,1,1,1,1,1,1,3),
      PV(1,1,1,1,1,1,1,1),
      PV(1,1,1,3,3,1,1,1),
      PV(1,1,1,3,3,1,1,1),
      PV(1,1,1,3,3,1,1,1),
      PV(1,1,1,3,3,1,1,1),
      PV(1,1,1,1,1,1,1,3)
    };
    memcpy(&g_zenv.vram[bg3_chr + 0xc * 8], kBytesForNewTile0xC_TopOfR, sizeof(kBytesForNewTile0xC_TopOfR));

    static const uint16 kBytesForNewTile0xD_BottomofR[8] = {
      PV(1,1,1,1,1,1,3,3),
      PV(1,1,1,3,1,1,1,3),
      PV(1,1,1,3,3,1,1,1),
      PV(1,1,1,3,3,1,1,1),
      PV(1,1,1,3,3,1,1,1),
      PV(1,1,1,3,3,1,1,1),
      PV(1,1,1,3,3,1,1,1),
      PV(1,1,1,3,3,1,1,1)
    };
    memcpy(&g_zenv.vram[bg3_chr + 0xd * 8], kBytesForNewTile0xD_BottomofR, sizeof(kBytesForNewTile0xD_BottomofR));
  } else if (switch_lr == 2) {
    static const uint16 kBytesForNewTile0xE_TopOfL[8] = {
      PV(1,1,1,3,3,3,3,3),
      PV(1,1,1,3,3,3,3,3),
      PV(1,1,1,3,3,3,3,3),
      PV(1,1,1,3,3,3,3,3),
      PV(1,1,1,3,3,3,3,3),
      PV(1,1,1,3,3,3,3,3),
      PV(1,1,1,3,3,3,3,3),
      PV(1,1,1,3,3,3,3,3)
    };
    memcpy(&g_zenv.vram[bg3_chr + 0xe * 8], kBytesForNewTile0xE_TopOfL, sizeof(kBytesForNewTile0xE_TopOfL));

    static const uint16 kBytesForNewTile0xF_BottomofL[8] = {
      PV(1,1,1,3,3,3,3,3),
      PV(1,1,1,3,3,3,3,3),
      PV(1,1,1,3,3,3,3,3),
      PV(1,1,1,3,3,3,3,3),
      PV(1,1,1,3,3,3,3,3),
      PV(1,1,1,1,1,1,1,1),
      PV(1,1,1,1,1,1,1,1),
      PV(1,1,1,1,1,1,1,1)
    };
    memcpy(&g_zenv.vram[bg3_chr + 0xf * 8], kBytesForNewTile0xF_BottomofL, sizeof(kBytesForNewTile0xF_BottomofL));
  }
#undef PV
}

static const uint8 kSwitchLR_palettes[] = { 7, 3, 4, 4 };

void Hud_DrawYButtonItems() {  // 8de3d9
  uint16 *dst = uvram_screen.row[0].col;
  int x = kNewStyleInventory ? 0 : 1;

  int btn_index = GetCurrentItemButtonIndex();
  CopyTilesForSwitchLR(btn_index);
  Hud_DrawBox(dst, x, 5, 20 - x, 19, kSwitchLR_palettes[btn_index]);
  static const uint16 kEquipmentLetterTiles[4][2] = {
    {0x3CF0, 0x3CF1},  // Y
    {0x2CF0, 0x2CF0 | 0x8000},  // X
    {0x200E | 4 << 10, 0x200F | 4 << 10},  // L
    {0x200C | 4 << 10, 0x200D | 4 << 10}   // R
   };

  if (!kNewStyleInventory) {
    dst[HUDXY(2, 6)] = kEquipmentLetterTiles[btn_index][0];
    dst[HUDXY(2, 7)] = kEquipmentLetterTiles[btn_index][1]; 
  }
  // ITEM, which the French Canadian, German and French cartridges do not
  // draw ($8D:E46C).
  if (!g_loc_rom && (!LanguageCart_Active() || LanguageCart_UsaLayout() || LanguageCart_Japanese())) {
    dst[HUDXY(x + 2, 5)] = 0x246E;
    dst[HUDXY(x + 3, 5)] = 0x246F;
  }

  for (int i = 0; i < kHudItemCount; i++) {
    int j = hud_inventory_order[i];
    Hud_DrawItem(dst + kHudItemInVramPtr[i], Hud_GetIconForItem(j == 0 ? i + 1: j) );
  }
}

void Hud_DrawAbilityBox() {  // 8de6b6
  static const uint16 kHudAbilityText[80] = {
    0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2d5b, 0x2d58, 0x2d55, 0x2d63, 0x2d27,
    0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2d61, 0x2d54, 0x2d50, 0x2d53,
    0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2d63, 0x2d50, 0x2d5b, 0x2d5a,
    0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f,
    0x2cf5, 0x2cf5, 0x2c2e, 0x2cf5, 0x2cf5, 0x2d5f, 0x2d64, 0x2d5b, 0x2d5b, 0x2cf5,
    0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2d61, 0x2d64, 0x2d5d, 0x2cf5,
    0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2d62, 0x2d66, 0x2d58, 0x2d5c,
    0x2cf5, 0x2cf5, 0x2cf5, 0x207f, 0x207f, 0x2c01, 0x2c18, 0x2c28, 0x207f, 0x207f,
  };
  static const uint16 kHudGlovesText[20] = {
    0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2d5b, 0x2d58, 0x2d55, 0x2d63, 0x2d28,
    0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2cf5, 0x2d5b, 0x2d58, 0x2d55, 0x2d63, 0x2d29,
  };
  uint16 *dst = uvram_screen.row[0].col;
  int x = kNewStyleInventory ? 0 : 1;
  uint16 ability_text[10];

  Hud_DrawBox(dst, x, 21, 19, 29, 1);
  // The Japanese cartridge's words start a column earlier ($8D:E6F9).
  int ability_x = LanguageCart_Japanese() ? 3 : 4;

  uint8 flags = link_ability_flags;
  for (int i = 0; i < 2; i++, flags <<= 1) {
    for (int j = 0; j < 3; j++, flags <<= 1) {
      if (flags & 0x80)
        Hud_DrawNxN(dst + HUDXY(ability_x + j * 5 + (LanguageCart_Japanese() ? i : 0), 22 + i * 2),
                    Hud_Text(ability_text, kHudAbilityText, 0x8df941, 0x8df959, i * 40 + j * 10, 10), 5, 2);
    }
  }
  // A
  if (!kNewStyleInventory) {
    dst[HUDXY(2, 22)] = 0xA4F0;
    dst[HUDXY(2, 23)] = 0x24F2;
  }
  // DO text
  // not drawn by the French Canadian, German and French cartridges ($8D:E79C)
  if (!g_loc_rom && (!LanguageCart_Active() || LanguageCart_UsaLayout() || LanguageCart_Japanese())) {
    dst[HUDXY(x + 2, 21)] = 0x2482;
    dst[HUDXY(x + 3, 21)] = 0x2483;
  }

  Hud_DrawItem(dst + HUDXY(8, 27), &kHudItemGloves[link_item_gloves]);
  Hud_DrawItem(dst + HUDXY(4, 27), &kHudItemBoots[link_item_boots]);
  Hud_DrawItem(dst + HUDXY(12, 27), &kHudItemFlippers[link_item_flippers]);
  Hud_DrawItem(dst + HUDXY(16, 27), &kHudItemMoonPearl[link_item_moon_pearl]);
  if (link_item_gloves)
    Hud_DrawNxN(dst + HUDXY(ability_x, 22),
                Hud_Text(ability_text, kHudGlovesText, 0x8df919, 0x8df931, (link_item_gloves != 1) * 10, 10), 5, 2);
}

void Hud_DrawProgressIcons() {  // 8de9c8
  if (sram_progress_indicator < 3)
    Hud_DrawProgressIcons_Pendants();
  else
    Hud_DrawProgressIcons_Crystals();
}

void Hud_DrawProgressIcons_Pendants() {  // 8de9d3
  static const uint16 kProgressIconPendantsBg[90] = {
    0x28fb, 0x28f9, 0x28f9, 0x28f9, 0x28f9, 0x28f9, 0x28f9, 0x28f9, 0x28f9, 0x68fb,
    0x28fc, 0x2521, 0x2522, 0x2523, 0x2524, 0x253f, 0x24f5, 0x24f5, 0x24f5, 0x68fc,
    0x28fc, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x68fc,
    0x28fc, 0x24f5, 0x24f5, 0x24f5, 0x213b, 0x213c, 0x24f5, 0x24f5, 0x24f5, 0x68fc,
    0x28fc, 0x24f5, 0x24f5, 0x24f5, 0x213d, 0x213e, 0x24f5, 0x24f5, 0x24f5, 0x68fc,
    0x28fc, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x68fc,
    0x28fc, 0x24f5, 0x213b, 0x213c, 0x24f5, 0x24f5, 0x213b, 0x213c, 0x24f5, 0x68fc,
    0x28fc, 0x24f5, 0x213d, 0x213e, 0x24f5, 0x24f5, 0x213d, 0x213e, 0x24f5, 0x68fc,
    0xa8fb, 0xa8f9, 0xa8f9, 0xa8f9, 0xa8f9, 0xa8f9, 0xa8f9, 0xa8f9, 0xa8f9, 0xe8fb,
  };
  static const ItemBoxGfx kHudPendants0[2] = {
    {{0x313b, 0x313c, 0x313d, 0x313e}},
    {{0x252b, 0x252c, 0x252d, 0x252e}},
  };
  static const ItemBoxGfx kHudPendants1[2] = {
    {{0x313b, 0x313c, 0x313d, 0x313e}},
    {{0x2d2b, 0x2d2c, 0x2d2d, 0x2d2e}},
  };
  static const ItemBoxGfx kHudPendants2[2] = {
    {{0x313b, 0x313c, 0x313d, 0x313e}},
    {{0x3d2b, 0x3d2c, 0x3d2d, 0x3d2e}},
  };
  uint16 *dst = uvram_screen.row[0].col + (kNewStyleInventory ? HUDXY(22, 11) : HUDXY(21, 11));
  uint16 bg[90];
  Hud_DrawNxN(dst, Hud_Text(bg, kProgressIconPendantsBg, 0x8de848, 0x8de860, 0, 90), 10, 9);
  Hud_DrawItem(dst + HUDXY(4, 3), &kHudPendants0[(link_which_pendants >> 0) & 1]);
  Hud_DrawItem(dst + HUDXY(2, 6), &kHudPendants1[(link_which_pendants >> 1) & 1]);
  Hud_DrawItem(dst + HUDXY(6, 6), &kHudPendants2[(link_which_pendants >> 2) & 1]);
}

void Hud_DrawProgressIcons_Crystals() {  // 8dea62
  static const uint16 kProgressIconCrystalsBg[90] = {
    0x28fb, 0x28f9, 0x28f9, 0x28f9, 0x28f9, 0x28f9, 0x28f9, 0x28f9, 0x28f9, 0x68fb,
    0x28fc, 0x252f, 0x2534, 0x2535, 0x2536, 0x2537, 0x24f5, 0x24f5, 0x24f5, 0x68fc,
    0x28fc, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x68fc,
    0x28fc, 0x24f5, 0x24f5, 0x3146, 0x3147, 0x3146, 0x3147, 0x24f5, 0x24f5, 0x68fc,
    0x28fc, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x68fc,
    0x28fc, 0x24f5, 0x3146, 0x3147, 0x3146, 0x3147, 0x3146, 0x3147, 0x24f5, 0x68fc,
    0x28fc, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x68fc,
    0x28fc, 0x24f5, 0x24f5, 0x3146, 0x3147, 0x3146, 0x3147, 0x24f5, 0x24f5, 0x68fc,
    0xa8fb, 0xa8f9, 0xa8f9, 0xa8f9, 0xa8f9, 0xa8f9, 0xa8f9, 0xa8f9, 0xa8f9, 0xe8fb,
  };

  uint16 *dst = uvram_screen.row[0].col + (kNewStyleInventory ? HUDXY(22, 11) : HUDXY(21, 11));
  uint16 bg[90];
  Hud_DrawNxN(dst, Hud_Text(bg, kProgressIconCrystalsBg, 0x8de8fc, 0x8de914, 0, 90), 10, 9);

  uint8 f = link_has_crystals;
  if (f & 1) {
    dst[HUDXY(3, 3)] = 0x2D44;
    dst[HUDXY(4, 3)] = 0x2D45;
  }
  if (f & 2) {
    dst[HUDXY(5, 3)] = 0x2D44;
    dst[HUDXY(6, 3)] = 0x2D45;
  }
  if (f & 4) {
    dst[HUDXY(2, 5)] = 0x2D44;
    dst[HUDXY(3, 5)] = 0x2D45;
  }
  if (f & 8) {
    dst[HUDXY(4, 5)] = 0x2D44;
    dst[HUDXY(5, 5)] = 0x2D45;
  }
  if (f & 16) {
    dst[HUDXY(6, 5)] = 0x2D44;
    dst[HUDXY(7, 5)] = 0x2D45;
  }
  if (f & 32) {
    dst[HUDXY(3, 7)] = 0x2D44;
    dst[HUDXY(4, 7)] = 0x2D45;
  }
  if (f & 64) {
    dst[HUDXY(5, 7)] = 0x2D44;
    dst[HUDXY(6, 7)] = 0x2D45;
  }
}

void Hud_DrawSelectedYButtonItem() {  // 8deb3a
  static const uint16 kHudBottlesItemText[128] = {
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x255c, 0x2564, 0x2562, 0x2557, 0x2561, 0x255e, 0x255e, 0x255c,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2551, 0x255e, 0x2563, 0x2563, 0x255b, 0x2554, 0x24f5, 0x24f5,
    0x255b, 0x2558, 0x2555, 0x2554, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x255c, 0x2554, 0x2553, 0x2558, 0x2552, 0x2558, 0x255d, 0x2554,
    0x255c, 0x2550, 0x2556, 0x2558, 0x2552, 0x24f5, 0x24f5, 0x24f5, 0x255c, 0x2554, 0x2553, 0x2558, 0x2552, 0x2558, 0x255d, 0x2554,
    0x2552, 0x2564, 0x2561, 0x2554, 0x256a, 0x2550, 0x255b, 0x255b, 0x255c, 0x2554, 0x2553, 0x2558, 0x2552, 0x2558, 0x255d, 0x2554,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2555, 0x2550, 0x2554, 0x2561, 0x2558, 0x2554, 0x24f5, 0x24f5,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2551, 0x2554, 0x2554, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2556, 0x255e, 0x255e, 0x2553, 0x24f5, 0x2551, 0x2554, 0x2554,
  };
  static const uint16 kHudMushroomItemText[16] = {
    0x255c, 0x2550, 0x2556, 0x2558, 0x2552, 0x24f5, 0x24f5, 0x24f5,
    0x24f5, 0x255f, 0x255e, 0x2566, 0x2553, 0x2554, 0x2561, 0x24f5,
  };
  static const uint16 kHudFluteItemText[32] = {
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2555, 0x255b, 0x2564, 0x2563, 0x2554, 0x24f5, 0x24f5, 0x24f5,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2555, 0x255b, 0x2564, 0x2563, 0x2554, 0x24f5, 0x24f5, 0x24f5
  };
  static const uint16 kHudMirrorItemText[16] = {
    0x255c, 0x2550, 0x2556, 0x2558, 0x2552, 0x24f5, 0x24f5, 0x24f5,
    0x24f5, 0x24f5, 0x255c, 0x2558, 0x2561, 0x2561, 0x255e, 0x2561
  };
  static const uint16 kHudBowItemText[48] = {
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x256b, 0x256c, 0x256e, 0x256f, 0x257c, 0x257d, 0x257e, 0x257f,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x256b, 0x256c, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5,
    0x256b, 0x256c, 0x24f5, 0x256e, 0x256f, 0x24f5, 0x24f5, 0x24f5, 0x2578, 0x2579, 0x257a, 0x257b, 0x257c, 0x257d, 0x257e, 0x257f,
  };
  static const uint16 kHudItemText[320] = {
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x256b, 0x256c, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2570, 0x2571, 0x2572, 0x2573, 0x2574, 0x2575, 0x2576, 0x2577,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2557, 0x255e, 0x255e, 0x255a, 0x2562, 0x2557, 0x255e, 0x2563,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2551, 0x255e, 0x255c, 0x2551, 0x24f5, 0x24f5, 0x24f5, 0x24f5,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x255c, 0x2564, 0x2562, 0x2557, 0x2561, 0x255e, 0x255e, 0x255c,
    
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2555, 0x2558, 0x2561, 0x2554, 0x2561, 0x255e, 0x2553, 0x24f5,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2558, 0x2552, 0x2554, 0x2561, 0x255e, 0x2553, 0x24f5, 0x24f5,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2551, 0x255e, 0x255c, 0x2551, 0x255e, 0x2562, 0x24f5, 0x24f5,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2554, 0x2563, 0x2557, 0x2554, 0x2561, 0x24f5, 0x24f5, 0x24f5,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2560, 0x2564, 0x2550, 0x255a, 0x2554, 0x24f5, 0x24f5, 0x24f5,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x255b, 0x2550, 0x255c, 0x255f, 0x24f5, 0x24f5, 0x24f5, 0x24f5,
    0x255c, 0x2550, 0x2556, 0x2558, 0x2552, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2557, 0x2550, 0x255c, 0x255c, 0x2554, 0x2561,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2562, 0x2557, 0x255e, 0x2565, 0x2554, 0x255b, 0x24f5, 0x24f5,
    0x2400, 0x2401, 0x2402, 0x2403, 0x2404, 0x2405, 0x2406, 0x2407, 0x2408, 0x2409, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5,
    0x2551, 0x255e, 0x255e, 0x255a, 0x24f5, 0x255e, 0x2555, 0x24f5, 0x255c, 0x2564, 0x2553, 0x255e, 0x2561, 0x2550, 0x24f5, 0x24f5,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x255c, 0x2564, 0x2562, 0x2557, 0x2561, 0x255e, 0x255e, 0x255c,
    0x2552, 0x2550, 0x255d, 0x2554, 0x24f5, 0x255e, 0x2555, 0x24f5, 0x24f5, 0x2562, 0x255e, 0x255c, 0x2550, 0x2561, 0x2558, 0x2550,
    0x2552, 0x2550, 0x255d, 0x2554, 0x24f5, 0x255e, 0x2555, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2551, 0x2568, 0x2561, 0x255d, 0x2550,
    0x255c, 0x2550, 0x2556, 0x2558, 0x2552, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x2552, 0x2550, 0x255f, 0x2554, 0x24f5,
    0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5, 0x24f5,
  };

#define L(x) (x == ' ' ? 0x24f5 : 0x2550 + x - 'A')
  const uint16 kNotAssignedItemTextUs[16] = {
    L('N'), L('O'), L('T'), L(' '), L(' '), L(' '), L(' '), L(' '),
    L('A'), L('S'), L('S'), L('I'), L('G'), L('N'), L('E'), L('D'),
  };
  // The port's own, so the French Canadian cartridge has no words for it: it
  // is spelled with its letters, which sit where the USA ones do, and
  // without accents, as it writes its item names.
  const uint16 kNotAssignedItemTextLoc[16] = {
    L(' '), L(' '), L('N'), L('O'), L('N'), L(' '), L(' '), L(' '),
    L('A'), L('S'), L('S'), L('I'), L('G'), L('N'), L('E'), L(' '),
  };
#undef L
  const uint16 *kNotAssignedItemText = g_loc_rom ? kNotAssignedItemTextLoc : kNotAssignedItemTextUs;

  uint16 *dst_org = uvram_screen.row[0].col;
  uint16 *dst_box = dst_org + (kNewStyleInventory ? 1 : 0);
  
  int btn_index = GetCurrentItemButtonIndex();
  int item = *GetCurrentItemButtonPtr(btn_index);
  Hud_DrawBox(dst_box, 21, 5, 21 + 9, 10, kSwitchLR_palettes[btn_index]);

  // Display either the current item or the item assigned
  // to the x, l, or r key.
  if (item != 0) {
    uint16 *p = dst_org + kHudItemInVramPtr[Hud_GetItemPosition(item)];
    Hud_Copy2x2(dst_box + HUDXY(25, 6), p);
    if (timer_for_flashing_circle & 0x10)
      Hud_DrawFlashingCircle(p, kSwitchLR_palettes[btn_index]);
  }

  // A mod can rename the items for the USA cartridge: the tables where that
  // cartridge keeps them, with the mod's bytes over them (kModTable_RomBytes).
  // The French Canadian cartridge's own names are read below.
  const uint16 *bottles = g_loc_rom ? kHudBottlesItemText : MODS_U16(kHudBottlesItemText, 0x8df449);
  const uint16 *mushroom = g_loc_rom ? kHudMushroomItemText : MODS_U16(kHudMushroomItemText, 0x8df549);
  const uint16 *flute = g_loc_rom ? kHudFluteItemText : MODS_U16(kHudFluteItemText, 0x8df569);
  const uint16 *mirror = g_loc_rom ? kHudMirrorItemText : MODS_U16(kHudMirrorItemText, 0x8df5a9);
  const uint16 *bow = g_loc_rom ? kHudBowItemText : MODS_U16(kHudBowItemText, 0x8df5c9);
  const uint16 *items = g_loc_rom ? kHudItemText : MODS_U16(kHudItemText, 0x8df1c9);

  uint8 flute_level=kNewStyleInventory&&g_rando_slot_active?Rando_FluteShovelEffectiveLevel():link_item_flute;
  if(!kNewStyleInventory && !g_rando_slot_active && (enhanced_features1&kFeatures1_QolShovel) && current_item_y==21)flute_level=1;
  const uint16 *src_p;
  if (item == kHudItem_BottleOld && !kNewStyleInventory && link_item_bottle_index) {
    src_p = &bottles[(link_bottle_info[link_item_bottle_index - 1] - 1) * 16];
  } else if (item == 5 && link_item_mushroom != 1) {
    src_p = &mushroom[(link_item_mushroom - 2) * 16];
  } else if (item == 20 && link_item_mirror != 1) {
    src_p = &mirror[(link_item_mirror - 2) * 16];
  } else if (item == 13 && flute_level >= 2) {
    src_p = &flute[(flute_level - 2) * 16];
  } else if (item == 1 && link_item_bow != 1) {
    src_p = &bow[(link_item_bow - 2) * 16];
  } else if (item >= kHudItem_Bottle1 && item <= kHudItem_Bottle4) {
    src_p = &bottles[(link_bottle_info[item - kHudItem_Bottle1] - 1) * 16];
  } else if (item == kHudItem_Shovel) {
    src_p = &items[(13 - 1) * 16];
  } else if (item == 0) {
    src_p = btn_index ? kNotAssignedItemText : &items[(20 - 1) * 16];
  } else {
    src_p = &items[(item - 1) * 16];
  }
  // The French Canadian cartridge's names, from the same place in its own
  // tables. NOT ASSIGNED is the port's and is spelled above.
  static const struct { const uint16 *table; int count; uint32 loc; } kLocItemText[] = {
    { kHudBottlesItemText, 128, 0x8df431 },
    { kHudMushroomItemText, 16, 0x8df531 },
    { kHudFluteItemText, 32, 0x8df551 },
    { kHudMirrorItemText, 16, 0x8df591 },
    { kHudBowItemText, 48, 0x8df5b1 },
    { kHudItemText, 320, 0x8df1b1 },
  };
  uint16 loc_text[16];
  for (size_t i = 0; g_loc_rom && i < countof(kLocItemText); i++) {
    if (src_p >= kLocItemText[i].table && src_p < kLocItemText[i].table + kLocItemText[i].count) {
      src_p = Hud_Text(loc_text, kLocItemText[i].table, kLocItemText[i].loc, 0,
                       (int)(src_p - kLocItemText[i].table), 16);
      break;
    }
  }
  Hud_DrawNxN(dst_box + HUDXY(22, 8), src_p, 8, 2);
}

void Hud_DrawEquipmentBox() {  // 8ded29
  uint16 *dst = uvram_screen.row[0].col + (kNewStyleInventory ? 1 : 0);

  Hud_DrawBox(dst, 21, 21, 30, 29, 2);

  // Draw dotted lines
  for (int i = 0; i < 8; i++)
    dst[HUDXY(22 + i, 25)] = 0x28D7;

  static const uint16 kHudEquipmentDungeonItemText[16] = {
    0x2479, 0x247a, 0x247b, 0x247c, 0x248c, 0x24f5, 0x24f5, 0x24f5,
    0x2469, 0x246a, 0x246b, 0x246c, 0x246d, 0x246e, 0x246f, 0x24f5,
  };
  uint16 titles[16];
  const uint16 *title = Hud_Text(titles, kHudEquipmentDungeonItemText, 0x8decf1, 0x8ded09, 0, 16);
  memcpy(dst + HUDXY(22, 22), &title[0], 8 * sizeof(uint16));
  memcpy(dst + HUDXY(22, 26), &title[8], 8 * sizeof(uint16));

  static const ItemBoxGfx kHudItemHeartPieces[4] = {
    {{0x2484, 0x6484, 0x2485, 0x6485}},
    {{0x24ad, 0x6484, 0x2485, 0x6485}},
    {{0x24ad, 0x6484, 0x24ae, 0x6485}},
    {{0x24ad, 0x64ad, 0x24ae, 0x6485}},
  };
  if (cur_palace_index_x2 == 0xff) {
    for (int i = 0; i < 8; i++)
      dst[HUDXY(22 + i, 26)] = 0x24F5;
    Hud_DrawItem(dst + HUDXY(25, 27), &kHudItemHeartPieces[link_heart_pieces]);
  }
  Hud_DrawItem(dst + HUDXY(22, 23), &kHudItemSword[link_sword_type == 0xff ? 0 : link_sword_type]);
  Hud_DrawItem(dst + HUDXY(25, 23), &kHudItemShield[link_shield_type]);
  Hud_DrawItem(dst + HUDXY(28, 23), &kHudItemArmor[link_armor]);

  static const ItemBoxGfx kHudItemPalaceItem[2] = {
    {{0x28d6, 0x68d6, 0x28e6, 0x28e7}},
    {{0x354b, 0x354c, 0x354d, 0x354e}},
  };
  static const ItemBoxGfx kHudItemDungeonMap[1] = {
    {{0x28de, 0x28df, 0x28ee, 0x28ef}},
  };
  static const ItemBoxGfx kHudItemDungeonCompass[1] = {
    {{0x24bf, 0x64bf, 0x2ccf, 0x6ccf}},
  };
  if (cur_palace_index_x2 != 0xff &&
     (link_bigkey << (cur_palace_index_x2 >> 1)) & 0x8000) {
    Hud_DrawItem(dst + HUDXY(28, 27), &kHudItemPalaceItem[CheckPalaceItemPosession()]);
  }
  if (cur_palace_index_x2 != 0xff &&
     (link_dungeon_map << (cur_palace_index_x2 >> 1)) & 0x8000) {
    Hud_DrawItem(dst + HUDXY(22, 27), &kHudItemDungeonMap[0]);
  }
  if (cur_palace_index_x2 != 0xff &&
     (link_compass << (cur_palace_index_x2 >> 1)) & 0x8000) {
    Hud_DrawItem(dst + HUDXY(25, 27), &kHudItemDungeonCompass[0]);
  }
}

static void Hud_IntToDecimal(unsigned int number, uint8 *out) {  // 8df0f7
  out[0] = number / 1000 + 0x90;
  out[1] = (number %= 1000) / 100 + 0x90;
  out[2] = (number %= 100) / 10 + 0x90;
  out[3] = (number % 10) + 0x90;
}

bool Hud_RefillHealth() {  // 8df128
  if (link_health_current >= link_health_capacity) {
    link_health_current = link_health_capacity;
    link_hearts_filler = 0;
    return (is_doing_heart_animation == 0);
  }
  link_hearts_filler = 160;
  return false;
}

void Hud_AnimateHeartRefill() {  // 8df14f
  if (--animate_heart_refill_countdown)
    return;
  uint16 n = ((uint16)((link_health_current & ~7) - 1) >> 3) << 1;
  uint16 *p = hud_tile_indices_buffer + HUDXY(20, 1);
  if (n >= 20) {
    n -= 20;
    p += 0x20;
  }
  n &= 0xff;
  animate_heart_refill_countdown = 1;

  static const uint16 kAnimHeartPartial[4] = { 0x24A3, 0x24A4, 0x24A3, 0x24A0 };
  p[n >> 1] = kAnimHeartPartial[animate_heart_refill_countdown_subpos];

  animate_heart_refill_countdown_subpos = (animate_heart_refill_countdown_subpos + 1) & 3;
  if (!animate_heart_refill_countdown_subpos) {
    Hud_Rebuild();
    is_doing_heart_animation = 0;
  }
}

bool Hud_RefillMagicPower() {  // 8df1b3
  if (link_magic_power >= 0x80)
    return true;
  link_magic_filler = 0x80;
  return false;
}

void Hud_RestoreTorchBackground() {  // 8dfa33
  if ((!link_item_torch && !Alttpr_HasSewerLightCone()) || !dung_want_lights_out || hdr_dungeon_dark_with_lantern ||
      dung_num_lit_torches)
    return;
  hdr_dungeon_dark_with_lantern = 1;
  if (dung_hdr_bg2_properties != 2)
    TS_copy = 1;
}

void Hud_RebuildIndoor() {  // 8dfa60
  overworld_fixed_color_plusminus = 0;
  // rando-exempt: state-shuffle — 0xff sentinel marks "outside dungeon" key
  // count; the per-dungeon count restores on entrance load. (audit.md §0.2.2)
  link_num_keys = 0xff;
  Hud_Rebuild();
}

static void Hud_UpdateItemBox() {  // 8dfafd
  // Update r
  if (hud_cur_item)
    Hud_DrawItem(&hud_tile_indices_buffer[HUDXY(5, 1)], Hud_GetIconForItem(hud_cur_item));
}

static void Hud_UpdateHearts_Inner(uint16 *dst, const uint16 *src, int n) {  // 8dfdab
  int x = 0;
  while (n > 0) {
    if (x >= 10) {
      dst += 0x20;
      x = 0;
    }
    dst[x] = src[n >= 5 ? 2 : 1];
    x++;
    n -= 8;
  }
}

static void DrawHudComponents(uint16 *dst, const uint16 *src, int w, int h) {
  do {
    memcpy(dst, src, w * sizeof(uint16));
  } while (src += w, dst += 32, --h);
}

static void Hud_Update_Hearts() {  // 8dfb94
  static const uint16 kHudItemBoxTab1[] = { 0x24A2, 0x24A2, 0x24A2 };
  static const uint16 kHudItemBoxTab2[] = { 0x24A2, 0x24A1, 0x24A0 };
  // The life meter
  Hud_UpdateHearts_Inner(&hud_tile_indices_buffer[HUDXY(20, 1)], kHudItemBoxTab1, link_health_capacity);
  Hud_UpdateHearts_Inner(&hud_tile_indices_buffer[HUDXY(20, 1)], kHudItemBoxTab2, (link_health_current + 3) & ~3);
}

static void Hud_Update_Magic() {  // 8dfc09
  uint16 *dst = &hud_tile_indices_buffer[HUDXY(2, 0)];
  if (link_magic_consumption >= 1) {
    dst[HUDXY(0, 0)] = 0x28F7;  // "1"
    dst[HUDXY(1, 0)] = 0x2851;  // "/"
    dst[HUDXY(2, 0)] = 0x28FA;  // "2" (half) — gfx rewritten to "4" for quarter
    // Vanilla only ever had half magic ("1/2"). The randomizer adds QuarterMagic
    // (link_magic_consumption == 2) -> "1/4". Rather than borrow a mismatched
    // full-size digit, author size-matched glyphs by rewriting the two right
    // tiles' gfx IN PLACE: the "/" (char 0x51) with its "2"-merge column cleared,
    // and a small "4" over the "2" (char 0xFA) that preserves the magic-meter
    // pixels in the tile's right columns. Both chars are referenced only by this
    // header (verified) and quarter never shows "1/2", so the rewrite is local.
    // Gated on consumption==2 (rando-only) -> inert under vanilla side-by-side
    // RAM/VRAM compare. Mirrors the CopyTilesForSwitchLR custom-HUD-tile pattern.
    if (link_magic_consumption >= 2) {
      // The glyph field must be OPAQUE BLACK (palette-2 index 3), not transparent
      // (index 0): both are colour 0x0000, but index 0 reveals the HUD layer
      // behind the header — which made the borrowed glyphs look inconsistent
      // against the original "1/2" (whose field is index 3). These two tiles are
      // the original 0x51/0xFA with the "2"-merge / "2" strokes recoloured to a
      // small "4" on the SAME black field, meter pixels (0xFA cols 3-7) kept.
      static const uint16 kQuarterSlashTile[8] = {  // "/" — "2"-merge col -> black
        0x8282, 0xC745, 0xCF4B, 0xFF77, 0xFF6F, 0xFF5F, 0xFFBF, 0xFFFF,
      };
      static const uint16 kQuarterFourTile[8] = {   // small "4" on a black field
        0x0000, 0xC0C0, 0xE040, 0xE040, 0xE000, 0xE0C3, 0xE1C6, 0xF3D4,
      };
      memcpy(&g_zenv.vram[0x7000 + 0x51 * 8], kQuarterSlashTile, sizeof kQuarterSlashTile);
      memcpy(&g_zenv.vram[0x7000 + 0xFA * 8], kQuarterFourTile, sizeof kQuarterFourTile);
    }
  }
  const uint16 *src = kUpdateMagicPowerTilemap[(link_magic_power + 7) >> 3];
  dst[HUDXY(1, 1)] = src[0];
  dst[HUDXY(1, 2)] = src[1];
  dst[HUDXY(1, 3)] = src[2];
  dst[HUDXY(1, 4)] = src[3];
}

static void Hud_Update_Inventory() {  // 8dfc09
  uint8 d[4];
  Hud_IntToDecimal(link_rupees_actual, d);

  const uint16 base_tiles[2] = {
    0x2400,
    (enhanced_features0 & kFeatures0_ShowMaxItemsInYellow) ? 0x3400 : 0x2400,
  };

  int inv_offs = (d[0] == 0x90) ? 1 : 0;

  static const uint16 kHudInventoryBg[26] = {
    0x207f, 0x207f, 0x3ca8, 0x207f, 0x207f, 0x2c88, 0x2c89, 0x207f, 0x20a7, 0x20a9, 0x207f, 0x2871, 0x207f,
    0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f,
  };

  uint16 *dst = &hud_tile_indices_buffer[HUDXY(8, 0)];
  memcpy(dst + HUDXY(0, 0), kHudInventoryBg +  0 + inv_offs, 12 * sizeof(uint16));
  memcpy(dst + HUDXY(0, 1), kHudInventoryBg + 13 + inv_offs, 12 * sizeof(uint16));

  if (link_item_bow) {
    if (link_item_bow >= 3) {
      int arrow_header_x = 16 - inv_offs;
      hud_tile_indices_buffer[HUDXY(arrow_header_x, 0)] = 0x2486;
      hud_tile_indices_buffer[HUDXY(arrow_header_x + 1, 0)] = 0x2487;
      // rando-exempt: state-shuffle — bow tier swap on arrow-count change
      // (4=silvers-with-arrows, 3=silvers-no-arrows, 2=bow-with-arrows,
      // 1=bow-no-arrows). The bow item was already granted via dispatch;
      // this is HUD tier re-derivation. (audit.md §0.2.2)
      link_item_bow = link_num_arrows ? 4 : 3;
    } else {
      // rando-exempt: state-shuffle — same as the silvers branch above.
      link_item_bow = link_num_arrows ? 2 : 1;
    }
  }

  // Offset everything if we have many coins?
  int base_tile = base_tiles[link_rupees_actual == MaxRupees()];
  if (inv_offs == 0) {
    dst++;
    dst[HUDXY(-1, 1)] = base_tile | d[0];
  }
  dst[HUDXY(0, 1)] = base_tile | d[1];
  dst[HUDXY(1, 1)] = base_tile | d[2];
  dst[HUDXY(2, 1)] = base_tile | d[3];

  Hud_IntToDecimal(link_item_bombs, d);
  base_tile = base_tiles[link_item_bombs == Hud_MaxBombs()];
  dst[HUDXY(4, 1)] = base_tile | d[2];
  dst[HUDXY(5, 1)] = base_tile | d[3];

  Hud_IntToDecimal(link_num_arrows, d);
  base_tile = base_tiles[link_num_arrows == Hud_MaxArrows()];
  dst[HUDXY(7, 1)] = base_tile | d[2];
  dst[HUDXY(8, 1)] = base_tile | d[3];

  // Show keys
  d[3] = 0x7f;
  if (link_num_keys != 0xff)
    Hud_IntToDecimal(link_num_keys, d);
  dst[HUDXY(10, 1)] = 0x2400 | d[3];
  if (dst[HUDXY(10, 1)] == 0x247f)
    dst[HUDXY(10, 0)] = 0x247f;
}

void Hud_Rebuild() {  // 8dfa70
  // Ensure all of the 165 hud words are initialized.
  // This was broken by my previous reorg... quick fix for now.
  if (hud_tile_indices_buffer[HUDXY(8, 2)] == 0) {
    for (int i = 0; i < 165; i++)
      hud_tile_indices_buffer[i] = 0x207f;
  }


  // The magic meter and item box
  static const uint16 kHudTilemapLeftPart[8 * 6] = {
    0x207f, 0x207f, 0x2850, 0xa856, 0x2852, 0x285b, 0x285b, 0x285c,
    0x207f, 0x207f, 0x2854, 0x2871, 0x2858, 0x207f, 0x207f, 0x285d,
    0x207f, 0x207f, 0x2854, 0x304e, 0x2858, 0x207f, 0x207f, 0x285d,
    0x207f, 0x207f, 0x2854, 0x305e, 0x2859, 0xa85b, 0xa85b, 0xa85c,
    0x207f, 0x207f, 0x2854, 0x305e, 0x6854, 0x207f, 0x207f, 0x207f,
    0x207f, 0x207f, 0xa850, 0x2856, 0xe850, 
  };
  DrawHudComponents(hud_tile_indices_buffer, kHudTilemapLeftPart, 8, 6);

  static const uint16 kHudTilemapRightPart[12 * 5] = {
    0x207f, 0x207f, 0x288b, 0x288f, 0x24ab, 0x24ac, 0x688f, 0x688b, 0x207f, 0x207f, 0x207f, 0x207f,
    0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f,
    0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f,
    0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f,
    0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f, 0x207f,
  };
  DrawHudComponents(&hud_tile_indices_buffer[HUDXY(20, 0)], kHudTilemapRightPart, 12, 5);
  // VIES rather than LIFE above the hearts: the top row of the right part of
  // the cartridge's HUD tilemap ($8D:FE5F, 32 words a row; $8D:FE77 in the
  // USA one and in the translations built from it, VIDA, VIE, LEBEN...).
  if (g_loc_rom)
    Loc_Words(&hud_tile_indices_buffer[HUDXY(20, 0)], 0x8dfe5f + 20 * 2, 12);
  else if (LanguageCart_Active())
    LanguageCart_Words(&hud_tile_indices_buffer[HUDXY(20, 0)], 0x8dfe77 + 20 * 2, 12);


  Hud_Update_Hearts();
  Hud_Update_Magic();
  Hud_Update_Inventory();
  Hud_UpdateItemBox();
  flag_update_hud_in_nmi++;
}


const uint16 *Hud_GetItemBoxPtr(int item) {
  return kHudItemBoxGfxPtrs[item]->v;
}

void Hud_HandleItemSwitchInputs() {
  if (!(enhanced_features0 & kFeatures0_SwitchLR))
    return;
  
  bool direction;
  
  if (filtered_joypad_L & kJoypadL_L && (hud_cur_item_l == 0))
    direction = (hud_cur_item_r != 0);
  else if (filtered_joypad_L & kJoypadL_R && (hud_cur_item_r == 0))
    direction = true;
  else
    return;

  uint8 item = hud_cur_item;
  for (int i = 0; i < kHudItemCount; i++) {
    if (!direction)
      Hud_GotoPrevItem(&item, 1);
    else
      Hud_GotoNextItem(&item, 1);
    if (Hud_DoWeHaveThisItem(item) && (!(enhanced_features0 & kFeatures0_SwitchLRLimit) || Hud_GetItemPosition(item) <= 3)) {
      if (item != hud_cur_item) {
        hud_cur_item = item;
        sound_effect_2 = 32;
        Hud_UpdateEquippedItem();
        Hud_UpdateItemBox();
        flag_update_hud_in_nmi++;
      }
      break;
    }
  }
}

static void Hud_ReorderItem(int direction) {
  // Initialize inventory order on first use
  if (hud_inventory_order[0] == 0) {
    for (int i = 0; i < 24; i++)
      hud_inventory_order[i] = i + 1;
  }
  int old_pos = Hud_GetItemPosition(hud_cur_item), new_pos = old_pos + direction;
  if (new_pos < 0)
    new_pos += kHudItemCount;
  else if (new_pos >= kHudItemCount)
    new_pos -= kHudItemCount;
  uint8 t = hud_inventory_order[old_pos];
  hud_inventory_order[old_pos] = hud_inventory_order[new_pos];
  hud_inventory_order[new_pos] = t;
  Hud_DrawYButtonItems();
  sound_effect_2 = 32;
}

// ===========================================================================
// Randomizer in-game OAM overlays.
//
// These overlays draw via OAM sprites so they composite over live gameplay on
// every renderer backend (the in-game HUD strip is a static BG tilemap and
// cannot host a fixed overlay that doesn't scroll). The glyph graphics are a
// tiny self-contained 4bpp font written over borrowed sprite VRAM each frame,
// so the overlay never depends on which area's sprite gfx subset happens to be
// loaded.
//
// Borrowed VRAM: per-area sprite gfx subsets fill VRAM 0x5000..0x5fff (four
// 0x400-word subsets — 64 tiles x 16 words — at 0x5000/0x5400/0x5800/0x5c00;
// see load_gfx.c InitializeTilesets + Do3To4Low/High). objTileAdr2 (PPU obj
// name-select bit, OAM flag 0x01) charnums 0xF0..0xFF = VRAM 0x5f00..0x5fff
// are therefore the LAST ROW of the live subset-3 sheet (e.g. the vanilla
// cucco's bottom tiles land at chars 0xFA/0xFB), NOT free space. We rewrite
// the glyphs there every frame (an area reload can't leave us pointing at
// stale tiles) and mark the scratch slots dirty via
// Rando_ObjScratchMarkSlotsDirty so the sheet row is restored the moment a
// visible sprite needs those chars (Rando_ObjScratchRestoreVisible).
//
// The tracker visibility flags (g_rando_show_item_tracker /
// g_rando_show_location_tracker) are owned by rando.c (declared in
// rando/rando.h); this file only reads them.
//
// Location-tracker status now reads the live checked-location bitmap
// (Rando_IsLocationChecked) for a have/not tri-state, so a placed-but-checked
// location renders the filled glyph and unchecked ones the neutral ring.
// ===========================================================================

// Charnums (in objTileAdr2 space; OAM flag bit 0 selects that base).
enum {
  kRandoTrkTile_Base   = 0xF0,  // first scratch charnum
  kRandoTrkGlyph_Blank = 0,     // empty / not-have
  kRandoTrkGlyph_Dot,           // small centered dot  ("." / unchecked-reachable)
  kRandoTrkGlyph_Ring,          // hollow square        ("?" / unknown)
  kRandoTrkGlyph_Full,          // filled square        ("*" / checked / have)
  kRandoTrkGlyph_One,           // single bar           (level 1 / minor have)
  kRandoTrkGlyph_Two,           // double bar           (level 2)
  kRandoTrkGlyph_Three,         // triple bar           (level 3+)
  kRandoTrkGlyph_Count,
};

// 8x8 glyph bitmaps, 1 bit per pixel (bit 7 = leftmost). Rendered into the
// low bitplane of a 4bpp sprite tile; a per-glyph palette-index nibble lets us
// pick a readable color. Shape is the primary (colorblind-safe) signal.
typedef struct RandoTrkGlyph { uint8 rows[8]; uint8 color; } RandoTrkGlyph;

static const RandoTrkGlyph kRandoTrkGlyphs[kRandoTrkGlyph_Count] = {
  [kRandoTrkGlyph_Blank] = { {0,0,0,0,0,0,0,0}, 0 },
  [kRandoTrkGlyph_Dot]   = { {0x00,0x00,0x18,0x3c,0x3c,0x18,0x00,0x00}, 0x9 },
  [kRandoTrkGlyph_Ring]  = { {0x00,0x7e,0x42,0x42,0x42,0x42,0x7e,0x00}, 0x8 },
  [kRandoTrkGlyph_Full]  = { {0x00,0x7e,0x7e,0x7e,0x7e,0x7e,0x7e,0x00}, 0x6 },
  [kRandoTrkGlyph_One]   = { {0x00,0x00,0x00,0x7e,0x00,0x00,0x00,0x00}, 0x6 },
  [kRandoTrkGlyph_Two]   = { {0x00,0x00,0x7e,0x00,0x7e,0x00,0x00,0x00}, 0x6 },
  [kRandoTrkGlyph_Three] = { {0x00,0x7e,0x00,0x7e,0x00,0x7e,0x00,0x00}, 0x6 },
};

// Write the glyph font into scratch sprite VRAM. Each glyph is a 4bpp tile:
// addr[r] holds bitplanes 0 (low byte) + 1 (high byte); addr[r+8] holds
// planes 2+3. We encode each set pixel with the glyph's 4-bit palette index so
// the PPU produces a single readable color per glyph (see ppu.c obj fetch).
static void Hud_RandoUploadGlyphs(void) {
  for (int g = 0; g < kRandoTrkGlyph_Count; g++) {
    const RandoTrkGlyph *gl = &kRandoTrkGlyphs[g];
    uint16 *tile = &g_zenv.vram[0x5000 + (kRandoTrkTile_Base + g) * 16];
    uint8 c = gl->color;
    for (int r = 0; r < 8; r++) {
      uint8 m = gl->rows[r];
      uint16 p01 = 0, p23 = 0;
      if (c & 1) p01 |= m;          // plane 0
      if (c & 2) p01 |= (uint16)m << 8;  // plane 1
      if (c & 4) p23 |= m;          // plane 2
      if (c & 8) p23 |= (uint16)m << 8;  // plane 3
      tile[r] = p01;
      tile[r + 8] = p23;
    }
  }
  // The seven tracker glyphs occupy F0..F6 (scratch slots 0 and 1). Record
  // that ownership so vanilla art can be restored if a later frame needs it.
  Rando_ObjScratchMarkSlotsDirty(0x03);
}

// Find the next free OAM slot at or below |*slot|, scanning downward. A slot
// is free when the game left it hidden (y == 0xf0) this frame — ClearOamBuffer
// hides all 128 entries at frame start and the game only un-hides the slots it
// actually uses, so claiming hidden slots never occludes a live sprite.
// Returns -1 when none remain.
static int Hud_RandoNextFreeSlot(int *slot) {
  while (*slot >= 0) {
    int s = (*slot)--;
    if (oam_buf[s].y == 0xf0)
      return s;
  }
  return -1;
}

// Place one overlay glyph at HUD pixel (px, py) into the next free OAM slot.
// palette is the sprite sub-palette index (0..7). Returns false (and draws
// nothing) when the slot budget is exhausted.
static bool Hud_RandoDrawGlyph(int *slot, uint8 px, uint8 py, int glyph, uint8 palette) {
  if (glyph <= kRandoTrkGlyph_Blank)
    return *slot >= 0;  // nothing to draw, but keep going
  int s = Hud_RandoNextFreeSlot(slot);
  if (s < 0)
    return false;
  uint8 flags = 0x01                 // bit0: select objTileAdr2 (scratch base)
              | ((palette & 7) << 1) // bits1-3: palette
              | 0x30;                // bits4-5: priority (in front of BG)
  SetOamPlain(&oam_buf[s], px, py, (uint8)(kRandoTrkTile_Base + glyph), flags, 0 /*8x8*/);
  return true;
}

// True only during normal interactive gameplay (overworld / dungeon) with no
// submodule active. Text boxes, the inventory menu, map screens, and screen
// transitions all push submodule_index != 0, which suppresses the overlay
// (the dialogue-box hide rule, generalized).
static bool Hud_RandoTrackerVisibleNow(void) {
  if (!(enhanced_features1 & kFeatures1_RandomizerActive))
    return false;
  if (main_module_index != 0x07 && main_module_index != 0x09)
    return false;
  if (submodule_index != 0)
    return false;
  return true;
}

bool Hud_RandoOamTrackerWillDrawThisFrame(void) {
#ifdef Z3R_NATIVE_SETTINGS_WINDOW
  return false;
#else
  return Hud_RandoTrackerVisibleNow() &&
         (g_rando_show_item_tracker || g_rando_show_location_tracker);
#endif
}

// ---- Item tracker --------------------------------------------------------
// A compact grid, anchored top-left, showing have/level for the progressive
// and absolute items plus bottles, magic, and the heart count. Each cell is a
// single overlay glyph: blank = don't have, full = have (binary item),
// one/two/three bars = level for tiered items. Reads inventory RAM directly.

typedef struct RandoTrkItemCell { const char *label; uint8 kind; uint16 addr; } RandoTrkItemCell;
enum { kRTI_Bool = 0, kRTI_Level, kRTI_Bottles, kRTI_Hearts };

static void Hud_RandoDrawItemTrackerInner(int *slot) {
  Hud_RandoUploadGlyphs();

  // (kind, RAM byte): binary items render full/blank; tiered items render a
  // bar count clamped to 3; bottles count non-empty slots; hearts shows tier.
  static const RandoTrkItemCell kCells[] = {
    { 0, kRTI_Level,   0xF340 },  // bow (0..4)
    { 0, kRTI_Level,   0xF341 },  // boomerang
    { 0, kRTI_Bool,    0xF342 },  // hookshot
    { 0, kRTI_Bool,    0xF343 },  // bombs (count, treat as have)
    { 0, kRTI_Bool,    0xF345 },  // fire rod
    { 0, kRTI_Bool,    0xF346 },  // ice rod
    { 0, kRTI_Bool,    0xF347 },  // bombos
    { 0, kRTI_Bool,    0xF348 },  // ether
    { 0, kRTI_Bool,    0xF349 },  // quake
    { 0, kRTI_Level,   0xF34A },  // lamp/torch
    { 0, kRTI_Bool,    0xF34B },  // hammer
    { 0, kRTI_Level,   0xF34C },  // flute (0..3)
    { 0, kRTI_Bool,    0xF34D },  // bug net
    { 0, kRTI_Bool,    0xF34E },  // book of mudora
    { 0, kRTI_Bool,    0xF350 },  // cane of somaria
    { 0, kRTI_Bool,    0xF351 },  // cane of byrna
    { 0, kRTI_Bool,    0xF352 },  // cape
    { 0, kRTI_Bool,    0xF353 },  // magic mirror
    { 0, kRTI_Level,   0xF354 },  // gloves (1=power,2=titan)
    { 0, kRTI_Level,   0xF355 },  // boots
    { 0, kRTI_Bool,    0xF356 },  // flippers
    { 0, kRTI_Bool,    0xF357 },  // moon pearl
    { 0, kRTI_Level,   0xF359 },  // sword (1..4; 0xff = none)
    { 0, kRTI_Level,   0xF35A },  // shield (1..3)
    { 0, kRTI_Level,   0xF35B },  // armor (0..2)
    { 0, kRTI_Bottles, 0 },       // bottles (count non-empty)
    { 0, kRTI_Hearts,  0 },       // hearts (capacity tier)
  };
  enum { kCols = 7, kCellPx = 9, kOriginX = 16, kOriginY = 16 };

  int n = (int)(sizeof(kCells) / sizeof(kCells[0]));
  for (int i = 0; i < n; i++) {
    const RandoTrkItemCell *c = &kCells[i];
    uint8 px = (uint8)(kOriginX + (i % kCols) * kCellPx);
    uint8 py = (uint8)(kOriginY + (i / kCols) * kCellPx);
    int glyph = kRandoTrkGlyph_Blank;
    switch (c->kind) {
    case kRTI_Bool:
      glyph = g_ram[c->addr] ? kRandoTrkGlyph_Full : kRandoTrkGlyph_Blank;
      break;
    case kRTI_Level: {
      uint8 v = g_ram[c->addr];
      if (v == 0 || v == 0xff) glyph = kRandoTrkGlyph_Blank;
      else if (v == 1)         glyph = kRandoTrkGlyph_One;
      else if (v == 2)         glyph = kRandoTrkGlyph_Two;
      else                     glyph = kRandoTrkGlyph_Three;
      break;
    }
    case kRTI_Bottles: {
      int cnt = 0;
      for (int b = 0; b < 4; b++) if (link_bottle_info[b]) cnt++;
      glyph = cnt == 0 ? kRandoTrkGlyph_Blank
            : cnt == 1 ? kRandoTrkGlyph_One
            : cnt == 2 ? kRandoTrkGlyph_Two
                       : kRandoTrkGlyph_Three;
      break;
    }
    case kRTI_Hearts: {
      // link_health_capacity is hearts*8; show a coarse tier.
      int hearts = link_health_capacity >> 3;
      glyph = hearts >= 16 ? kRandoTrkGlyph_Three
            : hearts >= 10 ? kRandoTrkGlyph_Two
                           : kRandoTrkGlyph_One;
      break;
    }
    }
    uint8 pal = (glyph == kRandoTrkGlyph_Full || glyph == kRandoTrkGlyph_One ||
                 glyph == kRandoTrkGlyph_Two  || glyph == kRandoTrkGlyph_Three) ? 6 : 4;
    if (!Hud_RandoDrawGlyph(slot, px, py, glyph, pal))
      break;
  }
}

void Hud_RandoDrawItemTracker(void) {
  int slot = 127;
  Hud_RandoDrawItemTrackerInner(&slot);
}

// ---- Location tracker ----------------------------------------------------
// Iterate the active placement table, group by region (matching the spoiler's
// region grouping via kRandoLocations[].region_id), and render a status glyph
// per location: filled when the location has been checked
// (Rando_IsLocationChecked), neutral ring when still outstanding. New rows are
// started on each region change so locations group visually.

static void Hud_RandoDrawLocationTrackerInner(int *slot) {
  Hud_RandoUploadGlyphs();

  const RandoPlacementTable *t = Placement_GetActive();
  if (t == NULL || t->count == 0)
    return;

  // kMaxY bounds the grid so a full ~266-location seed can't drive `py` past
  // the bottom of the screen (it would wrap the uint8 and overlap the HUD).
  // `rows` is monotonic, so once a row clears kMaxY every later row does too.
  enum { kCols = 8, kCellPx = 8, kOriginX = 256 - 8 - (kCols - 1) * 8, kOriginY = 16, kMaxY = 200 };
  uint16 prev_region = 0xFFFF;
  int col = 0, rows = 0;

  for (int i = 0; i < t->count && *slot >= 0; i++) {
    uint16 loc = t->entries[i].location_id;

    // Resolve the location's region for grouping (and its type, to hide pots).
    uint16 region = 0xFFFF;
    uint8 ltype = 0xFF;
    for (uint32 k = 0; k < kRandoLocationsCount; k++) {
      if (kRandoLocations[k].id == loc) {
        region = kRandoLocations[k].region_id;
        ltype = kRandoLocations[k].type;
        break;
      }
    }

    // add-rando-pot-sanity — pot_shuffle=All adds ~800 pot locations, which swamp
    // this 8px quick-glance grid and push the real checks off-screen. Hide pots
    // here (they live in the native check tracker instead). Skip BEFORE the
    // region/row/column bookkeeping so pots are fully invisible to the layout.
    // add-rando-grass-rock-shuffle — same for the ~3900 terrain checks.
    if (ltype == LOCTYPE_Pot || ltype == LOCTYPE_Grass || ltype == LOCTYPE_Rock)
      continue;

    // Start a new row when the region changes (groups locations visually).
    if (region != prev_region) {
      if (prev_region != 0xFFFF) rows++;
      prev_region = region;
      col = 0;
    }
    if (col >= kCols) { rows++; col = 0; }

    // Stop before the grid runs off the bottom of the screen (uint8 wrap).
    if (kOriginY + rows * kCellPx > kMaxY)
      break;

    uint8 px = (uint8)(kOriginX + col * kCellPx);
    uint8 py = (uint8)(kOriginY + rows * kCellPx);

    // Checked locations render the filled glyph; outstanding ones the ring.
    bool checked = Rando_IsLocationChecked(loc);
    int glyph = checked ? kRandoTrkGlyph_Full : kRandoTrkGlyph_Ring;
    uint8 pal = checked ? 6 : 4;
    if (!Hud_RandoDrawGlyph(slot, px, py, glyph, pal))
      break;
    col++;
  }
}

void Hud_RandoDrawLocationTracker(void) {
  int slot = 127;
  Hud_RandoDrawLocationTrackerInner(&slot);
}

// Decode one 16x16 item icon (a 2x2 quad of SNES tilemap words) into atlas slot
// `aslot` of `out` (an atlas `stride` px wide). Tiles come from `scratch` (the
// HUD 2bpp packs, char c at scratch[c*8+row]); colors from `pal` (BGR555, the
// 32-entry HUD palette, sub-palette = bits 10-12 of each word). Transparent
// pixels (index 0) are skipped.
static void DecodeIconQuad(uint32 *out, int stride, const uint16 *scratch,
                           int scratch_words, const uint16 *pal,
                           int aslot, const ItemBoxGfx *icon) {
  for (int q = 0; q < 4; q++) {
    uint16 w = icon->v[q];
    int chr = w & 0x3ff, subpal = (w >> 10) & 7;
    bool hflip = (w & 0x4000) != 0, vflip = (w & 0x8000) != 0;
    int qx = (q & 1) * 8, qy = (q >= 2) ? 8 : 0;
    for (int r = 0; r < 8; r++) {
      int srow = vflip ? (7 - r) : r;
      if (chr * 8 + srow >= scratch_words) continue;
      uint16 word = scratch[chr * 8 + srow];
      for (int cc = 0; cc < 8; cc++) {
        int pix = hflip ? (((word >> cc) & 1) | ((word >> (7 + cc)) & 2))
                        : (((word >> (7 - cc)) & 1) | ((word >> (14 - cc)) & 2));
        if (pix == 0) continue;
        uint16 col = pal[subpal * 4 + pix];
        uint32 rgba = (uint32)((col & 0x1f) << 3) | ((uint32)(((col >> 5) & 0x1f) << 3) << 8) |
                      ((uint32)(((col >> 10) & 0x1f) << 3) << 16) | 0xff000000u;
        out[(qy + r) * stride + aslot * kRandoIconSize + (qx + cc)] = rgba;
      }
    }
  }
}

// Like DecodeIconQuad, but treats a palette entry of 0 as transparent (skip).
// The quest-screen pendant tiles fill their whole cell with a background pixel
// (not pix 0), so the synthesized pendant palette sets that entry to 0 to drop
// the box fill and leave a clean transparent-background gem.
static void DecodePendantQuad(uint32 *out, int stride, const uint16 *scratch,
                              int scratch_words, const uint16 *pal,
                              int aslot, const ItemBoxGfx *icon) {
  for (int q = 0; q < 4; q++) {
    uint16 w = icon->v[q];
    int chr = w & 0x3ff, subpal = (w >> 10) & 7;
    bool hflip = (w & 0x4000) != 0, vflip = (w & 0x8000) != 0;
    int qx = (q & 1) * 8, qy = (q >= 2) ? 8 : 0;
    for (int r = 0; r < 8; r++) {
      int srow = vflip ? (7 - r) : r;
      if (chr * 8 + srow >= scratch_words) continue;
      uint16 word = scratch[chr * 8 + srow];
      for (int cc = 0; cc < 8; cc++) {
        int pix = hflip ? (((word >> cc) & 1) | ((word >> (7 + cc)) & 2))
                        : (((word >> (7 - cc)) & 1) | ((word >> (14 - cc)) & 2));
        if (pix == 0) continue;
        uint16 col = pal[subpal * 4 + pix];
        if (col == 0) continue;  // background sentinel -> transparent
        uint32 rgba = (uint32)((col & 0x1f) << 3) | ((uint32)(((col >> 5) & 0x1f) << 3) << 8) |
                      ((uint32)(((col >> 10) & 0x1f) << 3) << 16) | 0xff000000u;
        out[(qy + r) * stride + aslot * kRandoIconSize + (qx + cc)] = rgba;
      }
    }
  }
}

// Decode a single 8x8 2bpp HUD tile (tilemap word `w`) 2x-scaled into the
// 16x16 atlas slot `aslot`. For icons that are one HUD tile (the full heart).
static void DecodeIconTile2x(uint32 *out, int stride, const uint16 *scratch,
                             int scratch_words, const uint16 *pal, int aslot, uint16 w) {
  int chr = w & 0x3ff, subpal = (w >> 10) & 7;
  bool hflip = (w & 0x4000) != 0, vflip = (w & 0x8000) != 0;
  for (int r = 0; r < 8; r++) {
    int srow = vflip ? (7 - r) : r;
    if (chr * 8 + srow >= scratch_words) continue;
    uint16 word = scratch[chr * 8 + srow];
    for (int cc = 0; cc < 8; cc++) {
      int pix = hflip ? (((word >> cc) & 1) | ((word >> (7 + cc)) & 2))
                      : (((word >> (7 - cc)) & 1) | ((word >> (14 - cc)) & 2));
      if (pix == 0) continue;
      uint16 col = pal[subpal * 4 + pix];
      uint32 rgba = (uint32)((col & 0x1f) << 3) | ((uint32)(((col >> 5) & 0x1f) << 3) << 8) |
                    ((uint32)(((col >> 10) & 0x1f) << 3) << 16) | 0xff000000u;
      for (int dy = 0; dy < 2; dy++)
        for (int dx = 0; dx < 2; dx++)
          out[(r * 2 + dy) * stride + aslot * kRandoIconSize + (cc * 2 + dx)] = rgba;
    }
  }
}

// Decode the in-game crystal HUD art (2 horizontal 8x8 2bpp tiles = 16x8),
// vertically centered in the 16x16 atlas slot. Same reliable `scratch` HUD-tile
// path as DecodePendantQuad (palette entry 0 -> transparent) so the tracker
// crystal matches Hud_DrawProgressIcons_Crystals, not the state-dependent
// sprite-gfx path that left a garbage blob.
static void DecodeCrystalHud(uint32 *out, int stride, const uint16 *scratch,
                             int scratch_words, const uint16 *pal,
                             int aslot, const ItemBoxGfx *icon) {
  // The HUD crystal is a small 16x8 gem; render it 2x and clip so the centered
  // diamond fills the 16x16 slot (comparable to the 16x16 pendant icons).
  for (int q = 0; q < 2; q++) {
    uint16 w = icon->v[q];
    int chr = w & 0x3ff, subpal = (w >> 10) & 7;
    bool hflip = (w & 0x4000) != 0, vflip = (w & 0x8000) != 0;
    int qx = q * 8;
    for (int r = 0; r < 8; r++) {
      int srow = vflip ? (7 - r) : r;
      if (chr * 8 + srow >= scratch_words) continue;
      uint16 word = scratch[chr * 8 + srow];
      for (int cc = 0; cc < 8; cc++) {
        int pix = hflip ? (((word >> cc) & 1) | ((word >> (7 + cc)) & 2))
                        : (((word >> (7 - cc)) & 1) | ((word >> (14 - cc)) & 2));
        if (pix == 0) continue;
        uint16 col = pal[subpal * 4 + pix];
        if (col == 0) continue;
        uint32 rgba = (uint32)((col & 0x1f) << 3) | ((uint32)(((col >> 5) & 0x1f) << 3) << 8) |
                      ((uint32)(((col >> 10) & 0x1f) << 3) << 16) | 0xff000000u;
        int sx = (qx + cc) * 2 - 8;  // 2x; center the gem horizontally
        int sy = r * 2;              // 2x; fills the 16px slot height
        for (int dy = 0; dy < 2; dy++)
          for (int dx = 0; dx < 2; dx++) {
            int px2 = sx + dx, py2 = sy + dy;
            if (px2 < 0 || px2 >= kRandoIconSize || py2 >= kRandoIconSize) continue;
            out[py2 * stride + aslot * kRandoIconSize + px2] = rgba;
          }
      }
    }
  }
}

// Dungeon-item HUD icons (not in kHudItemBoxGfxPtrs — these are drawn directly
// by the dungeon HUD, hud.c Hud_UpdateItemBox). Big key = kHudItemPalaceItem[0],
// map = kHudItemDungeonMap, compass = kHudItemDungeonCompass.
static const ItemBoxGfx kRandoBigKeyIcon  = {{0x28d6, 0x68d6, 0x28e6, 0x28e7}};
static const ItemBoxGfx kRandoMapIcon     = {{0x28de, 0x28df, 0x28ee, 0x28ef}};
static const ItemBoxGfx kRandoCompassIcon = {{0x24bf, 0x64bf, 0x2ccf, 0x6ccf}};
// Full heart = the life-meter FILLED-heart tile (Hud_Update_Hearts kHudItemBoxTab2
// full entry = 0x24A0; 0x24A2 is the empty outline, 0x24A1 the half), 2x-scaled
// to 16x16 — a clean filled heart, not a heart-piece graphic.
#define kRandoFullHeartTile 0x24A0

int Hud_RandoBuildIconAtlas(uint32 *out) {
  // Decompress the three HUD 2bpp packs into a scratch char buffer laid out
  // exactly like VRAM 0x7000/0x7400/0x7800 (so char c -> scratch[c*8 + row]).
  static uint16 scratch[0xC00];  // chars 0..0x17f (the HUD icon range)
  memset(scratch, 0, sizeof(scratch));
  DecompAndUpload2bpp(&scratch[0x000], 0x6a);
  DecompAndUpload2bpp(&scratch[0x400], 0x6b);
  DecompAndUpload2bpp(&scratch[0x800], 0x69);
  const uint16 *pal = kHudPalData;  // 32 colors at offset 0 (hud_palette == 0)
  if (pal == NULL) return 0;

  const int stride = kRandoIconCount * kRandoIconSize;  // atlas width in pixels
  memset(out, 0, (size_t)stride * kRandoIconSize * 4);
  int n = (int)(sizeof(kHudItemBoxGfxPtrs) / sizeof(kHudItemBoxGfxPtrs[0]));
  if (n > kRandoIconCount) n = kRandoIconCount;
  for (int slot = 0; slot < n; slot++) {
    // kHudItemEmpty is a single-element array (no [1] tier) and is never drawn —
    // skip it so we don't read past the object (audit R2 MED).
    if (slot == kRandoIcon_Empty) continue;
    // Most items: [0]=empty placeholder, [1]=the "have" icon. Exceptions:
    //   Armor has NO empty slot — [0]=green, [1]=blue, [2]=red (use green base).
    //   Flute's [1] is the SHOVEL; [2] is the actual flute.
    int tier = 1;
    if (slot == kRandoIcon_Armor) tier = 0;        // [0]=green base ([1]=blue,[2]=red)
    else if (slot == kRandoIcon_Flute) tier = 2;   // [1]=shovel, [2]=flute
    else if (slot == kRandoIcon_Mirror) tier = 2;  // [1]=scroll, [2]=the mirror
    else if (slot == kRandoIcon_Bottle) tier = 2;  // [1]=shared mushroom tile, [2]=bottle
    DecodeIconQuad(out, stride, scratch, (int)(sizeof(scratch) / sizeof(scratch[0])),
                   pal, slot, &kHudItemBoxGfxPtrs[slot][tier]);
  }
  // Dungeon-item icons into the spare atlas slots (big key / map / compass).
  int sw = (int)(sizeof(scratch) / sizeof(scratch[0]));
  DecodeIconQuad(out, stride, scratch, sw, pal, kRandoIcon_BigKey, &kRandoBigKeyIcon);
  DecodeIconQuad(out, stride, scratch, sw, pal, kRandoIcon_Map, &kRandoMapIcon);
  DecodeIconQuad(out, stride, scratch, sw, pal, kRandoIcon_Compass, &kRandoCompassIcon);
  DecodeIconQuad(out, stride, scratch, sw, pal, kRandoIcon_Shovel, &kHudItemFlute[1]);
  // Magic Powder: the mushroom item box renders the powder when its value is 2,
  // so its tiles live at kHudItemMushroom[2] ([1] is the mushroom itself).
  DecodeIconQuad(out, stride, scratch, sw, pal, kRandoIcon_Powder, &kHudItemMushroom[2]);
  // Colour/tier variants so the tracker can show the upgraded sprite (the main
  // loop above renders boomerang/gloves at tier 1 and armor at tier 0=green).
  DecodeIconQuad(out, stride, scratch, sw, pal, kRandoIcon_BoomerangRed, &kHudItemBoomerang[2]);
  DecodeIconQuad(out, stride, scratch, sw, pal, kRandoIcon_GlovesTitan,  &kHudItemGloves[2]);
  DecodeIconQuad(out, stride, scratch, sw, pal, kRandoIcon_ArmorBlue,    &kHudItemArmor[1]);
  DecodeIconQuad(out, stride, scratch, sw, pal, kRandoIcon_ArmorRed,     &kHudItemArmor[2]);
  DecodeIconQuad(out, stride, scratch, sw, pal, kRandoIcon_SwordMaster,  &kHudItemSword[2]);
  DecodeIconQuad(out, stride, scratch, sw, pal, kRandoIcon_SwordTempered, &kHudItemSword[3]);
  DecodeIconQuad(out, stride, scratch, sw, pal, kRandoIcon_SwordGold,    &kHudItemSword[4]);
  DecodeIconQuad(out, stride, scratch, sw, pal, kRandoIcon_ShieldRed,    &kHudItemShield[2]);
  DecodeIconQuad(out, stride, scratch, sw, pal, kRandoIcon_ShieldMirror, &kHudItemShield[3]);
  DecodeIconQuad(out, stride, scratch, sw, pal, kRandoIcon_BowSilver,    &kHudItemBowRandoSilverArrows);
  DecodeIconTile2x(out, stride, scratch, sw, pal, kRandoIcon_Heart, kRandoFullHeartTile);
  // Pendant icons: reuse the SAME static HUD artwork the in-game progress screen
  // draws (Hud_DrawProgressIcons_Pendants): one template gem (chars 0x12b-0x12e
  // in HUD pack 0x69) recolored per hue by the tilemap palette nibble - green =
  // sub-palette 1 (0x25xx), blue = 3 (0x2dxx), red = 7 (0x3dxx). Decode them via
  // the same DecodeIconQuad path as big-key/map/compass (4 quadrant tiles, real
  // HUD palette) to get a proper gem. The old gfx-0x23 path blitted only the
  // TOP-LEFT quarter of the 16x16 receive sprite at 2x (WriteTo4BPPBuffer_at_
  // 7F4000 emits 4 tiles 0xbd40/60/80/a0; it read only 0xbd40) with a
  // hand-synthesized 3-index palette - a colored "blob".
  static const ItemBoxGfx kPendantGreenIcon = {{0x252b, 0x252c, 0x252d, 0x252e}};
  static const ItemBoxGfx kPendantBlueIcon  = {{0x2d2b, 0x2d2c, 0x2d2d, 0x2d2e}};
  static const ItemBoxGfx kPendantRedIcon   = {{0x3d2b, 0x3d2c, 0x3d2d, 0x3d2e}};
  // kHudPalData (gameplay HUD palette) only carries CGRAM row 0; the pendant
  // colors live at the tiles' palette nibbles 1/3/7, which the quest screen
  // loads into CGRAM at draw time. Borrowing kHudPalData washes the gems white
  // and swaps green<->red. Synthesize the pendant hues at sub-palettes 1 (green)
  // / 3 (blue) / 7 (red): pix 1/2/3 = dark / body / highlight of each hue.
  static uint16 pendpal[32];
  memset(pendpal, 0, sizeof pendpal);
#define Z3R_PEND_BGR(r, g, b) ((uint16)((r) | ((g) << 5) | ((b) << 10)))
  // pix 3 = the tile's background fill -> 0 (transparent); pix 1/2 = gem shades.
  // pix 1 = gem (hue), pix 2 = the pendant outline (WHITE — the quest-screen
  // white lines), pix 3 = background fill (transparent). Matches the pause menu.
  pendpal[5]  = Z3R_PEND_BGR(6, 26, 6);  pendpal[6]  = Z3R_PEND_BGR(31, 31, 31); pendpal[7]  = 0;
  pendpal[13] = Z3R_PEND_BGR(6, 12, 30); pendpal[14] = Z3R_PEND_BGR(31, 31, 31); pendpal[15] = 0;
  pendpal[29] = Z3R_PEND_BGR(31, 6, 6);  pendpal[30] = Z3R_PEND_BGR(31, 31, 31); pendpal[31] = 0;
#undef Z3R_PEND_BGR
  DecodePendantQuad(out, stride, scratch, sw, pendpal, kRandoIcon_PendantGreen, &kPendantGreenIcon);
  DecodePendantQuad(out, stride, scratch, sw, pendpal, kRandoIcon_PendantBlue,  &kPendantBlueIcon);
  DecodePendantQuad(out, stride, scratch, sw, pendpal, kRandoIcon_PendantRed,   &kPendantRedIcon);
  // Crystal: reuse the in-game pause-menu crystal HUD art (chars 0x144/0x145 in
  // HUD pack 0x69 -- the filled-crystal tiles Hud_DrawProgressIcons_Crystals
  // draws as 0x2D44/0x2D45) so the tracker matches the real game. The old
  // gfx-0x28 sprite path read the crystal sprite buffer, which the atlas build
  // never populates (no crystal-receipt context), so it indexed past the
  // synthesized palette into out-of-bounds garbage (a brown blob).
  // Blue hue synthesized like the pendants (sub-palette 1).
  {
    static uint16 crypal[32];
    memset(crypal, 0, sizeof crypal);
#define Z3R_CRY_BGR(r, g, b) ((uint16)((r) | ((g) << 5) | ((b) << 10)))
    crypal[5] = Z3R_CRY_BGR(7, 15, 27);   // pix1 = gem facet (dark blue)
    crypal[6] = Z3R_CRY_BGR(20, 28, 31);  // pix2 = gem body (light blue)
    crypal[7] = 0;                        // pix3 = cell background -> transparent
#undef Z3R_CRY_BGR
    static const ItemBoxGfx kCrystalIcon = {{0x2544, 0x2545, 0, 0}};
    DecodeCrystalHud(out, stride, scratch, sw, crypal, kRandoIcon_Crystal, &kCrystalIcon);
  }
  return n;
}

void Hud_RandoDrawTrackers(void) {
  if (!Hud_RandoOamTrackerWillDrawThisFrame())
    return;
  // The tracker is drawn after the game has completed OAM for this frame.
  // Never replace scratch tiles that a vanilla sprite is already using.
  if (Rando_ObjScratchVisibleSlotMask() & 0x03)
    return;
  if (!Rando_ObjScratchReserveForFrame(kRandoObjScratchOwner_LegacyTracker))
    return;
#ifndef Z3R_NATIVE_SETTINGS_WINDOW
  // Both trackers share one downward OAM-slot cursor so they never claim the
  // same free slot. Item tracker (top-left) draws first, then the location
  // tracker (top-right) from whatever slots remain.
  int slot = 127;
  if (g_rando_show_item_tracker)
    Hud_RandoDrawItemTrackerInner(&slot);
  if (g_rando_show_location_tracker)
    Hud_RandoDrawLocationTrackerInner(&slot);
#endif
}
