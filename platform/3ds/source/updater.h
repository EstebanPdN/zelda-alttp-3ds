#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "update_manifest.h"
typedef enum UpdateState {
  UPDATE_IDLE, UPDATE_CHECKING, UPDATE_CURRENT, UPDATE_AVAILABLE,
  UPDATE_EMPTY, UPDATE_DOWNLOADING, UPDATE_VERIFYING, UPDATE_INSTALLING,
  UPDATE_DONE, UPDATE_ERROR
} UpdateState;
typedef struct UpdateStatus {
  UpdateState state;
  bool prerelease;
  unsigned progress, revision;
  char version[48], message[80];
} UpdateStatus;
void Updater_Init(const char *launch_path);
void Updater_Check(void);
void Updater_SetChannel(bool prerelease);
void Updater_Download(void);
void Updater_Cancel(void);
void Updater_GetStatus(UpdateStatus *out);
bool Updater_Busy(void);
bool Updater_ShouldClose(void);
void Updater_Shutdown(void);

unsigned Updater_GetNotes(char *out, unsigned capacity);
