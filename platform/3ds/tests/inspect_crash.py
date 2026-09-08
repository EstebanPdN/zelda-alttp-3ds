#!/usr/bin/env python3
"""Match a local Luma ARM11 dump to an exact ELF before symbolizing it.
Header/register layout: LumaTeam/luma3ds_exception_dump_parser v1.4.0.
Never sends the dump to a service. Requires devkitARM for symbol names only.
"""
import argparse, hashlib, json, os, struct, subprocess
from pathlib import Path

def inspect(dump_path, elf_path):
    dump, elf = dump_path.read_bytes(), elf_path.read_bytes()
    if len(dump) < 40 or struct.unpack_from('<2I', dump) != (0xdeadc0de, 0xdeadcafe):
        raise ValueError('Not a Luma exception dump')
    version, processor, exception, total, regs_size, code_size, stack_size, extra_size = struct.unpack_from('<8I', dump, 8)
    if version < 0x10002 or regs_size < 80 or regs_size % 4 or total != len(dump) or 40+regs_size+code_size+stack_size+extra_size != total:
        raise ValueError('Unsupported/truncated dump')
    regs = struct.unpack_from('<20I', dump, 40)
    pc, sp, lr = regs[15], regs[13], regs[14]
    thumb = bool(regs[16] & 0x20)
    code_start = pc-code_size+(2 if thumb else 4)
    code = dump[40+regs_size:40+regs_size+code_size]
    if elf[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('Expected a little-endian ELF32')
    phoff = struct.unpack_from('<I', elf, 28)[0]
    phsize, phcount = struct.unpack_from('<HH', elf, 42)
    match = False
    for i in range(phcount):
        typ, offset, addr, _, size = struct.unpack_from('<5I', elf, phoff+i*phsize)
        if typ == 1 and addr <= code_start and code_start+code_size <= addr+size:
            start = offset+code_start-addr
            match = elf[start:start+code_size] == code
    result = dict(dump_sha256=hashlib.sha256(dump).hexdigest(), elf_sha256=hashlib.sha256(elf).hexdigest(),
        processor=processor & 0xffff, core=processor >> 16, exception=exception,
        pc=f'{pc:#010x}', lr=f'{lr:#010x}', sp=f'{sp:#010x}', far=f'{regs[19]:#010x}',
        dfsr=f'{regs[17]:#x}', write=bool(regs[17] & 0x800),
        captured_stack_bytes=stack_size, compared_code_bytes=code_size, exact_code_match=match)
    if match:
        tool=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-addr2line'
        result['symbols']=subprocess.check_output([str(tool),'-e',str(elf_path),'-f','-i',hex(pc),hex(lr)],text=True).splitlines()
    else:
        result['symbols']='Not resolved: captured instructions do not match this ELF'
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump',type=Path);parser.add_argument('elf',type=Path)
    args=parser.parse_args()
    print(json.dumps(inspect(args.dump,args.elf),indent=2))
