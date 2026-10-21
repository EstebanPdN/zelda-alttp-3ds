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
// Wide mode: pin the left/right halves of the HUD to the screen edges.
void PpuGpuSetHudAnchor(bool on);
// Scenery lean: depth added at the top of tall high-priority tiles and
// liftable tiles (0 disables). HUD pop: extra forward shift for the HUD and
// dialog boxes. SetPopTiles lists liftable tile characters and their place in
// a 16x16 block (0=TL 1=TR 2=BL 3=BR).
void PpuGpuSetScenery(float term);
void PpuGpuSetHudPop(float term);
// Strength of the bush lean, percent of the scenery lean (100 = full).
void PpuGpuSetBushPercent(unsigned percent);
void PpuGpuSetPopTiles(const uint16_t *chars,const uint8_t *roles,unsigned count);
void PpuGpuWriteDiagnostics(FILE *file);
const uint32_t *PpuGpuReadback(void);
