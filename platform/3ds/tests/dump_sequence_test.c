#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "dump_state.h"
static void CheckRead(const char *root, ZeldaDumpStateResult want, unsigned byte) {
  uint8_t *payload = NULL; size_t size = 0;
  assert(DumpState_ReadLatest(root, 123, &payload, &size) == want);
  if (want == kZeldaDumpStateLoaded) assert(size == 1 && payload[0] == byte);
  free(payload);
}
int main(int argc, char **argv) {
  assert(argc == 2); const char *root = argv[1];
  char directory[512], path[560]; unsigned char payload = 7;
  assert(mkdir(root, 0700) == 0);
  snprintf(directory, sizeof(directory), "%s/dump-20260831-095728", root);
  assert(mkdir(directory, 0700) == 0);
  snprintf(path, sizeof(path), "%s/load-state.bin", directory);
  assert(DumpState_WriteFile(path, 123, &payload, 1));
  CheckRead(root, kZeldaDumpStateLoaded, 7);
  for (unsigned i = 0; i <= 2; i++) {
    assert(DumpState_CreateDirectoryAt(root, "20260907-152424", directory, sizeof(directory)));
    snprintf(path, sizeof(path), "%s/%03u-dump-20260907-152424", root, i); assert(!strcmp(directory, path));
    payload = i;
    snprintf(path, sizeof(path), "%s/load-state.bin", directory);
    assert(DumpState_WriteFile(path, 123, &payload, 1));
    CheckRead(root, kZeldaDumpStateLoaded, i);
  }
  snprintf(directory, sizeof(directory), "%s/999", root); assert(mkdir(directory, 0700) == 0);
  assert(DumpState_CreateDirectoryAt(root, "20260907-152424", directory, sizeof(directory)));
  snprintf(path, sizeof(path), "%s/1000-dump-20260907-152424", root); assert(!strcmp(directory, path));
  CheckRead(root, kZeldaDumpStateNoState, 0); // Fail visibly on an incomplete newest dump.
  snprintf(path, sizeof(path), "%s/load-state.bin", directory);
  payload = 19; assert(DumpState_WriteFile(path, 456, &payload, 1));
  CheckRead(root, kZeldaDumpStateWrongRom, 0);
  assert(DumpState_WriteFile(path, 123, &payload, 1));
  CheckRead(root, kZeldaDumpStateLoaded, 19);
  FILE *f = fopen(path, "ab"); assert(f); fputc(0, f); fclose(f);
  CheckRead(root, kZeldaDumpStateInvalid, 0);
  assert(DumpState_WriteManifest(directory, false));
  snprintf(path, sizeof(path), "%s/manifest.txt", directory);
  f = fopen(path, "rb"); assert(f); char report[1024] = {0};
  assert(fread(report, 1, sizeof(report)-1, f) > 0); fclose(f);
  assert(strstr(report, "capture_complete=no") && strstr(report, "load-state.bin 26 "));
  assert(DumpState_WriteManifest(directory, true));
  f = fopen(path, "rb"); assert(f); memset(report, 0, sizeof(report));
  assert(fread(report, 1, sizeof(report)-1, f) > 0); fclose(f);
  assert(strstr(report, "capture_complete=yes") && !strstr(report, "manifest.txt "));
  // Empty collection resets a stale counter, as in Mario Kart.
  char empty_root[560]; snprintf(empty_root, sizeof(empty_root), "%s/empty", root);
  assert(mkdir(empty_root, 0700) == 0);
  snprintf(path, sizeof(path), "%s/empty/dump-sequence.txt", root);
  f = fopen(path, "wb"); assert(f); fputs("500\n", f); fclose(f);
  assert(DumpState_CreateDirectoryAt(empty_root, "20260907-152440", directory, sizeof(directory)));
  snprintf(path, sizeof(path), "%s/empty/000-dump-20260907-152440", root);
  assert(!strcmp(directory, path));
  // A malformed counter cannot disable capture; existing folders recover it.
  snprintf(path, sizeof(path), "%s/empty/dump-sequence.txt", root);
  f = fopen(path, "wb"); assert(f); fputs("-1\n", f); fclose(f);
  assert(DumpState_CreateDirectoryAt(empty_root, "20260907-152424", directory, sizeof(directory)));
  snprintf(path, sizeof(path), "%s/empty/001-dump-20260907-152424", root);
  assert(!strcmp(directory, path)); // Clock going backwards does not reorder IDs.
  snprintf(path, sizeof(path), "%s/load-state.bin", directory);
  payload = 31; assert(DumpState_WriteFile(path, 123, &payload, 1));
  CheckRead(empty_root, kZeldaDumpStateLoaded, 31);
  assert(!DumpState_CreateDirectoryAt(root, "../bad", directory, sizeof(directory)));
  assert(!directory[0]);
  char tiny[2] = {1, 1}; assert(!DumpState_CreateDirectory(root, tiny, sizeof(tiny))); assert(tiny[0] == 0);
  puts("PASS 000-dump/001-dump/002-dump, restart scan, 999->1000, legacy load, incomplete/corrupt/wrong-ROM, short path");
  return 0;
}
