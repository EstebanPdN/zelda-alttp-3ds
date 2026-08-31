#include "zelda_rtl.h"
#include "variables.h"
#include "misc.h"
#include "load_gfx.h"
#include "nmi.h"
#include "poly.h"
#include "attract.h"
#include "snes/ppu.h"
#include "snes/snes_regs.h"
#include "snes/dma.h"
#include "spc_player.h"
#include "util.h"
#include "dump_state.h"
#include <SDL.h>
#include "audio.h"
#include "assets.h"
#include "android_logging.h"
#include "wide_camera.h"
#ifdef __3DS__
#include <3ds.h>
#include "platform_3ds.h"
#endif
/*
 * The saving functions have been rewritten in this file to support saving to external storage on android.
 */

ZeldaEnv g_zenv;
uint8 g_ram[131072];

uint32 g_wanted_zelda_features;
static int g_widescreen_edge_mode;

static void Startup_InitializeMemory();

static bool IsDungeonMapMenuActive(void) {
  // The supplied dump is module 14 / submodule 3 while preserving module 7.
  // That stays true during every map initialization state.
  return main_module_index == 14 && submodule_index == 3 &&
         saved_module_for_menu == 7;
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

static void ConfigurePpuSideSpace(int visual_x, bool fixed_camera,
                                  bool horizontal_transition) {
  // Let PPU impl know about the maximum allowed extra space on the sides and bottom
  int extra_right = 0, extra_left = 0, extra_bottom = 0;
  if (IsDungeonMapMenuActive()) {
    // Keep map background tiles and the sprite overlay in the same native
    // 256-pixel coordinate space while the dungeon map is open.
    PpuSetExtraSideSpace(g_zenv.ppu, 0, 0, 0);
    return;
  }
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
    if (horizontal_transition) {
      extra_left = extra_right = kPpuExtraLeftRight;
    } else if (fixed_camera) {
      int left = WideCamera_Unwrap16(ow_scroll_vars0.xstart, visual_x);
      int right = WideCamera_Unwrap16(ow_scroll_vars0.xend, visual_x);
      extra_left = IntMax(visual_x - left, 0);
      extra_right = IntMax(right - visual_x, 0);
    } else {
      extra_left = BG2HOFS_copy2 - ow_scroll_vars0.xstart;
      extra_right = ow_scroll_vars0.xend - BG2HOFS_copy2;
    }
    extra_bottom = ow_scroll_vars0.yend - BG2VOFS_copy2;
  } else if (mod == 7) {
    // indoors, except when the light cone is in use
    if (!(hdr_dungeon_dark_with_lantern && TS_copy != 0)) {
      int qm = quadrant_fullsize_x >> 1;
      if (horizontal_transition) {
        extra_left = extra_right = kPpuExtraLeftRight;
      } else if (fixed_camera) {
        int left = WideCamera_Unwrap16(room_bounds_x.v[qm], visual_x);
        int right = WideCamera_Unwrap16(room_bounds_x.v[qm + 2], visual_x);
        extra_left = IntMax(visual_x - left, 0);
        extra_right = IntMax(right - visual_x, 0);
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
  PpuSetExtraSideSpace(g_zenv.ppu, extra_left, extra_right, extra_bottom);
}

typedef struct FixedCameraTracker {
  bool initialized;
  bool transition_active;
  uint8 context;
  int direction;
  uint16 last_logical_x;
  int last_visual_x;
  uint16 transition_start_x;
  int transition_end_x;
  int transition_distance;
  int transition_start_visual_x;
  int transition_end_offset;
} FixedCameraTracker;

static FixedCameraTracker g_fixed_camera_tracker;

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

void ZeldaSetWidescreenEdgeMode(int mode) {
  int new_mode = mode == 1 ? 1 : 0;
  if (g_widescreen_edge_mode != new_mode)
    memset(&g_fixed_camera_tracker, 0, sizeof(g_fixed_camera_tracker));
  g_widescreen_edge_mode = new_mode;
}

int ZeldaGetWidescreenEdgeMode(void) {
  return g_widescreen_edge_mode;
}

int ZeldaGetWidescreenFixedCameraMargin(void) {
  bool visual_fixes_enabled =
    (enhanced_features0 & kFeatures0_WidescreenVisualFixes) != 0;
#ifdef ZELDA_CAMERA_TEST
  visual_fixes_enabled = true;
#endif
  uint8 context = GetFixedCameraEffectiveContext();
  if (g_widescreen_edge_mode != 1 ||
      !visual_fixes_enabled ||
      !g_zenv.ppu || g_zenv.ppu->extraLeftRight == 0 ||
      context == 0)
    return 0;
  if (IsDungeonMapMenuActive())
    return 0;
  if (context == 7 &&
      main_module_index == 14 && submodule_index == 7 &&
      overworld_map_state >= 4)
    return 0;
  if (context == 7) {
    if (hdr_dungeon_dark_with_lantern && TS_copy != 0)
      return 0;
  }
  return g_zenv.ppu->extraLeftRight;
}

typedef struct FixedCameraRenderState {
  bool active;
  bool uses_visual_camera;
  bool horizontal_transition;
  int visual_x;
  uint16 ppu_bg1_hscroll;
  uint16 ppu_bg2_hscroll;
  int16 ppu_obj_x_offset;
} FixedCameraRenderState;

static uint8 GetFixedCameraContext(void) {
  if (!ZeldaGetWidescreenFixedCameraMargin())
    return 0;
  return GetFixedCameraEffectiveContext();
}

static void GetFixedCameraBounds(uint8 context, int reference_x,
                                 int *left, int *right) {
  uint16 left_raw, right_raw;
  if (context == 7) {
    int qm = quadrant_fullsize_x >> 1;
    left_raw = room_bounds_x.v[qm];
    right_raw = room_bounds_x.v[qm + 2];
  } else {
    left_raw = ow_scroll_vars0.xstart;
    right_raw = ow_scroll_vars0.xend;
  }
  *left = WideCamera_Unwrap16(left_raw, reference_x);
  *right = WideCamera_Unwrap16(right_raw, reference_x);
}

static int GetFixedCameraTransitionDirection(uint8 context) {
  if (context == 9) {
    int direction = BYTE(overworld_screen_trans_dir_bits) & 3;
    return direction == 1 ? 1 : direction == 2 ? -1 : 0;
  }
  if ((submodule_index == 1 || submodule_index == 2) &&
      overworld_screen_transition >= 2) {
    return overworld_screen_transition == 2 ? 1 :
           overworld_screen_transition == 3 ? -1 : 0;
  }
  return 0;
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

static int GetFixedCameraTransitionEndOffset(uint8 context,
                                             int end_logical_x,
                                             int margin) {
  int left, right;
  if (context == 9) {
    uint16 destination_left = overworld_offset_base_x << 3;
    left = WideCamera_Unwrap16(destination_left, end_logical_x);
    right = left + (BYTE(overworld_area_is_big) ? 0x300 : 0x100);
  } else {
    GetFixedCameraBounds(context, end_logical_x, &left, &right);
  }
  int end_visual_x =
    WideCamera_ClampToBounds(end_logical_x, left, right, margin);
  return end_logical_x - end_visual_x;
}

static int CalculateFixedCameraVisualX(uint8 context, int margin,
                                       bool *transitioning) {
  const uint16 logical_x = BG2HOFS_copy2;
  int left, right;
  GetFixedCameraBounds(context, logical_x, &left, &right);
  int direction = GetFixedCameraTransitionDirection(context);
  *transitioning = direction != 0;

#ifdef ZELDA_CAMERA_TEST
  bool was_transition_active = g_fixed_camera_tracker.transition_active;
  int previous_direction = g_fixed_camera_tracker.direction;
  uint16 previous_logical_x = g_fixed_camera_tracker.last_logical_x;
  int previous_visual_x = g_fixed_camera_tracker.last_visual_x;
#endif

  if (!g_fixed_camera_tracker.initialized ||
      g_fixed_camera_tracker.context != context) {
    memset(&g_fixed_camera_tracker, 0, sizeof(g_fixed_camera_tracker));
    g_fixed_camera_tracker.initialized = true;
    g_fixed_camera_tracker.context = context;
    g_fixed_camera_tracker.last_logical_x = logical_x;
    g_fixed_camera_tracker.last_visual_x =
      WideCamera_ClampToBounds(logical_x, left, right, margin);
  }

  int visual_x;
  if (direction != 0) {
#ifdef ZELDA_CAMERA_TEST
    bool started_transition = false;
#endif
    if (!g_fixed_camera_tracker.transition_active ||
        g_fixed_camera_tracker.direction != direction) {
      int logical_step =
        (int16)(logical_x - g_fixed_camera_tracker.last_logical_x);
      int previous_offset =
        logical_x - g_fixed_camera_tracker.last_visual_x;
      bool use_previous_frame =
        logical_step >= -16 && logical_step <= 16 &&
        previous_offset >= -margin - 16 &&
        previous_offset <= margin + 16;
      g_fixed_camera_tracker.transition_start_x = use_previous_frame ?
        g_fixed_camera_tracker.last_logical_x : logical_x;
      g_fixed_camera_tracker.transition_start_visual_x = use_previous_frame ?
        g_fixed_camera_tracker.last_visual_x : logical_x - direction * margin;
      g_fixed_camera_tracker.transition_end_x =
        GetFixedCameraTransitionEndX(
          context, direction, g_fixed_camera_tracker.transition_start_x);
      g_fixed_camera_tracker.transition_distance =
        direction > 0 ?
          g_fixed_camera_tracker.transition_end_x -
            g_fixed_camera_tracker.transition_start_x :
          g_fixed_camera_tracker.transition_start_x -
            g_fixed_camera_tracker.transition_end_x;
      if (g_fixed_camera_tracker.transition_distance <= 0)
        g_fixed_camera_tracker.transition_distance = 256;
      g_fixed_camera_tracker.transition_active = true;
      g_fixed_camera_tracker.direction = direction;
#ifdef ZELDA_CAMERA_TEST
      started_transition = true;
#endif
    }
    if (logical_x == g_fixed_camera_tracker.transition_start_x) {
      g_fixed_camera_tracker.transition_end_offset =
        GetFixedCameraTransitionEndOffset(
          context, g_fixed_camera_tracker.transition_end_x, margin);
    }
#ifdef ZELDA_CAMERA_TEST
    if (started_transition) {
      fprintf(stderr,
              "camera transition context=%u dir=%d logical=%u visual=%d "
              "end=%d distance=%d end_offset=%d "
              "sub=%u room=%04x bounds=%04x,%04x,%04x,%04x qm=%u "
              "ow_base=%04x big=%u\n",
              context, direction,
              g_fixed_camera_tracker.transition_start_x,
              g_fixed_camera_tracker.transition_start_visual_x,
              g_fixed_camera_tracker.transition_end_x,
              g_fixed_camera_tracker.transition_distance,
              g_fixed_camera_tracker.transition_end_offset,
              submodule_index, dungeon_room_index,
              room_bounds_x.v[0], room_bounds_x.v[1],
              room_bounds_x.v[2], room_bounds_x.v[3],
              quadrant_fullsize_x >> 1,
              overworld_offset_base_x << 3,
              BYTE(overworld_area_is_big) != 0);
    }
#endif
    visual_x = WideCamera_InterpolateTransition(
      logical_x,
      g_fixed_camera_tracker.transition_start_x,
      g_fixed_camera_tracker.transition_start_visual_x,
      g_fixed_camera_tracker.transition_end_offset,
      direction, g_fixed_camera_tracker.transition_distance);
  } else {
    g_fixed_camera_tracker.transition_active = false;
    g_fixed_camera_tracker.direction = 0;
    visual_x = WideCamera_ClampToBounds(logical_x, left, right, margin);
  }

#ifdef ZELDA_CAMERA_TEST
  if (direction != 0 && was_transition_active &&
      previous_direction == direction) {
    int logical_step = direction *
      (int16)(logical_x - previous_logical_x);
    int visual_step = direction * (visual_x - previous_visual_x);
    if (logical_step >= 0 && visual_step < 0) {
      fprintf(stderr,
              "camera ERROR reversed during transition: context=%u "
              "dir=%d logical=%u->%u visual=%d->%d\n",
              context, direction, previous_logical_x, logical_x,
              previous_visual_x, visual_x);
    }
  } else if (direction == 0 && was_transition_active) {
    int exit_step = visual_x - previous_visual_x;
    if (exit_step < 0)
      exit_step = -exit_step;
    if (exit_step > 4) {
      fprintf(stderr,
              "camera ERROR jump after transition: context=%u "
              "logical=%u visual=%d->%d step=%d\n",
              context, logical_x, previous_visual_x, visual_x, exit_step);
    }
  }
#endif

  g_fixed_camera_tracker.last_logical_x = logical_x;
  g_fixed_camera_tracker.last_visual_x = visual_x;
  return visual_x;
}

static FixedCameraRenderState BeginFixedCameraRender(void) {
  FixedCameraRenderState state = {0};
  state.visual_x = BG2HOFS_copy2;
  int margin = ZeldaGetWidescreenFixedCameraMargin();
  uint8 context = GetFixedCameraContext();
  if (!margin || !context) {
    memset(&g_fixed_camera_tracker, 0, sizeof(g_fixed_camera_tracker));
    return state;
  }

  state.uses_visual_camera = true;
  state.visual_x = CalculateFixedCameraVisualX(
    context, margin, &state.horizontal_transition);
  int delta = (int16)(BG2HOFS_copy2 - (uint16)state.visual_x);
  if (delta == 0)
    return state;

  state.active = true;
  state.ppu_bg1_hscroll = g_zenv.ppu->bgLayer[0].hScroll;
  state.ppu_bg2_hscroll = g_zenv.ppu->bgLayer[1].hScroll;
  state.ppu_obj_x_offset = g_zenv.ppu->renderObjXOffset;

  g_zenv.ppu->bgLayer[0].hScroll =
      (state.ppu_bg1_hscroll - delta) & 0x3ff;
  g_zenv.ppu->bgLayer[1].hScroll =
      (state.ppu_bg2_hscroll - delta) & 0x3ff;
  g_zenv.ppu->renderObjXOffset = state.ppu_obj_x_offset + delta;
  return state;
}

static void EndFixedCameraRender(const FixedCameraRenderState *state) {
  if (!state->active)
    return;
  g_zenv.ppu->bgLayer[0].hScroll = state->ppu_bg1_hscroll;
  g_zenv.ppu->bgLayer[1].hScroll = state->ppu_bg2_hscroll;
  g_zenv.ppu->renderObjXOffset = state->ppu_obj_x_offset;
}

static void ZeldaDrawPpuLines(Ppu *ppu, int height,
                              int first_line, int last_line,
                              uint8 irq_state) {
  SimpleHdma hdma_chans[2];
  SimpleHdma_Init(&hdma_chans[0], &g_zenv.dma->channel[6]);
  SimpleHdma_Init(&hdma_chans[1], &g_zenv.dma->channel[7]);

  for (int i = 0; i <= height; i++) {
    if (i == 128 && irq_state) {
      ppu_write(ppu, (uint8)BG3HOFS, selectfile_var8);
      ppu_write(ppu, (uint8)BG3HOFS, selectfile_var8 >> 8);
      ppu_write(ppu, (uint8)BG3VOFS, 0);
      ppu_write(ppu, (uint8)BG3VOFS, 0);
    }
    if (i >= first_line && i <= last_line)
      ppu_runLine(ppu, i);
    SimpleHdma_DoLine(&hdma_chans[0], ppu);
    SimpleHdma_DoLine(&hdma_chans[1], ppu);
  }
}

#ifdef __3DS__
typedef struct PpuWorkerState {
  Ppu ppu;
  PpuTileCache tile_cache;
  int height;
  int first_line;
  int last_line;
  uint8 irq_state;
  uint64 duration_ticks;
  LightEvent done;
  Thread thread;
  bool running;
  uint32 job_id;
} PpuWorkerState;

static PpuWorkerState g_ppu_system_worker;
static PpuWorkerState g_ppu_new_worker;
static bool g_ppu_worker_initialized;
static int g_ppu_split_line = 112;
static int g_ppu_last_split_line = 112;
static int g_ppu_old3ds_worker_lines = 56;
static int g_ppu_old3ds_last_worker_lines = 56;
static uint64 g_ppu_main_duration_ticks;

static void ZeldaPpuWorkerMain(void *argument) {
  PpuWorkerState *state = (PpuWorkerState *)argument;
  uint32 completed_job = 0;
  while (__atomic_load_n(&state->running, __ATOMIC_ACQUIRE)) {
    uint32 job = __atomic_load_n(&state->job_id, __ATOMIC_ACQUIRE);
    if (job == completed_job) {
      __asm__ volatile("yield");
      continue;
    }
    uint64 start = svcGetSystemTick();
    ZeldaDrawPpuLines(&state->ppu, state->height,
                      state->first_line, state->last_line,
                      state->irq_state);
    state->duration_ticks = svcGetSystemTick() - start;
    completed_job = job;
    LightEvent_Signal(&state->done);
  }
}

static bool ZeldaCreatePpuWorker(PpuWorkerState *state,
                                 int core, s32 priority) {
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
    Result join_result = threadJoin(state->thread, 2000000000ull);
    if (R_FAILED(join_result))
      Platform3DS_LogRuntime("WARNING: PPU worker join timeout: 0x%08lx",
                             (unsigned long)join_result);
    threadFree(state->thread);
    state->thread = NULL;
  }
}

bool ZeldaGetPpuWorkerStats(int *split_line,
                            uint32 *main_time_us,
                            uint32 *worker_time_us) {
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

void ZeldaDrawPpuFrame(uint8 *pixel_buffer, size_t pitch, uint32 render_flags) {
  SimpleHdma hdma_probe;

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

  FixedCameraRenderState fixed_camera_state = BeginFixedCameraRender();

  if (g_zenv.ppu->extraLeftRight != 0 || render_flags & kPpuRenderFlags_Height240) {
    ConfigurePpuSideSpace(fixed_camera_state.visual_x,
                          fixed_camera_state.uses_visual_camera,
                          fixed_camera_state.horizontal_transition);
  }

  PpuSetWindow1Ext(g_zenv.ppu, g_spotlight_ext_active ? g_spotlight_ext_left : NULL,
                   g_spotlight_ext_active ? g_spotlight_ext_right : NULL);

  int height = render_flags & kPpuRenderFlags_Height240 ? 240 : 224;
  uint8 irq_state = irq_flag;

#ifdef __3DS__
  if (ZeldaEnsurePpuWorkers()) {
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
      memcpy(&state->ppu, g_zenv.ppu, sizeof(Ppu));
      state->ppu.tileCache = &state->tile_cache;
      state->height = height;
      state->irq_state = irq_state;
      uint32 job = state->job_id + 1;
      __atomic_store_n(&state->job_id, job, __ATOMIC_RELEASE);
    }

    uint64 main_start = svcGetSystemTick();
    ZeldaDrawPpuLines(g_zenv.ppu, height,
                      main_first, main_last, irq_state);
    g_ppu_main_duration_ticks = svcGetSystemTick() - main_start;
    for (size_t i = 0; i < countof(workers); i++) {
      if (workers[i]->thread)
        LightEvent_Wait(&workers[i]->done);
    }
  } else
#endif
  {
    ZeldaDrawPpuLines(g_zenv.ppu, height, 1, height, irq_state);
  }

  if (irq_state & 0x80) {
    irq_flag = 0;
    zelda_snes_dummy_write(NMITIMEN, 0x81);
  }
  EndFixedCameraRender(&fixed_camera_state);
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
  Module_MainRouting();
  NMI_PrepareSprites();
  nmi_boolean = 0;
}

void ZeldaInitialize() {
  g_zenv.dma = dma_init(NULL);
  g_zenv.ppu = ppu_init(NULL);
  g_zenv.ram = g_ram;
  g_zenv.sram = (uint8*)calloc(8192, 1);
  g_zenv.vram = g_zenv.ppu->vram;
  g_zenv.player = SpcPlayer_Create();
  SpcPlayer_Initialize(g_zenv.player);
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
  dma_reset(g_zenv.dma);
  ppu_reset(g_zenv.ppu);
  memset(g_zenv.ram, 0, 0x20000);
  if (!preserve_sram)
    memset(g_zenv.sram, 0, 0x2000);
  ZeldaApuLock();
  ZeldaRestoreMusicAfterLoad_Locked(true);
  ZeldaApuUnlock();
  EmuSynchronizeWholeState();

}

static void LoadSnesState(SaveLoadFunc *func, void *ctx) {
  // Do the actual loading
  ZeldaApuLock();
  InternalSaveLoad(func, ctx);
  memcpy(g_zenv.ram + 0x1DBA0, g_zenv.ram + 0x1b00, 224 * 2); // hdma table was moved

  ZeldaRestoreMusicAfterLoad_Locked(false);
  ZeldaApuUnlock();
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

void StateRecorder_Init(StateRecorder *sr) {
  memset(sr, 0, sizeof(*sr));
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

void StateRecorder_Save(StateRecorder* sr, SDL_RWops* rwops) {
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

bool ZeldaWriteDumpState(const char *dump_directory) {
#ifdef __3DS__
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
#ifdef __3DS__
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
  size_t snapshot_size = payload_size - sizeof(header);
  if (header.version != kZeldaDumpPayloadVersion ||
      header.snapshot_size != snapshot_size ||
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
    payload + payload_size,
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

  bool is_replay = state_recorder.replay_mode;

  // Either copy state or apply state
  if (is_replay) {
    inputs = StateRecorder_ReadNextReplayState(&state_recorder);
  } else {
    //    input_state = InputStateReadFromFile();
    StateRecorder_Record(&state_recorder, inputs);

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
    }
  }

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

  if (g_emu_runframe == NULL || enhanced_features0 != 0 || g_zenv.dialogue_flags) {
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
        break;
      }
    }
  }
  g_zenv.dialogue_blk = kDialogue(found.ptr[0]);
  g_zenv.dialogue_font_blk = kDialogueFont(found.ptr[1]);
  g_zenv.dialogue_flags = found.ptr[2];
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

#ifdef __3DS__
  Platform3DS_FormatSavePath(name, path, sizeof(path));
#else
  snprintf(path, sizeof(path), "%s", name);
#endif
  rwops = SDL_RWFromFileInExternal(path, cmd != kSaveLoad_Save ? "rb" : "wb");
  if (rwops) {
    printf("*** %s slot %d\n",
           cmd == kSaveLoad_Save ? "Saving" : cmd == kSaveLoad_Load ? "Loading" : "Replaying", which);

    if (cmd != kSaveLoad_Save)
      StateRecorder_Load(&state_recorder, rwops, cmd == kSaveLoad_Replay);
    else
      StateRecorder_Save(&state_recorder, rwops);

    SDL_RWclose(rwops);
  }
}

void ZeldaClearAutosave() {
  char path[256];
#ifdef __3DS__
  Platform3DS_FormatSavePath("saves/save0.sav", path, sizeof(path));
#else
  snprintf(path, sizeof(path), "%s", "saves/save0.sav");
#endif
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
  char path[256];
#ifdef __3DS__
  Platform3DS_FormatSavePath("saves/sram.dat", path, sizeof(path));
#else
  snprintf(path, sizeof(path), "%s", "saves/sram.dat");
#endif
  SDL_RWops *stream = SDL_RWFromFileInExternal(path, "rb");
  if (stream) {
    size_t bytesRead = SDL_RWread(stream, g_zenv.sram, 1, 8192);
    if (bytesRead != 8192) {
      fprintf(stderr, "Error reading saves/sram.dat\n");
    }
    SDL_RWclose(stream);
    EmuSynchronizeWholeState();
  }
}

void ZeldaWriteSram() {
  // Back up the existing save before overwriting it.
#ifdef __ANDROID__
  const char* externalPath = SDL_AndroidGetExternalStoragePath();
  if (externalPath) {
    char oldFilePath[256];
    char newFilePath[256];
    snprintf(oldFilePath, sizeof(oldFilePath), "%s/saves/sram.dat", externalPath);
    snprintf(newFilePath, sizeof(newFilePath), "%s/saves/sram.bak", externalPath);
    rename(oldFilePath, newFilePath);
  } else {
    fprintf(stderr, "External storage path not available.\n");
  }
#else
  char old_path[256];
  char bak_path[256];
#ifdef __3DS__
  Platform3DS_FormatSavePath("saves/sram.dat", old_path, sizeof(old_path));
  Platform3DS_FormatSavePath("saves/sram.bak", bak_path, sizeof(bak_path));
#else
  snprintf(old_path, sizeof(old_path), "%s", "saves/sram.dat");
  snprintf(bak_path, sizeof(bak_path), "%s", "saves/sram.bak");
#endif
  rename(old_path, bak_path);
#endif

  char path[256];
#ifdef __3DS__
  Platform3DS_FormatSavePath("saves/sram.dat", path, sizeof(path));
#else
  snprintf(path, sizeof(path), "%s", "saves/sram.dat");
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
}
#ifdef __3DS__
#include "platform_3ds.h"
#endif
