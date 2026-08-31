#include "dump_state.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum {
  kDumpStateVersion = 1,
  kDumpStateMaxPath = 512,
  kDumpStateMaxPayload = 1024 * 1024,
};

typedef struct DumpStateFileHeader {
  uint8_t magic[8];
  uint32_t version;
  uint32_t profile_id;
  uint32_t payload_size;
  uint32_t payload_checksum;
} DumpStateFileHeader;

_Static_assert(sizeof(DumpStateFileHeader) == 24,
               "dump-state header layout changed");

static const uint8_t kDumpStateMagic[8] = {
  'Z', '3', 'D', 'L', 'D', 'S', '0', '1',
};

static uint32_t PayloadChecksum(const void *data, size_t size) {
  const uint8_t *bytes = data;
  uint32_t hash = 2166136261u;
  while (size-- != 0) {
    hash ^= *bytes++;
    hash *= 16777619u;
  }
  return hash;
}

bool DumpState_WriteFile(const char *path, uint32_t profile_id,
                         const void *payload, size_t payload_size) {
  if (!path || !payload || payload_size == 0 ||
      payload_size > kDumpStateMaxPayload)
    return false;

  DumpStateFileHeader header = {0};
  memcpy(header.magic, kDumpStateMagic, sizeof(header.magic));
  header.version = kDumpStateVersion;
  header.profile_id = profile_id;
  header.payload_size = (uint32_t)payload_size;
  header.payload_checksum = PayloadChecksum(payload, payload_size);

  char temporary[kDumpStateMaxPath];
  int length = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
  if (length < 0 || length >= (int)sizeof(temporary))
    return false;
  FILE *file = fopen(temporary, "wb");
  if (!file)
    return false;
  bool ok = fwrite(&header, 1, sizeof(header), file) == sizeof(header) &&
            fwrite(payload, 1, payload_size, file) == payload_size &&
            fflush(file) == 0 && !ferror(file);
  if (fclose(file) != 0)
    ok = false;
  if (ok) {
    remove(path);
    ok = rename(temporary, path) == 0;
  }
  if (!ok)
    remove(temporary);
  return ok;
}

static ZeldaDumpStateResult FindLatestDump(const char *dumps_directory,
                                           char *out, size_t out_size) {
  DIR *directory = opendir(dumps_directory);
  if (!directory)
    return errno == ENOENT ? kZeldaDumpStateNoDump :
                             kZeldaDumpStateIoError;

  char latest[256] = "";
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL) {
    if (strncmp(entry->d_name, "dump-", 5) != 0)
      continue;
    char path[kDumpStateMaxPath];
    struct stat info;
    int length = snprintf(path, sizeof(path), "%s/%s", dumps_directory,
                          entry->d_name);
    if (length < 0 || length >= (int)sizeof(path))
      continue;
    if (stat(path, &info) != 0 || !S_ISDIR(info.st_mode))
      continue;
    if (!latest[0] || strcmp(entry->d_name, latest) > 0)
      snprintf(latest, sizeof(latest), "%s", entry->d_name);
  }
  closedir(directory);

  if (!latest[0])
    return kZeldaDumpStateNoDump;
  int length = snprintf(out, out_size, "%s/%s", dumps_directory, latest);
  if (length < 0 || length >= (int)out_size)
    return kZeldaDumpStateIoError;
  return kZeldaDumpStateLoaded;
}

ZeldaDumpStateResult DumpState_ReadLatest(const char *dumps_directory,
                                          uint32_t profile_id,
                                          uint8_t **payload_out,
                                          size_t *payload_size_out) {
  if (!dumps_directory || !payload_out || !payload_size_out)
    return kZeldaDumpStateInvalid;
  *payload_out = NULL;
  *payload_size_out = 0;

  char dump_directory[kDumpStateMaxPath];
  ZeldaDumpStateResult result =
    FindLatestDump(dumps_directory, dump_directory, sizeof(dump_directory));
  if (result != kZeldaDumpStateLoaded)
    return result;

  char path[kDumpStateMaxPath];
  int length = snprintf(path, sizeof(path), "%s/%s", dump_directory,
                        ZELDA_DUMP_LOAD_STATE_FILENAME);
  if (length < 0 || length >= (int)sizeof(path))
    return kZeldaDumpStateIoError;

  errno = 0;
  FILE *file = fopen(path, "rb");
  if (!file)
    return errno == ENOENT ? kZeldaDumpStateNoState :
                             kZeldaDumpStateIoError;

  DumpStateFileHeader header;
  bool header_ok = fread(&header, 1, sizeof(header), file) == sizeof(header);
  if (!header_ok || memcmp(header.magic, kDumpStateMagic,
                           sizeof(header.magic)) != 0 ||
      header.version != kDumpStateVersion || header.payload_size == 0 ||
      header.payload_size > kDumpStateMaxPayload) {
    fclose(file);
    return kZeldaDumpStateInvalid;
  }

  uint8_t *payload = malloc(header.payload_size);
  if (!payload) {
    fclose(file);
    return kZeldaDumpStateIoError;
  }
  bool payload_ok =
    fread(payload, 1, header.payload_size, file) == header.payload_size &&
    fgetc(file) == EOF && !ferror(file);
  fclose(file);
  if (!payload_ok ||
      PayloadChecksum(payload, header.payload_size) !=
        header.payload_checksum) {
    free(payload);
    return kZeldaDumpStateInvalid;
  }
  if (header.profile_id != profile_id) {
    free(payload);
    return kZeldaDumpStateWrongRom;
  }

  *payload_out = payload;
  *payload_size_out = header.payload_size;
  return kZeldaDumpStateLoaded;
}

const char *DumpState_ResultLabel(ZeldaDumpStateResult result) {
  switch (result) {
  case kZeldaDumpStateLoaded: return "LOADED";
  case kZeldaDumpStateNoDump: return "NO DUMP";
  case kZeldaDumpStateNoState: return "NO STATE";
  case kZeldaDumpStateInvalid: return "INVALID";
  case kZeldaDumpStateWrongRom: return "WRONG ROM";
  case kZeldaDumpStateIoError: return "I O ERROR";
  }
  return "ERROR";
}
