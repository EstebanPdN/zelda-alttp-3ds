#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define UPDATE_REPOSITORY "EstebanPdN/zelda-alttp-3ds"
#define UPDATE_MAX_FILE (32u * 1024u * 1024u)
typedef struct UpdateRelease {
  char version[48];
  char notes[12289];
  char url[512];
  char sha256[65];
  uint32_t size;
} UpdateRelease;
// -1: invalid response, 0: no publication in this channel, 1: valid candidate.
int Update_ParseRelease(const char *data, size_t size, bool prerelease,
                        bool homebrew, UpdateRelease *out);
bool Update_IsNewer(const char *candidate, const char *installed);
bool Update_ValidVersion(const char *version);
bool Update_AllowedDownloadUrl(const char *url);

unsigned Update_FormatNotes(const char *markdown, char lines[][43], unsigned capacity);
