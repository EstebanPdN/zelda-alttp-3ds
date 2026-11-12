#!/usr/bin/env python3
"""Check the Chinese patch and the reader that takes the pack back out of it.

make_zh_bps.py writes a BPS whose cartridge is the USA ROM followed by the
language pack; platform_3ds.c's CartridgePack() finds the pack again. Neither
side is much use if the other drifts, so this runs the real generator on a
synthetic pack, decodes the patch with the CRC checks ApplyBps makes, and
compiles the real reader against the result.
"""
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

root = Path(__file__).resolve().parents[3]
generator = root / 'tools/languages/make_zh_bps.py'
source = (root / 'platform/3ds/source/platform_3ds.c').read_text()

# The headerless USA cartridge the shipped patch is made against.
USA_SIZE = 1048576
USA_CRC = 0x777AAC2F


def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


def make_pack(dialogue, font):
    return (b'ZCNB' + struct.pack('<I', len(dialogue)) + dialogue +
            struct.pack('<I', len(font)) + font)


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


def apply_checked(source_bytes, patch):
    """ApplyBps: the source CRC, the patch CRC and the target CRC all agree."""
    assert patch[:4] == b'BPS1'
    assert zlib.crc32(source_bytes) == struct.unpack('<I', patch[-12:-8])[0]
    assert zlib.crc32(patch[:-4]) == struct.unpack('<I', patch[-4:])[0]
    p = 4
    src_size, p = read_varint(patch, p)
    dst_size, p = read_varint(patch, p)
    meta, p = read_varint(patch, p)
    p += meta
    assert src_size == len(source_bytes)
    out = bytearray(dst_size)
    o = s_rel = t_rel = 0
    end = len(patch) - 12
    while p < end:
        cmd, p = read_varint(patch, p)
        length, mode = (cmd >> 2) + 1, cmd & 3
        if mode == 0:
            out[o:o + length] = source_bytes[o:o + length]
        elif mode == 1:
            out[o:o + length] = patch[p:p + length]
            p += length
        else:
            delta, p = read_varint(patch, p)
            delta = -(delta >> 1) if delta & 1 else delta >> 1
            if mode == 2:
                s_rel += delta
                assert 0 <= s_rel and s_rel + length <= len(source_bytes)
                out[o:o + length] = source_bytes[s_rel:s_rel + length]
                s_rel += length
            else:
                t_rel += delta
                assert 0 <= t_rel and t_rel + length <= dst_size
                out[o:o + length] = out[t_rel:t_rel + length]
                t_rel += length
        o += length
    assert o == dst_size
    assert zlib.crc32(out) == struct.unpack('<I', patch[-8:-4])[0]
    return bytes(out)


reader = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t uint8;
typedef uint32_t uint32;
'''
for name in ('static uint32 ReadU32LE(const uint8 *data) {',
             'static const uint8 *CartridgePack('):
    reader += function(name)
reader += r'''
static size_t write_pack(uint8 *cart, size_t at) {
  /* 'ZCNB', u32 dialogue size, dialogue, u32 font size, font, 'ZCNP', size. */
  uint8 *p = cart + at;
  memcpy(p, "ZCNB", 4);
  p[4] = 4; p[5] = p[6] = p[7] = 0;
  memcpy(p + 8, "text", 4);
  p[12] = 8; p[13] = p[14] = p[15] = 0;
  memcpy(p + 16, "abcdefgh", 8);
  size_t pack = 24;
  memcpy(p + pack, "ZCNP", 4);
  uint32 value = (uint32)pack;
  p[pack + 4] = (uint8)value;
  p[pack + 5] = (uint8)(value >> 8);
  p[pack + 6] = (uint8)(value >> 16);
  p[pack + 7] = (uint8)(value >> 24);
  return pack + 8;
}

int main(void) {
  static uint8 cart[4096];
  memset(cart, 0x5a, sizeof(cart));
  size_t tail = write_pack(cart, 1000);
  size_t pack_size = 0;
  const uint8 *pack = CartridgePack(cart, 1000 + tail, &pack_size);
  assert(pack && pack_size == 24 && pack == cart + 1000);
  assert(!memcmp(pack, "ZCNB", 4));

  /* A cartridge without the footer is just a cartridge. */
  assert(!CartridgePack(cart, 1000, &pack_size));
  /* The footer has to name a pack that fits. */
  cart[1000 + tail - 4] = 0xff;
  assert(!CartridgePack(cart, 1000 + tail, &pack_size));
  /* ... and the bytes it names have to be a pack. */
  write_pack(cart, 1000);
  cart[1000] = 'X';
  assert(!CartridgePack(cart, 1000 + tail, &pack_size));
  puts("PASS cartridge pack reader");
  return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='alttp-zh-bps-') as directory:
    work = Path(directory)

    # The generator refuses anything that is not a pack.
    (work / 'bad.bin').write_bytes(b'ZCNB\x00\x00')
    refused = subprocess.run([sys.executable, str(generator), str(work / 'bad.bin'),
                              str(work / 'bad.bps')], capture_output=True, text=True)
    assert refused.returncode != 0, 'a malformed pack must be refused'

    pack_bytes = make_pack(b'dialogue', bytes(range(64)))
    (work / 'zh.bin').write_bytes(pack_bytes)
    subprocess.run([sys.executable, str(generator), str(work / 'zh.bin'),
                    str(work / 'zh.bps')], check=True)
    patch = (work / 'zh.bps').read_bytes()
    tail = pack_bytes + b'ZCNP' + struct.pack('<I', len(pack_bytes))

    # Its header names the USA cartridge, and its target CRC follows from that
    # CRC plus the appended bytes, so it is right without ever seeing the ROM.
    p = 4
    _, p = read_varint(patch, p)
    target_size, p = read_varint(patch, p)
    assert struct.unpack('<I', patch[-12:-8])[0] == USA_CRC
    assert struct.unpack('<I', patch[-8:-4])[0] == zlib.crc32(tail, USA_CRC)
    assert target_size == USA_SIZE + len(tail)

    # One SourceRead over the cartridge, one TargetRead of the pack: the patch
    # carries no ROM data and is barely larger than the pack.
    meta, p = read_varint(patch, p)
    p += meta
    first, p = read_varint(patch, p)
    assert first & 3 == 0 and (first >> 2) + 1 == USA_SIZE, 'expected SourceRead'
    second, p = read_varint(patch, p)
    assert second & 3 == 1, 'expected TargetRead of the pack'
    assert (second >> 2) + 1 == len(tail)
    assert len(patch) < len(pack_bytes) + 64, 'the ROM must not be in the patch'

    # The same patch, re-based on a source we do have, decodes with ApplyBps's
    # checks to exactly the cartridge the generator promised.
    zeros = bytes(USA_SIZE)
    rebased = bytearray(patch)
    rebased[-12:-8] = struct.pack('<I', zlib.crc32(zeros))
    rebased[-8:-4] = struct.pack('<I', zlib.crc32(tail, zlib.crc32(zeros)))
    rebased[-4:] = struct.pack('<I', zlib.crc32(bytes(rebased[:-4])))
    assert apply_checked(zeros, bytes(rebased)) == zeros + tail

    (work / 'reader.c').write_text(reader)
    exe = work / 'reader'
    subprocess.run(['cc', '-O1', '-fsanitize=address,undefined',
                    str(work / 'reader.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

print('PASS Chinese patch: generator, CRCs, appended pack and cartridge reader')
