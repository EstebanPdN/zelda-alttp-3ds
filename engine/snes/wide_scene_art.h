#ifndef ZELDA3_WIDE_SCENE_ART_H_
#define ZELDA3_WIDE_SCENE_ART_H_
#include "wide_scene_art_data.h"

// Runs between main-background sampling and color math. Keep objects and HUD
// at their native priority; fog remains the original live subscreen layer.
static void PpuDrawForestArt(Ppu *p, unsigned line) {
  if (p->wideSceneArt != 1 || p->extraLeftRight != 72) return;
  unsigned wy = (line + p->bgLayer[1].vScroll) & 1023;
  if (wy >= 512) return;
  for (unsigned x=0; x<400; x++) {
    int ax=(int16_t)p->bgLayer[1].hScroll+(int)x;
    if ((unsigned)ax>=400) continue;
    unsigned value=(kWideForestCanopy[wy*100+(ax>>2)]>>((ax&3)*2))&3;
    unsigned index;
    if (value==3) {
      if (ax>=72 && ax<328) continue;
      int row=wy>=368 && wy<376 ? (int)wy-368 :
              wy>=464 && wy<480 ? (int)wy-456 : -1;
      if (ax<56 || ax>=72 || row<0) continue;
      index=p->wideSceneNativeTips[row*16+ax-56];
    } else index=124+value;
    PpuZbufType *dst=&p->bgBuffers[0].data[kPpuExtraLeftRight-72+x];
    unsigned layer=(*dst>>8)&15;
    if (layer==1 || layer==5) *dst=0x0100|index;
  }
}

static uint32_t WideArtLight(Ppu *p, uint32_t rgb) {
  uint32_t out=0;
  for (unsigned shift=0;shift<24;shift+=8) {
    unsigned c=(((rgb>>shift)&255)*p->wideSceneLight+128)>>8;
    c=IntMin(c+p->wideSceneWhite,255);
    out|=(c*p->brightness/15)<<shift;
  }
  return out;
}

// Generated curtains are presentation-only margins. Native center, polyhedral
// Triforce, Link and dialogue continue to be rendered by the game.
static void PpuDrawTriforceArt(Ppu *p, unsigned line, uint32_t *dst) {
  if (p->wideSceneArt!=2 || p->extraLeftRight!=72 || line<1 || line>240) return;
  unsigned y=line-1, mosaic=p->mosaicSize ? p->mosaicSize : 1;
  unsigned sy=y-y%mosaic;
  for (unsigned x=0;x<400;x++) {
    if (x>=72 && x<328 && y<224) continue;
    unsigned layer=(p->bgBuffers[0].data[kPpuExtraLeftRight-72+x]>>8)&15;
    if ((layer==2 || layer==4) && x>=72 && x<328) continue;
    unsigned sx=x-x%mosaic;
    uint32_t rgb;
    if (x<72 || x>=328) {
      unsigned col=x<72?IntMin(sx,71):IntMax(sx,328)-256;
      rgb=kWideTriforcePalette[kWideTriforceMargins[sy*144+col]];
    } else {
      rgb=0x394252;
      int delta=(int)(y-224)*3/4;
      if ((int)x>=159-delta && (int)x<241+delta)
        rgb=((sx+sy)&3)==0?0x7b424a:0x733942;
    }
    dst[x]=WideArtLight(p,rgb);
  }
}
#endif
