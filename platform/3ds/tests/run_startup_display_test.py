#!/usr/bin/env python3
"""Exercise actual LCD gate and handoff functions with a strict display model."""
from pathlib import Path
import tempfile,subprocess
r=Path(__file__).resolve().parents[3];s=(r/'platform/3ds/source/platform_3ds.c').read_text()
def fn(sig):
 a=s.index(sig);i=s.index('{',a)+1;n=1
 while n:n+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]+'\n'
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t u8;typedef int Result;
static bool g_startup_lcd_black=true,g_setup_console_active,g_gpu_presenter_initialized,g_gpu_frame_active;
static unsigned valid,setupPresents,reveals,waits;
static bool black=true;
static int g_top_target=1,g_bottom_target=2;
static uint16_t g_setup_top_pixels[400*240],g_setup_bottom_pixels[320*240];
enum {GFX_TOP,GFX_BOTTOM,GSP_RGB565_OES};
Result __real_GSPGPU_SetLcdForceBlack(u8 flags){if(!flags){assert(valid==3);reveals++;}black=flags!=0;return 0;}
WRAPPER
#define GSPGPU_SetLcdForceBlack __wrap_GSPGPU_SetLcdForceBlack
static void gspWaitForVBlank(void){waits++;}
REVEAL
static void gfxInitDefault(void){valid=0;setupPresents=0;GSPGPU_SetLcdForceBlack(0);assert(black);}
static void gfxSetScreenFormat(int s,int f){}
static void aptSetHomeAllowed(bool b){}
static void aptSetSleepAllowed(bool b){}
static void consoleInit(int s,void *p){}
static void consoleClear(void){}
static void PresentSetupConsole(void){if(++setupPresents>=2)valid=3;}
static void SetupAudioStop(void){}
static void gfxExit(void){GSPGPU_SetLcdForceBlack(1);valid=0;}
static bool C3D_FrameBegin(int flags){return true;}
static void Platform3DS_ClearBlackTarget(int target){valid|=target;}
static void C2D_SceneBegin(int target){}
static void Platform3DS_EndGpuFrame(void){}
BEGIN
END
BLANK
FRAME
int main(void){
 for(int session=0;session<20;session++) {
  g_gpu_presenter_initialized=false;BeginSetupConsole();assert(!black&&valid==3);
  EndSetupConsole();assert(black&&g_startup_lcd_black);
  gfxInitDefault(); // SDL gfxInit's premature unmask must be suppressed.
  assert(black);g_gpu_presenter_initialized=true;
  Platform3DS_BlankScreens();assert(black&&valid==3);
  unsigned before=waits;g_gpu_frame_active=true;Platform3DS_EndFrame();assert(!black&&waits==before+1);
  before=waits;g_gpu_frame_active=true;Platform3DS_EndFrame();assert(waits==before);
  gfxExit();
 }
 assert(reveals==40);puts("PASS 20 selector/SDL/probe/game handoffs: no uninitialized reveal; one startup wait, no added gameplay wait");
}
'''
for marker,sig in [('WRAPPER','Result __wrap_GSPGPU_SetLcdForceBlack('),('REVEAL','static void RevealInitializedScreens('),('BEGIN','static void BeginSetupConsole('),('END','static void EndSetupConsole('),('BLANK','void Platform3DS_BlankScreens('),('FRAME','void Platform3DS_EndFrame(')]:code=code.replace(marker,fn(sig))
assert '--wrap=GSPGPU_SetLcdForceBlack' in (r/'platform/3ds/CMakeLists.txt').read_text()
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.c').write_text(code)
 subprocess.run(['cc','-O1','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
