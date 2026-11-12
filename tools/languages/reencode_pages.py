#!/usr/bin/env python3
"""Move a Chinese language pack from its old glyph escapes to page bytes.

The v3.2 pack names a 16x16 glyph with two bytes, `0x6f..0x73` and an index.
Those bytes are also the letters o, p, q, r and s, so a pack could not print
them, and upstream v4.1 gave `0xf0`/`0xf1` to the Japanese cartridge's font
pages. The pack now uses the same shape: a page byte `0xf0..0xf4` and an index,
which names 5 * 256 = 1280 glyphs (the translation uses 1163).

Usage:
  reencode_pages.py PACK [OUT]     migrate, then check the round trip
  reencode_pages.py --check PACK   fail unless PACK is already migrated
"""
import struct
import sys


class PackError(Exception):
    pass


def unpack_memblk(blob, name):
    if len(blob) < 2:
        raise PackError(f'{name}: block too small')
    end = len(blob) - 2
    trailer = struct.unpack_from('<H', blob, end)[0]
    if trailer >= 8192:
        width, count = 4, trailer - 8192 + 1
    else:
        width, count = 2, trailer + 1
    base = (count - 1) * width
    if count <= 0 or base > end:
        raise PackError(f'{name}: bad block count')
    fmt = '<I' if width == 4 else '<H'

    def cumulative(i):
        return struct.unpack_from(fmt, blob, i * width)[0]

    blocks = []
    for i in range(count):
        left = base if i == 0 else base + cumulative(i - 1)
        right = end if i == count - 1 else base + cumulative(i)
        if left > right or right > end:
            raise PackError(f'{name}: block {i} does not fit')
        blocks.append(blob[left:right])
    return blocks


def pack_memblk(blocks):
    data_size = sum(len(b) for b in blocks)
    wide = data_size >= 65536 or len(blocks) > 8192
    width = 4 if wide else 2
    header = (len(blocks) - 1) * width
    out = bytearray(header + data_size + 2)
    fmt = '<I' if wide else '<H'
    cumulative = 0
    for i, block in enumerate(blocks):
        if i:
            struct.pack_into(fmt, out, (i - 1) * width, cumulative)
        out[header + cumulative:header + cumulative + len(block)] = block
        cumulative += len(block)
    struct.pack_into('<H', out, header + data_size,
                     (len(blocks) - 1) + (8192 if wide else 0))
    return bytes(out)


def parse_pack(data):
    if len(data) < 12 or data[:4] != b'ZCNB':
        raise PackError('not a language pack')
    dialogue_size = struct.unpack_from('<I', data, 4)[0]
    dialogue_at = 8
    if dialogue_size == 0 or dialogue_at + dialogue_size + 4 > len(data):
        raise PackError('dialogue block does not fit the file')
    font_size = struct.unpack_from('<I', data, dialogue_at + dialogue_size)[0]
    font_at = dialogue_at + dialogue_size + 4
    if font_at + font_size != len(data):
        raise PackError('font block does not fit the file')
    return data[dialogue_at:dialogue_at + dialogue_size], data[font_at:]


PAGE_FIRST = 0xf0
PAGE_LAST = 0xf4
ESCAPE_FIRST = 0x6f
ESCAPE_LAST = 0x73


# The EU encoding, exactly as engine/src/messaging.c decodes it: bytes below
# 0x7f are letters, 0x7f..0x86 are commands without arguments, and 0x87 and up
# take a selector byte and one argument. Only a *letter* position can carry a
# glyph escape. A command argument that happens to hold 0x6f..0x73 is data: the
# first version of this tool rewrote those too, which shifted the rest of every
# message that set a colour or a position to such a value.
EU_FIRST_COMMAND = 0x7f
EU_REST = 0x87


def tokens(message, name, index, first, last):
    """Split a message into ('escape', glyph) at letter positions and raw bytes.

    A three-byte command comes back as one 'raw' token so the caller can copy it
    untouched.
    """
    i = 0
    while i < len(message):
        byte = message[i]
        # The engine tests the glyph escape before the letter/command split, and
        # a page byte (0xf0..0xf4) is above every command, so the order matters.
        if first <= byte <= last:
            if i + 1 >= len(message):
                raise PackError(f'{name}: message {index} ends with a glyph prefix')
            yield ('escape', (byte - first) * 256 + message[i + 1])
            i += 2
        elif byte < EU_FIRST_COMMAND:
            yield ('byte', byte)
            i += 1
        elif byte < EU_REST:
            yield ('byte', byte)
            i += 1
        else:
            # 0x87 and up take a selector byte that doubles as the argument, so the
            # command is two bytes: the loader does `*dst++ = *src` with src on the
            # selector and then `src += 1` past it. Treating it as three bytes ate
            # the first glyph of the messages that put text right after one.
            chunk = message[i:i + 2]
            yield ('raw', chunk)
            i += len(chunk)


def decode_escapes(message, name, index):
    return list(tokens(message, name, index, ESCAPE_FIRST, ESCAPE_LAST))


def decode_pages(message):
    return list(tokens(message, name='pack', index=0,
                       first=PAGE_FIRST, last=PAGE_LAST))


def reencode(message):
    out = bytearray()
    for kind, value in tokens(message, 'pack', 0, ESCAPE_FIRST, ESCAPE_LAST):
        if kind == 'escape':
            out += bytes((PAGE_FIRST + (value >> 8), value & 0xff))
        elif kind == 'raw':
            out += value
        else:
            out.append(value)
    return bytes(out)


def count_escapes(messages, first, last):
    total = 0
    for message in messages:
        i = 0
        while i < len(message):
            byte = message[i]
            if first <= byte <= last:
                if i + 1 >= len(message):
                    break
                total += 1
                i += 2
            elif byte < EU_FIRST_COMMAND:
                i += 1
            elif byte < EU_REST:
                i += 1
            else:
                i += 3
    return total


def is_migrated(messages):
    """Whichever scheme names more glyphs is the one the pack is written in: a
    v3.2 pack has ~382 old escapes and a page-coded one has ~382 pages."""
    old = count_escapes(messages, ESCAPE_FIRST, ESCAPE_LAST)
    page = count_escapes(messages, PAGE_FIRST, PAGE_LAST)
    return page > old


def migrate(data):
    dialogue, font = parse_pack(data)
    parts = unpack_memblk(dialogue, 'dialogue')
    if len(parts) != 2:
        raise PackError(f'dialogue block holds {len(parts)} parts, expected 2')
    dictionary, messages = parts
    messages = unpack_memblk(messages, 'messages')
    if is_migrated(messages):
        return 0, len(messages), data
    new_messages, changed = [], 0
    for index, message in enumerate(messages):
        new = reencode(message)
        if len(new) != len(message):
            raise PackError(f'message {index} changed size')
        if decode_escapes(message, 'pack', index) != decode_pages(new):
            raise PackError(f'message {index} does not round trip')
        if new != message:
            changed += 1
        new_messages.append(new)
    block = pack_memblk([dictionary, pack_memblk(new_messages)])
    return (changed, len(messages),
            b'ZCNB' + struct.pack('<I', len(block)) + block +
            struct.pack('<I', len(font)) + font)


def main(argv):
    check = '--check' in argv
    args = [a for a in argv[1:] if not a.startswith('--')]
    if not args:
        raise SystemExit(__doc__)
    source = args[0]
    data = open(source, 'rb').read()
    if check:
        dialogue, _font = parse_pack(data)
        messages = unpack_memblk(
            unpack_memblk(dialogue, 'dialogue')[1], 'messages')
        if not is_migrated(messages):
            raise SystemExit(f'{source}: still uses the 0x6f..0x73 escapes '
                             f'({count_escapes(messages, ESCAPE_FIRST, ESCAPE_LAST)} '
                             f'glyphs to migrate)')
        print(f'{source}: already uses page bytes')
        return
    changed, total, migrated = migrate(data)
    if migrated == data:
        print(f'{source}: already uses page bytes')
        if not check:
            return
        return
    if check:
        raise SystemExit(f'{source}: still uses the 0x6f..0x73 escapes '
                         f'({changed} messages to migrate)')
    target = args[1] if len(args) > 1 else source
    open(target, 'wb').write(migrated)
    print(f'{target}: {changed} of {total} messages moved to page bytes')


if __name__ == '__main__':
    main(sys.argv)
