// JNI bridge for the second-screen companion UI (Ayn Thor bottom screen).
// Exposes read-only game state from g_ram to com.dishii.zelda3.GameState.
// The Java UI thread polls these while the game thread writes g_ram; values
// are single loads of small integers, so torn reads are harmless for a minimap.
#include <jni.h>
#include <string.h>

#include "types.h"
#include "variables.h"
#include "features.h"
#include "hud.h"
#include "assets.h"
#include "load_gfx.h"
#include "second_screen_tables.h"

JNIEXPORT jint JNICALL Java_com_dishii_zelda3_GameState_getLinkX(JNIEnv *env, jclass clazz) {
  return link_x_coord;
}

JNIEXPORT jint JNICALL Java_com_dishii_zelda3_GameState_getLinkY(JNIEnv *env, jclass clazz) {
  return link_y_coord;
}

// Overworld area number when outdoors, dungeon room index when indoors.
JNIEXPORT jint JNICALL Java_com_dishii_zelda3_GameState_getArea(JNIEnv *env, jclass clazz) {
  return player_is_indoors ? dungeon_room_index : overworld_screen_index;
}

// 0x09 = overworld, 0x07 = dungeon. Low byte = main module, high byte = submodule.
JNIEXPORT jint JNICALL Java_com_dishii_zelda3_GameState_getModule(JNIEnv *env, jclass clazz) {
  return main_module_index | (submodule_index << 8);
}

JNIEXPORT jboolean JNICALL Java_com_dishii_zelda3_GameState_isIndoors(JNIEnv *env, jclass clazz) {
  return player_is_indoors != 0;
}

// Copies g_ram[0xF300..0xF400) (save/equipment block: items, rupees, hearts,
// magic, keys, pendants, crystals) into the caller's byte[256] buffer.
JNIEXPORT void JNICALL Java_com_dishii_zelda3_GameState_readSram(JNIEnv *env, jclass clazz, jbyteArray out) {
  jsize n = (*env)->GetArrayLength(env, out);
  if (n > 0x100) n = 0x100;
  (*env)->SetByteArrayRegion(env, out, 0, n, (const jbyte *)(g_ram + 0xF300));
}

// The equipped Y-item as an inventory grid slot (1..20, 0 = none).
JNIEXPORT jint JNICALL Java_com_dishii_zelda3_GameState_getEquippedSlot(JNIEnv *env, jclass clazz) {
  uint8 item = hud_cur_item;
  if (item >= 21) return 16;  // Bottle1..4 -> the bottle cell
  if (item == 0 || item > 20) return 0;
  if (hud_inventory_order[0] != 0) {
    for (int i = 0; i < 20; i++)
      if (hud_inventory_order[i] == item) return i + 1;
  }
  return item;
}

// Palace index (0..13, 0xFF when not in a palace) in the low byte,
// current floor (int8: 0=1F, -1=B1...) in the high byte.
JNIEXPORT jint JNICALL Java_com_dishii_zelda3_GameState_getDungeon(JNIEnv *env, jclass clazz) {
  uint8 palace = (uint8)cur_palace_index_x2;
  return (palace == 0xff ? 0xff : palace >> 1) | ((dung_cur_floor & 0xFF) << 8);
}

// Copies save_dung_info (g_ram[0xF000..0xF500): uint16 per room; low nibble =
// visited quadrant bits) into the caller's byte[] (up to 0x500 bytes).
JNIEXPORT void JNICALL Java_com_dishii_zelda3_GameState_readDungFlags(JNIEnv *env, jclass clazz, jbyteArray out) {
  jsize n = (*env)->GetArrayLength(env, out);
  if (n > 0x500) n = 0x500;
  (*env)->SetByteArrayRegion(env, out, 0, n, (const jbyte *)(g_ram + 0xF000));
}

// ============ runtime asset generation ============
// All second-screen artwork is generated from the user's own zelda3_assets.dat
// (already required by the game), so the APK ships no copyrighted pixels.

// decoded HUD 2bpp sheets 0x6a,0x6b,0x69 -> 384 tiles of 64 pixel values (0..3),
// matching the tile ids used by the tables in second_screen_tables.h
static uint8 g_ss_tiles[384 * 64];
static bool g_ss_tiles_ready;

static bool SS_AssetsReady(void) {
  return g_asset_ptrs[57] && g_asset_ptrs[65] && g_asset_ptrs[66] && g_asset_ptrs[67] &&
         g_asset_ptrs[68] && g_asset_ptrs[81] && g_asset_ptrs[92] && g_asset_ptrs[93] &&
         g_asset_ptrs[97];
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

static jint SS_Snes555(uint16 w) {
  int r = w & 31, g = (w >> 5) & 31, b = (w >> 10) & 31;
  r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
  return (jint)0xff000000 | (r << 16) | (g << 8) | b;
}

static jint SS_HudColor(int group, int pix) {
  return SS_Snes555(kHudPalData[group * 4 + pix]);
}

// draw one HUD tile (tilemap word: id | pal<<10 | hflip 0x4000 | vflip 0x8000)
static void SS_DrawTile(jint *px, int stride, int x0, int y0, uint16 v) {
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
static void SS_StripBg(jint *px, int stride, int x0, int y0) {
  bool dark[256], seen[256];
  for (int i = 0; i < 256; i++) {
    jint c = px[(y0 + i / 16) * stride + x0 + i % 16];
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

// Item icon sheet: kIconCount cells of 16x16, kIconCols per row (matches icons.json).
JNIEXPORT jboolean JNICALL Java_com_dishii_zelda3_GameState_renderIconSheet(JNIEnv *env, jclass clazz, jintArray out) {
  int w = kIconCols * 16, h = ((kIconCount + kIconCols - 1) / kIconCols) * 16;
  if (!SS_AssetsReady() || (*env)->GetArrayLength(env, out) < w * h) return false;
  SS_EnsureTiles();
  jint *px = (*env)->GetIntArrayElements(env, out, NULL);
  memset(px, 0, (size_t)w * h * 4);
  for (int i = 0; i < kIconCount; i++) {
    int x0 = (i % kIconCols) * 16, y0 = (i / kIconCols) * 16;
    SS_DrawTile(px, w, x0, y0, kIconTilemap[i][0]);
    SS_DrawTile(px, w, x0 + 8, y0, kIconTilemap[i][1]);
    SS_DrawTile(px, w, x0, y0 + 8, kIconTilemap[i][2]);
    SS_DrawTile(px, w, x0 + 8, y0 + 8, kIconTilemap[i][3]);
    SS_StripBg(px, w, x0, y0);
  }
  (*env)->ReleaseIntArrayElements(env, out, px, 0);
  return true;
}

// Glyph sheet: kGlyphCount cells of 8x8, kGlyphCols per row (matches glyphs.json).
JNIEXPORT jboolean JNICALL Java_com_dishii_zelda3_GameState_renderGlyphSheet(JNIEnv *env, jclass clazz, jintArray out) {
  int w = kGlyphCols * 8, h = ((kGlyphCount + kGlyphCols - 1) / kGlyphCols) * 8;
  if (!SS_AssetsReady() || (*env)->GetArrayLength(env, out) < w * h) return false;
  SS_EnsureTiles();
  jint *px = (*env)->GetIntArrayElements(env, out, NULL);
  memset(px, 0, (size_t)w * h * 4);
  for (int i = 0; i < kGlyphCount; i++)
    SS_DrawTile(px, w, (i % kGlyphCols) * 8, (i / kGlyphCols) * 8, kGlyphTiles[i]);
  (*env)->ReleaseIntArrayElements(env, out, px, 0);
  return true;
}

// Letter sheet A-Z: 16 cols x 2 rows of 8x8 (matches letters.json);
// HUD tiles 0x150-0x15F (A-P) and 0x160-0x169 (Q-Z), fixed white-on-outline palette.
JNIEXPORT jboolean JNICALL Java_com_dishii_zelda3_GameState_renderLetterSheet(JNIEnv *env, jclass clazz, jintArray out) {
  int w = 16 * 8, h = 2 * 8;
  if (!SS_AssetsReady() || (*env)->GetArrayLength(env, out) < w * h) return false;
  SS_EnsureTiles();
  jint *px = (*env)->GetIntArrayElements(env, out, NULL);
  memset(px, 0, (size_t)w * h * 4);
  for (int i = 0; i < 26; i++) {
    int id = i < 16 ? 0x150 + i : 0x160 + (i - 16);
    const uint8 *t = g_ss_tiles + id * 64;
    int x0 = (i % 16) * 8, y0 = (i / 16) * 8;
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        uint8 pix = t[y * 8 + x];
        if (pix == 1) px[(y0 + y) * w + x0 + x] = (jint)0xff282830;
        else if (pix == 2) px[(y0 + y) * w + x0 + x] = (jint)0xfff8f8f8;
      }
  }
  (*env)->ReleaseIntArrayElements(env, out, px, 0);
  return true;
}

// The 512x512 world map, composited from the game's mode-7 map data.
JNIEXPORT jboolean JNICALL Java_com_dishii_zelda3_GameState_renderWorldMap(JNIEnv *env, jclass clazz, jintArray out, jboolean dark) {
  if (!SS_AssetsReady() || (*env)->GetArrayLength(env, out) < 512 * 512) return false;
  const uint8 *gfx = kOverworldMapGfx;                 // 256 mode-7 tiles, 64 bytes each
  const uint8 *light = kLightOverworldTilemap;         // 64x64 tiles as four 32x32 quadrants
  const uint8 *darkm = kDarkOverworldTilemap;          // 32x32 tiles, replaces the center
  const uint16 *pal = kOverworldMapPaletteData + (dark ? 128 : 0);
  jint *px = (*env)->GetIntArrayElements(env, out, NULL);
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
  (*env)->ReleaseIntArrayElements(env, out, px, 0);
  return true;
}

// Link's 16x16 head (map marker) from his sprite sheet, green-mail palette.
JNIEXPORT jboolean JNICALL Java_com_dishii_zelda3_GameState_renderLinkFace(JNIEnv *env, jclass clazz, jintArray out, jint chunk) {
  if (!SS_AssetsReady() || (*env)->GetArrayLength(env, out) < 16 * 16) return false;
  const uint8 *gfx = kLinkGraphics;
  const uint16 *pal = kPalette_ArmorAndGloves;         // first 15 colors = green mail
  jint *px = (*env)->GetIntArrayElements(env, out, NULL);
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
  (*env)->ReleaseIntArrayElements(env, out, px, 0);
  return true;
}

// Dungeon floor layouts (5x5 room ids per floor). Returns floors | basements<<8, or -1.
JNIEXPORT jint JNICALL Java_com_dishii_zelda3_GameState_getDungeonLayout(JNIEnv *env, jclass clazz, jint palace, jbyteArray out) {
  static const uint8 kBasements[14] = {1, 3, 0, 1, 0, 2, 1, 2, 2, 7, 0, 2, 3, 1};  // kDungMap_Tab5 low nibbles
  if (palace < 0 || palace >= 14 || !g_asset_ptrs[97]) return -1;
  MemBlk b = FindInAssetArray(97, palace);
  jsize n = (*env)->GetArrayLength(env, out);
  if ((jsize)b.size < n) n = (jsize)b.size;
  (*env)->SetByteArrayRegion(env, out, 0, n, (const jbyte *)b.ptr);
  return (jint)(b.size / 25) | (kBasements[palace] << 8);
}

// Requested from the UI thread; applied on the game thread at frame start.
static volatile int g_pending_equip_slot;

JNIEXPORT void JNICALL Java_com_dishii_zelda3_GameState_equipSlot(JNIEnv *env, jclass clazz, jint slot) {
  if (slot >= 1 && slot <= 20)
    g_pending_equip_slot = slot;
}

// Called from the main loop right before ZeldaRunFrame (game thread).
void SecondScreen_RunFrameHook(void) {
  int slot = g_pending_equip_slot;
  if (!slot) return;
  g_pending_equip_slot = 0;
  // Only during normal overworld/dungeon gameplay, not in menus or cutscenes.
  if ((main_module_index != 7 && main_module_index != 9) || submodule_index != 0)
    return;
  hud_cur_item = hud_inventory_order[0] ? hud_inventory_order[slot - 1] : (uint8)slot;
  Hud_RefreshIcon();
}
