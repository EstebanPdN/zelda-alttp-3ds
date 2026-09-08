#!/usr/bin/env python3
"""Execute actual ConfigureCompose through real Citro3D TEV setters.

The second mode injects a one-unit lower-bound interpolation error matching
E12's captured subtract/full-exception pixels. This is a sensitivity test,
not a claim that all physical TEV operations have that error.
"""
from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--e12-capture',type=Path);a=p.parse_args()
s=(r/'platform/3ds/source/ppu_gpu.c').read_text()
start=s.index('static void ConfigureCompose(');end=s.index('static bool DrawComposition',start)
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
typedef uint16_t u16;
#define BIT(n) (1u<<(n))
#include <3ds/gpu/enums.h>
#include <c3d/texenv.h>
static C3D_TexEnv stages[6];static int bufferMask;
C3D_TexEnv *C3D_GetTexEnv(int i){return &stages[i];}
void C3D_TexEnvBufUpdate(int mode,int mask){assert(mode==C3D_RGB);bufferMask=mask;}
static void ResetTev(void){for(int i=0;i<6;i++)C3D_TexEnvInit(&stages[i]);bufferMask=0;}
static uint32_t Rgba(unsigned r,unsigned g,unsigned b,unsigned a){return r|(g<<8)|(b<<16)|(a<<24);}
FUNCTION
static int clamp(int x){return x<0?0:x>255?255:x;}
static int combine(int f,int a,int b,int c){
 switch(f){case GPU_REPLACE:return a;case GPU_MODULATE:return a*b/255;
 case GPU_ADD:return clamp(a+b);case GPU_SUBTRACT:return clamp(a-b);
 case GPU_INTERPOLATE:return (a*c+b*(255-c))/255;default:assert(0);return 0;}
}
static int source(int which,int component,int prev[4],int main[4],int sub[4],int buffer[4],uint32_t constant){
 switch(which){case GPU_PREVIOUS:return prev[component];case GPU_TEXTURE0:return main[component];
 case GPU_TEXTURE1:return sub[component];case GPU_PREVIOUS_BUFFER:return buffer[component];
 case GPU_CONSTANT:return (constant>>(component*8))&255;case GPU_PRIMARY_COLOR:return 255;default:assert(0);return 0;}
}
static uint16_t render(unsigned i,bool boundaryError,bool old){
 unsigned m=i&31,s=(i>>5)&31,flags=i>>12;ConfigureCompose(flags);
 if(old){C3D_TexEnv *e=&stages[3];e->funcRgb=GPU_REPLACE;}
 int main[4]={8*m+1,8*(m*7&31)+1,8*(m*13&31)+1,(i&1024)?255:127};
 int sub[4]={8*s+1,8*(s*11&31)+1,8*(s*3&31)+1,(i&2048)?255:127};
 int prev[4]={0},buffer[4]={0},pending[4]={0};
 for(int st=0;st<6;st++){
  C3D_TexEnv *e=&stages[st];int next[4];
  for(int ch=0;ch<4;ch++){
   int sr=ch==3?e->srcAlpha:e->srcRgb,op=ch==3?e->opAlpha:e->opRgb,v[3];
   for(int j=0;j<3;j++){
    int operand=(op>>(4*j))&15;
    assert(operand==0 || (ch<3 && operand==GPU_TEVOP_RGB_SRC_ALPHA));
    int component=operand==GPU_TEVOP_RGB_SRC_ALPHA && ch<3?3:ch;
    v[j]=source((sr>>(4*j))&15,component,prev,main,sub,buffer,e->color);
   }
   int f=ch==3?e->funcAlpha:e->funcRgb;
   next[ch]=combine(f,v[0],v[1],v[2]);
   if(boundaryError && st==2 && ch<3 && f==GPU_INTERPOLATE && v[2]==0 && next[ch]>0)next[ch]--;
   next[ch]=clamp(next[ch]<<(ch==3?e->scaleAlpha:e->scaleRgb));
  }
  memcpy(buffer,pending,sizeof(buffer));
  if(bufferMask&(1<<st))for(int ch=0;ch<3;ch++)pending[ch]=next[ch];
  memcpy(prev,next,sizeof(prev));
 }
 return ((prev[0]>>3)<<11)|((prev[1]>>3)<<6)|((prev[2]>>3)<<1)|1;
}
static uint16_t oracle(unsigned i){
 unsigned m=i&31,s=(i>>5)&31,f=i>>12,out[3],mm[]={1,7,13},ss[]={1,11,3};
 for(int c=0;c<3;c++){
  int a=(f&4)?0:(m*mm[c]&31),b=s*ss[c]&31;
  if((i&1024)&&!(f&8)){
   if(f&1){a=a>b?a-b:0;if((f&2)&&(i&2048))a/=2;}
   else if((f&2)&&(i&2048))a=(a+b)/2;
   else a=a+b>31?31:a+b;
  }out[c]=a;
 }
 return (out[0]<<11)|(out[1]<<6)|(out[2]<<1)|1;
}
int main(int argc,char **argv){
 uint16_t raw[65536];bool captured=argc==2;unsigned oldErrors=0;
 if(captured){FILE*f=fopen(argv[1],"rb");assert(f&&fread(raw,2,65536,f)==65536);fclose(f);}
 for(unsigned i=0;i<65536;i++){
  uint16_t expected=oracle(i);assert(render(i,false,false)==expected);
  assert(render(i,true,false)==expected);
  uint16_t old=render(i,true,true);oldErrors+=old!=expected;
  if(captured)assert(raw[i]==old);
 }
 assert(oldErrors==820);
 puts("PASS 65536 real TEV configurations/cases, exact integer and lower-bound sensitivity; E12 reproduces 820 errors; E13 zero");
 if(captured)puts("PASS injected E12 error model matches ALL 65536 physical capture pixels exactly");
}
'''.replace('FUNCTION',s[start:end])
with tempfile.TemporaryDirectory(prefix='alttp-tev-') as t:
 p=Path(t);(p/'test.c').write_text(code)
 subprocess.run(['cc','-O1','-fsanitize=address,undefined','-I/opt/devkitpro/libctru/include',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),*([str(a.e12_capture.resolve())] if a.e12_capture else [])],check=True)
