#!/usr/bin/env python3
"""Compare against immutable E6, without downloading or using game assets."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
p = argparse.ArgumentParser()
p.add_argument('--sanitize', action='store_true')
p.add_argument('--scenes', type=int, default=2048)
p.add_argument('--dumps', nargs='*', default=[], type=Path)
args = p.parse_args()
with tempfile.TemporaryDirectory(prefix='lttp-e7-parity-') as directory:
    tmp = Path(directory)
    reference = subprocess.check_output(
        ['git', 'show', 'c166e5f6e89137db4918897afabbd09cae993c1a:app/jni/src/snes/ppu.c'], cwd=root).decode()
    (tmp / 'reference.c').write_text(reference)
    exports = re.findall(r'^(?:Ppu\*|void|int|uint8_t)\s+((?:ppu_|Ppu)[A-Za-z0-9_]+)\(', reference, re.M)
    common = [os.environ.get('CC', 'cc'), '-std=c11', '-O2' if args.sanitize else '-O3',
              '-fno-strict-aliasing', '-I' + str(root / 'app/jni/src'), '-I' + str(root / 'app/jni/src/snes')]
    # E6 relies on signed bit-plane/Mode 7 shifts. Keep all other UB and
    # address checks enabled, while preserving the reference implementation.
    if args.sanitize:
        common += ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=all', '-fno-sanitize=shift-base']
    subprocess.run(common + ['-D' + n + '=ref_' + n for n in exports] +
                   ['-c', str(tmp / 'reference.c'), '-o', str(tmp / 'reference.o')], check=True)
    subprocess.run(common + [str(root / 'platform/3ds/tests/ppu_old3ds_test.c'),
                   str(root / 'app/jni/src/snes/ppu.c'), str(tmp / 'reference.o'),
                   '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test'), str(args.scenes)] + [str(d.resolve()) for d in args.dumps], check=True)
