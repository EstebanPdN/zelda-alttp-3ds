#!/usr/bin/env python3
"""Regressions found while checking every language.

With a folder of language folders (zelda3_assets.dat, zelda3_language.sfc, as
built on the console) this checks, with the production engine:
- the WIDE item menu draws with the expanded inventory, which moves its panels;
- every dialogue line of the USA-layout translations fits its box;
- the German keyboard and copy-screen tables are read at their own addresses;
- Russian Game Over uses the cartridge's letter order and positions.
No game data belongs in the source tree. The work directory must be outside it.
"""
from pathlib import Path
import argparse, os, shlex, shutil, subprocess, sys

root = Path(__file__).resolve().parents[3]
p = argparse.ArgumentParser()
p.add_argument('--languages', type=Path, required=True)
p.add_argument('--refs', type=Path, required=True, help='reference .sav chapters')
p.add_argument('--work-dir', type=Path, required=True)
a = p.parse_args()
work = a.work_dir.resolve()
if work == root or root in work.parents:
    p.error('Keep private inputs outside tracked source')
work.mkdir(parents=True, exist_ok=True)

for language in ('es', 'pt', 'it', 'ru'):
    subprocess.run([sys.executable, str(root/'tools/languages/quality_text.py'), 'check', language,
                    str(a.languages/language/'zelda3_language.sfc')], check=True)

code = r'''
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#define main ZeldaUnusedUiMain
#include "src/main.c"
#undef main
#include "snes/ppu_ui.h"
#include "src/load_gfx.h"
#include "src/hud.h"
#include "src/localized.h"
#include "src/mods.h"
static uint8 pixels[400 * 240 * 4];
static void Run(int frames, int inputs) { while (frames--) ZeldaRunFrame(inputs); }
static int MenuPixels(void) {
  g_zenv.ppu->wideUi = PpuUiChooseLayout(main_module_index, submodule_index);
  ZeldaDrawPpuFrame(pixels, 400 * 4, g_ppu_render_flags);
  int black = 0;
  for (int y = 20; y < 110; y++) for (int x = 16; x < 230; x++)
    black += !(*(uint32 *)(pixels + (y * 400 + x) * 4) & 0xffffff);
  return black;
}
int main(int argc, char **argv) {
  assert(chdir(argv[1]) == 0);
  ParseConfigFile(NULL); LoadAssets(); LoadLinkGraphics(); ZeldaInitialize();
  ZeldaSetLanguage(g_config.language); ZeldaReadSram();
  g_config.extended_aspect_ratio = 72; g_config.extend_y = true;
  g_config.features0 |= kFeatures0_ExtendScreen64 | kFeatures0_WidescreenVisualFixes;
  g_wanted_zelda_features = g_config.features0;
  g_wanted_zelda_features1 = kFeatures1_QolManaged | kFeatures1_QolInventory;
  g_zenv.ppu->extraLeftRight = 72; g_snes_width = 400; g_snes_height = 240;
  g_ppu_render_flags = kPpuRenderFlags_NewRenderer | kPpuRenderFlags_Height240;
  ZeldaSetWidescreenEdgeMode(1);
  SaveLoadSlot(kSaveLoad_Load, 256 + 11);
  LoadDefaultGraphics(); Hud_Rebuild(); Run(60, 0);
  Run(1, 1 << 3); Run(90, 0);
  assert(main_module_index == 14 && overworld_map_state == 4);
  // The first frame still has the boot's raster split, which clears itself.
  MenuPixels();
  Run(1, 0);
  // The panel's inside is black; without it the room is drawn there.
  assert(MenuPixels() > 10000);
  if (!strcmp(argv[2], "de")) {
    assert(g_loc_rom);
    assert(Loc_Rom(0x8e982c)[0] == 0x06 && Loc_Rom(0x8e982c)[1] == 0x07);
    assert(WORD(Loc_Rom(0x8e9184)[0]) == 0x18e3);
  }
  if (!strcmp(argv[2], "ru")) {
    static const uint8 usa[8] = { 0x58, 0x60, 0x68, 0x70, 0x88, 0x90, 0x98, 0xa0 };
    assert(memcmp(Mods_RomTable(0x88f565, usa, 8), usa, 8) != 0);
  }
  return 0;
}
'''
config = os.environ.get('SDL2_CONFIG', 'sdl2-config')
flags = shlex.split(subprocess.check_output([config, '--cflags', '--libs'], text=True))
sources = [str(x) for x in (root/'engine/src').glob('*.c') if x.name not in ['main.c', 'opengl.c', 'glsl_shader.c']]
sources += [str(x) for x in (root/'engine/src/rando').glob('*.c')]
sources += [str(root/'engine/third_party/sha256/sha256.c')]
sources += [str(x) for x in (root/'engine/snes').glob('*.c')]
sources += [str(root/'engine/src/platform/linux/second_screen_sdl.c'),
            str(root/'engine/third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c')]
(work/'quality.c').write_text(code)
exe = work/'quality'
subprocess.run(['cc', '-DSDL_MAIN_HANDLED', '-DSYSTEM_VOLUME_MIXER_AVAILABLE=0', '-O1', '-g', '-w',
                '-ffunction-sections', '-fdata-sections', '-Wl,-dead_strip',
                '-I'+str(root/'engine'), '-I'+str(root/'platform/3ds/source'), '-iquote', str(root/'engine/src'),
                '-I'+str(root/'engine/third_party/opus-1.3.1-stripped'),
                str(work/'quality.c'), *sources, *flags, '-o', str(exe)], check=True)
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
for folder in sorted(x for x in a.languages.iterdir() if (x/'zelda3_assets.dat').exists()):
    data = work/'data'/folder.name
    shutil.rmtree(data, ignore_errors=True)
    (data/'saves/ref').mkdir(parents=True)
    for source in a.refs.glob('*.sav'):
        shutil.copyfile(source, data/'saves/ref'/source.name)
    for name in ('zelda3_assets.dat', 'zelda3_language.sfc'):
        if (folder/name).exists():
            (data/name).write_bytes((folder/name).read_bytes())
    language = '' if folder.name == 'en' else folder.name
    (data/'zelda3.ini').write_text('[General]\nAutosave = 0\nLanguage = %s\n[Sound]\nEnableAudio = 0\n' % language)
    subprocess.run([str(exe), str(data), folder.name], env=env, check=True, stdout=subprocess.DEVNULL)
    print(folder.name, 'ok')
print('PASS')
