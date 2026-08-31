// Game state access + art generation for the second screen. The SS_* core is
// plain C; the Android UI reaches it through the JNI wrappers at the bottom
// and the SDL UI (second_screen_sdl.c) calls it directly.
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "types.h"
#include "variables.h"
#include "features.h"
#include "hud.h"
#include "assets.h"
#include "load_gfx.h"
#include "messaging.h"
#include "config.h"
#include "zelda_rtl.h"
#include "snes/ppu.h"
#include "second_screen_tables.h"
#ifdef __3DS__
#include "platform_3ds.h"
#endif

// Save-state thumbnail size, in the 8:7 shape of the 256x224 SNES picture.
// The JNI/Java sides hardcode the same numbers (as the other render_* do).
#define kSsThumbW 128
#define kSsThumbH 112

int SS_GetLinkX(void) { return link_x_coord; }
int SS_GetLinkY(void) { return link_y_coord; }

// Overworld area number when outdoors, dungeon room index when indoors.
int SS_GetArea(void) {
  return player_is_indoors ? dungeon_room_index : overworld_screen_index;
}

// 0x09 = overworld, 0x07 = dungeon. Low byte = main module, high = submodule.
int SS_GetModule(void) {
  return main_module_index | (submodule_index << 8);
}

bool SS_IsIndoors(void) { return player_is_indoors != 0; }

// Copies g_ram[0xF300..0xF400) (save/equipment block: items, rupees, hearts,
// magic, keys, pendants, crystals) into the caller's buffer (up to 0x100).
void SS_ReadSram(uint8 *out, int n) {
  if (n > 0x100) n = 0x100;
  memcpy(out, g_ram + 0xF300, n);
}

// Maps a hud item id to an inventory grid slot (1..20, 0 = none).
static int SS_ItemToSlot(uint8 item) {
  if (item >= 21) return 16;  // Bottle1..4 -> the bottle cell
  if (item == 0 || item > 20) return 0;
  if (hud_inventory_order[0] != 0) {
    for (int i = 0; i < 20; i++)
      if (hud_inventory_order[i] == item) return i + 1;
  }
  return item;
}

// The equipped Y-item as an inventory grid slot (1..20, 0 = none).
int SS_GetEquippedSlot(void) { return SS_ItemToSlot(hud_cur_item); }

// The item assigned to the X button (hud_cur_item_x, set on the item screen
// when the ItemSwitchLR feature is on) as a grid slot; 0 when nothing is
// assigned or the feature is disabled.
int SS_GetEquippedSlotX(void) {
  if (!(enhanced_features0 & kFeatures0_SwitchLR)) return 0;
  return SS_ItemToSlot(hud_cur_item_x);
}

// Palace index (0..13, 0xFF when not in a palace) in the low byte,
// current floor (int8: 0=1F, -1=B1...) in the high byte.
int SS_GetDungeon(void) {
  uint8 palace = (uint8)cur_palace_index_x2;
  return (palace == 0xff ? 0xff : palace >> 1) | ((dung_cur_floor & 0xFF) << 8);
}

// Copies save_dung_info (g_ram[0xF000..0xF500): uint16 per room; low nibble =
// visited quadrant bits) into the caller's buffer (up to 0x500 bytes).
void SS_ReadDungFlags(uint8 *out, int n) {
  if (n > 0x500) n = 0x500;
  memcpy(out, g_ram + 0xF000, n);
}

// Link's most recent position on the real overworld, latched on the game
// thread each frame (SecondScreen_RunFrameHook). Outlives the Android view, so
// a rebuild on refocus can restore the exact doorway Link came in through.
// Cleared on death/file select, when the entrance is no longer meaningful.
static int g_ss_outdoor_pos[3] = { -1, -1, -1 };
static bool g_ss_has_outdoor;

// Where the current indoor room comes out onto the overworld: {link x, link y,
// overworld screen index} in out[0..2], false when unknown. Prefers the live
// doorway tracked above; without it (fresh save-load straight into a room)
// falls back to the engine's static exit table (the same data
// LoadOverworldFromDungeon walks) keyed on dungeon_room_index, so the marker
// survives view rebuilds with no persisted state (issue #9). The table walk
// copies the engine's own room test: caves 0x100..0x17F (except Link's house
// 0x104) exit via cached entrance data instead, and matching the table's
// special-screen pseudo-rooms (0x180+) would return coordinates in the special
// areas' own tiny space, parking the marker in the Lost Woods (issue #23).
bool SS_GetIndoorExit(int *out) {
  if (g_ss_has_outdoor) {
    out[0] = g_ss_outdoor_pos[0];
    out[1] = g_ss_outdoor_pos[1];
    out[2] = g_ss_outdoor_pos[2];
    return true;
  }
  if (!g_asset_ptrs[130] || !g_asset_ptrs[131] || !g_asset_ptrs[135] || !g_asset_ptrs[136])
    return false;
  int room = dungeon_room_index;
  if (room != 0x104 && room >= 0x100)
    return false;
  int n = (int)(kExitDataRooms_SIZE / sizeof(uint16));
  for (int k = 0; k < n; k++) {
    if (kExitDataRooms[k] == room) {
      out[0] = kExitData_XCoord[k];
      out[1] = kExitData_YCoord[k];
      out[2] = kExitData_ScreenIndex[k];
      return true;
    }
  }
  return false;
}

// ============ runtime asset generation ============

// decoded HUD 2bpp sheets 0x6a,0x6b,0x69 -> 384 tiles of 64 pixel values (0..3),
// matching the tile ids used by the tables in second_screen_tables.h
static uint8 g_ss_tiles[384 * 64];
static bool g_ss_tiles_ready;

static bool SS_AssetsReady(void) {
  return g_asset_ptrs[57] && g_asset_ptrs[65] && g_asset_ptrs[66] && g_asset_ptrs[67] &&
         g_asset_ptrs[68] && g_asset_ptrs[81] && g_asset_ptrs[90] && g_asset_ptrs[91] &&
         g_asset_ptrs[92] && g_asset_ptrs[93] && g_asset_ptrs[97] && g_asset_ptrs[98];
}

static void SS_EnsureTiles(void) {
  if (g_ss_tiles_ready) return;
  static const uint8 kSheets[3] = {0x6a, 0x6b, 0x69};  // same order as tile ids 0x000-0x17F
  uint8 raw[0x1000];
  for (int s = 0; s < 3; s++) {
    Decompress(raw, kSprGfx(kSheets[s]).ptr);  // HUD sheets are sprite-gfx packs (see Decomp_spr)
    for (int t = 0; t < 128; t++) {
      const uint8 *tp = raw + t * 16;
      uint8 *out = g_ss_tiles + (s * 128 + t) * 64;
      for (int y = 0; y < 8; y++) {
        uint8 b0 = tp[y * 2], b1 = tp[y * 2 + 1];
        for (int x = 0; x < 8; x++)
          out[y * 8 + x] = ((b0 >> (7 - x)) & 1) | (((b1 >> (7 - x)) & 1) << 1);
      }
    }
  }
  g_ss_tiles_ready = true;
}

static uint32 SS_Snes555(uint16 w) {
  int r = w & 31, g = (w >> 5) & 31, b = (w >> 10) & 31;
  r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
  return 0xff000000u | (r << 16) | (g << 8) | b;
}

static uint32 SS_HudColor(int group, int pix) {
  return SS_Snes555(kHudPalData[group * 4 + pix]);
}

// draw one HUD tile (tilemap word: id | pal<<10 | hflip 0x4000 | vflip 0x8000)
static void SS_DrawTile(uint32 *px, int stride, int x0, int y0, uint16 v) {
  int id = v & 0x3ff;
  if (id >= 384) return;
  const uint8 *t = g_ss_tiles + id * 64;
  int p = (v >> 10) & 7;
  for (int y = 0; y < 8; y++) {
    int sy = (v & 0x8000) ? 7 - y : y;
    for (int x = 0; x < 8; x++) {
      int sx = (v & 0x4000) ? 7 - x : x;
      uint8 pix = t[sy * 8 + sx];
      if (pix) px[(y0 + y) * stride + x0 + x] = SS_HudColor(p, pix);
    }
  }
}

// remove the near-black icon-box background connected to the 16x16 border
static void SS_StripBg(uint32 *px, int stride, int x0, int y0) {
  bool dark[256], seen[256];
  for (int i = 0; i < 256; i++) {
    uint32 c = px[(y0 + i / 16) * stride + x0 + i % 16];
    int sum = ((c >> 16) & 0xff) + ((c >> 8) & 0xff) + (c & 0xff);
    dark[i] = (c >> 24) != 0 && sum < 60;
    seen[i] = false;
  }
  int stack[512], sp = 0;
  for (int i = 0; i < 16; i++) {
    stack[sp++] = i; stack[sp++] = 240 + i;             // top + bottom rows
    stack[sp++] = i * 16; stack[sp++] = i * 16 + 15;    // left + right cols
  }
  while (sp > 0) {
    int i = stack[--sp];
    if (seen[i] || !dark[i]) continue;
    seen[i] = true;
    px[(y0 + i / 16) * stride + x0 + i % 16] = 0;
    int y = i / 16, x = i % 16;
    if (y > 0 && sp < 508) stack[sp++] = i - 16;
    if (y < 15 && sp < 508) stack[sp++] = i + 16;
    if (x > 0 && sp < 508) stack[sp++] = i - 1;
    if (x < 15 && sp < 508) stack[sp++] = i + 1;
  }
}

// Item icon sheet: kIconCount cells of 16x16, kIconCols per row (ss_sheets.h).
// px must hold (kIconCols*16) x (ceil(kIconCount/kIconCols)*16) ARGB pixels.
bool SS_RenderIconSheet(uint32 *px) {
  int w = kIconCols * 16, h = ((kIconCount + kIconCols - 1) / kIconCols) * 16;
  if (!SS_AssetsReady()) return false;
  SS_EnsureTiles();
  memset(px, 0, (size_t)w * h * 4);
  for (int i = 0; i < kIconCount; i++) {
    int x0 = (i % kIconCols) * 16, y0 = (i / kIconCols) * 16;
    SS_DrawTile(px, w, x0, y0, kIconTilemap[i][0]);
    SS_DrawTile(px, w, x0 + 8, y0, kIconTilemap[i][1]);
    SS_DrawTile(px, w, x0, y0 + 8, kIconTilemap[i][2]);
    SS_DrawTile(px, w, x0 + 8, y0 + 8, kIconTilemap[i][3]);
    SS_StripBg(px, w, x0, y0);
  }
  return true;
}

// Glyph sheet: kGlyphCount cells of 8x8, kGlyphCols per row (ss_sheets.h).
bool SS_RenderGlyphSheet(uint32 *px) {
  int w = kGlyphCols * 8, h = ((kGlyphCount + kGlyphCols - 1) / kGlyphCols) * 8;
  if (!SS_AssetsReady()) return false;
  SS_EnsureTiles();
  memset(px, 0, (size_t)w * h * 4);
  for (int i = 0; i < kGlyphCount; i++)
    SS_DrawTile(px, w, (i % kGlyphCols) * 8, (i / kGlyphCols) * 8, kGlyphTiles[i]);
  return true;
}

// Letter sheet A-Z: 16 cols x 2 rows of 8x8; HUD tiles 0x150-0x15F (A-P) and
// 0x160-0x169 (Q-Z), fixed white-on-outline palette.
bool SS_RenderLetterSheet(uint32 *px) {
  int w = 16 * 8, h = 2 * 8;
  if (!SS_AssetsReady()) return false;
  SS_EnsureTiles();
  memset(px, 0, (size_t)w * h * 4);
  for (int i = 0; i < 26; i++) {
    int id = i < 16 ? 0x150 + i : 0x160 + (i - 16);
    const uint8 *t = g_ss_tiles + id * 64;
    int x0 = (i % 16) * 8, y0 = (i / 16) * 8;
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        uint8 pix = t[y * 8 + x];
        if (pix == 1) px[(y0 + y) * w + x0 + x] = 0xff282830u;
        else if (pix == 2) px[(y0 + y) * w + x0 + x] = 0xfff8f8f8u;
      }
  }
  return true;
}

// The 512x512 world map, composited from the game's mode-7 map data.
bool SS_RenderWorldMap(uint32 *px, bool dark) {
  if (!SS_AssetsReady()) return false;
  const uint8 *gfx = kOverworldMapGfx;                 // 256 mode-7 tiles, 64 bytes each
  const uint8 *light = kLightOverworldTilemap;         // 64x64 tiles as four 32x32 quadrants
  const uint8 *darkm = kDarkOverworldTilemap;          // 32x32 tiles, replaces the center
  const uint16 *pal = kOverworldMapPaletteData + (dark ? 128 : 0);
  for (int ty = 0; ty < 64; ty++) {
    for (int tx = 0; tx < 64; tx++) {
      int q = (ty >= 32 ? 2 : 0) + (tx >= 32 ? 1 : 0);
      uint8 tid = light[q * 1024 + (ty & 31) * 32 + (tx & 31)];
      if (dark && ty >= 16 && ty < 48 && tx >= 16 && tx < 48)
        tid = darkm[(ty - 16) * 32 + (tx - 16)];
      const uint8 *t = gfx + tid * 64;
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
          px[(ty * 8 + y) * 512 + tx * 8 + x] = SS_Snes555(pal[t[y * 8 + x] & 0x7f]);
    }
  }
  return true;
}

// Link's 16x16 head (map marker) from his sprite sheet, green-mail palette.
bool SS_RenderLinkFace(uint32 *px, int chunk) {
  if (!SS_AssetsReady()) return false;
  const uint8 *gfx = kLinkGraphics;
  const uint16 *pal = kPalette_ArmorAndGloves;         // first 15 colors = green mail
  memset(px, 0, 16 * 16 * 4);
  static const int kOffs[4][2] = {{0, 0}, {8, 0}, {0, 8}, {8, 8}};
  for (int part = 0; part < 4; part++) {
    int tile = chunk + (part & 1) + (part >> 1) * 16;  // n, n+1, n+16, n+17
    const uint8 *tp = gfx + tile * 32;
    for (int y = 0; y < 8; y++) {
      uint8 b0 = tp[y * 2], b1 = tp[y * 2 + 1], b2 = tp[16 + y * 2], b3 = tp[16 + y * 2 + 1];
      for (int x = 0; x < 8; x++) {
        int pix = ((b0 >> (7 - x)) & 1) | (((b1 >> (7 - x)) & 1) << 1) |
                  (((b2 >> (7 - x)) & 1) << 2) | (((b3 >> (7 - x)) & 1) << 3);
        if (pix)
          px[(kOffs[part][1] + y) * 16 + kOffs[part][0] + x] = SS_Snes555(pal[pix - 1]);
      }
    }
  }
  return true;
}

// Dungeon floor layouts (5x5 room ids per floor) into out (cap bytes).
// Returns floors | basements<<8, or -1 while assets aren't loaded yet.
int SS_GetDungeonLayout(int palace, uint8 *out, int cap) {
  static const uint8 kBasements[14] = {1, 3, 0, 1, 0, 2, 1, 2, 2, 7, 0, 2, 3, 1};  // kDungMap_Tab5 low nibbles
  if (palace < 0 || palace >= 14 || !g_asset_ptrs[97]) return -1;
  MemBlk b = FindInAssetArray(97, palace);
  int n = cap;
  if ((int)b.size < n) n = (int)b.size;
  memcpy(out, b.ptr, n);
  return (int)(b.size / 25) | (kBasements[palace] << 8);
}

// decoded dungeon-map tiles: BG chars 0x300-0x3bf as pixel values (0..7).
// The map screen's BG char base is vram 0x2000 (BG12NBA=0x22), so those chars
// alias vram 0x5000-0x5bff: sprite subsets 0-2 of the per-palace tileset
// 0x80|palace loaded by Module0E_03_01_00_PrepMapGraphics.
static uint8 g_ss_dmap_tiles[192 * 64];
static int g_ss_dmap_palace = -1;

static void SS_EnsureDmapTiles(int palace) {
  if (g_ss_dmap_palace == palace) return;
  const uint8 *packs = GetSpriteTilesetPacks(0x80 | palace);
  uint8 raw[0x1000];
  for (int s = 0; s < 3; s++) {
    Decomp_spr(raw, packs[s]);  // these packs all take the Do3To4Low path
    for (int t = 0; t < 64; t++) {
      const uint8 *tp = raw + t * 24;  // 3bpp: 8 rows of planes 0+1, then 8 bytes of plane 2
      uint8 *out = g_ss_dmap_tiles + (s * 64 + t) * 64;
      for (int y = 0; y < 8; y++) {
        uint8 b0 = tp[y * 2], b1 = tp[y * 2 + 1], b2 = tp[16 + y];
        for (int x = 0; x < 8; x++)
          out[y * 8 + x] = ((b0 >> (7 - x)) & 1) | (((b1 >> (7 - x)) & 1) << 1) |
                           (((b2 >> (7 - x)) & 1) << 2);
      }
    }
  }
  g_ss_dmap_palace = palace;
}

// draw one dungeon-map tile; the palace map palette rows sit at BG palettes 2..7
static void SS_DmapDrawTile(uint32 *px, int stride, int x0, int y0, uint16 v) {
  int id = (v & 0x3ff) - 0x300;
  if (id < 0 || id >= 192) return;
  const uint8 *t = g_ss_dmap_tiles + id * 64;
  int p = (v >> 10) & 7;
  if (p < 2) p = 2;
  for (int y = 0; y < 8; y++) {
    int sy = (v & 0x8000) ? 7 - y : y;
    for (int x = 0; x < 8; x++) {
      int sx = (v & 0x4000) ? 7 - x : x;
      uint8 pix = t[sy * 8 + sx];
      if (pix) px[(y0 + y) * stride + x0 + x] = SS_Snes555(kPalette_PalaceMapBg[(p - 2) * 16 + pix]);
    }
  }
}

// per-quadrant tile choice, same rules as DungeonMap_DrawSingleRowOfRooms:
// visited quadrants in full color, map-item-only ones dimmed, otherwise blank
static uint16 SS_DmapQuad(uint16 e, bool visited, bool have_map) {
  if (visited || e == 0xB00)
    return (visited || have_map) ? e : 0xb00;
  if (!have_map)
    return 0xb00;
  return (e & 0x1000) ? (e & ~0x1c00) | 0xc00 : e + 0x400;
}

// One dungeon floor as the game's own map mode draws it: each room of the 5x5
// grid becomes a 2x2 block of map tiles keyed by its shape id from asset 98
// (16x16 px per room -> 80x80 out), read live from save_dung_info.
bool SS_RenderDungeonFloor(int palace, int floorIdx, uint32 *px) {
  if (palace < 0 || palace >= 14 || !SS_AssetsReady()) return false;
  MemBlk lay = FindInAssetArray(97, palace);
  MemBlk shapes = FindInAssetArray(98, palace);
  if (floorIdx < 0 || (size_t)(floorIdx + 1) * 25 > lay.size) return false;
  SS_EnsureDmapTiles(palace);
  bool have_map = (link_dungeon_map & (0x8000 >> palace)) != 0;
  memset(px, 0, 80 * 80 * 4);
  for (int i = 0; i < 25; i++) {
    uint8 v = lay.ptr[floorIdx * 25 + i];
    int visits = floorIdx, yv = 0x51;  // empty cells use the blank shape
    if (v != 0xf) {
      visits = save_dung_info[v] & 0xf;
      size_t count = 0, k = 0;  // shape ids are indexed by room order, skipping empty cells
      while (k < lay.size && lay.ptr[k] != v)
        count += (lay.ptr[k++] != 0xf);
      if (count < shapes.size) yv = shapes.ptr[count];
      if (yv >= 186) yv = 0x51;
    }
    const uint16 *e = GetDungmapRoomShape(yv);
    int x0 = i % 5 * 16, y0 = i / 5 * 16;
    SS_DmapDrawTile(px, 80, x0, y0, SS_DmapQuad(e[0], (visits & 8) != 0, have_map));
    SS_DmapDrawTile(px, 80, x0 + 8, y0, SS_DmapQuad(e[1], (visits & 4) != 0, have_map));
    SS_DmapDrawTile(px, 80, x0, y0 + 8, SS_DmapQuad(e[2], (visits & 2) != 0, have_map));
    SS_DmapDrawTile(px, 80, x0 + 8, y0 + 8, SS_DmapQuad(e[3], (visits & 1) != 0, have_map));
  }
  return true;
}

// The map screen's overlay sprites as a 32x8 sheet: the current-room dot
// (tile 0x34, DungeonMap_DrawBlinkingIndicator) in its three cycling sprite
// palettes 4/5/6 from asset 91, then the boss skull (tile 0x31,
// DungeonMap_DrawBossIcon) in live sprite palette 1.
bool SS_RenderMapIcons(int palace, uint32 *px) {
  if (palace < 0 || palace >= 14 || !SS_AssetsReady()) return false;
  SS_EnsureDmapTiles(palace);
  const uint8 *dot = g_ss_dmap_tiles + 0x34 * 64, *skull = g_ss_dmap_tiles + 0x31 * 64;
  memset(px, 0, 32 * 8 * 4);
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
      uint8 pix = dot[y * 8 + x];
      if (pix)
        for (int p = 0; p < 3; p++)
          px[y * 32 + p * 8 + x] = SS_Snes555(kPalette_PalaceMapSpr[p * 7 + pix - 1]);
      pix = skull[y * 8 + x];
      if (pix) px[y * 32 + 24 + x] = SS_Snes555(main_palette_buffer[144 + pix]);
    }
  return true;
}

// ============ actions (UI thread -> game thread) ============

// Requested from the UI thread; applied on the game thread at frame start.
static volatile int g_pending_equip_slot;
static volatile int g_pending_assign_x_slot;
static volatile int g_pending_widescreen = -1;
static volatile int g_pending_crt_filter = -1;
static volatile int g_pending_hide_hud = -1;
static volatile int g_pending_controls_set;
static uint8 g_pending_controls[12];
static volatile int g_pending_saveload;  // 0 idle, 1 save, 2 load
static volatile int g_pending_memory_dump;
static volatile int g_pending_restart;
static volatile int g_pending_display_mode = -1;
static volatile int g_pending_wide_edge_mode = -1;
static char g_pending_dump_dir[160];
static volatile int g_pending_load_dump_state;
static volatile int g_dump_load_result = -1;
// Save-state slot picker: kSaveLoad_Save/kSaveLoad_Load, -1 when idle.
static volatile int g_pending_state_cmd = -1;
static volatile int g_pending_state_slot;
// kFeatures0_* bits to set/clear; ZeldaRunFrame latches the result into game ram.
static volatile uint32 g_pending_features_on, g_pending_features_off;
// Redraw the top HUD once the changed feature bits have reached game ram.
static bool g_ss_hud_refresh;

// Read by nmi.c: blanks the HUD strip in VRAM instead of copying it.
bool g_ss_hide_hud;
// -1 idle, -2 armed (main.c swallows the next press), >= 0 captured button.
volatile int g_ss_capture_button = -1;

void SS_EquipSlot(int slot) {
  if (slot >= 1 && slot <= 20)
    g_pending_equip_slot = slot;
}

void SS_AssignSlotX(int slot) {
  if (slot >= 1 && slot <= 20)
    g_pending_assign_x_slot = slot;
}

void SS_SetWidescreen(bool on) { g_pending_widescreen = on ? 1 : 0; }

void SS_Set3DSDisplayMode(int mode) { g_pending_display_mode = mode; }
void SS_Set3DSWideEdgeMode(int mode) { g_pending_wide_edge_mode = mode; }

void SS_RequestMemoryDump(const char *dump_dir) {
  if (dump_dir && dump_dir[0]) {
    strncpy(g_pending_dump_dir, dump_dir, sizeof(g_pending_dump_dir) - 1);
    g_pending_dump_dir[sizeof(g_pending_dump_dir) - 1] = 0;
  } else {
    g_pending_dump_dir[0] = 0;
  }
  g_pending_memory_dump = 1;
}

void SS_RequestLoadLatestDumpState(void) {
  if (!g_pending_load_dump_state) {
    g_dump_load_result = -1;
    g_pending_load_dump_state = 1;
  }
}

int SS_TakeLoadDumpStateResult(void) {
  int result = g_dump_load_result;
  if (result >= 0)
    g_dump_load_result = -1;
  return result;
}

void SS_RequestRestart(void) { g_pending_restart = 1; }

bool SS_IsWidescreen(void) {
  int pending = g_pending_widescreen;
  if (pending >= 0) return pending != 0;
  return g_zenv.ppu && g_zenv.ppu->extraLeftRight != 0;
}

void SS_SetCrtFilter(bool on) { g_pending_crt_filter = on ? 1 : 0; }

bool SS_IsCrtFilter(void) {
  int pending = g_pending_crt_filter;
  if (pending >= 0) return pending != 0;
  return g_config.crt_filter;
}

void SS_SetHudHidden(bool hide) { g_pending_hide_hud = hide ? 1 : 0; }

bool SS_IsHudHidden(void) {
  int pending = g_pending_hide_hud;
  if (pending >= 0) return pending != 0;
  return g_ss_hide_hud;
}

// The kFeatures0_* flags the game is asked to run with (features.h).
uint32 SS_GetFeatures(void) {
  return (g_wanted_zelda_features | g_pending_features_on) & ~g_pending_features_off;
}

void SS_SetFeature(unsigned mask, bool on) {
  if (on) {
    g_pending_features_off &= ~(uint32)mask;
    g_pending_features_on |= (uint32)mask;
  } else {
    g_pending_features_on &= ~(uint32)mask;
    g_pending_features_off |= (uint32)mask;
  }
}

// Settings-screen save state. Slot 1: slot 0 belongs to the Autosave option,
// which would overwrite it on exit.
void SS_SaveLoadState(bool save) { g_pending_saveload = save ? 1 : 2; }

void SS_SetAutosave(bool on) { g_config.autosave = on; }

// ---- save states ----
// The picker drives SaveLoadSlot, which touches audio state, so the request is
// queued here and applied under the audio lock on the game thread.

void SS_RequestSaveState(int slot) {
  if (slot < 0 || slot >= 256) return;
  g_pending_state_slot = slot;
  g_pending_state_cmd = kSaveLoad_Save;
}

void SS_RequestLoadState(int slot) {
  if (slot < 0 || slot >= 256) return;
  g_pending_state_slot = slot;
  g_pending_state_cmd = kSaveLoad_Load;
}

// 0 idle, 1 waiting on the renderer, 2 a grabbed frame is sitting in g_ss_thumb.
static volatile int g_ss_thumb_state;
static uint32 g_ss_thumb[kSsThumbW * kSsThumbH];

static uint32 ExpandRgb565(uint16 pixel) {
  uint32 r = (pixel >> 11) & 0x1f;
  uint32 g = (pixel >> 5) & 0x3f;
  uint32 b = pixel & 0x1f;
  r = (r << 3) | (r >> 2);
  g = (g << 2) | (g >> 4);
  b = (b << 3) | (b >> 2);
  return 0xff000000u | r << 16 | g << 8 | b;
}

// Called from the renderer with the frame just drawn (game thread). Old 3DS
// supplies RGB565; other targets keep the original 32-bit BGRX buffer.
// Only copies when a save asked for a thumbnail, so the read-back off the
// renderer's buffer costs nothing on a normal frame.
void SecondScreen_CaptureFrameHook(const uint8 *px, int pitch,
                                   int width, int height, bool rgb565) {
  if (g_ss_thumb_state != 1 || !px || width <= 0 || height <= 0) return;
  // Widescreen frames carry extra side space; take the centered 8:7 window so
  // the thumbnail always has the shape of the normal 256x224 picture.
  int cw = height * kSsThumbW / kSsThumbH;
  if (cw > width) cw = width;
  int x0 = (width - cw) / 2;
  for (int y = 0; y < kSsThumbH; y++) {
    const uint8 *row =
      px + (size_t)(y * height / kSsThumbH) * pitch;
    uint32 *out = g_ss_thumb + y * kSsThumbW;
    for (int x = 0; x < kSsThumbW; x++) {
      int source_x = x0 + x * cw / kSsThumbW;
      out[x] = rgb565 ? ExpandRgb565(((const uint16 *)row)[source_x]) :
                        ((const uint32 *)row)[source_x] | 0xff000000u;
    }
  }
  g_ss_thumb_state = 2;
}

// Fills out (kSsThumbW*kSsThumbH ARGB) with the frame grabbed for the last save
// and clears it; false while none is waiting (same consume pattern as
// SS_GetCapturedButton).
bool SS_TakeThumbnail(uint32 *out) {
  if (g_ss_thumb_state != 2) return false;
  memcpy(out, g_ss_thumb, sizeof(g_ss_thumb));
  g_ss_thumb_state = 0;
  return true;
}

void SS_ArmButtonCapture(bool arm) { g_ss_capture_button = arm ? -2 : -1; }

// Returns the captured gamepad button and rearms idle; -2 still waiting, -1 idle.
int SS_GetCapturedButton(void) {
  int b = g_ss_capture_button;
  if (b >= 0) g_ss_capture_button = -1;
  return b;
}

// Fills out (12 ints) with the gamepad button bound to each joypad command
// (Up, Down, Left, Right, Select, Start, A, B, X, Y, L, R); -1 = unbound.
void SS_GetGamepadControls(int *out) {
  uint8 btns[12];
  GamepadMap_GetControls(btns);
  for (int i = 0; i < 12; i++)
    out[i] = btns[i] == 0xff ? -1 : btns[i];
}

void SS_SetGamepadControls(const int *in) {
  for (int i = 0; i < 12; i++)
    g_pending_controls[i] = (in[i] >= 0 && in[i] < kGamepadBtn_Count) ? (uint8)in[i] : 0xff;
  g_pending_controls_set = 1;
}

// Called from the main loop right before ZeldaRunFrame (game thread).
void SecondScreen_RunFrameHook(void) {
  // Track where Link last stood on the real overworld (for SS_GetIndoorExit).
  // Screens >= 0x80 are the special areas (Master Sword glade, Zora's Domain,
  // under the bridge); they run in their own small coordinate space near the
  // map origin, so latching them would drag the marker into the Lost Woods.
  if (main_module_index == 0x12 || main_module_index <= 0x05) {
    g_ss_has_outdoor = false;  // death/file select: entrance unknown
  } else if (!player_is_indoors && main_module_index == 9 && overworld_screen_index < 0x80) {
    g_ss_outdoor_pos[0] = link_x_coord;
    g_ss_outdoor_pos[1] = link_y_coord;
    g_ss_outdoor_pos[2] = overworld_screen_index;
    g_ss_has_outdoor = true;
  }
  int ws = g_pending_widescreen;
  if (ws >= 0) {
    g_pending_widescreen = -1;
    extern void ZeldaSetWidescreen(bool enable);
    ZeldaSetWidescreen(ws != 0);
  }
  int display_mode = g_pending_display_mode;
  if (display_mode >= 0) {
    g_pending_display_mode = -1;
#ifdef __3DS__
    extern void ZeldaSet3DSDisplayMode(int mode);
    ZeldaSet3DSDisplayMode(display_mode);
#else
    extern void ZeldaSetWidescreen(bool enable);
    ZeldaSetWidescreen(display_mode != 0);
#endif
  }
  int wide_edge_mode = g_pending_wide_edge_mode;
  if (wide_edge_mode >= 0) {
    g_pending_wide_edge_mode = -1;
    ZeldaSetWidescreenEdgeMode(wide_edge_mode);
  }
  int crt = g_pending_crt_filter;
  if (crt >= 0) {
    g_pending_crt_filter = -1;
    g_config.crt_filter = crt != 0;  // the renderer picks it up on the next frame
  }
  int hh = g_pending_hide_hud;
  if (hh >= 0) {
    g_pending_hide_hud = -1;
    g_ss_hide_hud = hh != 0;
    if (main_module_index == 7 || main_module_index == 9 || main_module_index == 14) {
      if (g_ss_hide_hud)
        flag_update_hud_in_nmi++;
      else
        Hud_Rebuild();
    }
  }
  if (g_pending_controls_set) {
    g_pending_controls_set = 0;
    GamepadMap_SetControls(g_pending_controls);
  }
  if (g_pending_memory_dump) {
    g_pending_memory_dump = 0;
#ifdef __3DS__
    // Freeze the NDSP channel before directory creation or SD writes. SDL's
    // regular pause only stops its callback and leaves queued audio audible.
    Platform3DS_MarkDumpTimingDiscontinuity();
    Platform3DS_SetAudioPausedForDump(true);

    char dump_dir[160];
    bool directory_ok;
    if (g_pending_dump_dir[0]) {
      snprintf(dump_dir, sizeof(dump_dir), "%s", g_pending_dump_dir);
      directory_ok = true;
    } else {
      directory_ok =
        Platform3DS_CreateDumpDirectory(dump_dir, sizeof(dump_dir));
    }

    bool screens_ok = false;
    bool state_ok = false;
    bool dump_ok = false;
    if (directory_ok) {
      char top_path[192];
      char bottom_path[192];
      char top_raw_path[192];
      char bottom_raw_path[192];
      snprintf(top_path, sizeof(top_path), "%s/top-screen.bmp", dump_dir);
      snprintf(bottom_path, sizeof(bottom_path), "%s/bottom-screen.bmp",
               dump_dir);
      snprintf(top_raw_path, sizeof(top_raw_path), "%s/top-screen.raw",
               dump_dir);
      snprintf(bottom_raw_path, sizeof(bottom_raw_path),
               "%s/bottom-screen.raw", dump_dir);

      Platform3DSCaptureStats capture_stats;
      memset(&capture_stats, 0, sizeof(capture_stats));
      // Capture first: these are the physical display framebuffers from the
      // last completed presentation, including borders and GPU overlays.
      screens_ok = Platform3DS_SaveDisplayedScreensDetailed(
        top_path, bottom_path, top_raw_path, bottom_raw_path,
        &capture_stats);
      state_ok = ZeldaWriteDumpState(dump_dir);
      dump_ok = Platform3DS_DumpMemory(
        dump_dir, g_ram, 131072, g_zenv.sram, 8192,
        g_zenv.vram, 32768, &capture_stats, screens_ok);
    }

    if (directory_ok && screens_ok && state_ok && dump_ok)
      Platform3DS_ShowDumpSavedOverlay();
    Platform3DS_LogRuntime(
      "Dump result: directory=%s screens=%s state=%s memory=%s",
      directory_ok ? "OK" : "FAILED", screens_ok ? "OK" : "FAILED",
      state_ok ? "OK" : "FAILED", dump_ok ? "OK" : "FAILED");
    Platform3DS_SetAudioPausedForDump(false);
#endif
  }
  if (g_pending_load_dump_state) {
    g_pending_load_dump_state = 0;
    ZeldaDumpStateResult result = ZeldaLoadLatestDumpState();
    if (result == kZeldaDumpStateLoaded)
      g_ss_has_outdoor = false;
    g_dump_load_result = (int)result;
  }
  if (g_pending_restart) {
    g_pending_restart = 0;
    g_ss_has_outdoor = false;
    ZeldaApuLock();
    ZeldaReset(true);
    ZeldaApuUnlock();
  }
  int state_cmd = g_pending_state_cmd;
  if (state_cmd >= 0) {
    int which = g_pending_state_slot;
    g_pending_state_cmd = -1;
    ZeldaApuLock();   // SaveLoadSlot touches audio state (same as HandleCommand)
    SaveLoadSlot(state_cmd, which);
    ZeldaApuUnlock();
    // grab the frame drawn just after the save for the slot's thumbnail
    if (state_cmd == kSaveLoad_Save)
      g_ss_thumb_state = 1;
  }
  uint32 f_on = g_pending_features_on, f_off = g_pending_features_off;
  if (f_on | f_off) {
    g_pending_features_on &= ~f_on;
    g_pending_features_off &= ~f_off;
    uint32 old = g_wanted_zelda_features;
    uint32 nf = (old | f_on) & ~f_off;
    if (nf != old) {
      g_wanted_zelda_features = nf;  // ZeldaRunFrame latches this into game ram
      g_config.features0 = nf;
      if ((nf ^ old) & kFeatures0_DimFlashes) {
        extern void ZeldaApplyDimFlashesPalette(bool enable);
        ZeldaApplyDimFlashesPalette((nf & kFeatures0_DimFlashes) != 0);
      }
      if ((nf ^ old) & (kFeatures0_SwitchLR | kFeatures0_ShowMaxItemsInYellow | kFeatures0_CarryMoreRupees))
        g_ss_hud_refresh = true;
    }
  }
  // HUD-affecting bits changed: rebuild once the new flags are in game ram
  if (g_ss_hud_refresh && enhanced_features0 == g_wanted_zelda_features) {
    g_ss_hud_refresh = false;
    if (!g_ss_hide_hud && (main_module_index == 7 || main_module_index == 9 || main_module_index == 14))
      Hud_Rebuild();
  }
  int sl = g_pending_saveload;
  if (sl) {
    g_pending_saveload = 0;
    // SaveLoadSlot touches the APU; the audio callback must not run meanwhile.
    ZeldaApuLock();
    SaveLoadSlot(sl == 1 ? kSaveLoad_Save : kSaveLoad_Load, 1);
    ZeldaApuUnlock();
  }
  // Only during normal overworld/dungeon gameplay, not in menus or cutscenes.
  bool in_gameplay = (main_module_index == 7 || main_module_index == 9) && submodule_index == 0;
  int slot = g_pending_equip_slot;
  if (slot) {
    g_pending_equip_slot = 0;
    if (in_gameplay) {
      hud_cur_item = hud_inventory_order[0] ? hud_inventory_order[slot - 1] : (uint8)slot;
      Hud_RefreshIcon();
    }
  }
  slot = g_pending_assign_x_slot;
  if (slot) {
    g_pending_assign_x_slot = 0;
    if (in_gameplay && (enhanced_features0 & kFeatures0_SwitchLR)) {
      hud_cur_item_x = hud_inventory_order[0] ? hud_inventory_order[slot - 1] : (uint8)slot;
      Hud_RefreshIcon();
    }
  }
}
