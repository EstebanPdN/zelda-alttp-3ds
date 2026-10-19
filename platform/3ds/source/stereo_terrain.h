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
//
// Threading: StereoTerrain_Solve owns the solver state and may run on a worker
// thread; it reads only its job and writes only its output. StereoTerrain_Sprites
// runs on the render thread every frame from a finished output.
#include <stdbool.h>
#include <stdint.h>
#include "snes/ppu.h"

enum { kTerrainSize = 64, kTerrainTiles = kTerrainSize * kTerrainSize, kTerrainNoFeet = -32768 };
// Eighths of parallax per 8 px row of rise, and the tallest object in rows.
enum { kTerrainRise = 2, kTerrainCap = 6 };

// What the geometry builder reads for one frame.
typedef struct PicaTerrain {
  bool valid;
  const uint8_t *top;      // BG2 tilemap order: parallax at each tile's top edge
  const uint8_t *bottom;   // parallax at each tile's bottom edge
  int16_t objFeet[128];    // screen row of the sprite object's feet, or kTerrainNoFeet
  uint8_t objGround[128];  // parallax of the ground under those feet
} PicaTerrain;

typedef struct StereoTerrainSource {
  const uint8_t *overworldAttr;  // 512-entry map8 -> attribute table (outdoors), or NULL
  const uint8_t *dungeonAttr;    // 64x64 BG2 attribute table (indoors), or NULL
} StereoTerrainSource;

// Inputs of one solve, copied from the frame so a worker never touches live state.
typedef struct StereoTerrainJob {
  uint16_t tilemap[kTerrainTiles];      // BG2 tilemap words, tile (tx,ty) at ty*64+tx
  uint8_t dungeonAttr[kTerrainTiles];   // indoors only
  const uint8_t *overworldAttr;         // static asset table, outdoors only
  bool indoors, wider, higher, reset;
  uint16_t hScroll, vScroll;
  int extraLeftRight;
} StereoTerrainJob;

typedef struct StereoTerrainOut {
  bool valid;
  uint8_t top[kTerrainTiles], bottom[kTerrainTiles];
  uint8_t ground[kTerrainTiles];        // parallax a sprite standing on this tile gets
  uint16_t gw, gh, changed, sweeps;
  uint32_t us;                          // solve time, filled in by the caller
} StereoTerrainOut;

// Engine adapter: 1 = update from src, 2 = keep the previous terrain (menus,
// dialog over gameplay), 0 = no terrain (title, map, cutscenes).
int StereoTerrain_GetSource(StereoTerrainSource *src);

void StereoTerrain_MakeJob(StereoTerrainJob *job, const Ppu *ppu, const StereoTerrainSource *src, bool reset);
// Warm-starts from the previous solve; job->reset or a new area restarts it.
void StereoTerrain_Solve(const StereoTerrainJob *job, StereoTerrainOut *out);
// Sprite feet and ground for this frame, plus the grid pointers, from a finished solve.
void StereoTerrain_Sprites(const StereoTerrainOut *in, const Ppu *ppu, PicaTerrain *t);

// Single-threaded convenience (host tools and tests): job, solve and sprites in one call.
void StereoTerrain_Update(PicaTerrain *out, const Ppu *ppu, const StereoTerrainSource *src);
void StereoTerrain_Reset(void);
