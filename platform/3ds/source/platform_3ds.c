#include "platform_3ds.h"

#include <3ds.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

uint16_t Platform3DS_ReadInput(bool *turbo_held) {
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
  *turbo_held = (keys & KEY_ZL) != 0;
  return input;
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
  printf("Pulsa B para salir.");
  PresentSetupConsole();
  WaitForButtons(KEY_B | KEY_START);
}

static bool ConfirmExtraction(void) {
  consoleClear();
  printf("\x1b[2;2HZelda 3DS v%s\n\n", ZELDA3_3DS_VERSION);
  printf("No se encontraron los assets del juego.\n\n");
  printf("Coloca tu ROM legal en:\n");
  printf("sdmc:/3ds/Zelda 3DS/\n\n");
  printf("Formatos aceptados: .sfc o .smc\n");
  printf("Region requerida: USA\n\n");
  printf("[A] Si, extraer assets\n");
  printf("[B] No, salir\n");
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
      "Error: no se encontro una ROM .sfc o .smc\n"
      "en la carpeta Zelda 3DS.");
    return false;
  }
  LogSetup("ROM found: %s", rom_path);

  consoleClear();
  printf("\x1b[2;2HZelda 3DS v%s\n\n", ZELDA3_3DS_VERSION);
  printf("ROM encontrada: %s\n\n", rom_path);
  printf("Validando y extrayendo assets...\n");
  printf("No apagues la consola.");
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
      "Error al leer archivos.\n"
      "ROM: %s (%lu bytes)\n"
      "Parche interno: %s (%lu bytes)",
      rom ? "OK" : "FALLO", (unsigned long)rom_size,
      patch ? "OK" : "FALLO", (unsigned long)patch_size);
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
      "Error: la ROM no es la version correcta.\n"
      "Se requiere Zelda 3 USA sin cabecera.\n"
      "SHA-256 esperado:\n"
      "66871d66be19ad2c34c927d6b14cd8eb\n"
      "6fc3181965b6e517cb361f7316009cfb");
    return false;
  }

  bool written = WriteAssetsFile(assets, assets_size);
  LogSetup("Assets write: %s", written ? "OK" : "FAIL");
  free(assets);
  if (!written || !AssetsFileLooksValid(kAssetsFilename)) {
    ShowFatalSetupError(
      "Error al guardar zelda3_assets.dat.\n"
      "Comprueba el espacio libre y la tarjeta SD.");
    return false;
  }

  consoleClear();
  printf("\x1b[2;2HZelda 3DS v%s\n\n", ZELDA3_3DS_VERSION);
  printf("Assets extraidos correctamente.\n\n");
  printf("Preparacion completada.");
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
      "Falta el firmware de audio DSP:\n"
      "sdmc:/3ds/dspfirm.cdc\n\n"
      "Abre Rosalina (L + Abajo + Select),\n"
      "entra en Miscellaneous options y usa\n"
      "Dump DSP firmware. Despues reinicia.");
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
  config->ignore_aspect_ratio = false;
  config->linear_filtering = false;
  config->crt_filter = false;
  config->enhanced_mode7 = false;
  config->new_renderer = true;
  config->extend_y = true;
  config->extended_aspect_ratio = 72;
  config->features0 |= kFeatures0_ExtendScreen64 |
                       kFeatures0_WidescreenVisualFixes;
  config->audio_freq = 32000;
  config->audio_channels = 2;
  config->audio_samples = 1024;
  config->enable_msu = 0;
}
