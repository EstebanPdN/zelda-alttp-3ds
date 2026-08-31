#ifndef ZELDA3_DUMP_STATE_H_
#define ZELDA3_DUMP_STATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ZELDA_DUMP_LOAD_STATE_FILENAME "load-state.bin"

typedef enum ZeldaDumpStateResult {
  kZeldaDumpStateLoaded = 0,
  kZeldaDumpStateNoDump,
  kZeldaDumpStateNoState,
  kZeldaDumpStateInvalid,
  kZeldaDumpStateWrongRom,
  kZeldaDumpStateIoError,
} ZeldaDumpStateResult;

bool DumpState_WriteFile(const char *path, uint32_t profile_id,
                         const void *payload, size_t payload_size);
ZeldaDumpStateResult DumpState_ReadLatest(const char *dumps_directory,
                                          uint32_t profile_id,
                                          uint8_t **payload_out,
                                          size_t *payload_size_out);
const char *DumpState_ResultLabel(ZeldaDumpStateResult result);

#endif  // ZELDA3_DUMP_STATE_H_
