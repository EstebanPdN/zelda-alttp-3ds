#!/usr/bin/env python3
"""Build and check language packs, and the packs the checker must refuse."""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[3]
check = root / 'tools/languages/check_language_pack.py'
sys.path.insert(0, str(root / 'tools/languages'))

import check_language_pack as pack  # noqa: E402


def pack_memblk(blocks):
    """Packs blocks the way PackBlocks() in platform_3ds.c does."""
    body = b''.join(blocks)
    count = len(blocks)
    data_size = len(body)
    wide = data_size >= 65536 or count > 8192
    fmt = '<I' if wide else '<H'
    offsets = b''.join(struct.pack(fmt, sum(len(b) for b in blocks[:i + 1]))
                       for i in range(count - 1))
    trailer = struct.pack('<H', (count - 1) + (8192 if wide else 0))
    # The offsets come first, then the block data, then the count.
    return offsets + body + trailer


def build_pack(glyph_count=8, message_count=397, width=16, letter_width=6):
    # The real pack's dictionary is a single empty block and its messages carry
    # no terminator, so the tests build it the same way.
    dictionary = pack_memblk([b''])
    message_blocks = [b'' if i == 0 else b'\x6f' + bytes([i % 256])
                      for i in range(message_count)]
    messages = pack_memblk(message_blocks)
    glyphs = b'\x00' * (pack.LETTER_TILES * pack.LETTER_TILE_BYTES)
    glyphs += b'\xff' * (pack.GLYPH_BYTES * glyph_count)
    widths = bytes([letter_width] * pack.WIDTH_PREFIX) + bytes([width] * glyph_count)
    dialogue = pack_memblk([dictionary, messages])
    font = pack_memblk([glyphs, widths])
    return (pack.MAGIC + struct.pack('<I', len(dialogue)) + dialogue +
            struct.pack('<I', len(font)) + font)


def expect_ok(data, **expected):
    summary = pack.check_pack(data)
    for key, value in expected.items():
        assert summary[key] == value, (key, summary[key], value)


def expect_error(data, needle):
    try:
        pack.check_pack(data)
    except pack.PackError as error:
        assert needle in str(error), (str(error), needle)
        return
    raise AssertionError(f'accepted a pack it should refuse ({needle})')


def main():
    expect_ok(build_pack(), glyphs=8, messages=397)
    expect_ok(build_pack(glyph_count=1400, width=8), glyphs=1400)

    expect_error(b'XXXX' + build_pack()[4:], 'ZCNB')
    expect_error(build_pack()[:-1], 'font block does not fit')
    expect_error(build_pack(width=17), 'expected 1..16')
    expect_error(build_pack(width=0), 'expected 1..16')
    expect_error(build_pack(letter_width=9), 'at most 8')

    # Both blocks must be memblks with the expected number of parts.
    broken = build_pack()
    dialogue_size = struct.unpack_from('<I', broken, 4)[0]
    one_part = pack_memblk([b'\x00' * 16])
    rebuilt = (pack.MAGIC + struct.pack('<I', len(one_part)) + one_part +
               broken[8 + dialogue_size:])
    expect_error(rebuilt, 'dictionary and the messages')

    # The command-line entrance reports the same verdict.
    with tempfile.TemporaryDirectory(prefix='alttp-pack-') as tmp:
        good = Path(tmp) / 'good.bin'
        good.write_bytes(build_pack(glyph_count=12, message_count=310))
        result = subprocess.run([sys.executable, str(check), str(good)],
                                capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        assert '310 messages, 12 glyphs' in result.stdout, result.stdout
        bad = Path(tmp) / 'bad.bin'
        bad.write_bytes(build_pack(width=99))
        result = subprocess.run([sys.executable, str(check), str(bad)],
                                capture_output=True, text=True)
        assert result.returncode == 1, result
    print('language pack test ok')


if __name__ == '__main__':
    main()
