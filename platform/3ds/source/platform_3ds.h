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

enum Platform3DSCStickMode {
  kPlatform3DSCStickTurbo,
  kPlatform3DSCStickWalk,
  kPlatform3DSCStickDisabled,
};

bool Platform3DS_PrepareStorage(void);
void Platform3DS_ApplyConfig(struct Config *config);
void Platform3DS_LogRuntime(const char *format, ...);
uint16_t Platform3DS_ReadInput(bool *turbo_held, int *turbo_multiplier);
void Platform3DS_LoadRuntimeSettings(void);
enum Platform3DSDisplayMode Platform3DS_GetDisplayMode(void);
void Platform3DS_SetDisplayMode(enum Platform3DSDisplayMode mode);
enum Platform3DSCStickMode Platform3DS_GetCStickMode(void);
void Platform3DS_SetCStickMode(enum Platform3DSCStickMode mode);
int Platform3DS_GetTurboMultiplier(void);
void Platform3DS_SetTurboMultiplier(int multiplier);
bool Platform3DS_DumpMemory(const uint8_t *ram, size_t ram_size,
                            const uint8_t *sram, size_t sram_size,
                            const uint16_t *vram, size_t vram_words);
