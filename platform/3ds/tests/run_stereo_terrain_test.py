#!/usr/bin/env python3
"""Host test for the stereo terrain solver; no ROM, no 3DS toolchain."""
from pathlib import Path
import subprocess, tempfile
r = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='alttp-terrain-') as t:
    out = Path(t) / 'test'
    subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-I' + str(r / 'app/jni/src'), '-I' + str(r / 'platform/3ds/source'),
                    str(r / 'platform/3ds/tests/stereo_terrain_test.c'), str(r / 'platform/3ds/source/stereo_terrain.c'),
                    '-o', str(out)], check=True)
    subprocess.run([str(out)], check=True)
