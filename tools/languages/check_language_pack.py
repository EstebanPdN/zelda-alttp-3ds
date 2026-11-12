#!/usr/bin/env python3
"""Check a language pack (`romfs:/languages/<code>.bin`) before it ships.

A pack carries the same two blocks the translated-cartridge path extracts from
a patched ROM, for languages whose translation is text and a font rather than a
ROM hack:

    'ZCNB'
    u32 dialogue length, dialogue block
    u32 font length,     font block

Both blocks are "memblks": N blocks are concatenated, followed by an offset
table (u16 entries, or u32 when there are more than 8192) and a final u16/u32
count - 1. The dialogue block holds the dictionary and the messages; the font
block holds the glyphs and the widths.

Usage:
    check_language_pack.py romfs/languages/zh.bin
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

MAGIC = b'ZCNB'
LETTER_TILES = 256
LETTER_TILE_BYTES = 16
GLYPH_BYTES = 64          # 16x16, one bit per pixel: TL, TR, BL, BR
WIDTH_PREFIX = 111        # widths of the letters the glyph indices skip
MAX_GLYPH_WIDTH = 16
MAX_LETTER_WIDTH = 8


class PackError(Exception):
    pass


def unpack_memblk(data: bytes, what: str) -> list[bytes]:
    """Splits a memblk into its blocks (see UnpackBlocks in platform_3ds.c)."""
    if len(data) < 2:
        raise PackError(f'{what}: block list is too short')
    end = len(data) - 2
    trailer = int.from_bytes(data[end:end + 2], 'little')
    if trailer >= 8192:
        width = 4
        count = trailer - 8192 + 1
    else:
        width = 2
        count = trailer + 1
    if count <= 0:
        raise PackError(f'{what}: block count is not positive')
    base = (count - 1) * width
    if base > end:
        raise PackError(f'{what}: {count} offsets do not fit in {len(data)} bytes')
    blocks = []
    previous = 0
    for i in range(count):
        if i == count - 1:
            following = end - base
        else:
            following = int.from_bytes(data[i * width:(i + 1) * width], 'little')
        if following < previous or base + following > end:
            raise PackError(f'{what}: block {i} is out of order or out of range')
        blocks.append(data[base + previous:base + following])
        previous = following
    return blocks


def parse_pack(data: bytes) -> tuple[list[bytes], list[bytes]]:
    if len(data) < 12 or data[:4] != MAGIC:
        raise PackError('not a language pack: expected the ZCNB magic')
    dialogue_size = struct.unpack_from('<I', data, 4)[0]
    dialogue_at = 8
    if dialogue_size == 0 or dialogue_at + dialogue_size + 4 > len(data):
        raise PackError('dialogue block does not fit the file')
    font_size = struct.unpack_from('<I', data, dialogue_at + dialogue_size)[0]
    font_at = dialogue_at + dialogue_size + 4
    if font_size == 0 or font_at + font_size != len(data):
        raise PackError('font block does not fit the file')
    return (unpack_memblk(data[dialogue_at:dialogue_at + dialogue_size], 'dialogue'),
            unpack_memblk(data[font_at:font_at + font_size], 'font'))


def check_pack(data: bytes) -> dict:
    dialogue, font = parse_pack(data)
    if len(dialogue) != 2:
        raise PackError(f'dialogue block holds {len(dialogue)} parts, expected '
                        'the dictionary and the messages')
    dictionary, messages = dialogue
    # Both are memblks themselves: the dictionary holds the words, the messages
    # one block per message (the engine looks a message up by its index).
    # The Chinese pack carries no dictionary words at all: its block is empty.
    words = unpack_memblk(dictionary, 'dictionary') if dictionary else []
    entries = unpack_memblk(messages, 'messages')
    if not entries:
        raise PackError('the messages are empty')
    # Messages are stored without their 0x7f terminator, and the first one may
    # be empty (the game numbers them, it does not read them in order).

    if len(font) != 2:
        raise PackError(f'font block holds {len(font)} parts, expected the '
                        'glyphs and the widths')
    glyphs, widths = font
    if len(glyphs) < GLYPH_BYTES or (len(glyphs) - LETTER_TILES * LETTER_TILE_BYTES) % GLYPH_BYTES:
        raise PackError(f'glyph data is {len(glyphs)} bytes: expected '
                        f'{LETTER_TILES * LETTER_TILE_BYTES} letter bytes plus a '
                        f'multiple of {GLYPH_BYTES}')
    glyph_count = (len(glyphs) - LETTER_TILES * LETTER_TILE_BYTES) // GLYPH_BYTES
    if glyph_count == 0:
        raise PackError('the pack carries no 16x16 glyphs')
    if len(widths) != WIDTH_PREFIX + glyph_count:
        raise PackError(f'{len(widths)} width bytes for {glyph_count} glyphs: '
                        f'expected {WIDTH_PREFIX} + one per glyph')
    for i, width in enumerate(widths[:WIDTH_PREFIX]):
        if width > MAX_LETTER_WIDTH:
            raise PackError(f'letter width {i} is {width}, at most '
                            f'{MAX_LETTER_WIDTH}')
    for i, width in enumerate(widths[WIDTH_PREFIX:]):
        if not 1 <= width <= MAX_GLYPH_WIDTH:
            raise PackError(f'glyph {i} is {width} pixels wide, expected 1..'
                            f'{MAX_GLYPH_WIDTH}')
    return {
        'glyphs': glyph_count,
        'messages': len(entries),
        'dictionary': len(words),
    }


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    path = Path(argv[1])
    try:
        summary = check_pack(path.read_bytes())
    except (OSError, PackError) as error:
        print(f'{path}: {error}', file=sys.stderr)
        return 1
    print(f'{path}: {summary["messages"]} messages, {summary["glyphs"]} glyphs, '
          f'{summary["dictionary"]}-word dictionary')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
