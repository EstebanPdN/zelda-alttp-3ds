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
#include "setup_audio_assets.h"
#include "features.h"
#include "setup_selector_assets.h"
#include "types.h"
#include "util.h"
#include "zelda_rtl.h"

extern void SecondScreenSDL_OpenDeveloperOverlay(void);

static const char kStorageDirectory[] = "sdmc:/3ds/Zelda 3DS";
static const char kProfilesDirectory[] = "profiles";
static const char kSelectedRomFile[] = "selected_rom.ini";
static const char kForceSelectorFile[] = "select-rom.flag";
static const char kAssetsFilename[] = "zelda3_assets.dat";
static const char kTemporaryAssetsFilename[] = "zelda3_assets.tmp";
static const char kBundledPatch[] = "romfs:/zelda3_assets.bps";
static const char kBundledConfig[] = "romfs:/zelda3.ini";

static enum Platform3DSDisplayMode g_display_mode =
  kPlatform3DSDisplayOriginal;
static enum Platform3DSWideEdgeMode g_wide_edge_mode =
  kPlatform3DSWideEdgeStandard;
static int g_wide_zoom_index;
static bool g_display_mode_auto = true;
static bool g_wide_edge_mode_auto = true;
static bool g_display_mode_legacy_stretch;
static bool g_runtime_wide_edge_seen;
static enum Platform3DSCStickMode g_cstick_mode = kPlatform3DSCStickTurbo;
static int g_turbo_multiplier = 5;
static bool g_show_fps;
static unsigned g_current_fps;
static bool g_dump_saved_overlay_requested;
static bool g_dump_saved_overlay_presented;
static bool g_quick_dump_requested;
static bool g_rom_selection_requested;
static aptHookCookie g_apt_hook_cookie;
static bool g_apt_hook_registered;
static volatile bool g_system_exit_requested;
static volatile bool g_system_suspended;
static char g_active_save_directory[512] = "saves";
static uint32_t g_active_profile_id;
static bool g_is_new_3ds;
static bool g_model_detected;
static bool g_irrst_initialized;
static bool g_core1_time_enabled;
static int g_core1_time_limit_percent;
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
static void *g_c2d_flush_base;
static size_t g_c2d_flush_size;
static int g_cache_clean_mode; /* 0 unprobed, 1 direct SVC, 2 GX fallback */
static uint16_t g_setup_top_pixels[400 * 240];
static uint16_t g_setup_bottom_pixels[320 * 240];
static bool g_setup_audio_initialized;
static int16_t *g_setup_music_buffer;
static int16_t *g_setup_move_buffer;
static ndspWaveBuf g_setup_music_wavebuf;
static ndspWaveBuf g_setup_move_wavebuf;

enum {
  kTopTextureWidth = 512,
  kTopTextureHeight = 256,
  kC2DFlushWindowSize = 64 * 1024,
};

extern u32 __ctru_linear_heap;
extern u32 __ctru_linear_heap_size;

static bool WriteBlob(const char *path, const void *data, size_t size);
static bool EnsureDirectory(const char *path);
static bool CopyFileReplacing(const char *source, const char *destination);
static void MakeTimestamp(char *stamp, size_t stamp_size);
static bool RomFileShouldBeIgnored(const char *name);
static uint32 ReadU32LE(const uint8 *data);

/* GSPGPU_FlushDataCache blocks on service IPC. The direct SVC performs the
 * same clean operation on this process without waking GSP on core 1. Some
 * launch environments may not grant the SVC, so retain a queued GX fallback. */
static bool Platform3DS_CleanDataCache(const void *address, size_t size) {
  if (!address || size == 0)
    return true;

  if (g_cache_clean_mode != 2) {
    Result result = svcStoreProcessDataCache(
      CUR_PROCESS_HANDLE, (u32)(uintptr_t)address, (u32)size);
    if (R_SUCCEEDED(result)) {
      g_cache_clean_mode = 1;
      return true;
    }
    g_cache_clean_mode = 2;
    Platform3DS_LogRuntime(
      "Direct cache clean unavailable (0x%08lx); using queued GX fallback",
      (unsigned long)result);
  }

  return R_SUCCEEDED(GX_FlushCacheRegions(
    (u32 *)(uintptr_t)address, (u32)size, NULL, 0, NULL, 0));
}

/* Passing GX_CMDLIST_FLUSH tells Citro3D that all GPU-visible linear-memory
 * ranges were cleaned explicitly. This avoids its default full-linear-heap
 * synchronous flush at the end of every frame. */
static void Platform3DS_EndGpuFrame(void) {
  C2D_Flush();
  bool clean = g_c2d_flush_base && g_c2d_flush_size &&
    Platform3DS_CleanDataCache(g_c2d_flush_base, g_c2d_flush_size);
  C3D_FrameEnd(clean ? GX_CMDLIST_FLUSH : 0);
}

/* C2D_TargetClear also clears depth. E4 render targets intentionally omit a
 * depth buffer, so preserve Citro2D's ordering barriers and clear color only. */
static void Platform3DS_ClearTarget(C3D_RenderTarget *target, u32 color) {
  C2D_Flush();
  C3D_FrameSplit(0);
  C3D_RenderTargetClear(
    target, C3D_CLEAR_COLOR, __builtin_bswap32(color), 0);
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

static void Platform3DS_ApplyAutoDisplayDefaults(void) {
  if (g_display_mode_legacy_stretch && !g_runtime_wide_edge_seen) {
    g_display_mode_auto = true;
    g_wide_edge_mode_auto = true;
  }
  if (g_display_mode_auto)
    g_display_mode = g_is_new_3ds ? kPlatform3DSDisplayUltraWideMod :
                                    kPlatform3DSDisplayOriginal;
  if (g_wide_edge_mode_auto)
    g_wide_edge_mode = g_is_new_3ds ? kPlatform3DSWideEdgeFixedCamera :
                                      kPlatform3DSWideEdgeStandard;
}

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
  u32 keys_up = hidKeysUp();
  static uint64_t old_3ds_x_hold_start_ms;
  static bool old_3ds_x_turbo_was_active;
  bool old_3ds_x_turbo = false;
  bool old_3ds_x_tap = false;
  if (!g_is_new_3ds && (keys & KEY_X)) {
    uint64_t now_ms = osGetTime();
    if (old_3ds_x_hold_start_ms == 0)
      old_3ds_x_hold_start_ms = now_ms;
    old_3ds_x_turbo = now_ms - old_3ds_x_hold_start_ms >= 1000;
    if (old_3ds_x_turbo)
      old_3ds_x_turbo_was_active = true;
  } else {
    if (!g_is_new_3ds && (keys_up & KEY_X) &&
        old_3ds_x_hold_start_ms != 0 &&
        !old_3ds_x_turbo_was_active &&
        osGetTime() - old_3ds_x_hold_start_ms < 1000)
      old_3ds_x_tap = true;
    old_3ds_x_hold_start_ms = 0;
    old_3ds_x_turbo_was_active = false;
  }
  static bool quick_dump_combo_was_held;
  static bool version_combo_was_held;
  bool quick_dump_combo =
    (keys & (KEY_L | KEY_R | KEY_A)) == (KEY_L | KEY_R | KEY_A);
  if (quick_dump_combo && !quick_dump_combo_was_held)
    g_quick_dump_requested = true;
  quick_dump_combo_was_held = quick_dump_combo;

  bool version_combo =
    (keys & (KEY_L | KEY_R | KEY_B)) == (KEY_L | KEY_R | KEY_B);
  if (version_combo && !version_combo_was_held)
    SecondScreenSDL_OpenDeveloperOverlay();
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
  if ((keys & KEY_B) && !version_combo) input |= 1u << 0;
  if (g_is_new_3ds ? (keys & KEY_X) : old_3ds_x_tap)
    input |= 1u << 9;
  if (keys & KEY_Y) input |= 1u << 1;
  if ((keys & KEY_L) && !version_combo) input |= 1u << 10;
  if ((keys & KEY_R) && !version_combo) input |= 1u << 11;
  *turbo_held = g_turbo_multiplier > 0 &&
                ((keys & (KEY_ZL | KEY_ZR)) != 0 || CStickIsHeld(keys) ||
                 old_3ds_x_turbo);
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
    g_display_mode_legacy_stretch = false;
    if (strcasecmp(value, "Auto") == 0) {
      g_display_mode_auto = true;
    } else if (strcasecmp(value, "Original") == 0) {
      g_display_mode_auto = false;
      g_display_mode = kPlatform3DSDisplayOriginal;
    } else if (strcasecmp(value, "Stretch") == 0 ||
               strcasecmp(value, "UltraWideStretch") == 0) {
      g_display_mode_auto = false;
      g_display_mode = kPlatform3DSDisplayStretch;
      g_display_mode_legacy_stretch = true;
    } else {
      g_display_mode_auto = false;
      g_display_mode = kPlatform3DSDisplayUltraWideMod;
    }
  } else if (strcasecmp(key, "WideEdgeMode") == 0) {
    g_runtime_wide_edge_seen = true;
    if (strcasecmp(value, "Auto") == 0) {
      g_wide_edge_mode_auto = true;
    } else if (strcasecmp(value, "FixedCamera") == 0) {
      g_wide_edge_mode_auto = false;
      g_wide_edge_mode = kPlatform3DSWideEdgeFixedCamera;
    } else {
      g_wide_edge_mode_auto = false;
      g_wide_edge_mode = kPlatform3DSWideEdgeStandard;
    }
  } else if (strcasecmp(key, "WideZoom") == 0) {
    int zoom_index = 0;
    if (strcasecmp(value, "1.2") == 0 || strcasecmp(value, "1.2x") == 0)
      zoom_index = 1;
    else if (strcasecmp(value, "1.5") == 0 || strcasecmp(value, "1.5x") == 0)
      zoom_index = 2;
    else if (strcasecmp(value, "2") == 0 || strcasecmp(value, "2x") == 0)
      zoom_index = 3;
    else if (strcasecmp(value, "2.5") == 0 || strcasecmp(value, "2.5x") == 0)
      zoom_index = 4;
    g_wide_zoom_index = zoom_index;
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
  } else if (strcasecmp(key, "ShowFps") == 0) {
    bool show = false;
    if (ParseBool(value, &show))
      g_show_fps = show;
  }
}

void Platform3DS_LoadRuntimeSettings(void) {
  g_display_mode_auto = true;
  g_wide_edge_mode_auto = true;
  g_display_mode_legacy_stretch = false;
  g_runtime_wide_edge_seen = false;
  g_show_fps = false;
  FILE *file = fopen("zelda3.ini", "rb");
  if (!file) {
    Platform3DS_ApplyAutoDisplayDefaults();
    return;
  }
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
  Platform3DS_ApplyAutoDisplayDefaults();
}

enum Platform3DSDisplayMode Platform3DS_GetDisplayMode(void) {
  return g_display_mode;
}

void Platform3DS_SetDisplayMode(enum Platform3DSDisplayMode mode) {
  if (mode > kPlatform3DSDisplayStretch)
    mode = kPlatform3DSDisplayUltraWideMod;
  g_display_mode_auto = false;
  g_display_mode_legacy_stretch = false;
  g_display_mode = mode;
  Platform3DS_LogRuntime("Display mode set: %d", (int)g_display_mode);
}

enum Platform3DSWideEdgeMode Platform3DS_GetWideEdgeMode(void) {
  return g_wide_edge_mode;
}

void Platform3DS_SetWideEdgeMode(enum Platform3DSWideEdgeMode mode) {
  if (mode > kPlatform3DSWideEdgeFixedCamera)
    mode = kPlatform3DSWideEdgeFixedCamera;
  g_wide_edge_mode_auto = false;
  g_wide_edge_mode = mode;
  ZeldaSetWidescreenEdgeMode((int)g_wide_edge_mode);
  Platform3DS_LogRuntime("Wide edge mode set: %d", (int)g_wide_edge_mode);
}

int Platform3DS_GetWideZoomIndex(void) {
  return g_wide_zoom_index;
}

void Platform3DS_SetWideZoomIndex(int zoom_index) {
  if (zoom_index < 0)
    zoom_index = 0;
  if (zoom_index > 4)
    zoom_index = 4;
  g_wide_zoom_index = zoom_index;
  Platform3DS_LogRuntime("Wide zoom set: %d", g_wide_zoom_index);
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

void Platform3DS_RequestRomSelection(void) {
  Platform3DS_BlankScreens();
  FILE *file = fopen("sdmc:/3ds/Zelda 3DS/select-rom.flag", "wb");
  if (file) {
    fputs("1\n", file);
    fclose(file);
  }
  g_rom_selection_requested = true;
  g_system_exit_requested = true;
  Platform3DS_LogRuntime("ROM selector requested from settings");
}

bool Platform3DS_TakeRomSelectionRequest(void) {
  bool requested = g_rom_selection_requested;
  g_rom_selection_requested = false;
  if (requested)
    g_system_exit_requested = false;
  return requested;
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

void Platform3DS_BlankScreens(void) {
  if (!g_gpu_presenter_initialized)
    return;
  if (g_gpu_frame_active) {
    Platform3DS_EndGpuFrame();
    g_gpu_frame_active = false;
  }
  for (int i = 0; i < 3; i++) {
    if (!C3D_FrameBegin(0))
      return;
    Platform3DS_ClearTarget(g_top_target, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(g_top_target);
    Platform3DS_ClearTarget(g_bottom_target, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(g_bottom_target);
    Platform3DS_EndGpuFrame();
    gspWaitForVBlank();
  }
}

bool Platform3DS_IsSystemClosing(void) {
  return g_system_exit_requested || aptShouldClose();
}

bool Platform3DS_IsNew3DS(void) {
  Platform3DS_DetectModel();
  return g_is_new_3ds;
}

bool Platform3DS_CanUseCore1PpuWorker(void) {
  return g_core1_time_enabled && g_core1_time_limit_percent > 0;
}

bool Platform3DS_IsVersionOverlayVisible(void) {
  return false;
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

bool Platform3DS_GetShowFps(void) {
  return g_show_fps;
}

void Platform3DS_SetShowFps(bool show) {
  g_show_fps = show;
}

void Platform3DS_SetCurrentFps(int fps) {
  if (fps < 0)
    fps = 0;
  if (fps > 999)
    fps = 999;
  g_current_fps = (unsigned)fps;
}

void Platform3DS_PersistRuntimeSettings(void) {
  const char *leaf = strrchr(g_active_save_directory, '/');
  leaf = leaf ? leaf + 1 : g_active_save_directory;
  char path[512];
  int length = snprintf(path, sizeof(path), "%s/%s/zelda3.ini",
                        kProfilesDirectory, leaf);
  bool ok = length >= 0 && length < (int)sizeof(path) &&
            CopyFileReplacing("zelda3.ini", path);
  Platform3DS_LogRuntime("Runtime settings persist: %s", ok ? "OK" : "FAILED");
}

void Platform3DS_ShowDumpSavedOverlay(void) {
  g_dump_saved_overlay_requested = true;
}

uint32_t Platform3DS_GetActiveProfileId(void) {
  return g_active_profile_id;
}

bool Platform3DS_InitTopPresenter(void) {
  Platform3DS_RegisterAptHook();
  Platform3DS_DetectModel();
  g_c2d_flush_base = NULL;
  g_c2d_flush_size = 0;
  g_cache_clean_mode = 0;

  // This is a no-op on Old 3DS and enables 804 MHz operation for 3DSX builds
  // on New 3DS. CIA builds also request the faster clock in their exheader.
  osSetSpeedupEnable(true);
  g_irrst_initialized = R_SUCCEEDED(irrstInit());
  aptSetHomeAllowed(true);
  aptSetSleepAllowed(true);

  // Reserve part of the system core for a parallel PPU segment.
  const u32 core1_candidates[] = {80, 70, 50, 30};
  g_core1_time_enabled = false;
  g_core1_time_limit_percent = 0;
  for (size_t i = 0; i < countof(core1_candidates); i++) {
    Result set_result = APT_SetAppCpuTimeLimit(core1_candidates[i]);
    if (R_SUCCEEDED(set_result)) {
      u32 actual_percent = 0;
      Result get_result = APT_GetAppCpuTimeLimit(&actual_percent);
      Platform3DS_LogRuntime(
        "Core 1 PPU budget request: wanted=%lu%% actual=%lu%% get=0x%08lx",
        (unsigned long)core1_candidates[i],
        (unsigned long)actual_percent,
        (unsigned long)get_result);
      if (R_SUCCEEDED(get_result) && actual_percent > 0) {
        g_core1_time_limit_percent = (int)actual_percent;
        g_core1_time_enabled = true;
        break;
      }
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
  C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);

  /* Citro2D allocates its streaming vertex and index buffers in linear
   * memory. Bound the dirty range around that allocation instead of flushing
   * the complete linear heap (which also contains the top and bottom upload
   * buffers) on every C3D_FrameEnd. */
  C3D_BufInfo *c2d_buffers = C3D_GetBufInfo();
  if (c2d_buffers && c2d_buffers->bufCount > 0) {
    u32 heap_physical = osConvertVirtToPhys((void *)__ctru_linear_heap);
    u32 vertex_physical =
      c2d_buffers->base_paddr + c2d_buffers->buffers[0].offset;
    uintptr_t heap_start = (uintptr_t)__ctru_linear_heap;
    uintptr_t heap_end = heap_start + __ctru_linear_heap_size;
    uintptr_t vertex_address =
      heap_start + (u32)(vertex_physical - heap_physical);
    uintptr_t flush_start = vertex_address & ~(uintptr_t)0x7f;
    uintptr_t flush_end = flush_start + kC2DFlushWindowSize;
    if (flush_end > heap_end)
      flush_end = heap_end;
    if (flush_start >= heap_start && flush_start < flush_end) {
      g_c2d_flush_base = (void *)flush_start;
      g_c2d_flush_size = flush_end - flush_start;
    }
  }
  if (!C3D_TexInitVRAM(&g_top_texture, kTopTextureWidth,
                       kTopTextureHeight, GPU_RGBA8)) {
    C2D_Fini();
    C3D_Fini();
    Platform3DS_LogRuntime("ERROR: unable to allocate top GPU texture");
    return false;
  }
  C3D_TexSetFilter(&g_top_texture, GPU_NEAREST, GPU_NEAREST);
  C3D_TexSetWrap(&g_top_texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
  GPU_TEXCOLOR bottom_texture_format =
    g_is_new_3ds ? GPU_RGBA8 : GPU_RGB565;
  if (!C3D_TexInitVRAM(&g_bottom_texture, kTopTextureWidth,
                       kTopTextureHeight, bottom_texture_format)) {
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
    GPU_RB_RGB565, -1);
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
      GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) |
      GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
      GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
  g_bottom_target = C3D_RenderTargetCreate(
    GSP_SCREEN_WIDTH, GSP_SCREEN_HEIGHT_BOTTOM,
    GPU_RB_RGB565, -1);
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
      GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) |
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
    "Core 1 PPU budget=%s%d%%, bounded C2D flush=%lu bytes",
    g_is_new_3ds ? "yes" : "no",
    Platform3DS_CanUseCore1PpuWorker() ? "" : "unavailable/",
    g_core1_time_limit_percent,
    (unsigned long)g_c2d_flush_size);
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
  g_c2d_flush_base = NULL;
  g_c2d_flush_size = 0;
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

static void ConfigureRgb565TextureEnv(void) {
  C3D_TexEnv *env = C3D_GetTexEnv(0);
  C3D_TexEnvInit(env);
  C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
  C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
  for (int i = 1; i < 3; i++)
    C3D_TexEnvInit(C3D_GetTexEnv(i));
}

static const uint8_t *StatusGlyph(char c) {
  static const uint8_t digits[10][7] = {
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
    {14, 17, 1, 2, 4, 8, 31},     {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2},     {31, 16, 16, 30, 1, 1, 30},
    {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14},
  };
  static const uint8_t letters[11][7] = {
    {14, 17, 17, 31, 17, 17, 17}, /* A */
    {30, 17, 17, 17, 17, 17, 30}, /* D */
    {31, 16, 16, 30, 16, 16, 31}, /* E */
    {31, 16, 16, 30, 16, 16, 16}, /* F */
    {17, 27, 21, 21, 17, 17, 17}, /* M */
    {30, 17, 17, 30, 16, 16, 16}, /* P */
    {15, 16, 16, 14, 1, 1, 30},   /* S */
    {17, 17, 17, 17, 17, 17, 14}, /* U */
    {17, 17, 17, 17, 17, 10, 4},  /* V */
    {14, 17, 16, 16, 16, 17, 14}, /* C */
    {14, 17, 16, 23, 17, 17, 15}, /* G */
  };
  static const uint8_t letter_ids[26] = {
    0, 255, 9, 1, 2, 3, 10, 255, 255, 255, 255, 255, 4,
    255, 255, 5, 255, 255, 6, 255, 7, 8, 255, 255, 255, 255,
  };
  if (c >= '0' && c <= '9')
    return digits[c - '0'];
  if (c >= 'A' && c <= 'Z') {
    uint8_t id = letter_ids[c - 'A'];
    if (id != 255)
      return letters[id];
  }
  return NULL;
}

static void DrawStatusText(float x, float y, float scale,
                           const char *text) {
  const uint32_t color = C2D_Color32(255, 255, 255, 255);
  for (; *text; text++, x += 6.0f * scale) {
    const uint8_t *glyph = StatusGlyph(*text);
    if (!glyph)
      continue;
    for (int row = 0; row < 7; row++) {
      for (int col = 0; col < 5;) {
        if ((glyph[row] & (1u << (4 - col))) == 0) {
          col++;
          continue;
        }
        int end = col + 1;
        while (end < 5 && (glyph[row] & (1u << (4 - end))) != 0)
          end++;
        C2D_DrawRectSolid(x + col * scale, y + row * scale, 0.8f,
                          (end - col) * scale, scale, color);
        col = end;
      }
    }
  }
}

void Platform3DS_PresentTopFrame(const uint8_t *pixels, int pitch,
                                 int width, int height,
                                 int focus_x, int focus_y) {
  if (!g_gpu_presenter_initialized || !pixels ||
      pitch != kTopTextureWidth * 4 ||
      width <= 0 || width > kTopTextureWidth ||
      height <= 0 || height > kTopTextureHeight)
    return;

  if (!C3D_FrameBegin(0))
    return;
  g_gpu_frame_active = true;
  Platform3DS_CleanDataCache(
    pixels, kTopTextureWidth * kTopTextureHeight * 4);
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
  const bool wide = g_display_mode == kPlatform3DSDisplayUltraWideMod;
  static const float zoom_values[5] = { 1.0f, 1.2f, 1.5f, 2.0f, 2.5f };
  float zoom = wide ? zoom_values[g_wide_zoom_index] : 1.0f;
  float source_width = (float)width / zoom;
  float source_height = (float)height / zoom;
  if (source_width < 1.0f)
    source_width = 1.0f;
  if (source_height < 1.0f)
    source_height = 1.0f;
  float source_left = ((float)width - source_width) * 0.5f;
  float source_top = ((float)height - source_height) * 0.5f;
  if (wide && g_wide_zoom_index > 0 &&
      focus_x >= 0 && focus_x < width &&
      focus_y >= 0 && focus_y < height) {
    source_left = (float)focus_x - source_width * 0.5f;
    source_top = (float)focus_y - source_height * 0.5f;
    if (source_left < 0.0f)
      source_left = 0.0f;
    if (source_top < 0.0f)
      source_top = 0.0f;
    if (source_left + source_width > (float)width)
      source_left = (float)width - source_width;
    if (source_top + source_height > (float)height)
      source_top = (float)height - source_height;
  }
  const float draw_width = stretch ? (float)GSP_SCREEN_HEIGHT_TOP :
                                     (float)width;
  const float draw_height = (stretch || wide) ? (float)GSP_SCREEN_WIDTH :
    (height < GSP_SCREEN_WIDTH ? (float)height : (float)GSP_SCREEN_WIDTH);
  g_top_subtexture = (Tex3DS_SubTexture){
    .width = (u16)source_width,
    .height = (u16)source_height,
    .left = source_left / kTopTextureWidth,
    .top = 1.0f - source_top / kTopTextureHeight,
    .right = (source_left + source_width) / kTopTextureWidth,
    .bottom = 1.0f - (source_top + source_height) / kTopTextureHeight,
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

  Platform3DS_ClearTarget(g_top_target, C2D_Color32(0, 0, 0, 255));
  C2D_SceneBegin(g_top_target);
  C2D_DrawImage(image, &params, NULL);
  ConfigureArgbTextureEnv();
  if (g_show_fps) {
    char label[28];
    snprintf(label, sizeof(label), "FPS %u CPU", g_current_fps);
    C2D_DrawRectSolid(5.0f, 216.0f, 0.7f, 122.0f, 20.0f,
                      C2D_Color32(0, 0, 0, 210));
    DrawStatusText(10.0f, 219.0f, 2.0f, label);
  }
  if (g_dump_saved_overlay_requested) {
    C2D_DrawRectSolid(132.0f, 12.0f, 0.7f, 136.0f, 24.0f,
                      C2D_Color32(0, 0, 0, 220));
    DrawStatusText(141.0f, 17.0f, 2.0f, "DUMP SAVED");
    g_dump_saved_overlay_requested = false;
    g_dump_saved_overlay_presented = true;
  }
}

void Platform3DS_PresentBottomFrame(const uint8_t *pixels, int pitch,
                                    int width, int height) {
  int bytes_per_pixel = g_is_new_3ds ? 4 : 2;
  if (!g_gpu_frame_active || !pixels ||
      pitch != kTopTextureWidth * bytes_per_pixel ||
      width <= 0 || width > kTopTextureWidth ||
      height <= 0 || height > kTopTextureHeight)
    return;

  Platform3DS_CleanDataCache(
    pixels, kTopTextureWidth * kTopTextureHeight * bytes_per_pixel);
  GX_TRANSFER_FORMAT bottom_transfer_format =
    g_is_new_3ds ? GX_TRANSFER_FMT_RGBA8 : GX_TRANSFER_FMT_RGB565;
  C3D_SyncDisplayTransfer(
    (u32 *)pixels, GX_BUFFER_DIM(kTopTextureWidth, kTopTextureHeight),
    (u32 *)g_bottom_texture.data,
    GX_BUFFER_DIM(kTopTextureWidth, kTopTextureHeight),
    GX_TRANSFER_FLIP_VERT(0) |
      GX_TRANSFER_OUT_TILED(1) |
      GX_TRANSFER_RAW_COPY(0) |
      GX_TRANSFER_IN_FORMAT(bottom_transfer_format) |
      GX_TRANSFER_OUT_FORMAT(bottom_transfer_format) |
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
  Platform3DS_ClearTarget(g_bottom_target, C2D_Color32(0, 0, 0, 255));
  C2D_SceneBegin(g_bottom_target);
  C2D_DrawImage(image, &params, NULL);
  if (g_is_new_3ds)
    ConfigureArgbTextureEnv();
  else
    ConfigureRgb565TextureEnv();
}

void Platform3DS_EndFrame(void) {
  if (!g_gpu_frame_active)
    return;
  Platform3DS_EndGpuFrame();
  g_gpu_frame_active = false;
  if (g_dump_saved_overlay_presented) {
    g_dump_saved_overlay_presented = false;
    gspWaitForEvent(GSPGPU_EVENT_VBlank0, false);
    svcSleepThread(600000000LL);
  }
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

static bool CopyFileReplacing(const char *source, const char *destination) {
  FILE *input = fopen(source, "rb");
  if (!input)
    return false;
  char temporary[512];
  snprintf(temporary, sizeof(temporary), "%s.tmp", destination);
  FILE *output = fopen(temporary, "wb");
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
  if (!success) {
    remove(temporary);
    return false;
  }
  remove(destination);
  if (rename(temporary, destination) != 0) {
    remove(temporary);
    return false;
  }
  return true;
}

static bool AssetsBlobLooksValid(const uint8 *data, size_t size) {
  if (size < 88)
    return false;
  static const char signature[] = { kAssets_Sig };
  uint32 count = ReadU32LE(data + 80);
  uint32 names_size = ReadU32LE(data + 84);
  if (memcmp(data, signature, sizeof(signature)) != 0 ||
      count != kNumberOfAssets ||
      size < 88 + count * 4 + names_size)
    return false;

  size_t offset = 88 + count * 4 + names_size;
  for (uint32 i = 0; i < count; i++) {
    uint32 asset_size = ReadU32LE(data + 88 + i * 4);
    offset = (offset + 3) & ~3;
    if (offset + asset_size > size)
      return false;
    offset += asset_size;
  }
  return true;
}

static bool AssetsFileLooksValid(const char *path) {
  size_t size = 0;
  uint8 *data = ReadWholeFile(path, &size);
  if (!data)
    return false;
  bool valid = AssetsBlobLooksValid(data, size);
  free(data);
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
        !RomFileShouldBeIgnored(entry->d_name) &&
        IsRegularFile(entry->d_name)) {
      snprintf(path, path_size, "%s", entry->d_name);
      found = true;
      break;
    }
  }
  closedir(directory);
  return found;
}

typedef struct RomEntry {
  char filename[256];
  char profile[320];
  uint32_t hash;
} RomEntry;

static RomEntry g_rom_entries[64];

static uint32_t HashRomFile(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file)
    return 0;
  uint32_t hash = 2166136261u;
  uint8_t buffer[4096];
  size_t count;
  while ((count = fread(buffer, 1, sizeof(buffer), file)) != 0) {
    for (size_t i = 0; i < count; i++) {
      hash ^= buffer[i];
      hash *= 16777619u;
    }
  }
  fclose(file);
  return hash ? hash : 1;
}

static void SanitizeProfileName(const char *filename, char *out,
                                size_t out_size) {
  size_t j = 0;
  for (size_t i = 0; filename[i] && j + 1 < out_size; i++) {
    char c = filename[i];
    if (c == '.')
      break;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_') {
      out[j++] = c;
    } else if (c == ' ' && j > 0 && out[j - 1] != '-') {
      out[j++] = '-';
    }
  }
  while (j > 0 && out[j - 1] == '-')
    j--;
  if (j == 0 && out_size > 1) {
    out[j++] = 'r';
    out[j++] = 'o';
    out[j++] = 'm';
  }
  out[j] = 0;
}

static void MakeProfilePath(const char *filename, uint32_t hash,
                            char *out, size_t out_size) {
  char clean[128];
  SanitizeProfileName(filename, clean, sizeof(clean));
  snprintf(out, out_size, "%s/%s-%08lx",
           kProfilesDirectory, clean, (unsigned long)hash);
}

static bool RomFileShouldBeIgnored(const char *name) {
  return strncmp(name, "._", 2) == 0;
}

static int ScanRoms(RomEntry *roms, int max_roms) {
  DIR *directory = opendir(".");
  if (!directory)
    return 0;
  int count = 0;
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL && count < max_roms) {
    if (RomFileShouldBeIgnored(entry->d_name) ||
        !(HasExtension(entry->d_name, ".sfc") ||
          HasExtension(entry->d_name, ".smc")) ||
        !IsRegularFile(entry->d_name))
      continue;
    snprintf(roms[count].filename, sizeof(roms[count].filename),
             "%s", entry->d_name);
    roms[count].hash = HashRomFile(entry->d_name);
    MakeProfilePath(roms[count].filename, roms[count].hash,
                    roms[count].profile, sizeof(roms[count].profile));
    count++;
  }
  closedir(directory);
  return count;
}

static void SetupAudioStop(void) {
  if (!g_setup_audio_initialized)
    return;
  ndspChnWaveBufClear(0);
  ndspChnWaveBufClear(1);
  ndspExit();
  g_setup_audio_initialized = false;
  if (g_setup_music_buffer) {
    linearFree(g_setup_music_buffer);
    g_setup_music_buffer = NULL;
  }
  if (g_setup_move_buffer) {
    linearFree(g_setup_move_buffer);
    g_setup_move_buffer = NULL;
  }
}

static void SetupAudioStart(void) {
  if (g_setup_audio_initialized)
    return;
  if (R_FAILED(ndspInit())) {
    LogSetup("Setup audio unavailable");
    return;
  }
  g_setup_audio_initialized = true;
  size_t music_size = sizeof(kSetupMusicSamples);
  size_t move_size = sizeof(kSetupMoveSamples);
  g_setup_music_buffer = (int16_t *)linearAlloc(music_size);
  g_setup_move_buffer = (int16_t *)linearAlloc(move_size);
  if (!g_setup_music_buffer || !g_setup_move_buffer) {
    LogSetup("Setup audio allocation failed");
    SetupAudioStop();
    return;
  }
  memcpy(g_setup_music_buffer, kSetupMusicSamples, music_size);
  memcpy(g_setup_move_buffer, kSetupMoveSamples, move_size);
  DSP_FlushDataCache(g_setup_music_buffer, music_size);
  DSP_FlushDataCache(g_setup_move_buffer, move_size);

  ndspSetOutputMode(NDSP_OUTPUT_MONO);
  ndspSetMasterVol(0.75f);
  for (int channel = 0; channel < 2; channel++) {
    float mix[12] = {0};
    mix[0] = channel == 0 ? 0.55f : 0.75f;
    mix[1] = channel == 0 ? 0.55f : 0.75f;
    ndspChnReset(channel);
    ndspChnSetInterp(channel, NDSP_INTERP_LINEAR);
    ndspChnSetRate(channel, (float)kSetupAudioRate);
    ndspChnSetFormat(channel, NDSP_FORMAT_MONO_PCM16);
    ndspChnSetMix(channel, mix);
  }

  memset(&g_setup_music_wavebuf, 0, sizeof(g_setup_music_wavebuf));
  g_setup_music_wavebuf.data_pcm16 = g_setup_music_buffer;
  g_setup_music_wavebuf.nsamples = kSetupMusicSampleCount;
  g_setup_music_wavebuf.looping = true;
  ndspChnWaveBufAdd(0, &g_setup_music_wavebuf);
  LogSetup("Setup audio started");
}

static void SetupAudioPlayMove(void) {
  if (!g_setup_audio_initialized || !g_setup_move_buffer)
    return;
  ndspChnWaveBufClear(1);
  memset(&g_setup_move_wavebuf, 0, sizeof(g_setup_move_wavebuf));
  g_setup_move_wavebuf.data_pcm16 = g_setup_move_buffer;
  g_setup_move_wavebuf.nsamples = kSetupMoveSampleCount;
  g_setup_move_wavebuf.looping = false;
  ndspChnWaveBufAdd(1, &g_setup_move_wavebuf);
}

static void PresentSetupConsole(void);

static void BeginSetupConsole(void) {
  if (g_setup_console_active)
    return;
  gfxInitDefault();
  gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
  gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
  aptSetHomeAllowed(true);
  aptSetSleepAllowed(true);
  consoleInit(GFX_TOP, NULL);
  consoleClear();
  memset(g_setup_top_pixels, 0, sizeof(g_setup_top_pixels));
  memset(g_setup_bottom_pixels, 0, sizeof(g_setup_bottom_pixels));
  g_setup_console_active = true;
  PresentSetupConsole();
  PresentSetupConsole();
}

static void PresentSetupConsole(void) {
  u16 fb_w = 0, fb_h = 0;
  uint16_t *top =
    (uint16_t *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_w, &fb_h);
  if (top) {
    for (int x = 0; x < 400; x++) {
      for (int y = 0; y < 240; y++)
        top[x * 240 + (239 - y)] = g_setup_top_pixels[y * 400 + x];
    }
  }
  uint16_t *bottom =
    (uint16_t *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &fb_w, &fb_h);
  if (bottom) {
    for (int x = 0; x < 320; x++) {
      for (int y = 0; y < 240; y++)
        bottom[x * 240 + (239 - y)] = g_setup_bottom_pixels[y * 320 + x];
    }
  }
  gfxFlushBuffers();
  gfxSwapBuffers();
  gspWaitForVBlank();
}

static void PresentSetupStable(void (*draw)(void *context), void *context) {
  draw(context);
  PresentSetupConsole();
  draw(context);
  PresentSetupConsole();
}

static void EndSetupConsole(void) {
  if (!g_setup_console_active)
    return;
  PresentSetupConsole();
  SetupAudioStop();
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

static bool SaveRgb565FramebufferBmp(const char *path, gfxScreen_t screen) {
  const int width = screen == GFX_TOP ? 400 : 320;
  const int height = 240;
  u16 fb_w = 0, fb_h = 0;
  uint16_t *fb =
    (uint16_t *)gfxGetFramebuffer(screen, GFX_LEFT, &fb_w, &fb_h);
  if (!fb)
    return false;
  (void)fb_w;
  (void)fb_h;

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
    for (int x = 0; x < width; x++) {
      uint16_t color = fb[x * height + (height - 1 - y)];
      row[x * 3 + 0] = (uint8_t)((color & 31) * 255 / 31);
      row[x * 3 + 1] = (uint8_t)(((color >> 5) & 63) * 255 / 63);
      row[x * 3 + 2] = (uint8_t)(((color >> 11) & 31) * 255 / 31);
    }
    ok = fwrite(row, 1, (size_t)row_size, file) == (size_t)row_size;
  }
  free(row);
  if (fclose(file) != 0)
    ok = false;
  if (!ok)
    remove(path);
  return ok;
}

static void CreateSetupDump(const char *screen_name) {
  char directory[192];
  char stamp[32];
  MakeTimestamp(stamp, sizeof(stamp));
  EnsureDirectory("dumps");
  snprintf(directory, sizeof(directory), "dumps/setup-%s", stamp);
  if (!EnsureDirectory(directory))
    return;
  char path[256];
  snprintf(path, sizeof(path), "%s/info.txt", directory);
  FILE *info = fopen(path, "wb");
  if (info) {
    fprintf(info, "Zelda 3DS v%s setup dump\n", ZELDA3_3DS_VERSION);
    fprintf(info, "Screen: %s\n", screen_name ? screen_name : "unknown");
    fprintf(info, "Working directory: %s\n", kStorageDirectory);
    fclose(info);
  }
  snprintf(path, sizeof(path), "%s/top-framebuffer-rgb565.bin", directory);
  u16 width = 0, height = 0;
  u8 *top = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &width, &height);
  if (top) {
    WriteBlob(path, top, (size_t)width * height * 2);
    snprintf(path, sizeof(path), "%s/top.bmp", directory);
    SaveRgb565FramebufferBmp(path, GFX_TOP);
  }
  snprintf(path, sizeof(path), "%s/bottom-framebuffer-rgb565.bin", directory);
  u8 *bottom = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &width, &height);
  if (bottom) {
    WriteBlob(path, bottom, (size_t)width * height * 2);
    snprintf(path, sizeof(path), "%s/bottom.bmp", directory);
    SaveRgb565FramebufferBmp(path, GFX_BOTTOM);
  }
}

static u32 WaitForSetupButtons(u32 accepted, const char *screen_name) {
  static bool dump_combo_was_held;
  while (aptMainLoop()) {
    hidScanInput();
    u32 held = hidKeysHeld();
    u32 down = hidKeysDown();
    bool dump_combo =
      (held & (KEY_L | KEY_R | KEY_A)) == (KEY_L | KEY_R | KEY_A);
    if (dump_combo && !dump_combo_was_held)
      CreateSetupDump(screen_name);
    dump_combo_was_held = dump_combo;
    if (down & accepted)
      return down & accepted;
    gspWaitForVBlank();
  }
  return KEY_B;
}

static uint16_t SetupRgb565(int r, int g, int b) {
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void SetupPutPixel(gfxScreen_t screen, int x, int y, uint16_t color) {
  const int logical_w = screen == GFX_TOP ? 400 : 320;
  const int logical_h = 240;
  if (x < 0 || y < 0 || x >= logical_w || y >= logical_h)
    return;
  if (screen == GFX_TOP)
    g_setup_top_pixels[y * logical_w + x] = color;
  else
    g_setup_bottom_pixels[y * logical_w + x] = color;
}

static void SetupFillRect(gfxScreen_t screen, int x, int y,
                          int w, int h, uint16_t color) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++)
      SetupPutPixel(screen, xx, yy, color);
}

static void SetupDrawRgb565Image(gfxScreen_t screen, int x, int y,
                                 int w, int h,
                                 const uint16_t *pixels) {
  for (int yy = 0; yy < h; yy++)
    for (int xx = 0; xx < w; xx++)
      SetupPutPixel(screen, x + xx, y + yy, pixels[yy * w + xx]);
}

static void SetupDrawMaskedRgb565Image(gfxScreen_t screen, int x, int y,
                                       int w, int h,
                                       const uint16_t *pixels,
                                       const uint8_t *mask) {
  for (int yy = 0; yy < h; yy++) {
    for (int xx = 0; xx < w; xx++) {
      int index = yy * w + xx;
      if (mask[index])
        SetupPutPixel(screen, x + xx, y + yy, pixels[index]);
    }
  }
}

static void SetupRect(gfxScreen_t screen, int x, int y, int w, int h,
                      uint16_t color) {
  SetupFillRect(screen, x, y, w, 2, color);
  SetupFillRect(screen, x, y + h - 2, w, 2, color);
  SetupFillRect(screen, x, y, 2, h, color);
  SetupFillRect(screen, x + w - 2, y, 2, h, color);
}

static uint8_t SetupGlyph(char c, int row) {
  if (c >= 'a' && c <= 'z')
    c = (char)(c - 'a' + 'A');
  static const uint8_t digits[10][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,30,1,1,17,14},
    {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,2,12},
  };
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
  if (c >= '0' && c <= '9')
    return digits[c - '0'][row];
  if (c >= 'A' && c <= 'Z')
    return letters[c - 'A'][row];
  switch (c) {
    case '.': return row == 6 ? 4 : 0;
    case '-': return row == 3 ? 31 : 0;
    case '_': return row == 6 ? 31 : 0;
    case '/': return (uint8_t)(1 << (6 - row));
    case ':': return row == 2 || row == 5 ? 4 : 0;
    case '(': return row == 0 ? 2 : row == 6 ? 2 : 4;
    case ')': return row == 0 ? 8 : row == 6 ? 8 : 4;
    case '[': return row == 0 || row == 6 ? 14 : 8;
    case ']': return row == 0 || row == 6 ? 14 : 2;
    case '+': return row == 3 ? 31 : (row >= 1 && row <= 5 ? 4 : 0);
    case '!': return row < 5 ? 4 : (row == 6 ? 4 : 0);
    case '>': return row < 3 ? (1 << row) : (1 << (6 - row));
    default: return 0;
  }
}

static int SetupTextWidth(const char *text, int scale) {
  return text ? (int)strlen(text) * 6 * scale : 0;
}

static void SetupDrawChar(gfxScreen_t screen, char c, int x, int y,
                          int scale, uint16_t color) {
  if (c == ' ')
    return;
  for (int row = 0; row < 7; row++) {
    uint8_t bits = SetupGlyph(c, row);
    for (int col = 0; col < 5; col++) {
      if (bits & (1 << (4 - col)))
        SetupFillRect(screen, x + col * scale, y + row * scale,
                      scale, scale, color);
    }
  }
}

static void SetupDrawText(gfxScreen_t screen, const char *text, int x, int y,
                          int scale, uint16_t color, int max_width) {
  if (!text)
    return;
  int cursor = x;
  int last = x + max_width;
  for (size_t i = 0; text[i]; i++) {
    if (max_width > 0 && cursor + 6 * scale > last)
      break;
    SetupDrawChar(screen, text[i], cursor, y, scale, color);
    cursor += 6 * scale;
  }
}

static void SetupDrawCharScaleXY(gfxScreen_t screen, char c, int x, int y,
                                 int sx, int sy, uint16_t color) {
  if (c == ' ')
    return;
  for (int row = 0; row < 7; row++) {
    uint8_t bits = SetupGlyph(c, row);
    for (int col = 0; col < 5; col++) {
      if (bits & (1 << (4 - col)))
        SetupFillRect(screen, x + col * sx, y + row * sy, sx, sy, color);
    }
  }
}

static void SetupDrawTextScaleXY(gfxScreen_t screen, const char *text,
                                 int x, int y, int sx, int sy,
                                 uint16_t color, int max_width) {
  if (!text)
    return;
  int cursor = x;
  int last = x + max_width;
  for (size_t i = 0; text[i]; i++) {
    if (max_width > 0 && cursor + 6 * sx > last)
      break;
    SetupDrawCharScaleXY(screen, text[i], cursor, y, sx, sy, color);
    cursor += 6 * sx;
  }
}

static int SetupTextWidthRational(const char *text, int num, int den) {
  return text ? (int)(strlen(text) * 6 * num / den) : 0;
}

static void SetupDrawCharRational(gfxScreen_t screen, char c, int x, int y,
                                  int num, int den, uint16_t color) {
  if (c == ' ')
    return;
  for (int row = 0; row < 7; row++) {
    uint8_t bits = SetupGlyph(c, row);
    for (int col = 0; col < 5; col++) {
      if (!(bits & (1 << (4 - col))))
        continue;
      int x0 = x + col * num / den;
      int x1 = x + (col + 1) * num / den;
      int y0 = y + row * num / den;
      int y1 = y + (row + 1) * num / den;
      if (x1 <= x0)
        x1 = x0 + 1;
      if (y1 <= y0)
        y1 = y0 + 1;
      SetupFillRect(screen, x0, y0, x1 - x0, y1 - y0, color);
    }
  }
}

static void SetupDrawTextRational(gfxScreen_t screen, const char *text,
                                  int x, int y, int num, int den,
                                  uint16_t color) {
  if (!text)
    return;
  int cursor = x;
  for (size_t i = 0; text[i]; i++) {
    SetupDrawCharRational(screen, text[i], cursor, y, num, den, color);
    cursor += 6 * num / den;
  }
}

static void SetupDrawCentered(gfxScreen_t screen, const char *text, int y,
                              int scale, uint16_t color) {
  int w = screen == GFX_TOP ? 400 : 320;
  int x = (w - SetupTextWidth(text, scale)) / 2;
  SetupDrawText(screen, text, x, y, scale, color, 0);
}

static void SetupClearScreens(void) {
  const uint16_t bg = SetupRgb565(8, 12, 24);
  const uint16_t shadow = SetupRgb565(2, 4, 10);
  SetupFillRect(GFX_TOP, 0, 0, 400, 240, bg);
  SetupFillRect(GFX_BOTTOM, 0, 0, 320, 240, bg);
  for (int y = 0; y < 240; y += 16) {
    SetupFillRect(GFX_TOP, 0, y, 400, 1, shadow);
    SetupFillRect(GFX_BOTTOM, 0, y, 320, 1, shadow);
  }
}

static void SetupDrawPanel(gfxScreen_t screen, int x, int y, int w, int h,
                           bool active) {
  const uint16_t blue = SetupRgb565(16, 38, 86);
  const uint16_t blue2 = SetupRgb565(10, 24, 58);
  const uint16_t gold = SetupRgb565(232, 184, 72);
  const uint16_t gray = SetupRgb565(92, 112, 142);
  SetupFillRect(screen, x + 4, y + 4, w, h, SetupRgb565(0, 0, 0));
  SetupFillRect(screen, x, y, w, h, blue2);
  SetupFillRect(screen, x + 4, y + 4, w - 8, h - 8, blue);
  SetupRect(screen, x, y, w, h, active ? gold : gray);
  if (active)
    SetupRect(screen, x + 3, y + 3, w - 6, h - 6, SetupRgb565(248, 224, 128));
}

static void SetupCopyDisplayText(const char *source,
                                 char *out, size_t out_size) {
  if (!out || out_size == 0)
    return;
  out[0] = 0;
  if (!source)
    return;
  size_t length = strlen(source);
  const size_t max_chars = out_size - 1;
  if (length <= max_chars) {
    snprintf(out, out_size, "%s", source);
    return;
  }
  if (max_chars < 4)
    return;
  size_t prefix = (max_chars - 3) / 2;
  size_t suffix = max_chars - 3 - prefix;
  memcpy(out, source, prefix);
  out[prefix + 0] = '.';
  out[prefix + 1] = '.';
  out[prefix + 2] = '.';
  memcpy(out + prefix + 3, source + length - suffix, suffix);
  out[max_chars] = 0;
}

static void DrawSetupMessage(const char *title, const char *line1,
                             const char *line2, const char *line3) {
  const uint16_t white = SetupRgb565(232, 240, 248);
  const uint16_t gold = SetupRgb565(232, 184, 72);
  SetupClearScreens();
  SetupDrawPanel(GFX_TOP, 42, 42, 316, 122, true);
  SetupDrawCentered(GFX_TOP, title, 64, 3, gold);
  if (line1) SetupDrawCentered(GFX_TOP, line1, 110, 2, white);
  if (line2) SetupDrawCentered(GFX_BOTTOM, line2, 82, 2, white);
  if (line3) SetupDrawCentered(GFX_BOTTOM, line3, 118, 2, gold);
}

typedef struct SetupMessageContext {
  const char *title;
  const char *line1;
  const char *line2;
  const char *line3;
} SetupMessageContext;

static void DrawSetupMessageFrame(void *context) {
  SetupMessageContext *message = (SetupMessageContext *)context;
  DrawSetupMessage(message->title, message->line1,
                   message->line2, message->line3);
}

static void PresentSetupMessage(const char *title, const char *line1,
                                const char *line2, const char *line3) {
  SetupMessageContext context = { title, line1, line2, line3 };
  PresentSetupStable(DrawSetupMessageFrame, &context);
}

static void ShowFatalSetupError(const char *message) {
  FILE *log = fopen("setup-error.txt", "wb");
  if (log) {
    fprintf(log, "Zelda 3DS v%s\n%s\n", ZELDA3_3DS_VERSION, message);
    fclose(log);
  }
  PresentSetupMessage("SETUP ERROR", "CHECK SETUP-ERROR.TXT",
                      "PRESS B TO EXIT", NULL);
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
  PresentSetupMessage("ROM SETUP", "ASSETS NOT FOUND",
                      "A EXTRACT    B EXIT", "SFC OR SMC IN ZELDA 3DS");
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

typedef struct OwnedBlock {
  uint8 *data;
  size_t size;
} OwnedBlock;

static uint16 ReadU16LE(const uint8 *data) {
  return data[0] | (data[1] << 8);
}

static uint32 ReadU32LE(const uint8 *data) {
  return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

static void WriteU16LE(uint8 *data, uint16 value) {
  data[0] = value;
  data[1] = value >> 8;
}

static void WriteU32LE(uint8 *data, uint32 value) {
  data[0] = value;
  data[1] = value >> 8;
  data[2] = value >> 16;
  data[3] = value >> 24;
}

static uint64 SetupBpsDecodeInt(const uint8 **src) {
  uint64 data = 0, shift = 1;
  for (;;) {
    uint8 x = *(*src)++;
    data += (x & 0x7f) * shift;
    if (x & 0x80)
      break;
    shift <<= 7;
    data += shift;
  }
  return data;
}

static uint8 *ApplyBpsCompatibleRom(const uint8 *src, size_t src_size_in,
                                    const uint8 *bps, size_t bps_size,
                                    size_t *length_out) {
  if (bps_size < 16 || memcmp(bps, "BPS1", 4) != 0)
    return NULL;
  const uint8 *patch = bps + 4;
  const uint8 *patch_end = bps + bps_size - 12;
  uint32 src_size = (uint32)SetupBpsDecodeInt(&patch);
  uint32 dst_size = (uint32)SetupBpsDecodeInt(&patch);
  uint32 meta_size = (uint32)SetupBpsDecodeInt(&patch);
  if (src_size > src_size_in || patch + meta_size > patch_end)
    return NULL;
  patch += meta_size;
  uint8 *dst = malloc(dst_size);
  if (!dst)
    return NULL;
  uint32 output_offset = 0;
  uint32 source_relative_offset = 0;
  uint32 target_relative_offset = 0;
  while (patch < patch_end) {
    uint32 command = (uint32)SetupBpsDecodeInt(&patch);
    uint32 length = (command >> 2) + 1;
    if (output_offset + length > dst_size)
      goto fail;
    switch (command & 3) {
    case 0:
      if (output_offset + length > src_size_in)
        goto fail;
      memcpy(dst + output_offset, src + output_offset, length);
      output_offset += length;
      break;
    case 1:
      if (patch + length > patch_end)
        goto fail;
      memcpy(dst + output_offset, patch, length);
      patch += length;
      output_offset += length;
      break;
    case 2:
      command = (uint32)SetupBpsDecodeInt(&patch);
      source_relative_offset += (command & 1 ? -1 : +1) * (command >> 1);
      if (source_relative_offset + length > src_size_in)
        goto fail;
      memcpy(dst + output_offset, src + source_relative_offset, length);
      output_offset += length;
      source_relative_offset += length;
      break;
    default:
      command = (uint32)SetupBpsDecodeInt(&patch);
      target_relative_offset += (command & 1 ? -1 : +1) * (command >> 1);
      if (target_relative_offset >= output_offset)
        goto fail;
      for (uint32 i = 0; i < length; i++)
        dst[output_offset++] = dst[target_relative_offset++];
      break;
    }
  }
  if (output_offset != dst_size)
    goto fail;
  *length_out = dst_size;
  return dst;

fail:
  free(dst);
  return NULL;
}

static uint8 *NormalizeRomForExtraction(uint8 *rom, size_t *rom_size) {
  if (!rom || !rom_size)
    return rom;
  if ((*rom_size % 0x8000) == 512) {
    LogSetup("Detected 512-byte copier header; stripping for extraction");
    memmove(rom, rom + 512, *rom_size - 512);
    *rom_size -= 512;
  }
  return rom;
}

static void FreeBlocks(OwnedBlock *blocks, int count) {
  for (int i = 0; i < count; i++)
    free(blocks[i].data);
}

static bool CopyBlock(OwnedBlock *block, const uint8 *data, size_t size) {
  block->data = NULL;
  block->size = 0;
  if (size == 0)
    return true;
  block->data = malloc(size);
  if (!block->data)
    return false;
  memcpy(block->data, data, size);
  block->size = size;
  return true;
}

static bool PackBlocks(const OwnedBlock *blocks, int count, OwnedBlock *out) {
  out->data = NULL;
  out->size = 0;
  if (count <= 0)
    return false;
  size_t data_size = 0;
  for (int i = 0; i < count; i++)
    data_size += blocks[i].size;
  bool wide = data_size >= 65536 || count > 8192;
  size_t width = wide ? 4 : 2;
  size_t header = (count - 1) * width;
  if (data_size + header + 2 > 0xffffffffu)
    return false;
  uint8 *data = calloc(1, header + data_size + 2);
  if (!data)
    return false;
  size_t pos = header;
  size_t cumulative = 0;
  for (int i = 0; i < count; i++) {
    if (i != 0) {
      if (wide)
        WriteU32LE(data + (i - 1) * 4, (uint32)cumulative);
      else
        WriteU16LE(data + (i - 1) * 2, (uint16)cumulative);
    }
    if (blocks[i].size)
      memcpy(data + pos + cumulative, blocks[i].data, blocks[i].size);
    cumulative += blocks[i].size;
  }
  WriteU16LE(data + header + data_size, (uint16)((count - 1) + (wide ? 8192 : 0)));
  out->data = data;
  out->size = header + data_size + 2;
  return true;
}

static bool UnpackBlocks(const uint8 *data, size_t size,
                         OwnedBlock **blocks_out, int *count_out) {
  *blocks_out = NULL;
  *count_out = 0;
  if (size < 2)
    return false;
  size_t end = size - 2;
  uint16 trailer = ReadU16LE(data + end);
  int width = 2;
  int count = trailer + 1;
  if (trailer >= 8192) {
    width = 4;
    count = trailer - 8192 + 1;
  }
  size_t base = (size_t)(count - 1) * width;
  if (count <= 0 || base > end)
    return false;
  OwnedBlock *blocks = calloc(count, sizeof(*blocks));
  if (!blocks)
    return false;
  size_t previous = 0;
  for (int i = 0; i < count; i++) {
    size_t next = (i == count - 1) ? end - base :
      (width == 2 ? ReadU16LE(data + i * 2) : ReadU32LE(data + i * 4));
    if (next < previous || base + next > end) {
      FreeBlocks(blocks, i);
      free(blocks);
      return false;
    }
    if (!CopyBlock(&blocks[i], data + base + previous, next - previous)) {
      FreeBlocks(blocks, i);
      free(blocks);
      return false;
    }
    previous = next;
  }
  *blocks_out = blocks;
  *count_out = count;
  return true;
}

static bool ExtractTranslationLanguage(const uint8 *rom, size_t rom_size,
                                       OwnedBlock *dialogue_block,
                                       OwnedBlock *font_block) {
  enum {
    kRomSize = 1048576,
    kTextBank1 = 0xe0000,
    kTextBank2 = 0x75f40,
    kDictPtrs = 0x74703,
    kDictPtrBase = 0xc703,
    kBank0E = 0x70000,
    kFontGfx = 0x70000,
    kFontGfxSize = 0x1000,
    kFontWidths = 0x74adf,
    kFontWidthsCount = 99,
  };
  static const uint8 kCommandArgBytes[] = {
    0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
    1, 1, 0, 0, 0, 0, 0,
  };
  static const uint8 kUsMessage4[] = {
    0x7a, 0x00, 0x34, 0x40, 0x59, 0x6c, 0x00, 0x41, 0x59, 0x35,
    0x40, 0x59, 0x6c, 0x01, 0x75, 0x36, 0x40, 0x59, 0x6c, 0x02,
    0x41, 0x59, 0x37, 0x40, 0x59, 0x6c, 0x03,
  };
  dialogue_block->data = NULL;
  dialogue_block->size = 0;
  font_block->data = NULL;
  font_block->size = 0;
  if (rom_size != kRomSize)
    return false;

  OwnedBlock messages[420] = {0};
  int message_count = 0;
  ByteArray current = {0};
  size_t p = kTextBank1;
  int bank_switches = 0;
  bool ok = true;
  for (int guard = 0; ok; guard++) {
    if (guard > 0x20000 || p >= rom_size || message_count >= countof(messages)) {
      ok = false;
      break;
    }
    uint8 c = rom[p];
    if (c == 0xff)
      break;
    if (c == 0x80) {
      if (++bank_switches > 1) {
        ok = false;
        break;
      }
      p = kTextBank2;
      continue;
    }
    size_t length = (c >= 0x67 && c < 0x80) ?
      1 + kCommandArgBytes[c - 0x67] : 1;
    if (p + length > rom_size) {
      ok = false;
      break;
    }
    ByteArray_AppendData(&current, rom + p, length);
    p += length;
    if (c == 0x7f) {
      if (current.size == 0) {
        ok = false;
        break;
      }
      current.size--;
      ok = CopyBlock(&messages[message_count++], current.data, current.size);
      current.size = 0;
    }
  }
  ByteArray_Destroy(&current);
  if (!ok || message_count < 300) {
    FreeBlocks(messages, message_count);
    return false;
  }
  if (message_count == 396 && message_count < countof(messages)) {
    for (int i = message_count; i > 4; i--)
      messages[i] = messages[i - 1];
    messages[4].data = NULL;
    messages[4].size = 0;
    ok = CopyBlock(&messages[4], kUsMessage4, sizeof(kUsMessage4));
    message_count++;
  }

  uint16 first = ReadU16LE(rom + kDictPtrs);
  int gap = (int)first - kDictPtrBase;
  int dict_count = gap / 2 - 1;
  OwnedBlock dictionary[220] = {0};
  if (!ok || gap <= 0 || (gap & 1) != 0 || dict_count < 50 ||
      dict_count > countof(dictionary)) {
    FreeBlocks(messages, message_count);
    return false;
  }
  for (int i = 0; i < dict_count; i++) {
    uint16 start = ReadU16LE(rom + kDictPtrs + i * 2);
    uint16 end = ReadU16LE(rom + kDictPtrs + i * 2 + 2);
    size_t offset = kBank0E + start - 0x8000;
    if (start < 0x8000 || end < start || end - start > 256 ||
        offset + (end - start) > rom_size ||
        !CopyBlock(&dictionary[i], rom + offset, end - start)) {
      FreeBlocks(messages, message_count);
      FreeBlocks(dictionary, i);
      return false;
    }
  }

  OwnedBlock packed_dictionary = {0};
  OwnedBlock packed_messages = {0};
  OwnedBlock dialogue_parts[2] = {0};
  OwnedBlock font_parts[2] = {0};
  ok = PackBlocks(dictionary, dict_count, &packed_dictionary) &&
       PackBlocks(messages, message_count, &packed_messages);
  FreeBlocks(dictionary, dict_count);
  FreeBlocks(messages, message_count);
  if (!ok) {
    free(packed_dictionary.data);
    free(packed_messages.data);
    return false;
  }
  dialogue_parts[0] = packed_dictionary;
  dialogue_parts[1] = packed_messages;
  ok = PackBlocks(dialogue_parts, 2, dialogue_block);
  FreeBlocks(dialogue_parts, 2);
  if (!ok)
    return false;

  ok = CopyBlock(&font_parts[0], rom + kFontGfx, kFontGfxSize) &&
       CopyBlock(&font_parts[1], rom + kFontWidths, kFontWidthsCount) &&
       PackBlocks(font_parts, 2, font_block);
  FreeBlocks(font_parts, 2);
  if (!ok) {
    free(dialogue_block->data);
    dialogue_block->data = NULL;
    dialogue_block->size = 0;
    return false;
  }
  return true;
}

static const char *DetectTranslatedRomLanguage(const uint8 *rom,
                                               size_t rom_size) {
  static const char kPortugueseTitle[] = "A LENDA DE ZELDA (BR)";
  if (rom_size >= 0x7fc0 + sizeof(kPortugueseTitle) - 1 &&
      memcmp(rom + 0x7fc0, kPortugueseTitle,
             sizeof(kPortugueseTitle) - 1) == 0)
    return "pt";
  return "es";
}

static bool RomLooksJapanese(const uint8 *rom, size_t rom_size) {
  static const char kJapaneseTitle[] = "ZELDANODENSETSU";
  return rom_size >= 0x7fc0 + sizeof(kJapaneseTitle) - 1 &&
         memcmp(rom + 0x7fc0, kJapaneseTitle,
                sizeof(kJapaneseTitle) - 1) == 0;
}

static bool AddTranslatedLanguageToAssets(const uint8 *assets_data,
                                          size_t assets_size,
                                          const uint8 *rom,
                                          size_t rom_size,
                                          const char *language_code,
                                          uint8 **out_data,
                                          size_t *out_size) {
  *out_data = NULL;
  *out_size = 0;
  OwnedBlock language_dialogue = {0};
  OwnedBlock language_font = {0};
  if (!ExtractTranslationLanguage(rom, rom_size,
                                  &language_dialogue, &language_font))
    return false;
  if (assets_size < 88) {
    FreeBlocks(&language_dialogue, 1);
    FreeBlocks(&language_font, 1);
    return false;
  }
  static const uint8 signature[] = { kAssets_Sig };
  uint32 asset_count = ReadU32LE(assets_data + 80);
  uint32 names_size = ReadU32LE(assets_data + 84);
  if (memcmp(assets_data, signature, sizeof(signature)) != 0 ||
      asset_count != kNumberOfAssets ||
      assets_size < 88 + asset_count * 4 + names_size) {
    FreeBlocks(&language_dialogue, 1);
    FreeBlocks(&language_font, 1);
    return false;
  }

  OwnedBlock *assets = calloc(asset_count, sizeof(*assets));
  if (!assets) {
    FreeBlocks(&language_dialogue, 1);
    FreeBlocks(&language_font, 1);
    return false;
  }
  bool ok = true;
  size_t offset = 88 + asset_count * 4 + names_size;
  for (uint32 i = 0; ok && i < asset_count; i++) {
    uint32 size = ReadU32LE(assets_data + 88 + i * 4);
    offset = (offset + 3) & ~3;
    if (offset + size > assets_size ||
        !CopyBlock(&assets[i], assets_data + offset, size))
      ok = false;
    offset += size;
  }
  OwnedBlock *dialogues = NULL, *fonts = NULL, *map = NULL;
  int dialogue_count = 0, font_count = 0, map_count = 0;
  if (ok)
    ok = UnpackBlocks(assets[94].data, assets[94].size,
                      &dialogues, &dialogue_count) &&
         UnpackBlocks(assets[95].data, assets[95].size,
                      &fonts, &font_count) &&
         UnpackBlocks(assets[96].data, assets[96].size,
                      &map, &map_count);
  if (ok && (dialogue_count != font_count || map_count <= 0))
    ok = false;
  if (ok) {
    int index = dialogue_count;
    OwnedBlock *new_dialogues = calloc(dialogue_count + 1, sizeof(*new_dialogues));
    OwnedBlock *new_fonts = calloc(font_count + 1, sizeof(*new_fonts));
    OwnedBlock *new_map = calloc(map_count + 1, sizeof(*new_map));
    if (!new_dialogues || !new_fonts || !new_map) {
      free(new_dialogues);
      free(new_fonts);
      free(new_map);
      ok = false;
    } else {
      memcpy(new_dialogues, dialogues, dialogue_count * sizeof(*dialogues));
      memcpy(new_fonts, fonts, font_count * sizeof(*fonts));
      memcpy(new_map, map, map_count * sizeof(*map));
      free(dialogues);
      free(fonts);
      free(map);
      dialogues = new_dialogues;
      fonts = new_fonts;
      map = new_map;
      dialogue_count++;
      font_count++;
      map_count++;
      dialogues[index] = language_dialogue;
      fonts[index] = language_font;
      language_dialogue.data = NULL;
      language_font.data = NULL;
      OwnedBlock map_parts[2] = {0};
      OwnedBlock packed_map_entry = {0};
      uint8 name[8];
      size_t name_size = strlen(language_code);
      if (name_size == 0 || name_size > sizeof(name)) {
        ok = false;
      }
      if (ok)
        memcpy(name, language_code, name_size);
      uint8 conf[] = { (uint8)index, (uint8)index, 2 };
      ok = ok &&
           CopyBlock(&map_parts[0], name, name_size) &&
           CopyBlock(&map_parts[1], conf, sizeof(conf)) &&
           PackBlocks(map_parts, 2, &packed_map_entry);
      FreeBlocks(map_parts, 2);
      if (ok) {
        map[index] = packed_map_entry;
        packed_map_entry.data = NULL;
        packed_map_entry.size = 0;
      }
    }
  }
  OwnedBlock packed_dialogues = {0};
  OwnedBlock packed_fonts = {0};
  OwnedBlock packed_map = {0};
  if (ok)
    ok = PackBlocks(dialogues, dialogue_count, &packed_dialogues) &&
         PackBlocks(fonts, font_count, &packed_fonts) &&
         PackBlocks(map, map_count, &packed_map);
  if (ok) {
    free(assets[94].data);
    free(assets[95].data);
    free(assets[96].data);
    assets[94] = packed_dialogues;
    assets[95] = packed_fonts;
    assets[96] = packed_map;
    packed_dialogues.data = NULL;
    packed_fonts.data = NULL;
    packed_map.data = NULL;
    size_t total = 88 + asset_count * 4 + names_size;
    for (uint32 i = 0; i < asset_count; i++)
      total = ((total + 3) & ~3) + assets[i].size;
    uint8 *out = calloc(1, total);
    if (!out) {
      ok = false;
    } else {
      memcpy(out, assets_data, 88);
      for (uint32 i = 0; i < asset_count; i++)
        WriteU32LE(out + 88 + i * 4, (uint32)assets[i].size);
      memcpy(out + 88 + asset_count * 4,
             assets_data + 88 + asset_count * 4, names_size);
      offset = 88 + asset_count * 4 + names_size;
      for (uint32 i = 0; i < asset_count; i++) {
        offset = (offset + 3) & ~3;
        memcpy(out + offset, assets[i].data, assets[i].size);
        offset += assets[i].size;
      }
      *out_data = out;
      *out_size = total;
    }
  }

  FreeBlocks(&language_dialogue, 1);
  FreeBlocks(&language_font, 1);
  FreeBlocks(dialogues, dialogue_count);
  FreeBlocks(fonts, font_count);
  FreeBlocks(map, map_count);
  free(dialogues);
  free(fonts);
  free(map);
  FreeBlocks(&packed_dialogues, 1);
  FreeBlocks(&packed_fonts, 1);
  FreeBlocks(&packed_map, 1);
  FreeBlocks(assets, asset_count);
  free(assets);
  return ok;
}

static bool WriteTranslatedProfileIni(const char *language_code) {
  FILE *file = fopen("zelda3.ini", "ab");
  if (!file)
    return false;
  bool ok = fprintf(file, "\n[General]\nLanguage = %s\n",
                    language_code) > 0;
  if (fclose(file) != 0)
    ok = false;
  return ok;
}

static bool TryBuildTranslatedAssets(const uint8 *rom, size_t rom_size,
                                     const uint8 *patch, size_t patch_size,
                                     uint8 **assets_out,
                                     size_t *assets_size_out) {
  *assets_out = NULL;
  *assets_size_out = 0;
  OwnedBlock probe_dialogue = {0};
  OwnedBlock probe_font = {0};
  bool translatable = ExtractTranslationLanguage(rom, rom_size,
                                                &probe_dialogue, &probe_font);
  FreeBlocks(&probe_dialogue, 1);
  FreeBlocks(&probe_font, 1);
  if (!translatable)
    return false;
  const char *language_code = DetectTranslatedRomLanguage(rom, rom_size);

  DIR *directory = opendir("../..");
  if (!directory)
    return false;
  bool success = false;
  struct dirent *entry;
  while (!success && (entry = readdir(directory)) != NULL) {
    if (RomFileShouldBeIgnored(entry->d_name) ||
        !(HasExtension(entry->d_name, ".sfc") ||
          HasExtension(entry->d_name, ".smc")))
      continue;
    char path[640];
    snprintf(path, sizeof(path), "../../%s", entry->d_name);
    if (!IsRegularFile(path))
      continue;
    size_t base_size = 0;
    uint8 *base_rom = ReadWholeFile(path, &base_size);
    if (!base_rom)
      continue;
    base_rom = NormalizeRomForExtraction(base_rom, &base_size);
    size_t base_assets_size = 0;
    uint8 *base_assets = ApplyBps(base_rom, base_size, patch, patch_size,
                                  &base_assets_size);
    if (!base_assets)
      base_assets = ApplyBpsCompatibleRom(base_rom, base_size, patch,
                                          patch_size, &base_assets_size);
    free(base_rom);
    if (base_assets && !AssetsBlobLooksValid(base_assets, base_assets_size)) {
      LogSetup("Installed base ROM produced invalid compatible assets: %s",
               entry->d_name);
      free(base_assets);
      base_assets = NULL;
      base_assets_size = 0;
    }
    if (!base_assets)
      continue;
    uint8 *translated_assets = NULL;
    size_t translated_assets_size = 0;
    success = AddTranslatedLanguageToAssets(base_assets, base_assets_size,
                                            rom, rom_size, language_code,
                                            &translated_assets,
                                            &translated_assets_size);
    free(base_assets);
    if (success) {
      *assets_out = translated_assets;
      *assets_size_out = translated_assets_size;
      WriteTranslatedProfileIni(language_code);
      LogSetup("Translated profile assets generated using base ROM: %s, language: %s",
               entry->d_name, language_code);
    } else {
      free(translated_assets);
    }
  }
  closedir(directory);
  return success;
}

static bool TryBuildBaseAssetsFromInstalledUsRom(const uint8 *patch,
                                                 size_t patch_size,
                                                 uint8 **assets_out,
                                                 size_t *assets_size_out) {
  *assets_out = NULL;
  *assets_size_out = 0;
  DIR *directory = opendir("../..");
  if (!directory)
    return false;
  bool success = false;
  struct dirent *entry;
  while (!success && (entry = readdir(directory)) != NULL) {
    if (RomFileShouldBeIgnored(entry->d_name) ||
        !(HasExtension(entry->d_name, ".sfc") ||
          HasExtension(entry->d_name, ".smc")))
      continue;
    char path[640];
    snprintf(path, sizeof(path), "../../%s", entry->d_name);
    if (!IsRegularFile(path))
      continue;
    size_t base_size = 0;
    uint8 *base_rom = ReadWholeFile(path, &base_size);
    if (!base_rom)
      continue;
    base_rom = NormalizeRomForExtraction(base_rom, &base_size);
    size_t base_assets_size = 0;
    uint8 *base_assets = ApplyBps(base_rom, base_size, patch, patch_size,
                                  &base_assets_size);
    if (!base_assets)
      base_assets = ApplyBpsCompatibleRom(base_rom, base_size, patch,
                                          patch_size, &base_assets_size);
    free(base_rom);
    if (base_assets && !AssetsBlobLooksValid(base_assets, base_assets_size)) {
      LogSetup("Installed fallback ROM produced invalid compatible assets: %s",
               entry->d_name);
      free(base_assets);
      base_assets = NULL;
      base_assets_size = 0;
    }
    if (base_assets && AssetsBlobLooksValid(base_assets, base_assets_size)) {
      *assets_out = base_assets;
      *assets_size_out = base_assets_size;
      success = true;
      LogSetup("Base assets generated using installed USA-compatible ROM: %s",
               entry->d_name);
    } else {
      free(base_assets);
    }
  }
  closedir(directory);
  return success;
}

static bool ExtractAssetsFromRom(const char *rom_path) {
  LogSetup("Extraction requested");
  LogSetup("ROM found: %s", rom_path);

  size_t rom_size = 0;
  size_t patch_size = 0;
  size_t assets_size = 0;
  uint8 *rom = ReadWholeFile(rom_path, &rom_size);
  rom = NormalizeRomForExtraction(rom, &rom_size);
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
  if (!assets) {
    LogSetup("Trying compatible BPS extraction");
    assets = ApplyBpsCompatibleRom(rom, rom_size, patch, patch_size,
                                   &assets_size);
    LogSetup("Compatible BPS result: %s, %lu bytes",
             assets ? "OK" : "FAIL", (unsigned long)assets_size);
    if (assets && !AssetsBlobLooksValid(assets, assets_size)) {
      LogSetup("Compatible BPS result failed assets validation");
      free(assets);
      assets = NULL;
      assets_size = 0;
    }
  }
  if (!assets) {
    LogSetup("Trying compatible translation extraction");
    assets = NULL;
    assets_size = 0;
    if (TryBuildTranslatedAssets(rom, rom_size, patch, patch_size,
                                 &assets, &assets_size)) {
      LogSetup("Translation assets result: OK, %lu bytes",
               (unsigned long)assets_size);
    }
  }
  if (!assets && RomLooksJapanese(rom, rom_size)) {
    LogSetup("Trying Japanese ROM boot fallback with USA-compatible base assets");
    if (TryBuildBaseAssetsFromInstalledUsRom(patch, patch_size,
                                             &assets, &assets_size)) {
      LogSetup("Japanese fallback assets result: OK, %lu bytes",
               (unsigned long)assets_size);
    }
  }
  free(rom);
  free(patch);
  if (!assets) {
    LogSetup("ROM not compatible with available extraction paths");
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

  LogSetup("Assets extracted and validated");
  return true;
}

static bool RomUsesBundledAssetsPatch(const RomEntry *rom) {
  size_t rom_size = 0;
  size_t patch_size = 0;
  uint8 *rom_data = ReadWholeFile(rom->filename, &rom_size);
  uint8 *patch = ReadWholeFile(kBundledPatch, &patch_size);
  if (!rom_data || !patch) {
    free(rom_data);
    free(patch);
    return false;
  }
  size_t assets_size = 0;
  uint8 *assets = ApplyBps(rom_data, rom_size, patch, patch_size,
                           &assets_size);
  free(rom_data);
  free(patch);
  if (!assets)
    return false;
  bool valid = AssetsBlobLooksValid(assets, assets_size);
  free(assets);
  return valid;
}

static bool WriteSelectedRom(const RomEntry *rom) {
  FILE *file = fopen(kSelectedRomFile, "wb");
  if (!file)
    return false;
  fprintf(file, "[SelectedRom]\n");
  fprintf(file, "RomFile = %s\n", rom->filename);
  fprintf(file, "RomHash = %08lx\n", (unsigned long)rom->hash);
  fprintf(file, "ActiveProfile = %s\n", rom->profile);
  bool ok = fclose(file) == 0;
  if (!ok)
    remove(kSelectedRomFile);
  return ok;
}

static const char *ProfileLeaf(const char *profile) {
  const char *slash = strrchr(profile, '/');
  return slash ? slash + 1 : profile;
}

static uint32_t ProfileId(const char *profile) {
  const uint8_t *text = (const uint8_t *)ProfileLeaf(profile);
  uint32_t hash = 2166136261u;
  while (*text) {
    hash ^= *text++;
    hash *= 16777619u;
  }
  return hash;
}

static bool PrepareSaveDirectory(const RomEntry *rom, bool copy_legacy) {
  if (!EnsureDirectory("saves"))
    return false;
  snprintf(g_active_save_directory, sizeof(g_active_save_directory),
           "saves/%s", ProfileLeaf(rom->profile));
  g_active_profile_id = ProfileId(rom->profile);
  if (!EnsureDirectory(g_active_save_directory))
    return false;

  if (copy_legacy) {
    static const char *const legacy_files[] = {
      "sram.dat", "sram.bak",
      "save0.sav", "save1.sav", "save2.sav", "save3.sav",
      "save4.sav", "save5.sav", "save6.sav", "save7.sav",
      "save8.sav", "save9.sav",
    };
    for (size_t i = 0; i < countof(legacy_files); i++) {
      char source[256];
      char destination[512];
      snprintf(source, sizeof(source), "saves/%s", legacy_files[i]);
      snprintf(destination, sizeof(destination), "%s/%s",
               g_active_save_directory, legacy_files[i]);
      if (IsRegularFile(source))
        CopyFileIfMissing(source, destination);
    }
  }
  return true;
}

static bool PrepareSaveDirectoryForProfile(const char *profile) {
  if (!EnsureDirectory("saves"))
    return false;
  snprintf(g_active_save_directory, sizeof(g_active_save_directory),
           "saves/%s", ProfileLeaf(profile));
  g_active_profile_id = ProfileId(profile);
  return EnsureDirectory(g_active_save_directory);
}

void Platform3DS_FormatSavePath(const char *filename,
                                char *out, size_t out_size) {
  if (!filename || !out || out_size == 0)
    return;
  if (strncmp(filename, "saves/ref/", 10) == 0) {
    snprintf(out, out_size, "%s", filename);
    return;
  }
  const char *leaf = filename;
  if (strncmp(filename, "saves/", 6) == 0)
    leaf = filename + 6;
  snprintf(out, out_size, "%s/%s", g_active_save_directory, leaf);
}

typedef struct SelectedRomInfo {
  char filename[256];
  char profile[320];
  uint32_t hash;
} SelectedRomInfo;

static bool ReadSelectedRomInfo(SelectedRomInfo *selected) {
  if (!selected)
    return false;
  memset(selected, 0, sizeof(*selected));
  FILE *file = fopen(kSelectedRomFile, "rb");
  if (!file)
    return false;
  char line[512];
  while (fgets(line, sizeof(line), file)) {
    char *text = Trim(line);
    char *equals = strchr(text, '=');
    if (!equals)
      continue;
    *equals = 0;
    char *key = Trim(text);
    char *value = Trim(equals + 1);
    if (strcasecmp(key, "RomFile") == 0) {
      snprintf(selected->filename, sizeof(selected->filename), "%s", value);
    } else if (strcasecmp(key, "RomHash") == 0) {
      selected->hash = (uint32_t)strtoul(value, NULL, 16);
    } else if (strcasecmp(key, "ActiveProfile") == 0) {
      snprintf(selected->profile, sizeof(selected->profile), "%s", value);
    }
  }
  fclose(file);
  return selected->filename[0] && selected->profile[0] &&
         selected->hash != 0;
}

static bool FindSelectedRomEntry(const SelectedRomInfo *selected,
                                 const RomEntry *roms, int rom_count,
                                 RomEntry *rom_out) {
  if (!selected)
    return false;
  for (int i = 0; i < rom_count; i++) {
    if (strcmp(selected->filename, roms[i].filename) == 0 &&
        strcmp(selected->profile, roms[i].profile) == 0 &&
        selected->hash == roms[i].hash) {
      if (rom_out)
        *rom_out = roms[i];
      return true;
    }
  }
  return false;
}

static bool ProfileAssetsValid(const char *profile) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", profile, kAssetsFilename);
  return AssetsFileLooksValid(path);
}

static bool MigrateLegacyStorage(const RomEntry *rom) {
  bool legacy_assets = AssetsFileLooksValid(kAssetsFilename);
  bool legacy_saves = IsRegularFile("saves/sram.dat");
  if (!legacy_assets && !legacy_saves)
    return true;
  LogSetup("Legacy 2.4 storage detected");
  if (!EnsureDirectory(kProfilesDirectory) || !EnsureDirectory(rom->profile))
    return false;
  char path[512];
  if (legacy_assets) {
    snprintf(path, sizeof(path), "%s/%s", rom->profile, kAssetsFilename);
    CopyFileIfMissing(kAssetsFilename, path);
  }
  snprintf(path, sizeof(path), "%s/zelda3.ini", rom->profile);
  CopyFileIfMissing("zelda3.ini", path);
  if (!PrepareSaveDirectory(rom, true))
    return false;
  WriteSelectedRom(rom);
  LogSetup("Legacy migration completed: %s", rom->profile);
  return true;
}

static void DrawRomSelector(const RomEntry *roms, int rom_count, int selected,
                            const char *status) {
  const uint16_t white = SetupRgb565(232, 240, 248);
  const uint16_t shadow = SetupRgb565(24, 24, 32);
  const uint16_t pale = SetupRgb565(216, 216, 232);
  const uint16_t red = SetupRgb565(240, 56, 56);
  const uint16_t title_shadow = SetupRgb565(64, 64, 80);
  SetupDrawRgb565Image(GFX_TOP, 0, 0,
                       kSetupTopBgWidth, kSetupTopBgHeight,
                       kSetupTopBgRgb565);
  SetupDrawRgb565Image(GFX_BOTTOM, 0, 0,
                       kSetupBottomBgWidth, kSetupBottomBgHeight,
                       kSetupBottomBgRgb565);
  const char *title = "ROM SELECT";
  int title_w = SetupTextWidthRational(title, 3, 2);
  int title_x = 102 + (134 - title_w) / 2;
  int title_y = 21 + (18 - (7 * 3 / 2)) / 2;
  SetupDrawTextRational(GFX_TOP, title, title_x + 1, title_y + 1,
                        3, 2, title_shadow);
  SetupDrawTextRational(GFX_TOP, title, title_x, title_y, 3, 2, white);

  const int visible_slots = 5;
  int first = selected - 2;
  if (first < 0)
    first = 0;
  if (first + visible_slots > rom_count)
    first = rom_count > visible_slots ? rom_count - visible_slots : 0;
  const int list_left = 84;
  const int list_width = 232;
  const int fairy_x = 104;
  const int list_y = 76;
  const int row_h = 28;
  for (int slot = 0; slot < visible_slots; slot++) {
    int index = first + slot;
    int y = list_y + slot * row_h;
    if (index < rom_count) {
      char name[22];
      SetupCopyDisplayText(roms[index].filename, name, sizeof(name));
      int text_width = SetupTextWidth(name, 2);
      int text_x = list_left + (list_width - text_width) / 2;
      if (text_x < list_left + 28)
        text_x = list_left + 28;
      if (index == selected) {
        SetupDrawMaskedRgb565Image(GFX_TOP, fairy_x, y - 2,
                                   kSetupFairyWidth, kSetupFairyHeight,
                                   kSetupFairyRgb565, kSetupFairyMask);
      }
      SetupDrawText(GFX_TOP, name, text_x + 1, y + 1, 2, shadow, 180);
      SetupDrawText(GFX_TOP, name, text_x, y, 2, white, 180);
    }
  }

  SetupDrawCentered(GFX_BOTTOM, "PRESS A TO SELECT", 74, 2, white);
  SetupDrawCentered(GFX_BOTTOM, "PRESS START TO QUIT", 108, 2, white);
  if (status && status[0]) {
    char status_text[28];
    SetupCopyDisplayText(status, status_text, sizeof(status_text));
    uint16_t status_color = pale;
    if (strcasecmp(status, "Incompatible ROM") == 0)
      status_color = ((osGetTime() / 300) & 1) ? red : white;
    SetupDrawCentered(GFX_BOTTOM, status_text, 166, 1, status_color);
  }
}

typedef struct RomSelectorContext {
  const RomEntry *roms;
  int rom_count;
  int selected;
  const char *status;
} RomSelectorContext;

static void DrawRomSelectorFrame(void *context) {
  RomSelectorContext *selector = (RomSelectorContext *)context;
  DrawRomSelector(selector->roms, selector->rom_count,
                  selector->selected, selector->status);
}

static void PresentRomSelector(const RomEntry *roms, int rom_count,
                               int selected, const char *status) {
  RomSelectorContext context = { roms, rom_count, selected, status };
  PresentSetupStable(DrawRomSelectorFrame, &context);
}

static int SelectRom(RomEntry *roms, int rom_count, const char *status) {
  if (rom_count <= 0) {
    ShowFatalSetupError(
      "No .sfc or .smc ROMs found in\n"
      "sdmc:/3ds/Zelda 3DS/");
    return -1;
  }
  int selected = 0;
  int last_presented = -1;
  uint64_t last_blink_step = UINT64_MAX;
  bool blink_status = status && strcasecmp(status, "Incompatible ROM") == 0;
  memset(g_setup_top_pixels, 0, sizeof(g_setup_top_pixels));
  memset(g_setup_bottom_pixels, 0, sizeof(g_setup_bottom_pixels));
  PresentSetupConsole();
  PresentSetupConsole();
  for (int i = 0; i < 150 && aptMainLoop(); i++)
    gspWaitForVBlank();
  SetupAudioStart();
  while (aptMainLoop()) {
    uint64_t blink_step = blink_status ? osGetTime() / 300 : 0;
    if (selected != last_presented ||
        (blink_status && blink_step != last_blink_step)) {
      PresentRomSelector(roms, rom_count, selected, status);
      last_presented = selected;
      last_blink_step = blink_step;
    }
    hidScanInput();
    u32 held = hidKeysHeld();
    u32 down = hidKeysDown();
    static bool dump_combo_was_held;
    bool dump_combo =
      (held & (KEY_L | KEY_R | KEY_A)) == (KEY_L | KEY_R | KEY_A);
    if (dump_combo && !dump_combo_was_held)
      CreateSetupDump("rom-selector");
    dump_combo_was_held = dump_combo;
    if ((down & KEY_DUP) && selected > 0) {
      selected--;
      SetupAudioPlayMove();
    }
    if ((down & KEY_DDOWN) && selected + 1 < rom_count) {
      selected++;
      SetupAudioPlayMove();
    }
    if (down & KEY_A)
      return selected;
    if (down & (KEY_B | KEY_START))
      return -1;
    gspWaitForVBlank();
  }
  return -1;
}

static bool EnsureProfileReady(RomEntry *rom, bool force_extract) {
  if (!EnsureDirectory(kProfilesDirectory) || !EnsureDirectory(rom->profile))
    return false;
  char cwd[512];
  if (!getcwd(cwd, sizeof(cwd)))
    return false;
  if (chdir(rom->profile) != 0)
    return false;
  CopyFileIfMissing(kBundledConfig, "zelda3.ini");
  bool ready = !force_extract && AssetsFileLooksValid(kAssetsFilename);
  if (!ready) {
    char rom_path[640];
    snprintf(rom_path, sizeof(rom_path), "../../%s", rom->filename);
    ready = ExtractAssetsFromRom(rom_path);
  }
  chdir(cwd);
  if (ready) {
    char profile_assets[512];
    char profile_ini[512];
    snprintf(profile_assets, sizeof(profile_assets), "%s/%s",
             rom->profile, kAssetsFilename);
    snprintf(profile_ini, sizeof(profile_ini), "%s/zelda3.ini", rom->profile);
    ready = CopyFileReplacing(profile_assets, kAssetsFilename) &&
            CopyFileReplacing(profile_ini, "zelda3.ini") &&
            PrepareSaveDirectory(rom, false);
    WriteSelectedRom(rom);
  }
  return ready;
}

static bool ResolveActiveProfile(char *profile, size_t profile_size) {
  bool force_selector = IsRegularFile(kForceSelectorFile);
  if (force_selector)
    remove(kForceSelectorFile);

  RomEntry *roms = g_rom_entries;
  int rom_count = ScanRoms(roms, countof(g_rom_entries));
  bool always_show_selector = force_selector || rom_count > 1;
  bool selected_file_exists = IsRegularFile(kSelectedRomFile);
  SelectedRomInfo selected_info;
  RomEntry selected_rom;
  if (!always_show_selector &&
      ReadSelectedRomInfo(&selected_info) &&
      FindSelectedRomEntry(&selected_info, roms, rom_count, &selected_rom) &&
      ProfileAssetsValid(selected_rom.profile)) {
    char profile_assets[512];
    char profile_ini[512];
    snprintf(profile_assets, sizeof(profile_assets), "%s/%s",
             selected_rom.profile, kAssetsFilename);
    snprintf(profile_ini, sizeof(profile_ini), "%s/zelda3.ini",
             selected_rom.profile);
    if (!CopyFileReplacing(profile_assets, kAssetsFilename) ||
        !CopyFileReplacing(profile_ini, "zelda3.ini"))
      return false;
    snprintf(profile, profile_size, "%s", selected_rom.profile);
    return PrepareSaveDirectory(&selected_rom, false);
  }

  int legacy_choice = -1;
  if (!always_show_selector && !selected_file_exists &&
      AssetsFileLooksValid(kAssetsFilename)) {
    legacy_choice = rom_count == 1 ? 0 : SelectRom(roms, rom_count, NULL);
    if (legacy_choice < 0)
      return false;
    if (RomUsesBundledAssetsPatch(&roms[legacy_choice])) {
      if (!MigrateLegacyStorage(&roms[legacy_choice]))
        return false;
      snprintf(profile, profile_size, "%s", roms[legacy_choice].profile);
      return ProfileAssetsValid(profile) &&
             PrepareSaveDirectoryForProfile(profile);
    }
    if (EnsureProfileReady(&roms[legacy_choice], true)) {
      snprintf(profile, profile_size, "%s", roms[legacy_choice].profile);
      return true;
    }
    SelectRom(roms, rom_count, "Incompatible ROM");
    return false;
  }

  const char *status = NULL;
  while (aptMainLoop()) {
    int choice = rom_count == 1 && !status ? 0 : SelectRom(roms, rom_count, status);
    if (choice < 0)
      return false;
    PresentRomSelector(roms, rom_count, choice, "Preparing selected ROM...");
    if (EnsureProfileReady(&roms[choice], false)) {
      snprintf(profile, profile_size, "%s", roms[choice].profile);
      return true;
    }
    status = "Incompatible ROM";
    if (rom_count <= 1)
      SelectRom(roms, rom_count, status);
  }
  return false;
}

bool Platform3DS_PrepareStorage(void) {
  mkdir("sdmc:/3ds", 0777);
  if (mkdir(kStorageDirectory, 0777) != 0 && errno != EEXIST)
    return false;
  if (chdir(kStorageDirectory) != 0)
    return false;

  remove("setup-progress.txt");
  LogSetup("Zelda 3DS v%s setup started", ZELDA3_3DS_VERSION);
  char profile[512];
  BeginSetupConsole();
  bool profile_ready = ResolveActiveProfile(profile, sizeof(profile));
  EndSetupConsole();
  if (!profile_ready)
    return false;
  remove("runtime.log");
  Platform3DS_LogRuntime("Zelda 3DS v%s runtime started", ZELDA3_3DS_VERSION);
  Platform3DS_LogRuntime("Active ROM profile: %s", profile);
  Platform3DS_DetectModel();
  CopyFileIfMissing(kBundledConfig, "zelda3.ini");
  Platform3DS_LoadRuntimeSettings();
  if (!AssetsFileLooksValid(kAssetsFilename)) {
    Platform3DS_LogRuntime("ERROR active profile assets missing/invalid");
    return false;
  }
  Platform3DS_LogRuntime("Assets file header validated");

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
  config->extend_y = false;
  config->extended_aspect_ratio =
    g_display_mode == kPlatform3DSDisplayUltraWideMod ? 72 : 0;
  config->features0 &= ~(kFeatures0_ExtendScreen64 |
                         kFeatures0_WidescreenVisualFixes);
  if (g_display_mode == kPlatform3DSDisplayUltraWideMod) {
    config->features0 |= kFeatures0_ExtendScreen64 |
                         kFeatures0_WidescreenVisualFixes;
  }
  config->audio_freq = 32000;
  config->audio_channels = 2;
  config->audio_samples = 1024;
  config->enable_msu = 0;
  config->disable_frame_delay = true;
  Platform3DS_LogRuntime("Runtime settings: display=%d, wide_edge=%d, turbo=%d",
                         (int)g_display_mode,
                         (int)g_wide_edge_mode,
                         g_turbo_multiplier);
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

bool Platform3DS_SaveRGB565Bmp(const char *path, const uint8_t *pixels,
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
    const uint16_t *source =
      (const uint16_t *)(pixels + (size_t)y * pitch);
    for (int x = 0; x < width; x++) {
      uint16_t color = source[x];
      row[x * 3 + 0] = (uint8_t)((color & 31) * 255 / 31);
      row[x * 3 + 1] = (uint8_t)(((color >> 5) & 63) * 255 / 63);
      row[x * 3 + 2] = (uint8_t)(((color >> 11) & 31) * 255 / 31);
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

  char load_state_path[192];
  snprintf(load_state_path, sizeof(load_state_path), "%s/%s", directory,
           ZELDA_DUMP_LOAD_STATE_FILENAME);
  bool load_state_ok = IsRegularFile(load_state_path);

  snprintf(path, sizeof(path), "%s/info.txt", directory);
  FILE *info = fopen(path, "wb");
  if (info) {
    fprintf(info, "Zelda 3DS v%s memory dump\n", ZELDA3_3DS_VERSION);
    fprintf(info, "RAM bytes: %lu\n", (unsigned long)ram_size);
    fprintf(info, "SRAM bytes: %lu\n", (unsigned long)sram_size);
    fprintf(info, "VRAM words: %lu\n", (unsigned long)vram_words);
    fprintf(info, "Load State checkpoint: %s\n",
            load_state_ok ? "load-state.bin (validated)" : "unavailable");
    fprintf(info, "Display mode: %d\n", (int)g_display_mode);
    fprintf(info, "Top presenter: PICA200 RGB565\n");
    fprintf(info, "Frame pacing: 60 Hz high-resolution timer\n");
    fprintf(info, "New 3DS speedup requested: %s\n",
            g_is_new_3ds ? "yes" : "no");
    fprintf(info, "Bottom pixel path: %s\n",
            g_is_new_3ds ? "ARGB8888" : "RGB565");
    fprintf(info, "Bottom periodic cadence: %d FPS\n",
            g_is_new_3ds ? 30 : 10);
    if (Platform3DS_CanUseCore1PpuWorker())
      fprintf(info, "Core 1 PPU budget: %d%%\n",
              g_core1_time_limit_percent);
    else
      fprintf(info, "Core 1 PPU budget: unavailable\n");
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
    if (!g_is_new_3ds) {
      extern void SecondScreenSDL_GetOld3DSWorkerStats(
        uint64_t *, uint32_t *, uint32_t *,
        uint64_t *, uint32_t *, uint32_t *,
        uint64_t *, uint32_t *, uint32_t *);
      uint64_t full_count = 0, patch_count = 0, touch_count = 0;
      uint32_t full_average_us = 0, full_max_us = 0;
      uint32_t patch_average_us = 0, patch_max_us = 0;
      uint32_t touch_average_us = 0, touch_max_us = 0;
      SecondScreenSDL_GetOld3DSWorkerStats(
        &full_count, &full_average_us, &full_max_us,
        &patch_count, &patch_average_us, &patch_max_us,
        &touch_count, &touch_average_us, &touch_max_us);
      fprintf(info, "Bottom full redraws: %llu\n",
              (unsigned long long)full_count);
      fprintf(info, "Average bottom full redraw: %lu us\n",
              (unsigned long)full_average_us);
      fprintf(info, "Maximum bottom full redraw: %lu us\n",
              (unsigned long)full_max_us);
      fprintf(info, "Bottom HUD patch redraws: %llu\n",
              (unsigned long long)patch_count);
      fprintf(info, "Average bottom HUD patch: %lu us\n",
              (unsigned long)patch_average_us);
      fprintf(info, "Maximum bottom HUD patch: %lu us\n",
              (unsigned long)patch_max_us);
      fprintf(info, "Measured touch redraws: %llu\n",
              (unsigned long long)touch_count);
      fprintf(info, "Average touch-to-render: %lu us\n",
              (unsigned long)touch_average_us);
      fprintf(info, "Maximum touch-to-render: %lu us\n",
              (unsigned long)touch_max_us);
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
