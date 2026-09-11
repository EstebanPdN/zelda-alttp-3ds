#!/usr/bin/env python3
"""Reproduce retained startup RAM using production initialization/frame dispatch.
Peripheral allocation/reset and music transfer are stubbed; no rendering or
physical audio result is claimed. The supplied RAM is private test input.
"""
from pathlib import Path
import argparse, subprocess, tempfile
r=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--source',type=Path);p.add_argument('--dump',type=Path,required=True);a=p.parse_args()
s=(a.source or r/'app/jni/src/src/zelda_rtl.c').read_text()
def fn(sig):
 start=s.index(sig);i=s.index('{',start)+1;depth=1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]+'\n'
code=r'''#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef int8_t int8;
typedef uint8_t uint8;
typedef uint16_t uint16;
uint8 g_ram[131072];
#include "src/variables.h"
typedef struct { uint16_t vram[32768]; } FakePpu;
static FakePpu ppu;
static uint8_t sram_fixture[8192], dma, player;
struct { void *dma; FakePpu *ppu; uint8 *ram,*sram; uint16_t *vram; void *player; } g_zenv;
static int boots, loops, nmis, music;
static void *dma_init(void*p){return &dma;}
static FakePpu *ppu_init(void*p){return &ppu;}
static void *SpcPlayer_Create(void){return &player;}
static void SpcPlayer_Initialize(void*p){music=0;}
static void dma_reset(void*p){}
static void ppu_reset(FakePpu*p){memset(p,0,sizeof(*p));}
static void ZeldaInitializationCode(void){
 assert(main_module_index==0 && submodule_index==0);
 for(unsigned i=0;i<sizeof(g_ram);i++)assert(!g_ram[i]);
 assert(!memcmp(g_zenv.sram,sram_fixture,sizeof(sram_fixture)));
 boots++;music++;animated_tile_data_src=0xa680;
}
static void ZeldaRunPolyLoop(void){}
static void ZeldaRunGameLoop(void){loops++;}
static void Interrupt_NMI(uint16 input){nmis++;}
'''+fn('void ZeldaInitialize() {')+fn('void ZeldaRunFrameInternal(')+r'''
int main(int argc,char**argv){
 assert(argc==3);uint8_t captured[sizeof(g_ram)];
 FILE*f=fopen(argv[1],"rb");assert(f);assert(fread(captured,1,sizeof(captured),f)==sizeof(captured));fclose(f);
 f=fopen(argv[2],"rb");assert(f);assert(fread(sram_fixture,1,sizeof(sram_fixture),f)==sizeof(sram_fixture));fclose(f);
 printf("Captured module=%u submodule=%u boot marker=%02x%02x\n",captured[0x10],captured[0x11],captured[0xadd],captured[0xadc]);
 assert(captured[0xadc] || captured[0xadd]);
 for(int cycle=0;cycle<5;cycle++){
  if(cycle==0)memset(g_ram,0,sizeof(g_ram));
  else if(cycle==1)memcpy(g_ram,captured,sizeof(g_ram));
  else memset(g_ram,0xa5,sizeof(g_ram));
  ZeldaInitialize();
  // main.c calls ZeldaReadSram after ZeldaInitialize, before first frame.
  memcpy(g_zenv.sram,sram_fixture,sizeof(sram_fixture));
  int before=boots;
  ZeldaRunFrameInternal(0,1);
  if(boots!=before+1){fprintf(stderr,"FAIL restart cycle=%d: stale RAM skipped boot/music upload\n",cycle);return 1;}
  ZeldaRunFrameInternal(0,1);assert(boots==before+1 && music==1);
  assert(!memcmp(g_zenv.sram,sram_fixture,sizeof(sram_fixture)));
  free(g_zenv.sram);
 }
 assert(boots==5 && loops==10 && nmis==10);
 puts("PASS cold start, supplied restart RAM, repeated dirty RAM starts: initialization once per boot; loaded SRAM unchanged");
}
'''
with tempfile.TemporaryDirectory() as td:
 t=Path(td);(t/'test.c').write_text(code)
 subprocess.run(['cc','-O1','-fsanitize=address,undefined','-I'+str(r/'app/jni/src'),str(t/'test.c'),'-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test'),str(a.dump/'ram.bin'),str(a.dump/'sram.bin')],check=True)
