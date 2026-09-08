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
  for (unsigned i = 1; i <= 3; i++) {
    assert(DumpState_CreateDirectory(root, directory, sizeof(directory)));
    snprintf(path, sizeof(path), "%s/%03u", root, i); assert(!strcmp(directory, path));
    payload = i;
    snprintf(path, sizeof(path), "%s/load-state.bin", directory);
    assert(DumpState_WriteFile(path, 123, &payload, 1));
    CheckRead(root, kZeldaDumpStateLoaded, i);
  }
  snprintf(directory, sizeof(directory), "%s/999", root); assert(mkdir(directory, 0700) == 0);
  assert(DumpState_CreateDirectory(root, directory, sizeof(directory)));
  snprintf(path, sizeof(path), "%s/1000", root); assert(!strcmp(directory, path));
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
  char tiny[2] = {1, 1}; assert(!DumpState_CreateDirectory(root, tiny, sizeof(tiny))); assert(tiny[0] == 0);
  puts("PASS 001/002/003, restart scan, 999->1000, legacy load, incomplete/corrupt/wrong-ROM, short path");
  return 0;
}
