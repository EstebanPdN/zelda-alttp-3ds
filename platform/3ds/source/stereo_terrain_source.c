// Engine adapter for the stereo terrain: which collision table describes the
// ground layer this frame. Kept apart so the terrain solver stays engine-free.
#include "stereo_terrain.h"
#include "types.h"
#include "variables.h"
#include "assets.h"

int StereoTerrain_GetSource(StereoTerrainSource *src) {
  src->overworldAttr = NULL;
  src->dungeonAttr = NULL;
  switch (main_module_index) {
  case 7:            // dungeon
  case 9:            // overworld
  case 11:           // special overworld areas
    if (player_is_indoors)
      src->dungeonAttr = g_ram + 0x12000;     // BG2 attribute table, 64x64
    else if (kMap8DataToTileAttr_SIZE >= 512)
      src->overworldAttr = kMap8DataToTileAttr;
    else
      return 0;
    return 1;
  case 14:           // item menu and dialog over gameplay: keep the last terrain
    return 2;
  default:
    return 0;
  }
}
