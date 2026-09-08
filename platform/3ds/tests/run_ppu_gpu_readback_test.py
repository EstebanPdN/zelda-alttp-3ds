#!/usr/bin/env python3
"""Inject delayed GX DMA into the actual readback functions, with cache errors."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[3];s=(r/'platform/3ds/source/ppu_gpu.c').read_text()
a=s.index('static bool TransferReadback');b=s.index('static bool ColorProbe',a)
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef int GX_TRANSFER_FORMAT;
#define GX_CMDLIST_FLUSH 2
#define GX_BUFFER_DIM(w,h) 0
#define GX_TRANSFER_FLIP_VERT(x) 0
#define GX_TRANSFER_OUT_TILED(x) 0
#define GX_TRANSFER_IN_FORMAT(x) 0
#define GX_TRANSFER_OUT_FORMAT(x) 0
#define R_SUCCEEDED(r) ((r)>=0)
static struct {struct {void *data;} result;void *readback;int readbackCacheResult;} g;
static bool cleanOk=true,waitOk=true,queued,complete;static int cacheResult;static unsigned splits,invalidates;
static void C3D_FrameSplit(unsigned flags){assert(flags==GX_CMDLIST_FLUSH);splits++;}
static bool Clean(const void *p,size_t n){assert(!queued && n==512*256*4);return cleanOk;}
static int GX_DisplayTransfer(void *a,int b,void *c,int d,int e){assert(splits);queued=true;complete=false;return 0;}
static bool PicaC3DWaitIdle(void){if(waitOk)complete=true;return waitOk;}
static int GSPGPU_InvalidateDataCache(void *p,size_t n){assert(complete);invalidates++;return cacheResult;}
FUNCTIONS
int main(void){
 assert(TransferReadback(0));assert(queued&&!complete);
 assert(FinishReadback(512*256*2));assert(complete&&invalidates==1);
 queued=false;cleanOk=false;assert(!TransferReadback(0));assert(!queued);
 cleanOk=true;assert(TransferReadback(0));waitOk=false;
 assert(!FinishReadback(512*256*2));assert(invalidates==1);
 waitOk=true;cacheResult=-1;assert(!FinishReadback(512*256*2));assert(g.readbackCacheResult==-1);
 puts("PASS delayed DMA completion precedes CPU invalidation; clean, queue and invalidation failures propagate");
}
'''.replace('FUNCTIONS',s[a:b])
assert 'C3D_FrameSync(' not in s,'VBlank is not a GPU fence'
with tempfile.TemporaryDirectory(prefix='alttp-gpu-readback-') as t:
 p=Path(t);(p/'test.c').write_text(code)
 subprocess.run(['cc','-O1','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
