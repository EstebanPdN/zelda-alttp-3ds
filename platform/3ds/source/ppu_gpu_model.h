#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "snes/ppu.h"

enum { PICA_ATLAS_W=1024, PICA_ATLAS_H=512, PICA_SLOTS=8192,
       PICA_HASH=16384, PICA_MAX_LINES=240, PICA_MAX_VERTICES=262140,
       PICA_GROUPS=20, PICA_LINE_BYTES=offsetof(Ppu, oam) };
// A register snapshot, never a pointer-owning PPU. All game memory is retained
// once separately. This also preserves the exact core's extended-window rules.
typedef struct { uint8_t bytes[PICA_LINE_BYTES]; } PicaLine;
typedef struct {
  uint32_t key, checked, used;
  uint16_t source[16], palette[16];
  bool valid, opaque;
} PicaTile;
typedef struct {
  PicaTile tile[PICA_SLOTS];
  uint16_t hash[PICA_HASH];
  uint32_t frame, cursor, hits, decodes, live;
  uint32_t dirty[PICA_SLOTS/32];
  uint8_t objectColumns[128][PICA_MAX_LINES];
} PicaAtlas;
typedef struct {
  int16_t x0,y0,x1,y1;
  int16_t u0,v0,u1,v1; // normalized UV * 4096
  uint16_t depth;
  uint8_t r,g,b,a;
} PicaQuad;
// Groups 0/2: main/sub backdrop and BG. 1/3: their OBJ, in OAM order.
// Groups 4..19: compose flags subtract=1 half=2 clip=4 prevent=8.
typedef bool PicaEmit(void *context, unsigned group, const PicaQuad *quad);
typedef struct {
  const Ppu *memory;
  const PicaLine *lines;
  Ppu *scratch;
  PicaAtlas *atlas;
  uint32_t *pixels;
  unsigned width,height;
  PicaEmit *emit;
  void *context;
  const char *failure;
  uint32_t quads[PICA_GROUPS];
  uint16_t bandEnd[PICA_MAX_LINES],commonEnd[PICA_MAX_LINES],layerEnd[PICA_MAX_LINES];
  bool sharedWindow;
} PicaFrame;
void PicaAtlasInit(PicaAtlas *cache, uint32_t *pixels);
void PicaAtlasBegin(PicaAtlas *cache);
void PicaCaptureLine(PicaLine *out, const Ppu *ppu, unsigned y);
bool PicaBuildFrame(PicaFrame *frame);
uint16_t PicaComposeRgb5(uint16_t main, uint16_t sub, bool eligible,
                        bool sub_pixel, unsigned flags);
