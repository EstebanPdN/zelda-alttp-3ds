#!/usr/bin/env python3
"""Make the Chinese language patch from the prebuilt language pack.

The other languages are ROM patches: a BPS that turns the USA cartridge into a
translated one, whose text the console then reads. Chinese is not a ROM hack -
its text is glyph indices plus a 16x16 font, which the cartridge's own text
engine cannot draw - so the cartridge it needs is the USA ROM with the finished
pack appended:

    0 .. 0x100000    the USA cartridge, untouched
    then             the pack ('ZCNB', dialogue block, font block)
    then             'ZCNP' + u32 pack size

The port never runs that cartridge: it reads the pack back out of it (see
CartridgePack in platform/3ds/source/platform_3ds.c), which is the same data a
pack-only build ships in romfs. That makes the Chinese translation a patch the
language selector can carry, like every other language.

The patch is one SourceRead over the cartridge plus one TargetRead of the
appended bytes, so it contains no ROM data and is barely larger than the pack.
Its header records the USA cartridge's CRC32 (0x777aac2f, the source the six
upstream language patches were made against), so the console refuses it for any
other ROM.

Usage:
  python3 make_zh_bps.py platform/3ds/romfs/languages/zh.bin zh.bps
  python3 make_zh_bps.py zh.bin zh.bps --rom zelda3.sfc    # apply and verify

--rom checks the patch against a local USA cartridge: its CRC32 must be the
recorded one, and applying the patch must reproduce the appended tail.
"""
import struct
import sys
import zlib
from pathlib import Path

# The headerless USA cartridge, as recorded by every upstream language patch.
SOURCE_SIZE = 1048576
SOURCE_CRC = 0x777AAC2F

FOOTER_MAGIC = b'ZCNP'
FOOTER_SIZE = 8


def varint(n):
    out = bytearray()
    while True:
        x = n & 0x7f
        n >>= 7
        if not n:
            out.append(0x80 | x)
            return bytes(out)
        out.append(x)
        n -= 1


def read_varint(data, p):
    value, shift = 0, 1
    while True:
        x = data[p]
        p += 1
        value += (x & 0x7f) * shift
        if x & 0x80:
            return value, p
        shift <<= 7
        value += shift


def cartridge_tail(pack):
    return pack + FOOTER_MAGIC + struct.pack('<I', len(pack))


def make(pack, source_size=SOURCE_SIZE, source_crc=SOURCE_CRC):
    """The pack appended to the cartridge, as a BPS patch."""
    tail = cartridge_tail(pack)
    target_size = source_size + len(tail)
    # CRC32 continues over appended bytes, and the cartridge's own bytes are
    # untouched, so the target's CRC follows from the source's.
    target_crc = zlib.crc32(tail, source_crc)
    out = bytearray(b'BPS1')
    out += varint(source_size) + varint(target_size) + varint(0)
    out += varint(((source_size - 1) << 2) | 0)          # SourceRead: the ROM
    out += varint(((len(tail) - 1) << 2) | 1) + tail     # TargetRead: the pack
    out += struct.pack('<II', source_crc, target_crc)
    out += struct.pack('<I', zlib.crc32(bytes(out)))
    return bytes(out)


def decode(source, patch):
    """Apply a patch without its CRC checks, to check the action stream."""
    assert patch[:4] == b'BPS1'
    p = 4
    src_size, p = read_varint(patch, p)
    dst_size, p = read_varint(patch, p)
    meta, p = read_varint(patch, p)
    p += meta
    assert src_size == len(source), (src_size, len(source))
    out = bytearray(dst_size)
    o = s_rel = t_rel = 0
    end = len(patch) - 12
    while p < end:
        cmd, p = read_varint(patch, p)
        length, mode = (cmd >> 2) + 1, cmd & 3
        if mode == 0:
            out[o:o + length] = source[o:o + length]
        elif mode == 1:
            out[o:o + length] = patch[p:p + length]
            p += length
        else:
            delta, p = read_varint(patch, p)
            delta = -(delta >> 1) if delta & 1 else delta >> 1
            if mode == 2:
                s_rel += delta
                assert 0 <= s_rel and s_rel + length <= len(source)
                out[o:o + length] = source[s_rel:s_rel + length]
                s_rel += length
            else:
                t_rel += delta
                assert 0 <= t_rel and t_rel + length <= dst_size
                out[o:o + length] = out[t_rel:t_rel + length]
                t_rel += length
        o += length
    assert o == dst_size, (o, dst_size)
    return bytes(out)


def check_append_crc():
    """The target CRC is only right if CRC32 really continues over the tail."""
    import random
    rng = random.Random(1)
    for _ in range(8):
        head = bytes(rng.randrange(256) for _ in range(4096))
        tail = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 4096)))
        assert zlib.crc32(head + tail) == zlib.crc32(tail, zlib.crc32(head))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    rom_path = None
    for i, a in enumerate(sys.argv[1:]):
        if a == '--rom':
            rom_path = Path(sys.argv[i + 2])
    if len(args) != 2:
        sys.exit(__doc__)
    pack = Path(args[0]).read_bytes()
    if pack[:4] != b'ZCNB':
        sys.exit('%s is not a language pack' % args[0])
    dialogue = struct.unpack('<I', pack[4:8])[0]
    font = struct.unpack('<I', pack[8 + dialogue:12 + dialogue])[0]
    if 8 + dialogue + 4 + font != len(pack):
        sys.exit('%s: block sizes do not add up' % args[0])

    check_append_crc()
    patch = make(pack)
    tail = cartridge_tail(pack)

    # The action stream has to replay into exactly the cartridge we mean.
    rebuilt = decode(bytes(SOURCE_SIZE), patch)
    assert rebuilt[:SOURCE_SIZE] == bytes(SOURCE_SIZE)
    assert rebuilt[SOURCE_SIZE:] == tail
    assert struct.unpack('<I', patch[-12:-8])[0] == SOURCE_CRC
    assert zlib.crc32(patch[:-4]) == struct.unpack('<I', patch[-4:])[0]

    if rom_path:
        rom = rom_path.read_bytes()
        if len(rom) != SOURCE_SIZE or zlib.crc32(rom) != SOURCE_CRC:
            sys.exit('%s is not the headerless USA cartridge (size %d, CRC32 %08x)'
                     % (rom_path, len(rom), zlib.crc32(rom)))
        full = decode(rom, patch)
        assert full == rom + tail
        assert zlib.crc32(full) == struct.unpack('<I', patch[-8:-4])[0]

    Path(args[1]).write_bytes(patch)
    print('%s: %d bytes (pack %d, cartridge %d, source CRC32 %08x)'
          % (args[1], len(patch), len(pack), SOURCE_SIZE + len(tail), SOURCE_CRC))


if __name__ == '__main__':
    main()
