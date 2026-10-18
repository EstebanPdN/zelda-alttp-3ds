#include "dump_state.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

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

static bool DumpNumber(const char *name, uint32_t *number) {
  uint32_t value = 0;
  unsigned digits = 0;
  while (name[digits] >= '0' && name[digits] <= '9') {
    uint32_t digit = (uint32_t)(name[digits++] - '0');
    if (value > (UINT32_MAX - digit) / 10) return false;
    value = value * 10 + digit;
  }
  if (digits < 3) return false;
  // E8 timestamped names and the previous E7 numeric-only names.
  if (name[digits] && strncmp(name + digits, "-dump-", 6)) return false;
  *number = value;
  return true;
}

static bool SaveDumpCounter(const char *path, uint32_t next) {
  char temporary[kDumpStateMaxPath];
  int n = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
  if (n < 0 || n >= (int)sizeof(temporary)) return false;
  FILE *f = fopen(temporary, "wb");
  if (!f) return false;
  bool ok = fprintf(f, "%lu\n", (unsigned long)next) > 0;
  if (fflush(f) || fsync(fileno(f))) ok = false;
  if (fclose(f)) ok = false;
#ifdef __3DS__
  if (ok && remove(path) && errno != ENOENT) ok = false;
#endif
  if (ok && rename(temporary, path) == 0) return true;
  remove(temporary);
  return false;
}

bool DumpState_CreateDirectoryAt(const char *root, const char *stamp,
                                  char *out, size_t out_size) {
  if (!out || !out_size) return false;
  out[0] = 0;
  if (!root || !stamp || strlen(stamp) != 15 || stamp[8] != '-') return false;
  for (unsigned i = 0; i < 15; i++)
    if (i != 8 && (stamp[i] < '0' || stamp[i] > '9')) return false;
  if (mkdir(root, 0777) && errno != EEXIST) return false;
  DIR *dir = opendir(root);
  if (!dir) return false;
  char counter[kDumpStateMaxPath];
  int n = snprintf(counter, sizeof(counter), "%s/dump-sequence.txt", root);
  if (n < 0 || n >= (int)sizeof(counter)) { closedir(dir); return false; }
  uint32_t next = 0;
  FILE *f = fopen(counter, "rb");
  if (f) {
    char text[64], *end;
    if (fgets(text, sizeof(text), f)) {
      errno = 0;
      unsigned long value = strtoul(text, &end, 10);
      if (!errno && text[0] >= '0' && text[0] <= '9' && value <= UINT32_MAX && end != text &&
          (*end == '\n' || *end == 0)) next = (uint32_t)value;
    }
    fclose(f);
  }
  bool has_dumps = false, exhausted = false;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    uint32_t number;
    if (!strncmp(entry->d_name, "dump-", 5) &&
        entry->d_name[5] >= '0' && entry->d_name[5] <= '9' )
      has_dumps = true;
    if (!DumpNumber(entry->d_name, &number)) continue;
    has_dumps = true;
    if (number == UINT32_MAX) { exhausted = true; break; }
    if (number >= next) next = number + 1;
  }
  closedir(dir);
  if (!has_dumps) next = 0;
  if (exhausted) return false;
  for (unsigned attempt = 0; attempt < 1000 && next != UINT32_MAX; attempt++, next++) {
    int length = snprintf(out, out_size, "%s/%03lu-dump-%s", root, (unsigned long)next, stamp);
    if (length < 0 || length >= (int)out_size) break;
    if (mkdir(out, 0777) == 0) {
      // A counter write failure does not lose the capture: scanning its
      // existing directory recovers the next number on the next request.
      SaveDumpCounter(counter, next + 1);
      return true;
    }
    if (errno != EEXIST) break;
  }
  out[0] = 0;
  return false;
}

bool DumpState_CreateDirectory(const char *root, char *out, size_t out_size) {
  time_t now = time(NULL);
  struct tm *calendar = localtime(&now);
  char stamp[32];
  if (!calendar || !strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", calendar)) {
    if (out && out_size) out[0] = 0;
    return false;
  }
  return DumpState_CreateDirectoryAt(root, stamp, out, out_size);
}

static ZeldaDumpStateResult FindLatestDump(const char *dumps_directory,
                                           char *out, size_t out_size) {
  DIR *directory = opendir(dumps_directory);
  if (!directory)
    return errno == ENOENT ? kZeldaDumpStateNoDump :
                             kZeldaDumpStateIoError;

  char latest[256] = "";
  uint32_t latest_number = 0;
  bool latest_numbered = false;
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL) {
    uint32_t number = 0;
    bool numbered = DumpNumber(entry->d_name, &number);
    if (!numbered && strncmp(entry->d_name, "dump-", 5) != 0)
      continue;
    char path[kDumpStateMaxPath];
    struct stat info;
    int length = snprintf(path, sizeof(path), "%s/%s", dumps_directory,
                          entry->d_name);
    if (length < 0 || length >= (int)sizeof(path))
      continue;
    if (stat(path, &info) != 0 || !S_ISDIR(info.st_mode))
      continue;
    // E7 numeric sessions supersede the legacy timestamp layout. If none
    // exist, old E4/E5/E6 checkpoints remain loadable without migration.
    if (!latest[0] || (numbered && !latest_numbered) ||
        (numbered == latest_numbered &&
         (number > latest_number || (number == latest_number && strcmp(entry->d_name, latest) > 0)))) {
      snprintf(latest, sizeof(latest), "%s", entry->d_name);
      latest_number = number;
      latest_numbered = numbered;
    }
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

// Written last. A partial capture is explicit even when some files succeeded.
// FNV-1a detects accidental truncation/corruption; it is not authentication.
bool DumpState_WriteManifest(const char *directory, bool capture_complete) {
  char path[kDumpStateMaxPath], manifest[kDumpStateMaxPath];
  int n = snprintf(manifest, sizeof(manifest), "%s/manifest.txt", directory);
  if (n < 0 || n >= (int)sizeof(manifest)) return false;
  DIR *dir = opendir(directory);
  if (!dir) return false;
  FILE *out = fopen(manifest, "wb");
  if (!out) { closedir(dir); return false; }
  fputs("Dump manifest schema: 1\nchecksum=FNV-1a-32 (non-cryptographic)\nfilename bytes checksum\n", out);
  bool ok = true;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (!strcmp(entry->d_name, "manifest.txt") || entry->d_name[0] == '.') continue;
    n = snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
    if (n < 0 || n >= (int)sizeof(path)) { ok = false; continue; }
    struct stat st;
    if (stat(path, &st) != 0) { ok = false; continue; }
    if (!S_ISREG(st.st_mode)) continue;
    FILE *in = fopen(path, "rb");
    if (!in) { ok = false; continue; }
    uint32_t hash = 2166136261u;
    size_t bytes = 0, count;
    uint8_t buffer[4096];
    while ((count = fread(buffer, 1, sizeof(buffer), in)) != 0) {
      bytes += count;
      for (size_t i = 0; i < count; i++) { hash ^= buffer[i]; hash *= 16777619u; }
    }
    if (ferror(in) || bytes != (size_t)st.st_size) ok = false;
    if (fclose(in) != 0) ok = false;
    fprintf(out, "%s %lu %08lx\n", entry->d_name, (unsigned long)bytes, (unsigned long)hash);
  }
  closedir(dir);
  fprintf(out, "capture_complete=%s\n", capture_complete && ok ? "yes" : "no");
  ok = !ferror(out) && ok;
  return fclose(out) == 0 && ok;
}
