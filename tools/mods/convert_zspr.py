#!/usr/bin/env python3
"""Wrap an author's ZSPR in the port's Mods format, without base game assets.

The input is supplied locally. This tool does not download or license artwork.
Requires a port build with kModTable_LinkPalette support.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct

ROOT = Path(__file__).resolve().parents[2]


def read_zspr(data):
    if len(data) < 29 or data[:5] != b'ZSPR\x01':
        raise ValueError('Expected a version 1 ZSPR file')
    check, inverse = struct.unpack_from('<HH', data, 5)
    if check ^ inverse != 0xffff or sum(data) & 0xffff != check:
        raise ValueError('ZSPR checksum mismatch')
    pixels, pixels_size, palette, palette_size, sprite_type = struct.unpack_from('<IHIHH', data, 9)
    if pixels_size != 0x7000 or palette_size != 124 or sprite_type != 1:
        raise ValueError('Expected a Link sprite with 0x7000 graphics and 124 palette bytes')
    if not (29 <= pixels < pixels + pixels_size <= palette < palette + palette_size <= len(data)):
        raise ValueError('Invalid or overlapping ZSPR sections')
    cursor = 29
    def string():
        nonlocal cursor
        start = cursor
        while cursor + 2 <= pixels:
            word = data[cursor:cursor+2]
            cursor += 2
            if word == b'\0\0':
                return data[start:cursor-2].decode('utf-16-le')
        raise ValueError('Unterminated ZSPR credit')
    name, author = string(), string()
    if any(c in name + author for c in '\r\n\0'):
        raise ValueError('Invalid multiline credit')
    return name, author, data[pixels:pixels+pixels_size], data[palette:palette+palette_size]


def preview_bmp(graphics, colors):
    # The same forward-facing 16x16 head used by SS_RenderLinkFace.
    pixels = bytearray()
    for y in range(15, -1, -1):
        for x in range(16):
            tile = (x//8) + (y//8)*16
            row, shift = y%8, 7-x%8
            t = graphics[tile*32:(tile+1)*32]
            index = sum(((t[offset] >> shift) & 1) << bit
                        for bit, offset in enumerate([row*2,row*2+1,16+row*2,17+row*2]))
            if index:
                word = struct.unpack_from('<H', colors, (index-1)*2)[0]
                rgb = [(word >> shift) & 31 for shift in [0,5,10]]
                r,g,b = [(v << 3) | (v >> 2) for v in rgb]
            else:
                r,g,b = 255,0,255
            pixels += bytes([b,g,r])
    header = b'BM' + struct.pack('<IHHI',54+len(pixels),0,0,54)
    header += struct.pack('<IiiHHIIiiII',40,16,16,1,24,0,len(pixels),0,0,0,0)
    return header + pixels


def convert(source, output, url=''):
    data = source.read_bytes()
    name, author, graphics, colors = read_zspr(data)
    header = (ROOT/'engine/src/assets.h').read_text()
    match = re.search(r'^#define kAssets_Sig ([0-9, ]+)', header, re.M)
    signature = bytes(int(x.strip()) for x in match[1].split(','))
    assert len(signature) == 48
    package = b'Z3MOD\r\n\x1a' + struct.pack('<II', 2, 2) + signature
    package += struct.pack('<IIII', 57, 0, 0, len(graphics))
    package += struct.pack('<IIII', 3, 0, 3, len(colors))
    package += graphics + colors
    output.mkdir(parents=True, exist_ok=True)
    (output/'skin.z3mod').write_bytes(package)
    (output/'preview.bmp').write_bytes(preview_bmp(graphics, colors))
    lines = [f'Name = {name}', 'Category = SKIN', f'Info = By {author}',
             'Info2 = Character graphics, armor and glove colors.',
             'Info3 = Requires the v4.0 skin trial build.']
    lines += [f'{language} = skin.z3mod' for language in ['us', 'en', 'es', 'pt', 'it', 'fr', 'de', 'ru', 'ja', 'fr-c']]
    (output/'mod.ini').write_text('\n'.join(lines) + '\n')
    provenance = dict(name=name, author=author, source=source.name, url=url,
                      source_sha256=hashlib.sha256(data).hexdigest(),
                      package_sha256=hashlib.sha256(package).hexdigest(),
                      note='Local test conversion; no base ROM or extracted base assets included. '
                           'Credit is preserved; redistribution rights are not inferred.')
    (output/'provenance.json').write_text(json.dumps(provenance, indent=2, ensure_ascii=False) + '\n')
    return provenance


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('output', type=Path, help='one mod folder, e.g. mods/conker')
    p.add_argument('--source-url', default='')
    a = p.parse_args()
    try:
        result = convert(a.source, a.output, a.source_url)
    except (ValueError, OSError) as exc:
        p.error(str(exc))
    print(f"{result['name']} by {result['author']}: {a.output}")


if __name__ == '__main__':
    main()
