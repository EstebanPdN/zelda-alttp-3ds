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
#include "updater.h"
#include "native_touch.h"
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
#include "../../dump_state.h"
#include "../../second_screen_tables.h"  // kIconCount/kIconCols/kGlyphCount/kGlyphCols
#include "ss_sheets.h"             // generated cell indices for icons/glyphs/letters
#include "second_screen_texture.h"
#include "second_screen_shapes.h"
#include "ss_textures.h"           // baked theme background tiles (menu/parchment/stone)
#include "ss_tex_triforce.h"       // baked AA triangle mask for the cinema-screen triforce

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
int  SS_GetEquippedSlotX(void);
int  SS_GetDungeon(void);
bool SS_GetIndoorExit(int *out);
void SS_ReadDungFlags(uint8_t *out, int n);
bool SS_RenderIconSheet(uint32_t *px);
bool SS_RenderGlyphSheet(uint32_t *px);
bool SS_RenderLetterSheet(uint32_t *px);
bool SS_RenderWorldMap(uint32_t *px, bool dark);
bool SS_GetMirrorPortal(int *out);
void SS_ResetRomCaches(void);
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
void SS_RequestLoadLatestDumpState(void);
int  SS_TakeLoadDumpStateResult(void);
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
  // A Link Between Worlds skin
  COL_WOOD        = COL(74, 44, 24),     // stand-in when the wood tile is missing
  COL_INK         = COL(27, 29, 36),     // dark text on stone tablets
  COL_INK_LIT     = COL(170, 175, 186),  // its engraved highlight
  COL_PARCH_INK   = COL(58, 42, 18),
  COL_PARCH_EDGE  = COL(138, 106, 44),
  COL_RIM_DARK    = COL(42, 26, 12),
  COL_RIM_BROWN   = COL(122, 84, 24),
  COL_RIM_GOLD    = COL(227, 182, 76),
  COL_SHADOW      = COL(26, 16, 10),
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
static SDL_Texture *tex_mapicons;
static SDL_Texture *tex_bg_parch, *tex_bg_wood;
static SDL_Texture *tex_ui_icons, *tex_heart_scratch;  // chrome icons; Old hearts layer
#ifdef __3DS__
#include "bottom_hearts.h"
static BottomHearts ss_worker_hearts[2], ss_display_hearts;
static uint8_t *ss_old_display_pixels;
static bool ss_old_display_valid;
static void capture_bottom_hearts(int count, int size, int columns, int step,
                                 int x, int y);
#endif

static SDL_Texture *tex_triforce;
static bool ss_is_new_3ds;  // Model is set before drawing the cinema card.
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
static bool update_mode, update_confirm, update_show_notes;
static unsigned update_page;
static RectFS update_release_r, update_prev_r, update_next_r;
static RectFS update_back_r, update_channel_r, update_action_r;
static RectFS settings_row_r[5], remap_row_r[6], remap_back_r;
static RectFS remap_page_r;
static RectFS screen_row_r[5], screen_back_r;
static RectFS cinema_back_r;
static RectFS developer_row_r[5], developer_back_r;
static RectFS load_cancel_r, load_confirm_r;

// settings / remap state
static bool remap_mode;
static bool screen_mode;
static bool developer_mode;
static bool developer_overlay_mode;
static bool load_confirm_mode;
#ifndef __3DS__
static bool developer_show_fps;
#endif
static int  remap_first_row;
static int  remap_arm = -1;         // row currently waiting for a button press
static uint32_t remap_arm_at;
static int  pad_controls[12];
static bool hud_pref_applied;
static uint32_t dump_flash_until;
static uint32_t load_flash_until;
static int load_result = -1;
static RectFS plaque_r[16];
static int    plaque_floor[16], plaque_count;
static float  grid_x, grid_y, grid_cell, grid_row_h;
static int    ss_diag_current_fps;
static int    ss_diag_average_fps;
#ifdef __3DS__
static uint32_t ss_diag_last_redraw_ms;
#endif

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
  // One unit is half a 3DS pixel; the desktop draws the same 320x240 layout.
  if (unit < 0.5f)
    unit = 0.5f;
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
#if defined(__3DS__) || defined(ZELDA3_TEST_3DS_UI)
  if (Platform3DS_GetHardwareProfile()->integer_ui_rounding) {
    set_color(c);
    SecondScreenFillRound(ss_r, x, y, w, h, rad);
    return;
  }
#endif
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
static void draw_x_mark(float cx, float cy, float r, float t, uint32_t c) {
  set_color(c);
  for (float d = -r; d <= r; d += 1.0f) {
    SDL_FRect a = {cx + d - t / 2, cy + d - t / 2, t, t};
    SDL_FRect b = {cx + d - t / 2, cy - d - t / 2, t, t};
    SDL_RenderFillRectF(ss_r, &a);
    SDL_RenderFillRectF(ss_r, &b);
  }
}
// blit one cell from a sheet texture
static void draw_cell(SDL_Texture *tex, int cell, int cellpx, int cols, float x, float y, float s) {
  if (cell < 0 || !tex) return;
  SDL_Rect src = {(cell % cols) * cellpx, (cell / cols) * cellpx, cellpx, cellpx};
  SDL_FRect dst = {x, y, cellpx * s, cellpx * s};
  SDL_RenderCopyF(ss_r, tex, &src, &dst);
}
static void draw_glyph(int cell, float x, float y, float s) { draw_cell(tex_glyphs, cell, 8, SS_GLYPH_COLS, x, y, s); }

// ROM sprite with a soft drop shadow: once 1 px lower in dark brown, then normal.
static void copy_with_shadow(SDL_Texture *t, const SDL_Rect *src, SDL_FRect dst) {
  if (!t) return;
  SDL_FRect below = {dst.x, dst.y + 1, dst.w, dst.h};
  SDL_SetTextureColorMod(t, 26, 16, 8);
  SDL_SetTextureAlphaMod(t, 200);
  SDL_RenderCopyF(ss_r, t, src, &below);
  SDL_SetTextureColorMod(t, 255, 255, 255);
  SDL_SetTextureAlphaMod(t, 255);
  SDL_RenderCopyF(ss_r, t, src, &dst);
}
// Item art: the 14 px inside of a 16 px icon cell, scaled to size.
static void draw_item_icon(int cell, float x, float y, float size) {
  if (cell < 0) return;
  SDL_Rect src = {(cell % SS_ICON_COLS) * 16 + 1, (cell / SS_ICON_COLS) * 16 + 1, 14, 14};
  copy_with_shadow(tex_icons, &src, (SDL_FRect){x, y, size, size});
}
// Gear art: the whole 16 px icon cell at 1x.
static void draw_gear_icon(int cell, float x, float y) {
  if (cell < 0) return;
  SDL_Rect src = {(cell % SS_ICON_COLS) * 16, (cell / SS_ICON_COLS) * 16, 16, 16};
  copy_with_shadow(tex_icons, &src, (SDL_FRect){x, y, 16, 16});
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
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    if (ch == ' ') { cx += 5 * sc; continue; }
    if (ch >= '0' && ch <= '9') draw_glyph(kDigitGlyph[ch - '0'], cx, y, sc);
    else if (ch >= 'A' && ch <= 'Z') draw_cell(tex_letters, kSS_LetterCell[ch - 'A'], 8, SS_LETTER_COLS, cx, y, sc);
    else if (ch == '.') fill_rect(cx + 3 * sc, y + 6 * sc, 2 * sc, 2 * sc, COL(255, 255, 255));
    else if (ch == '-') fill_rect(cx + sc, y + 3 * sc, 6 * sc, sc, COL(255, 255, 255));
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

// Baked Nunito ExtraBold label font (ui_font_{large,small}.png): white glyphs
// tinted per call. If a sheet failed to load, LARGE falls back to the ROM
// letters and SMALL to the 5x7 block font above.
enum { FONT_LARGE, FONT_SMALL };
typedef struct UiFont {
  SDL_Texture *tex;
  int cell_w, cell_h, cols, cap_top, pad, space;
  const unsigned char *advance;
} UiFont;
static UiFont ui_font[2] = {
  {NULL, SS_FONT_LARGE_CELL_W, SS_FONT_LARGE_CELL_H, SS_FONT_LARGE_COLS,
   SS_FONT_LARGE_CAP_TOP, SS_FONT_LARGE_PAD, SS_FONT_LARGE_SPACE, kSS_FontLargeAdvance},
  {NULL, SS_FONT_SMALL_CELL_W, SS_FONT_SMALL_CELL_H, SS_FONT_SMALL_COLS,
   SS_FONT_SMALL_CAP_TOP, SS_FONT_SMALL_PAD, SS_FONT_SMALL_SPACE, kSS_FontSmallAdvance},
};

// Sheet index of a character; lower case maps to upper case except the x of "x5".
static int ui_glyph(char ch) {
  if (ch >= 'a' && ch <= 'z' && ch != 'x') ch -= 'a' - 'A';
  const char *p = ch ? strchr(kSS_FontChars, ch) : NULL;
  return p ? (int)(p - kSS_FontChars) : -1;
}

static float ui_text_width(const char *s, int font, int spacing) {
  const UiFont *f = &ui_font[font];
  if (!f->tex) return font == FONT_LARGE ? text_width(s, 1) : tiny_text_width(s, 1);
  float w = 0;
  for (const char *c = s; *c; c++) {
    int g = ui_glyph(*c);
    w += (g >= 0 ? f->advance[g] : f->space) + (c[1] ? spacing : 0);
  }
  return w;
}

// Draws s with the top of its capitals at y; spacing is extra pixels per glyph.
static void ui_text(const char *s, float x, float y, int font, uint32_t color, int spacing) {
  const UiFont *f = &ui_font[font];
  uint8_t r = (color >> 16) & 0xff, g = (color >> 8) & 0xff, b = color & 0xff;
  if (!f->tex) {
    if (font == FONT_SMALL) {
      draw_tiny_text(s, x, y, 1, color);
    } else {
      SDL_SetTextureColorMod(tex_letters, r, g, b);
      SDL_SetTextureColorMod(tex_glyphs, r, g, b);
      draw_text(s, x, y, 1);
      SDL_SetTextureColorMod(tex_letters, 255, 255, 255);
      SDL_SetTextureColorMod(tex_glyphs, 255, 255, 255);
    }
    return;
  }
  SDL_SetTextureColorMod(f->tex, r, g, b);
  float cx = floorf(x + 0.5f), cy = floorf(y + 0.5f) - f->cap_top;
  for (; *s; s++) {
    int gi = ui_glyph(*s);
    if (gi >= 0) {
      SDL_Rect src = {gi % f->cols * f->cell_w, gi / f->cols * f->cell_h, f->cell_w, f->cell_h};
      SDL_FRect dst = {cx - f->pad, cy, (float)f->cell_w, (float)f->cell_h};
      SDL_RenderCopyF(ss_r, f->tex, &src, &dst);
    }
    cx += (gi >= 0 ? f->advance[gi] : f->space) + spacing;
  }
}

// Centred on cx; a non-zero shadow is drawn 1 px lower first (engraved look).
static void ui_text_center(const char *s, float cx, float y, int font,
                           uint32_t color, uint32_t shadow, int spacing) {
  float x = floorf(cx - ui_text_width(s, font, spacing) / 2 + 0.5f);
  if (shadow) ui_text(s, x, y + 1, font, shadow, spacing);
  ui_text(s, x, y, font, color, spacing);
}

// textures from second_screen.c buffers
static SDL_Texture *make_tex(int w, int h, const void *px, bool blend) {
  bool prefer_rgb565 = false;
#ifdef __3DS__
  prefer_rgb565 = Platform3DS_GetHardwareProfile()->rgb565_ui_textures;
#endif
  return SecondScreenCreateTexture(ss_r, w, h, px, blend, prefer_rgb565);
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

// A Link Between Worlds skin. Everything is flat fills and copies: gradients
// are one span per row, gold rims are concentric discs.
static uint32_t mix_col(uint32_t a, uint32_t b, float t) {
  uint32_t out = 0xff000000u;
  for (int sh = 0; sh < 24; sh += 8) {
    float ca = (float)((a >> sh) & 0xff), cb = (float)((b >> sh) & 0xff);
    out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << sh;
  }
  return out;
}

// Rounded box with a vertical gradient (top, mid at 60%, bottom), one span per
// row with the same corner insets as SecondScreenFillRound. hl/sh, when set,
// replace the first row and the last two rows: a tablet's inset light and shade.
static void fill_round_grad(float x, float y, float w, float h, float rad,
                            uint32_t top, uint32_t mid, uint32_t bot,
                            uint32_t hl, uint32_t sh) {
  int left = (int)floorf(x + 0.5f), ty = (int)floorf(y + 0.5f);
  int width = (int)floorf(x + w + 0.5f) - left;
  int height = (int)floorf(y + h + 0.5f) - ty;
  if (width <= 0 || height <= 0) return;
  int r = (int)floorf(rad + 0.5f);
  if (r < 0) r = 0;
  if (r > width / 2) r = width / 2;
  if (r > height / 2) r = height / 2;
  for (int row = 0; row < height; row++) {
    int d = row < r ? r - row : row >= height - r ? row - (height - r) + 1 : 0;
    int inset = 0;
    if (d > 0) {
      float dy = d - 0.5f;
      inset = (int)ceilf(r - sqrtf((float)r * r - dy * dy) - 0.5f);
    }
    float t = height > 1 ? row / (float)(height - 1) : 0;
    uint32_t c = t < 0.6f ? mix_col(top, mid, t / 0.6f) : mix_col(mid, bot, (t - 0.6f) / 0.4f);
    if (hl && row == 0) c = hl;
    if (sh && row >= height - 2) c = sh;
    fill_rect(left + inset, ty + row, width - 2 * inset, 1, c);
  }
}

static void disc(float cx, float cy, float d, uint32_t c) {
  fill_round(cx - d / 2, cy - d / 2, d, d, d / 2, c);
}

// Dark wood ground, tiled from the screen origin so partial redraws line up.
// Leaves no clip rect set.
static void draw_ground(SDL_Rect clip) {
  SDL_RenderSetClipRect(ss_r, &clip);
  if (!tex_bg_wood) fill_rect(clip.x, clip.y, clip.w, clip.h, COL_WOOD);
  int tw = kSSTexWood_W * 2, th = kSSTexWood_H * 2;
  for (int y = clip.y / th * th; tex_bg_wood && y < clip.y + clip.h; y += th)
    for (int x = clip.x / tw * tw; x < clip.x + clip.w; x += tw) {
      SDL_FRect dst = {(float)x, (float)y, (float)tw, (float)th};
      SDL_RenderCopyF(ss_r, tex_bg_wood, NULL, &dst);
    }
  SDL_RenderSetClipRect(ss_r, NULL);
}

// Parchment scroll: gold-brown border, light inner line, tiled paper and the
// rolled "curl" strip down the left edge.
static void draw_parchment(RectFS r) {
  fill_round(r.x, r.y + 2, r.w, r.h, 6, COL_SHADOW);
  fill_round(r.x, r.y, r.w, r.h, 6, COL_PARCH_EDGE);
  RectFS in = {r.x + 2, r.y + 2, r.w - 4, r.h - 4};
  draw_tiled(tex_bg_parch, kSSTexParch_W, kSSTexParch_H, in, COL_BG_PARCH);
  draw_frame(in.x, in.y, in.w, in.h, 1, COL(249, 237, 200));
  for (int i = 0; i < 12; i++) {
    float t = i / 11.0f;
    uint32_t c = t < 0.55f ? mix_col(COL(169, 138, 78), COL(232, 215, 168), t / 0.55f)
                           : mix_col(COL(232, 215, 168), COL(201, 176, 120), (t - 0.55f) / 0.45f);
    fill_rect(in.x + i, in.y, 1, in.h, i == 11 ? mix_col(c, COL(0, 0, 0), 0.15f) : c);
  }
}

// Small parchment strip (gear sections).
static void draw_strip(RectFS r) {
  fill_round(r.x, r.y + 1, r.w, r.h, 4, COL_SHADOW);
  fill_round(r.x, r.y, r.w, r.h, 4, COL_PARCH_EDGE);
  fill_round(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 3, COL(249, 237, 200));
  fill_round_grad(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 2, COL(240, 225, 184),
                  COL(229, 210, 158), COL(220, 198, 140), 0, 0);
}

// Dark plaque with a gold-brown border (status bar, info rows, version).
static void draw_plaque(RectFS r) {
  fill_round(r.x, r.y + 1, r.w, r.h, 5, COL(0, 0, 0));
  fill_round(r.x, r.y, r.w, r.h, 5, COL_RIM_BROWN);
  fill_round(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 4, COL(90, 58, 24));
  fill_round_grad(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 3, COL(45, 28, 16),
                  COL(32, 19, 10), COL(26, 15, 8), 0, 0);
}

// Stone tablet (tabs, settings rows); gold when active.
static void draw_tablet(RectFS r, bool active, float rad) {
  fill_round(r.x, r.y + 1, r.w, r.h, rad, COL(0, 0, 0));
  if (active)
    fill_round_grad(r.x, r.y, r.w, r.h, rad, COL(243, 210, 124), COL(212, 164, 63),
                    COL(169, 120, 42), COL(255, 240, 192), COL(110, 74, 20));
  else
    fill_round_grad(r.x, r.y, r.w, r.h, rad, COL(142, 148, 162), COL(92, 99, 115),
                    COL(69, 75, 88), COL(192, 196, 207), COL(43, 47, 56));
}

// Parchment title ribbon centred on cx.
static void draw_ribbon(const char *s, float cx, float y) {
  float tw = ui_text_width(s, FONT_LARGE, 2);
  RectFS r = {floorf(cx - tw / 2 - 15 + 0.5f), y, floorf(tw + 30 + 0.5f), 20};
  fill_round(r.x, r.y + 1, r.w, r.h, 3, COL_SHADOW);
  fill_round(r.x, r.y, r.w, r.h, 3, COL_PARCH_EDGE);
  fill_round_grad(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 2, COL(242, 227, 187),
                  COL(228, 208, 158), COL(217, 194, 138), 0, 0);
  ui_text(s, r.x + 15, r.y + 6, FONT_LARGE, COL_PARCH_INK, 2);
}

// Round slot: (x, y, d) is the dark centre's box; the rims lie outside it.
enum { SLOT_GOLD, SLOT_CELL, SLOT_EMPTY, SLOT_SELECTED };
static void draw_round_slot(float x, float y, float d, int style) {
  float cx = x + d / 2, cy = y + d / 2, r = d / 2;
  uint32_t lit = COL(58, 38, 20), dark = COL(27, 17, 10);
  if (style == SLOT_GOLD) {            // X/Y/B slots and the map zoom button
    disc(cx, cy + 2, d + 10, COL(18, 11, 6));
    disc(cx, cy, d + 10, COL_RIM_DARK);
    disc(cx, cy, d + 8, COL_RIM_GOLD);
    disc(cx, cy, d + 4, COL_RIM_BROWN);
  } else if (style == SLOT_SELECTED) { // equipped item: bright gold ring and glow
    disc(cx, cy, d + 14, COL(104, 76, 34));
    disc(cx, cy, d + 10, COL(158, 120, 52));
    disc(cx, cy, d + 8, COL(255, 242, 192));
    disc(cx, cy, d + 6, COL_RIM_GOLD);
    disc(cx, cy, d + 2, COL_RIM_DARK);
    lit = COL(46, 29, 16); dark = COL(22, 13, 7);
  } else if (style == SLOT_CELL) {
    disc(cx, cy, d + 6, COL(26, 16, 10));
    disc(cx, cy, d + 4, COL_RIM_BROWN);
    disc(cx, cy, d + 2, COL_RIM_DARK);
    lit = COL(46, 29, 16); dark = COL(22, 13, 7);
  } else {                             // not owned: dim ring, dull centre
    disc(cx, cy, d + 6, COL(48, 29, 15));
    disc(cx, cy, d + 4, COL(66, 46, 25));
    disc(cx, cy, d + 2, COL(55, 35, 18));
    lit = COL(50, 32, 18); dark = COL(41, 25, 13);
  }
  // centre lit from 35% of its height
  disc(cx, cy, d, dark);
  disc(cx, cy - r * 0.3f, r * 1.36f, mix_col(dark, lit, 0.5f));
  disc(cx, cy - r * 0.3f, r * 0.7f, lit);
}

// 24 px (big) or 12 px chrome icon from ui_icons.png.
static void draw_ui_icon(int icon, float x, float y, bool big) {
  if (!tex_ui_icons) return;
  int sz = big ? SS_UI_ICON_CELL : SS_UI_ICON_CELL / 2;
  SDL_Rect src = {icon * SS_UI_ICON_CELL, big ? 0 : SS_UI_ICON_CELL, sz, sz};
  SDL_FRect dst = {x, y, (float)sz, (float)sz};
  SDL_RenderCopyF(ss_r, tex_ui_icons, &src, &dst);
}

// One status-bar heart (0 empty, 1 half, 2 full): the 12 px icon cropped to
// 11x11 (its transparent top row and faint left column) so hearts on the
// 11 px pitch never overlap; the Old retained layer relies on that.
static void draw_heart(int glyph, int x, int y) {
  static const int kHeart[3] = {SS_UI_HEART_OFF, SS_UI_HEART_HALF, SS_UI_HEART};
  if (!tex_ui_icons) return;
  SDL_Rect src = {kHeart[glyph] * SS_UI_ICON_CELL + 1, SS_UI_ICON_CELL + 1, 11, 11};
  SDL_Rect dst = {x, y, 11, 11};
  SDL_RenderCopy(ss_r, tex_ui_icons, &src, &dst);
}

// Letter badge on a slot's top-left (cx, cy is its centre).
static void draw_key_badge(float cx, float cy, const char *key) {
  disc(cx, cy, 16, COL_RIM_GOLD);
  disc(cx, cy, 14, COL(26, 16, 10));
  ui_text_center(key, cx, cy - 3, FONT_SMALL, COL_RIM_GOLD, 0, 0);
}

static bool try_load_art(void) {
  static uint32_t buf[512 * 512];   // reused for every sheet; world map is the largest
  uint8_t lay[16 * 25];

  // theme tiles, chrome icons and the label font are baked into the binary
  if (!tex_bg_parch) tex_bg_parch = make_tex(kSSTexParch_W, kSSTexParch_H, kSSTexParch, false);
  if (!tex_bg_wood) {
    // the shared wood tile is a light plank; darken it once for the ground
    static uint32_t wood[kSSTexWood_W * kSSTexWood_H];
    for (int i = 0; i < kSSTexWood_W * kSSTexWood_H; i++) {
      uint32_t c = kSSTexWood[i];
      wood[i] = 0xff000000u | (((c >> 16) & 0xff) * 9 / 16) << 16 |
                (((c >> 8) & 0xff) * 9 / 16) << 8 | (c & 0xff) * 9 / 16;
    }
    tex_bg_wood = make_tex(kSSTexWood_W, kSSTexWood_H, wood, false);
  }
  if (!tex_ui_icons) tex_ui_icons = make_tex(kSSUiIcons_W, kSSUiIcons_H, kSSUiIcons, true);
  if (!ui_font[FONT_LARGE].tex)
    ui_font[FONT_LARGE].tex = make_tex(kSSUiFontLarge_W, kSSUiFontLarge_H, kSSUiFontLarge, true);
  if (!ui_font[FONT_SMALL].tex)
    ui_font[FONT_SMALL].tex = make_tex(kSSUiFontSmall_W, kSSUiFontSmall_H, kSSUiFontSmall, true);
#ifdef __3DS__
  // Old retained hearts put each cell's background back through this texture.
  if (!ss_is_new_3ds && !tex_heart_scratch) {
    tex_heart_scratch = SDL_CreateTexture(ss_r, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STATIC, 12, 12);
    if (tex_heart_scratch) SDL_SetTextureBlendMode(tex_heart_scratch, SDL_BLENDMODE_NONE);
  }
#endif
  if (!tex_triforce) {
    tex_triforce = make_tex(kSSTexTriforce_W, kSSTexTriforce_H, kSSTexTriforce, true);
    // baked as a smooth AA silhouette, not pixel art -- scale it smoothly.
    if (tex_triforce) SDL_SetTextureScaleMode(tex_triforce, SDL_ScaleModeLinear);
  }

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
  if (!tex_triforce) return;

  uint8_t r, g, b;
  if (ss_is_new_3ds) {
    float t = SDL_GetTicks() / 1000.0f;
    float pulse = sinf(t * 1.5f) * 0.5f + 0.5f;
    uint8_t v = (uint8_t)(150 + 100 * pulse);
    r = v; g = (uint8_t)(v * 0.83f); b = (uint8_t)(v * 0.41f);
  } else {
    // Old 3DS only redraws this screen every ~180 frames (see
    // bottom_needs_periodic_redraw's divisor), so a moving pulse just reads
    // as an occasional jump rather than real animation. Use one fixed,
    // pleasant gold tone instead of computing it every redraw.
    r = 214; g = 178; b = 88;
  }
  SDL_SetTextureColorMod(tex_triforce, r, g, b);

  // kSSTexTriforce is ~tight to the triangle's bounding box, so a dest rect
  // of {s, s} reproduces the same triangle "row height" the old per-scanline
  // tri_up(cx, top, size, ...) used, keeping the overall triforce the same
  // size and layout as before.
  float s = (W < H ? W : H) * 0.06f;
  float cx = W / 2.0f, cy = H / 2.0f;
  SDL_FRect top_r = {cx - s / 2, cy - s + 1, s, s};
  SDL_FRect bl_r  = {cx - s * 0.58f - s / 2, cy, s, s};
  // Baked-texture rounding leaves the bottom-right triangle ~1px further
  // from center than bottom-left (measured on real output: bottom-left sits
  // 32px from the top triangle's center, bottom-right sits 36px) -- nudge
  // it in by 1px so the center notch is symmetric instead of lopsided.
  SDL_FRect br_r  = {cx + s * 0.58f - s / 2 - 1, cy, s, s};
  SDL_RenderCopyF(ss_r, tex_triforce, NULL, &top_r);
  SDL_RenderCopyF(ss_r, tex_triforce, NULL, &bl_r);
  SDL_RenderCopyF(ss_r, tex_triforce, NULL, &br_r);
}

// Round gold +/- at the parchment's top-left (the whole map toggles the zoom).
// Both strokes snap to the button's pixel grid with matching size parity, so
// the glyph stays exactly centred.
static void draw_map_zoom_button(RectFS r) {
  int size = 22, length = 10, thickness = 2;
  int bx = (int)lroundf(r.x + 24), by = (int)lroundf(r.y + 13);
  draw_round_slot(bx, by, size, SLOT_GOLD);
  SDL_Rect horizontal = {bx + (size - length) / 2, by + (size - thickness) / 2, length, thickness};
  SDL_Rect vertical = {bx + (size - thickness) / 2, by + (size - length) / 2, thickness, length};
  set_color(COL_RIM_GOLD);
  SDL_RenderFillRect(ss_r, &horizontal);
  if (whole_map) SDL_RenderFillRect(ss_r, &vertical);
}

static void draw_overworld(RectFS r, int link_x, int link_y, int area) {
  // parchment scroll; the live map sits in an inset right of the curl
  draw_parchment(r);
  RectFS m = {r.x + 20, r.y + 9, r.w - 26, r.h - 18};

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

  // The mirror return point uses the same projection and clipping as Link.
  int portal[2];
  if (!dark && SS_GetMirrorPortal(portal)) {
    float mx = ox + (128.0f + portal[0] / 4096.0f * 256.0f) * scale;
    float my = oy + (128.0f + portal[1] / 4096.0f * 256.0f) * scale;
    fill_round(mx - 9*u, my - 9*u, 18*u, 18*u, 9*u, COL_OUTLINE);
    fill_round(mx - 7*u, my - 7*u, 14*u, 14*u, 7*u, COL(240, 192, 255));
    fill_round(mx - 3*u, my - 3*u, 6*u, 6*u, 3*u, COL(80, 48, 176));
  }

  // Link's bobbing head
  float fx = ox + px * scale, fy = oy + py * scale;
  float bob = sinf(SDL_GetTicks() / 300.0f) * 2 * u;
  float fs = (whole_map ? 1.2f : 1.6f) * u * 2;   // face tex is 16px (Java pre-scaled to 32)
  SDL_FRect fdst = {fx - 16 * fs / 2, fy - 16 * fs / 2 + bob, 16 * fs, 16 * fs};
  SDL_RenderCopyF(ss_r, tex_face, NULL, &fdst);
  SDL_RenderSetClipRect(ss_r, NULL);
  draw_frame(m.x, m.y, m.w, m.h, 1, COL(92, 70, 34));

  draw_map_zoom_button(r);
}

static void draw_dungeon(RectFS r, int link_x, int link_y, int room, int dungeon_info) {
  int palace = dungeon_info & 0xFF;
  int floor = (int8_t)(dungeon_info >> 8);
  Dungeon *d = (palace >= 0 && palace < 14) ? &dungeons[palace] : NULL;

  // parchment scroll, dungeon name on a ribbon
  draw_parchment(r);
  draw_ribbon(d ? d->name : "DUNGEON", r.x + 2 + (r.w - 4) * 0.58f, r.y + 8);
  if (!d) return;

  if (view_floor_touched_at && SDL_GetTicks() - view_floor_touched_at > 6000) {
    view_floor_offset = 0;
    view_floor_touched_at = 0;
  }
  int li = floor + view_floor_offset + d->basements;
  if (li < 0) li = 0;
  if (li > d->floors - 1) li = d->floors - 1;
  int view_floor = li - d->basements;

  // wooden floor tabs down the left: the viewed floor blue, Link's face beside
  // his own floor. A tab's touch area also covers its gaps.
  plaque_count = 0;
  float pitch = d->floors > 6 ? 136.0f / d->floors : 20;
  float ph = pitch - 4 < 16 ? pitch - 4 : 16, pw = 30;
  float px0 = r.x + 26, py0 = r.y + 36;
  for (int f = d->floors - 1; f >= 0; f--) {
    int fl = f - d->basements;
    if (plaque_count >= 16) break;
    RectFS tabr = {px0, floorf(py0 + 0.5f), pw, ph};
    plaque_r[plaque_count] = (RectFS){px0 - 10, py0 - (pitch - ph) / 2, pw + 14, pitch};
    plaque_floor[plaque_count] = fl;
    plaque_count++;
    bool sel = (fl == view_floor);
    fill_round(tabr.x, tabr.y, tabr.w, tabr.h, 3, sel ? COL(27, 58, 104) : COL_RIM_DARK);
    if (sel)
      fill_round_grad(tabr.x + 1, tabr.y + 1, tabr.w - 2, tabr.h - 2, 2, COL(90, 160, 240),
                      COL(62, 122, 204), COL(44, 94, 168), 0, 0);
    else
      fill_round_grad(tabr.x + 1, tabr.y + 1, tabr.w - 2, tabr.h - 2, 2, COL(107, 63, 34),
                      COL(90, 53, 28), COL(74, 44, 24), 0, 0);
    char label[16];
    if (fl >= 0) snprintf(label, sizeof(label), "%dF", fl + 1);
    else snprintf(label, sizeof(label), "B%d", -fl);
    ui_text_center(label, tabr.x + tabr.w / 2, tabr.y + floorf((tabr.h - 6) / 2), FONT_SMALL,
                   sel ? COL(255, 255, 255) : COL(232, 215, 168), 0, 1);
    if (fl == floor) {
      SDL_FRect fdst = {tabr.x - 17, tabr.y + tabr.h / 2 - 8, 16, 16};
      SDL_RenderCopyF(ss_r, tex_face, NULL, &fdst);
    }
    py0 += pitch;
  }

  // slate inset with one rounded tile per room: visited rooms blue, rooms
  // known only from the map item dim, Link's room gold.
  // ponytail: the ROM's per-quadrant room shapes are not drawn.
  RectFS g = {r.x + 60, r.y + 30, 150, 130};
  fill_round(g.x, g.y, g.w, g.h, 4, COL(22, 24, 30));
  fill_round_grad(g.x + 1, g.y + 1, g.w - 2, g.h - 2, 3, COL(44, 49, 60),
                  COL(36, 40, 50), COL(31, 35, 43), 0, 0);
  const uint8_t *lay = d->layout[li];
  float cell = 24;
  float gx = g.x + 15, gy = g.y + 5;
  bool have_map = (sram16(0x68) & (0x8000 >> palace)) != 0;
  for (int i = 0; i < 25; i++) {
    int v = lay[i];
    if (v == 0x0F) continue;
    bool is_cur = (v == (room & 0xFF)) && view_floor == floor;
    if (!is_cur && !(dung_flag(v) & 0xF) && !have_map) continue;
    float x = gx + (i % 5) * cell + 3, y = gy + (i / 5) * cell + 3;
    uint32_t edge = COL(31, 79, 148), top = COL(127, 182, 246), bot = COL(74, 142, 224);
    if (is_cur) { edge = COL_RIM_BROWN; top = COL(255, 224, 138); bot = COL(224, 168, 60); }
    else if (!(dung_flag(v) & 0xF)) { edge = COL(34, 56, 90); top = COL(84, 112, 152); bot = COL(58, 84, 120); }
    fill_round(x, y, 18, 18, 3, edge);
    fill_round_grad(x + 1, y + 1, 16, 16, 2, top, mix_col(top, bot, 0.6f), bot,
                    mix_col(top, COL(255, 255, 255), 0.4f), 0);
  }

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

// Grid slot 1..20 -> icon cell of the item's current level, -1 when not owned.
static int item_cell(int slot) {
  if (slot < 1 || slot > 20) return -1;
  int i = slot - 1;
  int lv = (i == 15) ? bottle_level() : sram8(0x40 + i);
  if (lv <= 0) return -1;
  if (lv > kSS_ItemMaxLevel[i]) lv = kSS_ItemMaxLevel[i];
  return kSS_ItemCell[i][lv];
}

static void draw_items(RectFS r) {
  (void)r;
  draw_ribbon("ITEMS", W / 2.0f, 4);
  // 5x4 round cells at 44 x 38 px; touch splits the gaps between them
  grid_cell = 44; grid_row_h = 38;
  grid_x = 16 - 6; grid_y = 24 - 3;

  int equipped = SS_GetEquippedSlot();
  bool flash = SDL_GetTicks() < tap_flash_until;
  for (int i = 0; i < 20; i++) {
    float x = 16 + (i % 5) * 44.0f, y = 24 + (i / 5) * 38.0f;
    int cell = item_cell(i + 1);
    draw_round_slot(x, y, 32, i + 1 == equipped ? SLOT_SELECTED : cell >= 0 ? SLOT_CELL : SLOT_EMPTY);
    if (i == tap_flash_slot && flash) disc(x + 16, y + 16, 28, COL(96, 74, 38));
    draw_item_icon(cell, x + 5, y + 5, 21);
  }
  (void)kItemNames;
}

static void draw_gear(RectFS r) {
  (void)r;
  draw_ribbon("GEAR", W / 2.0f, 4);

  int sword = sram8(0x59), shield = sram8(0x5A);
  int gear_cells[7];
  gear_cells[0] = (sword > 0 && sword != 0xFF) ? SS_ICON_SWORD_1 + (sword > 4 ? 3 : sword - 1) : -1;
  gear_cells[1] = (shield > 0 && shield != 0xFF) ? SS_ICON_SHIELD_1 + (shield > 3 ? 2 : shield - 1) : -1;
  gear_cells[2] = SS_ICON_ARMOR_0 + (sram8(0x5B) > 2 ? 2 : sram8(0x5B));
  gear_cells[3] = sram8(0x54) > 0 ? SS_ICON_GLOVES_1 + (sram8(0x54) > 2 ? 1 : sram8(0x54) - 1) : -1;
  gear_cells[4] = sram8(0x55) > 0 ? SS_ICON_BOOTS_1 : -1;
  gear_cells[5] = sram8(0x56) > 0 ? SS_ICON_FLIPPERS_1 : -1;
  gear_cells[6] = sram8(0x57) > 0 ? SS_ICON_MOONPEARL_1 : -1;

  // EQUIPMENT: the gear icons in dark wells on a parchment strip
  ui_text("EQUIPMENT", 16, 25, FONT_SMALL, COL_RIM_GOLD, 1);
  draw_strip((RectFS){14, 33, 220, 27});
  for (int i = 0; i < 7; i++) {
    float x = 23 + i * 30.0f, y = 35;
    fill_round(x, y, 22, 22, 4, COL_RIM_BROWN);
    fill_round(x + 1, y + 1, 20, 20, 3, COL(30, 19, 11));
    draw_gear_icon(gear_cells[i], x + 3, y + 3);
  }

  // BOTTLES
  ui_text("BOTTLES", 16, 67, FONT_SMALL, COL_RIM_GOLD, 1);
  draw_strip((RectFS){14, 75, 118, 33});
  for (int i = 0; i < 4; i++) {
    float x = 19 + i * 28.0f, y = 79;
    fill_round(x, y, 24, 24, 4, COL_RIM_BROWN);
    fill_round(x + 1, y + 1, 22, 22, 3, COL(30, 19, 11));
    int lv = sram8(0x5C + i);
    if (lv > 7) lv = 7;
    if (lv > 0) draw_gear_icon(SS_ICON_BOTTLE_1 + (lv - 1), x + 4, y + 4);
  }

  // PENDANTS
  ui_text("PENDANTS", 146, 67, FONT_SMALL, COL_RIM_GOLD, 1);
  draw_strip((RectFS){144, 75, 90, 33});
  int pend = sram8(0x74);
  static const int pbit[3] = {4, 2, 1};
  static const uint32_t pcol[3] = {COL(64, 200, 88), COL(70, 110, 240), COL(230, 60, 60)};
  for (int i = 0; i < 3; i++) {
    float cxp = 163 + i * 26.0f, cyp = 91;
    bool own = (pend & pbit[i]) != 0;
    disc(cxp, cyp, 20, COL_RIM_BROWN);
    disc(cxp, cyp, 16, own ? pcol[i] : COL(52, 40, 28));
    if (own) disc(cxp - 3, cyp - 3, 4, mix_col(pcol[i], COL(255, 255, 255), 0.6f));
  }

  // HEART PIECES: collected pieces are full hearts, plus "n / 4"
  ui_text("HEART PIECES", 16, 115, FONT_SMALL, COL_RIM_GOLD, 1);
  draw_strip((RectFS){14, 123, 118, 24});
  int pieces = sram8(0x6B) & 3;
  char count[8];
  snprintf(count, sizeof(count), "%d / 4", pieces);
  float hx = floorf(14 + (118 - (60 + 6 + ui_text_width(count, FONT_SMALL, 0))) / 2);
  for (int i = 0; i < 4; i++)
    draw_ui_icon(i < pieces ? SS_UI_HEART : SS_UI_HEART_OFF, hx + i * 16, 129, false);
  ui_text(count, hx + 66, 132, FONT_SMALL, COL(90, 58, 24), 0);

  // CRYSTALS
  ui_text("CRYSTALS", 146, 115, FONT_SMALL, COL_RIM_GOLD, 1);
  draw_strip((RectFS){144, 123, 90, 24});
  int owned7 = sram8(0x7A) & 0x7F, n_owned = 0;
  while (owned7) { n_owned += owned7 & 1; owned7 >>= 1; }
  for (int i = 0; i < 7; i++) {
    float x = 151 + i * 11.0f, y = 128;
    fill_round(x, y, 9, 13, 4, COL_RIM_BROWN);
    if (i < n_owned)
      fill_round_grad(x + 1, y + 1, 7, 11, 3, COL(190, 220, 255), COL(110, 160, 255),
                      COL(70, 110, 220), 0, 0);
    else
      fill_round(x + 1, y + 1, 7, 11, 3, COL(52, 40, 28));
  }
}

// Life, magic and counters on one plaque above the tabs, on every panel.
static const RectFS kStatusBar = {8, 182, 304, 24};
static void draw_status_bar(bool dungeon_mode) {
  RectFS b = kStatusBar;
  draw_plaque(b);
  bool show_keys = dungeon_mode && sram8(0x6F) != 0xFF;
  static const int kBombCap[8]  = {10, 15, 20, 25, 30, 35, 40, 50};
  static const int kArrowCap[8] = {30, 35, 40, 45, 50, 55, 60, 70};
  bool bombs_max = sram8(0x43) >= kBombCap[sram8(0x70) & 7];
  bool arrows_max = sram8(0x77) >= kArrowCap[sram8(0x71) & 7];
  int rupees = sram16(0x62); if (rupees > 9999) rupees = 9999;
  char counts[4][8];
  snprintf(counts[0], sizeof(counts[0]), "%03d", rupees);
  snprintf(counts[1], sizeof(counts[1]), "%02d", sram8(0x43) % 100);
  snprintf(counts[2], sizeof(counts[2]), "%02d", sram8(0x77) % 100);
  snprintf(counts[3], sizeof(counts[3]), "%d", sram8(0x6F) % 10);
  static const int icons[4] = {SS_UI_RUPEE, SS_UI_BOMB, SS_UI_ARROW, SS_UI_KEY};
  const uint32_t white = COL(255, 255, 255), full = COL(255, 220, 90);
  const uint32_t colors[4] = {white, bombs_max ? full : white, arrows_max ? full : white, white};
  int ncount = show_keys ? 4 : 3;

  // Hearts (110) | flask + gauge | counters, 6 px apart; with the key counter
  // the gauge narrows and the gaps close up so everything still fits.
  float gauge_w = show_keys ? 34 : 50;
  float widths[4], used = 110 + 15 + gauge_w;
  for (int i = 0; i < ncount; i++) {
    widths[i] = 15 + ui_text_width(counts[i], FONT_LARGE, -1);
    used += widths[i];
  }
  float x = b.x + 7;
  float gap = floorf((b.w - 14 - used) / ncount);
  if (gap > 6) gap = 6;
  if (gap < 2) gap = 2;

  // hearts: 10 per row on an 11 px pitch, up to two rows
  int cap = sram8(0x6C) >> 3; if (cap > 20) cap = 20;
  int cur = sram8(0x6D);
  int hx = (int)x, hy = (int)b.y + (cap > 10 ? 1 : 6);
#if defined(__3DS__) || defined(ZELDA3_TEST_BOTTOM_HEARTS)
  capture_bottom_hearts(cap, 11, 10, 11, hx, hy);
#endif
  for (int i = 0; i < cap; i++) {
    int g = i < (cur >> 3) ? 2 : (i == (cur >> 3) && (cur & 7) >= 4 ? 1 : 0);
    draw_heart(g, hx + i % 10 * 11, hy + i / 10 * 11);
  }
  x += 110 + gap;

  // magic flask and gauge; "1/2" marks the half-magic upgrade
  draw_ui_icon(SS_UI_FLASK, x, b.y + 6, false);
  float gx = x + 15, gy = b.y + 8;
  fill_round(gx - 2, gy - 2, gauge_w + 4, 13, 4, COL_RIM_DARK);
  fill_round(gx - 1, gy - 1, gauge_w + 2, 11, 4, COL_RIM_BROWN);
  fill_round(gx, gy, gauge_w, 9, 3, COL(14, 10, 6));
  int magic = sram8(0x6E); if (magic > 128) magic = 128;
  float fill = floorf((gauge_w - 2) * magic / 128.0f + 0.5f);
  if (fill > 0)
    fill_round_grad(gx + 1, gy + 1, fill, 7, 2, COL(185, 255, 192), COL(58, 208, 90),
                    COL(21, 122, 44), 0, 0);
  if (sram8(0x7B) >= 1)
    ui_text("1/2", gx + gauge_w - 2 - ui_text_width("1/2", FONT_SMALL, 0), gy + 1,
            FONT_SMALL, white, 0);
  x += 15 + gauge_w + gap;

  // rupees, bombs, arrows and (in dungeons) small keys
  for (int i = 0; i < ncount; i++) {
    draw_ui_icon(icons[i], x, b.y + 6, false);
    ui_text(counts[i], x + 15, b.y + 8, FONT_LARGE, colors[i], -1);
    x += widths[i] + gap;
  }
}

// X and Y items and the B sword in round gold slots on the right. X shows the
// engine's X item (ItemSwitchLR); tapping Y cycles to the next owned item.
static void draw_quick_slots(void) {
  draw_round_slot(260, 26, 40, SLOT_GOLD);
  draw_item_icon(item_cell(SS_GetEquippedSlotX()), 266, 32, 28);
  draw_key_badge(261, 27, "X");
  y_ring_r = (RectFS){255, 75, 50, 50};
  draw_round_slot(260, 80, 40, SLOT_GOLD);
  draw_item_icon(item_cell(SS_GetEquippedSlot()), 266, 86, 28);
  draw_key_badge(261, 81, "Y");
  draw_round_slot(265, 134, 30, SLOT_GOLD);
  int sword = sram8(0x59);
  if (sword > 0 && sword != 0xFF) draw_ui_icon(SS_UI_SWORD, 268, 137, true);
  draw_key_badge(266, 135, "B");
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
  bool saved = false;
  if (f) {
    saved = fwrite(out, 1, olen, f) == olen;
    if (fclose(f) != 0) saved = false;
  }
#ifdef __3DS__
  // The launcher restores this ROM's profile INI over the working copy on
  // every boot. Persist ALL successful menu writes, not only ShowFps.
  if (saved) Platform3DS_PersistRuntimeSettings();
#else
  (void)saved;
#endif
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
  update_mode = update_confirm = update_show_notes = false;
  update_page = 0;
  screen_mode = false;
  developer_mode = false;
  developer_overlay_mode = false;
  load_confirm_mode = false;
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
  draw_tablet(*row, armed, 5);
}

// Stone row: engraved label on the left, value (or nothing) on the right.
static void draw_settings_entry(RectFS *row, const char *label, const char *value, bool armed) {
  draw_settings_row(row, armed);
  uint32_t ink = armed ? COL(42, 26, 8) : COL_INK;
  uint32_t lit = armed ? COL(255, 236, 170) : COL_INK_LIT;
  float ty = floorf(row->y + row->h / 2 - 4);
  ui_text(label, row->x + 10, ty + 1, FONT_LARGE, lit, 1);
  ui_text(label, row->x + 10, ty, FONT_LARGE, ink, 1);
  if (value && value[0])
    ui_text(value, row->x + row->w - 10 - ui_text_width(value, FONT_SMALL, 1), ty + 1,
            FONT_SMALL, armed ? ink : COL(46, 51, 64), 1);
}

// Small stone button with a centred label (BACK, MORE, PREV...).
static void draw_button(RectFS *r, const char *label, bool armed) {
  draw_settings_row(r, armed);
  ui_text_center(label, r->x + r->w / 2, floorf(r->y + r->h / 2 - 4), FONT_LARGE,
                 armed ? COL(42, 26, 8) : COL_INK, armed ? COL(255, 236, 170) : COL_INK_LIT, 1);
}

// Sub-page header: title ribbon and a BACK tablet at the top-left.
static void draw_page_header(const char *title, RectFS *back) {
  draw_ribbon(title, W / 2.0f, 4);
  *back = (RectFS){14, 4, 52, 20};
  draw_button(back, "BACK", false);
}

// Settings pages lay out in the 220 px column left of the right-hand space.
static RectFS settings_row(int i) { return (RectFS){14, 26 + i * 30.0f, 220, 26}; }

static void draw_remap_panel(RectFS r) {
  (void)r;
  draw_page_header("REMAP BUTTONS", &remap_back_r);

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

  remap_page_r = (RectFS){254, 4, 52, 20};
  draw_button(&remap_page_r, remap_first_row == 0 ? "MORE" : "TOP", false);

  float row_h = 22, gap = 3;
  for (int visible = 0; visible < 6; visible++) {
    int i = remap_first_row + visible;
    RectFS *row = &remap_row_r[visible];
    *row = (RectFS){14, 26 + visible * (row_h + gap), 214, row_h};
    bool armed = remap_arm == i;
    const char *v = armed ? "PRESS KEY"
        : (pad_controls[i] >= 0 && pad_controls[i] < 17 ? kPadButtonLabel[pad_controls[i]] : "----");
    draw_settings_entry(row, kPadCmdNames[i], v, armed);
  }
  float bar_x = 232, bar_y = 26, bar_h = 6 * row_h + 5 * gap;
  fill_round(bar_x, bar_y, 3, bar_h, 1, COL_RIM_DARK);
  float thumb_h = bar_h * 0.5f;
  float thumb_y = bar_y + (remap_first_row == 0 ? 0 : bar_h - thumb_h);
  fill_round(bar_x, thumb_y, 3, thumb_h, 1, COL_RIM_GOLD);
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
  (void)r;
  draw_page_header("SCREEN", &screen_back_r);

  const char *edge_value = "FIXED CAMERA";
#ifdef __3DS__
  if (Platform3DS_GetWideEdgeMode() == kPlatform3DSWideEdgeStandard)
    edge_value = "STANDARD";
#endif
  bool hud_hidden = SS_IsHudHidden();
  bool wide = false;
  // 3D DEPTH is the last row, shown only on 3DS models with a 3D screen.
  bool stereo = false;
  const char *stereo_value = "OFF";
#ifdef __3DS__
  wide = Platform3DS_GetDisplayMode() == kPlatform3DSDisplayUltraWideMod;
  static const char *const stereo_labels[4] = {"OFF", "LOW", "MID", "HIGH"};
  stereo = Platform3DS_StereoSupported();
  stereo_value = stereo_labels[Platform3DS_GetStereoLevel()];
#else
  wide = SS_IsWidescreen();
#endif
  static const char *const labels[5] = {
    "DISPLAY MODE", "EDGE MODE", "ZOOM", "TOP HUD", "3D DEPTH",
  };
  const char *values[5] = {
    display_mode_label(), edge_value, wide_zoom_label(),
    hud_hidden ? "OFF" : "ON", stereo_value,
  };
  int rows = (wide ? 4 : 3) + (stereo ? 1 : 0);
  screen_row_r[2] = (RectFS){0};
  for (int visible = 0; visible < rows; visible++) {
    int item = !wide && visible >= 2 ? visible + 1 : visible;
    screen_row_r[item] = settings_row(visible);
    draw_settings_entry(&screen_row_r[item], labels[item], values[item], false);
  }
}

static void draw_developer_panel(RectFS r) {
  (void)r;
  draw_page_header("DEVELOPER", &developer_back_r);

  bool show_fps = false;
#ifdef __3DS__
  show_fps = Platform3DS_GetShowFps();
#else
  show_fps = developer_show_fps;
#endif
  const char *load_value = load_result == -2 ? "WAIT" :
    (load_result >= 0 && SDL_GetTicks() < load_flash_until ?
      DumpState_ResultLabel((ZeldaDumpStateResult)load_result) : "LOAD");
  const char *labels[5] = {
    "MEM DUMP", "LOAD STATE", "SHOW FPS", "OVERLAY", "3D TERRAIN",
  };
  const char *values[5] = {
    SDL_GetTicks() < dump_flash_until ? "DONE" : "WRITE",
    load_value,
    show_fps ? "ON" : "OFF",
    "OPEN",
    "OFF",
  };
  int rows = 4;
#ifdef __3DS__
  // 3D TERRAIN: stereo depth from the game's collision data, for A/B comparison.
  values[4] = Platform3DS_GetStereoTerrain() ? "ON" : "OFF";
  rows = Platform3DS_StereoSupported() ? 5 : 4;
#endif
  developer_row_r[4] = (RectFS){0};
  for (int i = 0; i < rows; i++) {
    developer_row_r[i] = settings_row(i);
    draw_settings_entry(&developer_row_r[i], labels[i], values[i], false);
  }
}

static void draw_load_confirmation(RectFS r) {
  (void)r;
  draw_ribbon("LOAD STATE", W / 2.0f, 4);
  const char *lines[] = {
    "LOAD THE LATEST DUMP",
    "FOR THIS ROM PROFILE",
    "UNSAVED PROGRESS IS LOST",
  };
  for (int i = 0; i < 3; i++)
    ui_text_center(lines[i], 124, 44 + i * 18, FONT_LARGE, COL(255, 255, 255), 0, 0);
  load_cancel_r = (RectFS){14, 146, 105, 26};
  load_confirm_r = (RectFS){129, 146, 105, 26};
  draw_button(&load_cancel_r, "CANCEL", false);
  draw_button(&load_confirm_r, "LOAD", false);
}

static void draw_developer_overlay_panel(RectFS r) {
  (void)r;
  draw_page_header("OVERLAY", &developer_back_r);

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

  float y = 26;
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
    draw_plaque((RectFS){14, y, 220, 17});
    ui_text(rows[i].label, 22, y + 5, FONT_LARGE, COL(170, 170, 170), 0);
    ui_text(rows[i].value, 102, y + 5, FONT_LARGE, rows[i].color, 0);
    y += 19;
  }
}

// Centred in the settings column; long texts drop to the small font.
static void draw_column_text(const char *s, float y, uint32_t color) {
  int font = ui_text_width(s, FONT_LARGE, 0) <= 220 ? FONT_LARGE : FONT_SMALL;
  ui_text_center(s, 124, y, font, color, 0, 0);
}

static void draw_update_panel(RectFS r) {
  (void)r;
  draw_page_header("UPDATE", &update_back_r);
#ifdef __3DS__
  UpdateStatus st; Updater_GetStatus(&st);
  update_channel_r = settings_row(0);
  draw_settings_entry(&update_channel_r, "CHANNEL", st.prerelease ? "PRE-RELEASE" : "STABLE", false);
  update_release_r = settings_row(1);
  draw_button(&update_release_r, st.version[0] ? st.version : "NO RELEASE SELECTED", update_show_notes);
  const char *message = update_confirm ? "DOWNLOAD AND INSTALL" : st.message;
  if (!message[0]) message = "CHECK FOR UPDATE";
  draw_column_text(message, 90, COL(255, 255, 255));
  const char *hint = update_confirm ? "UNSAVED PROGRESS IS LOST" :
    update_show_notes ? "CHANGELOG ON TOP SCREEN" : "TAP THE RELEASE FOR CHANGELOG";
  ui_text_center(hint, 124, 104, FONT_SMALL, COL(205, 180, 124), 0, 1);
  update_prev_r = (RectFS){14, 116, 70, 22};
  update_next_r = (RectFS){164, 116, 70, 22};
  if (update_show_notes && !update_confirm) {
    draw_button(&update_prev_r, "PREV", false);
    draw_button(&update_next_r, "NEXT", false);
  }
  const char *action = update_confirm ? "INSTALL UPDATE" :
    st.state == UPDATE_AVAILABLE ? "DOWNLOAD UPDATE" :
    (st.state == UPDATE_CHECKING || st.state == UPDATE_DOWNLOADING || st.state == UPDATE_VERIFYING) ? "CANCEL" :
    st.state == UPDATE_INSTALLING ? "PLEASE WAIT" : st.state == UPDATE_DONE ? "UPDATE INSTALLED" : "CHECK FOR UPDATE";
  update_action_r = (RectFS){14, 150, 220, 26};
  if (st.state == UPDATE_DOWNLOADING || st.state == UPDATE_INSTALLING) {
    fill_round(14, 142, 220, 5, 2, COL_RIM_DARK);
    fill_rect(15, 143, 218 * st.progress / 100.0f, 3, COL_RIM_GOLD);
  }
  draw_button(&update_action_r, action, update_confirm);
#else
  draw_column_text("3DS UPDATES", 45, COL(255, 255, 255));
#endif
}

bool SecondScreenSDL_UpdateIsOpen(void) { return update_mode; }
bool SecondScreenSDL_UpdateNotesPage(unsigned *page) { *page = update_page; return update_show_notes; }

// Version string on a small plaque at the bottom-right of every settings page.
static void draw_version_plaque(void) {
  char v[32];
  const char *ver = ZELDA3_3DS_VERSION;
  snprintf(v, sizeof(v), "%s%s", ver[0] >= '0' && ver[0] <= '9' ? "V" : "", ver);
  RectFS p = {250, 150, ui_text_width(v, FONT_SMALL, 1) + 12, 16};
  draw_plaque(p);
  ui_text(v, p.x + 6, p.y + 5, FONT_SMALL, COL(205, 180, 124), 1);
}

static void draw_settings(RectFS r) {
  draw_version_plaque();
  if (update_mode) { draw_update_panel(r); return; }
  if (load_confirm_mode) {
    draw_load_confirmation(r);
    return;
  }
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
  draw_ribbon("SETTINGS", W / 2.0f, 4);

  char turbo_value[12];
#ifdef __3DS__
  int turbo_multiplier = Platform3DS_GetTurboMultiplier();
  if (turbo_multiplier <= 0)
    snprintf(turbo_value, sizeof(turbo_value), "OFF");
  else
    snprintf(turbo_value, sizeof(turbo_value), "x%d", turbo_multiplier);
#else
  snprintf(turbo_value, sizeof(turbo_value), "x5");
#endif
  static const char *const labels[5] = {
    "SCREEN", "TURBO SPEED", "DEVELOPER", "UPDATE", "RESTART",
  };
  // "" draws a chevron for a sub-page, NULL nothing
  const char *values[5] = {display_mode_label(), turbo_value, "", "", NULL};
  for (int i = 0; i < 5; i++) {
    RectFS *row = &settings_row_r[i];
    *row = settings_row(i);
    draw_settings_entry(row, labels[i], values[i], false);
    if (values[i] && !values[i][0]) {
      float ax = row->x + row->w - 14, ay = floorf(row->y + row->h / 2);
      for (int d = 0; d < 4; d++) {
        fill_rect(ax + d, ay - 4 + d, 2, 1, COL_INK);
        fill_rect(ax + d, ay + 3 - d, 2, 1, COL_INK);
      }
    }
  }
}

static void draw_cinema_settings_overlay(void) {
  RectFS r = {10 * u, 10 * u, W - 20 * u, H - 20 * u};
  draw_ground((SDL_Rect){0, 0, W, H});
  draw_settings(r);
  if (!screen_mode && !remap_mode && !developer_mode && !update_mode) {
    cinema_back_r = (RectFS){14, 4, 52, 20};
    draw_button(&cinema_back_r, "BACK", false);
  } else {
    cinema_back_r = (RectFS){0, 0, 0, 0};
  }
}

static void draw_tab_button(RectFS r, const char *label, bool active) {
  draw_tablet(r, active, 4);
  if (label)
    ui_text_center(label, r.x + r.w / 2, floorf(r.y + r.h / 2 - 4), FONT_LARGE,
                   active ? COL(42, 26, 8) : COL_INK,
                   active ? COL(255, 236, 170) : COL_INK_LIT, 1);
}

// Dark band of stone tablets at the bottom: GEAR | MAP | ITEMS | cog.
static void draw_tab_bar(float tab_h) {
  float top = H - tab_h;
  fill_rect(0, top, W, 1, COL(0, 0, 0));
  fill_round_grad(0, top + 1, W, tab_h - 1, 0, COL(42, 26, 16), COL(30, 18, 10),
                  COL(22, 13, 7), 0, 0);
  float y = top + 3, bh = tab_h - 6;
  float sq = 30;   // the settings tablet on the right
  tab_settings_r = (RectFS){W - 4 - sq, y, sq, bh};
  // three equal tablets left of the settings cog, 3 px apart
  float x0 = 4, xr = tab_settings_r.x - 3, tgap = 3;
  float bw = (xr - x0 - 2 * tgap) / 3.0f;
  tab_gear_r  = (RectFS){x0, y, bw, bh};
  tab_map_r   = (RectFS){x0 + bw + tgap, y, bw, bh};
  tab_items_r = (RectFS){x0 + 2 * (bw + tgap), y, bw, bh};
  draw_tab_button(tab_gear_r, "GEAR", tab == TAB_GEAR);
  draw_tab_button(tab_map_r, "MAP", tab == TAB_MAP);
  draw_tab_button(tab_items_r, "ITEMS", tab == TAB_ITEMS);
  draw_tab_button(tab_settings_r, NULL, tab == TAB_SETTINGS);
  if (tex_ui_icons) {
    // the 24 px cog, smoothly scaled to fit inside the tablet's edges
    SDL_Rect src = {SS_UI_COG * SS_UI_ICON_CELL, 0, SS_UI_ICON_CELL, SS_UI_ICON_CELL};
    SDL_FRect dst = {tab_settings_r.x + (sq - 18) / 2, y + (bh - 18) / 2, 18, 18};
    SDL_SetTextureScaleMode(tex_ui_icons, SDL_ScaleModeLinear);
    SDL_RenderCopyF(ss_r, tex_ui_icons, &src, &dst);
    SDL_SetTextureScaleMode(tex_ui_icons, SDL_ScaleModeNearest);
  } else {
    draw_cog(tab_settings_r.x + tab_settings_r.w / 2, tab_settings_r.y + tab_settings_r.h / 2,
             bh * 0.28f);
  }
}

// public API

static SDL_Window *main_win;
static bool ss_enabled;
#if defined(__3DS__) || defined(ZELDA3_TEST_3DS_UI)
typedef struct BottomTouchTrace {
  uint32_t timestamp;
  uint16_t x, y;
  uint8_t before_tab, after_tab, update, contact;
} BottomTouchTrace;
static BottomTouchTrace ss_touch_trace[32];
static unsigned ss_touch_trace_count;
#endif
#ifdef __3DS__
static uint8_t *ss_present_pixels[2];
static int ss_front_buffer = -1;
static int ss_worker_buffer;
static bool ss_worker_busy;
static bool ss_frame_ready;
static bool ss_present_retry;
static uint32_t ss_redraw_requests;
static bool ss_worker_sidebar_patch;
static bool ss_worker_map_patch;
static bool ss_worker_running;
static Thread ss_worker_thread;
static LightEvent ss_worker_start;
static LightEvent ss_worker_done;
static int ss_worker_logic_frames;
static s32 ss_worker_idle_priority;
static s32 ss_worker_interactive_priority;
static s32 ss_worker_scene_priority;
static bool ss_worker_door_transition;
static bool ss_worker_interactive;
static uint64_t ss_full_redraw_count;
static uint64_t ss_full_redraw_total_ticks;
static uint64_t ss_full_redraw_max_ticks;
static uint64_t ss_patch_redraw_count;
static uint64_t ss_patch_redraw_total_ticks;
static uint64_t ss_patch_redraw_max_ticks;
static bool ss_touch_redraw_pending;
static bool ss_scene_redraw_pending;
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

static void capture_bottom_hearts(int count, int size, int columns, int step,
                                 int x, int y) {
  if (!ss_is_new_3ds && ss_old_display_pixels)
    BottomHearts_Capture(&ss_worker_hearts[ss_worker_buffer], ss_r,
      tex_heart_scratch, draw_heart, count, size, columns, step, x, y,
      sram8(0x6d), sram8(0x6c), sram8(0x7b) >= 1);
}

static uint32_t bottom_sdl_pixel_format(void) {
  return ss_is_new_3ds ? SDL_PIXELFORMAT_ARGB8888 : SDL_PIXELFORMAT_RGB565;
}

enum {
  kBottomRedrawHud = 1 << 0,
  kBottomRedrawFull = 1 << 1,
  kBottomRedrawMap = 1 << 2,
};

static void request_bottom_redraw(uint32_t request) {
  __atomic_fetch_or(&ss_redraw_requests, request, __ATOMIC_RELEASE);
}

// Automatic scene work shares the main thread's priority. Only real touch
// requests may preempt gameplay; a full map redraw can otherwise block an iris.
static void prioritize_bottom_scene(void) {
  if (!ss_is_new_3ds && ss_worker_thread)
    svcSetThreadPriority(threadGetHandle(ss_worker_thread), ss_worker_scene_priority);
}

static void prioritize_bottom_touch(void) {
  if (!ss_is_new_3ds && ss_worker_thread)
    svcSetThreadPriority(threadGetHandle(ss_worker_thread),
                         ss_worker_interactive_priority);
}
// Automatic HUD work must not outrank gameplay. Only touch may preempt it.
// Old health-only updates bypass the worker through the retained heart layer.
static s32 bottom_worker_priority(void) {
  if (ss_worker_touch_request_ticks)
    return ss_worker_interactive_priority;
  if (__atomic_load_n(&ss_worker_door_transition, __ATOMIC_ACQUIRE))
    return ss_worker_idle_priority;
  if (ss_worker_interactive || ss_worker_sidebar_patch ||
      (__atomic_load_n(&ss_redraw_requests, __ATOMIC_ACQUIRE) & kBottomRedrawHud))
    return ss_worker_scene_priority;
  return ss_worker_idle_priority;
}

static void prioritize_bottom_hud(void) {
  if (!ss_is_new_3ds && ss_worker_thread && ss_worker_busy &&
      !ss_touch_redraw_pending && !ss_worker_touch_request_ticks)
    svcSetThreadPriority(threadGetHandle(ss_worker_thread), bottom_worker_priority());
}

#endif

static void request_dump_now(void) {
#ifdef __3DS__
  // Directory creation and all capture I/O happen together on the game
  // thread while NDSP is paused. An empty path asks that handler to create a
  // unique session directory.
  SS_RequestMemoryDump(NULL);
#else
  SS_RequestMemoryDump("");
#endif
  dump_flash_until = SDL_GetTicks() + 1200;
}

bool SecondScreenSDL_Init(SDL_Window *main_window) {
#ifdef __3DS__
  main_win = main_window;
  ss_is_new_3ds = Platform3DS_IsNew3DS();
  if (!NativeTouch_Init()) {
    Platform3DS_LogRuntime("ERROR registering native touch event: %s", SDL_GetError());
    return false;
  }
  ss_enabled = true;
  return true;
#else
  const char *env = SDL_getenv("ZELDA3_SECOND_SCREEN");
  if (!env || env[0] != '1') return false;
  main_win = main_window;
  ss_is_new_3ds = true; // Preserve the desktop pulse animation.
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
#ifdef __3DS__
  bool changed = current_fps != ss_diag_current_fps ||
                 average_fps != ss_diag_average_fps;
#endif
  ss_diag_current_fps = current_fps;
  ss_diag_average_fps = average_fps;
#ifdef __3DS__
  Platform3DS_SetCurrentFps(current_fps);
  if (!ss_is_new_3ds && developer_overlay_mode && changed) {
    uint32_t now_ms = SDL_GetTicks();
    if (ss_diag_last_redraw_ms == 0 ||
        now_ms - ss_diag_last_redraw_ms >= 400) {
      ss_diag_last_redraw_ms = now_ms;
      request_bottom_redraw(kBottomRedrawFull);
    }
  }
#endif
}

void SecondScreenSDL_OpenDeveloperOverlay(void) {
  tab = TAB_SETTINGS;
  leave_remap();
  update_mode = update_confirm = update_show_notes = false;
  update_page = 0;
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
  // ponytail: the bottom UI is drawn for the 3DS's 320x240; the desktop shows
  // that same layout scaled by SDL (mouse/finger events arrive in its units).
  W = 320;
  H = 240;
#ifndef __3DS__
  SDL_RenderSetLogicalSize(ss_r, W, H);
#endif
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
#ifdef __3DS__
  if (!ss_is_new_3ds) {
    ss_old_display_pixels = linearMemAlign(k3DSBottomTextureWidth * k3DSBottomTextureHeight * 2, 64);
    if (!ss_old_display_pixels)
      Platform3DS_LogRuntime("Bottom hearts: retained buffer unavailable; using worker fallback");
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
  int portal_x, portal_y;
} BottomCriticalState;

static void request_bottom_redraw_on_state_change(void) {
  static bool initialized;
  static BottomCriticalState previous;
  BottomCriticalState current;
  uint8_t local_sram[0x80];

  memset(&current, 0, sizeof(current));
  SS_ReadSram(local_sram, sizeof(local_sram));
  current.module = SS_GetModule() & 0xff;
  // Keep the last map during door open/close; compare against that stable
  // state after the iris, instead of rebuilding for each temporary module.
  if (!ss_is_new_3ds && (current.module == 15 || current.module == 16))
    return;
  current.area = SS_GetArea();
  current.dungeon = SS_GetDungeon();
  current.indoors = SS_IsIndoors() ? 1 : 0;
  current.equipped = SS_GetEquippedSlot();
  int portal[2];
  if (SS_GetMirrorPortal(portal)) {
    current.portal_x = portal[0]; current.portal_y = portal[1];
  }
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
     current.portal_x != previous.portal_x || current.portal_y != previous.portal_y ||
     (!ss_is_new_3ds &&
      current.inventory_hash != previous.inventory_hash));
  if (!ss_is_new_3ds && initialized && tab == TAB_MAP &&
      (current.module != previous.module || current.area != previous.area ||
       current.dungeon != previous.dungeon || current.indoors != previous.indoors)) {
    // Refresh the destination map without preempting the top renderer.
    ss_scene_redraw_pending = true;
    ss_worker_interactive = true;
    if (!ss_touch_redraw_pending && !ss_worker_touch_request_ticks)
      prioritize_bottom_scene();
  }
  bool hud_changed = initialized &&
    (current.health_cap != previous.health_cap ||
     ((ss_is_new_3ds || !ss_old_display_pixels) &&
      current.health_cur != previous.health_cur) ||
     current.magic != previous.magic ||
     current.keys != previous.keys ||
     current.bombs != previous.bombs ||
     current.arrows != previous.arrows ||
     current.rupees != previous.rupees);
  // Movement must invalidate the Old map; the idle fallback is intentionally
  // slow. Coalesce movement while a worker is busy, at most ten requests/sec.
  static int map_x, map_y;
  static uint32_t map_request_ms;
  int x = SS_GetLinkX(), y = SS_GetLinkY();
  uint32_t now = SDL_GetTicks();
  if (!ss_is_new_3ds && tab == TAB_MAP &&
      mode_for_module(current.module) == MODE_GAME &&
      (x / 4 != map_x / 4 || y / 4 != map_y / 4) && now - map_request_ms >= 100) {
    map_x = x; map_y = y; map_request_ms = now;
    request_bottom_redraw(kBottomRedrawMap);
  }
  previous = current;
  initialized = true;
  if (full_changed || (ss_is_new_3ds && hud_changed))
    request_bottom_redraw(kBottomRedrawFull);
  if (!ss_is_new_3ds && hud_changed) {
    request_bottom_redraw(kBottomRedrawHud);
    prioritize_bottom_hud();
  }
}

static bool can_patch_bottom_sidebar(void) {
  int module = SS_GetModule() & 0xff;
  return !ss_is_new_3ds && art_ready && ss_front_buffer >= 0 &&
         (tab == TAB_MAP || tab == TAB_ITEMS) &&
         mode_for_module(module) == MODE_GAME;
}

static bool can_patch_bottom_map(void) {
  return !ss_is_new_3ds && art_ready && ss_front_buffer >= 0 && tab == TAB_MAP &&
    !SS_IsIndoors() && SS_GetArea() < 0x80 &&
    ((SS_GetModule() & 0xff) == 9 || (SS_GetModule() & 0xff) == 11);
}

static void draw_bottom_map_patch(void) {
  SS_ReadSram(sram, sizeof(sram));
  SDL_Rect clip = {(int)floorf(map_area_r.x), (int)floorf(map_area_r.y),
    (int)ceilf(map_area_r.x + map_area_r.w) - (int)floorf(map_area_r.x),
    (int)ceilf(map_area_r.y + map_area_r.h) - (int)floorf(map_area_r.y)};
  draw_ground(clip);  // wood behind the parchment's rounded corners
  SDL_RenderSetClipRect(ss_r, &clip);
  draw_overworld(map_area_r, SS_GetLinkX(), SS_GetLinkY(), SS_GetArea());
  SDL_RenderSetClipRect(ss_r, NULL);
  uint8_t *destination = ss_present_pixels[ss_worker_buffer] +
    clip.y * bottom_buffer_pitch() + clip.x * bottom_bytes_per_pixel();
  SDL_RenderReadPixels(ss_r, &clip, bottom_sdl_pixel_format(), destination, bottom_buffer_pitch());
}

static bool bottom_needs_periodic_redraw(void) {
  if (ss_is_new_3ds)
    return true;
  // Diagnostic values explicitly invalidate the overlay when their 500 ms
  // sample changes. Redrawing this software-rendered panel continuously made
  // the Old 3DS bottom worker consume Core 0 for seconds at a time.
  if (developer_overlay_mode)
    return false;
  if (mode_for_module(SS_GetModule() & 0xff) != MODE_GAME)
    return tab == TAB_SETTINGS; // Keep settings timers alive; the Old cinema card is static.
  return tab == TAB_MAP || tab == TAB_ITEMS;
}

// HUD patch: redraw only the status bar (and its 1 px drop shadow).
static void draw_bottom_sidebar_patch(void) {
  SS_ReadSram(sram, sizeof(sram));
  bool indoors = SS_IsIndoors();
  int dungeon_info = SS_GetDungeon();
  bool dungeon_mode = indoors && (dungeon_info & 0xff) != 0xff;
  SDL_Rect clip = {
    (int)kStatusBar.x, (int)kStatusBar.y, (int)kStatusBar.w, (int)kStatusBar.h + 1,
  };
  draw_ground(clip);
  SDL_RenderSetClipRect(ss_r, &clip);
  draw_status_bar(dungeon_mode);
  SDL_RenderSetClipRect(ss_r, NULL);

  uint8_t *destination = ss_present_pixels[ss_worker_buffer] +
    clip.y * bottom_buffer_pitch() + clip.x * bottom_bytes_per_pixel();
  SDL_RenderReadPixels(ss_r, &clip, bottom_sdl_pixel_format(),
                       destination, bottom_buffer_pitch());
}
#endif

// Split the entire visible tab strip at gap midpoints. Borders, rounded
// corners and spacing are valid touch targets, not dead pixels.
static int tab_at_position(float x, float y) {
  if (tab_gear_r.w <= 0 || tab_settings_r.w <= 0 || x < 0 || x >= W ||
      y < tab_gear_r.y - 6 * u || y >= H)
    return -1;
  if (x < (tab_gear_r.x + tab_gear_r.w + tab_map_r.x) * 0.5f) return TAB_GEAR;
  if (x < (tab_map_r.x + tab_map_r.w + tab_items_r.x) * 0.5f) return TAB_MAP;
  if (x < (tab_items_r.x + tab_items_r.w + tab_settings_r.x) * 0.5f) return TAB_ITEMS;
  return TAB_SETTINGS;
}

static void apply_tap(float x, float y) {
  int module = SS_GetModule() & 0xFF;
  int ui_mode = mode_for_module(module);
#ifdef __3DS__
  if (update_mode && Updater_Busy()) goto settings_tap;
#endif
  if (ui_mode != MODE_GAME) {
    if (tab != TAB_SETTINGS) {
      tab = TAB_SETTINGS;
      leave_settings_submenu();
      return;
    }
    if (!screen_mode && !remap_mode && !developer_mode && !update_mode &&
        in_rect(&cinema_back_r, x, y)) {
      tab = TAB_MAP;
      leave_settings_submenu();
      return;
    }
    goto settings_tap;
  }
  if (!art_ready) return;

  int requested_tab = tab_at_position(x, y);
  if (requested_tab >= 0) {
    // Content tabs select their view; contact bounce/repeated taps must not
    // toggle Items or Gear back to Map. The settings cog retains its back action.
    tab = requested_tab == TAB_SETTINGS && tab == TAB_SETTINGS ? TAB_MAP : requested_tab;
    leave_settings_submenu();
    return;
  }

settings_tap:
  if (tab == TAB_SETTINGS) {
    if (update_mode) {
#ifdef __3DS__
      UpdateStatus st; Updater_GetStatus(&st);
      if (st.state == UPDATE_INSTALLING || st.state == UPDATE_DONE) return;
      if (in_rect(&update_back_r, x, y)) {
        if (update_confirm) update_confirm = false;
        else { Updater_Cancel(); update_mode = false; }
      } else if (in_rect(&update_channel_r, x, y) && !Updater_Busy()) {
        update_confirm = update_show_notes = false; update_page = 0;
        Updater_SetChannel(!st.prerelease);
      } else if (in_rect(&update_release_r, x, y) && st.version[0] && !Updater_Busy()) {
        update_show_notes = true; update_page = 0;
      } else if (update_show_notes && !update_confirm && in_rect(&update_prev_r, x, y)) {
        if (update_page) update_page--;
      } else if (update_show_notes && !update_confirm && in_rect(&update_next_r, x, y)) {
        if (update_page + 1 < Platform3DS_UpdateNotesPages()) update_page++;
      } else if (in_rect(&update_action_r, x, y)) {
        if (update_confirm) { update_confirm = false; Updater_Download(); }
        else if (Updater_Busy()) Updater_Cancel();
        else if (st.state == UPDATE_AVAILABLE) update_confirm = true;
        else Updater_Check();
      }
#else
      if (in_rect(&update_back_r, x, y)) update_mode = false;
#endif
      return;
    }
    if (load_confirm_mode) {
      if (in_rect(&load_cancel_r, x, y)) {
        load_confirm_mode = false;
      } else if (in_rect(&load_confirm_r, x, y)) {
        load_confirm_mode = false;
        load_result = -2;
        SS_RequestLoadLatestDumpState();
      }
      return;
    } else if (remap_mode) {
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
#ifdef __3DS__
      } else if (in_rect(&screen_row_r[4], x, y)) {
        int level = (Platform3DS_GetStereoLevel() + 1) % 4;
        Platform3DS_SetStereoLevel(level);
        update_ini("[General]", "Stereo3D",
                   level == 0 ? "Off" : level == 1 ? "1" : level == 2 ? "2" : "3");
#endif
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
        load_confirm_mode = true;
      } else if (in_rect(&developer_row_r[2], x, y)) {
#ifdef __3DS__
        bool show = !Platform3DS_GetShowFps();
        Platform3DS_SetShowFps(show);
#else
        bool show = !developer_show_fps;
        developer_show_fps = show;
#endif
        update_ini("[General]", "ShowFps", show ? "1" : "0");
      } else if (in_rect(&developer_row_r[3], x, y)) {
        developer_overlay_mode = true;
#ifdef __3DS__
        request_bottom_redraw(kBottomRedrawFull);
#endif
#ifdef __3DS__
      } else if (in_rect(&developer_row_r[4], x, y)) {
        bool on = !Platform3DS_GetStereoTerrain();
        Platform3DS_SetStereoTerrain(on);
        update_ini("[General]", "Stereo3DTerrain", on ? "1" : "0");
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
        developer_mode = true;
      } else if (in_rect(&settings_row_r[4], x, y)) {
#ifdef __3DS__
        Platform3DS_RequestRomSelection();
#else
        SS_RequestRestart();
#endif
      } else if (in_rect(&settings_row_r[3], x, y)) {
        update_mode = true;
        update_confirm = update_show_notes = false;
        update_page = 0;
#ifdef __3DS__
        UpdateStatus st; Updater_GetStatus(&st);
        if (st.state != UPDATE_AVAILABLE) Updater_Check();
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
    int row = (int)((y - grid_y) / grid_row_h);
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

static void handle_tap(float x, float y) {
  if (mode_for_module(SS_GetModule() & 0xff) == MODE_GAME &&
      tab != TAB_SETTINGS && tab_at_position(x, y) == tab)
    return; // Already selected: no additional software redraw or upload.
  // Commit navigation before waking a worker that can preempt this thread.
  // Publishing first could draw the old tab and consume the only redraw.
  apply_tap(x, y);
#ifdef __3DS__
  if (!ss_is_new_3ds) {
    ss_touch_request_ticks = svcGetSystemTick();
    ss_touch_redraw_pending = true;
    ss_worker_interactive = true;
  }
  request_bottom_redraw(kBottomRedrawFull);
  prioritize_bottom_touch();
#endif
}

bool SecondScreenSDL_HandleEvent(const SDL_Event *e) {
  if (!ss_win) return false;
#ifdef __3DS__
  float x, y;
  if (NativeTouch_Decode(e, &x, &y)) {
    unsigned before = tab;
    handle_tap(x - (320.0f - W) * 0.5f, y - (240.0f - H) * 0.5f);
    ss_touch_trace[ss_touch_trace_count++ % 32] = (BottomTouchTrace){
      e->common.timestamp, (uint16_t)x, (uint16_t)y, before, tab,
      update_mode, (hidKeysHeld() & KEY_TOUCH) != 0
    };
    return true;
  }
#endif
  switch (e->type) {
  case SDL_FINGERDOWN:
#ifdef __3DS__
    // Native points arrive through the protected event above. Never reuse
    // renderer-transformed FINGER coordinates or activate a touch twice.
    return e->tfinger.touchId == 0;
#else
    if (e->tfinger.windowID == ss_winid) { handle_tap(e->tfinger.x * W, e->tfinger.y * H); return true; }
    return false;
#endif
  case SDL_FINGERUP: case SDL_FINGERMOTION:
#ifdef __3DS__
    return true;
#else
    return e->tfinger.windowID == ss_winid;
#endif
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

static void draw_second_screen(int logic_frames) {
  if (!ss_enabled) return;
#ifdef __3DS__
  // A cinema/settings/gear frame must never inherit a previous heart layout.
  if (!ss_is_new_3ds) ss_worker_hearts[ss_worker_buffer].valid = false;
#endif
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
#ifdef __3DS__
    UpdateStatus update_status; Updater_GetStatus(&update_status);
    if (update_status.state == UPDATE_AVAILABLE && tab != TAB_SETTINGS) {
      draw_plaque((RectFS){18 * u, H - 52 * u, W - 36 * u, 36 * u});
      ui_text_center("UPDATE AVAILABLE - OPEN SETTINGS", W / 2.0f, H - 52 * u + 5,
                     FONT_LARGE, COL(255, 255, 255), 0, 0);
    }
#endif
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

  // wood ground; content on the left, X/Y/B slots on the right, then the
  // status bar and the tab tablets along the bottom
  draw_ground((SDL_Rect){0, 0, W, H});
  float tab_h = 30;
  map_area_r = (RectFS){8, 8, 232, 170};

  if (tab == TAB_ITEMS)      draw_items(map_area_r);
  else if (tab == TAB_GEAR)  draw_gear(map_area_r);
  else if (tab == TAB_SETTINGS) draw_settings(map_area_r);
  else if (dungeon_mode)     draw_dungeon(map_area_r, link_x, link_y, area & 0xFF, dungeon_info);
  else                       draw_overworld(map_area_r, link_x, link_y, area);

  if (tab != TAB_SETTINGS)
    draw_quick_slots();
  draw_status_bar(dungeon_mode);
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
      svcSetThreadPriority(CUR_THREAD_HANDLE, bottom_worker_priority());
    }
    uint64_t start = !ss_is_new_3ds ? svcGetSystemTick() : 0;
    if (ss_worker_map_patch) {
      draw_bottom_map_patch();
      if (ss_worker_sidebar_patch) draw_bottom_sidebar_patch();
    } else if (ss_worker_sidebar_patch)
      draw_bottom_sidebar_patch();
    else
      draw_second_screen(ss_worker_logic_frames);
    if (!ss_is_new_3ds) {
      uint64_t elapsed = svcGetSystemTick() - start;
      if (ss_worker_sidebar_patch || ss_worker_map_patch) {
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
  ss_worker_scene_priority = main_priority;
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
  static unsigned update_revision;
  static uint32_t update_last_redraw;
  UpdateStatus update_status; Updater_GetStatus(&update_status);
  if (update_revision != update_status.revision && SDL_GetTicks() - update_last_redraw >= 150) {
    update_revision = update_status.revision;
    update_last_redraw = SDL_GetTicks();
    if (update_mode || mode_for_module(SS_GetModule() & 0xff) != MODE_GAME)
      request_bottom_redraw(kBottomRedrawFull);
  }
  int completed_load = SS_TakeLoadDumpStateResult();
  if (completed_load >= 0) {
    load_result = completed_load;
    load_flash_until = SDL_GetTicks() + 2400;
    request_bottom_redraw(kBottomRedrawFull);
  }
  if (!ss_win) {
    if (frame_no < 3 || !ensure_window())
      return;
  }

  // Produce the initial Old frame before relying on the low-priority worker.
  // Retry synchronously if game art was not available yet. No gameplay-frame
  // wait is introduced after this first usable bottom image.
  if (!ss_is_new_3ds && !ss_worker_thread &&
      (ss_front_buffer < 0 || !art_ready)) {
    __atomic_exchange_n(&ss_redraw_requests, 0, __ATOMIC_ACQ_REL);
    ss_worker_buffer = 0;
    draw_second_screen(logic_frames);
    ss_front_buffer = ss_worker_buffer;
    ss_frame_ready = true;
    return;
  }

  // A queued map request, or a job already drawing, can outlive the scene
  // invalidation guard. Keep automatic work below the iris until it completes.
  int module = SS_GetModule() & 0xff;
  bool door_transition = !ss_is_new_3ds && (module == 15 || module == 16);
  bool was_door = __atomic_exchange_n(&ss_worker_door_transition, door_transition, __ATOMIC_ACQ_REL);
  if (door_transition != was_door && !ss_is_new_3ds && ss_worker_busy &&
      !ss_touch_redraw_pending && !ss_worker_touch_request_ticks)
    svcSetThreadPriority(threadGetHandle(ss_worker_thread), bottom_worker_priority());
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

  uint32_t requests =
    __atomic_load_n(&ss_redraw_requests, __ATOMIC_ACQUIRE);
  bool periodic_redraw = ss_front_buffer < 0 ||
    (bottom_needs_periodic_redraw() && frame_no % divisor == 0);
  bool can_start_worker = (ss_is_new_3ds || !ss_frame_ready) &&
    (!door_transition || ss_touch_redraw_pending || ss_front_buffer < 0);
  if (!ss_worker_busy && can_start_worker &&
      (requests != 0 || periodic_redraw)) {
    requests = __atomic_exchange_n(&ss_redraw_requests, 0,
                                   __ATOMIC_ACQ_REL);
    // Serve health/magic/counters first using only the small existing sidebar.
    // Leave map/full requests queued; a simultaneous scene change must not
    // swallow this HUD update. Explicit touch navigation keeps precedence.
    if (!ss_is_new_3ds && (requests & kBottomRedrawHud) &&
        !ss_touch_redraw_pending && can_patch_bottom_sidebar()) {
      uint32_t deferred = requests & ~kBottomRedrawHud;
      if (periodic_redraw) deferred |= kBottomRedrawFull;
      if (deferred) request_bottom_redraw(deferred);
      requests = kBottomRedrawHud;
      periodic_redraw = false;
    }
    bool patch_only = !periodic_redraw && (requests & kBottomRedrawFull) == 0 &&
      (!(requests & kBottomRedrawMap) || can_patch_bottom_map()) &&
      (!(requests & kBottomRedrawHud) || can_patch_bottom_sidebar());
    ss_worker_map_patch = patch_only && (requests & kBottomRedrawMap);
    ss_worker_sidebar_patch = patch_only && (requests & kBottomRedrawHud);
    ss_worker_buffer = (ss_worker_sidebar_patch || ss_worker_map_patch) ? ss_front_buffer :
      (ss_front_buffer < 0 ? 0 : 1 - ss_front_buffer);
    ss_worker_touch_request_ticks = 0;
    ss_worker_interactive = false;
    if (!ss_worker_sidebar_patch && !ss_is_new_3ds &&
        (ss_touch_redraw_pending || ss_scene_redraw_pending)) {
      ss_worker_touch_request_ticks = ss_touch_redraw_pending ? ss_touch_request_ticks : 0;
      ss_worker_interactive = true;
      if (ss_touch_redraw_pending) prioritize_bottom_touch();
      else prioritize_bottom_scene();
      ss_touch_redraw_pending = ss_scene_redraw_pending = false;
    }
    ss_worker_logic_frames = logic_frames;
    ss_worker_busy = true;
    LightEvent_Signal(&ss_worker_start);
  }
}

void SecondScreenSDL_Update(int logic_frames) {
  (void)logic_frames;
  // The worker often finishes while the top PPU renders. Harvest its ready
  // event here as well as at BeginFrame, saving a frame without waiting or
  // scheduling another redraw. A busy worker remains fully asynchronous.
  if (ss_worker_busy && LightEvent_TryWait(&ss_worker_done)) {
    ss_front_buffer = ss_worker_buffer;
    ss_worker_busy = false;
    ss_frame_ready = true;
  }
  if (ss_front_buffer < 0) return;
  if (!ss_is_new_3ds && ss_old_display_pixels) {
    bool changed = ss_frame_ready || ss_present_retry;
    if (ss_frame_ready) {
      // BeginFrame has consumed worker_done; no new Old job may start until
      // this frame is presented. Copy into main-owned storage once per UI job.
      memcpy(ss_old_display_pixels, ss_present_pixels[ss_front_buffer],
             k3DSBottomTextureWidth * k3DSBottomTextureHeight * 2);
      ss_display_hearts = ss_worker_hearts[ss_front_buffer];
      ss_old_display_valid = true;
    }
    uint8_t live[0x80];
    SS_ReadSram(live, sizeof(live));
    // Every game panel shows the status bar; a frame without it (cinema)
    // never captured hearts, so valid alone selects the frames to patch.
    if (ss_old_display_valid && ss_display_hearts.valid &&
        mode_for_module(SS_GetModule() & 0xff) == MODE_GAME &&
        ss_display_hearts.capacity == live[0x6c] &&
        ss_display_hearts.half_magic == (live[0x7b] >= 1))
      changed |= BottomHearts_Apply(&ss_display_hearts, ss_old_display_pixels,
                                   bottom_buffer_pitch(), live[0x6d]) != 0;
    if (changed && !Platform3DS_PresentBottomFrame(ss_old_display_pixels, bottom_buffer_pitch(), W, H)) {
      // A skipped top GPU frame must not consume a bottom redraw. Preserve
      // the main-owned image, including live hearts, for the next submission.
      ss_present_retry = true;
      ss_frame_ready = false;
      return;
    }
  } else if (ss_frame_ready) {
    if (!Platform3DS_PresentBottomFrame(ss_present_pixels[ss_front_buffer],
                                       bottom_buffer_pitch(), W, H))
      return;
  }
  ss_frame_ready = ss_present_retry = false;
}

// Called on the main thread during the paused dump transaction. Do not read
// worker-owned geometry or pixels while a redraw is in flight.
bool SecondScreenSDL_WriteDiagnostics(const char *directory) {
  char path[256];
  snprintf(path, sizeof(path), "%s/bottom-ui.txt", directory);
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "Bottom UI schema: 1\nsize=%dx%d scale=%.6f format=%s pitch=%d\n",
          W, H, u, ss_is_new_3ds ? "ARGB8888" : "RGB565", bottom_buffer_pitch());
  fprintf(f, "map_patch=%d scene_redraw_pending=%d redraw_requests=0x%lx\n",
          ss_worker_map_patch, ss_scene_redraw_pending,
          (unsigned long)__atomic_load_n(&ss_redraw_requests, __ATOMIC_ACQUIRE));
  fprintf(f, "enabled=%d tab=%d settings_remap=%d screen=%d developer=%d overlay=%d load_confirm=%d\n",
          ss_enabled, tab, remap_mode, screen_mode, developer_mode,
          developer_overlay_mode, load_confirm_mode);
  fprintf(f, "worker_busy=%d sidebar_patch=%d front=%d worker_buffer=%d frame_ready=%d pending_redraw=0x%lx\n",
          ss_worker_busy, ss_worker_sidebar_patch, ss_front_buffer, ss_worker_buffer,
          ss_frame_ready, (unsigned long)__atomic_load_n(&ss_redraw_requests, __ATOMIC_ACQUIRE));
  fprintf(f, "worker_priority idle/interactive=0x%lx/0x%lx interactive=%d\n",
          (unsigned long)ss_worker_idle_priority, (unsigned long)ss_worker_interactive_priority,
          ss_worker_interactive);
  const uint8_t *capture_pixels = !ss_is_new_3ds && ss_old_display_valid ?
    ss_old_display_pixels : (!ss_worker_busy && ss_front_buffer >= 0 ? ss_present_pixels[ss_front_buffer] : NULL);
  bool capture = capture_pixels != NULL;
  fprintf(f, "retained_hearts=%d heart_layer_valid=%d displayed_health=%d\n",
          ss_old_display_valid, ss_display_hearts.valid, ss_display_hearts.health);
  fprintf(f, "Source pixel capture: %s\n", capture ? "bottom-ui.raw; linear little-endian, 512x256 including padding" : "omitted (no stable front buffer)");
  if (!ss_worker_busy) {
    fprintf(f, "grid_x/y/cell=%.6f,%.6f,%.6f room=%d floor=%d palace=%d\n",
            grid_x, grid_y, grid_cell, cur_room, cur_floor_now, cur_palace);
    const RectFS rects[] = {map_area_r, tab_items_r, tab_gear_r, tab_map_r, tab_settings_r, y_ring_r};
    const char *names[] = {"map", "items_tab", "gear_tab", "map_tab", "settings_tab", "equipped_ring"};
    for (unsigned i = 0; i < sizeof(rects) / sizeof(rects[0]); i++)
      fprintf(f, "%s=%.6f,%.6f,%.6f,%.6f\n", names[i], rects[i].x, rects[i].y, rects[i].w, rects[i].h);
  }
  fprintf(f, "Touch input: KEY_TOUCH edges; protected physical coordinates\n");
  unsigned first = ss_touch_trace_count > 32 ? ss_touch_trace_count - 32 : 0;
  for (unsigned i = first; i < ss_touch_trace_count; ++i) {
    const BottomTouchTrace *t = &ss_touch_trace[i % 32];
    fprintf(f, "touch=%u ms=%lu point=%u,%u contact=%u tab=%u->%u update=%u\n",
            i, (unsigned long)t->timestamp, t->x, t->y, t->contact,
            t->before_tab, t->after_tab, t->update);
  }
  bool ok = !ferror(f);
  ok = fclose(f) == 0 && ok;
  if (capture) {
    snprintf(path, sizeof(path), "%s/bottom-ui.raw", directory);
    f = fopen(path, "wb");
    if (!f) return false;
    size_t size = (size_t)bottom_buffer_pitch() * k3DSBottomTextureHeight;
    ok = fwrite(capture_pixels, 1, size, f) == size && ok;
    ok = fclose(f) == 0 && ok;
  }
  return ok;
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
                           &tex_letters, &tex_face, &tex_mapicons,
                           &tex_bg_parch, &tex_bg_wood, &tex_ui_icons,
                           &tex_heart_scratch, &ui_font[FONT_LARGE].tex,
                           &ui_font[FONT_SMALL].tex, &tex_triforce};
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
  SDL_RenderSetLogicalSize(ss_r, W, H);  // same 320x240 layout, rescaled
  printf("second screen resized: %dx%d (u=%.2f)\n", w2, h2, u);
  fflush(stdout);
}

void SecondScreenSDL_Shutdown(void) {
#ifdef __3DS__
  NativeTouch_Shutdown();
  ss_touch_trace_count = 0;
#endif
  ss_enabled = false;
  leave_settings_submenu();
  tab = TAB_MAP;
#ifdef __3DS__
  if (ss_worker_thread) {
    ss_worker_running = false;
    LightEvent_Signal(&ss_worker_start);
    Result join_result = threadJoin(ss_worker_thread, UINT64_MAX);
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
#ifdef __3DS__
  linearFree(ss_old_display_pixels);
  ss_old_display_pixels = NULL;
  ss_old_display_valid = false;
  memset(ss_worker_hearts, 0, sizeof(ss_worker_hearts));
  memset(&ss_display_hearts, 0, sizeof(ss_display_hearts));
#endif
  // A ROM restart reuses these statics. No job or completed frame from the
  // old renderer may survive after its events/buffers have been destroyed.
#ifdef __3DS__
  ss_worker_busy = ss_frame_ready = false;
  ss_front_buffer = -1;
  ss_present_retry = false;
  ss_worker_buffer = 0;
  ss_redraw_requests = kBottomRedrawFull;
  ss_worker_sidebar_patch = ss_worker_map_patch = ss_worker_interactive = false;
  ss_touch_redraw_pending = ss_scene_redraw_pending = false;
  ss_worker_touch_request_ticks = ss_touch_request_ticks = 0;
  ss_full_redraw_count = ss_full_redraw_total_ticks = ss_full_redraw_max_ticks = 0;
  ss_patch_redraw_count = ss_patch_redraw_total_ticks = ss_patch_redraw_max_ticks = 0;
  ss_touch_redraw_count = ss_touch_redraw_total_ticks = ss_touch_redraw_max_ticks = 0;
#endif
  SS_ResetRomCaches();
  has_last_outdoor = hud_pref_applied = ss_needs_rebuild = false;
  view_floor_offset = 0;
  view_floor_touched_at = 0;
  destroy_textures();
  if (!ss_win) return;
  SDL_DestroyRenderer(ss_r); ss_r = NULL;
  SDL_DestroyWindow(ss_win); ss_win = NULL;
}
