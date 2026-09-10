#include "ppu_gpu.h"
#include "ppu_gpu_model.h"
#include "ppu_gpu_c3d.h"
#include "platform_3ds.h"
#include "alttp_pica_shader_shbin.h"
#include <3ds.h>
#include <citro2d.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int16_t x,y,z,w,u,v; uint8_t r,g,b,a; } Vertex;
_Static_assert(sizeof(Vertex)==16,"GPU vertex layout");
typedef struct { unsigned first,count; } Range;
typedef struct {
  uint32_t number,prepare,wait,build,submit,upload,vertices,decodes;
  const char *reason;
  bool gpu;
} FrameRecord;
static struct {
  bool initialized,ready,prepared,output,forceCpu,program;
  Ppu *saved,*scratch;
  PicaAtlas *cache;
  PicaLine lines[PICA_MAX_LINES];
  C3D_Tex atlas, main, sub, result;
  C3D_Tex atlasPool[2];
  PicaAtlas *cachePool[2];
  Vertex *vertexPool[2];
  unsigned slot,lastSubmittedSlot;
  C3D_RenderTarget *mainTarget,*subTarget,*resultTarget;
  DVLB_s *shader;
  shaderProgram_s shaderProgram;
  int scaleLocation;
  C3D_AttrInfo attributes;
  C3D_BufInfo buffers;
  Vertex *vertices;
  uint32_t *readback;
  Range ranges[PICA_GROUPS];
  unsigned count,width,height,captured;
  float invWidth,invHeight;
  uint64_t frames,gpuFrames,cpuFrames,probePixels,probeErrors;
  uint64_t geometryPixels,geometryErrors;
  uint32_t prepareUs,syncUs,buildUs,submitUs,uploadBytes,firstBad,expected,actual;
  const char *reason;
  FrameRecord history[120];
  unsigned historyNext,historyCount;
  Result readbackCacheResult;
  uint32_t probeBandErrors[16];
} g;
static uint32_t Us(uint64_t start) { return (uint32_t)((svcGetSystemTick()-start)*1000000ull/SYSCLOCK_ARM11); }
static bool Clean(const void *p,size_t bytes) {
  if(!bytes) return true;
  if(R_SUCCEEDED(svcStoreProcessDataCache(CUR_PROCESS_HANDLE,(u32)(uintptr_t)p,bytes))) return true;
  return R_SUCCEEDED(GSPGPU_FlushDataCache(p,bytes));
}
static uint32_t Rgba(unsigned r,unsigned green,unsigned b,unsigned a) { return r|(green<<8)|(b<<16)|(a<<24); }
static void ResetRanges(unsigned width,unsigned height) {
  memset(g.ranges,0,sizeof(g.ranges)); g.count=0;
  g.width=width;g.height=height;g.invWidth=2.f/width;g.invHeight=2.f/height;
}
static bool Emit(void *ctx,unsigned group,const PicaQuad *q) {
  (void)ctx;
  if(g.count+6>PICA_MAX_VERTICES) return false;
  Range *range=&g.ranges[group];
  if(!range->count) range->first=g.count;
  if(range->first+range->count!=g.count) return false;
  int16_t depth=q->depth>>1;
  if(!depth) depth=1;
  Vertex a={q->x0,q->y0,depth,1,q->u0,q->v0,q->r,q->g,q->b,q->a};
  Vertex b=a,c=a,d=a;
  b.x=d.x=q->x1;b.u=d.u=q->u1;
  c.y=d.y=q->y1;c.v=d.v=q->v1;
  Vertex *v=&g.vertices[g.count];
  v[0]=a;v[1]=b;v[2]=c;v[3]=c;v[4]=b;v[5]=d;
  g.count+=6;
  range->count+=6;return true;
}
static void DrawRange(unsigned group) {
  Range r=g.ranges[group];
  // Bound individual hardware vertex counts; quads never cross a draw boundary.
  while(r.count) { unsigned n=r.count>32766?32766:r.count;
    C3D_DrawArrays(GPU_TRIANGLES,r.first,n);r.first+=n;r.count-=n;
  }
}
static void ResetTev(void) {
  for(int i=0;i<6;i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
  C3D_TexEnvBufUpdate(C3D_Both,0); C3D_TexEnvBufColor(0);
}
static void BindState(void) {
  C3D_BindProgram(&g.shaderProgram);C3D_SetAttrInfo(&g.attributes);C3D_SetBufInfo(&g.buffers);
  C3D_FVUnifSet(GPU_VERTEX_SHADER,g.scaleLocation,g.invWidth,-g.invHeight,-1.f/32768.f,1.f);
  C3D_CullFace(GPU_CULL_NONE);C3D_DepthMap(true,-1.f,0.f);
  C3D_FragOpMode(GPU_FRAGOPMODE_GL);
  C3D_EarlyDepthTest(false,GPU_EARLYDEPTH_GREATER,0);
  C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
  C3D_AlphaTest(false,GPU_ALWAYS,0);
  C3D_StencilTest(false,GPU_ALWAYS,0,0xff,0);
  C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);
}
static void Viewport(void) {
  C3D_SetViewport(0,256-g.height,g.width,g.height);
  C3D_SetScissor(GPU_SCISSOR_NORMAL,0,256-g.height,g.width,256);
}
static void RestoreC2D(void) {
  ResetTev();
  C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_ALL);
  C3D_StencilTest(false,GPU_ALWAYS,0,0xff,0);
  C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_KEEP,GPU_STENCIL_KEEP);
  C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);
  PicaC3DUnbindSecondary();
  C2D_Prepare();
  // Prepare enables depth testing. Both display targets have NO depth buffer,
  // so restore the presenter's color-only policy after Prepare, not before it.
  C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
}
static bool DrawLayers(C3D_RenderTarget *target,unsigned sub) {
  C3D_FrameSplit(GX_CMDLIST_FLUSH);
  C3D_RenderTargetClear(target,C3D_CLEAR_ALL,0,0);
  if(!C3D_FrameDrawOn(target)) return false;
  BindState();Viewport();ResetTev();
  C3D_TexBind(0,&g.atlas);
  C3D_TexEnv *e=C3D_GetTexEnv(0);
  C3D_TexEnvSrc(e,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PREVIOUS);
  C3D_TexEnvFunc(e,C3D_Both,GPU_MODULATE);
  C3D_AlphaTest(true,GPU_GREATER,0);
  C3D_DepthTest(true,GPU_GREATER,GPU_WRITE_ALL);
  DrawRange(sub*2);
  // ref=1 with compare mask=0 tests equal zero (0 == stencil&1). Write mask
  // sets bit 0 even on depth fail, preserving first-opaque-OAM ownership.
  // Actual comparator masks both ref and stored value: use NOT_EQUAL ref=1.
  C3D_StencilTest(true,GPU_NOTEQUAL,1,1,1);
  C3D_StencilOp(GPU_STENCIL_KEEP,GPU_STENCIL_REPLACE,GPU_STENCIL_REPLACE);
  DrawRange(sub*2+1);
  return true;
}
static void ConfigureCompose(unsigned flags) {
  ResetTev();
  GPU_TEVSRC main=(flags&4)?GPU_CONSTANT:GPU_TEXTURE0;
  C3D_TexEnv *e=C3D_GetTexEnv(0);
  C3D_TexEnvColor(e,Rgba(1,1,1,127));
  C3D_TexEnvSrc(e,C3D_RGB,main,GPU_TEXTURE1,GPU_PREVIOUS);
  C3D_TexEnvFunc(e,C3D_RGB,(flags&1)?GPU_SUBTRACT:GPU_ADD);
  C3D_TexEnvSrc(e,C3D_Alpha,GPU_TEXTURE1,GPU_CONSTANT,GPU_PREVIOUS);
  C3D_TexEnvFunc(e,C3D_Alpha,GPU_SUBTRACT);
  C3D_TexEnvScale(e,C3D_Alpha,GPU_TEVSCALE_2);
  // Stage 0's RGB reaches PREVIOUS_BUFFER at stage 2 (one-stage delay).
  C3D_TexEnvBufUpdate(C3D_RGB,1);
  e=C3D_GetTexEnv(1);
  C3D_TexEnvColor(e,Rgba(1,1,1,128));
  if(flags&1) {
    C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT,GPU_PREVIOUS);
    C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA,GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(e,C3D_RGB,GPU_MODULATE);
  } else {
    C3D_TexEnvSrc(e,C3D_RGB,main,GPU_TEXTURE1,GPU_CONSTANT);
    C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
    C3D_TexEnvFunc(e,C3D_RGB,GPU_INTERPOLATE);
  }
  e=C3D_GetTexEnv(2);
  if(flags&2) {
    C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_PREVIOUS_BUFFER,GPU_PREVIOUS);
    C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
    C3D_TexEnvFunc(e,C3D_RGB,GPU_INTERPOLATE);
  } else {
    C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS_BUFFER,GPU_PREVIOUS,GPU_PREVIOUS);
    C3D_TexEnvFunc(e,C3D_RGB,GPU_REPLACE);
  }
  e=C3D_GetTexEnv(3);
  // Subtraction cancels the atlas RGB bias. Restore one sub-RGB5 unit after
  // selection: physical PICA can land just below an exact 8*n boundary when
  // interpolating the full/half result. +1 preserves every SNES result under
  // integer TEV too (full=8*n, half=4*n), including clamped black.
  unsigned bias=(flags&1)?1:0;
  C3D_TexEnvColor(e,Rgba(bias,bias,bias,(flags&8)?255:127));
  C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,GPU_CONSTANT,GPU_PREVIOUS);
  C3D_TexEnvFunc(e,C3D_RGB,GPU_ADD);
  C3D_TexEnvSrc(e,C3D_Alpha,GPU_TEXTURE0,GPU_CONSTANT,GPU_PREVIOUS);
  C3D_TexEnvFunc(e,C3D_Alpha,GPU_SUBTRACT);C3D_TexEnvScale(e,C3D_Alpha,GPU_TEVSCALE_2);
  e=C3D_GetTexEnv(4);
  C3D_TexEnvColor(e,Rgba(1,1,1,255));
  C3D_TexEnvSrc(e,C3D_RGB,GPU_PREVIOUS,main,GPU_PREVIOUS);
  C3D_TexEnvOpRgb(e,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR,GPU_TEVOP_RGB_SRC_ALPHA);
  C3D_TexEnvFunc(e,C3D_RGB,GPU_INTERPOLATE);
  e=C3D_GetTexEnv(5);
  C3D_TexEnvSrc(e,C3D_Alpha,GPU_CONSTANT,GPU_PREVIOUS,GPU_PREVIOUS);
  C3D_TexEnvColor(e,0xffffffff);C3D_TexEnvFunc(e,C3D_Alpha,GPU_REPLACE);
}
static bool DrawComposition(void) {
  C3D_FrameSplit(GX_CMDLIST_FLUSH);
  C3D_RenderTargetClear(g.resultTarget,C3D_CLEAR_COLOR,0,0);
  if(!C3D_FrameDrawOn(g.resultTarget)) return false;
  BindState();Viewport();
  C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
  C3D_TexBind(0,&g.main);C3D_TexBind(1,&g.sub);
  for(unsigned i=0;i<16;i++) if(g.ranges[4+i].count) {ConfigureCompose(i);DrawRange(4+i);}
  return true;
}
static uint16_t Pack(uint16_t rgb) { return ((rgb&31)<<11)|((rgb&992)<<1)|((rgb>>9)&62)|1; }
static uint32_t Encoded(uint16_t rgb,unsigned alpha) {
  return (((rgb&31)*8+1)<<24)|((((rgb>>5)&31)*8+1)<<16)|((((rgb>>10)&31)*8+1)<<8)|alpha;
}
static bool TransferReadback(GX_TRANSFER_FORMAT output) {
  C3D_FrameSplit(GX_CMDLIST_FLUSH);
  if(!Clean(g.readback,512*256*4)) return false;
  return R_SUCCEEDED(GX_DisplayTransfer(g.result.data,GX_BUFFER_DIM(512,256),g.readback,GX_BUFFER_DIM(512,256),
    GX_TRANSFER_FLIP_VERT(0)|GX_TRANSFER_OUT_TILED(0)|GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB5A1)|GX_TRANSFER_OUT_FORMAT(output)));
}
static bool FinishReadback(size_t bytes) {
  if(!PicaC3DWaitIdle()) return false;
  g.readbackCacheResult=GSPGPU_InvalidateDataCache(g.readback,bytes);
  return R_SUCCEEDED(g.readbackCacheResult);
}
static bool ColorProbe(void) {
  if(!PicaC3DWaitIdle()) {g.reason="probe-queue-wait";return false;}
  ResetRanges(512,256);
  // Outside a frame, SyncDisplayTransfer waits for source consumption.
  // Inside a frame it only queues the copy: reusing this staging buffer there
  // would upload the second image to BOTH inputs of the color probe.
  for(unsigned which=0;which<2;which++) {
    for(unsigned i=0;i<512*256;i++) {
      unsigned n=which?((i>>5)&31):(i&31);
      uint16_t rgb=n|(((n*(which?11:7))&31)<<5)|(((n*(which?3:13))&31)<<10);
      g.readback[i]=Encoded(rgb,(i&(1u<<(which?11:10)))?255:127);
    }
    if(!Clean(g.readback,512*256*4)) {g.reason="probe-upload-cache";return false;}
    C3D_SyncDisplayTransfer(g.readback,GX_BUFFER_DIM(512,256),(which?g.sub.data:g.main.data),GX_BUFFER_DIM(512,256),
      GX_TRANSFER_OUT_TILED(1)|GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8));
  }
  if(!C3D_FrameBegin(0)) {g.reason="probe-frame-begin";return false;}
  for(unsigned flags=0;flags<16;flags++) {
    unsigned y=flags*8;
    PicaQuad q={0,y,512,y+8,0,4096-(int)y*16,4096,4096-(int)(y+8)*16,1,255,255,255,255};
    Emit(NULL,4+flags,&q);
  }
  bool ok=Clean(g.vertices,g.count*sizeof(Vertex)) && DrawComposition();
  if(ok) ok=TransferReadback(GX_TRANSFER_FMT_RGB5A1);
  RestoreC2D();C3D_FrameEnd(GX_CMDLIST_FLUSH);
  bool readbackOk=FinishReadback(512*256*2);
  if(!readbackOk) {g.reason="probe-readback-sync-cache";return false;}
  if(!ok) {g.reason="probe-draw";return false;}
  for(unsigned i=0;i<65536;i++) {
    unsigned m=i&31,s=(i>>5)&31;
    uint16_t main=m|(((m*7)&31)<<5)|(((m*13)&31)<<10);
    uint16_t sub=s|(((s*11)&31)<<5)|(((s*3)&31)<<10);
    uint16_t expected=Pack(PicaComposeRgb5(main,sub,(i>>10)&1,(i>>11)&1,(i>>12)&15));
    uint16_t actual=((uint16_t*)g.readback)[i];g.probePixels++;
    if(expected!=actual) {
      if(!g.probeErrors) {g.firstBad=i;g.expected=expected;g.actual=actual;}
      g.probeErrors++;g.probeBandErrors[i>>12]++;
    }
  }
  if(g.probeErrors) {
    FILE *probe=fopen("pica-color-probe.raw","wb");
    if(probe) {fwrite(g.readback,2,512*256,probe);fclose(probe);}
  }
  g.reason=g.probeErrors?"color-probe-mismatch":"ready";
  Platform3DS_LogRuntime("PICA200 color probe: %llu pixels, %llu mismatches, first=%lu expected=%04lx actual=%04lx",
    g.probePixels,g.probeErrors,(unsigned long)g.firstBad,(unsigned long)g.expected,(unsigned long)g.actual);
  return g.probeErrors==0;
}
// Synthetic scene only: no ROM data and no changes to the live game PPU.
// Compare actual shader/atlas/depth/stencil output with the established core.
static bool GeometryProbe(bool shared) {
  unsigned height=shared?240:224;
  int16_t left[240],right[240];
  Ppu *p=ppu_init();
  uint16_t *expected=malloc(400*height*2);
  if(!p || !expected) {ppu_free(p);free(expected);g.reason="geometry-probe-memory";return false;}
  ppu_reset(p);p->forcedBlank=false;p->mode=1;p->brightness=15;
  p->extraLeftRight=p->extraLeftCur=p->extraRightCur=72;
  p->mosaicSize=1;p->mosaicEnabled=0;p->objSize=1;
  p->objTileAdr1=0x4000;p->objTileAdr2=0x5000;
  p->screenEnabled[0]=0x17;p->screenEnabled[1]=0x13;
  p->screenWindowed[0]=p->screenWindowed[1]=0x13;
  p->windowsel=0x222222;p->window1left=72;p->window1right=181;
  p->mathEnabled=63;p->addSubscreen=true;
  p->fixedColorR=9;p->fixedColorG=21;p->fixedColorB=3;
  uint32_t random=0x327bc159;
  for(unsigned i=0;i<0x8000;i++) {
    random^=random<<13;random^=random>>17;random^=random<<5;p->vram[i]=random;
  }
  for(unsigned i=0;i<256;i++) p->cgram[i]=(i&31)|(((i*7)&31)<<5)|(((i*13)&31)<<10);
  for(unsigned i=0;i<256;i+=2) p->oam[i]=0xf000;
  for(unsigned i=0;i<48;i++) {
    p->oam[i*2]=((i*9%210)<<8)|(i*5%245);
    p->oam[i*2+1]=(i*7&255)|((i&7)<<9)|((i&3)<<12)|((i&1)?0x4000:0)|((i&2)?0x8000:0);
  }
  for(unsigned i=0;i<3;i++) {
    p->bgLayer[i]=(BgLayer){.hScroll=i*39,.vScroll=i*19,.tilemapWider=true,.tilemapHigher=true,
      .tilemapAdr=i==0?0x1000:i==1?0:0x6000,.tileAdr=i==2?0x7000:0x2000};
  }
  PpuBeginDrawing(p,(uint8_t*)g.readback,512*4,kPpuRenderFlags_NewRenderer);
  if(shared) {
    p->extraBottomCur=16;p->windowExtLeft=left;p->windowExtRight=right;
    p->windowsel=0x330333;p->screenWindowed[0]=p->screenWindowed[1]=0x17;
    p->fixedColorR=p->fixedColorG=p->fixedColorB=0;p->renderObjYOffset=16;
    for(int y=0;y<240;y++){int span=200-abs(y-120)*2;left[y]=128-span;right[y]=128+span;}
  }
  for(unsigned y=0;y<height;y++) {
    unsigned flags=y/(shared?15:14);
    p->subtractColor=flags&1;p->halfColor=flags&2;
    p->clipMode=shared?2:((flags&4)?2:0);p->preventMathMode=(flags&8)?1:0;
    p->bgLayer[0].hScroll=(y/7)*3;
    PicaCaptureLine(&g.lines[y],p,y);ppu_runLine(p,y+1);
    for(unsigned x=0;x<400;x++) {
      uint32_t c=g.readback[y*512+x];
      expected[y*400+x]=Pack(((c>>19)&31)|(((c>>11)&31)<<5)|(((c>>3)&31)<<10));
    }
  }
  ResetRanges(400,height);PicaAtlasBegin(g.cache);
  PicaFrame frame={.memory=p,.lines=g.lines,.scratch=g.scratch,.atlas=g.cache,
    .pixels=g.atlas.data,.width=400,.height=height,.emit=Emit};
  bool ok=PicaBuildFrame(&frame) && (!shared || frame.sharedWindow) && Clean(g.atlas.data,g.atlas.size) && Clean(g.vertices,g.count*sizeof(Vertex));
  if(ok && C3D_FrameBegin(0)) {
    ok=DrawLayers(g.mainTarget,0)&&DrawLayers(g.subTarget,1)&&DrawComposition();
    if(ok) ok=TransferReadback(GX_TRANSFER_FMT_RGB5A1);
    RestoreC2D();C3D_FrameEnd(GX_CMDLIST_FLUSH);
    bool readbackOk=FinishReadback(512*256*2);
    ok=ok && readbackOk;
  } else ok=false;
  if(ok) for(unsigned y=0;y<height;y++) for(unsigned x=0;x<400;x++) {
    uint16_t actual=((uint16_t*)g.readback)[y*512+x];g.geometryPixels++;
    if(actual!=expected[y*400+x]) {
      if(!g.geometryErrors) {g.firstBad=y*512+x;g.expected=expected[y*400+x];g.actual=actual;}
      g.geometryErrors++;
    }
  }
  if(ok && g.geometryErrors) {
    FILE *probe=fopen("pica-geometry-probe.raw","wb");
    if(probe) {fwrite(g.readback,2,512*256,probe);fclose(probe);}
  }
  Platform3DS_LogRuntime("PICA200 geometry probe: %llu pixels, %llu mismatches, first=%lu expected=%04lx actual=%04lx",
    g.geometryPixels,g.geometryErrors,(unsigned long)g.firstBad,(unsigned long)g.expected,(unsigned long)g.actual);
  g.reason=!ok?"geometry-probe-draw":g.geometryErrors?"geometry-probe-mismatch":"ready";
  ppu_free(p);free(expected);return ok && !g.geometryErrors;
}
static bool Target(C3D_Tex *texture,C3D_RenderTarget **target,GPU_TEXCOLOR fmt,bool depth) {
  if(!C3D_TexInitVRAM(texture,512,256,fmt)) return false;
  C3D_TexSetFilter(texture,GPU_NEAREST,GPU_NEAREST);
  C3D_TexSetWrap(texture,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
  *target=C3D_RenderTargetCreateFromTex(texture,GPU_TEXFACE_2D,0,depth?GPU_RB_DEPTH24_STENCIL8:-1);
  return *target && (!depth || ((*target)->frameBuf.depthBuf && (*target)->frameBuf.depthMask));
}
static bool SelectBuffers(unsigned slot) {
  g.slot=slot;g.atlas=g.atlasPool[slot];g.cache=g.cachePool[slot];g.vertices=g.vertexPool[slot];
  BufInfo_Init(&g.buffers);
  return BufInfo_Add(&g.buffers,g.vertices,sizeof(Vertex),3,0x210)>=0;
}
bool PpuGpuInit(void) {
  if(Platform3DS_IsNew3DS()) return false;
  if(g.initialized) return g.ready;
  g.initialized=true;g.reason="initialization";
  if(!PicaC3DCompatible()) {g.reason="citro3d-layout";return false;}
  remove("pica-color-probe.raw");remove("pica-geometry-probe.raw");
  g.saved=calloc(1,sizeof(Ppu));g.scratch=calloc(1,sizeof(Ppu));
  g.readback=linearMemAlign(512*256*4,128);
  if(!g.saved||!g.scratch||!g.readback)return false;
  for(unsigned slot=0;slot<2;slot++) {
    g.cachePool[slot]=calloc(1,sizeof(PicaAtlas));
    g.vertexPool[slot]=linearMemAlign(PICA_MAX_VERTICES*sizeof(Vertex),128);
    C3D_Tex *atlas=&g.atlasPool[slot];
    if(!g.cachePool[slot]||!g.vertexPool[slot]||!C3D_TexInit(atlas,PICA_ATLAS_W,PICA_ATLAS_H,GPU_RGBA8))return false;
    C3D_TexSetFilter(atlas,GPU_NEAREST,GPU_NEAREST);
    C3D_TexSetWrap(atlas,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);
    PicaAtlasInit(g.cachePool[slot],atlas->data);
    if(!Clean(atlas->data,atlas->size))return false;
  }
  if(!SelectBuffers(0))return false;
  if(!Target(&g.main,&g.mainTarget,GPU_RGBA8,true) || !Target(&g.sub,&g.subTarget,GPU_RGBA8,true) || !Target(&g.result,&g.resultTarget,GPU_RGBA5551,false)) return false;
  g.shader=DVLB_ParseFile((u32*)alttp_pica_shader_shbin,alttp_pica_shader_shbin_size);
  if(!g.shader||!g.shader->numDVLE || R_FAILED(shaderProgramInit(&g.shaderProgram))) return false;
  g.program=true;
  if(R_FAILED(shaderProgramSetVsh(&g.shaderProgram,&g.shader->DVLE[0]))) return false;
  g.scaleLocation=shaderInstanceGetUniformLocation(g.shaderProgram.vertexShader,"positionScale");
  if(g.scaleLocation<0) {g.reason="shader-uniform";return false;}
  AttrInfo_Init(&g.attributes);
  AttrInfo_AddLoader(&g.attributes,0,GPU_SHORT,4);AttrInfo_AddLoader(&g.attributes,1,GPU_SHORT,2);AttrInfo_AddLoader(&g.attributes,2,GPU_UNSIGNED_BYTE,4);
  g.ready=ColorProbe() && GeometryProbe(false) && GeometryProbe(true);g.output=g.prepared=false;return g.ready;
}
void PpuGpuShutdown(void) {
  if(!g.initialized) return;
  if(PicaC3DCompatible()) PicaC3DWaitIdle();
  if(g.mainTarget) C3D_RenderTargetDelete(g.mainTarget);
  if(g.subTarget) C3D_RenderTargetDelete(g.subTarget);
  if(g.resultTarget) C3D_RenderTargetDelete(g.resultTarget);
  C3D_Tex *textures[]={&g.atlasPool[0],&g.atlasPool[1],&g.main,&g.sub,&g.result};
  for(unsigned i=0;i<5;i++) if(textures[i]->data) C3D_TexDelete(textures[i]);
  if(g.program) shaderProgramFree(&g.shaderProgram);
  if(g.shader) DVLB_Free(g.shader);
  for(unsigned slot=0;slot<2;slot++){if(g.vertexPool[slot])linearFree(g.vertexPool[slot]);free(g.cachePool[slot]);}
  if(g.readback) linearFree(g.readback);
  free(g.saved);free(g.scratch);memset(&g,0,sizeof(g));
}
bool PpuGpuCanAttempt(void) {
  if(!g.ready || g.forceCpu) g.prepareUs=g.syncUs=g.buildUs=g.submitUs=g.uploadBytes=0;
  return g.ready && !g.forceCpu;
}
void PpuGpuForceCpuFrame(void) {g.forceCpu=true;}
bool PpuGpuBegin(Ppu *p,unsigned height) {
  g.prepared=g.output=false;g.frames++;
  if(!g.ready || g.forceCpu || height>PICA_MAX_LINES) return false;
  // The presenter retires the preceding queue at FrameBegin before submitting
  // a new one. Prepare in the OTHER slot while its GPU frame is in flight.
  // Choose from the last submitted slot, not attempted frames/fallbacks.
  if(!SelectBuffers(g.lastSubmittedSlot^1u)){g.reason="vertex-buffer";return false;}
  g.syncUs=0;
  memcpy(g.saved,p,sizeof(Ppu));g.captured=0;ResetRanges(256+p->extraLeftRight*2,height);
  PicaAtlasBegin(g.cache);p->gpuRecording=true;p->gpuInvalidWrite=false;
  return true;
}
void PpuGpuLine(Ppu *p,unsigned y) {if(y<g.height){PicaCaptureLine(&g.lines[y],p,y);g.captured++;}}
bool PpuGpuFinish(Ppu *p) {
  p->gpuRecording=false;uint64_t start=svcGetSystemTick();
  PicaFrame frame={.memory=g.saved,.lines=g.lines,.scratch=g.scratch,.atlas=g.cache,
    .pixels=g.atlas.data,.width=g.width,.height=g.height,.emit=Emit};
  bool ok=!p->gpuInvalidWrite && g.captured==g.height;
  if(ok) ok=PicaBuildFrame(&frame);
  g.buildUs=Us(start);g.prepareUs=g.buildUs+g.syncUs;
  if(!ok) {
    g.reason=p->gpuInvalidWrite?"live-vram-cgram-oam":frame.failure?frame.failure:"line-count";
    memcpy(p,g.saved,sizeof(Ppu));p->gpuRecording=false;
    return false;
  }
  start=svcGetSystemTick();g.uploadBytes=0;
  // Dirty adjacent slots become one cache-clean range. Atlas memory is kept
  // in the inactive slot while the GPU consumes the other frame, including failed preflights.
  for(unsigned i=1;i<PICA_SLOTS;) {
    if(!(g.cache->dirty[i/32]&(1u<<(i&31)))) {i++;continue;}
    unsigned first=i++;
    while(i<PICA_SLOTS && (g.cache->dirty[i/32]&(1u<<(i&31)))) i++;
    unsigned bytes=(i-first)*64*4;g.uploadBytes+=bytes;
    if(!Clean((uint8_t*)g.atlas.data+first*64*4,bytes)) ok=false;
    else for(unsigned j=first;j<i;j++) g.cache->dirty[j/32]&=~(1u<<(j&31));
  }
  if(!Clean(g.vertices,g.count*sizeof(Vertex))) ok=false;
  g.prepareUs+=Us(start);
  if(!ok) {g.reason="cache-clean";memcpy(p,g.saved,sizeof(Ppu));return false;}
  g.prepared=g.output=true;g.reason=frame.sharedWindow?"PICA200-shared-window":"PICA200";return true;
}
static void RecordFrame(bool gpu) {
  FrameRecord *r=&g.history[g.historyNext];
  *r=(FrameRecord){.number=g.gpuFrames+g.cpuFrames,.prepare=g.prepareUs,.wait=g.syncUs,
    .build=g.buildUs,.submit=gpu?g.submitUs:0,.upload=g.uploadBytes,.vertices=g.count,
    .decodes=g.cache?g.cache->decodes:0,.reason=g.reason,.gpu=gpu};
  g.historyNext=(g.historyNext+1)%120;if(g.historyCount<120)g.historyCount++;
}
void PpuGpuCpuFrame(void) {g.prepared=g.output=false;if(g.forceCpu)g.reason="thumbnail-or-cpu-overlay";g.forceCpu=false;g.cpuFrames++;RecordFrame(false);}
bool PpuGpuPrepared(void) {return g.prepared;}
bool PpuGpuOutputActive(void) {return g.output;}
void *PpuGpuOutput(void) {return g.output?&g.result:NULL;}
bool PpuGpuDraw(void) {
  if(!g.prepared) return g.output;
  uint64_t start=svcGetSystemTick();
  bool ok=DrawLayers(g.mainTarget,0)&&DrawLayers(g.subTarget,1)&&DrawComposition();
  RestoreC2D();g.submitUs=Us(start);g.prepared=false;
  if(ok) {
    g.lastSubmittedSlot=g.slot;g.gpuFrames++;RecordFrame(true);
    if(g.gpuFrames==1 || g.gpuFrames==120)
      Platform3DS_LogRuntime("PICA200 gameplay submitted=%llu cpu=%llu size=%ux%u vertices=%u prepare=%lu us build=%lu us",
        g.gpuFrames,g.cpuFrames,g.width,g.height,g.count,(unsigned long)g.prepareUs,(unsigned long)g.buildUs);
  }
  else {g.ready=g.output=false;g.reason="GPU-submit-failed";}
  return ok;
}
void PpuGpuWriteDiagnostics(FILE *f) {
  fprintf(f,"PICA200 schema=3 initialized=%u enabled=%u output=%u reason=%s\n",g.initialized,g.ready,g.output,g.reason?g.reason:"not-initialized");
  fprintf(f,"PICA frames_attempted=%llu submitted=%llu cpu_frames=%llu width=%u height=%u vertices=%u\n",g.frames,g.gpuFrames,g.cpuFrames,g.width,g.height,g.count);
  fprintf(f,"PICA prepare_us=%lu prior_gpu_wait_us=%lu geometry_build_us=%lu submit_us=%lu tile_upload_bytes=%lu\n",(unsigned long)g.prepareUs,(unsigned long)g.syncUs,(unsigned long)g.buildUs,(unsigned long)g.submitUs,(unsigned long)g.uploadBytes);
  if(g.cache) fprintf(f,"PICA tile_hits=%lu tile_decodes=%lu live_slots=%lu\n",(unsigned long)g.cache->hits,(unsigned long)g.cache->decodes,(unsigned long)g.cache->live);
  fprintf(f,"PICA color_probe_pixels=%llu mismatches=%llu first=%lu expected=%04lx actual=%04lx\n",g.probePixels,g.probeErrors,(unsigned long)g.firstBad,(unsigned long)g.expected,(unsigned long)g.actual);
  fprintf(f,"PICA geometry_probe_pixels=%llu mismatches=%llu first=%lu expected=%04lx actual=%04lx\n",g.geometryPixels,g.geometryErrors,(unsigned long)g.firstBad,(unsigned long)g.expected,(unsigned long)g.actual);
  fprintf(f,"PICA queue_fence=GX-completion readback_cache_result=%08lx color_band_errors=",(unsigned long)g.readbackCacheResult);
  for(unsigned i=0;i<16;i++) fprintf(f,"%s%lu",i?",":"",(unsigned long)g.probeBandErrors[i]);
  fputc('\n',f);
  fprintf(f,"PICA resource_slots=2 current_slot=%u last_submitted_slot=%u dump_readback=CPU-uncached-VRAM timeout_ms=1000\n",g.slot,g.lastSubmittedSlot);
  fputs("PICA recent-frame history: GPU preparation/submit wall spans; CPU rows describe GPU preflight only, not CPU PPU time.\n",f);
  fputs("number,gpu,prepare_us,prior_gpu_wait_us,build_us,submit_us,upload_bytes,vertices,decodes,reason\n",f);
  for(unsigned i=0;i<g.historyCount;i++) {
    const FrameRecord *r=&g.history[(g.historyNext+120-g.historyCount+i)%120];
    fprintf(f,"%lu,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%s\n",(unsigned long)r->number,r->gpu,
      (unsigned long)r->prepare,(unsigned long)r->wait,(unsigned long)r->build,(unsigned long)r->submit,
      (unsigned long)r->upload,(unsigned long)r->vertices,(unsigned long)r->decodes,r->reason?r->reason:"unavailable");
  }
  fputs("GPU geometry/composition is active only when output=1. CPU fallback is per frame; a GPU fault/probe failure disables this session explicitly. Color probe is on-device; host tests alone are not a hardware verdict.\n",f);
}
const uint32_t *PpuGpuReadback(void) {
  if(!g.output || !g.readback) return NULL;
  // VRAM is CPU-visible and uncached. Do not submit a transfer-only GPU frame
  // while capturing: both physical E13 failures stopped exactly at that step.
  if(!PicaC3DWaitIdleFor(1000000000ll)) return NULL;
  const volatile uint16_t *src=g.result.data;
  for(unsigned y=0;y<256;y++) for(unsigned x=0;x<512;x++) {
    unsigned xx=x&7,yy=y&7;
    unsigned m=(xx&1)|((yy&1)<<1)|((xx&2)<<1)|((yy&2)<<2)|((xx&4)<<2)|((yy&4)<<3);
    uint16_t v=src[((y/8)*64+x/8)*64+m];
    unsigned red=(v>>11)&31,green=(v>>6)&31,blue=(v>>1)&31;
    g.readback[y*512+x]=(((red<<3)|(red>>2))<<16)|(((green<<3)|(green>>2))<<8)|(blue<<3)|(blue>>2);
  }
  return g.readback;
}
