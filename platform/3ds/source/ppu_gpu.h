#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "snes/ppu.h"
bool PpuGpuInit(void);
void PpuGpuShutdown(void);
bool PpuGpuCanAttempt(void);
void PpuGpuForceCpuFrame(void);
bool PpuGpuBegin(Ppu *ppu,unsigned height);
void PpuGpuLine(Ppu *ppu,unsigned y);
bool PpuGpuFinish(Ppu *ppu);
void PpuGpuCpuFrame(void);
bool PpuGpuPrepared(void);
bool PpuGpuOutputActive(void);
bool PpuGpuDraw(void);
void *PpuGpuOutput(void);
void PpuGpuWriteDiagnostics(FILE *file);
const uint32_t *PpuGpuReadback(void);
