#!/usr/bin/env python3
"""Exercise actual buffer selection/Begin against simulated in-flight GPU reads."""
from pathlib import Path
import tempfile,subprocess
r=Path(__file__).resolve().parents[3];s=(r/'platform/3ds/source/ppu_gpu.c').read_text()
def fn(signature):
 a=s.index(signature);b=s.index('{',a);e=b+1;n=1
 while n:n+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
code='''#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#define PICA_MAX_LINES 240
typedef struct {int extraLeftRight;bool gpuRecording,gpuInvalidWrite;}Ppu;
typedef int Vertex;
static struct {unsigned slot,lastSubmittedSlot,frames,syncUs,captured;bool prepared,output,ready,forceCpu;int atlas,atlasPool[2],*cache,*cachePool[2],buffers;Vertex *vertices,*vertexPool[2];Ppu *saved;const char *reason;}g;
static int inFlight=-1;
static void BufInfo_Init(int *b){*b=0;}
static int BufInfo_Add(int *b,void *p,int stride,int count,int permutation){assert(p==g.vertexPool[g.slot]);return 0;}
static void ResetRanges(int w,unsigned h){}
static void PicaAtlasBegin(int *cache){assert((int)g.slot!=inFlight);assert(cache==g.cachePool[g.slot]);}
'''+fn('static bool SelectBuffers(')+'\n'+fn('bool PpuGpuBegin(')+'''
int main(void){int caches[2],vertices[2];Ppu saved,live={72,false,false};g.saved=&saved;g.ready=true;
 for(int i=0;i<2;i++){g.cachePool[i]=&caches[i];g.vertexPool[i]=&vertices[i];}
 for(unsigned frame=0;frame<1000;frame++){
  assert(PpuGpuBegin(&live,224));assert((int)g.slot!=inFlight);assert(g.syncUs==0);
  // Retry/failed preflight must never flip into the in-flight buffer.
  if(frame%3==0)assert(PpuGpuBegin(&live,224));
  assert((int)g.slot!=inFlight);
  inFlight=-1; // The presenter's FrameBegin retires the preceding queue.
  if(frame%5){g.lastSubmittedSlot=g.slot;inFlight=g.slot;}
 }
 puts("PASS 1000 GPU/CPU fallback cycles and preflight retries: inactive buffers selected, no overwrite of in-flight GPU data");}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.c').write_text(code)
 subprocess.run(['cc','-O1','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
