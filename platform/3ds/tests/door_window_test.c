// Replay the captured castle graphics through a sequence of synthetic iris
// radii. Uses the real core as an independent pixel oracle, not console FPS.
#define main existing_model_test_main
#include "ppu_gpu_model_test.c"
#undef main
#include <math.h>
int main(int argc,char **argv) {
  assert(argc>1);Ppu *p=ppu_init(),*scratch=calloc(1,sizeof(Ppu));
  PicaAtlas *cache=calloc(1,sizeof(PicaAtlas));PicaLine lines[240];
  atlas=malloc(1024*512*4);PicaAtlasInit(cache,atlas);
  uint32_t cpu[512*240];int16_t left[256],right[256];
  unsigned cases=0,shared=0,maxvertices=0;unsigned long long total=0;
  for(int dump=1;dump<argc;dump++)for(int guard=0;guard<6;guard++)for(int radius=0;radius<=256;radius+=16) {
    LoadDump(p,argv[dump]);p->forcedBlank=false;p->brightness=15;
    p->windowsel=0x330333;p->screenWindowed[0]=p->screenWindowed[1]=0x17;
    p->clipMode=guard==1?2:0;p->addSubscreen=true;
    p->fixedColorR=p->fixedColorG=p->fixedColorB=0;
    p->cgram[0]=guard==1||guard==2?0x2669:0;
    if(guard==3)p->screenWindowed[0]&=~2;
    if(guard==4)p->fixedColorR=5;
    if(guard==5)p->windowsel^=0x100000;
    p->windowExtLeft=left;p->windowExtRight=right;
    for(int y=0;y<256;y++) {
      int dy=y-120;int span=radius*radius-dy*dy;
      int half=span>=0?(int)sqrt((double)span):-1;
      left[y]=half>=0?128-half:0;right[y]=half>=0?128+half:-1;
    }
    width=256+p->extraLeftRight*2;
    memset(surface,0,sizeof(surface));memset(depth,0,sizeof(depth));memset(stencil,0,sizeof(stencil));
    memset(output,0,sizeof(output));memset(cpu,0,sizeof(cpu));p->colorMapDirty=true;
    PpuBeginDrawing(p,(uint8_t*)cpu,2048,1);
    for(unsigned y=0;y<240;y++){PicaCaptureLine(lines+y,p,y);ppu_runLine(p,y+1);}
    PicaAtlasBegin(cache);
    PicaFrame f={.memory=p,.lines=lines,.scratch=scratch,.atlas=cache,.pixels=atlas,.width=width,.height=240,.emit=Raster};
    assert(PicaBuildFrame(&f));unsigned vertices=0;
    for(unsigned g=0;g<PICA_GROUPS;g++)vertices+=f.quads[g]*6;
    if(guard==0){total+=vertices;if(vertices>maxvertices)maxvertices=vertices;}
    shared+=f.sharedWindow;
#ifdef EXPECT_E21
    assert(f.sharedWindow==(guard<2));
#endif
    for(unsigned y=0;y<240;y++)for(unsigned x=0;x<width;x++)if(output[y*512+x]!=RefRgb(cpu[y*512+x])) {
      fprintf(stderr,"FAIL dump=%d guard=%d radius=%d x=%u y=%u GPU=%04x CPU=%04x\n",dump,guard,radius,x,y,output[y*512+x],RefRgb(cpu[y*512+x]));return 1;
    }
    cases++;
  }
  printf("PASS %u castle-window pixel cases; shared=%u; black-backdrop total_vertices=%llu max_vertices=%u\n",cases,shared,total,maxvertices);
  free(atlas);free(cache);free(scratch);ppu_free(p);
}
