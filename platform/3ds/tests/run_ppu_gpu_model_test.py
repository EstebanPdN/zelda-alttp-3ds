#!/usr/bin/env python3
"""ROM-free differential geometry/atlas test; hardware is checked at startup."""
from pathlib import Path
import argparse,subprocess,tempfile
r=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--scenes',type=int,default=4096)
p.add_argument('--general-window-reference',action='store_true');p.add_argument('--dumps',nargs='*',type=Path,default=[]);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='alttp-pica-parity-') as t:
 out=Path(t)/'test'
 model=r/'platform/3ds/source/ppu_gpu_model.c'
 if a.general_window_reference:
  copy=Path(t)/'general.c';copy.write_text(model.read_text().replace('f->sharedWindow=SharedBlackWindow(f);','f->sharedWindow=false;'));model=copy
 subprocess.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined',
  '-fno-sanitize=shift-base','-fno-sanitize-recover=all',
  '-I'+str(r/'app/jni/src'),'-I'+str(r/'platform/3ds/source'),
  str(r/'platform/3ds/tests/ppu_gpu_model_test.c'),str(model),
  str(r/'app/jni/src/snes/ppu.c'),'-o',str(out)],check=True)
 subprocess.run([str(out),str(a.scenes),*[str(d.resolve()) for d in a.dumps]],check=True)
