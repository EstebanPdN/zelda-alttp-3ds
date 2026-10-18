#ifndef ZELDA3_PPU_RETAINED_H_
#define ZELDA3_PPU_RETAINED_H_
#include "ppu.h"
#include <stdlib.h>
#include <stddef.h>
_Static_assert(offsetof(Ppu, vram) % 4 == 0, "VRAM word comparison requires alignment");
#include <string.h>
typedef struct PpuRetainedLayer {
  uint16_t pixels[512 * 512];
  uint64_t active[512];
  uint16_t tiles[4096];
  uint16_t mapAddress, tileAddress, width, height;
  bool valid;
} PpuRetainedLayer;
typedef struct PpuRetainedMaps {
  PpuRetainedLayer layers[3];
  uint16_t vram[0x8000];
  uint8_t changed[4096];
  uint32_t rebuiltTiles;
} PpuRetainedMaps;

// Retain indexed background planes across frames. Palette changes do not
// rebuild geometry/pixels. Compare actual VRAM bytes, including direct DMA
// writes, and redraw only changed map cells or the cells using changed art.
static void PpuRetainMaps(Ppu *ppu) {
  PpuRetainedMaps *cache = ppu->retained;
  if (!cache) return;
  cache->rebuiltTiles = 0;
  if (ppu->mode != 1) return;
  for (unsigned block=0; block<4096; block++) {
    const uint32_t *src=(const uint32_t *)(ppu->vram+block*8);
    uint32_t *dst=(uint32_t *)(cache->vram+block*8);
    bool changed=src[0]!=dst[0] || src[1]!=dst[1] || src[2]!=dst[2] || src[3]!=dst[3];
    cache->changed[block]=changed;
    if (changed) {dst[0]=src[0];dst[1]=src[1];dst[2]=src[2];dst[3]=src[3];}
  }
  for (unsigned layer=0; layer<3; layer++) {
    PpuRetainedLayer *out=&cache->layers[layer];
    if (!((ppu->screenEnabled[0]|ppu->screenEnabled[1]) & (1u<<layer))) {
      // Missed VRAM mutations while hidden must not leave a stale plane.
      out->valid=false; continue;
    }
    const BgLayer *bg=&ppu->bgLayer[layer];
    unsigned width=bg->tilemapWider?512:256, height=bg->tilemapHigher?512:256;
    bool full=!out->valid || out->mapAddress!=bg->tilemapAdr || out->tileAddress!=bg->tileAdr || out->width!=width || out->height!=height;
    out->mapAddress=bg->tilemapAdr;out->tileAddress=bg->tileAdr;out->width=width;out->height=height;
    if(full) memset(out->active,0,sizeof(out->active));
    unsigned stride=layer==2?8:16;
    for(unsigned ty=0;ty<height/8;ty++) for(unsigned tx=0;tx<width/8;tx++) {
      unsigned map=(bg->tilemapAdr+(ty&31)*32+(tx&31)+(tx>=32?0x400:0)+(ty>=32?(width==512?0x800:0x400):0))&0x7fff;
      uint16_t tile=ppu->vram[map];unsigned cell=ty*64+tx;
      unsigned addr=(bg->tileAdr+(tile&1023)*stride)&0x7fff;
      // BG tile bases and strides are 8-word aligned by SNES register layout.
      if(!full && out->tiles[cell]==tile && !cache->changed[addr/8] &&
          (layer==2 || !cache->changed[((addr+8)&0x7fff)/8])) continue;
      out->tiles[cell]=tile; cache->rebuiltTiles++;
      unsigned z=layer==0?((tile&0x2000)?0xc000:0x8000):layer==1?((tile&0x2000)?0xb100:0x7100):((tile&0x2000)?0xf200:0x1200);
      z+=(tile&0x1c00)>>(layer==2?8:6);
      for(unsigned y=0;y<8;y++) {
        unsigned row=(tile&0x8000)?7-y:y;
        uint32_t bits=ppu->vram[(addr+row)&0x7fff];
        if(layer!=2)bits|=(uint32_t)ppu->vram[(addr+row+8)&0x7fff]<<16;
        uint16_t *dst=out->pixels+(ty*8+y)*512+tx*8;bool active=false;
        for(unsigned x=0;x<8;x++) {
          unsigned i=(tile&0x4000)?x:7-x;
          unsigned pixel=((bits>>i)&1)|((bits>>(7+i))&2)|((bits>>(14+i))&4)|((bits>>(21+i))&8);
          dst[x]=pixel?z+pixel:0;active|=pixel!=0;
        }
        uint64_t mask=(uint64_t)1<<tx;
        out->active[ty*8+y]=(out->active[ty*8+y]&~mask)|(active?mask:0);
      }
    }
    out->valid=true;
  }
}
static inline uint32_t PpuMergePair(uint32_t old,uint32_t src,uint32_t mask) {
  uint32_t z=src&mask;
#if defined(__3DS__) && defined(__arm__) && !defined(__thumb__)
  uint32_t result;
  __asm__("usub16 %0, %2, %3\n\tsel %0, %2, %1" : "=&r"(result) : "r"(src),"r"(old),"r"(z) : "cc");
  return result;
#else
  return (((uint16_t)old>=(uint16_t)z?old:src)&0xffffu)|(((old>>16)>=(z>>16)?old:src)&0xffff0000u);
#endif
}
static inline void PpuMergeSpan(uint16_t *dst,const uint16_t *src,unsigned n,unsigned layer) {
  if (!n) return;
  uint32_t mask=layer==2?0xfffcfffcu:0xfff0fff0u;
  if(((uintptr_t)dst&2) && n) {
    if((src[0]&mask)>dst[0])dst[0]=src[0];dst++;src++;n--;
  }
  uint32_t *out=(uint32_t *)__builtin_assume_aligned(dst,4);
  unsigned pairs=n/2;
  if(!((uintptr_t)src&2)) {
    const uint32_t *in=(const uint32_t *)__builtin_assume_aligned(src,4);
    for(unsigned i=0;i<pairs;i++)out[i]=PpuMergePair(out[i],in[i],mask);
  } else if(pairs) {
    const uint32_t *in=(const uint32_t *)(src-1);uint32_t lo=in[0];
    for(unsigned i=0;i<pairs;i++) {uint32_t hi=in[i+1];out[i]=PpuMergePair(out[i],(lo>>16)|(hi<<16),mask);lo=hi;}
  }
  if(n&1) {unsigned i=n-1;if((src[i]&mask)>dst[i])dst[i]=src[i];}
}
#endif
