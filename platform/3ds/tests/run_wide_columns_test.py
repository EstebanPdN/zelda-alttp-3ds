#!/usr/bin/env python3
"""Exercise the render-only WIDE tile repair: overworld and dungeon maps,
the place being left, exclusions, both CPU paths and GPU geometry on dumps."""
from pathlib import Path
import argparse, subprocess, tempfile, struct
r=Path(__file__).resolve().parents[3]
a=argparse.ArgumentParser();a.add_argument('--assets',type=Path);a.add_argument('--dumps',type=Path,nargs='*',default=[]);a.add_argument('--output',type=Path);args=a.parse_args()
s=(r/'engine/src/zelda_rtl.c').read_text();model=(r/'platform/3ds/tests/ppu_gpu_model_test.c').read_text()
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
static const uint16 *GetMap16toMap8Table(void){return map_words;}
// The screens beside an area, as the map data would decode them.
static unsigned fake_decodes;
static uint16 FakeScreenTile(int screen,int i){return (uint16)((screen*97+i*3)%4096);}
static bool Overworld_DecodeScreenMap16(int screen,uint16 *dst){fake_decodes++;for(int i=0;i<1024;i++)dst[i]=FakeScreenTile(screen,i);return true;}
static bool Dungeon_DecodeRoomMaps(uint16 room,uint16 *maps){return false;}
static bool IsDungeonMapMenuActive(void){return saved_module_for_menu==7&&WideCamera_DungeonMapPicture(main_module_index,submodule_index,overworld_map_state);}
'''
start=s.index('static uint8 GetFixedCameraEffectiveContext(void) {');end=s.index('static void ZeldaDrawPpuLines(',start);code+=s[start:end]
code=code.replace('static bool WideFindRoomVoid(int bx0, int by0, int bx1, int by1, WideVoidBlock *out) {','static unsigned void_searches;\nstatic bool WideFindRoomVoid(int bx0, int by0, int bx1, int by1, WideVoidBlock *out) {\n  void_searches++;')
code+=r'''
static uint16 original[32768];
static uint8 saved_ram[131072];
static uint32 cpu[3][512*240];
static unsigned Address(const BgLayer *bg,int x,int y) {
 unsigned wx=x&511,wy=y&511;
 return (bg->tilemapAdr+(wy>>3&31)*32+(wx>>3&31)+(wx>=256?0x400:0)+(wy>=256?0x800:0))&0x7fff;
}
static unsigned OverworldTruth(const uint16 *map16,int x,int y) {
 unsigned tile=map16[(y>>4)*64+(x>>4)];
 return map_words[tile*4+(((y&8)>>2)|((x&8)>>3))];
}
static void Layers(Ppu *p,int x,int y) {
 p->mode=1;p->screenEnabled[0]=3;p->screenEnabled[1]=0;p->forcedBlank=false;p->brightness=15;
 for(int i=0;i<2;i++){p->bgLayer[i].tilemapWider=p->bgLayer[i].tilemapHigher=true;p->bgLayer[i].hScroll=x&1023;p->bgLayer[i].vScroll=y&1023;}
 p->bgLayer[0].tilemapAdr=0x2000;p->bgLayer[1].tilemapAdr=0;
}
// Every drawn cell outside the SNES picture shows 'expect'; the SNES picture
// keeps VRAM. Returns the number of cells expected to change.
static unsigned Check(Ppu *p,const WideFrame *f,int layer,unsigned (*expect)(int,int)) {
 int x=(int16)BG2HOFS_copy2,y=(int16)BG2VOFS_copy2,visual=x-f->view.offset_x;
 unsigned changed=0;
 for(int ty=(y+1-f->view.delta_y)&~7;ty<y+1-f->view.delta_y+240;ty+=8)
  for(int tx=(visual-f->view.left)&~7;tx<visual+256+f->view.right;tx+=8) {
   unsigned adr=Address(&p->bgLayer[layer],tx,ty);
   if(tx+8>x && tx<x+256 && ty+8>y+1 && ty<=y+224){assert(p->vram[adr]==original[adr]);continue;}
   unsigned want=expect(tx,ty);
   if(want==0x10000){continue;}
   if(p->vram[adr]!=want)fprintf(stderr,"TILE context=%d camera=%d/%d layer=%d tile=%d/%d got=%x expected=%x\n",f->context,x,y,layer,tx,ty,p->vram[adr],want);
   assert(p->vram[adr]==want);changed+=original[adr]!=want;
  }
 return changed;
}
static const uint16 *g_expect_map;
static uint16 prev_map16[4096];
static unsigned ExpectOverworld(int x,int y){return x<0||x>=1024||y<0||y>=1024?0x10000:OverworldTruth(g_expect_map,x,y);}
static unsigned ExpectTwoAreas(int x,int y) {
 if(y<0||y>=512)return 0x10000;
 if(x>=512 && x<1024)return OverworldTruth(dung_bg2,x-512,y);
 if(x>=0 && x<512)return OverworldTruth(prev_map16,x,y);
 return 0x10000;
}
// The room entered at (0x200,0xa00), its maps live; the one left at
// (0x400,0xa00), its maps kept (BG2 0x5000+cell, BG1 0x6000+cell).
static unsigned ExpectRoom(int layer,int x,int y) {
 if(y<0xa00||y>=0xc00)return 0x10000;
 unsigned cell=((y-0xa00)>>3)*64;
 if(x>=0x200 && x<0x400)return (layer?dung_bg2:dung_bg1)[cell+((x-0x200)>>3)];
 if(x>=0x400 && x<0x600)return (layer?0x5000:0x6000)+cell+((x-0x400)>>3);
 return 0x10000;
}
// Going north from a small area at (512,1024) into a big one at (0,0).
static unsigned beside_cells;
static unsigned ExpectBeside(int x,int y) {
 if(x<0||x>=4096||y<0||y>=4096)return 0x10000;
 if(x<1024 && y<1024)return OverworldTruth(dung_bg2,x,y);
 if(x>=512 && x<1024 && y>=1024 && y<1536)return OverworldTruth(prev_map16,x-512,y-1024);
 beside_cells++;
 unsigned tile=FakeScreenTile((y>>9)*8+(x>>9),((y>>4)&31)*32+((x>>4)&31));
 return map_words[tile*4+(((y&8)>>2)|((x&8)>>3))];
}
static unsigned ExpectRoomsBg1(int x,int y){return ExpectRoom(0,x,y);}
static unsigned ExpectRoomsBg2(int x,int y){return ExpectRoom(1,x,y);}
static void Overworld(Ppu *p,int x,int y) {
 memset(g_ram,0,sizeof(g_ram));main_module_index=9;submodule_index=0;player_is_indoors=0;
 enhanced_features0=kFeatures0_WidescreenVisualFixes;
 BYTE(overworld_area_is_big)=1;overworld_offset_base_x=0;overworld_offset_base_y=0;
 ow_scroll_vars0.xstart=0;ow_scroll_vars0.xend=768;ow_scroll_vars0.ystart=0;ow_scroll_vars0.yend=798;
 BG2HOFS_copy2=BG1HOFS_copy2=x;BG2VOFS_copy2=BG1VOFS_copy2=y;
 Layers(p,x,y);
}
int main(int argc,char **argv) {
 Ppu *p=ppu_init();g_zenv.ppu=p;
 // Closed/pit edges never reveal a neighboring quadrant; four floor cells
 // on both sides are required. The decision is independent of a room ID.
 {
  uint16 seam[4096];for(int i=0;i<4096;i++)seam[i]=1;
  attributes_for_tile[0]=0;attributes_for_tile[1]=1;attributes_for_tile[2]=0x20;
  assert(!WideOpenSeam(seam,31,seam,32,0,512,NULL));
  for(int y=12;y<15;y++)seam[y*64+31]=seam[y*64+32]=0;
  assert(!WideOpenSeam(seam,31,seam,32,0,512,NULL));
  seam[15*64+31]=seam[15*64+32]=0;
  assert(WideOpenSeam(seam,31,seam,32,0,512,NULL));
  seam[14*64+32]=2;assert(!WideOpenSeam(seam,31,seam,32,0,512,NULL));
  memset(g_ram,0,sizeof(g_ram));
 }

 // A map fade retains the same room: do not rescan its surround per frame.
 {
  memset(g_ram,0,sizeof(g_ram));main_module_index=7;player_is_indoors=1;
  Layers(p,0,0);p->extraLeftRight=72;room_bounds_y.v[2]=16;
  ResetWideTracking();ZeldaWideAfterMapLoad(7);void_searches=0;
  WideFrame f={.context=7,.fixed_camera=true,.rows=true,.settled=true};
  for(int tick=0;tick<40;tick++) {
   main_module_index=14;saved_module_for_menu=7;submodule_index=3;overworld_map_state=0;
   BeginWideRoomVoid(&f);assert(g_wide_void.active);EndWideTileRepair();
  }
  assert(void_searches==1);
  ZeldaWideAfterMapLoad(7);BeginWideRoomVoid(&f);EndWideTileRepair();assert(void_searches==2);
  puts("PASS 40 gameplay-map fade frames use one surround scan; room reload invalidates it");
  memset(g_ram,0,sizeof(g_ram));ResetWideTracking();
 }
 for(unsigned i=0;i<sizeof(map_words)/2;i++)map_words[i]=i;
 // Settled outdoors: scan both camera ends and reversal, 72 and 96 margins,
 // with current map edits on every step.
 unsigned cases=0,total=0;
 for(int margin=72;margin<=96;margin+=24)for(int direction=-1;direction<=1;direction+=2)
 for(int step=0;step<=768;step+=3) {
  int logical=direction>0?step:768-step;
  Overworld(p,logical,300);p->extraLeftRight=margin;
  for(int i=0;i<4096;i++)dung_bg2[i]=(i+step)%4096;
  ResetWideTracking();ZeldaWideAfterMapLoad(9);
  for(int i=0;i<32768;i++)p->vram[i]=0xffff;
  memcpy(original,p->vram,sizeof(original));memcpy(saved_ram,g_ram,sizeof(saved_ram));
  WideFrame f=BeginWideFrame(5);assert(f.context==9 && f.fixed_camera && f.settled);
  assert(f.view.offset_x==logical-WideCamera_ClampToBounds(logical,0,768,margin));
  BeginWideTileRepair(&f);g_expect_map=dung_bg2;
  unsigned changed=Check(p,&f,1,ExpectOverworld);
  assert(g_wide_tile_repair.count==changed && changed<=countof(g_wide_tile_repair.address));
  assert(!memcmp(saved_ram,g_ram,sizeof(saved_ram)));EndWideTileRepair();
  assert(!memcmp(original,p->vram,sizeof(original)));cases++;total+=changed;
 }
 // Across a transition: the area being entered from its map16, the one being
 // left from the maps kept at the load.
 {
  Overworld(p,256,0);BYTE(overworld_area_is_big)=0;ow_scroll_vars0.xend=256;ow_scroll_vars0.yend=286;
  ResetWideTracking();ZeldaWideAfterMapLoad(9);
  for(int i=0;i<4096;i++)dung_bg2[i]=(i*7)%4096;
  ZeldaWideBeforeMapLoad(9);memcpy(prev_map16,dung_bg2,sizeof(prev_map16));
  for(int i=0;i<4096;i++)dung_bg2[i]=(i*13+5)%4096;
  overworld_offset_base_x=512>>3;ZeldaWideAfterMapLoad(9);
  submodule_index=6;BYTE(overworld_screen_trans_dir_bits)=1;left_right_scroll_target_end=512;
  for(int x=256;x<=512;x+=8) {
   BG2HOFS_copy2=BG1HOFS_copy2=x;Layers(p,x,0);
   for(int i=0;i<32768;i++)p->vram[i]=0xfffe;
   memcpy(original,p->vram,sizeof(original));
   WideFrame f=BeginWideFrame(5);assert(f.context==9 && !f.settled);
   BeginWideTileRepair(&f);Check(p,&f,1,ExpectTwoAreas);EndWideTileRepair();
   assert(!memcmp(original,p->vram,sizeof(original)));cases++;
  }
 }
 // Across a scroll whose two views are not placed alike: from the left edge
 // of a small area north into a big one, the drawn camera glides left, and
 // the left band of the rows still in the small area shows the screen beside
 // it, which neither map holds. It comes from that screen's own map16.
 {
  Overworld(p,512,1024);p->extraLeftRight=72;BYTE(overworld_area_is_big)=0;overworld_offset_base_x=512>>3;overworld_offset_base_y=1024;
  ow_scroll_vars0.xstart=512;ow_scroll_vars0.xend=768;ow_scroll_vars0.ystart=1024;ow_scroll_vars0.yend=1024+286;
  ResetWideTracking();ZeldaWideAfterMapLoad(9);
  for(int i=0;i<4096;i++)dung_bg2[i]=(i*7)%4096;
  WideFrame settled=BeginWideFrame(5);assert(settled.settled && settled.view.offset_x==-72);
  ZeldaWideBeforeMapLoad(9);memcpy(prev_map16,dung_bg2,sizeof(prev_map16));
  for(int i=0;i<4096;i++)dung_bg2[i]=(i*13+5)%4096;
  BYTE(overworld_area_is_big)=0x20;overworld_offset_base_x=0;overworld_offset_base_y=0;ZeldaWideAfterMapLoad(9);
  submodule_index=6;BYTE(overworld_screen_trans_dir_bits)=8;up_down_scroll_target=1024-224;
  fake_decodes=0;beside_cells=0;int last_offset=-72;
  for(int y=1024;y>=1024-224;y-=8) {
   BG2VOFS_copy2=BG1VOFS_copy2=y;Layers(p,512,y);
   for(int i=0;i<32768;i++)p->vram[i]=0xfffc;
   memcpy(original,p->vram,sizeof(original));
   WideFrame f=BeginWideFrame(5);assert(f.context==9 && !f.settled);
   assert(f.view.offset_x>=last_offset && f.view.offset_x<=0);last_offset=f.view.offset_x;
   BeginWideTileRepair(&f);Check(p,&f,1,ExpectBeside);EndWideTileRepair();
   assert(!memcmp(original,p->vram,sizeof(original)));cases++;
  }
  assert(last_offset==0 && beside_cells>0 && fake_decodes==1);
 }
 // Indoors: both layers of the room entered, and of the one left, placed
 // where the camera shows it. Settled, VRAM keeps the room.
 {
  memset(g_ram,0,sizeof(g_ram));main_module_index=7;submodule_index=0;player_is_indoors=1;
  enhanced_features0=kFeatures0_WidescreenVisualFixes;p->extraLeftRight=72;
  dungeon_room_index=0x52;ResetWideTracking();ZeldaWideAfterMapLoad(7);
  for(int i=0;i<0x1000;i++){dung_bg2[i]=0x5000+i;dung_bg1[i]=0x6000+i;}
  BG2HOFS_copy2=BG1HOFS_copy2=0x400;BG2VOFS_copy2=BG1VOFS_copy2=0xa00;Layers(p,0x400,0xa00);
  quadrant_fullsize_x=2;room_bounds_x.v[1]=0x400;room_bounds_x.v[3]=0x500;
  room_bounds_y.v[0]=0xa00;room_bounds_y.v[2]=0xa10;
  WideFrame settled=BeginWideFrame(5);assert(settled.settled && settled.view.offset_x==-72);
  // The door at the room's left edge: the next room is loaded, its bounds set.
  submodule_index=2;overworld_screen_transition=3;left_right_scroll_target_end=0x100;
  ZeldaWideBeforeMapLoad(7);
  for(int i=0;i<0x1000;i++){dung_bg2[i]=0x1000+i;dung_bg1[i]=0x2000+i;}
  dungeon_room_index=0x51;ZeldaWideAfterMapLoad(7);
  room_bounds_x.v[1]=0x200;room_bounds_x.v[3]=0x300;int last_offset=0;
  for(int x=0x400;x>=0x300;x-=4) {
   BG2HOFS_copy2=BG1HOFS_copy2=x;Layers(p,x,0xa00);
   for(int i=0;i<32768;i++)p->vram[i]=0xfffd;
   memcpy(original,p->vram,sizeof(original));
   WideFrame f=BeginWideFrame(5);assert(f.context==7 && !f.settled);
   BeginWideTileRepair(&f);Check(p,&f,0,ExpectRoomsBg1);Check(p,&f,1,ExpectRoomsBg2);EndWideTileRepair();
   assert(!memcmp(original,p->vram,sizeof(original)));cases++;
   // From the view before the door to the one the room has at the end
   // (WideCamera_FindDungeonTransitionEnd may stop 2 pixels short), in
   // steps of the scroll's own size.
   assert(x<0x400 ? abs(f.view.offset_x-last_offset)<=3 : f.view.offset_x==-72);
   assert(x>0x300 || abs(f.view.offset_x-72)<=2);
   last_offset=f.view.offset_x;
  }
  submodule_index=0;overworld_screen_transition=0;ResetWideTracking();ZeldaWideAfterMapLoad(7);
  WideFrame f=BeginWideFrame(5);assert(f.settled);BeginWideTileRepair(&f);assert(!g_wide_tile_repair.count);EndWideTileRepair();
  // A teleport door moves the camera's frame: the room shown follows it.
  submodule_index=2;BG2HOFS_copy2=BG1HOFS_copy2=0x1300;BG2VOFS_copy2=BG1VOFS_copy2=0x60b;Layers(p,0x1300,0x60b);
  f=BeginWideFrame(5);BeginWideTileRepair(&f);assert(g_wide_live.x==0x1200 && g_wide_live.y==0x600);EndWideTileRepair();
 }
 // Exclusions: no fixed camera, map screens, mode 7, shaking beyond the
 // edges' reach, a layer not 64x64 or moving apart from BG2.
 for(int guard=0;guard<7;guard++) {
  Overworld(p,384,300);p->extraLeftRight=72;g_widescreen_edge_mode=1;
  ResetWideTracking();ZeldaWideAfterMapLoad(9);for(int i=0;i<32768;i++)p->vram[i]=0xffff;
  if(guard==0)p->extraLeftRight=0;
  if(guard==1)g_widescreen_edge_mode=0;
  if(guard==2){main_module_index=14;submodule_index=7;}
  if(guard==3)p->mode=7;
  if(guard==4)p->bgLayer[1].hScroll=(384+40)&1023;
  if(guard==5){main_module_index=7;submodule_index=2;player_is_indoors=1;dungeon_room_index=0;ZeldaWideAfterMapLoad(7);p->bgLayer[1].tilemapWider=false;BG1HOFS_copy2=100;}
  if(guard==6){main_module_index=14;submodule_index=3;saved_module_for_menu=7;overworld_map_state=3;player_is_indoors=1;}
  WideFrame f=BeginWideFrame(5);
  BeginWideTileRepair(&f);assert(!g_wide_tile_repair.count);EndWideTileRepair();
 }
 g_widescreen_edge_mode=1;
 printf("PASS %u camera positions (settled scans at 72/96 margins, two area transitions, one with the screen beside them, a room transition, a teleport frame), %u repaired cells, full VRAM/RAM restoration and 7 exclusion guards\n",cases,total);
 if(argc>2 && strcmp(argv[1],"-")) {
  FILE *f=fopen(argv[1],"rb");assert(f);map_bytes=fread(map_words,1,sizeof(map_words),f);fclose(f);
 }
 Ppu *scratch=calloc(1,sizeof(Ppu));PicaAtlas *cache=calloc(1,sizeof(PicaAtlas));
 PicaLine lines[240];atlas=malloc(1024*512*4);PicaAtlasInit(cache,atlas);
 for(int arg=3;arg<argc;arg++) {
  char path[2048];snprintf(path,sizeof(path),"%s/ram.bin",argv[arg]);FILE *f=fopen(path,"rb");assert(f);assert(fread(g_ram,1,sizeof(g_ram),f)==sizeof(g_ram));fclose(f);
  memcpy(saved_ram,g_ram,sizeof(saved_ram));unsigned repaired=0;WideFrame frame={0};
  for(int mode=0;mode<3;mode++) {
   unsigned flags=5|(mode==1?16:0);
   LoadDump(p,argv[arg]);p->windowExtLeft=p->windowExtRight=NULL;p->extraLeftRight=72;
   memcpy(original,p->vram,sizeof(original));
   ResetWideTracking();frame=BeginWideFrame(flags);
   if(mode)BeginWideTileRepair(&frame);
   PpuBeginDrawing(p,(uint8*)cpu[mode],2048,flags);
   WideRenderState state=BeginWideRender(&frame);ConfigurePpuSideSpace(&frame);
   for(int y=0;y<240;y++)ppu_runLine(p,y+1);
   if(mode==2) {
    // Render corrected geometry through the existing GPU model and compare.
    width=400;memset(surface,0,sizeof(surface));memset(depth,0,sizeof(depth));memset(stencil,0,sizeof(stencil));memset(output,0,sizeof(output));
    for(int y=0;y<240;y++)PicaCaptureLine(lines+y,p,y);
    PicaAtlasBegin(cache);PicaFrame pf={.memory=p,.lines=lines,.scratch=scratch,.atlas=cache,.pixels=atlas,.width=400,.height=240,.emit=Raster};
    assert(PicaBuildFrame(&pf));
    for(int y=0;y<240;y++)for(int x=0;x<400;x++)assert(output[y*512+x]==RefRgb(cpu[mode][y*512+x]));
   }
   EndWideRender(&state);
   if(mode){repaired=g_wide_tile_repair.count;EndWideTileRepair();}
   assert(!memcmp(original,p->vram,sizeof(original)));assert(!memcmp(saved_ram,g_ram,sizeof(saved_ram)));
  }
  assert(!memcmp(cpu[1],cpu[2],sizeof(cpu[1])));
  // Only what the SNES picture does not show changes.
  int left=72+frame.view.offset_x,top=frame.view.delta_y;unsigned different=0;
  for(int y=0;y<240;y++)for(int x=0;x<400;x++)if(cpu[0][y*512+x]!=cpu[1][y*512+x]){assert(x<left||x>=left+256||y<top||y>=top+224);different++;}
  if(strcmp(argv[2],"-")) {
   snprintf(path,sizeof(path),"%s/%d.ppm",argv[2],arg-3);f=fopen(path,"wb");assert(f);fprintf(f,"P6\n400 240\n255\n");
   for(int y=0;y<240;y++)for(int x=0;x<400;x++){uint32 c=cpu[1][y*512+x];uint8 rgb[]={c>>16,c>>8,c};fwrite(rgb,1,3,f);}fclose(f);
  }
  printf("PASS %s: %u repaired words, %u changed pixels outside the SNES picture, CPU Old/New/GPU parity and restored state\n",argv[arg],repaired,different);
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
 subprocess.run(['cc','-O1','-D__3DS__','-fsanitize=address,undefined','-fno-sanitize=shift-base','-fno-sanitize-recover=all','-I'+str(r/'engine'),'-I'+str(r/'platform/3ds/source'),str(d/'test.c'),str(r/'engine/snes/ppu.c'),str(r/'engine/src/wide_camera.c'),str(r/'platform/3ds/source/ppu_gpu_model.c'),'-o',str(d/'test')],check=True)
 if args.output:args.output.mkdir(parents=True,exist_ok=True)
 subprocess.run([str(d/'test'),str(table),str(args.output.resolve()) if args.output else '-',*[str(x.resolve()) for x in args.dumps]],check=True)
