#!/usr/bin/env python3
"""Run real vertical camera/PPU on supplied checkpoints; compare both CPU paths."""
from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--dumps',nargs='*',type=Path,default=[]);p.add_argument('--output',type=Path);a=p.parse_args()
s=(r/'app/jni/src/src/zelda_rtl.c').read_text();model=(r/'platform/3ds/tests/ppu_gpu_model_test.c').read_text()
def fn(sig):
 start=s.index(sig);i=s.index('{',start)+1;n=1
 while n:n+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]+'\n'
code=model[:model.index('int main(')]+'''
#include "src/features.h"
#include "src/variables.h"
#include "src/wide_camera.h"
uint8 g_ram[131072];
uint64_t svcGetSystemTick(void){return 0;}
static struct {Ppu *ppu;} g_zenv;
static bool IsDungeonMapMenuActive(void){return WideCamera_IsMapMenu(main_module_index,submodule_index);}
'''+fn('static uint8 GetFixedCameraEffectiveContext(')+fn('static void ConfigurePpuSideSpace(')
start=s.index('typedef struct VerticalCameraRenderState');end=s.index('static void ZeldaDrawPpuLines',start)
code+=s[start:end]+r'''
static uint32_t cpu[2][512*240];
int main(int argc,char **argv) {
 Ppu *p=ppu_init();g_zenv.ppu=p;
 enhanced_features0=kFeatures0_WidescreenVisualFixes;
 // Continuous and wrapped lower bounds, both scene types, no logic writes.
 for(int context=7;context<=9;context+=2)for(int wrap=0;wrap<2;wrap++)for(int spare=32;spare>=0;spare--) {
  main_module_index=context;submodule_index=0;quadrant_fullsize_y=0;
  uint16 bottom=wrap?8:500;BG2VOFS_copy2=bottom-spare;
  if(context==7)room_bounds_y.v[2]=bottom;else ow_scroll_vars0.yend=bottom;
  p->renderFlags=5;p->bgLayer[0].vScroll=800;p->bgLayer[1].vScroll=500;p->renderObjYOffset=0;
  uint8 saved[131072];memcpy(saved,g_ram,sizeof(saved));
  VerticalCameraRenderState state=BeginVerticalCameraRender();
  assert(state.delta==(spare<16?16-spare:0));
  assert(p->bgLayer[1].vScroll==500-state.delta && p->renderObjYOffset==state.delta);
  assert(!memcmp(saved,g_ram,sizeof(saved)));EndVerticalCameraRender(&state);
  assert(p->bgLayer[1].vScroll==500 && p->renderObjYOffset==0);
 }
 p->renderFlags=1;assert(!BeginVerticalCameraRender().delta);
 p->renderFlags=5;main_module_index=14;submodule_index=7;assert(!BeginVerticalCameraRender().delta);
 for(int arg=2;arg<argc;arg++) {
  char path[2048];snprintf(path,sizeof(path),"%s/ram.bin",argv[arg]);
  FILE *f=fopen(path,"rb");assert(f);assert(fread(g_ram,1,sizeof(g_ram),f)==sizeof(g_ram));fclose(f);
  unsigned delta=0;
  for(int old=0;old<2;old++) {
   LoadDump(p,argv[arg]);p->windowExtLeft=p->windowExtRight=NULL;
   PpuBeginDrawing(p,(uint8*)cpu[old],2048,5|(old?16:0));
   VerticalCameraRenderState state=BeginVerticalCameraRender();delta=state.delta;
   ConfigurePpuSideSpace(BG2HOFS_copy2,false,false);
   p->extraBottomCur=UintMin(16,p->extraBottomCur+delta);
   assert(p->extraBottomCur==16);
   for(int y=1;y<=240;y++)ppu_runLine(p,y);
   EndVerticalCameraRender(&state);
  }
  assert(!memcmp(cpu[0],cpu[1],sizeof(cpu[0])));
  if(strcmp(argv[1],"-")) {
   snprintf(path,sizeof(path),"%s/%d.ppm",argv[1],arg-2);FILE *f=fopen(path,"wb");assert(f);
   fprintf(f,"P6\n400 240\n255\n");
   for(int y=0;y<240;y++)for(int x=0;x<400;x++){uint32 c=cpu[0][y*512+x];uint8 rgb[3]={c>>16,c>>8,c};fwrite(rgb,1,3,f);}fclose(f);
  }
  printf("PASS camera checkpoint %s: vertical delta=%u, full240, CPU Old/New parity\n",argv[arg],delta);
 }
 ppu_free(p);puts("PASS 132 camera boundary/wrap cases, restoration, unchanged RAM, Original and map exclusions");
}
'''
with tempfile.TemporaryDirectory() as t:
 d=Path(t);(d/'test.c').write_text(code)
 subprocess.run(['cc','-O1','-D__3DS__','-fsanitize=address,undefined','-fno-sanitize=shift-base','-fno-sanitize-recover=all','-I'+str(r/'app/jni/src'),'-I'+str(r/'platform/3ds/source'),str(d/'test.c'),str(r/'app/jni/src/snes/ppu.c'),str(r/'app/jni/src/src/wide_camera.c'),str(r/'platform/3ds/source/ppu_gpu_model.c'),'-o',str(d/'test')],check=True)
 if a.output:a.output.mkdir(parents=True,exist_ok=True)
 subprocess.run([str(d/'test'),str(a.output.resolve()) if a.output else '-',*[str(p.resolve()) for p in a.dumps]],check=True)
