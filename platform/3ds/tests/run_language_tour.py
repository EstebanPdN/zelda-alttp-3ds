#!/usr/bin/env python3
"""Render the game's own interface in each built language.

For every language folder (zelda3_assets.dat built for it on the console,
plus zelda3_language.sfc when the translation keeps the USA layout), loads a
snesrev/zelda3 reference save in WIDE and writes PNGs of the HUD, the item
menu and the file screens (select, copy, erase, name). File screens pass through
title initialization and the production 3DS compositor in Wide and Original.
Review them by eye for
words left in English or drawn wrong.

No game data belongs in the source tree. The work directory must be outside it.
"""
from pathlib import Path
import argparse, os, shlex, shutil, subprocess, zlib, struct

root = Path(__file__).resolve().parents[3]
p = argparse.ArgumentParser()
p.add_argument('--languages', type=Path, required=True,
               help='folder of <code>/ folders holding the built assets')
p.add_argument('--refs', type=Path, required=True, help='reference .sav chapters')
p.add_argument('--work-dir', type=Path, required=True)
p.add_argument('--chapter', type=int, default=11)
p.add_argument('--sanitize', action='store_true', help='enable host AddressSanitizer')
p.add_argument('--export-map-state', action='store_true', help='write a private map save state for isolated native checks')
a = p.parse_args()
work = a.work_dir.resolve()
if work == root or root in work.parents:
    p.error('Keep private inputs outside tracked source')
work.mkdir(parents=True, exist_ok=True)

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
#include "src/sprite.h"
#include "src/native_file_labels.h"
#include "src/localized.h"
#include "file_menu_present.h"
enum { kStart = 1 << 3, kA = 1 << 8, kX = 1 << 9 };
static uint8 pixels[400 * 240 * 4];
static uint8 presented[sizeof(pixels)];
static bool MenuRect(void *ctx, int sx, int sy, int x, int y, int w, int h) {
  assert(sx >= 0 && sy >= 0 && sx+w <= g_snes_width && sy+h <= 240);
  assert(x >= 0 && y >= 0 && x+w <= 400 && y+h <= 240);
  for (int row=0; row<h; row++)
    memcpy(presented+((y+row)*400+x)*4, pixels+((sy+row)*g_snes_width+sx)*4, w*4);
  return true;
}
static void Shot(const char *dir, const char *name) {
  NativeFileLabels_Apply(g_zenv.vram, main_module_index, g_config.language, g_loc_rom != NULL);
  g_zenv.ppu->wideUi = PpuUiChooseLayout(main_module_index, submodule_index);
  ZeldaDrawPpuFrame(pixels, g_snes_width * 4, g_ppu_render_flags);
  char path[512]; snprintf(path, sizeof(path), "%s/%s.raw", dir, name);
  FILE *f = fopen(path, "wb"); assert(f);
  if (main_module_index >= 1 && main_module_index <= 4) {
    // Verify the actual PPU input before composing the native 3DS layout:
    // a partial title initialization used to leave only a patterned corner.
    int ox=(g_snes_width-256)/2;
    bool textured=false;
    for (int y=0; y<16; y++) for (int x=0; x<16; x++)
      textured |= memcmp(pixels+(y*g_snes_width+ox+x)*4,pixels+ox*4,4) != 0;
    assert(textured || !strcmp(g_config.language ? g_config.language : "", "ja"));
    // The Japanese cartridge's background is a different repeating pattern.
    for (int y=0; y<224 && strcmp(g_config.language ? g_config.language : "", "ja"); y++) for (int x=0; x<16; x++)
      assert(!memcmp(pixels+(y*g_snes_width+ox+x)*4,
                     pixels+((y%16)*g_snes_width+ox+x)*4,4));
    bool drawn = g_snes_width == 400 &&
      FileMenu_DrawWide(g_snes_width,240,main_module_index,FileMenu_TitleWidth(g_zenv.vram),MenuRect,NULL);
    if (!drawn) assert(FileMenu_DrawBackground(g_snes_width,240,MenuRect,NULL));
    fwrite(presented,4,400*240,f);
  } else fwrite(pixels, 4, g_snes_width * 240, f);
  fclose(f);
}
static void FileShot(const char *dir, const char *name) {
  // Naming changes BG3 scroll at line 128. Each comparison is a fresh draw
  // of the same frame, so restore the pre-IRQ scroll as the next NMI does.
  unsigned bg3_scroll=g_zenv.ppu->bgLayer[2].hScroll;
  Shot(dir,name);
  static uint8 wide_menu[sizeof(presented)];
  memcpy(wide_menu,presented,sizeof(presented));
  g_ppu_render_flags |= kPpuRenderFlags_Old3DS | kPpuRenderFlags_FastPaths;
  char old[80]; snprintf(old,sizeof(old),"%s-old3ds",name);
  g_zenv.ppu->bgLayer[2].hScroll=bg3_scroll;
  Shot(dir,old);
  assert(!memcmp(wide_menu,presented,sizeof(presented)));
  g_ppu_render_flags &= ~(kPpuRenderFlags_Old3DS | kPpuRenderFlags_FastPaths);
  g_zenv.ppu->extraLeftRight=0; g_snes_width=256;
  char original[80]; snprintf(original,sizeof(original),"%s-original",name);
  g_zenv.ppu->bgLayer[2].hScroll=bg3_scroll;
  Shot(dir,original);
  g_zenv.ppu->bgLayer[2].hScroll=bg3_scroll;
  g_zenv.ppu->extraLeftRight=72; g_snes_width=400;
}
static void Run(int frames, int inputs) { while (frames--) ZeldaRunFrame(inputs); }
static void Module(int module, int frames) {
  main_module_index = module; submodule_index = 0; subsubmodule_index = 0;
  Run(frames, 0);
}
int main(int argc, char **argv) {
  assert(argc == 5);
  assert(chdir(argv[1]) == 0);
  ParseConfigFile(NULL); LoadAssets(); LoadLinkGraphics(); ZeldaInitialize();
  ZeldaSetLanguage(g_config.language); ZeldaReadSram();
  g_config.extended_aspect_ratio = 72; g_config.extend_y = true;
  g_config.features0 |= kFeatures0_ExtendScreen64 | kFeatures0_WidescreenVisualFixes;
  g_wanted_zelda_features = g_config.features0;
  g_zenv.ppu->extraLeftRight = 72; g_snes_width = 400; g_snes_height = 240;
  g_ppu_render_flags = kPpuRenderFlags_NewRenderer | kPpuRenderFlags_Height240;
  ZeldaSetWidescreenEdgeMode(1);
  SaveLoadSlot(kSaveLoad_Load, 256 + atoi(argv[3]));
  // The snapshot's VRAM holds the USA lettering: load the HUD graphics
  // again from the language's assets and rebuild the HUD, as starting a
  // game does.
  LoadDefaultGraphics();
  Hud_Rebuild();
  Run(90, 0);
  Shot(argv[2], "1-hud");
  Run(1, kStart); Run(70, 0);
  Shot(argv[2], "2-menu");
  Run(1, kStart); Run(70, 0);
  // File screens are entered from the title, which initializes BG map sizes
  // and scroll registers. Jumping here directly from gameplay leaves stale
  // room registers and produces invalid screenshots with missing frames.
  ZeldaReset(true);
  for (int frame=0; frame<2000 && !(main_module_index==0 && submodule_index==8); frame++)
    Run(1,0);
  assert(main_module_index==0 && submodule_index==8);
  Module(1, 90);  FileShot(argv[2], "3-select");
  Module(2, 90);  FileShot(argv[2], "4-copy");
  Run(1, 1<<8); Run(40,0); FileShot(argv[2], "4b-copy-target");
  Run(1, 1<<8); Run(40,0); FileShot(argv[2], "4c-copy-confirm");
  Module(3, 90);  FileShot(argv[2], "5-erase");
  Run(1, 1<<8); Run(40,0); FileShot(argv[2], "5b-erase-confirm");
  Module(4, 120); FileShot(argv[2], "6-name");
  // A dungeon's map: its floor names (Eastern Palace, X opens the map).
  SaveLoadSlot(kSaveLoad_Load, 256 + atoi(argv[3]));
  LoadDefaultGraphics(); Hud_Rebuild();
  which_entrance = 8; Module(6, 150);
  // A pending name-keyboard IRQ used to shift BG3 at scanline 128 in the
  // first map capture. Exercise that real entry path deliberately.
  irq_flag = 0xff; selectfile_var8 = 32;
  Run(1, kX); Run(150, 0);
  assert(main_module_index == 14 && submodule_index == 3 && overworld_map_state == 3);
  assert(irq_flag == 0);
  Shot(argv[2], "7-dungeon-map");
  if (atoi(argv[4])) SaveLoadSlot(kSaveLoad_Save,0);
  static uint8 map_wide[sizeof(pixels)];
  memcpy(map_wide, pixels, sizeof(pixels));
  // A second draw of this same frame must not move the lower half.
  Shot(argv[2], "7-dungeon-map-repeat");
  assert(!memcmp(map_wide, pixels, sizeof(pixels)));
  // Wide adds only backdrop: floor grids, borders and markers must match
  // the native picture exactly. Every margin follows its own 16px phase.
  for (int y = 0; y < 240; y++) {
    for (int x = 0; x < 72; x++)
      assert(!memcmp(pixels+(y*400+x)*4, pixels+(y*400+x+16)*4,4));
    for (int x = 328; x < 400; x++)
      assert(!memcmp(pixels+(y*400+x)*4, pixels+(y*400+x-16)*4,4));
  }
  g_zenv.ppu->extraLeftRight = 0; g_snes_width = 256;
  Shot(argv[2], "7-dungeon-map-original");
  for (int y = 0; y < 240; y++)
    assert(!memcmp(map_wide+(y*400+72)*4, pixels+y*256*4, 256*4));
  g_zenv.ppu->extraLeftRight = 72; g_snes_width = 400;
  g_ppu_render_flags |= kPpuRenderFlags_Old3DS | kPpuRenderFlags_FastPaths;
  Shot(argv[2], "7-dungeon-map-old3ds");
  assert(!memcmp(map_wide,pixels,sizeof(pixels)));
  g_ppu_render_flags &= ~(kPpuRenderFlags_Old3DS | kPpuRenderFlags_FastPaths);
  Run(1, 1<<4); Run(60,0); Shot(argv[2], "7-dungeon-map-floor-up");
  Run(1, 1<<5); Run(60,0); Shot(argv[2], "7-dungeon-map-floor-down");
  Run(1, kX); Run(60, 0);
  assert(main_module_index == 7 && submodule_index == 0);
  // Real death animation through all three choice positions (#38).
  link_bottle_info[0]=link_bottle_info[1]=link_bottle_info[2]=link_bottle_info[3]=0;
  main_module_index=18; submodule_index=0; subsubmodule_index=0;
  for(int n=0;n<2000 && submodule_index!=9;n++) Run(1,0);
  assert(main_module_index==18 && submodule_index==9);
  Run(20,0);
  for(int choice=0;choice<3;choice++) {
    subsubmodule_index=choice;Run(1,0);
    assert(oam_buf[0x14].y==(uint8)(0x7f+16*choice-g_wide_picture_rows));
    char name[64];snprintf(name,sizeof(name),"7-game-over-choice-%d",choice);Shot(argv[2],name);
  }
  // The intro's legend and the credits draw text of their own.
  // Attract state is separate from submodule_index; the save can contain
  // an arbitrary old value. Enter through its graphics initialization.
  ZeldaReset(true);
  for (int frame = 0; frame < 2000 && !(main_module_index == 0 && submodule_index == 8); frame++)
    Run(1, 0);
  assert(main_module_index == 0 && submodule_index == 8);
  attract_state = 0; attract_sequence = 0;
  Module(0x14, 700);
  assert(main_module_index == 0x14 && attract_state == 5);
  Shot(argv[2], "8-attract-a");
  Run(500, 0); Shot(argv[2], "8-attract-b");
  SaveLoadSlot(kSaveLoad_Load, 256 + atoi(argv[3]));
  LoadDefaultGraphics(); Hud_Rebuild();
  Module(0x1a, 1);
  unsigned scenes = 0;
  int previous = -1, elapsed = 0, next_credit_line = 32;
  for (int frame = 0; frame < 90000 && submodule_index != 38; frame++) {
    Run(1, 0);
    assert(main_module_index == 0x1a);
    if (submodule_index != previous) {
      previous = submodule_index;
      elapsed = 0;
    }
    if (++elapsed == 180 && submodule_index < 32 && (submodule_index & 1)) {
      char name[40];
      int scene = submodule_index / 2;
      snprintf(name, sizeof(name), "9-ending-%02d", scene);
      Shot(argv[2], name);
      scenes |= 1u << scene;
    }
    if (submodule_index == 34 && R18 >= next_credit_line) {
      char name[40];
      snprintf(name, sizeof(name), "9-staff-%03d", next_credit_line);
      Shot(argv[2], name);
      next_credit_line += 32;
    }
  }
  assert(scenes == 0xffff && submodule_index == 38);
  Shot(argv[2], "9-the-end");
  ZeldaShutdownSession(); return 0;
}
'''
config = os.environ.get('SDL2_CONFIG', 'sdl2-config')
prefix = subprocess.check_output([config, '--prefix'], text=True).strip()
raw = subprocess.check_output([config, '--cflags', '--libs'], text=True)
flags = shlex.split(raw.replace(prefix, shlex.quote(prefix)))
sources = [str(x) for x in (root/'engine/src').glob('*.c') if x.name not in ['main.c', 'opengl.c', 'glsl_shader.c']]
sources += [str(x) for x in (root/'engine/src/rando').glob('*.c')]
sources += [str(root/'engine/third_party/sha256/sha256.c')]
sources += [str(x) for x in (root/'engine/snes').glob('*.c')]
sources += [str(root/'engine/src/platform/linux/second_screen_sdl.c'),
            str(root/'engine/third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c')]
(work/'tour.c').write_text(code)
exe = work/'language-tour'
with (work/'build.log').open('w') as log:
    subprocess.run(['cc', '-DSDL_MAIN_HANDLED', '-DSYSTEM_VOLUME_MIXER_AVAILABLE=0', '-O1', '-g', '-w',
                    '-ffunction-sections', '-fdata-sections', '-Wl,-dead_strip',
                    '-I'+str(root/'engine'), '-I'+str(root/'platform/3ds/source'), '-iquote', str(root/'engine/src'),
                    '-I'+str(root/'engine/third_party/opus-1.3.1-stripped'),
                    str(work/'tour.c'), *sources, *(['-fsanitize=address', '-fno-omit-frame-pointer'] if a.sanitize else []), *flags, '-Wl,-rpath,'+prefix+'/lib', '-o', str(exe)],
                   stdout=log, stderr=subprocess.STDOUT, check=True)


def png(path, rgb, w, h):
    rows = b''.join(b'\0' + bytes(rgb[y*w*3:(y+1)*w*3]) for y in range(h))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
failures = 0
for folder in sorted(x for x in a.languages.iterdir() if (x/'zelda3_assets.dat').exists()):
    lang = folder.name
    data = work/'data'/lang
    shutil.rmtree(data, ignore_errors=True)
    (data/'saves/ref').mkdir(parents=True)
    for f in ('zelda3_assets.dat', 'zelda3_language.sfc', 'zelda3_localized.sfc'):
        if (folder/f).exists():
            shutil.copyfile(folder/f, data/f)
    for source in a.refs.glob('*.sav'):
        shutil.copyfile(source, data/'saves/ref'/source.name)
    language = '' if lang == 'en' else lang
    (data/'zelda3.ini').write_text('[General]\nAutosave = 0\nLanguage = %s\n[Sound]\nEnableAudio = 0\n' % language)
    out = work/'frames'/lang
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    log = subprocess.run([str(exe), str(data), str(out), str(a.chapter), str(int(a.export_map_state))], env=env, text=True,
                         capture_output=True)
    (out/'tour.log').write_text(log.stdout + log.stderr)
    failed = log.returncode != 0
    if failed:
        print(f'{lang}: FAILED ({log.returncode}) after {len(list(out.glob("*.raw")))} screens; see {out/"tour.log"}')
    for raw_path in sorted(out.glob('*.raw')):
        b = raw_path.read_bytes(); width = len(b) // (240 * 4)
        assert width in (256,400) and len(b) == width * 240 * 4
        rgb = bytearray(width * 240 * 3)
        rgb[0::3] = b[2::4]; rgb[1::3] = b[1::4]; rgb[2::3] = b[0::4]
        png(raw_path.with_suffix('.png'), rgb, width, 240); raw_path.unlink()
    if not failed:
        print(lang, 'ok')
    failures += failed
print(('FAIL %d languages; ' % failures if failures else 'PASS ') + 'frames in ' + str(work/'frames'))
raise SystemExit(1 if failures else 0)
