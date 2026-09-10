#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include "ppu.h"
#include "ppu_bg_span.h"
#include "ppu_retained.h"
#include "src/types.h"

#ifdef __3DS__
extern uint64_t svcGetSystemTick(void);
static inline uint64_t PpuProfileTick(void) { return svcGetSystemTick(); }
#else
static inline uint64_t PpuProfileTick(void) { return 0; }
#endif
static inline bool PpuProfileActive(const Ppu *p) {
  return (p->renderFlags & kPpuRenderFlags_Old3DS) && p->phase.active;
}

static const uint8 kSpriteSizes[8][2] = {
  {8, 16}, {8, 32}, {8, 64}, {16, 32},
  {16, 64}, {32, 64}, {16, 32}, {16, 32}
};

static void ppu_handlePixel(Ppu* ppu, int x, int y);
static int ppu_getPixel(Ppu* ppu, int x, int y, bool sub, int* r, int* g, int* b);
static int ppu_getPixelForBgLayer(Ppu *ppu, int x, int y, int layer, bool priority);
static void ppu_calculateMode7Starts(Ppu* ppu, int y);
static int ppu_getPixelForMode7(Ppu* ppu, int x, int layer, bool priority);
static bool ppu_getWindowState(Ppu* ppu, int layer, int x);
static bool ppu_evaluateSprites(Ppu* ppu, int line);
static bool PpuEvaluateVisibleSprites(Ppu *ppu, int line);
static void PpuDrawWholeLine(Ppu *ppu, uint y);

#define IS_SCREEN_ENABLED(ppu, sub, layer) (ppu->screenEnabled[sub] & (1 << layer))
#define IS_SCREEN_WINDOWED(ppu, sub, layer) (ppu->screenWindowed[sub] & (1 << layer))
#define IS_MOSAIC_ENABLED(ppu, layer) ((ppu->mosaicEnabled & (1 << layer)))
#define GET_WINDOW_FLAGS(ppu, layer) (ppu->windowsel >> (layer * 4))
enum {
  kWindow1Inversed = 1,
  kWindow1Enabled = 2,
  kWindow2Inversed = 4,
  kWindow2Enabled = 8,
};

Ppu* ppu_init() {
  Ppu* ppu = (Ppu *)calloc(1, sizeof(Ppu));
  if (!ppu)
    return NULL;
  ppu->tileCache = (PpuTileCache *)calloc(1, sizeof(PpuTileCache));
  if (!ppu->tileCache) {
    free(ppu);
    return NULL;
  }
  ppu->extraLeftRight = kPpuExtraLeftRight;
  return ppu;
}

void ppu_free(Ppu* ppu) {
  if (!ppu)
    return;
  free(ppu->retained);
  free(ppu->tileCache);
  free(ppu);
}

void PpuUpdateCgram(Ppu *ppu, const uint16_t *colors) {
  if (memcmp(ppu->cgram, colors, sizeof(ppu->cgram)) == 0)
    return;
  memcpy(ppu->cgram, colors, sizeof(ppu->cgram));
  ppu->colorMapDirty = true;
  ppu->fixedMathValid = false; ppu->backdropMathValid = false;
}

void ppu_reset(Ppu* ppu) {
  memset(ppu->vram, 0, sizeof(ppu->vram));
  ppu->lastBrightnessMult = 0xff;
  ppu->subscreenMathKey = 0xff;
  ppu->lastMosaicModulo = 0xff;
  ppu->extraLeftCur = 0;
  ppu->extraRightCur = 0;
  ppu->extraBottomCur = 0;
  ppu->renderObjXOffset = 0;
  ppu->renderObjYOffset = 0;
  ppu->spriteLinesValid = false;
  ppu->windowExtLeft = ppu->windowExtRight = NULL;
  ppu->vramPointer = 0;
  ppu->vramIncrementOnHigh = false;
  ppu->vramIncrement = 1;
  memset(ppu->cgram, 0, sizeof(ppu->cgram));
  ppu->cgramPointer = 0;
  ppu->cgramSecondWrite = false;
  ppu->colorMapDirty = true;
  ppu->fixedMathValid = false; ppu->backdropMathValid = false;
  ppu->cgramBuffer = 0;
  memset(ppu->oam, 0, sizeof(ppu->oam));
  ppu->oamAdr = 0;
  ppu->oamSecondWrite = false;
  ppu->oamBuffer = 0;
  ppu->objTileAdr1 = 0x4000;
  ppu->objTileAdr2 = 0x5000;
  ppu->objSize = 0;
  memset(&ppu->objBuffer, 0, sizeof(ppu->objBuffer));
  for(int i = 0; i < 4; i++) {
    ppu->bgLayer[i].hScroll = 0;
    ppu->bgLayer[i].vScroll = 0;
    ppu->bgLayer[i].tilemapWider = false;
    ppu->bgLayer[i].tilemapHigher = false;
    ppu->bgLayer[i].tilemapAdr = 0;
    ppu->bgLayer[i].tileAdr = 0;
  }
  ppu->scrollPrev = 0;
  ppu->scrollPrev2 = 0;
  ppu->mosaicSize = 1;
  ppu->screenEnabled[0] = ppu->screenEnabled[1] = 0;
  ppu->screenWindowed[0] = ppu->screenWindowed[1] = 0;
  memset(ppu->m7matrix, 0, sizeof(ppu->m7matrix));
  ppu->m7prev = 0;
  ppu->m7largeField = true;
  ppu->m7charFill = false;
  ppu->m7xFlip = false;
  ppu->m7yFlip = false;
  ppu->m7extBg_always_zero = false;
  ppu->m7startX = 0;
  ppu->m7startY = 0;
  ppu->windowsel = 0;
  ppu->window1left = 0;
  ppu->window1right = 0;
  ppu->window2left = 0;
  ppu->window2right = 0;
  ppu->clipMode = 0;
  ppu->preventMathMode = 0;
  ppu->addSubscreen = false;
  ppu->subtractColor = false;
  ppu->halfColor = false;
  ppu->mathEnabled = 0;
  ppu->fixedColorR = 0;
  ppu->fixedColorG = 0;
  ppu->fixedColorB = 0;
  ppu->forcedBlank = true;
  ppu->brightness = 0;
  ppu->mode = 0;
}

void ppu_saveload(Ppu *ppu, SaveLoadFunc *func, void *ctx) {
  uint8 tmp[556] = { 0 };

  func(ctx, &ppu->vram, 0x8000 * 2);
  func(ctx, tmp, 10);
  func(ctx, &ppu->cgram, 512);
  func(ctx, tmp, 556);
  ppu->lastBrightnessMult = 0xff;
  ppu->subscreenMathKey = 0xff;
  ppu->colorMapDirty = true;
  ppu->fixedMathValid = false; ppu->backdropMathValid = false;
  func(ctx, tmp, 520);
  for (int i = 0; i < 4; i++) {
    func(ctx, tmp, 4);
    func(ctx, &ppu->bgLayer[i].tilemapWider, 4);
    func(ctx, tmp, 4);
  }
  func(ctx, tmp, 123);
}

int PpuGetCurrentRenderScale(Ppu *ppu, uint32_t render_flags) {
  bool hq = ppu->mode == 7 && !ppu->forcedBlank &&
    (render_flags & (kPpuRenderFlags_4x4Mode7 | kPpuRenderFlags_NewRenderer)) == (kPpuRenderFlags_4x4Mode7 | kPpuRenderFlags_NewRenderer);
  return hq ? 4 : 1;
}

// Build once per Old frame instead of scanning all 128 OAM entries for each
// scanline. Bit order preserves OAM priority and sprite/tile-limit behavior.
static void PpuBuildSpriteLines(Ppu *ppu) {
  memset(ppu->spriteLines, 0, sizeof(ppu->spriteLines));
  for (unsigned sprite = 0; sprite < 128; sprite++) {
    unsigned index = sprite * 2;
    unsigned y = ppu->oam[index] >> 8;
    if (y == 0xf0) continue; // Same hidden-sprite convention as the base PPU.
    unsigned high = ppu->oam[0x100 + (index >> 4)] >> (index & 15);
    unsigned size = kSpriteSizes[ppu->objSize][(high >> 1) & 1];
    for (unsigned row = 0; row < size; row++)
      ppu->spriteLines[(y + row) & 255][sprite >> 5] |= 1u << (sprite & 31);
  }
  ppu->spriteLinesValid = true;
}

void PpuBeginDrawing(Ppu *ppu, uint8_t *pixels, size_t pitch, uint32_t render_flags) {
  ppu->renderFlags = render_flags;
  ppu->renderPitch = (uint)pitch;
  ppu->renderBuffer = pixels;
  ppu->renderObjXOffset = 0;
  ppu->renderObjYOffset = 0;
  uint64_t prepare_start = 0;
  if (render_flags & kPpuRenderFlags_Old3DS) {
    uint32_t frame = ppu->phase.frame + 1;
    memset(&ppu->phase, 0, sizeof(ppu->phase));
    ppu->phase.frame = frame;
    ppu->phase.active = (frame & 63) == 1;
    if (ppu->phase.active) prepare_start = PpuProfileTick();
    if (!ppu->retainedAttempted) {
      ppu->retainedAttempted = true;
      ppu->retained = calloc(1, sizeof(PpuRetainedMaps));
    }
    ppu->retainedUsable = ppu->retained != NULL && ppu->mode == 1;
    PpuRetainMaps(ppu);
    if (ppu->retained) ppu->phase.rebuiltTiles = ppu->retained->rebuiltTiles;
    PpuBuildSpriteLines(ppu);
  }

  // Cache the brightness computation
  if (ppu->brightness != ppu->lastBrightnessMult) {
    uint8_t ppu_brightness = ppu->brightness;
    ppu->lastBrightnessMult = ppu_brightness;
    ppu->colorMapDirty = true;
    ppu->fixedMathValid = false; ppu->backdropMathValid = false;
    for (int i = 0; i < 32; i++)
      ppu->brightnessMultHalf[i * 2] = ppu->brightnessMultHalf[i * 2 + 1] = ppu->brightnessMult[i] =
      ((i << 3) | (i >> 2)) * ppu_brightness / 15;
    // Store 31 extra entries to remove the need for clamping to 31.
    memset(&ppu->brightnessMult[32], ppu->brightnessMult[31], 31);
  }

  if (ppu->colorMapDirty) {
    ppu->fixedMathValid = false; ppu->backdropMathValid = false;
    ppu->colorMapDirty = false;
    for (int i = 0; i < 256; i++) {
      uint32 color = ppu->cgram[i];
      if (render_flags & kPpuRenderFlags_Old3DS)
        ppu->colorMapRgb5Spaced[i] = (color & 31) << 16 |
          ((color >> 5) & 31) << 8 | ((color >> 10) & 31);
      ppu->colorMapRgb[i] =
        ppu->brightnessMult[color & 0x1f] << 16 |
        ppu->brightnessMult[(color >> 5) & 0x1f] << 8 |
        ppu->brightnessMult[(color >> 10) & 0x1f];
    }
  }
  if (PpuProfileActive(ppu)) ppu->phase.prepare = PpuProfileTick() - prepare_start;
}

static inline void ClearBackdrop(Ppu *ppu, PpuPixelPrioBufs *buf) {
  size_t first = kPpuExtraLeftRight - ppu->extraLeftCur;
  size_t count = 256 + ppu->extraLeftCur + ppu->extraRightCur;
  PpuZbufType *data = &buf->data[first];
  size_t i = 0;
  if (((uintptr_t)data & 2) && i < count)
    data[i++] = 0x0500;
  for (; i + 2 <= count; i += 2)
    *(uint32 *)&data[i] = 0x05000500;
  for (; i < count; i++)
    data[i] = 0x0500;
}

static inline uint32 PpuGetCached4bppRow(Ppu *ppu, int address) {
  uint index = (uint)address & 0x7fff;
  uint32 bits =
    ppu->vram[index] | ppu->vram[(index + 8) & 0x7fff] << 16;
  PpuTileCache *cache = ppu->tileCache;
  if (cache->keys[index] != bits) {
    uint32 pixels = 0;
    for (int i = 0; i < 8; i++) {
      uint32 pixel =
        (bits >> i) & 1 |
        (bits >> (7 + i)) & 2 |
        (bits >> (14 + i)) & 4 |
        (bits >> (21 + i)) & 8;
      pixels |= pixel << (i * 4);
    }
    cache->keys[index] = bits;
    cache->pixels[index] = pixels;
  }
  return cache->pixels[index];
}


void ppu_runLine(Ppu *ppu, int line) {
  if(line != 0) {
    if (ppu->mosaicSize != ppu->lastMosaicModulo) {
      int mod = ppu->mosaicSize;
      ppu->lastMosaicModulo = mod;
      for (int i = 0, j = 0; i < countof(ppu->mosaicModulo); i++) {
        ppu->mosaicModulo[i] = i - j;
        j = (j + 1 == mod ? 0 : j + 1);
      }
    }
    // outside of visible range?
    if (line >= 225 + ppu->extraBottomCur) {
      memset(&ppu->renderBuffer[(line - 1) * ppu->renderPitch], 0,
             sizeof(uint32) * (256 + ppu->extraLeftRight * 2));
      return;
    }

    bool profile = PpuProfileActive(ppu);
    uint64_t sprite_start = profile ? PpuProfileTick() : 0;
    if (profile) ppu->phase.lines++;
    // Sprite evaluation is an OAM scan plus a full priority-buffer clear. Do
    // neither when OBJ is disabled on both main and subscreen.
    bool sprites_enabled =
      (ppu->screenEnabled[0] | ppu->screenEnabled[1]) & (1 << 4);
    if (!ppu->forcedBlank && sprites_enabled) {
      ClearBackdrop(ppu, &ppu->objBuffer);
      ppu->lineHasSprites = (ppu->renderFlags & kPpuRenderFlags_Old3DS) &&
                            ppu->spriteLinesValid ?
        PpuEvaluateVisibleSprites(ppu, line - 1) : ppu_evaluateSprites(ppu, line - 1);
    } else {
      ppu->lineHasSprites = false;
    }

    if (profile) ppu->phase.sprites += PpuProfileTick() - sprite_start;
    if (ppu->renderFlags & kPpuRenderFlags_NewRenderer) {
      PpuDrawWholeLine(ppu, line);
    } else {
      if (ppu->mode == 7)
        ppu_calculateMode7Starts(ppu, line);
      for (int x = 0; x < 256; x++)
        ppu_handlePixel(ppu, x, line);

      uint8 *dst = ppu->renderBuffer + ((line - 1) * ppu->renderPitch);
      if (ppu->extraLeftRight != 0) {
        memset(dst, 0, sizeof(uint32) * ppu->extraLeftRight);
        memset(dst + sizeof(uint32) * (256 + ppu->extraLeftRight), 0,
               sizeof(uint32) * ppu->extraLeftRight);
      }
    }
  }
}

typedef PpuWindowSpans PpuWindows;

static void PpuWindows_Clear(PpuWindows *win, Ppu *ppu, uint layer) {
  win->edges[0] = -(layer != 2 ? ppu->extraLeftCur : 0);
  win->edges[1] = 256 + (layer != 2 ? ppu->extraRightCur : 0);
  win->nr = 1;
  win->bits = 0;
}

static void PpuWindows_Calc(PpuWindows *win, Ppu *ppu, uint layer) {
  // Evaluate which spans to render based on the window settings.
  // There are at most 5 windows.
  // Algorithm from Snes9x
  uint32 winflags = GET_WINDOW_FLAGS(ppu, layer);
  uint nr = 1;
  int window_right = 256 + (layer != 2 ? ppu->extraRightCur : 0);
  win->edges[0] = - (layer != 2 ? ppu->extraLeftCur : 0);
  win->edges[1] = window_right;
  int w1_left = ppu->window1left, w1_right = ppu->window1right;
  if (ppu->windowExtLeft) {
    w1_left = IntMax(ppu->windowExtLeftCur, win->edges[0]);
    w1_right = IntMin(ppu->windowExtRightCur, window_right - 1);
  }
  uint i, j;
  int t;
  bool w1_ena = (winflags & kWindow1Enabled) && w1_left <= w1_right;
  if (w1_ena) {
    if (w1_left > win->edges[0]) {
      win->edges[nr] = w1_left;
      win->edges[++nr] = window_right;
    }
    if (w1_right + 1 < window_right) {
      win->edges[nr] = w1_right + 1;
      win->edges[++nr] = window_right;
    }
  }
  bool w2_ena = (winflags & kWindow2Enabled) && ppu->window2left <= ppu->window2right;
  if (w2_ena) {
    for (i = 0; i <= nr && (t = ppu->window2left) != win->edges[i]; i++) {
      if (t < win->edges[i]) {
        for (j = nr++; j >= i; j--)
          win->edges[j + 1] = win->edges[j];
        win->edges[i] = t;
        break;
      }
    }
    for (; i <= nr && (t = ppu->window2right + 1) != win->edges[i]; i++) {
      if (t < win->edges[i]) {
        for (j = nr++; j >= i; j--)
          win->edges[j + 1] = win->edges[j];
        win->edges[i] = t;
        break;
      }
    }
  }
  win->nr = nr;
  // get a bitmap of how regions map to windows
  uint8 w1_bits = 0, w2_bits = 0;
  if (w1_ena) {
    for (i = 0; win->edges[i] != w1_left; i++);
    for (j = i; win->edges[j] != w1_right + 1; j++);
    w1_bits = ((1 << (j - i)) - 1) << i;
  }
  if ((winflags & (kWindow1Enabled | kWindow1Inversed)) == (kWindow1Enabled | kWindow1Inversed))
    w1_bits = ~w1_bits;
  if (w2_ena) {
    for (i = 0; win->edges[i] != ppu->window2left; i++);
    for (j = i; win->edges[j] != ppu->window2right + 1; j++);
    w2_bits = ((1 << (j - i)) - 1) << i;
  }
  if ((winflags & (kWindow2Enabled | kWindow2Inversed)) == (kWindow2Enabled | kWindow2Inversed))
    w2_bits = ~w2_bits;
  win->bits = w1_bits | w2_bits;
}

void PpuGetWindowSpans(Ppu *p, unsigned layer, bool enabled, PpuWindowSpans *out) {
  if (enabled) PpuWindows_Calc(out, p, layer);
  else PpuWindows_Clear(out, p, layer);
}

static bool PpuDrawRetainedBackground(Ppu *ppu, unsigned y, bool sub, unsigned layer) {
  if (!(ppu->renderFlags & kPpuRenderFlags_Old3DS) || !ppu->retainedUsable || !ppu->retained) return false;
  const PpuRetainedLayer *plane=&ppu->retained->layers[layer];
  const BgLayer *bg=&ppu->bgLayer[layer];
  if (!plane->valid || plane->mapAddress!=bg->tilemapAdr || plane->tileAddress!=bg->tileAdr ||
      plane->width!=(bg->tilemapWider?512:256) || plane->height!=(bg->tilemapHigher?512:256)) return false;
  if (PpuProfileActive(ppu)) ppu->phase.retainedRows++;
  y=(y+bg->vScroll)&(plane->height-1);
  if(!plane->active[y]) return true;
  PpuWindows win;
  IS_SCREEN_WINDOWED(ppu,sub,layer)?PpuWindows_Calc(&win,ppu,layer):PpuWindows_Clear(&win,ppu,layer);
  for(unsigned i=0;i<win.nr;i++) {
    if(win.bits&(1u<<i))continue;
    unsigned x=(win.edges[i]+bg->hScroll)&(plane->width-1),n=win.edges[i+1]-win.edges[i];
    uint16_t *dst=ppu->bgBuffers[sub].data+win.edges[i]+kPpuExtraLeftRight;
    while(n) {
      unsigned span=IntMin(n,plane->width-x);
      PpuMergeSpan(dst,plane->pixels+y*512+x,span,layer);
      dst+=span;n-=span;x=0;
    }
  }
  return true;
}

// Draw a whole line of a 4bpp background layer into bgBuffers
static void PpuDrawBackground_4bpp(Ppu *ppu, uint y, bool sub, uint layer, PpuZbufType zhi, PpuZbufType zlo) {
#define DO_PIXEL(i) do { \
  pixel = (pixels >> ((i) * 4)) & 0xf; \
  if (pixel && z > dstz[i]) dstz[i] = z + pixel; } while (0)
#define DO_PIXEL_HFLIP(i) do { \
  pixel = (pixels >> ((7 - (i)) * 4)) & 0xf; \
  if (pixel && z > dstz[i]) dstz[i] = z + pixel; } while (0)
#define READ_PIXELS(ta, tile) PpuGetCached4bppRow(ppu, (ta) + (tile) * 16)
  enum { kPaletteShift = 6 };
  if (!IS_SCREEN_ENABLED(ppu, sub, layer))
    return;  // layer is completely hidden
  if (PpuDrawRetainedBackground(ppu,y,sub,layer)) return;
  PpuWindows win;
  IS_SCREEN_WINDOWED(ppu, sub, layer) ? PpuWindows_Calc(&win, ppu, layer) : PpuWindows_Clear(&win, ppu, layer);
  BgLayer *bglayer = &ppu->bgLayer[layer];
  y += bglayer->vScroll;
  int sc_offs = bglayer->tilemapAdr + (((y >> 3) & 0x1f) << 5);
  if ((y & 0x100) && bglayer->tilemapHigher)
    sc_offs += bglayer->tilemapWider ? 0x800 : 0x400;
  const uint16 *tps[2] = {
    &ppu->vram[sc_offs & 0x7fff],
    &ppu->vram[sc_offs + (bglayer->tilemapWider ? 0x400 : 0) & 0x7fff]
  };
  int tileadr = ppu->bgLayer[layer].tileAdr, pixel;
  int tileadr1 = tileadr + 7 - (y & 0x7), tileadr0 = tileadr + (y & 0x7);
  for (size_t windex = 0; windex < win.nr; windex++) {
    if (win.bits & (1 << windex))
      continue;  // layer is disabled for this window part
    uint x = win.edges[windex] + bglayer->hScroll;
    uint w = win.edges[windex + 1] - win.edges[windex];
    PpuZbufType *dstz = ppu->bgBuffers[sub].data + win.edges[windex] + kPpuExtraLeftRight;
    const uint16 *tp = tps[x >> 8 & 1] + ((x >> 3) & 0x1f);
    const uint16 *tp_last = tps[x >> 8 & 1] + 31;
    const uint16 *tp_next = tps[(x >> 8 & 1) ^ 1];
#define NEXT_TP() if (tp != tp_last) tp += 1; else tp = tp_next, tp_next = tp_last - 31, tp_last = tp + 31;
    // Handle clipped pixels on left side
    if (x & 7) {
      int curw = IntMin(8 - (x & 7), w);
      w -= curw;
      uint32 tile = *tp;
      NEXT_TP();
      int ta = (tile & 0x8000) ? tileadr1 : tileadr0;
      PpuZbufType z = (tile & 0x2000) ? zhi : zlo;
      uint32 pixels = READ_PIXELS(ta, tile & 0x3ff);
      if (pixels) {
        z += ((tile & 0x1c00) >> kPaletteShift);
        if (tile & 0x4000) {
          pixels >>= (x & 7) * 4, x += curw;
          do DO_PIXEL(0); while (pixels >>= 4, dstz++, --curw);
        } else {
          pixels <<= (x & 7) * 4, x += curw;
          do DO_PIXEL_HFLIP(0); while (pixels <<= 4, dstz++, --curw);
        }
      } else {
        dstz += curw;
      }
    }
    // Handle full tiles in the middle
    while (w >= 8) {
      uint32 tile = *tp;
      NEXT_TP();
      int ta = (tile & 0x8000) ? tileadr1 : tileadr0;
      PpuZbufType z = (tile & 0x2000) ? zhi : zlo;
      uint32 pixels = READ_PIXELS(ta, tile & 0x3ff);
      if (pixels) {
        z += ((tile & 0x1c00) >> kPaletteShift);
        if ((ppu->renderFlags & kPpuRenderFlags_Old3DS) && PpuRowIsOpaque(pixels)) {
          PpuDrawOpaqueBgRow(dstz, pixels, z, (tile & 0x4000) != 0);
        } else if (tile & 0x4000) {
          DO_PIXEL(0); DO_PIXEL(1); DO_PIXEL(2); DO_PIXEL(3);
          DO_PIXEL(4); DO_PIXEL(5); DO_PIXEL(6); DO_PIXEL(7);
        } else {
          DO_PIXEL_HFLIP(0); DO_PIXEL_HFLIP(1); DO_PIXEL_HFLIP(2); DO_PIXEL_HFLIP(3);
          DO_PIXEL_HFLIP(4); DO_PIXEL_HFLIP(5); DO_PIXEL_HFLIP(6); DO_PIXEL_HFLIP(7);
        }
      }
      dstz += 8, w -= 8;
    }
    // Handle remaining clipped part
    if (w) {
      uint32 tile = *tp;
      int ta = (tile & 0x8000) ? tileadr1 : tileadr0;
      PpuZbufType z = (tile & 0x2000) ? zhi : zlo;
      uint32 pixels = READ_PIXELS(ta, tile & 0x3ff);
      if (pixels) {
        z += ((tile & 0x1c00) >> kPaletteShift);
        if (tile & 0x4000) {
          do DO_PIXEL(0); while (pixels >>= 4, dstz++, --w);
        } else {
          do DO_PIXEL_HFLIP(0); while (pixels <<= 4, dstz++, --w);
        }
      }
    }
  }
#undef READ_PIXELS
#undef DO_PIXEL
#undef DO_PIXEL_HFLIP
}

// Draw a whole line of a 2bpp background layer into bgBuffers
static void PpuDrawBackground_2bpp(Ppu *ppu, uint y, bool sub, uint layer, PpuZbufType zhi, PpuZbufType zlo) {
#define DO_PIXEL(i) do { \
  pixel = (pixels >> ((i) * 4)) & 3; \
  if (pixel && z > dstz[i]) dstz[i] = z + pixel; } while (0)
#define DO_PIXEL_HFLIP(i) do { \
  pixel = (pixels >> ((7 - (i)) * 4)) & 3; \
  if (pixel && z > dstz[i]) dstz[i] = z + pixel; } while (0)
#define READ_PIXELS(ta, tile) PpuGetCached4bppRow(ppu, (ta) + (tile) * 8)
  enum { kPaletteShift = 8 };
  if (!IS_SCREEN_ENABLED(ppu, sub, layer))
    return;  // layer is completely hidden
  if (PpuDrawRetainedBackground(ppu,y,sub,layer)) return;
  PpuWindows win;
  IS_SCREEN_WINDOWED(ppu, sub, layer) ? PpuWindows_Calc(&win, ppu, layer) : PpuWindows_Clear(&win, ppu, layer);
  BgLayer *bglayer = &ppu->bgLayer[layer];
  y += bglayer->vScroll;
  int sc_offs = bglayer->tilemapAdr + (((y >> 3) & 0x1f) << 5);
  if ((y & 0x100) && bglayer->tilemapHigher)
    sc_offs += bglayer->tilemapWider ? 0x800 : 0x400;
  const uint16 *tps[2] = {
    &ppu->vram[sc_offs & 0x7fff],
    &ppu->vram[sc_offs + (bglayer->tilemapWider ? 0x400 : 0) & 0x7fff]
  };
  int tileadr = ppu->bgLayer[layer].tileAdr, pixel;
  int tileadr1 = tileadr + 7 - (y & 0x7), tileadr0 = tileadr + (y & 0x7);

  for (size_t windex = 0; windex < win.nr; windex++) {
    if (win.bits & (1 << windex))
      continue;  // layer is disabled for this window part
    uint x = win.edges[windex] + bglayer->hScroll;
    uint w = win.edges[windex + 1] - win.edges[windex];
    PpuZbufType *dstz = ppu->bgBuffers[sub].data + win.edges[windex] + kPpuExtraLeftRight;
    const uint16 *tp = tps[x >> 8 & 1] + ((x >> 3) & 0x1f);
    const uint16 *tp_last = tps[x >> 8 & 1] + 31;
    const uint16 *tp_next = tps[(x >> 8 & 1) ^ 1];

#define NEXT_TP() if (tp != tp_last) tp += 1; else tp = tp_next, tp_next = tp_last - 31, tp_last = tp + 31;
    // Handle clipped pixels on left side
    if (x & 7) {
      int curw = IntMin(8 - (x & 7), w);
      w -= curw;
      uint32 tile = *tp;
      NEXT_TP();
      int ta = (tile & 0x8000) ? tileadr1 : tileadr0;
      PpuZbufType z = (tile & 0x2000) ? zhi : zlo;
      uint32 pixels = READ_PIXELS(ta, tile & 0x3ff);
      if (pixels & 0x33333333) {
        z += ((tile & 0x1c00) >> kPaletteShift);
        if (tile & 0x4000) {
          pixels >>= (x & 7) * 4, x += curw;
          do DO_PIXEL(0); while (pixels >>= 4, dstz++, --curw);
        } else {
          pixels <<= (x & 7) * 4, x += curw;
          do DO_PIXEL_HFLIP(0); while (pixels <<= 4, dstz++, --curw);
        }
      } else {
        dstz += curw;
      }
    }
    // Handle full tiles in the middle
    while (w >= 8) {
      uint32 tile = *tp;
      NEXT_TP();
      int ta = (tile & 0x8000) ? tileadr1 : tileadr0;
      PpuZbufType z = (tile & 0x2000) ? zhi : zlo;
      uint32 pixels = READ_PIXELS(ta, tile & 0x3ff);
      if (pixels & 0x33333333) {
        z += ((tile & 0x1c00) >> kPaletteShift);
        if ((ppu->renderFlags & kPpuRenderFlags_Old3DS) && PpuRowIsOpaque((pixels & 0x33333333u))) {
          PpuDrawOpaqueBgRow(dstz, (pixels & 0x33333333u), z, (tile & 0x4000) != 0);
        } else if (tile & 0x4000) {
          DO_PIXEL(0); DO_PIXEL(1); DO_PIXEL(2); DO_PIXEL(3);
          DO_PIXEL(4); DO_PIXEL(5); DO_PIXEL(6); DO_PIXEL(7);
        } else {
          DO_PIXEL_HFLIP(0); DO_PIXEL_HFLIP(1); DO_PIXEL_HFLIP(2); DO_PIXEL_HFLIP(3);
          DO_PIXEL_HFLIP(4); DO_PIXEL_HFLIP(5); DO_PIXEL_HFLIP(6); DO_PIXEL_HFLIP(7);
        }
      }
      dstz += 8, w -= 8;
    }
    // Handle remaining clipped part
    if (w) {
      uint32 tile = *tp;
      int ta = (tile & 0x8000) ? tileadr1 : tileadr0;
      PpuZbufType z = (tile & 0x2000) ? zhi : zlo;
      uint32 pixels = READ_PIXELS(ta, tile & 0x3ff);
      if (pixels & 0x33333333) {
        z += ((tile & 0x1c00) >> kPaletteShift);
        if (tile & 0x4000) {
          do DO_PIXEL(0); while (pixels >>= 4, dstz++, --w);
        } else {
          do DO_PIXEL_HFLIP(0); while (pixels <<= 4, dstz++, --w);
        }
      }
    }
  }
#undef NEXT_TP
#undef READ_PIXELS
#undef DO_PIXEL
#undef DO_PIXEL_HFLIP
}

// Draw a whole line of a 4bpp background layer into bgBuffers, with mosaic applied
static void PpuDrawBackground_4bpp_mosaic(Ppu *ppu, uint y, bool sub, uint layer, PpuZbufType zhi, PpuZbufType zlo) {
#define GET_PIXEL() pixel = (bits) & 1 | (bits >> 7) & 2 | (bits >> 14) & 4 | (bits >> 21) & 8
#define GET_PIXEL_HFLIP() pixel = (bits >> 7) & 1 | (bits >> 14) & 2 | (bits >> 21) & 4 | (bits >> 28) & 8
#define READ_BITS(ta, tile) (addr = &ppu->vram[((ta) + (tile) * 16) & 0x7fff], addr[0] | addr[8] << 16)
  enum { kPaletteShift = 6 };
  if (!IS_SCREEN_ENABLED(ppu, sub, layer))
    return;  // layer is completely hidden
  PpuWindows win;
  IS_SCREEN_WINDOWED(ppu, sub, layer) ? PpuWindows_Calc(&win, ppu, layer) : PpuWindows_Clear(&win, ppu, layer);
  BgLayer *bglayer = &ppu->bgLayer[layer];
  y = ppu->mosaicModulo[y] + bglayer->vScroll;
  int sc_offs = bglayer->tilemapAdr + (((y >> 3) & 0x1f) << 5);
  if ((y & 0x100) && bglayer->tilemapHigher)
    sc_offs += bglayer->tilemapWider ? 0x800 : 0x400;
  const uint16 *tps[2] = {
    &ppu->vram[sc_offs & 0x7fff],
    &ppu->vram[sc_offs + (bglayer->tilemapWider ? 0x400 : 0) & 0x7fff]
  };
  int tileadr = ppu->bgLayer[layer].tileAdr, pixel;
  int tileadr1 = tileadr + 7 - (y & 0x7), tileadr0 = tileadr + (y & 0x7);
  const uint16 *addr;
  for (size_t windex = 0; windex < win.nr; windex++) {
    if (win.bits & (1 << windex))
      continue;  // layer is disabled for this window part
    int sx = win.edges[windex];
    PpuZbufType *dstz = ppu->bgBuffers[sub].data + sx + kPpuExtraLeftRight;
    PpuZbufType *dstz_end = ppu->bgBuffers[sub].data + win.edges[windex + 1] + kPpuExtraLeftRight;
    uint x = sx + bglayer->hScroll;
    const uint16 *tp = tps[x >> 8 & 1] + ((x >> 3) & 0x1f);
    const uint16 *tp_last = tps[x >> 8 & 1] + 31, *tp_next = tps[(x >> 8 & 1) ^ 1];
    x &= 7;
    int w = ppu->mosaicSize - (sx - ppu->mosaicModulo[sx]);
    do {
      w = IntMin(w, dstz_end - dstz);
      uint32 tile = *tp;
      int ta = (tile & 0x8000) ? tileadr1 : tileadr0;
      PpuZbufType z = (tile & 0x2000) ? zhi : zlo;
      uint32 bits = READ_BITS(ta, tile & 0x3ff);
      if (tile & 0x4000) bits >>= x, GET_PIXEL(); else bits <<= x, GET_PIXEL_HFLIP();
      if (pixel) {
        pixel += (tile & 0x1c00) >> kPaletteShift;
        int i = 0;
        do {
          if (z > dstz[i])
            dstz[i] = pixel + z;
        } while (++i != w);
      }
      dstz += w, x += w;
      for (; x >= 8; x -= 8)
        tp = (tp != tp_last) ? tp + 1 : tp_next;
      w = ppu->mosaicSize;
    } while (dstz_end - dstz != 0);
  }
#undef READ_BITS
#undef GET_PIXEL
#undef GET_PIXEL_HFLIP
}

// Draw a whole line of a 2bpp background layer into bgBuffers, with mosaic applied
static void PpuDrawBackground_2bpp_mosaic(Ppu *ppu, int y, bool sub, uint layer, PpuZbufType zhi, PpuZbufType zlo) {
#define GET_PIXEL() pixel = (bits) & 1 | (bits >> 7) & 2
#define GET_PIXEL_HFLIP() pixel = (bits >> 7) & 1 | (bits >> 14) & 2
#define READ_BITS(ta, tile) (addr = &ppu->vram[((ta) + (tile) * 8) & 0x7fff], addr[0])
  enum { kPaletteShift = 8 };
  if (!IS_SCREEN_ENABLED(ppu, sub, layer))
    return;  // layer is completely hidden
  PpuWindows win;
  IS_SCREEN_WINDOWED(ppu, sub, layer) ? PpuWindows_Calc(&win, ppu, layer) : PpuWindows_Clear(&win, ppu, layer);
  BgLayer *bglayer = &ppu->bgLayer[layer];
  y = ppu->mosaicModulo[y] + bglayer->vScroll;
  int sc_offs = bglayer->tilemapAdr + (((y >> 3) & 0x1f) << 5);
  if ((y & 0x100) && bglayer->tilemapHigher)
    sc_offs += bglayer->tilemapWider ? 0x800 : 0x400;
  const uint16 *tps[2] = {
    &ppu->vram[sc_offs & 0x7fff],
    &ppu->vram[sc_offs + (bglayer->tilemapWider ? 0x400 : 0) & 0x7fff]
  };
  int tileadr = ppu->bgLayer[layer].tileAdr, pixel;
  int tileadr1 = tileadr + 7 - (y & 0x7), tileadr0 = tileadr + (y & 0x7);
  const uint16 *addr;
  for (size_t windex = 0; windex < win.nr; windex++) {
    if (win.bits & (1 << windex))
      continue;  // layer is disabled for this window part
    int sx = win.edges[windex];
    PpuZbufType *dstz = ppu->bgBuffers[sub].data + sx + kPpuExtraLeftRight;
    PpuZbufType *dstz_end = ppu->bgBuffers[sub].data + win.edges[windex + 1] + kPpuExtraLeftRight;
    uint x = sx + bglayer->hScroll;
    const uint16 *tp = tps[x >> 8 & 1] + ((x >> 3) & 0x1f);
    const uint16 *tp_last = tps[x >> 8 & 1] + 31, *tp_next = tps[(x >> 8 & 1) ^ 1];
    x &= 7;
    int w = ppu->mosaicSize - (sx - ppu->mosaicModulo[sx]);
    do {
      w = IntMin(w, dstz_end - dstz);
      uint32 tile = *tp;
      int ta = (tile & 0x8000) ? tileadr1 : tileadr0;
      PpuZbufType z = (tile & 0x2000) ? zhi : zlo;
      uint32 bits = READ_BITS(ta, tile & 0x3ff);
      if (tile & 0x4000) bits >>= x, GET_PIXEL(); else bits <<= x, GET_PIXEL_HFLIP();
      if (pixel) {
        pixel += (tile & 0x1c00) >> kPaletteShift;
        uint i = 0;
        do {
          if (z > dstz[i])
            dstz[i] = pixel + z;
        } while (++i != w);
      }
      dstz += w, x += w;
      for (; x >= 8; x -= 8)
        tp = (tp != tp_last) ? tp + 1 : tp_next;
      w = ppu->mosaicSize;
    } while (dstz_end - dstz != 0);
  }
#undef READ_BITS
#undef GET_PIXEL
#undef GET_PIXEL_HFLIP
}


// level6 should be set if it's from palette 0xc0 which means color math is not applied
#define SPRITE_PRIO_TO_PRIO(prio, level6) (((prio) * 4 + 2) * 16 + 4 + (level6 ? 2 : 0))
#define SPRITE_PRIO_TO_PRIO_HI(prio) ((prio) * 4 + 2)

static void PpuDrawSprites(Ppu *ppu, uint y, uint sub, bool clear_backdrop) {
  int layer = 4;
  if (!IS_SCREEN_ENABLED(ppu, sub, layer))
    return;  // layer is completely hidden
  PpuWindows win;
  IS_SCREEN_WINDOWED(ppu, sub, layer) ? PpuWindows_Calc(&win, ppu, layer) : PpuWindows_Clear(&win, ppu, layer);
  for (size_t windex = 0; windex < win.nr; windex++) {
    if (win.bits & (1 << windex))
      continue;  // layer is disabled for this window part
    int left = win.edges[windex];
    int width = win.edges[windex + 1] - left;
    PpuZbufType *src = ppu->objBuffer.data + left + kPpuExtraLeftRight;
    PpuZbufType *dst = ppu->bgBuffers[sub].data + left + kPpuExtraLeftRight;
    if (clear_backdrop) {
      memcpy(dst, src, width * sizeof(uint16));
    } else {
      do {
        if (src[0] > dst[0])
          dst[0] = src[0];
      } while (src++, dst++, --width);
    }
  }
}

// Assumes it's drawn on an empty backdrop
static void PpuDrawBackground_mode7(Ppu *ppu, uint y, bool sub, PpuZbufType z) {
  int layer = 0;
  if (!IS_SCREEN_ENABLED(ppu, sub, layer))
    return;  // layer is completely hidden
  PpuWindows win;
  IS_SCREEN_WINDOWED(ppu, sub, layer) ? PpuWindows_Calc(&win, ppu, layer) : PpuWindows_Clear(&win, ppu, layer);

  // expand 13-bit values to signed values
  int hScroll = ((int16_t)(ppu->m7matrix[6] << 3)) >> 3;
  int vScroll = ((int16_t)(ppu->m7matrix[7] << 3)) >> 3;
  int xCenter = ((int16_t)(ppu->m7matrix[4] << 3)) >> 3;
  int yCenter = ((int16_t)(ppu->m7matrix[5] << 3)) >> 3;
  int clippedH = hScroll - xCenter;
  int clippedV = vScroll - yCenter;
  clippedH = (clippedH & 0x2000) ? (clippedH | ~1023) : (clippedH & 1023);
  clippedV = (clippedV & 0x2000) ? (clippedV | ~1023) : (clippedV & 1023);
  bool mosaic_enabled = IS_MOSAIC_ENABLED(ppu, 0);
  if (mosaic_enabled)
    y = ppu->mosaicModulo[y];
  uint32 ry = ppu->m7yFlip ? 255 - y : y;
  uint32 m7startX = (ppu->m7matrix[0] * clippedH & ~63) + (ppu->m7matrix[1] * ry & ~63) +
    (ppu->m7matrix[1] * clippedV & ~63) + (xCenter << 8);
  uint32 m7startY = (ppu->m7matrix[2] * clippedH & ~63) + (ppu->m7matrix[3] * ry & ~63) +
    (ppu->m7matrix[3] * clippedV & ~63) + (yCenter << 8);
  for (size_t windex = 0; windex < win.nr; windex++) {
    if (win.bits & (1 << windex))
      continue;  // layer is disabled for this window part
    int x = win.edges[windex], x2 = win.edges[windex + 1], tile;
    PpuZbufType *dstz = ppu->bgBuffers[sub].data + x + kPpuExtraLeftRight;
    PpuZbufType *dstz_end = ppu->bgBuffers[sub].data + x2 + kPpuExtraLeftRight;
    uint32 rx = ppu->m7xFlip ? 255 - x : x;
    uint32 xpos = m7startX + ppu->m7matrix[0] * rx;
    uint32 ypos = m7startY + ppu->m7matrix[2] * rx;
    uint32 dx = ppu->m7xFlip ? -ppu->m7matrix[0] : ppu->m7matrix[0];
    uint32 dy = ppu->m7xFlip ? -ppu->m7matrix[2] : ppu->m7matrix[2];
    uint32 outside_value = ppu->m7largeField ? 0x3ffff : 0xffffffff;
    bool char_fill = ppu->m7charFill;
    if (mosaic_enabled) {
      int w = ppu->mosaicSize - (x - ppu->mosaicModulo[x]);
      do {
        w = IntMin(w, dstz_end - dstz);
        if ((uint32)(xpos | ypos) > outside_value) {
          if (!char_fill)
            continue;
          tile = 0;
        } else {
          tile = ppu->vram[(ypos >> 11 & 0x7f) * 128 + (xpos >> 11 & 0x7f)] & 0xff;
        }
        uint8 pixel = ppu->vram[tile * 64 + (ypos >> 8 & 7) * 8 + (xpos >> 8 & 7)] >> 8;
        if (pixel) {
          int i = 0;
          do dstz[i] = pixel + z; while (++i != w);
        }
      } while (xpos += dx * w, ypos += dy * w, dstz += w, w = ppu->mosaicSize, dstz_end - dstz != 0);
    } else {
      do {
        if ((uint32)(xpos | ypos) > outside_value) {
          if (!char_fill)
            continue;
          tile = 0;
        } else {
          tile = ppu->vram[(ypos >> 11 & 0x7f) * 128 + (xpos >> 11 & 0x7f)] & 0xff;
        }
        uint8 pixel = ppu->vram[tile * 64 + (ypos >> 8 & 7) * 8 + (xpos >> 8 & 7)] >> 8;
        if (pixel)
          dstz[0] = pixel + z;
      } while (xpos += dx, ypos += dy, ++dstz != dstz_end);
    }
  }
}

void PpuSetMode7PerspectiveCorrection(Ppu *ppu, int low, int high) {
  ppu->mode7PerspectiveLow = low ? 1.0f / low : 0.0f;
  ppu->mode7PerspectiveHigh = 1.0f / high;
}

void PpuSetExtraSideSpace(Ppu *ppu, int left, int right, int bottom) {
  ppu->extraLeftCur = UintMin(left, ppu->extraLeftRight);
  ppu->extraRightCur = UintMin(right, ppu->extraLeftRight);
  ppu->extraBottomCur = UintMin(bottom, 16);
}

void PpuSetWindow1Ext(Ppu *ppu, const int16 *left, const int16 *right) {
  ppu->windowExtLeft = left;
  ppu->windowExtRight = right;
}

static FORCEINLINE float FloatInterpolate(float x, float xmin, float xmax, float ymin, float ymax) {
  return ymin + (ymax - ymin) * (x - xmin) * (1.0f / (xmax - xmin));
}

// Upsampled version of mode7 rendering. Draws everything in 4x the normal resolution.
// Draws directly to the pixel buffer and bypasses any math, and supports only
// a subset of the normal features (all that zelda needs)
static void PpuDrawMode7Upsampled(Ppu *ppu, uint y) {
  // expand 13-bit values to signed values
  uint32 xCenter = ((int16_t)(ppu->m7matrix[4] << 3)) >> 3, yCenter = ((int16_t)(ppu->m7matrix[5] << 3)) >> 3;
  uint32 clippedH = (((int16_t)(ppu->m7matrix[6] << 3)) >> 3) - xCenter;
  uint32 clippedV = (((int16_t)(ppu->m7matrix[7] << 3)) >> 3) - yCenter;
  int32 m0v[4];
  if (*(uint32*)&ppu->mode7PerspectiveLow == 0) {
    m0v[0] = m0v[1] = m0v[2] = m0v[3] = ppu->m7matrix[0] << 12;
  } else {
    static const float kInterpolateOffsets[4] = { -1, -1 + 0.25f, -1 + 0.5f, -1 + 0.75f };
    for (int i = 0; i < 4; i++)
      m0v[i] = 4096.0f / FloatInterpolate((int)y + kInterpolateOffsets[i], 0, 223, ppu->mode7PerspectiveLow, ppu->mode7PerspectiveHigh);
  }
  size_t pitch = ppu->renderPitch;
  uint8 *render_buffer_ptr = &ppu->renderBuffer[(y - 1) * 4 * pitch];
  uint8 *dst_start = render_buffer_ptr + (ppu->extraLeftRight - ppu->extraLeftCur) * 16;
  size_t draw_width = 256 + ppu->extraLeftCur + ppu->extraRightCur;
  uint8 *dst_curline = dst_start;
  uint32 m1 = ppu->m7matrix[1] << 12;  // xpos increment per vert movement
  uint32 m2 = ppu->m7matrix[2] << 12;  // ypos increment per horiz movement
  for (int j = 0; j < 4; j++) {
    uint32 m0 = m0v[j], m3 = m0;
    uint32 xpos = m0 * clippedH + m1 * (clippedV + y) + (xCenter << 20), xcur;
    uint32 ypos = m2 * clippedH + m3 * (clippedV + y) + (yCenter << 20), ycur;

    uint32 tile, pixel;
    xpos -= (m0 + m1) >> 1;
    ypos -= (m2 + m3) >> 1;
    xcur = (xpos << 2) + j * m1;
    ycur = (ypos << 2) + j * m3;

    xcur -= ppu->extraLeftCur * 4 * m0;
    ycur -= ppu->extraLeftCur * 4 * m2;

    uint8 *dst = dst_curline;
    uint8 *dst_end = dst_curline + draw_width * 16;

#define DRAW_PIXEL(mode) \
    tile = ppu->vram[(ycur >> 25 & 0x7f) * 128 + (xcur >> 25 & 0x7f)] & 0xff;  \
    pixel = ppu->vram[tile * 64 + (ycur >> 22 & 7) * 8 + (xcur >> 22 & 7)] >> 8; \
    pixel = (xcur & 0x80000000) ? 0 : pixel; \
    *(uint32*)dst = (mode ? (ppu->colorMapRgb[pixel] & 0xfefefe) >> 1 : ppu->colorMapRgb[pixel]); \
    xcur += m0, ycur += m2, dst += 4;

    if (!ppu->halfColor) {
      do {
        DRAW_PIXEL(0);
        DRAW_PIXEL(0);
        DRAW_PIXEL(0);
        DRAW_PIXEL(0);
      } while (dst != dst_end);
    } else {
      do {
        DRAW_PIXEL(1);
        DRAW_PIXEL(1);
        DRAW_PIXEL(1);
        DRAW_PIXEL(1);
      } while (dst != dst_end);
    }
#undef DRAW_PIXEL

    dst_curline += pitch;
  }

  if (ppu->lineHasSprites) {
    uint8 *dst = dst_start;
    PpuZbufType *pixels = ppu->objBuffer.data + (kPpuExtraLeftRight - ppu->extraLeftCur);
    for (size_t i = 0; i < draw_width; i++, dst += 16) {
      uint32 pixel = pixels[i] & 0xff;
      if (pixel) {
        uint32 color = ppu->colorMapRgb[pixel];
        ((uint32 *)dst)[3] = ((uint32 *)dst)[2] = ((uint32 *)dst)[1] = ((uint32 *)dst)[0] = color;
        ((uint32 *)(dst + pitch * 1))[3] = ((uint32 *)(dst + pitch * 1))[2] = ((uint32 *)(dst + pitch * 1))[1] = ((uint32 *)(dst + pitch * 1))[0] = color;
        ((uint32 *)(dst + pitch * 2))[3] = ((uint32 *)(dst + pitch * 2))[2] = ((uint32 *)(dst + pitch * 2))[1] = ((uint32 *)(dst + pitch * 2))[0] = color;
        ((uint32 *)(dst + pitch * 3))[3] = ((uint32 *)(dst + pitch * 3))[2] = ((uint32 *)(dst + pitch * 3))[1] = ((uint32 *)(dst + pitch * 3))[0] = color;
      }
    }
  }

  if (ppu->extraLeftRight - ppu->extraLeftCur != 0) {
    size_t n = 4 * sizeof(uint32) * (ppu->extraLeftRight - ppu->extraLeftCur);
    for(int i = 0; i < 4; i++)
      memset(render_buffer_ptr + pitch * i, 0, n);
  }
  if (ppu->extraLeftRight - ppu->extraRightCur != 0) {
    size_t n = 4 * sizeof(uint32) * (ppu->extraLeftRight - ppu->extraRightCur);
    for (int i = 0; i < 4; i++)
      memset(render_buffer_ptr + pitch * i + (256 + ppu->extraLeftRight * 2 - (ppu->extraLeftRight - ppu->extraRightCur)) * 4 * sizeof(uint32), 0, n);
  }
#undef DRAW_PIXEL
}

static void PpuDrawBackgrounds(Ppu *ppu, int y, bool sub) {
// Top 4 bits contain the prio level, and bottom 4 bits the layer type.
// SPRITE_PRIO_TO_PRIO can be used to convert from obj prio to this prio.
//  15: BG3 tiles with priority 1 if bit 3 of $2105 is set
//  14: Sprites with priority 3 (4 * sprite_prio + 2)
//  12: BG1 tiles with priority 1
//  11: BG2 tiles with priority 1
//  10: Sprites with priority 2 (4 * sprite_prio + 2)
//  8: BG1 tiles with priority 0
//  7: BG2 tiles with priority 0
//  6: Sprites with priority 1 (4 * sprite_prio + 2)
//  3: BG3 tiles with priority 1 if bit 3 of $2105 is clear
//  2: Sprites with priority 0 (4 * sprite_prio + 2)
//  1: BG3 tiles with priority 0
//  0: backdrop

  if (ppu->mode == 1) {
    if (ppu->lineHasSprites)
      PpuDrawSprites(ppu, y, sub, true);

    if (IS_MOSAIC_ENABLED(ppu, 0))
      PpuDrawBackground_4bpp_mosaic(ppu, y, sub, 0, 0xc000, 0x8000);
    else
      PpuDrawBackground_4bpp(ppu, y, sub, 0, 0xc000, 0x8000);

    if (IS_MOSAIC_ENABLED(ppu, 1))
      PpuDrawBackground_4bpp_mosaic(ppu, y, sub, 1, 0xb100, 0x7100);
    else
      PpuDrawBackground_4bpp(ppu, y, sub, 1, 0xb100, 0x7100);

    if (IS_MOSAIC_ENABLED(ppu, 2))
      PpuDrawBackground_2bpp_mosaic(ppu, y, sub, 2, 0xf200, 0x1200);
    else
      PpuDrawBackground_2bpp(ppu, y, sub, 2, 0xf200, 0x1200);
  } else {
    // mode 7
    PpuDrawBackground_mode7(ppu, y, sub, 0xc000);
    if (ppu->lineHasSprites)
      PpuDrawSprites(ppu, y, sub, false);
  }
}

static inline void PpuWriteMappedSpan(
    uint32 *__restrict dst, const PpuZbufType *__restrict src,
    uint32 count, const uint32 *__restrict color_map) {
  while (count >= 8) {
    dst[0] = color_map[src[0] & 0xff];
    dst[1] = color_map[src[1] & 0xff];
    dst[2] = color_map[src[2] & 0xff];
    dst[3] = color_map[src[3] & 0xff];
    dst[4] = color_map[src[4] & 0xff];
    dst[5] = color_map[src[5] & 0xff];
    dst[6] = color_map[src[6] & 0xff];
    dst[7] = color_map[src[7] & 0xff];
    dst += 8;
    src += 8;
    count -= 8;
  }
  while (count != 0) {
    *dst++ = color_map[*src++ & 0xff];
    count--;
  }
}

// Fixed color math is a function of CGRAM and a handful of registers, not
// screen position. Keep it out of the pixel loop (especially dark interiors).
static uint32 PpuFixedMathColor(Ppu *ppu, uint32 color, bool halve) {
  int r = color & 31, g = (color >> 5) & 31, b = (color >> 10) & 31;
  if (ppu->subtractColor) {
    r = IntMax(r - ppu->fixedColorR, 0);
    g = IntMax(g - ppu->fixedColorG, 0);
    b = IntMax(b - ppu->fixedColorB, 0);
  } else {
    r += ppu->fixedColorR;
    g += ppu->fixedColorG;
    b += ppu->fixedColorB;
  }
  const uint8 *map = halve ? ppu->brightnessMultHalf : ppu->brightnessMult;
  return map[b] | map[g] << 8 | map[r] << 16;
}

static void PpuPrepareFixedMath(Ppu *ppu) {
  // A subscreen backdrop uses fixed color WITHOUT halving, even when the
  // half flag is set. Do not conflate it with a real subscreen color.
  bool halve = ppu->halfColor && !ppu->addSubscreen;
  uint32 key = ppu->fixedColorR | ppu->fixedColorG << 5 |
    ppu->fixedColorB << 10 | halve << 15 | ppu->subtractColor << 16 |
    ppu->lastBrightnessMult << 17;
  if (ppu->fixedMathValid && ppu->fixedMathKey == key)
    return;
  for (uint i = 0; i < 256; i++)
    ppu->fixedMathRgb[i] = PpuFixedMathColor(ppu, ppu->cgram[i], halve);
  ppu->fixedMathBlack = PpuFixedMathColor(ppu, 0, halve);
  ppu->fixedMathKey = key;
  ppu->fixedMathValid = true;
}

// A screen may be enabled in TS yet contribute to no visible main pixel.
// The graveyard capture, for example, enables math only for backdrop.
static bool PpuLineUsesMath(Ppu *ppu, uint32 mask) {
  const PpuZbufType *p = ppu->bgBuffers[0].data +
    kPpuExtraLeftRight - ppu->extraLeftCur;
  const PpuZbufType *end = p + 256 + ppu->extraLeftCur + ppu->extraRightCur;
  while (p != end) {
    if (mask & (1u << ((*p++ >> 8) & 15)))
      return true;
  }
  return false;
}

static void PpuWriteFixedMathSpan(Ppu *ppu, uint32 *dst,
                                 const PpuZbufType *src, uint32 count,
                                 uint32 mask, bool unclipped) {
  if (unclipped) {
    while (count--) {
      uint32 pixel = *src++;
      *dst++ = (mask & (1u << ((pixel >> 8) & 15))) ?
        ppu->fixedMathRgb[pixel & 255] : ppu->colorMapRgb[pixel & 255];
    }
  } else {
    uint32 black = ppu->fixedMathBlack;
    while (count--) {
      uint32 pixel = *src++;
      *dst++ = (mask & (1u << ((pixel >> 8) & 15))) ? black : 0;
    }
  }
}

// One 1 KiB table evaluates a pair of 5-bit components, including clamp,
// half-color rounding and brightness. Rebuilt only on math/brightness changes.
static void PpuPrepareSubscreenMath(Ppu *ppu) {
  uint8 key = ppu->lastBrightnessMult | ppu->halfColor << 4 |
    ppu->subtractColor << 5;
  if (ppu->subscreenMathKey == key)
    return;
  const uint8 *map = ppu->halfColor ? ppu->brightnessMultHalf : ppu->brightnessMult;
  for (uint b = 0; b < 32; b++) {
    for (uint a = 0; a < 32; a++) {
      uint component = ppu->subtractColor ? (a >= b ? a - b : 0) : a + b;
      ppu->subscreenMath[a | b << 5] = map[component];
    }
  }
  ppu->subscreenMathKey = key;
}

// In Zelda's storm the main backdrop is blended with the terrain subscreen.
// Its main color is always CGRAM[0], so cache the 256 possible results once
// instead of doing three component operations for every background pixel.
static void PpuWriteBackdropSubMath(Ppu *ppu, uint32 *dst, uint32 left,
                                    uint32 right, bool unclipped) {
  uint32 key = ppu->fixedMathKey | (uint32)ppu->halfColor << 22 |
    (uint32)unclipped << 23;
  if (!ppu->backdropMathValid || ppu->backdropMathKey != key) {
    PpuPrepareSubscreenMath(ppu);
    const uint8 *map = ppu->subscreenMath;
    uint32 a = unclipped ? ppu->cgram[0] : 0;
    ppu->backdropMathRgb[0] = unclipped ? ppu->fixedMathRgb[0] : ppu->fixedMathBlack;
    for (unsigned i = 1; i < 256; i++) {
      uint32 b = ppu->cgram[i];
      uint32 r = map[(a & 31) | ((b & 31) << 5)];
      uint32 g = map[((a >> 5) & 31) | (b & 0x3e0)];
      uint32 blue = map[((a >> 10) & 31) | ((b >> 5) & 0x3e0)];
      ppu->backdropMathRgb[i] = blue | g << 8 | r << 16;
    }
    ppu->backdropMathKey = key;
    ppu->backdropMathValid = true;
  }
  for (unsigned i = left; i < right; i++) {
    uint32 pixel = ppu->bgBuffers[0].data[i];
    *dst++ = (pixel & 0xf00) == 0x500 ?
      ppu->backdropMathRgb[ppu->bgBuffers[1].data[i] & 255] :
      (unclipped ? ppu->colorMapRgb[pixel & 255] : 0);
  }
}

// Rain uses full-brightness half-add. Store RGB5 in separate bytes, so a
// single addition handles all three channels without cross-channel carries.
// Shift/mask performs SNES floor((a+b)/2), then expand 5 bits to 8 exactly.
static void PpuWriteFullBrightnessHalfAdd(Ppu *ppu, uint32 *dst, uint32 left,
                                         uint32 right, uint32 mask, bool unclipped) {
  const uint32 *spaced = ppu->colorMapRgb5Spaced;
  for (uint i = left; i < right; i++) {
    uint32 pixel = ppu->bgBuffers[0].data[i];
    uint32 index = pixel & 255;
    if (!(mask & (1u << ((pixel >> 8) & 15)))) {
      *dst++ = unclipped ? ppu->colorMapRgb[index] : 0;
      continue;
    }
    uint32 sub_index = ppu->bgBuffers[1].data[i] & 255;
    if (sub_index == 0) {
      *dst++ = unclipped ? ppu->fixedMathRgb[index] : ppu->fixedMathBlack;
      continue;
    }
    uint32 sum = (unclipped ? spaced[index] : 0) + spaced[sub_index];
    uint32 rgb5 = (sum >> 1) & 0x1f1f1f;
    *dst++ = (rgb5 << 3) | ((rgb5 >> 2) & 0x070707);
  }
}

static void PpuWriteSubscreenMathSpan(Ppu *ppu, uint32 *dst, uint32 left,
                                     uint32 right, uint32 mask, bool unclipped) {
  PpuPrepareFixedMath(ppu);
  if (mask == (1u << 5)) {
    PpuWriteBackdropSubMath(ppu, dst, left, right, unclipped);
    return;
  }
  if (ppu->lastBrightnessMult == 15 && ppu->halfColor && !ppu->subtractColor) {
    PpuWriteFullBrightnessHalfAdd(ppu, dst, left, right, mask, unclipped);
    return;
  }
  PpuPrepareSubscreenMath(ppu);
  const uint8 *map = ppu->subscreenMath;
  for (uint i = left; i < right; i++) {
    uint32 pixel = ppu->bgBuffers[0].data[i];
    uint32 index = pixel & 255;
    if (!(mask & (1u << ((pixel >> 8) & 15)))) {
      *dst++ = unclipped ? ppu->colorMapRgb[index] : 0;
      continue;
    }
    uint32 sub_index = ppu->bgBuffers[1].data[i] & 255;
    if (sub_index == 0) {
      *dst++ = unclipped ? ppu->fixedMathRgb[index] : ppu->fixedMathBlack;
      continue;
    }
    uint32 color = unclipped ? ppu->cgram[index] : 0;
    uint32 color2 = ppu->cgram[sub_index];
    uint32 r = map[(color & 31) | ((color2 & 31) << 5)];
    uint32 g = map[((color >> 5) & 31) | (color2 & 0x3e0)];
    uint32 b = map[((color >> 10) & 31) | ((color2 >> 5) & 0x3e0)];
    *dst++ = b | g << 8 | r << 16;
  }
}

static NOINLINE void PpuDrawWholeLine(Ppu *ppu, uint y) {
  bool profile = PpuProfileActive(ppu);
  uint64_t phase_start = profile ? PpuProfileTick() : 0;
  if (ppu->forcedBlank) {
    uint8 *dst = &ppu->renderBuffer[(y - 1) * ppu->renderPitch];
    size_t n = sizeof(uint32) * (256 + ppu->extraLeftRight * 2);
    memset(dst, 0, n);
    return;
  }

  if (ppu->windowExtLeft) {
    int wy = y - 1 - ppu->renderObjYOffset;
    ppu->windowExtLeftCur = wy >= 0 ? ppu->windowExtLeft[wy] : 0;
    ppu->windowExtRightCur = wy >= 0 ? ppu->windowExtRight[wy] : -1;
  }

  if (ppu->mode == 7 && (ppu->renderFlags & kPpuRenderFlags_4x4Mode7)) {
    PpuDrawMode7Upsampled(ppu, y);
    if (profile) ppu->phase.mode7 += PpuProfileTick() - phase_start;
    return;
  }

  // Default background is backdrop
  ClearBackdrop(ppu, &ppu->bgBuffers[0]);

  // Render main screen
  PpuDrawBackgrounds(ppu, y, false);
  if (profile) {
    uint64_t now = PpuProfileTick();
    ppu->phase.main += now - phase_start; phase_start = now;
  }

  // The 6:th bit is automatically zero, math is never applied to the first half of the sprites.
  uint32 math_enabled = ppu->mathEnabled;
  // E6 refreshes its normal palette at BeginDrawing. Mid-frame CGRAM writes
  // therefore require the original path until the next palette refresh.
  bool old3ds = (ppu->renderFlags & kPpuRenderFlags_Old3DS) != 0 &&
    !ppu->colorMapDirty;
  if (old3ds && math_enabled && ppu->preventMathMode != 3 &&
      !PpuLineUsesMath(ppu, math_enabled))
    math_enabled = 0;

  // Render also the subscreen?
  bool rendered_subscreen = false;
  if (ppu->preventMathMode != 3 && ppu->addSubscreen && math_enabled &&
      ppu->screenEnabled[1] != 0) {
    ClearBackdrop(ppu, &ppu->bgBuffers[1]);
    PpuDrawBackgrounds(ppu, y, true);
    rendered_subscreen = true;
  }

  if (profile) {
    uint64_t now = PpuProfileTick();
    ppu->phase.sub += now - phase_start; phase_start = now;
  }
  // Color window affects the drawing mode in each region
  PpuWindows cwin;
  uint32 color_window_flags = GET_WINDOW_FLAGS(ppu, 5);
  if (color_window_flags & (kWindow1Enabled | kWindow2Enabled))
    PpuWindows_Calc(&cwin, ppu, 5);
  else
    PpuWindows_Clear(&cwin, ppu, 5);
  static const uint8 kCwBitsMod[8] = {
    0x00, 0xff, 0xff, 0x00,
    0xff, 0x00, 0xff, 0x00,
  };
  uint32 cw_clip_math = ((cwin.bits & kCwBitsMod[ppu->clipMode]) ^ kCwBitsMod[ppu->clipMode + 4]) |
                        ((cwin.bits & kCwBitsMod[ppu->preventMathMode]) ^ kCwBitsMod[ppu->preventMathMode + 4]) << 8;

  uint32 *dst =
    (uint32 *)&ppu->renderBuffer[(y - 1) * ppu->renderPitch];
  uint32 *dst_org = dst;
  dst += ppu->extraLeftRight - ppu->extraLeftCur;

  uint32 windex = 0;
  do {
    uint32 left = cwin.edges[windex] + kPpuExtraLeftRight, right = cwin.edges[windex + 1] + kPpuExtraLeftRight;
    // If clip is set, then zero out the rgb values from the main screen.
    uint32 clip_color_mask = (cw_clip_math & 1) ? 0x1f : 0;
    uint32 math_enabled_cur = (cw_clip_math & 0x100) ? math_enabled : 0;
    uint32 fixed_color = ppu->fixedColorR | ppu->fixedColorG << 5 | ppu->fixedColorB << 10;
    if (math_enabled_cur == 0 || fixed_color == 0 && !ppu->halfColor && !rendered_subscreen) {
      // Math is disabled (or has no effect), so can avoid the per-pixel maths check
      uint32 i = left;
      if (clip_color_mask == 0) {
        memset(dst, 0, (right - left) * sizeof(*dst));
        dst += right - left;
      } else {
        uint32 count = right - left;
        PpuWriteMappedSpan(dst, &ppu->bgBuffers[0].data[i], count,
                           ppu->colorMapRgb);
        dst += count;
      }
    } else if (old3ds && !rendered_subscreen) {
      PpuPrepareFixedMath(ppu);
      PpuWriteFixedMathSpan(ppu, dst, &ppu->bgBuffers[0].data[left],
                            right - left, math_enabled_cur, clip_color_mask != 0);
      dst += right - left;
    } else if (old3ds) {
      PpuWriteSubscreenMathSpan(ppu, dst, left, right, math_enabled_cur,
                                clip_color_mask != 0);
      dst += right - left;
    } else {
      uint8 *half_color_map = ppu->halfColor ? ppu->brightnessMultHalf : ppu->brightnessMult;
      // Store this in locals
      math_enabled_cur |= ppu->addSubscreen << 8 | ppu->subtractColor << 9;
      // Need to check for each pixel whether to use math or not based on the main screen layer.
      uint32 i = left;
      do {
        PpuZbufType main_pixel = ppu->bgBuffers[0].data[i];
        uint32 color = ppu->cgram[main_pixel & 0xff], color2;
        uint8 main_layer = (main_pixel >> 8) & 0xf;
        uint32 r = color & clip_color_mask;
        uint32 g = (color >> 5) & clip_color_mask;
        uint32 b = (color >> 10) & clip_color_mask;
        uint8 *color_map = ppu->brightnessMult;
        if (math_enabled_cur & (1 << main_layer)) {
          if (math_enabled_cur & 0x100) {  // addSubscreen ?
            // When TS has no enabled layers, the subscreen is the backdrop.
            // Do not inspect the uncleared buffer left by an earlier line.
            if (rendered_subscreen &&
                (ppu->bgBuffers[1].data[i] & 0xff) != 0)
              color2 = ppu->cgram[ppu->bgBuffers[1].data[i] & 0xff], color_map = half_color_map;
            else  // Don't halve if ppu->addSubscreen && backdrop
              color2 = fixed_color;
          } else {
            color2 = fixed_color, color_map = half_color_map;
          }
          uint32 r2 = (color2 & 0x1f), g2 = ((color2 >> 5) & 0x1f), b2 = ((color2 >> 10) & 0x1f);
          if (math_enabled_cur & 0x200) {  // subtractColor?
            r = (r >= r2) ? r - r2 : 0;
            g = (g >= g2) ? g - g2 : 0;
            b = (b >= b2) ? b - b2 : 0;
          } else {
            r += r2;
            g += g2;
            b += b2;
          }
        }
        *dst++ = color_map[b] | color_map[g] << 8 | color_map[r] << 16;
      } while (++i < right);
    }
  } while (cw_clip_math >>= 1, ++windex < cwin.nr);

  // Clear out stuff on the sides.
  if (ppu->extraLeftRight - ppu->extraLeftCur != 0)
    memset(dst_org, 0, sizeof(*dst_org) *
           (ppu->extraLeftRight - ppu->extraLeftCur));
  if (ppu->extraLeftRight - ppu->extraRightCur != 0) {
    size_t first = 256 + ppu->extraLeftRight * 2 -
                   (ppu->extraLeftRight - ppu->extraRightCur);
    memset(dst_org + first, 0, sizeof(*dst_org) *
           (ppu->extraLeftRight - ppu->extraRightCur));
  }
  if (profile) ppu->phase.compose += PpuProfileTick() - phase_start;
}

static void ppu_handlePixel(Ppu* ppu, int x, int y) {
  int r = 0, r2 = 0;
  int g = 0, g2 = 0;
  int b = 0, b2 = 0;
  if (!ppu->forcedBlank) {
    int mainLayer = ppu_getPixel(ppu, x, y, false, &r, &g, &b);

    bool colorWindowState = ppu_getWindowState(ppu, 5, x);
    if (
      ppu->clipMode == 3 ||
      (ppu->clipMode == 2 && colorWindowState) ||
      (ppu->clipMode == 1 && !colorWindowState)
      ) {
      r = g = b = 0;
    }
    int secondLayer = 5; // backdrop
    bool mathEnabled = mainLayer < 6 && (ppu->mathEnabled & (1 << mainLayer)) && !(
      ppu->preventMathMode == 3 ||
      (ppu->preventMathMode == 2 && colorWindowState) ||
      (ppu->preventMathMode == 1 && !colorWindowState)
      );
    if ((mathEnabled && ppu->addSubscreen) || ppu->mode == 5 || ppu->mode == 6) {
      secondLayer = ppu_getPixel(ppu, x, y, true, &r2, &g2, &b2);
    }
    // TODO: subscreen pixels can be clipped to black as well
    // TODO: math for subscreen pixels (add/sub sub to main)
    if (mathEnabled) {
      if (ppu->subtractColor) {
        r -= (ppu->addSubscreen && secondLayer != 5) ? r2 : ppu->fixedColorR;
        g -= (ppu->addSubscreen && secondLayer != 5) ? g2 : ppu->fixedColorG;
        b -= (ppu->addSubscreen && secondLayer != 5) ? b2 : ppu->fixedColorB;
      } else {
        r += (ppu->addSubscreen && secondLayer != 5) ? r2 : ppu->fixedColorR;
        g += (ppu->addSubscreen && secondLayer != 5) ? g2 : ppu->fixedColorG;
        b += (ppu->addSubscreen && secondLayer != 5) ? b2 : ppu->fixedColorB;
      }
      if (ppu->halfColor && (secondLayer != 5 || !ppu->addSubscreen)) {
        r >>= 1;
        g >>= 1;
        b >>= 1;
      }
      if (r > 31) r = 31;
      if (g > 31) g = 31;
      if (b > 31) b = 31;
      if (r < 0) r = 0;
      if (g < 0) g = 0;
      if (b < 0) b = 0;
    }
    if (!(ppu->mode == 5 || ppu->mode == 6)) {
      r2 = r; g2 = g; b2 = b;
    }
  }
  int row = y - 1;
  uint8 *pixel = &ppu->renderBuffer[
    row * ppu->renderPitch + (x + ppu->extraLeftRight) * 4];
  pixel[0] = ((b << 3) | (b >> 2)) * ppu->brightness / 15;
  pixel[1] = ((g << 3) | (g >> 2)) * ppu->brightness / 15;
  pixel[2] = ((r << 3) | (r >> 2)) * ppu->brightness / 15;
  pixel[3] = 0;
}

static const int bitDepthsPerMode[10][4] = {
  {2, 2, 2, 2},
  {4, 4, 2, 5},
  {4, 4, 5, 5},
  {8, 4, 5, 5},
  {8, 2, 5, 5},
  {4, 2, 5, 5},
  {4, 5, 5, 5},
  {8, 5, 5, 5},
  {4, 4, 2, 5},
  {8, 7, 5, 5}
};

static int ppu_getPixel(Ppu *ppu, int x, int y, bool sub, int *r, int *g, int *b) {
  // array for layer definitions per mode:
//   0-7: mode 0-7; 8: mode 1 + l3prio; 9: mode 7 + extbg

//   0-3; layers 1-4; 4: sprites; 5: nonexistent
  static const int layersPerMode[10][12] = {
    {4, 0, 1, 4, 0, 1, 4, 2, 3, 4, 2, 3},
    {4, 0, 1, 4, 0, 1, 4, 2, 4, 2, 5, 5},
    {4, 0, 4, 1, 4, 0, 4, 1, 5, 5, 5, 5},
    {4, 0, 4, 1, 4, 0, 4, 1, 5, 5, 5, 5},
    {4, 0, 4, 1, 4, 0, 4, 1, 5, 5, 5, 5},
    {4, 0, 4, 1, 4, 0, 4, 1, 5, 5, 5, 5},
    {4, 0, 4, 4, 0, 4, 5, 5, 5, 5, 5, 5},
    {4, 4, 4, 0, 4, 5, 5, 5, 5, 5, 5, 5},
    {2, 4, 0, 1, 4, 0, 1, 4, 4, 2, 5, 5},
    {4, 4, 1, 4, 0, 4, 1, 5, 5, 5, 5, 5}
  };

  static const int prioritysPerMode[10][12] = {
    {3, 1, 1, 2, 0, 0, 1, 1, 1, 0, 0, 0},
    {3, 1, 1, 2, 0, 0, 1, 1, 0, 0, 5, 5},
    {3, 1, 2, 1, 1, 0, 0, 0, 5, 5, 5, 5},
    {3, 1, 2, 1, 1, 0, 0, 0, 5, 5, 5, 5},
    {3, 1, 2, 1, 1, 0, 0, 0, 5, 5, 5, 5},
    {3, 1, 2, 1, 1, 0, 0, 0, 5, 5, 5, 5},
    {3, 1, 2, 1, 0, 0, 5, 5, 5, 5, 5, 5},
    {3, 2, 1, 0, 0, 5, 5, 5, 5, 5, 5, 5},
    {1, 3, 1, 1, 2, 0, 0, 1, 0, 0, 5, 5},
    {3, 2, 1, 1, 0, 0, 0, 5, 5, 5, 5, 5}
  };

  static const int layerCountPerMode[10] = {
    12, 10, 8, 8, 8, 8, 6, 5, 10, 7
  };

  
  // figure out which color is on this location on main- or subscreen, sets it in r, g, b
  // returns which layer it is: 0-3 for bg layer, 4 or 6 for sprites (depending on palette), 5 for backdrop
  int actMode = ppu->mode == 1 ? 8 : ppu->mode;
  actMode = ppu->mode == 7 && ppu->m7extBg_always_zero ? 9 : actMode;
  int layer = 5;
  int pixel = 0;
  for (int i = 0; i < layerCountPerMode[actMode]; i++) {
    int curLayer = layersPerMode[actMode][i];
    int curPriority = prioritysPerMode[actMode][i];
    bool layerActive = false;
    if (!sub) {
      layerActive = IS_SCREEN_ENABLED(ppu, 0, curLayer) && (
        !IS_SCREEN_WINDOWED(ppu, 0, curLayer) || !ppu_getWindowState(ppu, curLayer, x)
        );
    } else {
      layerActive = IS_SCREEN_ENABLED(ppu, 1, curLayer) && (
        !IS_SCREEN_WINDOWED(ppu, 1, curLayer) || !ppu_getWindowState(ppu, curLayer, x)
        );
    }
    if (layerActive) {
      if (curLayer < 4) {
        // bg layer
        int lx = x;
        int ly = y;
        if (IS_MOSAIC_ENABLED(ppu, curLayer)) {
          lx -= lx % ppu->mosaicSize;
          ly -= (ly - 1) % ppu->mosaicSize;
        }
        if (ppu->mode == 7) {
          pixel = ppu_getPixelForMode7(ppu, lx, curLayer, curPriority);
        } else {
          lx += ppu->bgLayer[curLayer].hScroll;
          ly += ppu->bgLayer[curLayer].vScroll;
          pixel = ppu_getPixelForBgLayer(
            ppu, lx & 0x3ff, ly & 0x3ff,
            curLayer, curPriority
          );
        }
      } else {
        // get a pixel from the sprite buffer
        pixel = 0;
        if ((ppu->objBuffer.data[x + kPpuExtraLeftRight] >> 12) == SPRITE_PRIO_TO_PRIO_HI(curPriority))
          pixel = ppu->objBuffer.data[x + kPpuExtraLeftRight] & 0xff;
      }
    }
    if (pixel > 0) {
      layer = curLayer;
      break;
    }
  }
  uint16_t color = ppu->cgram[pixel & 0xff];
  *r = color & 0x1f;
  *g = (color >> 5) & 0x1f;
  *b = (color >> 10) & 0x1f;
  if (layer == 4 && pixel < 0xc0) layer = 6; // sprites with palette color < 0xc0
  return layer;

}


static int ppu_getPixelForBgLayer(Ppu *ppu, int x, int y, int layer, bool priority) {
  BgLayer *layerp = &ppu->bgLayer[layer];
  // figure out address of tilemap word and read it
  bool wideTiles = ppu->mode == 5 || ppu->mode == 6;
  int tileBitsX = wideTiles ? 4 : 3;
  int tileHighBitX = wideTiles ? 0x200 : 0x100;
  int tileBitsY = 3;
  int tileHighBitY = 0x100;
  uint16_t tilemapAdr = layerp->tilemapAdr + (((y >> tileBitsY) & 0x1f) << 5 | ((x >> tileBitsX) & 0x1f));
  if ((x & tileHighBitX) && layerp->tilemapWider) tilemapAdr += 0x400;
  if ((y & tileHighBitY) && layerp->tilemapHigher) tilemapAdr += layerp->tilemapWider ? 0x800 : 0x400;
  uint16_t tile = ppu->vram[tilemapAdr & 0x7fff];
  // check priority, get palette
  if (((bool)(tile & 0x2000)) != priority) return 0; // wrong priority
  int paletteNum = (tile & 0x1c00) >> 10;
  // figure out position within tile
  int row = (tile & 0x8000) ? 7 - (y & 0x7) : (y & 0x7);
  int col = (tile & 0x4000) ? (x & 0x7) : 7 - (x & 0x7);
  int tileNum = tile & 0x3ff;
  if (wideTiles) {
    // if unflipped right half of tile, or flipped left half of tile
    if (((bool)(x & 8)) ^ ((bool)(tile & 0x4000))) tileNum += 1;
  }
  // read tiledata, ajust palette for mode 0
  int bitDepth = bitDepthsPerMode[ppu->mode][layer];
  if (ppu->mode == 0) paletteNum += 8 * layer;
  // plane 1 (always)
  int paletteSize = 4;
  uint16_t plane1 = ppu->vram[(layerp->tileAdr + ((tileNum & 0x3ff) * 4 * bitDepth) + row) & 0x7fff];
  int pixel = (plane1 >> col) & 1;
  pixel |= ((plane1 >> (8 + col)) & 1) << 1;
  // plane 2 (for 4bpp, 8bpp)
  if (bitDepth > 2) {
    paletteSize = 16;
    uint16_t plane2 = ppu->vram[(layerp->tileAdr + ((tileNum & 0x3ff) * 4 * bitDepth) + 8 + row) & 0x7fff];
    pixel |= ((plane2 >> col) & 1) << 2;
    pixel |= ((plane2 >> (8 + col)) & 1) << 3;
  }
  // plane 3 & 4 (for 8bpp)
  if (bitDepth > 4) {
    paletteSize = 256;
    uint16_t plane3 = ppu->vram[(layerp->tileAdr + ((tileNum & 0x3ff) * 4 * bitDepth) + 16 + row) & 0x7fff];
    pixel |= ((plane3 >> col) & 1) << 4;
    pixel |= ((plane3 >> (8 + col)) & 1) << 5;
    uint16_t plane4 = ppu->vram[(layerp->tileAdr + ((tileNum & 0x3ff) * 4 * bitDepth) + 24 + row) & 0x7fff];
    pixel |= ((plane4 >> col) & 1) << 6;
    pixel |= ((plane4 >> (8 + col)) & 1) << 7;
  }
  // return cgram index, or 0 if transparent, palette number in bits 10-8 for 8-color layers
  return pixel == 0 ? 0 : paletteSize * paletteNum + pixel;
}

static void ppu_calculateMode7Starts(Ppu* ppu, int y) {
  // expand 13-bit values to signed values
  int hScroll = ((int16_t) (ppu->m7matrix[6] << 3)) >> 3;
  int vScroll = ((int16_t) (ppu->m7matrix[7] << 3)) >> 3;
  int xCenter = ((int16_t) (ppu->m7matrix[4] << 3)) >> 3;
  int yCenter = ((int16_t) (ppu->m7matrix[5] << 3)) >> 3;
  // do calculation
  int clippedH = hScroll - xCenter;
  int clippedV = vScroll - yCenter;
  clippedH = (clippedH & 0x2000) ? (clippedH | ~1023) : (clippedH & 1023);
  clippedV = (clippedV & 0x2000) ? (clippedV | ~1023) : (clippedV & 1023);
  if(IS_MOSAIC_ENABLED(ppu, 0)) {
    y -= (y - 1) % ppu->mosaicSize;
  }
  uint8_t ry = ppu->m7yFlip ? 255 - y : y;
  ppu->m7startX = (
    ((ppu->m7matrix[0] * clippedH) & ~63) +
    ((ppu->m7matrix[1] * ry) & ~63) +
    ((ppu->m7matrix[1] * clippedV) & ~63) +
    (xCenter << 8)
  );
  ppu->m7startY = (
    ((ppu->m7matrix[2] * clippedH) & ~63) +
    ((ppu->m7matrix[3] * ry) & ~63) +
    ((ppu->m7matrix[3] * clippedV) & ~63) +
    (yCenter << 8)
  );
}

static int ppu_getPixelForMode7(Ppu* ppu, int x, int layer, bool priority) {
  if (IS_MOSAIC_ENABLED(ppu, layer))
    x -= x % ppu->mosaicSize;
  uint8_t rx = ppu->m7xFlip ? 255 - x : x;
  int xPos = (ppu->m7startX + ppu->m7matrix[0] * rx) >> 8;
  int yPos = (ppu->m7startY + ppu->m7matrix[2] * rx) >> 8;
  bool outsideMap = xPos < 0 || xPos >= 1024 || yPos < 0 || yPos >= 1024;
  xPos &= 0x3ff;
  yPos &= 0x3ff;
  if(!ppu->m7largeField) outsideMap = false;
  uint8_t tile = outsideMap ? 0 : ppu->vram[(yPos >> 3) * 128 + (xPos >> 3)] & 0xff;
  uint8_t pixel = outsideMap && !ppu->m7charFill ? 0 : ppu->vram[tile * 64 + (yPos & 7) * 8 + (xPos & 7)] >> 8;
  if(layer == 1) {
    if(((bool) (pixel & 0x80)) != priority) return 0;
    return pixel & 0x7f;
  }
  return pixel;
}

static bool ppu_getWindowState(Ppu* ppu, int layer, int x) {
  uint32 winflags = GET_WINDOW_FLAGS(ppu, layer);
  if (!(winflags & kWindow1Enabled) && !(winflags & kWindow2Enabled)) {
    return false;
  }
  if ((winflags & kWindow1Enabled) && !(winflags & kWindow2Enabled)) {
    bool test = x >= ppu->window1left && x <= ppu->window1right;
    return (winflags & kWindow1Inversed) ? !test : test;
  }
  if (!(winflags & kWindow1Enabled) && (winflags & kWindow2Enabled)) {
    bool test = x >= ppu->window2left && x <= ppu->window2right;
    return (winflags & kWindow2Inversed) ? !test : test;
  }
  bool test1 = x >= ppu->window1left && x <= ppu->window1right;
  bool test2 = x >= ppu->window2left && x <= ppu->window2right;
  if (winflags & kWindow1Inversed) test1 = !test1;
  if (winflags & kWindow2Inversed) test2 = !test2;
  return test1 || test2;
}

static bool ppu_evaluateSprites(Ppu* ppu, int line) {
  line -= ppu->renderObjYOffset;
  // TODO: iterate over oam normally to determine in-range sprites,
  //   then iterate those in-range sprites in reverse for tile-fetching
  // TODO: rectangular sprites, wierdness with sprites at -256
  int index = 0, index_end = index;
  int spritesLeft = 32 + 1, tilesLeft = 34 + 1;
  uint8 spriteSizes[2] = { kSpriteSizes[ppu->objSize][0], kSpriteSizes[ppu->objSize][1] };
  int extra_left_right = ppu->extraLeftRight;
  if (ppu->renderFlags & kPpuRenderFlags_NoSpriteLimits)
    spritesLeft = tilesLeft = 1024;
  int tilesLeftOrg = tilesLeft;

  do {
    int yy = ppu->oam[index] >> 8;
    if (yy == 0xf0)
      continue;  // this works for zelda because sprites are always 8 or 16.
    // check if the sprite is on this line and get the sprite size
    int row = (line - yy) & 0xff;
    int highOam = ppu->oam[0x100 + (index >> 4)] >> (index & 15);
    int spriteSize = spriteSizes[(highOam >> 1) & 1];
    if (row >= spriteSize)
      continue;
    // in y-range, get the x location, using the high bit as well
    int x = (ppu->oam[index] & 0xff) + (highOam & 1) * 256;
    x -= (x >= 256 + extra_left_right) * 512;
    x += ppu->renderObjXOffset;
    // if in x-range
    if (x <= -(spriteSize + extra_left_right))
      continue;
    // break if we found 32 sprites already
    if (--spritesLeft == 0) {
      break;
    }
    // get some data for the sprite and y-flip row if needed
    int oam1 = ppu->oam[index + 1];
    int objAdr = (oam1 & 0x100) ? ppu->objTileAdr2 : ppu->objTileAdr1;
    if (oam1 & 0x8000)
      row = spriteSize - 1 - row;
    // fetch all tiles in x-range
    int paletteBase = 0x80 + 16 * ((oam1 & 0xe00) >> 9);
    int prio = SPRITE_PRIO_TO_PRIO((oam1 & 0x3000) >> 12, (oam1 & 0x800) == 0);
    PpuZbufType z = paletteBase + (prio << 8);
    
    for (int col = 0; col < spriteSize; col += 8) {
      if (col + x > -8 - extra_left_right && col + x < 256 + extra_left_right) {
        // break if we found 34 8*1 slivers already
        if (--tilesLeft == 0) {
          return true;
        }
        // figure out which tile this uses, looping within 16x16 pages, and get it's data
        int usedCol = oam1 & 0x4000 ? spriteSize - 1 - col : col;
        int usedTile = ((((oam1 & 0xff) >> 4) + (row >> 3)) << 4) | (((oam1 & 0xf) + (usedCol >> 3)) & 0xf);
        uint32 pixels = PpuGetCached4bppRow(
          ppu, objAdr + usedTile * 16 + (row & 0x7));
        // go over each pixel
        int px_left = IntMax(-(col + x + kPpuExtraLeftRight), 0);
        int px_right = IntMin(256 + kPpuExtraLeftRight - (col + x), 8);
        PpuZbufType *dst = ppu->objBuffer.data + col + x + px_left + kPpuExtraLeftRight;
        
        for (int px = px_left; px < px_right; px++, dst++) {
          int shift = oam1 & 0x4000 ? px : 7 - px;
          int pixel = (pixels >> (shift * 4)) & 0xf;
          // draw it in the buffer if there is a pixel here, and the buffer there is still empty
          if (pixel != 0 && (dst[0] & 0xff) == 0)
            dst[0] = z + pixel;
        }
      }
    }
  } while ((index = (index + 2) & 0xff) != index_end);
  return (tilesLeft != tilesLeftOrg);
}

static bool PpuEvaluateVisibleSprites(Ppu *ppu, int line) {
  line -= ppu->renderObjYOffset;
  // Old-only candidate iteration; all fetch/priority rules match the base path.
  int spritesLeft = 32 + 1, tilesLeft = 34 + 1;
  uint8 spriteSizes[2] = { kSpriteSizes[ppu->objSize][0], kSpriteSizes[ppu->objSize][1] };
  int extra_left_right = ppu->extraLeftRight;
  if (ppu->renderFlags & kPpuRenderFlags_NoSpriteLimits)
    spritesLeft = tilesLeft = 1024;
  int tilesLeftOrg = tilesLeft;

  for (unsigned word = 0; word < 4; word++) {
   uint32 remaining = ppu->spriteLines[line & 255][word];
   while (remaining) {
    int index = (word * 32 + __builtin_ctz(remaining)) * 2;
    remaining &= remaining - 1;
    int yy = ppu->oam[index] >> 8;
    // check if the sprite is on this line and get the sprite size
    int row = (line - yy) & 0xff;
    int highOam = ppu->oam[0x100 + (index >> 4)] >> (index & 15);
    int spriteSize = spriteSizes[(highOam >> 1) & 1];
    // in y-range, get the x location, using the high bit as well
    int x = (ppu->oam[index] & 0xff) + (highOam & 1) * 256;
    x -= (x >= 256 + extra_left_right) * 512;
    x += ppu->renderObjXOffset;
    // if in x-range
    if (x <= -(spriteSize + extra_left_right))
      continue;
    // break if we found 32 sprites already
    if (--spritesLeft == 0) {
      return tilesLeft != tilesLeftOrg;
    }
    // get some data for the sprite and y-flip row if needed
    int oam1 = ppu->oam[index + 1];
    int objAdr = (oam1 & 0x100) ? ppu->objTileAdr2 : ppu->objTileAdr1;
    if (oam1 & 0x8000)
      row = spriteSize - 1 - row;
    // fetch all tiles in x-range
    int paletteBase = 0x80 + 16 * ((oam1 & 0xe00) >> 9);
    int prio = SPRITE_PRIO_TO_PRIO((oam1 & 0x3000) >> 12, (oam1 & 0x800) == 0);
    PpuZbufType z = paletteBase + (prio << 8);

    for (int col = 0; col < spriteSize; col += 8) {
      if (col + x > -8 - extra_left_right && col + x < 256 + extra_left_right) {
        // break if we found 34 8*1 slivers already
        if (--tilesLeft == 0) {
          return true;
        }
        // figure out which tile this uses, looping within 16x16 pages, and get it's data
        int usedCol = oam1 & 0x4000 ? spriteSize - 1 - col : col;
        int usedTile = ((((oam1 & 0xff) >> 4) + (row >> 3)) << 4) | (((oam1 & 0xf) + (usedCol >> 3)) & 0xf);
        uint32 pixels = PpuGetCached4bppRow(
          ppu, objAdr + usedTile * 16 + (row & 0x7));
        // go over each pixel
        int px_left = IntMax(-(col + x + kPpuExtraLeftRight), 0);
        int px_right = IntMin(256 + kPpuExtraLeftRight - (col + x), 8);
        PpuZbufType *dst = ppu->objBuffer.data + col + x + px_left + kPpuExtraLeftRight;

        for (int px = px_left; px < px_right; px++, dst++) {
          int shift = oam1 & 0x4000 ? px : 7 - px;
          int pixel = (pixels >> (shift * 4)) & 0xf;
          // draw it in the buffer if there is a pixel here, and the buffer there is still empty
          if (pixel != 0 && (dst[0] & 0xff) == 0)
            dst[0] = z + pixel;
        }
      }
    }
   }
  }
  return (tilesLeft != tilesLeftOrg);
}

uint8_t ppu_read(Ppu* ppu, uint8_t adr) {
  switch (adr) {
  case 0x34:
  case 0x35:
  case 0x36: {
    int result = ppu->m7matrix[0] * (ppu->m7matrix[1] >> 8);
    return (result >> (8 * (adr - 0x34))) & 0xff;
  }
  }
  return 0xff;
}

void ppu_write(Ppu* ppu, uint8_t adr, uint8_t val) {
  if (ppu->gpuRecording && (adr==0x18 || adr==0x19 || adr==0x22 || adr==0x04)) ppu->gpuInvalidWrite=true;
  switch(adr) {
    case 0x00: {  // INIDISP
      ppu->brightness = val & 0xf;
      ppu->forcedBlank = val & 0x80;
      break;
    }
    case 0x01: {
      ppu->spriteLinesValid = false;
      assert(val == 2);
      break;
    }
    case 0x02: {
      ppu->oamAdr = (ppu->oamAdr & ~0xff) | val;
      ppu->oamSecondWrite = false;
      break;
    }
    case 0x03: {
      assert((val & 0x80) == 0);
      ppu->oamAdr = (ppu->oamAdr & ~0xff00) | ((val & 1) << 8);
      ppu->oamSecondWrite = false;
      break;
    }
    case 0x04: {
      ppu->spriteLinesValid = false;
      if (!ppu->oamSecondWrite) {
        ppu->oamBuffer = val;
      } else {
        if (ppu->oamAdr < 0x110)
          ppu->oam[ppu->oamAdr++] = (val << 8) | ppu->oamBuffer;
      }
      ppu->oamSecondWrite = !ppu->oamSecondWrite;
      break;
    }
    case 0x05: {  // BGMODE
      ppu->mode = val & 0x7;
      assert(val == 7 || val == 9);
      assert(ppu->mode == 1 || ppu->mode == 7);
      assert((val & 0xf0) == 0);
      break;
    }
    case 0x06: {  // MOSAIC
      ppu->mosaicSize = (val >> 4) + 1;
      ppu->mosaicEnabled = (ppu->mosaicSize > 1) ? val : 0;
      break;
    }
    case 0x07:  // BG1SC
    case 0x08:
    case 0x09:
    case 0x0a: {
      // small tilemaps are used in attract intro
      ppu->bgLayer[adr - 7].tilemapWider = val & 0x1;
      ppu->bgLayer[adr - 7].tilemapHigher = val & 0x2;
      ppu->bgLayer[adr - 7].tilemapAdr = (val & 0xfc) << 8;
      break;
    }
    case 0x0b: {  // BG12NBA
      ppu->bgLayer[0].tileAdr = (val & 0xf) << 12;
      ppu->bgLayer[1].tileAdr = (val & 0xf0) << 8;
      break;
    }
    case 0x0c: { // BG34NBA
      ppu->bgLayer[2].tileAdr = (val & 0xf) << 12;
      ppu->bgLayer[3].tileAdr = (val & 0xf0) << 8;
      break;
    }
    case 0x0d: { // BG1HOFS
      ppu->m7matrix[6] = ((val << 8) | ppu->m7prev) & 0x1fff;
      ppu->m7prev = val;
      // fallthrough to normal layer BG-HOFS
    }
    case 0x0f:
    case 0x11:
    case 0x13: {
      ppu->bgLayer[(adr - 0xd) / 2].hScroll = ((val << 8) | (ppu->scrollPrev & 0xf8) | (ppu->scrollPrev2 & 0x7)) & 0x3ff;
      ppu->scrollPrev = val;
      ppu->scrollPrev2 = val;
      break;
    }
    case 0x0e: { // BG1VOFS
      ppu->m7matrix[7] = ((val << 8) | ppu->m7prev) & 0x1fff;
      ppu->m7prev = val;
      // fallthrough to normal layer BG-VOFS
    }
    case 0x10:
    case 0x12:
    case 0x14: {
      ppu->bgLayer[(adr - 0xe) / 2].vScroll = ((val << 8) | ppu->scrollPrev) & 0x3ff;
      ppu->scrollPrev = val;
      break;
    }
    case 0x15: {  // VMAIN
      if((val & 3) == 0) {
        ppu->vramIncrement = 1;
      } else if((val & 3) == 1) {
        ppu->vramIncrement = 32;
      } else {
        ppu->vramIncrement = 128;
      }
      assert(((val & 0xc) >> 2) == 0);
      ppu->vramIncrementOnHigh = val & 0x80;
      break;
    }
    case 0x16: {  // VMADDL
      ppu->vramPointer = (ppu->vramPointer & 0xff00) | val;
      break;
    }
    case 0x17: {  // VMADDH
      ppu->vramPointer = (ppu->vramPointer & 0x00ff) | (val << 8);
      break;
    }
    case 0x18: {  // VMDATAL
      ppu->retainedUsable = false;
      uint16_t vramAdr = ppu->vramPointer;
      ppu->vram[vramAdr & 0x7fff] = (ppu->vram[vramAdr & 0x7fff] & 0xff00) | val;
      if(!ppu->vramIncrementOnHigh) ppu->vramPointer += ppu->vramIncrement;
      break;
    }
    case 0x19: {  // VMDATAH
      ppu->retainedUsable = false;
      uint16_t vramAdr = ppu->vramPointer;
      ppu->vram[vramAdr & 0x7fff] = (ppu->vram[vramAdr & 0x7fff] & 0x00ff) | (val << 8);
      if(ppu->vramIncrementOnHigh) ppu->vramPointer += ppu->vramIncrement;
      break;
    }
    case 0x1a: {  // M7SEL
      assert(val == 0x80);
      ppu->m7largeField = val & 0x80;
      ppu->m7charFill = val & 0x40;
      ppu->m7yFlip = val & 0x2;
      ppu->m7xFlip = val & 0x1;
      break;
    }
    case 0x1b:  // M7A etc
    case 0x1c:
    case 0x1d:
    case 0x1e: {
      ppu->m7matrix[adr - 0x1b] = (val << 8) | ppu->m7prev;
      ppu->m7prev = val;
      break;
    }
    case 0x1f:
    case 0x20: {
      ppu->m7matrix[adr - 0x1b] = ((val << 8) | ppu->m7prev) & 0x1fff;
      ppu->m7prev = val;
      break;
    }
    case 0x21: {
      ppu->cgramPointer = val;
      ppu->cgramSecondWrite = false;
      break;
    }
    case 0x22: {
      if(!ppu->cgramSecondWrite) {
        ppu->cgramBuffer = val;
      } else {
        ppu->cgram[ppu->cgramPointer++] = (val << 8) | ppu->cgramBuffer;
        ppu->colorMapDirty = true;
        ppu->fixedMathValid = false; ppu->backdropMathValid = false;
      }
      ppu->cgramSecondWrite = !ppu->cgramSecondWrite;
      break;
    }
    case 0x23:  // W12SEL
      ppu->windowsel = (ppu->windowsel & ~0xff) | val;
      break;
    case 0x24:  // W34SEL
      ppu->windowsel = (ppu->windowsel & ~0xff00) | (val << 8);
      break;
    case 0x25:  // WOBJSEL
      ppu->windowsel = (ppu->windowsel & ~0xff0000) | (val << 16);
      break;
    case 0x26:
      ppu->window1left = val;
      break;
    case 0x27:
      ppu->window1right = val;
      break;
    case 0x28:
      ppu->window2left = val;
      break;
    case 0x29:
      ppu->window2right = val;
      break;
    case 0x2a:  // WBGLOG
      assert(val == 0);
      break;
    case 0x2b:  // WOBJLOG
      assert(val == 0);
      break;
    case 0x2c:  // TM
      ppu->screenEnabled[0] = val;
      break;
    case 0x2d:  // TS
      ppu->screenEnabled[1] = val;
      break;
    case 0x2e: // TMW
      ppu->screenWindowed[0] = val;
      break;
    case 0x2f:  // TSW
      ppu->screenWindowed[1] = val;
      break;
    case 0x30: {  // CGWSEL
      assert((val & 1) == 0);  // directColor always zero
      ppu->addSubscreen = val & 0x2;
      ppu->preventMathMode = (val & 0x30) >> 4;
      ppu->clipMode = (val & 0xc0) >> 6;
      break;
    }
    case 0x31: {  // CGADSUB
      ppu->subtractColor = val & 0x80;
      ppu->halfColor = val & 0x40;
      ppu->mathEnabled = val & 0x3f;
      break;
    }
    case 0x32: {  // COLDATA
      if(val & 0x80) ppu->fixedColorB = val & 0x1f;
      if(val & 0x40) ppu->fixedColorG = val & 0x1f;
      if(val & 0x20) ppu->fixedColorR = val & 0x1f;
      break;
    }
    case 0x33: {
      assert(val == 0);
      ppu->m7extBg_always_zero = val & 0x40;
      break;
    }
    default: {
      break;
    }
  }
}

