#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hardware_profile.h"

const Platform3DSHardwareProfile *Platform3DS_GetHardwareProfile(void);

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
void Platform3DS_RequestStartupMenu(void);
bool Platform3DS_TakeRomSelectionRequest(void);
bool Platform3DS_RomSelectionRequested(void);
void Platform3DS_RequestReset(void);
bool Platform3DS_ResetRequested(void);
bool Platform3DS_TakeResetRequest(void);
bool Platform3DS_GetAutosave(void);
void Platform3DS_SetAutosave(bool enabled);
enum Platform3DSTurboControl { kTurboControlAuto, kTurboControlX, kTurboControlZL, kTurboControlCStick };
int Platform3DS_GetTurboControl(void);
void Platform3DS_SetTurboControl(int control);

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
int Platform3DS_GetStereoLevel(void);
void Platform3DS_SetStereoLevel(int level);
bool Platform3DS_StereoSupported(void);
void Platform3DS_SetCurrentFps(int fps);
void Platform3DS_PersistRuntimeSettings(void);
void Platform3DS_ShowDumpSavedOverlay(void);
void Platform3DS_SetAudioPausedForDump(bool paused);
void Platform3DS_SetMenuAudio(bool enabled);
void Platform3DS_PlayMenuMove(void);
void Platform3DS_MarkDumpTimingDiscontinuity(void);
uint32_t Platform3DS_GetActiveProfileId(void);
void Platform3DS_FormatProfilePath(const char *filename,
                                   char *out, size_t out_size);
bool Platform3DS_InitTopPresenter(void);
void Platform3DS_ShutdownTopPresenter(void);
void Platform3DS_PresentTopFrame(const uint8_t *pixels, int pitch,
                                 int width, int height,
                                 int focus_x, int focus_y, unsigned module);
void Platform3DS_SetBottomCinema(bool cinema);
bool Platform3DS_PresentBottomFrame(const uint8_t *pixels, int pitch,
                                    int width, int height);
void Platform3DS_EndFrame(void);
void Platform3DS_RevealStartupCard(void);
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

void Platform3DS_PresentUpdatePage(bool show_notes, unsigned page);
void Platform3DS_PresentQolPage(int selected);
unsigned Platform3DS_UpdateNotesPages(void);

// Read only the original local cartridge chest table (including copier-header handling).
bool Platform3DS_ReadRandomizerChestTable(uint8_t table[504]);

// A NULL/empty key selects ordinary saves. Keys are canonical SHA-256 hex.
bool Platform3DS_SelectRandomizerStorage(const char *key, bool persist);
const char *Platform3DS_RandomizerSeedKey(void);

#include "../../../engine/src/rando/seed_summary.h"
typedef RandoSeedSummary Platform3DSRandomizerSeed;
void Platform3DS_RequestRandomizerMenu(void);
bool Platform3DS_TakeRandomizerMenuRequest(void);
bool Platform3DS_RequestGeneratedSeed(int preset, uint64_t seed);
bool Platform3DS_RequestSavedSeed(const char *key);
void Platform3DS_RequestOrdinaryGame(void);
int Platform3DS_ListRandomizerSeeds(Platform3DSRandomizerSeed *out, int capacity);
const char *Platform3DS_RandomizerError(void);
RandoErrorCode Platform3DS_RandomizerErrorCode(void);
bool Platform3DS_ApplyPendingRandomizer(void);
int Platform3DS_ListRandomizerImports(RandoImportFile *out, int capacity);
bool Platform3DS_RequestImportedSeed(const char *filename);

// Startup before SDL/audio workers: 1 continue, 0 restart, -1 quit.
bool Platform3DS_StartupMenuPending(void);
int Platform3DS_ShowStartupMenu(const uint32_t *logo);

void Platform3DS_ShowOperationProgress(bool dumping,int percent);
