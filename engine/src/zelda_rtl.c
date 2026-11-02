#include "zelda_rtl.h"
#include "variables.h"
#include "misc.h"
#include "load_gfx.h"
#include "nmi.h"
#include "poly.h"
#include "attract.h"
#include "snes/ppu.h"
#include "snes/ppu_ui.h"
#include "snes/snes_regs.h"
#include "snes/dma.h"
#include "spc_player.h"
#include "rando/rando.h"
#include "rando/goal_runtime.h"
#include "rando/rando_save.h"
#include "rando/rando_placement.h"
#include "rando/rando_snapshot_tail.h"
#include "hud.h"
#include "sprite.h"
#include "load_gfx.h"
#include "snes/dsp.h"
#include "util.h"
#include "dump_state.h"
#include <stddef.h>
#include <SDL.h>
#include "audio.h"
#include "assets.h"
#include "localized.h"
#include "overworld.h"
#include "android_logging.h"
#include "test_scenarios.h"
#include "messaging.h"
#include "wide_camera.h"
#include "dungeon.h"
#include "config.h"
#include "quality_of_life.h"
#include "language_cart.h"
#ifdef __3DS__
#include <3ds.h>
#include "platform_3ds.h"
#include "sram_store.h"
#include "native_menu.h"
#include "native_file_labels.h"
#include "ppu_gpu.h"
#endif
/*
 * The saving functions have been rewritten in this file to support saving to external storage on android.
 */

ZeldaEnv g_zenv;
uint8 g_ram[131072];

uint32 g_wanted_zelda_features;
uint32 g_wanted_zelda_features1;
static int g_widescreen_edge_mode;

static void Startup_InitializeMemory();
static bool g_rendering_startup_logo;
bool g_test_scenario_active;
static bool g_test_scenario_building;
static int g_test_scenario_index=-1;

static bool IsDungeonMapMenuActive(void) {
  return saved_module_for_menu == 7 &&
    WideCamera_DungeonMapPicture(main_module_index, submodule_index, overworld_map_state);
}

typedef struct SimpleHdma {
  const uint8 *table;
  const uint8 *indir_ptr;
  uint8 rep_count;
  uint8 mode;
  uint8 ppu_addr;
  uint8 indir_bank;
} SimpleHdma;
static void SimpleHdma_Init(SimpleHdma *c, DmaChannel *dc);
static void SimpleHdma_DoLine(SimpleHdma *c, Ppu *ppu);

static const uint8 bAdrOffsets[8][4] = {
  {0, 0, 0, 0},
  {0, 1, 0, 1},
  {0, 0, 0, 0},
  {0, 0, 1, 1},
  {0, 1, 2, 3},
  {0, 1, 0, 1},
  {0, 0, 0, 0},
  {0, 0, 1, 1}
};
static const uint8 transferLength[8] = {
  1, 2, 2, 4, 4, 4, 2, 4
};
const uint16 kUpperBitmasks[] = { 0x8000, 0x4000, 0x2000, 0x1000, 0x800, 0x400, 0x200, 0x100, 0x80, 0x40, 0x20, 0x10, 8, 4, 2, 1 };
const uint8 kLitTorchesColorPlus[] = {31, 8, 4, 0};
const uint8 kDungeonCrystalPendantBit[13] = {0, 0, 4, 2, 0, 16, 2, 1, 64, 4, 1, 32, 8};
const int8 kGetBestActionToPerformOnTile_x[4] = { 7, 7, -3, 16 };
const int8 kGetBestActionToPerformOnTile_y[4] = { 6, 24, 12, 12 };
#define AT_WORD(x) (uint8)(x), (x)>>8
// direct
static const uint8 kAttractDmaTable0[13] = {0x20, AT_WORD(0x00ff), 0x50, AT_WORD(0xe018), 0x50, AT_WORD(0xe018), 1, AT_WORD(0x00ff), 0};
static const uint8 kAttractDmaTable1[10] = {0x48, AT_WORD(0x00ff), 0x30, AT_WORD(0xd830), 1, AT_WORD(0x00ff), 0};
static const uint8 kHdmaTableForEnding[19] = {
  0x52, AT_WORD(0x600), 8, AT_WORD(0xe2), 8, AT_WORD(0x602), 5, AT_WORD(0x604), 0x10, AT_WORD(0x606), 0x81, AT_WORD(0xe2), 0,
};
static const uint8 kSpotlightIndirectHdma[7] = {0xf8, AT_WORD(0x1b00), 0xf8, AT_WORD(0x1bf0), 0};
static const uint8 kMapModeHdma0[7] = {0xf0, AT_WORD(0xdd27), 0xf0, AT_WORD(0xde07), 0};
static const uint8 kMapModeHdma1[7] = {0xf0, AT_WORD(0xdee7), 0xf0, AT_WORD(0xdfc7), 0};
static const uint8 kAttractIndirectHdmaTab[7] = {0xf0, AT_WORD(0x1b00), 0xf0, AT_WORD(0x1be0), 0};
static const uint8 kHdmaTableForPrayingScene[7] = {0xf8, AT_WORD(0x1b00), 0xf8, AT_WORD(0x1bf0), 0};

void zelda_ppu_write(uint32_t adr, uint8_t val) {
  assert(adr >= INIDISP && adr <= STAT78);
  ppu_write(g_zenv.ppu, (uint8)adr, val);
}

void zelda_ppu_write_word(uint32_t adr, uint16_t val) {
  zelda_ppu_write(adr, val);
  zelda_ppu_write(adr + 1, val >> 8);
}

static const uint8 *SimpleHdma_GetPtr(uint32 p) {
  switch (p) {

  case 0xCFA87: return kAttractDmaTable0;
  case 0xCFA94: return kAttractDmaTable1;
  case 0xebd53: return kHdmaTableForEnding;
  case 0x0F2FB: return kSpotlightIndirectHdma;
  case 0xabdcf: return kMapModeHdma0;             // mode7
  case 0xabdd6: return kMapModeHdma1;             // mode7
  case 0xABDDD: return kAttractIndirectHdmaTab;   // mode7
  case 0x2c80c: return kHdmaTableForPrayingScene;

  case 0x1b00: return (uint8 *)hdma_table_dynamic;
  case 0x1be0: return (uint8 *)hdma_table_dynamic + 0xe0;
  case 0x1bf0: return (uint8 *)hdma_table_dynamic + 0xf0;
  case 0xadd27: return (uint8*)kMapMode_Zooms1;
  case 0xade07: return (uint8*)kMapMode_Zooms1 + 0xe0;
  case 0xadee7: return (uint8*)kMapMode_Zooms2;
  case 0xadfc7: return (uint8*)kMapMode_Zooms2 + 0xe0;
  case 0x600: return &g_ram[0x600];
  case 0x602: return &g_ram[0x602];
  case 0x604: return &g_ram[0x604];
  case 0x606: return &g_ram[0x606];
  case 0xe2: return &g_ram[0xe2];
  default:
    assert(0);
    return NULL;
  }
}

static void SimpleHdma_Init(SimpleHdma *c, DmaChannel *dc) {
  if (!dc->hdmaActive) {
    c->table = 0;
    return;
  }
  c->table = SimpleHdma_GetPtr(dc->aAdr | dc->aBank << 16);
  c->rep_count = 0;
  c->mode = dc->mode | dc->indirect << 6;
  c->ppu_addr = dc->bAdr;
  c->indir_bank = dc->indBank;
}

static void SimpleHdma_DoLine(SimpleHdma *c, Ppu *ppu) {
  if (c->table == NULL)
    return;
  bool do_transfer = false;
  if ((c->rep_count & 0x7f) == 0) {
    c->rep_count = *c->table++;
    if (c->rep_count == 0) {
      c->table = NULL;
      return;
    }
    if(c->mode & 0x40) {
      c->indir_ptr = SimpleHdma_GetPtr(c->indir_bank << 16 | c->table[0] | c->table[1] * 256);
      c->table += 2;
    }
    do_transfer = true;
  }
  if(do_transfer || c->rep_count & 0x80) {
    for(int j = 0, j_end = transferLength[c->mode & 7]; j < j_end; j++) {
      uint8 v = c->mode & 0x40 ? *c->indir_ptr++ : *c->table++;
      ppu_write(ppu,
                c->ppu_addr + bAdrOffsets[c->mode & 7][j],
                v);
    }
  }
  c->rep_count--;
}

static uint8 GetFixedCameraEffectiveContext(void) {
  int mod = main_module_index;
  if (mod == 14)
    mod = saved_module_for_menu;
  if ((enhanced_features0 & kFeatures0_WidescreenVisualFixes) &&
      (mod == 6 || mod == 8 || mod == 10 || mod == 15 || mod == 16 || mod == 17 ||
       mod == 18 || mod == 19 || mod == 21 || mod == 22 || mod == 23))
    mod = player_is_indoors ? 7 : 9;
  return mod == 7 || mod == 9 ? (uint8)mod : 0;
}

// The WIDE picture around the SNES one.
//
// The SNES draws 256x224 pixels at the game's camera (BG2HOFS/BG2VOFS_copy2).
// The WIDE picture adds side bands and, 240 lines high, 16 rows. A WideView
// places it: offset_x is the game's camera x minus the drawn camera x (the
// fixed camera stops short of an area's edges), delta_y the rows drawn above
// the SNES view (at the bottom of an area there is nothing below it), and
// left/right the side bands drawn.
//
// The rules for these change at once when an area or room edge moves, at the
// start or the end of a scroll transition. The drawn view does not jump:
// across a scroll transition it goes from the view before it to the view the
// destination has at its end, with the scroll's progress, and elsewhere it
// follows the rules at the camera's speed plus a few pixels per frame. It
// snaps only where two frames are not one picture: a new place, a dark
// screen, the light cone of a dark room.
typedef struct WideView {
  int offset_x;
  int delta_y;
  int left, right;
} WideView;

typedef struct WideBounds {
  int left, right;  // leftmost and rightmost camera x
  int bottom;       // lowest camera y
} WideBounds;

typedef struct WideViewTracker {
  bool valid;
  uint8 context;
  uint8 axis;       // scroll transition in progress: 1 horizontal, 2 vertical
  int direction;
  int start, end;   // game camera along the axis, from and to
  uint16 last_x, last_y;
  WideView last, last_target, from;
} WideViewTracker;

static WideViewTracker g_wide_view;

void ZeldaSetWidescreenEdgeMode(int mode) {
  int new_mode = mode == 1 ? 1 : 0;
  if (g_widescreen_edge_mode != new_mode)
    memset(&g_wide_view, 0, sizeof(g_wide_view));
  g_widescreen_edge_mode = new_mode;
}

int ZeldaGetWidescreenEdgeMode(void) {
  return g_widescreen_edge_mode;
}

static bool WideFixedCameraEnabled(void) {
  bool visual_fixes_enabled =
    (enhanced_features0 & kFeatures0_WidescreenVisualFixes) != 0;
#ifdef ZELDA_CAMERA_TEST
  visual_fixes_enabled = true;
#endif
  return g_widescreen_edge_mode == 1 && visual_fixes_enabled &&
         g_zenv.ppu && g_zenv.ppu->extraLeftRight != 0;
}

static bool QolBrightRoomActive(void);
static struct { bool active,reveal; uint8 tick; int axis,source,end; unsigned light; } g_private_fade;
static void UpdatePrivateRoomFade(void);
static bool WideLanternRoom(uint8 context) {
  return context == 7 && hdr_dungeon_dark_with_lantern && TS_copy != 0 && !QolBrightRoomActive();
}

// Set as each picture is drawn, read by the game logic of the next frame.
static int g_wide_picture_shift;
static uint32 g_wide_render_flags;
// The rows above the game's screen whose sprite tiles the game keeps
// (sprite.h): those of the last picture, then of the picture of this frame
// once its camera is set (ZeldaWideBeforeSprites).
int g_wide_picture_rows;

int ZeldaWidePictureShift(void) {
  return (enhanced_features0 & kFeatures0_ExtendScreen64) ? g_wide_picture_shift : 0;
}

// Both maps fade the gameplay picture before drawing the map. The dungeon
// map also restores the room before fading back in. Keep gameplay presentation
// during those phases; module 14 alone does not identify the visible picture.
static bool UseNativeMapCamera(void) {
  if (!WideCamera_IsMapMenu(main_module_index, submodule_index)) return false;
  if (submodule_index == 3 && !IsDungeonMapMenuActive()) return false;
  bool world_fade = (submodule_index == 7 || submodule_index == 10) &&
                    overworld_map_state == 0 && g_zenv.ppu->mode == 1;
  return !world_fade;
}

int ZeldaGetWidescreenFixedCameraMargin(void) {
  uint8 context = GetFixedCameraEffectiveContext();
  if (!WideFixedCameraEnabled() || context == 0)
    return 0;
  // Map sprites are projected in map-screen coordinates, not the gameplay
  // camera. Applying the outdoor/dungeon camera delta hides or shifts them.
  if (UseNativeMapCamera())
    return 0;
  if (WideLanternRoom(context))
    return 0;
  return g_zenv.ppu->extraLeftRight;
}

static void GetWideBounds(uint8 context, int x, int y, WideBounds *b) {
  uint16 left, right, bottom;
  if (context == 7) {
    int qm = quadrant_fullsize_x >> 1;
    left = room_bounds_x.v[qm];
    right = room_bounds_x.v[qm + 2];
    bottom = room_bounds_y.v[(quadrant_fullsize_y >> 1) + 2];
  } else {
    left = ow_scroll_vars0.xstart;
    right = ow_scroll_vars0.xend;
    bottom = ow_scroll_vars0.yend;
  }
  b->left = WideCamera_Unwrap16(left, x);
  b->right = WideCamera_Unwrap16(right, x);
  b->bottom = WideCamera_Unwrap16(bottom, y);
}

// The bounds of the overworld area being entered, which the game sets only
// once the scroll is over (Overworld_SetCameraBoundaries, from the same base
// and size).
static void GetOverworldDestinationBounds(int x, int y, WideBounds *b) {
  bool big = BYTE(overworld_area_is_big) != 0;
  b->left = WideCamera_Unwrap16(overworld_offset_base_x << 3, x);
  b->right = b->left + (big ? 0x300 : 0x100);
  b->bottom = WideCamera_Unwrap16(overworld_offset_base_y, y) +
              (big ? 0x31e : 0x11e);
}

// Axis (1 horizontal, 2 vertical) and direction of the scroll transition in
// progress: overworld screen edges, dungeon room and door edges, straight
// stairs. Indoors overworld_screen_transition is 0 down, 1 up, 2 right,
// 3 left (kStaircaseTab3).
static int GetWideTransition(uint8 context, int *direction) {
  *direction = 0;
  if (context == 9) {
    // The bits also follow the player near edges outside transitions: only
    // the scroll submodules, plain (1-8) and with mosaic (0x0d-0x16), count.
    if (main_module_index != 9 || submodule_index == 0 ||
        submodule_index > 0x16)
      return 0;
    int bits = BYTE(overworld_screen_trans_dir_bits);
    if (bits & 3) {
      *direction = bits & 1 ? 1 : -1;
      return 1;
    }
    if (bits & 12) {
      *direction = bits & 4 ? 1 : -1;
      return 2;
    }
    return 0;
  }
  int sub = submodule_index, t = overworld_screen_transition;
  if (main_module_index != 7 || t > 3 ||
      !(sub == 1 || sub == 2 || (sub >= 0x11 && sub <= 0x13)))
    return 0;
  if (t >= 2) {
    if (sub > 2)
      return 0;
    *direction = t == 2 ? 1 : -1;
    return 1;
  }
  *direction = t == 0 ? 1 : -1;
  return 2;
}

static int GetFixedCameraTransitionEndX(uint8 context, int direction,
                                        uint16 start_logical_x) {
  if (context == 7) {
    int target = direction > 0 ? left_right_scroll_target :
                                 left_right_scroll_target_end;
    return WideCamera_FindDungeonTransitionEnd(
      start_logical_x, direction, target);
  }

  uint16 target = direction > 0 ? left_right_scroll_target_end :
                                  left_right_scroll_target;
  int end_logical_x = WideCamera_Unwrap16(target, start_logical_x);
  if ((direction > 0 && end_logical_x <= start_logical_x) ||
      (direction < 0 && end_logical_x >= start_logical_x))
    return start_logical_x + direction * 256;
  return end_logical_x;
}

// Where the game's camera stops, along the axis: the scroll targets the
// transition compares against.
static int GetWideTransitionEnd(uint8 context, int axis, int direction,
                                int start) {
  if (axis == 1)
    return WideCamera_Unwrap16(
      GetFixedCameraTransitionEndX(context, direction, (uint16)start), start);
  if (context == 7)  // DungeonTransition_ScrollRoom: 0 down, 1 up
    return WideCamera_Unwrap16(WideCamera_FindDungeonTransitionEnd(
      (uint16)start, direction,
      direction > 0 ? up_down_scroll_target : up_down_scroll_target_end),
      start);
  // OverworldScrollTransition: 0 up, 1 down
  uint16 target = direction > 0 ? up_down_scroll_target_end :
                                  up_down_scroll_target;
  int end = WideCamera_Unwrap16(target, start);
  if ((end - start) * direction <= 0)
    end = start + direction * 224;
  return end;
}

static WideView WideView_Steady(int margin, bool rows, int x, int y,
                                const WideBounds *b) {
  WideView v;
  int visual = WideCamera_ClampToBounds(x, b->left, b->right, margin);
  v.offset_x = x - visual;
  v.left = IntMin(IntMax(visual - b->left, 0), margin);
  v.right = IntMin(IntMax(b->right - visual, 0), margin);
  v.delta_y = rows ? IntMin(IntMax(16 - (b->bottom - y), 0), 16) : 0;
  return v;
}

// Without commit, the view a picture drawn now would have, the tracking of
// the last one kept.
static WideView UpdateWideView(uint8 context, int margin, bool rows,
                               bool smooth, bool smooth_x, bool *settled,
                               bool commit) {
  WideViewTracker next = g_wide_view, *t = &next;
  uint16 x = BG2HOFS_copy2, y = BG2VOFS_copy2;
  WideBounds bounds;
  GetWideBounds(context, x, y, &bounds);
  WideView target = WideView_Steady(margin, rows, x, y, &bounds);
  if (!t->valid || t->context != context) {
    memset(t, 0, sizeof(*t));
    t->valid = true;
    t->context = context;
    t->last = target;
    t->last_target = target;
    t->last_x = x;
    t->last_y = y;
  }
  int dx = (int16)(x - t->last_x), dy = (int16)(y - t->last_y);
  int direction, axis = GetWideTransition(context, &direction);
  WideView v = target;
  if (axis) {
    int cur = axis == 1 ? x : y;
    if (t->axis != axis || t->direction != direction) {
      int step = axis == 1 ? dx : dy;
      t->axis = (uint8)axis;
      t->direction = direction;
      t->start = step >= -16 && step <= 16 ? cur - step : cur;
      t->end = GetWideTransitionEnd(context, axis, direction, t->start);
      t->from = t->last;
    }
    int moved = (WideCamera_Unwrap16(cur, t->start) - t->start) * direction;
    int total = (t->end - t->start) * direction;
    int progress = total > 0 ? IntMin(IntMax(moved * 256 / total, 0), 256) : 256;
    WideBounds destination = bounds;
    if (context == 9)
      GetOverworldDestinationBounds(x, y, &destination);
    // Into or out of a dark room the side bands close: its light cone keeps
    // them dark once there.
    int end_margin = context == 7 && submodule_index == 2 &&
      (dung_want_lights_out | dung_want_lights_out_copy) && !QolBrightRoomActive() ? 0 : margin;
    WideView end = WideView_Steady(end_margin, rows, axis == 1 ? t->end : x,
                                   axis == 2 ? t->end : y, &destination);
    if (smooth)
      v.delta_y = WideCamera_Lerp(t->from.delta_y, end.delta_y, progress);
    if (smooth && smooth_x) {
      v.offset_x = WideCamera_Lerp(t->from.offset_x, end.offset_x, progress);
      v.left = WideCamera_Lerp(t->from.left, end.left, progress);
      v.right = WideCamera_Lerp(t->from.right, end.right, progress);
    }
  } else {
    t->axis = 0;
    t->direction = 0;
    // Take the part of the rules' change the camera's own move explains
    // (the picture stays put at an edge), then close the rest slowly.
    if (smooth && abs(dx) <= 48 && abs(dy) <= 48) {
      v.delta_y = WideCamera_Follow(t->last.delta_y, t->last_target.delta_y,
                             target.delta_y, dy, 1);
      if (smooth_x) {
        v.offset_x = WideCamera_Follow(t->last.offset_x, t->last_target.offset_x,
                                target.offset_x, dx, 3);
        v.left = WideCamera_Follow(t->last.left, t->last_target.left, target.left,
                            dx, 3);
        v.right = WideCamera_Follow(t->last.right, t->last_target.right,
                             target.right, -dx, 3);
      }
    }
  }
#ifdef ZELDA_CAMERA_TEST
  // The drawn picture follows the game's camera, gliding by a few pixels.
  int picture_x = dx - (v.offset_x - t->last.offset_x);
  int picture_y = dy - (v.delta_y - t->last.delta_y);
  if (commit && smooth && abs(dx) <= 48 && abs(dy) <= 48 &&
      (abs(picture_x - dx) > 12 || abs(picture_y - dy) > 4))
    fprintf(stderr, "camera ERROR jump: context=%u axis=%d camera=%d,%d "
            "picture=%d,%d\n", context, axis, dx, dy, picture_x, picture_y);
#endif
  *settled = !axis && !memcmp(&v, &target, sizeof(v));
  t->last = v;
  t->last_target = target;
  t->last_x = x;
  t->last_y = y;
  if (commit)
    g_wide_view = next;
  return v;
}

static bool WideRowsEnabled(uint32 render_flags) {
  // The original camera's lower limit is defined for 224 lines. Near that
  // limit the 240-line picture reveals the missing rows above instead of
  // reading beyond the room or area.
  return (render_flags & kPpuRenderFlags_Height240) != 0;
}

typedef struct WideFrame {
  uint8 context;      // 7 or 9 while the picture follows the game's camera
  bool fixed_camera;  // the drawn camera and the side bands follow the view
  bool rows;          // 240-line picture
  bool settled;       // no transition, and the view is the rules' one
  WideView view;
} WideFrame;

// Only the drawing moves: collision, camera RAM and saved state stay intact.
static WideFrame WideFrameNow(uint32 render_flags, bool commit) {
  WideFrame f = {0};
  Ppu *p = g_zenv.ppu;
  uint8 context = UseNativeMapCamera() ?
                  0 : GetFixedCameraEffectiveContext();
  bool fixed = WideFixedCameraEnabled();
  bool rows = WideRowsEnabled(render_flags);
  if (!context || !(fixed || rows)) {
    if (commit)
      memset(&g_wide_view, 0, sizeof(g_wide_view));
    return f;
  }
  bool lantern = WideLanternRoom(context);
  bool visible = !p->forcedBlank && p->brightness != 0;
  f.context = context;
  f.fixed_camera = fixed;
  f.rows = rows;
  f.view = UpdateWideView(context, fixed && !lantern ? p->extraLeftRight : 0,
                          rows, visible, !lantern, &f.settled, commit);
  if(g_private_fade.active) {
    if(g_private_fade.axis==1)f.view.offset_x=(int16)(BG2HOFS_copy2-g_private_fade.source);
    else f.view.delta_y=(int16)(BG2VOFS_copy2-g_private_fade.source);
    f.view.left=f.view.right=0;f.settled=false;
  }
  return f;
}

static WideFrame BeginWideFrame(uint32 render_flags) {
  return WideFrameNow(render_flags, true);
}

// The 240-line picture moves up at the bottom of an area or a room: the
// sprite tiles of the rows it then shows above the game's screen are kept
// with WidescreenVisualFixes.
static int WideSpriteRows(const WideFrame *f) {
  return f->rows && (enhanced_features0 & kFeatures0_WidescreenVisualFixes) ?
         f->view.delta_y : 0;
}

void ZeldaWideBeforeSprites(void) {
  WideFrame f = WideFrameNow(g_wide_render_flags, false);
  g_wide_picture_rows = WideSpriteRows(&f);
}

static bool WideRoomVoidActive(void);

static void ConfigurePpuSideSpace(const WideFrame *f) {
  // Let PPU impl know about the maximum allowed extra space on the sides and bottom
  int extra_right = 0, extra_left = 0, extra_bottom = 0;
  if (IsDungeonMapMenuActive()) {
    // The outer 16 native pixels contain only the map backdrop. Continue
    // that strip through Wide margins without wrapping floor grids or
    // moving the frame and its sprite markers out of native coordinates.
    PpuSetExtraSideSpace(g_zenv.ppu, 0, 0, 16);
    g_zenv.ppu->wideMarginRepeat = 1;
    return;
  }
  bool repeat_margins = false;
//  printf("main %d, sub %d  (%d, %d, %d)\n", main_module_index, submodule_index, BG2HOFS_copy2, room_bounds_x.v[2 | (quadrant_fullsize_x >> 1)], quadrant_fullsize_x >> 1);
  int mod = main_module_index;
  if (mod == 14)
    mod = saved_module_for_menu;
  if ((enhanced_features0 & kFeatures0_WidescreenVisualFixes) &&
      (mod == 6 || mod == 8 || mod == 10 || mod == 15 || mod == 16 || mod == 17 ||
       mod == 18 || mod == 19 || mod == 21 || mod == 22 || mod == 23))
    mod = player_is_indoors ? 7 : 9;
  if (mod == 9) {
    // Outdoors
    if (f->fixed_camera) {
      extra_left = f->view.left;
      extra_right = f->view.right;
    } else {
      extra_left = BG2HOFS_copy2 - ow_scroll_vars0.xstart;
      extra_right = ow_scroll_vars0.xend - BG2HOFS_copy2;
    }
    extra_bottom = ow_scroll_vars0.yend - BG2VOFS_copy2;
  } else if (mod == 7) {
    // indoors, except when the light cone is in use
    if (!WideLanternRoom(7)) {
      // Rooms end at their bounds. Beside them draw the room's void, or else
      // repeat its outer edge (shaking or layered rooms) instead of black.
      repeat_margins = false;
      int qm = quadrant_fullsize_x >> 1;
      if (WideRoomVoidActive()) {
        // The bands beside the room draw its void (BeginWideRoomVoid).
        extra_left = extra_right = kPpuExtraLeftRight;
        repeat_margins = false;
      } else if (f->fixed_camera) {
        extra_left = f->view.left;
        extra_right = f->view.right;
      } else {
        extra_left = IntMax(BG2HOFS_copy2 - room_bounds_x.v[qm], 0);
        extra_right = IntMax(room_bounds_x.v[qm + 2] - BG2HOFS_copy2, 0);
      }
    }

    int qy = quadrant_fullsize_y >> 1;
    extra_bottom = IntMax(room_bounds_y.v[qy + 2] - BG2VOFS_copy2, 0);
  } else if (mod == 20 || mod == 0 || mod == 1) {
    extra_left = kPpuExtraLeftRight, extra_right = kPpuExtraLeftRight;
    extra_bottom = 16;
  }
  // The 240-line picture is drawn whole: delta_y moves it up instead.
  if (f->rows && f->context)
    extra_bottom = 16;
  PpuSetExtraSideSpace(g_zenv.ppu, extra_left, extra_right, extra_bottom);
  g_zenv.ppu->wideMarginRepeat = repeat_margins;
}

typedef struct WideRenderState {
  int dx, dy;
  uint16 bg1_h, bg2_h, bg1_v, bg2_v;
  int16 obj_x, obj_y;
} WideRenderState;

static WideRenderState BeginWideRender(const WideFrame *f) {
  WideRenderState s = {0};
  Ppu *p = g_zenv.ppu;
  if (!f->context)
    return s;
  s.dx = f->fixed_camera ? f->view.offset_x : 0;
  s.dy = f->rows ? f->view.delta_y : 0;
  s.bg1_h = p->bgLayer[0].hScroll;
  s.bg2_h = p->bgLayer[1].hScroll;
  s.bg1_v = p->bgLayer[0].vScroll;
  s.bg2_v = p->bgLayer[1].vScroll;
  s.obj_x = p->renderObjXOffset;
  s.obj_y = p->renderObjYOffset;
  if (s.dx) {
    p->bgLayer[0].hScroll = (s.bg1_h - s.dx) & 0x3ff;
    p->bgLayer[1].hScroll = (s.bg2_h - s.dx) & 0x3ff;
    p->renderObjXOffset = s.obj_x + s.dx;
  }
  if (s.dy) {
    p->bgLayer[0].vScroll = (s.bg1_v - s.dy) & 0x3ff;
    p->bgLayer[1].vScroll = (s.bg2_v - s.dy) & 0x3ff;
    p->renderObjYOffset = s.obj_y + s.dy;
  }
  return s;
}

static void EndWideRender(const WideRenderState *s) {
  Ppu *p = g_zenv.ppu;
  if (s->dx) {
    p->bgLayer[0].hScroll = s->bg1_h;
    p->bgLayer[1].hScroll = s->bg2_h;
    p->renderObjXOffset = s->obj_x;
  }
  if (s->dy) {
    p->bgLayer[0].vScroll = s->bg1_v;
    p->bgLayer[1].vScroll = s->bg2_v;
    p->renderObjYOffset = s->obj_y;
  }
}

// The maps the game keeps in WRAM of the room (BG2 at $2000, BG1 at $4000,
// 64x64 entries) or of the overworld area (map16 at $2000), and of the place
// before them: the game rebuilds them for the next place before the scroll
// towards it, and streams the tile maps in VRAM only for the SNES picture.
typedef struct WideRegion {
  uint8 context;     // 7 room, 9 overworld area, 0 unknown
  int x, y, size;    // world rectangle
  uint16 room;       // room whose geometry is retained (may differ during a load)
  int room_left, room_right; // selected subroom span, relative to x
  bool private_room; // houses/caves keep closed neighboring chambers covered
} WideRegion;

static WideRegion g_wide_live, g_wide_prev;
static uint16 g_wide_prev_maps[0x2000];

static void WideRoomUpdateSpan(WideRegion *r) {
  int qx = quadrant_fullsize_x >> 1;
  int left = (int16)(room_bounds_x.v[qx] - r->x);
  int right = (int16)(room_bounds_x.v[qx + 2] - r->x) + 256;
  if(g_private_fade.active && r->private_room && g_private_fade.axis==1) {
    left=(int16)(g_private_fade.source-r->x);right=left+256;
  }
  if (left >= 0 && right <= 512 && left < right) {
    r->room_left = left;
    r->room_right = right;
  }
}

static WideRegion WideRegionFromGame(uint8 context) {
  WideRegion r = {0};
  r.context = context;
  if (context == 7) {
    r.room = dungeon_room_index;
    r.private_room = (uint8)cur_palace_index_x2 == 0xff;
    r.x = (dungeon_room_index & 15) * 512;
    r.y = (dungeon_room_index >> 4) * 512;
    r.size = r.room_right = 512;
    WideRoomUpdateSpan(&r);
  } else {
    r.x = overworld_offset_base_x << 3;
    r.y = overworld_offset_base_y;
    r.size = BYTE(overworld_area_is_big) ? 1024 : 512;
  }
  return r;
}

// Rooms have no fixed place in the camera's frame: teleport doors and
// stairs move the frame (Dungeon_AdjustForTeleportDoors,
// Dungeon_AdjustAfterSpiralStairs). The room the SNES camera shows sits in
// the 512-pixel block around it.
static bool WideRegionHas(const WideRegion *r, int x, int y) {
  return (unsigned)(int16)(x - r->x) < (unsigned)r->size &&
         (unsigned)(int16)(y - r->y) < (unsigned)r->size;
}

static void WideRoomFollowCamera(void) {
  int x = BG2HOFS_copy2 & ~0x1ff, y = BG2VOFS_copy2 & ~0x1ff;
  if (g_wide_live.context != 7 || (main_module_index == 7 && submodule_index == 2 &&
      abs((int16)(x - g_wide_live.x)) <= 512 &&
      abs((int16)(y - g_wide_live.y)) <= 512) ||
      WideRegionHas(&g_wide_live, x, y) ||
      (g_wide_prev.context == 7 && WideRegionHas(&g_wide_prev, x, y)))
    return;
  g_wide_live.x = x;
  g_wide_live.y = y;
}

// The game calls these around rebuilding a room (Dungeon_LoadRoom) or an
// area (Overworld_DecompressAndDrawAllQuadrants) in WRAM.
static void InvalidateWideRoomVoid(void);

void ZeldaWideBeforeMapLoad(int context) {
  g_wide_prev = g_wide_live;
  if (context == 7 && g_wide_prev.context == 7) {
    // The room being left is the one the camera shows.
    g_wide_prev.x = BG2HOFS_copy2 & ~0x1ff;
    g_wide_prev.y = BG2VOFS_copy2 & ~0x1ff;
  }
  if (g_wide_prev.context)
    memcpy(g_wide_prev_maps, g_ram + 0x2000, sizeof(g_wide_prev_maps));
}

void ZeldaWideAfterMapLoad(int context) {
  InvalidateWideRoomVoid();
  g_wide_live = WideRegionFromGame((uint8)context);
}

// Beside the two regions, outdoors: across a scroll the drawn camera glides
// from the view of one area to the view of the next, so the rows of one area
// can show the screen beside it, which neither region holds and the game has
// not loaded. Its map16 is decoded from the map data
// (Overworld_DecodeScreenMap16, without the overlays the game adds when it
// enters an area) and kept for the frames after.
static struct {
  int room[4];
  bool valid[4];
  uint8 next, pinned;
  uint16 maps[4][0x2000];
} g_wide_rooms;

static struct {
  int16 screen[4];  // screen index + 1, 0 while free
  bool valid[4];
  uint8 next;
  uint16 map16[4][32 * 32];
} g_wide_screens;

// Outdoors, a scroll transition puts the next area's aux graphics in VRAM
// (Module09_LoadAuxGFX, then the NMI of that frame and of the next) before
// the camera moves. The SNES picture shows the area being left only where
// the two share their graphics; the WIDE picture shows more of it, whose
// tiles would take the next area's graphics (a house of Secrets of the Past
// turns into pale scales). The graphics of the area left are kept here, and
// for the frames of the transition its cells in the WIDE picture draw them
// from tiles the frame does not show.
static struct {
  bool valid;
  WideRegion region;    // the area they belong to
  uint16 tile_adr;      // VRAM address of BG tile 0
  uint16 chr[0x2000];   // BG tiles 0-511
} g_wide_old_chr;

static void ResetWideTracking(void) {
  InvalidateWideRoomVoid();
  memset(&g_wide_view, 0, sizeof(g_wide_view));
  memset(&g_private_fade,0,sizeof(g_private_fade));
  memset(&g_wide_live, 0, sizeof(g_wide_live));
  memset(&g_wide_prev, 0, sizeof(g_wide_prev));
  memset(g_wide_screens.screen, 0, sizeof(g_wide_screens.screen));
  g_wide_old_chr.valid = false;
  memset(&g_wide_rooms, 0, sizeof(g_wide_rooms));
}

// The WIDE picture shows tile map cells the SNES picture does not, which the
// game may have filled for another place or not yet. Draw them from the
// maps above for the duration of the frame. VRAM is restored after joined
// CPU rendering / synchronous GPU scene preparation, so prefetch state, game
// logic and later transitions remain intact.
static struct {
  uint16 address[8192], value[8192];
  unsigned count, last_count;
} g_wide_tile_repair;

void ZeldaWideBeforeAuxGfx(void) {
  Ppu *p = g_zenv.ppu;
  g_wide_old_chr.valid = false;
  // Only the scroll transitions (submodule 1); the others change the whole
  // picture at once.
  if (!p || !p->extraLeftRight || main_module_index != 9 || submodule_index != 1 ||
      g_wide_live.context != 9)
    return;
  g_wide_old_chr.region = g_wide_live;
  g_wide_old_chr.tile_adr = p->bgLayer[1].tileAdr;
  for (int i = 0; i < 0x2000; i++)
    g_wide_old_chr.chr[i] = p->vram[(g_wide_old_chr.tile_adr + i) & 0x7fff];
  g_wide_old_chr.valid = true;
}

static bool WidePatchVram(unsigned address, uint16 value) {
  Ppu *p = g_zenv.ppu;
  address &= 0x7fff;
  if (p->vram[address] == value)
    return true;
  unsigned n = g_wide_tile_repair.count;
  if (n == countof(g_wide_tile_repair.address))
    return false;
  g_wide_tile_repair.address[n] = address;
  g_wide_tile_repair.value[n] = p->vram[address];
  g_wide_tile_repair.count = n + 1;
  p->vram[address] = value;
  return true;
}

static unsigned WideMapAddress(const BgLayer *bg, int tx, int ty) {
  unsigned wx = tx & 511, wy = ty & 511;
  return (bg->tilemapAdr + (wy >> 3 & 31) * 32 + (wx >> 3 & 31) +
          (wx >= 256 ? 0x400 : 0) + (wy >= 256 ? 0x800 : 0)) & 0x7fff;
}

// The cells of the area left, beside the SNES picture of BG2 (x0..x1, y0..y1
// around the camera x, y), take its graphics from tiles no cell of BG1 or BG2
// shows in this frame.
static void WideRepairOldGraphics(int x, int y, int x0, int x1, int y0, int y1) {
  Ppu *p = g_zenv.ppu;
  const BgLayer *bg2 = &p->bgLayer[1];
  if (main_module_index != 9 || submodule_index == 0)
    g_wide_old_chr.valid = false;  // the transition is over
  if (!g_wide_old_chr.valid || bg2->tileAdr != g_wide_old_chr.tile_adr ||
      p->bgLayer[0].tileAdr != g_wide_old_chr.tile_adr)
    return;
  int ox = g_wide_old_chr.region.x, oy = g_wide_old_chr.region.y;
  int osize = g_wide_old_chr.region.size;
  if (x1 <= ox || x0 >= ox + osize || y1 <= oy || y0 >= oy + osize)
    return;
  // Tiles 0-511 a cell of the frame shows, or whose words hold other data.
  static uint8 busy[512];
  static int16 slot_of[512];
  memset(busy, 0, sizeof(busy));
  for (int layer = 0; layer < 2; layer++) {
    const BgLayer *bg = &p->bgLayer[layer];
    if (!((p->screenEnabled[0] | p->screenEnabled[1]) & (1 << layer)))
      continue;
    int lx = (int16)(layer ? BG2HOFS_copy2 : BG1HOFS_copy2) + (x - (int16)BG2HOFS_copy2);
    int ly = (int16)(layer ? BG2VOFS_copy2 : BG1VOFS_copy2) + (y - (int16)BG2VOFS_copy2);
    int dx = lx - x, dy = ly - y;
    for (int ty = (y0 + dy) & ~7; ty < y1 + dy; ty += 8)
      for (int tx = (x0 + dx) & ~7; tx < x1 + dx; tx += 8) {
        unsigned t = p->vram[WideMapAddress(bg, tx, ty)] & 0x3ff;
        if (t < 512)
          busy[t] = 1;
      }
  }
  // The words of BG3's tiles and of the sprites' are not borrowed.
  const unsigned other[3][2] = {
    { p->bgLayer[2].tileAdr, 0x2000 }, { p->objTileAdr1, 0x1000 }, { p->objTileAdr2, 0x1000 },
  };
  for (int t = 0; t < 512; t++) {
    unsigned a = (g_wide_old_chr.tile_adr + t * 16) & 0x7fff;
    for (int i = 0; i < 3; i++)
      if (((a - other[i][0]) & 0x7fff) < other[i][1] || ((other[i][0] - a) & 0x7fff) < 16)
        busy[t] = 1;
  }
  for (int t = 0; t < 512; t++)
    slot_of[t] = -1;
  int next_free = 0;
  for (int ty = y0 & ~7; ty < y1; ty += 8) {
    bool row_in_view = ty + 8 > y + 1 && ty <= y + 224;
    for (int tx = x0 & ~7; tx < x1; tx += 8) {
      if (row_in_view && tx + 8 > x && tx < x + 256)
        continue;  // the SNES picture: as the game draws it
      if ((unsigned)(tx - ox) >= (unsigned)osize || (unsigned)(ty - oy) >= (unsigned)osize)
        continue;
      unsigned address = WideMapAddress(bg2, tx, ty);
      uint16 cell = p->vram[address];
      unsigned t = cell & 0x3ff;
      if (t >= 512)
        continue;
      const uint16 *old = &g_wide_old_chr.chr[t * 16];
      unsigned chr = (g_wide_old_chr.tile_adr + t * 16) & 0x7fff;
      bool same = true;
      for (int i = 0; i < 16 && same; i++)
        same = p->vram[(chr + i) & 0x7fff] == old[i];
      if (same)
        continue;
      if (slot_of[t] < 0) {
        while (next_free < 512 && busy[next_free])
          next_free++;
        if (next_free == 512)
          return;
        unsigned dst = (g_wide_old_chr.tile_adr + next_free * 16) & 0x7fff;
        for (int i = 0; i < 16; i++)
          if (!WidePatchVram(dst + i, old[i]))
            return;
        busy[next_free] = 1;
        slot_of[t] = (int16)next_free;
      }
      if (!WidePatchVram(address, (cell & ~0x3ff) | slot_of[t]))
        return;
    }
  }
}

static int WideTileTruth(const WideRegion *r, const uint16 *maps, int layer,
                         int x, int y, const uint16 *map8) {
  int rx = (int16)(x - r->x), ry = (int16)(y - r->y);
  if ((unsigned)rx >= (unsigned)r->size || (unsigned)ry >= (unsigned)r->size)
    return -1;
  if (r->context == 7)
    return maps[(layer ? 0 : 0x1000) + (ry >> 3) * 64 + (rx >> 3)];
  if (layer != 1 || !map8)
    return -1;
  unsigned tile = maps[(ry >> 4) * 64 + (rx >> 4)];
  if (tile >= kMap16ToMap8_SIZE / 8)
    return -1;
  return map8[tile * 4 + (((ry & 8) >> 2) | ((rx & 8) >> 3))];
}

static int WideScreenTruth(int x, int y, const uint16 *map8) {
  if (!map8 || (unsigned)x >= 0x1000 || (unsigned)y >= 0x1000 ||
      overworld_screen_index >= 0x80)
    return -1;
  int screen = (y >> 9) * 8 + (x >> 9) + (overworld_screen_index & 0x40);
  int i = 0;
  while (i < 4 && g_wide_screens.screen[i] != screen + 1)
    i++;
  if (i == 4) {
    i = g_wide_screens.next++ & 3;
    g_wide_screens.screen[i] = (int16)(screen + 1);
    g_wide_screens.valid[i] =
      Overworld_DecodeScreenMap16(screen, g_wide_screens.map16[i]);
  }
  if (!g_wide_screens.valid[i])
    return -1;
  unsigned tile = g_wide_screens.map16[i][(y >> 4 & 31) * 32 + (x >> 4 & 31)];
  if (tile >= kMap16ToMap8_SIZE / 8)
    return -1;
  return map8[tile * 4 + (((y & 8) >> 2) | ((x & 8) >> 3))];
}

static void BeginWideTileRepair(const WideFrame *f) {
  g_wide_tile_repair.count = g_wide_tile_repair.last_count = 0;
  Ppu *p = g_zenv.ppu;
  if (!f->context || !f->fixed_camera || p->mode != 1)
    return;
  if (!g_wide_live.context) {
    g_wide_live = WideRegionFromGame(f->context);
    int previous=dungeon_room_index_prev,delta=previous-dungeon_room_index;
    if(f->context==7&&!g_wide_live.private_room&&main_module_index==7&&submodule_index==2&&
       (abs(delta)==16||(abs(delta)==1&&(previous>>4)==(dungeon_room_index>>4)))&&
       previous<0x140&&Dungeon_DecodeRoomMaps(previous,g_wide_prev_maps)) {
      g_wide_prev=g_wide_live;g_wide_prev.room=previous;
      g_wide_prev.x=(previous&15)*512;g_wide_prev.y=(previous>>4)*512;
      g_wide_prev.room_left=0;g_wide_prev.room_right=512;
    }
  }
  if (g_wide_live.context != f->context)
    return;
  WideRoomFollowCamera();
  // Settled indoors, the whole room stays in VRAM, and the game animates
  // some of it there only.
  if (f->context == 7 && f->settled && main_module_index == 7 &&
      submodule_index == 0)
    return;
  const uint16 *map8 = kMap16ToMap8 ? GetMap16toMap8Table() : NULL;
  const WideRegion *regions[2] = { &g_wide_live, &g_wide_prev };
  const uint16 *maps[2] = { (const uint16 *)(g_ram + 0x2000), g_wide_prev_maps };
  int nregions = g_wide_prev.context == g_wide_live.context ? 2 : 1;
  for (int layer = f->context == 7 ? 0 : 1; layer < 2; layer++) {
    BgLayer *bg = &p->bgLayer[layer];
    uint16 sx = layer ? BG2HOFS_copy2 : BG1HOFS_copy2;
    uint16 sy = layer ? BG2VOFS_copy2 : BG1VOFS_copy2;
    // The picture shakes (bg1_x_offset, bg1_y_offset) by what the scroll
    // registers add to the camera.
    int shake_x = ((bg->hScroll - sx) & 511) ^ 256, shake_y = ((bg->vScroll - sy) & 511) ^ 256;
    shake_x -= 256, shake_y -= 256;
    if (!((p->screenEnabled[0] | p->screenEnabled[1]) & (1 << layer)) ||
        !bg->tilemapWider || !bg->tilemapHigher ||
        sx != BG2HOFS_copy2 || sy != BG2VOFS_copy2 ||
        abs(shake_x) > 16 || abs(shake_y) > 16)
      continue;
    int x = (int16)sx + shake_x, y = (int16)sy + shake_y;
    int visual = x - f->view.offset_x;
    int x0 = visual - f->view.left, x1 = visual + 256 + f->view.right;
    int y0 = y + 1 - f->view.delta_y, y1 = y0 + (f->rows ? 240 : 224);
    for (int ty = y0 & ~7; ty < y1; ty += 8) {
      bool row_in_view = ty + 8 > y + 1 && ty <= y + 224;
      for (int tx = x0 & ~7; tx < x1; tx += 8) {
        if (row_in_view && tx + 8 > x && tx < x + 256)
          continue;  // the SNES picture: VRAM is right
        int value = -1;
        for (int i = 0; i < nregions && value < 0; i++)
          value = WideTileTruth(regions[i], maps[i], layer, tx, ty, map8);
        if (value < 0 && f->context == 9)
          value = WideScreenTruth(tx, ty, map8);
        if (value < 0)
          continue;
        unsigned wx = tx & 511, wy = ty & 511;
        unsigned address = (bg->tilemapAdr + (wy >> 3 & 31) * 32 + (wx >> 3 & 31) +
          (wx >= 256 ? 0x400 : 0) + (wy >= 256 ? 0x800 : 0)) & 0x7fff;
        if (p->vram[address] == value)
          continue;
        unsigned n = g_wide_tile_repair.count;
        if (n == countof(g_wide_tile_repair.address))
          return;
        g_wide_tile_repair.address[n] = address;
        g_wide_tile_repair.value[n] = p->vram[address];
        g_wide_tile_repair.count = n + 1;
        p->vram[address] = value;
      }
    }
    if (f->context == 9 && layer == 1)
      WideRepairOldGraphics(x, y, x0, x1, y0, y1);
  }
}

// Fixed-camera rooms stop at their bounds, which left black bands beside
// small rooms. The cells of those bands, which belong to other parts of the
// 512-pixel room or to nothing, draw the room's void instead: the 16x16
// block (BG1 and BG2) most common along the edge of its bounds, which is
// what surrounds the room (the dark of a house, the sand outside a tower).
// Sprites wholly outside the room are hidden for the frame, so enemies of
// the next quadrant do not appear on the void. Like the tile repair above,
// VRAM and OAM are restored after rendering.
typedef struct WideVoidBlock { uint16 cell[2][4]; } WideVoidBlock;
static struct {
  bool active;
  int key[5];
  bool cached_ok;
  WideVoidBlock block;
  uint16 oam_index[128], oam_value[128];
  unsigned oam_count;
} g_wide_void;

static void InvalidateWideRoomVoid(void) {
  g_wide_void.cached_ok = false;
}

static bool WideVoidBlockAt(const WideRegion *r, const uint16 *maps, int x, int y,
                            WideVoidBlock *b) {
  for (int layer = 0; layer < 2; layer++)
    for (int i = 0; i < 4; i++) {
      int v = WideTileTruth(r, maps, layer, x + (i & 1) * 8, y + (i >> 1) * 8, NULL);
      if (v < 0)
        return false;
      b->cell[layer][i] = (uint16)v;
    }
  return true;
}

static bool WideFindRoomVoid(int bx0, int by0, int bx1, int by1, WideVoidBlock *out) {
  const uint16 *maps = (const uint16 *)(g_ram + 0x2000);
  static WideVoidBlock seen[256];
  static uint16 count[256];
  unsigned n = 0;
  for (int side = 0; side < 4; side++) {
    bool horizontal = side < 2;
    int fixed = side == 0 ? by0 : side == 1 ? by1 - 16 : side == 2 ? bx0 : bx1 - 16;
    int from = horizontal ? bx0 : by0, to = horizontal ? bx1 : by1;
    for (int t = from; t + 16 <= to; t += 16) {
      WideVoidBlock b;
      if (!WideVoidBlockAt(&g_wide_live, maps, horizontal ? t : fixed,
                           horizontal ? fixed : t, &b))
        continue;
      unsigned i = 0;
      while (i < n && memcmp(&seen[i], &b, sizeof(b)))
        i++;
      if (i == n) {
        if (n == countof(seen))
          continue;
        seen[n] = b, count[n++] = 0;
      }
      count[i]++;
    }
  }
  if (!n)
    return false;
  unsigned best = 0;
  for (unsigned i = 1; i < n; i++)
    if (count[i] > count[best])
      best = i;
  *out = seen[best];
  return true;
}

// A continuous floor along at least 32 pixels at the camera boundary is an
// open seam, rather than a door. Sample both sides; walls/pits/closed doors
// retain the room's own surround. This also covers internal quadrant edges.
static bool WideOpenSeam(const uint16 *a, int ax, const uint16 *b, int bx,
                         int y0, int y1, const WideVoidBlock *surround) {
  int run = 0;
  for (int y = IntMax(y0, 0); y < IntMin(y1, 512); y += 8) {
    int ta = a[(y >> 3) * 64 + ax] & 1023;
    int tb = b[(y >> 3) * 64 + bx] & 1023;
    bool void_tile=false;
    if(surround)for(int i=0;i<4;i++)
      void_tile|=a[(y>>3)*64+ax]==surround->cell[1][i];
    if (!void_tile && attributes_for_tile[ta] == 0 && attributes_for_tile[tb] == 0) {
      if (++run >= 4) return true;
    } else run = 0;
  }
  return false;
}

static void UpdatePrivateRoomFade(void) {
  if(!WideFixedCameraEnabled()){memset(&g_private_fade,0,sizeof(g_private_fade));return;}
  int direction,axis=GetWideTransition(7,&direction);
  bool closed=false;
  if(main_module_index==7&&submodule_index==1&&(uint8)cur_palace_index_x2==0xff&&axis) {
    const uint16 *maps=(const uint16*)(g_ram+0x2000);int run=0;
    closed=true;
    for(int i=0;i<64;i++) {
      int a=axis==1?i*64+31:31*64+i,b=axis==1?i*64+32:32*64+i;
      if(attributes_for_tile[maps[a]&1023]==0&&attributes_for_tile[maps[b]&1023]==0) {
        if(++run>=4){closed=false;break;}
      } else run=0;
    }
  }
  if(closed) {
    int current=axis==1?BG2HOFS_copy2:BG2VOFS_copy2;
    int end=subsubmodule_index>=4?current:GetWideTransitionEnd(7,axis,direction,current);
    // Native room scrolls advance four pixels. The general two-pixel camera
    // estimator can stop two pixels early when travelling up or left.
    end=(end&~3)|(current&2);
    // At the last scroll coordinate the target equals current, while the
    // native landing phase is still in flight. Keep the established source.
    if(!g_private_fade.active) {
      g_private_fade.axis=axis;g_private_fade.end=end;
      g_private_fade.source=end-direction*256;
    }
    g_private_fade.active=true;g_private_fade.reveal=false;
    int moved=(current-g_private_fade.source)*direction;
    g_private_fade.light=(unsigned)IntMax(0,15-IntMax(moved,0)*15/128);
  } else {
    if(g_private_fade.active){g_private_fade.reveal=true;g_private_fade.tick=frame_counter;}
    g_private_fade.active=false;g_private_fade.light=15;
    if(g_private_fade.reveal) {
      unsigned age=(uint8)(frame_counter-g_private_fade.tick);
      if(age>=18)g_private_fade.reveal=false;
      else g_private_fade.light=age*15/18;
    }
  }
}

static unsigned WideRoomOpenSides(const WideRegion *r, const uint16 *maps,
                                  int y0, int y1) {
  if (r->context != 7 || !r->private_room) return 0;
  unsigned open = 0;
  const int edges[2] = {r->room_left, r->room_right};
  for (int side = 0; side < 2; side++) {
    int seam = edges[side];
    if (seam > 0 && seam < 512 &&
        WideOpenSeam(maps, (seam - 1) >> 3, maps, seam >> 3, y0 - r->y, y1 - r->y, NULL))
      open |= 1u << side;
  }
  return open;
}

static bool WideRoomTileVisible(const WideRegion *r, unsigned open, int x, int y) {
  if (r->context != 7 || !WideRegionHas(r, x, y)) return false;
  if (!r->private_room) return true;
  int rx = (int16)(x - r->x);
  if (rx >= r->room_left && rx < r->room_right) return true;
  return (open & (rx < r->room_left ? 1 : 2)) != 0;
}

static const uint16 *WideNeighborMaps(int room) {
  if (room < 0 || room >= 0x140) return NULL;
  for (int i = 0; i < 4; i++)
    if (g_wide_rooms.room[i] == room + 1) {
      g_wide_rooms.pinned |= 1u << i;
      return g_wide_rooms.valid[i] ? g_wide_rooms.maps[i] : NULL;
    }
  if(g_wide_rooms.pinned==15)return NULL;
  int i;do {i=g_wide_rooms.next++&3;}while(g_wide_rooms.pinned&(1u<<i));
  g_wide_rooms.pinned|=1u<<i;
  g_wide_rooms.room[i] = room + 1;
  g_wide_rooms.valid[i] = Dungeon_DecodeRoomMaps((uint16)room, g_wide_rooms.maps[i]);
  return g_wide_rooms.valid[i] ? g_wide_rooms.maps[i] : NULL;
}

static void WideHideSpritesOutside(int left, int right, unsigned live_open, unsigned prev_open) {
  static const uint8 kSizes[8][2] = {
    {8, 16}, {8, 32}, {8, 64}, {16, 32}, {16, 64}, {32, 64}, {16, 32}, {16, 32},
  };
  Ppu *p = g_zenv.ppu;
  for (unsigned index = 0; index < 256; index += 2) {
    uint16 word = p->oam[index];
    if ((word >> 8) == 0xf0)
      continue;
    unsigned high = p->oam[0x100 + (index >> 4)] >> (index & 15);
    int size = kSizes[p->objSize & 7][(high >> 1) & 1];
    // As the renderers place it: 9-bit X, shifted with the drawn camera.
    int x = (word & 255) + (high & 1) * 256 + g_wide_picture_shift;
    x = ((x + 256 - p->extraLeftRight) & 511) - 256 + p->extraLeftRight;
    if (x + size > left && x < right) {
      int wx = BG2HOFS_copy2 + x - g_wide_picture_shift;
      int sy = word >> 8;
      if (sy >= 240) sy -= 256;
      int wy = BG2VOFS_copy2 + sy;
      bool native = wx + size > BG2HOFS_copy2 && wx < BG2HOFS_copy2 + 256;
      bool shown = native;
      for (int end = 0; end < 2 && !shown; end++) {
        int sx = wx + (end ? size - 1 : 0);
        shown = WideRoomTileVisible(&g_wide_live, live_open, sx, wy) ||
                WideRoomTileVisible(&g_wide_prev, prev_open, sx, wy);
      }
      if (shown) continue;
    }
    unsigned n = g_wide_void.oam_count;
    g_wide_void.oam_index[n] = (uint16)index;
    g_wide_void.oam_value[n] = word;
    g_wide_void.oam_count = n + 1;
    p->oam[index] = (word & 0xff) | 0xf000;
  }
}

static void BeginWideRoomVoid(const WideFrame *f) {
  g_wide_void.active = false;
  g_wide_void.oam_count = 0;
  Ppu *p = g_zenv.ppu;
  if (f->context != 7 || !f->fixed_camera || p->mode != 1 || !p->extraLeftRight ||
      WideLanternRoom(7) ||
      (f->settled && f->view.left >= p->extraLeftRight && f->view.right >= p->extraLeftRight))
    return;
  if (!g_wide_live.context)
    g_wide_live = WideRegionFromGame(f->context);
  if (g_wide_live.context != 7)
    return;
  if (g_wide_live.room == dungeon_room_index)
    WideRoomUpdateSpan(&g_wide_live);
  const BgLayer *room_bg = &p->bgLayer[1];
  if (!room_bg->tilemapWider || !room_bg->tilemapHigher)
    return;
  int shake_x = (int)((room_bg->hScroll - BG2HOFS_copy2 + 256) & 511) - 256;
  int shake_y = (int)((room_bg->vScroll - BG2VOFS_copy2 + 256) & 511) - 256;
  if (abs(shake_x) > 16 || abs(shake_y) > 16) return;
  int qx = quadrant_fullsize_x >> 1, qy = quadrant_fullsize_y >> 1;
  int bx0 = room_bounds_x.v[qx] & ~15, bx1 = (room_bounds_x.v[qx + 2] + 256 + 15) & ~15;
  int by0 = room_bounds_y.v[qy] & ~15, by1 = (room_bounds_y.v[qy + 2] + 224 + 15) & ~15;
  // The game changes the destination index/bounds before rebuilding WRAM.
  // Until that load, use the bounds of the geometry actually retained.
  if (bx0 < g_wide_live.x || bx1 > g_wide_live.x + 512 ||
      by0 < g_wide_live.y || by1 > g_wide_live.y + 512) {
    bx0 = g_wide_live.x; bx1 = bx0 + 512;
    by0 = g_wide_live.y; by1 = by0 + 512;
  }
  int key[5] = { g_wide_live.room, bx0, by0, bx1, by1 };
  if (memcmp(key, g_wide_void.key, sizeof(key)) || !g_wide_void.cached_ok) {
    memcpy(g_wide_void.key, key, sizeof(key));
    g_wide_void.cached_ok = WideFindRoomVoid(bx0, by0, bx1, by1, &g_wide_void.block);
  }
  if (!g_wide_void.cached_ok)
    return;
  // Use the PPU sampling origin, including sub-tile shake/scroll latency.
  int x = BG2HOFS_copy2 + shake_x, y = BG2VOFS_copy2 + shake_y;
  int visual = x - f->view.offset_x;
  int in0 = visual - f->view.left, in1 = visual + 256 + f->view.right;
  int out0 = visual - p->extraLeftRight, out1 = visual + 256 + p->extraLeftRight;
  int y0 = y + 1 - f->view.delta_y, y1 = y0 + (f->rows ? 240 : 224);
  const uint16 *live = (const uint16 *)(g_ram + 0x2000);
  unsigned live_open = WideRoomOpenSides(&g_wide_live, live, y0, y1);
  unsigned prev_open = WideRoomOpenSides(&g_wide_prev, g_wide_prev_maps, y0, y1);
  g_wide_rooms.pinned=0; // retain all neighbor pointers for this picture
  const uint16 *connected[2][2] = {{NULL}};
  const WideRegion *owners[2] = {&g_wide_live,&g_wide_prev};
  const uint16 *owner_maps[2] = {live,g_wide_prev_maps};
  WideRegion region[2][2];
  for(int owner=0;owner<2;owner++)for(int side=0;side<2;side++) {
    const WideRegion *r=owners[owner];const uint16 *maps=owner_maps[owner];
    region[owner][side]=*r;
    if(r->context!=7)continue;
    int boundary=side?r->room_right:r->room_left;
    if(boundary!=0&&boundary!=512)continue;
    if((side&&(r->room&15)==15)||(!side&&(r->room&15)==0))continue;
    int ax=side?63:0,bx=side?0:63;
    // Connectivity belongs to the room, not the moving viewport. A doorway
    // going off screen during a vertical scroll must not remove its neighbor.
    int first=r->private_room?y0-r->y:0,last=r->private_room?y1-r->y:512;
    if(!WideOpenSeam(maps,ax,maps,ax,first,last,&g_wide_void.block))continue;
    const uint16 *neighbor=WideNeighborMaps(r->room+(side?1:-1));
    if(neighbor&&WideOpenSeam(maps,ax,neighbor,bx,first,last,&g_wide_void.block)) {
      connected[owner][side]=neighbor;region[owner][side].x+=side?512:-512;
    }
  }
  unsigned restore = g_wide_tile_repair.count;
  for (int layer = 0; layer < 2; layer++) {
    const BgLayer *bg = &p->bgLayer[layer];
    if (!bg->tilemapWider || !bg->tilemapHigher) continue;
    int dx = (bg->hScroll - room_bg->hScroll) & 511;
    int dy = (bg->vScroll - room_bg->vScroll) & 511;
    bool aligned = layer == 1 || (BG1HOFS_copy2 == BG2HOFS_copy2 && BG1VOFS_copy2 == BG2VOFS_copy2);
    for (int ty = y0 & ~7; ty < y1; ty += 8)
      for (int tx = out0 & ~7; tx < out1; tx += 8) {
        if (tx + 8 > x && tx < x + 256 && !g_private_fade.active)
          continue;  // preserve the game's original picture, including doors
        unsigned parity = (((ty - by0) >> 3) & 1) * 2 + (((tx - bx0) >> 3) & 1);
        int value = -1, side = tx < x ? 0 : 1;
        bool preview = false;
        // During scrolls the two retained live maps take priority over static
        // previews, preserving opened chests and changed room tiles.
        if (aligned) {
          if (WideRoomTileVisible(&g_wide_live, live_open, tx, ty))
            value = WideTileTruth(&g_wide_live, live, layer, tx, ty, NULL);
          if (value < 0 && WideRoomTileVisible(&g_wide_prev, prev_open, tx, ty))
            value = WideTileTruth(&g_wide_prev, g_wide_prev_maps, layer, tx, ty, NULL);
          // An external open passage may extend beyond both retained maps.
          for(int owner=0;owner<2&&value<0;owner++)if(connected[owner][side]) {
            value=WideTileTruth(&region[owner][side],connected[owner][side],layer,tx,ty,NULL);
            preview=value>=0;
          }
        }
        // Tile repair already supplied visible geometry inside the moving
        // view. Only missing geometry receives the repeating surround.
        if (value >= 0 && !preview && tx + 8 > in0 && tx < in1) continue;
        if (value < 0) value = g_wide_void.block.cell[layer][parity];
        if (!WidePatchVram(WideMapAddress(bg, tx + dx, ty + dy), (uint16)value)) {
          // Out of room: undo this pass and keep the black bands.
          while (g_wide_tile_repair.count > restore) {
            unsigned n = --g_wide_tile_repair.count;
            p->vram[g_wide_tile_repair.address[n]] = g_wide_tile_repair.value[n];
          }
          return;
        }
      }
  }
  g_wide_void.active = true;
  WideHideSpritesOutside(-p->extraLeftRight, 256 + p->extraLeftRight, live_open, prev_open);
}

static bool WideRoomVoidActive(void) {
  return g_wide_void.active;
}

static void EndWideTileRepair(void) {
  g_wide_tile_repair.last_count = g_wide_tile_repair.count;
  while (g_wide_tile_repair.count) {
    unsigned n = --g_wide_tile_repair.count;
    g_zenv.ppu->vram[g_wide_tile_repair.address[n]] = g_wide_tile_repair.value[n];
  }
  while (g_wide_void.oam_count) {
    unsigned n = --g_wide_void.oam_count;
    g_zenv.ppu->oam[g_wide_void.oam_index[n]] = g_wide_void.oam_value[n];
  }
}

uint32 ZeldaGetWideColumnRepairCount(void) {
  return g_wide_tile_repair.last_count;
}

static bool QolBrightRoomActive(void) {
  int module=main_module_index==14?saved_module_for_menu:main_module_index;
  bool transition=module!=7||submodule_index!=0;
  bool indoor_scene=module==7||module==6||module==8||module==15||module==16||module==17;
  return (enhanced_features1 & kFeatures1_QolBrightRooms) && player_is_indoors && indoor_scene &&
    (hdr_dungeon_dark_with_lantern || dung_want_lights_out || (transition&&dung_want_lights_out_copy));
}

static void ZeldaDrawPpuLines(Ppu *ppu, int height,
                              int first_line, int last_line,
                              uint8 irq_state) {
  SimpleHdma hdma_chans[2];
  SimpleHdma_Init(&hdma_chans[0], &g_zenv.dma->channel[6]);
  SimpleHdma_Init(&hdma_chans[1], &g_zenv.dma->channel[7]);

  for (int i = 0; i <= height; i++) {
    if (i == (LanguageCart_Japanese() ? 0x38 : 128) && irq_state) {  // $80:8206
      ppu_write(ppu, (uint8)BG3HOFS, selectfile_var8);
      ppu_write(ppu, (uint8)BG3HOFS, selectfile_var8 >> 8);
      ppu_write(ppu, (uint8)BG3VOFS, 0);
      ppu_write(ppu, (uint8)BG3VOFS, 0);
    }
    if (i >= first_line && i <= last_line) {
      // Presentation-only accessibility: retain the exact dungeon lighting
      // state and HDMA. Temporarily remove its darkening/window for this line.
      bool bright=QolBrightRoomActive();
      uint8 main=ppu->screenEnabled[0],window=ppu->screenWindowed[0];
      uint8 clip=ppu->clipMode,prevent=ppu->preventMathMode,math=ppu->mathEnabled;
      if(bright){
        ppu->screenEnabled[0]|=ppu->screenEnabled[1];
        if(hdr_dungeon_dark_with_lantern || ((ppu->screenEnabled[1]&1) && ppu->subtractColor))
          ppu->screenEnabled[0]&=~1; // Includes the retained mask after the header changes.
        ppu->screenWindowed[0]=0;
        ppu->clipMode=ppu->preventMathMode=ppu->mathEnabled=0;
      }
      ppu_runLine(ppu, i);
      if(bright){
        ppu->screenEnabled[0]=main;ppu->screenWindowed[0]=window;
        ppu->clipMode=clip;ppu->preventMathMode=prevent;ppu->mathEnabled=math;
      }
    }
    SimpleHdma_DoLine(&hdma_chans[0], ppu);
    SimpleHdma_DoLine(&hdma_chans[1], ppu);
  }
}

#ifdef __3DS__
static bool ZeldaTryGpuPpu(Ppu *ppu, int height, uint8 irq_state) {
  if (!PpuGpuBegin(ppu, height)) return false;
  SimpleHdma chans[2];
  SimpleHdma_Init(&chans[0], &g_zenv.dma->channel[6]);
  SimpleHdma_Init(&chans[1], &g_zenv.dma->channel[7]);
  for (int i=0; i<=height; i++) {
    if (i==(LanguageCart_Japanese() ? 0x38 : 128) && irq_state) {
      ppu_write(ppu, (uint8)BG3HOFS, selectfile_var8);
      ppu_write(ppu, (uint8)BG3HOFS, selectfile_var8 >> 8);
      ppu_write(ppu, (uint8)BG3VOFS, 0);
      ppu_write(ppu, (uint8)BG3VOFS, 0);
    }
    if (i) PpuGpuLine(ppu, i-1);
    SimpleHdma_DoLine(&chans[0], ppu);
    SimpleHdma_DoLine(&chans[1], ppu);
  }
  return PpuGpuFinish(ppu);
}
#endif

#ifdef __3DS__
typedef struct PpuWorkerState {
  Ppu ppu;
  PpuTileCache tile_cache;
  int height;
  int first_line;
  int last_line;
  uint8 irq_state;
  uint64 duration_ticks;
  LightEvent start;
  LightEvent done;
  Thread thread;
  bool running;
} PpuWorkerState;

static PpuWorkerState g_ppu_system_worker;
static PpuWorkerState g_ppu_new_worker;
static bool g_ppu_worker_initialized;
static int g_ppu_split_line = 112;
static int g_ppu_last_split_line = 112;
static int g_ppu_old3ds_worker_lines = 56;
static int g_ppu_old3ds_last_worker_lines = 56;
static uint64 g_ppu_main_duration_ticks;
static uint32 g_ppu_join_us;
uint32 ZeldaGetPpuJoinTimeUs(void) { return g_ppu_join_us; }

static void ZeldaPpuWorkerMain(void *argument) {
  PpuWorkerState *state = (PpuWorkerState *)argument;
  for (;;) {
    LightEvent_Wait(&state->start);
    // Pair the job publication fence with an acquire before reading the PPU
    // pointer and line range. LightEvent provides the wakeup; these fences
    // make the data handoff explicit to both the compiler and the ARM CPU.
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (!__atomic_load_n(&state->running, __ATOMIC_ACQUIRE))
      break;
    uint64 start = svcGetSystemTick();
    ZeldaDrawPpuLines(&state->ppu, state->height,
                      state->first_line, state->last_line,
                      state->irq_state);
    state->duration_ticks = svcGetSystemTick() - start;
    LightEvent_Signal(&state->done);
  }
}

static bool ZeldaCreatePpuWorker(PpuWorkerState *state,
                                 int core, s32 priority) {
  LightEvent_Init(&state->start, RESET_ONESHOT);
  LightEvent_Init(&state->done, RESET_ONESHOT);
  state->running = true;
  state->thread = threadCreate(
    ZeldaPpuWorkerMain, state, 32 * 1024, priority, core, false);
  if (!state->thread) {
    state->running = false;
    return false;
  }
  return true;
}

static int ZeldaEnsurePpuWorkers(void) {
  if (g_ppu_worker_initialized)
    return (g_ppu_system_worker.thread != NULL) +
           (g_ppu_new_worker.thread != NULL);
  g_ppu_worker_initialized = true;

  bool is_new_3ds = false;
  APT_CheckNew3DS(&is_new_3ds);
  s32 priority = 0x30;
  svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);

  bool can_use_core1 = Platform3DS_CanUseCore1PpuWorker();
  bool system_worker = can_use_core1 &&
    ZeldaCreatePpuWorker(&g_ppu_system_worker, 1, priority);
  bool new_worker = is_new_3ds &&
    ZeldaCreatePpuWorker(&g_ppu_new_worker, 2, priority);
  int count = system_worker + new_worker;
  if (count == 0) {
    Platform3DS_LogRuntime(
      "PPU workers: unavailable, using sequential renderer");
  } else {
    Platform3DS_LogRuntime(
      "PPU workers: Core 1=%s, Core 2=%s",
      system_worker ? "enabled" :
      (can_use_core1 ? "unavailable" : "disabled/no budget"),
      new_worker ? "enabled" : "unavailable");
  }
  return count;
}

void ZeldaShutdownPpuWorker(void) {
  PpuWorkerState *workers[] = {
    &g_ppu_system_worker, &g_ppu_new_worker,
  };
  for (size_t i = 0; i < countof(workers); i++) {
    PpuWorkerState *state = workers[i];
    if (!state->thread)
      continue;
    __atomic_store_n(&state->running, false, __ATOMIC_RELEASE);
    LightEvent_Signal(&state->start);
    Result join_result = threadJoin(state->thread, UINT64_MAX);
    if (R_FAILED(join_result))
      Platform3DS_LogRuntime("WARNING: PPU worker join timeout: 0x%08lx",
                             (unsigned long)join_result);
    threadFree(state->thread);
    state->thread = NULL;
  }
  g_ppu_worker_initialized = false;
  g_ppu_split_line = g_ppu_last_split_line = 112;
  g_ppu_old3ds_worker_lines = g_ppu_old3ds_last_worker_lines = 56;
  g_ppu_main_duration_ticks = 0;
  g_ppu_join_us = 0;
  g_ppu_system_worker.duration_ticks = g_ppu_new_worker.duration_ticks = 0;
}

bool ZeldaGetPpuWorkerStats(int *split_line,
                            uint32 *main_time_us,
                            uint32 *worker_time_us) {
  if (PpuGpuOutputActive()) {
    if (split_line) *split_line=0;
    if (main_time_us) *main_time_us=0;
    if (worker_time_us) *worker_time_us=0;
    return false;
  }
  if (!g_ppu_system_worker.thread && !g_ppu_new_worker.thread)
    return false;
  if (split_line)
    *split_line = g_ppu_last_split_line;
  if (main_time_us) {
    *main_time_us = (uint32)(
      g_ppu_main_duration_ticks * 1000000ull / SYSCLOCK_ARM11);
  }
  if (worker_time_us) {
    uint64 worker_ticks = g_ppu_system_worker.duration_ticks;
    if (g_ppu_new_worker.duration_ticks > worker_ticks)
      worker_ticks = g_ppu_new_worker.duration_ticks;
    *worker_time_us = (uint32)(
      worker_ticks * 1000000ull / SYSCLOCK_ARM11);
  }
  return true;
}

static int ZeldaOld3DSChooseWorkerLines(int height) {
  const int min_worker_lines = 24;
  const int max_worker_lines = height / 3;
  int worker_lines = g_ppu_old3ds_worker_lines;

  if (g_ppu_main_duration_ticks != 0 &&
      g_ppu_system_worker.duration_ticks != 0 &&
      g_ppu_old3ds_last_worker_lines > 0) {
    int previous_worker_lines = g_ppu_old3ds_last_worker_lines;
    int previous_main_lines = height - previous_worker_lines;
    uint64 main_per_line =
      g_ppu_main_duration_ticks / (uint64)IntMax(previous_main_lines, 1);
    uint64 worker_per_line =
      g_ppu_system_worker.duration_ticks /
      (uint64)IntMax(previous_worker_lines, 1);

    if (main_per_line != 0 && worker_per_line != 0) {
      uint64 wanted =
        (uint64)height * main_per_line / (main_per_line + worker_per_line);
      int target = (int)wanted;
      target = IntMin(IntMax(target, min_worker_lines), max_worker_lines);

      if (target > worker_lines + 4)
        worker_lines += 4;
      else if (target < worker_lines - 4)
        worker_lines -= 4;
      else
        worker_lines = target;
    }
  }

  worker_lines = IntMin(IntMax(worker_lines, min_worker_lines),
                        max_worker_lines);
  g_ppu_old3ds_worker_lines = worker_lines;
  g_ppu_old3ds_last_worker_lines = worker_lines;
  return worker_lines;
}
#else
uint32 ZeldaGetPpuJoinTimeUs(void) { return 0; }
void ZeldaShutdownPpuWorker(void) {
}

bool ZeldaGetPpuWorkerStats(int *split_line,
                            uint32 *main_time_us,
                            uint32 *worker_time_us) {
  (void)split_line;
  (void)main_time_us;
  (void)worker_time_us;
  return false;
}
#endif

#ifdef __3DS__
static PpuPhaseProfile g_ppu_phase_main, g_ppu_phase_worker;
static uint32 g_ppu_phase_scene;
static int g_ppu_phase_split;
#endif

void ZeldaDrawPpuFrame(uint8 *pixel_buffer, size_t pitch, uint32 render_flags) {
  SimpleHdma hdma_probe;
#ifdef __3DS__
  NativeFileLabels_Apply(g_zenv.vram, main_module_index, g_config.language, g_loc_rom != NULL);
  g_zenv.ppu->wideUi = PpuUiChooseSceneLayout(main_module_index, submodule_index, overworld_map_state);
  g_zenv.ppu->wideUiSaveX = (text_msgbox_topleft & 31) * 8;
  g_zenv.ppu->wideUiSaveY = ((text_msgbox_topleft >> 5) & 31) * 8;
#endif
  UpdatePrivateRoomFade();
  uint8 original_brightness = g_zenv.ppu->brightness;
  if(QolBrightRoomActive())g_zenv.ppu->brightness=original_brightness*12/15;
  if(g_private_fade.active||g_private_fade.reveal)g_zenv.ppu->brightness=g_zenv.ppu->brightness*g_private_fade.light/15;
  WideFrame wide = BeginWideFrame(render_flags);
  BeginWideTileRepair(&wide);
  g_wide_picture_shift = wide.fixed_camera ? wide.view.offset_x : 0;
  BeginWideRoomVoid(&wide);
  g_wide_render_flags = render_flags;
  g_wide_picture_rows = WideSpriteRows(&wide);

#ifdef __3DS__
  bool gpu_candidate = (render_flags & kPpuRenderFlags_Old3DS) &&
    !QolBrightRoomActive() && PpuGpuCanAttempt();
  if (gpu_candidate) {
    // The GPU consumes register/tile/OAM state; do not rasterize or rebuild
    // E11's software background cache on a GPU frame.
    g_zenv.ppu->renderFlags = render_flags;
    g_zenv.ppu->renderPitch = pitch;
    g_zenv.ppu->renderBuffer = pixel_buffer;
    g_zenv.ppu->renderObjXOffset = 0;
    g_zenv.ppu->renderObjYOffset = 0;
    g_zenv.ppu->phase.active = false;
  } else
#endif
    PpuBeginDrawing(g_zenv.ppu, pixel_buffer, pitch, render_flags);

  dma_startDma(g_zenv.dma, HDMAEN_copy, true);
  SimpleHdma_Init(&hdma_probe, &g_zenv.dma->channel[6]);

  // Cheat: Let the PPU impl know about the hdma perspective correction so it can avoid guessing.
  if ((render_flags & kPpuRenderFlags_4x4Mode7) && g_zenv.ppu->mode == 7) {
    if (hdma_probe.table == kMapModeHdma0)
      PpuSetMode7PerspectiveCorrection(g_zenv.ppu, kMapMode_Zooms1[0], kMapMode_Zooms1[223]);
    else if (hdma_probe.table == kMapModeHdma1)
      PpuSetMode7PerspectiveCorrection(g_zenv.ppu, kMapMode_Zooms2[0], kMapMode_Zooms2[223]);
    else if (hdma_probe.table == kAttractIndirectHdmaTab)
      PpuSetMode7PerspectiveCorrection(g_zenv.ppu, hdma_table_dynamic[0], hdma_table_dynamic[223]);
    else
      PpuSetMode7PerspectiveCorrection(g_zenv.ppu, 0, 0);
  }

  WideRenderState wide_render = BeginWideRender(&wide);

  if (g_zenv.ppu->extraLeftRight != 0 || render_flags & kPpuRenderFlags_Height240)
    ConfigurePpuSideSpace(&wide);

  PpuSetWindow1Ext(g_zenv.ppu, g_spotlight_ext_active ? g_spotlight_ext_left : NULL,
                   g_spotlight_ext_active ? g_spotlight_ext_right : NULL);

  int height = render_flags & kPpuRenderFlags_Height240 ? 240 : 224;
  uint8 irq_state = irq_flag;

#ifdef __3DS__
  if (gpu_candidate) {
    if (ZeldaTryGpuPpu(g_zenv.ppu, height, irq_state)) {
      g_ppu_join_us = 0;
      goto rendering_complete;
    }
    int obj_offset = g_zenv.ppu->renderObjXOffset;
    int obj_y_offset = g_zenv.ppu->renderObjYOffset;
    PpuBeginDrawing(g_zenv.ppu, pixel_buffer, pitch, render_flags);
    g_zenv.ppu->renderObjXOffset = obj_offset;
    g_zenv.ppu->renderObjYOffset = obj_y_offset;
  }
  if (render_flags & kPpuRenderFlags_Old3DS) PpuGpuCpuFrame();
  if (!g_rendering_startup_logo && ZeldaEnsurePpuWorkers()) {
    PpuWorkerState *system_worker = &g_ppu_system_worker;
    PpuWorkerState *new_worker = &g_ppu_new_worker;
    int main_first = 1;
    int main_last = height;

    if (system_worker->thread && new_worker->thread) {
      int system_last = height * 3 / 28;
      main_first = system_last + 1;
      main_last = (height + system_last) / 2;
      system_worker->first_line = 1;
      system_worker->last_line = system_last;
      new_worker->first_line = main_last + 1;
      new_worker->last_line = height;
    } else if (new_worker->thread) {
      main_last = IntMin(IntMax(g_ppu_split_line, 48), height - 48);
      new_worker->first_line = main_last + 1;
      new_worker->last_line = height;
    } else {
      int worker_lines = ZeldaOld3DSChooseWorkerLines(height);
      main_last = height - worker_lines;
      system_worker->first_line = main_last + 1;
      system_worker->last_line = height;
    }
    g_ppu_last_split_line = main_last;

    PpuWorkerState *workers[] = { system_worker, new_worker };
    for (size_t i = 0; i < countof(workers); i++) {
      PpuWorkerState *state = workers[i];
      if (!state->thread)
        continue;
      // The fast paths read the caches appended after the original span
      // (sprite lines, retained planes); without them that span is enough.
      size_t snapshot_size = (render_flags & (kPpuRenderFlags_Old3DS | kPpuRenderFlags_FastPaths)) ?
        sizeof(Ppu) : offsetof(Ppu, spriteLines);
      memcpy(&state->ppu, g_zenv.ppu, snapshot_size);
      state->ppu.tileCache = &state->tile_cache;
      state->height = height;
      state->irq_state = irq_state;
      __atomic_thread_fence(__ATOMIC_RELEASE);
      LightEvent_Signal(&state->start);
    }

    uint64 main_start = svcGetSystemTick();
    ZeldaDrawPpuLines(g_zenv.ppu, height,
                      main_first, main_last, irq_state);
    g_ppu_main_duration_ticks = svcGetSystemTick() - main_start;
    uint64 join_start = svcGetSystemTick();
    for (size_t i = 0; i < countof(workers); i++) {
      if (workers[i]->thread)
        LightEvent_Wait(&workers[i]->done);
    }
    g_ppu_join_us = (uint32)((svcGetSystemTick() - join_start) * 1000000ull / SYSCLOCK_ARM11);
  } else
#endif
  {
    ZeldaDrawPpuLines(g_zenv.ppu, height, 1, height, irq_state);
  }

#ifdef __3DS__
rendering_complete:
#endif
  if (irq_state & 0x80) {
    irq_flag = 0;
    zelda_snes_dummy_write(NMITIMEN, 0x81);
  }
#ifdef __3DS__
  if ((render_flags & kPpuRenderFlags_Old3DS) && g_zenv.ppu->phase.active) {
    // Workers are joined; these snapshots are read only by the game thread.
    g_ppu_phase_main = g_zenv.ppu->phase;
    memset(&g_ppu_phase_worker, 0, sizeof(g_ppu_phase_worker));
    if (g_ppu_system_worker.thread)
      g_ppu_phase_worker = g_ppu_system_worker.ppu.phase;
    g_ppu_phase_split = g_ppu_system_worker.thread ? g_ppu_last_split_line : height;
    g_ppu_phase_scene = (main_module_index << 24) | (player_is_indoors << 23) |
      (player_is_indoors ? dungeon_room_index : overworld_area_index);
  }
#endif
  EndWideTileRepair();
  EndWideRender(&wide_render);
  g_zenv.ppu->brightness=original_brightness;
}

void HdmaSetup(uint32 addr6, uint32 addr7, uint8 transfer_unit, uint8 reg6, uint8 reg7, uint8 indirect_bank) {
  Dma *dma = g_zenv.dma;
  if (addr6) {
    dma_write(dma, DMAP6, transfer_unit);
    dma_write(dma, BBAD6, reg6);
    dma_write(dma, A1T6L, addr6);
    dma_write(dma, A1T6H, addr6 >> 8);
    dma_write(dma, A1B6, addr6 >> 16);
    dma_write(dma, DAS60, indirect_bank);
  }
  dma_write(dma, DMAP7, transfer_unit);
  dma_write(dma, BBAD7, reg7);
  dma_write(dma, A1T7L, addr7);
  dma_write(dma, A1T7H, addr7 >> 8);
  dma_write(dma, A1B7, addr7 >> 16);
  dma_write(dma, DAS70, indirect_bank);
}

static void ZeldaInitializationCode() {
  zelda_snes_dummy_write(NMITIMEN, 0);
  zelda_snes_dummy_write(HDMAEN, 0);
  zelda_snes_dummy_write(MDMAEN, 0);

  Sound_LoadIntroSongBank();

  Startup_InitializeMemory();

  animated_tile_data_src = 0xa680;
  dma_source_addr_9 = 0xb280;
  dma_source_addr_14 = 0xb280 + 0x60;
  zelda_snes_dummy_write(NMITIMEN, 0x81);
}

static void ClearOamBuffer() {  // 80841e
  for (int i = 0; i < 128; i++)
    oam_buf[i].y = 0xf0;
}

static void ZeldaRunGameLoop() {
  frame_counter++;
  g_spotlight_ext_active = false;
  ClearOamBuffer();
  Rando_TickTrapEffects();
  Rando_TickDiscovery();
  Module_MainRouting();
  Rando_TickGoalCompletion();
  Hud_RandoDrawTrackers();
  NMI_PrepareSprites();
  nmi_boolean = 0;
}

void ZeldaInitialize() {
  // ROM reselection re-enters here in the same process. Reset volatile RAM
  // with the new PPU/APU so the first frame runs the normal boot sequence.
  // Saved SRAM is loaded separately by ZeldaReadSram after initialization.
  memset(g_ram, 0, sizeof(g_ram));
  g_zenv.dma = dma_init(NULL);
  g_zenv.ppu = ppu_init(NULL);
  g_zenv.ram = g_ram;
  g_zenv.sram = (uint8*)calloc(8192, 1);
  g_zenv.vram = g_zenv.ppu->vram;
  g_zenv.player = SpcPlayer_Create();
  ZeldaResetAudio_Locked();
  ResetWideTracking();
  dma_reset(g_zenv.dma);
  ppu_reset(g_zenv.ppu);
}

static void ZeldaRunPolyLoop() {
  if (intro_did_run_step && !nmi_flag_update_polyhedral) {
    Poly_RunFrame();
    intro_did_run_step = 0;
    nmi_flag_update_polyhedral = 0xff;
  }
}

void ZeldaRunFrameInternal(uint16 input, int run_what) {
  if (animated_tile_data_src == 0)
    ZeldaInitializationCode();

  if (run_what & 2)
    ZeldaRunPolyLoop();
  if (run_what & 1)
    ZeldaRunGameLoop();
  Interrupt_NMI(input);
}


static int IncrementCrystalCountdown(uint8 *a, int v) {
  int t = *a + v;
  *a = t;
  return t >> 8;
}

int frame_ctr_dbg;
static uint8 *g_emu_memory_ptr;
static ZeldaRunFrameFunc *g_emu_runframe;
bool ZeldaIsEmulatorAttached(void) { return g_emu_runframe != NULL; }
static ZeldaSyncAllFunc *g_emu_syncall;

void ZeldaSetupEmuCallbacks(uint8 *emu_ram, ZeldaRunFrameFunc *func, ZeldaSyncAllFunc *sync_all) {
  g_emu_memory_ptr = emu_ram;
  g_emu_runframe = func;
  g_emu_syncall = sync_all;
}

static void EmuSynchronizeWholeState() {
  if (g_emu_syncall)
    g_emu_syncall();
}

// |ptr| must be a pointer into g_ram, will synchronize the RAM memory with the
// emulator.
static void EmuSyncMemoryRegion(void *ptr, size_t n) {
  uint8 *data = (uint8 *)ptr;
  assert(data >= g_ram && data < g_ram + 0x20000);
  if (g_emu_memory_ptr)
    memcpy(g_emu_memory_ptr + (data - g_ram), data, n);
}

static void Startup_InitializeMemory() {  // 8087c0
  memset(g_ram + 0x0, 0, 0x2000);
  main_palette_buffer[0] = 0;
  srm_var1 = 0;
  uint8 *sram = g_zenv.sram;
  if (WORD(sram[0x3e5]) != 0x55aa)
    WORD(sram[0x3e5]) = 0;
  if (WORD(sram[0x8e5]) != 0x55aa)
    WORD(sram[0x8e5]) = 0;
  if (WORD(sram[0xde5]) != 0x55aa)
    WORD(sram[0xde5]) = 0;
  INIDISP_copy = 0x80;
  flag_update_cgram_in_nmi++;
}

void ByteArray_AppendVl(ByteArray *arr, uint32 v) {
  for (; v >= 255; v -= 255)
    ByteArray_AppendByte(arr, 255);
  ByteArray_AppendByte(arr, v);
}

void saveFunc(void *ctx_in, void *data, size_t data_size) {
  ByteArray_AppendData((ByteArray *)ctx_in, data, data_size);
}

typedef struct LoadFuncState {
  uint8 *p, *pend;
} LoadFuncState;

void loadFunc(void *ctx, void *data, size_t data_size) {
  LoadFuncState *st = (LoadFuncState *)ctx;
  assert(st->pend - st->p >= data_size);
  memcpy(data, st->p, data_size);
  st->p += data_size;
}

static void InternalSaveLoad(SaveLoadFunc *func, void *ctx) {
  uint8 junk[58] = { 0 };
  func(ctx, junk, 27);
  func(ctx, g_zenv.player->ram, 0x10000);  // apu ram
  func(ctx, junk, 40); // junk
  dsp_saveload(g_zenv.player->dsp, func, ctx); // 3024 bytes of dsp
  func(ctx, junk, 15); // spc junk
  dma_saveload(g_zenv.dma, func, ctx); // 192 bytes of dma state
  ppu_saveload(g_zenv.ppu, func, ctx); // 66619 + 512 + 174
  func(ctx, g_zenv.sram, 0x2000);  // 8192 bytes of sram
  func(ctx, junk, 58); // snes junk
  func(ctx, g_zenv.ram, 0x20000);  // 0x20000 bytes of ram
  func(ctx, junk, 4); // snes junk
}

void ZeldaReset(bool preserve_sram) {
  frame_ctr_dbg = 0;
  g_test_scenario_active = false;
  dma_reset(g_zenv.dma);
  ppu_reset(g_zenv.ppu);
  Rando_ObjScratchInvalidateAll();
  memset(g_zenv.ram, 0, 0x20000);
  if (!preserve_sram)
    memset(g_zenv.sram, 0, 0x2000);
  ZeldaApuLock();
  ZeldaRestoreMusicAfterLoad_Locked(true);
  ZeldaApuUnlock();
  ResetWideTracking();
  EmuSynchronizeWholeState();

}

static void LoadSnesState(SaveLoadFunc *func, void *ctx) {
  // Do the actual loading
  ZeldaApuLock();
  InternalSaveLoad(func, ctx);
  memcpy(g_zenv.ram + 0x1DBA0, g_zenv.ram + 0x1b00, 224 * 2); // hdma table was moved

  ZeldaRestoreMusicAfterLoad_Locked(false);
  ZeldaApuUnlock();
  Localized_AfterStateLoad();
  Rando_InvalidateTransientVideoState();
  Rando_ObjScratchInvalidateAll();
  ResetWideTracking();
  EmuSynchronizeWholeState();
}

static void SaveSnesState(SaveLoadFunc *func, void *ctx) {
  memcpy(g_zenv.ram + 0x1b00, g_zenv.ram + 0x1DBA0, 224 * 2); // hdma table was moved
  ZeldaApuLock();
  ZeldaSaveMusicStateToRam_Locked();
  InternalSaveLoad(func, ctx);
  ZeldaApuUnlock();
}

typedef struct StateRecorder {
  uint16 last_inputs;
  uint32 frames_since_last;
  uint32 total_frames;

  // For replay
  uint32 replay_pos, replay_pos_last_complete;
  uint32 replay_frame_counter;
  uint32 replay_next_cmd_at;
  uint8 replay_cmd;
  bool replay_mode;

  ByteArray log;
  ByteArray base_snapshot;
} StateRecorder;

static StateRecorder state_recorder;

void ZeldaClearInputLog(void) {
  ByteArray_Destroy(&state_recorder.log);
  ByteArray_Destroy(&state_recorder.base_snapshot);
  memset(&state_recorder,0,sizeof(state_recorder));
}

void StateRecorder_Init(StateRecorder *sr) {
  memset(sr, 0, sizeof(*sr));
}

// Called only after audio, UI and PPU workers have stopped using the session.
void ZeldaShutdownSession(void) {
  ZeldaShutdownPpuWorker();
  Rando_DeactivateSlot();
  if (g_zenv.player) {
    dsp_free(g_zenv.player->dsp);
    free(g_zenv.player); g_zenv.player = NULL;
  }
  if (g_zenv.dma) dma_free(g_zenv.dma);
  ppu_free(g_zenv.ppu);
  free(g_zenv.sram);
  memset(&g_zenv, 0, sizeof(g_zenv));
  ByteArray_Destroy(&state_recorder.log);
  ByteArray_Destroy(&state_recorder.base_snapshot);
  StateRecorder_Init(&state_recorder);
  memset(&g_wide_view, 0, sizeof(g_wide_view));
}

void StateRecorder_RecordCmd(StateRecorder *sr, uint8 cmd) {
  int frames = sr->frames_since_last;
  sr->frames_since_last = 0;
  int x = (cmd < 0xc0) ? 0xf : 0x1;
  ByteArray_AppendByte(&sr->log, cmd | (frames < x ? frames : x));
  if (frames >= x)
    ByteArray_AppendVl(&sr->log, frames - x);
}

void StateRecorder_Record(StateRecorder *sr, uint16 inputs) {
  uint16 diff = inputs ^ sr->last_inputs;
  if (diff != 0) {
    sr->last_inputs = inputs;
    //    printf("0x%.4x %d: ", diff, sr->frames_since_last);
    //    size_t lb = sr->log.size;
    for (int i = 0; i < 12; i++) {
      if ((diff >> i) & 1)
        StateRecorder_RecordCmd(sr, i << 4);
    }
    //    while (lb < sr->log.size)
    //      printf("%.2x ", sr->log.data[lb++]);
    //    printf("\n");
  }
  sr->frames_since_last++;
  sr->total_frames++;
}

void StateRecorder_RecordPatchByte(StateRecorder *sr, uint32 addr, const uint8 *value, int num) {
  assert(addr < 0x20000);

  //  printf("%d: PatchByte(0x%x, 0x%x. %d): ", sr->frames_since_last, addr, *value, num);
  //  size_t lb = sr->log.size;
  int lq = (num - 1) <= 3 ? (num - 1) : 3;
  StateRecorder_RecordCmd(sr, 0xc0 | (addr & 0x10000 ? 2 : 0) | lq << 2);
  if (lq == 3)
    ByteArray_AppendVl(&sr->log, num - 1 - 3);
  ByteArray_AppendByte(&sr->log, addr >> 8);
  ByteArray_AppendByte(&sr->log, addr);
  for (int i = 0; i < num; i++)
    ByteArray_AppendByte(&sr->log, value[i]);
  //  while (lb < sr->log.size)
  //    printf("%.2x ", sr->log.data[lb++]);
  //  printf("\n");
}

/*void ReadFromFile(FILE *f, void *data, size_t n) {
  if (fread(data, 1, n, f) != n)
    Die("fread failed\n");
}*/

void ReadFromFile(SDL_RWops *stream, void *data, size_t n) {
  size_t bytesRead = SDL_RWread(stream, data, 1, n);
  if (bytesRead != n) {
    Die("SDL_RWread failed");
  }
}

// Optional developer footer, after the unchanged SNES/randomizer snapshot.
// Versioned magic, LE index and its complement. Ordinary and legacy states
// have no footer. A test snapshot must retain its no-SRAM-write protection
// when explicitly loaded after a cold launch.
enum { kTestFooterSize=16 };
static void TestFooterEncode(uint8 footer[kTestFooterSize]) {
  memcpy(footer,"Z3TEST01",8);
  uint32 index=(uint32)g_test_scenario_index;
  for(int i=0;i<4;i++){footer[8+i]=index>>(8*i);footer[12+i]=~index>>(8*i);}
}
static int TestFooterDecode(const uint8 *footer) {
  if(memcmp(footer,"Z3TEST01",8))return -1;
  uint32 index=0,check=0;
  for(int i=0;i<4;i++){index|=(uint32)footer[8+i]<<(8*i);check|=(uint32)footer[12+i]<<(8*i);}
  return index<(uint32)kTestScenarioCount&&check==~index?(int)index:-1;
}
static int TestFooterRead(SDL_RWops *file) {
  Sint64 position=SDL_RWtell(file),size=SDL_RWsize(file);
  uint8 footer[kTestFooterSize];int index=-1;
  if(size>=kTestFooterSize&&SDL_RWseek(file,size-kTestFooterSize,RW_SEEK_SET)>=0&&
     SDL_RWread(file,footer,1,sizeof(footer))==sizeof(footer))index=TestFooterDecode(footer);
  SDL_RWseek(file,position,RW_SEEK_SET);
  return index;
}

void StateRecorder_Load(StateRecorder *sr, SDL_RWops *f, bool replay_mode) {
  // todo: fix robustness on invalid data.
  uint32 hdr[8] = { 0 };
  ReadFromFile(f, hdr, sizeof(hdr));

  assert(hdr[0] == 1);

  sr->total_frames = hdr[1];
  ByteArray_Resize(&sr->log, hdr[2]);
  ReadFromFile(f, sr->log.data, sr->log.size);
  sr->last_inputs = hdr[3];
  sr->frames_since_last = hdr[4];

  ByteArray_Resize(&sr->base_snapshot, (hdr[5] & 1) ? hdr[6] : 0);
  ReadFromFile(f, sr->base_snapshot.data, sr->base_snapshot.size);

  sr->replay_next_cmd_at = 0;

  uint64 oncheck_before = g_rando_oncheck_call_count;
  sr->replay_mode = replay_mode;
  if (replay_mode) {
    sr->frames_since_last = 0;
    sr->last_inputs = 0;
    sr->replay_pos = sr->replay_pos_last_complete = 0;
    sr->replay_frame_counter = 0;
    // Load snapshot from |base_snapshot_|, or reset if empty.

    if (sr->base_snapshot.size) {
      LoadFuncState state = { sr->base_snapshot.data, sr->base_snapshot.data + sr->base_snapshot.size };
      LoadSnesState(&loadFunc, &state);
      assert(state.p == state.pend);
    } else {
      ZeldaReset(false);
    }
  } else {
    // Resume replay from the saved position?
    sr->replay_pos = sr->replay_pos_last_complete = hdr[5] >> 1;
    sr->replay_frame_counter = hdr[7];
    sr->replay_mode = (sr->replay_frame_counter != 0);

    ByteArray arr = { 0 };
    ByteArray_Resize(&arr, hdr[6]);
    ReadFromFile(f, arr.data, arr.size);
    LoadFuncState state = { arr.data, arr.data + arr.size };
    LoadSnesState(&loadFunc, &state);
    ByteArray_Destroy(&arr);
    assert(state.p == state.pend);
  }
  // Replay restores the base snapshot above, but the file still contains the
  // saved current-state chunk before its extension chain.
  if (replay_mode && SDL_RWseek(f, hdr[6], RW_SEEK_CUR) < 0)
    Die("Unable to seek to randomizer snapshot metadata");
  bool randomizer_snapshot = g_rando_slot_active != 0;
  (void)RandoSnapshotTail_LoadRW(f);
  Rando_BumpReachabilityCounter();
  g_recv_item_slot_owner = 0xffff;
  Rando_InvalidateTransientVideoState();
  if (g_rando_oncheck_call_count != oncheck_before)
    Die("Game dispatch ran while restoring randomizer metadata");
  if (randomizer_snapshot && !Rando_IsActive())
    Die("Randomizer snapshot metadata is missing or incompatible");

  g_test_scenario_index=TestFooterRead(f);
  g_test_scenario_active=g_test_scenario_index>=0;

}

/*void StateRecorder_Save(StateRecorder *sr, FILE *f) {
  uint32 hdr[8] = { 0 };
  ByteArray arr = { 0 };
  SaveSnesState(&saveFunc, &arr);
  assert(sr->base_snapshot.size == 0 || sr->base_snapshot.size == arr.size);

  hdr[0] = 1;
  hdr[1] = sr->total_frames;
  hdr[2] = (uint32)sr->log.size;
  hdr[3] = sr->last_inputs;
  hdr[4] = sr->frames_since_last;
  hdr[5] = sr->base_snapshot.size ? 1 : 0;
  hdr[6] = (uint32)arr.size;
  // If saving while in replay mode, also need to persist
  // sr->replay_pos_last_complete and sr->replay_frame_counter
  // so the replaying can be resumed.
  if (sr->replay_mode) {
    hdr[5] |= sr->replay_pos_last_complete << 1;
    hdr[7] = sr->replay_frame_counter;
  }
  fwrite(hdr, 1, sizeof(hdr), f);
  fwrite(sr->log.data, 1, hdr[2], f);
  fwrite(sr->base_snapshot.data, 1, sr->base_snapshot.size, f);
  fwrite(arr.data, 1, arr.size, f);

  ByteArray_Destroy(&arr);
}*/

void StateRecorder_ClearKeyLog(StateRecorder *sr);

void StateRecorder_Save(StateRecorder* sr, SDL_RWops* rwops) {
  if (Placement_GetActive() && sr->base_snapshot.size == 0)
    StateRecorder_ClearKeyLog(sr);
  uint32 hdr[8] = { 0 };
  ByteArray arr = { 0 };
  SaveSnesState(&saveFunc, &arr);
  assert(sr->base_snapshot.size == 0 || sr->base_snapshot.size == arr.size);

  hdr[0] = 1;
  hdr[1] = sr->total_frames;
  hdr[2] = (uint32)sr->log.size;
  hdr[3] = sr->last_inputs;
  hdr[4] = sr->frames_since_last;
  hdr[5] = sr->base_snapshot.size ? 1 : 0;
  hdr[6] = (uint32)arr.size;
  // If saving while in replay mode, also need to persist
  // sr->replay_pos_last_complete and sr->replay_frame_counter
  // so the replaying can be resumed.
  if (sr->replay_mode) {
    hdr[5] |= sr->replay_pos_last_complete << 1;
    hdr[7] = sr->replay_frame_counter;
  }
  SDL_RWwrite(rwops, hdr, sizeof(hdr), 1);
  SDL_RWwrite(rwops, sr->log.data, hdr[2], 1);
  SDL_RWwrite(rwops, sr->base_snapshot.data, sr->base_snapshot.size, 1);
  SDL_RWwrite(rwops, arr.data, arr.size, 1);

  if (!RandoSnapshotTail_SaveRW(rwops))
    fprintf(stderr, "Unable to append randomizer snapshot metadata\n");
  if(g_test_scenario_active) {
    uint8 footer[kTestFooterSize];TestFooterEncode(footer);
    SDL_RWwrite(rwops,footer,1,sizeof(footer));
  }
  ByteArray_Destroy(&arr);
}

typedef struct ZeldaDumpPayloadHeader {
  uint32 version;
  uint32 total_frames;
  uint32 last_inputs;
  uint32 frames_since_last;
  uint32 snapshot_size;
} ZeldaDumpPayloadHeader;

enum { kZeldaDumpPayloadVersion = 1 };

#ifdef ZELDA3_TEST_DUMP_STATE
extern uint32_t Platform3DS_GetActiveProfileId(void);
#endif

bool ZeldaWriteDumpState(const char *dump_directory) {
#if defined(__3DS__) || defined(ZELDA3_TEST_DUMP_STATE)
  if (!dump_directory || !dump_directory[0])
    return false;

  ByteArray snapshot = {0};
  SaveSnesState(&saveFunc, &snapshot);
  if (snapshot.size == 0 || snapshot.size > UINT32_MAX) {
    ByteArray_Destroy(&snapshot);
    return false;
  }

  ZeldaDumpPayloadHeader header = {
    .version = kZeldaDumpPayloadVersion,
    .total_frames = state_recorder.total_frames,
    .last_inputs = state_recorder.last_inputs,
    .frames_since_last = state_recorder.frames_since_last,
    .snapshot_size = (uint32)snapshot.size,
  };
  ByteArray payload = {0};
  ByteArray_AppendData(&payload, (const uint8 *)&header, sizeof(header));
  ByteArray_AppendData(&payload, snapshot.data, snapshot.size);
  if(g_test_scenario_active) {
    uint8 footer[kTestFooterSize];TestFooterEncode(footer);
    ByteArray_AppendData(&payload,footer,sizeof(footer));
  }

  char path[512];
  int length = snprintf(path, sizeof(path), "%s/%s", dump_directory,
                        ZELDA_DUMP_LOAD_STATE_FILENAME);
  bool ok = length >= 0 && length < (int)sizeof(path) &&
            DumpState_WriteFile(path, Platform3DS_GetActiveProfileId(),
                                payload.data, payload.size);
  ByteArray_Destroy(&payload);
  ByteArray_Destroy(&snapshot);
  return ok;
#else
  (void)dump_directory;
  return false;
#endif
}

ZeldaDumpStateResult ZeldaLoadLatestDumpState(void) {
#if defined(__3DS__) || defined(ZELDA3_TEST_DUMP_STATE)
  uint8 *payload = NULL;
  size_t payload_size = 0;
  ZeldaDumpStateResult result =
    DumpState_ReadLatest("dumps", Platform3DS_GetActiveProfileId(),
                         &payload, &payload_size);
  if (result != kZeldaDumpStateLoaded)
    return result;

  if (payload_size < sizeof(ZeldaDumpPayloadHeader)) {
    free(payload);
    return kZeldaDumpStateInvalid;
  }
  ZeldaDumpPayloadHeader header;
  memcpy(&header, payload, sizeof(header));
  size_t snapshot_size=header.snapshot_size;
  size_t extra=payload_size-sizeof(header);
  int scenario=-1;
  if(extra>=kTestFooterSize&&extra==snapshot_size+kTestFooterSize)
    scenario=TestFooterDecode(payload+payload_size-kTestFooterSize);
  if (header.version != kZeldaDumpPayloadVersion ||
      (extra!=snapshot_size && (extra!=snapshot_size+kTestFooterSize || scenario<0)) ||
      header.last_inputs > 0x0fff) {
    free(payload);
    return kZeldaDumpStateInvalid;
  }

  /* InternalSaveLoad has a fixed serialized size. Compare the checkpoint with
   * a capture from this exact engine build before giving the data to its
   * assert-oriented loader, so truncated or foreign states cannot overrun it. */
  ByteArray current = {0};
  SaveSnesState(&saveFunc, &current);
  bool compatible = current.size == snapshot_size;
  ByteArray_Destroy(&current);
  if (!compatible) {
    free(payload);
    return kZeldaDumpStateInvalid;
  }

  LoadFuncState load = {
    payload + sizeof(header),
    payload + sizeof(header) + snapshot_size,
  };
  LoadSnesState(&loadFunc, &load);
  bool fully_consumed = load.p == load.pend;
  free(payload);
  if (!fully_consumed)
    return kZeldaDumpStateInvalid;

  ByteArray_Destroy(&state_recorder.log);
  ByteArray_Destroy(&state_recorder.base_snapshot);
  memset(&state_recorder, 0, sizeof(state_recorder));
  state_recorder.total_frames = header.total_frames;
  state_recorder.last_inputs = (uint16)header.last_inputs;
  state_recorder.frames_since_last = header.frames_since_last;
  g_test_scenario_index=scenario;
  g_test_scenario_active=scenario>=0;
  return kZeldaDumpStateLoaded;
#else
  return kZeldaDumpStateIoError;
#endif
}

void StateRecorder_ClearKeyLog(StateRecorder *sr) {
  printf("Clearing key log!\n");
  sr->base_snapshot.size = 0;
  SaveSnesState(&saveFunc, &sr->base_snapshot);
  ByteArray old_log = sr->log;
  int old_frames_since_last = sr->frames_since_last;
  memset(&sr->log, 0, sizeof(sr->log));
  // If there are currently any active inputs, record them initially at timestamp 0.
  sr->frames_since_last = 0;
  if (sr->last_inputs) {
    for (int i = 0; i < 12; i++) {
      if ((sr->last_inputs >> i) & 1)
        StateRecorder_RecordCmd(sr, i << 4);
    }
  }
  if (sr->replay_mode) {
    // When clearing the key log while in replay mode, we want to keep
    // replaying but discarding all key history up until this point.
    if (sr->replay_next_cmd_at != 0xffffffff) {
      sr->replay_next_cmd_at -= old_frames_since_last;
      sr->frames_since_last = sr->replay_next_cmd_at;
      sr->replay_pos_last_complete = (uint32)sr->log.size;
      StateRecorder_RecordCmd(sr, sr->replay_cmd);
      int old_replay_pos = sr->replay_pos;
      sr->replay_pos = (uint32)sr->log.size;
      ByteArray_AppendData(&sr->log, old_log.data + old_replay_pos, old_log.size - old_replay_pos);
    }
    sr->total_frames -= sr->replay_frame_counter;
    sr->replay_frame_counter = 0;
  } else {
    sr->total_frames = 0;
  }
  ByteArray_Destroy(&old_log);
  sr->frames_since_last = 0;
}

uint16 StateRecorder_ReadNextReplayState(StateRecorder *sr) {
  assert(sr->replay_mode);
  while (sr->frames_since_last >= sr->replay_next_cmd_at) {
    int replay_pos = sr->replay_pos;
    if (replay_pos != sr->replay_pos_last_complete) {
      // Apply next command
      sr->frames_since_last = 0;
      if (sr->replay_cmd < 0xc0) {
        sr->last_inputs ^= 1 << (sr->replay_cmd >> 4);
      } else if (sr->replay_cmd < 0xd0) {
        int nb = 1 + ((sr->replay_cmd >> 2) & 3);
        uint8 t;
        if (nb == 4) do {
          nb += t = sr->log.data[replay_pos++];
        } while (t == 255);
        uint32 addr = ((sr->replay_cmd >> 1) & 1) << 16;
        addr |= sr->log.data[replay_pos++] << 8;
        addr |= sr->log.data[replay_pos++];
        do {
          g_ram[addr & 0x1ffff] = sr->log.data[replay_pos++];
          EmuSyncMemoryRegion(&g_ram[addr & 0x1ffff], 1);
        } while (addr++, --nb);
      } else {
        assert(0);
      }
    }
    sr->replay_pos_last_complete = replay_pos;
    if (replay_pos >= sr->log.size) {
      sr->replay_pos = replay_pos;
      sr->replay_next_cmd_at = 0xffffffff;
      break;
    }
    // Read the next one
    uint8 cmd = sr->log.data[replay_pos++], t;
    int mask = (cmd < 0xc0) ? 0xf : 0x1;
    int frames = cmd & mask;
    if (frames == mask) do {
      frames += t = sr->log.data[replay_pos++];
    } while (t == 255);
    sr->replay_next_cmd_at = frames;
    sr->replay_cmd = cmd;
    sr->replay_pos = replay_pos;
  }
  sr->frames_since_last++;
  // Turn off replay mode after we reached the final frame position
  if (++sr->replay_frame_counter >= sr->total_frames) {
    sr->replay_mode = false;
  }
  return sr->last_inputs;
}

void StateRecorder_StopReplay(StateRecorder *sr) {
  if (!sr->replay_mode)
    return;
  sr->replay_mode = false;
  sr->total_frames = sr->replay_frame_counter;
  sr->log.size = sr->replay_pos_last_complete;
}

#ifdef _DEBUG
// This can be used to read inputs from a text file for easier debugging
int InputStateReadFromFile() {
  static FILE *f;
  static uint32 next_ts, next_keys, cur_keys;
  char buf[64];
  char keys[64];

  while (state_recorder.total_frames == next_ts) {
    cur_keys = next_keys;
    if (!f)
      f = fopen("boss_bug.txt", "r");
    if (fgets(buf, sizeof(buf), f)) {
      if (sscanf(buf, "%d: %s", &next_ts, keys) == 1) keys[0] = 0;
      int i = 0;
      for (const char *s = keys; *s; s++) {
        static const char kKeys[] = "AXsSUDLRBY";
        const char *t = strchr(kKeys, *s);
        assert(t);
        i |= 1 << (t - kKeys);
      }
      next_keys = i;
    } else {
      next_ts = 0xffffffff;
    }
  }

  return cur_keys;
}
#endif

bool ZeldaRunFrame(int inputs) {

  // Avoid up/down and left/right from being pressed at the same time
  if ((inputs & 0x30) == 0x30) inputs ^= 0x30;
  if ((inputs & 0xc0) == 0xc0) inputs ^= 0xc0;

  frame_ctr_dbg++;

  uint32 previous_features1=enhanced_features1;
  bool is_replay = state_recorder.replay_mode;

  // Either copy state or apply state
  if (is_replay) {
    inputs = StateRecorder_ReadNextReplayState(&state_recorder);
  } else {
    //    input_state = InputStateReadFromFile();
    StateRecorder_Record(&state_recorder, inputs);
    if (Qol_Ready()) {
      g_wanted_zelda_features=Qol_Features0(g_wanted_zelda_features);
      g_wanted_zelda_features1=Qol_Features1(g_wanted_zelda_features1);
      Rando_ApplyActiveForcedFeatures0();
    }

    // This is whether APUI00 is true or false, this is used by the ancilla code.
    uint8 apui00 = ZeldaIsMusicPlaying();
    if (apui00 != g_ram[kRam_APUI00]) {
      g_ram[kRam_APUI00] = apui00;
      EmuSyncMemoryRegion(&g_ram[kRam_APUI00], 1);
      StateRecorder_RecordPatchByte(&state_recorder, 0x648, &apui00, 1);
    }

    if (animated_tile_data_src != 0) {
      // Whenever we're no longer replaying, we'll remember what bugs were fixed,
      // but only if game is initialized.
      if (g_ram[kRam_BugsFixed] < kBugFix_Latest) {
        g_ram[kRam_BugsFixed] = kBugFix_Latest;
        EmuSyncMemoryRegion(&g_ram[kRam_BugsFixed], 1);
        StateRecorder_RecordPatchByte(&state_recorder, kRam_BugsFixed, &g_ram[kRam_BugsFixed], 1);
      }

      if (enhanced_features0 != g_wanted_zelda_features) {
        enhanced_features0 = g_wanted_zelda_features;
        EmuSyncMemoryRegion(&enhanced_features0, sizeof(enhanced_features0));
        StateRecorder_RecordPatchByte(&state_recorder, kRam_Features0, (uint8 *)&enhanced_features0, 4);
      }
      if (enhanced_features1 != g_wanted_zelda_features1) {
        enhanced_features1 = g_wanted_zelda_features1;
        EmuSyncMemoryRegion(&enhanced_features1, sizeof(enhanced_features1));
        StateRecorder_RecordPatchByte(&state_recorder, kRam_Features1,
                                      (uint8 *)&enhanced_features1, 4);
      }
    }
  }

  if ((previous_features1 ^ enhanced_features1)&kFeatures1_QolInventory)
    Hud_QolTranslateInventory((enhanced_features1&kFeatures1_QolInventory)!=0);
  {
    extern void ZeldaApplyDimFlashesPalette(bool enable);
    ZeldaApplyDimFlashesPalette((enhanced_features0 & kFeatures0_DimFlashes) != 0);
  }
  if (enhanced_features1 & kFeatures1_QolManaged)
    Hud_QolNormalizeInventory();

  int run_what;
  if (g_ram[kRam_BugsFixed] < kBugFix_PolyRenderer) {
    // A previous version of this code alternated the game loop with
    // the poly renderer.
    run_what = (is_nmi_thread_active && thread_other_stack != 0x1f31) ? 2 : 1;
  } else {
    // The snes seems to let poly rendering run for a little
    // while each fram until it eventually completes a frame.
    // Simulate this by rendering the poly every n:th frame.
    run_what = (is_nmi_thread_active && IncrementCrystalCountdown(&g_ram[kRam_CrystalRotateCounter], virq_trigger)) ? 3 : 1;
    EmuSyncMemoryRegion(&g_ram[kRam_CrystalRotateCounter], 1);
  }

  if (g_emu_runframe == NULL || enhanced_features0 != 0 || enhanced_features1 != 0 || g_zenv.dialogue_flags) {
    // can't compare against real impl when running with extra features.
    ZeldaRunFrameInternal(inputs, run_what);
  } else {
    g_emu_runframe(inputs, run_what);
  }

  ZeldaPushApuState();

#ifdef ZELDA_CAMERA_TEST
  if (is_replay && !state_recorder.replay_mode) {
    SaveLoadSlot(kSaveLoad_Save, 19);
    SDL_Event event = { .type = SDL_QUIT };
    SDL_PushEvent(&event);
    fprintf(stderr, "camera replay completed after %u frames\n",
            state_recorder.replay_frame_counter);
  }
#endif

  return is_replay;
}

void ZeldaSetLanguage(const char *language) {
  static const uint8 kDefaultConf[3] = { 0, 0, 0 };
  MemBlk found = { kDefaultConf, 3 };
  bool language_found = false;
  if (language) {
    size_t n = strlen(language);
    for (int i = 0; ; i++) {
      MemBlk mb = kDialogueMap(i);
      if (mb.ptr == 0) {
        fprintf(stderr, "Unable to find language '%s'\n", language);
        break;
      }
      MemBlk name = FindIndexInMemblk(mb, 0);
      if (name.size == n && !memcmp(name.ptr, language, n)) {
        found = FindIndexInMemblk(mb, 1);
        language_found = true;
        break;
      }
    }
  }
  // Assets built from a translated cartridge alone hold no USA text: their
  // map names the language of dialogue 0, which then needs no Language line.
  char base_language[16];
  for (int i = 0; !language_found; i++) {
    MemBlk mb = kDialogueMap(i);
    if (mb.ptr == 0)
      break;
    MemBlk name = FindIndexInMemblk(mb, 0), conf = FindIndexInMemblk(mb, 1);
    if (conf.size >= 3 && conf.ptr[0] == 0 && conf.ptr[2] != 0 &&
        name.size < sizeof(base_language)) {
      memcpy(base_language, name.ptr, name.size);
      base_language[name.size] = 0;
      language = base_language;
      found = conf;
      language_found = true;
    }
  }
  g_zenv.dialogue_blk = kDialogue(found.ptr[0]);
  g_zenv.dialogue_font_blk = kDialogueFont(found.ptr[1]);
  g_zenv.dialogue_flags = found.ptr[2];
  // The screens follow the dialogue: a cartridge's menus only go with its text.
  Localized_SetLanguage(language_found ? language : NULL);
}


static const char *const kReferenceSaves[] = {
  "Chapter 1 - Zelda's Rescue.sav",
  "Chapter 2 - After Eastern Palace.sav",
  "Chapter 3 - After Desert Palace.sav",
  "Chapter 4 - After Tower of Hera.sav",
  "Chapter 5 - After Hyrule Castle Tower.sav",
  "Chapter 6 - After Dark Palace.sav",
  "Chapter 7 - After Swamp Palace.sav",
  "Chapter 8 - After Skull Woods.sav",
  "Chapter 9 - After Gargoyle's Domain.sav",
  "Chapter 10 - After Ice Palace.sav",
  "Chapter 11 - After Misery Mire.sav",
  "Chapter 12 - After Turtle Rock.sav",
  "Chapter 13 - After Ganon's Tower.sav",
};

/*
void SaveLoadSlot(int cmd, int which) {
  char name[128];
  if (which & 256) {
    if (cmd == kSaveLoad_Save)
      return;
    sprintf(name, "saves/ref/%s", kReferenceSaves[which - 256]);
  } else {
    sprintf(name, "saves/save%d.sav", which);
  }
  FILE *f = fopen(name, cmd != kSaveLoad_Save ? "rb" : "wb");
  if (f) {
    printf("*** %s slot %d\n",
      cmd == kSaveLoad_Save ? "Saving" : cmd == kSaveLoad_Load ? "Loading" : "Replaying", which);

    if (cmd != kSaveLoad_Save)
      StateRecorder_Load(&state_recorder, f, cmd == kSaveLoad_Replay);
    else
      StateRecorder_Save(&state_recorder, f);

    fclose(f);
  }
}*/

// The path of a save: saves/<file>, or <SaveFolder>/<file> when zelda3.ini
// names a folder (a QUEST's own saves) outside the 3DS, whose profiles keep
// their saves. The reference saves stay in saves/ref.
void ZeldaFormatSavePath(const char *name, char *path, size_t size) {
#ifdef __3DS__
  Platform3DS_FormatSavePath(name, path, size);
#else
  if (g_config.save_folder && strncmp(name, "saves/", 6) == 0 &&
      strncmp(name, "saves/ref/", 10) != 0)
    snprintf(path, size, "%s/%s", g_config.save_folder, name + 6);
  else
    snprintf(path, size, "%s", name);
#endif
}

// A state keeps the screen of the language it was saved in, and the language
// may have changed since: the HUD's letters and an open save box are drawn again
// in the current one.
static void RedrawLanguageAfterLoad(void) {
  bool save_box = main_module_index == 14 && submodule_index == 11;
  if (!save_box && main_module_index != 7 && main_module_index != 9)
    return;
  LoadHudGraphics();
  Hud_Rebuild();
  if (save_box) {
    // The box stops the game's HUD uploads until it closes.
    extern bool g_ss_hide_hud;
    if (!g_ss_hide_hud)
      memcpy(&g_zenv.vram[word_7E0219], hud_tile_indices_buffer, 165 * sizeof(uint16));
    main_module_index = saved_module_for_menu;
    DisplaySelectMenu();
  }
}

void SaveLoadSlot(int cmd, int which) {
  char name[128];
  char path[256];
  SDL_RWops* rwops;

  if (which & 256) {
    if (cmd == kSaveLoad_Save)
      return;
    snprintf(name, sizeof(name), "saves/ref/%s", kReferenceSaves[which - 256]);
  } else {
    snprintf(name, sizeof(name), "saves/save%d.sav", which);
  }

  ZeldaFormatSavePath(name, path, sizeof(path));
  rwops = SDL_RWFromFileInExternal(path, cmd != kSaveLoad_Save ? "rb" : "wb");
  if (rwops) {
    printf("*** %s slot %d\n",
           cmd == kSaveLoad_Save ? "Saving" : cmd == kSaveLoad_Load ? "Loading" : "Replaying", which);

    if (cmd != kSaveLoad_Save)
      StateRecorder_Load(&state_recorder, rwops, cmd == kSaveLoad_Replay);
    else
      StateRecorder_Save(&state_recorder, rwops);

    SDL_RWclose(rwops);
    if (cmd == kSaveLoad_Load)
      RedrawLanguageAfterLoad();
#ifdef __3DS__
    // Older builds wrote slot 0 even in naming/credits. Recover those boots
    // through the ordinary clean reset, which reloads SRAM from disk.
    if (cmd == kSaveLoad_Load && which == 0 &&
        !NativeMenu_CanAutosave(main_module_index))
      Platform3DS_RequestReset();
#endif
  }
}


// A scenario state is the snapshot cut into sections, so the 64-bit tool
// that builds it and the 32-bit 3DS agree on it: the DSP and DMA state are
// copies of C structs whose padding differs between the two.
static const uint8 kScenarioMagic[8] = { 'Z', '3', 'S', 'C', 'E', 'N', 'E', '1' };
enum { kScenarioSections = 6 };  // APU RAM, DSP, DMA, PPU, SRAM, RAM

static size_t ScenarioDspSize(void) {  // the fields, without padding
  return offsetof(Dsp, sampleOffset) + sizeof(uint16_t) - offsetof(Dsp, ram);
}
static size_t ScenarioDmaSize(void) {
  return offsetof(Dma, dmaBusy) + sizeof(bool) - offsetof(Dma, channel);
}

static void DrawStartupLogoFrame(uint32 *pixels) {
  uint8 tm=TM_copy,ts=TS_copy,math=CGADSUB_copy,window=CGWSEL_copy;
  uint16 backdrop=g_zenv.ppu->cgram[0];
  TM_copy=0x11;TS_copy=0;CGADSUB_copy=0;CGWSEL_copy=0;
  g_zenv.ppu->screenEnabled[0]=0x11;g_zenv.ppu->screenEnabled[1]=0;
  g_zenv.ppu->mathEnabled=0;g_zenv.ppu->cgram[0]=0;
  g_rendering_startup_logo=true;
  ZeldaDrawPpuFrame((uint8*)pixels,256*4,kPpuRenderFlags_NewRenderer);
  g_rendering_startup_logo=false;
  // Cool silver highlights on the blade only. Track the native sword's
  // descending OAM position; leave the golden hilt and title art untouched.
  if(main_module_index==0&&submodule_index>=6&&!LanguageCart_Japanese()) {
    int shift=(int16)WORD(g_ram[0xc8])-30; // intro_sword_ypos in ending.c
    int top=84+shift,bottom=165+shift;
    if(top<0)top=0;if(bottom>224)bottom=224;
    for(int y=top;y<bottom;y++)for(int x=67;x<=75;x++) {
      uint32 *pixel=&pixels[y*256+x];
      if((*pixel&0xffffff)==0xffde6b)*pixel=(*pixel&0xff000000)|0xe7efff;
    }
  }
  TM_copy=tm;TS_copy=ts;CGADSUB_copy=math;CGWSEL_copy=window;
  g_zenv.ppu->screenEnabled[0]=tm;g_zenv.ppu->screenEnabled[1]=ts;
  g_zenv.ppu->mathEnabled=math;g_zenv.ppu->cgram[0]=backdrop;
}
void ZeldaDrawStartupLogo(uint32 *pixels) {
  int extra=g_zenv.ppu->extraLeftRight;g_zenv.ppu->extraLeftRight=0;
  // The Japanese title ends a step sooner ($8C:C2A5).
  int last=LanguageCart_Japanese()?7:8;
  for(int f=0;f<2000&&!(main_module_index==0&&submodule_index==last);f++)ZeldaRunFrame(0);
  DrawStartupLogoFrame(pixels);
  ZeldaReset(true);g_zenv.ppu->extraLeftRight=extra;ZeldaClearInputLog();
}
void ZeldaAnimateStartupLogoWithAudio(uint32 *pixels,bool (*present)(const uint32 *,unsigned,void *),
                                     void (*audio)(const int16 *,unsigned,void *),void *context) {
  int extra=g_zenv.ppu->extraLeftRight;g_zenv.ppu->extraLeftRight=0;
  unsigned tick=0,settled=0;
  int16 samples[534*2];
  for(int frame=0;frame<2000;frame++) {
    ZeldaRunFrame(0);
    ZeldaRenderStartupSfx(samples,534);
    if(main_module_index!=0||submodule_index<5)continue;
    if(audio)audio(samples,534,context);
    // Native title palettes, sword OAM and sparkle frames; display at 30 Hz
    // while keeping the original 60 Hz animation logic and timing.
    if(!(tick&1)) {DrawStartupLogoFrame(pixels);if(!present(pixels,tick,context))break;}
    tick++;
    if(submodule_index>=7&&++settled>=100)break;
  }
  ZeldaReset(true);g_zenv.ppu->extraLeftRight=extra;ZeldaClearInputLog();
}

void ZeldaAnimateStartupLogo(uint32 *pixels,bool (*present)(const uint32 *,unsigned,void *),void *context) {
  ZeldaAnimateStartupLogoWithAudio(pixels,present,NULL,context);
}

static void ApplyNativeTestScenario(const TestScenario *scenario) {
  for (const TestScenarioSet *set = scenario->sets; set->addr; set++)
    g_ram[0xf000 + set->addr] = set->value;
  follower_indicator = 0;
  link_health_current=link_health_capacity;link_magic_power=0x80;
  // Explicit low-health/empty-magic settings are applied again below.
  for (const TestScenarioSet *set=scenario->sets;set->addr;set++)
    g_ram[0xf000+set->addr]=set->value;
  if (scenario->dark_world && !link_item_moon_pearl) {
    link_is_bunny=link_is_bunny_mirror=1; link_player_handler_state=23;
  }
  DecompressSwordGraphics(); DecompressShieldGraphics();
  Palette_Load_Sword(); Palette_Load_Shield(); Palette_Load_LinkArmorAndGloves();
  Hud_SearchForEquippedItem(); Hud_Rebuild(); Hud_UpdateEquippedItem();
}

bool ZeldaLoadTestScenario(int index) {
  if (!ZELDA3_ENABLE_DEVELOPER_TESTS) return false;
  if (index < 0 || index >= kTestScenarioCount) return false;
  g_test_scenario_building=true;
  const TestScenario *scenario = &kTestScenarios[index];
  // Only authored inventory/story flags are built in. Every tile, sprite and
  // sound is initialized by the engine from the user's selected cartridge.
  // Preserve the player's entire SRAM even when starting from a randomizer.
  static uint8 player_sram[0x2000];
  memcpy(player_sram, g_zenv.sram, sizeof(player_sram));
  if (Rando_IsActive()) Rando_DeactivateSlot();
  ZeldaReset(true);
  g_test_scenario_active = true;
  ByteArray_Destroy(&state_recorder.log);
  ByteArray_Destroy(&state_recorder.base_snapshot);
  memset(&state_recorder, 0, sizeof(state_recorder));
  TestScenario_InitSram(index, g_zenv.sram);
  // Initialize the normal game's memory and graphics, then use its own room
  // and overworld loaders. This does not read or write a snapshot file.
  ZeldaRunFrame(0);
  WORD(g_ram[0]) = 0;
  srm_var1 = 2;
  CopySaveToWRAM();
  // Direct developer loads bypass the file-select fade that builds this table.
  DecompressEnemyDamageSubclasses();
  // The intro also builds shared item/peg animation data before file select.
  LoadItemGFXIntoWRAM4BPPBuffer();
  Module05_LoadFile();
  g_test_scenario_active = true;
  ApplyNativeTestScenario(scenario);
  savegame_is_darkworld = scenario->dark_world ? 0x40 : 0;
  link_auxiliary_state = link_incapacitated_timer = 0;
  submodule_index = subsubmodule_index = 0;
  if (scenario->kind == kTestScenario_Entrance) {
    which_entrance = scenario->target;
    main_module_index = 6;
  } else {
    EnableForceBlank();
    dungeon_room_index = scenario->target;
    player_is_indoors = 1;
    main_module_index = 8;
  }
  int want = scenario->kind == kTestScenario_Entrance ? 7 : 9;
  int frames = 0;
  while (frames++ < 1500 && !(main_module_index == want && submodule_index == 0))
    ZeldaRunFrame(0);
  for (int i = 0; i < 90; i++) ZeldaRunFrame(0);
  ApplyNativeTestScenario(scenario);
  for (int i = 0; i < 10; i++) ZeldaRunFrame(0);
  memcpy(g_zenv.sram, player_sram, sizeof(player_sram));
  g_test_scenario_active = true;
  g_test_scenario_index = index;
  LoadHudGraphics(); Hud_Rebuild();
  ResetWideTracking();
  Rando_InvalidateTransientVideoState();
  g_test_scenario_building=false;
  return main_module_index == want || main_module_index == 14;
}

int ZeldaGetTestScenarioIndex(void) {
  return g_test_scenario_active ? g_test_scenario_index : -1;
}

bool ZeldaRestartTestScenario(void) {
  if(!g_test_scenario_active||g_test_scenario_building||g_test_scenario_index<0)return false;
  return ZeldaLoadTestScenario(g_test_scenario_index);
}

bool ZeldaWriteTestScenarioState(const char *path) {
  ByteArray snapshot = { 0 };
  SaveSnesState(&saveFunc, &snapshot);
  size_t dsp = sizeof(Dsp) - offsetof(Dsp, ram), dma = sizeof(Dma) - offsetof(Dma, channel);
  size_t apu_at = 27, dsp_at = apu_at + 0x10000 + 40, dma_at = dsp_at + dsp + 15;
  size_t ppu_at = dma_at + dma;
  size_t ppu = snapshot.size - ppu_at - 0x2000 - 58 - 0x20000 - 4;
  const uint8 *base = snapshot.data;
  const uint8 *data[kScenarioSections] = {
    base + apu_at, base + dsp_at, base + dma_at, base + ppu_at,
    base + ppu_at + ppu, base + ppu_at + ppu + 0x2000 + 58,
  };
  uint32 sizes[kScenarioSections] = {
    0x10000, (uint32)ScenarioDspSize(), (uint32)ScenarioDmaSize(), (uint32)ppu, 0x2000, 0x20000,
  };
  SDL_RWops *rwops = SDL_RWFromFile(path, "wb");
  bool ok = rwops != NULL;
  uint32 count = kScenarioSections;
  ok = ok && SDL_RWwrite(rwops, kScenarioMagic, sizeof(kScenarioMagic), 1) == 1 &&
       SDL_RWwrite(rwops, &count, 4, 1) == 1;
  for (int i = 0; ok && i < kScenarioSections; i++)
    ok = SDL_RWwrite(rwops, &sizes[i], 4, 1) == 1 && SDL_RWwrite(rwops, data[i], sizes[i], 1) == 1;
  ok = ok && RandoSnapshotTail_SaveRW(rwops);
  if (rwops)
    SDL_RWclose(rwops);
  ByteArray_Destroy(&snapshot);
  return ok;
}

static bool WriteDumpFile(const char *dir, const char *name, const void *data, size_t size) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  FILE *f = fopen(path, "wb");
  if (!f)
    return false;
  bool ok = fwrite(data, 1, size, f) == size;
  return (fclose(f) == 0) && ok;
}

bool ZeldaWriteDumpExtras(const char *dir) {
  // Audio: the sound CPU's 64 KB and the DSP's registers and voices.
  bool ok = WriteDumpFile(dir, "apu-ram.bin", g_zenv.player->ram, 0x10000);
  Dsp *dsp = g_zenv.player->dsp;
  ok = WriteDumpFile(dir, "dsp.bin", dsp->ram, sizeof(Dsp) - offsetof(Dsp, ram)) && ok;
  // The whole PPU: VRAM, CGRAM, OAM (both tables) and its registers as bytes.
  ok = WriteDumpFile(dir, "ppu-struct.bin", g_zenv.ppu, sizeof(*g_zenv.ppu)) && ok;
  char path[512];
  snprintf(path, sizeof(path), "%s/context.txt", dir);
  FILE *f = fopen(path, "w");
  if (!f)
    return false;
  fprintf(f, "Context schema: 1\n");
  fprintf(f, "Game language: %s\n", g_config.language ? g_config.language : "en");
  fprintf(f, "Mods: %s\n", g_config.mods ? g_config.mods : "(none)");
  fprintf(f, "Randomizer active: %d\n", Rando_IsActive() ? 1 : 0);
#ifdef __3DS__
  const char *seed = Platform3DS_RandomizerSeedKey();
  fprintf(f, "Randomizer seed key: %s\n", seed && seed[0] ? seed : "(none)");
#endif
  fprintf(f, "Test scenario active: %d\n", g_test_scenario_active ? 1 : 0);
  if (g_test_scenario_index >= 0 && g_test_scenario_index < kTestScenarioCount)
    fprintf(f, "Test scenario: %02d %s\n", g_test_scenario_index + 1,
            kTestScenarios[g_test_scenario_index].name);
  fprintf(f, "Features: features0=%08x enhanced0=%08x enhanced1=%08x\n",
          g_config.features0, (unsigned)enhanced_features0, (unsigned)enhanced_features1);
  fprintf(f, "Wide: extra=%d edge_mode=%d\n", g_zenv.ppu->extraLeftRight,
          ZeldaGetWidescreenEdgeMode());
  fprintf(f, "Module: main=%u sub=%u subsub=%u room=0x%03x area=0x%02x indoors=%u dark=%u\n",
          main_module_index, submodule_index, subsubmodule_index, dungeon_room_index,
          overworld_screen_index, player_is_indoors, savegame_is_darkworld);
  fprintf(f, "Link: x=%u y=%u health=%u/%u magic=%u sword=%u shield=%u armor=%u\n",
          link_x_coord, link_y_coord, link_health_current, link_health_capacity,
          link_magic_power, link_sword_type, link_shield_type, link_armor);
  fprintf(f, "Files: ram.bin (WRAM 128 KB), vram.bin (64 KB), cgram.bin, oam.bin, sram.bin,\n"
             "apu-ram.bin, dsp.bin, ppu-struct.bin, load-state.bin (everything, loadable),\n"
             "top/bottom screens (BMP and raw), ppu.txt, info.txt, runtime.log.\n");
  ok = (fclose(f) == 0) && ok;
  return ok;
}

void ZeldaClearAutosave() {
  char path[256];
  ZeldaFormatSavePath("saves/save0.sav", path, sizeof(path));
  remove(path);
}

typedef struct StateRecoderMultiPatch {
  uint32 count;
  uint32 addr;
  uint8 vals[256];
} StateRecoderMultiPatch;


void StateRecoderMultiPatch_Init(StateRecoderMultiPatch *mp) {
  mp->count = mp->addr = 0;
}

void StateRecoderMultiPatch_Commit(StateRecoderMultiPatch *mp) {
  if (mp->count)
    StateRecorder_RecordPatchByte(&state_recorder, mp->addr, mp->vals, mp->count);
}

void StateRecoderMultiPatch_Patch(StateRecoderMultiPatch *mp, uint32 addr, uint8 value) {
  if (mp->count >= 256 || addr != mp->addr + mp->count) {
    StateRecoderMultiPatch_Commit(mp);
    mp->addr = addr;
    mp->count = 0;
  }
  mp->vals[mp->count++] = value;
  g_ram[addr] = value;
  EmuSyncMemoryRegion(&g_ram[addr], 1);
}

void PatchCommand(char c) {
  StateRecoderMultiPatch mp;

  StateRecoderMultiPatch_Init(&mp);
  if (c == 'w') {
    StateRecoderMultiPatch_Patch(&mp, 0xf372, 80);  // health filler
    StateRecoderMultiPatch_Patch(&mp, 0xf373, 80);  // magic filler
    //    b.Patch(0x1FE01, 25);
  } else if (c == 'W') {
    StateRecoderMultiPatch_Patch(&mp, 0xf375, 10);  // link_bomb_filler
    StateRecoderMultiPatch_Patch(&mp, 0xf376, 10);  // link_arrow_filler
    uint16 rupees = link_rupees_goal + 100;
    StateRecoderMultiPatch_Patch(&mp, 0xf360, rupees);  // link_rupees_goal
    StateRecoderMultiPatch_Patch(&mp, 0xf361, rupees >> 8);  // link_rupees_goal
  } else if (c == 'k') {
    StateRecorder_ClearKeyLog(&state_recorder);
  } else if (c == 'o') {
    StateRecoderMultiPatch_Patch(&mp, 0xf36f, 1);
  } else if (c == 'l') {
    StateRecorder_StopReplay(&state_recorder);
  } else if (c == 'E') {
    StateRecoderMultiPatch_Patch(&mp, 0x37f, g_ram[0x37f] ^ 1);
  }
  StateRecoderMultiPatch_Commit(&mp);
}

/*
void ZeldaReadSram() {
  FILE *f = fopen("saves/sram.dat", "rb");
  if (f) {
    if (fread(g_zenv.sram, 1, 8192, f) != 8192)
      fprintf(stderr, "Error reading saves/sram.dat\n");
    fclose(f);
    EmuSynchronizeWholeState();
  }
}

void ZeldaWriteSram() {
  rename("saves/sram.dat", "saves/sram.bak");
  FILE *f = fopen("saves/sram.dat", "wb");
  if (f) {
    fwrite(g_zenv.sram, 1, 8192, f);
    fclose(f);
  } else {
    fprintf(stderr, "Unable to write saves/sram.dat\n");
  }
}*/

#ifndef __ANDROID__
// Off Android there is no external-storage indirection: files live in cwd.
SDL_RWops* SDL_RWFromFileInExternal(const char *filename, const char *mode) {
  return SDL_RWFromFile(filename, mode);
}
#else
SDL_RWops* SDL_RWFromFileInExternal(const char *filename, const char *mode) {
  // Get the external storage path
  const char* externalDir = SDL_AndroidGetExternalStoragePath();

  if (externalDir) {
    // Create a file path for the config file in the external storage directory
    char ExternalFilePath[256];
    snprintf(ExternalFilePath, sizeof(ExternalFilePath), "%s/%s", externalDir, filename);

    SDL_RWops *stream = SDL_RWFromFile(ExternalFilePath, mode);
    if (stream == NULL) {
      fprintf(stderr, "Failed to open file: %s\n", SDL_GetError());
      return NULL;
    }
    return stream;
  }else {
    fprintf(stderr, "External storage path not available.\n");
    return 0;
  }
}
#endif  // __ANDROID__

void ZeldaReadSram() {
  int paired = RandoSave_RecoverPairedSave();
  if (paired) {
    if (paired < 0) {
      memset(g_zenv.sram, 0, 8192);
      fprintf(stderr, "Rando: refusing corrupt paired save bundle\n");
    }
    EmuSynchronizeWholeState();
    Localized_AfterSramLoad();
    LanguageCart_AfterSramLoad(g_zenv.sram, &g_ram[0xf000]);
    return;
  }
#ifdef __3DS__
  char path[256], backup[256];
  ZeldaFormatSavePath("saves/sram.dat", path, sizeof(path));
  ZeldaFormatSavePath("saves/sram.bak", backup, sizeof(backup));
  if (SramStore_Read(path, backup, g_zenv.sram))
    EmuSynchronizeWholeState();
  else
    Platform3DS_LogRuntime("SRAM: no complete save available; retaining initialized SRAM");
#else
  char path[256];
  ZeldaFormatSavePath("saves/sram.dat", path, sizeof(path));
  SDL_RWops *stream = SDL_RWFromFileInExternal(path, "rb");
  if (stream) {
    size_t bytesRead = SDL_RWread(stream, g_zenv.sram, 1, 8192);
    if (bytesRead != 8192) {
      fprintf(stderr, "Error reading saves/sram.dat\n");
    }
    SDL_RWclose(stream);
    EmuSynchronizeWholeState();
  }
#endif
  Localized_AfterSramLoad();
  LanguageCart_AfterSramLoad(g_zenv.sram, &g_ram[0xf000]);
}

void ZeldaWriteSram() {
  // Seed writes are committed with their sidecar by RandoSave_CommitPairedSlot.
  // Never overwrite a compatibility mirror independently of that transaction.
  if (RandoSave_SeedStorageActive()) return;
  // A test scenario runs on the reference save, not the player's.
  if (g_test_scenario_active) return;
#ifdef __3DS__
  char path[256], backup[256];
  ZeldaFormatSavePath("saves/sram.dat", path, sizeof(path));
  ZeldaFormatSavePath("saves/sram.bak", backup, sizeof(backup));
  if (!SramStore_Write(path, backup, g_zenv.sram))
    Platform3DS_LogRuntime("ERROR SRAM save failed; previous complete save retained");
#else
  char path[256], bak_path[256];
  ZeldaFormatSavePath("saves/sram.dat", path, sizeof(path));
  ZeldaFormatSavePath("saves/sram.bak", bak_path, sizeof(bak_path));
  // Back up the existing save before overwriting it.
#ifdef __ANDROID__
  const char* externalPath = SDL_AndroidGetExternalStoragePath();
  if (externalPath) {
    char oldFilePath[512];
    char newFilePath[512];
    snprintf(oldFilePath, sizeof(oldFilePath), "%s/%s", externalPath, path);
    snprintf(newFilePath, sizeof(newFilePath), "%s/%s", externalPath, bak_path);
    rename(oldFilePath, newFilePath);
  } else {
    fprintf(stderr, "External storage path not available.\n");
  }
#else
  rename(path, bak_path);
#endif

  SDL_RWops *stream = SDL_RWFromFileInExternal(path, "wb");
  if (stream) {
    // Fill 'sram' with the data you want to write
    size_t bytesWritten = SDL_RWwrite(stream, g_zenv.sram, 1, 8192);
    if (bytesWritten != 8192) {
      fprintf(stderr, "Error writing saves/sram.dat\n");
    }
    SDL_RWclose(stream);
  } else {
    fprintf(stderr, "Unable to write saves/sram.dat\n");
  }
#endif
}
#ifdef __3DS__
#include "platform_3ds.h"
#include "ppu_gpu.h"
#endif

void ZeldaWriteGameDiagnostics(FILE *file) {
#ifdef __3DS__
  if (g_zenv.ppu && (g_zenv.ppu->renderFlags & kPpuRenderFlags_Old3DS) && PpuGpuOutputActive()) {
    fputs("PPU CPU phase sample unavailable for current GPU frame; see PICA history in ppu.txt.\n", file);
  } else if (g_zenv.ppu && (g_zenv.ppu->renderFlags & kPpuRenderFlags_Old3DS)) {
    fputs("PPU sample frame and age count CPU-rendered frames only.\n", file);
    fprintf(file, "PPU phase schema=1 interval=64 frames sample_frame=%lu age=%lu scene=0x%08lx split=%d\n",
      (unsigned long)g_ppu_phase_main.frame,
      (unsigned long)(g_zenv.ppu->phase.frame - g_ppu_phase_main.frame),
      (unsigned long)g_ppu_phase_scene, g_ppu_phase_split);
    fputs("Phase times are sampled wall spans, including preemption. Main and worker overlap; do not add them. Prepare runs once on main.\n", file);
    const PpuPhaseProfile *phases[] = {&g_ppu_phase_main, &g_ppu_phase_worker};
    for (unsigned i=0; i<2; i++) {
      const PpuPhaseProfile *p=phases[i];
      fprintf(file, "PPU %s lines=%lu prepare_us=%llu sprites_us=%llu main_bg_us=%llu sub_bg_us=%llu compose_us=%llu mode7_hq_us=%llu retained_rows=%lu rebuilt_tiles=%lu\n",
        i ? "worker" : "main", (unsigned long)p->lines,
        (unsigned long long)(i ? 0 : p->prepare * 1000000ull / SYSCLOCK_ARM11),
        (unsigned long long)(p->sprites * 1000000ull / SYSCLOCK_ARM11),
        (unsigned long long)(p->main * 1000000ull / SYSCLOCK_ARM11),
        (unsigned long long)(p->sub * 1000000ull / SYSCLOCK_ARM11),
        (unsigned long long)(p->compose * 1000000ull / SYSCLOCK_ARM11),
        (unsigned long long)(p->mode7 * 1000000ull / SYSCLOCK_ARM11),
        (unsigned long)p->retainedRows, (unsigned long)(i ? 0 : p->rebuiltTiles));
    }
  }
#endif
  fprintf(file, "Scene: module=%u submodule=%u room=%u area=%u indoors=%u Link=(%u,%u)\nHDMA enable copy=%02x\n",
          main_module_index, submodule_index, dungeon_room_index, overworld_screen_index,
          player_is_indoors, link_x_coord, link_y_coord, HDMAEN_copy);
  for (unsigned i = 0; i < 8; i++) {
    const DmaChannel *d = &g_zenv.dma->channel[i];
    fprintf(file, "DMA%u source=%02x:%04x B-bus=%02x mode=%u indirect=%u bank=%02x size=%u table=%04x repeat=%u active=%u\n", i,
            d->aBank, d->aAdr, d->bAdr, d->mode, d->indirect, d->indBank,
            d->size, d->tableAdr, d->repCount, d->hdmaActive);
  }

}
