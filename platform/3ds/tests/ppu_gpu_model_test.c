// Rasterize emitted GPU geometry on the host and compare against the core.
// This validates tile addressing, priorities and windows, not PICA hardware.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "ppu_gpu_model.h"
enum { PITCH = 512, HEIGHT = 240, GUARD = 32 };
static unsigned candidate_flags = kPpuRenderFlags_Old3DS;
static uint32_t rng = 0x714a2bu;
static uint32_t Random(void) {
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
  return rng;
}
static void Setup(Ppu *p, unsigned scene) {
  ppu_reset(p);
  p->forcedBlank = scene % 29 == 0;
  p->brightness = Random() & 15;
  p->mode = scene % 11 == 0 ? 7 : 1;
  p->extraLeftRight = scene & 1 ? 72 : 0;
  p->extraLeftCur = Random() % (p->extraLeftRight + 1);
  p->extraRightCur = Random() % (p->extraLeftRight + 1);
  p->extraBottomCur = scene % 17;
  p->screenEnabled[0] = Random() & 0x17;
  p->screenEnabled[1] = Random() & 0x17;
  p->screenWindowed[0] = Random() & 0x17;
  p->screenWindowed[1] = Random() & 0x17;
  p->windowsel = Random() & 0xffffff;
  p->window1left = Random(); p->window1right = Random();
  p->window2left = Random(); p->window2right = Random();
  p->clipMode = Random() & 3; p->preventMathMode = Random() & 3;
  p->addSubscreen = Random() & 1; p->subtractColor = Random() & 1;
  p->halfColor = Random() & 1; p->mathEnabled = Random() & 63;
  p->fixedColorR = Random() & 31;
  p->fixedColorG = Random() & 31;
  p->fixedColorB = Random() & 31;
  p->mosaicSize = 1 + Random() % 16;
  p->mosaicEnabled = p->mosaicSize > 1 ? Random() & 7 : 0;
  // The upstream mosaic path indexes mosaicModulo by screen x and only
  // accepts native-width mosaic windows. Wide effects clamp side space.
  if (p->mosaicEnabled) p->extraLeftCur = p->extraRightCur = 0;
  for (int i = 0; i < 256; i++) p->cgram[i] = Random() & 0x7fff;
  for (int i = 0; i < 0x8000; i++) p->vram[i] = Random();
  for (int i = 0; i < 0x110; i++) p->oam[i] = Random();
  p->objSize = 0; // Zelda uses 8/16-pixel sprites.
  for (int i = 0; i < 3; i++) {
    p->bgLayer[i].hScroll = Random() & 1023;
    p->bgLayer[i].vScroll = Random() & 1023;
    p->bgLayer[i].tilemapAdr = i * 0x1000;
    p->bgLayer[i].tileAdr = i * 0x2000;
    p->bgLayer[i].tilemapWider = Random() & 1;
    p->bgLayer[i].tilemapHigher = Random() & 1;
  }
  for (int i = 0; i < 8; i++) p->m7matrix[i] = i < 4 ? (int)(Random() % 513) - 256 : Random() & 0x1fff;
}
static void ReadState(void *context, void *data, size_t size) {
  if (fread(data, 1, size, context) != size) { fputs("Short checkpoint\n", stderr); exit(2); }
}
static void LoadDump(Ppu *p, const char *directory) {
  uint8_t ram[131072]; char path[1024];
  snprintf(path, sizeof(path), "%s/ram.bin", directory);
  FILE *f = fopen(path, "rb");
  if (!f) { perror(path); exit(2); }
  ReadState(f, ram, sizeof(ram)); fclose(f);
  snprintf(path, sizeof(path), "%s/load-state.bin", directory);
  f = fopen(path, "rb"); if (!f) { perror(path); exit(2); }
  uint8_t magic[8]; ReadState(f, magic, 8);
  if (memcmp(magic, "Z3DLDS01", 8)) { fputs("Invalid checkpoint\n", stderr); exit(2); }
  // Dump header + payload header + serialized APU/DSP/DMA prefix, per
  // InternalSaveLoad. Only read the PPU fields that the format actually saves.
  if (fseek(f, 24 + 20 + 27 + 65536 + 40 + 3024 + 15 + 192, SEEK_SET)) exit(2);
  ppu_reset(p); ppu_saveload(p, ReadState, f); fclose(f);
  memcpy(p->oam, ram + 0x800, sizeof(p->oam));
  const unsigned regs[][2] = {
    {0x00, 0x13}, {0x05, 0x94}, {0x06, 0x95},
    {0x23, 0x96}, {0x24, 0x97}, {0x25, 0x98},
    {0x30, 0x99}, {0x31, 0x9a}, {0x32, 0x9c}, {0x32, 0x9d}, {0x32, 0x9e},
    {0x2c, 0x1c}, {0x2d, 0x1d}, {0x2e, 0x1e}, {0x2f, 0x1f},
  };
  for (unsigned i = 0; i < sizeof(regs)/sizeof(regs[0]); i++)
    ppu_write(p, regs[i][0], ram[regs[i][1]]);
  const unsigned scroll[][2] = {
    {0x0d, 0x120}, {0x0e, 0x124}, {0x0f, 0x11e},
    {0x10, 0x122}, {0x11, 0xe4}, {0x12, 0xea}
  };
  for (unsigned i = 0; i < sizeof(scroll)/sizeof(scroll[0]); i++) {
    ppu_write(p, scroll[i][0], ram[scroll[i][1]]);
    ppu_write(p, scroll[i][0], ram[scroll[i][1] + 1]);
  }
  ppu_write(p, 0x0b, 0x22); ppu_write(p, 0x0c, 7);
  // GPU dumps preserve the live PPU memory explicitly. A serialized checkpoint
  // also contains legacy emulator state; use these blobs for rendered pixels.
  snprintf(path,sizeof(path),"%s/vram.bin",directory);f=fopen(path,"rb");if(!f)exit(2);
  ReadState(f,p->vram,sizeof(p->vram));fclose(f);
  snprintf(path,sizeof(path),"%s/cgram.bin",directory);f=fopen(path,"rb");if(!f)exit(2);
  ReadState(f,p->cgram,sizeof(p->cgram));fclose(f);
  snprintf(path,sizeof(path),"%s/oam.bin",directory);f=fopen(path,"rb");if(!f)exit(2);
  ReadState(f,p->oam,sizeof(p->oam));fclose(f);
  p->extraLeftRight = p->extraLeftCur = p->extraRightCur = 72;
  snprintf(path, sizeof(path), "%s/ppu.txt", directory);
  f = fopen(path, "rb");
  if (f) {
    char text[256]; unsigned configured, left, right, bottom;
    while (fgets(text, sizeof(text), f)) {
      unsigned layer,hscroll,vscroll,map,tiles,wider,higher;
      if(sscanf(text,"BG%u scroll=%u,%u map=%x tiles=%x wider=%u higher=%u",
                &layer,&hscroll,&vscroll,&map,&tiles,&wider,&higher)==7 && layer>=1 && layer<=4)
        p->bgLayer[layer-1]=(BgLayer){.hScroll=hscroll,.vScroll=vscroll,.tilemapAdr=map,
          .tileAdr=tiles,.tilemapWider=wider,.tilemapHigher=higher};
      if (sscanf(text, "side_space configured/left/right/bottom=%u/%u/%u/%u",
                 &configured, &left, &right, &bottom) == 4) {
        if (configured > kPpuExtraLeftRight || left > configured || right > configured || bottom > 16) exit(2);
        p->extraLeftRight = configured; p->extraLeftCur = left;
        p->extraRightCur = right; p->extraBottomCur = bottom;
      }
    }
    fclose(f);
  }
  // Reconstructed static register state, not full game/HDMA playback. The
  // randomized suite separately exercises changing windows and math registers.
}

static uint32_t surface[2][512*240], output[512*240];
static uint16_t depth[2][512*240];
static unsigned char stencil[2][512*240];
static uint32_t *atlas;
static unsigned width;
static unsigned morton(unsigned x,unsigned y) {
 return (x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
}
static uint16_t Unpack(uint32_t c) {return ((c>>27)&31)|(((c>>19)&31)<<5)|(((c>>11)&31)<<10);}
static uint16_t RefRgb(uint32_t c) {return ((c>>19)&31)|(((c>>11)&31)<<5)|(((c>>3)&31)<<10);}
static bool Raster(void *ctx,unsigned group,const PicaQuad *q) {
 (void)ctx;
 for(int y=q->y0;y<q->y1;y++) for(int x=q->x0;x<q->x1;x++) {
  assert(x>=0 && x<(int)width && y>=0 && y<240);
  unsigned i=y*512+x;
  if(group>=4) {
   output[i]=PicaComposeRgb5(Unpack(surface[0][i]),Unpack(surface[1][i]),
     (surface[0][i]&255)==255,(surface[1][i]&255)==255,group-4);
  } else {
   // Integer rational interpolation at pixel centers, then PICA nearest texel.
   double u=q->u0+(q->u1-q->u0)*(x-q->x0+0.5)/(q->x1-q->x0);
   double v=q->v0+(q->v1-q->v0)*(y-q->y0+0.5)/(q->y1-q->y0);
   int tx=(int)(u/4),ty=511-(int)(v/8);
   assert(tx>=0&&tx<1024&&ty>=0&&ty<512);
   unsigned offset=((ty/8)*128+tx/8)*64+morton(tx&7,ty&7);
   uint32_t c=atlas[offset];
   unsigned a=(c&255)*q->a/255;
   if(!a) continue;
   unsigned sub=group/2;
   if(group&1) {if(stencil[sub][i])continue;stencil[sub][i]=1;}
   if(q->depth<=depth[sub][i])continue;
   depth[sub][i]=q->depth;
   surface[sub][i]=((((c>>24)*q->r/255)<<24)|((((c>>16)&255)*q->g/255)<<16)|((((c>>8)&255)*q->b/255)<<8)|a);
  }
 }
 return true;
}
static void MathTest(void) {
 for(unsigned flags=0;flags<16;flags++) for(unsigned eligible=0;eligible<2;eligible++)
 for(unsigned pixel=0;pixel<2;pixel++) for(unsigned m=0;m<32;m++)for(unsigned s=0;s<32;s++) {
  unsigned n=(flags&4)?0:m;
  if(eligible && !(flags&8)) {
   n=(flags&1)?(n>s?n-s:0):n+s;
   if((flags&2)&&pixel)n/=2;
   if(n>31)n=31;
  }
  assert(PicaComposeRgb5(m,s,eligible,pixel,flags)==n);
 }
 puts("PASS 65,536 independent SNES color-math cases");
}
int main(int argc,char **argv) {
 MathTest();
 unsigned shared=0;unsigned long long vertices=0;
 Ppu *p=ppu_init(),*scratch=calloc(1,sizeof(Ppu));
 PicaAtlas *cache=calloc(1,sizeof(PicaAtlas));
 PicaLine lines[240];
 atlas=malloc(1024*512*4);PicaAtlasInit(cache,atlas);
 uint32_t cpu[512*240];
 unsigned scenes=argc>1?atoi(argv[1]):128;
 for(unsigned scene=0;scene<scenes+(unsigned)(argc>2?argc-2:0);scene++) {
  if(scene<scenes) {
   Setup(p,scene);p->mode=1;p->brightness=15;p->mosaicSize=1;p->mosaicEnabled=0;
   p->objSize=scene%8;
  } else LoadDump(p,argv[2+scene-scenes]);
  static int16_t wl[240],wr[240];
  p->windowExtLeft=p->windowExtRight=NULL;
  if(scene<scenes && scene%4==1) {
    p->forcedBlank=false;p->screenEnabled[0]=0x17;p->screenEnabled[1]=0x13;
    p->screenWindowed[0]=p->screenWindowed[1]=0x17;
    p->windowsel=0x330333;p->clipMode=2;p->addSubscreen=true;
    p->fixedColorR=p->fixedColorG=p->fixedColorB=0;
    for(int y=0;y<240;y++) {int span=200-abs(y-120)*2;
      wl[y]=128-span;wr[y]=128+span;
    }
    p->windowExtLeft=wl;p->windowExtRight=wr;
  }
  width=256+p->extraLeftRight*2;
  for(unsigned frame=0;frame<2;frame++) {
   memset(surface,0,sizeof(surface));memset(depth,0,sizeof(depth));memset(stencil,0,sizeof(stencil));
   memset(output,0,sizeof(output));memset(cpu,0,sizeof(cpu));
   p->colorMapDirty=true;
   PpuBeginDrawing(p,(uint8_t*)cpu,512*4,1|(scene&2?kPpuRenderFlags_NoSpriteLimits:0));
   p->renderObjYOffset=scene<scenes?scene%17:0;
   for(unsigned y=0;y<240;y++) {
    if(scene<scenes && scene%3==0 && scene%4!=1 && y%31==0) {
     p->bgLayer[0].hScroll=(p->bgLayer[0].hScroll+7)&1023;
     p->window1left+=3;p->fixedColorB=(p->fixedColorB+1)&31;
    }
    PicaCaptureLine(lines+y,p,y);ppu_runLine(p,y+1);
   }
   PicaAtlasBegin(cache);
   PicaFrame f={.memory=p,.lines=lines,.scratch=scratch,.atlas=cache,.pixels=atlas,.width=width,.height=240,.emit=Raster};
   if(!PicaBuildFrame(&f)){fprintf(stderr,"FAIL rejected scene %u: %s\n",scene,f.failure);return 1;}
   shared+=f.sharedWindow;
   for(unsigned group=0;group<PICA_GROUPS;group++)vertices+=f.quads[group]*6;
   for(unsigned y=0;y<240;y++)for(unsigned x=0;x<width;x++) {
    unsigned i=y*512+x;
    if(output[i]!=RefRgb(cpu[i])) {
     fprintf(stderr,"FAIL scene=%u frame=%u x=%u y=%u GPU=%04x CPU=%04x raw=%06x main=%08x sub=%08x objsize=%u extra=%u/%u/%u/%u flags=%u\n",scene,frame,x,y,output[i],RefRgb(cpu[i]),cpu[i],surface[0][i],surface[1][i],p->objSize,p->extraLeftRight,p->extraLeftCur,p->extraRightCur,p->extraBottomCur,p->renderFlags);return 1;
    }
   }
   // Palette and direct VRAM changes must invalidate retained atlas entries.
   if(scene<scenes)for(unsigned i=0;i<32;i++){p->vram[Random()&32767]=Random();p->cgram[Random()&255]=Random()&32767;}
  }
  if(scene>=scenes) printf("PASS dump %s\n",argv[2+scene-scenes]);
 }
 printf("PASS %u randomized GPU geometry scenes, cold/warm atlas, 224/240 rows, native/wide\n",scenes);
 printf("Shared-window frames=%u total vertices=%llu\n",shared,vertices);
 ppu_free(p);free(scratch);free(cache);free(atlas);return 0;
}
