#include "rando/rando_generate.h"
#include "rando/rando_save.h"
#include "rando/alttpr_import.h"
#include "rando/seed_identity.h"
#include "file_store.h"
#include "ini_edit.h"
#include "platform_3ds.h"
#include "updater.h"
#include "update_view.h"
extern bool SS_RenderLetterSheet(uint32_t *pixels);
extern bool SS_RenderOriginalLinkFace(uint32_t *pixels);
extern bool SS_RenderGlyphSheet(uint32_t *pixels);
static bool g_update_fonts_ready,g_update_view_valid,g_qol_view_valid;
static uint32_t *g_update_pixels;
#include "ppu_gpu.h"
#include "present_image.h"
#include "startup_card.h"
#include "developer_tests.h"
#include "file_menu_present.h"

#include <3ds.h>
#include <citro2d.h>

// Generation and paired sidecar validation exceed libctru's 32 KiB default.
// All randomizer work runs on the main thread, never the small SDL workers.
u32 __stacksize__ = 512 * 1024;
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
#include "mods.h"
#include "assets_fr_c.h"
#include "config.h"
#include "quality_of_life.h"
#include "setup_audio_assets.h"
#include "features.h"
#include "setup_selector_assets.h"
#include "setup_pattern.h"
#include "generated_operation_plaque.h"
#include "ui_strings.h"
#include "native_title_credit.h"
#include "native_list_layout.h"
#include "ui_glyphs_intl.h"
#include "platform/linux/ss_textures.h"
#include "platform/linux/ui_font.h"
#include "platform/linux/native_menu_button.h"
#include "game_languages.h"
#include "types.h"
#include "second_screen_tables.h"
#include "util.h"
#include "zelda_rtl.h"
#include "localized.h"
#include "language_cart.h"
#include "snes/ppu.h"

extern void SecondScreenSDL_OpenDeveloperOverlay(void);

static const char kStorageDirectory[] = "sdmc:/3ds/Zelda 3DS";
static const char kProfilesDirectory[] = "profiles";
static const char kSelectedRomFile[] = "selected_rom.ini";
static const char kForceSelectorFile[] = "select-rom.flag";
static const char kAssetsFilename[] = "zelda3_assets.dat";
static const char kTemporaryAssetsFilename[] = "zelda3_assets.tmp";
static const char kBundledPatch[] = "romfs:/zelda3_assets.bps";
// Builds the assets from the French Canadian cartridge alone (assets_fr_c.h).
static const char kBundledPatchFrC[] = "romfs:/zelda3_assets_fr-c.bps";
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
static bool g_autosave = true;
static int g_turbo_control = kTurboControlAuto;
static bool g_reset_requested;

static bool g_menu_language_seen;
static unsigned g_current_fps;
static uint64_t g_dump_saved_overlay_until_ms;
static bool g_quick_dump_requested;
static bool g_rom_selection_requested;
static aptHookCookie g_apt_hook_cookie;
static bool g_apt_hook_registered;
static volatile bool g_system_exit_requested;
static volatile bool g_system_suspended;
static char g_active_save_directory[512] = "saves";
static char g_active_rom_path[640];
static char g_randomizer_seed_key[65];
static char g_randomizer_save_prefix[96];
static bool g_randomizer_menu_requested;
static int g_pending_randomizer_kind; // 0 none, 1 generate, 2 resume, 3 ordinary, 4 import
static AlttprSeed g_pending_imported_seed;
static char g_pending_imported_path[192];
static Platform3DSRandomizerSeed g_pending_randomizer_seed;
static char g_randomizer_error[256];
static RandoErrorCode g_randomizer_error_code;

// "quest-<mod>/" while a QUEST mod is on: its saves stay apart from the
// game's (BuildActiveModList).
static char g_quest_save_prefix[128];
static uint32_t g_active_profile_id;
static char g_active_profile[512];
static bool g_is_new_3ds;
const Platform3DSHardwareProfile *Platform3DS_GetHardwareProfile(void) {
  return Platform3DS_ProfileForModel(g_is_new_3ds);
}
static bool g_model_detected;
static bool g_irrst_initialized;
static bool g_core1_time_enabled;
static int g_core1_time_limit_percent;
// Last 120 presented-frame samples: scene-local evidence, without old menu,
// loading or diagnostic-I/O outliers dominating a session-wide average.
enum { kRecentFrameCount = 120 };
typedef struct RecentFrameTiming {
  uint32_t ppu, work, interval;
  uint32_t logic, present, bottom, scheduled, executed;
  uint32_t ppu_main, ppu_worker, ppu_join, split;
  uint32_t gpu_begin, top_transfer, gpu_end;
} RecentFrameTiming;
static uint32_t g_last_gpu_begin_us, g_last_top_transfer_us, g_last_gpu_end_us;
static RecentFrameTiming g_recent_frames[kRecentFrameCount];
static uint32_t g_recent_count, g_recent_next, g_recent_over_budget;
static uint64_t g_recent_ppu_us, g_recent_work_us, g_recent_interval_us;

static void RecordRecentFrame(uint32_t ppu, uint32_t work, uint32_t interval,
                              uint32_t logic, uint32_t present, uint32_t bottom,
                              int scheduled, int executed) {
  if (interval == 0) return;
  RecentFrameTiming *old = &g_recent_frames[g_recent_next];
  g_recent_ppu_us -= old->ppu;
  g_recent_work_us -= old->work;
  g_recent_interval_us -= old->interval;
  g_recent_over_budget -= old->work > 16667;
  *old = (RecentFrameTiming){.ppu = ppu, .work = work, .interval = interval,
    .logic = logic, .present = present, .bottom = bottom,
    .scheduled = scheduled, .executed = executed,
    .gpu_begin = g_last_gpu_begin_us, .top_transfer = g_last_top_transfer_us,
    .gpu_end = g_last_gpu_end_us};
  int split = 0;
  ZeldaGetPpuWorkerStats(&split, &old->ppu_main, &old->ppu_worker);
  old->split = split;
  old->ppu_join = ZeldaGetPpuJoinTimeUs();
  g_recent_ppu_us += ppu;
  g_recent_work_us += work;
  g_recent_interval_us += interval;
  g_recent_over_budget += work > 16667;
  if (g_recent_count < kRecentFrameCount) g_recent_count++;
  g_recent_next = (g_recent_next + 1) % kRecentFrameCount;
}

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
static bool g_ignore_next_frame_timing;
static bool g_dump_audio_pause_active;
static bool g_dump_audio_was_paused;
static bool g_gpu_presenter_initialized;
static bool g_gpu_frame_active;
static bool g_setup_console_active;
static C3D_RenderTarget *g_top_target;
static C3D_RenderTarget *g_bottom_target;
static C3D_Tex g_top_texture;
static const uint8_t *g_last_top_source;
static int g_last_top_source_pitch, g_last_top_source_width, g_last_top_source_height;
static C3D_Tex g_bottom_texture;
static bool g_bottom_fade_ready, g_bottom_fade_active, g_bottom_cinema, g_bottom_image_ready;
static uint64_t g_bottom_fade_start_ms;
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
enum { kSetupIntroBlocks=8, kSetupIntroSamples=534 };
static int16_t *g_setup_intro_buffer;
static ndspWaveBuf g_setup_intro_wavebuf[kSetupIntroBlocks];
static unsigned g_setup_intro_next;

enum {
  kTopTextureWidth = 512,
  kTopTextureHeight = 256,
  kC2DMaxObjects = 512,
  kC2DFlushWindowSize = 128 * 1024,
};

extern u32 __ctru_linear_heap;
extern u32 __ctru_linear_heap_size;

static bool WriteBlob(const char *path, const void *data, size_t size);
static bool EnsureDirectory(const char *path);
static bool CopyFileReplacing(const char *source, const char *destination);
static void MakeTimestamp(char *stamp, size_t stamp_size);
static bool RomFileShouldBeIgnored(const char *name);
static uint32 ReadU32LE(const uint8 *data);
static const char *RomModLanguage(const char *path);

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
  uint64_t end_start = svcGetSystemTick();
  C3D_FrameEnd(clean ? GX_CMDLIST_FLUSH : 0);
  g_last_gpu_end_us = (uint32_t)((svcGetSystemTick() - end_start) * 1000000ull / SYSCLOCK_ARM11);
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
    g_display_mode = kPlatform3DSDisplayUltraWideMod;
  if (g_wide_edge_mode_auto)
    g_wide_edge_mode = kPlatform3DSWideEdgeFixedCamera;
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

// libctru gfxInit unmasks the LCD immediately after allocating uninitialized
// buffers. Keep it black across selector teardown, SDL format changes and
// renderer probes; reveal only initialized setup/game frames.
static bool g_startup_lcd_black = true;
Result __real_GSPGPU_SetLcdForceBlack(u8 flags);
Result __wrap_GSPGPU_SetLcdForceBlack(u8 flags) {
  if (!flags && g_startup_lcd_black) return 0;
  return __real_GSPGPU_SetLcdForceBlack(flags);
}
static void RevealInitializedScreens(void) {
  gspWaitForVBlank();
  g_startup_lcd_black = false;
  GSPGPU_SetLcdForceBlack(0);
}

uint16_t SecondScreenSDL_FilterButtons(uint16_t buttons);
void SS_RequestNextTestScenario(void);
int SS_GetModule(void);
uint32_t SS_GetDeathOverlay(void);

uint16_t Platform3DS_ReadInput(bool *turbo_held, int *turbo_multiplier) {
  hidScanInput();
  u32 keys = hidKeysHeld();
  static uint64_t old_3ds_x_hold_start_ms;
  static bool old_3ds_x_was_held;
  static bool old_3ds_x_turbo_was_active;
  bool old_3ds_x_turbo = false;
  bool old_3ds_x_tap = false;
  bool delayed_x = Platform3DS_GetTurboControl() == kTurboControlX && g_turbo_multiplier > 0;
  if (delayed_x && (keys & KEY_X)) {
    uint64_t now_ms = osGetTime();
    if (!old_3ds_x_was_held)
      old_3ds_x_hold_start_ms = now_ms;
    old_3ds_x_was_held = true;
    old_3ds_x_turbo = now_ms - old_3ds_x_hold_start_ms >= 1000;
    if (old_3ds_x_turbo)
      old_3ds_x_turbo_was_active = true;
  } else {
    // SDL pumps HID before us, so a second scan can erase hidKeysUp().
    // Track our own held transition; ReadInput runs only before logic steps.
    if (delayed_x && old_3ds_x_was_held &&
        !old_3ds_x_turbo_was_active &&
        osGetTime() - old_3ds_x_hold_start_ms < 1000)
      old_3ds_x_tap = true;
    old_3ds_x_was_held = false;
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

  const u32 reset_combo = KEY_L | KEY_R | KEY_START | KEY_SELECT;
  static uint64_t reset_hold_start;
  static bool reset_holding;
  static bool reset_triggered;
  if ((keys & reset_combo) == reset_combo) {
    if (!reset_holding) { reset_holding = true; reset_hold_start = osGetTime(); }
    if (!reset_triggered && osGetTime() - reset_hold_start >= 1000) {
      reset_triggered = true;
      Platform3DS_RequestReset();
    }
    *turbo_held = false; *turbo_multiplier = g_turbo_multiplier > 0 ? g_turbo_multiplier : 1;
    return 0;
  }
  reset_holding = false;
  reset_triggered = false;

#if ZELDA3_ENABLE_DEVELOPER_TESTS
  static bool test_l_was_held;
  bool test_combo = (keys & (KEY_B | KEY_L)) == (KEY_B | KEY_L) &&
    !(keys & (KEY_R | KEY_A | KEY_START | KEY_SELECT));
  if (test_combo && !test_l_was_held) SS_RequestNextTestScenario();
  test_l_was_held = (keys & KEY_L) != 0;
  if (test_combo) keys &= ~(KEY_B | KEY_L);
#endif

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
  if (delayed_x ? old_3ds_x_tap : (keys & KEY_X))
    input |= 1u << 9;
  if (keys & KEY_Y) input |= 1u << 1;
  if ((keys & KEY_L) && !version_combo) input |= 1u << 10;
  if ((keys & KEY_R) && !version_combo) input |= 1u << 11;
  // The developer menu's test list takes the buttons while it is open.
  input = SecondScreenSDL_FilterButtons(input);
  int control = Platform3DS_GetTurboControl();
  *turbo_held = g_turbo_multiplier > 0 &&
    (control == kTurboControlX ? old_3ds_x_turbo :
     control == kTurboControlZL ? (keys & KEY_ZL) != 0 : CStickIsHeld(keys));
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
  } else if (strcasecmp(key, "Autosave") == 0) {
    g_autosave = strtol(value, NULL, 10) != 0;
  } else if (strcasecmp(key, "TurboControl") == 0) {
    g_turbo_control = strcasecmp(value, "X") == 0 ? kTurboControlX :
      strcasecmp(value, "ZL") == 0 ? kTurboControlZL :
      strcasecmp(value, "CStick") == 0 ? kTurboControlCStick : kTurboControlAuto;
  } else if (strcasecmp(key, "MenuLanguage") == 0) {
    UI_SetLanguage(UI_LanguageFromCode(value));
    g_menu_language_seen = true;
  }
}

// Without a MenuLanguage line the interface follows the console. Only French
// has a translation; everything else stays on English.
static int MenuLanguageFromSystem(void) {
  u8 language = CFG_LANGUAGE_EN;
  bool ok = false;
  if (R_SUCCEEDED(cfguInit())) {
    ok = R_SUCCEEDED(CFGU_GetSystemLanguage(&language));
    cfguExit();
  }
  if (!ok)
    return kUiLanguage_English;
  switch (language) {
  case CFG_LANGUAGE_FR: return kUiLanguage_French;
  case CFG_LANGUAGE_ES: return kUiLanguage_Spanish;
  case CFG_LANGUAGE_PT: return kUiLanguage_Portuguese;
  case CFG_LANGUAGE_IT: return kUiLanguage_Italian;
  case CFG_LANGUAGE_DE: return kUiLanguage_German;
  default: return kUiLanguage_English;
  }
}

void Platform3DS_LoadRuntimeSettings(void) {
  g_menu_language_seen = false;
  g_autosave = true;
  g_turbo_control = kTurboControlAuto;
  g_display_mode_auto = true;
  g_wide_edge_mode_auto = true;
  g_display_mode_legacy_stretch = false;
  g_runtime_wide_edge_seen = false;
  g_show_fps = false;
  FILE *file = fopen("zelda3.ini", "rb");
  if (!file) {
    UI_SetLanguage(MenuLanguageFromSystem());
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
  if (!g_menu_language_seen)
    UI_SetLanguage(MenuLanguageFromSystem());
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

// Reset is independent of language/ROM selection. The active profile and
// regular SRAM remain in place; only automatic slot 0 is invalidated.
void Platform3DS_RequestReset(void) {
  g_reset_requested = true;
  g_system_exit_requested = true;
  Platform3DS_LogRuntime("Fresh game reset requested");
}
bool Platform3DS_ResetRequested(void) { return g_reset_requested; }
bool Platform3DS_TakeResetRequest(void) {
  bool requested = g_reset_requested;
  g_reset_requested = false;
  if (requested) g_system_exit_requested = false;
  return requested;
}
bool Platform3DS_GetAutosave(void) { return g_autosave; }
void Platform3DS_SetAutosave(bool enabled) {
  g_autosave = enabled;
  g_config.autosave = enabled;
}
int Platform3DS_GetTurboControl(void) {
  if (!g_is_new_3ds) return kTurboControlX;
  return g_turbo_control == kTurboControlAuto ? kTurboControlCStick : g_turbo_control;
}
void Platform3DS_SetTurboControl(int control) {
  if (control < kTurboControlX || control > kTurboControlCStick) control = kTurboControlAuto;
  g_turbo_control = control;
}

void Platform3DS_RequestRomSelection(void) {
  // Choosing a game language leaves the English-only randomizer session.
  // Keep its save namespace until shutdown; switch ordinary storage on reboot.
  if (g_randomizer_seed_key[0]) {
    memset(&g_pending_randomizer_seed, 0, sizeof(g_pending_randomizer_seed));
    g_pending_randomizer_kind = 3;
  }
  Platform3DS_BlankScreens();
  FILE *file = fopen("sdmc:/3ds/Zelda 3DS/select-rom.flag", "wb");
  if (file) {
    fputs("1\n", file);
    fclose(file);
  }
  g_rom_selection_requested = true;
  g_system_exit_requested = true;
  Platform3DS_LogRuntime("Game language selector requested from settings");
}

bool Platform3DS_RomSelectionRequested(void) { return g_rom_selection_requested; }

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
    Platform3DS_ClearBlackTarget(g_top_target);
    C2D_SceneBegin(g_top_target);
    Platform3DS_ClearBlackTarget(g_bottom_target);
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
  // Keep this non-blocking. E4 drew a single frame and then slept the game
  // thread for 600 ms, which both inflated dump timing outliers and made the
  // notice depend on one successful Citro2D batch. A deadline lets normal
  // frames keep flowing while the confirmation remains visible.
  g_dump_saved_overlay_until_ms = osGetTime() + 1200;
}

void Platform3DS_SetAudioPausedForDump(bool paused) {
  // SDL's pause flag stops producing new samples, but the N3DS audio backend
  // keeps several NDSP wave buffers queued. Pausing channel 0 freezes those
  // already-queued samples immediately and resumes at the same position.
  if (paused) {
    if (!g_dump_audio_pause_active) {
      g_dump_audio_was_paused = ndspChnIsPaused(0);
      g_dump_audio_pause_active = true;
    }
    ndspChnSetPaused(0, true);
  } else if (g_dump_audio_pause_active) {
    ndspChnSetPaused(0, g_dump_audio_was_paused);
    g_dump_audio_pause_active = false;
  }
}

void Platform3DS_MarkDumpTimingDiscontinuity(void) {
  // The synchronous SD transaction is deliberately outside normal gameplay
  // timing. Ignore the frame that contains it so the next dump does not
  // report this diagnostic pause as a PPU or presentation regression.
  g_ignore_next_frame_timing = true;
}

uint32_t Platform3DS_GetActiveProfileId(void) {
  return g_active_profile_id;
}

// A file the active profile keeps in its own folder for the game: the French
// Canadian cartridge (localized.c).
void Platform3DS_FormatProfilePath(const char *filename,
                                   char *out, size_t out_size) {
  if (!filename || !out || out_size == 0)
    return;
  if (g_active_profile[0])
    snprintf(out, out_size, "%s/%s", g_active_profile, filename);
  else
    snprintf(out, out_size, "%s", filename);
}

bool Platform3DS_InitTopPresenter(void) {
  Platform3DS_RegisterAptHook();
  Platform3DS_DetectModel();
  extern int Platform3DS_GetAptEventPriority(void);
  Platform3DS_LogRuntime("APT notification thread priority: 0x%x", Platform3DS_GetAptEventPriority());
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

  gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
  gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
  gfxSetDoubleBuffering(GFX_TOP, true);
  gfxSetDoubleBuffering(GFX_BOTTOM, true);

  if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
    Platform3DS_LogRuntime("ERROR: unable to initialize Citro2D presenter");
    return false;
  }
  // Wide file menus use up to 240 quads. The 5x7 status font adds one quad
  // per horizontal glyph run: reserve room for FPS, DUMP SAVED, their panels
  // and the bottom screen in the same frame without dropping geometry.
  if (!C2D_Init(kC2DMaxObjects)) {
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
  g_bottom_fade_ready = g_bottom_fade_active = false;
  g_bottom_cinema = g_bottom_image_ready = false;

  memset(g_recent_frames, 0, sizeof(g_recent_frames));
  g_recent_count = g_recent_next = g_recent_over_budget = 0;
  g_recent_ppu_us = g_recent_work_us = g_recent_interval_us = 0;
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
  if (!g_is_new_3ds) PpuGpuInit();
  Platform3DS_BlankScreens();
  return gfxGetScreenFormat(GFX_TOP) == GSP_RGB565_OES;
}

void Platform3DS_ShutdownTopPresenter(void) {
  linearFree(g_update_pixels); g_update_pixels = NULL;
  g_update_fonts_ready = g_update_view_valid = false;
  g_last_top_source = NULL;
  if (!g_gpu_presenter_initialized)
    return;
  Platform3DS_EndFrame();
  if (!Platform3DS_IsSystemClosing())
    C3D_FrameSync();
  PpuGpuShutdown();
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

static float StatusTextWidth(const char *text, float scale) {
  size_t length = strlen(text);
  return length ? ((float)length * 6.0f - 1.0f) * scale : 0.0f;
}

static void DrawQolListTop(int language,int selected,int first);
static void DrawGameplayQolTop(int language,int selected);
static void PresentMenuPixels(void) {
  if (!C3D_FrameBegin(0)) return;
  g_gpu_frame_active = true;
  Platform3DS_CleanDataCache(g_update_pixels, 512*256*4);
  Platform3DS_QueueDisplayTransfer(g_update_pixels, GX_BUFFER_DIM(512,256),
    g_top_texture.data, GX_BUFFER_DIM(512,256), GX_TRANSFER_OUT_TILED(1) |
    GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8));
  g_top_subtexture = (Tex3DS_SubTexture){.width=400,.height=240,.left=0,.right=400.0f/512,.top=1,.bottom=1-240.0f/256};
  C2D_Image image = {.tex=&g_top_texture,.subtex=&g_top_subtexture};
  C2D_DrawParams params = {.pos={.x=0,.y=0,.w=400,.h=240},.depth=0};
  Platform3DS_ClearBlackTarget(g_top_target); C2D_SceneBegin(g_top_target);
  Platform3DS_DrawMappedImage(image, &params, ConfigureArgbTextureEnv);
}
void Platform3DS_PresentQolPage(int selected) {
  if(!g_gpu_presenter_initialized)return;
  if(!g_update_pixels){g_update_pixels=linearMemAlign(512*256*4,64);g_qol_view_valid=false;}
  if(!g_update_pixels)return;
  int language=0;for(int i=0;i<kGameLanguageCount;i++)if(kGameLanguages[i].menu==UI_GetLanguage())language=i;
  unsigned mask=0;for(int i=0;i<kQolCount;i++)if(Qol_Selected(i))mask|=1u<<i;
  static int last_selected,last_language;static unsigned last_mask;
  if(!g_qol_view_valid||last_selected!=selected||last_language!=language||last_mask!=mask) {
  DrawGameplayQolTop(language,selected);
  for(int y=0;y<240;y++)for(int x=0;x<400;x++) {
    unsigned c=g_setup_top_pixels[y*400+x];
    g_update_pixels[y*512+x]=0xff000000|((c>>11)*255/31)<<16|(((c>>5)&63)*255/63)<<8|(c&31)*255/31;
  }
  last_selected=selected;last_language=language;last_mask=mask;g_qol_view_valid=true;
  }
  PresentMenuPixels();
  g_update_view_valid=false;
}

static unsigned g_update_note_pages = 1;
unsigned Platform3DS_UpdateNotesPages(void) { return g_update_note_pages; }
void Platform3DS_PresentUpdatePage(bool show_notes, unsigned page) {
  g_qol_view_valid=false;
  if (!g_gpu_presenter_initialized) return;
  if (!g_update_pixels) g_update_pixels = linearMemAlign(512*256*4, 64);
  if (!g_update_pixels) return;
  static uint32_t letters[128*16], glyphs[kGlyphCols*8*((kGlyphCount+kGlyphCols-1)/kGlyphCols)*8];
  if (!g_update_fonts_ready) {
    if (!SS_RenderLetterSheet(letters) || !SS_RenderGlyphSheet(glyphs)) return;
    g_update_fonts_ready = true;
  }
  static char notes[12289], lines[384][43];
  UpdateStatus state; Updater_GetStatus(&state);
  static unsigned last_revision, last_page;
  static bool last_show_notes;
  static int last_language=-1;
  if (!g_update_view_valid || last_revision != state.revision ||
      last_page != page || last_show_notes != show_notes || last_language != UI_GetLanguage()) {
    unsigned count;
    if (show_notes && state.version[0]) {
      Updater_GetNotes(notes, sizeof(notes));
      count = Update_FormatNotes(notes, lines, 384);
    } else {
      snprintf(lines[0], sizeof(lines[0]), "%s", UI_Text(kUi_PickReleaseLine1));
      snprintf(lines[1], sizeof(lines[1]), "%s", UI_Text(kUi_PickReleaseLine2));
      count = 2;
    }
    g_update_note_pages = (count + 13) / 14;
    if (page >= g_update_note_pages) page = g_update_note_pages - 1;
    UpdateView_Draw(g_update_pixels, letters, glyphs,
                    show_notes ? state.version : "", lines, count, page);
    last_revision = state.revision; last_page = page; last_show_notes = show_notes;
    last_language=UI_GetLanguage();
    g_update_view_valid = true;
  }
  PresentMenuPixels();
}

typedef struct FileMenuImage {
  C3D_Tex *texture;
  void (*configure)(void);
} FileMenuImage;

static bool DrawFileMenuRect(void *context, int sx, int sy,
                             int x, int y, int width, int height) {
  FileMenuImage *menu = context;
  Tex3DS_SubTexture sub = {
    .width = width, .height = height,
    .left = (float)sx / kTopTextureWidth,
    .right = (float)(sx + width) / kTopTextureWidth,
    .top = 1.0f - (float)sy / kTopTextureHeight,
    .bottom = 1.0f - (float)(sy + height) / kTopTextureHeight,
  };
  C2D_Image image = {.tex = menu->texture, .subtex = &sub};
  C2D_DrawParams params = {.pos = {.x=x, .y=y, .w=width, .h=height}};
  return Platform3DS_DrawMappedImage(image, &params, menu->configure);
}

static void DrawTitleCreditRect(void *context,int x,int y,int w,int h,uint32_t color) {
  (void)context;
  C2D_DrawRectSolid(x,y,0.6f,w,h,C2D_Color32((color>>16)&255,(color>>8)&255,color&255,255));
}
static uint32_t TitleCreditColor(uint16_t value,int brightness) {
  int r=(value&31)*255/31,g=((value>>5)&31)*255/31,b=((value>>10)&31)*255/31;
  return (r*brightness/15)<<16|(g*brightness/15)<<8|(b*brightness/15);
}

void Platform3DS_PresentTopFrame(const uint8_t *pixels, int pitch,
                                 int width, int height,
                                 int focus_x, int focus_y, unsigned module) {
  if (!g_gpu_presenter_initialized || !pixels ||
      pitch != kTopTextureWidth * (int)sizeof(uint32_t) ||
      width <= 0 || width > kTopTextureWidth ||
      height <= 0 || height > kTopTextureHeight)
    return;

  g_last_top_transfer_us = g_last_gpu_end_us = 0;
  uint64_t begin_start = svcGetSystemTick();
  bool began = C3D_FrameBegin(0);
  g_last_gpu_begin_us = (uint32_t)((svcGetSystemTick() - begin_start) * 1000000ull / SYSCLOCK_ARM11);
  if (!began) return;
  bool gpu_image = !g_is_new_3ds && PpuGpuOutputActive();
  g_gpu_frame_active = true;
  if (gpu_image) {
    if (PpuGpuPrepared() && !PpuGpuDraw()) {
      Platform3DS_LogRuntime("PICA200 submission failed; software resumes next frame");
      return;
    }
    g_last_top_source = NULL;
  } else {
  g_last_top_source = pixels;
  g_last_top_source_pitch = pitch;
  g_last_top_source_width = width;
  g_last_top_source_height = height;
  uint64_t transfer_start = svcGetSystemTick();
  g_gpu_frame_active = true;
  Platform3DS_CleanDataCache(
    pixels, kTopTextureWidth * kTopTextureHeight * sizeof(uint32_t));
  Platform3DS_QueueDisplayTransfer(
    (u32 *)pixels, GX_BUFFER_DIM(kTopTextureWidth, kTopTextureHeight),
    (u32 *)g_top_texture.data,
    GX_BUFFER_DIM(kTopTextureWidth, kTopTextureHeight),
    GX_TRANSFER_FLIP_VERT(0) |
      GX_TRANSFER_OUT_TILED(1) |
      GX_TRANSFER_RAW_COPY(0) |
      GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
      GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
      GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));

  g_last_top_transfer_us = (uint32_t)((svcGetSystemTick() - transfer_start) * 1000000ull / SYSCLOCK_ARM11);
  }
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
  const float draw_height = (stretch) ? (float)GSP_SCREEN_WIDTH :
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
    .tex = gpu_image ? (C3D_Tex*)PpuGpuOutput() : &g_top_texture,
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

  Platform3DS_ClearBlackTarget(g_top_target);
  C2D_SceneBegin(g_top_target);
  FileMenuImage menu = {image.tex, gpu_image ? ConfigureRgb565TextureEnv : ConfigureArgbTextureEnv};
  bool file_menu = !stretch && module >= 1 && module <= 4;
  bool menu_drawn = file_menu && wide &&
    FileMenu_DrawWide(width, height, module, FileMenu_TitleWidth(g_zenv.vram), DrawFileMenuRect, &menu);
  if (!menu_drawn && file_menu)
    menu_drawn = FileMenu_DrawBackground(width, height, DrawFileMenuRect, &menu);
  if (!menu_drawn)
    Platform3DS_DrawMappedImage(image, &params, menu.configure);
  if(NativeTitleCredit_Visible(module,g_zenv.ram[0x11])&&!g_zenv.ppu->forcedBlank) {
    int brightness=g_zenv.ppu->brightness;
    NativeTitleCredit_Draw(400,193+(height<240?(240-height)/2:0),
      TitleCreditColor(g_zenv.ppu->cgram[0xd9],brightness),
      TitleCreditColor(g_zenv.ppu->cgram[0xdb],brightness),DrawTitleCreditRect,NULL);
  }
  if (g_show_fps) {
    char label[28];
    snprintf(label, sizeof(label), "FPS %u", g_current_fps);
    float box_width = StatusTextWidth(label, 2.0f) + 10.0f;
    C2D_DrawRectSolid(5.0f, 216.0f, 0.7f, box_width, 20.0f,
                      C2D_Color32(0, 0, 0, 210));
    DrawStatusText(10.0f, 219.0f, 2.0f, label);
  }
  uint64_t now_ms = osGetTime();
  if (now_ms < g_dump_saved_overlay_until_ms) {
    static const char kDumpSavedText[] = "DUMP SAVED";
    float text_width = StatusTextWidth(kDumpSavedText, 2.0f);
    float box_width = text_width + 18.0f;
    float box_x = (400.0f - box_width) * 0.5f;
    C2D_DrawRectSolid(box_x, 12.0f, 0.7f, box_width, 24.0f,
                      C2D_Color32(0, 0, 0, 220));
    DrawStatusText(box_x + 9.0f, 17.0f, 2.0f, kDumpSavedText);
  } else {
    g_dump_saved_overlay_until_ms = 0;
  }
}

void Platform3DS_SetBottomCinema(bool cinema) {
  if(cinema==g_bottom_cinema)return;
  g_bottom_cinema=cinema;
  g_bottom_fade_ready=g_bottom_fade_active=false;
}

// Run on the compositor thread; cached Old 3DS cards need no worker redraw.
static void Platform3DS_ApplyBottomFade(void) {
  unsigned state=SS_GetModule(),phase=state>>8;
  if((state&255)==0x12) {
    uint32_t color=SS_GetDeathOverlay();
    if(color>>24)Platform3DS_DrawFadeRect(C2D_Color32((color>>16)&255,(color>>8)&255,color&255,color>>24));
    if(phase<=5||phase>=10)return;
    if(!g_bottom_cinema){Platform3DS_DrawFadeRect(C2D_Color32(0,0,0,255));return;}
  }
  if(!g_bottom_cinema)return;
  uint64_t now = osGetTime();
  if (!g_bottom_fade_ready) {
    g_bottom_fade_ready = g_bottom_fade_active = true;
    g_bottom_fade_start_ms = now;
  }
  uint64_t elapsed = now - g_bottom_fade_start_ms;
  if (elapsed >= 300) { g_bottom_fade_active = false; return; }
  unsigned alpha = 255 - (unsigned)(elapsed * 255 / 300);
  Platform3DS_DrawFadeRect(C2D_Color32(0, 0, 0, alpha));
}

static void Platform3DS_DrawBottomTexture(void) {
  if(!g_bottom_image_ready) {
    Platform3DS_ClearBlackTarget(g_bottom_target);C2D_SceneBegin(g_bottom_target);
    Platform3DS_ApplyBottomFade();return;
  }
  int width = g_bottom_subtexture.width, height = g_bottom_subtexture.height;
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
  Platform3DS_ClearBlackTarget(g_bottom_target);
  C2D_SceneBegin(g_bottom_target);
  Platform3DS_DrawMappedImage(image, &params,
    g_is_new_3ds ? ConfigureArgbTextureEnv : ConfigureRgb565TextureEnv);
  Platform3DS_ApplyBottomFade();
}

bool Platform3DS_PresentBottomFrame(const uint8_t *pixels, int pitch,
                                    int width, int height) {
  int bytes_per_pixel = g_is_new_3ds ? 4 : 2;
  if (!g_gpu_frame_active || !pixels ||
      pitch != kTopTextureWidth * bytes_per_pixel ||
      width <= 0 || width > kTopTextureWidth ||
      height <= 0 || height > kTopTextureHeight)
    return false;

  Platform3DS_CleanDataCache(
    pixels, kTopTextureWidth * kTopTextureHeight * bytes_per_pixel);
  GX_TRANSFER_FORMAT bottom_transfer_format =
    g_is_new_3ds ? GX_TRANSFER_FMT_RGBA8 : GX_TRANSFER_FMT_RGB565;
  Platform3DS_QueueDisplayTransfer(
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
  g_bottom_image_ready=true;
  Platform3DS_DrawBottomTexture();
  return true;
}

void Platform3DS_EndFrame(void) {
  if (!g_gpu_frame_active)
    return;
  if (g_bottom_fade_active || (SS_GetModule()&255)==0x12) Platform3DS_DrawBottomTexture();
  Platform3DS_EndGpuFrame();
  g_gpu_frame_active = false;
  if (g_startup_lcd_black) RevealInitializedScreens();
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
  if (g_ignore_next_frame_timing) {
    g_ignore_next_frame_timing = false;
    return;
  }
  if (!g_is_new_3ds)
    RecordRecentFrame(ppu_draw_us, total_work_us, render_interval_us,
                      logic_work_us, present_us, bottom_work_us,
                      scheduled_logic_frames, executed_logic_frames);
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
  if (!FileStore_Recover(destination)) return false;
  return IsRegularFile(destination) || FileStore_Copy(source, destination);
}

static bool CopyFileReplacing(const char *source, const char *destination) {
  return FileStore_Copy(source, destination);
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

typedef struct RomEntry {
  char filename[256];
  char profile[320];
  uint32_t hash;
  bool french;  // the French Canadian cartridge: the menu speaks French
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

// Not ROMs to offer or to build from: macOS resource forks, and the copy of
// the French Canadian cartridge that earlier builds left beside the ROMs.
static bool RomFileShouldBeIgnored(const char *name) {
  return strncmp(name, "._", 2) == 0 ||
         strcasecmp(name, LOCALIZED_CART_FILE_FR_C) == 0;
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
    roms[count].french = strcmp(RomModLanguage(entry->d_name), "fr-c") == 0;
    MakeProfilePath(roms[count].filename, roms[count].hash,
                    roms[count].profile, sizeof(roms[count].profile));
    count++;
  }
  closedir(directory);
  return count;
}

// The menu music fades in and out: its channel's volume moves a step each
// DSP frame (160 samples at 32728 Hz, about 4.9 ms), on the NDSP thread, so
// the menu's drawing and the SD card never hold it back. It starts once the
// menu is on screen, the DSP started seconds before, and stops, faded out,
// before a ROM is prepared: a sound cut mid-wave clicks, and the copy of a
// profile's files left the music stuttering under it.
enum {
  kSetupMusicFadeInFrames = 61,   // about 300 ms
  kSetupMusicFadeOutFrames = 31,  // about 150 ms
};
static const float kSetupMusicMix = 0.55f;
static const float kSetupMoveMix = 0.75f;
static volatile float g_setup_music_gain, g_setup_music_target,
                      g_setup_music_step;
static bool g_setup_music_playing,g_setup_audio_preserve;

static void SetupAudioSetMix(int channel, float volume) {
  float mix[12] = {0};
  mix[0] = mix[1] = volume;
  ndspChnSetMix(channel, mix);
}

static void SetupAudioFrame(void *data) {
  (void)data;
  float gain = g_setup_music_gain, target = g_setup_music_target;
  if (gain == target)
    return;
  float step = g_setup_music_step;
  gain = gain < target ? (gain + step < target ? gain + step : target)
                       : (gain - step > target ? gain - step : target);
  g_setup_music_gain = gain;
  SetupAudioSetMix(0, kSetupMusicMix * gain);
}

static void SetupAudioStop(void) {
  if (!g_setup_audio_initialized)
    return;
  ndspSetCallback(NULL, NULL);
  ndspChnWaveBufClear(0);
  ndspChnWaveBufClear(1);
  ndspChnWaveBufClear(2);
  ndspExit();
  g_setup_audio_initialized = false;
  g_setup_music_playing = false;
  if (g_setup_music_buffer) {
    linearFree(g_setup_music_buffer);
    g_setup_music_buffer = NULL;
  }
  if (g_setup_move_buffer) {
    linearFree(g_setup_move_buffer);
    g_setup_move_buffer = NULL;
  }
  if (g_setup_intro_buffer) {
    linearFree(g_setup_intro_buffer);
    g_setup_intro_buffer = NULL;
  }
}

// Starts the DSP, silent: the music waits for SetupAudioStartMusic.
static void SetupAudioInit(void) {
  static bool exit_cleanup_registered;
  if (!exit_cleanup_registered) {
    if (atexit(SetupAudioStop) != 0) {
      LogSetup("Setup audio exit cleanup registration failed");
      return;
    }
    exit_cleanup_registered = true;
  }
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
  g_setup_intro_buffer = (int16_t *)linearAlloc(kSetupIntroBlocks*kSetupIntroSamples*2*sizeof(int16_t));
  if (!g_setup_music_buffer || !g_setup_move_buffer || !g_setup_intro_buffer) {
    LogSetup("Setup audio allocation failed");
    SetupAudioStop();
    return;
  }
  memcpy(g_setup_music_buffer, kSetupMusicSamples, music_size);
  memcpy(g_setup_move_buffer, kSetupMoveSamples, move_size);
  DSP_FlushDataCache(g_setup_music_buffer, music_size);
  DSP_FlushDataCache(g_setup_move_buffer, move_size);

  /* NDSP's output mode is global state.  This setup jingle only needs it to not
   * be left on something narrower than the game will want later, so ask for
   * STEREO rather than MONO: if ndspExit() ever stops resetting the mode (it
   * does today -- EndSetupConsole() tears NDSP down before SDL reopens it), the
   * game's stereo mix would otherwise inherit MONO for the whole session.
   *
   * The jingle itself stays mono PCM (NDSP_FORMAT_MONO_PCM16 below); a mono
   * source on a stereo output is legal and plays on both sides. */
  ndspSetOutputMode(NDSP_OUTPUT_STEREO);
  ndspSetMasterVol(0.75f);
  for (int channel = 0; channel < 2; channel++) {
    ndspChnReset(channel);
    ndspChnSetInterp(channel, NDSP_INTERP_LINEAR);
    ndspChnSetRate(channel, (float)kSetupAudioRate);
    ndspChnSetFormat(channel, NDSP_FORMAT_MONO_PCM16);
    SetupAudioSetMix(channel, channel == 0 ? 0.0f : kSetupMoveMix);
  }
  ndspChnReset(2);
  ndspChnSetInterp(2, NDSP_INTERP_LINEAR);
  ndspChnSetRate(2, 32040.0f);
  ndspChnSetFormat(2, NDSP_FORMAT_STEREO_PCM16);
  SetupAudioSetMix(2, kSetupMoveMix);
  memset(g_setup_intro_wavebuf,0,sizeof(g_setup_intro_wavebuf));
  g_setup_intro_next=0;
  g_setup_music_gain = g_setup_music_target = 0.0f;
  ndspSetCallback(SetupAudioFrame, NULL);
  LogSetup("Setup audio started");
}

static void SetupAudioStartMusic(void) {
  if (!g_setup_audio_initialized || !g_setup_music_buffer ||
      g_setup_music_playing)
    return;
  memset(&g_setup_music_wavebuf, 0, sizeof(g_setup_music_wavebuf));
  g_setup_music_wavebuf.data_pcm16 = g_setup_music_buffer;
  g_setup_music_wavebuf.nsamples = kSetupMusicSampleCount;
  g_setup_music_wavebuf.looping = true;
  g_setup_music_step = 1.0f / kSetupMusicFadeInFrames;
  g_setup_music_target = 1.0f;
  ndspChnWaveBufAdd(0, &g_setup_music_wavebuf);
  g_setup_music_playing = true;
}

// Fades the music out, lets the DSP play a few frames of silence, then
// stops it.
static void SetupAudioFadeOutAndStop(void) {
  if (!g_setup_audio_initialized)
    return;
  g_setup_music_step = 1.0f / kSetupMusicFadeOutFrames;
  g_setup_music_target = 0.0f;
  for (int i = 0; i < 20 && g_setup_music_gain > 0.0f; i++)
    gspWaitForVBlank();
  ndspChnWaveBufClear(0);
  ndspChnWaveBufClear(1);
  gspWaitForVBlank();
  gspWaitForVBlank();
  SetupAudioStop();
}

static void SetupAudioPlayIntro(const int16_t *samples,unsigned count,void *context) {
  (void)context;
  if(!g_setup_audio_initialized||!g_setup_intro_buffer||count!=kSetupIntroSamples)return;
  for(unsigned n=0;n<kSetupIntroBlocks;n++) {
    unsigned i=(g_setup_intro_next+n)%kSetupIntroBlocks;
    ndspWaveBuf *wave=&g_setup_intro_wavebuf[i];
    if(wave->status!=NDSP_WBUF_FREE&&wave->status!=NDSP_WBUF_DONE)continue;
    int16_t *dest=g_setup_intro_buffer+i*kSetupIntroSamples*2;
    memcpy(dest,samples,count*2*sizeof(*dest));
    DSP_FlushDataCache(dest,count*2*sizeof(*dest));
    memset(wave,0,sizeof(*wave));wave->data_pcm16=dest;wave->nsamples=count;
    ndspChnWaveBufAdd(2,wave);g_setup_intro_next=(i+1)%kSetupIntroBlocks;return;
  }
}

static void SetupAudioStopIntro(void) {
  if(g_setup_audio_initialized)ndspChnWaveBufClear(2);
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

// SDL owns NDSP channel 0 and its frame callback. Paused menu effects use
// channel 23 without changing that callback, service ownership or game audio.
static int16_t *g_menu_move_buffer;
static ndspWaveBuf g_menu_move_wavebuf;
static bool g_menu_audio_enabled;
static void FreeMenuMoveBuffer(void) { linearFree(g_menu_move_buffer);g_menu_move_buffer=NULL; }
void Platform3DS_SetMenuAudio(bool enabled) {
  if(enabled==g_menu_audio_enabled)return;
  if(enabled&&!g_menu_move_buffer) {
    g_menu_move_buffer=linearAlloc(sizeof(kSetupMoveSamples));
    if(!g_menu_move_buffer)return;
    memcpy(g_menu_move_buffer,kSetupMoveSamples,sizeof(kSetupMoveSamples));
    DSP_FlushDataCache(g_menu_move_buffer,sizeof(kSetupMoveSamples));
    atexit(FreeMenuMoveBuffer);
  }
  ndspChnWaveBufClear(23);
  if(enabled) {
    ndspChnReset(23);ndspChnSetInterp(23,NDSP_INTERP_LINEAR);
    ndspChnSetRate(23,kSetupAudioRate);ndspChnSetFormat(23,NDSP_FORMAT_MONO_PCM16);
    SetupAudioSetMix(23,kSetupMoveMix);
  }
  g_menu_audio_enabled=enabled;
}
void Platform3DS_PlayMenuMove(void) {
  if(g_setup_audio_initialized){SetupAudioPlayMove();return;}
  if(!g_menu_audio_enabled||!g_menu_move_buffer)return;
  ndspChnWaveBufClear(23);memset(&g_menu_move_wavebuf,0,sizeof(g_menu_move_wavebuf));
  g_menu_move_wavebuf.data_pcm16=g_menu_move_buffer;g_menu_move_wavebuf.nsamples=kSetupMoveSampleCount;
  ndspChnWaveBufAdd(23,&g_menu_move_wavebuf);
}

static void PresentSetupConsole(void);
static bool g_setup_bottom_only;

static void BeginSetupConsole(void) {
  if (g_setup_console_active)
    return;
  g_startup_lcd_black = true;
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
  RevealInitializedScreens();
}

static void PresentSetupConsole(void) {
  u16 fb_w = 0, fb_h = 0;
  uint16_t *top =
    (uint16_t *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_w, &fb_h);
  if (top && !g_setup_bottom_only) {
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
  if(!g_setup_audio_preserve)SetupAudioStop();
  g_startup_lcd_black = true;
  GSPGPU_SetLcdForceBlack(1);
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

// The menu text is UTF-8: one glyph per character, so the widths count
// characters, not bytes. Unknown or broken sequences draw as a blank.
static uint32_t SetupNextChar(const char **text) {
  const uint8_t *p = (const uint8_t *)*text;
  uint32_t c = *p++;
  int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
  if (c >= 0x80 && !extra)
    c = 0xfffd;
  else if (extra)
    c &= 0x3f >> extra;
  for (; extra > 0; extra--, p++) {
    if ((*p & 0xc0) != 0x80) {
      c = 0xfffd;
      break;
    }
    c = c << 6 | (*p & 0x3f);
  }
  *text = (const char *)p;
  return c;
}

static int SetupTextLength(const char *text) {
  int n = 0;
  if (text)
    while (*text) {
      SetupNextChar(&text);
      n++;
    }
  return n;
}

// Accented capitals of the offered languages: the base letter and its accent.
enum { kAccentNone, kAccentAcute, kAccentGrave, kAccentCircumflex,
       kAccentDiaeresis, kAccentCedilla, kAccentTilde, kAccentBreve };

static char SetupBaseLetter(uint32_t c, int *accent) {
  static const struct { uint16_t code; char base; uint8_t accent; } kAccented[] = {
    {0xc1, 'A', kAccentAcute}, {0xc3, 'A', kAccentTilde},
    {0xcc, 'I', kAccentGrave}, {0xcd, 'I', kAccentAcute},
    {0xd1, 'N', kAccentTilde}, {0xd2, 'O', kAccentGrave},
    {0xd3, 'O', kAccentAcute}, {0xd5, 'O', kAccentTilde},
    {0xda, 'U', kAccentAcute},
    {0xc0, 'A', kAccentGrave}, {0xc2, 'A', kAccentCircumflex},
    {0xc4, 'A', kAccentDiaeresis}, {0xc7, 'C', kAccentCedilla},
    {0xc8, 'E', kAccentGrave}, {0xc9, 'E', kAccentAcute},
    {0xca, 'E', kAccentCircumflex}, {0xcb, 'E', kAccentDiaeresis},
    {0xce, 'I', kAccentCircumflex}, {0xcf, 'I', kAccentDiaeresis},
    {0xd4, 'O', kAccentCircumflex}, {0xd6, 'O', kAccentDiaeresis},
    {0xd9, 'U', kAccentGrave}, {0xdb, 'U', kAccentCircumflex},
    {0xdc, 'U', kAccentDiaeresis},
  };
  *accent = kAccentNone;
  if (c >= 0xe0 && c <= 0xfe && c != 0xf7)
    c -= 0x20;  // lower case Latin-1 letters draw as capitals
  if (c < 0x80)
    return (char)c;
  for (size_t i = 0; i < sizeof(kAccented) / sizeof(kAccented[0]); i++)
    if (kAccented[i].code == c) {
      *accent = kAccented[i].accent;
      return kAccented[i].base;
    }
  return 0;
}

// Rows 0 to 6 hold the letter; an accent takes rows -2 and -1 above it,
// a cedilla rows 7 and 8 below.
static uint8_t SetupGlyph(uint32_t code, int row) {
  int accent;
  char c = SetupBaseLetter(code, &accent);
  // Cyrillic capitals; lower case, Ё and Й map onto them with their marks.
  int cyrillic = -1;
  if (code == 0x401 || code == 0x451)
    cyrillic = 5, accent = kAccentDiaeresis;
  else if (code >= 0x410 && code <= 0x44f)
    cyrillic = (int)((code - 0x410) & 31);
  if (cyrillic == 9)
    accent = kAccentBreve;
  if (row < 0 || row > 6) {
    static const uint8_t kMarks[8][4] = {
      {0, 0, 0, 0}, {2, 4, 0, 0}, {8, 4, 0, 0}, {4, 10, 0, 0},
      {0, 10, 0, 0}, {0, 0, 4, 8}, {13, 22, 0, 0}, {17, 14, 0, 0},
    };
    return kMarks[accent][row < 0 ? row + 2 : row - 5];
  }
  if (cyrillic >= 0)
    return kUiCyrillic5x7[cyrillic][row];
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
    case ',': return row == 5 ? 4 : row == 6 ? 8 : 0;
    case '\'': return row == 0 ? 4 : row == 1 ? 8 : 0;
    case '-': return row == 3 ? 31 : 0;
    case '_': return row == 6 ? 31 : 0;
    case '/': {
      static const uint8_t kSlash[7] = { 1, 1, 2, 4, 8, 16, 16 };
      return kSlash[row];
    }
    case ':': return row == 2 || row == 5 ? 4 : 0;
    case '(': return row == 0 ? 2 : row == 6 ? 2 : 4;
    case ')': return row == 0 ? 8 : row == 6 ? 8 : 4;
    case '[': return row == 0 || row == 6 ? 14 : 8;
    case ']': return row == 0 || row == 6 ? 14 : 2;
    case '+': return row == 3 ? 31 : (row >= 1 && row <= 5 ? 4 : 0);
    case '!': return row < 5 ? 4 : (row == 6 ? 4 : 0);
    case '<': return row < 4 ? (1 << row) : (1 << (6 - row));
    case '>': return row < 4 ? (16 >> row) : (16 >> (6 - row));
    default: return 0;
  }
}

static int SetupTextWidth(const char *text, int scale) {
  int width = 0;
  while (text && *text)
    width += (UiFontJapaneseIndex(SetupNextChar(&text)) >= 0 ? 11 : 6) * scale;
  return width;
}

static void SetupDrawChar(gfxScreen_t screen, uint32_t c, int x, int y,
                          int scale, uint16_t color) {
  if (c == ' ')
    return;
  int japanese = UiFontJapaneseIndex(c);
  if (japanese >= 0) {
    for (int row = 0; row < 10; row++)
      for (int col = 0; col < 10; col++)
        if (kUiJapaneseGlyphs10[japanese][row] & (1 << (9 - col)))
          SetupFillRect(screen, x + col * scale, y + (row - 2) * scale, scale, scale, color);
    return;
  }
  for (int row = -2; row < 9; row++) {
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
  while (*text) {
    if (max_width > 0 && cursor + 6 * scale > last)
      break;
    uint32_t c = SetupNextChar(&text);
    SetupDrawChar(screen, c, cursor, y, scale, color);
    cursor += (UiFontJapaneseIndex(c) >= 0 ? 11 : 6) * scale;
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

// Layout of the text engine inside one family of ROMs.
//
// The stock US ROM, and every translation hack built on top of it, keeps the
// messages, the dictionary and the message font at fixed offsets and already
// encodes commands the way the assets file wants them, so those ROMs are
// copied out verbatim. The official European and Canadian ROMs use a second
// layout: different offsets, a wider alphabet, and a command encoding that has
// to be translated (see EncodeEuTextCommand).
// The language the next extraction adds to the USA assets (the selector's
// choice); NULL or English keeps the USA text alone.
static const GameLanguage *g_build_language;
static const GameLanguage *g_profile_menu_language;

typedef struct RomTextLayout {
  const char *language_code;   // NULL: derive it from the ROM header instead
  const char *header_title;    // internal ROM name, NULL when the layout is
                               // reached by trial instead
  uint8 dialogue_flags;        // bit 0: EU command encoding, bit 1: not US text
  size_t text_bank1;           // first block of message data
  size_t text_bank2;           // block the bank-switch byte jumps to
  size_t dict_pointers;        // 16-bit pointer table, one entry per word
  uint16 dict_pointer_base;    // value of the first entry when the words start
                               // immediately behind the table
  size_t dict_bank;            // file offset of the bank that holds the words
  size_t font_gfx;             // 256 characters, 2bpp
  size_t font_widths;
  size_t font_widths_count;
  uint8 command_start;         // first byte that is a command
  uint8 switch_bank;           // bank-switch byte, also ends the command range
  uint8 finish;                // ends the whole message list
  uint8 dict_base;             // first byte that selects a dictionary word
  const uint8 *command_args;   // extra bytes per command, from command_start
  const uint8 *message4;       // item-select template, spliced back in when the
  size_t message4_size;        // ROM's message list is missing it
  bool eu_commands;
  bool build_only;             // tried only for the language being built
  bool japanese;               // its own text and compressed font, see below
} RomTextLayout;

// Extra bytes following each command byte 0x67..0x7f, mirroring
// kText_CommandLengths_US in messaging.c.
static const uint8 kUsCommandArgs[] = {
  0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
  1, 1, 0, 0, 0, 0, 0,
};

// Same thing for the European command range 0x70..0x87.
static const uint8 kEuCommandArgs[] = {
  0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 1, 1, 1, 1,
};

// Message 4 is the item-select digit template. Some ROM dumps drop it, and the
// game hardcodes message numbers, so it is spliced back in to keep the list
// aligned. These are the original bytes for each encoding.
static const uint8 kUsMessage4[] = {
  0x7a, 0x00, 0x34, 0x40, 0x59, 0x6c, 0x00, 0x41, 0x59, 0x35,
  0x40, 0x59, 0x6c, 0x01, 0x75, 0x36, 0x40, 0x59, 0x6c, 0x02,
  0x41, 0x59, 0x37, 0x40, 0x59, 0x6c, 0x03,
};
static const uint8 kEuMessage4[] = {
  0x87, 0x30, 0x34, 0x40, 0x59, 0x87, 0x20, 0x41, 0x59, 0x35,
  0x40, 0x59, 0x87, 0x21, 0x83, 0x36, 0x40, 0x59, 0x87, 0x22,
  0x41, 0x59, 0x37, 0x40, 0x59, 0x87, 0x23,
};

static const RomTextLayout kRomTextLayouts[] = {
  // "Zelda no Densetsu: Kamigami no Triforce": read by ExtractJapaneseText.
  { .language_code = "ja", .dialogue_flags = 6, .build_only = true, .japanese = true },
  // Stock US layout: the US ROM itself and the translation hacks based on it.
  {
    .language_code = NULL,
    .header_title = NULL,
    .dialogue_flags = 2,
    .text_bank1 = 0xe0000, .text_bank2 = 0x75f40,
    .dict_pointers = 0x74703, .dict_pointer_base = 0xc703, .dict_bank = 0x70000,
    .font_gfx = 0x70000, .font_widths = 0x74adf, .font_widths_count = 103,
    .command_start = 0x67, .switch_bank = 0x80, .finish = 0xff, .dict_base = 0x88,
    .command_args = kUsCommandArgs,
    .message4 = kUsMessage4, .message4_size = sizeof(kUsMessage4),
    .eu_commands = false,
  },
  // "Legend of Zelda, The - A Link to the Past (Canada)", the official French
  // release (SHA-1 C1C6C7F76FFF936C534FF11F87A54162FC0AA100). Offsets follow
  // upstream zelda3's fr-c tables: messages at $9C:8000 and $8C:F150, the
  // dictionary pointer table at $8C:E3C3, the font at $8C:D078 and its 112
  // widths at $8C:E83F.
  {
    .language_code = "fr-c",
    .header_title = "LA LEGENDE DE ZELDA",
    .dialogue_flags = 3,
    .text_bank1 = 0xe0000, .text_bank2 = 0x67150,
    .dict_pointers = 0x663c3, .dict_pointer_base = 0xe3c3, .dict_bank = 0x60000,
    .font_gfx = 0x65078, .font_widths = 0x6683f, .font_widths_count = 112,
    .command_start = 0x70, .switch_bank = 0x88, .finish = 0x8f, .dict_base = 0x90,
    .command_args = kEuCommandArgs,
    .message4 = kEuMessage4, .message4_size = sizeof(kEuMessage4),
    .eu_commands = true,
  },
  // The official German and French (PAL) releases, rebuilt from the USA ROM
  // by the language patches. They share the Canadian release's encoding but
  // not its addresses: messages at $9C:8000 then $8C:EB00 (German) or
  // $8C:E800 (French), dictionaries at $8C:DA2B / $8C:DA33 with the words
  // right behind them, and one font at $8C:C6E8 whose 112 widths are at
  // $8C:DECF / $8C:DEAF. Their header titles prove nothing (the German one
  // is the USA title, the French one the Canadian), so only the language
  // being built selects them.
  {
    .language_code = "de",
    .header_title = NULL,
    .dialogue_flags = 3,
    .text_bank1 = 0xe0000, .text_bank2 = 0x66b00,
    .dict_pointers = 0x65a2b, .dict_pointer_base = 0xda2b, .dict_bank = 0x60000,
    .font_gfx = 0x646e8, .font_widths = 0x65ecf, .font_widths_count = 112,
    .command_start = 0x70, .switch_bank = 0x88, .finish = 0x8f, .dict_base = 0x90,
    .command_args = kEuCommandArgs,
    .message4 = kEuMessage4, .message4_size = sizeof(kEuMessage4),
    .eu_commands = true,
    .build_only = true,
  },
  {
    .language_code = "fr",
    .header_title = NULL,
    .dialogue_flags = 3,
    .text_bank1 = 0xe0000, .text_bank2 = 0x66800,
    .dict_pointers = 0x65a33, .dict_pointer_base = 0xda33, .dict_bank = 0x60000,
    .font_gfx = 0x646e8, .font_widths = 0x65eaf, .font_widths_count = 112,
    .command_start = 0x70, .switch_bank = 0x88, .finish = 0x8f, .dict_base = 0x90,
    .command_args = kEuCommandArgs,
    .message4 = kEuMessage4, .message4_size = sizeof(kEuMessage4),
    .eu_commands = true,
    .build_only = true,
  },
};

// Rewrite one European ROM command into the encoding the engine's EU decoder
// expects (Text_DecodeCmd in messaging.c, dialogue_flags bit 0). Returns how
// many bytes were written, or -1 for a command the engine has no equivalent
// for. `param` is only meaningful for commands that carry an argument byte.
static int EncodeEuTextCommand(uint8 command, uint8 param, uint8 *out) {
  switch (command) {
  case 0x73: out[0] = 0x80; return 1;                                  // Scroll
  case 0x7e: out[0] = 0x81; return 1;                                  // Waitkey
  case 0x74: out[0] = 0x82; return 1;                                  // Line 1
  case 0x75: out[0] = 0x83; return 1;                                  // Line 2
  case 0x76: out[0] = 0x84; return 1;                                  // Line 3
  case 0x83: out[0] = 0x85; return 1;                                  // Player name
  case 0x78: out[0] = 0x87; out[1] = 0x00 | (param & 0xf); return 2;   // Wait
  case 0x77: out[0] = 0x87; out[1] = 0x10 | (param & 0xf); return 2;   // Color
  case 0x85: out[0] = 0x87; out[1] = 0x20 | (param & 0xf); return 2;   // Number
  case 0x7a: out[0] = 0x87; out[1] = 0x30 | (param & 0xf); return 2;   // Speed
  case 0x79: out[0] = 0x87; out[1] = 0x40; return 2;                   // Sound
  case 0x81: out[0] = 0x87; out[1] = 0x80; return 2;                   // Choose
  case 0x72: out[0] = 0x87; out[1] = 0x81; return 2;                   // Choose2
  case 0x71: out[0] = 0x87; out[1] = 0x82; return 2;                   // Choose3
  case 0x70: out[0] = 0x87; out[1] = 0x83; return 2;                   // Selchg
  case 0x82: out[0] = 0x87; out[1] = 0x84; return 2;                   // Item
  case 0x80: out[0] = 0x87; out[1] = 0x85; return 2;                   // NextPic
  // Window type 0 is the default the engine already starts in and has no
  // encoding of its own; only the tall box (type 2) is emitted.
  case 0x84: if (param == 0) return 0;
             out[0] = 0x87; out[1] = 0x86; return 2;                   // Window
  case 0x86: out[0] = 0x87; out[1] = 0x87 + (param & 1); return 2;     // Position
  case 0x87: out[0] = 0x87; out[1] = 0x50 | (param & 0xf); return 2;   // ScrollSpd
  default: return -1;                               // Mark, Mark2, Clear: unused
  }
}

// The Japanese cartridge has no dictionary. Its messages run from $1C:8000, each
// ended by 0xfb: 0xfd and 0xfc take one more byte, 0xfe a command and maybe its
// argument. Its font at $0E:8000 is 512 letters, with a mask of 5 bytes for each
// at $0E:B844 saying which of its 40 bytes are stored.
enum {
  kJpText = 0xe0000, kJpTextEnd = 0xe8000, kJpFont = 0x70000, kJpFontMasks = 0x73844,
  kJpLetters = 512, kJpLetterBytes = 44, kJpMessages = 395,
};

static bool ExtractJapaneseText(const uint8 *rom, size_t rom_size,
                                OwnedBlock *dialogue_block, OwnedBlock *font_block) {
  // The USA cartridge added two choice messages, 11 and 12; it has the others
  // two places later. Written as those messages are in the USA text.
  static const uint8 kChoice11[] = { 0xfc, 0x00, 0xf9, 0xff, 0xff, 0xff, 0xf8, 0xff, 0xff, 0xe4, 0xfe, 0x6f };
  static const uint8 kChoice12[] = { 0xfc, 0x00, 0xf8, 0xff, 0xff, 0xff, 0xf9, 0xff, 0xff, 0xe4, 0xfe, 0x6f };
  if (rom_size < kJpTextEnd)
    return false;
  OwnedBlock messages[kJpMessages + 2] = {0};
  int count = 0;
  bool ok = true;
  size_t p = kJpText, start = p;
  while (ok && p < kJpTextEnd && count < kJpMessages) {
    uint8 c = rom[p];
    if (c == 0xfb) {
      int at = count < 11 ? count : count + 2;
      ok = CopyBlock(&messages[at], rom + start, p - start);
      count++;
      start = ++p;
    } else if (c == 0xfd || c == 0xfc) {
      p += 2;
    } else if (c == 0xfe) {
      uint8 cmd = rom[p + 1];
      if (cmd < 0x67 || cmd >= 0x80) {
        ok = false;
        break;
      }
      p += 2 + kUsCommandArgs[cmd - 0x67];
    } else {
      p++;
    }
  }
  ok = ok && count == kJpMessages &&
       CopyBlock(&messages[11], kChoice11, sizeof(kChoice11)) &&
       CopyBlock(&messages[12], kChoice12, sizeof(kChoice12));
  OwnedBlock dictionary = {0}, packed_dictionary = {0}, packed_messages = {0};
  OwnedBlock dialogue_parts[2] = {0}, font_parts[2] = {0};
  static const uint8 kNoWord[1] = { 0 };
  ok = ok && CopyBlock(&dictionary, kNoWord, 1) &&
       PackBlocks(&dictionary, 1, &packed_dictionary) &&
       PackBlocks(messages, kJpMessages + 2, &packed_messages);
  FreeBlocks(&dictionary, 1);
  FreeBlocks(messages, kJpMessages + 2);
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
  uint8 *letters = calloc(kJpLetters, kJpLetterBytes);
  size_t from = kJpFont;
  for (int g = 0; letters && g < kJpLetters; g++) {
    const uint8 *mask = rom + kJpFontMasks + g * 5;
    uint8 *to = letters + g * kJpLetterBytes;
    // The first stored byte is byte 4 of the letter: its first line is blank.
    for (int bit = 0; bit < 40; bit++) {
      if (mask[bit >> 3] & (0x80 >> (bit & 7))) {
        if (bit + 4 < kJpLetterBytes)
          to[bit + 4] = rom[from];
        from++;
      }
    }
  }
  static const uint8 kNoWidths[1] = { 0 };
  ok = letters && CopyBlock(&font_parts[0], letters, kJpLetters * kJpLetterBytes) &&
       CopyBlock(&font_parts[1], kNoWidths, 1) &&
       PackBlocks(font_parts, 2, font_block);
  free(letters);
  FreeBlocks(font_parts, 2);
  if (!ok) {
    free(dialogue_block->data);
    dialogue_block->data = NULL;
    dialogue_block->size = 0;
  }
  return ok;
}

static bool ExtractTranslationWithLayout(const uint8 *rom, size_t rom_size,
                                         const RomTextLayout *layout,
                                         OwnedBlock *dialogue_block,
                                         OwnedBlock *font_block) {
  enum { kRomSize = 1048576 };
  if (layout->japanese)
    return ExtractJapaneseText(rom, rom_size, dialogue_block, font_block);
  dialogue_block->data = NULL;
  dialogue_block->size = 0;
  font_block->data = NULL;
  font_block->size = 0;
  if (rom_size != kRomSize)
    return false;

  OwnedBlock messages[420] = {0};
  int message_count = 0;
  ByteArray current = {0};
  size_t p = layout->text_bank1;
  int bank_switches = 0;
  bool ok = true;
  for (int guard = 0; ok; guard++) {
    if (guard > 0x20000 || p >= rom_size || message_count >= (int)countof(messages)) {
      ok = false;
      break;
    }
    uint8 c = rom[p];
    if (c == layout->finish)
      break;
    if (c == layout->switch_bank) {
      if (++bank_switches > 1) {
        ok = false;
        break;
      }
      p = layout->text_bank2;
      current.size = 0;
      continue;
    }
    bool is_command = c >= layout->command_start && c < layout->switch_bank;
    size_t length = is_command ?
      1 + layout->command_args[c - layout->command_start] : 1;
    if (p + length > rom_size) {
      ok = false;
      break;
    }
    if (c == 0x7f) {
      // End of message. The terminator itself is not stored.
      ok = CopyBlock(&messages[message_count++], current.data, current.size);
      current.size = 0;
      p += length;
      continue;
    }
    if (!layout->eu_commands) {
      ByteArray_AppendData(&current, rom + p, length);
    } else if (is_command) {
      uint8 encoded[2];
      int encoded_size = EncodeEuTextCommand(c, length == 2 ? rom[p + 1] : 0,
                                             encoded);
      if (encoded_size < 0) {
        ok = false;
        break;
      }
      ByteArray_AppendData(&current, encoded, encoded_size);
    } else if (c >= layout->dict_base) {
      // The assets file always references dictionary words from 0x88 up.
      uint8 rebased = (uint8)(c - layout->dict_base + 0x88);
      ByteArray_AppendData(&current, &rebased, 1);
    } else {
      ByteArray_AppendData(&current, rom + p, 1);
    }
    p += length;
  }
  ByteArray_Destroy(&current);
  if (!ok || message_count < 300) {
    FreeBlocks(messages, message_count);
    return false;
  }
  if (message_count == 396 && message_count < (int)countof(messages)) {
    for (int i = message_count; i > 4; i--)
      messages[i] = messages[i - 1];
    messages[4].data = NULL;
    messages[4].size = 0;
    ok = CopyBlock(&messages[4], layout->message4, layout->message4_size);
    message_count++;
  }

  uint16 first = ReadU16LE(rom + layout->dict_pointers);
  int gap = (int)first - (int)layout->dict_pointer_base;
  int dict_count = gap / 2 - 1;
  OwnedBlock dictionary[220] = {0};
  if (!ok || gap <= 0 || (gap & 1) != 0 || dict_count < 50 ||
      dict_count > (int)countof(dictionary)) {
    FreeBlocks(messages, message_count);
    return false;
  }
  for (int i = 0; i < dict_count; i++) {
    uint16 start = ReadU16LE(rom + layout->dict_pointers + i * 2);
    uint16 end = ReadU16LE(rom + layout->dict_pointers + i * 2 + 2);
    size_t offset = layout->dict_bank + start - 0x8000;
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

  if (layout->font_gfx + 0x1000 > rom_size ||
      layout->font_widths + layout->font_widths_count > rom_size) {
    free(dialogue_block->data);
    dialogue_block->data = NULL;
    dialogue_block->size = 0;
    return false;
  }
  ok = CopyBlock(&font_parts[0], rom + layout->font_gfx, 0x1000) &&
       CopyBlock(&font_parts[1], rom + layout->font_widths,
                 layout->font_widths_count) &&
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

static bool RomHeaderTitleMatches(const uint8 *rom, size_t rom_size,
                                  const char *title) {
  size_t length = strlen(title);
  return rom_size >= 0x7fc0 + length &&
         memcmp(rom + 0x7fc0, title, length) == 0;
}

// Pick the text layout that fits the ROM and return it, or NULL when the ROM
// carries no text we can lift. A ROM whose internal name matches one of the
// localized releases is only ever tried with that release's layout: the US
// layout is loose enough to find plausible-looking messages in the wrong
// place, and guessing wrong here means silently shipping garbage dialogue.
static const RomTextLayout *ExtractTranslationLanguage(const uint8 *rom,
                                                       size_t rom_size,
                                                       OwnedBlock *dialogue_block,
                                                       OwnedBlock *font_block) {
  // A cartridge rebuilt by a language patch is read with that language's
  // layout first.
  for (size_t i = 0; g_build_language && i < countof(kRomTextLayouts); i++) {
    const char *code = kRomTextLayouts[i].language_code;
    if (code && !strcmp(code, g_build_language->code) &&
        ExtractTranslationWithLayout(rom, rom_size, &kRomTextLayouts[i],
                                     dialogue_block, font_block))
      return &kRomTextLayouts[i];
  }
  const RomTextLayout *named = NULL;
  for (size_t i = 0; i < countof(kRomTextLayouts); i++) {
    if (kRomTextLayouts[i].header_title &&
        RomHeaderTitleMatches(rom, rom_size, kRomTextLayouts[i].header_title)) {
      named = &kRomTextLayouts[i];
      break;
    }
  }
  if (named) {
    if (ExtractTranslationWithLayout(rom, rom_size, named,
                                     dialogue_block, font_block))
      return named;
  } else {
    for (size_t i = 0; i < countof(kRomTextLayouts); i++) {
      if (!kRomTextLayouts[i].header_title && !kRomTextLayouts[i].build_only &&
          ExtractTranslationWithLayout(rom, rom_size, &kRomTextLayouts[i],
                                       dialogue_block, font_block))
        return &kRomTextLayouts[i];
    }
  }
  dialogue_block->data = NULL;
  dialogue_block->size = 0;
  font_block->data = NULL;
  font_block->size = 0;
  return NULL;
}

// Non-NULL when the ROM is an official localized release rather than the US
// ROM or a hack of it. Those ROMs must never be fed to the bundled assets
// patch: it is a delta against the US ROM, and the loose fallback below
// happily applies it to anything the right size, which would silently build
// the assets out of the wrong bytes.
static const RomTextLayout *RomLocalizedLayout(const uint8 *rom,
                                               size_t rom_size) {
  OwnedBlock dialogue = {0};
  OwnedBlock font = {0};
  const RomTextLayout *layout = ExtractTranslationLanguage(rom, rom_size,
                                                           &dialogue, &font);
  FreeBlocks(&dialogue, 1);
  FreeBlocks(&font, 1);
  return (layout && layout->language_code) ? layout : NULL;
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

// only_language: the assets carry no dialogue yet (the French Canadian
// cartridge's own build) and this one becomes dialogue 0, the only one.
static bool AddTranslatedLanguageToAssets(const uint8 *assets_data,
                                          size_t assets_size,
                                          const uint8 *rom,
                                          size_t rom_size,
                                          const char *language_code,
                                          bool only_language,
                                          uint8 **out_data,
                                          size_t *out_size) {
  *out_data = NULL;
  *out_size = 0;
  OwnedBlock language_dialogue = {0};
  OwnedBlock language_font = {0};
  const RomTextLayout *layout = ExtractTranslationLanguage(rom, rom_size,
                                                           &language_dialogue,
                                                           &language_font);
  if (!layout)
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
  if (ok && !only_language)
    ok = UnpackBlocks(assets[94].data, assets[94].size,
                      &dialogues, &dialogue_count) &&
         UnpackBlocks(assets[95].data, assets[95].size,
                      &fonts, &font_count) &&
         UnpackBlocks(assets[96].data, assets[96].size,
                      &map, &map_count);
  if (ok && !only_language && (dialogue_count != font_count || map_count <= 0))
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
      if (dialogue_count)
        memcpy(new_dialogues, dialogues, dialogue_count * sizeof(*dialogues));
      if (font_count)
        memcpy(new_fonts, fonts, font_count * sizeof(*fonts));
      if (map_count)
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
      uint8 conf[] = { (uint8)index, (uint8)index, layout->dialogue_flags };
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

// True when the settings file sets Language to this code.
static bool IniDeclaresLanguage(const char *path, const char *language_code) {
  FILE *file = fopen(path, "rb");
  if (!file)
    return false;
  char line[256];
  bool found = false;
  while (!found && fgets(line, sizeof(line), file)) {
    char *text = Trim(line);
    char *equals = strchr(text, '=');
    if (!equals)
      continue;
    *equals = 0;
    found = strcmp(Trim(text), "Language") == 0 &&
            strcmp(Trim(equals + 1), language_code) == 0;
  }
  fclose(file);
  return found;
}

// The French Canadian profile keeps a copy of its cartridge next to its
// assets: the game draws its menus and screens from it (localized.h in the
// engine). Written through a temporary file like the assets.
static bool WriteLocalizedCartridge(const uint8 *rom, size_t rom_size) {
  static const char kTemporary[] = LOCALIZED_CART_FILE_FR_C ".tmp";
  FILE *output = fopen(kTemporary, "wb");
  if (!output)
    return false;
  bool success = fwrite(rom, 1, rom_size, output) == rom_size;
  if (fclose(output) != 0)
    success = false;
  remove(LOCALIZED_CART_FILE_FR_C);
  if (!success || rename(kTemporary, LOCALIZED_CART_FILE_FR_C) != 0) {
    remove(kTemporary);
    return false;
  }
  return true;
}

static bool WriteTranslatedProfileIni(const char *language_code) {
  // A profile built again keeps the line it already has.
  if (IniDeclaresLanguage("zelda3.ini", language_code))
    return true;
  FILE *file = fopen("zelda3.ini", "ab");
  if (!file)
    return false;
  bool ok = fprintf(file, "\n[General]\nLanguage = %s\n",
                    language_code) > 0;
  if (fclose(file) != 0)
    ok = false;
  return ok;
}

// The French Canadian cartridge alone: kBundledPatchFrC builds its assets
// from it (its source checksum is the cartridge's), AssetsFrC_Renumber gives
// them the engine's numbering and its dialogue is the only one. No USA ROM.
static bool TryBuildStandaloneFrCAssets(const uint8 *rom, size_t rom_size,
                                        uint8 **assets_out,
                                        size_t *assets_size_out) {
  *assets_out = NULL;
  *assets_size_out = 0;
  size_t patch_size = 0, base_size = 0;
  uint8 *patch = ReadWholeFile(kBundledPatchFrC, &patch_size);
  uint8 *base = patch ? ApplyBps(rom, rom_size, patch, patch_size, &base_size)
                      : NULL;
  free(patch);
  bool ok = base && AssetsFrC_Renumber(base, base_size) &&
            AddTranslatedLanguageToAssets(base, base_size, rom, rom_size, "fr-c",
                                          true, assets_out, assets_size_out) &&
            AssetsBlobLooksValid(*assets_out, *assets_size_out);
  free(base);
  if (!ok) {
    free(*assets_out);
    *assets_out = NULL;
    *assets_size_out = 0;
    LogSetup("French Canadian assets from the cartridge alone: FAIL (patch %s)",
             patch_size ? "read" : "missing");
    return false;
  }
  WriteTranslatedProfileIni("fr-c");
  LogSetup("French Canadian assets built from the cartridge alone, %lu bytes",
           (unsigned long)*assets_size_out);
  LogSetup("Cartridge copy for the localized screens: %s",
           WriteLocalizedCartridge(rom, rom_size) ? "OK" : "FAIL");
  return true;
}

static bool TryBuildTranslatedAssets(const uint8 *rom, size_t rom_size,
                                     const uint8 *patch, size_t patch_size,
                                     uint8 **assets_out,
                                     size_t *assets_size_out) {
  *assets_out = NULL;
  *assets_size_out = 0;
  OwnedBlock probe_dialogue = {0};
  OwnedBlock probe_font = {0};
  const RomTextLayout *layout = ExtractTranslationLanguage(rom, rom_size,
                                                           &probe_dialogue,
                                                           &probe_font);
  FreeBlocks(&probe_dialogue, 1);
  FreeBlocks(&probe_font, 1);
  if (!layout)
    return false;
  // The US layout is shared by every translation hack, so the language still
  // has to be guessed from the header there. A ROM matched by one of the
  // other layouts already knows what it is.
  const char *language_code = layout->language_code ? layout->language_code :
    DetectTranslatedRomLanguage(rom, rom_size);

  DIR *directory = opendir(kStorageDirectory);
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
    snprintf(path, sizeof(path), "%s/%s", kStorageDirectory, entry->d_name);
    if (!IsRegularFile(path))
      continue;
    size_t base_size = 0;
    uint8 *base_rom = ReadWholeFile(path, &base_size);
    if (!base_rom)
      continue;
    base_rom = NormalizeRomForExtraction(base_rom, &base_size);
    if (RomLocalizedLayout(base_rom, base_size)) {
      // Official localized ROM: it carries text we can read but it is not a
      // valid source for the assets patch.
      free(base_rom);
      continue;
    }
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
                                            rom, rom_size, language_code, false,
                                            &translated_assets,
                                            &translated_assets_size);
    free(base_assets);
    if (success) {
      *assets_out = translated_assets;
      *assets_size_out = translated_assets_size;
      WriteTranslatedProfileIni(language_code);
      LogSetup("Translated profile assets generated using base ROM: %s, language: %s",
               entry->d_name, language_code);
      if (!strcmp(language_code, "fr-c"))
        LogSetup("Cartridge copy for the localized screens: %s",
                 WriteLocalizedCartridge(rom, rom_size) ? "OK" : "FAIL");
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
  DIR *directory = opendir(kStorageDirectory);
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
    snprintf(path, sizeof(path), "%s/%s", kStorageDirectory, entry->d_name);
    if (!IsRegularFile(path))
      continue;
    size_t base_size = 0;
    uint8 *base_rom = ReadWholeFile(path, &base_size);
    if (!base_rom)
      continue;
    base_rom = NormalizeRomForExtraction(base_rom, &base_size);
    if (RomLocalizedLayout(base_rom, base_size)) {
      // Official localized ROM: it carries text we can read but it is not a
      // valid source for the assets patch.
      free(base_rom);
      continue;
    }
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

static const char *g_profile_prepare_status = "ROM preparation failed";

// Adds g_build_language to assets built from the USA cartridge: its bundled
// patch turns the cartridge into the translated one, whose text and font are
// then read as from any translated cartridge. The cartridge is not changed.
static bool AddBuildLanguage(uint8 **assets, size_t *assets_size,
                             const uint8 *rom, size_t rom_size) {
  const GameLanguage *language = g_build_language;
  if (!language || !language->patch)
    return true;
  size_t patch_size = 0, translated_size = 0;
  uint8 *patch = ReadWholeFile(language->patch, &patch_size);
  uint8 *translated = patch ? ApplyBps(rom, rom_size, patch, patch_size,
                                       &translated_size) : NULL;
  free(patch);
  LogSetup("Language %s: patch %s, translated cartridge %s", language->code,
           patch_size ? "read" : "missing", translated ? "OK" : "FAIL");
  uint8 *combined = NULL;
  size_t combined_size = 0;
  bool ok = translated &&
            AddTranslatedLanguageToAssets(*assets, *assets_size, translated,
                                          translated_size, language->code, false,
                                          &combined, &combined_size) &&
            AssetsBlobLooksValid(combined, combined_size);
  // Its lettering (sheets, names) is drawn from the cartridge it built.
  if (ok && !FileStore_Write(LANGUAGE_CART_FILE, translated, translated_size))
    ok = false;
  free(translated);
  if (!ok) {
    free(combined);
    g_profile_prepare_status = patch_size ? "Translation failed" : "Translation unavailable";
    return false;
  }
  free(*assets);
  *assets = combined;
  *assets_size = combined_size;
  return true;
}

static bool ProfileSetupFailure(const char *status, const char *path) {
  int error = errno;
  g_profile_prepare_status = status;
  LogSetup("%s: %s (errno=%d)", status, path, error);
  return false;
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
    ProfileSetupFailure("SD read error", rom_path);
    ShowFatalSetupError(error);
    return false;
  }

  const RomTextLayout *localized = RomLocalizedLayout(rom, rom_size);
  uint8 *assets = NULL;
  if (localized && !strcmp(localized->language_code, "fr-c")) {
    LogSetup("ROM identified as the official fr-c release");
    TryBuildStandaloneFrCAssets(rom, rom_size, &assets, &assets_size);
  } else if (localized) {
    LogSetup("ROM identified as the official %s release; it provides text only",
             localized->language_code);
  } else {
    LogSetup("Applying BPS patch");
    assets = ApplyBps(rom, rom_size, patch, patch_size, &assets_size);
    LogSetup("BPS result: %s, %lu bytes", assets ? "OK" : "FAIL",
             (unsigned long)assets_size);

    if (assets && !AddBuildLanguage(&assets, &assets_size, rom, rom_size)) {
      free(rom);
      free(patch);
      free(assets);
      return false;
    }
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
    g_profile_prepare_status = "Incompatible ROM";
    LogSetup("ROM not compatible with available extraction paths");
    return false;
  }

  bool written = WriteAssetsFile(assets, assets_size);
  LogSetup("Assets write: %s", written ? "OK" : "FAIL");
  free(assets);
  if (!written || !AssetsFileLooksValid(kAssetsFilename)) {
    ProfileSetupFailure("SD assets write error", kAssetsFilename);
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
  snprintf(out, out_size, "%s/%s%s", g_active_save_directory,
           g_randomizer_save_prefix[0] ? g_randomizer_save_prefix : g_quest_save_prefix, leaf);
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

// Mods: sdmc:/3ds/Zelda 3DS/mods/<folder>/mod.ini names the file made for
// each ROM language ("us = us.z3mod", "fr-c = fr-c.z3mod") and a category.
// Each category has one mod on at most, and mods of different categories go
// together: their files apply category by category, QUEST and GAME first,
// SKIN last, so a skin shows over the hero of the game or quest underneath
// (engine/src/mods.h). A QUEST is a whole other game made from a ROM
// hack; it and a GAME mod both replace the story, so one turns the other
// off, and a QUEST keeps its own saves. A profile keeps the folders turned
// on in mods.txt, one per line. The build may also put mods in the RomFS
// (ZELDA3_BUNDLED_MODS, platform/3ds/CMakeLists.txt): they are listed with
// those of the SD card, and a mod of the SD card replaces the built-in one
// whose folder has the same name.
static const char kLooseModsDirectory[] = "sdmc:/3ds/Zelda 3DS";
static const char kModsDirectory[] = "sdmc:/3ds/Zelda 3DS/mods";
static const char kBundledModsDirectory[] = "romfs:/mods";
static const char kModSelectionFile[] = "mods.txt";

enum { kMaxMods = 512, kModInfoLines = 7 };

typedef struct ModEntry {
  const char *base;  // kModsDirectory or kBundledModsDirectory
  char folder[96];  // "<mod>" or "<group>/<mod>"
  char name[40];  // UTF-8: room for accented letters
  char category[12];
  char info[kModInfoLines][80];  // Info, Info2... Info7
  char file[64];
  bool enabled,loose;
} ModEntry;

static ModEntry g_mod_entries[kMaxMods];

// QUEST mods (a whole other game) and GAME mods (story, text, their
// graphics) apply first, SKIN mods (the hero's look) last, any other
// category in between.
static int ModCategoryRank(const char *category) {
  return strcasecmp(category, "QUEST") == 0 ? 0 :
         strcasecmp(category, "GAME") == 0 ? 1 :
         strcasecmp(category, "SKIN") == 0 ? 3 : 2;
}

// Two mods that cannot be on together: same category, or a QUEST and a
// GAME mod, which both replace the story and its text.
static bool ModCategoriesClash(const char *a, const char *b) {
  return strcasecmp(a, b) == 0 || ModCategoryRank(a) + ModCategoryRank(b) == 1;
}

static int CompareMods(const void *a, const void *b) {
  const ModEntry *x = (const ModEntry *)a, *y = (const ModEntry *)b;
  int r = ModCategoryRank(x->category) - ModCategoryRank(y->category);
  if (!r)
    r = strcasecmp(x->category, y->category);
  return r ? r : strcasecmp(x->name, y->name);
}

// The language a ROM's mods are made for: the French Canadian cartridge has
// its own files, every other ROM takes the USA ones.
static const char *RomModLanguage(const char *path) {
  static const char kCanadianTitle[] = "LA LEGENDE DE ZELDA";
  char title[sizeof(kCanadianTitle) - 1];
  FILE *file = fopen(path, "rb");
  if (!file)
    return "us";
  long header = 0;
  if (fseek(file, 0, SEEK_END) == 0 && (ftell(file) & 0x3ff) == 0x200)
    header = 0x200;
  bool canadian = fseek(file, header + 0x7fc0, SEEK_SET) == 0 &&
                  fread(title, 1, sizeof(title), file) == sizeof(title) &&
                  memcmp(title, kCanadianTitle, sizeof(title)) == 0;
  fclose(file);
  return canadian ? "fr-c" : "us";
}

// A mod folder is "<mod>" or, for a folder of mods copied as a whole,
// "<group>/<mod>": one slash at most, never "..", nothing that splits the list.
static bool ModFolderValid(const char *folder) {
  const char *slash = strchr(folder, '/');
  return folder[0] && folder[0] != '.' && folder[0] != '/' &&
         strlen(folder) < sizeof(((ModEntry *)0)->folder) &&
         !strstr(folder, "..") && !strchr(folder, ',') &&
         (!slash || (slash[1] && slash[1] != '.' && !strchr(slash + 1, '/')));
}

// Reads <base>/<folder>/mod.ini: Name, Category, Info, Info2... Info7 (Name
// and the Info lines may have a version for one language, "Info.fr-c"; the
// menu shows the Info lines as the mod's description) and a line
// per language naming its file. False when the mod has no file for
// `language`.
static bool ReadModManifest(const char *base, const char *folder,
                            const char *language, ModEntry *mod) {
  char path[400];
  snprintf(path, sizeof(path), "%s/%s/mod.ini", base, folder);
  FILE *file = fopen(path, "rb");
  if (!file)
    return false;
  memset(mod, 0, sizeof(*mod));
  mod->base = base;
  snprintf(mod->folder, sizeof(mod->folder), "%s", folder);
  enum { kTexts = 1 + kModInfoLines };
  static const char *const kTextKeys[kTexts] = {
    "Name", "Info", "Info2", "Info3", "Info4", "Info5", "Info6", "Info7",
  };
  char *texts[kTexts] = { mod->name };
  size_t sizes[kTexts] = { sizeof(mod->name) };
  for (int i = 1; i < kTexts; i++) {
    texts[i] = mod->info[i - 1];
    sizes[i] = sizeof(mod->info[i - 1]);
  }
  bool localized[kTexts] = { false };
  // A description for the language replaces the default one whole: the
  // default's extra lines would not belong to it.
  bool info_localized = false;
  char line[256];
  while (fgets(line, sizeof(line), file)) {
    char *text = Trim(line);
    char *equals = strchr(text, '=');
    if (text[0] == '#' || !equals)
      continue;
    *equals = 0;
    char *key = Trim(text), *value = Trim(equals + 1);
    char *dot = strchr(key, '.');
    if (dot)
      *dot = 0;
    for (int i = 0; i < kTexts; i++) {
      if (strcasecmp(key, kTextKeys[i]) != 0)
        continue;
      if (dot && strcasecmp(dot + 1, language) == 0) {
        if (i > 0 && !info_localized) {
          for (int j = 1; j < kTexts; j++)
            if (!localized[j])
              texts[j][0] = 0;
          info_localized = true;
        }
        snprintf(texts[i], sizes[i], "%s", value);
        localized[i] = true;
      } else if (!dot && !localized[i] && !(i > 0 && info_localized)) {
        snprintf(texts[i], sizes[i], "%s", value);
      }
    }
    if (!dot && strcasecmp(key, "Category") == 0)
      snprintf(mod->category, sizeof(mod->category), "%s", value);
    if (!dot && strcasecmp(key, language) == 0)
      snprintf(mod->file, sizeof(mod->file), "%s", value);
  }
  fclose(file);
  if (!mod->name[0])
    snprintf(mod->name, sizeof(mod->name), "%.*s", (int)sizeof(mod->name) - 1,
             folder);
  if (!mod->category[0])
    snprintf(mod->category, sizeof(mod->category), "OTHER");
  // The file stays in the mod's folder, and a comma would split the list.
  bool exists=false;snprintf(path,sizeof(path),"%s/%s/%s",base,folder,mod->file);
  return mod->file[0] && !strchr(mod->file, '/') && !strchr(mod->file, ',') &&
         strcmp(mod->file, "..") != 0 && ModFolderValid(folder)&&FileStore_Exists(path,&exists)&&exists;
}

// The UTF-16 text at p, up to its end or `end`, as UTF-8 that fits `out`.
// Returns what follows its terminator, NULL if it has none before `end`.
static const uint8_t *ZsprText(const uint8_t *p,const uint8_t *end,char *out,size_t size) {
  size_t used=0;out[0]=0;
  for(;p+2<=end;p+=2) {
    unsigned c=p[0]|p[1]<<8;
    if(!c)return p+2;
    if(c<32||(c>=0xd800&&c<0xe000))c='?';
    int n=c<0x80?1:c<0x800?2:3;
    if(used+n>=size)continue;
    if(n==1)out[used]=(char)c;
    else if(n==2){out[used]=(char)(0xc0|c>>6);out[used+1]=(char)(0x80|(c&63));}
    else{out[used]=(char)(0xe0|c>>12);out[used+1]=(char)(0x80|((c>>6)&63));out[used+2]=(char)(0x80|(c&63));}
    used+=n;out[used]=0;
  }
  return NULL;
}

// A ZSPR needs nothing but its own file: its name and author come from inside.
// Only its header is read here, as a folder can hold hundreds of them.
static bool ReadLooseZspr(const char *base,const char *folder,const char *name,size_t len,ModEntry *mod) {
  char path[400];snprintf(path,sizeof(path),"%s/%s",base,folder);
  FILE *file=fopen(path,"rb");if(!file)return false;
  uint8_t head[512];size_t got=fread(head,1,sizeof(head),file);
  long length=-1;if(fseek(file,0,SEEK_END)==0)length=ftell(file);
  fclose(file);
  uint32_t pixels,palette;
  if(length<0||!Mods_ZsprLayout(head,got,(size_t)length,&pixels,&palette))return false;
  memset(mod,0,sizeof(*mod));mod->base=base;mod->loose=true;
  snprintf(mod->folder,sizeof(mod->folder),"%s",folder);snprintf(mod->file,sizeof(mod->file),"%s",name);
  snprintf(mod->category,sizeof(mod->category),"SKIN");
  const uint8_t *end=head+(pixels<got?pixels:got);
  const uint8_t *next=ZsprText(head+kZsprHeaderSize,end,mod->name,sizeof(mod->name));
  if(next)ZsprText(next,end,mod->info[0],sizeof(mod->info[0]));
  if(!mod->name[0]) {
    snprintf(mod->name,sizeof(mod->name),"%.*s",(int)(len-5),name);
    for(char *c=mod->name;*c;c++)if(*c=='_'||*c=='-')*c=' ';
  }
  return true;
}

// Loose native skin files need no manifest. Restrict auto-classification to
// Link graphics/palette entries; story and quest mods retain explicit manifests
// and their save isolation. Validation here never applies the file to the game.
static bool ReadLooseSkin(const char *base,const char *folder,ModEntry *mod) {
  const char *name=strrchr(folder,'/');name=name?name+1:folder;
  size_t len=strlen(name);
  if(len>5&&len<sizeof(mod->file)&&!strcasecmp(name+len-5,".zspr"))return ReadLooseZspr(base,folder,name,len,mod);
  if(len<7||strcasecmp(name+len-6,".z3mod")||len>=sizeof(mod->file))return false;
  char path[400];snprintf(path,sizeof(path),"%s/%s",base,folder);
  FILE *file=fopen(path,"rb");if(!file)return false;
  uint8_t header[64]={0};bool valid=fread(header,1,64,file)==64;
  uint32_t version=0,count=0;memcpy(&version,header+8,4);memcpy(&count,header+12,4);
  static const uint8_t signature[48]={kAssets_Sig};
  valid=valid&&!memcmp(header,"Z3MOD\r\n\x1a",8)&&!memcmp(header+16,signature,48)&&
    (version==1||version==2)&&count>0&&count<=32;
  long length=-1;if(fseek(file,0,SEEK_END)==0)length=ftell(file);
  size_t entry_size=version==1?8:16,offset=64+count*entry_size;
  valid=valid&&length>=64&&length<=2*1024*1024;
  for(unsigned i=0;valid&&i<count;i++) {
    uint32_t entry[4]={0};valid=fseek(file,64+i*entry_size,SEEK_SET)==0&&fread(entry,1,entry_size,file)==entry_size;
    uint32_t kind=version==1?0:entry[2],size=version==1?entry[1]:entry[3];
    valid=valid&&((kind==0&&entry[0]==57&&size==0x7000)||
                  (version==2&&kind==3&&entry[0]==kModTable_LinkPalette&&size==124));
    offset=(offset+3)&~(size_t)3;
    valid=valid&&offset<=(size_t)length&&size<=(size_t)length-offset;offset+=size;
  }
  fclose(file);if(!valid)return false;
  memset(mod,0,sizeof(*mod));mod->base=base;mod->loose=true;
  snprintf(mod->folder,sizeof(mod->folder),"%s",folder);snprintf(mod->file,sizeof(mod->file),"%s",name);
  snprintf(mod->name,sizeof(mod->name),"%.*s",(int)(len-6),name);snprintf(mod->category,sizeof(mod->category),"SKIN");
  for(char *c=mod->name;*c;c++)if(*c=='_'||*c=='-')*c=' ';
  return true;
}

// The mods in <base>/<mod>/, and one level down in <base>/<group>/<mod>/.
static int ScanModFolder(const char *base, const char *group,
                         const char *language, ModEntry *mods, int count,
                         int max_mods) {
  char path[400], folder[sizeof(mods[0].folder)];
  snprintf(path, sizeof(path), "%s%s%s", base, group ? "/" : "",
           group ? group : "");
  DIR *directory = opendir(path);
  if (!directory)
    return count;
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL && count < max_mods) {
    int n = snprintf(folder, sizeof(folder), "%s%s%s", group ? group : "",
                     group ? "/" : "", entry->d_name);
    if (entry->d_name[0] == '.' || n < 0 || (size_t)n >= sizeof(folder) ||
        !ModFolderValid(folder))
      continue;
    if (ReadLooseSkin(base,folder,&mods[count])||ReadModManifest(base, folder, language, &mods[count]))
      count++;
    else if (!group)
      count = ScanModFolder(base, folder, language, mods, count, max_mods);
  }
  closedir(directory);
  return count;
}

// Native loose skins may also sit directly beside the ROM in Zelda 3DS.
static int ScanLooseSkins(ModEntry *mods,int count,int max_mods) {
  DIR *dir=opendir(kLooseModsDirectory);if(!dir)return count;
  struct dirent *entry;
  while((entry=readdir(dir))&&count<max_mods) {
    if(!ModFolderValid(entry->d_name))continue;
    bool duplicate=false;for(int i=0;i<count;i++)if(!strcmp(mods[i].folder,entry->d_name))duplicate=true;
    if(!duplicate&&ReadLooseSkin(kLooseModsDirectory,entry->d_name,&mods[count]))count++;
  }
  closedir(dir);return count;
}

// The name of a mod's own folder: "<mod>" in "<group>/<mod>".
static const char *ModFolderName(const char *folder) {
  const char *slash = strrchr(folder, '/');
  return slash ? slash + 1 : folder;
}

// The mods for `language`, category by category in the order they apply:
// those of the SD card, then those built into the RomFS that no mod of the
// SD card for `language` replaces (same folder name, in a group or not).
static int ScanMods(const char *language, ModEntry *mods, int max_mods) {
  static ModEntry built_in[kMaxMods];
  int sd = ScanModFolder(kModsDirectory, NULL, language, mods, 0, max_mods);
  sd=ScanLooseSkins(mods,sd,max_mods);
  int n = ScanModFolder(kBundledModsDirectory, NULL, language, built_in, 0,
                        kMaxMods);
  int count = sd;
  for (int i = 0; i < n && count < max_mods; i++) {
    bool replaced = false;
    for (int j = 0; j < sd && !replaced; j++)
      replaced = strcmp(ModFolderName(mods[j].folder),
                        ModFolderName(built_in[i].folder)) == 0;
    if (!replaced)
      mods[count++] = built_in[i];
  }
  qsort(mods, count, sizeof(mods[0]), CompareMods);
  return count;
}

static void LoadModSelection(const char *profile, ModEntry *mods, int count) {
  char path[512], line[128];
  snprintf(path, sizeof(path), "%s/%s", profile, kModSelectionFile);
  if(!FileStore_Recover(path))return;
  FILE *file = fopen(path, "rb");
  if (!file)
    return;
  while (fgets(line, sizeof(line), file)) {
    char *folder = Trim(line);
    for (int i = 0; i < count; i++)
      if (strcmp(mods[i].folder, folder) == 0)
        mods[i].enabled = true;
  }
  fclose(file);
}

static bool SaveModSelection(const char *profile, const ModEntry *mods,
                             int count) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", profile, kModSelectionFile);
  if (!EnsureDirectory(kProfilesDirectory) || !EnsureDirectory(profile))
    return false;
  static char text[kMaxMods*97];size_t used=0;
  for(int i=0;i<count;i++)if(mods[i].enabled) {
    int n=snprintf(text+used,sizeof(text)-used,"%s\n",mods[i].folder);
    if(n<0||(size_t)n>=sizeof(text)-used)return false;used+=n;
  }
  return FileStore_Write(path,text,used);
}

// One mod on per category: turning one on turns off the others of its
// category (and a QUEST the GAME mod, or the other way round), and leaves
// the other categories as they are.
static void ToggleMod(ModEntry *mods, int count, int index) {
  mods[index].enabled = !mods[index].enabled;
  if (!mods[index].enabled)
    return;
  for (int i = 0; i < count; i++)
    if (i != index && ModCategoriesClash(mods[i].category, mods[index].category))
      mods[i].enabled = false;
}

// The files of the active profile's mods, for its language, in the order
// they apply, as the comma separated list the engine's Mods setting takes.
// A folder of mods.txt names the mod the menu lists under it (ScanMods), so
// the game loads the mods the menu shows on. With a QUEST on, the saves go
// to quest-<folder>/ in the profile's save folder. False without mods.txt.
static bool BuildActiveModList(const char *language, char *out,
                               size_t out_size) {
  char path[sizeof(g_active_profile) + sizeof(kModSelectionFile) + 1], line[128];
  snprintf(path, sizeof(path), "%s/%s", g_active_profile, kModSelectionFile);
  if(!FileStore_Recover(path))return false;
  FILE *file = fopen(path, "rb");
  if (!file)
    return false;
  static ModEntry found[kMaxMods], on[kMaxMods];
  int found_count = ScanMods(language, found, kMaxMods);
  int count = 0;
  while (fgets(line, sizeof(line), file) && count < kMaxMods) {
    char *folder = Trim(line);
    if (!folder[0])
      continue;
    int i = 0;
    while (i < found_count && strcmp(found[i].folder, folder) != 0)
      i++;
    if (i < found_count)
      on[count++] = found[i];
    else
      Platform3DS_LogRuntime("Mod left out (not found, or no %s file): %s",
                             language, folder);
  }
  fclose(file);
  qsort(on, count, sizeof(on[0]), CompareMods);
  // mods.txt may have been edited by hand: of mods that cannot be on
  // together, the first one in order stays. A QUEST whose save folder cannot
  // be made stays out, its saves would land on the game's.
  bool kept[kMaxMods];
  g_quest_save_prefix[0] = 0;
  for (int i = 0; i < count; i++) {
    kept[i] = !g_randomizer_seed_key[0] || strcasecmp(on[i].category, "SKIN") == 0;
    for (int j = 0; j < i; j++)
      if (kept[j] && ModCategoriesClash(on[j].category, on[i].category))
        kept[i] = false;
    if (!kept[i] || strcasecmp(on[i].category, "QUEST") != 0)
      continue;
    char dir[sizeof(g_active_save_directory) + sizeof(g_quest_save_prefix)];
    int n = snprintf(g_quest_save_prefix, sizeof(g_quest_save_prefix),
                     "quest-%s/", on[i].folder);
    for (char *c = g_quest_save_prefix + 6; *c && c[1]; c++)
      if (*c == '/')
        *c = '-';
    snprintf(dir, sizeof(dir), "%s/%.*s", g_active_save_directory,
             n > 0 ? n - 1 : 0, g_quest_save_prefix);
    if (n <= 0 || (size_t)n >= sizeof(g_quest_save_prefix) || !EnsureDirectory(dir)) {
      Platform3DS_LogRuntime("Quest left out, no save folder: %s", dir);
      g_quest_save_prefix[0] = 0;
      kept[i] = false;
    } else {
      Platform3DS_LogRuntime("Quest saves: %s", dir);
    }
  }
  size_t used = 0;
  out[0] = 0;
  for (int i = 0; i < count; i++) {
    if (!kept[i]) {
      Platform3DS_LogRuntime("Mod left out: %s (%s)", on[i].folder, on[i].category);
      continue;
    }
    int n = snprintf(out+used,out_size-used,"%s%s/%s%s%s",used?",":"",on[i].base,on[i].folder,
                     on[i].loose?"":"/",on[i].loose?"":on[i].file);
    if (n < 0 || (size_t)n >= out_size - used) {
      out[used] = 0;
      break;
    }
    used += (size_t)n;
    Platform3DS_LogRuntime("Mod on: %s (%s, %s%s)", on[i].folder, on[i].category,
                           on[i].file,
                           on[i].base == kBundledModsDirectory ? ", built in" : "");
  }
  return true;
}

// The menus speak the language of the ROM they are for: French for the
// French Canadian cartridge, English for the others.




// The status lines of the ROM selector, in French.


// The categories of a sorted list: where each one starts, and how many.
static int ModCategories(const ModEntry *mods, int count, int *starts) {
  int n = 0;
  for (int i = 0; i < count; i++)
    if (i == 0 || strcasecmp(mods[i].category, mods[i - 1].category) != 0)
      starts[n++] = i;
  return n;
}

typedef struct ModsMenuContext {
  const ModEntry *mods;
  int count;
  int starts[kMaxMods + 1];  // where each category starts, then count
  int categories;
  int category;  // shown
  int selected;  // in all mods
  const char *language;
  const char *status;
} ModsMenuContext;

// A mod's look: <mod>/preview.bmp, an uncompressed 24- or 32-bit BMP of at
// most 64 x 64 pixels, magenta (255, 0, 255) for the transparent ones. The
// menu shows it left of the list, as large as the 76 pixels there allow, four
// times its size at most. One is kept, the highlighted mod's.
enum { kPreviewMax = 64, kPreviewMaxScale = 4, kPreviewSpace = 76 };
static struct {
  char path[400];
  bool loaded;
  int width, height;
  uint16_t pixels[kPreviewMax * kPreviewMax];
  uint8_t mask[kPreviewMax * kPreviewMax];
} g_mod_preview;

static uint32_t ReadLe(const uint8_t *p, int n) {
  uint32_t v = 0;
  while (n--)
    v = v << 8 | p[n];
  return v;
}

// The forward-facing 16x16 head a ZSPR's own graphics show, for a ZSPR without
// its own BMP. The same one as SS_RenderLinkFace, in the skin's first palette.
static bool LoadZsprPreview(const ModEntry *mod) {
  char path[sizeof(g_mod_preview.path)];
  snprintf(path,sizeof(path),"%s/%s",mod->base,mod->folder);
  FILE *file=fopen(path,"rb");if(!file)return false;
  uint8_t head[kZsprHeaderSize],tiles[18*32],colors[30];
  bool ok=fread(head,1,sizeof(head),file)==sizeof(head);
  long length=-1;if(ok&&fseek(file,0,SEEK_END)==0)length=ftell(file);
  uint32_t pixels,palette;
  ok=ok&&length>=0&&Mods_ZsprLayout(head,sizeof(head),(size_t)length,&pixels,&palette)&&
     fseek(file,pixels,SEEK_SET)==0&&fread(tiles,1,sizeof(tiles),file)==sizeof(tiles)&&
     fseek(file,palette,SEEK_SET)==0&&fread(colors,1,sizeof(colors),file)==sizeof(colors);
  fclose(file);if(!ok)return false;
  for(int y=0;y<16;y++)for(int x=0;x<16;x++) {
    const uint8_t *t=tiles+(x/8+(y/8)*16)*32;int row=y%8,bit=7-x%8;
    int color=((t[row*2]>>bit)&1)|((t[row*2+1]>>bit)&1)<<1|((t[16+row*2]>>bit)&1)<<2|((t[17+row*2]>>bit)&1)<<3;
    int i=y*16+x;g_mod_preview.mask[i]=color!=0;
    if(color) {
      unsigned word=colors[(color-1)*2]|colors[(color-1)*2+1]<<8;
      int r=word&31,g=(word>>5)&31,b=(word>>10)&31;
      g_mod_preview.pixels[i]=SetupRgb565(r<<3|r>>2,g<<3|g>>2,b<<3|b>>2);
    }
  }
  g_mod_preview.width=g_mod_preview.height=16;g_mod_preview.loaded=true;
  return true;
}

static bool LoadModPreview(const ModEntry *mod) {
  char path[sizeof(g_mod_preview.path)];
  const char *dot=strrchr(mod->folder,'.');
  if(mod->loose)snprintf(path,sizeof(path),"%s/%.*s.bmp",mod->base,(int)(dot-mod->folder),mod->folder);
  else snprintf(path, sizeof(path), "%s/%s/preview.bmp", mod->base, mod->folder);
  if (strcmp(path, g_mod_preview.path) == 0)
    return g_mod_preview.loaded;
  snprintf(g_mod_preview.path, sizeof(g_mod_preview.path), "%s", path);
  g_mod_preview.loaded = false;
  FILE *file = fopen(path, "rb");
  if (!file)
    return mod->loose && !strcasecmp(dot, ".zspr") && LoadZsprPreview(mod);
  static uint8_t data[54 + kPreviewMax * kPreviewMax * 4 + 64];
  size_t size = fread(data, 1, sizeof(data), file);
  fclose(file);
  if (size < 54 || data[0] != 'B' || data[1] != 'M')
    return false;
  uint32_t offset = ReadLe(data + 10, 4);
  int32_t width = (int32_t)ReadLe(data + 18, 4);
  int32_t height = (int32_t)ReadLe(data + 22, 4);
  int bpp = (int)ReadLe(data + 28, 2);
  uint32_t compression = ReadLe(data + 30, 4);
  bool top_down = height < 0;
  if (top_down)
    height = -height;
  if ((bpp != 24 && bpp != 32) || (compression != 0 && compression != 3) ||
      width <= 0 || height <= 0 || width > kPreviewMax || height > kPreviewMax)
    return false;
  size_t stride = ((size_t)width * (bpp / 8) + 3) & ~(size_t)3;
  if (offset > size || stride * (size_t)height > size - offset)
    return false;
  for (int y = 0; y < height; y++) {
    const uint8_t *row = data + offset + stride * (top_down ? y : height - 1 - y);
    for (int x = 0; x < width; x++) {
      const uint8_t *px = row + x * (bpp / 8);  // blue, green, red
      int i = y * width + x;
      g_mod_preview.mask[i] = !(px[2] == 255 && px[1] == 0 && px[0] == 255);
      g_mod_preview.pixels[i] = SetupRgb565(px[2], px[1], px[0]);
    }
  }
  g_mod_preview.width = width;
  g_mod_preview.height = height;
  g_mod_preview.loaded = true;
  return true;
}

static void DrawModPreview(const ModEntry *mod) {
  if (!LoadModPreview(mod))
    return;
  int s = kPreviewSpace / g_mod_preview.width;
  if (s > kPreviewMaxScale)
    s = kPreviewMaxScale;
  if (s < 1)
    s = 1;
  int x0 = 300+(88-g_mod_preview.width*s)/2;
  int y0 = 100+(88-g_mod_preview.height*s)/2;
  for (int y = 0; y < g_mod_preview.height; y++)
    for (int x = 0; x < g_mod_preview.width; x++) {
      int i = y * g_mod_preview.width + x;
      if (g_mod_preview.mask[i])
        SetupFillRect(GFX_TOP, x0 + x * s, y0 + y * s, s, s,
                      g_mod_preview.pixels[i]);
    }
}

static void DrawOriginalLinkPreview(void) {
  uint32_t pixels[16*16];if(!SS_RenderOriginalLinkFace(pixels))return;
  for(int y=0;y<16;y++)for(int x=0;x<16;x++) {
    uint32_t c=pixels[y*16+x];if(c>>24)SetupFillRect(GFX_TOP,312+x*4,112+y*4,4,4,SetupRgb565(c>>16,(c>>8)&255,c&255));
  }
}

static void SetupPurpleBackground(bool);
static const char *SetupUiText(int,int);
static void SetupStyledButton(int,int,int,int,bool);
static void SetupMenuText(gfxScreen_t,const char*,int,int,int,uint16_t);
static void SetupMenuLabel(int,int,int,int,const char*,int);
static void SetupTitle(gfxScreen_t,int,int,int,const char*);
static void SetupPanel(gfxScreen_t,int,int,int,int);
static void SetupFooter(int,bool,bool,bool);
static int SetupFooterTouch(void);
static u32 SetupNavigationKeys(u32);
static int SetupWrap(gfxScreen_t,const char*,int,int,int,int,int,uint16_t);
static int ReadChosenLanguage(void);

static void DrawModsMenu(void *context) {
  const ModsMenuContext *menu=context;int language=ReadChosenLanguage();if(language<0)language=0;
  SetupPurpleBackground(false);uint16_t white=SetupRgb565(255,255,255),green=SetupRgb565(120,232,120);
  char title[64];const char *category="";
  int begin=0,end=0;if(menu->count) {
    begin=menu->starts[menu->category];end=menu->starts[menu->category+1];
    category=menu->mods[begin].category;
    if(!strcasecmp(category,"SKIN"))category=SetupUiText(language,kUi_Skins);
  }
  snprintf(title,sizeof(title),"%s%s%s",SetupUiText(language,kUi_Mods),*category?": ":"",category);
  SetupTitle(GFX_TOP,8,264,38,title);
  SetupPanel(GFX_TOP,12,58,276,174);SetupPanel(GFX_TOP,300,100,88,88);
  if(menu->count) {
    int selected=menu->selected<0?0:menu->selected-begin+1,total=end-begin+1;
    int first=NativeListFirst(selected),rows=NativeListVisible(total,first);
    bool original=true;for(int i=begin;i<end;i++)if(menu->mods[i].enabled)original=false;
    for(int row=0;row<rows;row++) {
      int pos=first+row,index=begin+pos-1,y=NativeListRowY(rows,row);
      const char *name=pos?menu->mods[index].name:SetupUiText(language,kUi_Original);
      bool enabled=pos?menu->mods[index].enabled:original;
      if(pos==selected)SetupDrawMaskedRgb565Image(GFX_TOP,22,y-5,kSetupFairyWidth,kSetupFairyHeight,kSetupFairyRgb565,kSetupFairyMask);
      SetupWrap(GFX_TOP,name,46,y,230,1,3,enabled?green:white);
    }
    char pages[16];snprintf(pages,sizeof(pages),"%d / %d",NativeListPage(selected)+1,NativeListPages(total));
    SetupMenuText(GFX_TOP,pages,12+(276-UiFontWidth(pages,3))/2,kNativeListCounterY,3,white);
    if(menu->selected>=0)DrawModPreview(&menu->mods[menu->selected]);
    else DrawOriginalLinkPreview();
  } else SetupWrap(GFX_TOP,SetupUiText(language,kUi_NoMods),46,115,230,2,3,white);
  SetupPanel(GFX_BOTTOM,10,10,300,182);
  if(menu->count&&menu->selected>=0) {
    const ModEntry *mod=&menu->mods[menu->selected];
    int y=25+16*SetupWrap(GFX_BOTTOM,mod->name,22,25,276,2,3,white)+12;
    for(int i=0;i<kModInfoLines&&y<158;i++) {
      double minimum=0;
      if(sscanf(mod->info[i],"Requires v%lf or later",&minimum)==1&&minimum<=4.0)continue;
      if(!mod->info[i][0])continue;
      y+=16*SetupWrap(GFX_BOTTOM,mod->info[i],22,y,276,(164-y)/16,3,white)+3;
    }
  } else SetupWrap(GFX_BOTTOM,SetupUiText(language,menu->count?kUi_Original:kUi_NoMods),22,25,276,3,3,white);
  const char *help=SetupUiText(language,kUi_SelectHelp);
  SetupMenuText(GFX_BOTTOM,help,(320-UiFontWidth(help,3))/2,176,3,white);
  int page=NativeListPage(menu->selected<0?0:menu->selected-begin+1);
  int pages=NativeListPages(end-begin+1);
  SetupFooter(language,menu->categories>1||page>0,menu->categories>1||page+1<pages,true);
  if(menu->status)SetupMenuText(GFX_BOTTOM,SetupUiText(language,kUi_ErrorSdWrite),14,190,1,white);
}

// The mods of one ROM, from the ROM selector (Y), one category at a time:
// L / R (or left / right) change category, A turns the highlighted mod on
// or off and saves the choice to the ROM's profile at once, B goes back.
static void ShowModsMenu(const RomEntry *rom) {
  ModsMenuContext menu;
  memset(&menu, 0, sizeof(menu));
  menu.mods = g_mod_entries;
  menu.language = RomModLanguage(rom->filename);
  menu.count = ScanMods(menu.language, g_mod_entries, kMaxMods);
  menu.categories = ModCategories(g_mod_entries, menu.count, menu.starts);
  menu.starts[menu.categories] = menu.count;
  LoadModSelection(rom->profile, g_mod_entries, menu.count);
  int built_in = 0;
  for (int i = 0; i < menu.count; i++)
    built_in += g_mod_entries[i].base == kBundledModsDirectory;
  LogSetup("Mods menu: %d mod(s), %d built in, in %d categories for %s, %s",
           menu.count, built_in, menu.categories, menu.language, rom->profile);
  menu.selected=-1;
  for(int i=0;i<menu.starts[1];i++)if(menu.mods[i].enabled)menu.selected=i;
  bool redraw = true;u32 blocked=hidKeysHeld();
  while (aptMainLoop()) {
    if (redraw)
      PresentSetupStable(DrawModsMenu, &menu);
    redraw = false;
    hidScanInput();
    blocked&=hidKeysHeld();u32 down=SetupNavigationKeys(hidKeysDown())&~blocked;
    if(down&KEY_TOUCH) {
      int action=SetupFooterTouch();
      if(action==0)down|=KEY_L;else if(action==1)down|=KEY_B;else if(action==2)down|=KEY_A;else if(action==3)down|=KEY_R;
    }
    if (menu.count > 0) {
      int begin = menu.starts[menu.category];
      int end = menu.starts[menu.category + 1];
      if ((down & KEY_DUP) && menu.selected >= begin) {
        menu.selected=menu.selected==begin?-1:menu.selected-1;
        SetupAudioPlayMove();
        redraw = true;
      }
      if ((down & KEY_DDOWN) && (menu.selected<0||menu.selected+1<end)) {
        menu.selected=menu.selected<0?begin:menu.selected+1;
        SetupAudioPlayMove();
        redraw = true;
      }
      int step = (down & (KEY_R | KEY_DRIGHT)) ? 1 :
                 (down & (KEY_L | KEY_DLEFT)) ? -1 : 0;
      if(step) {
        int pos=menu.selected<0?0:menu.selected-begin+1;
        int page=NativeListPage(pos),pages=NativeListPages(end-begin+1);
        if(page+step>=0&&page+step<pages) {
          int target=NativeListStepPage(pos,step,end-begin+1);
          menu.selected=target?begin+target-1:-1;redraw=true;step=0;
        } else if(menu.categories<=1)step=0;
      }
      if (step && menu.categories > 1) {
        menu.category = (menu.category + step + menu.categories) %
                        menu.categories;
        menu.selected=step<0?menu.starts[menu.category+1]-1:-1;
        down&=~(KEY_A|KEY_START);
        SetupAudioPlayMove();
        redraw = true;
      }
      if (down & (KEY_A|KEY_START)) {
        bool previous[kMaxMods];for(int i=0;i<menu.count;i++)previous[i]=g_mod_entries[i].enabled;
        if(menu.selected<0)for(int i=begin;i<end;i++)g_mod_entries[i].enabled=false;
        else if(!g_mod_entries[menu.selected].enabled)ToggleMod(g_mod_entries,menu.count,menu.selected);
        bool saved = SaveModSelection(rom->profile, g_mod_entries, menu.count);
        if(!saved)for(int i=0;i<menu.count;i++)g_mod_entries[i].enabled=previous[i];
        menu.status = saved ? NULL : "SD WRITE ERROR";
        LogSetup("Mod %s: %s%s", menu.selected<0?"original":g_mod_entries[menu.selected].folder,
                 menu.selected<0?"original":g_mod_entries[menu.selected].enabled?"on":"off",
                 saved ? "" : " (not saved)");
        SetupAudioPlayMove();
        redraw = true;
      }
    }
    if (down & (KEY_B | KEY_Y))
      return;
    gspWaitForVBlank();
  }
}


// One migration per ROM profile, including pre-E16 profiles. A separate marker
// is intentional: a new profile may inherit another profile's INI, but must
// not inherit its "already migrated" state. Later menu writes leave it alone.
static bool MigrateWideDefaults(const char *ini) {
  char marker[640], temporary[640], marker_temporary[660], backup[640];
  if (snprintf(marker,sizeof(marker),"%s.wide-defaults-v1",ini)>=(int)sizeof(marker) ||
      snprintf(temporary,sizeof(temporary),"%s.wide-defaults.tmp",ini)>=(int)sizeof(temporary) ||
      snprintf(marker_temporary,sizeof(marker_temporary),"%s.tmp",marker)>=(int)sizeof(marker_temporary) ||
      snprintf(backup,sizeof(backup),"%s.wide-defaults.bak",ini)>=(int)sizeof(backup))
    return ProfileSetupFailure("Settings path too long", ini);
  // Recover a stopped replacement before creating any default INI. Never
  // rename over an existing destination: SD FS and host POSIX differ here.
  if (!IsRegularFile(ini) && IsRegularFile(backup) && rename(backup,ini)!=0)
    return ProfileSetupFailure("Settings recovery failed", backup);
  if (!CopyFileIfMissing(kBundledConfig,ini))
    return ProfileSetupFailure("Settings create failed", ini);
  if (IsRegularFile(marker)) return true;
  FILE *input=fopen(ini,"rb");
  if(!input)return ProfileSetupFailure("Settings read failed", ini);
  FILE *output=fopen(temporary,"wb");
  if(!output){fclose(input);return ProfileSetupFailure("Settings write failed", temporary);}
  char line[1024], parsed[1024];bool general=false,inserted=false,ok=true;
  while(fgets(line,sizeof(line),input)) {
    strcpy(parsed,line);char *text=Trim(parsed);
    if(text[0]=='[') {
      general=strcasecmp(text,"[General]")==0;
      if(general && !inserted) {
        if(fputs("[General]\nDisplayMode = Wide\nWideEdgeMode = FixedCamera\n",output)<0)ok=false;
        inserted=true;continue;
      }
    }
    char *equals=strchr(text,'=');
    if(general && equals) {
      *equals=0;char *key=Trim(text);
      if(!strcasecmp(key,"DisplayMode") || !strcasecmp(key,"WideEdgeMode"))continue;
    }
    if(fputs(line,output)<0)ok=false;
  }
  if(!inserted && fputs("\n[General]\nDisplayMode = Wide\nWideEdgeMode = FixedCamera\n",output)<0)ok=false;
  if(ferror(input))ok=false;
  if(fclose(input)!=0)ok=false;
  if(fclose(output)!=0)ok=false;
  if(!ok){remove(temporary);return ProfileSetupFailure("Settings write failed", ini);}
  // Keep a complete old INI until the new file and migration marker are closed.
  // A restart can recover the backup if promotion was interrupted.
  if(IsRegularFile(backup) && remove(backup)!=0)
    return ProfileSetupFailure("Settings backup failed", backup);
  if(rename(ini,backup)!=0)
    return ProfileSetupFailure("Settings backup failed", ini);
  if(rename(temporary,ini)!=0) {
    ProfileSetupFailure("Settings install failed", ini);
    if(rename(backup,ini)!=0)
      LogSetup("Settings rollback deferred to next boot: %s (errno=%d)",backup,errno);
    return false;
  }
  output=fopen(marker_temporary,"wb");
  if(!output)return ProfileSetupFailure("Settings marker failed", marker_temporary);
  ok=fputs("1\n",output)>=0;
  if(fclose(output)!=0)ok=false;
  if(!ok || rename(marker_temporary,marker)!=0) {
    remove(marker_temporary);
    return ProfileSetupFailure("Settings marker failed", marker);
  }
  if(remove(backup)!=0)
    LogSetup("Settings migrated; backup retained: %s (errno=%d)",backup,errno);
  LogSetup("Applied one-time WIDE/FixedCamera defaults: %s",ini);
  return true;
}

// A French Canadian profile made before its screens were drawn from the
// cartridge has no copy of it, and its dialogue lacks the scroll speed of the
// attract mode's legend. Building it again from the ROM and the installed USA
// one brings both. Without a USA ROM the copy alone still brings the screens
// and the assets already there are kept. Runs in the profile folder.
static void UpgradeLocalizedProfile(const char *rom_path) {
  size_t rom_size = 0, patch_size = 0, assets_size = 0;
  uint8 *rom = ReadWholeFile(rom_path, &rom_size);
  rom = NormalizeRomForExtraction(rom, &rom_size);
  const RomTextLayout *layout = rom ? RomLocalizedLayout(rom, rom_size) : NULL;
  if (!layout || strcmp(layout->language_code, "fr-c") != 0) {
    LogSetup("Localized profile upgrade: not the French Canadian ROM: %s", rom_path);
    free(rom);
    return;
  }
  uint8 *patch = ReadWholeFile(kBundledPatch, &patch_size);
  uint8 *assets = NULL;
  if ((TryBuildStandaloneFrCAssets(rom, rom_size, &assets, &assets_size) ||
       (patch && TryBuildTranslatedAssets(rom, rom_size, patch, patch_size,
                                          &assets, &assets_size))) &&
      WriteAssetsFile(assets, assets_size) &&
      AssetsFileLooksValid(kAssetsFilename)) {
    LogSetup("Localized profile upgrade: rebuilt");
  } else if (!IsRegularFile(LOCALIZED_CART_FILE_FR_C)) {
    LogSetup("Localized profile upgrade: assets kept, cartridge copy: %s",
             WriteLocalizedCartridge(rom, rom_size) ? "OK" : "FAIL");
  }
  free(assets);
  free(patch);
  free(rom);
}

// The language, extractor schema and exact bundled patch that produced the
// cached assets. A patch update must rebuild even when the language is unchanged.
static const char kProfileLanguageFile[] = "assets-language.txt";

static bool LanguageCacheStamp(const GameLanguage *language, char stamp[64]) {
  uint32_t crc = 0;
  if (language->patch) {
    FILE *file = fopen(language->patch, "rb");
    if (!file)
      return false;
    uint8_t magic[4], footer[4];
    bool ok = fread(magic, 1, 4, file) == 4 && !memcmp(magic, "BPS1", 4) &&
              fseek(file, 0, SEEK_END) == 0 && ftell(file) >= 16 &&
              fseek(file, -4, SEEK_END) == 0 && fread(footer, 1, 4, file) == 4;
    fclose(file);
    if (!ok)
      return false;
    crc = (uint32_t)footer[0] | (uint32_t)footer[1] << 8 |
          (uint32_t)footer[2] << 16 | (uint32_t)footer[3] << 24;
  }
  snprintf(stamp, 64, "%s:2:%08lx", language->code, (unsigned long)crc);
  return true;
}

static bool ProfileLanguageMatches(const char *profile, const GameLanguage *language) {
  char path[640], line[64] = "", stamp[64];
  if (!LanguageCacheStamp(language, stamp))
    return false;
  if (language->patch) {
    snprintf(path, sizeof(path), "%s/%s", profile, LANGUAGE_CART_FILE);
    if (!IsRegularFile(path))
      return false;
  }
  snprintf(path, sizeof(path), "%s/%s", profile, kProfileLanguageFile);
  FILE *file = fopen(path, "rb");
  if (file) {
    if (!fgets(line, sizeof(line), file))
      line[0] = 0;
    fclose(file);
  }
  return !strcmp(Trim(line), stamp);
}

// In the profile folder: records the assets' language and makes the game and
// the menus speak it.
static bool ApplyProfileLanguage(void) {
  const GameLanguage *language = g_build_language;
  if (!language)
    return true;
  char stamp[64];
  if (!LanguageCacheStamp(language, stamp))
    return false;
  bool ok = true;
  if (!language->patch)
    remove(LANGUAGE_CART_FILE);
  ok &= IniEdit_SetValue("zelda3.ini", "[General]", "Language",
                         language->patch ? language->code : "");
  ok &= IniEdit_SetValue("zelda3.ini", "[General]", "MenuLanguage",
                         UI_LanguageCode(g_profile_menu_language ? g_profile_menu_language->menu :
                                         language->menu >= 0 ? language->menu : kUiLanguage_English));
  return ok && FileStore_Write(kProfileLanguageFile, stamp, strlen(stamp));
}

static bool EnsureProfileReady(RomEntry *rom, bool force_extract) {
  g_profile_prepare_status = "ROM preparation failed";
  LogSetup("Preparing profile: %s, ROM: %s", rom->profile, rom->filename);

  if (!EnsureDirectory(kProfilesDirectory) || !EnsureDirectory(rom->profile))
    return ProfileSetupFailure("SD profile folder error", rom->profile);
  char profile_assets[512], profile_ini[512];
  snprintf(profile_assets,sizeof(profile_assets),"%s/%s",rom->profile,kAssetsFilename);
  snprintf(profile_ini,sizeof(profile_ini),"%s/zelda3.ini",rom->profile);
  if (!MigrateWideDefaults(profile_ini))
    return false;
  char cwd[512];
  if (!getcwd(cwd, sizeof(cwd)))
    return ProfileSetupFailure("SD directory error", rom->profile);
  if (chdir(rom->profile) != 0)
    return ProfileSetupFailure("SD directory error", rom->profile);
  bool ready = !force_extract && AssetsFileLooksValid(kAssetsFilename);
  if (ready && IniDeclaresLanguage("zelda3.ini", "fr-c") &&
      !IsRegularFile(LOCALIZED_CART_FILE_FR_C)) {
    char rom_path[640];
    snprintf(rom_path, sizeof(rom_path), "%s/%s", kStorageDirectory,
             rom->filename);
    UpgradeLocalizedProfile(rom_path);
    ready = AssetsFileLooksValid(kAssetsFilename);
  }
  if (!ready) {
    char rom_path[640];
    // Absolute on purpose: the SD device joins a relative path onto the
    // working directory without collapsing "..", and the filesystem service
    // rejects what comes out, so "../../rom.sfc" from inside the profile
    // folder never opens (see archive_fixpath in libctru).
    snprintf(rom_path, sizeof(rom_path), "%s/%s", kStorageDirectory,
             rom->filename);
    ready = ExtractAssetsFromRom(rom_path);
  } else {
    LogSetup("Reusing validated profile assets");
  }

  if (ready && !ApplyProfileLanguage())
    ready = ProfileSetupFailure("SD settings write error", rom->profile);
  if (chdir(cwd)!=0)
    return ProfileSetupFailure("SD directory error", cwd);
  if (!ready) return false;

  if (!CopyFileReplacing(profile_assets, kAssetsFilename))
    return ProfileSetupFailure("SD assets copy error", profile_assets);
  /* Only seed zelda3.ini the first time.  Replacing it on every launch silently
   * discarded whatever the user edited in the working directory, because the
   * per-ROM profile ini would overwrite it before config parsing read it back.
   * This matches what MigrateWideDefaults() already does for the same file. */
  if (!CopyFileIfMissing(profile_ini, "zelda3.ini"))
    return ProfileSetupFailure("SD settings copy error", profile_ini);
  // The game reads the cartridge copy in the profile folder
  // (Platform3DS_FormatProfilePath). Earlier builds also copied it here,
  // beside the ROMs, where the ROM list took it for one: that copy goes.
  remove(LOCALIZED_CART_FILE_FR_C);
  if (!PrepareSaveDirectory(rom, false))
    return ProfileSetupFailure("SD save folder error", rom->profile);
  WriteSelectedRom(rom);
  snprintf(g_active_rom_path, sizeof(g_active_rom_path), "%s/%s",
           kStorageDirectory, rom->filename);
  return true;
}

// Persist an explicit menu choice or the supported console language on first boot.
static const char kLanguageFile[] = "language.ini";

static int ReadChosenLanguage(void) {
  char line[16] = "";
  FILE *file = fopen(kLanguageFile, "rb");
  if (!file)
    return -1;
  if (!fgets(line, sizeof(line), file))
    line[0] = 0;
  fclose(file);
  for (int i = 0; i < kGameLanguageCount; i++)
    if (!strcmp(Trim(line), kGameLanguages[i].code))
      return i;
  return -1;
}

static bool WriteChosenLanguage(int language) {
  const char *code = kGameLanguages[language].code;
  return FileStore_Write(kLanguageFile, code, strlen(code));
}

// Unsupported console languages fall back to English.
static int LanguageFromSystem(void) {
  u8 language = CFG_LANGUAGE_EN;
  if (R_SUCCEEDED(cfguInit())) {
    if (R_FAILED(CFGU_GetSystemLanguage(&language)))
      language = CFG_LANGUAGE_EN;
    cfguExit();
  }
  static const struct { u8 system; const char *code; } kMap[] = {
    {CFG_LANGUAGE_ES, "es"}, {CFG_LANGUAGE_PT, "pt"}, {CFG_LANGUAGE_IT, "it"},
    {CFG_LANGUAGE_FR, "fr"}, {CFG_LANGUAGE_DE, "de"}, {CFG_LANGUAGE_RU, "ru"},
    {CFG_LANGUAGE_JP, "ja"},
  };
  for (size_t i = 0; i < countof(kMap); i++)
    if (kMap[i].system == language)
      for (int j = 0; j < kGameLanguageCount; j++)
        if (!strcmp(kGameLanguages[j].code, kMap[i].code))
          return j;
  return 0;
}

// True when the setup font draws every letter of the text.
static bool SetupCanDraw(const char *text) {
  while (text && *text) {
    uint32_t c = SetupNextChar(&text);
    if (c == ' ' || UiFontJapaneseIndex(c) >= 0)
      continue;
    bool drawn = false;
    for (int row = -2; row < 9 && !drawn; row++)
      drawn = SetupGlyph(c, row) != 0;
    if (!drawn)
      return false;
  }
  return true;
}

static const char *LanguageText(int language, int id) {
  const char *text = kLanguageSelectorText[language][id];
  return SetupCanDraw(text) ? text : kLanguageSelectorText[0][id];
}

static bool g_startup_menu_seen,g_startup_menu_pending,g_startup_intro_seen,g_startup_reopen_once;
static bool g_startup_card_pending;

// Return to the full startup home without forcing language selection or
// leaving the current ordinary/Randomizer save namespace.
void Platform3DS_RequestStartupMenu(void) {
  g_startup_reopen_once=true;
  Platform3DS_RequestReset();
}


static void SetupPurpleBackground(bool language_frame) {
  for(int y=0;y<240;y++) {
    for(int x=0;x<400;x++)SetupPutPixel(GFX_TOP,x,y,kSetupPattern[y*400+x]);
    for(int x=0;x<320;x++)SetupPutPixel(GFX_BOTTOM,x,y,kSetupPattern[y*400+x+40]);
  }
}

static const char *SetupUiText(int language,int id) {
  int previous=UI_GetLanguage();
  UI_SetLanguage(kGameLanguages[language].menu);
  const char *text=UI_Text(id);
  UI_SetLanguage(previous);
  return text;
}

static void SetupMenuSpan(void *context,int x,int y,int w,int h,uint32_t color) {
  SetupFillRect((gfxScreen_t)(uintptr_t)context,x,y,w,h,
    SetupRgb565((color>>16)&255,(color>>8)&255,color&255));
}
static void SetupStyledButton(int x,int y,int w,int h,bool selected) {
  NativeMenuButton(SetupMenuSpan,(void*)(uintptr_t)GFX_BOTTOM,x,y,w,h,.5f,selected);
}
// Same authored glyphs, spacing and ink centering as the normal bottom UI.
static void SetupMenuText(gfxScreen_t target,const char *text,int x,int y,int scale,uint16_t color) {
  unsigned code;
  while((code=UiFontNextCode(&text))) {
    int rows=UiFontGlyphRows(code,scale),width=UiFontGlyphColumns(code,scale);
    for(int yy=0;yy<rows;yy++)for(int xx=0;xx<width;xx++) {
      unsigned bits=UiFontGlyphBits(code,scale,yy);
      if(bits&(1u<<(width-1-xx)))SetupPutPixel(target,x+xx,y+UiFontGlyphTop(code,scale)+yy,color);
    }
    x+=UiFontAdvance(code,scale);
  }
}
static void SetupMenuLabel(int x,int y,int w,int h,const char *label,int scale) {
  SetupMenuText(GFX_BOTTOM,label,x+(w-UiFontWidth(label,scale))/2,
    UiFontCenteredY(label,scale,y,h),scale,SetupRgb565(255,255,255));
}
static void SetupFooter(int language,bool previous,bool next,bool back) {
  const char *labels[]={"<",SetupUiText(language,back?kUi_Back:kUi_Quit),SetupUiText(language,kUi_Select),">"};
  for(int i=0;i<4;i++) {
    int x=kNativeMenuFooterX[i],w=kNativeMenuFooterW[i];SetupStyledButton(x,202,w,28,i==2);
    if((i==0&&!previous)||(i==3&&!next))SetupMenuText(GFX_BOTTOM,labels[i],x+(w-UiFontWidth(labels[i],3))/2,UiFontCenteredY(labels[i],3,202,28),3,SetupRgb565(112,112,112));
    else SetupMenuLabel(x,202,w,28,labels[i],3);
  }
}
static int SetupFooterTouch(void) {
  touchPosition pt;hidTouchRead(&pt);if(pt.py<202||pt.py>=230)return -1;
  for(int i=0;i<4;i++)if(pt.px>=kNativeMenuFooterX[i]&&pt.px<kNativeMenuFooterX[i]+kNativeMenuFooterW[i])return i;
  return -1;
}
static u32 SetupNavigationKeys(u32 down) {
  circlePosition circle;hidCircleRead(&circle);u32 direction=0;
  if(abs(circle.dx)>abs(circle.dy)){if(circle.dx>40)direction=KEY_DRIGHT;else if(circle.dx<-40)direction=KEY_DLEFT;}
  else {if(circle.dy>40)direction=KEY_DUP;else if(circle.dy<-40)direction=KEY_DDOWN;}
  direction|=hidKeysHeld()&(KEY_DUP|KEY_DDOWN|KEY_DLEFT|KEY_DRIGHT);
  static u32 previous;static uint64_t repeat_at;uint64_t now=osGetTime();
  if(direction!=previous){down|=direction&~previous;repeat_at=now+300;}
  else if(direction&&now>=repeat_at){down|=direction;repeat_at=now+120;}
  previous=direction;return down;
}
static void SetupMenuButton(int row,int count,const char *label,bool selected) {
  int step=count>=5?38:44,h=count>=5?32:36,y=(240-count*step+step-h)/2+row*step;
  SetupStyledButton(30,y,260,h,selected);
  int scale=2;
  SetupMenuText(GFX_BOTTOM,label,38,UiFontCenteredY(label,scale,y,h),scale,SetupRgb565(255,255,255));
  SetupMenuText(GFX_BOTTOM,">",276,UiFontCenteredY(">",1,y,h),1,SetupRgb565(255,255,255));
}

static void SetupPanel(gfxScreen_t target,int x,int y,int w,int h) {
  NativeMenuPanel(SetupMenuSpan,(void*)(uintptr_t)target,x,y,w,h);
}
static void SetupTitle(gfxScreen_t target,int y,int w,int h,const char *title) {
  int screen_w=target==GFX_TOP?400:320;w=264;(void)h;
  NativeMenuPlaque(SetupMenuSpan,(void*)(uintptr_t)target,(screen_w-w)/2,y,w);
  SetupMenuText(target,title,(screen_w-UiFontWidth(title,2))/2,
    UiFontCenteredY(title,2,y+8,24),2,SetupRgb565(255,255,255));
}
static int SetupWrap(gfxScreen_t target,const char *text,int x,int y,int width,int max_lines,int face,uint16_t color) {
  int lines=0;
  while(*text && lines<max_lines) {
    const char *end=text,*space=NULL;char line[256];int length=0;
    while(*end) {
      const char *next=end;UiFontNextCode(&next);int n=next-text;
      if(n>=(int)sizeof(line))break;
      memcpy(line,text,n);line[n]=0;
      if(UiFontWidth(line,face)>width)break;
      space=UiFontBreakAt(end,space);
      end=next;length=n;
    }
    if(*end&&space&&space>text){end=space;length=end-text;}
    else if(*end&&!space){end=UiFontLineEnd(text,end,NULL);length=end-text;}
    if(!length)break;
    memcpy(line,text,length);line[length]=0;
    SetupMenuText(target,line,x,y+lines*(face==2?22:16),face,color);lines++;
    text=UiFontSkipBreak(end);
  }
  return lines;
}
static const uint32_t *g_setup_menu_logo;
static void SetupDrawLogoPixels(const uint32_t *logo,const uint32_t *reference) {
  if(!logo)return;
  int minx=256,miny=180,maxx=-1,maxy=-1;
  for(int y=0;y<180;y++)for(int x=0;x<256;x++)if(reference[y*256+x]&0xffffff) {
    if(x<minx)minx=x;if(x>maxx)maxx=x;if(y<miny)miny=y;if(y>maxy)maxy=y;
  }
  int sw=maxx-minx+1,sh=maxy-miny+1;if(sw<=0||sh<=0)return;
  // A native intermediate composition: nearest samples land on physical pixels.
  int w=sw*3/2,h=sh*3/2,ox=(400-w)/2+20,oy=(240-h)/2-5;
  for(int y=0;y<h;y++)for(int x=0;x<w;x++) {
    uint32_t c=logo[(miny+y*2/3)*256+minx+x*2/3];
    if(c&0xffffff)SetupPutPixel(GFX_TOP,ox+x,oy+y,SetupRgb565(c>>16,(c>>8)&255,c&255));
  }
}
static void SetupDrawLogo(const uint32_t *logo) {SetupDrawLogoPixels(logo,logo);}
static void SetupCreditPixel(int x,int y,uint16_t color,unsigned step) {
  if(x<0||x>=400||y<0||y>=240||!step)return;
  uint16_t bg=g_setup_top_pixels[y*400+x];unsigned inv=18-step;
  uint16_t blended=((((color>>11)*step+(bg>>11)*inv)/18)<<11)|
    (((((color>>5)&63)*step+((bg>>5)&63)*inv)/18)<<5)|
    (((color&31)*step+(bg&31)*inv)/18);
  SetupPutPixel(GFX_TOP,x,y,blended);
}
static void SetupCreditSpan(void *context,int x,int y,int w,int h,uint32_t color) {
  uint16_t ink=SetupRgb565(color>>16,(color>>8)&255,color&255);
  for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++)
    SetupCreditPixel(xx,yy,ink,(unsigned)(uintptr_t)context);
}
static void SetupDrawStartupCreditsFaded(const uint32_t *logo,unsigned step) {
  int minx=256,maxx=-1,miny=224,maxy=-1;
  for(int y=180;y<224;y++)for(int x=0;x<256;x++)if(logo[y*256+x]&0xffffff){if(x<minx)minx=x;if(x>maxx)maxx=x;if(y<miny)miny=y;if(y>maxy)maxy=y;}
  if(maxx<minx)return;
  int left=220-(maxx-minx+1)/2;
  for(int y=miny;y<=maxy;y++)for(int x=minx;x<=maxx;x++) {
    uint32_t c=logo[y*256+x];if(c&0xffffff)SetupCreditPixel(left+x-minx,190+y-miny,SetupRgb565(c>>16,(c>>8)&255,c&255),step);
  }
  NativeTitleCredit_Draw(440,190+(maxy-miny+1)+1,0xffffffff,0xff520000,SetupCreditSpan,(void*)(uintptr_t)step);
}
static void SetupDrawStartupCredits(const uint32_t *logo) { SetupDrawStartupCreditsFaded(logo,18); }
static bool SetupHasStarted(void) {
  if(IsRegularFile("startup-started.flag"))return true;
  char path[640];Platform3DS_FormatSavePath("saves/sram.dat",path,sizeof(path));
  FILE *f=fopen(path,"rb");if(!f)return false;
  uint8_t data[8192];size_t n=fread(data,1,sizeof(data),f);fclose(f);
  if(n!=sizeof(data))return false;
  for(int copy=0;copy<2;copy++)for(int slot=0;slot<3;slot++) {
    unsigned off=copy*0xf00+slot*0x500,sum=0;
    if(data[off+0x3e5]!=0xaa||data[off+0x3e6]!=0x55)continue;
    for(int i=0;i<0x500;i+=2)sum+=data[off+i]|data[off+i+1]<<8;
    if((sum&65535)==0x5a5a)return true;
  }
  return false;
}
static void SetupMarkStarted(void) {
  const char text[]="Z3START 1\n";FileStore_Write("startup-started.flag",text,sizeof(text)-1);
}

static void DrawOperationProgress(bool dumping,int percent) {
  if(!dumping)return;
  if(percent<0)percent=0;if(percent>100)percent=100;
  for(int y=0;y<240;y++)for(int x=0;x<320;x++)
    SetupPutPixel(GFX_BOTTOM,x,y,kSetupPattern[y*400+x+40]);
  const char *label="Dumping";
  gfxScreen_t target=GFX_BOTTOM;int width=320;
    const int frame_w=264,frame_h=38,frame_x=(width-frame_w)/2,frame_y=108;
    const int inner_x=frame_x+20,inner_y=frame_y+9,inner_w=frame_w-40;
    // Back the entire opening, including the transparent outline pixels above
    // the fill and the cap notches. Theme tiles must never show through here.
    SetupFillRect(target,frame_x+17,frame_y+8,frame_w-34,24,0);
    SetupFillRect(target,inner_x,inner_y,inner_w,20,0);
    int filled=inner_w*percent/100;
    SetupFillRect(target,inner_x,inner_y,filled,20,SetupRgb565(200,160,56));
    if(filled)SetupFillRect(target,inner_x,inner_y,filled,2,SetupRgb565(240,216,112));
    // Nine-slice raster: caps stay unchanged; only an 8px beam segment repeats.
    for(int y=0;y<frame_h;y++)for(int x=0;x<frame_w;x++) {
      int sx=x<24?x:x>=frame_w-24?192-(frame_w-x):24+(x-24)%8;
      unsigned pixel=kOperationPlaque[y*192+sx];
      if(pixel)SetupPutPixel(target,frame_x+x,frame_y+y,kOperationPlaquePalette[pixel]);
    }
    // One unframed native caption, with a one-pixel outline for both themes.
    int label_x=(width-SetupTextWidth(label,2))/2;
    for(int yy=-1;yy<=1;yy++)for(int xx=-1;xx<=1;xx++)
      if(xx||yy)SetupDrawText(target,label,label_x+xx,82+yy,2,0,0);
    SetupDrawText(target,label,label_x,82,2,SetupRgb565(248,240,216),0);

}

void Platform3DS_ShowOperationProgress(bool dumping,int percent) {
  if(!dumping)return;
  if(!g_setup_console_active && !g_gpu_presenter_initialized)return;
  // Dump capture has already finished. Drain the previous GPU presentation
  // before drawing both physical buffers; no worker or graphics teardown.
  if(g_gpu_presenter_initialized) {
    if(g_gpu_frame_active){Platform3DS_EndGpuFrame();g_gpu_frame_active=false;}
    C3D_FrameSync();
  }
  DrawOperationProgress(dumping,percent);
  g_setup_bottom_only=dumping;PresentSetupConsole();PresentSetupConsole();g_setup_bottom_only=false;
}

static int SetupMenuTouch(int count) {
  touchPosition touch;hidTouchRead(&touch);
  int step=count>=5?38:44,h=count>=5?32:36;
  int y0=(240-count*step+step-h)/2;
  if (touch.px<30 || touch.px>=290) return -1;
  int row=((int)touch.py-y0)/step;
  if (touch.py<y0 || row<0 || row>=count || touch.py-y0-row*step>=h) return -1;
  return row;
}

typedef struct SetupQolContext { int language,selected,first; bool error; } SetupQolContext;
static bool g_qol_gameplay_theme;
static void DrawQolListTop(int language,int selected,int first);
static void DrawGameplayQolTop(int language,int selected) {
  g_qol_gameplay_theme=true;
  DrawQolListTop(language,selected,Qol_PageFirst(Qol_Page(selected)));
  g_qol_gameplay_theme=false;
}
static void DrawQolListTop(int language,int selected,int first) {
  if(g_qol_gameplay_theme) {
    for(int y=0;y<240;y++)for(int x=0;x<400;x++) {
      uint32_t c=kSSTexMenu[(y%kSSTexMenu_H)*kSSTexMenu_W+x%kSSTexMenu_W];
      SetupPutPixel(GFX_TOP,x,y,SetupRgb565(c>>16,(c>>8)&255,c&255));
    }
  } else SetupPurpleBackground(false);
  if(g_qol_gameplay_theme) {
    SetupPanel(GFX_TOP,12,8,376,38);
    const char *title=SetupUiText(language,kUi_QolTitle);
    SetupMenuText(GFX_TOP,title,(400-UiFontWidth(title,2))/2,UiFontCenteredY(title,2,8,38),2,SetupRgb565(255,255,255));
  } else SetupTitle(GFX_TOP,8,264,38,SetupUiText(language,kUi_QolTitle));
  SetupPanel(GFX_TOP,12,58,376,174);
  int page=Qol_Page(selected),rows=Qol_PageSize(page);first=Qol_PageFirst(page);
  for(int row=0;row<rows&&first+row<kQolCount;row++) {
    int option=first+row,y=NativeListRowY(rows,row);bool enabled=Qol_Selected(option);
    uint16_t color=SetupRgb565(enabled?120:255,enabled?232:255,enabled?120:255);
    if(g_qol_gameplay_theme) {
      NativeMenuButton(SetupMenuSpan,(void*)(uintptr_t)GFX_TOP,20,y-6,360,22,.5f,option==selected);
      if(option==selected)SetupMenuText(GFX_TOP,">",28,y,3,color);
    } else if(option==selected)SetupDrawMaskedRgb565Image(GFX_TOP,22,y-5,kSetupFairyWidth,kSetupFairyHeight,kSetupFairyRgb565,kSetupFairyMask);
    SetupMenuText(GFX_TOP,SetupUiText(language,kUi_QolFastSwitch+option),46,y,3,color);
    const char *value=SetupUiText(language,enabled?kUi_On:kUi_Off);
    SetupMenuText(GFX_TOP,value,354-UiFontWidth(value,3),y,3,color);
  }
  char page_text[16];snprintf(page_text,sizeof(page_text),"%d / 3",page+1);
  SetupMenuText(GFX_TOP,page_text,(400-UiFontWidth(page_text,3))/2,kNativeListCounterY,3,SetupRgb565(255,255,255));
}
static void DrawSetupQol(void *opaque) {
  SetupQolContext *menu=opaque;DrawQolListTop(menu->language,menu->selected,menu->first);
  uint16_t white=SetupRgb565(255,255,255);
  SetupPanel(GFX_BOTTOM,10,10,300,182);
  SetupWrap(GFX_BOTTOM,SetupUiText(menu->language,kUi_QolDescFastSwitch+menu->selected),22,32,276,7,3,white);
  if(Qol_NeedsRestart(menu->selected)) {
    const char *notice=SetupUiText(menu->language,kUi_QolRestart);
    SetupMenuText(GFX_BOTTOM,notice,(320-UiFontWidth(notice,3))/2,158,3,SetupRgb565(240,216,112));
  }
  SetupFooter(menu->language,Qol_Page(menu->selected)>0,Qol_Page(menu->selected)<2,true);
  if(menu->error)SetupMenuText(GFX_BOTTOM,SetupUiText(menu->language,kUi_ErrorSdWrite),20,190,1,white);
}
static bool ShowSetupQol(int language) {
  SetupQolContext menu={language,0,0,false};bool changed=false,redraw=true;u32 blocked=hidKeysHeld();
  while(aptMainLoop()) {
    if(redraw){PresentSetupStable(DrawSetupQol,&menu);redraw=false;}
    hidScanInput();blocked&=hidKeysHeld();u32 down=SetupNavigationKeys(hidKeysDown())&~blocked;
    int step=0,page_step=0;bool toggle=(down&(KEY_A|KEY_START))!=0;
    if(down&KEY_B)break;
    if(down&KEY_DUP)step=-1;if(down&KEY_DDOWN)step=1;
    if(down&(KEY_L|KEY_DLEFT))page_step=-1;if(down&(KEY_R|KEY_DRIGHT))page_step=1;
    if(down&KEY_TOUCH) {
      int action=SetupFooterTouch();
      if(action==0)page_step=-1;else if(action==1)break;else if(action==2)toggle=true;else if(action==3)page_step=1;
    }
    if(page_step)step=0;
    int old_selection=menu.selected;
    if(step){menu.selected+=step;if(menu.selected<0)menu.selected=0;if(menu.selected>=kQolCount)menu.selected=kQolCount-1;redraw=true;}
    if(page_step){menu.selected=Qol_StepPage(menu.selected,page_step);redraw=true;}
    menu.first=Qol_PageFirst(Qol_Page(menu.selected));
    if(menu.selected!=old_selection)SetupAudioPlayMove();
    if(toggle){menu.error=!Qol_SetOption(menu.selected,!Qol_Selected(menu.selected));changed|=!menu.error;if(!menu.error)SetupAudioPlayMove();redraw=true;}
    gspWaitForVBlank();
  }
  return changed;
}

static void DrawLanguageSelector(int selected,int status) {
  SetupPurpleBackground(false);uint16_t white=SetupRgb565(255,255,255);
  SetupTitle(GFX_TOP,8,264,38,SetupUiText(selected,kUi_Language));
  SetupPanel(GFX_TOP,60,58,280,174);
  int first=NativeListFirst(selected),rows=NativeListVisible(kGameLanguageCount,first);
  for(int row=0;row<rows;row++) {
    int index=first+row,y=NativeListRowY(rows,row);const char *name=kGameLanguages[index].name;
    if(index==selected)SetupDrawMaskedRgb565Image(GFX_TOP,70,y-5,kSetupFairyWidth,kSetupFairyHeight,kSetupFairyRgb565,kSetupFairyMask);
    SetupMenuText(GFX_TOP,name,94,y,3,index==selected?SetupRgb565(120,232,120):white);
  }
  char pages[16];snprintf(pages,sizeof(pages),"%d / %d",NativeListPage(selected)+1,NativeListPages(kGameLanguageCount));
  SetupMenuText(GFX_TOP,pages,(400-UiFontWidth(pages,3))/2,kNativeListCounterY,3,white);
  bool can_back=ReadChosenLanguage()>=0&&status<0;
  SetupPanel(GFX_BOTTOM,10,10,300,182);
  const char *name=kGameLanguages[selected].name;
  SetupMenuText(GFX_BOTTOM,name,(320-UiFontWidth(name,2))/2,28,2,white);
  SetupWrap(GFX_BOTTOM,SetupUiText(selected,kUi_LanguageDescription),24,68,272,6,3,white);
  const char *help=SetupUiText(selected,kUi_SelectHelp);
  SetupMenuText(GFX_BOTTOM,help,(320-UiFontWidth(help,3))/2,174,3,white);
  SetupFooter(selected,first>0,first+rows<kGameLanguageCount,can_back);
  if(status>=0)SetupMenuText(GFX_BOTTOM,LanguageText(selected,status),16,190,1,white);
}

typedef struct LanguageSelectorContext { int selected, status; } LanguageSelectorContext;

static void DrawLanguageSelectorFrame(void *context) {
  LanguageSelectorContext *selector = (LanguageSelectorContext *)context;
  DrawLanguageSelector(selector->selected, selector->status);
}

static void PresentLanguageSelector(int selected, int status) {
  LanguageSelectorContext context = { selected, status };
  PresentSetupStable(DrawLanguageSelectorFrame, &context);
}

// Returns the chosen language, -2 for Back, or -1 for Quit.
// The shared setup audio continues across this page.
static int SelectLanguage(int selected, int status, const RomEntry *rom) {
  int last_presented = -1;
  uint64_t last_blink_step = UINT64_MAX;
  SetupAudioInit();
  int choice = -1;u32 blocked=hidKeysHeld();
  while (aptMainLoop()) {
    uint64_t blink_step = status >= 0 && status != kSel_Preparing ? osGetTime() / 300 : 0;
    if (selected != last_presented || blink_step != last_blink_step) {
      PresentLanguageSelector(selected, status);
      last_presented = selected;
      last_blink_step = blink_step;
      SetupAudioStartMusic();
    }
    hidScanInput();
    u32 held=hidKeysHeld();blocked&=held;u32 down=SetupNavigationKeys(hidKeysDown())&~blocked;
    static bool dump_combo_was_held;
    bool dump_combo = (held & (KEY_L | KEY_R | KEY_A)) == (KEY_L | KEY_R | KEY_A);
    if (dump_combo && !dump_combo_was_held)
      CreateSetupDump("language-selector");
    dump_combo_was_held = dump_combo;
    if ((down & KEY_DUP) && selected > 0) {
      selected--;
      SetupAudioPlayMove();
    }
    if ((down & KEY_DDOWN) && selected + 1 < kGameLanguageCount) {
      selected++;
      SetupAudioPlayMove();
    }
    bool can_back=ReadChosenLanguage()>=0&&status<0;
    int touch=(down&KEY_TOUCH)?SetupFooterTouch():-1;
    int page_step=(down&(KEY_L|KEY_DLEFT))||touch==0?-1:(down&(KEY_R|KEY_DRIGHT))||touch==3?1:0;
    if(page_step){selected=NativeListStepPage(selected,page_step,kGameLanguageCount);SetupAudioPlayMove();}
    if((down&(KEY_A|KEY_START))||touch==2){choice=selected;break;}
    if(can_back&&((down&KEY_B)||touch==1)){choice=-2;break;}
    if(!can_back&&touch==1)break;
    gspWaitForVBlank();
  }
  return choice;
}

typedef struct StartupMenuContext {
  const uint32_t *logo;
  int language,selected;
} StartupMenuContext;

static int g_startup_started_cache=-1;
static void DrawStartupMenu(void *opaque) {
  bool started=g_startup_started_cache<0?SetupHasStarted():g_startup_started_cache!=0;
  StartupMenuContext *menu=opaque;
  SetupPurpleBackground(false);
  g_setup_menu_logo=menu->logo;SetupDrawLogo(menu->logo);SetupDrawStartupCredits(menu->logo);
  static const int labels[]={kUi_Continue,kUi_Language,kUi_Mods,kUi_QolTitle,kUi_Quit};
  for(int i=0;i<5;i++)SetupMenuButton(i,5,SetupUiText(menu->language,i==0&&!started?kUi_Start:labels[i]),i==menu->selected);
}

static uint16_t SetupFadePixel(uint16_t c,unsigned level) {
  return (((c>>11)*level/18)<<11)|(((((c>>5)&63)*level/18))<<5)|(c&31)*level/18;
}
static void PresentSetupTransition(void (*draw)(void *),void *context) {
  static uint16_t top[400*240],bottom[320*240];
  memcpy(top,g_setup_top_pixels,sizeof(top));memcpy(bottom,g_setup_bottom_pixels,sizeof(bottom));
  for(unsigned step=1;step<=18&&aptMainLoop();step++) {
    if(draw)draw(context);
    else {memset(g_setup_top_pixels,0,sizeof(g_setup_top_pixels));memset(g_setup_bottom_pixels,0,sizeof(g_setup_bottom_pixels));}
    for(int i=0;i<400*240;i++)g_setup_top_pixels[i]=SetupFadePixel(top[i],18-step)+SetupFadePixel(g_setup_top_pixels[i],step);
    for(int i=0;i<320*240;i++)g_setup_bottom_pixels[i]=SetupFadePixel(bottom[i],18-step)+SetupFadePixel(g_setup_bottom_pixels[i],step);
    PresentSetupConsole();
  }
  // Retire the second physical buffer with the same final image.
  PresentSetupConsole();
}
static void FadeSetupToBlack(void) {
  g_setup_music_step=1.0f/kSetupMusicFadeInFrames;g_setup_music_target=0.0f;
  PresentSetupTransition(NULL,NULL);
}

typedef struct StartupIntroContext { const uint32_t *reference; uint64_t start; bool skipped; } StartupIntroContext;
static bool PresentStartupIntro(const uint32_t *pixels,unsigned tick,void *opaque) {
  StartupIntroContext *intro=opaque;
  if(!aptMainLoop())return false;
  hidScanInput();if(hidKeysDown()&(KEY_A|KEY_START|KEY_TOUCH)){intro->skipped=true;return false;}
  if(!intro->start)intro->start=osGetTime();
  memset(g_setup_top_pixels,0,sizeof(g_setup_top_pixels));memset(g_setup_bottom_pixels,0,sizeof(g_setup_bottom_pixels));
  SetupDrawLogoPixels(pixels,intro->reference);PresentSetupConsole();
  while(osGetTime()<intro->start+tick*1000/60)gspWaitForVBlank();
  return true;
}
static void ShowStartupIntro(const uint32_t *logo) {
  static uint32_t frame[256*224];StartupIntroContext intro={logo,0,false};
  memset(g_setup_top_pixels,0,sizeof(g_setup_top_pixels));memset(g_setup_bottom_pixels,0,sizeof(g_setup_bottom_pixels));
  PresentSetupConsole();PresentSetupConsole();
  SetupAudioStartMusic();ZeldaAnimateStartupLogoWithAudio(frame,PresentStartupIntro,SetupAudioPlayIntro,&intro);
  // Keep the finished logo opaque. Credits and the purple background fade
  // in together over 18 VBlanks (about 0.3 seconds). Actions appear afterward.
  for(int step=0;step<=18&&aptMainLoop();step++) {
    SetupPurpleBackground(false);
    for(int screen=0;screen<2;screen++) {
      int count=(screen?320:400)*240;uint16_t *pixels=screen?g_setup_bottom_pixels:g_setup_top_pixels;
      for(int i=0;i<count;i++){unsigned c=pixels[i];pixels[i]=(((c>>11)*step/18)<<11)|(((((c>>5)&63)*step/18))<<5)|(c&31)*step/18;}
    }
    SetupDrawLogo(logo);SetupDrawStartupCreditsFaded(logo,step);PresentSetupConsole();
  }
  SetupAudioStopIntro();
}

void Platform3DS_RevealStartupCard(void) {
  if(!g_startup_card_pending||!g_gpu_presenter_initialized)return;
  g_startup_card_pending=false;
  unsigned bpp=g_is_new_3ds?4:2;void *pixels=linearAlloc(512*256*bpp);
  if(!pixels)return;
  StartupCardPixels(pixels,g_is_new_3ds);
  Platform3DS_SetBottomCinema(false);Platform3DS_SetBottomCinema(true);
  uint64_t deadline=osGetTime()+800;bool presented=false;
  while(aptMainLoop()&&osGetTime()<deadline) {
    if(!C3D_FrameBegin(0)){gspWaitForVBlank();continue;}
    g_gpu_frame_active=true;
    Platform3DS_ClearBlackTarget(g_top_target);
    if(!presented)presented=Platform3DS_PresentBottomFrame(pixels,512*bpp,320,240);
    else Platform3DS_DrawBottomTexture();
    Platform3DS_EndFrame();
    if(presented&&!g_bottom_fade_active)break;
    gspWaitForVBlank();
  }
  C3D_FrameSync();linearFree(pixels);
}

bool Platform3DS_StartupMenuPending(void) { return g_startup_menu_pending; }

static const RomEntry *StartupActiveRom(void) {
  for(int i=0;i<(int)countof(g_rom_entries);i++)
    if(!strcmp(g_rom_entries[i].profile,g_active_profile))return &g_rom_entries[i];
  return &g_rom_entries[0];
}

int Platform3DS_ShowStartupMenu(const uint32_t *logo) {
  g_startup_menu_pending=false;
  g_startup_started_cache=SetupHasStarted();
  StartupMenuContext menu={logo,ReadChosenLanguage(),0};
  if(menu.language<0)menu.language=0;
  BeginSetupConsole();SetupAudioInit();g_setup_audio_preserve=false;if(!g_startup_intro_seen){ShowStartupIntro(logo);g_startup_intro_seen=true;}
  bool dirty=false,redraw=true,reveal=true;u32 blocked=hidKeysHeld();
  int result=-1;
  while(aptMainLoop()) {
    if(redraw){if(reveal){PresentSetupTransition(DrawStartupMenu,&menu);reveal=false;}else PresentSetupStable(DrawStartupMenu,&menu);SetupAudioStartMusic();redraw=false;}
    hidScanInput();blocked&=hidKeysHeld();u32 down=SetupNavigationKeys(hidKeysDown())&~blocked;
    if(down&KEY_DUP){menu.selected=(menu.selected+4)%5;SetupAudioPlayMove();redraw=true;}
    if(down&KEY_DDOWN){menu.selected=(menu.selected+1)%5;SetupAudioPlayMove();redraw=true;}
    int action=(down&(KEY_A|KEY_START))?menu.selected:-1;
    if(down&KEY_TOUCH)action=SetupMenuTouch(5);
    if(down&KEY_Y)action=2;
    if(action==0){SetupMarkStarted();FadeSetupToBlack();g_startup_card_pending=true;result=dirty?0:1;break;}
    if(action==4)break;
    if(action==3){dirty|=ShowSetupQol(menu.language);blocked|=hidKeysHeld();redraw=true;}
    if(action==1) {
      int selected=SelectLanguage(menu.language,-1,StartupActiveRom());
      if(selected>=0&&selected!=menu.language) {
        menu.language=selected;
        if(WriteChosenLanguage(selected)){g_startup_reopen_once=true;g_setup_audio_preserve=true;result=0;break;}
      }
      blocked|=hidKeysHeld();SetupAudioInit();redraw=true;
    }
    if(action==2) {
      ShowModsMenu(StartupActiveRom());blocked|=hidKeysHeld();dirty=true;redraw=true;
    }
    gspWaitForVBlank();
  }
  if(!g_setup_audio_preserve)SetupAudioFadeOutAndStop();EndSetupConsole();
  g_startup_started_cache=-1;
  if(result==0)Platform3DS_RequestReset();
  return result;
}

// The USA cartridge every language is built from: the first ROM the bundled
// assets patch accepts.
static int FindUsaRom(RomEntry *roms, int rom_count) {
  for (int i = 0; i < rom_count; i++)
    if (RomUsesBundledAssetsPatch(&roms[i]))
      return i;
  return -1;
}

// This recovery view requires no cartridge assets or engine initialization.
static void DrawMissingRomFrame(void *context) {
  unsigned step=*(unsigned*)context;
  SetupPurpleBackground(false);
  for(int y=0;y<84;y++)for(int x=0;x<96;x++) {
    uint32_t c=kNativeUiTriforceSprite[(y/2)*48+x/2];
    if(c>>24)SetupPutPixel(GFX_TOP,152+x,53+y+(18-step),SetupRgb565(c>>16,(c>>8)&255,c&255));
  }
  const char *title="ZELDA 3DS";
  SetupMenuText(GFX_TOP,title,(400-UiFontWidth(title,2))/2,171,2,SetupRgb565(248,224,128));
  SetupFillRect(GFX_BOTTOM,12,48,296,132,0);
  SetupMenuLabel(12,55,296,30,"ROM NOT FOUND",2);
  SetupMenuLabel(12,96,296,20,"Please place ROM in:",3);
  SetupMenuLabel(12,123,296,20,"sdmc:/3ds/Zelda 3DS/",3);
  SetupMenuLabel(12,151,296,20,"USA .sfc or .smc",1);
  SetupMenuLabel(12,204,296,22,"A: RETRY   B: EXIT",1);
  for(int i=0;i<400*240;i++)g_setup_top_pixels[i]=SetupFadePixel(g_setup_top_pixels[i],step);
  for(int i=0;i<320*240;i++)g_setup_bottom_pixels[i]=SetupFadePixel(g_setup_bottom_pixels[i],step);
}
static bool ShowMissingRom(void) {
  for(unsigned step=0;step<=18&&aptMainLoop();step++) {
    DrawMissingRomFrame(&step);PresentSetupConsole();gspWaitForVBlank();
  }
  u32 blocked=hidKeysHeld();uint64_t retry_at=osGetTime()+1000;
  while(aptMainLoop()) {
    hidScanInput();blocked&=hidKeysHeld();u32 down=hidKeysDown()&~blocked;
    if(down&KEY_B)return false;
    if((down&(KEY_A|KEY_START))||osGetTime()>=retry_at) {
      if(FindUsaRom(g_rom_entries,ScanRoms(g_rom_entries,countof(g_rom_entries)))>=0) {
        PresentSetupTransition(NULL,NULL);return true;
      }
      retry_at=osGetTime()+1000;
    }
    gspWaitForVBlank();
  }
  return false;
}

static bool ResolveActiveProfile(char *profile, size_t profile_size) {
  bool force_selector = IsRegularFile(kForceSelectorFile);
  if (force_selector)
    remove(kForceSelectorFile);

  RomEntry *roms = g_rom_entries;
  int rom_count = ScanRoms(roms, countof(g_rom_entries));
  int usa = FindUsaRom(roms, rom_count);
  if(usa<0) {
    if(!ShowMissingRom())return false;
    rom_count=ScanRoms(roms,countof(g_rom_entries));usa=FindUsaRom(roms,rom_count);
    if(usa<0)return false;
  }
  int language = ReadChosenLanguage();
  bool ask = force_selector;
  g_startup_menu_pending=!g_startup_menu_seen||g_startup_reopen_once;g_startup_reopen_once=false;
  g_startup_menu_seen = true;
  int status = -1;
  if (language < 0)
    language = LanguageFromSystem();
  while (aptMainLoop()) {
    if (ask || status >= 0) {
      language = SelectLanguage(language, status, usa >= 0 ? &roms[usa] : NULL);
      if(language==-2)language=ReadChosenLanguage();
      if (language < 0)
        return false;
    }
    ask = true;
    if (usa < 0) {
      rom_count = ScanRoms(roms, countof(g_rom_entries));
      usa = FindUsaRom(roms, rom_count);
      if (usa < 0) {
        if(!ShowMissingRom())return false;
        status=-1;ask=false;continue;
      }
    }
    RomEntry *rom = &roms[usa];
    char cached[640];snprintf(cached,sizeof(cached),"%s/%s",rom->profile,kAssetsFilename);

    // Builds before profiles kept their assets beside the ROMs.
    if (!IsRegularFile(kSelectedRomFile) && AssetsFileLooksValid(kAssetsFilename) &&
        !MigrateLegacyStorage(rom)) {
      status = kSel_Failed;
      continue;
    }
    g_profile_menu_language = &kGameLanguages[language];
    bool randomizer_game = g_pending_randomizer_kind && g_pending_randomizer_seed.key[0];
    if (!g_pending_randomizer_kind) {
      char choice_path[640], choice[80] = "";
      snprintf(choice_path, sizeof(choice_path), "%s/randomizer.ini", rom->profile);
      if (FileStore_Recover(choice_path)) {
        FILE *choice_file = fopen(choice_path, "rb");
        if (choice_file) {
          if (!fgets(choice, sizeof(choice), choice_file)) choice[0] = 0;
          fclose(choice_file);
        }
      }
      randomizer_game = RandoSeedKey_Valid(Trim(choice));
    }
    // Reward-dependent native templates use US glyphs/grammar. Keep the
    // selected UI language, but build English gameplay for randomizer sessions.
    // The global language.ini choice remains intact for ordinary play.
    g_build_language = &kGameLanguages[randomizer_game ? 0 : language];
    bool rebuild = !ProfileLanguageMatches(rom->profile, g_build_language);
    if (!EnsureProfileReady(rom, rebuild)) {
      status = !strcmp(g_profile_prepare_status, "Translation unavailable") ?
               kSel_Unavailable : kSel_Failed;
      continue;
    }

    WriteChosenLanguage(language);
    snprintf(profile, profile_size, "%s", rom->profile);
    return true;
  }
  return false;
}

bool Platform3DS_PrepareStorage(void) {
  mkdir("sdmc:/3ds", 0777);
  if (mkdir(kStorageDirectory, 0777) != 0 && errno != EEXIST)
    return false;
  if (chdir(kStorageDirectory) != 0)
    return false;
  Qol_LoadPreferences("qol.ini");
  // The mods folder, made on the first run even when empty: a player sees
  // where mods go without reading about it.
  mkdir(kModsDirectory, 0777);

  remove("setup-progress.txt");
  LogSetup("Zelda 3DS v%s setup started", ZELDA3_3DS_VERSION);
  char profile[512];
  BeginSetupConsole();
  bool profile_ready = ResolveActiveProfile(profile, sizeof(profile));
  EndSetupConsole();
  if (!profile_ready)
    return false;
  snprintf(g_active_profile, sizeof(g_active_profile), "%s", profile);
  char seed_path[640], seed_key[80] = "";
  snprintf(seed_path, sizeof(seed_path), "%s/randomizer.ini", g_active_profile);
  FILE *seed_file = fopen(seed_path, "rb");
  if (seed_file) {
    if (!fgets(seed_key, sizeof(seed_key), seed_file)) seed_key[0] = 0;
    fclose(seed_file);
  }
  const char *selected_seed = g_pending_randomizer_kind ? g_pending_randomizer_seed.key : Trim(seed_key);
  if (!Platform3DS_SelectRandomizerStorage(selected_seed, false))
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
  config->autosave = g_autosave;
  // Preserve the user's Sound/PannedSoundEffects setting (defaulted by the INI).
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
  // PR #31: render 16 extra lines in WIDE instead of stretching 224 lines.
  config->extend_y = g_display_mode == kPlatform3DSDisplayUltraWideMod;
  config->extended_aspect_ratio =
    g_display_mode == kPlatform3DSDisplayUltraWideMod ? 72 : 0;
  config->features0 &= ~(kFeatures0_ExtendScreen64 |
                         kFeatures0_WidescreenVisualFixes);
  if (g_display_mode == kPlatform3DSDisplayUltraWideMod) {
    config->features0 |= kFeatures0_ExtendScreen64 |
                         kFeatures0_WidescreenVisualFixes;
  }
  /* Audio: stereo, with the sample rate chosen by whether MSU is in use.
   *
   * MSU-1 PCM tracks are 44100 Hz 16-bit stereo and the mixer does no rate
   * conversion, so the output must run at 44100 whenever MSU is in play.
   *
   * Cost model: the SPC700 interpreter's work is fixed at 534 samples per frame
   * regardless of the output rate, so raising the rate only adds DSP resampling
   * (534 -> 735 output samples, ~+38% on that one stage).  Old 3DS has the least
   * headroom, so it keeps 32000 Hz unless MSU is actually present; New 3DS has
   * the margin to always run 44100 Hz for the better resampling quality.
   *
   * Detection runs here because cwd is already the app folder
   * (Platform3DS_PrepareStorage chdir'd into it earlier).  It only ADDS MSU when
   * tracks are present: an explicit EnableMSU the user wrote into zelda3.ini is
   * preserved so `deluxe` still works, and a missing probe file never silently
   * disables a configured setup. */
  {
    /* MSUPath is a *prefix*, not a directory: the loader concatenates it with
     * the track number, so "msu/alttp_msu-" + 1 + ".pcm" is how stock MSU packs
     * are addressed (the engine's switch ini documents msu/alttp_msu-*.pcm).
     * Probe the prefixes packs actually ship under, and remember which one hit
     * so the loader opens the same file that detection found. */
    static const char *kMsuPrefixes[] = {
      "msu/alttp_msu-",   /* Snes9x/bsnes naming, what stock packs use */
      "msu/",             /* plain numbers in a subfolder */
      "alttp_msu-",       /* loose files in the working directory */
      "",                 /* bare numbers, legacy layout */
    };
    const char *msu_prefix = NULL;
    for (size_t i = 0; i < sizeof(kMsuPrefixes) / sizeof(kMsuPrefixes[0]); i++) {
      char probe[512];
      snprintf(probe, sizeof(probe), "%s1.pcm", kMsuPrefixes[i]);
      if (IsRegularFile(probe)) {
        msu_prefix = kMsuPrefixes[i];
        break;
      }
    }
    const bool msu_available = msu_prefix != NULL;
    if (msu_available && config->enable_msu == 0) {
      /* No explicit request, but tracks exist: turn plain PCM MSU on. */
      config->enable_msu = kMsuEnabled_Msu;
    }
    if (msu_available && (config->msu_path == NULL || config->msu_path[0] == '\0')) {
      /* No MSUPath in zelda3.ini (older installs predate the key): adopt the
       * prefix the probe actually found, so loading matches detection. */
      config->msu_path = msu_prefix;
    }
    /* Opuz needs 48000 Hz, which this port does not use for MSU (see below), so
     * a leftover opuz flag from a desktop config is dropped rather than played
     * at the wrong rate. */
    if (config->enable_msu & kMsuEnabled_Opuz) {
      config->enable_msu &= (uint8)~kMsuEnabled_Opuz;
      Platform3DS_LogRuntime("Audio: dropping Opuz flag (3DS plays MSU as 44100 Hz PCM)");
    }

    const bool msu_active = config->enable_msu != 0;
    /* Old 3DS without MSU keeps the shipped 32000 Hz rate; everything else runs
     * 44100 Hz so MSU needs no resampling and plain SPC audio gets the slightly
     * better ratio. */
    config->audio_freq = (!msu_active && !g_is_new_3ds) ? 32000 : 44100;
    config->audio_channels = 2;
    config->audio_samples = 1024;
    Platform3DS_LogRuntime("Audio config: %d Hz stereo, MSU %s (enable=0x%02X, tracks %s)",
                           (int)config->audio_freq,
                           msu_active ? "on" : "off",
                           (unsigned)config->enable_msu,
                           msu_available ? "found" : "not found");
  }
  config->disable_frame_delay = true;
  // The mods turned on in the ROM selector for this profile (mods.txt).
  static char mod_list[1024];
  if (BuildActiveModList(config->language && config->language[0] ?
                         config->language : "us", mod_list, sizeof(mod_list)))
    config->mods = mod_list[0] ? mod_list : NULL;
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
  bool ok = DumpState_CreateDirectory("dumps", out, out_size);
  Platform3DS_LogRuntime("Dump session directory: %s", ok ? out : "FAILED");
  return ok;
}

static void ReadDisplayedPixel(const uint8_t *pixel,
                               GSPGPU_FramebufferFormat format,
                               uint8_t *red, uint8_t *green,
                               uint8_t *blue) {
  switch (format) {
  case GSP_RGB565_OES: {
    uint16_t color;
    memcpy(&color, pixel, sizeof(color));
    *red = (uint8_t)(((color >> 11) & 31u) * 255u / 31u);
    *green = (uint8_t)(((color >> 5) & 63u) * 255u / 63u);
    *blue = (uint8_t)((color & 31u) * 255u / 31u);
    break;
  }
  case GSP_BGR8_OES:
    *blue = pixel[0];
    *green = pixel[1];
    *red = pixel[2];
    break;
  case GSP_RGBA8_OES:
    *red = pixel[0];
    *green = pixel[1];
    *blue = pixel[2];
    break;
  case GSP_RGB5_A1_OES: {
    uint16_t color;
    memcpy(&color, pixel, sizeof(color));
    *red = (uint8_t)(((color >> 11) & 31u) * 255u / 31u);
    *green = (uint8_t)(((color >> 6) & 31u) * 255u / 31u);
    *blue = (uint8_t)(((color >> 1) & 31u) * 255u / 31u);
    break;
  }
  case GSP_RGBA4_OES: {
    uint16_t color;
    memcpy(&color, pixel, sizeof(color));
    *red = (uint8_t)(((color >> 12) & 15u) * 17u);
    *green = (uint8_t)(((color >> 8) & 15u) * 17u);
    *blue = (uint8_t)(((color >> 4) & 15u) * 17u);
    break;
  }
  default:
    *red = 0;
    *green = 0;
    *blue = 0;
    break;
  }
}

static bool SaveDisplayedFramebufferBmp(
    const char *path, const GSPGPU_CaptureInfoEntry *capture,
    int width, int height) {
  if (!path || !capture || !capture->framebuf0_vaddr)
    return false;
  GSPGPU_FramebufferFormat format =
    (GSPGPU_FramebufferFormat)(capture->format & 7u);
  unsigned bytes_per_pixel = gspGetBytesPerPixel(format);
  if (bytes_per_pixel < 2 || bytes_per_pixel > 4 ||
      capture->framebuf_widthbytesize == 0)
    return false;

  const uint8_t *framebuffer =
    (const uint8_t *)capture->framebuf0_vaddr;
  size_t framebuffer_size =
    (size_t)capture->framebuf_widthbytesize * (size_t)width;
  GSPGPU_InvalidateDataCache(framebuffer, framebuffer_size);

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
      // 3DS display framebuffers are rotated: each physical X coordinate is
      // one memory row and Y runs in reverse inside that row.
      const uint8_t *pixel =
        framebuffer + (size_t)x * capture->framebuf_widthbytesize +
        (size_t)(height - 1 - y) * bytes_per_pixel;
      uint8_t red, green, blue;
      ReadDisplayedPixel(pixel, format, &red, &green, &blue);
      row[x * 3 + 0] = blue;
      row[x * 3 + 1] = green;
      row[x * 3 + 2] = red;
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

static bool SaveDisplayedFramebufferRaw(
    const char *path, const GSPGPU_CaptureInfoEntry *capture, int width) {
  if (!path)
    return true;
  if (!capture || !capture->framebuf0_vaddr ||
      capture->framebuf_widthbytesize == 0)
    return false;
  size_t size = (size_t)capture->framebuf_widthbytesize * (size_t)width;
  GSPGPU_InvalidateDataCache(capture->framebuf0_vaddr, size);
  return WriteBlob(path, capture->framebuf0_vaddr, size);
}

bool Platform3DS_SaveDisplayedScreensDetailed(
    const char *top_path, const char *bottom_path,
    const char *top_raw_path, const char *bottom_raw_path,
    Platform3DSCaptureStats *stats) {
  if (stats)
    memset(stats, 0, sizeof(*stats));

  GSPGPU_CaptureInfo capture;
  memset(&capture, 0, sizeof(capture));
  Result result = GSPGPU_ImportDisplayCaptureInfo(&capture);
  if (R_FAILED(result)) {
    Platform3DS_LogRuntime(
      "Physical display capture import failed: 0x%08lx",
      (unsigned long)result);
    return false;
  }

  const GSPGPU_CaptureInfoEntry *top =
    &capture.screencapture[GSP_SCREEN_TOP];
  const GSPGPU_CaptureInfoEntry *bottom =
    &capture.screencapture[GSP_SCREEN_BOTTOM];
  if (stats) {
    stats->top_format = top->format & 7u;
    stats->top_stride = top->framebuf_widthbytesize;
    stats->bottom_format = bottom->format & 7u;
    stats->bottom_stride = bottom->framebuf_widthbytesize;
    stats->top_address = (uintptr_t)top->framebuf0_vaddr;
    stats->bottom_address = (uintptr_t)bottom->framebuf0_vaddr;
  }

  bool top_ok = SaveDisplayedFramebufferBmp(top_path, top, 400, 240);
  bool bottom_ok =
    SaveDisplayedFramebufferBmp(bottom_path, bottom, 320, 240);
  bool top_raw_ok = SaveDisplayedFramebufferRaw(top_raw_path, top, 400);
  bool bottom_raw_ok =
    SaveDisplayedFramebufferRaw(bottom_raw_path, bottom, 320);
  bool ok = top_ok && bottom_ok && top_raw_ok && bottom_raw_ok;
  Platform3DS_LogRuntime(
    "Physical screen capture: top=%s bottom=%s top-raw=%s bottom-raw=%s",
    top_ok ? "OK" : "FAILED", bottom_ok ? "OK" : "FAILED",
    top_raw_ok ? "OK" : "FAILED", bottom_raw_ok ? "OK" : "FAILED");
  return ok;
}

static const char *DisplayedFramebufferFormatName(uint32_t format) {
  switch ((GSPGPU_FramebufferFormat)(format & 7u)) {
  case GSP_RGBA8_OES: return "RGBA8";
  case GSP_BGR8_OES: return "BGR8";
  case GSP_RGB565_OES: return "RGB565";
  case GSP_RGB5_A1_OES: return "RGB5A1";
  case GSP_RGBA4_OES: return "RGBA4";
  default: return "unknown";
  }
}

extern void Zelda3_N3DSAudioGetStats(uint32_t values[16]);
extern bool SecondScreenSDL_WriteDiagnostics(const char *directory);

static bool CloseDiagnosticFile(FILE *file) {
  bool ok = !ferror(file);
  return fclose(file) == 0 && ok;
}

static bool WriteExtendedDiagnostics(const char *directory) {
  char path[256];
  uint32_t audio[16];
  Zelda3_N3DSAudioGetStats(audio);
  snprintf(path, sizeof(path), "%s/audio.txt", directory);
  FILE *f = fopen(path, "wb");
  bool ok = f != NULL;
  if (f) {
    fprintf(f, "Audio diagnostic schema: 1\nActive: %lu\n", (unsigned long)audio[0]);
    fprintf(f, "Rate: %lu Hz; samples/buffer: %lu; channels: %lu; buffers: %lu; SDL format: 0x%04lx\n",
            (unsigned long)audio[1], (unsigned long)audio[2], (unsigned long)audio[3],
            (unsigned long)audio[4], (unsigned long)audio[15]);
    fprintf(f, "Queue at capture: queued=%lu playing=%lu free=%lu\n",
            (unsigned long)audio[5], (unsigned long)audio[6], (unsigned long)audio[7]);
    fprintf(f, "Refill samples: %lu; average/last/max wall span: %lu/%lu/%lu us\n",
            (unsigned long)audio[8], (unsigned long)audio[9], (unsigned long)audio[10], (unsigned long)audio[11]);
    fprintf(f, "Empty queue transitions while unpaused: %lu\nWorker priority: 0x%02lx\nCache mode: %lu (1=SVC, 2=DSP fallback)\n",
            (unsigned long)audio[12], (unsigned long)audio[13], (unsigned long)audio[14]);
    fprintf(f, "Refill wall span includes callback work and thread preemption; it is not CPU-only time.\n"
               "Refill instrumentation is Old 3DS only. Queue paused for dump: %d; previously paused: %d. Empty-queue transitions are observations, not an audible-glitch count.\n",
            g_dump_audio_pause_active, g_dump_audio_was_paused);
    ok = CloseDiagnosticFile(f) && ok;
  }

  snprintf(path, sizeof(path), "%s/frame-times.csv", directory);
  f = fopen(path, "wb");
  if (!f) ok = false;
  else {
    fputs("sample,logic_us,ppu_us,present_us,bottom_submit_us,total_work_us,interval_us,logic_scheduled,logic_executed,ppu_main_us,ppu_worker_us,ppu_join_us,split_line,gpu_begin_us,top_clean_transfer_us,gpu_end_us\n", f);
    unsigned first = (g_recent_next + kRecentFrameCount - g_recent_count) % kRecentFrameCount;
    for (unsigned i = 0; i < g_recent_count; i++) {
      const RecentFrameTiming *v = &g_recent_frames[(first + i) % kRecentFrameCount];
      fprintf(f, "%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n", i + 1,
              (unsigned long)v->logic, (unsigned long)v->ppu, (unsigned long)v->present,
              (unsigned long)v->bottom, (unsigned long)v->work, (unsigned long)v->interval,
              (unsigned long)v->scheduled, (unsigned long)v->executed,
              (unsigned long)v->ppu_main, (unsigned long)v->ppu_worker, (unsigned long)v->ppu_join,
              (unsigned long)v->split, (unsigned long)v->gpu_begin,
              (unsigned long)v->top_transfer, (unsigned long)v->gpu_end);
    }
    ok = CloseDiagnosticFile(f) && ok;
  }

  Ppu *p = g_zenv.ppu;
  snprintf(path, sizeof(path), "%s/ppu.txt", directory);
  f = fopen(path, "wb");
  if (!f || !p) { if (f) fclose(f); ok = false; }
  else {
    fputs("PPU diagnostic schema: 1\nPhase: current post-render registers; not a per-scanline HDMA trace.\n"
          "Binary data: little-endian u16 CGRAM/OAM/priority buffers. RAM and VRAM are in ram.bin/vram.bin.\n"
          "Priority buffers describe the final main-thread scanline, not a complete frame.\n", f);
    if (!g_is_new_3ds && PpuGpuOutputActive())
      fputs("Current output is PICA200: CPU priority/sprite caches below are retained diagnostic memory, NOT current rendered pixels.\n", f);
    fprintf(f, "mode=%u brightness=%u forced_blank=%u render_flags=0x%02x pitch=%lu\n",
            p->mode, p->brightness, p->forcedBlank, p->renderFlags, (unsigned long)p->renderPitch);
    fprintf(f, "side_space configured/left/right/bottom=%u/%u/%u/%u obj_x_offset=%d\n",
            p->extraLeftRight, p->extraLeftCur, p->extraRightCur, p->extraBottomCur, p->renderObjXOffset);
    fprintf(f, "wide_visible_column_words=%lu (render-only; raw VRAM retains original streamer data)\n",
            (unsigned long)ZeldaGetWideColumnRepairCount());
    fprintf(f, "TM=%02x TS=%02x TMW=%02x TSW=%02x mosaic_size=%u mosaic_enabled=%02x\n",
            p->screenEnabled[0], p->screenEnabled[1], p->screenWindowed[0], p->screenWindowed[1],
            p->mosaicSize, p->mosaicEnabled);
    fprintf(f, "math_enabled=%02x clip=%u prevent=%u subscreen=%u subtract=%u half=%u fixed_rgb5=%u,%u,%u\n",
            p->mathEnabled, p->clipMode, p->preventMathMode, p->addSubscreen, p->subtractColor,
            p->halfColor, p->fixedColorR, p->fixedColorG, p->fixedColorB);
    fprintf(f, "windowsel=%06lx W1=%u,%u W2=%u,%u extended_window=%u current_ext=%d,%d\n",
            (unsigned long)p->windowsel, p->window1left, p->window1right, p->window2left,
            p->window2right, p->windowExtLeft != NULL, p->windowExtLeftCur, p->windowExtRightCur);
    fprintf(f, "OBJ bases=%04x,%04x size=%u\n", p->objTileAdr1, p->objTileAdr2, p->objSize);
    for (unsigned i = 0; i < 4; i++) {
      const BgLayer *b = &p->bgLayer[i];
      fprintf(f, "BG%u scroll=%u,%u map=%04x tiles=%04x wider=%u higher=%u\n",
              i + 1, b->hScroll, b->vScroll, b->tilemapAdr, b->tileAdr, b->tilemapWider, b->tilemapHigher);
    }
    if ((p->renderFlags & kPpuRenderFlags_Old3DS) && p->spriteLinesValid) {
      unsigned candidates = 0;
      unsigned height = (p->renderFlags & kPpuRenderFlags_Height240) ? 240 : 224;
      for (unsigned line = 0; line < height; line++)
        for (unsigned word = 0; word < 4; word++)
          candidates += __builtin_popcount(p->spriteLines[line][word]);
      fprintf(f, "Old sprite candidates=%u baseline_entries=%u (before X/OBJ limits); backdrop_math_cache=%d\n",
              candidates, height * 128, p->backdropMathValid);
    }
    fputs("Mode7 matrix:", f);
    for (unsigned i = 0; i < 8; i++) fprintf(f, " %d", p->m7matrix[i]);
    fputc('\n', f);
    ZeldaWriteGameDiagnostics(f);
    if (!g_is_new_3ds) PpuGpuWriteDiagnostics(f);
    ok = CloseDiagnosticFile(f) && ok;
    const struct { const char *name; const void *data; size_t size; } blobs[] = {
      {"cgram.bin", p->cgram, sizeof(p->cgram)}, {"oam.bin", p->oam, sizeof(p->oam)},
      {"ppu-main-priority.bin", &p->bgBuffers[0], sizeof(p->bgBuffers[0])},
      {"ppu-sub-priority.bin", &p->bgBuffers[1], sizeof(p->bgBuffers[1])},
    };
    for (unsigned i = 0; i < sizeof(blobs) / sizeof(blobs[0]); i++) {
      snprintf(path, sizeof(path), "%s/%s", directory, blobs[i].name);
      ok = WriteBlob(path, blobs[i].data, blobs[i].size) && ok;
    }
  }
  if (!g_is_new_3ds && PpuGpuOutputActive()) {
    const uint32_t *gpu_pixels = PpuGpuReadback();
    if (gpu_pixels) {
      snprintf(path, sizeof(path), "%s/pica-source.raw", directory);
      ok = WriteBlob(path, gpu_pixels, 512*256*4) && ok;
      snprintf(path, sizeof(path), "%s/pica-source.txt", directory);
      FILE *gpu_info=fopen(path,"wb");
      if(gpu_info) {
        fputs("PICA200 resolved image: 512x256, row 0 at top, little-endian u32 00RRGGBB.\nActive width/height and backend history are in ppu.txt.\nCPU priority buffers/top-source.raw do not describe a GPU frame.\n",gpu_info);
        fclose(gpu_info);
      } else ok=false;
    } else ok = false;
  }
  // The frame source remains owned by the presenter until the next BeginDraw.
  // Dumps run on the game thread before that point, with PPU workers joined.
  if (g_last_top_source) {
    snprintf(path, sizeof(path), "%s/top-source.raw", directory);
    ok = WriteBlob(path, g_last_top_source,
      (size_t)g_last_top_source_pitch * g_last_top_source_height) && ok;
    snprintf(path, sizeof(path), "%s/top-source.txt", directory);
    f = fopen(path, "wb");
    if (!f) ok = false;
    else {
      fprintf(f, "CPU source of the last submitted top frame; linear BGRX8888 (little-endian 0x00RRGGBB).\n"
                 "width=%d height=%d pitch=%d bytes\nPhysical capture may differ by one presentation.\n",
              g_last_top_source_width, g_last_top_source_height, g_last_top_source_pitch);
      ok = CloseDiagnosticFile(f) && ok;
    }
  }
  ok = SecondScreenSDL_WriteDiagnostics(directory) && ok;
  // Snapshot optional context only when present; no ROM/assets are copied.
  const char *context[] = {"runtime.log", "zelda3.ini", "pica-color-probe.raw", "pica-geometry-probe.raw"};
  for (unsigned i = 0; i < countof(context); i++) if ((i < 2 || !g_is_new_3ds) && IsRegularFile(context[i])) {
    snprintf(path, sizeof(path), "%s/%s", directory, context[i]);
    ok = CopyFileReplacing(context[i], path) && ok;
  }
  return ok;
}

bool Platform3DS_DumpMemory(const char *directory,
                            const uint8_t *ram, size_t ram_size,
                            const uint8_t *sram, size_t sram_size,
                            const uint16_t *vram, size_t vram_words,
                            const Platform3DSCaptureStats *capture_stats,
                            bool screens_ok) {
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
  ok = screens_ok && ok;

  char load_state_path[192];
  snprintf(load_state_path, sizeof(load_state_path), "%s/%s", directory,
           ZELDA_DUMP_LOAD_STATE_FILENAME);
  bool load_state_ok = IsRegularFile(load_state_path);

  snprintf(path, sizeof(path), "%s/info.txt", directory);
  FILE *info = fopen(path, "wb");
  if (info) {
    char captured_at[32]; MakeTimestamp(captured_at, sizeof(captured_at));
    fprintf(info, "Zelda 3DS v%s memory dump\n", ZELDA3_3DS_VERSION);
    fprintf(info, "Dump schema: 2; captured at: %s; session: %s\n", captured_at, directory);
    fprintf(info, "Active ROM profile ID: %08lx\n", (unsigned long)g_active_profile_id);
    ZeldaWriteGameDiagnostics(info);
    fprintf(info, "Display/camera/zoom/FPS overlay: %d/%d/%d/%d\n",
            g_display_mode, g_wide_edge_mode, g_wide_zoom_index, g_show_fps);
    fprintf(info, "Linear heap free: %lu bytes; VRAM free: %lu bytes\n",
            (unsigned long)linearSpaceFree(), (unsigned long)vramSpaceFree());
    fprintf(info, "Cache clean mode: %d (1=direct SVC, 2=GX fallback); C2D range: %lu bytes\n",
            g_cache_clean_mode, (unsigned long)g_c2d_flush_size);
    fprintf(info, "Citro3D last completed GPU draw: %.3f ms; CPU processing: %.3f ms; command usage: %.3f\n",
            C3D_GetDrawingTime(), C3D_GetProcessingTime(), C3D_GetCmdBufUsage());
    fprintf(info, "RAM bytes: %lu\n", (unsigned long)ram_size);
    fprintf(info, "SRAM bytes: %lu\n", (unsigned long)sram_size);
    fprintf(info, "VRAM words: %lu\n", (unsigned long)vram_words);
    fprintf(info, "Physical screen capture: %s\n",
            screens_ok ? "complete" : "failed or incomplete");
    fprintf(info, "Top screen capture: 400x240 BMP plus raw framebuffer\n");
    fprintf(info, "Bottom screen capture: 320x240 BMP plus raw framebuffer\n");
    if (capture_stats && capture_stats->top_stride != 0 &&
        capture_stats->bottom_stride != 0) {
      fprintf(info, "Top framebuffer format: %s (%lu)\n",
              DisplayedFramebufferFormatName(capture_stats->top_format),
              (unsigned long)capture_stats->top_format);
      fprintf(info, "Top framebuffer stride: %lu bytes\n",
              (unsigned long)capture_stats->top_stride);
      fprintf(info, "Top framebuffer address: 0x%08lx\n",
              (unsigned long)capture_stats->top_address);
      fprintf(info, "Bottom framebuffer format: %s (%lu)\n",
              DisplayedFramebufferFormatName(capture_stats->bottom_format),
              (unsigned long)capture_stats->bottom_format);
      fprintf(info, "Bottom framebuffer stride: %lu bytes\n",
              (unsigned long)capture_stats->bottom_stride);
      fprintf(info, "Bottom framebuffer address: 0x%08lx\n",
              (unsigned long)capture_stats->bottom_address);
    } else {
      fprintf(info, "Physical framebuffer metadata: unavailable\n");
    }
    fprintf(info, "Load State checkpoint: %s\n",
            load_state_ok ? "load-state.bin (validated)" : "unavailable");
    fprintf(info, "Display mode: %d\n", (int)g_display_mode);
    fprintf(info, "Top presenter: PICA200 RGB565\n");
    if (!g_is_new_3ds) {
      fprintf(info, "Old 3DS PPU: E9 sprite-line masks; backdrop/subscreen palette; packed half-add; ARMv6 opaque spans\n");
      fprintf(info, "Old 3DS opaque UI textures: preconverted RGB565\n");
      fprintf(info, "Recent frame samples: %lu (maximum 120)\n", (unsigned long)g_recent_count);
      if (g_recent_count) {
        fprintf(info, "Recent average PPU draw: %lu us\n", (unsigned long)(g_recent_ppu_us / g_recent_count));
        fprintf(info, "Recent average total frame work: %lu us\n", (unsigned long)(g_recent_work_us / g_recent_count));
        fprintf(info, "Recent work frames over 16.67 ms: %lu/%lu\n",
                (unsigned long)g_recent_over_budget, (unsigned long)g_recent_count);
        fprintf(info, "Recent presentation rate: %.2f Hz\n",
                g_recent_interval_us ? 1000000.0 * g_recent_count / g_recent_interval_us : 0.0);
      }
    }
    fprintf(info, "Hardware policy: %s\n", Platform3DS_GetHardwareProfile()->name);
    fprintf(info, "Top software pixel path: BGRX8888\n");
    fprintf(info, "Frame pacing: 60 Hz high-resolution timer\n");
    fprintf(info, "New 3DS speedup requested: %s\n",
            g_is_new_3ds ? "yes" : "no");
    fprintf(info, "Bottom pixel path: %s\n",
            g_is_new_3ds ? "ARGB8888" : "RGB565");
    fprintf(info, "Bottom periodic cadence: %s\n",
            g_is_new_3ds ? "30 FPS" :
            "event/state driven; 0.33 FPS idle fallback");
    fprintf(info, "Bottom developer overlay refresh: %s\n",
            g_is_new_3ds ? "30 FPS" :
            "on diagnostic change; at most 2.5 FPS");
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
    if (!CloseDiagnosticFile(info))
      ok = false;
  } else {
    ok = false;
  }

  ok = WriteExtendedDiagnostics(directory) && ok;

  Platform3DS_LogRuntime("Memory dump %s: %s", directory,
                         ok ? "OK" : "FAILED");
  return ok;
}


bool Platform3DS_ReadRandomizerChestTable(uint8_t table[504]) {
  if (!table || !g_active_rom_path[0]) return false;
  FILE *file = fopen(g_active_rom_path, "rb");
  if (!file) return false;
  bool ok = fseek(file, 0, SEEK_END) == 0;
  long size = ok ? ftell(file) : -1;
  long header = size > 0 && size % 1024 == 512 ? 512 : 0;
  ok = size >= header + 0xe96e + 504 &&
       fseek(file, header + 0xe96e, SEEK_SET) == 0 &&
       fread(table, 1, 504, file) == 504;
  fclose(file);
  return ok;
}


const char *Platform3DS_RandomizerSeedKey(void) { return g_randomizer_seed_key; }

bool Platform3DS_SelectRandomizerStorage(const char *key, bool persist) {
  const char *input = key ? key : "";
  if (input[0] && !RandoSeedKey_Valid(input)) return false;
  char value[65];
  snprintf(value, sizeof(value), "%s", input);
  char prefix[sizeof(g_randomizer_save_prefix)] = "";
  if (value[0]) {
    char parent[640], folder[768];
    snprintf(parent, sizeof(parent), "%s/randomizer", g_active_save_directory);
    snprintf(folder, sizeof(folder), "%s/%s", parent, value);
    if (!EnsureDirectory(parent) || !EnsureDirectory(folder)) return false;
    snprintf(prefix, sizeof(prefix), "randomizer/%s/", value);
  }
  if (persist) {
    char path[640];
    snprintf(path, sizeof(path), "%s/randomizer.ini", g_active_profile);
    if (!FileStore_Write(path, value, strlen(value))) return false;
  }
  // Change the active in-memory namespace only after all operations succeed.
  snprintf(g_randomizer_seed_key, sizeof(g_randomizer_seed_key), "%s", value);
  snprintf(g_randomizer_save_prefix, sizeof(g_randomizer_save_prefix), "%s", prefix);
  RandoSave_SetSeedStorageActive(value[0] != 0);
  return true;
}


void Platform3DS_RequestRandomizerMenu(void) {
  g_randomizer_menu_requested = true;
  g_randomizer_error[0] = 0;
  g_randomizer_error_code = kRandoError_None;
  Platform3DS_RequestReset();
}

bool Platform3DS_TakeRandomizerMenuRequest(void) {
  bool requested = g_randomizer_menu_requested;
  g_randomizer_menu_requested = false;
  return requested;
}

const char *Platform3DS_RandomizerError(void) { return g_randomizer_error; }
RandoErrorCode Platform3DS_RandomizerErrorCode(void) { return g_randomizer_error_code; }

bool Platform3DS_RequestGeneratedSeed(int preset, uint64_t seed) {
  RandoSettings settings;
  if (preset < 0 || preset >= kPreset__Count ||
      Settings_ApplyPreset((SettingsPreset)preset, &settings) != 0) return false;
  Platform3DSRandomizerSeed request = {.seed = seed, .preset = preset, .origin = 1};
  if (!RandoSeedKey_Create(&settings, seed, kRandoSeedOrigin_Native, NULL, request.key))
    return false;
  g_pending_randomizer_seed = request;
  g_pending_randomizer_kind = 1;
  g_randomizer_error[0] = 0;
  g_randomizer_error_code = kRandoError_None;
  Platform3DS_RequestReset();
  return true;
}

bool Platform3DS_RequestSavedSeed(const char *key) {
  if (!RandoSeedKey_Valid(key)) return false;
  memset(&g_pending_randomizer_seed, 0, sizeof(g_pending_randomizer_seed));
  snprintf(g_pending_randomizer_seed.key, sizeof(g_pending_randomizer_seed.key), "%s", key);
  g_pending_randomizer_kind = 2;
  g_randomizer_error[0] = 0;
  g_randomizer_error_code = kRandoError_None;
  Platform3DS_RequestReset();
  return true;
}

void Platform3DS_RequestOrdinaryGame(void) {
  memset(&g_pending_randomizer_seed, 0, sizeof(g_pending_randomizer_seed));
  g_pending_randomizer_kind = 3;
  g_randomizer_error[0] = 0;
  g_randomizer_error_code = kRandoError_None;
  Platform3DS_RequestReset();
}

static bool ReadRandomizerSeedInfo(const char *folder, Platform3DSRandomizerSeed *out) {
  char path[768]; snprintf(path, sizeof(path), "%s/seed.ini", folder);
  if (!FileStore_Recover(path)) return false;
  FILE *file = fopen(path, "rb");
  if (!file) return false;
  unsigned version, preset;
  unsigned long long seed;
  char label[24]="";
  int fields=fscanf(file, "Z3DS-SEED %u %u %llu %23s", &version, &preset, &seed, label);
  bool ok = fields>=3 && (version==1 || version==2) && preset<kPreset__Count;
  fclose(file);
  if (ok) {
    out->preset=(int)preset; out->seed=(uint64_t)seed; out->origin=version==2?2:1;
    snprintf(out->label,sizeof(out->label),"%s",label);
  }
  return ok;
}

int Platform3DS_ListRandomizerSeeds(Platform3DSRandomizerSeed *out, int capacity) {
  if (!out || capacity <= 0) return 0;
  char root[640]; snprintf(root, sizeof(root), "%s/randomizer", g_active_save_directory);
  DIR *directory = opendir(root);
  if (!directory) return 0;
  struct dirent *entry;
  int count = 0;
  while ((entry = readdir(directory)) && count < capacity) {
    if (!RandoSeedKey_Valid(entry->d_name)) continue;
    char folder[768]; snprintf(folder, sizeof(folder), "%s/%s", root, entry->d_name);
    char playable[800];snprintf(playable,sizeof(playable),"%s/sram.dat",folder);
    if(!IsRegularFile(playable)) {
      snprintf(playable,sizeof(playable),"%s/seed_save.dat",folder);
      if(!IsRegularFile(playable)) continue;
    }
    Platform3DSRandomizerSeed candidate = {0};
    snprintf(candidate.key, sizeof(candidate.key), "%s", entry->d_name);
    if (ReadRandomizerSeedInfo(folder, &candidate)) out[count++] = candidate;
  }
  closedir(directory);
  // Stable ordering independent of the filesystem's readdir order.
  for (int i = 1; i < count; i++) {
    Platform3DSRandomizerSeed value = out[i];
    int j = i;
    while (j > 0 && (out[j-1].preset > value.preset ||
          (out[j-1].preset == value.preset && out[j-1].seed > value.seed))) {
      out[j] = out[j-1]; j--;
    }
    out[j] = value;
  }
  return count;
}

static bool ImportFilenameValid(const char *name) {
  if(!name || !name[0] || strlen(name)>=96 || name[0]=='.') return false;
  size_t n=strlen(name);
  if(n<6 || strcasecmp(name+n-5,".json")) return false;
  for(size_t i=0;i<n;i++) {
    unsigned char c=name[i];
    if(c<32 || c>=127 || c=='/' || c=='\\' || c==':') return false;
  }
  return true;
}

int Platform3DS_ListRandomizerImports(RandoImportFile *out,int capacity) {
  if(!out || capacity<=0 || !EnsureDirectory("randomizer") ||
      !EnsureDirectory("randomizer/imports")) return 0;
  DIR *directory=opendir("randomizer/imports"); if(!directory) return 0;
  int count=0;struct dirent *entry;
  while((entry=readdir(directory)) && count<capacity) {
    if(!ImportFilenameValid(entry->d_name)) continue;
    char path[192];snprintf(path,sizeof(path),"randomizer/imports/%s",entry->d_name);
    if(!IsRegularFile(path)) continue;
    snprintf(out[count++].name,sizeof(out[0].name),"%s",entry->d_name);
  }
  closedir(directory);
  for(int i=1;i<count;i++) {RandoImportFile value=out[i];int j=i;
    while(j>0 && strcmp(out[j-1].name,value.name)>0) {out[j]=out[j-1];j--;}
    out[j]=value;
  }
  return count;
}

bool Platform3DS_RequestImportedSeed(const char *filename) {
  if(!ImportFilenameValid(filename)) {
    g_randomizer_error_code=kRandoError_InvalidFile;
    snprintf(g_randomizer_error,sizeof(g_randomizer_error),"Invalid import filename.");return false;
  }
  snprintf(g_pending_imported_path,sizeof(g_pending_imported_path),"randomizer/imports/%s",filename);
  if(!Alttpr_Load(g_pending_imported_path,&g_pending_imported_seed,
                  g_randomizer_error,sizeof(g_randomizer_error))) {
    g_randomizer_error_code=Alttpr_LastError();
    Platform3DS_LogRuntime("ALttPR import refused: %s",g_randomizer_error);return false;
  }
  Platform3DSRandomizerSeed request={.seed=g_pending_imported_seed.identity_seed,
    .preset=g_pending_imported_seed.settings.world_state==kWorldState_Standard?1:0,.origin=2};
  if(!RandoSeedKey_Create(&g_pending_imported_seed.settings,request.seed,kRandoSeedOrigin_Alttpr,
                          g_pending_imported_seed.placement_digest,request.key)) return false;
  snprintf(request.label,sizeof(request.label),"%s",g_pending_imported_seed.public_hash);
  g_pending_randomizer_seed=request;g_pending_randomizer_kind=4;
  g_randomizer_error[0]=0;g_randomizer_error_code=kRandoError_None;Platform3DS_RequestReset();return true;
}

// The immutable source travels with the seed. Re-parse and bind its exact
// placements/rules on every boot/resume; a missing or modified source is an
// error, never a request to derive native assignments from the public hash.
static bool LoadStoredRandomizerRules(void) {
  if(!g_randomizer_seed_key[0]) {Alttpr_Clear();return true;}
  char folder[512];Platform3DS_FormatSavePath("saves/seed.ini",folder,sizeof(folder));
  char *slash=strrchr(folder,'/');if(!slash) return false;*slash=0;
  Platform3DSRandomizerSeed info={0};
  if(!ReadRandomizerSeedInfo(folder,&info)) {
    Platform3DS_LogRuntime("Randomizer metadata unavailable: %s",folder);return false;
  }
  Platform3DS_LogRuntime("Randomizer storage: origin=%d seed=%llu",info.origin,(unsigned long long)info.seed);
  if(info.origin!=2) {Alttpr_Clear();return true;}
  char path[512];Platform3DS_FormatSavePath("saves/alttpr.json",path,sizeof(path));
  AlttprSeed imported;char key[65];
  if(!Alttpr_Load(path,&imported,g_randomizer_error,sizeof(g_randomizer_error)) ||
     !RandoSeedKey_Create(&imported.settings,imported.identity_seed,kRandoSeedOrigin_Alttpr,
                          imported.placement_digest,key) || strcmp(key,g_randomizer_seed_key)) {
    g_randomizer_error_code=kRandoError_SourceChanged;
    snprintf(g_randomizer_error,sizeof(g_randomizer_error),"Imported seed source is missing or differs from its save identity.");return false;
  }
  Alttpr_Install(&imported);return true;
}

static bool RandomizerSramMatchesSidecar(void) {
  RandoSidecarSlot slots[kRandoSidecar_SlotCount];
  char path[512]; Platform3DS_FormatSavePath("saves/sram_rando.dat", path, sizeof(path));
  if (!RandoSave_ReadFile(path, slots)) return false;
  bool found = false;
  for (int i = 0; i < 3; i++) {
    const uint8 *sram = g_zenv.sram + i * 0x500;
    if ((sram[0x3e5] | (unsigned)sram[0x3e6] << 8) != 0x55aa) continue;
    if (slots[i].header.slot_kind != kSlotKind_Randomizer ||
        !Alttpr_ValidateSlot(&slots[i]) ||
        Rando_DetectChecksumDrift(&slots[i].header, sram, 0x500)) return false;
    found = true;
  }
  return found || RandoSave_InitialSeedReady();
}

bool Platform3DS_ApplyPendingRandomizer(void) {
  int kind = g_pending_randomizer_kind;
  Platform3DS_LogRuntime("Randomizer startup request=%d seed_storage=%s healthy=%d",kind,
                         g_randomizer_seed_key[0]?g_randomizer_seed_key:"ordinary",RandoSave_SeedStorageHealthy());
  if (!RandoSave_SeedStorageHealthy()) {
    g_randomizer_error_code=kRandoError_Storage;
    snprintf(g_randomizer_error, sizeof(g_randomizer_error),
             "Seed save bundle is damaged. Its files were preserved.");
    memset(&g_pending_randomizer_seed, 0, sizeof(g_pending_randomizer_seed));
    g_pending_randomizer_kind = 3;
    g_randomizer_menu_requested = true;
    Platform3DS_RequestReset();
    return false;
  }
  g_pending_randomizer_kind = 0;
  bool ok = true;
  if(!kind) {
    if(LoadStoredRandomizerRules()) return true;
    goto failure;
  }
  if (kind == 1) {
    char sram_path[512], sidecar_path[512], metadata[512];
    Platform3DS_FormatSavePath("saves/sram.dat", sram_path, sizeof(sram_path));
    Platform3DS_FormatSavePath("saves/sram_rando.dat", sidecar_path, sizeof(sidecar_path));
    Platform3DS_FormatSavePath("saves/seed.ini", metadata, sizeof(metadata));
    if (IsRegularFile(sram_path) || IsRegularFile(sidecar_path)) {
      // Re-entering a known seed resumes it. Never regenerate over its progress.
      ok = LoadStoredRandomizerRules() && RandomizerSramMatchesSidecar();
      if (!ok) snprintf(g_randomizer_error, sizeof(g_randomizer_error),
                         "Existing seed save is incomplete or inconsistent.");
    } else {
      RandoSettings settings;
      ok = Settings_ApplyPreset((SettingsPreset)g_pending_randomizer_seed.preset, &settings) == 0;
      char text[96];
      int n = snprintf(text, sizeof(text), "Z3DS-SEED 1 %u %llu\n",
                       (unsigned)g_pending_randomizer_seed.preset,
                       (unsigned long long)g_pending_randomizer_seed.seed);
      ok = ok && n > 0 && (size_t)n < sizeof(text) && FileStore_Write(metadata, text, (size_t)n);
      if (ok) {
        char spoiler_folder[512];
        Platform3DS_FormatSavePath("saves/spoilers", spoiler_folder, sizeof(spoiler_folder));
        ok = EnsureDirectory(spoiler_folder);
        if (ok) g_config.rando_spoiler_dir = Config_InternString(spoiler_folder);
        if (ok) ok = Rando_GenerateSlot(&settings, g_pending_randomizer_seed.seed, 0,
                 0, g_config.features0, NULL, g_randomizer_error, sizeof(g_randomizer_error));
        if (ok) ok = RandoSave_OfferNamedFiles();
      }
    }
  } else if (kind == 2) {
    ok = LoadStoredRandomizerRules() && RandomizerSramMatchesSidecar();
    if (!ok) snprintf(g_randomizer_error, sizeof(g_randomizer_error),
                       "Seed save is missing or inconsistent.");
  }
  if(kind==4) {
    char sram_path[512],sidecar_path[512],metadata[512],source[512];
    Platform3DS_FormatSavePath("saves/sram.dat",sram_path,sizeof(sram_path));
    Platform3DS_FormatSavePath("saves/sram_rando.dat",sidecar_path,sizeof(sidecar_path));
    Platform3DS_FormatSavePath("saves/seed.ini",metadata,sizeof(metadata));
    Platform3DS_FormatSavePath("saves/alttpr.json",source,sizeof(source));
    if(IsRegularFile(sram_path)||IsRegularFile(sidecar_path)) {
      ok=LoadStoredRandomizerRules() && RandomizerSramMatchesSidecar();
    } else {
      // Verify the input again after session shutdown, before publishing it.
      AlttprSeed checked;
      ok=Alttpr_Load(g_pending_imported_path,&checked,g_randomizer_error,sizeof(g_randomizer_error)) &&
         !memcmp(checked.source_digest,g_pending_imported_seed.source_digest,32);
      char text[160];int n=snprintf(text,sizeof(text),"Z3DS-SEED 2 %u %llu %s\n",
        (unsigned)g_pending_randomizer_seed.preset,(unsigned long long)g_pending_randomizer_seed.seed,
        g_pending_randomizer_seed.label[0]?g_pending_randomizer_seed.label:"ALttPR");
      ok=ok && n>0 && n<(int)sizeof(text) && FileStore_Write(metadata,text,(size_t)n) &&
         FileStore_Copy(g_pending_imported_path,source);
      if(ok) {
        ok=Alttpr_CreateSlot(&checked,0,g_randomizer_error,sizeof(g_randomizer_error));
        if(ok) ok=RandoSave_OfferNamedFiles();
        if(!ok) g_randomizer_error_code=Alttpr_LastError();
      }
    }
  }
  if (ok) ok = Platform3DS_SelectRandomizerStorage(g_pending_randomizer_seed.key, true);
  if (ok) ok = LoadStoredRandomizerRules();
failure:
  if (!ok || !kind) {
    if(g_randomizer_error_code==kRandoError_None) g_randomizer_error_code=kRandoError_Storage;
    Platform3DS_LogRuntime("Randomizer startup failed: %s",g_randomizer_error);
    if (!g_randomizer_error[0]) snprintf(g_randomizer_error, sizeof(g_randomizer_error),
                                        "Unable to prepare the seed save folder.");
    // The persisted selection is still the previous one. Restart through it,
    // and reopen the wizard with the error, without autosaving this session.
    memset(&g_pending_randomizer_seed,0,sizeof(g_pending_randomizer_seed));
    g_pending_randomizer_kind=3;
    g_randomizer_menu_requested = true;
    Platform3DS_RequestReset();
    return false;
  }
  return ok;
}
