from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[3]
s=(r/'platform/3ds/source/ppu_gpu.c').read_text();fn=s[s.index('const uint32_t *PpuGpuReadback(void) {'):]
code='''#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
static struct {bool output;uint32_t *readback;struct{void *data;}result;}g;
static bool waitOk=true;
static bool PicaC3DWaitIdleFor(long long timeout){assert(timeout==1000000000ll);return waitOk;}
'''+fn+'''
int main(void){static uint16_t tiled[512*256];static uint32_t dest[512*256];
 g.output=true;g.result.data=tiled;g.readback=dest;
 for(unsigned y=0;y<256;y++)for(unsigned x=0;x<512;x++){
  unsigned m=0;for(unsigned bit=0;bit<3;bit++)m|=((x>>bit)&1)<<(bit*2),m|=((y>>bit)&1)<<(bit*2+1);
  tiled[((y/8)*64+x/8)*64+m]=((x&31)<<11)|((y&31)<<6)|(((x+y)&31)<<1)|1;
 }
 assert(PpuGpuReadback()==dest);
 for(unsigned y=0;y<256;y++)for(unsigned x=0;x<512;x++){
 unsigned r=x&31,g=y&31,b=(x+y)&31;
 assert(dest[y*512+x]==((((r<<3)|(r>>2))<<16)|(((g<<3)|(g>>2))<<8)|(b<<3)|(b>>2)));
 }
 dest[0]=0x12345678;waitOk=false;assert(!PpuGpuReadback());assert(dest[0]==0x12345678);
 puts("PASS actual CPU dump readback: 131072 pixels, RGB5 expansion, bounded-wait failure; no GPU submission possible");}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.c').write_text(code)
 subprocess.run(['cc','-O1','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
