#!/usr/bin/env python3
"""Replay saved PPU state using compiled ARMv6K E10 and candidate instructions.
Counts instructions, NOT CPU cycles or console FPS. No ROM. Needs Unicorn.
"""
import argparse, re, os, struct, subprocess, tempfile, json, bisect
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
root=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('dump',type=Path);args=p.parse_args()
tools=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin'
def cbytes(name,b):return 'static const unsigned char '+name+'[]={'+','.join(map(str,b))+'};\n'
reference=subprocess.check_output(['git','show','ce53c44:app/jni/src/snes/ppu.c'],cwd=root,text=True)
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
extern void ref_ppu_runLine(Ppu*,int);
extern void ref_PpuBeginDrawing(Ppu*,uint8_t*,size_t,uint32_t);
static Ppu ppus[2];static PpuTileCache caches[2];
static PpuRetainedMaps retained;
static uint32_t output[512*240];
static void load(void *ctx,void *dst,size_t n){unsigned char **ptr=ctx;memcpy(dst,*ptr,n);*ptr+=n;}
'''
code+=cbytes('ram',(args.dump/'ram.bin').read_bytes())
code+=cbytes('state',(args.dump/'load-state.bin').read_bytes())
code+='''void init(void){for(int v=0;v<2;v++){
Ppu *p=&ppus[v];p->tileCache=&caches[v];ppu_reset(p);
unsigned char *ptr=(unsigned char*)state+24+20+27+65536+40+3024+15+192;
ppu_saveload(p,load,&ptr);memcpy(p->oam,ram+0x800,sizeof(p->oam));
'''+setup+'''} ppus[1].retained=&retained;ppus[1].retainedAttempted=true; }
unsigned run(unsigned v){
Ppu*p=&ppus[v];
if(v)PpuBeginDrawing(p,(uint8_t*)output,2048,17);
else ref_PpuBeginDrawing(p,(uint8_t*)output,2048,17);
for(int y=1;y<=240;y++){if(v)ppu_runLine(p,y);else ref_ppu_runLine(p,y);}
return 0;}
unsigned checksum(void){unsigned h=2166136261u;for(int i=0;i<512*240;i++)h=(h^output[i])*16777619u;return h;}
'''
with tempfile.TemporaryDirectory(prefix='lttp-arm-ppu-') as td:
 tmp=Path(td);(tmp/'driver.c').write_text(code);(tmp/'reference.c').write_text(reference)
 common=[str(tools/'arm-none-eabi-gcc'),'-mcpu=mpcore','-marm','-O3','-ffast-math','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-D__3DS__','-I'+str(root/'app/jni/src'),'-I'+str(root/'app/jni/src/snes')]
 subprocess.run(common+['-D'+s+'=ref_'+s for s in exports]+['-c',str(tmp/'reference.c'),'-o',str(tmp/'ref.o')],check=True)
 subprocess.run(common+[str(tmp/'driver.c'),str(root/'app/jni/src/snes/ppu.c'),str(tmp/'ref.o'),'-nostartfiles','-specs=nosys.specs','-Wl,--gc-sections','-Wl,-Ttext=0x10000','-Wl,-e,run','-Wl,-u,init','-Wl,-u,checksum','-lm','-o',str(tmp/'test.elf')],check=True)
 elf=(tmp/'test.elf').read_bytes();uc=Uc(UC_ARCH_ARM,UC_MODE_ARM);uc.mem_map(0x10000,0x2100000)
 off=struct.unpack_from('<I',elf,28)[0];size,count=struct.unpack_from('<HH',elf,42)
 for i in range(count):
  typ,offset,addr,_,length=struct.unpack_from('<5I',elf,off+size*i)
  if typ==1 and length:uc.mem_write(addr,elf[offset:offset+length])
 symbol_rows=[line.split() for line in subprocess.check_output([str(tools/'arm-none-eabi-nm'),str(tmp/'test.elf')],text=True).splitlines() if len(line.split())==3]
 syms={row[-1]:int(row[0],16) for row in symbol_rows}
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
  h=uc.hook_add(UC_HOOK_CODE,tick);uc.ctl_flush_tb();call('run',v);uc.hook_del(h);counts.append(counter[0]);hashes.append(call('checksum'))
  totals={}
  for addr,n in pc_counts.items():
   name=names[bisect.bisect_right(starts,addr)-1][1];totals[name]=totals.get(name,0)+n
  profiles.append(sorted(totals.items(),key=lambda x:-x[1])[:12])
 assert hashes[0]==hashes[1],hashes
 print(json.dumps(dict(dump=args.dump.name,hash=hex(hashes[0]),e10_instructions=counts[0],candidate_instructions=counts[1],ratio=counts[1]/counts[0],cold_e10_instructions=cold_counts[0],cold_candidate_instructions=cold_counts[1],profiles=profiles,note='Static replay; not cycles, timings or FPS. Hashing excluded. Cold includes initial cache population; warm reuses the scene. No OS, audio, cache-miss costs or live HDMA replay.'),indent=2))
