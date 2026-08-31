#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct Config;

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

typedef struct Platform3DSCaptureStats {
  uint32_t top_format;
  uint32_t top_stride;
  uint32_t bottom_format;
  uint32_t bottom_stride;
  uintptr_t top_address;
  uintptr_t bottom_address;
} Platform3DSCaptureStats;

bool Platform3DS_PrepareStorage(void);
void Platform3DS_ApplyConfig(struct Config *config);
void Platform3DS_LogRuntime(const char *format, ...);
uint16_t Platform3DS_ReadInput(bool *turbo_held, int *turbo_multiplier);
void Platform3DS_LoadRuntimeSettings(void);
void Platform3DS_ShowFatalError(const char *message);
enum Platform3DSDisplayMode Platform3DS_GetDisplayMode(void);
void Platform3DS_SetDisplayMode(enum Platform3DSDisplayMode mode);
enum Platform3DSWideEdgeMode Platform3DS_GetWideEdgeMode(void);
void Platform3DS_SetWideEdgeMode(enum Platform3DSWideEdgeMode mode);
int Platform3DS_GetWideZoomIndex(void);
void Platform3DS_SetWideZoomIndex(int zoom_index);
enum Platform3DSCStickMode Platform3DS_GetCStickMode(void);
void Platform3DS_SetCStickMode(enum Platform3DSCStickMode mode);
bool Platform3DS_TakeQuickDumpRequest(void);
void Platform3DS_RequestRomSelection(void);
bool Platform3DS_TakeRomSelectionRequest(void);
bool Platform3DS_ShouldExit(void);
bool Platform3DS_IsSystemClosing(void);
bool Platform3DS_IsNew3DS(void);
bool Platform3DS_CanUseCore1PpuWorker(void);
bool Platform3DS_IsVersionOverlayVisible(void);
void Platform3DS_BlankScreens(void);
void Platform3DS_FormatSavePath(const char *filename,
                                char *out, size_t out_size);
int Platform3DS_GetTurboMultiplier(void);
void Platform3DS_SetTurboMultiplier(int multiplier);
bool Platform3DS_GetShowFps(void);
void Platform3DS_SetShowFps(bool show);
void Platform3DS_SetCurrentFps(int fps);
void Platform3DS_PersistRuntimeSettings(void);
void Platform3DS_ShowDumpSavedOverlay(void);
void Platform3DS_SetAudioPausedForDump(bool paused);
void Platform3DS_MarkDumpTimingDiscontinuity(void);
uint32_t Platform3DS_GetActiveProfileId(void);
bool Platform3DS_InitTopPresenter(void);
void Platform3DS_ShutdownTopPresenter(void);
void Platform3DS_PresentTopFrame(const uint8_t *pixels, int pitch,
                                 int width, int height,
                                 int focus_x, int focus_y);
void Platform3DS_PresentBottomFrame(const uint8_t *pixels, int pitch,
                                    int width, int height);
void Platform3DS_EndFrame(void);
uint32_t Platform3DS_WaitForVBlank(void);
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
                                   int executed_logic_frames);
bool Platform3DS_CreateDumpDirectory(char *out, size_t out_size);
bool Platform3DS_SaveDisplayedScreensDetailed(
  const char *top_path, const char *bottom_path,
  const char *top_raw_path, const char *bottom_raw_path,
  Platform3DSCaptureStats *stats);
bool Platform3DS_DumpMemory(const char *directory,
                            const uint8_t *ram, size_t ram_size,
                            const uint8_t *sram, size_t sram_size,
                            const uint16_t *vram, size_t vram_words,
                            const Platform3DSCaptureStats *capture_stats,
                            bool screens_ok);
