#include "ppu_gpu_model.h"
#include <string.h>
#include <assert.h>

static const uint8_t morton[64] = {
  0,1,4,5,16,17,20,21,2,3,6,7,18,19,22,23,
  8,9,12,13,24,25,28,29,10,11,14,15,26,27,30,31,
  32,33,36,37,48,49,52,53,34,35,38,39,50,51,54,55,
  40,41,44,45,56,57,60,61,42,43,46,47,58,59,62,63
};
static const uint8_t sizes[8][2] = {
  {8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{16,32},{16,32}
};
static uint32_t Hash(uint32_t key) { return (key * 2654435761u) >> 18; }
static uint32_t Encode(uint16_t rgb, unsigned alpha) {
  // Bias by one BEFORE arithmetic. This makes half-add interpolation robust
  // to the 128/255 coefficient while retaining exact SNES 5-bit quantization.
  return (((rgb&31)*8+1)<<24) | ((((rgb>>5)&31)*8+1)<<16) |
         ((((rgb>>10)&31)*8+1)<<8) | alpha;
}
void PicaAtlasInit(PicaAtlas *a, uint32_t *pixels) {
  memset(a,0,sizeof(*a)); memset(pixels,0,PICA_ATLAS_W*PICA_ATLAS_H*4);
  // Slot zero is a permanent opaque white tile for backdrop/fixed-color quads.
  unsigned offset=0;
  for (unsigned i=0;i<64;i++) pixels[offset+i]=0xffffffffu;
  a->tile[0].valid=true; a->tile[0].opaque=true; a->dirty[0]=1;
}
void PicaAtlasBegin(PicaAtlas *a) {
  if (++a->frame==0) { a->frame=1; for(unsigned i=1;i<PICA_SLOTS;i++) a->tile[i].used=a->tile[i].checked=0; }
  a->hits=a->decodes=a->live=0;
}
static void Remove(PicaAtlas *a,uint32_t key) {
  unsigned i=Hash(key);
  while(a->hash[i] && a->tile[a->hash[i]].key!=key) i=(i+1)&(PICA_HASH-1);
  if(!a->hash[i]) return;
  a->hash[i]=0;
  // Reinsert the following cluster, so lookup can still stop at a free entry.
  for(unsigned j=(i+1)&(PICA_HASH-1);a->hash[j];j=(j+1)&(PICA_HASH-1)) {
    unsigned slot=a->hash[j]; a->hash[j]=0;
    unsigned k=Hash(a->tile[slot].key);
    while(a->hash[k]) k=(k+1)&(PICA_HASH-1);
    a->hash[k]=slot;
  }
}
static int Tile(PicaFrame *f,unsigned address,unsigned palette,unsigned bpp) {
  PicaAtlas *a=f->atlas;
  address&=0x7fff;
  uint32_t key=address | (palette<<15) | ((bpp==4)<<23);
  unsigned h=Hash(key),slot;
  while(a->hash[h] && a->tile[a->hash[h]].key!=key) h=(h+1)&(PICA_HASH-1);
  slot=a->hash[h];
  if(!slot) {
    unsigned scanned=0;
    do { a->cursor=a->cursor%(PICA_SLOTS-1)+1; slot=a->cursor; }
    while(a->tile[slot].used==a->frame && ++scanned<PICA_SLOTS-1);
    if(a->tile[slot].used==a->frame) { f->failure="atlas-live-capacity"; return -1; }
    if(a->tile[slot].valid) Remove(a,a->tile[slot].key);
    h=Hash(key); while(a->hash[h]) h=(h+1)&(PICA_HASH-1);
    a->hash[h]=slot; a->tile[slot].valid=false; a->tile[slot].key=key;
  }
  PicaTile *t=&a->tile[slot];
  if(t->used!=a->frame) { t->used=a->frame; a->live++; }
  if(t->checked==a->frame && t->valid) { a->hits++; return t->opaque?(int)slot:-2; }
  t->checked=a->frame;
  unsigned words=bpp==4?16:8, colors=1u<<bpp;
  const uint16_t *src=f->memory->vram+address, *pal=f->memory->cgram+palette;
  if(t->valid && !memcmp(t->source,src,words*2) && !memcmp(t->palette,pal,colors*2)) {
    a->hits++; return t->opaque?(int)slot:-2;
  }
  memcpy(t->source,src,words*2); memcpy(t->palette,pal,colors*2);
  t->valid=true; t->opaque=false; a->decodes++; a->dirty[slot/32]|=1u<<(slot&31);
  // Native tiled memory starts at the top row; normalized V=1 samples it.
  uint32_t *dest=f->pixels+slot*64;
  for(unsigned y=0;y<8;y++) {
    uint32_t bits=src[y]; if(bpp==4) bits|=(uint32_t)src[y+8]<<16;
    for(unsigned x=0;x<8;x++) {
      unsigned i=7-x;
      unsigned p=((bits>>i)&1)|((bits>>(i+7))&2)|((bits>>(i+14))&4)|((bits>>(i+21))&8);
      dest[morton[y*8+x]]=p?Encode(pal[p],255):0;
      t->opaque|=p!=0;
    }
  }
  return t->opaque?(int)slot:-2;
}
void PicaCaptureLine(PicaLine *out,const Ppu *p,unsigned y) {
  memcpy(out->bytes,p,PICA_LINE_BYTES);
  // Prefix access through a suitably aligned full scratch happens later.
  int wy=(int)y-p->renderObjYOffset;
  int16_t left=p->windowExtLeft?(wy>=0?p->windowExtLeft[wy]:0):p->windowExtLeftCur;
  int16_t right=p->windowExtRight?(wy>=0?p->windowExtRight[wy]:-1):p->windowExtRightCur;
  memcpy(out->bytes+offsetof(Ppu,windowExtLeftCur),&left,sizeof(left));
  memcpy(out->bytes+offsetof(Ppu,windowExtRightCur),&right,sizeof(right));
}
static Ppu *Line(PicaFrame *f,unsigned y) {
  memcpy(f->scratch,f->lines[y].bytes,PICA_LINE_BYTES); return f->scratch;
}
static bool Emit(PicaFrame *f,unsigned group,PicaQuad q) {
  if(q.x0>=q.x1 || q.y0>=q.y1) return true;
  if(!f->emit(f->context,group,&q)) { f->failure="vertex-capacity"; return false; }
  f->quads[group]++; return true;
}
static PicaQuad Solid(int x0,int y0,int x1,int y1,uint16_t rgb,unsigned alpha) {
  return (PicaQuad){x0,y0,x1,y1,2,4092,2,4092,1,
    (rgb&31)*8+1,((rgb>>5)&31)*8+1,((rgb>>10)&31)*8+1,alpha};
}
static bool TileQuad(PicaFrame *f,unsigned group,unsigned slot,int x,int y,
                     int w,int h,int sx,int sy,bool hf,bool vf,unsigned z,unsigned alpha) {
  unsigned col=slot%(PICA_ATLAS_W/8),row=slot/(PICA_ATLAS_W/8);
  int u0=col*8+(hf?8-sx:sx),u1=u0+(hf?-w:w);
  int v0=row*8+(vf?8-sy:sy),v1=v0+(vf?-h:h);
  PicaQuad q={x,y,x+w,y+h,u0*4,4096-v0*8,u1*4,4096-v1*8,z,255,255,255,alpha};
  return Emit(f,group,q);
}
static unsigned Band(PicaFrame *f,unsigned y,unsigned max) {
  unsigned visible=224+f->scratch->extraBottomCur;
  if(y<visible && max>visible-y) max=visible-y;
  unsigned count=f->bandEnd[y]-y;
  return count<max?count:max;
}
// Build bands from this pass's actual inputs. Unrelated Mode 7/HDMA state
// must not split otherwise identical Mode 1 tiles into single scanlines.
static void SelectBands(PicaFrame *f,unsigned kind,unsigned layer,unsigned sub) {
  (void)sub;
  if(kind!=1){memcpy(f->bandEnd,kind==3?f->commonEnd:f->layerEnd,sizeof(f->bandEnd));return;}
  for(unsigned y=f->height;y--;) {
    f->bandEnd[y]=y+1;
    if(f->layerEnd[y]>y+1 && !memcmp(f->lines[y].bytes+offsetof(Ppu,bgLayer)+layer*sizeof(BgLayer),
       f->lines[y+1].bytes+offsetof(Ppu,bgLayer)+layer*sizeof(BgLayer),sizeof(BgLayer)))
      f->bandEnd[y]=f->bandEnd[y+1];
  }
}
static bool HasLayer(const PicaFrame *f,unsigned sub,unsigned mask) {
  for(unsigned y=0;y<f->height;y++)
    if((f->lines[y].bytes[offsetof(Ppu,screenEnabled)+sub]&mask) &&
       (!sub || f->lines[y].bytes[offsetof(Ppu,addSubscreen)]))return true;
  return false;
}
static bool Backgrounds(PicaFrame *f,unsigned sub) {
  unsigned group=sub*2;
  SelectBands(f,0,0,sub);
  for(unsigned y=0;y<f->height;) {
    Ppu *p=Line(f,y); unsigned n=Band(f,y,f->height-y);
    unsigned alpha=sub?(!p->addSubscreen?255:127):((p->mathEnabled&32)?255:127);
    uint16_t rgb=sub?(p->fixedColorR|(p->fixedColorG<<5)|(p->fixedColorB<<10)):f->memory->cgram[0];
    if(p->forcedBlank || y>=224u+p->extraBottomCur) {rgb=0;alpha=127;}
    if(!Emit(f,group,Solid(0,y,f->width,y+n,rgb,alpha))) return false;
    y+=n;
  }
  for(unsigned layer=0;layer<3;layer++) {
   if(!HasLayer(f,sub,1u<<layer))continue;
   SelectBands(f,1,layer,sub);
   for(unsigned y=0;y<f->height;) {
    Ppu *p=Line(f,y); BgLayer *bg=&p->bgLayer[layer];
    unsigned wy=(y+1+bg->vScroll)&(bg->tilemapHigher?511:255);
    unsigned h=Band(f,y,8-(wy&7));
    if(p->forcedBlank || y>=224u+p->extraBottomCur || !(p->screenEnabled[sub]&(1u<<layer)) || (sub && !p->addSubscreen)) {y+=h;continue;}
    if(y+h>224u+p->extraBottomCur) h=224u+p->extraBottomCur-y;
    PpuWindowSpans win;
    PpuGetWindowSpans(p,layer,!f->sharedWindow && (p->screenWindowed[sub]&(1u<<layer))!=0,&win);
    for(unsigned i=0;i<win.nr;i++) {
      if(win.bits&(1u<<i)) continue;
      int x=win.edges[i],end=win.edges[i+1];
      while(x<end) {
        unsigned wx=(x+bg->hScroll)&(bg->tilemapWider?511:255);
        unsigned map=(bg->tilemapAdr+((wy>>3)&31)*32+((wx>>3)&31)+
          (wx>=256?0x400:0)+(wy>=256?(bg->tilemapWider?0x800:0x400):0))&0x7fff;
        unsigned tile=f->memory->vram[map];
        unsigned bpp=layer==2?2:4;
        int slot=Tile(f,(bg->tileAdr+(tile&1023)*(bpp==4?16:8))&0x7fff,
                       (tile&0x1c00)>>(bpp==4?6:8),bpp);
        if(slot==-1) return false;
        unsigned w=IntMin(8-(wx&7),end-x);
        unsigned z=layer==0?((tile&0x2000)?0xc000:0x8000):layer==1?((tile&0x2000)?0xb100:0x7100):((tile&0x2000)?0xf200:0x1200);
        if(slot>=0 && !TileQuad(f,group,slot,x+p->extraLeftRight,y,w,h,wx&7,wy&7,
            tile&0x4000,tile&0x8000,z,sub?255:((p->mathEnabled&(1u<<layer))?255:127))) return false;
        x+=w;
      }
    }
    y+=h;
   }
  }
  return true;
}
static bool Objects(PicaFrame *f,unsigned sub) {
  // First select accepted slivers using the original per-line SNES limits.
  // Then draw in OAM order, merging equal rows up to tile/window boundaries.
  if(!HasLayer(f,sub,16))return true;
  uint16_t active[128],first[128],last[128];unsigned activeCount=0;
  for(unsigned index=0;index<256;index+=2)if((f->memory->oam[index]>>8)!=0xf0) {
    active[activeCount++]=index;first[index/2]=f->height;last[index/2]=0;
  }
  uint8_t (*columns)[PICA_MAX_LINES]=f->atlas->objectColumns;
  memset(columns,0,sizeof(f->atlas->objectColumns));
  SelectBands(f,2,0,sub);
  for(unsigned y=0;y<f->height;y++) {
    Ppu *p=Line(f,y);
    if(p->forcedBlank || y>=224u+p->extraBottomCur || !(p->screenEnabled[sub]&16) || (sub&&!p->addSubscreen)) continue;
    int sprites=33,tiles=35;
    if(p->renderFlags&kPpuRenderFlags_NoSpriteLimits) sprites=tiles=1024;
    bool stop=false;
    for(unsigned item=0;item<activeCount && !stop;item++) {
      unsigned index=active[item];
      unsigned yy=f->memory->oam[index]>>8;
      if(yy==0xf0) continue;
      unsigned row=(y-yy-p->renderObjYOffset)&255;
      unsigned high=f->memory->oam[0x100+(index>>4)]>>(index&15);
      unsigned size=sizes[p->objSize][(high>>1)&1];
      if(row>=size) continue;
      int x=(f->memory->oam[index]&255)+(high&1)*256;
      x-=(x>=256+p->extraLeftRight)*512; x+=p->renderObjXOffset;
      if(x<=-(int)(size+p->extraLeftRight)) continue;
      if(--sprites==0) break;
      for(unsigned col=0;col<size;col+=8) {
        int left=x+col;
        if(left<=-8-p->extraLeftRight || left>=256+p->extraLeftRight) continue;
        if(--tiles==0) {stop=true;break;}
        columns[index/2][y]|=1u<<(col/8);
        if(first[index/2]>y)first[index/2]=y;
        last[index/2]=y+1;
      }
    }
  }
  for(unsigned item=0;item<activeCount;item++) {
   unsigned index=active[item];
   for(unsigned y=first[index/2];y<last[index/2];) {
    unsigned mask=columns[index/2][y];
    if(!mask){y++;continue;}
    Ppu *p=Line(f,y);
    unsigned yy=f->memory->oam[index]>>8;
    unsigned high=f->memory->oam[0x100+(index>>4)]>>(index&15);
    unsigned size=sizes[p->objSize][(high>>1)&1];
    unsigned attr=f->memory->oam[index+1],row=(y-yy-p->renderObjYOffset)&255;
    bool vf=(attr&0x8000)!=0;
    if(vf)row=size-1-row;
    unsigned h=Band(f,y,vf?1+(row&7):8-(row&7));
    for(unsigned dy=1;dy<h;dy++)if(columns[index/2][y+dy]!=mask){h=dy;break;}
    int x=(f->memory->oam[index]&255)+(high&1)*256;
    x-=(x>=256+p->extraLeftRight)*512;x+=p->renderObjXOffset;
    unsigned base=(attr&0x100)?p->objTileAdr2:p->objTileAdr1;
    unsigned palette=128+((attr>>9)&7)*16;
    unsigned z=((((attr>>12)&3)*4+2)*16+4+((attr&0x800)?0:2))<<8;
    PpuWindowSpans win;PpuGetWindowSpans(p,4,!f->sharedWindow && (p->screenWindowed[sub]&16)!=0,&win);
    while(mask) {
      unsigned col=__builtin_ctz(mask)*8;mask&=mask-1;int left=x+col;
      unsigned usedcol=(attr&0x4000)?size-1-col:col;
      unsigned usedtile=((((attr&255)>>4)+(row>>3))<<4)|(((attr&15)+(usedcol>>3))&15);
      int slot=Tile(f,(base+usedtile*16)&0x7fff,palette,4);
      if(slot==-1)return false;
      if(slot==-2)continue;
      for(unsigned i=0;i<win.nr;i++) {
        if(win.bits&(1u<<i))continue;
        int l=IntMax(left,win.edges[i]),r=IntMin(left+8,win.edges[i+1]);
        if(l>=r)continue;
        if(!TileQuad(f,sub*2+1,slot,l+p->extraLeftRight,y,r-l,h,l-left,vf?7-(row&7):row&7,
          attr&0x4000,vf,z,sub?255:((attr&0x800)&&(p->mathEnabled&16)?255:127)))return false;
      }
    }
    y+=h;
   }
  }
  return true;
}
static bool Compose(PicaFrame *f) {
  SelectBands(f,3,0,0);
  for(unsigned cfg=0;cfg<16;cfg++) for(unsigned y=0;y<f->height;) {
    Ppu *p=Line(f,y);unsigned h=Band(f,y,f->height-y);
    PpuWindowSpans win;PpuGetWindowSpans(p,5,true,&win);
    for(unsigned i=0;i<win.nr;i++) {
      bool inside=(win.bits&(1u<<i))!=0;
      bool clip=p->clipMode==3 || (p->clipMode==2 && inside) || (p->clipMode==1 && !inside);
      bool prevent=p->preventMathMode==3 || (p->preventMathMode==2&&inside) || (p->preventMathMode==1&&!inside);
      unsigned flags=(p->subtractColor?1:0)|(p->halfColor?2:0)|(clip?4:0)|(prevent?8:0);
      if(p->forcedBlank || y>=224u+p->extraBottomCur || (f->sharedWindow && inside)) flags=12;
      if(flags!=cfg) continue;
      int l=win.edges[i]+p->extraLeftRight,r=win.edges[i+1]+p->extraLeftRight;
      PicaQuad q={l,y,r,y+h,l*8,4096-(int)y*16,r*8,4096-(int)(y+h)*16,1,255,255,255,255};
      if(!Emit(f,4+cfg,q)) return false;
    }
    y+=h;
  }
  return true;
}
// A circular door transition applies the same inverse W1 to every active
// plane. The masked region is black either through color clipping or a black
// backdrop (castle entry uses clipMode=0). Draw full tiles once,
// then apply the exact scanline mask at composition. Other windows keep the
// general path, including independent plane windows and non-black backdrops.
static bool SharedBlackWindow(PicaFrame *f) {
  bool visible=false;
  for(unsigned y=0;y<f->height;y++) {
    Ppu *p=Line(f,y);
    if(p->forcedBlank)continue;
    if(!p->windowExtLeft || ((p->windowsel>>20)&15)!=3 ||
       (p->clipMode!=2 && p->clipMode!=3 && (f->memory->cgram[0]&0x7fff)!=0) ||
       !p->addSubscreen || p->fixedColorR || p->fixedColorG || p->fixedColorB)
      return false;
    for(unsigned sub=0;sub<2;sub++) {
      unsigned active=p->screenEnabled[sub]&0x17;
      if((p->screenWindowed[sub]&active)!=active)return false;
      for(unsigned layer=0;layer<5;layer++)if((active&(1u<<layer)) &&
         ((p->windowsel>>(layer*4))&15)!=3)return false;
    }
    visible=true;
  }
  return visible;
}

bool PicaBuildFrame(PicaFrame *f) {
  memset(f->quads,0,sizeof(f->quads)); f->failure=NULL;
  if(f->width>512 || f->height>240 || !f->width || !f->height) { f->failure="dimensions";return false; }
  f->sharedWindow=SharedBlackWindow(f);
  // Snapshot fields before VRAM access contain common visible state. Ignore
  // access latches, unrelated backgrounds and all Mode 7 registers in Mode 1.
  for(unsigned y=f->height;y--;) {
    f->commonEnd[y]=f->layerEnd[y]=y+1;
    if(y+1<f->height && !memcmp(f->lines[y].bytes+offsetof(Ppu,extraLeftCur),
       f->lines[y+1].bytes+offsetof(Ppu,extraLeftCur),
       offsetof(Ppu,vramPointer)-offsetof(Ppu,extraLeftCur)))f->commonEnd[y]=f->commonEnd[y+1];
    f->layerEnd[y]=f->commonEnd[y];
    if(f->sharedWindow && y+1<f->height &&
       !memcmp(f->lines[y].bytes+offsetof(Ppu,extraLeftCur),f->lines[y+1].bytes+offsetof(Ppu,extraLeftCur),
               offsetof(Ppu,window1left)-offsetof(Ppu,extraLeftCur)) &&
       !memcmp(f->lines[y].bytes+offsetof(Ppu,clipMode),f->lines[y+1].bytes+offsetof(Ppu,clipMode),
               offsetof(Ppu,vramPointer)-offsetof(Ppu,clipMode)))f->layerEnd[y]=f->layerEnd[y+1];
  }
  for(unsigned y=0;y<f->height;y++) {
    Ppu *p=Line(f,y);
    if(p->forcedBlank) continue;
    if(p->mode!=1) {f->failure="mode7-or-non-mode1";return false;}
    if(p->brightness!=15) {f->failure="brightness-fade";return false;}
    if(p->mosaicEnabled && p->mosaicSize>1) {f->failure="mosaic";return false;}
    if(p->objSize>7 || p->extraLeftRight*2u+256u!=f->width) {f->failure="register-range";return false;}
  }
  return Backgrounds(f,0) && Objects(f,0) && Backgrounds(f,1) && Objects(f,1) && Compose(f);
}
uint16_t PicaComposeRgb5(uint16_t main,uint16_t sub,bool eligible,bool sub_pixel,unsigned flags) {
  uint16_t out=0;
  for(unsigned shift=0;shift<15;shift+=5) {
    unsigned m=(flags&4)?1:(((main>>shift)&31)*8+1),s=((sub>>shift)&31)*8+1;
    unsigned result=m;
    if(eligible && !(flags&8)) {
      unsigned full=(flags&1)?(m>s?m-s:0):(m+s>255?255:m+s);
      if((flags&2)&&sub_pixel) result=(flags&1)?full*128/255:(m*128+s*127)/255;
      else result=full;
    }
    out|=(result>>3)<<shift;
  }
  return out;
}
