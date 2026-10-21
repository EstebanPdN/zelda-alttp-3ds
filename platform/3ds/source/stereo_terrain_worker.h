#pragma once
// Runs the stereo terrain solve off the render thread: on the system core
// (core 1) when the app has time there, otherwise inline every few frames.
#include <stdbool.h>
#include <stdio.h>
#include "stereo_terrain.h"

// Posts a new solve when the worker is idle and fills t for this frame from the
// latest finished one (sprites are placed every frame). Returns t->valid.
bool TerrainWorker_Frame(PicaTerrain *t, const Ppu *ppu, const StereoTerrainSource *src);
// Drops the current terrain; the next solve starts from scratch.
void TerrainWorker_RequestReset(void);
void TerrainWorker_Shutdown(void);
void TerrainWorker_Diagnostics(FILE *f);
