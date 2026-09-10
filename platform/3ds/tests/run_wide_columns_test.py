#!/usr/bin/env python3
"""Exercise render-only column repair, both CPU paths and GPU geometry on dumps."""
from pathlib import Path
import argparse, subprocess, tempfile, struct
r=Path(__file__).resolve().parents[3]
a=argparse.ArgumentParser();a.add_argument('--assets',type=Path);a.add_argument('--dumps',type=Path,nargs='*',default=[]);a.add_argument('--output',type=Path);args=a.parse_args()
s=(r/'app/jni/src/src/zelda_rtl.c').read_text();model=(r/'platform/3ds/tests/ppu_gpu_model_test.c').read_text()
def fn(sig):
 i=s.index(sig);j=s.index('{',i)+1;n=1
 while n:n+=(s[j]=='{')-(s[j]=='}');j+=1
 return s[i:j]+'\n'
code=model[:model.index('int main(')]+r'''
#include "src/features.h"
#include "src/variables.h"
#include "src/wide_camera.h"
uint8 g_ram[131072];
uint64_t svcGetSystemTick(void){return 0;}
static struct {Ppu *ppu;} g_zenv;
static int g_widescreen_edge_mode=1;
static uint16 map_words[16384];
static unsigned map_bytes=sizeof(map_words);
#define kMap16ToMap8 map_words
#define kMap16ToMap8_SIZE map_bytes
'''
for sig in ['static uint8 GetFixedCameraEffectiveContext(', 'static void GetFixedCameraBounds(', 'static int GetFixedCameraTransitionDirection(']:code+=fn(sig)
start=s.index('// The SNES streamer may recycle');end=s.index('// The original camera\'s lower limit',start);code+=s[start:end]
code+=r'''
static uint16 original[32768];
static uint8 saved_ram[131072];
static uint32 cpu[3][512*240];
static void Draw(Ppu *p,uint32 *pixels,unsigned flags) {
 int visual=WideCamera_ClampToBounds(BG2HOFS_copy2,ow_scroll_vars0.xstart,ow_scroll_vars0.xend,p->extraLeftRight);
 int dx=BG2HOFS_copy2-visual;
 int dy=IntMin(IntMax(16-(int16)(ow_scroll_vars0.yend-BG2VOFS_copy2),0),16);
 PpuBeginDrawing(p,(uint8*)pixels,2048,flags);
 p->bgLayer[0].hScroll=(p->bgLayer[0].hScroll-dx)&1023;
 p->bgLayer[1].hScroll=(p->bgLayer[1].hScroll-dx)&1023;
 p->bgLayer[0].vScroll=(p->bgLayer[0].vScroll-dy)&1023;
 p->bgLayer[1].vScroll=(p->bgLayer[1].vScroll-dy)&1023;
 p->renderObjXOffset=dx;p->renderObjYOffset=dy;
 p->extraLeftCur=p->extraRightCur=p->extraLeftRight;p->extraBottomCur=16;
 for(int y=0;y<240;y++)ppu_runLine(p,y+1);
}
int main(int argc,char **argv) {
 Ppu *p=ppu_init();g_zenv.ppu=p;
 enhanced_features0=kFeatures0_WidescreenVisualFixes;
 for(unsigned i=0;i<sizeof(map_words)/2;i++)map_words[i]=i;
 // Scan both camera ends and reversal with current map edits on every iteration.
 unsigned cases=0;
 for(int margin=72;margin<=96;margin+=24)for(int direction=-1;direction<=1;direction+=2)
 for(int step=0;step<=768;step++) {
  int logical=direction>0?step:768-step;
  memset(g_ram,0,sizeof(g_ram));main_module_index=9;enhanced_features0=kFeatures0_WidescreenVisualFixes;
  ow_scroll_vars0.xend=768;ow_scroll_vars0.yend=800;BG2HOFS_copy2=logical;BG2VOFS_copy2=300;
  p->mode=1;p->extraLeftRight=margin;p->bgLayer[1].tilemapAdr=0;
  p->bgLayer[1].tilemapWider=p->bgLayer[1].tilemapHigher=true;
  p->bgLayer[1].hScroll=logical;p->bgLayer[1].vScroll=300;
  for(int i=0;i<4096;i++)dung_bg2[i]=(i+step)%4096;
  for(int i=0;i<32768;i++)p->vram[i]=0xffff;
  memcpy(original,p->vram,sizeof(original));memcpy(saved_ram,g_ram,sizeof(saved_ram));
  BeginWideOverworldColumns(5);
  int visual=WideCamera_ClampToBounds(logical,0,768,margin);
  int safe=((visual==768-margin?768:logical)&~15)-128;
  int safe_end=((visual==margin?0:logical)&~15)+384;
  unsigned changed=0;
  for(int x=(visual-margin)&~7;x<visual+256+margin;x+=8)for(int y=296;y<541;y+=8) {
   unsigned addr=((y&255)>>3)*32+((x&255)>>3)+(x&256?0x400:0)+(y&256?0x800:0);
   if(x<safe || x+8>safe_end) {
    unsigned tile=dung_bg2[(y>>4)*64+(x>>4)];unsigned part=((y&8)>>2)|((x&8)>>3);
    assert(p->vram[addr]==map_words[tile*4+part]);changed++;
   } else assert(p->vram[addr]==original[addr]);
  }
  assert(g_wide_column_repair.count==changed && changed<=512);
  assert(!memcmp(saved_ram,g_ram,sizeof(saved_ram)));EndWideOverworldColumns();
  assert(!memcmp(original,p->vram,sizeof(original)));cases++;
 }
 // Guard unsupported contexts, Original, non-fixed camera and different BG scroll.
 BG2HOFS_copy2=768;p->bgLayer[1].hScroll=768;p->extraLeftRight=72;
 for(int guard=0;guard<7;guard++) {
  main_module_index=9;submodule_index=0;player_is_indoors=0;g_widescreen_edge_mode=1;
  p->extraLeftRight=72;p->mode=1;p->bgLayer[1].hScroll=768;
  if(guard==0)p->extraLeftRight=0;
  if(guard==1)g_widescreen_edge_mode=0;
  if(guard==2)player_is_indoors=1;
  if(guard==3){main_module_index=14;submodule_index=7;}
  if(guard==4)submodule_index=1;
  if(guard==5)p->mode=7;
  if(guard==6)p->bgLayer[1].hScroll=5;
  BeginWideOverworldColumns(5);assert(!g_wide_column_repair.count);EndWideOverworldColumns();
 }
 printf("PASS %u forward/reverse camera positions, both edges, 72/96 margins, current tile edits, full VRAM/RAM restoration and 7 exclusion guards\n",cases);
 if(argc>2 && strcmp(argv[1],"-")) {
  FILE *f=fopen(argv[1],"rb");assert(f);map_bytes=fread(map_words,1,sizeof(map_words),f);fclose(f);
 }
 g_widescreen_edge_mode=1;
 Ppu *scratch=calloc(1,sizeof(Ppu));PicaAtlas *cache=calloc(1,sizeof(PicaAtlas));
 PicaLine lines[240];atlas=malloc(1024*512*4);PicaAtlasInit(cache,atlas);
 for(int arg=3;arg<argc;arg++) {
  char path[2048];snprintf(path,sizeof(path),"%s/ram.bin",argv[arg]);FILE *f=fopen(path,"rb");assert(f);assert(fread(g_ram,1,sizeof(g_ram),f)==sizeof(g_ram));fclose(f);
  memcpy(saved_ram,g_ram,sizeof(saved_ram));unsigned repaired=0;
  for(int mode=0;mode<3;mode++) {
   LoadDump(p,argv[arg]);p->windowExtLeft=p->windowExtRight=NULL;
   memcpy(original,p->vram,sizeof(original));
   if(mode)BeginWideOverworldColumns(5|(mode==1?16:0));
   Draw(p,cpu[mode],5|(mode==1?16:0));
   if(mode==2) {
    // Render corrected geometry through the existing GPU model and compare.
    width=400;memset(surface,0,sizeof(surface));memset(depth,0,sizeof(depth));memset(stencil,0,sizeof(stencil));memset(output,0,sizeof(output));
    for(int y=0;y<240;y++)PicaCaptureLine(lines+y,p,y);
    PicaAtlasBegin(cache);PicaFrame frame={.memory=p,.lines=lines,.scratch=scratch,.atlas=cache,.pixels=atlas,.width=400,.height=240,.emit=Raster};
    assert(PicaBuildFrame(&frame));
    for(int y=0;y<240;y++)for(int x=0;x<400;x++)assert(output[y*512+x]==RefRgb(cpu[mode][y*512+x]));
   }
   if(mode){repaired=g_wide_column_repair.count;EndWideOverworldColumns();}
   assert(!memcmp(original,p->vram,sizeof(original)));assert(!memcmp(saved_ram,g_ram,sizeof(saved_ram)));
  }
  assert(!memcmp(cpu[1],cpu[2],sizeof(cpu[1])));
  unsigned different=0;
  for(int y=0;y<240;y++)for(int x=0;x<400;x++)if(cpu[0][y*512+x]!=cpu[1][y*512+x]){assert(x<16);different++;}
  if(strstr(argv[arg],"000-dump") || strstr(argv[arg],"002-dump"))assert(repaired==0 && different==0);
  else assert(repaired>0 && different>0);
  if(strcmp(argv[2],"-")) {
   snprintf(path,sizeof(path),"%s/%d.ppm",argv[2],arg-3);f=fopen(path,"wb");assert(f);fprintf(f,"P6\n400 240\n255\n");
   for(int y=0;y<240;y++)for(int x=0;x<400;x++){uint32 c=cpu[1][y*512+x];uint8 rgb[]={c>>16,c>>8,c};fwrite(rgb,1,3,f);}fclose(f);
  }
  printf("PASS %s: %u repaired words, %u changed pixels confined to first16 columns, CPU Old/New/GPU parity and restored state\n",argv[arg],repaired,different);
 }
 ppu_free(p);free(scratch);free(cache);free(atlas);
}
'''
with tempfile.TemporaryDirectory(prefix='alttp-wide-columns-') as t:
 d=Path(t);(d/'test.c').write_text(code);table='-'
 if args.assets:
  data=args.assets.read_bytes();count,names=struct.unpack_from('<II',data,80);offset=88+4*count+names
  for i in range(count):
   size=struct.unpack_from('<I',data,88+4*i)[0];offset=(offset+3)&~3
   if i==70:table=d/'map.bin';table.write_bytes(data[offset:offset+size]);break
   offset+=size
 if args.dumps and table=='-':raise SystemExit('--assets is required for actual dump world-map definitions')
 subprocess.run(['cc','-O1','-D__3DS__','-fsanitize=address,undefined','-fno-sanitize=shift-base','-fno-sanitize-recover=all','-I'+str(r/'app/jni/src'),'-I'+str(r/'platform/3ds/source'),str(d/'test.c'),str(r/'app/jni/src/snes/ppu.c'),str(r/'app/jni/src/src/wide_camera.c'),str(r/'platform/3ds/source/ppu_gpu_model.c'),'-o',str(d/'test')],check=True)
 if args.output:args.output.mkdir(parents=True,exist_ok=True)
 subprocess.run([str(d/'test'),str(table),str(args.output.resolve()) if args.output else '-',*[str(x.resolve()) for x in args.dumps]],check=True)
