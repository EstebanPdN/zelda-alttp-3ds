#pragma once

#include <stdbool.h>
#include <stdint.h>

struct Config;

bool Platform3DS_PrepareStorage(void);
void Platform3DS_ApplyConfig(struct Config *config);
void Platform3DS_LogRuntime(const char *format, ...);
uint16_t Platform3DS_ReadInput(bool *turbo_held);
