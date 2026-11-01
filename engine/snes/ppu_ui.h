#pragma once
#include "ppu.h"

enum { kPpuUiNone, kPpuUiHud, kPpuUiInventory, kPpuUiSave };

// Presentation state only. The SNES tilemap, input coordinates and saved state
// remain native. Both renderers sample this same layout at integer pixel scale.
static inline unsigned PpuUiChooseLayout(unsigned module, unsigned submodule) {
  if (module == 14) {
    if (submodule == 1) return kPpuUiInventory;
    if (submodule == 11) return kPpuUiSave;
    if (submodule == 2 || submodule == 4 || submodule == 8 || submodule == 9)
      return kPpuUiHud;
    return kPpuUiNone;
  }
  return ((module >= 6 && module <= 11) || (module >= 15 && module <= 17) ||
          module == 19 || (module >= 21 && module <= 23)) ? kPpuUiHud : kPpuUiNone;
}

// The room and its HUD remain visible during dungeon-map fade-out/fade-in.
// Selecting the map layout at module entry shrinks the HUD for these frames.
static inline unsigned PpuUiChooseSceneLayout(unsigned module, unsigned submodule,
                                             unsigned map_state) {
  if (module == 14 && submodule == 3 && (map_state == 0 || map_state >= 7))
    return kPpuUiHud;
  // The exterior map still shows mode-1 gameplay in its initial fade.
  if (module == 14 && (submodule == 7 || submodule == 10) && map_state == 0)
    return kPpuUiHud;
  return PpuUiChooseLayout(module, submodule);
}

static inline unsigned PpuUiWorldY(const Ppu *p, unsigned line) {
  return (line + p->bgLayer[2].vScroll) & (p->bgLayer[2].tilemapHigher ? 511 : 255);
}

static inline bool PpuUiActiveLine(const Ppu *p, unsigned line) {
  if (!p->wideUi || p->extraLeftRight != 72 || p->mode != 1 || p->forcedBlank ||
      line >= 225u + p->extraBottomCur || !(p->screenEnabled[0] & 4) || (p->screenEnabled[1] & 4) ||
      (p->screenWindowed[0] & 4) || (p->mathEnabled & 4) ||
      (p->mosaicEnabled & 4) || (p->bgLayer[2].hScroll & 255)) return false;
  unsigned wy = PpuUiWorldY(p, line);
  return wy < 64 || (p->wideUi == kPpuUiInventory && wy >= 256) ||
    (p->wideUi == kPpuUiSave && wy >= p->wideUiSaveY && wy < p->wideUiSaveY + 64u);
}

static inline uint16_t PpuUiWord(const Ppu *p, const uint16_t *vram, unsigned x, unsigned y) {
  const BgLayer *bg = &p->bgLayer[2];
  unsigned address = bg->tilemapAdr + ((y >> 3) & 31) * 32 + ((x >> 3) & 31);
  if (y >= 256) address += bg->tilemapWider ? 0x800 : 0x400;
  return vram[address & 0x7fff];
}

static inline bool PpuUiIsPanelEdge(uint16_t word) {
  unsigned chr = word & 1023;
  return chr == 0xfb || chr == 0xfc;
}

// x is in the complete 400-pixel output. Return a native tile and the pixel
// within it, or false for transparent space. Panel extensions use the engine's
// existing border/fill tiles, never resampled lettering or embedded artwork.
static inline bool PpuUiSample(const Ppu *p, const uint16_t *vram,
                               unsigned line, unsigned x,
                               uint16_t *tile, unsigned *sx, unsigned *sy) {
  unsigned wy = PpuUiWorldY(p, line), source;
  *sy = wy & 7;
  if (wy < 64) {
    // HUD columns 0..18 contain magic/items/counters; 20..31 hold life/hearts.
    if (x < 152) source = x;
    else if (x >= 296) source = x - 144;
    else return false;
  } else if (p->wideUi == kPpuUiInventory && wy >= 256) {
    bool right = x >= 248;
    unsigned left = right ? 248 : 8, end = right ? 392 : 240;
    unsigned native_left = right ? 168 : 8, native_end = right ? 248 : 160;
    unsigned content_left = right ? 288 : 56;
    // The expanded inventory moves the panels outward: the left ones start at
    // column 0 and the upper one ends at column 20, the right ones start at 22.
    if (right) {
      if (!PpuUiIsPanelEdge(PpuUiWord(p, vram, 168, wy)))
        native_left = 176, native_end = 256;
    } else if (PpuUiIsPanelEdge(PpuUiWord(p, vram, 0, wy))) {
      native_left = 0;
      if (PpuUiIsPanelEdge(PpuUiWord(p, vram, 160, wy))) native_end = 168;
      content_left = 48;
    }
    if (x < left || x >= end) return false;
    uint16_t edge = PpuUiWord(p, vram, native_left, wy);
    unsigned chr = edge & 1023;
    if (!PpuUiIsPanelEdge(edge)) return false; // gap between panels
    unsigned content_end = content_left + native_end - native_left - 16;
    if (x < left + 8) source = native_left + x - left;
    else if (x >= end - 8) source = native_end - 8 + x - (end - 8);
    else if (x >= content_left && x < content_end)
      source = native_left + 8 + x - content_left;
    else {
      *tile = chr == 0xfb ? (edge & 0xbc00) | 0xf9 : 0x24f5;
      *sx = x & 7;
      return true;
    }
  } else if (p->wideUi == kPpuUiSave) {
    unsigned left = p->wideUiSaveX, end = left + 336;
    if (x < left || x >= end) return false;
    if (x < left + 8) source = left + x - left;
    else if (x >= end - 8) source = left + 184 + x - (end - 8);
    else if (x >= left + 80 && x < left + 256) source = x - 72;
    else {
      unsigned row = (wy - p->wideUiSaveY) >> 3;
      // RenderText_Draw_Border writes a top row, six body rows and a bottom
      // row. Include all eight rows so the bottom corners move with the top.
      *tile = row == 0 || row == 7 ? PpuUiWord(p, vram, left + 8, wy) : 0x387f;
      *sx = x & 7;
      return true;
    }
  } else return false;
  *tile = PpuUiWord(p, vram, source, wy);
  *sx = source & 7;
  return true;
}
