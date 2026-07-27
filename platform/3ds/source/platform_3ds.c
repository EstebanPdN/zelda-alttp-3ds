#include "platform_3ds.h"

#include <3ds.h>
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

static const char kStorageDirectory[] = "sdmc:/3ds/Zelda 3DS";
static const char kAssetsFilename[] = "zelda3_assets.dat";
static const char kTemporaryAssetsFilename[] = "zelda3_assets.tmp";
static const char kBundledPatch[] = "romfs:/zelda3_assets.bps";
static const char kBundledConfig[] = "romfs:/zelda3.ini";

static enum Platform3DSDisplayMode g_display_mode =
  kPlatform3DSDisplayUltraWideMod;
static enum Platform3DSCStickMode g_cstick_mode = kPlatform3DSCStickTurbo;
static int g_turbo_multiplier = 5;

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

static bool CStickIsHeld(u32 keys) {
  return (keys & (KEY_CSTICK_UP | KEY_CSTICK_DOWN |
                  KEY_CSTICK_LEFT | KEY_CSTICK_RIGHT)) != 0;
}

uint16_t Platform3DS_ReadInput(bool *turbo_held, int *turbo_multiplier) {
  hidScanInput();
  u32 keys = hidKeysHeld();
  circlePosition circle;
  hidCircleRead(&circle);

  uint16_t input = 0;
  if ((keys & KEY_DUP) || circle.dy > 40) input |= 1u << 4;
  if ((keys & KEY_DDOWN) || circle.dy < -40) input |= 1u << 5;
  if ((keys & KEY_DLEFT) || circle.dx < -40) input |= 1u << 6;
  if ((keys & KEY_DRIGHT) || circle.dx > 40) input |= 1u << 7;
  if (keys & KEY_SELECT) input |= 1u << 2;
  if (keys & KEY_START) input |= 1u << 3;
  if (keys & KEY_A) input |= 1u << 8;
  if (keys & KEY_B) input |= 1u << 0;
  if (keys & KEY_X) input |= 1u << 9;
  if (keys & KEY_Y) input |= 1u << 1;
  if (keys & KEY_L) input |= 1u << 10;
  if (keys & KEY_R) input |= 1u << 11;
  *turbo_held = g_turbo_multiplier > 0 &&
                ((keys & KEY_ZL) != 0 || CStickIsHeld(keys));
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
    if (strcasecmp(value, "Original") == 0)
      g_display_mode = kPlatform3DSDisplayOriginal;
    else if (strcasecmp(value, "Stretch") == 0 ||
             strcasecmp(value, "UltraWideStretch") == 0)
      g_display_mode = kPlatform3DSDisplayStretch;
    else
      g_display_mode = kPlatform3DSDisplayUltraWideMod;
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
  if (mode < kPlatform3DSDisplayOriginal ||
      mode > kPlatform3DSDisplayStretch)
    mode = kPlatform3DSDisplayUltraWideMod;
  g_display_mode = mode;
  Platform3DS_LogRuntime("Display mode set: %d", (int)g_display_mode);
}

enum Platform3DSCStickMode Platform3DS_GetCStickMode(void) {
  return g_cstick_mode;
}

void Platform3DS_SetCStickMode(enum Platform3DSCStickMode mode) {
  if (mode < kPlatform3DSCStickTurbo ||
      mode > kPlatform3DSCStickDisabled)
    mode = kPlatform3DSCStickTurbo;
  g_cstick_mode = mode;
  Platform3DS_LogRuntime("C-stick mode set: %d", (int)g_cstick_mode);
}

int Platform3DS_GetTurboMultiplier(void) {
  return g_turbo_multiplier;
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
  gfxInitDefault();
  consoleInit(GFX_TOP, NULL);
  consoleClear();
}

static void PresentSetupConsole(void) {
  gfxFlushBuffers();
  gfxSwapBuffers();
  gspWaitForVBlank();
}

static void EndSetupConsole(void) {
  PresentSetupConsole();
  gfxExit();
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
  config->extend_y = true;
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
  Platform3DS_LogRuntime("Runtime settings: display=%d, turbo=%d",
                         (int)g_display_mode,
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
