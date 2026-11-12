#!/usr/bin/env python3
"""Build and run the Chinese dialogue text test on the host."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[3]
cc = os.environ.get('CC', 'cc')
with tempfile.TemporaryDirectory(prefix='alttp-cjk-') as tmp:
    exe = str(Path(tmp) / 'dialogue_cjk_test')
    subprocess.run([cc, '-std=c99', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(root / 'platform/3ds/tests/dialogue_cjk_test.c'),
                    str(root / 'engine/src/dialogue_cjk.c'),
                    '-o', exe], check=True)
    subprocess.run([exe], check=True)
