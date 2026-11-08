#!/usr/bin/env python3
"""Drive real dungeon/world map open/close fades and compare visible room frames to gameplay presentation."""
from pathlib import Path
import argparse,os,subprocess,shlex,shutil,struct,zlib
root=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--assets',type=Path,required=True);p.add_argument('--work-dir',type=Path,required=True);p.add_argument('--record-only',action='store_true');a=p.parse_args();work=a.work_dir.resolve();work.mkdir(parents=True,exist_ok=True)
assert work != root and root not in work.parents
scene_layout='PpuUiChooseSceneLayout(main_module_index,submodule_index,overworld_map_state)' if 'PpuUiChooseSceneLayout' in (root/'engine/snes/ppu_ui.h').read_text() else 'PpuUiChooseLayout(main_module_index,submodule_index)'
code=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define main ZeldaUnusedUiMain
#include "src/main.c"
#undef main
#include "snes/ppu_ui.h"
uint32_t Platform3DS_GetActiveProfileId(void){return 1234;}
static uint32 actual[400*240],reference[400*240];
static uint8 ram_before[131072];static uint16 vram_before[32768],oam_before[0x120];
static void Render(uint32 *pixels){
 memcpy(ram_before,g_ram,sizeof(ram_before));memcpy(vram_before,g_zenv.ppu->vram,sizeof(vram_before));memcpy(oam_before,g_zenv.ppu->oam,sizeof(g_zenv.ppu->oam));
 ZeldaDrawPpuFrame((uint8*)pixels,400*4,g_ppu_render_flags);
 assert(!memcmp(ram_before,g_ram,sizeof(ram_before))&&!memcmp(vram_before,g_zenv.ppu->vram,sizeof(vram_before))&&!memcmp(oam_before,g_zenv.ppu->oam,sizeof(g_zenv.ppu->oam)));
}
int main(int argc,char**argv){
 setvbuf(stdout,NULL,_IOLBF,0);assert(argc==4);assert(!chdir(argv[1]));ParseConfigFile(NULL);LoadAssets();LoadLinkGraphics();ZeldaInitialize();ZeldaSetLanguage(NULL);ZeldaReadSram();
 g_config.extended_aspect_ratio=72;g_config.extend_y=true;g_config.features0|=kFeatures0_ExtendScreen64|kFeatures0_WidescreenVisualFixes;
 g_wanted_zelda_features=g_config.features0;g_zenv.ppu->extraLeftRight=72;g_snes_width=400;g_snes_height=240;g_ppu_render_flags=kPpuRenderFlags_NewRenderer|kPpuRenderFlags_Height240;ZeldaSetWidescreenEdgeMode(1);
 unsigned failures=0,checked=0,states=0;
 const int scenes[]={1,2,3,13,18,5,12};
 for(unsigned renderer=0;renderer<2;renderer++)for(unsigned scene=0;scene<sizeof(scenes)/sizeof(*scenes);scene++){
  g_ppu_render_flags=kPpuRenderFlags_NewRenderer|kPpuRenderFlags_Height240|(renderer?(kPpuRenderFlags_Old3DS|kPpuRenderFlags_FastPaths):0);
  assert(ZeldaLoadTestScenario(scenes[scene]));for(int f=0;f<30;f++)ZeldaRunFrame(0);
  unsigned gameplay_module=main_module_index;bool world=!player_is_indoors;
  g_zenv.ppu->wideUi=kPpuUiHud;Render(reference);ZeldaRunFrame(1<<9);ZeldaRunFrame(0);
  printf("OPEN scene=%d module=%u/%u palace=%u room=%u frozen=%u blocked=%u joy=%u\n",scenes[scene],main_module_index,submodule_index,cur_palace_index_x2,dungeon_room_index,flag_is_link_immobilized,flag_block_link_menu,filtered_joypad_L);assert(main_module_index==14&&submodule_index==(world?7:3));
  bool closing=false;unsigned stable=0,last_state=999;
  for(int tick=0;tick<500;tick++){
   unsigned phase=overworld_map_state;states|=1u<<phase;
   if(phase!=last_state)printf("SCENE %d TICK %d MAP %u MODULE %u/%u BRIGHT %u\n",scenes[scene],tick,phase,main_module_index,submodule_index,g_zenv.ppu->brightness);
   last_state=phase;
   g_zenv.ppu->wideUi=@LAYOUT@;Render(actual);unsigned repeat=g_zenv.ppu->wideMarginRepeat,ui=g_zenv.ppu->wideUi;
   if(!world&&phase==3)assert(ui==kPpuUiNone&&repeat==1);
   if(world&&phase==5&&(stable==0||stable==20)) {
    assert(g_zenv.ppu->extraLeftCur==72&&g_zenv.ppu->extraRightCur==72);
    if(stable==20)assert(actual[0]==g_zenv.ppu->colorMapRgb[12]&&actual[399]==g_zenv.ppu->colorMapRgb[12]);
    // Opening the map at an area boundary must not inherit its side clipping.
    uint16 left=ow_scroll_vars0.xstart,right=ow_scroll_vars0.xend;
    ow_scroll_vars0.xstart=BG2HOFS_copy2;ow_scroll_vars0.xend=BG2HOFS_copy2;
    Render(reference);assert(!memcmp(actual,reference,sizeof(actual)));
    ow_scroll_vars0.xstart=left;ow_scroll_vars0.xend=right;
    char path[1024];snprintf(path,sizeof(path),"%s/world-r%u-s%d-z%u.raw",argv[2],renderer,scenes[scene],stable);
    FILE*f=fopen(path,"wb");fwrite(actual,4,400*240,f);fclose(f);
   }

   if(main_module_index==14&&(phase==0||(!world&&phase>=7))&&g_zenv.ppu->brightness&&!g_zenv.ppu->forcedBlank){
    unsigned module=main_module_index,sub=submodule_index;main_module_index=saved_module_for_menu;submodule_index=0;
    unsigned gameplay_module=main_module_index;bool world=!player_is_indoors;
  g_zenv.ppu->wideUi=kPpuUiHud;Render(reference);main_module_index=module;submodule_index=sub;
    unsigned diff=0;for(int i=0;i<400*240;i++)diff+=actual[i]!=reference[i];checked++;failures+=diff!=0;
    printf("FADE scene=%d tick=%d phase=%u ui=%u repeat=%u different_pixels=%u\n",scenes[scene],tick,phase,ui,repeat,diff);
    if(tick<15||phase>=7){char path[1024];snprintf(path,sizeof(path),"%s/r%u-s%d-f%03d.raw",argv[2],renderer,scenes[scene],tick);FILE*f=fopen(path,"wb");fwrite(actual,4,400*240,f);fclose(f);}
   }
   if(phase==(world?5:3)&&!closing&&++stable==(world?40:4)){ZeldaRunFrame(world?1:1<<9);closing=true;}else ZeldaRunFrame(world&&phase==5&&stable==10?1<<10:0);
   if(main_module_index==gameplay_module&&submodule_index==0)break;
  }
  assert(closing&&main_module_index==gameplay_module&&submodule_index==0);
 }
 printf("RESULT checked=%u failures=%u states=%x\n",checked,failures,states);assert(checked>160&&(states&0x1ef)==0x1ef);
 if(atoi(argv[3]))assert(!failures);
 ZeldaShutdownSession();return 0;
}
'''.replace('@LAYOUT@',scene_layout)
config=os.environ.get('SDL2_CONFIG','sdl2-config');prefix=subprocess.check_output([config,'--prefix'],text=True).strip();raw=subprocess.check_output([config,'--cflags','--libs'],text=True);flags=shlex.split(raw.replace(prefix,shlex.quote(prefix)))
sources=[str(x)for x in(root/'engine/src').glob('*.c')if x.name not in ['main.c','opengl.c','glsl_shader.c']]+[str(x)for x in(root/'engine/src/rando').glob('*.c')]+[str(x)for x in(root/'engine/snes').glob('*.c')]+[str(root/'engine/third_party/sha256/sha256.c'),str(root/'engine/src/platform/linux/second_screen_sdl.c'),str(root/'engine/third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c')]
(work/'test.c').write_text(code)
with(work/'build.log').open('w')as log:subprocess.run(['cc','-DSDL_MAIN_HANDLED','-DSYSTEM_VOLUME_MIXER_AVAILABLE=0','-DZELDA3_TEST_NATIVE_FILE_MENU','-O1','-g','-w','-fsanitize=address','-ffunction-sections','-fdata-sections','-Wl,-dead_strip','-I'+str(root/'engine'),'-iquote',str(root/'engine/src'),'-I'+str(root/'engine/third_party/opus-1.3.1-stripped'),str(work/'test.c'),*sources,*flags,'-Wl,-rpath,'+prefix+'/lib','-o',str(work/'test')],stdout=log,stderr=subprocess.STDOUT,check=True)
data=work/'data';(data/'saves').mkdir(parents=True,exist_ok=True);shutil.copyfile(a.assets,data/'zelda3_assets.dat');(data/'zelda3.ini').write_text('[General]\nAutosave = 0\n[Sound]\nEnableAudio = 0\n')
r=subprocess.run([str(work/'test'),str(data),str(work),'0'if a.record_only else'1'],env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy'),text=True,capture_output=True);(work/'result.log').write_text(r.stdout+r.stderr);print(r.stdout[-2500:]);
for path in work.glob('*.raw'):
 b=path.read_bytes();rgb=bytearray(400*240*3);rgb[0::3]=b[2::4];rgb[1::3]=b[1::4];rgb[2::3]=b[0::4]
 def chunk(t,d):return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
 scan=b''.join(b'\0'+rgb[y*1200:(y+1)*1200]for y in range(240));path.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',400,240,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b''));path.unlink()
if r.returncode:raise SystemExit(f'FAIL: see {work/"result.log"}')
print('PASS dungeon/world-map transition presentation and render state preservation'if not a.record_only else'BASELINE recorded')
