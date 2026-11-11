#!/usr/bin/env python3
"""Play the item menu in the real engine in every supplied language.

SELECT in the menu must reach the save box and return to the game, also on the
cartridges whose menu scrolls 10 lines a frame. Moving a held button's ring from
an empty slot must end whatever items Link has.
Private assets stay outside source.
"""
from pathlib import Path
import argparse, os, shlex, shutil, struct, subprocess, zlib

ROOT = Path(__file__).resolve().parents[3]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--languages', type=Path, required=True)
p.add_argument('--work-dir', type=Path, required=True)
a = p.parse_args()
work = a.work_dir.resolve()
if work == ROOT or ROOT in work.parents:
    p.error('Keep private inputs outside source')
work.mkdir(parents=True, exist_ok=True)
code = r'''
#include <assert.h>
#include <unistd.h>
#define main ZeldaUnusedItemMenuMain
#include "src/main.c"
#undef main
#include "src/test_scenarios.h"
#include "src/hud.h"
#include "snes/ppu_ui.h"
uint32_t Platform3DS_GetActiveProfileId(void){return 1234;}
enum { kB=1, kSelect=4, kStart=8, kUp=16, kDown=32, kLeft=64, kRight=128, kA=256, kX=512, kL=1024, kR=2048 };
static uint8 pixels[400*240*4];
static void Run(int n,int keys){while(n--){ZeldaRunFrame(keys);g_zenv.ppu->wideUi=PpuUiChooseSceneLayout(main_module_index,submodule_index,overworld_map_state);ZeldaDrawPpuFrame(pixels,400*4,kPpuRenderFlags_NewRenderer|kPpuRenderFlags_Height240);}}
static void Shot(const char *name){char path[64];snprintf(path,sizeof(path),"%s.raw",name);FILE *f=fopen(path,"wb");assert(f);fwrite(pixels,1,sizeof(pixels),f);fclose(f);}
static void OpenMenu(void){
  Run(1,kStart);
  for(int i=0;i<60&&!(main_module_index==14&&overworld_map_state==4);i++)Run(1,0);
  assert(main_module_index==14&&submodule_index==1&&overworld_map_state==4);
}
static void CloseMenu(void){
  Run(1,kStart);
  for(int i=0;i<60&&main_module_index==14;i++)Run(1,0);
  assert(main_module_index==9&&submodule_index==0&&BG3VOFS_copy2==0);
}
int main(int argc,char **argv){
  assert(argc>=2&&!chdir(argv[1]));
  const char *mode=argc>2?argv[2]:"";
  ParseConfigFile(NULL);LoadAssets();LoadLinkGraphics();ZeldaInitialize();ZeldaSetLanguage(g_config.language);
  g_zenv.ppu->extraLeftRight=72;
  g_wanted_zelda_features=g_config.features0|=kFeatures0_SwitchLR;
  assert(ZeldaLoadTestScenario(32));
  for(int i=0;i<900&&!(main_module_index==9&&submodule_index==0);i++)Run(1,0);
  assert(main_module_index==9&&submodule_index==0);

  if(!strcmp(mode,"load")){
    // States of another language: gameplay, then the open save box.
    sram_progress_indicator=2;
    SaveLoadSlot(kSaveLoad_Load,2);Run(2,0);assert(main_module_index==9);Shot("5-loaded-game");
    SaveLoadSlot(kSaveLoad_Load,3);Run(90,0);assert(main_module_index==14&&submodule_index==11&&saved_module_for_menu==9);Shot("6-loaded-box");
    assert(!memcmp(&g_zenv.vram[word_7E0219],hud_tile_indices_buffer,165*2));
    for(int i=0;i<600&&main_module_index==14;i++)Run(1,i&1?kA:0);
    assert(main_module_index==9&&submodule_index==0);
    puts("OK states of another language");ZeldaShutdownSession();return 0;
  }
  if(!strcmp(mode,"save")){
    sram_progress_indicator=2;SaveLoadSlot(kSaveLoad_Save,2);
    OpenMenu();for(int i=0;i<4&&submodule_index!=11;i++)Run(1,kSelect);Run(90,0);
    assert(submodule_index==11);SaveLoadSlot(kSaveLoad_Save,3);
    ZeldaShutdownSession();return 0;
  }
  Shot("1-game");
  // SELECT: the save box opens over the game, and answering returns to it.
  OpenMenu();
  Shot("2-menu");
  sram_progress_indicator=sram_progress_indicator?sram_progress_indicator:2;
  for(int i=0;i<4&&submodule_index!=11;i++)Run(1,kSelect);
  assert(main_module_index==14&&submodule_index==11&&saved_module_for_menu==9);
  assert(PpuUiChooseLayout(main_module_index,submodule_index)==kPpuUiSave);
  Run(90,0);Shot("3-box");
  Run(1,0);
  for(int i=0;i<600&&main_module_index==14;i++)Run(1,i&1?kA:0);
  assert(main_module_index==9&&submodule_index==0&&overworld_map_state==0);
  Run(30,0);Shot("4-after");
  uint16 x=link_x_coord,y=link_y_coord;
  Run(8,kLeft);Run(8,kDown);Run(20,0);
  assert(link_x_coord!=x||link_y_coord!=y);
  assert(main_module_index==9&&submodule_index==0);
  puts("OK select returns to the game");

  // A ring moved up from an empty slot, owning nothing in its column.
  enhanced_features1|=kFeatures1_QolInventory;enhanced_features0|=kFeatures0_SwitchLR;
  g_wanted_zelda_features1=enhanced_features1;g_wanted_zelda_features=enhanced_features0;
  memset(&link_item_bow,0,20);memset(link_bottle_info,0,4);memset(hud_inventory_order,0,24);
  (&link_item_bow)[10]=1;hud_cur_item=11;hud_cur_item_l=hud_cur_item_r=hud_cur_item_x=0;
  static const int rings[]={kL,kR,kX};
  uint8 *slots[]={&hud_cur_item_l,&hud_cur_item_r,&hud_cur_item_x};
  for(int i=0;i<3;i++){
    static const int moves[]={kUp,kDown,kLeft,kRight};
    for(int m=0;m<4;m++){
      *slots[i]=0;
      OpenMenu();
      alarm(20);
      Run(1,rings[i]);Run(1,rings[i]|moves[m]);
      alarm(0);
      assert(*slots[i]==11||*slots[i]==0);
      Run(1,0);CloseMenu();
    }
  }
  puts("OK rings end from an empty slot");
  // Nothing owned at all but an assigned item that has gone: the ring gives up.
  (&link_item_bow)[10]=0;hud_cur_item=0;link_item_bombs=1;hud_cur_item_l=11;
  OpenMenu();
  alarm(20);
  Run(1,kL);Run(1,kL|kUp);
  alarm(0);
  Run(1,0);CloseMenu();
  puts("OK ring ends from an item no longer owned");
  ZeldaShutdownSession();return 0;
}
'''
config = os.environ.get('SDL2_CONFIG', 'sdl2-config')
prefix = subprocess.check_output([config, '--prefix'], text=True).strip()
flags = shlex.split(subprocess.check_output([config, '--cflags', '--libs'], text=True).replace(prefix, shlex.quote(prefix)))
sources = [str(x) for x in (ROOT/'engine/src').glob('*.c') if x.name not in ['main.c', 'opengl.c', 'glsl_shader.c']]
sources += [str(x) for x in (ROOT/'engine/src/rando').glob('*.c')]
sources += [str(ROOT/'engine/third_party/sha256/sha256.c')]
sources += [str(x) for x in (ROOT/'engine/snes').glob('*.c')]
sources += [str(ROOT/'engine/src/platform/linux/second_screen_sdl.c'), str(ROOT/'engine/third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c')]
(work/'item_menu.c').write_text(code)
exe = work/'item-menu'
with (work/'build.log').open('w') as log:
    subprocess.run(['cc', '-DSDL_MAIN_HANDLED', '-DSYSTEM_VOLUME_MIXER_AVAILABLE=0', '-O1', '-g', '-w', '-DZELDA3_TEST_DUMP_STATE',
                    '-ffunction-sections', '-fdata-sections', '-Wl,-dead_strip',
                    '-I'+str(ROOT/'engine'), '-iquote', str(ROOT/'engine/src'),
                    '-I'+str(ROOT/'engine/third_party/opus-1.3.1-stripped'),
                    str(work/'item_menu.c'), *sources, *flags, '-Wl,-rpath,'+prefix+'/lib', '-o', str(exe)],
                   stdout=log, stderr=subprocess.STDOUT, check=True)
def png(path, rgb, w, h):
    rows = b''.join(b'\0' + bytes(rgb[y*w*3:(y+1)*w*3]) for y in range(h))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))

env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
failed = []
source_states = work/'data'/'en'/'saves'
for folder in sorted(a.languages.iterdir(), key=lambda f: (f.name != 'en', f.name)):
    if not (folder/'zelda3_assets.dat').exists():
        continue
    lang = folder.name
    data = work/'data'/lang
    (data/'saves').mkdir(parents=True, exist_ok=True)
    for name in ['zelda3_assets.dat', 'zelda3_language.sfc', 'zelda3_localized.sfc']:
        if (folder/name).exists():
            shutil.copyfile(folder/name, data/name)
    (data/'zelda3.ini').write_text('[General]\nAutosave = 0\nLanguage = '+('' if lang == 'en' else lang)+'\n[Sound]\nEnableAudio = 0\n')
    run = subprocess.run([str(exe), str(data)], env=env, text=True, capture_output=True, cwd=None)
    (work/(lang+'.log')).write_text(run.stdout+run.stderr)
    ok = run.returncode == 0
    if lang == 'en':
        ok = ok and subprocess.run([str(exe), str(data), 'save'], env=env, capture_output=True).returncode == 0
    for slot in ('save2.sav', 'save3.sav'):
        if lang != 'en' and (source_states/slot).exists():
            shutil.copyfile(source_states/slot, data/'saves'/slot)
    if lang != 'en' and (source_states/'save3.sav').exists():
        loaded = subprocess.run([str(exe), str(data), 'load'], env=env, text=True, capture_output=True)
        (work/(lang+'-load.log')).write_text(loaded.stdout+loaded.stderr)
        ok = ok and loaded.returncode == 0
        run = loaded if loaded.returncode else run
    for raw in sorted(data.glob('*.raw')):
        b = raw.read_bytes(); rgb = bytearray(400*240*3); rgb[0::3] = b[2::4]; rgb[1::3] = b[1::4]; rgb[2::3] = b[0::4]
        png(work/f'{lang}-{raw.stem}.png', rgb, 400, 240); raw.unlink()
    last = (run.stdout+run.stderr).strip().splitlines()[-1:] or ['']
    print(lang, 'PASS' if ok else f'FAIL ({run.returncode}): {last[0]}', flush=True)
    if not ok:
        failed.append(lang)
if failed:
    raise SystemExit('FAILED: '+' '.join(failed))
print('PASS item menu in every supplied language')
