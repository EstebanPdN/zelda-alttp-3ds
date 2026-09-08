#!/usr/bin/env python3
"""Replay saved PPU state using compiled ARMv6K E10 and candidate instructions.
Counts CPU instructions, NOT CPU cycles or console FPS; GPU rasterization is excluded. No ROM. Needs Unicorn.
"""
import argparse, re, os, struct, subprocess, tempfile, json, bisect
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
root=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('dump',type=Path);args=p.parse_args()
tools=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin'
def cbytes(name,b):return 'static const unsigned char '+name+'[]={'+','.join(map(str,b))+'};\n'
reference=subprocess.check_output(['git','show','b723a0f:app/jni/src/snes/ppu.c'],cwd=root,text=True)
exports=re.findall(r'^(?:Ppu\*|void|int|uint8_t)\s+((?:ppu_|Ppu)[A-Za-z0-9_]+)\(',reference,re.M)
loader=(root/'platform/3ds/tests/ppu_old3ds_test.c').read_text()
start=loader.index('  const unsigned regs[][2]');end=loader.index('  snprintf(path, sizeof(path), "%s/ppu.txt"',start)
setup=loader[start:end]
side=re.search(r'configured/left/right/bottom=(\d+)/(\d+)/(\d+)/(\d+)',(args.dump/'ppu.txt').read_text()).groups()
setup+=f' p->extraLeftRight={side[0]};p->extraLeftCur={side[1]};p->extraRightCur={side[2]};p->extraBottomCur={side[3]};'
code='''#include <stdint.h>
uint64_t svcGetSystemTick(void) { return 0; }
#include <string.h>
#include "snes/ppu.h"
#include "snes/ppu_retained.h"
#include "ppu_gpu_model.h"
extern void ref_ppu_runLine(Ppu*,int);
extern void ref_PpuBeginDrawing(Ppu*,uint8_t*,size_t,uint32_t);
static Ppu ppus[2];static PpuTileCache caches[2];
static PpuRetainedMaps retained;
static uint32_t output[512*240];
static void load(void *ctx,void *dst,size_t n){unsigned char **ptr=ctx;memcpy(dst,*ptr,n);*ptr+=n;}
'''
code+=cbytes('ram',(args.dump/'ram.bin').read_bytes())
code+=cbytes('state',(args.dump/'load-state.bin').read_bytes())
code+='''static PicaAtlas atlas;static PicaLine lines[224];static Ppu scratch,saved;
static uint32_t atlasPixels[1024*512];
struct Vertex {int16_t x,y,z,w,u,v;uint8_t r,g,b,a;};
static struct Vertex vertices[PICA_MAX_VERTICES];
static unsigned vertexCount;static float iw,ih;
static bool Emit(void *ctx,unsigned group,const PicaQuad *q) {
 if(vertexCount+6>PICA_MAX_VERTICES)return false;
 int16_t depth=q->depth>>1;if(!depth)depth=1;
 struct Vertex a={q->x0,q->y0,depth,1,q->u0,q->v0,q->r,q->g,q->b,q->a},b=a,c=a,d=a;
 b.x=d.x=q->x1;b.u=d.u=q->u1;c.y=d.y=q->y1;c.v=d.v=q->v1;
 struct Vertex *v=&vertices[vertexCount];v[0]=a;v[1]=b;v[2]=c;v[3]=c;v[4]=b;v[5]=d;vertexCount+=6;
 return true;
}
void init(void){for(int v=0;v<2;v++){
Ppu *p=&ppus[v];p->tileCache=&caches[v];ppu_reset(p);
unsigned char *ptr=(unsigned char*)state+24+20+27+65536+40+3024+15+192;
ppu_saveload(p,load,&ptr);memcpy(p->oam,ram+0x800,sizeof(p->oam));
'''+setup+'''} ppus[0].retained=&retained;ppus[0].retainedAttempted=true;PicaAtlasInit(&atlas,atlasPixels); }
unsigned run(unsigned v){
Ppu*p=&ppus[v];
if(v) {
 memcpy(&saved,p,sizeof(Ppu));PicaAtlasBegin(&atlas);vertexCount=0;
 iw=2.f/(256+p->extraLeftRight*2);ih=2.f/224;
 for(unsigned y=0;y<224;y++)PicaCaptureLine(&lines[y],p,y);
 PicaFrame f={.memory=&saved,.scratch=&scratch,.lines=lines,.atlas=&atlas,.pixels=atlasPixels,.width=256+p->extraLeftRight*2,.height=224,.emit=Emit};
 return PicaBuildFrame(&f)?vertexCount:0xffffffff;
}
ref_PpuBeginDrawing(p,(uint8_t*)output,2048,17);
for(int y=1;y<=224;y++)ref_ppu_runLine(p,y);
return 0;}
unsigned checksum(void){unsigned h=2166136261u;for(int i=0;i<512*240;i++)h=(h^output[i])*16777619u;const unsigned char *v=(const unsigned char*)vertices;for(unsigned i=0;i<vertexCount*sizeof(vertices[0]);i++)h=(h^v[i])*16777619u;return h;}
'''
with tempfile.TemporaryDirectory(prefix='lttp-arm-ppu-') as td:
 tmp=Path(td);(tmp/'driver.c').write_text(code);(tmp/'reference.c').write_text(reference)
 common=[str(tools/'arm-none-eabi-gcc'),'-mcpu=mpcore','-marm','-mfpu=vfp','-mfloat-abi=hard','-O3','-ffast-math','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-D__3DS__','-I'+str(root/'app/jni/src'),'-I'+str(root/'app/jni/src/snes'),'-I'+str(root/'platform/3ds/source')]
 subprocess.run(common+['-D'+s+'=ref_'+s for s in exports]+['-c',str(tmp/'reference.c'),'-o',str(tmp/'ref.o')],check=True)
 subprocess.run(common+[str(tmp/'driver.c'),str(root/'app/jni/src/snes/ppu.c'),str(root/'platform/3ds/source/ppu_gpu_model.c'),str(tmp/'ref.o'),'-nostartfiles','-specs=nosys.specs','-Wl,--gc-sections','-Wl,-Ttext=0x10000','-Wl,-e,run','-Wl,-u,init','-Wl,-u,checksum','-lm','-o',str(tmp/'test.elf')],check=True)
 elf=(tmp/'test.elf').read_bytes();uc=Uc(UC_ARCH_ARM,UC_MODE_ARM);uc.mem_map(0x10000,0x2100000)
 off=struct.unpack_from('<I',elf,28)[0];size,count=struct.unpack_from('<HH',elf,42)
 for i in range(count):
  typ,offset,addr,_,length=struct.unpack_from('<5I',elf,off+size*i)
  if typ==1 and length:uc.mem_write(addr,elf[offset:offset+length])
 symbol_rows=[line.split() for line in subprocess.check_output([str(tools/'arm-none-eabi-nm'),str(tmp/'test.elf')],text=True).splitlines() if len(line.split())==3]
 syms={row[-1]:int(row[0],16) for row in symbol_rows}
 from unicorn.arm_const import UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC
 uc.reg_write(UC_ARM_REG_C1_C0_2,0xf00000);uc.reg_write(UC_ARM_REG_FPEXC,0x40000000)
 def call(name,arg=0):
  uc.reg_write(UC_ARM_REG_SP,0x2000000);uc.reg_write(UC_ARM_REG_LR,0x2080000);uc.reg_write(UC_ARM_REG_R0,arg)
  uc.emu_start(syms[name],0x2080000,timeout=55000000)
  assert uc.reg_read(UC_ARM_REG_PC)==0x2080000,'ARM replay timeout'
  return uc.reg_read(UC_ARM_REG_R0)
 call('init');hashes=[];counts=[];profiles=[];cold_counts=[]
 names=sorted((int(row[0],16),row[2]+'@'+row[0]) for row in symbol_rows if row[1] in ('T','t') and not row[2].startswith('$'));starts=[a for a,n in names]
 for v in [0,1]:
  counter=[0];pc_counts={}
  def tick(uc,address,size,data):
   counter[0]+=1;pc_counts[address]=pc_counts.get(address,0)+1
  h=uc.hook_add(UC_HOOK_CODE,tick);uc.ctl_flush_tb();call('run',v);uc.hook_del(h);cold_counts.append(counter[0])
  counter[0]=0;pc_counts.clear()
  h=uc.hook_add(UC_HOOK_CODE,tick);uc.ctl_flush_tb();result=call('run',v);uc.hook_del(h);counts.append(counter[0]);hashes.append(call('checksum'))
  totals={}
  for addr,n in pc_counts.items():
   name=names[bisect.bisect_right(starts,addr)-1][1];totals[name]=totals.get(name,0)+n
  profiles.append(sorted(totals.items(),key=lambda x:-x[1])[:12])
 assert result!=0xffffffff,result
 print(json.dumps(dict(dump=args.dump.name,hash=hex(hashes[0]),e11_cpu_instructions=counts[0],gpu_prepare_instructions=counts[1],ratio=counts[1]/counts[0],cold_e11_cpu_instructions=cold_counts[0],cold_gpu_prepare_instructions=cold_counts[1],profiles=profiles,vertices=result,note='GPU counts include source snapshot, line recording, atlas validation and vertex writes, but not Citro3D submission or cache maintenance. Static replay; not cycles, timings or FPS. Hashing excluded. Cold includes initial cache population; warm reuses the scene. No OS, audio, cache-miss costs or live HDMA replay.'),indent=2))
