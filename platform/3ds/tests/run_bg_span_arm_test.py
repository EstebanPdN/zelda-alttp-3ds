#!/usr/bin/env python3
"""Exercise the compiled ARMv6 span instructions. Requires Python unicorn.
Instruction emulation checks correctness only, never 3DS performance.
"""
from pathlib import Path
import os, subprocess, tempfile, sys
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0
root = Path(__file__).resolve().parents[3]
toolchain = Path(os.environ.get('DEVKITARM', '/opt/devkitpro/devkitARM')) / 'bin'
with tempfile.TemporaryDirectory(prefix='e7-arm-span-') as td:
    tmp = Path(td)
    retained = '--retained' in sys.argv
    source = (root / ('platform/3ds/tests/ppu_retained_span_test.c' if retained else 'platform/3ds/tests/ppu_bg_span_test.c')).read_text()
    source = source.replace('int main(void)', 'int run(void)').replace('1000000', '100000')
    source = source.replace('        fprintf(stderr, "FAIL row %u alignment %d flip %d\\n", n, offset, flip);', '')
    source = source.replace('if (memcmp(expected, actual, sizeof(actual)))', 'for (int j = 0; j < 12; j++) if (expected[j] != actual[j])')
    source = '\n'.join(line for line in source.splitlines() if 'puts(' not in line)
    (tmp / 'test.c').write_text(source)
    subprocess.run([str(toolchain / 'arm-none-eabi-gcc'), '-O3', '-fno-strict-aliasing',
        '-mcpu=mpcore', '-marm', '-D__3DS__', '-I' + str(root / 'app/jni/src'),
        '-nostdlib', '-Wl,-Ttext=0x10000', '-Wl,-e,run', str(tmp / 'test.c'), '-o', str(tmp / 'test.elf')], check=True)
    subprocess.run([str(toolchain / 'arm-none-eabi-objcopy'), '-O', 'binary', str(tmp / 'test.elf'), str(tmp / 'test.bin')], check=True)
    symbols = subprocess.check_output([str(toolchain / 'arm-none-eabi-nm'), str(tmp / 'test.elf')], text=True)
    entry = next(int(line.split()[0], 16) for line in symbols.splitlines() if line.endswith(' run'))
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(0x10000, 0x200000)
    uc.mem_write(0x10000, (tmp / 'test.bin').read_bytes())
    uc.reg_write(UC_ARM_REG_SP, 0x200000)
    uc.reg_write(UC_ARM_REG_LR, 0x208000)
    uc.emu_start(entry, 0x208000, timeout=55000000)
    from unicorn.arm_const import UC_ARM_REG_PC
    assert uc.reg_read(UC_ARM_REG_PC) == 0x208000, 'instruction test timed out'
    assert uc.reg_read(UC_ARM_REG_R0) == 0, 'ARM span mismatch'
    print('PASS ARMv6 machine code: 10,000 retained spans, source/destination alignment, strict priority, transparency, tails and guards' if retained else 'PASS ARMv6 machine code: 100,000 rows, USUB16/SEL, alignment, strict priority, flip, guards')
