#pragma once
// Stereo terrain: per-tile and per-sprite stereo depth derived from the game's
// tile collision attributes, for the PICA200 stereo pass.
//
// Ground tiles get a height from a least-squares height field: neighbouring
// ground is level, a ledge is a step down on the side Link jumps to, a wide band
// of plain solid rows between two ground areas (a cliff face) is a step of its
// row count, and stairs climb one row per tile going north. Solid tiles stand on
// the nearest ground below them and rise one row per tile, so statues, columns,
// trees and house walls lean as one piece. Sprites stand on the ground under
// their feet and lean the same way.
//
// Depth is parallax in eighths of a unit; 0 is the screen plane (HUD).
#include <stdbool.h>
#include <stdint.h>
#include "snes/ppu.h"

enum { kTerrainSize = 64, kTerrainNoFeet = -32768 };
// Eighths of parallax per 8 px row of rise, and the tallest object in rows.
enum { kTerrainRise = 2, kTerrainCap = 6 };

typedef struct PicaTerrain {
  bool valid;
  uint8_t top[kTerrainSize * kTerrainSize];     // BG2 tilemap order: parallax at each tile's top edge
  uint8_t bottom[kTerrainSize * kTerrainSize];  // parallax at each tile's bottom edge
  int16_t objFeet[128];    // screen row of the sprite object's feet, or kTerrainNoFeet
  uint8_t objGround[128];  // parallax of the ground under those feet
  uint16_t changed, sweeps;  // diagnostics for the last update
} PicaTerrain;

typedef struct StereoTerrainSource {
  const uint8_t *overworldAttr;  // 512-entry map8 -> attribute table (outdoors), or NULL
  const uint8_t *dungeonAttr;    // 64x64 BG2 attribute table (indoors), or NULL
} StereoTerrainSource;

// Engine adapter: 1 = update from src, 2 = keep the previous terrain (menus,
// dialog over gameplay), 0 = no terrain (title, map, cutscenes).
int StereoTerrain_GetSource(StereoTerrainSource *src);

// Rebuilds out from the frame's PPU state. Warm-starts from the previous frame;
// a large tilemap change (new area or room) restarts the solve.
void StereoTerrain_Update(PicaTerrain *out, const Ppu *ppu, const StereoTerrainSource *src);
void StereoTerrain_Reset(void);
