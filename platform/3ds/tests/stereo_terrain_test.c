// Host checks for the stereo terrain solver (stereo_terrain.c): flat ground,
// a statue leaning as one piece, the wall behind it, a ledge step, sprite feet.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "stereo_terrain.h"

static Ppu ppu;
static uint8_t attr[512];      // overworld map8 -> attribute table
static PicaTerrain t;

// Tilemap word at BG2 tile (tx,ty): char number selects the attribute.
static void SetTile(unsigned tx, unsigned ty, unsigned chr, bool prio) {
  unsigned adr = (ppu.bgLayer[1].tilemapAdr + (ty & 31) * 32 + (tx & 31) + (tx >= 32 ? 0x400 : 0) +
                  (ty >= 32 ? 0x800 : 0)) & 0x7fff;
  ppu.vram[adr] = (uint16_t)(chr | (prio ? 0x2000 : 0));
}

static void Flat(void) {
  memset(&ppu, 0, sizeof(ppu));
  ppu.bgLayer[1].tilemapWider = ppu.bgLayer[1].tilemapHigher = true;
  ppu.extraLeftRight = 72;
  for (unsigned i = 0; i < 0x100; i += 2) ppu.oam[i] = 0xf000;   // all sprites hidden
  for (unsigned y = 0; y < 64; y++) for (unsigned x = 0; x < 64; x++) SetTile(x, y, 0, false);
  StereoTerrain_Reset();
}

static void Update(void) {
  StereoTerrainSource src = {.overworldAttr = attr};
  StereoTerrain_Update(&t, &ppu, &src);
  assert(t.valid);
}

#define AT(x, y) ((y) * 64 + (x))

int main(void) {
  attr[0] = 0x00;   // walkable
  attr[1] = 0x01;   // solid
  attr[2] = 0x29;   // ledge, jump south

  Flat(); Update();
  for (unsigned i = 0; i < 64 * 64; i++) assert(t.top[i] == 16 && t.bottom[i] == 16);
  puts("PASS flat ground sits at the floor depth");

  // A statue three tiles tall at column 10: priority head on row 19, body on 20-21,
  // ground from row 22 down. A plain wall tile on row 18 sits behind the head.
  Flat();
  SetTile(10, 18, 1, false);
  SetTile(10, 19, 1, true); SetTile(10, 20, 1, false); SetTile(10, 21, 1, false);
  Update();
  assert(t.bottom[AT(10, 21)] == 16 && t.top[AT(10, 21)] == 14);
  assert(t.bottom[AT(10, 20)] == 14 && t.top[AT(10, 20)] == 12);
  assert(t.bottom[AT(10, 19)] == 12 && t.top[AT(10, 19)] == 10);
  assert(t.bottom[AT(10, 18)] == 16 && t.top[AT(10, 18)] == 16);   // not part of the statue
  puts("PASS statue leans as one piece from its base; the wall behind is separate");

  // A full-width ledge row at 30: the ground north of it is higher (nearer).
  Flat();
  for (unsigned x = 0; x < 64; x++) SetTile(x, 30, 2, false);
  Update();
  for (int i = 0; i < 40; i++) Update();     // warm sweeps converge
  assert(t.top[AT(5, 29)] + 2 <= t.top[AT(5, 31)]);
  puts("PASS ground on the jump side of a ledge is lower");

  // Two touching 16x16 sprites stacked at x=100: one object, feet at row 82.
  Flat();
  ppu.objSize = 0;
  ppu.oam[0] = (uint16_t)(100 | (50 << 8)); ppu.oam[2] = (uint16_t)(100 | (66 << 8));
  ppu.oam[0x100] = (2 << 0) | (2 << 2);      // both large (16x16)
  Update();
  assert(t.objFeet[0] == 82 && t.objFeet[1] == 82);
  assert(t.objGround[0] == 16 && t.objGround[1] == 16);
  assert(t.objFeet[2] == kTerrainNoFeet);
  puts("PASS stacked sprites share their feet and stand on the floor");
  return 0;
}
