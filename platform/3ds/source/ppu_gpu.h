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
// Layered stereo: half_separation_px is the per-eye shift of the nearest layer
// (0 disables). PpuGpuOutputRight() is non-NULL only after a stereo frame.
void PpuGpuSetStereo(float half_separation_px);
void *PpuGpuOutputRight(void);
// Layer term for sprites (default 6700); larger pops sprites out more.
void PpuGpuSetSpriteDepth(float term);
// Body lean: sprite upper bodies pop out up to this multiple of the old flat
// sprite pop while the feet stay on the ground plane (default 2.0, 0 = flat
// ground-level sprites).
void PpuGpuSetBodyScale(float scale);
void PpuGpuWriteDiagnostics(FILE *file);
const uint32_t *PpuGpuReadback(void);
