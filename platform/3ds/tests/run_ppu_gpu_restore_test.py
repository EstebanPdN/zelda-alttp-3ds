#!/usr/bin/env python3
"""Verify the actual C2D state handoff, including a depth-less display target."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[3]
s=(r/'platform/3ds/source/ppu_gpu.c').read_text()
a=s.index('static void RestoreC2D(void)');b=s.index('\nstatic bool DrawLayers',a)
program=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#define GPU_ALWAYS 1
#define GPU_WRITE_ALL 31
#define GPU_WRITE_COLOR 15
#define GPU_STENCIL_KEEP 0
#define GPU_SCISSOR_DISABLE 0
static struct {int atlas;} g;
static bool depth,prepared,alpha,stencil,secondary=true;
static unsigned mask,tevReset;
static void ResetTev(void) {tevReset++;}
static void C3D_AlphaTest(bool enable,int function,int reference) {alpha=enable;}
static void C3D_DepthTest(bool enable,int function,int write) {depth=enable;mask=write;}
static void C3D_StencilTest(bool enable,int function,int ref,int compare,int write) {stencil=enable;}
static void C3D_StencilOp(int a,int b,int c) {assert(!a&&!b&&!c);}
static void C3D_SetScissor(int mode,int a,int b,int c,int d) {assert(!mode);}
static void PicaC3DUnbindSecondary(void) {secondary=false;}
// Real Citro2D Prepare enables depth; the display targets contain color only.
static void C2D_Prepare(void) {prepared=true;depth=true;mask=GPU_WRITE_ALL;}
FUNCTION
int main(void) {
 depth=alpha=stencil=true;RestoreC2D();
 assert(!secondary&&prepared&&tevReset==1&&!depth&&!alpha&&!stencil&&mask==GPU_WRITE_COLOR);
 puts("PASS GPU/C2D handoff: shader prepare, color-only targets, secondary sampler disabled, alpha/stencil/scissor reset");
}
'''.replace('FUNCTION',s[a:b])
with tempfile.TemporaryDirectory(prefix='alttp-pica-restore-') as t:
 p=Path(t);(p/'test.c').write_text(program)
 subprocess.run(['cc','-O1','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
