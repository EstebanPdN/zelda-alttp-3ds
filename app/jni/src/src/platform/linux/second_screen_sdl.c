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

enum Platform3DSWideEdgeMode {
  kPlatform3DSWideEdgeStandard,
  kPlatform3DSWideEdgeFixedCamera,
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

#ifndef ZELDA3_3DS_VERSION
#define ZELDA3_3DS_VERSION "dev"
#endif

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
void SS_Set3DSWideEdgeMode(int mode);
void SS_SetHudHidden(bool hide);
bool SS_IsHudHidden(void);
void SS_RequestMemoryDump(const char *dump_dir);
void SS_RequestRestart(void);
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
static RectFS settings_row_r[6], remap_row_r[6], remap_back_r;
static RectFS remap_page_r;
static RectFS screen_row_r[4], screen_back_r;
static RectFS cinema_back_r;
static RectFS developer_row_r[2], developer_back_r;

// settings / remap state
static bool remap_mode;
static bool screen_mode;
static bool developer_mode;
static bool developer_overlay_mode;
static int  remap_first_row;
static int  remap_arm = -1;         // row currently waiting for a button press
static uint32_t remap_arm_at;
static int  pad_controls[12];
static bool hud_pref_applied;
static uint32_t dump_flash_until;
static RectFS plaque_r[16];
static int    plaque_floor[16], plaque_count;
static float  grid_x, grid_y, grid_cell;
static int    ss_diag_current_fps;
static int    ss_diag_average_fps;

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

static void draw_icon_inner(int cell, float x, float y, float size) {
  if (cell < 0 || !tex_icons) return;
  SDL_Rect src = {(cell % SS_ICON_COLS) * 16 + 1, (cell / SS_ICON_COLS) * 16 + 1, 14, 14};
  SDL_FRect dst = {x, y, size, size};
  SDL_RenderCopyF(ss_r, tex_icons, &src, &dst);
}

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

static const uint8_t *tiny_letter(char ch) {
  static const uint8_t letters[26][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,14}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
  };
  static const uint8_t digits[10][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,30,1,1,17,14},
    {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,2,12},
  };
  static const uint8_t dot[7] = {0,0,0,0,0,12,12};
  static const uint8_t dash[7] = {0,0,0,31,0,0,0};
  if (ch >= 'A' && ch <= 'Z')
    return letters[ch - 'A'];
  if (ch >= '0' && ch <= '9')
    return digits[ch - '0'];
  if (ch == '.')
    return dot;
  if (ch == '-')
    return dash;
  return NULL;
}

static float tiny_text_width(const char *s, float sc) {
  float w = 0;
  for (; *s; s++)
    w += (*s == ' ' ? 4 : 6) * sc;
  return w;
}

static void draw_tiny_text(const char *s, float x, float y, float sc, uint32_t c) {
  float cx = floorf(x + 0.5f);
  float cy = floorf(y + 0.5f);
  for (; *s; s++) {
    const uint8_t *rows = tiny_letter(*s);
    if (rows) {
      for (int yy = 0; yy < 7; yy++)
        for (int xx = 0; xx < 5; xx++)
          if (rows[yy] & (1 << (4 - xx)))
            fill_rect(cx + xx * sc, cy + yy * sc, sc, sc, c);
    }
    cx += (*s == ' ' ? 4 : 6) * sc;
  }
}

static void draw_block_text(const char *s, float x, float y, float sc,
                            uint32_t color) {
  draw_tiny_text(s, floorf(x + 0.5f), floorf(y + 0.5f), floorf(sc + 0.5f),
                 color);
}

static void draw_block_label_value(const char *label, const char *value,
                                   float x, float y, float sc,
                                   uint32_t label_color,
                                   uint32_t value_color) {
  draw_block_text(label, x, y, sc, label_color);
  draw_block_text(value, x + 190 * u, y, sc, value_color);
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
    float icon_size = 16 * is;
    draw_icon_inner(kSS_ItemCell[i][lv], x + (cellW - icon_size) / 2,
                    y + (cellW - icon_size) / 2, icon_size);
  }
  (void)kItemNames;
}

static void draw_gear(RectFS r) {
  menu_box(r, COL_BOX_BORDER2);
  draw_tiny_text("GEAR", r.x + r.w / 2 - tiny_text_width("GEAR", 2.0f) / 2,
                 r.y + 12.0f, 2.0f, COL(250, 250, 250));

  int sword = sram8(0x59), shield = sram8(0x5A);
  int gear_cells[7];
  gear_cells[0] = (sword > 0 && sword != 0xFF) ? SS_ICON_SWORD_1 + (sword > 4 ? 3 : sword - 1) : -1;
  gear_cells[1] = (shield > 0 && shield != 0xFF) ? SS_ICON_SHIELD_1 + (shield > 3 ? 2 : shield - 1) : -1;
  gear_cells[2] = SS_ICON_ARMOR_0 + (sram8(0x5B) > 2 ? 2 : sram8(0x5B));
  gear_cells[3] = sram8(0x54) > 0 ? SS_ICON_GLOVES_1 + (sram8(0x54) > 2 ? 1 : sram8(0x54) - 1) : -1;
  gear_cells[4] = sram8(0x55) > 0 ? SS_ICON_BOOTS_1 : -1;
  gear_cells[5] = sram8(0x56) > 0 ? SS_ICON_FLIPPERS_1 : -1;
  gear_cells[6] = sram8(0x57) > 0 ? SS_ICON_MOONPEARL_1 : -1;

  float cell = 30.0f;
  float gap = 7.0f;
  float row_w = 7 * cell + 6 * gap;
  if (row_w > r.w - 22) {
    gap = 5.0f;
    cell = (r.w - 22 - 6 * gap) / 7.0f;
  }
  float x0 = r.x + (r.w - (7 * cell + 6 * gap)) / 2;
  float y0 = r.y + 30.0f;
  float icon_s = clampf((cell - 5.0f) / 16.0f, 1.25f, 1.75f);

  for (int i = 0; i < 7; i++) {
    float x = x0 + i * (cell + gap);
    float y = y0;
    slot_bg(x, y, cell);
    if (gear_cells[i] >= 0)
      draw_icon(gear_cells[i], x + (cell - 16 * icon_s) / 2,
                y + (cell - 16 * icon_s) / 2, icon_s);
  }

  float y1 = y0 + cell + 24.0f;
  float bottle_cell = 25.0f;
  float bottle_gap = 7.0f;
  draw_tiny_text("BOTTLES", x0, y1 - 12.0f, 1.0f, COL(250, 250, 250));
  for (int i = 0; i < 4; i++) {
    float x = x0 + i * (bottle_cell + bottle_gap);
    slot_bg(x, y1, bottle_cell);
    int lv = sram8(0x5C + i);
    if (lv > 7) lv = 7;
    if (lv > 0) {
      float bs = clampf((bottle_cell - 5.0f) / 16.0f, 1.15f, 1.35f);
      draw_icon(SS_ICON_BOTTLE_1 + (lv - 1),
                x + (bottle_cell - 16 * bs) / 2,
                y1 + (bottle_cell - 16 * bs) / 2, bs);
    }
  }
  float pend_x = r.x + r.w - 108.0f;
  draw_tiny_text("PENDANTS", pend_x, y1 - 12.0f, 1.0f, COL(250, 250, 250));
  int pend = sram8(0x74);
  static const int pbit[3] = {4, 2, 1};
  static const uint32_t pcol[3] = {COL(64, 200, 88), COL(70, 110, 240), COL(230, 60, 60)};
  for (int i = 0; i < 3; i++) {
    float cxp = pend_x + i * 28.0f + 13.0f;
    float cyp = y1 + 13.0f;
    fill_circle(cxp, cyp, 8.0f, (pend & pbit[i]) ? pcol[i] : COL(34, 34, 34));
    stroke_circle(cxp, cyp, 8.0f, 2.0f, COL_GOLD_DARK);
  }

  float cyC = y1 + 49.0f;
  draw_tiny_text("CRYSTALS", x0, cyC - 12.0f, 1.0f, COL(250, 250, 250));
  float crystal_start = x0 + 78.0f;
  float crystal_step = 23.0f;
  int owned7 = sram8(0x7A) & 0x7F, n_owned = 0;
  while (owned7) { n_owned += owned7 & 1; owned7 >>= 1; }
  for (int i = 0; i < 7; i++) {
    float cxp = crystal_start + i * crystal_step;
    float cyp = cyC;
    fill_circle(cxp, cyp, 7.0f, i < n_owned ? COL(110, 160, 255) : COL(34, 34, 34));
    stroke_circle(cxp, cyp, 7.0f, 2.0f, COL_GOLD_DARK);
  }

  float yp = cyC + 25.0f;
  draw_glyph(SS_GLYPH_HEART_FULL, x0, yp - 3.0f, 1.7f);
  int pieces = sram8(0x6B) & 3;
  for (int i = 0; i < 4; i++) {
    float gx = x0 + 34.0f + i * 25.0f, gy = yp;
    fill_round(gx - 2.0f, gy - 2.0f, 20.0f, 20.0f, 5.0f, COL_GOLD_DARK);
    fill_round(gx, gy, 16.0f, 16.0f, 4.0f, i < pieces ? COL(235, 80, 80) : COL(40, 34, 30));
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

  // Magic and hearts use integer pixel sizes on 3DS; fractional scaling made
  // the HUD look uneven in real bottom-screen dumps.
  float bar_h = 10.0f;
  bool half_magic = sram8(0x7B) >= 1;
  float my = floorf(y + h - bar_h - 5.0f + 0.5f);
  int cap = sram8(0x6C) >> 3; if (cap > 20) cap = 20;
  int cur = sram8(0x6D);
  float heart_s = cap > 10 ? 1.25f : cap > 5 ? 1.5f : 2.0f;
  float heart_px = 8.0f * heart_s;
  float hs = heart_px + (cap > 10 ? 2.0f : 3.0f);
  int desired_cols = cap <= 5 ? cap : cap <= 10 ? 5 : 10;
  int max_cols = (int)((w + 2.0f) / hs);
  int heart_cols = desired_cols;
  if (heart_cols <= 0) heart_cols = 1;
  if (max_cols > 0 && heart_cols > max_cols) heart_cols = max_cols;
  int heart_rows = (cap + heart_cols - 1) / heart_cols;
  float half_gap = half_magic ? 8.0f : 0.0f;
  float hy = floorf(my - heart_rows * hs - 6.0f - half_gap + 0.5f);

  // equipped item ring: tap cycles to the next owned item
  float ring_top = y + chip_h + 4.0f;
  float ring_r = clampf((hy - ring_top) / 2.0f - 2.0f, 18.0f, 32.0f);
  float rcx = x + w / 2, rcy = ring_top + ring_r;
  if (rcy + ring_r > hy - 2.0f)
    rcy = hy - ring_r - 2.0f;
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
    if (lv > 0) {
      float item_size = clampf(ring_r * 1.15f, 26.0f, 38.0f);
      draw_icon_inner(kSS_ItemCell[i][lv], rcx - item_size / 2,
                      rcy - item_size / 2, item_size);
    }
  }
  draw_text("Y", rcx + ring_r - 11.0f, rcy - ring_r + 4.0f, 1.0f);

  // hearts (live health)
  float hx0 = floorf(x + (w - (heart_cols - 1) * hs - heart_px) / 2 + 0.5f);
  for (int i = 0; i < cap; i++) {
    int g = i < (cur >> 3) ? SS_GLYPH_HEART_FULL
          : (i == (cur >> 3) && (cur & 7) >= 4 ? SS_GLYPH_HEART_HALF : SS_GLYPH_HEART_EMPTY);
    draw_glyph(g, hx0 + (i % heart_cols) * hs, hy + (i / heart_cols) * hs, heart_s);
  }

  // magic bar (with the HUD's 1/2 marker when the upgrade is owned)
  if (half_magic) {
    float gx = floorf(x + (w - 24.0f) / 2 + 0.5f);
    draw_glyph(SS_GLYPH_HALF0, gx, my - 8.0f, 1.0f);
    draw_glyph(SS_GLYPH_HALF1, gx + 8.0f, my - 8.0f, 1.0f);
    draw_glyph(SS_GLYPH_HALF2, gx + 16.0f, my - 8.0f, 1.0f);
  }
  int magic = sram8(0x6E); if (magic > 128) magic = 128;
  float bar_x = floorf(x + 8.0f + 0.5f);
  float bar_w = floorf(w - 16.0f + 0.5f);
  fill_rect(bar_x, my, bar_w, bar_h, COL_GOLD_DARK);
  fill_rect(bar_x + 2.0f, my + 2.0f, bar_w - 4.0f, bar_h - 4.0f, COL_BOX);
  float frac = magic / 128.0f;
  if (frac > 0)
    fill_rect(bar_x + 3.0f, my + 3.0f, floorf((bar_w - 6.0f) * frac + 0.5f),
              bar_h - 6.0f, COL(72, 208, 72));
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
  remap_first_row = 0;
}

static void leave_settings_submenu(void) {
  leave_remap();
  screen_mode = false;
  developer_mode = false;
  developer_overlay_mode = false;
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
  remap_back_r = (RectFS){r.x + 14 * u, r.y + 12 * u, 76 * u, 32 * u};
  draw_settings_row(&remap_back_r, false);
  draw_text("BACK", remap_back_r.x + remap_back_r.w / 2 - text_width("BACK", 1.8f * u) / 2,
            remap_back_r.y + remap_back_r.h / 2 - 7 * u, 1.8f * u);

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

  if (remap_first_row < 0)
    remap_first_row = 0;
  if (remap_first_row > 6)
    remap_first_row = 6;

  remap_page_r = (RectFS){r.x + r.w - 86 * u, r.y + 12 * u, 72 * u, 32 * u};
  draw_settings_row(&remap_page_r, false);
  draw_text(remap_first_row == 0 ? "MORE" : "TOP",
            remap_page_r.x + remap_page_r.w / 2 -
              text_width(remap_first_row == 0 ? "MORE" : "TOP", 1.8f * u) / 2,
            remap_page_r.y + remap_page_r.h / 2 - 7 * u, 1.8f * u);

  float row_h = 44 * u, gap = 6 * u;
  float y0 = r.y + 56 * u;
  float row_w = r.w - 42 * u;
  for (int visible = 0; visible < 6; visible++) {
    int i = remap_first_row + visible;
    RectFS *row = &remap_row_r[visible];
    *row = (RectFS){r.x + 14 * u, y0 + visible * (row_h + gap), row_w, row_h};
    bool armed = remap_arm == i;
    draw_settings_row(row, armed);
    float ty = row->y + row->h / 2 - 9 * u;
    draw_text(kPadCmdNames[i], row->x + 12 * u, ty, 2.1f * u);
    const char *v = armed ? "PRESS KEY"
        : (pad_controls[i] >= 0 && pad_controls[i] < 17 ? kPadButtonLabel[pad_controls[i]] : "----");
    draw_text(v, row->x + row->w - 12 * u - text_width(v, 2.1f * u), ty, 2.1f * u);
  }
  float bar_x = r.x + r.w - 18 * u;
  float bar_y = y0;
  float bar_h = 6 * row_h + 5 * gap;
  fill_round(bar_x, bar_y, 4 * u, bar_h, 2 * u, COL(42, 42, 42));
  float thumb_h = bar_h * 0.5f;
  float thumb_y = bar_y + (remap_first_row == 0 ? 0 : bar_h - thumb_h);
  fill_round(bar_x, thumb_y, 4 * u, thumb_h, 2 * u, COL_GOLD);
}

static const char *display_mode_label(void) {
#ifdef __3DS__
  switch (Platform3DS_GetDisplayMode()) {
  case kPlatform3DSDisplayOriginal: return "ORIGINAL";
  case kPlatform3DSDisplayStretch: return "STRETCH";
  case kPlatform3DSDisplayUltraWideMod:
  default: return "WIDE";
  }
#else
  return SS_IsWidescreen() ? "WIDE" : "ORIGINAL";
#endif
}

static const char *wide_zoom_label(void) {
#ifdef __3DS__
  switch (Platform3DS_GetWideZoomIndex()) {
  case 1: return "1.2X";
  case 2: return "1.5X";
  case 3: return "2X";
  case 4: return "2.5X";
  case 0:
  default: return "1X";
  }
#else
  return "1X";
#endif
}

static void draw_screen_panel(RectFS r) {
  draw_text("SCREEN", r.x + r.w / 2 - text_width("SCREEN", 3 * u) / 2,
            r.y + 18 * u, 3 * u);
  screen_back_r = (RectFS){r.x + 20 * u, r.y + 12 * u, 90 * u, 38 * u};
  draw_settings_row(&screen_back_r, false);
  draw_text("BACK", screen_back_r.x + screen_back_r.w / 2 - text_width("BACK", 2.2f * u) / 2,
            screen_back_r.y + screen_back_r.h / 2 - 9 * u, 2.2f * u);

  const char *edge_value = "FIXED CAMERA";
#ifdef __3DS__
  if (Platform3DS_GetWideEdgeMode() == kPlatform3DSWideEdgeStandard)
    edge_value = "STANDARD";
#endif
  bool hud_hidden = SS_IsHudHidden();
  bool wide = false;
#ifdef __3DS__
  wide = Platform3DS_GetDisplayMode() == kPlatform3DSDisplayUltraWideMod;
#else
  wide = SS_IsWidescreen();
#endif
  static const char *const labels[4] = {
    "DISPLAY MODE", "EDGE MODE", "ZOOM", "TOP HUD",
  };
  const char *values[4] = {
    display_mode_label(), edge_value, wide_zoom_label(),
    hud_hidden ? "OFF" : "ON",
  };
  int rows = wide ? 4 : 3;
  screen_row_r[2] = (RectFS){0};
  float row_h = 58 * u, gap = 14 * u;
  float y0 = r.y + 82 * u;
  for (int visible = 0; visible < rows; visible++) {
    int item = !wide && visible == 2 ? 3 : visible;
    RectFS *row = &screen_row_r[item];
    *row = (RectFS){
      r.x + 28 * u, y0 + visible * (row_h + gap),
      r.w - 56 * u, row_h,
    };
    draw_settings_row(row, false);
    float ty = row->y + row->h / 2 - 8 * u;
    draw_text(labels[item], row->x + 16 * u, ty, 2 * u);
    draw_text(values[item],
              row->x + row->w - 16 * u - text_width(values[item], 2 * u),
              ty, 2 * u);
  }
}

static void draw_developer_panel(RectFS r) {
  draw_text("DEVELOPER", r.x + r.w / 2 - text_width("DEVELOPER", 3 * u) / 2,
            r.y + 18 * u, 3 * u);
  developer_back_r = (RectFS){r.x + 20 * u, r.y + 12 * u, 90 * u, 38 * u};
  draw_settings_row(&developer_back_r, false);
  draw_text("BACK", developer_back_r.x + developer_back_r.w / 2 - text_width("BACK", 2.2f * u) / 2,
            developer_back_r.y + developer_back_r.h / 2 - 9 * u, 2.2f * u);

  const char *labels[2] = {"MEM DUMP", "OVERLAY"};
  const char *values[2] = {
    SDL_GetTicks() < dump_flash_until ? "DONE" : "WRITE",
    "OPEN",
  };
  float row_h = 58 * u, gap = 14 * u;
  float y0 = r.y + 82 * u;
  for (int i = 0; i < 2; i++) {
    RectFS *row = &developer_row_r[i];
    *row = (RectFS){r.x + 28 * u, y0 + i * (row_h + gap), r.w - 56 * u, row_h};
    draw_settings_row(row, false);
    float ty = row->y + row->h / 2 - 8 * u;
    draw_text(labels[i], row->x + 16 * u, ty, 2 * u);
    draw_text(values[i], row->x + row->w - 16 * u - text_width(values[i], 2 * u),
              ty, 2 * u);
  }
}

static void draw_developer_overlay_panel(RectFS r) {
  draw_text("OVERLAY", r.x + r.w / 2 - text_width("OVERLAY", 3 * u) / 2,
            r.y + 18 * u, 3 * u);
  developer_back_r = (RectFS){r.x + 20 * u, r.y + 12 * u, 90 * u, 38 * u};
  draw_settings_row(&developer_back_r, false);
  draw_text("BACK", developer_back_r.x + developer_back_r.w / 2 - text_width("BACK", 2.2f * u) / 2,
            developer_back_r.y + developer_back_r.h / 2 - 9 * u, 2.2f * u);

  char version[32], model[32], fps_now[32], fps_avg[32], core[32],
       display[32], location[48], module[32];
#ifdef __3DS__
  snprintf(model, sizeof(model), "%s",
           Platform3DS_IsNew3DS() ? "NEW 3DS" : "OLD 3DS");
  snprintf(core, sizeof(core), "%s",
           Platform3DS_CanUseCore1PpuWorker() ? "ON" : "OFF");
#else
  snprintf(model, sizeof(model), "DESKTOP");
  snprintf(core, sizeof(core), "N A");
#endif
  snprintf(version, sizeof(version), "%s", ZELDA3_3DS_VERSION);
  for (char *p = version; *p; p++)
    if (*p == '.' || *p == '-')
      *p = ' ';
  snprintf(fps_now, sizeof(fps_now), "%d", ss_diag_current_fps);
  snprintf(fps_avg, sizeof(fps_avg), "%d", ss_diag_average_fps);
  snprintf(display, sizeof(display), "%s", display_mode_label());
  if (SS_IsIndoors()) {
    int dungeon_info = SS_GetDungeon();
    int palace = dungeon_info & 0xff;
    if (palace >= 0 && palace < 14)
      snprintf(location, sizeof(location), "%s", kDungeonNames[palace]);
    else
      snprintf(location, sizeof(location), "HOUSE %02X", SS_GetArea() & 0xff);
  } else {
    snprintf(location, sizeof(location), "OVERWORLD %02X", SS_GetArea() & 0xff);
  }
  snprintf(module, sizeof(module), "%02X", SS_GetModule() & 0xff);

  float sc = 3.0f * u;
  float row_h = 30 * u;
  float x = r.x + 26 * u;
  float y = r.y + 62 * u;
  struct {
    const char *label;
    const char *value;
    uint32_t color;
  } rows[] = {
    {"VERSION", version, COL(255, 255, 255)},
    {"MODEL", model, COL_GOLD},
    {"FPS NOW", fps_now, COL(120, 255, 140)},
    {"FPS AVG", fps_avg, COL(120, 220, 255)},
    {"CORE1", core, COL(230, 230, 230)},
    {"SCREEN", display, COL(230, 230, 230)},
    {"ROOM", location, COL(230, 230, 230)},
    {"MODULE", module, COL(230, 230, 230)},
  };
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
    fill_round(x - 8 * u, y - 4 * u, r.w - 40 * u, row_h, 5 * u,
               i == 2 || i == 3 ? COL(34, 28, 12) : COL(24, 24, 24));
    draw_block_label_value(rows[i].label, rows[i].value, x, y, sc,
                           COL(170, 170, 170), rows[i].color);
    y += row_h + 3 * u;
  }
}

static void draw_settings(RectFS r) {
  menu_box(r, COL_BOX_BORDER);
  if (remap_mode) {
    draw_remap_panel(r);
    return;
  }
  if (screen_mode) {
    draw_screen_panel(r);
    return;
  }
  if (developer_mode) {
    if (developer_overlay_mode)
      draw_developer_overlay_panel(r);
    else
      draw_developer_panel(r);
    return;
  }
  draw_text("SETTINGS", r.x + r.w / 2 - text_width("SETTINGS", 3 * u) / 2, r.y + 18 * u, 3 * u);

  char turbo_value[12];
#ifdef __3DS__
  int turbo_multiplier = Platform3DS_GetTurboMultiplier();
  if (turbo_multiplier <= 0)
    snprintf(turbo_value, sizeof(turbo_value), "OFF");
  else
    snprintf(turbo_value, sizeof(turbo_value), "X%d", turbo_multiplier);
#else
  snprintf(turbo_value, sizeof(turbo_value), "X5");
#endif
  static const char *const labels[6] = {
    "SCREEN", "TURBO SPEED", "REMAP BUTTONS",
    "DEVELOPER", "RESTART", "SELECT ROM",
  };
  const char *values[6] = {
    "", turbo_value, "", "", NULL, NULL,
  };
  float row_h = 44 * u, gap = 8 * u;
  float y0 = r.y + 55 * u;
  for (int i = 0; i < 6; i++) {
    RectFS *row = &settings_row_r[i];
    *row = (RectFS){r.x + 28 * u, y0 + i * (row_h + gap), r.w - 56 * u, row_h};
    draw_settings_row(row, false);
    float ty = row->y + row->h / 2 - 8 * u;
    draw_text(labels[i], row->x + 16 * u, ty, 2 * u);
    if (!values[i]) {
      continue;
    }
    if (values[i][0] == 0) {
      // chevron for sub-screens
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

static void draw_cinema_settings_overlay(void) {
  RectFS r = {10 * u, 10 * u, W - 20 * u, H - 20 * u};
  draw_settings(r);
  if (!screen_mode && !remap_mode && !developer_mode) {
    cinema_back_r = (RectFS){r.x + 20 * u, r.y + 12 * u, 90 * u, 38 * u};
    draw_settings_row(&cinema_back_r, false);
    draw_text("BACK",
              cinema_back_r.x + cinema_back_r.w / 2 -
                text_width("BACK", 2.2f * u) / 2,
              cinema_back_r.y + cinema_back_r.h / 2 - 9 * u, 2.2f * u);
  } else {
    cinema_back_r = (RectFS){0, 0, 0, 0};
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
#ifdef __3DS__
static uint8_t *ss_present_pixels[2];
static bool ss_is_new_3ds;
static int ss_front_buffer = -1;
static int ss_worker_buffer;
static bool ss_worker_busy;
static bool ss_frame_ready;
static uint32_t ss_redraw_requests;
static bool ss_worker_sidebar_patch;
static bool ss_worker_running;
static Thread ss_worker_thread;
static LightEvent ss_worker_start;
static LightEvent ss_worker_done;
static int ss_worker_logic_frames;
static s32 ss_worker_idle_priority;
static s32 ss_worker_interactive_priority;
static bool ss_worker_interactive;
static uint64_t ss_full_redraw_count;
static uint64_t ss_full_redraw_total_ticks;
static uint64_t ss_full_redraw_max_ticks;
static uint64_t ss_patch_redraw_count;
static uint64_t ss_patch_redraw_total_ticks;
static uint64_t ss_patch_redraw_max_ticks;
static bool ss_touch_redraw_pending;
static uint64_t ss_touch_request_ticks;
static uint64_t ss_worker_touch_request_ticks;
static uint64_t ss_touch_redraw_count;
static uint64_t ss_touch_redraw_total_ticks;
static uint64_t ss_touch_redraw_max_ticks;
enum {
  k3DSBottomTextureWidth = 512,
  k3DSBottomTextureHeight = 256,
};
static void draw_second_screen(int logic_frames);

static int bottom_bytes_per_pixel(void) {
  return ss_is_new_3ds ? 4 : 2;
}

static int bottom_buffer_pitch(void) {
  return k3DSBottomTextureWidth * bottom_bytes_per_pixel();
}

static uint32_t bottom_sdl_pixel_format(void) {
  return ss_is_new_3ds ? SDL_PIXELFORMAT_ARGB8888 : SDL_PIXELFORMAT_RGB565;
}

enum {
  kBottomRedrawHud = 1 << 0,
  kBottomRedrawFull = 1 << 1,
};

static void request_bottom_redraw(uint32_t request) {
  __atomic_fetch_or(&ss_redraw_requests, request, __ATOMIC_RELEASE);
}

static void prioritize_bottom_touch(void) {
  if (!ss_is_new_3ds && ss_worker_thread)
    svcSetThreadPriority(threadGetHandle(ss_worker_thread),
                         ss_worker_interactive_priority);
}
#endif

static void request_dump_now(void) {
  char dump_dir[160] = {0};
#ifdef __3DS__
  if (Platform3DS_CreateDumpDirectory(dump_dir, sizeof(dump_dir)) &&
      ss_front_buffer >= 0 && ss_present_pixels[ss_front_buffer]) {
    char path[192];
    snprintf(path, sizeof(path), "%s/bottom-screen.bmp", dump_dir);
    if (ss_is_new_3ds) {
      Platform3DS_SaveARGB8888Bmp(
        path, ss_present_pixels[ss_front_buffer], bottom_buffer_pitch(), W, H);
    } else {
      Platform3DS_SaveRGB565Bmp(
        path, ss_present_pixels[ss_front_buffer], bottom_buffer_pitch(), W, H);
    }
  }
#endif
  SS_RequestMemoryDump(dump_dir);
  dump_flash_until = SDL_GetTicks() + 1200;
}

bool SecondScreenSDL_Init(SDL_Window *main_window) {
#ifdef __3DS__
  main_win = main_window;
  ss_is_new_3ds = Platform3DS_IsNew3DS();
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

void SecondScreenSDL_RequestDump(void) {
  if (!ss_enabled)
    return;
  request_dump_now();
}

void SecondScreenSDL_SetDiagnostics(int current_fps, int average_fps) {
  ss_diag_current_fps = current_fps;
  ss_diag_average_fps = average_fps;
}

void SecondScreenSDL_OpenDeveloperOverlay(void) {
  tab = TAB_SETTINGS;
  leave_remap();
  screen_mode = false;
  developer_mode = true;
  developer_overlay_mode = true;
#ifdef __3DS__
  request_bottom_redraw(kBottomRedrawFull);
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
#ifdef __3DS__
  int target = n > 1 ? 1 : 0;
#else
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
#endif

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
#ifdef __3DS__
  W = 320;
  H = 240;
#endif
  if (W <= 0 || H <= 0) { W = 640; H = 480; }
#ifdef __3DS__
  for (int i = 0; i < 2; i++) {
    ss_present_pixels[i] = linearMemAlign(
      k3DSBottomTextureWidth * k3DSBottomTextureHeight *
        bottom_bytes_per_pixel(), 64);
    if (ss_present_pixels[i]) {
      memset(ss_present_pixels[i], 0,
             k3DSBottomTextureWidth * k3DSBottomTextureHeight *
               bottom_bytes_per_pixel());
    }
  }
  if (!ss_present_pixels[0] || !ss_present_pixels[1]) {
    Platform3DS_LogRuntime("ERROR bottom GPU upload buffer allocation");
    for (int i = 0; i < 2; i++) {
      linearFree(ss_present_pixels[i]);
      ss_present_pixels[i] = NULL;
    }
    SDL_DestroyRenderer(ss_r); ss_r = NULL;
    SDL_DestroyWindow(ss_win); ss_win = NULL;
    ss_enabled = false;
    return false;
  }
#endif
  u = unit_for_size(W, H);
  printf("second screen: display %d of %d, %dx%d (u=%.2f)\n", target, n, W, H, u);
#ifdef __3DS__
  Platform3DS_LogRuntime("Bottom screen initialized: display %d of %d, %dx%d",
                         target, n, W, H);
#endif
  return true;
}

static void present_second_screen(void) {
#ifdef __3DS__
  if (!ss_present_pixels[ss_worker_buffer])
    return;
  SDL_RenderReadPixels(
    ss_r, NULL, bottom_sdl_pixel_format(),
    ss_present_pixels[ss_worker_buffer], bottom_buffer_pitch());
#else
  SDL_RenderPresent(ss_r);
#endif
}

#ifdef __3DS__
typedef struct BottomCriticalState {
  int module;
  int area;
  int dungeon;
  int indoors;
  int equipped;
  uint8_t health_cap;
  uint8_t health_cur;
  uint8_t magic;
  uint8_t keys;
  uint8_t bombs;
  uint8_t arrows;
  uint16_t rupees;
  uint32_t inventory_hash;
} BottomCriticalState;

static void request_bottom_redraw_on_state_change(void) {
  static bool initialized;
  static BottomCriticalState previous;
  BottomCriticalState current;
  uint8_t local_sram[0x80];

  memset(&current, 0, sizeof(current));
  SS_ReadSram(local_sram, sizeof(local_sram));
  current.module = SS_GetModule() & 0xff;
  current.area = SS_GetArea();
  current.dungeon = SS_GetDungeon();
  current.indoors = SS_IsIndoors() ? 1 : 0;
  current.equipped = SS_GetEquippedSlot();
  current.health_cap = local_sram[0x6c];
  current.health_cur = local_sram[0x6d];
  current.magic = local_sram[0x6e];
  current.keys = local_sram[0x6f];
  current.bombs = local_sram[0x43];
  current.arrows = local_sram[0x77];
  current.rupees = (uint16_t)local_sram[0x62] |
                   ((uint16_t)local_sram[0x63] << 8);
  current.inventory_hash = 2166136261u;
  if (!ss_is_new_3ds) {
    for (int i = 0x40; i < 0x80; i++) {
      if (i == 0x43 || i == 0x62 || i == 0x63 ||
          (i >= 0x6c && i <= 0x6f) || i == 0x77)
        continue;
      current.inventory_hash =
        (current.inventory_hash ^ local_sram[i]) * 16777619u;
    }
  }

  bool full_changed = initialized &&
    (current.module != previous.module ||
     current.area != previous.area ||
     current.dungeon != previous.dungeon ||
     current.indoors != previous.indoors ||
     current.equipped != previous.equipped ||
     (!ss_is_new_3ds &&
      current.inventory_hash != previous.inventory_hash));
  bool hud_changed = initialized &&
    (current.health_cap != previous.health_cap ||
     current.health_cur != previous.health_cur ||
     current.magic != previous.magic ||
     current.keys != previous.keys ||
     current.bombs != previous.bombs ||
     current.arrows != previous.arrows ||
     current.rupees != previous.rupees);
  previous = current;
  initialized = true;
  if (full_changed || (ss_is_new_3ds && hud_changed))
    request_bottom_redraw(kBottomRedrawFull);
  else if (hud_changed)
    request_bottom_redraw(kBottomRedrawHud);
}

static bool can_patch_bottom_sidebar(void) {
  int module = SS_GetModule() & 0xff;
  return !ss_is_new_3ds && art_ready && ss_front_buffer >= 0 &&
         (tab == TAB_MAP || tab == TAB_ITEMS) &&
         mode_for_module(module) == MODE_GAME;
}

static bool bottom_needs_periodic_redraw(void) {
  if (ss_is_new_3ds || developer_overlay_mode)
    return true;
  if (mode_for_module(SS_GetModule() & 0xff) != MODE_GAME)
    return true;
  return tab == TAB_MAP || tab == TAB_ITEMS;
}

static void draw_bottom_sidebar_patch(void) {
  SS_ReadSram(sram, sizeof(sram));
  bool indoors = SS_IsIndoors();
  int dungeon_info = SS_GetDungeon();
  bool dungeon_mode = indoors && (dungeon_info & 0xff) != 0xff;
  float tab_h = 84 * u;
  float side_w = 200 * u;
  RectFS side = {
    W - side_w + 4 * u,
    10 * u,
    side_w - 14 * u,
    H - tab_h - 14 * u,
  };
  SDL_Rect clip = {
    (int)side.x, (int)side.y, (int)side.w, (int)side.h,
  };

  SDL_RenderSetClipRect(ss_r, &clip);
  SDL_Texture *background = dungeon_mode ? tex_bg_stone : tex_bg_menu;
  int texture_width = dungeon_mode ? kSSTexStone_W : kSSTexMenu_W;
  int texture_height = dungeon_mode ? kSSTexStone_H : kSSTexMenu_H;
  if (background) {
    float tile_width = texture_width * 2.0f;
    float tile_height = texture_height * 2.0f;
    for (float y = 0; y < H; y += tile_height) {
      for (float x = 0; x < W; x += tile_width) {
        SDL_FRect destination = {x, y, tile_width, tile_height};
        SDL_RenderCopyF(ss_r, background, NULL, &destination);
      }
    }
  } else {
    fill_rect(side.x, side.y, side.w, side.h,
              dungeon_mode ? COL_BG_STONE : COL_BG_MENU);
  }
  draw_sidebar(side.x, side.y, side.w, side.h, dungeon_mode);
  SDL_RenderSetClipRect(ss_r, NULL);

  uint8_t *destination = ss_present_pixels[ss_worker_buffer] +
    clip.y * bottom_buffer_pitch() + clip.x * bottom_bytes_per_pixel();
  SDL_RenderReadPixels(ss_r, &clip, bottom_sdl_pixel_format(),
                       destination, bottom_buffer_pitch());
}
#endif

static void handle_tap(float x, float y) {
  int module = SS_GetModule() & 0xFF;
  int ui_mode = mode_for_module(module);
#ifdef __3DS__
  if (!ss_is_new_3ds) {
    ss_touch_request_ticks = svcGetSystemTick();
    ss_touch_redraw_pending = true;
    ss_worker_interactive = true;
    prioritize_bottom_touch();
  }
  request_bottom_redraw(kBottomRedrawFull);
#endif
  if (ui_mode != MODE_GAME) {
    if (tab != TAB_SETTINGS) {
      tab = TAB_SETTINGS;
      leave_settings_submenu();
      return;
    }
    if (!screen_mode && !remap_mode && !developer_mode &&
        in_rect(&cinema_back_r, x, y)) {
      tab = TAB_MAP;
      leave_settings_submenu();
      return;
    }
    goto settings_tap;
  }
  if (!art_ready) return;

  if (in_rect(&tab_items_r, x, y)) { tab = (tab == TAB_ITEMS) ? TAB_MAP : TAB_ITEMS; leave_settings_submenu(); return; }
  if (in_rect(&tab_map_r, x, y))   { tab = TAB_MAP; leave_settings_submenu(); return; }
  if (in_rect(&tab_gear_r, x, y))  { tab = (tab == TAB_GEAR) ? TAB_MAP : TAB_GEAR; leave_settings_submenu(); return; }
  if (in_rect(&tab_settings_r, x, y)) { tab = (tab == TAB_SETTINGS) ? TAB_MAP : TAB_SETTINGS; leave_settings_submenu(); return; }

settings_tap:
  if (tab == TAB_SETTINGS) {
    if (remap_mode) {
      if (in_rect(&remap_back_r, x, y)) { leave_remap(); return; }
      if (in_rect(&remap_page_r, x, y)) {
        remap_first_row = remap_first_row == 0 ? 6 : 0;
        return;
      }
      for (int visible = 0; visible < 6; visible++) {
        int i = remap_first_row + visible;
        if (in_rect(&remap_row_r[visible], x, y)) {
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
    } else if (screen_mode) {
      if (in_rect(&screen_back_r, x, y)) {
        screen_mode = false;
      } else if (in_rect(&screen_row_r[0], x, y)) {
        enum Platform3DSDisplayMode mode = kPlatform3DSDisplayUltraWideMod;
#ifdef __3DS__
        switch (Platform3DS_GetDisplayMode()) {
        case kPlatform3DSDisplayOriginal:
          mode = kPlatform3DSDisplayStretch;
          break;
        case kPlatform3DSDisplayStretch:
          mode = kPlatform3DSDisplayUltraWideMod;
          break;
        case kPlatform3DSDisplayUltraWideMod:
        default:
          mode = kPlatform3DSDisplayOriginal;
          break;
        }
#else
        mode = SS_IsWidescreen() ? kPlatform3DSDisplayOriginal :
               kPlatform3DSDisplayUltraWideMod;
#endif
        SS_Set3DSDisplayMode((int)mode);
#ifdef __3DS__
        Platform3DS_SetDisplayMode(mode);
#endif
        update_ini("[General]", "DisplayMode",
                   mode == kPlatform3DSDisplayOriginal ? "Original" :
                   mode == kPlatform3DSDisplayStretch ? "Stretch" : "Wide");
      } else if (in_rect(&screen_row_r[1], x, y)) {
        enum Platform3DSWideEdgeMode mode = kPlatform3DSWideEdgeStandard;
#ifdef __3DS__
        mode =
          Platform3DS_GetWideEdgeMode() == kPlatform3DSWideEdgeFixedCamera ?
          kPlatform3DSWideEdgeStandard : kPlatform3DSWideEdgeFixedCamera;
        Platform3DS_SetWideEdgeMode(mode);
#endif
        SS_Set3DSWideEdgeMode((int)mode);
        update_ini("[General]", "WideEdgeMode",
                   mode == kPlatform3DSWideEdgeStandard ? "Standard" : "FixedCamera");
      } else if (in_rect(&screen_row_r[2], x, y)) {
#ifdef __3DS__
        if (Platform3DS_GetDisplayMode() == kPlatform3DSDisplayUltraWideMod) {
          int zoom_index = (Platform3DS_GetWideZoomIndex() + 1) % 5;
          Platform3DS_SetWideZoomIndex(zoom_index);
          update_ini("[General]", "WideZoom",
                     zoom_index == 0 ? "1x" :
                     zoom_index == 1 ? "1.2x" :
                     zoom_index == 2 ? "1.5x" :
                     zoom_index == 3 ? "2x" : "2.5x");
        }
#endif
      } else if (in_rect(&screen_row_r[3], x, y)) {
        bool hide = !SS_IsHudHidden();
        SS_SetHudHidden(hide);
        if (hide) { FILE *f = fopen(".ss_hidehud", "wb"); if (f) fclose(f); }
        else remove(".ss_hidehud");
      }
    } else if (developer_mode) {
      if (developer_overlay_mode) {
        if (in_rect(&developer_back_r, x, y))
          developer_overlay_mode = false;
      } else if (in_rect(&developer_back_r, x, y)) {
        developer_mode = false;
      } else if (in_rect(&developer_row_r[0], x, y)) {
        request_dump_now();
      } else if (in_rect(&developer_row_r[1], x, y)) {
        developer_overlay_mode = true;
#ifdef __3DS__
        request_bottom_redraw(kBottomRedrawFull);
#endif
      }
    } else {
      if (in_rect(&settings_row_r[0], x, y)) {
        screen_mode = true;
      } else if (in_rect(&settings_row_r[1], x, y)) {
        int multiplier = 5;
#ifdef __3DS__
        multiplier = Platform3DS_GetTurboMultiplier();
        multiplier = multiplier >= 5 ? 0 : multiplier <= 0 ? 2 : multiplier + 1;
        Platform3DS_SetTurboMultiplier(multiplier);
#endif
        char value[16];
        if (multiplier <= 0)
          snprintf(value, sizeof(value), "Off");
        else
          snprintf(value, sizeof(value), "%d", multiplier);
        update_ini("[General]", "CStickTurboMultiplier", value);
      } else if (in_rect(&settings_row_r[2], x, y)) {
        SS_GetGamepadControls(pad_controls);
        remap_mode = true;
      } else if (in_rect(&settings_row_r[3], x, y)) {
        developer_mode = true;
      } else if (in_rect(&settings_row_r[4], x, y)) {
        SS_RequestRestart();
      } else if (in_rect(&settings_row_r[5], x, y)) {
#ifdef __3DS__
        Platform3DS_RequestRomSelection();
#endif
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
#ifdef __3DS__
    return e->tfinger.windowID == ss_winid;
#else
    if (e->tfinger.windowID == ss_winid) { handle_tap(e->tfinger.x * W, e->tfinger.y * H); return true; }
    return false;
#endif
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
    float draw_x = (320.0f - W) * 0.5f;
    float draw_y = (240.0f - H) * 0.5f;
    float x = (float)pos.px - draw_x;
    float y = (float)pos.py - draw_y;
    handle_tap(x, y);
  }
  was_touching = touching;
#endif
}

static void draw_second_screen(int logic_frames) {
  if (!ss_enabled) return;
#ifndef __3DS__
  static uint32_t frame_no;
  frame_no++;
  if (!ss_win) {
    if (frame_no < 3) return;      // let the game settle its first GL frames
    if (!ensure_window()) return;  // disables itself on failure
  }
  (void)logic_frames;
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
      present_second_screen();
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
    if (tab == TAB_SETTINGS)
      draw_cinema_settings_overlay();
    present_second_screen();
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
  bool full_width = tab == TAB_GEAR || tab == TAB_SETTINGS;
  map_area_r = full_width ?
    (RectFS){10 * u, 10 * u, W - 20 * u, H - tab_h - 14 * u} :
    (RectFS){10 * u, 10 * u, W - side_w - 14 * u, H - tab_h - 14 * u};

  if (tab == TAB_ITEMS)      draw_items(map_area_r);
  else if (tab == TAB_GEAR)  draw_gear(map_area_r);
  else if (tab == TAB_SETTINGS) draw_settings(map_area_r);
  else if (dungeon_mode)     draw_dungeon(map_area_r, link_x, link_y, area & 0xFF, dungeon_info);
  else                       draw_overworld(map_area_r, link_x, link_y, area);

  if (!full_width)
    draw_sidebar(W - side_w + 4 * u, 10 * u, side_w - 14 * u, H - tab_h - 14 * u, dungeon_mode);
  draw_tab_bar(tab_h);

  present_second_screen();
}

#ifdef __3DS__
static void second_screen_worker_main(void *unused) {
  (void)unused;
  for (;;) {
    LightEvent_Wait(&ss_worker_start);
    if (!ss_worker_running)
      break;
    if (!ss_is_new_3ds) {
      s32 priority = ss_worker_interactive ? ss_worker_interactive_priority :
                                             ss_worker_idle_priority;
      svcSetThreadPriority(CUR_THREAD_HANDLE, priority);
    }
    uint64_t start = !ss_is_new_3ds ? svcGetSystemTick() : 0;
    if (ss_worker_sidebar_patch)
      draw_bottom_sidebar_patch();
    else
      draw_second_screen(ss_worker_logic_frames);
    if (!ss_is_new_3ds) {
      uint64_t elapsed = svcGetSystemTick() - start;
      if (ss_worker_sidebar_patch) {
        ss_patch_redraw_count++;
        ss_patch_redraw_total_ticks += elapsed;
        if (elapsed > ss_patch_redraw_max_ticks)
          ss_patch_redraw_max_ticks = elapsed;
      } else {
        ss_full_redraw_count++;
        ss_full_redraw_total_ticks += elapsed;
        if (elapsed > ss_full_redraw_max_ticks)
          ss_full_redraw_max_ticks = elapsed;
      }
      if (ss_worker_touch_request_ticks != 0) {
        uint64_t touch_elapsed =
          svcGetSystemTick() - ss_worker_touch_request_ticks;
        ss_touch_redraw_count++;
        ss_touch_redraw_total_ticks += touch_elapsed;
        if (touch_elapsed > ss_touch_redraw_max_ticks)
          ss_touch_redraw_max_ticks = touch_elapsed;
        ss_worker_touch_request_ticks = 0;
      }
      svcSetThreadPriority(CUR_THREAD_HANDLE, ss_worker_idle_priority);
    }
    LightEvent_Signal(&ss_worker_done);
  }
}

static bool ensure_second_screen_worker(void) {
  if (ss_worker_thread)
    return true;
  LightEvent_Init(&ss_worker_start, RESET_ONESHOT);
  LightEvent_Init(&ss_worker_done, RESET_ONESHOT);
  s32 main_priority = 0x30;
  svcGetThreadPriority(&main_priority, CUR_THREAD_HANDLE);
  ss_worker_idle_priority = main_priority < 0x3f ? main_priority + 1 :
                                                   main_priority;
  ss_worker_interactive_priority = main_priority > 0 ? main_priority - 1 :
                                                       main_priority;
  ss_worker_running = true;
  ss_worker_thread = threadCreate(
    second_screen_worker_main, NULL, 48 * 1024, ss_worker_idle_priority, 0,
    false);
  if (!ss_worker_thread) {
    ss_worker_running = false;
    Platform3DS_LogRuntime(
      "Bottom worker: asynchronous thread unavailable, using synchronous redraws");
    return false;
  }
  Platform3DS_LogRuntime(
    "Bottom worker: Old 3DS touch-priority Core 0 renderer enabled");
  return true;
}

void SecondScreenSDL_BeginFrame(int logic_frames) {
  if (!ss_enabled || Platform3DS_IsSystemClosing())
    return;
  static uint32_t frame_no;
  frame_no++;
  if (!ss_win) {
    if (frame_no < 3 || !ensure_window())
      return;
  }

  request_bottom_redraw_on_state_change();

  if (!ensure_second_screen_worker()) {
    ss_worker_buffer = ss_front_buffer < 0 ? 0 : 1 - ss_front_buffer;
    draw_second_screen(logic_frames);
    ss_front_buffer = ss_worker_buffer;
    ss_frame_ready = true;
    return;
  }

  if (ss_worker_busy && LightEvent_TryWait(&ss_worker_done)) {
    ss_front_buffer = ss_worker_buffer;
    ss_worker_busy = false;
    ss_frame_ready = true;
  }

  int divisor = ss_is_new_3ds ? (logic_frames <= 1 ? 2 : 6) : 180;
  if (developer_overlay_mode)
    request_bottom_redraw(kBottomRedrawFull);

  uint32_t requests =
    __atomic_load_n(&ss_redraw_requests, __ATOMIC_ACQUIRE);
  bool periodic_redraw = ss_front_buffer < 0 ||
    (bottom_needs_periodic_redraw() && frame_no % divisor == 0);
  bool can_start_worker = ss_is_new_3ds || !ss_frame_ready;
  if (!ss_worker_busy && can_start_worker &&
      (requests != 0 || periodic_redraw)) {
    requests = __atomic_exchange_n(&ss_redraw_requests, 0,
                                   __ATOMIC_ACQ_REL);
    ss_worker_sidebar_patch = !periodic_redraw &&
      (requests & kBottomRedrawFull) == 0 &&
      (requests & kBottomRedrawHud) != 0 &&
      can_patch_bottom_sidebar();
    ss_worker_buffer = ss_worker_sidebar_patch ? ss_front_buffer :
      (ss_front_buffer < 0 ? 0 : 1 - ss_front_buffer);
    ss_worker_touch_request_ticks = 0;
    ss_worker_interactive = false;
    if (!ss_worker_sidebar_patch && !ss_is_new_3ds &&
        ss_touch_redraw_pending) {
      ss_worker_touch_request_ticks = ss_touch_request_ticks;
      ss_touch_redraw_pending = false;
      ss_worker_interactive = true;
      prioritize_bottom_touch();
    }
    ss_worker_logic_frames = logic_frames;
    ss_worker_busy = true;
    LightEvent_Signal(&ss_worker_start);
  }
}

void SecondScreenSDL_Update(int logic_frames) {
  (void)logic_frames;
  if (!ss_frame_ready || ss_front_buffer < 0)
    return;
  Platform3DS_PresentBottomFrame(
    ss_present_pixels[ss_front_buffer],
    bottom_buffer_pitch(), W, H);
  ss_frame_ready = false;
}

void SecondScreenSDL_GetOld3DSWorkerStats(
    uint64_t *full_count, uint32_t *full_average_us, uint32_t *full_max_us,
    uint64_t *patch_count, uint32_t *patch_average_us, uint32_t *patch_max_us,
    uint64_t *touch_count, uint32_t *touch_average_us, uint32_t *touch_max_us) {
  if (full_count)
    *full_count = ss_full_redraw_count;
  if (full_average_us)
    *full_average_us = ss_full_redraw_count ? (uint32_t)(
      ss_full_redraw_total_ticks * 1000000ull /
      (SYSCLOCK_ARM11 * ss_full_redraw_count)) : 0;
  if (full_max_us)
    *full_max_us = (uint32_t)(
      ss_full_redraw_max_ticks * 1000000ull / SYSCLOCK_ARM11);
  if (patch_count)
    *patch_count = ss_patch_redraw_count;
  if (patch_average_us)
    *patch_average_us = ss_patch_redraw_count ? (uint32_t)(
      ss_patch_redraw_total_ticks * 1000000ull /
      (SYSCLOCK_ARM11 * ss_patch_redraw_count)) : 0;
  if (patch_max_us)
    *patch_max_us = (uint32_t)(
      ss_patch_redraw_max_ticks * 1000000ull / SYSCLOCK_ARM11);
  if (touch_count)
    *touch_count = ss_touch_redraw_count;
  if (touch_average_us)
    *touch_average_us = ss_touch_redraw_count ? (uint32_t)(
      ss_touch_redraw_total_ticks * 1000000ull /
      (SYSCLOCK_ARM11 * ss_touch_redraw_count)) : 0;
  if (touch_max_us)
    *touch_max_us = (uint32_t)(
      ss_touch_redraw_max_ticks * 1000000ull / SYSCLOCK_ARM11);
}
#else
void SecondScreenSDL_Update(int logic_frames) {
  draw_second_screen(logic_frames);
}
#endif

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
#ifdef __3DS__
  if (ss_worker_thread) {
    ss_worker_running = false;
    LightEvent_Signal(&ss_worker_start);
    Result join_result = threadJoin(ss_worker_thread, 2000000000ull);
    if (R_FAILED(join_result))
      Platform3DS_LogRuntime("WARNING: second screen worker join timeout: 0x%08lx",
                             (unsigned long)join_result);
    threadFree(ss_worker_thread);
    ss_worker_thread = NULL;
  }
  for (int i = 0; i < 2; i++) {
    linearFree(ss_present_pixels[i]);
    ss_present_pixels[i] = NULL;
  }
#endif
  if (!ss_win) return;
  destroy_textures();
  SDL_DestroyRenderer(ss_r); ss_r = NULL;
  SDL_DestroyWindow(ss_win); ss_win = NULL;
}
