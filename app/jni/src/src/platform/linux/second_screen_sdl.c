// SDL second screen: Linux/desktop frontend, compiled only in the make
// build (the Android build uses second_screen_sdl_stub.c instead).
// SDL version of the second screen (MinimapView.java) for dual-screen linux
// handhelds: map with follow-cam, dungeon automap, touch items/gear/settings,
// all drawn from art generated out of zelda3_assets.dat (see second_screen.c).
//
// Enabled with ZELDA3_SECOND_SCREEN=1. The window opens fullscreen on the
// second display when there is one; ZELDA3_SECOND_SCREEN_DISPLAY=n picks a
// display and ZELDA3_SECOND_SCREEN_TITLE overrides the window title for
// systems that route windows by title. Software renderer so it can't fight
// the game's GL context.
#include <SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __3DS__
#include <3ds.h>
#include "platform_3ds.h"
#else
enum Platform3DSDisplayMode {
  kPlatform3DSDisplayOriginal,
  kPlatform3DSDisplayUltraWideMod,
  kPlatform3DSDisplayStretch,
};

enum Platform3DSCStickMode {
  kPlatform3DSCStickTurbo,
  kPlatform3DSCStickWalk,
  kPlatform3DSCStickDisabled,
};
#endif

#include "../../types.h"                 // uint8/uint16 for the tables header
#include "../../second_screen_tables.h"  // kIconCount/kIconCols/kGlyphCount/kGlyphCols
#include "ss_sheets.h"             // generated cell indices for icons/glyphs/letters
#include "ss_textures.h"           // baked theme background tiles (menu/parchment/stone)

// API provided by second_screen.c
int  SS_GetLinkX(void);
int  SS_GetLinkY(void);
int  SS_GetArea(void);
int  SS_GetModule(void);
bool SS_IsIndoors(void);
void SS_ReadSram(uint8_t *out, int n);
int  SS_GetEquippedSlot(void);
int  SS_GetDungeon(void);
bool SS_GetIndoorExit(int *out);
void SS_ReadDungFlags(uint8_t *out, int n);
bool SS_RenderIconSheet(uint32_t *px);
bool SS_RenderGlyphSheet(uint32_t *px);
bool SS_RenderLetterSheet(uint32_t *px);
bool SS_RenderWorldMap(uint32_t *px, bool dark);
bool SS_RenderLinkFace(uint32_t *px, int chunk);
int  SS_GetDungeonLayout(int palace, uint8_t *out, int cap);
bool SS_RenderDungeonFloor(int palace, int floorIdx, uint32_t *px);
bool SS_RenderMapIcons(int palace, uint32_t *px);
void SS_EquipSlot(int slot);
void SS_SetWidescreen(bool on);
bool SS_IsWidescreen(void);
void SS_Set3DSDisplayMode(int mode);
void SS_SetHudHidden(bool hide);
bool SS_IsHudHidden(void);
void SS_RequestMemoryDump(void);
void SS_ArmButtonCapture(bool arm);
int  SS_GetCapturedButton(void);
void SS_GetGamepadControls(int *out12);
void SS_SetGamepadControls(const int *in12);

// palette (MinimapView)
#define COL(r,g,b) (0xff000000u | ((r) << 16) | ((g) << 8) | (b))
enum {
  COL_GOLD        = COL(232, 194, 96),
  COL_GOLD_DARK   = COL(122, 88, 30),
  COL_OUTLINE     = COL(30, 22, 10),
  COL_BOX         = COL(12, 12, 12),
  COL_BOX_BORDER  = COL(96, 200, 120),
  COL_BOX_BORDER2 = COL(224, 176, 60),
  COL_STONE_EDGE_L= COL(134, 142, 158),
  COL_STONE_EDGE_D= COL(44, 50, 62),
  COL_STONE_INSET = COL(38, 44, 56),
  COL_PLAQUE      = COL(88, 96, 112),
  COL_PLAQUE_SEL  = COL(58, 108, 196),
  COL_BG_MENU     = COL(24, 28, 22),   // stand-ins for the tiled theme textures
  COL_BG_STONE    = COL(52, 58, 70),
  COL_BG_PARCH    = COL(214, 188, 138),
};

enum { TAB_MAP, TAB_ITEMS, TAB_GEAR, TAB_SETTINGS };
enum { MODE_GAME, MODE_TITLE, MODE_CINEMA };

typedef struct { float x, y, w, h; } RectFS;

// constants ported from MinimapView
static const char *const kItemNames[20] = {
  "bow", "boomerang", "hookshot", "bombs", "mushroom",
  "firerod", "icerod", "bombos", "ether", "quake",
  "torch", "hammer", "flute", "bugnet", "book",
  "bottle", "somaria", "byrna", "cape", "mirror",
};
static const int kPendantMarks[3][3] = {   // {bit, x, y}
  {4, 3928, 1600},   // Courage - Eastern Palace
  {2, 296, 3248},    // Power   - Desert Palace
  {1, 2160, 320},    // Wisdom  - Tower of Hera
};
static const int kCrystalMarks[7][3] = {
  {2, 3960, 1600},   // Palace of Darkness
  {16, 1888, 3776},  // Swamp Palace
  {64, 208, 320},    // Skull Woods
  {32, 384, 1888},   // Thieves' Town
  {4, 3168, 3660},   // Ice Palace
  {1, 320, 3376},    // Misery Mire
  {8, 3800, 256},    // Turtle Rock
};
static const char *const kDungeonNames[14] = {
  "SEWERS", "HYRULE CASTLE", "EASTERN PALACE", "DESERT PALACE", "CASTLE TOWER",
  "SWAMP PALACE", "DARK PALACE", "MISERY MIRE", "SKULL WOODS", "ICE PALACE",
  "TOWER OF HERA", "THIEVES TOWN", "TURTLE ROCK", "GANONS TOWER",
};
static const int kDungeonBoss[14]    = {15, 15, 200, 51, 32, 6, 90, 144, 41, 222, 7, 172, 164, 13};
static const int kDungeonBossPos[14] = {   // x<<8|y of the skull inside its room (kDungMap_Tab37)
  -1, -1, 0x808, 8, 0, 8, 0x808, 8, 0x808, 0x800, 0x404, 0x808, 8, 8,
};
static const int kDotPalette[4] = {0, 1, 2, 1};  // marker blink cycle (kDungMap_Tab38)

// joypad command names + gamepad button names, in the game's orders
static const char *const kPadCmdNames[12] = {
  "UP", "DOWN", "LEFT", "RIGHT", "SELECT", "START", "A", "B", "X", "Y", "L", "R",
};
static const char *const kPadButtonLabel[17] = {
  "A", "B", "X", "Y", "BACK", "GUIDE", "START", "L3", "R3",
  "L1", "R1", "D UP", "D DOWN", "D LEFT", "D RIGHT", "L2", "R2",
};
static const char *const kPadButtonIni[17] = {
  "A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3",
  "L1", "R1", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "L2", "R2",
};

// state
static SDL_Window   *ss_win;
static SDL_Renderer *ss_r;
static uint32_t      ss_winid;
static int           W, H;
static float         u = 1.0f;

static SDL_Texture *tex_map[2], *tex_icons, *tex_glyphs, *tex_letters, *tex_face;
static SDL_Texture *tex_floor, *tex_mapicons;
static SDL_Texture *tex_bg_menu, *tex_bg_parch, *tex_bg_stone;
static bool art_ready;

typedef struct {
  const char *name;
  int boss, floors, basements;
  uint8_t layout[16][25];
} Dungeon;
static Dungeon dungeons[14];

static int  tab = TAB_MAP;
static bool whole_map;
static int  tap_flash_slot = -1;
static uint32_t tap_flash_until;
static int  view_floor_offset;
static uint32_t view_floor_touched_at;

static bool has_last_outdoor;
static int  last_out_x, last_out_y, last_out_area;

static uint8_t sram[256];
static uint8_t dung_flags[0x500];

static void rebuild_renderer(int w2, int h2);
// set on SIZE_CHANGED, handled at the top of the next Update
static bool ss_needs_rebuild;

// touch rects recomputed every draw, used by the tap handler
static RectFS map_area_r, tab_items_r, tab_gear_r, tab_map_r, tab_settings_r, y_ring_r;
static RectFS settings_row_r[6], remap_row_r[12], remap_back_r;

// settings / remap state
static bool remap_mode;
static int  remap_arm = -1;         // row currently waiting for a button press
static uint32_t remap_arm_at;
static int  pad_controls[12];
static bool hud_pref_applied;
static uint32_t dump_flash_until;
static RectFS plaque_r[16];
static int    plaque_floor[16], plaque_count;
static float  grid_x, grid_y, grid_cell;

// live values snapshot for the current frame
static int cur_room, cur_floor_now, cur_palace;

static int sram8(int off) { return sram[off]; }
static int sram16(int off) { return sram[off] | (sram[off + 1] << 8); }
static int dung_flag(int room) {
  int off = room * 2;
  if (off + 1 >= (int)sizeof(dung_flags)) return 0;
  return dung_flags[off] | (dung_flags[off + 1] << 8);
}
static int bottle_level(void) {
  int sel = sram8(0x4F);
  if (sel <= 0) return 0;
  int v = sram8(0x5C + sel - 1);
  return v > 7 ? 7 : v;
}
static bool slot_owned(int i) {
  return (i == 15 ? sram8(0x4F) : sram8(0x40 + i)) > 0;
}
static int mode_for_module(int m) {
  if (m <= 0x05) return MODE_TITLE;
  if (m == 0x12 || m == 0x14 || m == 0x17 || (m >= 0x18 && m <= 0x1A)) return MODE_CINEMA;
  return MODE_GAME;
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static bool in_rect(const RectFS *r, float x, float y) {
  return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}
static float unit_for_size(int w, int h) {
  float unit = (w < h ? w : h) / 720.0f;
#ifdef __3DS__
  if (unit < 0.5f)
    unit = 0.5f;
#endif
  return unit;
}

// draw primitives
static void set_color(uint32_t c) {
  SDL_SetRenderDrawColor(ss_r, (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, (c >> 24) & 0xff);
}
static void fill_rect(float x, float y, float w, float h, uint32_t c) {
  SDL_FRect r = {x, y, w, h};
  set_color(c);
  SDL_RenderFillRectF(ss_r, &r);
}
static void draw_frame(float x, float y, float w, float h, float t, uint32_t c) {
  fill_rect(x, y, w, t, c);
  fill_rect(x, y + h - t, w, t, c);
  fill_rect(x, y, t, h, c);
  fill_rect(x + w - t, y, t, h, c);
}
// rounded-rect fill; nested insets give rounded borders
static void fill_round(float x, float y, float w, float h, float rad, uint32_t c) {
  if (rad > w / 2) rad = w / 2;
  if (rad > h / 2) rad = h / 2;
  set_color(c);
  SDL_FRect mid = {x, y + rad, w, h - 2 * rad};
  SDL_RenderFillRectF(ss_r, &mid);
  for (int i = 0; i < (int)rad; i++) {
    float dy = rad - i;
    float dx = rad - sqrtf(rad * rad - dy * dy);
    SDL_FRect t = {x + dx, y + i, w - 2 * dx, 1};
    SDL_FRect b = {x + dx, y + h - 1 - i, w - 2 * dx, 1};
    SDL_RenderFillRectF(ss_r, &t);
    SDL_RenderFillRectF(ss_r, &b);
  }
}
static void fill_circle(float cx, float cy, float r, uint32_t c) {
  set_color(c);
  for (int dy = (int)-r; dy <= (int)r; dy++) {
    float dx = sqrtf(r * r - dy * dy);
    SDL_FRect seg = {cx - dx, cy + dy, dx * 2, 1};
    SDL_RenderFillRectF(ss_r, &seg);
  }
}
static void stroke_circle(float cx, float cy, float r, float t, uint32_t c) {
  set_color(c);
  float ri = r - t;
  for (int dy = (int)-r; dy <= (int)r; dy++) {
    float dxo = sqrtf(r * r - dy * dy);
    float dxi = (float)fabs((double)dy) < ri ? sqrtf(ri * ri - dy * dy) : 0;
    SDL_FRect a = {cx - dxo, cy + dy, dxo - dxi, 1};
    SDL_FRect b = {cx + dxi, cy + dy, dxo - dxi, 1};
    SDL_RenderFillRectF(ss_r, &a);
    SDL_RenderFillRectF(ss_r, &b);
  }
}
static void draw_x_mark(float cx, float cy, float r, float t, uint32_t c) {
  set_color(c);
  for (float d = -r; d <= r; d += 1.0f) {
    SDL_FRect a = {cx + d - t / 2, cy + d - t / 2, t, t};
    SDL_FRect b = {cx + d - t / 2, cy - d - t / 2, t, t};
    SDL_RenderFillRectF(ss_r, &a);
    SDL_RenderFillRectF(ss_r, &b);
  }
}
static void tri_up(float cx, float top, float size, uint32_t c) {
  set_color(c);
  for (int i = 0; i <= (int)size; i++) {
    float half = size * (i / size) * 0.5773f * 2.0f;  // ~equilateral
    SDL_FRect seg = {cx - half / 2, top + i, half, 1};
    SDL_RenderFillRectF(ss_r, &seg);
  }
}

// blit one cell from a sheet texture
static void draw_cell(SDL_Texture *tex, int cell, int cellpx, int cols, float x, float y, float s) {
  if (cell < 0 || !tex) return;
  SDL_Rect src = {(cell % cols) * cellpx, (cell / cols) * cellpx, cellpx, cellpx};
  SDL_FRect dst = {x, y, cellpx * s, cellpx * s};
  SDL_RenderCopyF(ss_r, tex, &src, &dst);
}
static void draw_icon(int cell, float x, float y, float s)  { draw_cell(tex_icons, cell, 16, SS_ICON_COLS, x, y, s); }
static void draw_glyph(int cell, float x, float y, float s) { draw_cell(tex_glyphs, cell, 8, SS_GLYPH_COLS, x, y, s); }

static float text_width(const char *s, float sc) {
  float w = 0;
  for (; *s; s++) w += (*s == ' ' ? 5 : 8) * sc;
  return w;
}
static void draw_text(const char *s, float x, float y, float sc) {
  static const int kDigitGlyph[10] = {
    SS_GLYPH_DIGIT0, SS_GLYPH_DIGIT1, SS_GLYPH_DIGIT2, SS_GLYPH_DIGIT3, SS_GLYPH_DIGIT4,
    SS_GLYPH_DIGIT5, SS_GLYPH_DIGIT6, SS_GLYPH_DIGIT7, SS_GLYPH_DIGIT8, SS_GLYPH_DIGIT9,
  };
  float cx = x;
  for (; *s; s++) {
    char ch = *s;
    if (ch == ' ') { cx += 5 * sc; continue; }
    if (ch >= '0' && ch <= '9') draw_glyph(kDigitGlyph[ch - '0'], cx, y, sc);
    else if (ch >= 'A' && ch <= 'Z') draw_cell(tex_letters, kSS_LetterCell[ch - 'A'], 8, SS_LETTER_COLS, cx, y, sc);
    cx += 8 * sc;
  }
}
static void draw_number(int value, int digits, float x, float y, float s, bool yellow) {
  static const int kD[10]  = {SS_GLYPH_DIGIT0, SS_GLYPH_DIGIT1, SS_GLYPH_DIGIT2, SS_GLYPH_DIGIT3, SS_GLYPH_DIGIT4,
                              SS_GLYPH_DIGIT5, SS_GLYPH_DIGIT6, SS_GLYPH_DIGIT7, SS_GLYPH_DIGIT8, SS_GLYPH_DIGIT9};
  static const int kDy[10] = {SS_GLYPH_DIGIT0Y, SS_GLYPH_DIGIT1Y, SS_GLYPH_DIGIT2Y, SS_GLYPH_DIGIT3Y, SS_GLYPH_DIGIT4Y,
                              SS_GLYPH_DIGIT5Y, SS_GLYPH_DIGIT6Y, SS_GLYPH_DIGIT7Y, SS_GLYPH_DIGIT8Y, SS_GLYPH_DIGIT9Y};
  for (int i = digits - 1; i >= 0; i--) {
    draw_glyph((yellow ? kDy : kD)[value % 10], x + i * 8 * s, y, s);
    value /= 10;
  }
}

// ALttP menu-style box: black fill, colored double border, corner dots
static void menu_box(RectFS r, uint32_t border) {
  fill_round(r.x, r.y, r.w, r.h, 10 * u, COL_BOX);
  fill_round(r.x + 3 * u, r.y + 3 * u, r.w - 6 * u, r.h - 6 * u, 8 * u, border);
  fill_round(r.x + 7 * u, r.y + 7 * u, r.w - 14 * u, r.h - 14 * u, 6 * u, COL(200, 200, 200));
  fill_round(r.x + 9 * u, r.y + 9 * u, r.w - 18 * u, r.h - 18 * u, 6 * u, COL_BOX);
  float d = 3.5f * u;
  fill_circle(r.x + 8 * u, r.y + 8 * u, d, COL(255, 255, 255));
  fill_circle(r.x + r.w - 8 * u, r.y + 8 * u, d, COL(255, 255, 255));
  fill_circle(r.x + 8 * u, r.y + r.h - 8 * u, d, COL(255, 255, 255));
  fill_circle(r.x + r.w - 8 * u, r.y + r.h - 8 * u, d, COL(255, 255, 255));
}
static void slot_bg(float x, float y, float size) {
  fill_round(x, y, size, size, 10 * u, COL(70, 70, 70));
  fill_round(x + 2.5f * u, y + 2.5f * u, size - 5 * u, size - 5 * u, 8 * u, COL(30, 30, 30));
}

// textures from second_screen.c buffers
static SDL_Texture *make_tex(int w, int h, const void *px, bool blend) {
  SDL_Texture *t = SDL_CreateTexture(ss_r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
  if (!t) return NULL;
  SDL_UpdateTexture(t, NULL, px, w * 4);
  SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
  if (blend) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  return t;
}

// Tile a theme texture across r at 2x, clipped
static void draw_tiled(SDL_Texture *tex, int tw, int th, RectFS r, uint32_t fallback) {
  if (!tex) { fill_rect(r.x, r.y, r.w, r.h, fallback); return; }
  SDL_Rect clip = {(int)r.x, (int)r.y, (int)r.w, (int)r.h};
  SDL_RenderSetClipRect(ss_r, &clip);
  float sw = tw * 2.0f, sh = th * 2.0f;
  for (float y = r.y; y < r.y + r.h; y += sh)
    for (float x = r.x; x < r.x + r.w; x += sw) {
      SDL_FRect dst = {x, y, sw, sh};
      SDL_RenderCopyF(ss_r, tex, NULL, &dst);
    }
  SDL_RenderSetClipRect(ss_r, NULL);
}

static bool try_load_art(void) {
  static uint32_t buf[512 * 512];   // reused for every sheet; world map is the largest
  uint8_t lay[16 * 25];

  // theme tiles are baked into the binary
  if (!tex_bg_menu)  tex_bg_menu  = make_tex(kSSTexMenu_W, kSSTexMenu_H, kSSTexMenu, false);
  if (!tex_bg_parch) tex_bg_parch = make_tex(kSSTexParch_W, kSSTexParch_H, kSSTexParch, false);
  if (!tex_bg_stone) tex_bg_stone = make_tex(kSSTexStone_W, kSSTexStone_H, kSSTexStone, false);

  // cheap probe: the engine hasn't parsed zelda3_assets.dat yet
  if (SS_GetDungeonLayout(0, lay, sizeof(lay)) < 0) return false;
  if (!SS_RenderWorldMap(buf, false)) return false;
  tex_map[0] = make_tex(512, 512, buf, false);
  SS_RenderWorldMap(buf, true);
  tex_map[1] = make_tex(512, 512, buf, false);

  SS_RenderIconSheet(buf);
  tex_icons = make_tex(SS_ICON_COLS * 16, ((kIconCount + kIconCols - 1) / kIconCols) * 16, buf, true);
  SS_RenderGlyphSheet(buf);
  tex_glyphs = make_tex(SS_GLYPH_COLS * 8, ((kGlyphCount + kGlyphCols - 1) / kGlyphCols) * 8, buf, true);
  SS_RenderLetterSheet(buf);
  tex_letters = make_tex(16 * 8, 2 * 8, buf, true);
  SS_RenderLinkFace(buf, 0);
  tex_face = make_tex(16, 16, buf, true);

  tex_floor = SDL_CreateTexture(ss_r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 80, 80);
  SDL_SetTextureBlendMode(tex_floor, SDL_BLENDMODE_BLEND);
  tex_mapicons = SDL_CreateTexture(ss_r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 32, 8);
  SDL_SetTextureBlendMode(tex_mapicons, SDL_BLENDMODE_BLEND);

  for (int i = 0; i < 14; i++) {
    int r = SS_GetDungeonLayout(i, lay, sizeof(lay));
    if (r < 0) return false;
    Dungeon *d = &dungeons[i];
    d->name = kDungeonNames[i];
    d->boss = kDungeonBoss[i];
    d->floors = r & 0xFF;
    if (d->floors > 16) d->floors = 16;
    d->basements = (r >> 8) & 0xFF;
    for (int f = 0; f < d->floors; f++)
      memcpy(d->layout[f], lay + f * 25, 25);
  }
  return true;
}

// panels

static void draw_cinema(void) {
  fill_rect(0, 0, W, H, COL_BOX & 0xff000000u);  // black
  draw_frame(12 * u, 12 * u, W - 24 * u, H - 24 * u, 2 * u, COL_GOLD_DARK);
  float t = SDL_GetTicks() / 1000.0f;
  float pulse = sinf(t * 1.5f) * 0.5f + 0.5f;
  uint8_t g = (uint8_t)(150 + 100 * pulse);
  uint32_t c = COL(g, (uint8_t)(g * 0.83f), (uint8_t)(g * 0.41f));
  float s = (W < H ? W : H) * 0.06f;
  float cx = W / 2.0f, cy = H / 2.0f;
  tri_up(cx, cy - s, s, c);            // top
  tri_up(cx - s * 0.58f, cy, s, c);    // bottom-left
  tri_up(cx + s * 0.58f, cy, s, c);    // bottom-right
}

static void draw_overworld(RectFS r, int link_x, int link_y, int area) {
  // parchment sheet with gold frame
  draw_tiled(tex_bg_parch, kSSTexParch_W, kSSTexParch_H, r, COL_BG_PARCH);
  draw_frame(r.x + u, r.y + u, r.w - 2 * u, r.h - 2 * u, 3 * u, COL_GOLD_DARK);
  draw_frame(r.x + 4 * u, r.y + 4 * u, r.w - 8 * u, r.h - 8 * u, 2 * u, COL_GOLD);

  float pad = 8 * u;
  RectFS m = {r.x + pad, r.y + pad, r.w - 2 * pad, r.h - 2 * pad};

  bool dark = (area & 0x40) != 0;
  SDL_Texture *map = tex_map[dark ? 1 : 0];
  float px = 128.0f + (link_x / 4096.0f) * 256.0f;
  float py = 128.0f + (link_y / 4096.0f) * 256.0f;

  SDL_Rect clip = {(int)m.x, (int)m.y, (int)m.w, (int)m.h};
  SDL_RenderSetClipRect(ss_r, &clip);

  float scale, ox, oy;
  if (whole_map) {
    scale = (m.w < m.h ? m.w : m.h) / 512.0f;
    ox = m.x + m.w / 2 - 256.0f * scale;
    oy = m.y + m.h / 2 - 256.0f * scale;
  } else {
    scale = 2.6f * u;
    float cxm = clampf(px, m.w / scale / 2, 512.0f - m.w / scale / 2);
    float cym = clampf(py, m.h / scale / 2, 512.0f - m.h / scale / 2);
    ox = m.x + m.w / 2 - cxm * scale;
    oy = m.y + m.h / 2 - cym * scale;
  }
  SDL_FRect dst = {ox, oy, 512 * scale, 512 * scale};
  SDL_RenderCopyF(ss_r, map, NULL, &dst);

  // X marks for unclaimed pendants / crystals
  const int (*marks)[3] = dark ? kCrystalMarks : kPendantMarks;
  int nmarks = dark ? 7 : 3;
  int owned = sram8(dark ? 0x7A : 0x74);
  for (int i = 0; i < nmarks; i++) {
    if (owned & marks[i][0]) continue;
    float mx = ox + (128.0f + marks[i][1] / 4096.0f * 256.0f) * scale;
    float my = oy + (128.0f + marks[i][2] / 4096.0f * 256.0f) * scale;
    draw_x_mark(mx, my, 8 * u, 8 * u, COL_OUTLINE);
    draw_x_mark(mx, my, 8 * u, 4.5f * u, COL(224, 40, 32));
  }

  // Link's bobbing head
  float fx = ox + px * scale, fy = oy + py * scale;
  float bob = sinf(SDL_GetTicks() / 300.0f) * 2 * u;
  float fs = (whole_map ? 1.2f : 1.6f) * u * 2;   // face tex is 16px (Java pre-scaled to 32)
  SDL_FRect fdst = {fx - 16 * fs / 2, fy - 16 * fs / 2 + bob, 16 * fs, 16 * fs};
  SDL_RenderCopyF(ss_r, tex_face, NULL, &fdst);
  SDL_RenderSetClipRect(ss_r, NULL);

  // zoom toggle button
  float bs2 = 56 * u, bx = r.x + 14 * u, by = r.y + 14 * u;
  fill_round(bx, by, bs2, bs2, 8 * u, COL_BOX);
  fill_round(bx + 3 * u, by + 3 * u, bs2 - 6 * u, bs2 - 6 * u, 6 * u, COL_BOX_BORDER2);
  fill_round(bx + 6 * u, by + 6 * u, bs2 - 12 * u, bs2 - 12 * u, 5 * u, COL_BOX);
  float cxb = bx + bs2 / 2, cyb = by + bs2 / 2, arm = 14 * u, th = 5 * u;
  fill_rect(cxb - arm, cyb - th / 2, arm * 2, th, COL(255, 255, 255));
  if (whole_map) fill_rect(cxb - th / 2, cyb - arm, th, arm * 2, COL(255, 255, 255));
}

static void draw_dungeon(RectFS r, int link_x, int link_y, int room, int dungeon_info) {
  int palace = dungeon_info & 0xFF;
  int floor = (int8_t)(dungeon_info >> 8);
  Dungeon *d = (palace >= 0 && palace < 14) ? &dungeons[palace] : NULL;

  float bs = 3 * u;
  const char *name = d ? d->name : "DUNGEON";
  float tw = text_width(name, bs);
  float bx = r.x + r.w / 2 - tw / 2, by = r.y + 20 * u;
  fill_round(bx - 20 * u, by - 9 * u, tw + 40 * u, 8 * bs + 18 * u, 8 * u, COL_STONE_EDGE_L);
  fill_round(bx - 18 * u, by - 7 * u, tw + 36 * u, 8 * bs + 14 * u, 7 * u, COL_STONE_INSET);
  draw_text(name, bx, by, bs);
  if (!d) return;

  if (view_floor_touched_at && SDL_GetTicks() - view_floor_touched_at > 6000) {
    view_floor_offset = 0;
    view_floor_touched_at = 0;
  }
  int li = floor + view_floor_offset + d->basements;
  if (li < 0) li = 0;
  if (li > d->floors - 1) li = d->floors - 1;
  int view_floor = li - d->basements;

  // floor plaques down the left side
  plaque_count = 0;
  float ph = 50 * u, pw = 100 * u, pgap = 8 * u;
  float px0 = r.x + 24 * u, py0 = r.y + 78 * u;
  for (int f = d->floors - 1; f >= 0; f--) {
    int fl = f - d->basements;
    if (plaque_count >= 16) break;
    RectFS *pr = &plaque_r[plaque_count];
    *pr = (RectFS){px0, py0, pw, ph};
    plaque_floor[plaque_count] = fl;
    plaque_count++;
    bool sel = (fl == view_floor);
    fill_round(pr->x, pr->y, pr->w, pr->h, 6 * u, sel ? COL(160, 200, 255) : COL_STONE_EDGE_L);
    fill_round(pr->x + 2 * u, pr->y + 2 * u, pr->w - 4 * u, pr->h - 4 * u, 5 * u,
               sel ? COL_PLAQUE_SEL : COL_PLAQUE);
    char label[16];
    if (fl >= 0) snprintf(label, sizeof(label), "%dF", fl + 1);
    else snprintf(label, sizeof(label), "B%d", -fl);
    draw_text(label, pr->x + pr->w / 2 - text_width(label, 2 * u) / 2 + 8 * u,
              pr->y + pr->h / 2 - 8 * u, 2 * u);
    if (fl == floor) {
      SDL_FRect fdst = {pr->x + 4 * u, pr->y + pr->h / 2 - 13 * u, 16 * 1.7f * u, 16 * 1.7f * u};
      SDL_RenderCopyF(ss_r, tex_face, NULL, &fdst);
    }
    py0 += ph + pgap;
  }

  // the floor map square
  float inset = 20 * u;
  float mx0 = px0 + pw + 28 * u, my0 = r.y + 74 * u;
  float avail_w = r.x + r.w - inset - mx0, avail_h = r.y + r.h - inset - my0;
  float msize = avail_w < avail_h ? avail_w : avail_h;
  mx0 += (avail_w - msize) / 2;
  my0 += (avail_h - msize) / 2;
  fill_round(mx0, my0, msize, msize, 10 * u, COL_STONE_EDGE_D);
  fill_round(mx0 + 3 * u, my0 + 3 * u, msize - 6 * u, msize - 6 * u, 8 * u, COL_STONE_INSET);

  const uint8_t *lay = d->layout[li];
  float cell = (msize - 24 * u) / 5.0f;
  float gx = mx0 + 12 * u, gy = my0 + 12 * u;

  // the floor's rooms with the game's own map tiles
  static uint32_t floor_buf[80 * 80];
  if (!SS_RenderDungeonFloor(palace, li, floor_buf)) return;
  SDL_UpdateTexture(tex_floor, NULL, floor_buf, 80 * 4);
  SDL_FRect fdst = {gx, gy, 5 * cell, 5 * cell};
  SDL_RenderCopyF(ss_r, tex_floor, NULL, &fdst);

  // overlay sprites: blinking here-dot + boss skull
  static uint32_t icon_buf[32 * 8];
  bool icons = SS_RenderMapIcons(palace, icon_buf);
  if (icons) SDL_UpdateTexture(tex_mapicons, NULL, icon_buf, 32 * 4);
  bool has_compass = (sram16(0x64) & (0x8000 >> palace)) != 0;
  uint32_t frame = SDL_GetTicks() / 17;
  float ms = cell / 16.0f;

  for (int i = 0; i < 25; i++) {
    int v = lay[i];
    if (v == 0x0F) continue;
    int col = i % 5, row = i / 5;
    float x = gx + col * cell, y = gy + row * cell;
    bool is_cur = (v == (room & 0xFF)) && view_floor == floor;

    if (icons && has_compass && palace >= 2 && v == d->boss &&
        (dung_flag(v) & 0x800) == 0 && (frame & 0xF) < 10) {
      int pos = kDungeonBossPos[palace];
      if (pos >= 0) {
        float sx = x + (pos >> 8) * ms, sy = y + (pos & 0xFF) * ms;
        SDL_Rect src = {24, 0, 8, 8};
        SDL_FRect dd = {sx, sy, 8 * ms, 8 * ms};
        SDL_RenderCopyF(ss_r, tex_mapicons, &src, &dd);
      }
    }
    if (is_cur) {
      draw_frame(x + 1.5f * u, y + 1.5f * u, cell - 3 * u, cell - 3 * u, 3 * u, COL_GOLD);
      if (icons) {
        int p = kDotPalette[(frame >> 2) & 3];
        float sx = x + (((link_x & 0x1E0) >> 5) - 3) * ms;
        float sy = y + (((link_y & 0x1E0) >> 5) - 3) * ms;
        SDL_Rect src = {p * 8, 0, 8, 8};
        SDL_FRect dd = {sx, sy, 8 * ms, 8 * ms};
        SDL_RenderCopyF(ss_r, tex_mapicons, &src, &dd);
      }
    }
  }
}

static void draw_items(RectFS r) {
  menu_box(r, COL_BOX_BORDER);
  draw_text("ITEMS", r.x + r.w / 2 - text_width("ITEMS", 3 * u) / 2, r.y + 18 * u, 3 * u);

  float cw1 = (r.w - 70 * u) / 5, cw2 = (r.h - 100 * u) / 4;
  float cellW = cw1 < cw2 ? cw1 : cw2;
  grid_cell = cellW;
  grid_x = r.x + r.w / 2 - cellW * 2.5f;
  grid_y = r.y + 40 * u + (r.h - 40 * u - 4 * cellW) / 2;

  int equipped = SS_GetEquippedSlot();
  for (int i = 0; i < 20; i++) {
    int col = i % 5, row = i / 5;
    float x = grid_x + col * cellW, y = grid_y + row * cellW;
    if (i + 1 == equipped) {
      fill_round(x + 4 * u, y + 4 * u, cellW - 8 * u, cellW - 8 * u, 10 * u, COL_GOLD);
      fill_round(x + 8 * u, y + 8 * u, cellW - 16 * u, cellW - 16 * u, 7 * u, COL(46, 40, 16));
    }
    if (i == tap_flash_slot && SDL_GetTicks() < tap_flash_until)
      fill_round(x + 4 * u, y + 4 * u, cellW - 8 * u, cellW - 8 * u, 10 * u, COL(90, 82, 56));
    int lv = (i == 15) ? bottle_level() : sram8(0x40 + i);
    if (lv <= 0) continue;
    if (lv > kSS_ItemMaxLevel[i]) lv = kSS_ItemMaxLevel[i];
    float is = (cellW - 24 * u) / 16.0f;
    is = clampf(is, 3 * u, 6 * u);
    draw_icon(kSS_ItemCell[i][lv], x + (cellW - 16 * is) / 2, y + (cellW - 16 * is) / 2, is);
  }
  (void)kItemNames;
}

static void draw_gear(RectFS r) {
  menu_box(r, COL_BOX_BORDER2);
  draw_text("GEAR", r.x + r.w / 2 - text_width("GEAR", 3 * u) / 2, r.y + 18 * u, 3 * u);

  float Hh = r.h;
  float x0 = r.x + 56 * u;
  float step = (r.w - 112 * u) / 7.0f;
  float s = (step - 24 * u) / 16.0f;
  if (s > 4 * u) s = 4 * u;
  float y0 = r.y + 0.14f * Hh;

  // row 1: sword shield armor gloves boots flippers pearl
  int sword = sram8(0x59), shield = sram8(0x5A);
  int gear_cells[7];
  gear_cells[0] = (sword > 0 && sword != 0xFF) ? SS_ICON_SWORD_1 + (sword > 4 ? 3 : sword - 1) : -1;
  gear_cells[1] = (shield > 0 && shield != 0xFF) ? SS_ICON_SHIELD_1 + (shield > 3 ? 2 : shield - 1) : -1;
  gear_cells[2] = SS_ICON_ARMOR_0 + (sram8(0x5B) > 2 ? 2 : sram8(0x5B));
  gear_cells[3] = sram8(0x54) > 0 ? SS_ICON_GLOVES_1 + (sram8(0x54) > 2 ? 1 : sram8(0x54) - 1) : -1;
  gear_cells[4] = sram8(0x55) > 0 ? SS_ICON_BOOTS_1 : -1;
  gear_cells[5] = sram8(0x56) > 0 ? SS_ICON_FLIPPERS_1 : -1;
  gear_cells[6] = sram8(0x57) > 0 ? SS_ICON_MOONPEARL_1 : -1;
  for (int i = 0; i < 7; i++) {
    float x = x0 + i * step + (step - 16 * s) / 2;
    slot_bg(x - 8 * u, y0 - 8 * u, 16 * s + 16 * u);
    if (gear_cells[i] >= 0) draw_icon(gear_cells[i], x, y0, s);
  }

  // row 2: bottles (left) + pendants (right)
  float y1 = y0 + 16 * s + 0.115f * Hh;
  draw_text("BOTTLES", x0, y1 - 40 * u, 2.4f * u);
  for (int i = 0; i < 4; i++) {
    float x = x0 + i * step + (step - 16 * s) / 2;
    slot_bg(x - 8 * u, y1 - 8 * u, 16 * s + 16 * u);
    int lv = sram8(0x5C + i);
    if (lv > 7) lv = 7;
    if (lv > 0) draw_icon(SS_ICON_BOTTLE_1 + (lv - 1), x, y1, s);
  }
  float px = x0 + 4.3f * step;
  float right_w = r.x + r.w - 20 * u - px;
  draw_text("PENDANTS", px, y1 - 40 * u, 2.4f * u);
  int pend = sram8(0x74);
  static const int pbit[3] = {4, 2, 1};
  static const uint32_t pcol[3] = {COL(64, 200, 88), COL(70, 110, 240), COL(230, 60, 60)};
  for (int i = 0; i < 3; i++) {
    float cxp = px + i * 66 * u + 24 * u, cyp = y1 + 30 * u;
    fill_circle(cxp, cyp, 22 * u, (pend & pbit[i]) ? pcol[i] : COL(34, 34, 34));
    stroke_circle(cxp, cyp, 22 * u, 4 * u, COL_GOLD_DARK);
  }
  // crystals under pendants
  float cyC = y1 + 0.14f * Hh;
  draw_text("CRYSTALS", px, cyC, 2.4f * u);
  float cs = right_w / 7.0f;
  if (cs > 40 * u) cs = 40 * u;
  int owned7 = sram8(0x7A) & 0x7F, n_owned = 0;
  while (owned7) { n_owned += owned7 & 1; owned7 >>= 1; }
  for (int i = 0; i < 7; i++) {
    float cxp = px + i * cs + 14 * u, cyp = cyC + 52 * u;
    fill_circle(cxp, cyp, 14 * u, i < n_owned ? COL(110, 160, 255) : COL(34, 34, 34));
    stroke_circle(cxp, cyp, 14 * u, 3 * u, COL_GOLD_DARK);
  }
  // counters + heart pieces
  float cg = 3 * u;
  float cx0 = x0, ax0 = x0 + 2.6f * step;
  float yc = cyC + 100 * u;
  static const int kBombCap[8]  = {10, 15, 20, 25, 30, 35, 40, 50};
  static const int kArrowCap[8] = {30, 35, 40, 45, 50, 55, 60, 70};
  bool bombs_max = sram8(0x43) >= kBombCap[sram8(0x70) & 7];
  bool arrows_max = sram8(0x77) >= kArrowCap[sram8(0x71) & 7];
  draw_glyph(SS_GLYPH_BOMB0, cx0, yc, cg); draw_glyph(SS_GLYPH_BOMB1, cx0 + 8 * cg, yc, cg);
  draw_number(sram8(0x43), 2, cx0 + 18 * cg, yc, cg, bombs_max);
  draw_glyph(SS_GLYPH_ARROW0, ax0, yc, cg); draw_glyph(SS_GLYPH_ARROW1, ax0 + 8 * cg, yc, cg);
  draw_number(sram8(0x77), 2, ax0 + 18 * cg, yc, cg, arrows_max);
  float yp = yc + 50 * u;
  draw_glyph(SS_GLYPH_HEART_FULL, cx0, yp, 4 * u);
  int pieces = sram8(0x6B) & 3;
  for (int i = 0; i < 4; i++) {
    float gx = cx0 + 48 * u + i * 30 * u, gy = yp + 4 * u;
    fill_round(gx - 2.5f * u, gy - 2.5f * u, 25 * u, 25 * u, 6 * u, COL_GOLD_DARK);
    fill_round(gx, gy, 20 * u, 20 * u, 5 * u, i < pieces ? COL(235, 80, 80) : COL(40, 34, 30));
  }
}

static void draw_sidebar(float x, float y, float w, float h, bool dungeon_mode) {
  float s = 3 * u;
  bool show_keys = dungeon_mode && sram8(0x6F) != 0xFF;
  float chip_h = (show_keys ? 40 : 30) * s + 20 * u;
  menu_box((RectFS){x, y, w, chip_h}, COL_BOX_BORDER);
  float ry = y + 12 * u, ix = x + 10 * u, ne = x + w - 10 * u;
  draw_glyph(SS_GLYPH_RUPEE, ix + 4 * s, ry, s);
  int rupees = sram16(0x62); if (rupees > 9999) rupees = 9999;
  draw_number(rupees, 4, ne - 32 * s, ry, s, false);
  static const int kBombCap[8]  = {10, 15, 20, 25, 30, 35, 40, 50};
  static const int kArrowCap[8] = {30, 35, 40, 45, 50, 55, 60, 70};
  bool bombs_max = sram8(0x43) >= kBombCap[sram8(0x70) & 7];
  bool arrows_max = sram8(0x77) >= kArrowCap[sram8(0x71) & 7];
  ry += 10 * s;
  draw_glyph(SS_GLYPH_BOMB0, ix, ry, s); draw_glyph(SS_GLYPH_BOMB1, ix + 8 * s, ry, s);
  draw_number(sram8(0x43), 2, ne - 16 * s, ry, s, bombs_max);
  ry += 10 * s;
  draw_glyph(SS_GLYPH_ARROW0, ix, ry, s); draw_glyph(SS_GLYPH_ARROW1, ix + 8 * s, ry, s);
  draw_number(sram8(0x77), 2, ne - 16 * s, ry, s, arrows_max);
  if (show_keys) {
    ry += 10 * s;
    draw_glyph(SS_GLYPH_KEY, ix + 4 * s, ry, s);
    draw_number(sram8(0x6F), 1, ne - 8 * s, ry, s, false);
  }

  // magic bar + hearts anchored at the bottom; ring centered in what's left
  float hs = 19 * u, bar_h = 18 * u;
  bool half_magic = sram8(0x7B) >= 1;
  float my = y + h - bar_h - 6 * u;
  float hy = my - 2 * hs - 16 * u - (half_magic ? 18 * u : 0);

  // equipped item ring: tap cycles to the next owned item
  float ring_r = 66 * u;
  float rcx = x + w / 2, rcy = ((y + chip_h) + hy) / 2;
  y_ring_r = (RectFS){rcx - ring_r, rcy - ring_r, ring_r * 2, ring_r * 2};
  fill_circle(rcx, rcy, ring_r, COL(12, 12, 12));
  stroke_circle(rcx, rcy, ring_r, 6 * u, COL_GOLD_DARK);
  stroke_circle(rcx, rcy, ring_r - 3 * u, 2.5f * u, COL_GOLD);
  int slot = SS_GetEquippedSlot();
  if (slot >= 1 && slot <= 20) {
    int i = slot - 1;
    int lv = (i == 15) ? bottle_level() : sram8(0x40 + i);
    if (lv < 0) lv = 0;
    if (lv > kSS_ItemMaxLevel[i]) lv = kSS_ItemMaxLevel[i];
    if (lv > 0) draw_icon(kSS_ItemCell[i][lv], rcx - 40 * u, rcy - 40 * u, 5 * u);
  }
  draw_text("Y", rcx + ring_r - 20 * u, rcy - ring_r + 4 * u, 2 * u);

  // hearts (live health)
  int cap = sram8(0x6C) >> 3; if (cap > 20) cap = 20;
  int cur = sram8(0x6D);
  int row_n = cap < 10 ? cap : 10;
  float hx0 = x + (w - row_n * hs) / 2;
  for (int i = 0; i < cap; i++) {
    int g = i < (cur >> 3) ? SS_GLYPH_HEART_FULL
          : (i == (cur >> 3) && (cur & 7) >= 4 ? SS_GLYPH_HEART_HALF : SS_GLYPH_HEART_EMPTY);
    draw_glyph(g, hx0 + (i % 10) * hs, hy + (i / 10) * hs, 2.2f * u);
  }

  // magic bar (with the HUD's 1/2 marker when the upgrade is owned)
  if (half_magic) {
    float gx = x + (w - 48 * u) / 2;
    draw_glyph(SS_GLYPH_HALF0, gx, my - 20 * u, 2 * u);
    draw_glyph(SS_GLYPH_HALF1, gx + 16 * u, my - 20 * u, 2 * u);
    draw_glyph(SS_GLYPH_HALF2, gx + 32 * u, my - 20 * u, 2 * u);
  }
  int magic = sram8(0x6E); if (magic > 128) magic = 128;
  fill_round(x + 16 * u, my, w - 32 * u, bar_h, 5 * u, COL_GOLD_DARK);
  fill_round(x + 18 * u, my + 2 * u, w - 36 * u, bar_h - 4 * u, 4 * u, COL_BOX);
  float frac = magic / 128.0f;
  if (frac > 0)
    fill_round(x + 19 * u, my + 3 * u, (w - 38 * u) * frac, bar_h - 6 * u, 3 * u, COL(72, 208, 72));
}

// Rewrite one `key = value` line inside a section of zelda3.ini
static void update_ini(const char *section, const char *key, const char *value) {
  FILE *f = fopen("zelda3.ini", "rb");
  if (!f) return;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0 || size > 1 << 20) { fclose(f); return; }
  char *buf = malloc(size + 1);
  if (!buf || fread(buf, 1, size, f) != (size_t)size) { free(buf); fclose(f); return; }
  fclose(f);
  buf[size] = 0;

  char *out = malloc(size + 256);
  if (!out) { free(buf); return; }
  size_t olen = 0, klen = strlen(key);
  bool done = false;
  char cur[64] = "";
  char *line = buf;
  while (line) {
    char *nl = strchr(line, '\n');
    size_t len = nl ? (size_t)(nl - line) + 1 : strlen(line);
    const char *t = line;
    while (*t == ' ' || *t == '\t') t++;
    if (*t == '[') {
      size_t n = 0;
      while (t[n] && t[n] != '\n' && t[n] != '\r' && n < sizeof(cur) - 1) { cur[n] = t[n]; n++; }
      cur[n] = 0;
    } else if (!done && !SDL_strcasecmp(cur, section) &&
               !SDL_strncasecmp(t, key, klen)) {
      const char *after = t + klen;
      while (*after == ' ' || *after == '\t') after++;
      if (*after == '=') {
        olen += sprintf(out + olen, "%s = %s\n", key, value);
        done = true;
        line = nl ? nl + 1 : NULL;
        continue;
      }
    }
    memcpy(out + olen, line, len);
    olen += len;
    line = nl ? nl + 1 : NULL;
  }
  if (!done)
    olen += sprintf(out + olen, "%s\n%s = %s\n", section, key, value);
  f = fopen("zelda3.ini", "wb");
  if (f) { fwrite(out, 1, olen, f); fclose(f); }
  free(out);
  free(buf);
}

static void write_ini_gamepad_controls(void) {
  char v[256];
  size_t n = 0;
  for (int i = 0; i < 12; i++) {
    if (i) n += sprintf(v + n, ", ");
    if (pad_controls[i] >= 0 && pad_controls[i] < 17)
      n += sprintf(v + n, "%s", kPadButtonIni[pad_controls[i]]);
  }
  update_ini("[GamepadMap]", "Controls", v);
}

static void leave_remap(void) {
  if (remap_arm >= 0) SS_ArmButtonCapture(false);
  remap_arm = -1;
  remap_mode = false;
}

static void draw_cog(float cx, float cy, float r) {
  for (int i = 0; i < 8; i++) {
    float a = (float)M_PI / 4 * i;
    fill_circle(cx + cosf(a) * r, cy + sinf(a) * r, r * 0.3f, COL(255, 255, 255));
  }
  fill_circle(cx, cy, r * 0.85f, COL(255, 255, 255));
  fill_circle(cx, cy, r * 0.38f, COL_BOX);
}

static void draw_settings_row(RectFS *row, bool armed) {
  fill_round(row->x, row->y, row->w, row->h, 8 * u, armed ? COL_GOLD : COL_GOLD_DARK);
  fill_round(row->x + 3 * u, row->y + 3 * u, row->w - 6 * u, row->h - 6 * u, 6 * u,
             armed ? COL(58, 48, 12) : COL(28, 28, 28));
}

static void draw_remap_panel(RectFS r) {
  draw_text("REMAP BUTTONS", r.x + r.w / 2 - text_width("REMAP BUTTONS", 3 * u) / 2,
            r.y + 18 * u, 3 * u);
  remap_back_r = (RectFS){r.x + 20 * u, r.y + 12 * u, 90 * u, 38 * u};
  draw_settings_row(&remap_back_r, false);
  draw_text("BACK", remap_back_r.x + remap_back_r.w / 2 - text_width("BACK", 2.2f * u) / 2,
            remap_back_r.y + remap_back_r.h / 2 - 9 * u, 2.2f * u);

  // resolve a pending capture from the game thread
  if (remap_arm >= 0) {
    int b = SS_GetCapturedButton();
    if (b >= 0) {
      pad_controls[remap_arm] = b;
      SS_SetGamepadControls(pad_controls);
      write_ini_gamepad_controls();
      remap_arm = -1;
    } else if (b == -1 || SDL_GetTicks() - remap_arm_at > 8000) {
      SS_ArmButtonCapture(false);
      remap_arm = -1;
    }
  }

  float row_h = 58 * u, gap = 12 * u;
  float col_w = (r.w - 3 * 24 * u) / 2;
  float y0 = r.y + 70 * u;
  for (int i = 0; i < 12; i++) {
    int col = i / 6, row_i = i % 6;
    float x = r.x + 24 * u + col * (col_w + 24 * u);
    float y = y0 + row_i * (row_h + gap);
    RectFS *row = &remap_row_r[i];
    *row = (RectFS){x, y, col_w, row_h};
    bool armed = remap_arm == i;
    draw_settings_row(row, armed);
    float ty = row->y + row->h / 2 - 9 * u;
    draw_text(kPadCmdNames[i], row->x + 14 * u, ty, 2.2f * u);
    const char *v = armed ? "PRESS KEY"
        : (pad_controls[i] >= 0 && pad_controls[i] < 17 ? kPadButtonLabel[pad_controls[i]] : "----");
    draw_text(v, row->x + row->w - 14 * u - text_width(v, 2.2f * u), ty, 2.2f * u);
  }
}

static void draw_settings(RectFS r) {
  menu_box(r, COL_BOX_BORDER);
  if (remap_mode) {
    draw_remap_panel(r);
    return;
  }
  draw_text("SETTINGS", r.x + r.w / 2 - text_width("SETTINGS", 3 * u) / 2, r.y + 18 * u, 3 * u);

  bool hud_hidden = SS_IsHudHidden();
  const char *display_value = SS_IsWidescreen() ? "WIDE MOD" : "ORIGINAL";
  const char *cstick_value = "TURBO";
  char turbo_value[8];
#ifdef __3DS__
  switch (Platform3DS_GetDisplayMode()) {
  case kPlatform3DSDisplayOriginal: display_value = "ORIGINAL"; break;
  case kPlatform3DSDisplayStretch: display_value = "STRETCH"; break;
  case kPlatform3DSDisplayUltraWideMod:
  default: display_value = "WIDE MOD"; break;
  }
  switch (Platform3DS_GetCStickMode()) {
  case kPlatform3DSCStickWalk: cstick_value = "WALK"; break;
  case kPlatform3DSCStickDisabled: cstick_value = "OFF"; break;
  case kPlatform3DSCStickTurbo:
  default: cstick_value = "TURBO"; break;
  }
  snprintf(turbo_value, sizeof(turbo_value), "X%d", Platform3DS_GetTurboMultiplier());
#else
  snprintf(turbo_value, sizeof(turbo_value), "X5");
#endif
  static const char *const labels[6] = {
    "TOP SCREEN", "C STICK", "TURBO SPEED",
    "TOP HUD", "REMAP BUTTONS", "MEM DUMP",
  };
  const char *values[6] = {
    display_value, cstick_value, turbo_value,
    hud_hidden ? "OFF" : "ON", "", SDL_GetTicks() < dump_flash_until ? "DONE" : "WRITE",
  };
  float row_h = 44 * u, gap = 8 * u;
  float y0 = r.y + 55 * u;
  for (int i = 0; i < 6; i++) {
    RectFS *row = &settings_row_r[i];
    *row = (RectFS){r.x + 28 * u, y0 + i * (row_h + gap), r.w - 56 * u, row_h};
    draw_settings_row(row, false);
    float ty = row->y + row->h / 2 - 8 * u;
    draw_text(labels[i], row->x + 16 * u, ty, 2 * u);
    if (values[i][0] == 0) {
      // chevron for the remap sub-screen
      float ax = row->x + row->w - 26 * u, ay = row->y + row->h / 2;
      for (float d = 0; d < 10 * u; d += 1.0f) {
        fill_rect(ax - 6 * u + d, ay - 8 * u + d * 0.8f, 4 * u, 2 * u, COL_GOLD);
        fill_rect(ax - 6 * u + d, ay + 8 * u - d * 0.8f - 2 * u, 4 * u, 2 * u, COL_GOLD);
      }
    } else {
      draw_text(values[i], row->x + row->w - 16 * u - text_width(values[i], 2 * u), ty, 2 * u);
    }
  }
}

static void draw_tab_button(RectFS r, const char *label, bool active) {
  uint32_t bg = active ? COL(40, 34, 12) : COL_BOX;
  fill_round(r.x, r.y, r.w, r.h, 10 * u, bg);
  fill_round(r.x + 3 * u, r.y + 3 * u, r.w - 6 * u, r.h - 6 * u, 8 * u,
             active ? COL_GOLD : COL_BOX_BORDER2);
  fill_round(r.x + 7 * u, r.y + 7 * u, r.w - 14 * u, r.h - 14 * u, 6 * u, bg);
  float s = 3 * u;
  if (label)
    draw_text(label, r.x + r.w / 2 - text_width(label, s) / 2, r.y + r.h / 2 - 4 * s, s);
}

static void draw_tab_bar(float tab_h) {
  float y = H - tab_h + 4 * u;
  float bh = tab_h - 16 * u;
  float sq = bh;   // square settings button on the right
  tab_settings_r = (RectFS){W - 8 * u - sq, y, sq, bh};
  // three equal buttons: GEAR | MAP | ITEMS, left of the settings cog
  float x0 = 8 * u, xr = tab_settings_r.x - 8 * u, tgap = 8 * u;
  float bw = (xr - x0 - 2 * tgap) / 3.0f;
  tab_gear_r  = (RectFS){x0, y, bw, bh};
  tab_map_r   = (RectFS){x0 + bw + tgap, y, bw, bh};
  tab_items_r = (RectFS){x0 + 2 * (bw + tgap), y, bw, bh};
  draw_tab_button(tab_gear_r, "GEAR", tab == TAB_GEAR);
  draw_tab_button(tab_map_r, "MAP", tab == TAB_MAP);
  draw_tab_button(tab_items_r, "ITEMS", tab == TAB_ITEMS);
  draw_tab_button(tab_settings_r, NULL, tab == TAB_SETTINGS);
  draw_cog(tab_settings_r.x + tab_settings_r.w / 2, tab_settings_r.y + tab_settings_r.h / 2,
           bh * 0.28f);
}

// public API

static SDL_Window *main_win;
static bool ss_enabled;

bool SecondScreenSDL_Init(SDL_Window *main_window) {
#ifdef __3DS__
  main_win = main_window;
  ss_enabled = true;
  return true;
#else
  const char *env = SDL_getenv("ZELDA3_SECOND_SCREEN");
  if (!env || env[0] != '1') return false;
  main_win = main_window;
  ss_enabled = true;
  return true;
#endif
}

// Create the bottom window lazily on the other display, after the game has
// drawn its first frames -- opening a second fullscreen window on the same
// output mid-init can resize the game window under its GL renderer.
static bool ensure_window(void) {
  if (ss_win) return true;

  printf("second screen: creating window...\n");
  fflush(stdout);
  int n = SDL_GetNumVideoDisplays();
  int main_disp = main_win ? SDL_GetWindowDisplayIndex(main_win) : 0;
  if (main_disp < 0) main_disp = 0;
  int target = -1;
  for (int i = 0; i < n; i++)
    if (i != main_disp) { target = i; break; }
  if (target < 0) target = main_disp;
  const char *disp_env = SDL_getenv("ZELDA3_SECOND_SCREEN_DISPLAY");
  if (disp_env && disp_env[0]) {
    target = SDL_atoi(disp_env);
    if (target < 0 || target >= n) target = main_disp;
  }

  const char *title = SDL_getenv("ZELDA3_SECOND_SCREEN_TITLE");
  if (!title || !title[0]) title = "Zelda3 Bottom Screen";
  ss_win = SDL_CreateWindow(title,
                            SDL_WINDOWPOS_CENTERED_DISPLAY(target),
                            SDL_WINDOWPOS_CENTERED_DISPLAY(target),
#ifdef __3DS__
                            320, 240,
#else
                            640, 480,
#endif
                            SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS);
  if (!ss_win) {
    fprintf(stderr, "second screen: CreateWindow failed: %s\n", SDL_GetError());
#ifdef __3DS__
    Platform3DS_LogRuntime("ERROR bottom window: %s", SDL_GetError());
#endif
    ss_enabled = false;
    return false;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  ss_r = SDL_CreateRenderer(ss_win, -1, SDL_RENDERER_SOFTWARE);
  if (!ss_r) {
    fprintf(stderr, "second screen: CreateRenderer failed: %s\n", SDL_GetError());
#ifdef __3DS__
    Platform3DS_LogRuntime("ERROR bottom renderer: %s", SDL_GetError());
#endif
    SDL_DestroyWindow(ss_win); ss_win = NULL;
    ss_enabled = false;
    return false;
  }
  ss_winid = SDL_GetWindowID(ss_win);
  SDL_GetRendererOutputSize(ss_r, &W, &H);
  if (W <= 0 || H <= 0) { W = 640; H = 480; }
  u = unit_for_size(W, H);
  printf("second screen: display %d of %d, %dx%d (u=%.2f)\n", target, n, W, H, u);
#ifdef __3DS__
  Platform3DS_LogRuntime("Bottom screen initialized: display %d of %d, %dx%d",
                         target, n, W, H);
#endif
  return true;
}

static void handle_tap(float x, float y) {
  int module = SS_GetModule() & 0xFF;
  if (mode_for_module(module) != MODE_GAME || !art_ready) return;

  if (in_rect(&tab_items_r, x, y)) { tab = (tab == TAB_ITEMS) ? TAB_MAP : TAB_ITEMS; leave_remap(); return; }
  if (in_rect(&tab_map_r, x, y))   { tab = TAB_MAP; leave_remap(); return; }
  if (in_rect(&tab_gear_r, x, y))  { tab = (tab == TAB_GEAR) ? TAB_MAP : TAB_GEAR; leave_remap(); return; }
  if (in_rect(&tab_settings_r, x, y)) { tab = (tab == TAB_SETTINGS) ? TAB_MAP : TAB_SETTINGS; leave_remap(); return; }

  if (tab == TAB_SETTINGS) {
    if (remap_mode) {
      if (in_rect(&remap_back_r, x, y)) { leave_remap(); return; }
      for (int i = 0; i < 12; i++) {
        if (in_rect(&remap_row_r[i], x, y)) {
          if (remap_arm == i) {
            SS_ArmButtonCapture(false);
            remap_arm = -1;
          } else {
            remap_arm = i;
            remap_arm_at = SDL_GetTicks();
            SS_ArmButtonCapture(true);
          }
          return;
        }
      }
    } else {
      if (in_rect(&settings_row_r[0], x, y)) {
        enum Platform3DSDisplayMode mode = kPlatform3DSDisplayUltraWideMod;
#ifdef __3DS__
        mode = Platform3DS_GetDisplayMode();
#else
        mode = SS_IsWidescreen() ? kPlatform3DSDisplayUltraWideMod : kPlatform3DSDisplayOriginal;
#endif
        mode = mode == kPlatform3DSDisplayUltraWideMod ? kPlatform3DSDisplayStretch :
               mode == kPlatform3DSDisplayStretch ? kPlatform3DSDisplayOriginal :
               kPlatform3DSDisplayUltraWideMod;
        SS_Set3DSDisplayMode((int)mode);
#ifdef __3DS__
        Platform3DS_SetDisplayMode(mode);
#endif
        update_ini("[General]", "DisplayMode",
                   mode == kPlatform3DSDisplayOriginal ? "Original" :
                   mode == kPlatform3DSDisplayStretch ? "Stretch" : "UltraWideMod");
      } else if (in_rect(&settings_row_r[1], x, y)) {
        enum Platform3DSCStickMode mode = kPlatform3DSCStickTurbo;
#ifdef __3DS__
        mode = Platform3DS_GetCStickMode();
        mode = mode == kPlatform3DSCStickTurbo ? kPlatform3DSCStickWalk :
               mode == kPlatform3DSCStickWalk ? kPlatform3DSCStickDisabled :
               kPlatform3DSCStickTurbo;
        Platform3DS_SetCStickMode(mode);
#endif
        update_ini("[General]", "CStickMode",
                   mode == kPlatform3DSCStickWalk ? "Walk" :
                   mode == kPlatform3DSCStickDisabled ? "Disabled" : "Turbo");
      } else if (in_rect(&settings_row_r[2], x, y)) {
        int multiplier = 5;
#ifdef __3DS__
        multiplier = Platform3DS_GetTurboMultiplier();
        multiplier = multiplier >= 5 ? 2 : multiplier + 1;
        Platform3DS_SetTurboMultiplier(multiplier);
#endif
        char value[16];
        snprintf(value, sizeof(value), "%d", multiplier);
        update_ini("[General]", "CStickTurboMultiplier", value);
      } else if (in_rect(&settings_row_r[3], x, y)) {
        bool hide = !SS_IsHudHidden();
        SS_SetHudHidden(hide);
        if (hide) { FILE *f = fopen(".ss_hidehud", "wb"); if (f) fclose(f); }
        else remove(".ss_hidehud");
      } else if (in_rect(&settings_row_r[4], x, y)) {
        SS_GetGamepadControls(pad_controls);
        remap_mode = true;
      } else if (in_rect(&settings_row_r[5], x, y)) {
        SS_RequestMemoryDump();
        dump_flash_until = SDL_GetTicks() + 1200;
      }
    }
    return;
  }

  if (in_rect(&y_ring_r, x, y)) {
    int cur = SS_GetEquippedSlot();
    for (int k = 1; k <= 20; k++) {
      int slot = ((cur - 1 + k) % 20) + 1;
      if (slot_owned(slot - 1)) { SS_EquipSlot(slot); break; }
    }
    return;
  }
  if (tab == TAB_ITEMS && grid_cell > 0) {
    int col = (int)((x - grid_x) / grid_cell);
    int row = (int)((y - grid_y) / grid_cell);
    if (x >= grid_x && y >= grid_y && col >= 0 && col <= 4 && row >= 0 && row <= 3) {
      int i = row * 5 + col;
      if (i < 20 && slot_owned(i)) {
        SS_EquipSlot(i + 1);
        tap_flash_slot = i;
        tap_flash_until = SDL_GetTicks() + 250;
      }
    }
    return;
  }
  if (tab == TAB_MAP) {
    for (int i = 0; i < plaque_count; i++) {
      if (in_rect(&plaque_r[i], x, y)) {
        int floor = (int8_t)(SS_GetDungeon() >> 8);
        view_floor_offset = plaque_floor[i] - floor;
        view_floor_touched_at = SDL_GetTicks();
        return;
      }
    }
    if (in_rect(&map_area_r, x, y)) whole_map = !whole_map;
  }
}

bool SecondScreenSDL_HandleEvent(const SDL_Event *e) {
  if (!ss_win) return false;
  switch (e->type) {
  case SDL_FINGERDOWN:
    if (e->tfinger.windowID == ss_winid) { handle_tap(e->tfinger.x * W, e->tfinger.y * H); return true; }
    return false;
  case SDL_FINGERUP: case SDL_FINGERMOTION:
    return e->tfinger.windowID == ss_winid;
  case SDL_MOUSEBUTTONDOWN:
    if (e->button.windowID == ss_winid) {
      if (e->button.which != SDL_TOUCH_MOUSEID)  // real mouse (dev); touch already handled
        handle_tap((float)e->button.x, (float)e->button.y);
      return true;
    }
    return false;
  case SDL_MOUSEBUTTONUP: case SDL_MOUSEMOTION:
    return e->button.windowID == ss_winid;
  case SDL_WINDOWEVENT:
    if (e->window.windowID != ss_winid) return false;
    if (e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
      ss_needs_rebuild = true;
    return true;
  default:
    return false;
  }
}

void SecondScreenSDL_Handle3DSTouch(void) {
#ifdef __3DS__
  if (!ss_win)
    return;
  static bool was_touching;
  u32 keys = hidKeysHeld();
  bool touching = (keys & KEY_TOUCH) != 0;
  if (touching && !was_touching) {
    touchPosition pos;
    hidTouchRead(&pos);
    float x = W > 0 ? (float)pos.px * W / 320.0f : (float)pos.px;
    float y = H > 0 ? (float)pos.py * H / 240.0f : (float)pos.py;
    handle_tap(x, y);
  }
  was_touching = touching;
#endif
}

void SecondScreenSDL_Update(void) {
  if (!ss_enabled) return;
  static uint32_t frame_no;
  frame_no++;
  if (!ss_win) {
    if (frame_no < 3) return;      // let the game settle its first GL frames
    if (!ensure_window()) return;  // disables itself on failure
  }
#ifdef __3DS__
  if (frame_no % 6) return;   // 10fps; avoid a second-display present tax.
#else
  if (frame_no & 1) return;   // UI renders at 30fps
#endif

  // rebuild the renderer if the compositor resized us
  if (ss_needs_rebuild) {
    ss_needs_rebuild = false;
    int w2, h2;
    SDL_GetWindowSize(ss_win, &w2, &h2);
    if ((w2 != W || h2 != H) && w2 > 0 && h2 > 0) {
      rebuild_renderer(w2, h2);
      if (!ss_win || !ss_r) return;
    }
  }

  if (!art_ready) {
    art_ready = try_load_art();
    if (art_ready && !hud_pref_applied) {
      hud_pref_applied = true;
      FILE *f = fopen(".ss_hidehud", "rb");
      if (f) { fclose(f); SS_SetHudHidden(true); }
    }
    if (!art_ready) {
      // engine still booting: quiet dark frame
      set_color(COL_BOX);
      SDL_RenderClear(ss_r);
      SDL_RenderPresent(ss_r);
      return;
    }
  }

  int link_x = SS_GetLinkX(), link_y = SS_GetLinkY();
  int area = SS_GetArea();
  bool indoors = SS_IsIndoors();
  int dungeon_info = SS_GetDungeon();
  int module = SS_GetModule() & 0xFF;
  SS_ReadSram(sram, sizeof(sram));
  SS_ReadDungFlags(dung_flags, sizeof(dung_flags));
  cur_room = area; cur_palace = dungeon_info & 0xFF; cur_floor_now = (int8_t)(dungeon_info >> 8);

  bool dungeon_mode = indoors;
  int ui_mode = mode_for_module(module);
  if (module == 0x12 || module <= 0x05) has_last_outdoor = false;
  // houses/caves have no dungeon map: keep the overworld view frozen at the door.
  // The special overworld screens (>= 0x80: Master Sword glade, Zora's Domain,
  // under the bridge) run in their own small coordinate space near the map origin,
  // which would park the marker in the Lost Woods (#23); freeze it there too.
  // When the live "last outdoor" spot is unknown (fresh save-load, or the view was
  // rebuilt on refocus) recover the doorway from the engine (last tracked outdoor
  // position, else its exit table) so the map still shows instead of getting stuck
  // on the cinema card (#9).
  bool in_house = ui_mode == MODE_GAME && indoors && (dungeon_info & 0xFF) == 0xFF;
  bool special = ui_mode == MODE_GAME && !indoors && area >= 0x80;
  int exit_pos[3];
  bool have_exit = (in_house || special) && !has_last_outdoor && SS_GetIndoorExit(exit_pos);
  if ((in_house || special) && !has_last_outdoor && !have_exit) ui_mode = MODE_CINEMA;
  if (ui_mode != MODE_GAME) {
    draw_cinema();
    SDL_RenderPresent(ss_r);
    return;
  }
  if (!indoors && area < 0x80 && (module == 0x09 || module == 0x0B)) {
    last_out_x = link_x; last_out_y = link_y; last_out_area = area;
    has_last_outdoor = true;
  } else if (in_house || special) {
    dungeon_mode = false;
    if (has_last_outdoor) {
      link_x = last_out_x; link_y = last_out_y; area = last_out_area;
    } else {
      link_x = exit_pos[0]; link_y = exit_pos[1]; area = exit_pos[2];
    }
  }

  draw_tiled(dungeon_mode ? tex_bg_stone : tex_bg_menu,
             dungeon_mode ? kSSTexStone_W : kSSTexMenu_W,
             dungeon_mode ? kSSTexStone_H : kSSTexMenu_H,
             (RectFS){0, 0, W, H},
             dungeon_mode ? COL_BG_STONE : COL_BG_MENU);
  float tab_h = 84 * u;
  float side_w = 200 * u;
  map_area_r = (RectFS){10 * u, 10 * u, W - side_w - 14 * u, H - tab_h - 14 * u};

  if (tab == TAB_ITEMS)      draw_items(map_area_r);
  else if (tab == TAB_GEAR)  draw_gear(map_area_r);
  else if (tab == TAB_SETTINGS) draw_settings(map_area_r);
  else if (dungeon_mode)     draw_dungeon(map_area_r, link_x, link_y, area & 0xFF, dungeon_info);
  else                       draw_overworld(map_area_r, link_x, link_y, area);

  draw_sidebar(W - side_w + 4 * u, 10 * u, side_w - 14 * u, H - tab_h - 14 * u, dungeon_mode);
  draw_tab_bar(tab_h);

  SDL_RenderPresent(ss_r);
}

static void destroy_textures(void) {
  SDL_Texture **texes[] = {&tex_map[0], &tex_map[1], &tex_icons, &tex_glyphs,
                           &tex_letters, &tex_face, &tex_floor, &tex_mapicons,
                           &tex_bg_menu, &tex_bg_parch, &tex_bg_stone};
  for (size_t i = 0; i < sizeof(texes) / sizeof(texes[0]); i++) {
    if (*texes[i]) SDL_DestroyTexture(*texes[i]);
    *texes[i] = NULL;
  }
  art_ready = false;
}

// The window surface is invalidated when the compositor resizes us; rebuild
// the renderer and let the art regenerate at the new size.
static void rebuild_renderer(int w2, int h2) {
  destroy_textures();
  if (ss_r) SDL_DestroyRenderer(ss_r);
  ss_r = SDL_CreateRenderer(ss_win, -1, SDL_RENDERER_SOFTWARE);
  if (!ss_r) {
    fprintf(stderr, "second screen: renderer rebuild failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(ss_win); ss_win = NULL;
    ss_enabled = false;
    return;
  }
  W = w2; H = h2;
  u = unit_for_size(W, H);
  printf("second screen resized: %dx%d (u=%.2f)\n", W, H, u);
  fflush(stdout);
}

void SecondScreenSDL_Shutdown(void) {
  ss_enabled = false;
  if (!ss_win) return;
  destroy_textures();
  SDL_DestroyRenderer(ss_r); ss_r = NULL;
  SDL_DestroyWindow(ss_win); ss_win = NULL;
}
