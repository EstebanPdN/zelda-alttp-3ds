#!/usr/bin/env python3
"""Run the production engine against the E6 Discord regression cases.
Supply private cartridge assets; all temporary state stays outside the checkout.
"""
from pathlib import Path
import argparse, os, shutil, subprocess
from host_test_support import sdl_flags
root = Path(__file__).resolve().parents[3]
p = argparse.ArgumentParser()
p.add_argument('--assets', type=Path, required=True)
p.add_argument('--work-dir', type=Path, required=True)
a = p.parse_args()
work = a.work_dir.resolve()
assert work != root and root not in work.parents
work.mkdir(parents=True, exist_ok=True)
rtl = (root/'engine/src/zelda_rtl.c').read_text().replace('#include "spc_player.h"', '// included by main.c')
code = (Path(__file__).parent/'discord_bug_regressions.c').read_text().replace('@RTL@', rtl)
(work/'test.c').write_text(code)
sources = [str(x) for x in (root/'engine/src').glob('*.c') if x.name not in ['main.c','zelda_rtl.c','opengl.c','glsl_shader.c']]
sources += [str(x) for x in (root/'engine/src/rando').glob('*.c')] + [str(x) for x in (root/'engine/snes').glob('*.c')]
sources += [str(root/'engine'/x) for x in ['third_party/sha256/sha256.c','src/platform/linux/second_screen_sdl.c','third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c']]
with (work/'build.log').open('w') as log:
    subprocess.run(['cc','-DSDL_MAIN_HANDLED','-DSYSTEM_VOLUME_MIXER_AVAILABLE=0','-DZELDA3_TEST_NATIVE_FILE_MENU','-O1','-g','-w','-fsanitize=address,undefined','-fno-sanitize=shift-base,alignment','-ffunction-sections','-fdata-sections','-Wl,-dead_strip','-I'+str(root/'engine'),'-iquote',str(root/'engine/src'),'-I'+str(root/'engine/third_party/opus-1.3.1-stripped'),str(work/'test.c'),*sources,*sdl_flags(),'-o',str(work/'test')],stdout=log,stderr=subprocess.STDOUT,check=True)
data=work/'data';(data/'saves').mkdir(parents=True,exist_ok=True)
shutil.copyfile(a.assets,data/'zelda3_assets.dat')
(data/'zelda3.ini').write_text('[General]\nAutosave=0\n[Sound]\nEnableAudio=0\n')
r=subprocess.run([str(work/'test'),str(data)],env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy'),capture_output=True,text=True)
(work/'result.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr)
raise SystemExit(r.returncode)
