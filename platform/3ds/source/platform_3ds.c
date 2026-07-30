#include "platform_3ds.h"

#include <3ds.h>
#include <citro2d.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "assets.h"
#include "config.h"
#include "features.h"
#include "types.h"
#include "util.h"
#include "zelda_rtl.h"

static const char kStorageDirectory[] = "sdmc:/3ds/Zelda 3DS";
static const char kAssetsFilename[] = "zelda3_assets.dat";
static const char kTemporaryAssetsFilename[] = "zelda3_assets.tmp";
static const char kBundledPatch[] = "romfs:/zelda3_assets.bps";
static const char kBundledConfig[] = "romfs:/zelda3.ini";

static enum Platform3DSDisplayMode g_display_mode =
  kPlatform3DSDisplayStretch;
static enum Platform3DSWideMode g_wide_mode = kPlatform3DSWideStandard;
static enum Platform3DSCStickMode g_cstick_mode = kPlatform3DSCStickTurbo;
static int g_turbo_multiplier = 5;
static bool g_quick_dump_requested;
static aptHookCookie g_apt_hook_cookie;
static bool g_apt_hook_registered;
static volatile bool g_system_exit_requested;
static volatile bool g_system_suspended;
static bool g_irrst_initialized;
static bool g_is_new_3ds;
static bool g_model_detected;
static bool g_core1_time_enabled;
static int g_core1_time_limit_percent;
static bool g_version_overlay_visible;
static uint64_t g_version_overlay_hide_time_ms;
static uint64_t g_frame_timing_samples;
static uint64_t g_top_work_total_us;
static uint64_t g_total_work_total_us;
static uint64_t g_logic_work_total_us;
static uint64_t g_top_draw_total_us;
static uint64_t g_ppu_draw_total_us;
static uint64_t g_capture_total_us;
static uint64_t g_present_total_us;
static uint64_t g_bottom_work_total_us;
static uint64_t g_top_frames_over_budget;
static uint64_t g_total_frames_over_budget;
static uint32_t g_logic_work_max_us;
static uint32_t g_top_draw_max_us;
static uint32_t g_ppu_draw_max_us;
static uint32_t g_capture_max_us;
static uint32_t g_present_max_us;
static uint32_t g_bottom_work_max_us;
static uint32_t g_top_work_max_us;
static uint32_t g_total_work_max_us;
static uint64_t g_render_interval_samples;
static uint64_t g_render_interval_total_us;
static uint64_t g_scheduled_logic_frames;
static uint64_t g_timed_scheduled_logic_frames;
static uint64_t g_executed_logic_frames;
static uint64_t g_catchup_presentations;
static uint32_t g_max_scheduled_logic_frames;
static bool g_gpu_presenter_initialized;
static bool g_gpu_frame_active;
static bool g_setup_console_active;
static C3D_RenderTarget *g_top_target;
static C3D_RenderTarget *g_bottom_target;
static C3D_Tex g_top_texture;
static C3D_Tex g_bottom_texture;
static Tex3DS_SubTexture g_top_subtexture;
static Tex3DS_SubTexture g_bottom_subtexture;

enum {
  kTopTextureWidth = 512,
  kTopTextureHeight = 256,
};

static void LogSetup(const char *format, ...) {
  FILE *log = fopen("setup-progress.txt", "ab");
  if (!log)
    return;
  va_list arguments;
  va_start(arguments, format);
  vfprintf(log, format, arguments);
  va_end(arguments);
  fputc('\n', log);
  fclose(log);
}

void Platform3DS_LogRuntime(const char *format, ...) {
  FILE *log = fopen("runtime.log", "ab");
  if (!log)
    return;
  va_list arguments;
  va_start(arguments, format);
  vfprintf(log, format, arguments);
  va_end(arguments);
  fputc('\n', log);
  fclose(log);
}

static void Platform3DS_AptHook(APT_HookType hook, void *param) {
  (void)param;
  switch (hook) {
  case APTHOOK_ONSUSPEND:
  case APTHOOK_ONSLEEP:
    g_system_suspended = true;
    break;
  case APTHOOK_ONRESTORE:
  case APTHOOK_ONWAKEUP:
    g_system_suspended = false;
    break;
  case APTHOOK_ONEXIT:
    g_system_exit_requested = true;
    break;
  default:
    break;
  }
}

static void Platform3DS_RegisterAptHook(void) {
  if (g_apt_hook_registered)
    return;
  aptHook(&g_apt_hook_cookie, Platform3DS_AptHook, NULL);
  g_apt_hook_registered = true;
}

static void Platform3DS_DetectModel(void) {
  if (g_model_detected)
    return;
  bool is_new_3ds = false;
  if (R_SUCCEEDED(APT_CheckNew3DS(&is_new_3ds)))
    g_is_new_3ds = is_new_3ds;
  else
    g_is_new_3ds = false;
  g_model_detected = true;
}

static bool CStickIsHeld(u32 keys) {
  if (keys & (KEY_CSTICK_UP | KEY_CSTICK_DOWN |
              KEY_CSTICK_LEFT | KEY_CSTICK_RIGHT))
    return true;

  if (g_irrst_initialized) {
    circlePosition cstick = {0};
    hidCstickRead(&cstick);
    return abs((int)cstick.dx) > 24 || abs((int)cstick.dy) > 24;
  }
  return false;
}

uint16_t Platform3DS_ReadInput(bool *turbo_held, int *turbo_multiplier) {
  hidScanInput();
  u32 keys = hidKeysHeld();
  static bool quick_dump_combo_was_held;
  static bool version_combo_was_held;
  bool quick_dump_combo =
    (keys & (KEY_L | KEY_R | KEY_A)) == (KEY_L | KEY_R | KEY_A);
  if (quick_dump_combo && !quick_dump_combo_was_held)
    g_quick_dump_requested = true;
  quick_dump_combo_was_held = quick_dump_combo;

  bool version_combo =
    (keys & (KEY_L | KEY_R | KEY_B)) == (KEY_L | KEY_R | KEY_B);
  if (version_combo && !version_combo_was_held) {
    g_version_overlay_visible = !g_version_overlay_visible;
    g_version_overlay_hide_time_ms =
      g_version_overlay_visible ? osGetTime() + 5000 : 0;
  } else if (g_version_overlay_visible &&
             osGetTime() >= g_version_overlay_hide_time_ms) {
    g_version_overlay_visible = false;
    g_version_overlay_hide_time_ms = 0;
  }
  version_combo_was_held = version_combo;

  circlePosition circle;
  hidCircleRead(&circle);

  uint16_t input = 0;
  if ((keys & KEY_DUP) || circle.dy > 40) input |= 1u << 4;
  if ((keys & KEY_DDOWN) || circle.dy < -40) input |= 1u << 5;
  if ((keys & KEY_DLEFT) || circle.dx < -40) input |= 1u << 6;
  if ((keys & KEY_DRIGHT) || circle.dx > 40) input |= 1u << 7;
  if (keys & KEY_SELECT) input |= 1u << 2;
  if (keys & KEY_START) input |= 1u << 3;
  if ((keys & KEY_A) && !quick_dump_combo) input |= 1u << 8;
  if ((keys & KEY_B) && !g_version_overlay_visible && !version_combo)
    input |= 1u << 0;
  if (keys & KEY_X) input |= 1u << 9;
  if (keys & KEY_Y) input |= 1u << 1;
  if ((keys & KEY_L) && !version_combo) input |= 1u << 10;
  if ((keys & KEY_R) && !version_combo) input |= 1u << 11;
  *turbo_held = g_turbo_multiplier > 0 &&
                ((keys & (KEY_ZL | KEY_ZR)) != 0 || CStickIsHeld(keys));
  *turbo_multiplier = g_turbo_multiplier > 0 ? g_turbo_multiplier : 1;
  return input;
}

static char *Trim(char *text) {
  while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
    text++;
  char *end = text + strlen(text);
  while (end > text &&
         (end[-1] == ' ' || end[-1] == '\t' ||
          end[-1] == '\r' || end[-1] == '\n')) {
    *--end = 0;
  }
  return text;
}

static void LoadRuntimeSetting(const char *key, const char *value) {
  if (strcasecmp(key, "DisplayMode") == 0) {
    if (strcasecmp(value, "Original") == 0) {
      g_display_mode = kPlatform3DSDisplayOriginal;
    } else if (strcasecmp(value, "Stretch") == 0 ||
               strcasecmp(value, "Standard") == 0 ||
               strcasecmp(value, "UltraWideStretch") == 0) {
      g_display_mode = kPlatform3DSDisplayStretch;
    } else if (strcasecmp(value, "ForceWide") == 0 ||
               strcasecmp(value, "Force Wide") == 0 ||
               strcasecmp(value, "UltraWideMod") == 0) {
      g_display_mode = kPlatform3DSDisplayUltraWideMod;
      g_wide_mode = kPlatform3DSWideFixed;
    } else {
      g_display_mode = kPlatform3DSDisplayUltraWideMod;
    }
  } else if (strcasecmp(key, "WideMode") == 0 ||
             strcasecmp(key, "WideEdgeMode") == 0) {
    if (strcasecmp(value, "Fixed") == 0 ||
        strcasecmp(value, "FixedCamera") == 0 ||
        strcasecmp(value, "Force") == 0 ||
        strcasecmp(value, "ForceWide") == 0 ||
        strcasecmp(value, "LogicWide") == 0 ||
        strcasecmp(value, "Logic") == 0 ||
        strcasecmp(value, "ExtendedSprites") == 0)
      g_wide_mode = kPlatform3DSWideFixed;
    else
      g_wide_mode = kPlatform3DSWideStandard;
  } else if (strcasecmp(key, "CStickMode") == 0) {
    if (strcasecmp(value, "Disabled") == 0 ||
        strcasecmp(value, "Off") == 0) {
      g_cstick_mode = kPlatform3DSCStickDisabled;
      g_turbo_multiplier = 0;
    } else {
      g_cstick_mode = kPlatform3DSCStickTurbo;
    }
  } else if (strcasecmp(key, "CStickTurboMultiplier") == 0) {
    if (strcasecmp(value, "Off") == 0 || strcasecmp(value, "Disabled") == 0) {
      g_turbo_multiplier = 0;
      return;
    }
    int multiplier = atoi(value);
    if (multiplier <= 0)
      multiplier = 0;
    else if (multiplier < 2)
      multiplier = 2;
    if (multiplier > 5)
      multiplier = 5;
    g_turbo_multiplier = multiplier;
  }
}

void Platform3DS_LoadRuntimeSettings(void) {
  FILE *file = fopen("zelda3.ini", "rb");
  if (!file)
    return;
  bool in_general = false;
  char line[256];
  while (fgets(line, sizeof(line), file)) {
    char *text = Trim(line);
    if (text[0] == 0 || text[0] == '#' || text[0] == ';')
      continue;
    if (text[0] == '[') {
      in_general = strcasecmp(text, "[General]") == 0;
      continue;
    }
    if (!in_general)
      continue;
    char *equals = strchr(text, '=');
    if (!equals)
      continue;
    *equals = 0;
    LoadRuntimeSetting(Trim(text), Trim(equals + 1));
  }
  fclose(file);
}

enum Platform3DSDisplayMode Platform3DS_GetDisplayMode(void) {
  return g_display_mode;
}

void Platform3DS_SetDisplayMode(enum Platform3DSDisplayMode mode) {
  Platform3DS_DetectModel();
  if (mode > kPlatform3DSDisplayStretch)
    mode = kPlatform3DSDisplayUltraWideMod;
  g_display_mode = mode;
  ZeldaSetWidescreenFixedMode(
    g_display_mode == kPlatform3DSDisplayUltraWideMod &&
    g_wide_mode == kPlatform3DSWideFixed);
  Platform3DS_LogRuntime("Display mode set: %d", (int)g_display_mode);
}

bool Platform3DS_IsNew3DS(void) {
  Platform3DS_DetectModel();
  return g_is_new_3ds;
}

bool Platform3DS_CanUseCore1PpuWorker(void) {
  return g_core1_time_enabled && g_core1_time_limit_percent > 0;
}

bool Platform3DS_IsVersionOverlayVisible(void) {
  if (g_version_overlay_visible &&
      osGetTime() >= g_version_overlay_hide_time_ms) {
    g_version_overlay_visible = false;
    g_version_overlay_hide_time_ms = 0;
  }
  return g_version_overlay_visible;
}

enum Platform3DSWideMode Platform3DS_GetWideMode(void) {
  return g_wide_mode;
}

void Platform3DS_SetWideMode(enum Platform3DSWideMode mode) {
  if (mode > kPlatform3DSWideFixed)
    mode = kPlatform3DSWideStandard;
  g_wide_mode = mode;
  g_config.features0 &= ~kFeatures0_ExtendScreen64;
  ZeldaSetWidescreenFixedMode(
    g_display_mode == kPlatform3DSDisplayUltraWideMod &&
    g_wide_mode == kPlatform3DSWideFixed);
  g_wanted_zelda_features = g_config.features0;
  Platform3DS_LogRuntime("Wide mode set: %d", (int)g_wide_mode);
}

bool Platform3DS_ShouldExit(void) {
  if (g_system_exit_requested || aptShouldClose())
    return true;
  if (!aptMainLoop()) {
    g_system_exit_requested = true;
    return true;
  }
  if (g_system_suspended || !aptIsActive() || aptShouldJumpToHome()) {
    Platform3DS_EndFrame();
    while (!aptShouldClose() && aptMainLoop() &&
           (g_system_suspended || !aptIsActive() || aptShouldJumpToHome())) {
      aptHandleSleep();
      gspWaitForVBlank();
    }
    if (aptShouldClose()) {
      g_system_exit_requested = true;
      return true;
    }
  }
  return false;
}

bool Platform3DS_IsSystemClosing(void) {
  return g_system_exit_requested || aptShouldClose();
}

enum Platform3DSCStickMode Platform3DS_GetCStickMode(void) {
  return g_cstick_mode;
}

void Platform3DS_SetCStickMode(enum Platform3DSCStickMode mode) {
  if (mode > kPlatform3DSCStickDisabled)
    mode = kPlatform3DSCStickTurbo;
  g_cstick_mode = mode;
  Platform3DS_LogRuntime("C-stick mode set: %d", (int)g_cstick_mode);
}

int Platform3DS_GetTurboMultiplier(void) {
  return g_turbo_multiplier;
}

bool Platform3DS_TakeQuickDumpRequest(void) {
  bool requested = g_quick_dump_requested;
  g_quick_dump_requested = false;
  return requested;
}

void Platform3DS_SetTurboMultiplier(int multiplier) {
  if (multiplier <= 0)
    multiplier = 0;
  else if (multiplier < 2)
    multiplier = 2;
  if (multiplier > 5)
    multiplier = 5;
  g_turbo_multiplier = multiplier;
  Platform3DS_LogRuntime("Turbo multiplier set: %d", g_turbo_multiplier);
}

bool Platform3DS_InitTopPresenter(void) {
  Platform3DS_RegisterAptHook();
  Platform3DS_DetectModel();

  // This is a no-op on Old 3DS and enables 804 MHz operation for 3DSX builds
  // on New 3DS. CIA builds also request the faster clock in their exheader.
  osSetSpeedupEnable(true);
  g_irrst_initialized = R_SUCCEEDED(irrstInit());
  aptSetHomeAllowed(true);
  aptSetSleepAllowed(true);

  // Reserve part of the system core for a parallel PPU segment.
  //
  // Some systems accept the APT call but report 0% for small requests. Try the
  // libctru/homebrew maximum first and fall back until APT gives us a real,
  // non-zero budget. If all requests become 0%, Core 1 is not usable and the
  // renderer must not create a worker there.
  const u32 core1_candidates[] = {80, 70, 50, 30};
  g_core1_time_enabled = false;
  g_core1_time_limit_percent = 0;
  for (size_t i = 0; i < sizeof(core1_candidates) / sizeof(core1_candidates[0]);
       i++) {
    Result set_result = APT_SetAppCpuTimeLimit(core1_candidates[i]);
    if (R_SUCCEEDED(set_result)) {
      u32 actual_percent = 0;
      Result get_result = APT_GetAppCpuTimeLimit(&actual_percent);
      Platform3DS_LogRuntime(
        "Core 1 PPU budget request: wanted=%lu%% actual=%lu%% get=0x%08lx",
        (unsigned long)core1_candidates[i],
        (unsigned long)actual_percent,
        (unsigned long)get_result);
      if (actual_percent > 0) {
        g_core1_time_limit_percent = (int)actual_percent;
        g_core1_time_enabled = true;
        break;
      }
      g_core1_time_limit_percent = 0;
    } else {
      Platform3DS_LogRuntime(
        "Core 1 PPU budget request failed: wanted=%lu%% result=0x%08lx",
        (unsigned long)core1_candidates[i],
        (unsigned long)set_result);
    }
  }
  if (!g_core1_time_enabled) {
    Platform3DS_LogRuntime(
      "Core 1 PPU budget unavailable; disabling Core 1 worker");
  }

  // Zelda's source image is derived from the SNES 15-bit palette. RGB565 keeps
  // that detail while halving the top framebuffer bandwidth versus RGBA8.
  gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
  gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
  gfxSetDoubleBuffering(GFX_TOP, true);
  gfxSetDoubleBuffering(GFX_BOTTOM, true);

  if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
    Platform3DS_LogRuntime("ERROR: unable to initialize Citro2D presenter");
    return false;
  }
  if (!C2D_Init(64)) {
    C3D_Fini();
    Platform3DS_LogRuntime("ERROR: unable to initialize Citro2D presenter");
    return false;
  }
  C2D_Prepare();
  if (!C3D_TexInitVRAM(&g_top_texture, kTopTextureWidth,
                       kTopTextureHeight, GPU_RGBA8)) {
    C2D_Fini();
    C3D_Fini();
    Platform3DS_LogRuntime("ERROR: unable to allocate top GPU texture");
    return false;
  }
  C3D_TexSetFilter(&g_top_texture, GPU_NEAREST, GPU_NEAREST);
  C3D_TexSetWrap(&g_top_texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
  if (!C3D_TexInitVRAM(&g_bottom_texture, kTopTextureWidth,
                       kTopTextureHeight, GPU_RGBA8)) {
    C3D_TexDelete(&g_top_texture);
    C2D_Fini();
    C3D_Fini();
    Platform3DS_LogRuntime("ERROR: unable to allocate bottom GPU texture");
    return false;
  }
  C3D_TexSetFilter(&g_bottom_texture, GPU_NEAREST, GPU_NEAREST);
  C3D_TexSetWrap(&g_bottom_texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

  g_top_target = C3D_RenderTargetCreate(
    GSP_SCREEN_WIDTH, GSP_SCREEN_HEIGHT_TOP,
    GPU_RB_RGBA8, GPU_RB_DEPTH16);
  if (!g_top_target) {
    C3D_TexDelete(&g_bottom_texture);
    C3D_TexDelete(&g_top_texture);
    C2D_Fini();
    C3D_Fini();
    Platform3DS_LogRuntime("ERROR: unable to allocate top GPU target");
    return false;
  }
  C3D_RenderTargetSetOutput(
    g_top_target, GFX_TOP, GFX_LEFT,
    GX_TRANSFER_FLIP_VERT(0) |
      GX_TRANSFER_OUT_TILED(0) |
      GX_TRANSFER_RAW_COPY(0) |
      GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
      GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
      GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
  g_bottom_target = C3D_RenderTargetCreate(
    GSP_SCREEN_WIDTH, GSP_SCREEN_HEIGHT_BOTTOM,
    GPU_RB_RGBA8, GPU_RB_DEPTH16);
  if (!g_bottom_target) {
    C3D_RenderTargetDelete(g_top_target);
    g_top_target = NULL;
    C3D_TexDelete(&g_bottom_texture);
    C3D_TexDelete(&g_top_texture);
    C2D_Fini();
    C3D_Fini();
    Platform3DS_LogRuntime("ERROR: unable to allocate bottom GPU target");
    return false;
  }
  C3D_RenderTargetSetOutput(
    g_bottom_target, GFX_BOTTOM, GFX_LEFT,
    GX_TRANSFER_FLIP_VERT(0) |
      GX_TRANSFER_OUT_TILED(0) |
      GX_TRANSFER_RAW_COPY(0) |
      GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
      GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
      GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
  g_gpu_presenter_initialized = true;

  g_frame_timing_samples = 0;
  g_top_work_total_us = 0;
  g_total_work_total_us = 0;
  g_logic_work_total_us = 0;
  g_top_draw_total_us = 0;
  g_ppu_draw_total_us = 0;
  g_capture_total_us = 0;
  g_present_total_us = 0;
  g_bottom_work_total_us = 0;
  g_top_frames_over_budget = 0;
  g_total_frames_over_budget = 0;
  g_logic_work_max_us = 0;
  g_top_draw_max_us = 0;
  g_ppu_draw_max_us = 0;
  g_capture_max_us = 0;
  g_present_max_us = 0;
  g_bottom_work_max_us = 0;
  g_top_work_max_us = 0;
  g_total_work_max_us = 0;
  g_render_interval_samples = 0;
  g_render_interval_total_us = 0;
  g_scheduled_logic_frames = 0;
  g_timed_scheduled_logic_frames = 0;
  g_executed_logic_frames = 0;
  g_catchup_presentations = 0;
  g_max_scheduled_logic_frames = 0;
  Platform3DS_LogRuntime(
    "Top presenter: PICA200 RGB565, 60 Hz timer pacing, New 3DS=%s, "
    "Core 1 PPU budget=%s%d%%",
    g_is_new_3ds ? "yes" : "no",
    Platform3DS_CanUseCore1PpuWorker() ? "" : "unavailable/",
    g_core1_time_limit_percent);
  return gfxGetScreenFormat(GFX_TOP) == GSP_RGB565_OES;
}

void Platform3DS_ShutdownTopPresenter(void) {
  if (!g_gpu_presenter_initialized)
    return;
  Platform3DS_EndFrame();
  if (!Platform3DS_IsSystemClosing())
    C3D_FrameSync();
  C3D_RenderTargetDelete(g_bottom_target);
  g_bottom_target = NULL;
  C3D_RenderTargetDelete(g_top_target);
  g_top_target = NULL;
  C3D_TexDelete(&g_bottom_texture);
  C3D_TexDelete(&g_top_texture);
  C2D_Fini();
  C3D_Fini();
  g_gpu_presenter_initialized = false;
  if (g_apt_hook_registered) {
    aptUnhook(&g_apt_hook_cookie);
    g_apt_hook_registered = false;
  }
  if (g_irrst_initialized) {
    irrstExit();
    g_irrst_initialized = false;
  }
}

static void ConfigureArgbTextureEnv(void) {
  C3D_TexEnv *env = C3D_GetTexEnv(0);
  C3D_TexEnvInit(env);
  C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
  C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_G,
                  GPU_TEVOP_RGB_SRC_COLOR,
                  GPU_TEVOP_RGB_SRC_COLOR);
  C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
  C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, GPU_CONSTANT, GPU_CONSTANT);
  C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
  C3D_TexEnvColor(env, C2D_Color32(255, 0, 0, 255));

  env = C3D_GetTexEnv(1);
  C3D_TexEnvInit(env);
  C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
  C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_B,
                  GPU_TEVOP_RGB_SRC_COLOR,
                  GPU_TEVOP_RGB_SRC_COLOR);
  C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
  C3D_TexEnvColor(env, C2D_Color32(0, 255, 0, 255));

  env = C3D_GetTexEnv(2);
  C3D_TexEnvInit(env);
  C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
  C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_ALPHA,
                  GPU_TEVOP_RGB_SRC_COLOR,
                  GPU_TEVOP_RGB_SRC_COLOR);
  C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
  C3D_TexEnvColor(env, C2D_Color32(0, 0, 255, 255));
}

void Platform3DS_PresentTopFrame(const uint8_t *pixels, int pitch,
                                 int width, int height) {
  if (!g_gpu_presenter_initialized || !pixels ||
      pitch != kTopTextureWidth * 4 ||
      width <= 0 || width > kTopTextureWidth ||
      height <= 0 || height > kTopTextureHeight)
    return;

  if (!C3D_FrameBegin(0))
    return;
  g_gpu_frame_active = true;
  GSPGPU_FlushDataCache(pixels,
                        kTopTextureWidth * kTopTextureHeight * 4);
  C3D_SyncDisplayTransfer(
    (u32 *)pixels, GX_BUFFER_DIM(kTopTextureWidth, kTopTextureHeight),
    (u32 *)g_top_texture.data,
    GX_BUFFER_DIM(kTopTextureWidth, kTopTextureHeight),
    GX_TRANSFER_FLIP_VERT(0) |
      GX_TRANSFER_OUT_TILED(1) |
      GX_TRANSFER_RAW_COPY(0) |
      GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
      GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
      GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));

  const bool stretch = g_display_mode == kPlatform3DSDisplayStretch;
  const float draw_width = stretch ? (float)GSP_SCREEN_HEIGHT_TOP :
                                     (float)width;
  const float draw_height =
    height < GSP_SCREEN_WIDTH ? (float)height : (float)GSP_SCREEN_WIDTH;
  g_top_subtexture = (Tex3DS_SubTexture){
    .width = (u16)width,
    .height = (u16)height,
    .left = 0.0f,
    .top = 1.0f,
    .right = (float)width / kTopTextureWidth,
    .bottom = 1.0f - (float)height / kTopTextureHeight,
  };
  C2D_Image image = {
    .tex = &g_top_texture,
    .subtex = &g_top_subtexture,
  };
  C2D_DrawParams params = {
    .pos = {
      .x = (GSP_SCREEN_HEIGHT_TOP - draw_width) * 0.5f,
      .y = (GSP_SCREEN_WIDTH - draw_height) * 0.5f,
      .w = draw_width,
      .h = draw_height,
    },
    .center = { 0.0f, 0.0f },
    .depth = 0.0f,
    .angle = 0.0f,
  };

  C2D_TargetClear(g_top_target, C2D_Color32(0, 0, 0, 255));
  C2D_SceneBegin(g_top_target);
  C2D_DrawImage(image, &params, NULL);
  ConfigureArgbTextureEnv();
}

void Platform3DS_PresentBottomFrame(const uint8_t *pixels, int pitch,
                                    int width, int height) {
  if (!pixels || pitch != kTopTextureWidth * 4 ||
      width <= 0 || width > kTopTextureWidth ||
      height <= 0 || height > kTopTextureHeight)
    return;

  bool bottom_only_frame = false;
  if (!g_gpu_frame_active) {
    if (!C3D_FrameBegin(0))
      return;
    g_gpu_frame_active = true;
    bottom_only_frame = true;
  }

  GSPGPU_FlushDataCache(pixels,
                        kTopTextureWidth * kTopTextureHeight * 4);
  C3D_SyncDisplayTransfer(
    (u32 *)pixels, GX_BUFFER_DIM(kTopTextureWidth, kTopTextureHeight),
    (u32 *)g_bottom_texture.data,
    GX_BUFFER_DIM(kTopTextureWidth, kTopTextureHeight),
    GX_TRANSFER_FLIP_VERT(0) |
      GX_TRANSFER_OUT_TILED(1) |
      GX_TRANSFER_RAW_COPY(0) |
      GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
      GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
      GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));

  g_bottom_subtexture = (Tex3DS_SubTexture){
    .width = (u16)width,
    .height = (u16)height,
    .left = 0.0f,
    .top = 1.0f,
    .right = (float)width / kTopTextureWidth,
    .bottom = 1.0f - (float)height / kTopTextureHeight,
  };
  C2D_Image image = {
    .tex = &g_bottom_texture,
    .subtex = &g_bottom_subtexture,
  };
  C2D_DrawParams params = {
    .pos = {
      .x = (GSP_SCREEN_HEIGHT_BOTTOM - width) * 0.5f,
      .y = (GSP_SCREEN_WIDTH - height) * 0.5f,
      .w = (float)width,
      .h = (float)height,
    },
    .center = { 0.0f, 0.0f },
    .depth = 0.0f,
    .angle = 0.0f,
  };
  C2D_TargetClear(g_bottom_target, C2D_Color32(0, 0, 0, 255));
  C2D_SceneBegin(g_bottom_target);
  C2D_DrawImage(image, &params, NULL);
  ConfigureArgbTextureEnv();
  if (bottom_only_frame)
    Platform3DS_EndFrame();
}

void Platform3DS_EndFrame(void) {
  if (!g_gpu_frame_active)
    return;
  C3D_FrameEnd(0);
  g_gpu_frame_active = false;
}

uint32_t Platform3DS_WaitForVBlank(void) {
  uint64_t before = svcGetSystemTick();
  // Consume an already-signaled VBlank when rendering crossed the refresh
  // boundary. Waiting for an additional refresh here turns a small miss into
  // a full-frame stutter; Citro3D serializes framebuffer transfers itself.
  gspWaitForEvent(GSPGPU_EVENT_VBlank0, false);
  uint64_t elapsed = svcGetSystemTick() - before;
  return (uint32_t)(elapsed * 1000000ull / SYSCLOCK_ARM11);
}

void Platform3DS_RecordFrameTiming(uint32_t logic_work_us,
                                   uint32_t top_draw_us,
                                   uint32_t ppu_draw_us,
                                   uint32_t capture_us,
                                   uint32_t present_us,
                                   uint32_t top_work_us,
                                   uint32_t bottom_work_us,
                                   uint32_t total_work_us,
                                   uint32_t render_interval_us,
                                   int scheduled_logic_frames,
                                   int executed_logic_frames) {
  g_frame_timing_samples++;
  g_logic_work_total_us += logic_work_us;
  g_top_draw_total_us += top_draw_us;
  g_ppu_draw_total_us += ppu_draw_us;
  g_capture_total_us += capture_us;
  g_present_total_us += present_us;
  g_top_work_total_us += top_work_us;
  g_bottom_work_total_us += bottom_work_us;
  g_total_work_total_us += total_work_us;
  if (logic_work_us > g_logic_work_max_us)
    g_logic_work_max_us = logic_work_us;
  if (top_draw_us > g_top_draw_max_us)
    g_top_draw_max_us = top_draw_us;
  if (ppu_draw_us > g_ppu_draw_max_us)
    g_ppu_draw_max_us = ppu_draw_us;
  if (capture_us > g_capture_max_us)
    g_capture_max_us = capture_us;
  if (present_us > g_present_max_us)
    g_present_max_us = present_us;
  if (top_work_us > g_top_work_max_us)
    g_top_work_max_us = top_work_us;
  if (bottom_work_us > g_bottom_work_max_us)
    g_bottom_work_max_us = bottom_work_us;
  if (total_work_us > g_total_work_max_us)
    g_total_work_max_us = total_work_us;
  if (top_work_us > 16667)
    g_top_frames_over_budget++;
  if (total_work_us > 16667)
    g_total_frames_over_budget++;
  if (render_interval_us != 0) {
    g_render_interval_samples++;
    g_render_interval_total_us += render_interval_us;
    if (scheduled_logic_frames > 0)
      g_timed_scheduled_logic_frames +=
        (uint32_t)scheduled_logic_frames;
  }
  if (scheduled_logic_frames > 0) {
    g_scheduled_logic_frames += (uint32_t)scheduled_logic_frames;
    if (scheduled_logic_frames > 1)
      g_catchup_presentations++;
    if ((uint32_t)scheduled_logic_frames > g_max_scheduled_logic_frames)
      g_max_scheduled_logic_frames = (uint32_t)scheduled_logic_frames;
  }
  if (executed_logic_frames > 0)
    g_executed_logic_frames += (uint32_t)executed_logic_frames;
}

static bool HasExtension(const char *name, const char *extension) {
  size_t name_length = strlen(name);
  size_t extension_length = strlen(extension);
  if (name_length < extension_length)
    return false;
  return strcasecmp(name + name_length - extension_length, extension) == 0;
}

static bool IsRegularFile(const char *path) {
  struct stat info;
  return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static bool CopyFileIfMissing(const char *source, const char *destination) {
  if (IsRegularFile(destination))
    return true;

  FILE *input = fopen(source, "rb");
  if (!input)
    return false;
  FILE *output = fopen(destination, "wb");
  if (!output) {
    fclose(input);
    return false;
  }

  bool success = true;
  char buffer[4096];
  size_t count;
  while ((count = fread(buffer, 1, sizeof(buffer), input)) != 0) {
    if (fwrite(buffer, 1, count, output) != count) {
      success = false;
      break;
    }
  }
  if (ferror(input))
    success = false;
  if (fclose(output) != 0)
    success = false;
  fclose(input);

  if (!success)
    remove(destination);
  return success;
}

static bool AssetsFileLooksValid(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file)
    return false;

  uint8 header[88];
  bool valid = fread(header, 1, sizeof(header), file) == sizeof(header);
  if (valid) {
    static const char signature[] = { kAssets_Sig };
    uint32 count;
    memcpy(&count, header + 80, sizeof(count));
    valid = memcmp(header, signature, sizeof(signature)) == 0 &&
            count == kNumberOfAssets;
  }
  fclose(file);
  return valid;
}

static bool FindRom(char *path, size_t path_size) {
  static const char *const preferred_names[] = {
    "zelda3.sfc",
    "Zelda 3.sfc",
    "zelda3.smc",
  };
  for (size_t i = 0; i < countof(preferred_names); i++) {
    if (IsRegularFile(preferred_names[i])) {
      snprintf(path, path_size, "%s", preferred_names[i]);
      return true;
    }
  }

  DIR *directory = opendir(".");
  if (!directory)
    return false;
  bool found = false;
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL) {
    if ((HasExtension(entry->d_name, ".sfc") ||
         HasExtension(entry->d_name, ".smc")) &&
        IsRegularFile(entry->d_name)) {
      snprintf(path, path_size, "%s", entry->d_name);
      found = true;
      break;
    }
  }
  closedir(directory);
  return found;
}

static void BeginSetupConsole(void) {
  if (g_setup_console_active)
    return;
  gfxInitDefault();
  consoleInit(GFX_TOP, NULL);
  consoleClear();
  aptSetHomeAllowed(true);
  aptSetSleepAllowed(true);
  g_setup_console_active = true;
}

static void PresentSetupConsole(void) {
  gfxFlushBuffers();
  gfxSwapBuffers();
  gspWaitForVBlank();
}

static void EndSetupConsole(void) {
  if (!g_setup_console_active)
    return;
  PresentSetupConsole();
  gfxExit();
  g_setup_console_active = false;
}

static u32 WaitForButtons(u32 accepted) {
  while (aptMainLoop()) {
    hidScanInput();
    u32 down = hidKeysDown();
    if (down & accepted)
      return down & accepted;
    gspWaitForVBlank();
  }
  return KEY_B;
}

static void ShowFatalSetupError(const char *message) {
  FILE *log = fopen("setup-error.txt", "wb");
  if (log) {
    fprintf(log, "Zelda 3DS v%s\n%s\n", ZELDA3_3DS_VERSION, message);
    fclose(log);
  }
  consoleClear();
  printf("\x1b[2;2HZelda 3DS v%s\n\n", ZELDA3_3DS_VERSION);
  printf("%s\n\n", message);
  printf("Press B to exit.");
  PresentSetupConsole();
  WaitForButtons(KEY_B | KEY_START);
}

void Platform3DS_ShowFatalError(const char *message) {
  Platform3DS_LogRuntime("FATAL: %s", message ? message : "(null)");
  if (g_gpu_presenter_initialized)
    Platform3DS_ShutdownTopPresenter();
  bool already_in_console = g_setup_console_active;
  if (!already_in_console)
    BeginSetupConsole();
  ShowFatalSetupError(message ? message : "Unknown fatal error.");
  if (!already_in_console)
    EndSetupConsole();
}

static bool ConfirmExtraction(void) {
  consoleClear();
  printf("\x1b[2;2HZelda 3DS v%s\n\n", ZELDA3_3DS_VERSION);
  printf("Game assets were not found.\n\n");
  printf("Place your legal ROM in:\n");
  printf("sdmc:/3ds/Zelda 3DS/\n\n");
  printf("Accepted formats: .sfc or .smc\n");
  printf("Required region: USA, no header\n\n");
  printf("[A] Extract assets\n");
  printf("[B] Exit\n");
  PresentSetupConsole();
  return (WaitForButtons(KEY_A | KEY_B) & KEY_A) != 0;
}

static bool WriteAssetsFile(const uint8 *data, size_t size) {
  FILE *output = fopen(kTemporaryAssetsFilename, "wb");
  if (!output)
    return false;
  bool success = fwrite(data, 1, size, output) == size;
  if (fclose(output) != 0)
    success = false;
  if (!success) {
    remove(kTemporaryAssetsFilename);
    return false;
  }

  remove(kAssetsFilename);
  if (rename(kTemporaryAssetsFilename, kAssetsFilename) != 0) {
    remove(kTemporaryAssetsFilename);
    return false;
  }
  return true;
}

static bool ExtractAssetsFromRom(void) {
  LogSetup("Extraction requested");
  char rom_path[512];
  if (!FindRom(rom_path, sizeof(rom_path))) {
    LogSetup("ROM search failed");
    ShowFatalSetupError(
      "Error: no .sfc or .smc ROM was found\n"
      "in the Zelda 3DS folder.");
    return false;
  }
  LogSetup("ROM found: %s", rom_path);

  consoleClear();
  printf("\x1b[2;2HZelda 3DS v%s\n\n", ZELDA3_3DS_VERSION);
  printf("ROM found: %s\n\n", rom_path);
  printf("Validating and extracting assets...\n");
  printf("Do not power off the console.");
  PresentSetupConsole();

  size_t rom_size = 0;
  size_t patch_size = 0;
  size_t assets_size = 0;
  uint8 *rom = ReadWholeFile(rom_path, &rom_size);
  LogSetup("ROM read: %lu bytes", (unsigned long)rom_size);
  uint8 *patch = ReadWholeFile(kBundledPatch, &patch_size);
  LogSetup("Patch read: %lu bytes", (unsigned long)patch_size);
  if (!rom || !patch) {
    char error[256];
    snprintf(error, sizeof(error),
      "Error reading files.\n"
      "ROM: %s (%lu bytes)\n"
      "Internal patch: %s (%lu bytes)",
      rom ? "OK" : "FAILED", (unsigned long)rom_size,
      patch ? "OK" : "FAILED", (unsigned long)patch_size);
    free(rom);
    free(patch);
    ShowFatalSetupError(error);
    return false;
  }

  LogSetup("Applying BPS patch");
  uint8 *assets = ApplyBps(rom, rom_size, patch, patch_size, &assets_size);
  LogSetup("BPS result: %s, %lu bytes", assets ? "OK" : "FAIL",
           (unsigned long)assets_size);
  free(rom);
  free(patch);
  if (!assets) {
    ShowFatalSetupError(
      "Error: the ROM is not the correct version.\n"
      "Required: Zelda 3 USA, no header.\n"
      "Expected SHA-256:\n"
      "66871d66be19ad2c34c927d6b14cd8eb\n"
      "6fc3181965b6e517cb361f7316009cfb");
    return false;
  }

  bool written = WriteAssetsFile(assets, assets_size);
  LogSetup("Assets write: %s", written ? "OK" : "FAIL");
  free(assets);
  if (!written || !AssetsFileLooksValid(kAssetsFilename)) {
    ShowFatalSetupError(
      "Error saving zelda3_assets.dat.\n"
      "Check free space and the SD card.");
    return false;
  }

  consoleClear();
  printf("\x1b[2;2HZelda 3DS v%s\n\n", ZELDA3_3DS_VERSION);
  printf("Assets extracted successfully.\n\n");
  printf("Setup complete.");
  PresentSetupConsole();
  for (int i = 0; i < 60; i++)
    gspWaitForVBlank();
  return true;
}

bool Platform3DS_PrepareStorage(void) {
  mkdir("sdmc:/3ds", 0777);
  if (mkdir(kStorageDirectory, 0777) != 0 && errno != EEXIST)
    return false;
  if (chdir(kStorageDirectory) != 0)
    return false;

  remove("setup-progress.txt");
  remove("runtime.log");
  LogSetup("Zelda 3DS v%s setup started", ZELDA3_3DS_VERSION);
  Platform3DS_LogRuntime("Zelda 3DS v%s runtime started", ZELDA3_3DS_VERSION);
  CopyFileIfMissing(kBundledConfig, "zelda3.ini");
  Platform3DS_LoadRuntimeSettings();
  bool assets_ready = AssetsFileLooksValid(kAssetsFilename);
  if (assets_ready) {
    Platform3DS_LogRuntime("Assets file header validated");
  } else {
    BeginSetupConsole();
    bool should_extract = ConfirmExtraction();
    assets_ready = should_extract && ExtractAssetsFromRom();
    EndSetupConsole();
    Platform3DS_LogRuntime("First-run setup result: %s",
                           assets_ready ? "OK" : "cancelled/error");
    if (!assets_ready)
      return false;
  }

  if (!IsRegularFile("sdmc:/3ds/dspfirm.cdc")) {
    Platform3DS_LogRuntime("ERROR DSP firmware missing");
    BeginSetupConsole();
    ShowFatalSetupError(
      "DSP audio firmware is missing:\n"
      "sdmc:/3ds/dspfirm.cdc\n\n"
      "Open Rosalina (L + Down + Select),\n"
      "enter Miscellaneous options, then use\n"
      "Dump DSP firmware. Restart afterward.");
    EndSetupConsole();
    return false;
  }

  return true;
}

void Platform3DS_ApplyConfig(struct Config *config) {
  Platform3DS_DetectModel();
  config->window_width = 400;
  config->window_height = 240;
  config->window_scale = 1;
  config->fullscreen = 1;
  config->output_method = kOutputMethod_SDLSoftware;
  config->ignore_aspect_ratio = g_display_mode == kPlatform3DSDisplayStretch;
  config->linear_filtering = false;
  config->crt_filter = false;
  config->enhanced_mode7 = false;
  config->new_renderer = true;
  config->no_sprite_limits = false;
  config->extend_y = g_is_new_3ds;
  config->extended_aspect_ratio =
    g_display_mode == kPlatform3DSDisplayUltraWideMod ? 72 : 0;
  config->features0 &= ~(kFeatures0_ExtendScreen64 |
                         kFeatures0_WidescreenVisualFixes);
  bool wide = g_display_mode == kPlatform3DSDisplayUltraWideMod;
  if (wide)
    config->features0 |= kFeatures0_WidescreenVisualFixes;
  ZeldaSetWidescreenFixedMode(wide && g_wide_mode == kPlatform3DSWideFixed);
  config->audio_freq = 32000;
  config->audio_channels = 2;
  config->audio_samples = g_is_new_3ds ? 1024 : 2048;
  config->enable_msu = 0;
  config->disable_frame_delay = true;
  Platform3DS_LogRuntime("Runtime settings: model=%s, display=%d, wide=%d, turbo=%d, audio_samples=%d",
                         g_is_new_3ds ? "New 3DS" : "Old 3DS",
                         (int)g_display_mode,
                         (int)g_wide_mode,
                         g_turbo_multiplier,
                         config->audio_samples);
}

static bool WriteBlob(const char *path, const void *data, size_t size) {
  FILE *file = fopen(path, "wb");
  if (!file)
    return false;
  bool ok = fwrite(data, 1, size, file) == size;
  if (fclose(file) != 0)
    ok = false;
  if (!ok)
    remove(path);
  return ok;
}

static bool EnsureDirectory(const char *path) {
  if (mkdir(path, 0777) == 0)
    return true;
  if (errno == EEXIST) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
  }
  return false;
}

static void MakeTimestamp(char *stamp, size_t stamp_size) {
  time_t now = time(NULL);
  struct tm *tm_now = now > 0 ? localtime(&now) : NULL;
  if (tm_now)
    strftime(stamp, stamp_size, "%Y%m%d-%H%M%S", tm_now);
  else
    snprintf(stamp, stamp_size, "unknown-time");
}

bool Platform3DS_CreateDumpDirectory(char *out, size_t out_size) {
  if (!out || out_size == 0)
    return false;
  if (!EnsureDirectory("dumps")) {
    Platform3DS_LogRuntime("Dump directory create failed: dumps");
    return false;
  }
  char stamp[32];
  MakeTimestamp(stamp, sizeof(stamp));
  for (int attempt = 0; attempt < 100; attempt++) {
    if (attempt == 0)
      snprintf(out, out_size, "dumps/dump-%s", stamp);
    else
      snprintf(out, out_size, "dumps/dump-%s-%02d", stamp, attempt);
    if (mkdir(out, 0777) == 0) {
      Platform3DS_LogRuntime("Dump session directory: %s", out);
      return true;
    }
    if (errno != EEXIST)
      break;
  }
  Platform3DS_LogRuntime("Dump session directory create failed");
  out[0] = 0;
  return false;
}

bool Platform3DS_SaveARGB8888Bmp(const char *path, const uint8_t *pixels,
                                 int pitch, int width, int height) {
  if (!path || !pixels || pitch <= 0 || width <= 0 || height <= 0)
    return false;
  FILE *file = fopen(path, "wb");
  if (!file)
    return false;

  int row_size = (width * 3 + 3) & ~3;
  uint32_t file_size = 54u + (uint32_t)row_size * (uint32_t)height;
  uint8_t header[54] = {
    'B', 'M',
    (uint8_t)file_size, (uint8_t)(file_size >> 8),
    (uint8_t)(file_size >> 16), (uint8_t)(file_size >> 24),
    0, 0, 0, 0, 54, 0, 0, 0,
    40, 0, 0, 0,
    (uint8_t)width, (uint8_t)(width >> 8),
    (uint8_t)(width >> 16), (uint8_t)(width >> 24),
    (uint8_t)height, (uint8_t)(height >> 8),
    (uint8_t)(height >> 16), (uint8_t)(height >> 24),
    1, 0, 24, 0,
  };
  bool ok = fwrite(header, 1, sizeof(header), file) == sizeof(header);
  uint8_t *row = malloc((size_t)row_size);
  if (!row)
    ok = false;
  for (int y = height - 1; ok && y >= 0; y--) {
    memset(row, 0, (size_t)row_size);
    const uint32_t *src = (const uint32_t *)(pixels + (size_t)y * pitch);
    for (int x = 0; x < width; x++) {
      uint32_t c = src[x];
      row[x * 3 + 0] = (uint8_t)c;
      row[x * 3 + 1] = (uint8_t)(c >> 8);
      row[x * 3 + 2] = (uint8_t)(c >> 16);
    }
    ok = fwrite(row, 1, (size_t)row_size, file) == (size_t)row_size;
  }
  free(row);
  if (fclose(file) != 0)
    ok = false;
  if (!ok)
    remove(path);
  Platform3DS_LogRuntime("Screenshot %s: %s", path, ok ? "OK" : "FAILED");
  return ok;
}

bool Platform3DS_DumpMemory(const char *directory,
                            const uint8_t *ram, size_t ram_size,
                            const uint8_t *sram, size_t sram_size,
                            const uint16_t *vram, size_t vram_words) {
  char local_directory[128];
  if (!directory || !directory[0]) {
    if (!Platform3DS_CreateDumpDirectory(local_directory, sizeof(local_directory)))
      return false;
    directory = local_directory;
  }

  char path[192];
  snprintf(path, sizeof(path), "%s/ram.bin", directory);
  bool ok = WriteBlob(path, ram, ram_size);
  snprintf(path, sizeof(path), "%s/sram.bin", directory);
  ok = WriteBlob(path, sram, sram_size) && ok;
  snprintf(path, sizeof(path), "%s/vram.bin", directory);
  ok = WriteBlob(path, vram, vram_words * sizeof(*vram)) && ok;

  snprintf(path, sizeof(path), "%s/info.txt", directory);
  FILE *info = fopen(path, "wb");
  if (info) {
    fprintf(info, "Zelda 3DS v%s memory dump\n", ZELDA3_3DS_VERSION);
    fprintf(info, "RAM bytes: %lu\n", (unsigned long)ram_size);
    fprintf(info, "SRAM bytes: %lu\n", (unsigned long)sram_size);
    fprintf(info, "VRAM words: %lu\n", (unsigned long)vram_words);
    fprintf(info, "Display mode: %d\n", (int)g_display_mode);
    fprintf(info, "Top presenter: PICA200 RGB565\n");
    fprintf(info, "Frame pacing: 60 Hz high-resolution timer\n");
    fprintf(info, "New 3DS speedup requested: %s\n",
            g_is_new_3ds ? "yes" : "no");
    if (g_core1_time_enabled) {
      fprintf(info, "Core 1 PPU budget: %d%%\n",
              g_core1_time_limit_percent);
    } else {
      fprintf(info, "Core 1 PPU budget: unavailable/%d%% requested\n",
              g_core1_time_limit_percent);
    }
    int ppu_split_line = 0;
    uint32 ppu_main_time_us = 0;
    uint32 ppu_worker_time_us = 0;
    bool ppu_worker_enabled =
      ZeldaGetPpuWorkerStats(&ppu_split_line,
                             &ppu_main_time_us,
                             &ppu_worker_time_us);
    fprintf(info, "Parallel PPU renderer: %s\n",
            ppu_worker_enabled ? "enabled" : "unavailable");
    if (ppu_worker_enabled) {
      fprintf(info, "PPU split line: %d\n", ppu_split_line);
      fprintf(info, "Last main PPU segment: %lu us\n",
              (unsigned long)ppu_main_time_us);
      fprintf(info, "Last slowest PPU worker: %lu us\n",
              (unsigned long)ppu_worker_time_us);
    }
    fprintf(info, "Frame timing samples: %llu\n",
            (unsigned long long)g_frame_timing_samples);
    if (g_frame_timing_samples != 0) {
      fprintf(info, "Average logic work: %llu us\n",
              (unsigned long long)(g_logic_work_total_us /
                                   g_frame_timing_samples));
      fprintf(info, "Maximum logic work: %lu us\n",
              (unsigned long)g_logic_work_max_us);
      fprintf(info, "Average top draw/present: %llu us\n",
              (unsigned long long)(g_top_draw_total_us /
                                   g_frame_timing_samples));
      fprintf(info, "Maximum top draw/present: %lu us\n",
              (unsigned long)g_top_draw_max_us);
      fprintf(info, "Average PPU draw: %llu us\n",
              (unsigned long long)(g_ppu_draw_total_us /
                                   g_frame_timing_samples));
      fprintf(info, "Maximum PPU draw: %lu us\n",
              (unsigned long)g_ppu_draw_max_us);
      fprintf(info, "Average capture hooks: %llu us\n",
              (unsigned long long)(g_capture_total_us /
                                   g_frame_timing_samples));
      fprintf(info, "Maximum capture hooks: %lu us\n",
              (unsigned long)g_capture_max_us);
      fprintf(info, "Average native present: %llu us\n",
              (unsigned long long)(g_present_total_us /
                                   g_frame_timing_samples));
      fprintf(info, "Maximum native present: %lu us\n",
              (unsigned long)g_present_max_us);
      fprintf(info, "Average top frame work: %llu us\n",
              (unsigned long long)(g_top_work_total_us /
                                   g_frame_timing_samples));
      fprintf(info, "Maximum top frame work: %lu us\n",
              (unsigned long)g_top_work_max_us);
      fprintf(info, "Top frames over 16.67 ms: %llu\n",
              (unsigned long long)g_top_frames_over_budget);
      fprintf(info, "Average bottom work: %llu us\n",
              (unsigned long long)(g_bottom_work_total_us /
                                   g_frame_timing_samples));
      fprintf(info, "Maximum bottom work: %lu us\n",
              (unsigned long)g_bottom_work_max_us);
      fprintf(info, "Average total frame work: %llu us\n",
              (unsigned long long)(g_total_work_total_us /
                                   g_frame_timing_samples));
      fprintf(info, "Maximum total frame work: %lu us\n",
              (unsigned long)g_total_work_max_us);
      fprintf(info, "Total frames over 16.67 ms: %llu\n",
              (unsigned long long)g_total_frames_over_budget);
      if (g_render_interval_samples != 0 &&
          g_render_interval_total_us != 0) {
        uint64_t presentation_rate_x100 =
          g_render_interval_samples * 100000000ull /
          g_render_interval_total_us;
        uint64_t logic_rate_x100 =
          g_timed_scheduled_logic_frames * 100000000ull /
          g_render_interval_total_us;
        fprintf(info, "Average presentation interval: %llu us\n",
                (unsigned long long)(g_render_interval_total_us /
                                     g_render_interval_samples));
        fprintf(info, "Measured presentation rate: %llu.%02llu Hz\n",
                (unsigned long long)(presentation_rate_x100 / 100),
                (unsigned long long)(presentation_rate_x100 % 100));
        fprintf(info, "Measured normal logic rate: %llu.%02llu Hz\n",
                (unsigned long long)(logic_rate_x100 / 100),
                (unsigned long long)(logic_rate_x100 % 100));
      }
      fprintf(info, "Scheduled normal logic frames: %llu\n",
              (unsigned long long)g_scheduled_logic_frames);
      fprintf(info, "Executed logic frames including turbo: %llu\n",
              (unsigned long long)g_executed_logic_frames);
      fprintf(info, "Catch-up presentations: %llu\n",
              (unsigned long long)g_catchup_presentations);
      fprintf(info, "Maximum scheduled frames per presentation: %lu\n",
              (unsigned long)g_max_scheduled_logic_frames);
    }
    if (g_turbo_multiplier > 0)
      fprintf(info, "Turbo speed: x%d\n", g_turbo_multiplier);
    else
      fprintf(info, "Turbo speed: off\n");
    if (fclose(info) != 0)
      ok = false;
  } else {
    ok = false;
  }

  Platform3DS_LogRuntime("Memory dump %s: %s", directory,
                         ok ? "OK" : "FAILED");
  return ok;
}
